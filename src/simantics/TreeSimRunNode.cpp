#include "simantics/TreeSimRunNode.h"

#include "game/game.h"
#include "game/vtable.h"
#include "ifaces/GscGate.h"
#include "ifaces/cIGZPropertySetString.h"
#include "ifaces/SimServiceHub.h"
#include "ifaces/cTSBehaviorNode.h"
#include "ifaces/cTSObject.h"
#include "ifaces/cTSTreeSim.h"
#include "rz/Singleton_20ddfb.h"
#include "simantics/PrimR.h"

#include <string>

// cTSObject::vf246 (0x53e560, slot 6 of the sim subobject through the thunk 0x52184d): executes one node. Written from
// the disassembly; the calls are made in the original's order.

namespace {

constexpr uint32_t kGateConfigWord = 0x35;     // the gate word the editor's configuration flags live in
constexpr uint32_t kOpcodeRunTreeByName = 0x1c; // the only opcode with no dispatch entry that RunNode still handles
constexpr int16_t kErrorUnknownOpcode = 5;
constexpr uint32_t kNotifyFirstId = 0xac1887e, kNotifySecondId = 0x4bb0d269;

// The value argument of the property notification: a cGZAny<AnyUint32> object (a vtable and the number).
struct AnyUint32Arg {
    const void *mVTable;
    int32_t mValue;
};

constexpr uint32_t kAnyUint32VTable = 0xd9f534;
constexpr uint32_t kAnyBaseVTable = 0xd9c654;

// The object that holds a reference from the service until it goes out of scope; the release swallows C++ exceptions
// like the game's ScopedRef holder does.
struct HeldService {
    ScopedRef mRef{nullptr};
    ~HeldService() { mRef.ReleaseObject(); }
};

} // namespace

int32_t TreeRunNodeObject::RunNode(void *frame, void *node)
{
    using Sim = ifaces::cTSTreeSim;
    using Object = ifaces::cTSObject;
    int32_t result = -1;
    ifaces::cTSBehaviorNode *nodeIface = As<ifaces::cTSBehaviorNode>(node);
    const uint8_t *param = nodeIface->GetParam();
    ifaces::GscGate *gate = As<ifaces::GscGate>(mGate);
    const uint16_t configBefore = static_cast<uint16_t>(gate->GetWord(kGateConfigWord));

    Sim *sim = As<Sim>(reinterpret_cast<uint8_t *>(this) + 4);
    Object *object = As<Object>(this);
    uint8_t handled = sim->RunEdithNode(frame, node, &result);
    if (!handled) handled = object->RunPrimitive(frame, node, &result);
    sim->AfterEdithNode(frame, node);
    object->AfterPrimitive();
    if (!handled) {
        // Nothing handles the node: only "run tree by name" is left.
        if (nodeIface->GetOpcode() != kOpcodeRunTreeByName) throw kErrorUnknownOpcode;
        result = static_cast<int32_t>(reinterpret_cast<PrimRObject *>(this)->Prim1C(frame, reinterpret_cast<const PrimParamView *>(param)));
    }

    if (static_cast<uint16_t>(gate->GetWord(kGateConfigWord)) != configBefore) {
        // The configuration flags changed while the node ran: publish them.
        HeldService held;
        if (void *hub = GetServiceObject()) As<ifaces::SimServiceHub>(hub)->GetSystemService(kNotifyFirstId, kNotifySecondId, &held.mRef.mObject);
        std::string name("edithConfigFlags");
        AnyUint32Arg value{GameAddress<void>(kAnyUint32VTable), static_cast<int16_t>(gate->GetWord(kGateConfigWord))};
        // ORIGINAL-BUG: the interface the hub gave is used without a null test.
        As<ifaces::cIGZPropertySetString>(held.mRef.mObject)->SetProperty(&name, &value);
        value.mVTable = GameAddress<void>(kAnyBaseVTable);
    }
    return result;
}
