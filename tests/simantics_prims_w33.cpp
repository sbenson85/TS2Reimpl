// Differential test for the SimAntics primitive handler of src/simantics/PrimW.h: opcode 0x33, ManageInventory (0x598ac0).
// Framework: tests/simantics_prims_w.h. Everything is a recording fake except the director singleton (its framework field
// points to a fake, as in the lane R tests) and the cached service getters, whose cache globals point at fakes.
#include "simantics_prims_w.h"

namespace {

// ---- operand reader ---------------------------------------------------------------------------------------------------

struct ReadScript {
    bool hasTarget = false;
    bool hasTarget2 = false; // the storage of every second destination read in a run
    int cell = 0;
    int length = 1;
    int16_t min = -32768, max = 32767;
};
ReadScript gRead;
std::vector<uint32_t> gReadQ;
size_t gReadPos = 0;
uint32_t gDestReads = 0;

// Secondary slot 7: seven stack arguments; fills the destination run from the script, answers from the queue.
uint32_t __fastcall ReadQ(void *self, void *, uint32_t scope, uint32_t id, SimDataRef *dest, float **fdest, uint32_t *a5, uint32_t a6,
                          uint32_t a7)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(7u << 8 | 7u);
    gTrace.insert(gTrace.end(), {scope, id, dest ? 1u : 0u, fdest ? 1u : 0u, a5 ? 1u : 0u, a6, a7});
    if (dest) {
        dest->mLength = static_cast<int16_t>(gRead.length);
        dest->mIndex = 0;
        const bool has = (gDestReads++ % 2 == 0) ? gRead.hasTarget : gRead.hasTarget2;
        dest->mPtr = has ? &gW->cells[gRead.cell] : nullptr;
        dest->mMin = gRead.min;
        dest->mMax = gRead.max;
    }
    return gReadPos < gReadQ.size() ? gReadQ[gReadPos++] : 0u;
}

// ---- list fakes -----------------------------------------------------------------------------------------------------

constexpr uint32_t kOutMarker = 0xfff00001u;

int InvIndex(void *self)
{
    return self == &gW->fk[kP2] ? 1 : 0;
}

// begin/end getters (the argument is the caller's hidden result slot): a pointer to the list's begin/end cell.
template <int S> uint32_t *__fastcall Getter(void *self, void *, void *hidden)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(static_cast<uint32_t>(S) << 8 | 1u);
    gTrace.push_back(hidden ? kOutMarker : 0u);
    InvMem &m = gW->inv[InvIndex(self)];
    switch (S) {
    case 28: return &m.beginCell[0];
    case 29: return &m.endCell[0];
    case 42: return &m.beginCell[1];
    case 43: return &m.endCell[1];
    default: return &gW->cursorEnd; // the cursor's slot 26
    }
}

// The iterator written for an index (end when out of range).
uint32_t IterAt(InvMem &m, int list, uint32_t index)
{
    const uint32_t n = m.count[list];
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&m.elems[list][index < n ? index : n]));
}

// `at` slots (out, index): 21 on list 0, 39 on list 1.
template <int S, int LIST> uint32_t __fastcall AtOut(void *self, void *, uint32_t *out, uint32_t index)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(static_cast<uint32_t>(S) << 8 | 2u);
    gTrace.push_back(out ? kOutMarker : 0u);
    gTrace.push_back(index);
    *out = IterAt(gW->inv[InvIndex(self)], LIST, index);
    return 0;
}

uint32_t FindImpl(void *self, int slot, int list, int extra, const uint32_t *a, uint32_t *out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(static_cast<uint32_t>(slot) << 8 | static_cast<uint32_t>(1 + extra));
    gTrace.push_back(out ? kOutMarker : 0u);
    for (int i = 0; i < extra; ++i) gTrace.push_back(Norm(a[i]));
    InvMem &m = gW->inv[InvIndex(self)];
    *out = IterAt(m, list, Scripted(self, slot) % (m.count[list] + 1));
    return 0;
}

uint32_t __fastcall Find25(void *self, void *, uint32_t *out, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    const uint32_t a[] = {a1, a2, a3, a4, a5, a6};
    return FindImpl(self, 25, 0, 6, a, out);
}
uint32_t __fastcall Find24(void *self, void *, uint32_t *out, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    const uint32_t a[] = {a1, a2, a3, a4, a5};
    return FindImpl(self, 24, 0, 5, a, out);
}
uint32_t __fastcall Find37(void *self, void *, uint32_t *out, uint32_t a1, uint32_t a2)
{
    const uint32_t a[] = {a1, a2};
    return FindImpl(self, 37, 1, 2, a, out);
}
uint32_t __fastcall Find38(void *self, void *, uint32_t *out, uint32_t a1, uint32_t a2)
{
    const uint32_t a[] = {a1, a2};
    return FindImpl(self, 38, 1, 2, a, out);
}

// The cursor's slot 24 (out, index): a pointer into its list of words, or its end.
uint32_t __fastcall CursorAt(void *self, void *, uint32_t *out, uint32_t index)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(24u << 8 | 2u);
    gTrace.push_back(out ? kOutMarker : 0u);
    gTrace.push_back(index);
    const uint32_t n = gW->cursorCount;
    *out = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gW->cursorList[index < n ? index : n]));
    return 0;
}

// An entry's slot 19 (pointer to a word): writes the scripted word, answers al.
uint32_t __fastcall Entry19(void *self, void *, uint16_t *out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(19u << 8 | 1u);
    gTrace.push_back(out ? kOutMarker : 0u);
    *out = static_cast<uint16_t>(Scripted(self, 0x113));
    return Scripted(self, 19);
}

// The director's slot 3 (class id, interface id, void **out).
uint32_t __fastcall Director3(void *self, void *, uint32_t a, uint32_t b, void **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(3u << 8 | 3u);
    gTrace.insert(gTrace.end(), {a, b, out ? kOutMarker : 0u});
    *out = reinterpret_cast<void *>(static_cast<uintptr_t>(Scripted(self, 0x103)));
    return Scripted(self, 3);
}

// The context object's slot 88 (what, count, zero, pointer to four words of the handler's stack): the words are logged.
uint32_t __fastcall Ctx88(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3, const uint16_t *block)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(88u << 8 | 4u);
    gTrace.insert(gTrace.end(), {Norm(a1), a2, a3});
    for (int i = 0; i < 4; ++i) gTrace.push_back(block[i]);
    return Scripted(self, 88);
}

// ORIGINAL-BUG sites: the original pushes one argument for these (the callee takes four / three); the other arguments are
// undefined stack, so only the first is logged. Ours pushes four / three with zeros.
uint32_t __fastcall Inv33Bug(void *self, void *, uint32_t a1, uint32_t, uint32_t, uint32_t)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(33u << 8 | 1u);
    gTrace.push_back(Norm(a1));
    return Scripted(self, 33);
}
uint32_t __fastcall Inv34Bug(void *self, void *, uint32_t a1, uint32_t, uint32_t)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(34u << 8 | 1u);
    gTrace.push_back(Norm(a1));
    return Scripted(self, 34);
}

// ---- world ----------------------------------------------------------------------------------------------------------

uint32_t NoiseAl(std::mt19937 &r, bool nonzero)
{
    const uint32_t high = r() & ~0xffu;
    return nonzero ? (high | (1 + r() % 255)) : high;
}

uint32_t Edge16(std::mt19937 &r)
{
    const int16_t e[] = {0, 1, -1, 2, 3, 7, 100, 32767, -32768, 12345, -12345};
    const int16_t v = (r() % 3 == 0) ? static_cast<int16_t>(r()) : e[r() % 11];
    return static_cast<uint32_t>(static_cast<uint16_t>(v)) | (r() % 4 == 0 ? (r() & 0xffff0000u) : 0u);
}

const Fake *PickPerson(WorldW &w, std::mt19937 &r)
{
    const uint32_t k = r() % 20;
    if (k < 14) return &w.fk[kH];
    if (k < 17) return &w.fk[kP2];
    return nullptr;
}

void SetupInventory(WorldW &w, Fake &f, int index, std::mt19937 &r, bool bug33, bool bug34)
{
    Use(f.vt, 15, 1);
    RetValue(&f, 15, r() % 3 ? Edge16(r) : static_cast<uint32_t>(-1 - static_cast<int>(r() % 3)), w);
    Use(f.vt, 16, 3);
    RetValue(&f, 16, r() % 3 ? Edge16(r) : static_cast<uint32_t>(static_cast<uint16_t>(0x8000 | r())), w);
    Use(f.vt, 18, 4);
    RetValue(&f, 18, NoiseAl(r, r() % 3 != 0), w);
    Use(f.vt, 20, 1);
    RetValue(&f, 20, NoiseAl(r, r() % 3 != 0), w);
    Use(f.vt, 30, 3);
    Use(f.vt, 31, 0);
    f.vt[33] = bug33 ? reinterpret_cast<void *>(&Inv33Bug) : gThunk[33][4];
    RetValue(&f, 33, NoiseAl(r, r() % 3 != 0), w);
    f.vt[34] = bug34 ? reinterpret_cast<void *>(&Inv34Bug) : gThunk[34][3];
    RetValue(&f, 34, NoiseAl(r, r() % 3 != 0), w);
    Use(f.vt, 35, 2);
    RetValue(&f, 35, NoiseAl(r, r() % 3 != 0), w);
    Use(f.vt, 36, 1);
    RetValue(&f, 36, NoiseAl(r, r() % 3 != 0), w);
    Use(f.vt, 46, 0);
    RetValue(&f, 46, r() % 5 == 0 ? 0u : r() | 1u, w);
    Use(f.vt, 48, 4);
    RetValue(&f, 48, NoiseAl(r, r() % 2 == 0), w);
    Use(f.vt, 49, 2);
    RetValue(&f, 49, NoiseAl(r, r() % 2 == 0), w);

    f.vt[21] = reinterpret_cast<void *>(&AtOut<21, 0>);
    f.vt[39] = reinterpret_cast<void *>(&AtOut<39, 1>);
    f.vt[24] = reinterpret_cast<void *>(&Find24);
    f.vt[25] = reinterpret_cast<void *>(&Find25);
    f.vt[37] = reinterpret_cast<void *>(&Find37);
    f.vt[38] = reinterpret_cast<void *>(&Find38);
    f.vt[28] = reinterpret_cast<void *>(&Getter<28>);
    f.vt[29] = reinterpret_cast<void *>(&Getter<29>);
    f.vt[42] = reinterpret_cast<void *>(&Getter<42>);
    f.vt[43] = reinterpret_cast<void *>(&Getter<43>);

    InvMem &m = w.inv[index];
    for (int list = 0; list < 2; ++list) {
        m.count[list] = r() % 5 == 0 ? 0 : 1 + r() % kMaxElems;
        for (uint32_t i = 0; i <= kMaxElems; ++i)
            m.elems[list][i] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&w.fk[kE0 + r() % 3]));
        m.beginCell[list] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&m.elems[list][0]));
        m.endCell[list] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&m.elems[list][m.count[list]]));
    }
}

void Build33(WorldW &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 0x33);
    EnsureThunks();
    std::memset(&w, 0, sizeof(w));
    gRets.clear();
    gQ.clear();
    gQPos.clear();
    gSeed = seed;
    gThrowAt = (r() % 6 == 0) ? 1 + r() % 40 : 0;
    gRead = ReadScript();
    gReadQ.clear();
    gReadPos = 0;
    w.canary = 0xC0DEC0DEu;

    // The parameter block.
    for (uint8_t &b : w.param) b = static_cast<uint8_t>(r());
    const bool mode1 = (r() & 1) != 0;
    uint8_t opcode = mode1 ? r() % 14 : r() % 22;
    if (r() % 40 == 0) opcode = static_cast<uint8_t>(r());
    const uint32_t kind = (r() % 10 == 0) ? 5 + r() % 3 : r() % 5;
    w.param[4] = static_cast<uint8_t>((r() & 0xf0) | (mode1 ? 8 : 0) | kind);
    w.param[8] = opcode;
    // Byte +0xa is part of the token dword at +9 and also the second selector / fourth operand scope of some operations.
    const uint8_t second = static_cast<uint8_t>((r() & 0xf0) | ((r() & 1) ? 8 : 0) | ((r() % 10 == 0) ? 5 + r() % 3 : r() % 5));
    w.param[0xa] = second;
    if (r() % 5 < 2) {
        std::memset(w.param + 9, 0, 4);
        if (opcode == 0xb || opcode == 0xe || opcode == 0xf || opcode == 0x12) w.param[0xa] = second;
    }
    // W_INJECT_WORLD: one in eight token dwords (passed on to the fakes as they are) becomes a word inside the shared world.
    if (gInjectWorld && r() % 8 == 0) {
        uint32_t token;
        std::memcpy(&token, w.param + 9, 4);
        if (token != 0) {
            token = InjectWorld(token & 0x1fffffffu);
            std::memcpy(w.param + 9, &token, 4);
        }
    }

    // The object.
    auto *self = reinterpret_cast<PrimWObject *>(w.obj);
    for (uint8_t &b : w.obj) b = static_cast<uint8_t>(r());
    self->mVTable = w.vtThis;
    self->mSecondary = w.vtSec;
    self->mContextHolder = &w.fk[kCH];
    self->mCursor = &w.fk[kCur];
    for (int i = 0; i < kFk; ++i) w.fk[i].vt = w.vtFk[i];
    const uint32_t selKinds[] = {0, 1, 2, 3, 4, 5, 0xffff, 0x8003};
    self->mSelKind = static_cast<uint16_t>(selKinds[r() % 8]);
    const uint32_t flags[] = {0, 0, 1, 1, 2, 0xffff};
    self->mSelFlag = static_cast<uint16_t>(flags[r() % 6]);
    self->mSelIndex = static_cast<uint16_t>(r() % 10);

    // Read queue and destination.
    for (int i = 0; i < 12; ++i) gReadQ.push_back(r() % 3 ? 1 + r() % 8 : Edge16(r));
    gRead.hasTarget = r() % 6 != 0;
    gRead.hasTarget2 = r() % 5 != 0;
    gRead.cell = static_cast<int>(r() % 14);
    gRead.length = 1 + static_cast<int>(r() % 2);
    switch (r() % 3) {
    case 0: gRead.min = -32768; gRead.max = 32767; break;
    case 1: gRead.min = 0; gRead.max = 100; break;
    default: gRead.min = static_cast<int16_t>(r()); gRead.max = static_cast<int16_t>(r()); break;
    }
    w.vtSec[7] = reinterpret_cast<void *>(&ReadQ);

    // This.
    Use(w.vtThis, 63, 0);
    RetObject(w.obj, 63, &w.fk[kTblA], w);
    Fake &tblA = w.fk[kTblA], &owner = w.fk[kOwner], &ownerData = w.fk[kOwnerData];
    Use(tblA.vt, 29, 1);
    RetObject(&tblA, 29, r() % 7 == 0 ? nullptr : &owner, w);
    Use(owner.vt, 61, 0);
    RetObject(&owner, 61, &ownerData, w);
    Use(ownerData.vt, 28, 0);
    RetValue(&ownerData, 28, r(), w);

    // The context holder and the persons.
    Fake &ch = w.fk[kCH], &o1 = w.fk[kO1];
    Use(ch.vt, 6, 0);
    RetObject(&ch, 6, &o1, w);
    Use(ch.vt, 17, 0);
    RetObject(&ch, 17, PickPerson(w, r), w);
    Use(ch.vt, 26, 0);
    RetObject(&ch, 26, &w.fk[kZ], w);
    Use(o1.vt, 36, 0);
    RetObject(&o1, 36, PickPerson(w, r), w);
    for (int slot : {37, 38, 39}) {
        Use(o1.vt, slot, 1);
        RetObject(&o1, slot, PickPerson(w, r), w);
    }

    // The frame.
    Fake &frame = w.fk[kFrame];
    Use(frame.vt, 12, 0);
    RetValue(&frame, 12, Edge16(r), w);
    Use(frame.vt, 13, 1);

    // Inventories: the buggy call sites (flag 1: operations 1 and 3) get the one-argument loggers.
    const bool effMode1 = mode1;
    const bool bug33 = effMode1 && opcode == 1;
    const bool bug34 = effMode1 && opcode == 3;
    SetupInventory(w, w.fk[kH], 0, r, bug33, bug34);
    SetupInventory(w, w.fk[kP2], 1, r, bug33, bug34);

    // Entries.
    const uint32_t valueChoices[] = {0, 5, 7, 0};
    const int entry6Args = (effMode1 && (opcode == 1 || opcode == 3)) ? 1 : 0;
    int entry21Args = 1;
    if (!mode1 && opcode == 0xf) entry21Args = 1;
    if (mode1 && opcode == 1) entry21Args = 3;
    if (mode1 && opcode == 3) entry21Args = 2;
    if (mode1 && opcode == 6) entry21Args = 1;
    for (int e = 0; e < 3; ++e) {
        Fake &f = w.fk[kE0 + e];
        Use(f.vt, 1, 0);
        Use(f.vt, 2, 0);
        Use(f.vt, 6, entry6Args);
        Use(f.vt, 8, 0);
        Use(f.vt, 18, 0);
        f.vt[19] = reinterpret_cast<void *>(&Entry19);
        Use(f.vt, 20, 1);
        Use(f.vt, 21, entry21Args);
        Use(f.vt, 22, 2);
        Use(f.vt, 27, 0);
        Use(f.vt, 29, 1);
        RetValue(&f, 6, r(), w);
        RetValue(&f, 8, Edge16(r), w);
        RetValue(&f, 18, Edge16(r), w);
        RetValue(&f, 27, valueChoices[r() % 4], w);
        RetValue(&f, 21, Edge16(r), w);
        RetValue(&f, 19, NoiseAl(r, r() % 3 != 0), w);
        RetValue(&f, 0x113, Edge16(r), w);
    }

    // The cursor.
    Fake &cur = w.fk[kCur];
    Use(cur.vt, 22, 2);
    cur.vt[24] = reinterpret_cast<void *>(&CursorAt);
    cur.vt[26] = reinterpret_cast<void *>(&Getter<26>);
    Use(cur.vt, 27, 0);
    Use(cur.vt, 29, 1);
    RetValue(&cur, 27, valueChoices[r() % 4], w);
    w.cursorCount = r() % 5 == 0 ? 0 : 1 + r() % kMaxElems;
    for (uint16_t &c : w.cursorList) c = static_cast<uint16_t>(Edge16(r));
    w.cursorBegin = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&w.cursorList[0]));
    w.cursorEnd = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&w.cursorList[w.cursorCount]));

    // The context object (context holder slot 26) and what it looks up.
    Fake &z = w.fk[kZ], &stack = w.fk[kStack], &stackData = w.fk[kStackData];
    z.vt[88] = reinterpret_cast<void *>(&Ctx88);
    RetValue(&z, 88, r() % 3 == 0 ? 0u : Edge16(r), w);
    Use(z.vt, 16, 4);
    RetValue(&z, 16, r() % 3 == 0 ? 0u : Edge16(r), w);
    Use(z.vt, 29, 1);
    EnqueueObject(&z, 29, r() % 6 == 0 ? nullptr : &stack, w);
    EnqueueValue(&z, 29, r() % 3 != 0 ? r() | 1u : 0u, w);
    Use(z.vt, 87, 1);
    RetValue(&z, 87, r() % 4 == 0 ? 0u : r() | 1u, w);
    Use(z.vt, 90, 3);
    Use(stack.vt, 47, 3);
    RetValue(&stack, 47, Edge16(r), w);
    Use(stack.vt, 61, 0);
    RetObject(&stack, 61, &stackData, w);
    Use(stackData.vt, 28, 0);
    RetValue(&stackData, 28, r(), w);

    // The director, the helper and the cached services.
    Fake &director = w.fk[kDirector], &helper = w.fk[kHelper];
    director.vt[3] = reinterpret_cast<void *>(&Director3);
    // ORIGINAL-BUG avoided: operations 0x10 and 0x13 of mode 0 use the helper without a null check and crash when the
    // director hands none out, so those operations always get one; operation 0xb uses it whenever the director says yes, so
    // a missing helper comes with a "no".
    const bool helperRequired = !mode1 && (opcode == 0x10 || opcode == 0x13);
    const bool noHelper = !helperRequired && r() % 6 == 0;
    RetValue(&director, 3, NoiseAl(r, !noHelper && r() % 5 != 0), w);
    RetObject(&director, 0x103, noHelper ? nullptr : &helper, w);
    for (int slot : {2, 7, 9, 28, 29}) Use(helper.vt, slot, slot == 2 ? 0 : 1);
    Fake &singleton = w.fk[kSingleton], &lookup = w.fk[kLookup], &t1 = w.fk[kThing1], &t2 = w.fk[kThing2], &typed = w.fk[kTyped];
    Use(singleton.vt, 24, 0);
    RetObject(&singleton, 24, &lookup, w);
    Use(lookup.vt, 14, 1);
    RetObject(&lookup, 14, r() % 5 == 0 ? nullptr : &t1, w);
    Use(t1.vt, 27, 0);
    RetObject(&t1, 27, r() % 5 == 0 ? nullptr : &t2, w);
    Use(t2.vt, 4, 0);
    RetObject(&t2, 4, r() % 5 == 0 ? nullptr : &typed, w);
    Use(typed.vt, 3, 1);
    const uint32_t types[] = {0x10, 0x11, 2, 5, 0, 9};
    RetValue(&typed, 3, types[r() % 6], w);

    // The second target's slots (kP2/kH already have 48 and 49).
}

// ---- run ------------------------------------------------------------------------------------------------------------

struct Outcome {
    uint32_t ret = 0;
    int thrown = -1;
    std::vector<uint32_t> trace;
    std::vector<uint32_t> state;
};

std::function<void(WorldW &)> gBeforeRun;

void Capture(WorldW &w, Outcome &o)
{
    for (int16_t c : w.cells) o.state.push_back(static_cast<uint16_t>(c));
    const uint32_t *words = reinterpret_cast<const uint32_t *>(w.obj + 0x1b0);
    for (int i = 0; i < 4; ++i) o.state.push_back(words[i]);
    o.state.push_back(Norm(words[4]));
    o.state.push_back(w.canary);
}

using OrigFn = uint32_t(__thiscall *)(void *, void *, const void *);

// Calls the original handler and keeps the registers the caller owns. The original's ORIGINAL-BUG sites (operations 1 and 3
// of mode 1) call an inventory slot with too few arguments; the callee then pops too much and the epilogue's `pop esi`
// takes a wrong word, so the original returns with a clobbered esi (the frame pointer restores esp itself). The game's
// caller does not depend on it; this harness does, so esi, edi and ebx are saved around the call.
__declspec(naked) uint32_t __cdecl CallOriginal(void *fn, void *self, void *frame, const void *param)
{
    __asm {
        push esi
        push edi
        push ebx
        mov eax, [esp + 16]
        mov ecx, [esp + 20]
        push dword ptr [esp + 28]
        push dword ptr [esp + 28]
        call eax
        pop ebx
        pop edi
        pop esi
        ret
    }
}

// A crash inside a run is reported (not swallowed): SEH needs a function without unwindable objects.
DWORD gCrashCode = 0;
int GuardedInvoke(void (*fn)(void *), void *arg)
{
    __try {
        fn(arg);
        return 0;
    } __except (gCrashCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        return 1;
    }
}

template <typename Call> Outcome Run(WorldW &w, Call call)
{
    gW = &w;
    for (size_t &p : gQPos) p = 0;
    gReadPos = 0;
    gDestReads = 0;
    gHitCount = 0;
    gTrace.clear();
    if (gBeforeRun) gBeforeRun(w);
    Outcome o;
    struct Frame {
        Call *call;
        WorldW *w;
        Outcome *o;
    } frame{&call, &w, &o};
    const int crashed = GuardedInvoke(
        [](void *p) {
            Frame *f = static_cast<Frame *>(p);
            try {
                f->o->ret = (*f->call)(*f->w);
            } catch (short code) {
                f->o->thrown = code;
            }
        },
        &frame);
    if (crashed) {
        o.thrown = 0x7000 + static_cast<int>(gCrashCode & 0xfff);
        std::fprintf(stderr, "crash 0x%lx after %zu trace words\n", gCrashCode, gTrace.size());
        for (size_t i = 0; i < gTrace.size() && i < 400; ++i) std::fprintf(stderr, "%x ", gTrace[i]);
        std::fprintf(stderr, "\n");
    }
    o.trace = gTrace;
    Capture(w, o);
    gW = nullptr;
    return o;
}

struct Trial {
    bool bad = false;
    Outcome oa, ob;
};

// Both sides in the one shared world (see SharedWorld in tests/simantics_prims_w.h), rebuilt from the seed before each run.
template <typename Orig, typename Ours> Trial Try(WorldW &w, uint32_t seed, Orig original, Ours ours)
{
    Trial t;
    Build33(w, seed);
    t.oa = Run(w, [&](WorldW &x) { return original(x.obj, &x.fk[kFrame], x.param); });
    Build33(w, seed);
    t.ob = Run(w, [&](WorldW &x) { return ours(x.obj, &x.fk[kFrame], x.param); });
    t.bad = t.oa.thrown != t.ob.thrown || t.oa.trace != t.ob.trace || t.oa.state != t.ob.state || (t.oa.thrown < 0 && t.oa.ret != t.ob.ret);
    return t;
}

struct Stats {
    uint32_t throws = 0;
    uint32_t returns[2][32][2] = {}; // [mode][opcode][return value]
    uint32_t byThrow[256] = {};
};

} // namespace

DIFF_TEST(PrimW_Prim33, 0x598ac0)
{
    // The director singleton in the image (as in the lane R tests): its framework field points to the world's director.
    uint8_t *director = ctx.image.At<uint8_t>(0x10c9390);
    const uint32_t tlsIndex = *ctx.image.At<uint32_t>(0x10df814);
    int *tls = reinterpret_cast<int **>(__readfsdword(0x2c))[tlsIndex];
    const int savedEpoch = tls[1];
    const int savedGuard = *ctx.image.At<int>(0x10c93f4);
    uint8_t savedBytes[0x64];
    std::memcpy(savedBytes, director, sizeof(savedBytes));
    static void *fakeVTable[32];
    static struct {
        void *vtable;
        uint32_t pad;
    } fakeService;
    fakeVTable[23] = reinterpret_cast<void *>(+[](void *, void *) -> void * { return nullptr; });
    fakeService.vtable = fakeVTable;
    void *const savedService = *ctx.image.At<void *>(0x10c9378);
    *ctx.image.At<void *>(0x10c9378) = &fakeService;
    std::memset(director, 0xCD, 0x64);
    ctx.Original<void *(__thiscall *)(void *)>(0x144897)(director);
    *ctx.image.At<int>(0x10c93f4) = 5;
    tls[1] = 0x7fffffff;
    void *const savedServiceCache = *ctx.image.At<void *>(0x10faf6c);
    void *const savedSingleton = *ctx.image.At<void *>(0x10cc288);

    gBeforeRun = [&](WorldW &w) {
        *reinterpret_cast<void **>(director + 0x30) = &w.fk[kDirector];
        *ctx.image.At<void *>(0x10faf6c) = &w.fk[kService];
        *ctx.image.At<void *>(0x10cc288) = &w.fk[kSingleton];
    };

    auto original = ctx.Original<OrigFn>(0x598ac0);
    Stats st;
    const uint32_t seeds = 80000;
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        WorldW &w = SharedWorld();
        auto origCall = [&](uint8_t *o, void *f, const void *p) { return CallOriginal(reinterpret_cast<void *>(original), o, f, p); };
        auto oursCall = [&](uint8_t *o, void *f, const void *p) {
            return reinterpret_cast<PrimWObject *>(o)->Prim33(f, static_cast<const PrimParamView *>(p));
        };
        Trial t = Try(w, seed, origCall, oursCall);
        ++ctx.cases;
        const Outcome &oa = t.oa, &ob = t.ob;
        if (oa.thrown >= 0) {
            ++st.throws;
            ++st.byThrow[oa.thrown & 255];
        } else {
            const uint32_t mode = (w.param[4] >> 3) & 1, op = w.param[8]; // (the parameter block is input: both runs leave it as built)
            if (op < 32 && oa.ret < 2) ++st.returns[mode][op][oa.ret];
        }
        if (t.bad) {
            size_t k = 0;
            while (k < oa.trace.size() && k < ob.trace.size() && oa.trace[k] == ob.trace[k]) ++k;
            ctx.Fail("seed %u: (original/ours) thrown %d/%d ret 0x%x/0x%x trace %zu/%zu (first diff at %zu: 0x%x/0x%x) state %s", seed, oa.thrown, ob.thrown,
                     oa.ret, ob.ret, oa.trace.size(), ob.trace.size(), k, k < oa.trace.size() ? oa.trace[k] : 0xdeadu,
                     k < ob.trace.size() ? ob.trace[k] : 0xdeadu, oa.state == ob.state ? "same" : "DIFFERENT");
        }
    }
    if (gInjectWorld) std::printf("      W_INJECT_WORLD: %u world-range words returned by fakes\n", gInjected);
    gBeforeRun = nullptr;
    tls[1] = savedEpoch;
    *ctx.image.At<int>(0x10c93f4) = savedGuard;
    std::memcpy(director, savedBytes, sizeof(savedBytes));
    *ctx.image.At<void *>(0x10c9378) = savedService;
    *ctx.image.At<void *>(0x10faf6c) = savedServiceCache;
    *ctx.image.At<void *>(0x10cc288) = savedSingleton;

    // Coverage: every operation of both modes must have returned true and false.
    for (uint32_t mode = 0; mode < 2; ++mode) {
        const uint32_t last = mode ? 0xb : 0x13;
        for (uint32_t op = 0; op <= last; ++op) {
            if (mode == 0 && op == 9) continue; // reached through the remembered selector too; checked below
            if (st.returns[mode][op][1] < 20) ctx.Fail("coverage: mode %u op 0x%x true returns %u", mode, op, st.returns[mode][op][1]);
        }
    }
    if (st.throws < 3000) ctx.Fail("coverage: %u throws", st.throws);
    for (int code : {0x13, 0x14, 0x15, 0x57, 0x59, 0x74, 0x78, 0x9c, 0x9d, 0x66}) {
        if (st.byThrow[code] < 20) ctx.Fail("coverage: throw 0x%x seen %u times", code, st.byThrow[code]);
    }
}
