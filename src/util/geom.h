#ifndef GEOM_H
#define GEOM_H

#include <stdbool.h>

typedef struct {
    int x;
    int y;
} NorthPoint;

typedef struct {
    int x;
    int y;
    int width;
    int height;
} NorthBox;

static inline bool north_box_contains_point(const NorthBox *box, int x, int y)
{
    if (!box)
        return false;
    return x >= box->x && x < box->x + box->width && y >= box->y && y < box->y + box->height;
}

static inline bool north_box_empty(const NorthBox *box)
{
    return !box || box->width <= 0 || box->height <= 0;
}

static inline bool north_box_intersection(NorthBox *dest, const NorthBox *a, const NorthBox *b)
{
    if (!dest || !a || !b)
        return false;

    int x1 = a->x > b->x ? a->x : b->x;
    int y1 = a->y > b->y ? a->y : b->y;
    int x2 = (a->x + a->width) < (b->x + b->width) ? (a->x + a->width) : (b->x + b->width);
    int y2 = (a->y + a->height) < (b->y + b->height) ? (a->y + a->height) : (b->y + b->height);

    if (x2 <= x1 || y2 <= y1) {
        dest->x = 0;
        dest->y = 0;
        dest->width = 0;
        dest->height = 0;
        return false;
    }

    dest->x = x1;
    dest->y = y1;
    dest->width = x2 - x1;
    dest->height = y2 - y1;
    return true;
}

#endif
