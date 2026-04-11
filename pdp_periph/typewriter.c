#ifdef UNITY_BUILD

enum { 
	SE = 240,
	NOP = 241,
	BRK = 243,
	IP = 244,
	AO = 245,
	AYT = 246,
	EC = 247,
	EL = 248,
	GA = 249,
	SB = 250,
	WILL = 251,
	WONT = 252,
	DO = 253,
	DONT = 254,
	IAC = 255,

	XMITBIN = 0,
	ECHO_ = 1,
	SUPRGA = 3,
	LINEEDIT = 34,
};

enum {
	NUMLINES = 200,
	LINELEN = 100,
};
typedef struct {
	Component c;
	int fd;
	int port;

	char typ_lines[NUMLINES][LINELEN];
	int nlines;
	int curline;
	int red;
	int istty33;

	char escstr[20];
	int escp;

	char utf8str[10];
	int utf8p;
	int utf8len;

	int updated;
} Typewriter;

void
drawGlyph(int i, float x, float y)
{
	Glyph *g = &glyphs[i];
	float x1 = x/drawW;
	float y1 = y/drawH;
	float x2 = (x+g->w)/drawW;
	float y2 = (y+g->h)/drawH;
	Vertex quad[] = {
		{ x1, y1,		0.0f, 1.0f },
		{ x2, y1,		1.0f, 1.0f },
		{ x2, y2,		1.0f, 0.0f },

		{ x1, y1,		0.0f, 1.0f },
		{ x2, y2,		1.0f, 0.0f },
		{ x1, y2,		0.0f, 0.0f },
	};
	glUseProgram(tex_program);
	setvbo(immVbo);
	glBindTexture(GL_TEXTURE_2D, g->tex);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_DYNAMIC_DRAW);
	glUniform4f(glGetUniformLocation(tex_program, "u_color"),
		col.r/255.0f, col.g/255.0f, col.b/255.0f, col.a/255.0f);
	glDrawArrays(GL_TRIANGLES, 0, 6);
}

// for ASR33
int
linelen(const char *s)
{
	int l = 0;
	for(; *s; s++) {
		if(*s == '\b') {
			l--;
			if(l < 0) l = 0;
		} else if(*s == '\t')
			l = ((l+7)/8)*8;
		else if(*s == '\r')
			l = 0;
		else if(*s >= 040 && *s < 0140)
			l++;
	}
	return l;
}

int
drawString(Typewriter *t, const char *s, int x, int y)
{
	int space = glyphs[' '].advance;
	int lx = 0;
	for(; *s; s++) {
		int c = *s;
		if(c & 0200)
			setColor(170,0,0,255);
		else
			setColor(0,0,0,255);
		c &= 0177;

		switch(c) {
		case '\b':
			lx -= space;
			if(lx < 0) lx = 0;
			break;

		case '\t':
			int nsp = lx/space;
			nsp = ((nsp+8)/8)*8;
			lx = nsp*space;
			break;

		case '\r':
			lx = 0;
			break;

		case '|':
		case '_':
			if(!t->istty33) {
		case 020:	// mid dot
		case 021:	// overline
				drawGlyph(c, x+lx, y);
				break;
			}

		default:
			drawGlyph(c, x+lx, y);
			lx += glyphs[c].advance;
			break;
		}
	}
	return lx;
}

void
drawTypewriter(Typewriter *t)
{
	Region *r = &t->c.r;
	setRegion(r);
	setColor(255, 255, 255, 255);
	drawRectangle(0.0f, 0.0f, drawW, drawH);

	drawW = winW;
	drawH = winH;
	glViewport(0.0f, 0.0f, drawW, drawH);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glScissor(r->x, drawH-r->y-r->h, r->w, r->h);
	glEnable(GL_SCISSOR_TEST);

	int spacing = fontsize+1;
	int x = r->x + 5;
	int y = r->y+r->h - 10 - spacing;
	int lx;
	for(int i = t->nlines-1; i >= 0; i--)
		lx = drawString(t, t->typ_lines[(t->curline+NUMLINES-i)%NUMLINES], x, y-i*spacing);
	if(t->red)
		setColor(170,0,0,255);
	else
		setColor(128,128,128,255);
	drawGlyph('_', x+lx, y);

	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_BLEND);
}

int
mapchar(int r)
{
	switch(r) {
	case 0x00B7:	return 020;	//	·	middle dot
	case 0x203E:	return 021;	//	‾	overline
	case 0x2192:	return 022;	//	→	right arrow
	case 0x2283:	return 023;	//	⊃	superset
	case 0x2228:	return 024;	//	∨	or
	case 0x2227:	return 025;	//	∧	and
	case 0x2191:	return 026;	//	↑	up arrow
	case 0x00D7:	return 027;	//	×	times
	}
	if(r >= 128)
		return ' ';
	return r;
}

void
typeChar(Typewriter *t, int c)
{
	if((c&0177) == '\n') {
		int l = linelen(t->typ_lines[t->curline]);
		t->curline = (t->curline+1)%NUMLINES;
		t->typ_lines[t->curline][0] = 0;
		if(t->nlines < NUMLINES)
			t->nlines++;
		t->updated = 1;
		// TODO: make this better
		if(t->istty33)
			while(l--)
				typeChar(t, ' ');
		return;
	}
	char *line = t->typ_lines[t->curline];
	int i;
	for(i = 0; line[i] != 0; i++);
	line[i++] = c;
	line[i] = 0;
	t->updated = 1;
}

void
typeString(Typewriter *t, const char *s)
{
	while(*s)
		typeChar(t, *s++);
}

int
leading1s(int byte)
{
	int bit = 0200;
	int n = 0;
	while(bit & byte) {
		n++;
		bit >>= 1;
	}
	return n;
}

int
torune(char *s)
{
	int r;
	int nc = leading1s(*s);
	r = *s++ & (0177>>nc--);
	while(nc--) {
		r <<= 6;
		r |= *s++ & 077;
	}
	return r;
}

void
recvChar(Typewriter *t, int c)
{
	if(t->istty33) {
		if(islower(c)) c = toupper(c);
		if(c >= 0140)
			return;
		if(c < 040) switch(c) {
		case ' ':
		case '\n':
		case '\t':	// not sure about this one
		case '\r':
		case '\b':
			break;
		default:
			// TODO: bell
			return;
		}
	} else {
		if(c == '\r')
			return;
		if(c == 033) {
			t->escp = 0;
			t->escstr[t->escp++] = c;
			return;
		}
		if(t->escp) {
			t->escstr[t->escp++] = c;
			t->escstr[t->escp] = 0;
			if(strcmp(t->escstr, "\033[31m") == 0) {
				t->red = 0200;
				t->escp = 0;
			} else if(strcmp(t->escstr, "\033[39;49m") == 0) {
				t->red = 0;
				t->escp = 0;
			}
			return;
		}
	}

	// this UTF-8 nonsense is really annoying
	int nc = leading1s(c);
	if(nc == 0)
		typeChar(t, c | t->red);
	else if(nc > 1) {
		// utf8 starter
		t->utf8len = nc;
		t->utf8p = 0;
		t->utf8str[t->utf8p++] = c;
	} else if(nc == 1) {
		// utf8 char
		t->utf8str[t->utf8p++] = c;
		if(t->utf8p == t->utf8len)
			typeChar(t, mapchar(torune(t->utf8str)) | t->red);
	}
}

int
telnetchar(Typewriter *t, int state, char cc)
{
	int c = cc & 0377;
	switch(state) {
	case 0:
		if(c == IAC)
			return IAC;
		recvChar(t, c);
		break;

	case WILL: case WONT: case DO: case DONT:
		// ignore c
		break;

	default:
		switch(c) {
		case NOP: break;
		case WILL: case WONT: case DO: case DONT:
			return c;
		case IAC:
			recvChar(t, c);
			break;
		default:
			printf("unknown cmd %x %o\n", c, c);
		}
	}
	return 0;
}

void*
typthread(void *args)
{
	Typewriter *t = args;
	int n;
	char buf[20];
	int state = 0;
	while(n = read(t->fd, buf, sizeof(buf)), n >= 0) {
		for(int i = 0; i < n; i++)
			state = telnetchar(t, state, buf[i]);
	}
}

void
strikeChar(Typewriter *t, int c)
{
	if(islower(c)) c = toupper(c);
	char b = c;
	write(t->fd, &b, 1);
}

int
doDrawTypewriter(Typewriter *t)
{
	int draw = t->updated;
	t->updated = 0;
	return draw;
}

void
initTypewriter(Typewriter *t)
{
	pthread_t th;
	t->red = 0;
	t->curline = 0;
	t->typ_lines[t->curline][0] = 0;
	t->nlines = 1;
	t->updated = 1;
	t->escp = 0;
	t->utf8p = 0;
	t->utf8len = 0;
	t->istty33 = 1;
	pthread_create(&th, nil, typthread, t);
}


Typewriter*
addTypewriter(int x, int y, int w, int h, int iscircle)
{
	Typewriter *t = malloc(sizeof(*t));
	memset(t, 0, sizeof(*t));
	Component *c = &t->c;
	assert(c);
	c->draw = (void (*)(Component*))drawTypewriter;
	c->processUpdate = (int (*)(Component*))doDrawTypewriter;
	addComponent(c, x, y, w, h, iscircle);
	return t;
}

#endif
