#include "kui.h"

int kui_rect_contains(KuiRect r, int px, int py)
{
	return px >= r.x && py >= r.y && px < r.x + r.w && py < r.y + r.h;
}

KuiRect kui_rect_union(KuiRect a, KuiRect b)
{
	if (a.w <= 0 || a.h <= 0) return b;
	if (b.w <= 0 || b.h <= 0) return a;
	int x0 = a.x < b.x ? a.x : b.x, y0 = a.y < b.y ? a.y : b.y;
	int x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
	int y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
	return (KuiRect){ x0, y0, x1 - x0, y1 - y0 };
}

KuiRect kui_grid_cell(KuiRect area, int cols, int rows, int col, int row, int gap)
{
	int cw = (area.w - gap * (cols + 1)) / cols;
	int ch = (area.h - gap * (rows + 1)) / rows;
	return (KuiRect){ area.x + gap + col * (cw + gap), area.y + gap + row * (ch + gap), cw, ch };
}
