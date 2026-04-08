#ifdef UNITY_BUILD

extern unsigned char dectape_cap_circle_raw[];
extern unsigned char dectape_reel_raw[];

typedef struct {
	Component c;
	struct pollfd *pfd;
	int fd;
	int port;

	// tape status from emulator
	int pos;
	int wrlock;
	int motion;
	int num;
	int mounted;

	int updated;
	float angle;
	float reelL, reelR;
} DecTape;

const char *sprite_vs_src =
glslheader
"VSIN vec2 in_pos;\n"
"VSIN vec2 in_uv;\n"
"VSOUT vec2 v_uv;\n"
"uniform vec2 u_viewport;\n"
"uniform vec2 u_center;\n"
"uniform vec2 u_halfsize;\n"
"uniform float u_angle;\n"
"void main()\n"
"{\n"
"	float c = cos(u_angle);\n"
"	float s = sin(u_angle);\n"
"	vec2 scaled = in_pos * u_halfsize;\n"
"	vec2 rotated = vec2(c*scaled.x - s*scaled.y,\n"
"	                    s*scaled.x + c*scaled.y);\n"
"	vec2 world = u_center + rotated;\n"
"	vec2 norm = world / u_viewport;\n"
"	vec2 ndc = vec2(norm.x*2.0-1.0, -(norm.y*2.0-1.0));\n"
"	v_uv = in_pos*0.5 + 0.5;\n"
"	v_uv.y = 1.0 - v_uv.y;\n"
"	gl_Position = vec4(ndc, -0.5, 1.0);\n"
"}\n";

const char *sprite_fs_src =
glslheader
outcolor
"FSIN vec2 v_uv;\n"
"uniform sampler2D tex0;\n"
"void main()\n"
"{\n"
"	vec4 color = texture2D(tex0, vec2(v_uv.x, 1.0-v_uv.y));\n"
output
"}\n";

GLint sprite_program;
GLuint cap_tex, reel_tex;

void
initDecTapeGL(void)
{
	GLint vs = compileshader(GL_VERTEX_SHADER, sprite_vs_src);
	GLint fs = compileshader(GL_FRAGMENT_SHADER, sprite_fs_src);
	sprite_program = linkprogram(vs, fs);

	glGenTextures(1, &cap_tex);
	glBindTexture(GL_TEXTURE_2D, cap_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 512, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, dectape_cap_circle_raw);
	texDefaults();

	glGenTextures(1, &reel_tex);
	glBindTexture(GL_TEXTURE_2D, reel_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 512, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, dectape_reel_raw);
	texDefaults();

	glBindTexture(GL_TEXTURE_2D, 0);
}

void
drawSprite(float cx, float cy, float hw, float hh, float angle, GLint tex)
{
	Vertex quad[] = {
		{ -1.0f, -1.0f,   0.0f, 0.0f },
		{  1.0f, -1.0f,   1.0f, 0.0f },
		{  1.0f,  1.0f,   1.0f, 1.0f },

		{ -1.0f, -1.0f,   0.0f, 0.0f },
		{  1.0f,  1.0f,   1.0f, 1.0f },
		{ -1.0f,  1.0f,   0.0f, 1.0f },
	};

	glBindTexture(GL_TEXTURE_2D, tex);
	glUseProgram(sprite_program);
	setvbo(immVbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_DYNAMIC_DRAW);

	glUniform2f(glGetUniformLocation(sprite_program, "u_viewport"), drawW, drawH);
	glUniform2f(glGetUniformLocation(sprite_program, "u_center"),   cx, cy);
	glUniform2f(glGetUniformLocation(sprite_program, "u_halfsize"), hw, hh);
	glUniform1f(glGetUniformLocation(sprite_program, "u_angle"),    angle);

	glDrawArrays(GL_TRIANGLES, 0, 6);
}

void
drawDecTape(DecTape *dt)
{
	setRegion(&dt->c.r);

	float cx = drawW * 0.5f;
	float cy = drawH * 0.5f;
	float cap_half = (drawW < drawH ? drawW : drawH) * 0.3f;

	setColor(80, 80, 80, 255);
	drawRectangle(0.0f, 0.0f, drawW, drawH);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glActiveTexture(GL_TEXTURE0);

	// 350 +- 55 bits per inch
	// ca. 3 inches diameter
	float inch = dt->pos / 350.0f;
	dt->angle = inch / 1.5f;

	float scl = 1/0.71f;
	if(dt->mounted)
		drawSprite(cx - cx/2, cy, cap_half*scl, cap_half*scl, dt->angle + dt->reelL, reel_tex);
	if(1)
		drawSprite(cx + cx/2, cy, cap_half*scl, cap_half*scl, dt->angle + dt->reelR, reel_tex);
	drawSprite(cx - cx/2, cy, cap_half, cap_half, dt->angle, cap_tex);
	drawSprite(cx + cx/2, cy, cap_half, cap_half, dt->angle, cap_tex);

	glDisable(GL_BLEND);

	clearState();
}

int
doDrawDecTape(DecTape *dt)
{
	if(dt->motion) {
		int cmd = 0x80000000;
		write(dt->fd, &cmd, 4);
	}

	int draw = dt->updated;
	dt->updated = 0;
	return draw;
}

void
connectdt(DecTape *dt)
{
	struct pollfd *pfd = dt->pfd;
	pfd->fd = dial(host, dt->port);
	dt->fd = pfd->fd;
	if(pfd->fd < 0)
		pfd->events = 0;
	else
		pfd->events = POLLIN;
}

void
disconnectdt(DecTape *dt)
{
	struct pollfd *pfd = dt->pfd;
	close(pfd->fd);
	pfd->fd = -1;
	pfd->events = 0;
}

void
handleDecTape(void *owner)
{
	DecTape *dt = (DecTape*)owner;
	int status;
	if(readn(dt->fd, &status, 4) < 0) {
		disconnectdt(dt);
	} else {
		dt->pos = status & 0xFFFFF;
		dt->num = (status>>23) & 7;
		dt->motion = (status>>24) & 3;
		dt->mounted = (status>>26) & 1;
		dt->updated = 1;
	}
}

float randAngle(void) { return (float)rand() / RAND_MAX * 6.2831853f; }

void
initDecTape(DecTape *dt)
{
	dt->pfd = addPollHandler(handleDecTape, dt);
	connectdt(dt);

	dt->angle = 0.0f;
	dt->reelL = randAngle();
	dt->reelR = randAngle();
}

DecTape*
addDecTape(int x, int y, int w, int h, int iscircle)
{
	DecTape *dt = malloc(sizeof(*dt));
	memset(dt, 0, sizeof(*dt));
	Component *c = &dt->c;
	assert(c);
	c->draw = (void (*)(Component*))drawDecTape;
	c->processUpdate = (int (*)(Component*))doDrawDecTape;
	addComponent(c, x, y, w, h, iscircle);
	return dt;
}

#endif
