#pragma once
// Message helpers shared by the GenericSimCall case files: the cached message server and a message kept in a case's frame.
#include "game/game.h"
#include "simantics/PrimGenericSimCallOriginals.h"
#include "rz/RZMemory.h"
#include "rz/cRZMessage2Standard.h"
#include "simantics/PrimXCommon.h"

#include <cstdint>

namespace gscmsg {

// The cached message server (0x9c5a89, __cdecl); still an original call.
inline void *MessageServer()
{
    return gscorig::MessageServer();
}

// A message kept in the case's frame: allocated (0x2c bytes) and constructed the way 0x1c033d does (vtable, zero count, clear
// flag byte, the other fields zero), referenced once (slot 1) when the allocation succeeded, released (slot 2) when the case
// returns or throws. mMessage is null when the allocation failed; the cases then call through it anyway, as the original does.
struct MessageRef {
    void *mMessage = nullptr;
    MessageRef()
    {
        auto *message = static_cast<uint8_t *>(RZMemory::Malloc(cRZMessage2Standard::kSize));
        if (message) {
            *reinterpret_cast<const void **>(message) = GameAddress(cRZMessage2Standard::kVTable);
            *reinterpret_cast<uint32_t *>(message + 4) = 0;
            message[8] = 0;
            for (uint32_t offset = 0x0c; offset <= 0x28; offset += 4) *reinterpret_cast<uint32_t *>(message + offset) = 0;
        }
        mMessage = message;
        if (mMessage) primX::Call<uint32_t>(mMessage, 1); // AddRef
    }
    MessageRef(const MessageRef &) = delete;
    MessageRef &operator=(const MessageRef &) = delete;
    ~MessageRef() noexcept
    {
        if (mMessage) primX::Call<uint32_t>(mMessage, 2); // Release
    }
    void SetId(uint32_t id) { primX::Call<void>(mMessage, 5, id); }
};

} // namespace gscmsg
