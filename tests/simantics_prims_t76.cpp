// Differential test for the SimAntics primitive handler of src/simantics/PrimT.h: opcode 0x76, ArrayOperation (0x57f2b0).
// Framework: tests/simantics_prims_mid.h, tests/simantics_prims_r.h (queue-driven operand reader), tests/simantics_prims_t.h.
//
// The array is a plain three-pointer vector of 16-bit values the target's slot 265 hands out; its storage comes from the
// allocator service (the handler frees and allocates through it). After each run the array (size, capacity, contents)
// is appended to the trace and freed, so the two sides are compared on exactly what they leave behind, capacity
// included (the growth rule is observable). A std::length_error thrown from inside (resize by a negative amount) is
// recorded as thrown code 0x7777: the original's copy of the exception type is not the test exe's.
#include "simantics_prims_t.h"

#include "app/AppServices2.h"
#include "rz/cGZAllocatorService.h"

namespace {

struct FakeVec {
    int16_t *first, *last, *end;
};

FakeVec gVec = {};
bool gVecNull = false;
uint32_t gCountReturn = 0;

// Slot 266 of the target (the number of arrays: the handler uses ax) and slot 265 (the array of a number).
uint32_t __fastcall Slot266(void *self, void *)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(266u << 8);
    return gCountReturn;
}

FakeVec *__fastcall Slot265(void *self, void *, uint32_t number)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(265u << 8 | 1u);
    gTrace.push_back(number);
    return gVecNull ? nullptr : &gVec;
}

int16_t SmallValue(std::mt19937 &r)
{
    switch (r() % 8) {
    case 0: return 32767;
    case 1: return -32768;
    case 2: return -1;
    case 3: return 0;
    default: return static_cast<int16_t>(static_cast<int>(r() % 7) - 3);
    }
}

struct VecSpec {
    uint32_t size = 0;
    uint32_t spare = 0;
    bool noStorage = false;
    std::vector<int16_t> data;
};
VecSpec gSpec;

// The array of a seed, rebuilt before each run (both sides start from the same one).
void InitVec(uint32_t seed)
{
    std::mt19937 r(seed * 40503u + 7);
    const uint32_t size = gSpec.size;
    if (gSpec.noStorage) {
        gVec = {nullptr, nullptr, nullptr};
        return;
    }
    const uint32_t capacity = size + gSpec.spare;
    gVec.first = static_cast<int16_t *>(as2::AllocateViaAllocatorService(capacity * 2 + 2, 0));
    gVec.last = gVec.first + size;
    gVec.end = gVec.first + capacity;
    for (uint32_t i = 0; i < capacity; ++i) gVec.first[i] = gSpec.data[i];
    (void)r;
}

void Snapshot()
{
    gTrace.push_back(0x76000000u);
    if (gVec.first) {
        gTrace.push_back(static_cast<uint32_t>(gVec.last - gVec.first));
        gTrace.push_back(static_cast<uint32_t>(gVec.end - gVec.first));
        for (int16_t *p = gVec.first; p != gVec.last; ++p) gTrace.push_back(static_cast<uint16_t>(*p));
        // the spare elements are not part of the vector's value, but a stale one would be copied by a bad grow
        FreeThroughAllocatorService(gVec.first, 0);
    } else {
        gTrace.push_back(0xffffffffu);
        gTrace.push_back(gVec.last == nullptr && gVec.end == nullptr ? 1u : 0u);
    }
    gVec = {};
}

void Build76(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 76);
    Base(w, seed, r);
    SetParam(w, r);
    ClearQueues();
    Fake &ctx = w.fk[0], &table = w.fk[2], &source = w.fk[3], &other = w.fk[4];

    const bool viaTable = r() % 3 == 0;
    w.param[6] = viaTable ? static_cast<uint8_t>(1 + r() % 255) : 0;
    uint32_t op = r() % 15;
    if (r() % 40 == 0) op = 14 + r() % 250;
    w.param[5] = static_cast<uint8_t>(op);

    // The array: size 0..10, spare capacity mostly 0..2.
    gSpec = VecSpec();
    gSpec.size = (r() % 6 == 0) ? 0 : r() % 11;
    gSpec.spare = (r() % 3 == 0) ? r() % 5 : 0;
    gSpec.noStorage = r() % 40 == 0;
    for (uint32_t i = 0; i < gSpec.size + gSpec.spare + 1; ++i) gSpec.data.push_back(SmallValue(r));
    if (gSpec.noStorage) {
        gSpec.size = gSpec.spare = 0;
    }
    gVecNull = r() % 30 == 0;

    // The array number (word at +7) against the count slot 266 answers (ax; the upper half is noise).
    const int16_t count = static_cast<int16_t>(r() % 12 == 0 ? Pick(r, {0, 0x7fff, 0x8000, 0xffff}) : 1 + r() % 4);
    gCountReturn = static_cast<uint32_t>(static_cast<uint16_t>(count)) | (r() & 0xffff0000u);
    int16_t number;
    switch (r() % 24) {
    case 0: number = count; break;
    case 1: number = -1; break;
    case 2: number = static_cast<int16_t>(r()); break;
    default: number = static_cast<int16_t>(count > 0 ? r() % count : 0); break;
    }
    PutWord(w, 7, static_cast<uint16_t>(number));

    // Who the target is.
    Use(ctx.vt, 26, 0);
    RetObject(&ctx, 26, &table, w);
    table.vt[29] = reinterpret_cast<void *>(&Lookup29);
    LookupObj((r() % 8 == 0) ? nullptr : &other, w);
    w.vtSec[20] = gThunk[20][0];
    RetObject(reinterpret_cast<const char *>(&w.obj) + 4, 20, &source, w); // keyed by the secondary subobject (this + 4)
    Use(source.vt, 12, 0);
    RetValue(&source, 12, r(), w);
    for (void **vt : {w.vtThis, other.vt}) {
        vt[266] = reinterpret_cast<void *>(&Slot266);
        vt[265] = reinterpret_cast<void *>(&Slot265);
    }

    // The operands: the value, then an index (or the second operand).
    const int16_t first = (r() % 4 == 0) ? SmallValue(r) : ((gSpec.size && r() % 2) ? gSpec.data[r() % gSpec.size] : SmallValue(r));
    int16_t second;
    switch (r() % 5) {
    case 0: second = -1; break;
    case 1: second = static_cast<int16_t>(gSpec.size); break;
    case 2: second = static_cast<int16_t>(r()); break;
    default: second = static_cast<int16_t>(gSpec.size ? r() % gSpec.size : 0); break;
    }
    gReadQ = {static_cast<uint32_t>(static_cast<uint16_t>(first)) | (r() & 0xffff0000u),
              static_cast<uint32_t>(static_cast<uint16_t>(second)) | (r() & 0xffff0000u)};
    w.vtSec[7] = reinterpret_cast<void *>(&ReadQ);
    gRead.hasTarget = r() % 8 != 0;
    gRead.cell = static_cast<int>(r() % 16);
    switch (r() % 4) {
    case 0: gRead.min = -32768; gRead.max = 32767; break;
    case 1: gRead.min = 0; gRead.max = 100; break;
    case 2: gRead.min = static_cast<int16_t>(r() % 5); gRead.max = static_cast<int16_t>(r() % 12); break;
    default: gRead.min = static_cast<int16_t>(r()); gRead.max = static_cast<int16_t>(r()); break;
    }
}

} // namespace

DIFF_TEST(PrimT_Prim76, 0x57f2b0)
{
    auto original = ctx.Original<OrigFn>(0x57f2b0);
    FakeAllocatorService allocator(ctx);
    gBeforeRun = [](World &) {
        ResetQueues();
        InitVec(gSeed);
    };
    auto wrap = [](auto call) {
        return [call](PrimMidObject *o, void *f, const void *p) -> uint32_t {
            uint32_t result;
            try {
                result = call(o, f, p);
            } catch (short) {
                Snapshot();
                throw;
            } catch (...) {
                Snapshot();
                throw static_cast<short>(0x7777);
            }
            Snapshot();
            return result;
        };
    };
    Stats st;
    Compare(ctx, "Prim76", 60000, Build76, wrap([&](PrimMidObject *o, void *f, const void *p) { return original(o, f, p); }),
            wrap([&](PrimMidObject *o, void *f, const void *p) { return SelfT(o)->Prim76(f, static_cast<const PrimParamView *>(p)); }), st);
    gBeforeRun = nullptr;
    if (st.throws < 3000 || st.byRet[1] < 20000 || st.byRet[0] < 300) ctx.Fail("coverage: %u throws, %u/%u returns", st.throws, st.byRet[0], st.byRet[1]);
}
