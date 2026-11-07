#pragma once
#include <cstdint>

// Base COM-style interface of the GZ framework. Slot order is ABI: it must match the game.
class cIGZUnknown {
public:
    virtual bool QueryInterface(uint32_t riid, void **ppvObj) = 0; // slot 0
    virtual uint32_t AddRef() = 0;                                 // slot 1
    virtual uint32_t Release() = 0;                                // slot 2
};
