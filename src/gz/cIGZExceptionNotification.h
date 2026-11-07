#pragma once
#include "gz/cIGZUnknown.h"

#include <cstdint>

// cIGZExceptionNotification (interface id 0xa4b30150): asked before an exception is thrown, with where it comes from.
// Slot order is ABI (cRZExceptionNotification's vtable 0xda44d4). Implementers append their deleting destructor as
// slot 4.
class cIGZExceptionNotification : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xa4b30150;

    // Slot 3 (ret 8, al): true when the exception may be thrown.
    virtual bool Notify(const char *module, int32_t line) = 0;
};
