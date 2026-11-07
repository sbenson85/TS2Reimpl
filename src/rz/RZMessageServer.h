#pragma once
#include "game/owned.h"

// The cached message server (lane RW): the framework's service 0x04fa845b / 0x652294c7, kept in the cell 0x10f519c
// without a reference of ours, as the game's getter at 0x9c5a89 keeps it. The one owner of that getter: every caller
// of ours takes it from here, and the hook (src/hook/tables/RZMessageServer.cpp) puts it in place of the image's.
// RVAs are 1.18.0.187.

// The cache cell: the image's word in the hooked game (the image's getter is the only other user), owned_statics.cpp's
// otherwise.
TS2_OWNED_STATIC(void *, 10f519c);

// 0x9c5a89 (__cdecl, no arguments, 43 bytes). When the cell is empty: asks the framework (ServiceHolder::QueryFromService)
// for the service, stores what it gave (null too: nothing is remembered about a failure) and releases the holder's
// reference (RZAutoRef::ReleaseObject, a C++ exception from Release is swallowed). Returns the cell, read again.
void *GetCachedMessageServer();
