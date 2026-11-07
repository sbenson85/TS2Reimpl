#include "misc/UtilitiesB.h"

void *__fastcall UtilitiesB_SetInvalidIndex(void *self, void * /*unusedEdx*/)
{
    *static_cast<int32_t *>(self) = -1;
    return self;
}
