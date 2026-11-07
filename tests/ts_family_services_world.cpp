// The service fakes of the cTSFamily difftests (see ts_family_services.h).
#include "game_allocator.h"
#include "ts_family_services.h"

#include "game/game.h"
#include "rz/GeneralAllocator.h"

#include <utility>

namespace tsfam::svc {
namespace {
SObj gFakes[kFakeCount];
void *gDirectorVT[16];
void *gTable[kSlots][7];

uint32_t Dispatch(SObj *self, unsigned slot, uint32_t *args, unsigned count)
{
    Ev({evSvc, self->id, slot, count});
    for (unsigned i = 0; i < count; ++i) Ev({Norm(args[i])});
    if (self->custom[slot]) return self->custom[slot](self, args);
    if (slot == 1) return uint32_t(++self->refs);
    if (slot == 2) return uint32_t(--self->refs);
    return self->ret[slot];
}

template <size_t> using Word = uint32_t;
template <unsigned Slot, class Seq> struct Gen;
template <unsigned Slot, size_t... I> struct Gen<Slot, std::index_sequence<I...>> {
    static uint32_t __fastcall Fn(SObj *self, void *, Word<I>... a)
    {
        uint32_t args[] = {a..., 0u};
        return Dispatch(self, Slot, args, unsigned(sizeof...(I)));
    }
};
template <unsigned Slot> void FillSlot()
{
    gTable[Slot][0] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<0>>::Fn);
    gTable[Slot][1] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<1>>::Fn);
    gTable[Slot][2] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<2>>::Fn);
    gTable[Slot][3] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<3>>::Fn);
    gTable[Slot][4] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<4>>::Fn);
    gTable[Slot][5] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<5>>::Fn);
    gTable[Slot][6] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<6>>::Fn);
}
template <size_t... S> void FillAll(std::index_sequence<S...>) { (FillSlot<unsigned(S)>(), ...); }
bool gFilled = false;

void *__fastcall DirectorFramework(void *, void *) { return &gFakes[kFramework]; }

// The family's own slots the members call, recorded and answered from the family's fields (or our verified members).
uint8_t __fastcall FamSetFamilyId(Family *f, void *, uint32_t id)
{
    Ev({evFamSlot, 6, id});
    return f->Family::SetFamilyId(id);
}
uint8_t __fastcall FamGetFlag79(Family *f, void *)
{
    Ev({evFamSlot, 22});
    return f->mFlag79;
}
void __fastcall FamSetFlag79(Family *f, void *, uint32_t value)
{
    Ev({evFamSlot, 23, value});
    f->mFlag79 = uint8_t(value);
}
uint32_t *__fastcall FamGetMemberIdSlot(Family *f, void *, int32_t index)
{
    Ev({evFamSlot, 26, uint32_t(index)});
    return f->Family::GetMemberIdSlot(index);
}
uint8_t __fastcall FamHasMemberId(Family *f, void *, uint32_t id)
{
    Ev({evFamSlot, 29, id});
    return f->Family::HasMemberId(id);
}
uint32_t __fastcall FamGetFamilyId(Family *f, void *)
{
    Ev({evFamSlot, 41});
    return f->mFamilyId;
}
uint32_t __fastcall FamGetHomeLot(Family *f, void *)
{
    Ev({evFamSlot, 42});
    return f->mHomeLot;
}
} // namespace

void SObj::Reset(uint32_t newId)
{
    if (!gFilled) FillAll(std::make_index_sequence<kSlots>()), gFilled = true;
    vt = table;
    id = newId;
    refs = 1000;
    for (auto &v : value) v = 0;
    for (unsigned i = 0; i < kSlots; ++i) ret[i] = 0, custom[i] = nullptr, table[i] = nullptr;
    // Every fake counts references.
    Set(1, 0);
    Set(2, 0);
}

void SObj::Set(unsigned slot, unsigned args, uint32_t result, Custom fn)
{
    table[slot] = gTable[slot][args];
    ret[slot] = result;
    custom[slot] = fn;
}

SObj &S(Fake which) { return gFakes[which]; }

uint32_t Norm(uint32_t value)
{
    if (!value) return 0;
    for (unsigned i = 0; i < kFakeCount; ++i)
        if (value == uint32_t(uintptr_t(&gFakes[i]))) return 200 + i;
    if (const uint32_t id = Id(reinterpret_cast<const void *>(uintptr_t(value))); id >= 100) return id;
    const auto family = uint32_t(uintptr_t(F()));
    if (value >= family && value < family + 0xc8) return 0xfa000000 + (value - family);
    const auto *tib = reinterpret_cast<const NT_TIB *>(NtCurrentTeb());
    if (value >= uint32_t(uintptr_t(tib->StackLimit)) && value < uint32_t(uintptr_t(tib->StackBase))) return 0x5ac00000;
    return value;
}

uint32_t *Cache(uint32_t rva) { return reinterpret_cast<uint32_t *>(GameAddress<void *>(rva)); }

namespace {
constexpr uint32_t kCells[7] = {0x10cc288, 0x10cc2a8, 0x10cc2ac, 0x10faf6c, 0x10c9390, 0x10faf78, 0x10c95d4};
}

Globals::Globals(difftest::Context &c) : ctx(c)
{
    for (int i = 0; i < 7; ++i) saved[i] = *ctx.image.At<void *>(kCells[i]);
    savedAllocatorGuard = *ctx.image.At<int32_t>(0x10c95d8);
    auto *ga = GameAddress<GeneralAllocator>(0x10cb098);
    const auto *bytes = reinterpret_cast<const uint8_t *>(ga);
    bool zero = true;
    for (size_t i = 0; i < sizeof(GeneralAllocator); ++i) zero = zero && bytes[i] == 0;
    if (zero) ga->Construct(nullptr, 0, 0, 0, nullptr, nullptr), builtAllocator = true;
    std::memset(allocatorService, 0, sizeof(allocatorService));
    const void *serviceVtable = ctx.image.At<void>(0xda3cd4);
    std::memcpy(allocatorService, &serviceVtable, sizeof(serviceVtable));
    *ctx.image.At<void *>(0x10c95d4) = allocatorService;
    *ctx.image.At<int32_t>(0x10c95d8) = INT32_MIN;
    *ctx.image.At<void *>(0x10faf78) = &gFakes[kStore];
    savedGuard = *ctx.image.At<int32_t>(0x10c93f4);
    gDirectorVT[12] = reinterpret_cast<void *>(&DirectorFramework);
    *ctx.image.At<void *>(0x10cc288) = &gFakes[kApp];
    *ctx.image.At<void *>(0x10faf6c) = &gFakes[kPersist];
    *ctx.image.At<void *>(0x10c9390) = gDirectorVT;
    *ctx.image.At<int32_t>(0x10c93f4) = INT32_MIN;
    SetFamilySlot(6, reinterpret_cast<void *>(&FamSetFamilyId));
    SetFamilySlot(22, reinterpret_cast<void *>(&FamGetFlag79));
    SetFamilySlot(23, reinterpret_cast<void *>(&FamSetFlag79));
    SetFamilySlot(26, reinterpret_cast<void *>(&FamGetMemberIdSlot));
    SetFamilySlot(29, reinterpret_cast<void *>(&FamHasMemberId));
    SetFamilySlot(41, reinterpret_cast<void *>(&FamGetFamilyId));
    SetFamilySlot(42, reinterpret_cast<void *>(&FamGetHomeLot));
}

Globals::~Globals()
{
    for (int i = 0; i < 7; ++i) *ctx.image.At<void *>(kCells[i]) = saved[i];
    *ctx.image.At<int32_t>(0x10c95d8) = savedAllocatorGuard;
    if (builtAllocator) {
        auto *ga = GameAddress<GeneralAllocator>(0x10cb098);
        difftest_alloc::ShutdownAndRelease(ga);
    }
    *ctx.image.At<int32_t>(0x10c93f4) = savedGuard;
    ClearFamilySlots();
}

void Reset(void *currentLot, void *household)
{
    for (unsigned i = 0; i < kFakeCount; ++i) gFakes[i].Reset(200 + i);
    *reinterpret_cast<void **>(Cache(0x10cc2a8)) = currentLot;
    *reinterpret_cast<void **>(Cache(0x10cc2ac)) = household;
}

void CaptureFakes(Blob &b)
{
    for (const SObj &o : gFakes) {
        b.push_back(uint32_t(o.refs));
        for (uint32_t v : o.value) b.push_back(v);
    }
    b.push_back(Norm(*Cache(0x10cc2a8)));
    b.push_back(Norm(*Cache(0x10cc2ac)));
}
} // namespace tsfam::svc
