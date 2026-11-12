// cGZRect's intersection (lane 261108s). See src/gz/cGZRect.h.
#include "gz/cGZRect.h"

#include "gz/cIGZBuffer.h" // cGZRect

#include <algorithm>

bool GZRectIntersect(cGZRect *out, const cGZRect *a, const cGZRect *b)
{
    if (a->right > b->left && a->left < b->right && a->bottom > b->top && a->top < b->bottom) {
        out->left = std::max(a->left, b->left);
        out->top = std::max(a->top, b->top);
        out->right = std::min(a->right, b->right);
        out->bottom = std::min(a->bottom, b->bottom);
        return true;
    }
    out->left = 0;
    out->top = 0;
    out->right = 0;
    out->bottom = 0;
    return false;
}

bool __fastcall cGZRectAdapters::Intersect(cGZRect *out, void *, const cGZRect *a, const cGZRect *b)
{
    return GZRectIntersect(out, a, b);
}
