#include "simantics/PrimR.h"
#include "game/vtable.h"
#include "ifaces/cTSObject.h"

#include "game/original.h"
#include "rz/cRZIniFile.h"
#include "rz/cRZString.h"
#include "simantics/PrimRCommon.h"
#include "ts/cTSString.h"

using namespace primr;

// ---------------------------------------------------------------------------------------------------------
// Opcode 0x1a
// ---------------------------------------------------------------------------------------------------------

uint32_t PrimRObject::Prim1A(void * /*frame*/, const PrimParamView *param)
{
    // The two ids (zero-extended words; later widened with sign extension, as the original does).
    uint32_t idA = static_cast<uint16_t>(Read(this, Byte(param, 6), Word(param, 7), nullptr, nullptr));
    uint32_t idB = static_cast<uint16_t>(Read(this, Byte(param, 9), Word(param, 0xa), nullptr, nullptr));

    void *manager = Call<void *>(mContext, 10);
    const uint8_t flags = Byte(param, 5);
    void *target;

    if (flags & 2) {
        void *a = Call<void *>(manager, 16, static_cast<int32_t>(static_cast<int16_t>(idA)), 1);
        void *b = Call<void *>(manager, 16, static_cast<int32_t>(static_cast<int16_t>(idB)), 1);
        // ORIGINAL-BUG: when only one of the two lookups finds an object, the handler throws without handing that
        // one back to the manager (slot 19), as it does on the success path.
        if (!a || !b) Raise(0x1c);
        target = Call<void *>(a, 12);
        Call<void>(manager, 19, a);
        Call<void>(manager, 19, b);
    } else if (flags & 8) {
        Held first;
        void *table = Call<void *>(this, 63);
        first.Assign(Call<void *>(table, 29, static_cast<int32_t>(static_cast<int16_t>(idA))));
        if (!first.Get()) Raise(0x17);
        target = Call<void *>(first.Get(), 109);
    } else {
        Held first;
        Held second;
        void *table = Call<void *>(this, 63);
        first.Assign(Call<void *>(table, 29, static_cast<int32_t>(static_cast<int16_t>(idA))));
        table = Call<void *>(this, 63);
        second.Assign(Call<void *>(table, 29, static_cast<int32_t>(static_cast<int16_t>(idB))));
        if (!first.Get()) Raise(0x17);
        if (!second.Get()) Raise(0x17);
        if (Call<int32_t>(first.Get(), 98) == 2 && Call<int32_t>(second.Get(), 98) == 2) {
            void *firstOwner = Call<void *>(first.Get(), 61);
            void *firstObject = Call<void *>(manager, 17, Call<int32_t>(firstOwner, 28));
            void *secondOwner = Call<void *>(second.Get(), 61);
            void *secondObject = Call<void *>(manager, 17, Call<int32_t>(secondOwner, 28));
            idA = static_cast<uint16_t>(Call<int16_t>(firstObject, 8));
            idB = static_cast<uint16_t>(Call<int16_t>(secondObject, 8));
            target = Call<void *>(firstObject, 12);
            Call<void>(manager, 19, firstObject);
            Call<void>(manager, 19, secondObject);
        } else {
            target = Call<void *>(first.Get(), 109);
        }
        // `second`, then `first`, are released here
    }

    if (!target) Raise(0x68);

    const int32_t b = static_cast<int32_t>(static_cast<int16_t>(idB));
    const uint8_t limit = Byte(param, 4);
    if (Call<uint32_t>(target, 5, b) <= limit) {
        if (flags & 1) return 0;
        Call<void>(target, 6, static_cast<int32_t>(static_cast<int16_t>(idA)), b, static_cast<uint32_t>(limit) + 1);
        if (Call<uint32_t>(target, 5, b) <= limit) return 0;
    }

    SimDataRef dest;
    dest.mLength = 0;
    dest.mIndex = 0;
    dest.mPtr = nullptr;
    dest.mMin = -32768; // the dword at +8 is 0x7fff8000
    dest.mMax = 32767;
    const int16_t value = Read(this, Byte(param, 0xc), Word(param, 0xd), &dest, nullptr);

    if (flags & 4) {
        if (!Call<uint8_t>(target, 10, b, static_cast<uint32_t>(limit), static_cast<int32_t>(value))) Raise(0x14);
        return 1;
    }
    if (!dest.mPtr) Raise(0x13);
    const int16_t stored = Call<int16_t>(target, 9, b, static_cast<uint32_t>(limit));
    if (dest.mPtr && stored >= dest.mMin && stored <= dest.mMax) {
        *dest.mPtr = stored;
        return 1;
    }
    Raise(0x14);
}

// ---------------------------------------------------------------------------------------------------------
// Opcode 0x1c
// ---------------------------------------------------------------------------------------------------------

namespace {

// The object registry the handler looks object numbers up in: the object at this + 0x1c4 keeps a vector of
// object pointers (begin at +0x1a24, end at +0x1a28); the number is 1-based. Null when the number is out of range or
// the slot is empty.
void *RegistryElement(PrimRObject *self, void *frame)
{
    const int32_t number = Call<int16_t>(frame, 12);
    const uint8_t *registry = *reinterpret_cast<uint8_t *const *>(reinterpret_cast<const uint8_t *>(self) + 0x1c4);
    if (number <= 0) return nullptr;
    void *const *begin = *reinterpret_cast<void *const *const *>(registry + 0x1a24);
    void *const *end = *reinterpret_cast<void *const *const *>(registry + 0x1a28);
    const int32_t count = static_cast<int32_t>(reinterpret_cast<const uint8_t *>(end) - reinterpret_cast<const uint8_t *>(begin)) >> 2;
    if (static_cast<uint32_t>(number) > static_cast<uint32_t>(count)) return nullptr;
    return begin[number - 1];
}

} // namespace

uint32_t PrimRObject::Prim1C(void *frame, const PrimParamView *param)
{
    void *lookup = Call<void *>(mContext, 24);
    if (Call<int32_t>(frame, 18) == 1) {
        void *sub = &mSecondary;
        return Call<uint8_t>(sub, 29) != 0 ? 1 : 0;
    }
    void *keyOwner = Call<void *>(frame, 20);
    const uint32_t key = Call<uint32_t>(keyOwner, 38);
    void *source = Call<void *>(lookup, 14, key);

    LocalString name;
    const int32_t flags = static_cast<int16_t>(Word(param, 6));
    const uint32_t low = Byte(param, 8);
    const uint32_t stringId = static_cast<uint16_t>(((flags >> 6) & 1) ? (low | 0x100) : low);

    // The string comes from the string-table loader (cTSString::Construct, 0x1d0df0), which builds a 0x34-byte
    // cRZString-derived object in place: table 0x12f, the id as the entry, a group (the default or the owner's), flags 1.
    {
        alignas(4) uint8_t loadedBytes[0x34];
        cTSString *const loaded = reinterpret_cast<cTSString *>(loadedBytes);
        constexpr uint16_t kTable = 0x12f;
        constexpr uint8_t kLoadFlags = 1;
        cTSString *result;
        if (flags & 1) {
            result = loaded->Construct(kTable, static_cast<uint16_t>(stringId), 0x7fd46cd0, kLoadFlags, cTSString::kStringTableType);
        } else if ((flags >> 1) & 1) {
            void *owner = Call<void *>(source, 70);
            const uint32_t tableKey = Call<uint32_t>(owner, 11);
            result = loaded->Construct(kTable, static_cast<uint16_t>(stringId), tableKey, kLoadFlags, cTSString::kStringTableType);
        } else {
            void *owner = Call<void *>(source, 18);
            const uint32_t tableKey = Call<uint32_t>(owner, 11);
            result = loaded->Construct(kTable, static_cast<uint16_t>(stringId), tableKey, kLoadFlags, cTSString::kStringTableType);
        }
        const std::string &text = result->mString.mString;
        if (&name.mString.mString != &text) name.mString.mString.assign(text.data(), text.size());
        loaded->Destruct();
    }
    if (name.mString.mString.empty()) return 0;
    reinterpret_cast<cRZIniStringHelpers *>(&name.mString)->TrimTrailing();

    // Who runs the tree (caller), the object found in the registry (owner), and the object that resolves the string
    // (tree).
    void *caller;
    void *owner = nullptr;
    void *tree;
    bool throughInterface;
    switch (Byte(param, 9)) {
    case 0:
    case 2: {
        caller = this;
        void *e = RegistryElement(this, frame);
        if (!e) Raise(0x15);
        owner = Call<void *>(e, 280);
        if (!owner) Raise(0x15);
        tree = Call<void *>(owner, 89);
        throughInterface = Byte(param, 9) == 0;
        break;
    }
    case 1: {
        void *e = RegistryElement(this, frame);
        if (!e) Raise(0x15);
        owner = Call<void *>(e, 280);
        if (!owner) Raise(0x15);
        caller = owner;
        tree = Call<void *>(owner, 89);
        throughInterface = true;
        break;
    }
    case 3: {
        caller = this;
        if (void *e = RegistryElement(this, frame)) {
            caller = Call<void *>(e, 280);
            if (!caller) caller = this;
        }
        const int16_t high = As<ifaces::cTSObject>(this)->GetArgument(1u);
        const uint16_t lowWord = static_cast<uint16_t>(As<ifaces::cTSObject>(this)->GetArgument(0u));
        const uint32_t treeKey = (static_cast<uint32_t>(static_cast<int32_t>(high)) << 16) | lowWord;
        void *entry = Call<void *>(lookup, 14, treeKey);
        if (!entry) {
            const uint32_t fallbackKey = Call<uint32_t>(this, 312);
            entry = Call<void *>(lookup, 14, fallbackKey);
            Call<void>(this, 313, 0u);
            if (!entry) Raise(0x7a);
        }
        tree = Call<void *>(entry, 18);
        Call<void>(entry, 62, 1u);
        throughInterface = false;
        break;
    }
    default:
        Raise(0x2c);
    }
    if (!caller) Raise(0x15);
    if (Byte(param, 9) == 3 && !tree) Raise(0x15);

    const int32_t treeFlags = static_cast<int16_t>(Word(param, 6));
    const uint32_t treeId = static_cast<uint16_t>(Call<int16_t>(tree, 31, &name.mString, static_cast<uint32_t>((treeFlags >> 2) & 1),
                                                                static_cast<uint32_t>((treeFlags >> 3) & 1)));
    if (treeId == 0) return 0;

    uint16_t words[4] = {0, 0, 0, 0};
    uint16_t *args = words;
    if ((treeFlags >> 5) & 1) {
        uint32_t count = Call<uint8_t>(frame, 26);
        if (count > 4) count = 4;
        for (uint32_t i = 0; i < count; ++i) words[i] = static_cast<uint16_t>(Call<int16_t>(frame, 30, i));
    } else if ((treeFlags >> 4) & 1) {
        words[0] = static_cast<uint16_t>(Read(this, Byte(param, 0xa), Word(param, 0xb), nullptr, nullptr));
        words[1] = static_cast<uint16_t>(Read(this, Byte(param, 0xd), Word(param, 0xe), nullptr, nullptr));
        words[2] = static_cast<uint16_t>(Read(this, Byte(param, 0x10), Word(param, 0x11), nullptr, nullptr));
    } else {
        args = nullptr;
    }

    if (throughInterface) {
        Held ref;
        Call<uint8_t>(caller, 0, 0x99c7f5au, &ref.mRef.mObject);
        // ORIGINAL-BUG: the result of the interface query is ignored; an object without the interface leaves the
        // pointer null and the next line reads through it.
        uint8_t *iface = static_cast<uint8_t *>(ref.Get());
        void **ifaceVTable = *reinterpret_cast<void ***>(iface + 4);
        // Slot 47 is the plain object-id getter (the arguments the decompiler shows on it belong to the call below,
        // which the compiler pushed early).
        const uint16_t callerId = static_cast<uint16_t>(Call<int16_t>(caller, 47));
        const uint16_t ownerId = static_cast<uint16_t>(Call<int16_t>(owner, 47));
        using Start = uint8_t(__thiscall *)(void *, void *, uint32_t, uint32_t, uint32_t, uint32_t, void *, uint32_t, uint32_t);
        return reinterpret_cast<Start>(ifaceVTable[15])(iface + 4, tree, static_cast<uint32_t>(ownerId), treeId,
                                                        static_cast<uint32_t>(callerId), 4, args, 0, 0) != 0
                   ? 1
                   : 0;
    }

    using RunOwner = uint8_t(__thiscall *)(void *, void *, void *, uint32_t, uint32_t, uint32_t);
    using RunTree = uint8_t(__thiscall *)(void *, void *, void *, void *, uint32_t, uint32_t, uint32_t);
    uint8_t ok;
    if (caller == this) {
        if (owner) ok = VirtualCall<RunOwner, 248>(caller, owner, args, treeId, 0, 1);
        else ok = VirtualCall<RunTree, 247>(caller, caller, args, tree, treeId, 0, 1);
        if (!ok) return 0;
        Call<void>(frame, 19, 1u);
        return 3;
    }
    if (Byte(param, 9) != 3) return 0;
    ok = VirtualCall<RunTree, 247>(this, caller, args, tree, treeId, 0, 1);
    if (!ok) return 0;
    Call<void>(frame, 19, 1u);
    return 3;
}
