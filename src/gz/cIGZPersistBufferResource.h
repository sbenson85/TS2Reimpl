#pragma once
#include "gz/cIGZBuffer.h"
#include "gz/cIGZUnknown.h"

#include <cstdint>

// cIGZPersistBufferResource (interface id 0x06f1568e): the face of cGZPersistBufferResource (+0x1c) that hands out the
// image's buffer. For classes in the standalone shape (docs/standalone-shape.md) to derive from; callers of other objects
// keep using the generated ifaces::cIGZPersistBufferResource view (same slots). Lane NU. RVAs 1.18.0.187.
class cIGZPersistBufferResource : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x06f1568e;

    virtual cIGZBuffer *GetBuffer() = 0;                    // slot 3: the buffer, counted (null when there is none)
    virtual bool QueryBuffer(uint32_t iid, void **out) = 0; // slot 4: the buffer's QueryInterface (false without one)
    virtual void SetBuffer(cIGZBuffer *buffer) = 0;         // slot 5: counted; the old one is released
};
