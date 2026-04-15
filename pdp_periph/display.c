#ifdef UNITY_BUILD

typedef struct {
	pthread_mutex_t mutex;
	pthread_cond_t wake;
	int canprocess, candraw;
} Synch;

GLint point_program, excite_program, combine_program;
typedef struct Point Point;
struct Point
{
	int x, y;
	int i;
	int time;
};

typedef struct {
	Component c;
	int fd;
	int port;

	int flip;
	int indices[1024*1024];
	Point newpoints[1024*1024];
	int nnewpoints;
	Point points[1024*1024];
	int npoints;
	float pverts[4*30000];

	GLuint pvbo;
	GLuint whiteTex, yellowTex[2];
	GLuint whiteFBO, yellowFBO[2];

	Synch synch;
} Display;

void init_synch(Synch *s)
{
	pthread_mutex_init(&s->mutex, nil);
	pthread_cond_init(&s->wake, nil);
	s->canprocess = 1;
	s->candraw = 0;
}

void signal_process(Synch *s)
{
	pthread_mutex_lock(&s->mutex);
	s->canprocess = 1;
	pthread_cond_signal(&s->wake);
	pthread_mutex_unlock(&s->mutex);
}

void wait_canprocess(Synch *s)
{
	pthread_mutex_lock(&s->mutex);
	while(!s->canprocess)
		pthread_cond_wait(&s->wake, &s->mutex);
	s->canprocess = 0;
	pthread_mutex_unlock(&s->mutex);
}

void signal_draw(Synch *s)
{
	pthread_mutex_lock(&s->mutex);
	s->candraw = 1;
	pthread_mutex_unlock(&s->mutex);
}

int candraw(Synch *s)
{
	int ret;
	pthread_mutex_lock(&s->mutex);
	ret = s->candraw;
	s->candraw = 0;
	pthread_mutex_unlock(&s->mutex);
	return ret;
}

const char *vs_src =
glslheader
"VSIN vec2 in_pos;\n"
"VSIN vec2 in_uv;\n"
"VSOUT vec2 v_uv;\n"
"void main()\n"
"{\n"
"	v_uv = in_uv;\n"
"	gl_Position = vec4(in_pos.x, in_pos.y, -0.5, 1.0);\n"
"}\n";

const char *point_vs_src =
glslheader
"VSIN vec4 in_pos;\n"
"VSOUT float v_fade;\n"
"VSOUT float v_intensity;\n"
"uniform float u_pointSize;\n"
"void main()\n"
"{\n"
"	v_fade = pow(0.5, in_pos.z);\n"
"	float sz = mix(0.0018, 0.0055, in_pos.w)*1024.0/2.0;\n"
"	v_intensity = mix(0.25, 1.0, in_pos.w);\n"
"	gl_Position = vec4((in_pos.xy / 512.0) - 1.0, 0, 1);\n"
"	gl_PointSize = u_pointSize*sz;\n"
"}\n";

const char *point_fs_src = 
glslheader
outcolor
"FSIN float v_fade;\n"
"FSIN float v_intensity;\n"
"void main()\n"
"{\n"
"	float dist = length(2.0*gl_PointCoord - vec2(1));\n"
"	float intens = clamp(1.0 - dist*dist, 0.0, 1.0)*v_intensity;\n"
"	vec4 color = vec4(0);\n"
"	color.x = intens*v_fade;\n"
"	color.y = intens;\n"
"	color.z = 1.0;\n"
output
"}\n";

const char *excite_fs_src = 
glslheader
outcolor
"FSIN vec2 v_uv;\n"
"uniform sampler2D tex0;\n"
"uniform sampler2D tex1;\n"
"void main()\n"
"{\n"
"	vec2 uv = vec2(v_uv.x, v_uv.y);\n"
"	vec4 white = texture2D(tex0, uv);\n"
"	vec4 yellow = texture2D(tex1, uv);\n"
"	vec4 color = max(vec4(white.y*white.z), 0.987*yellow);\n"
"	color = floor(color*255.0)/255.0;\n"
output
"}\n";

const char *combine_fs_src = 
glslheader
outcolor
"FSIN vec2 v_uv;\n"
"uniform sampler2D tex0;\n"
"uniform sampler2D tex1;\n"
"void main()\n"
"{\n"
"	vec4 bphos1 = vec4(0.24, 0.667, 0.969, 1.0);\n"
"	vec4 yphos1 = 0.9*vec4(0.475, 0.8, 0.243, 1.0);\n"
"	vec4 yphos2 = 0.975*vec4(0.494, 0.729, 0.118, 0.0);\n"

"	vec2 uv = vec2(v_uv.x, v_uv.y);\n"
"	vec4 white = texture2D(tex0, uv);\n"
"	vec4 yellow = texture2D(tex1, uv);\n"
"	vec4 yel = mix(yphos2, yphos1, yellow.x);\n"
"	float a = 0.663 * (yel.a + (1.0-cos(3.141569*yel.a))/2.0)/2.0;\n"
"	vec4 color = bphos1*white.x*white.z + yel*a;\n"
//"	color = vec4(white.x);\n"
//"	color.z = 1.0;\n"
output
"}\n";

void
initDisplayGL(void)
{
	GLint vs = compileshader(GL_VERTEX_SHADER, vs_src);
	GLint point_vs = compileshader(GL_VERTEX_SHADER, point_vs_src);
	GLint point_fs = compileshader(GL_FRAGMENT_SHADER, point_fs_src);
	GLint excite_fs = compileshader(GL_FRAGMENT_SHADER, excite_fs_src);
	GLint combine_fs = compileshader(GL_FRAGMENT_SHADER, combine_fs_src);
	point_program = linkprogram(point_fs, point_vs);
	excite_program = linkprogram(excite_fs, vs);
	combine_program = linkprogram(combine_fs, vs);

	clearState();

	makeQuad();
}

void
drawDisplayUpdate(Display *d)
{
	if(dbgflag) {
		float dt = getDeltaTime();
		float st = simtime/1000000.0f;
		float rt = (float)realtime/SDL_GetPerformanceFrequency();
		printf("%f %d. %.2f %.2f %.2f\n", dt, d->npoints, st, rt, rt-st);
	}

	glViewport(0, 0, BWIDTH, BHEIGHT);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

	/* draw white phosphor */
	glEnable(GL_BLEND);
//	glBlendEquation(GL_MAX);
	glBlendFunc(GL_ONE, GL_ONE);

	glBindFramebuffer(GL_FRAMEBUFFER, d->whiteFBO);
	glClear(GL_COLOR_BUFFER_BIT);
	glUseProgram(point_program);


	float pointSize = 2.0f * WIDTH/1024.0f;
	glUniform1f(glGetUniformLocation(point_program, "u_pointSize"), pointSize);

	int i;
	for(i = 0; i < d->npoints; i++) {
		if(i*4 >= nelem(d->pverts))
			break;
		d->pverts[i*4 + 0] = d->points[i].x;
		d->pverts[i*4 + 1] = d->points[i].y;
		d->pverts[i*4 + 2] = d->points[i].time/50000.0f;
		d->pverts[i*4 + 3] = d->points[i].i/7.0f;
	}
// THREAD: signal ready to process
signal_process(&d->synch);
	int stride = sizeof(float[4]);
	glBindBuffer(GL_ARRAY_BUFFER, d->pvbo);
	glBufferData(GL_ARRAY_BUFFER, i*stride, d->pverts, GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, stride, 0);
	glDrawArrays(GL_POINTS, 0, i);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDisable(GL_BLEND);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, d->whiteTex);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, d->yellowTex[d->flip]);

	/* draw and age yellow layer */
	setvbo(vbo);
	glBindFramebuffer(GL_FRAMEBUFFER, d->yellowFBO[!d->flip]);
	glClear(GL_COLOR_BUFFER_BIT);
	glUseProgram(excite_program);
	glDrawArrays(GL_TRIANGLES, 0, 6);

	d->flip = !d->flip;

	clearState();
}

void
drawDisplay(Display *d)
{
	Component *c = &d->c;
	if(fullscreenDisplay) {
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		setSquareRegion(&winRegion);
	} else {
		setColor(0,0,0,255);
		Circle cr = getCircle(&c->r);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		drawCircle(cr.x, cr.y, cr.r);
		glDisable(GL_BLEND);

		setSquareRegion(&c->r);
	}

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, d->whiteTex);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, d->yellowTex[d->flip]);

	glUseProgram(combine_program);
	glDisable(GL_BLEND);
	setvbo(vbo);
	glDrawArrays(GL_TRIANGLES, 0, 6);

	clearState();
}


void
process(Display *d, int frmtime)
{
	Point *p;
	int i, n, idx;

//printf("process %d\n", nnewpoints);
	/* age */
	n = 0;
	for(i = 0; i < d->npoints; i++) {
		p = &d->points[i];
		p->time += frmtime;
		if(p->time < 200000)
			d->points[idx = n++] = *p;
		else
			idx = -1;
		d->indices[p->y*1024 + p->x] = idx;
	}
	d->npoints = n;

	/* add new points */
	for(i = 0; i < d->nnewpoints; i++) {
		Point *np = &d->newpoints[i];
		idx = d->indices[np->y*1024 + np->x];
		if(idx < 0) {
			idx = d->npoints++;
			d->indices[np->y*1024 + np->x] = idx;
		}
		p = &d->points[idx];
		p->x = np->x;
		p->y = np->y;
		p->i = np->i;
		p->time = frmtime - np->time;
	}
	d->nnewpoints = 0;
}

void*
dispthread(void *args)
{
	uint32 cmd;
	uint32 cmds[128];
	int ncmds;
	int nbytes;
	int i;
	uint64 time;
	uint64 frmtime = 33333;
	int x, y, intensity, dt;
	Display *d = args;

	uint64 realtime_start = SDL_GetPerformanceCounter();
	simtime = 0;
	realtime = realtime_start;

	time = 0;
	int esc = 0;
for(;;){
	nbytes = read(d->fd, cmds, sizeof(cmds));
	if(nbytes <= 0) {
		// This seems to happen when the pdp1 isn't noticing the closed
		// connection quickly enough. shouldn't be a huge issue in practice
		fprintf(stderr, "dpy disconnected\n");
		break;
	}
	if((nbytes % 4) != 0) printf("yikes %d\n", nbytes), exit(1);
	ncmds = nbytes/4;

	for(i = 0; i < ncmds; i++) {
		cmd = cmds[i];
		dt = cmd>>23;

		// escape for longer delays of nothing
		if(esc) {
			esc = 0;
			time += cmd;
		} else if(dt == 511) {
			esc = 1;
		} else {
			x = cmd&01777;
			y = cmd>>10 & 01777;
			intensity = cmd>>20 & 7;
			time += dt;

			if(x || y) {
				Point *np = &d->newpoints[d->nnewpoints++];
				np->x = x;
				np->y = y;
				np->i = intensity;
				np->time = time;
			}
		}

		// we hope draw is finished before we decide to flip again
		// 30fps should be doable
		while(time > frmtime) {
			time -= frmtime;
simtime += frmtime;
realtime = SDL_GetPerformanceCounter() - realtime_start;

// THREAD: wait here until ready
wait_canprocess(&d->synch);
			process(d, frmtime);
// THREAD: signal ready to draw
signal_draw(&d->synch);
		}
	}
}

	SDL_Event event = { SDL_QUIT };
	SDL_PushEvent(&event);
}

int
doDrawDisplay(Display *d)
{
// THREAD: check for ready to draw, then draw
	int draw = candraw(&d->synch);
	if(draw)
		drawDisplayUpdate(d);
	return draw;
}

int penx;
int peny;
int pendown;

void
updatepen(Display *d)
{
	uint32 cmd;
	cmd = 0xFF<<24;
	cmd |= pendown << 20;

	Region sr = getSquareRegion(fullscreenDisplay ? &winRegion : &d->c.r);
	int px = (penx-sr.x)/sr.w * 1024;
	int py = (peny-sr.y)/sr.h * 1024;

	if(px >= 0 && px < 1024 &&
	   py >= 0 && py < 1024) {
		cmd |= px << 10;
		cmd |= 1023-py;
		write(d->fd, &cmd, 4);
	} else if(!pendown) {
		write(d->fd, &cmd, 4);
	}
}


void
initDisplay(Display *d)
{
	pthread_t th;

	makeFBO(&d->whiteFBO, &d->whiteTex);
	makeFBO(&d->yellowFBO[0], &d->yellowTex[0]);
	makeFBO(&d->yellowFBO[1], &d->yellowTex[1]);
	glGenBuffers(1, &d->pvbo);

	for(int i = 0; i < 1024*1024; i++)
		d->indices[i] = -1;

	init_synch(&d->synch);
	pthread_create(&th, nil, dispthread, d);
}

Display*
addDisplay(int x, int y, int w, int h, int iscircle)
{
	Display *d = malloc(sizeof(*d));
	Component *c = &d->c;
	assert(c);
	c->draw = (void (*)(Component*))drawDisplay;
	c->processUpdate = (int (*)(Component*))doDrawDisplay;
	addComponent(c, x, y, w, h, iscircle);
	return d;
}

#endif
