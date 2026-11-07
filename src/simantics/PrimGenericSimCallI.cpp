#include "game/game.h"
#include "game/vtable.h"
#include "ifaces/GscContext.h"
#include "ifaces/GscFrame.h"
#include "ifaces/GscObjectTable.h"
#include "ifaces/cITSPersistSystem.h"
#include "rz/StaticGetters2.h"
#include "rz/Singleton_20ddfb.h"
#include "simantics/PrimGenericSimCall.h"
#include "simantics/PrimGenericSimCallMath.h"
#include "simantics/PrimGenericSimCallMisc.h"
#include "simantics/PrimGenericSimCallOriginalsB.h"
#include "simantics/PrimGenericSimCallRefs.h"
#include "simantics/PrimXCommon.h"

#include <vector>

using gscmisc::RefRow;
using gscref::PlainRef;
using gscref::VBaseRef;
using primX::Call;

// GenericSimCall (opcode 0x01) cases, group I: 81 (outfit rows), 70 (a collection pass over the sim's belongings), 52 (the tile in
// front of an object) and 43 (can the object stand where it is).

namespace {
constexpr uint32_t kPropertyKeyType = 0xac598eac;
constexpr uint32_t kNoInterface = 0x1c;

inline ifaces::GscContext *Ctx(void *context)
{
    return As<ifaces::GscContext>(context);
}

inline ifaces::GscFrame *FrameOf(void *frame)
{
    return As<ifaces::GscFrame>(frame);
}

inline float GameFloat(uint32_t rva)
{
    return *GameAddress<float>(rva);
}
} // namespace

// Case 81 (always 0): the object of argument 0 answers the attribute interface; the "outfit table" resource {0x6c4f359d,
// 0x4f184aa9, 0xb343967f, 0} (kind 0x10, slot 12) is listed by the text service (slot 26) as rows of ten references; the entries
// of rows argument 1 and argument 2 (unless -1), column 2, each give a key (slot 3) whose resource (interface 0xc1fe23e, kind
// 8 on its slot 4) is handed to the entry the sim's word 0x3d names (slot 10).
uint32_t GenericSimCall::Case81()
{
    void *const textService = CachedServiceGetter<0x10cc8ec, 0x4cf22ab5, 0x2cf22a9b>();
    void *const keyService = gscref::KeyService();
    PlainRef object(As<ifaces::GscObjectTable>(mL1c)->FindObject(Arg(0)));
    PlainRef iface;
    if (!textService || !keyService || !object.Get()) return 0;
    if ((Call<uint32_t>(object.Get(), 0, gscref::kAttributeIid, iface.Reset()) & 0xff) == 0) return 0;
    PlainRef table;
    const Key16 tableKey{{0x6c4f359du, 0x4f184aa9u, 0xb343967fu, 0u}};
    auto *const persist = As<ifaces::cITSPersistSystem>(keyService);
    if ((persist->GetResource(&tableKey, 0x6c4f3599u, table.Reset(), 0, 0) & 0xff) == 0) return 0;
    if (Call<int32_t>(table.Get(), 12) != 0x10) return 0;
    PlainRef resource;
    std::vector<RefRow> rows;
    Call<void>(CachedServiceGetter<0x10cc8ec, 0x4cf22ab5, 0x2cf22a9b>(), 26, table.Get(), &rows, static_cast<uint32_t>(-1),
               static_cast<uint32_t>(-1), 1u, 0u, 0u, 0u, 0u, static_cast<uint32_t>(-2));
    const int32_t rowA = Arg(1);
    const int32_t rowB = Arg(2);
    Key16 key{};
    // Hands the resource that column 2 of `row` points to over to the entry of the sim's word 0x3d.
    auto apply = [&](RefRow &row) {
        void *const holder = row.mRefs[2];
        if (!holder) return;
        PlainRef typed;
        Call<void>(holder, 0, 0xc1fe23eu, typed.Reset());
        if (!typed.Get() || Call<int32_t>(typed.Get(), 4) != 8) return;
        uint8_t scratch[16];
        const uint32_t *const made = Call<const uint32_t *>(holder, 3, scratch);
        key.w[0] = made[0];
        key.w[1] = made[1];
        key.w[2] = made[2];
        key.w[3] = made[3];
        if ((persist->GetResource(&key, 0xc1fe23eu, resource.Reset(), 0, 0) & 0xff) == 0) return;
        void *const singleton = GetCachedSingleton();
        void *const entries = Call<void *>(singleton, 9);
        void *const entry = Call<void *>(entries, 11, static_cast<int32_t>(Call<int16_t>(iface.Get(), 10, 0x3du, 1u)));
        if (entry) Call<void>(entry, 10, &key);
    };
    // ORIGINAL-BUG: the element at the row index is read without a bounds check, as in the original.
    apply(rows.data()[rowA]);
    if (rowB != -1) apply(rows.data()[rowB]);
    return 0;
}

