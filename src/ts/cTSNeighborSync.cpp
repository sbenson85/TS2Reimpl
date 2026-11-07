// The person / neighbour sync table and the two slots of cTSNeighborhood that copy a sim's data between its person object and
// its neighbour record (CopyNeighborToPerson, slot 28; CopyPersonToNeighbor, slot 29; GetSyncTableEntry, slot 30).
#include "ts/cTSNeighborhood.h"
#include "game/vtable.h"
#include "ifaces/TokenObjectInfo.h"
#include "ifaces/TokenWantFearService.h"
#include "ifaces/cITSGlobals.h"
#include "ifaces/cITSNeighbor.h"
#include "ifaces/cITSNeighborManager.h"
#include "ifaces/cITSPerson.h"
#include "ifaces/cTSObject.h"
#include "rz/Singleton_20ddfb.h"

namespace {
// The table's entries (the game builds the same list of 169 words the first time it is asked for): the low 16 bits are the
// person data field and neighbour attribute index, the high 16 bits a category number.
constexpr uint32_t kSyncEntries[] = {
    0x10002, 0x10003, 0x10004, 0x10005, 0x10006, 0x10007, 0x10046, 0x10009, 0x1000a, 0x1000b, 0x1000c, 0x1000d,
    0x1000e, 0x1000f, 0x10010, 0x10011, 0x10012, 0x1003a, 0x1003c, 0x1003d, 0x20038, 0x20039, 0x2003f, 0x30041,
    0x40043, 0x40044, 0x50045, 0x6004a, 0x70051, 0x70052, 0x80053, 0x80054, 0x80055, 0x80056, 0x80057, 0x90058,
    0xa0059, 0xa005a, 0xb005b, 0xc005d, 0xc005e, 0xc005f, 0xc0060, 0xc0061, 0xc0062, 0xc0063, 0xc0065, 0xc0064,
    0x2c006a, 0xf006b, 0xf006c, 0x10006d, 0x10006e, 0x10006f, 0x110070, 0x110071, 0x110072, 0x110073, 0x110074,
    0x110075, 0x120076, 0x130077, 0x130078, 0x140079, 0x15007a, 0x16007b, 0x17007c, 0x17007d, 0x17007e, 0x17007f,
    0x170080, 0x170081, 0x170082, 0x170083, 0x170084, 0x170085, 0x170086, 0x170087, 0x170088, 0x170089, 0x17008a,
    0x17008b, 0x17008c, 0x17008d, 0x17008e, 0x17008f, 0x180090, 0x180091, 0x180092, 0x180093, 0x180094, 0x180095,
    0x180096, 0x180097, 0x180098, 0x180099, 0x18009b, 0x18009c, 0x1b009f, 0x18002e, 0x18002f, 0x180030, 0x180031,
    0x180032, 0x180033, 0x60016, 0x60017, 0x1e00a5, 0x1c00a2, 0x1c00a0, 0x1c00a1, 0x2000a7, 0x2000a8, 0x2000a9,
    0x1d005c, 0x2100aa, 0x2100ab, 0x2100ac, 0x2100ad, 0x2100ae, 0x2100af, 0x2100b0, 0x2100b1, 0x2100b2, 0x2400b4,
    0x2400b5, 0x2400b6, 0x2400b7, 0x2400b8, 0x2400b9, 0x2500ba, 0x2500bc, 0x2700c0, 0x2a00c3, 0x2a00c4, 0x2a00c5,
    0x2a00c6, 0x2b00c7, 0x2f0014, 0x2d00c8, 0x2e00c9, 0x2e00ca, 0x2e00cb, 0x3000cc, 0x3000cd, 0x3000ce, 0x3000cf,
    0x3000d0, 0x3000d1, 0x3000d2, 0x3000d3, 0x3000d4, 0x3000d5, 0x3000d6, 0x3000d7, 0x3100d8, 0x3100d9, 0x3100da,
    0x3200db, 0x3200dc, 0x3200dd, 0x3200de, 0x3200df, 0x3200e0, 0x3200e1, 0x3300e2, 0x3300e3, 0x3400e4, 0x3600e6,
};

// The table as the game's callers see it: the first and last entry and the end of the storage (a std::vector<uint32_t>'s
// three pointers).
constexpr NeighborSyncTable kSyncTable = {kSyncEntries, kSyncEntries + sizeof(kSyncEntries) / sizeof(kSyncEntries[0]),
                                          kSyncEntries + sizeof(kSyncEntries) / sizeof(kSyncEntries[0])};

// The person data field index of a table entry: the low half, sign-extended.
int32_t EntryField(uint32_t entry) { return static_cast<int16_t>(entry); }
} // namespace

const NeighborSyncTable *__cdecl GetNeighborSyncTable() { return &kSyncTable; }

uint32_t __cdecl GetNeighborSyncCount()
{
    const NeighborSyncTable *table = GetNeighborSyncTable();
    return static_cast<uint32_t>(table->mLast - table->mFirst);
}

uint8_t __cdecl GetNeighborSyncEntry(int32_t index, uint16_t *field, uint16_t *extra)
{
    const NeighborSyncTable *table = GetNeighborSyncTable();
    if (index > 0 && static_cast<uint32_t>(index) < static_cast<uint32_t>(table->mLast - table->mFirst)) {
        *field = static_cast<uint16_t>(table->mFirst[index]);
        *extra = static_cast<uint16_t>(table->mFirst[index] >> 16);
        return 1;
    }
    *field = 0;
    *extra = 0;
    return 0;
}

uint32_t cTSNeighborhood::GetSyncTableCount() { return GetNeighborSyncCount(); }

uint8_t cTSNeighborhood::GetSyncTableEntry(int32_t index, uint16_t *field, uint16_t *extra)
{
    return GetNeighborSyncEntry(index, field, extra);
}

namespace {
constexpr uint32_t kPersonDataExtra = 0x3d;      // the entry written after the table's loop
constexpr uint32_t kNewNeighborField = 0x3a;     // the person data a freshly made neighbour record starts with
constexpr uint32_t kNewNeighborValue = 0x13;
} // namespace

void cTSNeighborhood::CopyNeighborToPerson(ifaces::cITSPerson *person)
{
    auto *app = As<ifaces::cITSGlobals>(GetCachedSingleton());
    auto *neighbors = app->GetNeighborManager();
    ifaces::cITSNeighbor *neighbor = neighbors->FindNeighborByKey(person->GetSimObject()->GetObjectInfo()->GetDataId());
    person->SetNeighborId(0);
    if (!neighbor) {
        ifaces::cITSNeighbor *created = neighbors->CreateNeighbor(person->GetSimObject()->GetObjectInfo());
        if (!created) return;
        person->SetPersonData(kNewNeighborField, kNewNeighborValue, 1);
        person->SetNeighborId(created->GetId());
        return;
    }
    person->SetNeighborId(neighbor->GetId());
    const NeighborSyncTable *table = GetNeighborSyncTable();
    for (const uint32_t *entry = table->mFirst; entry != table->mLast; ++entry)
        person->SetPersonData(EntryField(*entry), neighbor->GetAttribute(EntryField(*entry)), 1);
    person->SetPersonData(kPersonDataExtra, neighbor->GetAttribute(kPersonDataExtra), 1);
    person->SetExtraWord(1, neighbor->GetExtraWord(1));
    person->SetExtraWord(2, neighbor->GetExtraWord(2));
    app->GetWantFearService()->RestoreSimWants(person->GetSimRow());
}

void cTSNeighborhood::CopyPersonToNeighbor(ifaces::cITSPerson *person)
{
    auto *app = As<ifaces::cITSGlobals>(GetCachedSingleton());
    ifaces::cITSNeighbor *neighbor = app->GetNeighborManager()->GetNeighbor(static_cast<int16_t>(person->GetSimRow()), 1);
    if (!neighbor) return;
    const NeighborSyncTable *table = GetNeighborSyncTable();
    for (const uint32_t *entry = table->mFirst; entry != table->mLast; ++entry)
        neighbor->SetAttribute(EntryField(*entry), person->GetPersonData(EntryField(*entry), 1));
    neighbor->SetExtraWord(1, person->GetExtraWord(1));
    neighbor->SetExtraWord(2, person->GetExtraWord(2));
    app->GetWantFearService()->StoreSimWants(person->GetSimRow());
}

// ---- adapters (the RVA hooks; src/hook/tables/cTSNeighborhood.cpp): next to the bodies they inline ----

using Adapters = cTSNeighborhoodAdapters;

uint8_t __fastcall Adapters::GetSyncTableEntry(Self *self, void *, int32_t index, uint16_t *field, uint16_t *extra)
{
    [[msvc::forceinline_calls]] return self->Self::GetSyncTableEntry(index, field, extra);
}

void __fastcall Adapters::CopyNeighborToPerson(Self *self, void *, ifaces::cITSPerson *person)
{
    [[msvc::forceinline_calls]] self->Self::CopyNeighborToPerson(person);
}

void __fastcall Adapters::CopyPersonToNeighbor(Self *self, void *, ifaces::cITSPerson *person)
{
    [[msvc::forceinline_calls]] self->Self::CopyPersonToNeighbor(person);
}
