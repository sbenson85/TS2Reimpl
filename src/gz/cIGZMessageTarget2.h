#pragma once
#include "gz/cIGZUnknown.h"

// cIGZMessageTarget2: a receiver of cIGZMessage2 messages (one slot past cIGZUnknown). The framework has this face at
// +0x180 (cCTGFramework vtable 0xe995cc's third table), the application at +0x5c (cTSAppFW vtable 0xe9855c), and every
// other message target (cTSFamilyManager +8, ...) the same one slot. Lane EC (docs/subsystems/startup.md).
class cIGZMessageTarget2 : public cIGZUnknown {
public:
    // slot 3 (ret 4): true when the message was handled. cCTGFramework 0xc2d2ca, cTSAppFW 0xc27605.
    virtual bool DoMessage(void *message) = 0;
};
