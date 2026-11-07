#pragma once
// Shared by the TreeSim*.cpp files: the interface aliases and the few helpers the interpreter functions share.
#include "game/game.h"
#include "game/vtable.h"
#include "ifaces/GscContext.h"
#include "ifaces/GscGate.h"
#include "ifaces/GscObjectTable.h"
#include "ifaces/SimServiceC.h"
#include "ifaces/TreeOwner.h"
#include "ifaces/cTSBehaviorNode.h"
#include "ifaces/cTSBehaviorTree.h"
#include "ifaces/cTSObject.h"
#include "ifaces/cTSTreeSim.h"
#include "ifaces/cTSTreeStack.h"
#include "ifaces/cTSTreeStackElem.h"
#include "simantics/SimTraceLog.h"
#include "simantics/TreeHeld.h"
#include "simantics/TreeSim.h"

namespace treesim {

using Frame = ifaces::cTSTreeStackElem;
using Stack = ifaces::cTSTreeStack;
using Sim = ifaces::cTSTreeSim;
using Node = ifaces::cTSBehaviorNode;
using Tree = ifaces::cTSBehaviorTree;
using Owner = ifaces::TreeOwner;
using Gate = ifaces::GscGate;
using Context = ifaces::GscContext;

inline Sim *AsSim(cTSTreeSim *sim)
{
    return As<Sim>(sim);
}

inline Stack *AsStack(cTSTreeSim *sim)
{
    return As<Stack>(&sim->mStack);
}

// The fields of a frame, for the places the original reads them directly instead of through the vtable.
inline cTSTreeStackElem *Fields(Frame *frame)
{
    return reinterpret_cast<cTSTreeStackElem *>(frame);
}

// `throw (short)code`: the game's error type for everything but a missing frame.
[[noreturn]] inline void Raise(int16_t code)
{
    throw code;
}

// The top frame the way the interpreter asks for it when it must exist: the stack's count (virtual slot 10) less one,
// tested against the vector itself (signed below, unsigned above), then the frame's own Self (slot 41).
// A missing frame throws the 32-bit cTSTreeStack::kStackErrorNoFrame.
inline Frame *TopFrameChecked(cTSTreeSim *sim)
{
    const int32_t index = AsStack(sim)->Count() - 1;
    if (index <= -1 || static_cast<uint32_t>(index) >= sim->mStack.mFrames.size()) throw cTSTreeStack::kStackErrorNoFrame;
    return As<Frame>(sim->mStack.mFrames[static_cast<uint32_t>(index)].get())->Self();
}

// The error code a caught short is passed on with (the original pushes the dword its catch slot held; only the low
// half is ever the code).
inline uint32_t ErrorArgument(int16_t code)
{
    return static_cast<uint16_t>(code);
}

} // namespace treesim
