#ifdef UNITY_BUILD

typedef struct {
	Component c;

	int updated;
} DecTape;

void
drawDecTape(DecTape *dt)
{
	setRegion(&dt->c.r);

	int width = drawW;
	int height = drawH;

	setColor(80, 80, 80, 255);
	drawRectangle(0.0f, 0.0f, width, height);
}

int
doDrawDecTape(DecTape *dt)
{
	int draw = dt->updated;
	dt->updated = 0;
	return draw;
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
