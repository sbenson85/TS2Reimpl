#include "game/game.h"
#include "game/vtable.h"
#include "ifaces/GscFrame.h"
#include "ifaces/GscObjectTable.h"
#include "ifaces/cITSPersistSystem.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "simantics/PrimGenericSimCall.h"
#include "simantics/PrimGenericSimCallMath.h"
#include "simantics/PrimGenericSimCallMisc.h"
#include "simantics/PrimGenericSimCallOriginalsB.h"
#include "simantics/PrimGenericSimCallRefs.h"
#include "simantics/PrimXCommon.h"

#include <cstring>
#include <vector>

using gscref::PlainRef;
using gscref::VBaseRef;
using primX::Call;

// GenericSimCall (opcode 0x01) cases, group K: 70 (a collection pass), 52 (the tile in front of an object) and 43 (can the object
// stand where it is).

namespace {
constexpr uint32_t kPropertyKeyType = 0xac598eac;
constexpr uint32_t kNoInterface = 0x1c;

inline float GameFloat(uint32_t rva)
{
    return *GameAddress<float>(rva);
}

// 0x549fb0 (__cdecl, one argument): the person with this id from the cached singleton's person list, null when there is none.
inline void *PersonOfSingleton(int32_t id)
{
    void *const singleton = GetCachedSingleton();
    if (!singleton) return nullptr;
    void *const people = Call<void *>(singleton, 10);
    if (!people) return nullptr;
    return Call<void *>(people, 16, id, 1u);
}

// 0x54ac40 (__cdecl, one argument): the object a person stands for (the singleton's object table, by the person's slot 15 value).
inline void *ObjectOfPerson(void *person)
{
    if (!person) return nullptr;
    const uint32_t handle = Call<uint32_t>(person, 15);
    void *const singleton = GetCachedSingleton();
    if (!singleton) return nullptr;
    void *const table = Call<void *>(singleton, 26);
    if (!table) return nullptr;
    return Call<void *>(table, 43, handle);
}
} // namespace

// Case 70 (result 1; 0 when the list refuses): the director makes a list object (class 0x31d1ad09, interface 0xf1d1b6bf) that is
// set to argument 0 - 1; its head (slot 10) answers slot 10 "refused" or not. The head's slot 22 names a person and so an object,
// whose property words 0x3d and 8 (or the person's own) name an entry of the entry table. With argument 1 nonzero the object must
// give the component holder (0xfa1c39f7 / 0x1a34f3f4) and the property resource {0xac598eac, <this slot 61 / slot 66>, 1, 0} must
// exist. Every key the head lists (slot 21) opens its resource (interface 0x8c1571d6; skipped for keys the entry already has, entry
// slot 12): with argument 1 it goes to the property resource (resource slot 22 with the head's slot 16 value and the key, into
// the property resource's slot 73), without it the key is added to the entry (slot 10) and counted. When the property resource
// was changed it is stored again (persist system slots 17 and 16) if it can be (slot 28, and for resources of interface
// 0x3b2271bb only while their slot 6 is not 2). The stack object gets the number of keys not added and, with the list's slot 7
// weight, the product in thousands in its arguments 1..3.
uint32_t GenericSimCall::Case70()
{
    PlainRef list;
    Call<void>(GetDirectorFrameworkObject(), 3, 0x31d1ad09u, 0xf1d1b6bfu, list.Reset());
    Call<void>(list.Get(), 3, static_cast<int32_t>(static_cast<int16_t>(Arg(0) - 1)));
    void *const head = Call<void *>(list.Get(), 10);
    if ((Call<uint32_t>(head, 10) & 0xff) != 0) return 0; // (lane Z4: ours asked one level too deep, head->slot 10->slot 10)
    void *const keyService = gscref::KeyService();
    void *const singleton = GetCachedSingleton();
    void *const person = PersonOfSingleton(Call<int16_t>(Call<void *>(list.Get(), 10), 22));
    void *const object = ObjectOfPerson(person);
    const bool collect = Arg(1) != 0;
    const bool addToEntry = !collect;
    bool changed = false;
    VBaseRef component;
    PlainRef property;
    auto *const persist = As<ifaces::cITSPersistSystem>(keyService);
    if (collect) {
        if (!object) return 0;
        if ((Call<uint32_t>(Call<void *>(object, 8), 9, gscref::kComponentIidA, gscref::kVirtualBaseIid, component.Reset()) & 0xff) == 0) {
            return 0;
        }
        void *const owner = Call<void *>(mSelf, 61);
        const Key16 key{{kPropertyKeyType, Call<uint32_t>(owner, 66), 1u, 0u}};
        (void)persist->GetResource(&key, gscref::kKeyLookupType, property.Reset(), 0, 0);
        if (!property.Get()) return 0;
    }
    int32_t entryId = 0;
    int32_t wordEight = 0;
    if (object) {
        entryId = Call<int16_t>(object, 10, 0x3du, 1u);
        wordEight = Call<int16_t>(object, 10, 8u, 1u);
    } else if (person) {
        entryId = Call<int16_t>(person, 16, 0x3du);
        wordEight = Call<int16_t>(person, 16, 8u);
    }
    void *const entries = Call<void *>(singleton, 9);
    void *const entry = Call<void *>(entries, 11, entryId);
    const uint32_t headValue = Call<uint32_t>(Call<void *>(list.Get(), 10), 16);
    (void)Call<uint32_t>(Call<void *>(list.Get(), 10), 15); // read, then only compared in dead code by the original
    (void)wordEight;
    std::vector<Key16> keys;
    Call<void>(Call<void *>(list.Get(), 10), 21, &keys);
    const int32_t count = static_cast<int32_t>(keys.size());
    int32_t added = 0;
    for (int32_t i = 0; i < count; ++i) {
        const Key16 *const key = &keys[static_cast<size_t>(i)];
        // ORIGINAL-BUG: the original asks the entry (slot 12) without checking that the entry table found one; a null entry crashes here
        // in both (difftest: tests/simantics_gsc_case70.cpp counts those worlds as "both crashed").
        const bool known = addToEntry && (Call<uint32_t>(entry, 12, key) & 0xff) != 0;
        PlainRef opened;
        if (known) continue;
        if ((persist->GetResource(key, 0x8c1571d6u, opened.Reset(), 0, 0) & 0xff) == 0) continue;
        if (collect) {
            Call<void>(property.Get(), 73, Call<uint32_t>(opened.Get(), 22, headValue, key));
            changed = true;
        }
        if (addToEntry && entry) {
            Call<void>(entry, 10, key);
            ++added;
        }
    }
    if (collect && property.Get() && changed) {
        const Key16 storeKey{{kPropertyKeyType, Call<uint32_t>(Call<void *>(mSelf, 61), 66), 1u, 0u}};
        bool storable = false;
        PlainRef sink;
        PlainRef stored;
        void *const primary = Call<void *>(keyService, 3);
        uint8_t scratch[16];
        const Key16 *const made = Call<const Key16 *>(property.Get(), 3, scratch);
        Call<uint32_t>(primary, 34, made, stored.Reset());
        if (!stored.Get()) {
            uint8_t scratch2[16];
            const Key16 *const made2 = Call<const Key16 *>(property.Get(), 3, scratch2);
            Call<uint32_t>(keyService, 34, made2, 0x656b8f0cu, stored.Reset());
        }
        if (stored.Get()) {
            if ((Call<uint32_t>(stored.Get(), 0, 0x3b2271bbu, sink.Reset()) & 0xff) != 0) {
                if ((Call<uint32_t>(keyService, 28, stored.Get()) & 0xff) != 0) storable = Call<int32_t>(sink.Get(), 6) != 2;
            } else {
                storable = (Call<uint32_t>(keyService, 28, stored.Get()) & 0xff) != 0;
            }
        }
        if (storable) {
            Call<void>(keyService, 17, &storeKey);
            Call<void>(keyService, 16, property.Get(), 0u);
        }
    }
    void *const target = As<ifaces::GscObjectTable>(Call<void *>(singleton, 26))->FindObject(As<ifaces::GscFrame>(mFrame)->GetStackObjectId());
    if (target) {
        const int32_t remaining = count - added;
        const int32_t weight = Call<int32_t>(list.Get(), 7);
        const int32_t product = weight * remaining;
        Call<void>(target, 20, 1u, static_cast<uint32_t>(static_cast<uint16_t>(remaining)));
        Call<void>(target, 20, 2u, static_cast<uint32_t>(product / 10000));
        Call<void>(target, 20, 3u, static_cast<uint32_t>(product % 10000));
    }
    return 1;
}
