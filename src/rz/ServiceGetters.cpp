#include "rz/ServiceGetters.h"

#include "rz/RZAutoRef.h"

void ReleaseHeldObject(void *object)
{
    // The original releases its local holder with 0x1466e0.
    RZAutoRef holder{object};
    holder.ReleaseObject();
}
