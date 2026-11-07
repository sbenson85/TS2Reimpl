#pragma once
// Shared pieces of the lane S primitive tests (src/simantics/PrimS.h): the recording-fake framework of
// tests/simantics_prims_mid.h (fake objects whose vtable slots record their calls; the original and ours are
// compared on the calls made, the thrown code, the return value and memory) plus the operand reader that answers
// from a sequence and the cached-singleton fixture.
#include "simantics_prims_mid.h"
#include "simantics/PrimS.h"

namespace {

PrimSObject *Self(PrimMidObject *o)
{
    return reinterpret_cast<PrimSObject *>(o);
}

// The operand reader (secondary slot 7), answering from a sequence: the handlers read several operands and the
// recorders' per-slot scripts give one answer only. gReadPos restarts for each run (gBeforeRun).
std::vector<uint32_t> gReadVals;
size_t gReadPos = 0;

uint32_t __fastcall ReadSeq(void *self, void *, uint32_t scope, uint32_t id, SimDataRef *dest, float **fdest, uint32_t *a5, uint32_t a6,
                            uint32_t a7)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(7u << 8 | 7u);
    gTrace.insert(gTrace.end(), {scope, id, dest ? 1u : 0u, fdest ? 1u : 0u, a5 ? 1u : 0u, a6, a7});
    return gReadPos < gReadVals.size() ? gReadVals[gReadPos++] : 0u;
}

uint32_t ReadWord(std::mt19937 &r)
{
    return static_cast<uint32_t>(static_cast<uint16_t>(Edge16(r))) | (r() & 0xffff0000u);
}

// The cached singleton 0x20ddfb returns: the game global 0x10cc288, pointed into the world being run.
void *gSavedSingleton = nullptr;
void SetSingleton(World &w)
{
    *GameAddress<void *>(0x10cc288) = &w.fk[2];
}

// A QueryInterface whose answer (al) and object come from separate scripts: slot 0 gives the object, key 1000 the answer.
uint32_t __fastcall QiS(void *self, void *, uint32_t iid, void **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(0u << 8 | 2u);
    gTrace.push_back(iid);
    gTrace.push_back(out ? 1u : 0u);
    const uint32_t object = Scripted(self, 0);
    if (out) *out = reinterpret_cast<void *>(static_cast<uintptr_t>(object));
    return Scripted(self, 1000);
}

// The singleton and what 0x5a9f00 reaches through it: fk[2] service, fk[6] checker, fk[4] id table, fk[7] object,
// fk[8] interface.
void BuildResolver(World &w, std::mt19937 &r)
{
    Fake &service = w.fk[2], &checker = w.fk[6], &table = w.fk[4], &object = w.fk[7], &iface = w.fk[8];
    Use(service.vt, 10, 0);
    Use(service.vt, 26, 0);
    RetObject(&service, 10, &checker, w);
    RetObject(&service, 26, &table, w);
    Use(checker.vt, 16, 2);
    RetValue(&checker, 16, (r() % 2) ? 0u : (static_cast<uint32_t>(r()) | 1u), w);
    table.vt[29] = reinterpret_cast<void *>(&Lookup29);
    for (int i = 0; i < 6; ++i) LookupObj((r() % 6 == 0) ? nullptr : &object, w);
    object.vt[0] = reinterpret_cast<void *>(&Qi0);
    RetObject(&object, 0, (r() % 5 == 0) ? nullptr : &iface, w);
    Use(iface.vt, 57, 0);
    Use(iface.vt, 2, 0);
    RetValue(&iface, 57, ReadWord(r), w);
}

} // namespace
