#ifndef CGBA_TEST_GINT_TOUCH_H
#define CGBA_TEST_GINT_TOUCH_H

struct _touch_adraw;
struct _touch_adconv;

struct _touch_addots {
	int touches;
	int x1, y1, z1;
	int x2, y2, z2;
};

void touch_get_last_scan(struct _touch_adraw *raw,
	struct _touch_adconv *conv, struct _touch_addots *dots);

#endif
