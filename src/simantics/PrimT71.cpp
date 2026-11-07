#include "rz/RZMessageServer.h"
#include "simantics/SnapReport.h"
#include "game/game.h"
#include "game/vtable.h"
#include "ifaces/cTSObject.h"
#include "game/original.h"
#include "rz/RZMemory.h"
#include "rz/cRZMessage2Standard.h"
#include "simantics/PrimTCommon.h"

using namespace primT;

namespace {

// The snap-test state the handler keeps in its frame: built by 0x7f02f0, torn down by 0x7f04e0 whenever the
// handler leaves (also when it throws).
struct SnapState {
    uint8_t mBytes[0x20];
};

// The error report 0x7f0ca0 makes from the state (0x18 bytes): the error code first, an owned reference at +0x10
// that 0x416c90 (RZRefField<0x10>::ReleaseRef) drops, and the kind that 0x7f0c00 switches on.
struct SnapReport {
    uint32_t mCode;       // +0x00 (0x2ecf00 reads it)
    uint8_t mData[0x0c];  // +0x04
    void *mObject;        // +0x10 the owned reference
    uint32_t mKind;       // +0x14
};
static_assert(sizeof(SnapReport) == 0x18, "SnapReport layout");

struct StateGuard {
    SnapState *mState;
    explicit StateGuard(SnapState *state) : mState(state) {}
    StateGuard(const StateGuard &) = delete;
    ~StateGuard() { OriginalFunction<void(__thiscall *)(void *), 0x7f04e0>()(mState); }
};

struct ReportGuard {
    SnapReport *mReport;
    explicit ReportGuard(SnapReport *report) : mReport(report) {}
    ReportGuard(const ReportGuard &) = delete;
    ~ReportGuard() { reinterpret_cast<RZRefField<0x10> *>(mReport)->ReleaseRef(); }
};

// 0x7f0ca0 (__thiscall, ret 4): builds the report for the state.
inline void MakeReport(SnapState *state, SnapReport *report)
{
    reinterpret_cast<const ::SnapStateReports *>(state)->BuildReport(reinterpret_cast<::SnapErrorReport *>(report));
}

// The cached message server (0x9c5a89, __cdecl).
inline void *MessageServer()
{
    return GetCachedMessageServer();
}

// Posts a framework message (cRZMessage2Standard, 0x2c bytes) with `id` and `object` as its value.
void PostMessageTo(void *object, uint32_t id)
{
    auto *message = static_cast<uint8_t *>(RZMemory::Malloc(cRZMessage2Standard::kSize));
    if (message) {
        // The message constructor (0x1c033d): vtable, a zero count, a clear flag byte and the rest zero.
        *reinterpret_cast<const void **>(message) = &ts2_rva_da4064;
        *reinterpret_cast<uint32_t *>(message + 4) = 0;
        message[8] = 0;
        for (uint32_t offset = 0x0c; offset <= 0x28; offset += 4) *reinterpret_cast<uint32_t *>(message + offset) = 0;
    }
    if (message) Call<uint32_t>(message, 1); // AddRef
    Held holder;
    holder.mRef.mObject = message;
    Call<void>(message, 5, id);
    Call<void>(message, 12, object);
    void *server = MessageServer();
    Call<void>(server, 3, message);
}

constexpr uint32_t kInterfaceNamed = 0x5a69d3d5;

// The diagnostic walk over one object that offers the named interface: the original asks for its owner's name and
// id, formats them into a local string and throws the string away; the calls are made, the text is not.
void WalkNamed(void *named)
{
    void *owner = Call<void *>(named, 8);
    void *namer = Call<void *>(owner, 61);
    (void)Call<const char *>(namer, 8);
    void *owner2 = Call<void *>(named, 8);
    (void)Call<int16_t>(owner2, 47);
}

} // namespace

uint32_t PrimTObject::Prim71(void *frame, const PrimParamView *param)
{
    const uint8_t flags = Byte(param, 0xf);
    int32_t slotNumber;
    if ((flags & 1) != 0) slotNumber = static_cast<int16_t>(static_cast<uint32_t>(As<ifaces::cTSObject>(this)->GetArgument(0u)));
    else slotNumber = Byte(param, 0xa);
    Call<void>(this, 14, 0x34u, 0u);
    Call<void>(this, 14, 0x36u, 0u);

    void *table = Call<void *>(mContext, 26);
    const int16_t idA = Read(this, Byte(param, 7), Word(param, 8));
    void *objA = Call<void *>(table, 30, static_cast<int32_t>(idA));
    if (!objA) Raise(0x17);
    const int16_t idB = Read(this, Byte(param, 4), Word(param, 5));
    void *objB = Call<void *>(table, 30, static_cast<int32_t>(idB));
    if (!objB) Raise(0x17);

    SnapState state;
    OriginalFunction<void(__thiscall *)(void *), 0x7f02f0>()(&state);
    StateGuard stateGuard(&state);

    const bool snapped = OriginalFunction<uint8_t(__cdecl *)(void *, void *, int32_t, void *), 0x7e8aa0>()(objB, objA, slotNumber, &state) != 0;
    if (snapped) {
        if ((flags & 2) == 0) {
            Call<void>(objB, 174, objA, static_cast<uint32_t>(slotNumber), 0u);
            if ((flags & 4) != 0) {
                HeldVirtualBase query;
                query.mRef.ResetVirtualBase();
                Call<void>(objB, 9, 0xfa1c39f7u, 0xfa1c39edu, &query.mRef);
                if (query.mRef.mObject) Call<void>(query.mRef.mObject, 85);
            }
        }
        if (Call<uint32_t>(objB, 98) == 0x13) PostMessageTo(objB, 0xaf6129b0);
        return 1;
    }

    // The snap was refused. The context is always set here (it was used above); the original still tests it.
    if (mContext != nullptr && Call<uint8_t>(mContext, 31) != 0) {
        SnapReport report;
        MakeReport(&state, &report);
        ReportGuard reportGuard(&report);
        const uint32_t code = report.mCode; // 0x2ecf00

        Held entry; // the error's string table entry (0x7f0c00 fills it)
        const bool haveEntry = reinterpret_cast<::SnapErrorReport *>(&report)->LookupMessage(&entry.mRef.mObject) != 0;
        if (haveEntry && entry.mRef.mObject != nullptr && Call<uint32_t>(entry.mRef.mObject, 7) != 0) {
            // "SnapInto failed because : %s": the text (slot 5) is formatted into a local string that is dropped.
            (void)Call<const char *>(entry.mRef.mObject, 5);
        }

        Held namedA;
        Call<uint32_t>(objA, 0, kInterfaceNamed, &namedA.mRef.mObject);
        if (namedA.mRef.mObject) WalkNamed(namedA.mRef.mObject);

        if (Call<uint8_t>(objB, 72) != 0) {
            Held namedB;
            Held holder;
            const int16_t who = Call<int16_t>(objB, 73);
            holder.Assign(Call<void *>(table, 29, static_cast<int32_t>(who)));
            // ORIGINAL-BUG: a missing object (the table's slot 29 answering null) is queried without a check.
            Call<uint32_t>(holder.mRef.mObject, 0, kInterfaceNamed, &namedB.mRef.mObject);
            if (namedB.mRef.mObject) WalkNamed(namedB.mRef.mObject);
        }

        void *table2 = Call<void *>(mContext, 26);
        const int16_t stackId = Call<int16_t>(frame, 12);
        void *stackObject = Call<void *>(table2, 29, static_cast<int32_t>(stackId));
        if (stackObject) {
            void *namer = Call<void *>(stackObject, 61);
            (void)Call<const char *>(namer, 8);
            (void)Call<int16_t>(stackObject, 47);
        }
        Call<void>(this, 14, 0x34u, code + 2);
        return 0;
    }

    SnapReport report;
    MakeReport(&state, &report);
    ReportGuard reportGuard(&report);
    Call<void>(this, 14, 0x34u, report.mCode + 2);
    return 0;
}
