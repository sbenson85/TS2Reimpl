#pragma once
// The sound layer's callback interfaces the audio classes implement (lane IY). The sound renderer (the object the sound
// probe 0x9c5b7e returns) registers them: slot 15 / 16 add / remove an event handler, slot 30 (target, period) / 31 add /
// remove an update target; slot 7 / 11 post an event (id, four zero words) to the handlers; slot 8 posts a message.
#include "gz/cIGZUnknown.h"

#include <cstdint>

// cIGZSndEventHandler: one slot past cIGZUnknown. The event's slot 4 is its id.
class cIGZSndEventHandler : public cIGZUnknown {
public:
    virtual bool HandleEvent(cIGZUnknown *event) = 0; // slot 3 (ret 4): true when handled
};

// cIGZSndUpdateTarget (iid 0x5b34f0af): one slot past cIGZUnknown, called on the renderer's update period.
class cIGZSndUpdateTarget : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x5b34f0af;
    virtual void Update(uint32_t now) = 0; // slot 3 (ret 4; the original leaves eax as it is)
};
