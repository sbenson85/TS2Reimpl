// cRZLock (lane FD): src/rz/cRZLock.h. The constructor, speed-built as the image's.
#include "rz/cRZLock.h"

#include <new>

cRZLock *cRZLock::Construct(void *lockable) { return ::new (this) cRZLock(lockable); }
