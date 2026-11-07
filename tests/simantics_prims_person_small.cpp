// Differential tests for the small person primitive handlers of src/simantics/PrimPerson.h: opcode 0x25 (0x522300, vtable
// slot 362) and opcode 0x1d (0x522110, vtable slot 361). Framework: tests/simantics_prims_person.h.
#include "simantics_prims_person.h"

namespace {

// ---- opcode 0x25 -------------------------------------------------------------------------------------------------------

void Build25(WorldP &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 0x25);
    std::memset(&w, 0, sizeof(w));
    ResetScript(seed, r);
    w.canary = 0xC0DEC0DEu;
    FillRandom(w.obj, sizeof(w.obj), r);
    for (uint8_t &b : w.param) b = static_cast<uint8_t>(r());
    InitPerson(w);

    Fake &frame = w.fk[kF0];
    Use(frame.vt, 12, 0);
    // The stack object id is the frame's 16-bit answer; the upper half is noise the handler must not pass on.
    RetValue(&frame, 12, Edge16(r) | (r() & 0xffff0000u), w);
    Use(w.vtPerson, 137, 1);
    RetValue(w.obj + 0x2f0, 137, NoiseAl(r, r() % 2 == 0), w);
}

// ---- opcode 0x1d -------------------------------------------------------------------------------------------------------

// The reallocating insert of the cMotiveInc vector (0x50da10, __thiscall ret 8: the end iterator, the element): logged with
// the element's id word and floats (the word at +6 is uninitialised stack in the original).
// Checked (DT): the +6 word is dead: 0x5221f9 builds the stack element without it, the in-place append 0x52225a writes
// +0/+4/+8/+0xc only (its +6 stays as the scratch held: compared), and 0x50da10's copy constructor never reads it.
uint32_t __fastcall MotiveInsert(void *self, void *, uint32_t pos, const uint8_t *element)
{
    Tick();
    gTrace.push_back(0x8050da10u);
    gTrace.push_back(Norm(reinterpret_cast<uint32_t>(self)));
    gTrace.push_back(Norm(pos));
    uint16_t id;
    std::memcpy(&id, element + 4, 2);
    uint32_t a, b, vt;
    std::memcpy(&a, element + 8, 4);
    std::memcpy(&b, element + 12, 4);
    std::memcpy(&vt, element, 4);
    gTrace.insert(gTrace.end(), {id, a, b, vt});
    return Scripted(0x8050da10u, 0);
}

void Build1D(WorldP &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 0x1d);
    std::memset(&w, 0, sizeof(w));
    ResetScript(seed, r);
    w.canary = 0xC0DEC0DEu;
    FillRandom(w.obj, sizeof(w.obj), r);
    for (uint8_t &b : w.param) b = static_cast<uint8_t>(r());
    FillRandom(w.scratch, sizeof(w.scratch), r);
    InitPerson(w);

    const uint8_t valid[] = {0, 1, 5, 6, 7, 8, 9, 14, 15};
    w.param[6] = (r() % 5 == 0) ? static_cast<uint8_t>(r()) : valid[r() % 9];
    if (r() % 6 == 0) w.param[7] = static_cast<uint8_t>(w.param[7] | 1);
    else if (r() % 3 == 0) w.param[7] = static_cast<uint8_t>(w.param[7] & ~1);

    auto *self = reinterpret_cast<PrimPersonObject *>(w.obj);
    const uint32_t count = r() % 4;
    self->mMotiveBegin = w.scratch + 0x10;
    self->mMotiveEnd = w.scratch + 0x10 + 16 * count;
    self->mMotiveLimit = (r() % 3 == 0) ? self->mMotiveEnd : self->mMotiveEnd + 16 * (1 + r() % 2);
    if (r() % 10 == 0) self->mMotiveLimit = self->mMotiveEnd + 4; // a limit that is not a whole element away

    // The two operand reads: the answer is the low 16 bits, the upper half noise.
    Use(w.vtTree, 7, 7);
    const uint32_t extra = r() % 2 ? (r() & 0xffff0000u) : 0u;
    EnqueueValue(w.obj + 4, 7, Edge16(r) | extra, w);
    EnqueueValue(w.obj + 4, 7, Edge16(r) | (r() & 0xffff0000u), w);
    Use(w.vtPerson, 11, 3);
}

} // namespace

DIFF_TEST(PrimPerson_Prim25, 0x522300)
{
    auto original = ctx.Original<OrigFn>(0x522300);
    uint32_t counts[2] = {};
    RunSeeds(
        ctx, 20000, Build25, [&](uint8_t *o, void *f, const void *p) { return original(o, f, p); },
        [&](uint8_t *o, void *f, const void *p) { return reinterpret_cast<PrimPersonObject *>(o)->Prim25(f, static_cast<const PrimParamView *>(p)); },
        [&](WorldP &, const Trial &t) {
            if (t.oa.thrown < 0 && t.oa.ret < 2) ++counts[t.oa.ret];
            if (t.oa.thrown < 0 && t.oa.ret > 1) ++counts[0]; // (reported through the comparison)
        });
    if (counts[0] < 1000 || counts[1] < 1000) ctx.Fail("coverage: returns %u/%u", counts[0], counts[1]);
}

DIFF_TEST(PrimPerson_Prim1D, 0x522110)
{
    // Both sides' reallocating insert (0x50da10 and ours, which Prim1D calls directly) go to the logging stand-in:
    // the vector lives in scratch memory, and the insert has its own test (tests/simantics_motive_insert.cpp).
    CodePatch insert(ctx, 0x50da10, reinterpret_cast<void *>(&MotiveInsert));
    CodePatch insertOurs(reinterpret_cast<void *>(&MotiveEmplaceReallocate), reinterpret_cast<void *>(&MotiveInsert));
    auto original = ctx.Original<OrigFn>(0x522110);
    uint32_t clears = 0, badMotive = 0, injected = 0, slot11 = 0, inPlace = 0, grown = 0;
    RunSeeds(
        ctx, 40000, Build1D, [&](uint8_t *o, void *f, const void *p) { return original(o, f, p); },
        [&](uint8_t *o, void *f, const void *p) { return reinterpret_cast<PrimPersonObject *>(o)->Prim1D(f, static_cast<const PrimParamView *>(p)); },
        [&](WorldP &w, const Trial &t) {
            const Outcome &o = t.oa;
            if (o.thrown == 0x1a) ++badMotive;
            if (o.thrown == 0x66) ++injected;
            if (o.thrown < 0 && (w.param[7] & 1)) ++clears;
            if (o.thrown < 0 && !(w.param[7] & 1)) {
                bool insertSeen = false, s11 = false;
                for (size_t i = 0; i + 1 < o.trace.size(); ++i) {
                    if (o.trace[i] == 0x8050da10u) insertSeen = true;
                    if (o.trace[i] == (11u << 8 | 3u) && i > 0 && o.trace[i - 1] == 0x2f0u) s11 = true;
                }
                if (insertSeen) ++grown; else ++inPlace;
                if (s11) ++slot11;
            }
        });
    if (clears < 500 || badMotive < 500 || injected < 100 || slot11 < 500 || inPlace < 2000 || grown < 500)
        ctx.Fail("coverage: clears %u badMotive %u injected %u slot11 %u inPlace %u grown %u", clears, badMotive, injected, slot11, inPlace, grown);
}
