#ifdef UNITY_BUILD

typedef struct {
	Component c;
	int fd;
	struct pollfd *pfd;
	int port;

	unsigned char *buf;
	int buflen;
	int pos;

	int updated;
} Reader;

typedef struct {
	Component c;
	int fd;
	struct pollfd *pfd;
	int port;

	int *buf;
	int buflen;

	int updated;
} Punch;

char *lasttape;

const char *hole_vs_src =
glslheader
"VSIN vec2 in_pos;\n"
"VSIN vec2 in_uv;\n"
"VSOUT vec2 v_uv;\n"
"uniform float u_size;\n"
"void main()\n"
"{\n"
"	v_uv = in_uv;\n"
"	vec2 p = vec2(in_pos.x*2.0-1.0, -(in_pos.y*2.0-1.0));\n"
"	gl_Position = vec4(p.x, p.y, -0.5, 1.0);\n"
"	gl_PointSize = 2.0*u_size;\n"
"}\n";

const char *hole_fs_src = 
glslheader
outcolor
"uniform vec4 u_color;\n"
"void main()\n"
"{\n"
"	vec2 pos = 2.0*gl_PointCoord - vec2(1.0);\n"
"	vec4 color = u_color;\n"
"	color.a = 1.0 - smoothstep(0.8, 1.0, length(pos));\n"
output
"}\n";

GLint hole_program;

void
initReaderGL(void)
{
	GLint hole_vs = compileshader(GL_VERTEX_SHADER, hole_vs_src);
	GLint hole_fs = compileshader(GL_FRAGMENT_SHADER, hole_fs_src);
	hole_program = linkprogram(hole_fs, hole_vs);

	glGenBuffers(1, &immVbo);
}

void
drawHole(float x, float y, float r)
{
	x /= drawW;
	y /= drawH;

	Vertex v[] = { { x, y,	0.0f, 0.0f } };
	glUseProgram(hole_program);
	setvbo(immVbo);
	glEnable(GL_VERTEX_PROGRAM_POINT_SIZE);
	glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);
	glUniform4f(glGetUniformLocation(hole_program, "u_color"),
		col.r/255.0f, col.g/255.0f, col.b/255.0f, col.a/255.0f);
	glUniform1f(glGetUniformLocation(hole_program, "u_size"), r);
	glDrawArrays(GL_POINTS, 0, 1);
}

void
drawReader(Reader *ptr)
{
	setRegion(&ptr->c.r);

	int width = drawW;
	int height = drawH;

	int space = height/10;
	if(space < 4) space = 4;
	// heuristics
	int r = space/2 - 1;
	int fr = r/2 + 1;
	if(fr == r) fr--;

	int i = ptr->pos - 10;
	int c;


	setColor(80, 80, 80, 255);
	drawRectangle(0.0f, 0.0f, width, height);

	if(ptr->buflen > 0) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		setColor(255, 255, 176, 255);
		int x = i < 0 ? -i*space : 0;
		int w = (ptr->buflen-i)*space;
		drawRectangle(x, 0.0f, w, height);

		setColor(0, 0, 0, 255);
		for(int x = space/2; x < width+2*r; x += space, i++) {
			if(i >= 0 && i < ptr->buflen)
				c = ptr->buf[i];
			else
				continue;
			if(c & 1) drawHole(x, 1*space, r);
			if(c & 2) drawHole(x, 2*space, r);
			if(c & 4) drawHole(x, 3*space, r);
			drawHole(x, 4*space, fr);
			if(c & 010) drawHole(x, 5*space,  r);
			if(c & 020) drawHole(x, 6*space,  r);
			if(c & 040) drawHole(x, 7*space,  r);
			if(c & 0100) drawHole(x, 8*space, r);
			if(c & 0200) drawHole(x, 9*space, r);
		}

		glDisable(GL_BLEND);
	}

	clearState();
}

void
drawPunch(Punch *ptp)
{
	setRegion(&ptp->c.r);

	int width = drawW;
	int height = drawH;

	int space = height/10;
	if(space < 4) space = 4;
	// heuristics
	int r = space/2 - 1;
	int fr = r/2 + 1;
	if(fr == r) fr--;

	int i, c;

	int punchpos = width-5*space;


	setColor(80, 80, 80, 255);
	drawRectangle(0.0f, 0.0f, width, height);

	if(ptp->fd >= 0) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		setColor(255, 255, 176, 255);
		for(i = 0; i < ptp->buflen; i++)
			if(ptp->buf[i] < 0)
				break;
		int x = punchpos - i*space;
		if(x < 0) x = 0;
		drawRectangle(x, 0.0f, width, height);

		setColor(0, 0, 0, 255);
		i = 0;
		for(int x = punchpos; x > -2*r; x -= space) {
			if(i < ptp->buflen)
				c = ptp->buf[i++];
			else
				c = -1;
			if(c < 0)
				continue;
			if(c & 1) drawHole(x, 1*space, r);
			if(c & 2) drawHole(x, 2*space, r);
			if(c & 4) drawHole(x, 3*space, r);
			drawHole(x, 4*space, fr);
			if(c & 010) drawHole(x, 5*space,  r);
			if(c & 020) drawHole(x, 6*space,  r);
			if(c & 040) drawHole(x, 7*space,  r);
			if(c & 0100) drawHole(x, 8*space, r);
			if(c & 0200) drawHole(x, 9*space, r);
		}

		glDisable(GL_BLEND);
	}

	clearState();
}

// don't call when still mounted!
void
mountptr(Reader *ptr, const char *file)
{
	FILE *f = fopen(file, "rb");
	if(f == nil) {
		fprintf(stderr, "couldn't open file %s\n", file);
		return;
	}
	fseek(f, 0, 2);
	ptr->buflen = ftell(f);
	fseek(f, 0, 0);
	ptr->buf = malloc(ptr->buflen);
	fread(ptr->buf, 1, ptr->buflen, f);
	fclose(f);

	ptr->pos = 0;
	// find beginning
	while(ptr->pos < ptr->buflen && ptr->buf[ptr->pos] == 0)
		ptr->pos++;
	if(ptr->pos > 20) ptr->pos -= 10;
	ptr->updated = 1;
}

void
unmountptr(Reader *ptr)
{
	// i'm lazy...try to mitigate race somewhat
	static char unsigned dummy[100], *p;
	ptr->buflen = 0;
	p = ptr->buf;
	ptr->buf = dummy;
	if(p != dummy)
		free(p);
	ptr->updated = 1;
}

void
ptrsend(Reader *ptr)
{
	char c;
	c = ptr->buf[ptr->pos];
	write(ptr->fd, &c, 1);
}

void
disconnectptr(Reader *ptr)
{
	struct pollfd *pfd = ptr->pfd;
	close(pfd->fd);
	pfd->fd = -1;
	pfd->events = 0;
}

void
connectptr(Reader *ptr)
{
	struct pollfd *pfd = ptr->pfd;
	pfd->fd = dial(host, ptr->port);
	ptr->fd = pfd->fd;
	if(pfd->fd < 0) {
		pfd->events = 0;
		unmountptr(ptr);
	} else {
		pfd->events = POLLIN;
		ptrsend(ptr);
	}
}




void
initptp(Punch *ptp)
{
	ptp->fd = open("/tmp/ptpunch", O_CREAT|O_WRONLY|O_TRUNC, 0644);
	if(ptp->fd < 0) {
		fprintf(stderr, "can't open /tmp/ptpunch\n");
		exit(1);
	}
	for(int i = 0; i < ptp->buflen; i++)
		ptp->buf[i] = -1;
	ptp->updated = 1;
}

void
fileptp(Punch *ptp, const char *file)
{
	int fd;
	char c;

	fd = open(file, O_CREAT|O_WRONLY|O_TRUNC, 0644);
	if(fd < 0) {
		fprintf(stderr, "couldn't open file %s\n", file);
		return;
	}
	close(ptp->fd);
	ptp->fd = open("/tmp/ptpunch", O_RDONLY);
	while(read(ptp->fd, &c, 1) > 0)
		write(fd, &c, 1);
	close(fd);
	close(ptp->fd);
	initptp(ptp);
}

void
disconnectptp(Punch *ptp)
{
	struct pollfd *pfd = ptp->pfd;
	close(pfd->fd);
	pfd->fd = -1;
	pfd->events = 0;
}

void
connectptp(Punch *ptp)
{
	struct pollfd *pfd = ptp->pfd;
	pfd->fd = dial(host, ptp->port);
	ptp->fd = pfd->fd;
	if(pfd->fd < 0) {
		pfd->events = 0;
		initptp(ptp);
	} else {
		pfd->events = POLLIN;
	}
}


void
cliReader(void *owner, char **args)
{
printf("cliReader\n");
	Reader *ptr = (Reader*)owner;
	disconnectptr(ptr);
	unmountptr(ptr);
	if(args[1]) {
		mountptr(ptr, args[1]);
		if(ptr->buflen > 0)
			connectptr(ptr);
	}
}

void
handleReader(void *owner)
{
printf("handleReader\n");
	Reader *ptr = (Reader*)owner;
	char c;
	if(read(ptr->fd, &c, 1) <= 0) {
		disconnectptr(ptr);
		unmountptr(ptr);
	} else {
		ptr->pos++;
		ptrsend(ptr);
		ptr->updated = 1;
	}
}

void
cliPunch(void *owner, char **args)
{
printf("cliPunch\n");
	Punch *ptp = (Punch*)owner;
	disconnectptp(ptp);
	if(args[1])
		fileptp(ptp, args[1]);
	else
		initptp(ptp);
	connectptp(ptp);
}

void
handlePunch(void *owner)
{
printf("handlePunch\n");
	Punch *ptp = (Punch*)owner;
	char c;
	if(read(ptp->fd, &c, 1) <= 0) {
		disconnectptp(ptp);
		initptp(ptp);
		connectptp(ptp);
	} else {
		memcpy(ptp->buf+1, ptp->buf, (ptp->buflen-1)*sizeof(*ptp->buf));
		ptp->buf[0] = c&0377;
		write(ptp->fd, &c, 1);
		ptp->updated = 1;
	}
}

int
doDrawReader(Reader *ptr)
{
	int draw = ptr->updated;
	ptr->updated = 0;
	return draw;
}

int
doDrawPunch(Punch *ptp)
{
	int draw = ptp->updated;
	ptp->updated = 0;
	return draw;
}

void
initReader(Reader *ptr)
{
	pthread_t th;
	ptr->pfd = addPollHandler(handleReader, ptr);
	addCliCmd("r", ptr, cliReader);
}

void
initPunch(Punch *ptp)
{
	pthread_t th;
	ptp->pfd = addPollHandler(handlePunch, ptp);
	addCliCmd("p", ptp, cliPunch);
	ptp->buflen = 2000;
	ptp->buf = malloc(ptp->buflen*sizeof(int));

	initptp(ptp);
	connectptp(ptp);
}

Reader*
addReader(int x, int y, int w, int h, int iscircle)
{
	Reader *ptr = malloc(sizeof(*ptr));
	memset(ptr, 0, sizeof(*ptr));
	Component *c = &ptr->c;
	assert(c);
	c->draw = (void (*)(Component*))drawReader;
	c->processUpdate = (int (*)(Component*))doDrawReader;
	addComponent(c, x, y, w, h, iscircle);
	return ptr;
}

Punch*
addPunch(int x, int y, int w, int h, int iscircle)
{
	Punch *ptp = malloc(sizeof(*ptp));
	memset(ptp, 0, sizeof(*ptp));
	Component *c = &ptp->c;
	assert(c);
	c->draw = (void (*)(Component*))drawPunch;
	c->processUpdate = (int (*)(Component*))doDrawPunch;
	addComponent(c, x, y, w, h, iscircle);
	return ptp;
}

#endif
