#include "ui/cHTMLElement.h"

bool HTMLRect::Contains(int32_t x, int32_t y) const
{
    return x >= left && y >= top && x < right && y < bottom;
}
