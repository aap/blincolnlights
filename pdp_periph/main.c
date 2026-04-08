#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <ctype.h>

#include <limits.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/types.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>     
#include <fcntl.h>     
#include <poll.h>     
#include <netdb.h>      

#include <pthread.h>      

#include <SDL.h>
#include <SDL_ttf.h>
#include "glad/glad.h"

#include <common.h>

#include "args.h"

typedef uint64_t uint64;
typedef uint32_t uint32;
typedef uint16_t uint16;
typedef uint8_t uint8;

#define nil NULL
#define void_offsetof (void*)(uintptr_t)offsetof


int resolution = 1024;
#define WIDTH resolution
#define HEIGHT resolution
#define BORDER 2
#define BWIDTH (WIDTH+2*BORDER)
#define BHEIGHT (HEIGHT+2*BORDER)

char *argv0;

SDL_Window *window;
int fullscreen;
int realW, realH;	// non-fullscreen, what a pain to keep track of
int winW, winH;
int dbgflag;

const char *host = "localhost";
int cliport = 1650;
int emuport = 1640;
int typport = 1641;
int ptrport = 1642;
int ptpport = 1643;
int utport = 1660;

int clifd[2];

char fontpath[PATH_MAX];
TTF_Font *font;

int audio;
int muldiv;

typedef struct {
	int r, g, b, a;
} Color;

typedef struct {
	float x, y;
	float w, h;
	int iscircle;
} Region;

typedef struct {
	float x, y;
	float r;
} Circle;

typedef struct Component Component;
struct Component {
	Region r;
	Component *next;
	void (*draw)(Component *c);
	int (*processUpdate)(Component *c);
};

typedef struct {
	Component *comp;
	int w, h;
	int fullscreen;
	int fontsize;
	Color bgcol;
} Layout;
Layout *layout;

typedef struct {
	int minx, maxx, miny, maxy, advance;
	int w, h;
	GLuint tex;
} Glyph;
Glyph glyphs[128];
int fontsize;

Component *selected;
Component *hover = nil;

int layoutmode;

GLuint vbo;

uint64 simtime, realtime;

char*
emucmd(const char *cmd)
{
	static char reply[1024];

	int emu = dial(host, emuport);
	if(emu < 0) {
		fprintf(stderr, "error: couldn't connct to %s:1040\n", host);
		return nil;
	}

	int n = write(emu, cmd, strlen(cmd));
	if(n <= 0) {
		fprintf(stderr, "error: couldn't sent cmd to emulator"
			" (%d bytes sent)\n", n);
		close(emu);
		return nil;
	}

	n = read(emu, reply, sizeof(reply)-1);
	if(n <= 0) {
		fprintf(stderr, "error: no reply from emulator\n");
		close(emu);
		return nil;
	}

	reply[n] = '\0';
	close(emu);
	return reply;
}

void
emuoption(const char *name, int *opt, int val)
{
	char *r, cmd[64];
	snprintf(cmd, sizeof(cmd)-1, "%s %s\n", name,
		val < 0 ? "?" : val ? "on" : "off");
	r = emucmd(cmd);
	*opt = strstr(r, " on\n") != nil;
	printf("%s: %d\n", name, *opt);
}

uint64 time_now;
uint64 time_prev;

float
getDeltaTime(void)
{
	time_prev = time_now;
	time_now = SDL_GetPerformanceCounter();
	return (float)(time_now-time_prev)/SDL_GetPerformanceFrequency();
}

void    
printlog(GLuint object)
{
	GLint log_length;
	char *log;

	if(glIsShader(object))
		glGetShaderiv(object, GL_INFO_LOG_LENGTH, &log_length);
	else if(glIsProgram(object))
		glGetProgramiv(object, GL_INFO_LOG_LENGTH, &log_length);
	else{
		fprintf(stderr, "printlog: Not a shader or a program\n");
		return;
	}

	log = (char*) malloc(log_length);
	if(glIsShader(object))
		glGetShaderInfoLog(object, log_length, NULL, log);
	else if(glIsProgram(object))
		glGetProgramInfoLog(object, log_length, NULL, log);
	fprintf(stderr, "%s", log);
	free(log);
}

GLint
compileshader(GLenum type, const char *src)
{
	GLint shader, success;

	shader = glCreateShader(type);
	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if(!success){
		fprintf(stderr, "Error in shader\n");
printf("%s\n", src);
		printlog(shader);
exit(1);
		return -1;
	}
	return shader;
}

GLint
linkprogram(GLint vs, GLint fs)
{
	GLint program, success;

	program = glCreateProgram();

	glBindAttribLocation(program, 0, "in_pos");
	glBindAttribLocation(program, 1, "in_uv");
	glBindAttribLocation(program, 2, "in_params1");
	glBindAttribLocation(program, 3, "in_params2");

	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);
	glGetProgramiv(program, GL_LINK_STATUS, &success);
	if(!success){
		fprintf(stderr, "glLinkProgram:");
		printlog(program);
exit(1);
		return -1;
	}

	glUseProgram(program);
	glUniform1i(glGetUniformLocation(program, "tex0"), 0);
	glUniform1i(glGetUniformLocation(program, "tex1"), 1);

	return program;
}

#ifdef GLES
#define glslheader "#version 100\nprecision highp float; precision highp int;\n" \
	"#define VSIN attribute\n" \
	"#define VSOUT varying\n" \
	"#define FSIN varying\n"
#define outcolor
#define output "gl_FragColor = color;\n"
#else
#define glslheader "#version 130\n" \
	"#define VSIN in\n" \
	"#define VSOUT out\n" \
	"#define FSIN in\n"
#define outcolor
#define output "gl_FragColor = color;\n"
#endif

const char *color_vs_src =
glslheader
"VSIN vec2 in_pos;\n"
"VSIN vec2 in_uv;\n"
"VSOUT vec2 v_uv;\n"
"void main()\n"
"{\n"
"	v_uv = in_uv;\n"
"	vec2 p = vec2(in_pos.x*2.0-1.0, -(in_pos.y*2.0-1.0));\n"
"	gl_Position = vec4(p.x, p.y, -0.5, 1.0);\n"
"}\n";

const char *color_fs_src =
glslheader
outcolor
"uniform vec4 u_color;\n"
"void main()\n"
"{\n"
"	vec4 color = u_color;\n"
output
"}\n";

const char *tex_fs_src = 
glslheader
outcolor
"FSIN vec2 v_uv;\n"
"uniform vec4 u_color;\n"
"uniform sampler2D tex0;\n"
"void main()\n"
"{\n"
"	vec2 uv = vec2(v_uv.x, 1.0-v_uv.y);\n"
"	vec4 color = u_color*texture2D(tex0, uv);\n"
output
"}\n";

const char *circle_fs_src = 
glslheader
outcolor
"uniform vec4 u_color;\n"
"FSIN vec2 v_uv;\n"
"void main()\n"
"{\n"
"	vec4 color = u_color;\n"
"	color.a = 1.0 - smoothstep(0.995, 0.999, length(v_uv));\n"
output
"}\n";

void
texDefaults(void)
{
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void
makeFBO(GLuint *fbo, GLuint *tex)
{
	glGenTextures(1, tex);
	glBindTexture(GL_TEXTURE_2D, *tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, BWIDTH, BHEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, nil);
	texDefaults();
	glGenFramebuffers(1, fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *tex, 0);
}


typedef struct Vertex Vertex;
struct Vertex {
	float x, y;
	float u, v;
};

void
clearState(void)
{
	glUseProgram(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void
setvbo(GLuint vbo)
{
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glDisableVertexAttribArray(2);
	glDisableVertexAttribArray(3);
	int stride = sizeof(Vertex);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, 0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, void_offsetof(Vertex, u));
}

int drawW, drawH;

void
glViewportFlipped(int x, int y, int w, int h)
{
	glViewport(x, winH-y-h, w, h);
}

void
resetViewport(void)
{
	drawW = winW;
	drawH = winH;
	glViewport(0, 0, drawW, drawH);
}

void
setRegion(Region *r)
{
	drawW = r->w;
	drawH = r->h;
	glViewportFlipped(r->x, r->y, r->w, r->h);
}

void
setSquareRegion(Region *r)
{
	int w = drawW = r->w;
	int h = drawH = r->h;
	if(w > h)
		glViewportFlipped(r->x+(w-h)/2, r->y, h, h);
	else
		glViewportFlipped(r->x, r->y+(h-w)/2, w, w);

}

Vertex screenquad[] = {
	{ -1.0f, -1.0f,		0.0f, 0.0f },
	{ 1.0f, -1.0f,		1.0f, 0.0f },
	{ 1.0f, 1.0f,		1.0f, 1.0f },

	{ -1.0f, -1.0f,		0.0f, 0.0f },
	{ 1.0f, 1.0f,		1.0f, 1.0f },
	{ -1.0f, 1.0f,		0.0f, 1.0f },
};

void
makeQuad(void)
{
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(screenquad), screenquad, GL_STATIC_DRAW);
}

Color col;
Color colA = { 0x6e, 0x8b, 0x8e, 255 };
Color colB = { 0xa8, 0xc6, 0xa8, 255 };
Color colC = { 0xd2, 0xa6, 0x79, 255 };
Color colD = { 0x4A, 0x4F, 0x55, 255 };

void
setColor(int r, int g, int b, int a)
{
	col.r = r;
	col.g = g;
	col.b = b;
	col.a = a;
}

GLint color_program, tex_program, circle_program;
GLuint immVbo;

void
drawRectangle_(float x1, float y1, float x2, float y2)
{
	Vertex quad[] = {
		{ x1, y1,		-1.0f, -1.0f },
		{ x2, y1,		1.0f, -1.0f },
		{ x2, y2,		1.0f, 1.0f },

		{ x1, y1,		-1.0f, -1.0f },
		{ x2, y2,		1.0f, 1.0f },
		{ x1, y2,		-1.0f, 1.0f },
	};
	glUseProgram(color_program);
	setvbo(immVbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_DYNAMIC_DRAW);
	glUniform4f(glGetUniformLocation(color_program, "u_color"),
		col.r/255.0f, col.g/255.0f, col.b/255.0f, col.a/255.0f);
	glDrawArrays(GL_TRIANGLES, 0, 6);
}

void
drawRectangle(float x1, float y1, float x2, float y2)
{
	x1 /= drawW; x2 /= drawW;
	y1 /= drawH; y2 /= drawH;

	drawRectangle_(x1, y1, x2, y2);
}

Circle
getCircle(Region *r)
{
	int xx = r->w < r->h ? r->w : r->h;
	xx /= 2;
	Circle c;
	c.r = 1.4142f*xx + 5;	// TODO: this shouldn't depend on resolution
	c.x = r->x+r->w/2;
	c.y = r->y+r->h/2;
	return c;
}

Region
getSquareRegion(Region *r)
{
	Region rr;
	rr.x = r->x;
	rr.y = r->y;
	rr.w = r->w;
	rr.h = r->h;
	if(rr.w > rr.h) {
		rr.x += (rr.w-rr.h)/2;
		rr.w = rr.h;
	} else {
		rr.y += (rr.h-rr.w)/2;
		rr.h = rr.w;
	}
	return rr;
}

void
drawCircle_(float x1, float y1, float x2, float y2)
{
	Vertex quad[] = {
		{ x1, y1,		-1.0f, -1.0f },
		{ x2, y1,		1.0f, -1.0f },
		{ x2, y2,		1.0f, 1.0f },

		{ x1, y1,		-1.0f, -1.0f },
		{ x2, y2,		1.0f, 1.0f },
		{ x1, y2,		-1.0f, 1.0f },
	};
	glUseProgram(circle_program);
	setvbo(immVbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_DYNAMIC_DRAW);
	glUniform4f(glGetUniformLocation(color_program, "u_color"),
		col.r/255.0f, col.g/255.0f, col.b/255.0f, col.a/255.0f);
	glDrawArrays(GL_TRIANGLES, 0, 6);
}

void
drawCircle(float x, float y, float r)
{
	x /= drawW;
	y /= drawH;
	float rx = r/drawW;
	float ry = r/drawH;

	drawCircle_(x-rx, y-ry, x+rx, y+ry);
}

void
drawOutlineCircle(Region *r, int hover, int select, Color c)
{
	Circle cl = getCircle(r);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	setColor(0xff, 0xfd, 0xd2, 255);
	if(hover) col = colB;
	if(select) setColor(190, 0, 0, 255);
	if(hover && select) setColor(220, 0, 0, 255);
	drawCircle(cl.x, cl.y, cl.r);
	col = c;
	int b = (hover || select) ? 5 : 3;
	drawCircle(cl.x, cl.y, cl.r-b);

	glDisable(GL_BLEND);
}

void
drawOutlineRect(Region *r, int hover, int select, Color c)
{
	setRegion(r);
	setColor(0xff, 0xfd, 0xd2, 255);
	if(hover) col = colB;
	if(select) setColor(190, 0, 0, 255);
	if(hover && select) setColor(220, 0, 0, 255);
	drawRectangle(0.0f, 0.0f, drawW, drawH);
	col = c;
	int b = (hover || select) ? 5 : 3;
	drawRectangle(b, b, drawW-b, drawH-b);
}

Component*
addComponent(Component *c, int x, int y, int w, int h, int iscircle)
{
	assert(c);
	c->r.x = x;
	c->r.y = y;
	c->r.w = w;
	c->r.h = h;
	c->r.iscircle = iscircle;
	c->next = layout->comp;
	layout->comp = c;
	return c;
}

// unity build style for now
#define UNITY_BUILD
#include "poll.c"
#include "display.c"
#include "papertape.c"
#include "typewriter.c"
#include "dectape.c"
#include "lua.c"
Typewriter *typ;

void
initGlyph(int c, Glyph *g)
{
	static SDL_Color white = {255, 255, 255, 255};

	SDL_Surface *surf = TTF_RenderGlyph_Blended(font, c, white);
	g->w = surf->w;
	g->h = surf->h;
	TTF_GlyphMetrics(font, c, &g->minx, &g->maxx, &g->miny, &g->maxy, &g->advance);
//printf("%d %p %c %d %d %d %d %d    ", c, surf, i, g->minx, g->maxx, g->miny, g->maxy, g->advance);

	if(g->tex == 0)
		glGenTextures(1, &g->tex);
	glBindTexture(GL_TEXTURE_2D, g->tex);
	texDefaults();

	SDL_Surface *srgb = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_RGBA32, 0);
//printf("%d %d\n", srgb->w, srgb->h);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
		srgb->w, srgb->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, srgb->pixels);

	SDL_FreeSurface(surf);
	SDL_FreeSurface(srgb);
}

void
initFont(void)
{
	for(int i = 1; i < 128; i++)
		initGlyph(i, &glyphs[i]);
	initGlyph(0x00B7, &glyphs[020]);	//	·	middle dot
	initGlyph(0x203E, &glyphs[021]);	//	‾	overline
	initGlyph(0x2192, &glyphs[022]);	//	→	right arrow
	initGlyph(0x2283, &glyphs[023]);	//	⊃	superset
	initGlyph(0x2228, &glyphs[024]);	//	∨	or
	initGlyph(0x2227, &glyphs[025]);	//	∧	and
	initGlyph(0x2191, &glyphs[026]);	//	↑	up arrow
	initGlyph(0x00D7, &glyphs[027]);	//	×	times
	glBindTexture(GL_TEXTURE_2D, 0);
}

void
initGL(void)
{
	glGenBuffers(1, &vbo);
	glGenBuffers(1, &immVbo);
	initDisplayGL();
	initReaderGL();
	initDecTapeGL();

	GLint vs = compileshader(GL_VERTEX_SHADER, color_vs_src);
	GLint fs = compileshader(GL_FRAGMENT_SHADER, color_fs_src);
	color_program = linkprogram(vs, fs);

	GLint tex_fs = compileshader(GL_FRAGMENT_SHADER, tex_fs_src);
	tex_program = linkprogram(vs, tex_fs);

	GLint color_vs = compileshader(GL_VERTEX_SHADER, color_vs_src);
	GLint circle_fs = compileshader(GL_FRAGMENT_SHADER, circle_fs_src);
	circle_program = linkprogram(color_vs, circle_fs);
}

void resize(int w, int h);

void
setFullscreen(int f)
{
	static uint32 screenmodes[2] = { 0, SDL_WINDOW_FULLSCREEN_DESKTOP };
	if(!fullscreen)
		SDL_GetWindowSize(window, &realW, &realH);
	fullscreen = f;
	SDL_SetWindowFullscreen(window, screenmodes[fullscreen]);
}

int shift, ctrl;

void
mountTape(const char *filename)
{
	static char cmd[1024];
	snprintf(cmd, sizeof(cmd), "r %s", filename);
	printf("%s\n", cmd);
	write(clifd[1], cmd, strlen(cmd));
}

void
chomp(char *line)
{
	char *p;
	if(p = strchr(line, '\r'), p) *p = '\0';
	if(p = strchr(line, '\n'), p) *p = '\0';
}

void*
openreaderthread(void *)
{
	FILE* pipe = popen("tkaskopenfile", "r");
	if(pipe == nil) {
		printf("Failed to run tkaskopenfile\n");
		return nil;
	}
	static char line[1024];
	if(fgets(line, sizeof(line), pipe) != nil) {
		chomp(line);
		if(*line) {
			mountTape(line);
			free(lasttape);
			lasttape = strdup(line);
		}
	}
	pclose(pipe);

	return nil;
}

void*
filepunchthread(void *)
{
	FILE* pipe = popen("tkaskopenfilewrite", "r");
	if(pipe == nil) {
		printf("Failed to run tkaskopenfilewrite\n");
		return nil;
	}
	static char line[1024], cmd[1024];
	if(fgets(line, sizeof(line), pipe) != nil) {
		chomp(line);
		if(*line) {
			snprintf(cmd, sizeof(cmd), "p %s", line);
	printf("%s\n", cmd);
			write(clifd[1], cmd, strlen(cmd));
		}
	}
	pclose(pipe);

	return nil;
}

void
chooseReader(void)
{
	pthread_t th;
	pthread_create(&th, nil, openreaderthread, nil);
}

void
filePunch(void)
{
	pthread_t th;
	pthread_create(&th, nil, filepunchthread, nil);
}

void
setfontsize(int sz)
{
	if(sz < 2) sz = 2;
	fontsize = sz;
	font = TTF_OpenFont(fontpath, fontsize);
	if(font == nil) {
		fprintf(stderr, "couldn't open font\n");
		return;
	}
	initFont();
	TTF_CloseFont(font);
}

void
setlayout(void)
{
	setFullscreen(layout->fullscreen);
	setfontsize(layout->fontsize);
}

void
keydown(SDL_Keysym keysym)
{
	if(keysym.scancode == SDL_SCANCODE_F11){
		layout->fullscreen = !layout->fullscreen;
		setFullscreen(layout->fullscreen);
	}
//	if(keysym.scancode == SDL_SCANCODE_ESCAPE)
//		exit(0);

	Region *r = &selected->r;

	int inc = 10;
	if(shift) inc = 1;

	switch(keysym.scancode) {
	case SDL_SCANCODE_LSHIFT:
		shift |= 1;
		break;
	case SDL_SCANCODE_RSHIFT:
		shift |= 2;
		break;
	case SDL_SCANCODE_LCTRL:
		ctrl |= 1;
		break;
	case SDL_SCANCODE_RCTRL:
		ctrl |= 2;
		break;
	case SDL_SCANCODE_CAPSLOCK:
		ctrl |= 4;
		break;

	case SDL_SCANCODE_UP:
		if(layoutmode) {
			if(ctrl)
				r->h -= inc;
			r->y -= inc;
		}
		break;
	case SDL_SCANCODE_DOWN:
		if(layoutmode) {
			if(ctrl)
				r->h += inc;
			r->y += inc;
		}
		break;
	case SDL_SCANCODE_LEFT:
		if(layoutmode) {
			if(ctrl)
				r->w -= inc;
			else
				r->x -= inc;
		}
		break;
	case SDL_SCANCODE_RIGHT:
		if(layoutmode) {
			if(ctrl)
				r->w += inc;
			else
				r->x += inc;
		}
		break;

	case SDL_SCANCODE_F1:
		if(ctrl)
			emuoption("audio", &audio, !audio);
//		else
//			setlayout((lay+1)%nlayouts);
		break;

	case SDL_SCANCODE_F2:
		if(ctrl)
			emuoption("muldiv", &muldiv, !muldiv);
		else
			layoutmode = !layoutmode;
		break;

	case SDL_SCANCODE_F3:
//		layouts[nlayouts] = layouts[nlayouts-1];
//		nlayouts++;
//		nlayouts %= nelem(layouts);
		break;

	case SDL_SCANCODE_SPACE:
		if(layoutmode)
;//			layouts[lay].regions[reg].hidden ^= 1;
		else
			strikeChar(typ, ctrl ? 0 : ' ');
		break;
	case SDL_SCANCODE_TAB:
		if(layoutmode)
			selected = selected->next ? selected->next : layout->comp;
		else
			strikeChar(typ, '\t');
		break;
	case SDL_SCANCODE_BACKSPACE:
		strikeChar(typ, '\b');
		break;
	case SDL_SCANCODE_RETURN:
		strikeChar(typ, '\r');
		break;
	case SDL_SCANCODE_ESCAPE:
		// ALTMODE
		if(typ->istty33)
			strikeChar(typ, '}');
		break;
	case SDL_SCANCODE_DELETE:
		strikeChar(typ, 0177);
		break;

	case SDL_SCANCODE_F5:
//		readLayout();
//		setlayout(lay);
		break;
	case SDL_SCANCODE_F6:
//		saveLayout();
		break;

	case SDL_SCANCODE_F7:
		chooseReader();
		break;
	case SDL_SCANCODE_F8:
		if(lasttape)
			mountTape(lasttape);
		break;
	case SDL_SCANCODE_F9:
		filePunch();
		break;
	case SDL_SCANCODE_F10:
		write(clifd[1], "p", 1);
		break;

	default:
		// only catch what textinput doesn't
		// this is where ctrl & shift logic happens
		if(ctrl && keysym.sym < 256) {
			int c = keysym.sym;
			if(islower(c)) c = toupper(c);
			if(shift) c ^= 020;
			if(ctrl) c &= 037;
			strikeChar(typ, c);
		}
	}
}

void
keyup(SDL_Keysym keysym)
{
	switch(keysym.scancode) {
	case SDL_SCANCODE_LSHIFT:
		shift &= ~1;
		break;
	case SDL_SCANCODE_RSHIFT:
		shift &= ~2;
		break;

	case SDL_SCANCODE_LCTRL:
		ctrl &= ~1;
		break;
	case SDL_SCANCODE_RCTRL:
		ctrl &= ~2;
		break;
	case SDL_SCANCODE_CAPSLOCK:
		ctrl &= ~4;
		break;

	default:
	}
}

void
textinput(char *text)
{
	if(layoutmode) return;
	int c = text[0];
	if(ctrl) {
		if(c == '+' || c == '=')
			setfontsize(fontsize+1);
		else if(c == '-' || c == '_')
			setfontsize(fontsize-1);
	} else if(c < 128 && c != ' ')
// TODO: handle ctrl & shift here?
		strikeChar(typ, c);
}

int mdown;
Component *dragging;

void
mousemotion(SDL_MouseMotionEvent m)
{
	hover = nil;
	for(Component *c = layout->comp; c; c = c->next) {
		Region *r = &c->r;
		if(r->iscircle) {
			Circle cr = getCircle(r);
			int dx = m.x - cr.x;
			int dy = m.y - cr.y;

			if(dx*dx + dy*dy < cr.r*cr.r)
				hover = c;
		} else {
			if(r->x <= m.x && m.x <= r->x+r->w &&
			   r->y <= m.y && m.y <= r->y+r->h)
				hover = c;
		}
	}

	if(dragging) {
		Region *r = &dragging->r;
		if(mdown & 2) {
			r->x += m.xrel;
			r->y += m.yrel;
		}
		if(mdown & 8) {
			r->w += m.xrel;
			r->h += m.yrel;
			if(r->w < 10) r->w = 10;
			if(r->h < 10) r->h = 10;
		}
	}
}

void
mouseup(SDL_MouseButtonEvent m)
{
	dragging = nil;
	mdown &= ~(1<<m.button);
}

void
mousedown(SDL_MouseButtonEvent m)
{
	if(hover) {
		selected = hover;
		dragging = hover;
	}
	mdown |= 1<<m.button;
}

void
resize(int w, int h)
{
//	for(int i = 0; i < nlayouts; i++) {
		for(Component *c = layout->comp; c; c = c->next) {
			Region *r = &c->r;
			r->x = r->x*w/winW;
			r->y = r->y*h/winH;
			r->w = r->w*w/winW;
			r->h = r->h*h/winH;
		}
//	}
	winW = w;
	winH = h;
	if(!fullscreen) {
		realW = winW;
		realH = winH;
	}
}

void
usage(void)
{
	// no need to mention -d here
	fprintf(stderr, "usage: %s [-h host] [-p port] [-r resolution] [tapefile]\n", argv0);
	exit(0);
}


char*
bindir()
{
	char path[PATH_MAX];
	ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
	if (len != -1) {
		path[len] = '\0';
		return strdup(dirname(path));
	}
	return nil;
}

int
main(int argc, char *argv[])
{
	pthread_t th;
	SDL_Event event;
	int running;
	int port;

	port = 3400;
	ARGBEGIN{
	case 'p':
		port = atoi(EARGF(usage()));
		break;
	case 'd':
		dbgflag++;
		break;
	case 'h':
		host = EARGF(usage());
		break;
	case 'r':
		resolution = atoi(EARGF(usage()));
		break;
	}ARGEND;
	if(resolution < 128) resolution = 128;

	SDL_Init(SDL_INIT_EVERYTHING);
	TTF_Init();
	char *dir = bindir();
	snprintf(fontpath, sizeof(fontpath), "%s/DejaVuSansMono.ttf", dir);
	font = TTF_OpenFont(fontpath, 10);
	if(font == nil) {
		snprintf(fontpath, sizeof(fontpath), "/usr/share/fonts/TTF/DejaVuSansMono.ttf");
		font = TTF_OpenFont(fontpath, 10);
	}
	TTF_CloseFont(font);

#ifdef GLES
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	window = SDL_CreateWindow("Programmed Data Processor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1600, 1050, window_flags);
	if(window == nil) {
		fprintf(stderr, "can't create window\n");
		return 1;
	}
	SDL_GLContext gl_context = SDL_GL_CreateContext(window);
	SDL_GL_MakeCurrent(window, gl_context);
	SDL_GL_SetSwapInterval(0); // vsynch (1 on, 0 off)

	pipe(clifd);

	gladLoadGLES2Loader((GLADloadproc)SDL_GL_GetProcAddress);

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	SDL_ShowCursor(SDL_DISABLE);

	initGL();

	initlua();

	layout = malloc(sizeof(*layout));
	layout->w = 1024;
	layout->h = 640;
	layout->fullscreen = 0;
	layout->fontsize = 16;
	layout->bgcol = (Color){ 0x6e, 0x8b, 0x8e, 255 };
	layout->comp = nil;
/*
	Display *disp = addDisplay(595, 155, 330, 330, 1);
	typ = addTypewriter(15, 275, 480, 300, 0);
	Punch *punch = addPunch(15, 155, 480, 80, 0);
	Reader *reader = addReader(15,  45, 480, 80, 0);
*/
	Display *disp = addDisplay(595, 87, 324, 310, 1);
	typ = addTypewriter(10, 378, 486, 236, 0);
	Punch *punch = addPunch(514, 482, 472, 60, 0);
	Reader *reader = addReader(514, 554, 472, 60, 0);
	DecTape *ut1 = addDecTape(10, 50, 240, 120, 0);
	DecTape *ut2 = addDecTape(10, 200, 240, 120, 0);
	DecTape *ut3 = addDecTape(256, 50, 240, 120, 0);
	DecTape *ut4 = addDecTape(256, 200, 240, 120, 0);
	selected = layout->comp;
	winW = layout->w;
	winH = layout->h;
	SDL_SetWindowSize(window, winW, winH);
	setlayout();

	disp->port = port;
	typ->port = typport;
	punch->port = ptpport;
	reader->port = ptrport;
	ut1->port = utport+0;
	ut2->port = utport+1;
	ut3->port = utport+2;
	ut4->port = utport+3;


	disp->fd = dial(host, disp->port);
	if(disp->fd < 0) {
		fprintf(stderr, "couldn't connect to display %s:%d\n", host, port);
		return 1;
	}
	typ->fd = dial(host, typ->port);
	if(typ->fd < 0) {
		fprintf(stderr, "couldn't connect to typewriter %s:%d\n", host, typport);
		return 1;
	}
//	emuoption("audio", &audio, -1);
//	emuoption("muldiv", &muldiv, -1);

	initTypewriter(typ);
	initDisplay(disp);
	initPunch(punch);
	initReader(reader);
	initDecTape(ut1);
	initDecTape(ut2);
	initDecTape(ut3);
	initDecTape(ut4);

	pthread_create(&th, nil, pollthread, nil);

	if(argc > 0) {
		lasttape = strdup(argv[0]);
		mountTape(lasttape);
	}

	running = 1;
	int cursortimer = 0;
	while(running) {
		int show = 0;
		while(SDL_PollEvent(&event)) {
			Layout *l = layout;
			switch(event.type) {
			case SDL_KEYDOWN:
				keydown(event.key.keysym);
				show = 1;
				break;
			case SDL_KEYUP:
				keyup(event.key.keysym);
				show = 1;
				break;

			case SDL_TEXTINPUT:
				textinput(event.text.text);
				show = 1;
				break;

			case SDL_MOUSEMOTION:
				SDL_ShowCursor(SDL_ENABLE);
				cursortimer = 50;
				penx = event.motion.x;
				peny = event.motion.y;
				mousemotion(event.motion);
				if(pendown && disp)
					updatepen(disp);
				break;
			case SDL_MOUSEBUTTONDOWN:
				if(layoutmode) mousedown(event.button);
				if(event.button.button == 1)
					pendown = 1;
				if(disp)
					updatepen(disp);
				break;
			case SDL_MOUSEBUTTONUP:
				if(layoutmode) mouseup(event.button);
				if(event.button.button == 1)
					pendown = 0;
				if(disp)
					updatepen(disp);
				break;

			case SDL_QUIT:
				running = 0;
				break;

			case SDL_WINDOWEVENT:
				switch(event.window.event){
				case SDL_WINDOWEVENT_CLOSE:
					running = 0;
					break;
				case SDL_WINDOWEVENT_MOVED:
				case SDL_WINDOWEVENT_ENTER:
				case SDL_WINDOWEVENT_LEAVE:
				case SDL_WINDOWEVENT_FOCUS_GAINED:
				case SDL_WINDOWEVENT_FOCUS_LOST:
#ifdef SDL_WINDOWEVENT_TAKE_FOCUS
				case SDL_WINDOWEVENT_TAKE_FOCUS:
#endif
					break;
				}
				show = 1;
			}
		}

		int newW, newH;
		SDL_GetWindowSize(window, &newW, &newH);
		if(newW != winW || newH != winH)
			resize(newW, newH);

		show |= layoutmode;
		for(Component *c = layout->comp; c; c = c->next)
			show |= c->processUpdate(c);

	// TODO: cursortimer = 50;

		if(show) {
			clearState();
			resetViewport();
			glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

			col = layoutmode ? colA : layout->bgcol;
			drawRectangle(0, 0, drawW, drawH);

			for(Component *c = layout->comp; c; c = c->next) {
				resetViewport();
				if(!layoutmode)
					c->draw(c);
				else if(c->r.iscircle)
					drawOutlineCircle(&c->r, c == hover, c == selected, colD);
				else
					drawOutlineRect(&c->r, c == hover, c == selected, colC);
			}

			SDL_GL_SwapWindow(window);
		} else
			SDL_Delay(1);	// so we don't busy-wait like crazy
	}

	SDL_GL_DeleteContext(gl_context);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}
