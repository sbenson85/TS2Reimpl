#include "simantics/PrimDispatch.h"

#include "simantics/PrimAA.h"
#include "simantics/PrimGenericSimCall.h"
#include "ifaces/cTSObject.h"
#include "ifaces/cTSPerson.h"

#include "game/original.h"
#include "game/vtable.h"
#include "simantics/OperandReader.h"
#include "simantics/PrimHandlers.h"
#include "simantics/PrimMid.h"
#include "simantics/PrimPerson.h"
#include "simantics/PrimR.h"
#include "simantics/PrimS.h"
#include "simantics/PrimSetToNext.h"
#include "simantics/PrimT.h"
#include "simantics/PrimW.h"
#include "simantics/PrimX.h"

#include <cstring>

namespace {

template <typename R, typename... A> inline R Call(const void *obj, int slot, A... args)
{
    return VirtualSlot<R(__thiscall *)(void *, A...)>(obj, slot)(const_cast<void *>(obj), args...);
}

// ---- the handler table ----------------------------------------------------------------------------------------

template <typename T, uint32_t (T::*M)(void *, const PrimParamView *)> uint32_t Via(void *self, void *frame, void *param)
{
    return (static_cast<T *>(self)->*M)(frame, static_cast<const PrimParamView *>(param));
}

// The Expression handler returns a signed value; the dispatch stores eax as it is.
uint32_t ViaExpression(void *self, void *frame, void *param)
{
    return static_cast<uint32_t>(static_cast<ExpressionPrimitive *>(self)->Execute(frame, static_cast<ExpressionParam *>(param)));
}

uint32_t ViaSetToNext(void *self, void *frame, void *param)
{
    return static_cast<SetToNextObject *>(self)->SetToNext(frame, param);
}

// The handlers nobody has replaced yet: __thiscall (this, frame, param), ret 8.
using OriginalHandler = uint32_t(__thiscall *)(void *, void *, void *);


PrimHandlerTable MakeTable()
{
    PrimHandlerTable t;
    std::memset(&t, 0, sizeof(t));
    auto &h = t.mByOpcode;
    h[0x00] = Via<PrimMidObject, &PrimMidObject::Idle>;
    h[0x01] = Via<PrimGscObject, &PrimGscObject::GenericSimCall>;
    h[0x02] = ViaExpression;
    h[0x07] = Via<PrimObject, &PrimObject::Prim07>;
    h[0x08] = Via<PrimObject, &PrimObject::Random>;
    // 0x0a: no handler, the dispatch sets result 1 itself.
    h[0x0b] = Via<PrimMidObject, &PrimMidObject::DistanceTo>;
    h[0x0c] = Via<PrimMidObject, &PrimMidObject::DirectionTo>;
    h[0x0d] = Via<PrimRObject, &PrimRObject::Prim0D>;
    h[0x0e] = Via<PrimRObject, &PrimRObject::Prim0E>;
    h[0x0f] = Via<PrimObject, &PrimObject::Prim0F>;
    h[0x10] = Via<PrimRObject, &PrimRObject::Prim10>;
    h[0x12] = Via<PrimRObject, &PrimRObject::Prim12>;
    h[0x14] = Via<PrimMidObject, &PrimMidObject::Prim14>;
    h[0x17] = Via<PrimTObject, &PrimTObject::Prim17>;
    h[0x19] = Via<PrimMidObject, &PrimMidObject::Budget>;
    h[0x1a] = Via<PrimRObject, &PrimRObject::Prim1A>;
    h[0x1c] = Via<PrimRObject, &PrimRObject::Prim1C>;
    h[0x1f] = ViaSetToNext;
    h[0x20] = Via<PrimMidObject, &PrimMidObject::Prim20>;
    h[0x21] = Via<PrimMidObject, &PrimMidObject::Prim21>;
    h[0x22] = Via<PrimSObject, &PrimSObject::Prim22>;
    h[0x23] = Via<PrimMidObject, &PrimMidObject::Prim23>;
    h[0x24] = Via<PrimTObject, &PrimTObject::Prim24>;
    h[0x2a] = Via<PrimSObject, &PrimSObject::Prim2A>;
    h[0x2e] = Via<PrimXObject, &PrimXObject::Prim2E>;
    h[0x30] = Via<PrimMidObject, &PrimMidObject::Prim30>;
    h[0x31] = Via<PrimSObject, &PrimSObject::Prim31>;
    h[0x32] = Via<PrimSObject, &PrimSObject::Prim32>;
    h[0x33] = Via<PrimWObject, &PrimWObject::Prim33>;
    // 0x69, 0x6a, 0x6b, 0x6c, 0x6e, 0x6f: the shared case (forwarded through the interface 0x5a6216f6).
    h[0x6d] = Via<PrimAAObject, &PrimAAObject::Prim6D>;
    h[0x70] = Via<PrimAAObject, &PrimAAObject::Prim70>;
    h[0x71] = Via<PrimTObject, &PrimTObject::Prim71>;
    h[0x73] = Via<PrimXObject, &PrimXObject::Prim73>;
    h[0x76] = Via<PrimTObject, &PrimTObject::Prim76>;
    h[0x77] = Via<PrimSObject, &PrimSObject::Prim77>;
    h[0x7a] = Via<PrimMidObject, &PrimMidObject::OnTimer>;
    h[0x7b] = Via<PrimSObject, &PrimSObject::Prim7B>;
    h[0x7c] = Via<PrimSObject, &PrimSObject::Prim7C>;
    h[0x7e] = Via<PrimMidObject, &PrimMidObject::Prim7E>;
    return t;
}

// The reference the shared case keeps in its frame: released (slot 2) when the case throws or returns.
struct HeldInterface {
    void *mObject = nullptr;
    HeldInterface() = default;
    HeldInterface(const HeldInterface &) = delete;
    HeldInterface &operator=(const HeldInterface &) = delete;
    ~HeldInterface() noexcept
    {
        if (mObject) Call<uint32_t>(mObject, 2);
    }
};

constexpr uint32_t kIidForwarder = 0x5a6216f6;

} // namespace

const PrimHandlerTable &DefaultPrimHandlers()
{
    static const PrimHandlerTable table = MakeTable();
    return table;
}

// 0x534650. The EH frame covers only the interface reference (unwound by releasing it).
uint8_t DispatchPrimitive(void *self, void *frame, void *node, int32_t *result, const PrimHandlerTable &table)
{
    *result = -1;
    void *const param = Call<void *>(node, 16);
    const int32_t opcode = Call<int16_t>(node, 3);

    if (static_cast<uint32_t>(opcode) >= static_cast<uint32_t>(kPrimOpcodeCount)) return 0;

    switch (opcode) {
    case 0x0a:
        *result = 1;
        return 1;
    case 0x69:
    case 0x6a:
    case 0x6b:
    case 0x6c:
    case 0x6e:
    case 0x6f: {
        // Slot 104 says whether the object forwards; then the interface's owner chain leads to the object that
        // runs the primitive (slot 246 of it), unless that is this object or it declines (slot 64).
        if (As<ifaces::cTSObject>(self)->ForwardsPrimitives()) {
            HeldInterface held;
            Call<uint32_t>(self, 0, kIidForwarder, &held.mObject);
            void *const step = Call<void *>(held.mObject, 6);
            void *const target = Call<void *>(step, 11);
            if (target != self && Call<uint32_t>(target, 64) != 0) {
                *result = static_cast<int32_t>(Call<uint32_t>(target, 246, frame, node));
                void *const toRelease = held.mObject;
                held.mObject = nullptr;
                if (toRelease) Call<uint32_t>(toRelease, 2);
                return 1;
            }
        }
        // Declined: the opcode is asked for again and picks the error (0x92 for ChangeLight, else 0x47).
        const int16_t again = Call<int16_t>(node, 3);
        if (again == 0x6f) throw static_cast<short>(0x92);
        throw static_cast<short>(0x47);
    }
    default: {
        const PrimHandlerFn handler = table.mByOpcode[opcode];
        if (!handler) return 0;
        *result = static_cast<int32_t>(handler(self, frame, param));
        return 1;
    }
    }
}

uint8_t PrimDispatchObject::Dispatch(void *frame, void *node, int32_t *result)
{
    return DispatchPrimitive(this, frame, node, result, DefaultPrimHandlers());
}

// ---------------------------------------------------------------------------------------------------------
// cTSPerson::vf332
// ---------------------------------------------------------------------------------------------------------

namespace {

uint8_t BaseDispatch(void *self, void *frame, void *node, int32_t *result)
{
    return DispatchPrimitive(self, frame, node, result, DefaultPrimHandlers());
}

} // namespace

const PersonPrimHandlers &DefaultPersonPrimHandlers()
{
    static const PersonPrimHandlers handlers = {Via<PrimPersonObject, &PrimPersonObject::Prim03>,
                                                Via<PrimPersonObject, &PrimPersonObject::IdleForInput>,
                                                Via<PrimPersonObject, &PrimPersonObject::Prim1E0>};
    return handlers;
}

// 0x51e4c0 (no EH frame).
uint8_t DispatchPersonPrimitive(void *self, void *frame, void *node, int32_t *result, ObjectDispatchFn base,
                                const PersonPrimHandlers &handlers)
{
    *result = -1;
    void *const param = Call<void *>(node, 16);
    const int32_t opcode = Call<int16_t>(node, 3);

    switch (opcode) {
    case 0x03:
        *result = static_cast<int32_t>(handlers.mOp03(self, frame, param));
        return 1;
    case 0x11: // IdleForInput
        *result = static_cast<int32_t>(handlers.mOp11(self, frame, param));
        return 1;
    case 0x1d:
        *result = static_cast<int32_t>(As<ifaces::cTSPerson>(self)->SetMotiveDelta(frame, static_cast<const PrimParamView *>(param)));
        return 1;
    case 0x1e: {
        // The frame's slot 12 word must be non-zero, the param present and its selector 0..3: selector 0 runs
        // 0x525520, the others leave the result at -1.
        if (static_cast<int16_t>(Call<int16_t>(frame, 12)) == 0) throw static_cast<short>(0x15);
        if (!param) throw static_cast<short>(0x0a);
        const int32_t selector = static_cast<int16_t>(static_cast<const PrimParamView *>(param)->mWord4);
        if (static_cast<uint32_t>(selector) > 3) throw static_cast<short>(0x0d);
        if (selector == 0) *result = static_cast<int32_t>(handlers.mOp1E0(self, frame, param));
        return 1;
    }
    case 0x25:
        *result = static_cast<int32_t>(As<ifaces::cTSPerson>(self)->TestInteractingWith(frame, static_cast<const PrimParamView *>(param)));
        return 1;
    default:
        return base(self, frame, node, result);
    }
}

uint8_t PrimDispatchPerson::Dispatch(void *frame, void *node, int32_t *result)
{
    return DispatchPersonPrimitive(this, frame, node, result, BaseDispatch, DefaultPersonPrimHandlers());
}

// ---------------------------------------------------------------------------------------------------------
// cTSPerson::vf4_7
// ---------------------------------------------------------------------------------------------------------

namespace {

uint32_t BaseReader(void *s, int32_t owner, uint32_t id, SimDataRef *dest, uint32_t *floatDest, uint32_t *zeroed, uint32_t clearFlag,
                    uint32_t arg7)
{
    return static_cast<SimOperandSource *>(s)->Read(owner, id, dest, floatDest, zeroed, clearFlag, arg7);
}

// The object must report 2 from its slot 98 (the person kind) for any of the person's own owners.
void RequirePerson(void *object)
{
    if (As<ifaces::cTSObject>(object)->ObjectKind() != 2) throw static_cast<short>(0x1c);
}

} // namespace

// 0x51b870 (no EH frame). `s` is the cTSTreeSim subobject (object + 4).
uint32_t PersonReadOperand(void *s, int32_t ownerArg, uint32_t idArg, SimDataRef *destArg, uint32_t *floatDest, uint32_t *zeroed,
                           uint32_t clearFlag, [[maybe_unused]] uint32_t arg7, OperandReadFn base)
{
    uint8_t *const sub = static_cast<uint8_t *>(s);
    void *const object = sub - 4;

    SimDataRef local;
    SimDataRef *const d = destArg ? destArg : &local;
    d->Reset();
    if (floatDest) *floatDest = 0;
    if (zeroed) *zeroed = 0;

    const uint16_t idu = static_cast<uint16_t>(idArg);
    const int16_t id16 = static_cast<int16_t>(idArg);
    int slot = 0; // 53, 54 or 55 for the run-of-values owners
    int16_t *table = nullptr;

    switch (static_cast<int16_t>(ownerArg)) {
    case 0x0c:
        slot = 53;
        break;
    case 0x1d:
        slot = 54;
        break;
    case 0x1c:
        slot = 55;
        break;
    case 0x12: {
        RequirePerson(object);
        if (idu > 0xe6) throw static_cast<short>(0x21);
        table = Call<int16_t *>(sub + 0x2ec, 12);
        static_cast<OperandRef *>(d)->MakeFromBase(table, id16, 0xe7);
        break;
    }
    case 0x1e: {
        RequirePerson(object);
        if (idu > 7) throw static_cast<short>(2);
        const uint16_t index = static_cast<uint16_t>(As<ifaces::cTSObject>(object)->GetArgument(idArg));
        if (index > 0xe6) throw static_cast<short>(0x21);
        table = Call<int16_t *>(sub + 0x2ec, 12);
        static_cast<OperandRef *>(d)->MakeFromBase(table, static_cast<int16_t>(index), 0xe7);
        break;
    }
    default:
        return base(s, ownerArg, idArg, d, floatDest, zeroed, clearFlag, 0);
    }

    int16_t *run = nullptr;
    if (slot != 0) {
        RequirePerson(object);
        if (idu > 0xf) throw static_cast<short>(0x1a);
        const uint32_t extra = Call<uint32_t>(sub + 0x2ec, 20);
        void *const person = *reinterpret_cast<void *const *>(sub + 0x44);
        void *const holder = Call<void *>(person, 63);
        run = Call<int16_t *>(holder, slot, static_cast<int32_t>(id16), extra);
        d->mLength = run != nullptr ? 1 : 0;
        d->mIndex = 0;
        d->mPtr = run;
        d->mMin = INT16_MIN;
        d->mMax = INT16_MAX;
        if (!run) throw static_cast<short>(0x1a);
        if (slot == 55) {
            d->mMin = 0;
            d->mMax = 0x16;
        }
    }

    const uint32_t value = static_cast<uint16_t>(d->Get());

    if (slot == 55) {
        // The stored value must be 0..0x16; otherwise it is clamped in place and the read fails.
        if (*run < 0) {
            *run = 0;
            throw static_cast<short>(0x14);
        }
        if (*run > 0x16) {
            *run = 0x16;
            throw static_cast<short>(0x14);
        }
    }

    if (static_cast<uint8_t>(clearFlag) != 0) {
        d->Reset();
        if (floatDest) *floatDest = 0;
    }
    return value;
}

uint32_t PersonOperandSource::Read(int32_t owner, uint32_t id, SimDataRef *dest, uint32_t *floatDest, uint32_t *zeroed,
                                   uint32_t clearFlag, uint32_t arg7)
{
    return PersonReadOperand(this, owner, id, dest, floatDest, zeroed, clearFlag, arg7, BaseReader);
}
