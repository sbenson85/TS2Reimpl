#include "game/game.h"
#include "simantics/PrimGenericSimCallOriginals.h"
#include "rz/RZAutoRef.h"
#include "rz/ServiceGetters.h"
#include "rz/StaticGetters2.h"
#include "simantics/PrimGenericSimCall.h"
#include "simantics/PrimGenericSimCallRefs.h"
#include "simantics/PrimXCommon.h"

using primX::Call;
using gscref::KeyService;
using gscref::PackWords;
using gscref::PlainRef;
using gscref::QueryViaVirtualBase;
using gscref::ResourceService;
using gscref::VBaseRef;

// GenericSimCall (opcode 0x01) cases, group D: the cases that hold references through the object's interface 9 query (the
// "virtual base" holders: a reference to an object that is released through its virtual base) and the cases that look keys up
// in the cached services.

namespace {
constexpr uint32_t kDefaultResult = 1;

} // namespace

// Asks the object for interface (0xfa1c39f7, 0xfa1c39ed) through its slot 9, holding the answer in `ref`.
static void QueryGraphInterface(void *self, VBaseRef &ref)
{
    Call<void>(self, 9, 0xfa1c39f7u, 0xfa1c39edu, ref.Reset());
}

// Case 24 and 30: when the context's slot-3 object's slot 21 is below 4: asks the object for the interface above and, if that
// object answers the query 0x1a34f3f4 through its virtual base, calls its slot 6 (case 24) or slot 7 (case 30) with
// argument 0.
uint32_t GenericSimCall::Case24()
{
    void *const gate = ContextSlot(3);
    if (Call<int32_t>(gate, 21) >= 4) return kDefaultResult;
    const int32_t a0 = Arg(0);
    VBaseRef first;
    QueryGraphInterface(mSelf, first);
    if (first.Get()) {
        VBaseRef second;
        // ORIGINAL-BUG: the original calls the second interface's slot without checking that the answer filled the holder.
        if (QueryViaVirtualBase(first.Get(), 0x1a34f3f4u, second.Reset())) Call<void>(second.Get(), 6, a0);
    }
    return kDefaultResult;
}

uint32_t GenericSimCall::Case30()
{
    void *const gate = ContextSlot(3);
    if (Call<int32_t>(gate, 21) >= 4) return kDefaultResult;
    const int32_t a0 = Arg(0);
    VBaseRef first;
    QueryGraphInterface(mSelf, first);
    if (first.Get()) {
        VBaseRef second;
        // ORIGINAL-BUG: as in case 24, the second interface is not checked.
        if (QueryViaVirtualBase(first.Get(), 0x1a34f3f4u, second.Reset())) Call<void>(second.Get(), 7, a0);
    }
    return kDefaultResult;
}

// Case 47: result 0 when the object has no such interface (as case 24). Otherwise the interface is asked for 0x1a34f3f4
// through its virtual base (its answer is not looked at); with no object in the second holder throws 0x1c, else that object
// gets slot 1 (AddRef) once and the result is 1.
uint32_t GenericSimCall::Case47()
{
    VBaseRef first;
    QueryGraphInterface(mSelf, first);
    if (!first.Get()) return 0;
    VBaseRef second;
    (void)QueryViaVirtualBase(first.Get(), 0x1a34f3f4u, second.Reset());
    if (!second.Get()) Throw(0x1c);
    Call<uint32_t>(second.Get(), 1);
    return 1;
}

// Cases 48, 74, 75: argument 1 = the low word of the answer. When the context's slot 6 says nonzero and the context's slot-23
// object (call it G) exists, says nonzero to slot 9 and has the type 0xbea28c2 or 0x2c1805b0 (slot 5): the thing slot 10 of G
// returns names (slot 8) a key service entry (service 0xb390c0, slot 11); keys {first, entry, 2, 0} are looked up with
// slot 3 of the service's slot-3 object (id, a holder, 0, 0) until one hits, and the held object then gets `slot` with
// (argument 0, &answer). The answer starts as -1.
void GenericSimCall::LookupByKeys(const uint32_t *firstWords, int keyCount, uint32_t lookupId, int slot)
{
    const int32_t a0 = Arg(0);
    int32_t answer = -1;
    void *const ctx = Context();
    void *const g = Call<void *>(ctx, 23);
    if (Call<uint32_t>(Context(), 6) != 0 && g != nullptr && Call<uint32_t>(g, 9) != 0) {
        bool typeOk = Call<uint32_t>(g, 5) == 0x0bea28c2u;
        if (!typeOk) typeOk = Call<uint32_t>(g, 5) == 0x2c1805b0u;
        if (typeOk) {
            void *const thing = Call<void *>(g, 10);
            void *const service = KeyService();
            const uint32_t entry = Call<uint32_t>(service, 11, Call<uint32_t>(thing, 8));
            PlainRef held;
            Key16 keys[2];
            for (int i = 0; i < keyCount; ++i) keys[i] = Key16{{firstWords[i], entry, 2u, 0u}};
            bool hit = false;
            for (int i = 0; i < keyCount && !hit; ++i) {
                void *const lookup = Call<void *>(service, 3);
                hit = (Call<uint32_t>(lookup, 3, &keys[i], lookupId, held.Reset(), 0u, 0u) & 0xff) != 0;
            }
            if (hit) Call<uint32_t>(held.Get(), slot, a0, &answer);
        }
    }
    SetArg(1, static_cast<uint16_t>(answer));
}

uint32_t GenericSimCall::Case48()
{
    static const uint32_t kFirst[2] = {0x2db5c0f4u, 0x6e9c59cfu};
    LookupByKeys(kFirst, 2, 0x0db5c0c2u, 32);
    return kDefaultResult;
}

uint32_t GenericSimCall::Case74()
{
    static const uint32_t kFirst[1] = {0x6e9c59cfu};
    LookupByKeys(kFirst, 1, 0x4e9c59c7u, 25);
    return kDefaultResult;
}

uint32_t GenericSimCall::Case75()
{
    static const uint32_t kFirst[2] = {0x2db5c0f4u, 0x6e9c59cfu};
    LookupByKeys(kFirst, 2, 0x6db6f3ffu, 25);
    return kDefaultResult;
}

// Case 84: key {arg7 | arg6 << 16, arg5 | arg4 << 16, arg3 | arg2 << 16, 0} to slot 37 of the resource service (0xb39110) with
// 0xebcf3e20 and an empty holder (released afterwards).
uint32_t GenericSimCall::Case84()
{
    // Read in the original's order (see KeyedEntry): the last pair first, each pair low word first.
    const int16_t a3 = Arg(3);
    const int16_t a2 = Arg(2);
    const uint32_t third = PackWords(a3, a2);
    const int16_t a5 = Arg(5);
    const int16_t a4 = Arg(4);
    const uint32_t second = PackWords(a5, a4);
    const int16_t a7 = Arg(7);
    const int16_t a6 = Arg(6);
    const uint32_t first = PackWords(a7, a6);
    const Key16 key{{first, second, third, 0u}};
    void *const service = ResourceService();
    PlainRef held;
    // The branch result is the service's answer (1 when its low byte is nonzero, else 0); lane Z4: ours dropped it and always gave 1.
    return (Call<uint32_t>(service, 37, &key, 0xebcf3e20u, held.Reset()) & 0xff) != 0 ? 1u : 0u;
}

// Case 67: the person-list object for argument 0 (none throws 0x37) gives a value (slot 15) the object table turns into an
// object (slot 43; none throws 0x1c). That object's slot-8 object is asked (slot 9, ids 0xbc66baec / 0x3c66baf1) for an
// interface held for the call; when it answers nonzero to its slot 0, it gets slot 12 with a block of four words (the frame's
// slot 3, arguments 1, 2, 3).
uint32_t GenericSimCall::Case67()
{
    void *const person = Call<void *>(mL24, 16, static_cast<int32_t>(Arg(0)), 1u);
    if (!person) Throw(0x37);
    void *const target = Call<void *>(mL1c, 43, Call<uint32_t>(person, 15));
    if (!target) Throw(0x1c);
    VBaseRef held;
    uint32_t result = 0; // (the original sets 0 here; lane Z4: ours had left the default 1)
    void *const owner = Call<void *>(target, 8);
    if ((Call<uint32_t>(owner, 9, 0xbc66baecu, 0x3c66baf1u, held.Reset()) & 0xff) != 0) {
        if ((Call<uint32_t>(held.Get(), 0) & 0xff) != 0) {
            uint16_t block[4];
            block[0] = Call<uint16_t>(mFrame, 3);
            block[1] = static_cast<uint16_t>(Arg(1));
            block[2] = static_cast<uint16_t>(Arg(2));
            block[3] = static_cast<uint16_t>(Arg(3));
            if ((Call<uint32_t>(held.Get(), 12, block) & 0xff) != 0) result = 1; // the answer of the held object is the result
        }
    }
    return result;
}

// Case 76: argument 0 (sign-extended) is an id; the object for argument 1 (object table) is held and asked for the interface
// 0x5a69d3d5: slot 10 gives two words (0x3a and 0x41, with 1), which become two masks (the first from bands: 0x10 above 0x32, 8
// above 0x12, 4 above 0xf, 2 above 2, 1 above 1, 0x20 above 0, 0x40 for -1, else all ones; the second 1 for 1, 2 for 0, else all
// ones); slot 43 forces the first word to -1. Then the key {0xac598eac, <slot 11 of the cached slot-23 object>, id, 0xb0858c5f}
// is looked up in the key service (slot 5, id 0xac598ea8); the hit is dropped again unless its slot 22 value (when nonzero)
// intersects the first mask and then its slot 23 value (when nonzero) the second. Result 1 with a hit kept, else 0.
uint32_t GenericSimCall::Case76()
{
    const int32_t id = Arg(0);
    // The holder takes its reference in its constructor (an AddRef that throws leaves nothing to release, as in the original).
    PlainRef target(Call<void *>(mL1c, 29, static_cast<int32_t>(Arg(1))));
    PlainRef iface;
    int32_t mask22 = -1;
    int32_t mask23 = -1;
    if (target.Get() && (Call<uint32_t>(target.Get(), 0, 0x5a69d3d5u, iface.Reset()) & 0xff) != 0) {
        int16_t w54 = Call<int16_t>(iface.Get(), 10, 0x3au, 1u);
        const int16_t w210 = Call<int16_t>(iface.Get(), 10, 0x41u, 1u);
        if ((Call<uint32_t>(iface.Get(), 43) & 0xff) != 0) w54 = -1;
        if (w54 > 0x32) mask22 = 0x10;
        else if (w54 > 0x12) mask22 = 8;
        else if (w54 > 0xf) mask22 = 4;
        else if (w54 > 2) mask22 = 2;
        else if (w54 > 1) mask22 = 1;
        else if (w54 > 0) mask22 = 0x20;
        else if (w54 == -1) mask22 = 0x40;
        if (w210 == 1) mask23 = 1;
        else if (w210 == 0) mask23 = 2;
    }
    void *const service = KeyService();
    PlainRef found;
    uint32_t cached = 0;
    if (void *const slot23 = GetCachedSlot23()) cached = Call<uint32_t>(slot23, 11); // 0x54a380
    const Key16 key{{0xac598eacu, cached, static_cast<uint32_t>(id), 0xb0858c5fu}};
    if ((Call<uint32_t>(service, 5, &key, 0xac598ea8u, found.Reset(), 0u, 0u) & 0xff) != 0) {
        bool drop = false;
        if (Call<uint32_t>(found.Get(), 22) != 0) {
            if ((Call<uint32_t>(found.Get(), 22) & static_cast<uint32_t>(mask22)) == 0) drop = true;
        }
        if (!drop && Call<uint32_t>(found.Get(), 23) != 0) {
            if ((Call<uint32_t>(found.Get(), 23) & static_cast<uint32_t>(mask23)) == 0) drop = true;
        }
        if (drop) {
            // 0x57ca80 with null: clear the holder, releasing what it held.
            void *old = found.mRef.mObject;
            found.mRef.mObject = nullptr;
            if (old) Call<uint32_t>(old, 2);
        }
    }
    return found.Get() != nullptr ? 1 : 0;
}

// Case 41: asks the stack object (none throws 0xa; argument 0 is read first) for the interface (0xfa1c39f7, 0xfa1c39ed) through
// its slot 9 (none throws 0x1c) and that for 0x1a34f3f4 through its virtual base. If that answers, its slot 3 gets the GUID the
// string at game global 0x10171cc parses to (0x4f8eb0) and a flag (argument 0 == 1); if it does not answer and the holder is
// empty, throws 0x1c.
uint32_t GenericSimCall::Case41()
{
    const int32_t a0 = Arg(0);
    void *const obj = StackObject();
    VBaseRef first;
    QueryGraphInterface(obj, first);
    if (!first.Get()) Throw(0x1c);
    VBaseRef second;
    if (QueryViaVirtualBase(first.Get(), 0x1a34f3f4u, second.Reset())) {
        const uint8_t flag = a0 == 1 ? 1 : 0;
        alignas(16) uint8_t guid[16] = {};
        const char *const text = *GameAddress<const char *>(0x10171cc);
        gscorig::ParseGuid(guid, text);
        Call<void>(second.Get(), 3, guid, static_cast<uint32_t>(flag));
    } else if (!second.Get()) {
        Throw(0x1c);
    }
    return kDefaultResult;
}

// Case 42: the stack object (none throws 0xa) is asked for the interface 0x5a69d3d5 (none throws 0x1c) and for the interface of
// case 41's first query. When that exists and the check 0x54b2d0 on the stack object says no (al zero), the second query goes
// through its virtual base and the answer is released once more (slot 2).
uint32_t GenericSimCall::Case42()
{
    void *const obj = StackObject();
    PlainRef iface;
    Call<uint32_t>(obj, 0, 0x5a69d3d5u, iface.Reset());
    if (!iface.Get()) Throw(0x1c);
    const uint8_t check = gscorig::CheckStackObject(obj);
    VBaseRef graph;
    QueryGraphInterface(obj, graph);
    if (graph.Get() && check == 0) {
        VBaseRef second;
        if (QueryViaVirtualBase(graph.Get(), 0x1a34f3f4u, second.Reset())) Call<uint32_t>(second.Get(), 2);
    }
    return kDefaultResult;
}
