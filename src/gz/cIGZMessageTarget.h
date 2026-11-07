#pragma once
#include "gz/cIGZUnknown.h"

#include <cstdint>

// The framework's first (old-style) message: 16 bytes, passed by pointer. mId is the notification key the message
// server (src/gz/cGZMessageServer.h) looks the targets up by (0 reaches no one); the other three words are the message's
// data (+8: the window, for the main toolbar's messages; the IME server's {id, data1, data2, data3}). The message server
// copies all 16 bytes into its queue and its general posts.
struct cGZMessage {
    uint32_t mId;     // +0x0
    uint32_t mWord4;  // +0x4
    void *mObject;    // +0x8
    uint32_t mWordC;  // +0xc
};
static_assert(sizeof(cGZMessage) == 0x10, "cGZMessage: an id and three data words");

// cIGZMessageTarget (iid 0xe98b2f57): a receiver of cGZMessage messages, one slot past cIGZUnknown. The message server
// AddRefs a target while it is registered (and around each delivery), not while a general post for it waits.
class cIGZMessageTarget : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xe98b2f57;
    virtual bool DoMessage(cGZMessage *message) = 0; // slot 3 (ret 4): true when handled (the server ignores it)
};
