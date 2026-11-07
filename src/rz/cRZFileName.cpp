// The speed-profile member of cRZFileName (lane HQ); the rest is cRZFileName.size.cpp.
#include "rz/cRZFileName.h"

// 0x1c7f30
cRZFileName *cRZFileName::ConstructEmpty()
{
    mDrive.Construct();
    mDir.Construct();
    mName.Construct();
    mExt.Construct();
    mFull.Construct();
    return this;
}
