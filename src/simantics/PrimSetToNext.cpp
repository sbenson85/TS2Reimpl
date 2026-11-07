#include "simantics/PrimSetToNext.h"

#include "game/original.h"
#include "world/WorldTileMath.h"
#include "game/vtable.h"
#include "ts/cTSFamilyRelationshipArray.h"

// Opcode 0x1f, handler 0x5a12c0. Written from the disassembly (and the decompilation, checked against it);
// every virtual call is made in the order the original makes it: the objects behind the slots are game
// objects whose implementations are not replaced yet.
//
// Operand block (the node's param, 0x18 bytes, only the bytes the handler reads):
//   +0x04 dword  key (mode 4: stack object id, low word + high word)
//   +0x08 byte   bit 7: the destination operand is (scope = byte +9, id = byte +0x0b), else (10, 0);
//                bits 0..6: the mode (switch in Execute)
//   +0x0a byte   index of a stack parameter (modes 9 and 0x22)
//   +0x0c byte   bit 0: don't reject objects whose attribute 0x38 has bits 0 or 2; bit 1: an attribute test
//                follows (word +0x0d is the attribute index, word +0x0f the value it must have); bit 2:
//                skip the attribute-0x6b test
//
// The destination (the 12-byte record built by the operand reader 0x531700, slot 7 of the tree-sim
// subobject: target pointer, min, max) holds the current position (0 to start) and receives the result.
// The handler returns 1 or 0; bad operands throw a 16-bit code (0x13 no destination, 0x14 result out of the
// destination's range, 0x17 a flagged object, 0x1c interface missing, 0x1e unknown mode, 0x68, 0x77, 0x37,
// 8, 0xa lookups that failed).
//
// The environment object at this+0x40 vends: A = slot 10 (the list manager: slot 15 next index, slot 16
// entry), B = slot 24 (stack objects by key: slot 14, count slot 10, entry slot 13), C = slot 26 (objects by
// id: slot 29; by entry: slot 42; lists: slot 83), D = slot 29 (records by id: slot 7).
//
// Modes:
//   0x00 0x02 0x06 0x0c 0x19 0x1b 0x21 0x25 (and any unlisted one, which throws 0x1e): the std::list of all
//        objects (this+0x1c4 -> slot 31), starting after the object whose id is in the destination.
//        0x00 any; 0x02 not slot 98 == 2; 0x06 attr 0x3b == frame slot 30(0); 0x0c attr 0x3b == this slot
//        19(0); 0x19 slot 131 == this slot 19(0); 0x1b attr 0x4c or 0x4d nonzero; 0x21 definition field
//        0x1c nonzero; 0x25 definition field 0x2f == this slot 19(0). All also apply the common filter.
//   0x01 0x28..0x2d: entries of the container this slot 63 -> slot 45, filtered by the entry's state.
//   0x03 the stack object's interface 0x5a6216f6 -> related object id.
//   0x04 0x12: objects in a stack object's container.
//   0x05 0x2e..0x33: list manager indices, filtered by state.
//   0x07 list manager indices until an entry's definition number equals the key.
//   0x08 the objects at one tile (an Edith iterator).
//   0x09 0x22: the neighbour tiles (4 / 8) of a stack object.
//   0x0a always false.
//   0x0b 0x0d 0x0e 0x0f 0x10 0x11 0x13..0x18 0x1a 0x1c..0x20 0x23 0x24 0x26 0x27: see the functions.

namespace {

[[noreturn]] void Throw(int code)
{
    throw static_cast<short>(code);
}

// Calls slot byteOffset/4 of obj's vtable (__thiscall).
template <typename R, typename... A> inline R VC(void *obj, int byteOffset, A... args)
{
    return VirtualSlot<R(__thiscall *)(void *, A...)>(obj, byteOffset / 4)(obj, args...);
}

// A reference-counted object held by a stack local (the 0x57c2f0 / 0x1dae60 / 0x57ca80 / 0x1cbd45 family):
// AddRef is slot 1, Release slot 2.
struct AutoRef {
    void *p = nullptr;
    AutoRef() = default;
    explicit AutoRef(void *q) : p(q)
    {
        if (p) VC<void>(p, 4);
    }
    AutoRef(const AutoRef &) = delete;
    AutoRef &operator=(const AutoRef &) = delete;
    ~AutoRef()
    {
        if (p) VC<void>(p, 8);
    }
    // Clears the reference (releasing the old object); returns the address for an out-parameter.
    void **Reset()
    {
        void *old = p;
        p = nullptr;
        if (old) VC<void>(old, 8);
        return &p;
    }
    // AddRef the new object, store it, release the old one.
    void Assign(void *q)
    {
        if (q == p) return;
        void *old = p;
        if (q) VC<void>(q, 4);
        p = q;
        if (old) VC<void>(old, 8);
    }
};

// The destination operand: target, and the range it accepts.
struct Dest {
    uint32_t mUnknown0;
    int16_t *mTarget;
    int16_t mMin;
    int16_t mMax;

    // The current value clamped to [mMin, mMax], 0 without a target (0x512030).
    int16_t Get() const
    {
        int v = mTarget ? *mTarget : 0;
        int lo = mMin, hi = mMax;
        int r = (v >= lo) ? v : lo;
        if (r > hi) r = hi;
        return static_cast<int16_t>(r);
    }
    // Stores the low word of v when there is a target and it is in range, else throws 0x14.
    void Store(uint32_t v) const
    {
        int16_t s = static_cast<int16_t>(v);
        if (!mTarget || s < mMin || s > mMax) Throw(0x14);
        *mTarget = s;
    }
    // Clamps the value at the target in place (min, then max).
    void ClampInPlace() const
    {
        if (!mTarget) return;
        if (*mTarget < mMin) *mTarget = mMin;
        if (*mTarget > mMax) *mTarget = mMax;
    }
};
static_assert(sizeof(Dest) == 12, "Dest layout");

// std::list<void *> as the game's MSVC builds it: a sentinel node, begin = head->next, end = head.
struct ListNode {
    ListNode *next;
    ListNode *prev;
    void *value;
};
struct ListObj {
    ListNode *head;
};
// std::vector of pointers (first, last at +0, +4) and of 16-bit values.
struct PtrVec {
    void **first;
    void **last;
};
struct U16Vec {
    uint16_t *first;
    uint16_t *last;
    uint16_t *end;
    uint32_t Size() const
    {
        return static_cast<uint32_t>((reinterpret_cast<const char *>(last) - reinterpret_cast<const char *>(first)) >> 1);
    }
};
inline PtrVec *AsVec(void *p)
{
    return static_cast<PtrVec *>(p);
}

// An Edith object iterator (vtable 0xe91a6c, 0x14 bytes): constructed by 0xc0bd34 from a 3-byte tile
// position; slot 7 advances, slot 8 returns the current object; "empty" is the field at +0xc being null
// (0x744eb0).
struct EdithIter {
    alignas(4) uint8_t raw[0x20];
    explicit EdithIter(const void *pos)
    {
        OriginalFunction<void *(__thiscall *)(void *, const void *), 0xc0bd34>()(raw, pos);
    }
    EdithIter(const EdithIter &) = delete;
    ~EdithIter() { OriginalFunction<void(__thiscall *)(void *), 0xc0bd79>()(raw); }
    bool Empty() const { return *reinterpret_cast<void *const *>(raw + 0xc) == nullptr; }
    void Advance() { VC<void>(raw, 0x1c); }
    void *Current() { return VC<void *>(raw, 0x20); }
    // cEdithObjectIterator::vf4 (0xc0c026, ret 4): re-reads the object list at a tile position.
    void Rewind(const void *pos) { OriginalFunction<bool(__thiscall *)(void *, const void *), 0xc0c026>()(raw, pos); }
};

// A std::vector<uint16_t> filled by game code; freed as a SimIdVector (our 0x4f2110).
struct U16VecGuard {
    U16Vec v{nullptr, nullptr, nullptr};
    U16VecGuard() = default;
    U16VecGuard(const U16VecGuard &) = delete;
    ~U16VecGuard() { reinterpret_cast<SimIdVectorTidy *>(&v)->Destroy(); }
};

// 0x7dcb60 (cdecl): three signed tile bytes -> (y, x, level - 1).
inline void TileFromPos(int32_t out[3], const uint8_t pos[3])
{
    out[0] = static_cast<int8_t>(pos[1]);
    out[1] = static_cast<int8_t>(pos[0]);
    out[2] = static_cast<int8_t>(pos[2]) - 1;
}

// Original helpers of the tile modes (9 and 0x22): the lot's tile service and the two object-at-tile lookups.
inline void *TileService()
{
    return OriginalFunction<void *(__cdecl *)(), 0x20e0b2>()();
}
inline bool TileValid(void *service, const int32_t tile[3])
{
    return VC<bool>(service, 0x11c, tile);
}

struct Run {
    SetToNextObject *self;
    void *frame;
    const uint8_t *prm;
    void *A, *B, *C, *D;
    Dest dest;
    uint8_t mode;
    bool bit0;
    uint8_t flags;
    uint32_t idx;
    uint32_t cmpVal;

    Run(SetToNextObject *s, void *f, const uint8_t *p);

    // --- object helpers (offsets are bytes into the vtable) ---
    static bool Flag(void *o, uint32_t mask) { return VC<bool>(o, 0x64, mask); }
    static uint8_t Attr8(void *o, uint32_t a) { return VC<uint8_t>(o, 0x34, a); }
    static int16_t Attr16(void *o, uint32_t a) { return VC<int16_t>(o, 0x34, a); }
    static uint16_t Id(void *o) { return VC<uint16_t>(o, 0xbc); }
    static bool Flagged(void *o) { return Flag(o, 0x10000) || Flag(o, 0x20000); }

    // The filter the iterating modes apply to a candidate: true when it is rejected.
    bool Rejects(void *o) const
    {
        if (Flag(o, 0x10000)) return true;
        if (Flag(o, 0x20000)) return true;
        if (!bit0 && (Attr8(o, 0x38) & 5)) return true;
        if (!(flags & 4) && Attr16(o, 0x6b) != 0) return true;
        if (static_cast<int16_t>(idx) > -1 && Attr16(o, idx) != static_cast<int16_t>(cmpVal)) return true;
        return false;
    }

    bool FailZero()
    {
        dest.Store(0);
        return false;
    }
    bool EndCheck() const { return dest.Get() != 0; }

    void *ObjectById(int32_t id) { return VC<void *>(C, 0x74, id); }
    int16_t SelfParam(uint32_t n) { return VC<int16_t>(self, 0x4c, n); }
    uint16_t SelfParamU(uint32_t n) { return VC<uint16_t>(self, 0x4c, n); }
    // A slot 15: the next index after a position (clamped value passed as in the original).
    uint16_t NextIndexZ() { return VC<uint16_t>(A, 0x3c, static_cast<uint32_t>(static_cast<uint16_t>(dest.Get())), 0); }
    uint16_t NextIndexS() { return VC<uint16_t>(A, 0x3c, static_cast<int32_t>(dest.Get()), 0); }
    void *EntryAt(int32_t v, int flag) { return VC<void *>(A, 0x40, v, flag); }

    bool Execute();
    bool ModeAll();
    bool ModeEntries();
    bool Mode03();
    bool Mode04();
    bool Mode05();
    bool Mode07();
    bool Mode08();
    bool Mode09();
    bool Mode0B();
    bool Mode0D();
    bool Mode0E();
    bool Mode0F();
    bool Mode10();
    bool Mode11();
    bool Mode13();
    bool Mode14();
    bool Mode15();
    bool Mode16(bool slot6);
    bool Mode18();
    bool Mode1A();
    bool Mode1C();
    bool Mode1D();
    bool Mode1E();
    bool Mode1F();
    bool Mode20();
    bool Mode22();
    bool Mode23();
    bool Mode24();
    bool Mode26();
    bool Mode27();
};

Run::Run(SetToNextObject *s, void *f, const uint8_t *p) : self(s), frame(f), prm(p)
{
    // Four getters on the environment object (the holder at this+0x40 is re-read for each).
    A = VC<void *>(self->mEnvironment, 0x28);
    B = VC<void *>(self->mEnvironment, 0x60);
    C = VC<void *>(self->mEnvironment, 0x68);
    D = VC<void *>(self->mEnvironment, 0x74);

    dest.mUnknown0 = 0;
    dest.mTarget = nullptr;
    dest.mMin = -32768;
    dest.mMax = 32767;

    const bool hasOperand = static_cast<int8_t>(prm[8]) < 0;
    const uint32_t operandId = hasOperand ? prm[0xb] : 0;
    const uint32_t operandScope = hasOperand ? prm[9] : 10;
    using ReadFn = int32_t(__thiscall *)(void *, uint32_t, uint32_t, Dest *, float **, uint32_t, uint32_t, uint32_t);
    void *sub = &self->mTreeSimVTable;
    VirtualCall<ReadFn, 7>(sub, operandScope, operandId, &dest, nullptr, 0, 0, 0);
    if (!dest.mTarget) Throw(0x13);

    mode = prm[8] & 0x7f;
    idx = (prm[0xc] & 2) ? *reinterpret_cast<const uint16_t *>(prm + 0xd) : 0xffffffffu;
    cmpVal = *reinterpret_cast<const uint16_t *>(prm + 0xf);
    bit0 = (prm[0xc] & 1) != 0;
    flags = prm[0xc];
}

bool Run::Execute()
{
    switch (mode) {
    case 0x01:
    case 0x28:
    case 0x29:
    case 0x2a:
    case 0x2b:
    case 0x2c:
    case 0x2d:
        return ModeEntries();
    case 0x03: return Mode03();
    case 0x04:
    case 0x12: return Mode04();
    case 0x05:
    case 0x2e:
    case 0x2f:
    case 0x30:
    case 0x31:
    case 0x32:
    case 0x33: return Mode05();
    case 0x07: return Mode07();
    case 0x08: return Mode08();
    case 0x09: return Mode09();
    case 0x0a: return false;
    case 0x0b: return Mode0B();
    case 0x0d: return Mode0D();
    case 0x0e: return Mode0E();
    case 0x0f: return Mode0F();
    case 0x10: return Mode10();
    case 0x11: return Mode11();
    case 0x13: return Mode13();
    case 0x14: return Mode14();
    case 0x15: return Mode15();
    case 0x16: return Mode16(true);
    case 0x17: return Mode16(false);
    case 0x18: return Mode18();
    case 0x1a: return Mode1A();
    case 0x1c: return Mode1C();
    case 0x1d: return Mode1D();
    case 0x1e: return Mode1E();
    case 0x1f: return Mode1F();
    case 0x20: return Mode20();
    case 0x22: return Mode22();
    case 0x23: return Mode23();
    case 0x24: return Mode24();
    case 0x26: return Mode26();
    case 0x27: return Mode27();
    default: return ModeAll(); // 0x00 0x02 0x06 0x0c 0x19 0x1b 0x21 0x25, and everything else (which throws 0x1e)
    }
}

// --- the std::list of all objects ---------------------------------------------------------------------

bool Run::ModeAll()
{
    ListObj *list = static_cast<ListObj *>(VC<void *>(self->mObjectListOwner, 0x7c));
    ListNode *cur = nullptr;
    void *found = nullptr;
    if (dest.Get() == 0) {
        cur = list->head->next; // begin
    } else {
        found = ObjectById(dest.Get());
        ListNode *end = list->head;
        ListNode *n = list->head->next;
        while (n != end && n->value != found) n = n->next; // std::find of the object
        // The original post-increments the find result even when it is end (then it wraps to begin).
        cur = n->next;
    }
    // A zero or unknown id only continues when the position is begin.
    const bool proceed = found != nullptr || cur == list->head->next;
    if (proceed) {
        switch (mode) {
        case 0x00:
            while (cur != list->head) {
                void *o = cur->value;
                if (o) {
                    if (!Rejects(o)) break;
                }
                cur = cur->next;
            }
            break;
        case 0x02:
            while (cur != list->head) {
                if (VC<int32_t>(cur->value, 0x188) != 2) { // slot 98
                    void *o = cur->value;
                    if (o) {
                        if (!Rejects(o)) break;
                    }
                }
                cur = cur->next;
            }
            break;
        case 0x06: {
            uint16_t want = VC<uint16_t>(frame, 0x78, 0);
            while (cur != list->head) {
                if (Attr16(cur->value, 0x3b) == static_cast<int16_t>(want)) {
                    void *o = cur->value;
                    if (o) {
                        if (!Rejects(o)) break;
                    }
                }
                cur = cur->next;
            }
            break;
        }
        case 0x0c: {
            uint16_t want = SelfParamU(0);
            while (cur != list->head) {
                if (Attr16(cur->value, 0x3b) == static_cast<int16_t>(want)) {
                    void *o = cur->value;
                    if (o) {
                        if (!Rejects(o)) break;
                    }
                }
                cur = cur->next;
            }
            break;
        }
        case 0x19: {
            uint16_t want = SelfParamU(0);
            while (cur != list->head) {
                if (VC<int16_t>(cur->value, 0x20c) == static_cast<int16_t>(want)) {
                    void *o = cur->value;
                    if (o) {
                        if (!Rejects(o)) break;
                    }
                }
                cur = cur->next;
            }
            break;
        }
        case 0x1b:
            while (cur != list->head) {
                bool take = Attr16(cur->value, 0x4c) != 0;
                if (!take) take = Attr16(cur->value, 0x4d) != 0;
                if (take) {
                    void *o = cur->value;
                    if (o) {
                        if (!Rejects(o)) break;
                    }
                }
                cur = cur->next;
            }
            break;
        case 0x21:
            while (cur != list->head) {
                void *x = cur->value;
                void *def = VC<void *>(x, 0xf4);
                void *rec = VC<void *>(def, 0x10);
                int32_t v = VC<int32_t>(rec, 0xc, 0x1c);
                if (v != 0) {
                    void *o = cur->value;
                    if (o) {
                        if (!Rejects(o)) break;
                    }
                }
                cur = cur->next;
            }
            break;
        case 0x25: {
            uint16_t want = SelfParamU(0);
            while (cur != list->head) {
                void *o = cur->value; // not null-checked
                if (VC<void *>(o, 0xf4) == nullptr) break;
                void *d1 = VC<void *>(o, 0xf4);
                if (VC<void *>(d1, 0x10) == nullptr) break;
                void *d2 = VC<void *>(o, 0xf4);
                void *rec = VC<void *>(d2, 0x10);
                int32_t v = VC<int32_t>(rec, 0xc, 0x2f);
                if (v == static_cast<int16_t>(want)) {
                    if (!Rejects(o)) break;
                }
                cur = cur->next;
            }
            break;
        }
        default: Throw(0x1e);
        }
    }
    if (cur == list->head) return false;
    uint16_t id = Id(cur->value);
    dest.Store(id);
    return true;
}

// --- modes 1, 0x28..0x2d: entries of a container -----------------------------------------------------

bool Run::ModeEntries()
{
    void *w = VC<void *>(self, 0xfc);
    PtrVec *cont = AsVec(VC<void *>(w, 0xb4));
    uint16_t cur = static_cast<uint16_t>(dest.Get());
    void **it = cont->first;
    if (cur != 0) {
        it = cont->first;
        while (it != cont->last) {
            void *x = *it;
            void *o = VC<void *>(x, 0x20);
            if (Id(o) == cur) {
                ++it;
                break;
            }
            ++it;
        }
    }
    while (it != cont->last) {
        void *x = *it;
        bool take = true;
        if (mode != 0x28) {
            uint16_t st = VC<uint16_t>(x, 0x28, 0xba, 1);
            switch (mode) {
            case 0x01: take = st == 0; break;
            case 0x29: take = st != 0; break;
            case 0x2a: take = st == 1 || st == 2; break;
            case 0x2b: take = st == 1; break;
            case 0x2c: take = st == 2; break;
            case 0x2d: take = st == 3; break;
            default: take = false; break;
            }
        }
        if (take) {
            void *o = VC<void *>(*it, 0x20);
            if (o) {
                if (!Rejects(o)) break;
            }
        }
        ++it;
    }
    uint32_t id = (it == cont->last) ? 0 : Id(VC<void *>(*it, 0x20));
    dest.Store(id);
    return dest.Get() != 0;
}

// --- mode 3 -------------------------------------------------------------------------------------------

bool Run::Mode03()
{
    int16_t id = dest.Get();
    void *o = ObjectById(id);
    if (o && VC<bool>(o, 0x1a0)) { // slot 104
        bool ok = true;
        if (Flag(o, 0x10000) || Flag(o, 0x20000)) ok = VC<bool>(self, 0x2c0, o); // slot 176
        if (ok) {
            AutoRef r;
            VC<void>(o, 0, 0x5a6216f6u, r.Reset()); // QueryInterface; the result is not checked
            void *first = VC<void *>(r.p, 0x1c);
            void *sel;
            if (first != nullptr) sel = VC<void *>(r.p, 0x1c);
            else sel = VC<void *>(r.p, 0x18);
            r.Assign(sel);
            void *rel = VC<void *>(r.p, 0x2c);
            uint16_t v = Id(rel);
            dest.Store(v);
            return true;
        }
    }
    return FailZero();
}

// --- modes 4, 0x12 ------------------------------------------------------------------------------------

bool Run::Mode04()
{
    uint32_t key;
    if (mode == 4) {
        key = *reinterpret_cast<const uint32_t *>(prm + 4);
    } else {
        int16_t hi = SelfParam(1);
        uint16_t lo = SelfParamU(0);
        key = lo | (static_cast<uint32_t>(static_cast<int32_t>(hi)) << 16);
    }
    void *so = VC<void *>(B, 0x38, key);
    if (!so) return false;
    void *w = VC<void *>(self, 0xfc);
    PtrVec *cont = AsVec(VC<void *>(w, 0x88, so));
    void **it = nullptr;
    if (dest.Get() == 0) {
        it = cont->first;
    } else {
        it = cont->first;
        while (it != cont->last) {
            void *x = *it;
            int16_t c = dest.Get();
            if (Id(x) == static_cast<uint16_t>(c)) {
                ++it;
                break;
            }
            ++it;
        }
    }
    while (it != cont->last) {
        void *x = *it;
        if (x) {
            if (!Rejects(x)) break;
        }
        ++it;
    }
    uint32_t id = (it == cont->last) ? 0 : Id(*it);
    dest.Store(id);
    return dest.Get() != 0;
}

// --- modes 5, 0x2e..0x33 ------------------------------------------------------------------------------

bool Run::Mode05()
{
    uint16_t n = NextIndexS();
    dest.Store(n);
    for (;;) {
        int16_t v = dest.Get();
        if (v == 0) break;
        void *obj = EntryAt(v, 1);
        bool advance = false;
        if (mode != 0x2e) {
            uint16_t st = VC<uint16_t>(obj, 0x40, 0xba);
            switch (mode) {
            case 0x05: advance = st != 0; break;
            case 0x2f: advance = st == 0; break;
            case 0x30: advance = !(st == 1 || st == 2); break;
            case 0x31: advance = st != 1; break;
            case 0x32: advance = st != 2; break;
            case 0x33: advance = st != 3; break;
            default: advance = true; break;
            }
        }
        if (!advance) {
            int32_t ref = VC<int32_t>(obj, 0x3c);
            void *o2 = VC<void *>(C, 0xa8, ref); // slot 42: object of an entry
            if (!o2) break;
            if (!Rejects(o2)) break;
        }
        n = NextIndexS();
        dest.Store(n);
    }
    return dest.Get() != 0;
}

// --- mode 7: scan list manager indices for an entry whose definition number matches ---------------------

bool Run::Mode07()
{
    uint16_t n = NextIndexZ();
    dest.Store(n);
    const uint32_t key = *reinterpret_cast<const uint32_t *>(prm + 4);
    void *so = VC<void *>(B, 0x38, key);
    if (!so) return false;
    void *def = VC<void *>(so, 0x10);
    int32_t type = VC<int32_t>(def, 0xc, 9);
    for (;;) {
        if (dest.Get() == 0) return false;
        void *e = EntryAt(dest.Get(), 1);
        if (e) {
            void *x = VC<void *>(e, 0x28);
            int32_t v;
            if (type == 0x10) {
                if (!x) {
                    v = 0;
                } else {
                    void *y = VC<void *>(x, 0x10);
                    v = VC<int32_t>(y, 0xc, 0xcb);
                }
            } else {
                if (!x) v = 0;
                else v = VC<int32_t>(x, 0x70);
            }
            if (v == static_cast<int32_t>(key)) return true;
        }
        n = NextIndexS();
        dest.Store(n);
    }
}

// --- mode 8: the objects at the tile of the destination object -------------------------------------------

bool Run::Mode08()
{
    if (dest.Get() == 0) return false;
    void *o = ObjectById(dest.Get());
    if (!o) return false;
    if (!VC<bool>(o, 0x2f0)) return false;
    if (Flag(o, 0x10000)) return false;
    if (Flag(o, 0x20000)) return false;
    if (!bit0 && (Attr8(o, 0x38) & 5)) return false;
    if (static_cast<int16_t>(idx) > -1 && Attr16(o, idx) != static_cast<int16_t>(cmpVal)) return false;
    if (VC<uint16_t>(o, 0x34, 0x2a) & 0x2000) Throw(0x17);

    uint8_t pos[4];
    void *posPtr = VC<void *>(o, 0x194, pos);
    EdithIter it(posPtr);
    while (!it.Empty()) {
        void *cur = it.Current();
        int16_t c = dest.Get();
        if (Id(cur) == static_cast<uint16_t>(c)) break;
        it.Advance();
    }
    if (it.Empty()) return false;
    bool again;
    do {
        it.Advance();
        if (it.Empty()) {
            uint8_t pos2[4];
            void *p2 = VC<void *>(o, 0x194, pos2);
            it.Rewind(p2);
        }
        void *cur = it.Current();
        int16_t c = dest.Get();
        if (Id(cur) == static_cast<uint16_t>(c)) {
            again = false;
        } else {
            again = true;
            void *cur2 = it.Current();
            if (!Flag(cur2, 0x10000) && !Flag(cur2, 0x20000)) {
                bool rej = false;
                if (!bit0) {
                    void *cur3 = it.Current();
                    if (Attr8(cur3, 0x38) & 5) rej = true;
                }
                if (!rej && static_cast<int16_t>(idx) > -1) {
                    void *cur4 = it.Current();
                    if (Attr16(cur4, idx) != static_cast<int16_t>(cmpVal)) rej = true;
                }
                if (!rej) {
                    void *cur5 = it.Current();
                    uint8_t pa[4], pb[4];
                    uint8_t *a = VC<uint8_t *>(cur5, 0x194, pa);
                    uint8_t *b = VC<uint8_t *>(o, 0x194, pb);
                    if (a[0] == b[0] && a[1] == b[1] && a[2] == b[2]) again = false;
                }
            }
        }
    } while (again);
    void *cur = it.Current();
    uint16_t id = Id(cur);
    dest.Store(id);
    return true;
}

// --- modes 9 and 0x22: neighbour tiles of a stack object ----------------------------------------------------

namespace {
// Mode 9's rotation over the four neighbours: (dx, dy) in bl/bh. False when the sweep is over.
bool Rotate4(uint8_t &bl, uint8_t &bh)
{
    if (static_cast<int8_t>(bl) > 0) {
        bl = 0;
        bh = 1;
    } else if (static_cast<int8_t>(bl) < 0) {
        bl = 0;
        bh = 0xff;
    } else {
        if (static_cast<int8_t>(bh) <= 0) return false;
        bl = 0xff;
        bh = 0;
    }
    return true;
}

// Mode 0x22's rotation over the eight neighbours and the tile itself: (bh, bl).
bool Rotate8(uint8_t &bh, uint8_t &bl)
{
    if (bh == 1) {
        if (bl == 0) bl = bh;
        else if (bl == 1) bh = 0;
        else if (bl == 0xff) {
            bh = 0;
            bl = 0;
        } else return false;
    } else if (bh == 0) {
        if (bl == 1) bh = 0xff;
        else if (bl == 0xff) bh = 1;
        else return false;
    } else if (bh == 0xff) {
        if (bl == 1) bl = 0;
        else if (bl == 0) bl = 0xff;
        else if (bl == 0xff) {
            bh = 0;
            bl = 0xff;
        } else return false;
    } else {
        return false;
    }
    return true;
}
} // namespace

bool Run::Mode09()
{
    void *cmpObj = ObjectById(dest.Get());
    const uint32_t k = prm[0xa];
    const uint32_t count = VC<uint8_t>(frame, 0x6c);
    if (k >= count) Throw(8);
    int16_t id = VC<int16_t>(frame, 0x7c, k);
    void *o = ObjectById(id);
    if (!o) Throw(0xa);
    if (Flagged(o)) Throw(0x17);
    uint8_t a3[4];
    VC<void *>(o, 0x194, a3);

    uint8_t bl, bh;
    bool first;
    if (!cmpObj) {
        first = true;
        bl = 1;
        bh = 0;
    } else {
        first = false;
        uint8_t b3[4];
        const uint8_t *q = VC<uint8_t *>(cmpObj, 0x194, b3);
        bl = static_cast<uint8_t>(q[0] - a3[0]);
        bh = static_cast<uint8_t>(q[1] - a3[1]);
    }
    for (;;) {
        if (!first) {
            if (!Rotate4(bl, bh)) return false;
        }
        first = false;
        uint8_t pos[3] = {static_cast<uint8_t>(a3[0] + bl), static_cast<uint8_t>(a3[1] + bh), a3[2]};
        int32_t tile[3];
        TileFromPos(tile, pos);
        void *service = TileService();
        if (!TileValid(service, tile)) continue;
        void *cand = OriginalFunction<void *(__cdecl *)(const int32_t *, int), 0x7e2790>()(tile, 0);
        if (!cand) continue;
        int16_t lo = VC<int16_t>(o, 0x20c);
        int16_t lc = VC<int16_t>(cand, 0x20c);
        if (lc != lo) continue;
        uint16_t cid = Id(cand);
        dest.Store(cid);
        return true;
    }
}

bool Run::Mode22()
{
    void *cmpObj = ObjectById(dest.Get());
    const uint32_t k = prm[0xa];
    const uint32_t count = VC<uint8_t>(frame, 0x6c);
    if (k >= count) Throw(8);
    int16_t id = VC<int16_t>(frame, 0x7c, k);
    void *o = ObjectById(id);
    if (!o) Throw(0xa);
    if (Flagged(o)) Throw(0x17);
    uint8_t a3[4];
    VC<void *>(o, 0x194, a3);

    uint8_t bl, bh;
    bool first;
    if (!cmpObj) {
        first = true;
        bl = 0;
        bh = 1;
    } else {
        first = false;
        uint8_t b3[4];
        VC<void *>(cmpObj, 0x194, b3);
        int32_t tile[3];
        TileFromPos(tile, b3);
        void *service = TileService();
        if (TileValid(service, tile)) {
            EdithIter it(b3);
            while (!it.Empty()) {
                void *cur = it.Current();
                int16_t c = dest.Get();
                if (Id(cur) == static_cast<uint16_t>(c)) break;
                it.Advance();
            }
            if (!it.Empty()) {
                it.Advance();
                if (!it.Empty()) {
                    for (;;) {
                        void *cur = it.Current();
                        uint16_t oid = Id(o);
                        uint16_t cid = Id(cur);
                        if (cid != oid) {
                            void *cur2 = it.Current();
                            if (!Flag(cur2, 0x10000) && !Flag(cur2, 0x20000)) {
                                void *cur3 = it.Current();
                                uint16_t nid = Id(cur3);
                                dest.Store(nid);
                                return true;
                            }
                        }
                        it.Advance();
                        if (it.Empty()) break;
                    }
                }
            }
        }
        bh = static_cast<uint8_t>(b3[0] - a3[0]);
        bl = static_cast<uint8_t>(b3[1] - a3[1]);
    }
    for (;;) {
        if (!first) {
            if (!Rotate8(bh, bl)) return false;
        }
        first = false;
        uint8_t pos[3] = {static_cast<uint8_t>(a3[0] + bh), static_cast<uint8_t>(a3[1] + bl), a3[2]};
        int32_t tile[3];
        TileFromPos(tile, pos);
        void *service = TileService();
        if (!TileValid(service, tile)) continue;
        void *cand = OriginalFunction<void *(__cdecl *)(const int32_t *, int), 0x7deff0>()(tile, 0);
        if (!cand) continue;
        int16_t lo = VC<int16_t>(o, 0x20c);
        int16_t lc = VC<int16_t>(cand, 0x20c);
        if (lc != lo) continue;
        uint16_t oid = Id(o);
        uint16_t cid = Id(cand);
        if (cid == oid) {
            // The tile's own object: step to the next unflagged object in its list.
            uint8_t b3[4];
            VC<void *>(cand, 0x194, b3);
            EdithIter it(b3);
            while (!it.Empty()) {
                void *cur = it.Current();
                uint16_t a = Id(cur);
                uint16_t b = Id(cand);
                if (a == b) break;
                it.Advance();
            }
            cand = nullptr;
            if (!it.Empty()) {
                it.Advance();
                if (!it.Empty()) {
                    for (;;) {
                        void *cur = it.Current();
                        if (!Flag(cur, 0x10000) && !Flag(cur, 0x20000)) break;
                        it.Advance();
                        if (it.Empty()) break;
                    }
                    if (!it.Empty()) cand = it.Current();
                }
            }
        }
        if (!cand) continue;
        uint16_t nid = Id(cand);
        dest.Store(nid);
        return true;
    }
}

// --- mode 0x0b ------------------------------------------------------------------------------------------

bool Run::Mode0B()
{
    void *x = VC<void *>(self->mEnvironment, 0x18);
    void *y = VC<void *>(x, 0x34);
    int16_t g = dest.Get();
    uint16_t v = VC<uint16_t>(y, 0x54, static_cast<int32_t>(g));
    dest.Store(v);
    return dest.Get() != 0;
}

// --- modes 0x0d and 0x18: a path walker interface asked for the next point -----------------------------------

bool Run::Mode0D()
{
    AutoRef world(VC<void *>(self, 0xfc));
    AutoRef so(VC<void *>(world.p, 0x74, static_cast<int32_t>(self->mField96)));
    if (!so.p) Throw(0x17);
    AutoRef walker;
    VC<bool>(so.p, 0, 0x5a69d3d5u, walker.Reset()); // QueryInterface; the result is not checked
    if (!walker.p) Throw(0x1c);
    void *x = walker.p;
    float fa = 0.0f, fb = 0.0f;
    dest.ClampInPlace();
    bool ok = VC<bool>(x, 0x64, dest.mTarget, &fa, &fb);
    for (;;) {
        if (!dest.mTarget) Throw(0x14);
        int16_t v = *dest.mTarget;
        if (v < dest.mMin) {
            *dest.mTarget = dest.mMin;
            Throw(0x14);
        }
        if (v > dest.mMax) {
            *dest.mTarget = dest.mMax;
            Throw(0x14);
        }
        if (!ok) return false;
        int16_t c = dest.Get();
        if (c == 0) break;
        void *o = ObjectById(c);
        if (!o) return false;
        if (!Rejects(o)) break;
        x = walker.p;
        dest.ClampInPlace();
        ok = VC<bool>(x, 0x64, dest.mTarget, &fa, &fb);
    }
    self->mField9e = static_cast<int16_t>(static_cast<int32_t>(fa));
    self->mFieldA0 = worldmath::AngleToFacing(fb); // 0x7dcfe0
    return true;
}

bool Run::Mode18()
{
    AutoRef walker;
    VC<bool>(self, 0, 0x5a69d3d5u, walker.Reset()); // QueryInterface on the object itself
    if (!walker.p) Throw(0x1c);
    const uint32_t w = static_cast<uint16_t>(self->mField96);
    void *x = walker.p;
    dest.ClampInPlace();
    uint8_t ok = VC<uint8_t>(x, 0x68, dest.mTarget, w);
    if (!dest.mTarget) Throw(0x14);
    for (;;) {
        int16_t v = *dest.mTarget;
        if (v < dest.mMin) {
            *dest.mTarget = dest.mMin;
            Throw(0x14);
        }
        if (v > dest.mMax) {
            *dest.mTarget = dest.mMax;
            Throw(0x14);
        }
        if (!ok) return false;
        int16_t c = dest.Get();
        if (c == 0) return true;
        void *o = ObjectById(c);
        if (!o) return false;
        if (!Rejects(o)) return true;
        x = walker.p;
        dest.ClampInPlace();
        ok = VC<uint8_t>(x, 0x68, dest.mTarget, w);
        if (!dest.mTarget) Throw(0x14);
    }
}

// --- mode 0x0e ------------------------------------------------------------------------------------------

bool Run::Mode0E()
{
    AutoRef world(VC<void *>(self, 0xfc));
    int16_t g = dest.Get();
    AutoRef so(VC<void *>(world.p, 0x74, static_cast<int32_t>(g)));
    AutoRef walker;
    VC<bool>(so.p, 0, 0x5a69d3d5u, walker.Reset()); // QueryInterface; the result is not checked
    if (!walker.p) Throw(0x1c);
    void *z = VC<void *>(walker.p, 0x20);
    if (Flagged(z)) Throw(0x17);
    void *e1 = walker.p;
    void *e2 = walker.p;
    int16_t hi = VC<int16_t>(e2, 0x28, 0x5a, 1);
    uint16_t lo = VC<uint16_t>(e1, 0x28, 0x59, 1);
    uint32_t key = lo | (static_cast<uint32_t>(static_cast<int32_t>(hi)) << 16);
    void *sobj = VC<void *>(B, 0x38, key);
    if (!sobj) return false;
    void *w = VC<void *>(self, 0xfc);
    PtrVec *cont = AsVec(VC<void *>(w, 0x88, sobj));
    void **it = cont->first;
    uint32_t id = (it == cont->last) ? 0 : Id(*it);
    dest.Store(id);
    return true;
}

// --- mode 0x0f ------------------------------------------------------------------------------------------

bool Run::Mode0F()
{
    int16_t g = dest.Get();
    void *o = EntryAt(g, 1);
    if (!o) Throw(0x37);
    int16_t hi = VC<int16_t>(o, 0x40, 0x5a);
    uint16_t lo = VC<uint16_t>(o, 0x40, 0x59);
    uint32_t key = lo | (static_cast<uint32_t>(static_cast<int32_t>(hi)) << 16);
    void *sobj = VC<void *>(B, 0x38, key);
    if (!sobj) return false;
    void *w = VC<void *>(self, 0xfc);
    PtrVec *cont = AsVec(VC<void *>(w, 0x88, sobj));
    void **it = cont->first;
    while (it != cont->last) {
        void *x = *it;
        if (!Flagged(x)) break;
        ++it;
    }
    uint32_t id = (it == cont->last) ? 0 : Id(*it);
    dest.Store(id);
    return true;
}

// --- mode 0x10 ------------------------------------------------------------------------------------------

bool Run::Mode10()
{
    void *w = VC<void *>(self, 0xfc);
    PtrVec *cont = AsVec(VC<void *>(w, 0xbc));
    void **it = nullptr;
    if (dest.Get() == 0) {
        it = cont->first;
    } else {
        it = cont->first;
        while (it != cont->last) {
            void *x = *it;
            int16_t c = dest.Get();
            if (Id(x) == static_cast<uint16_t>(c)) {
                ++it;
                break;
            }
            ++it;
        }
    }
    while (it != cont->last) {
        void *x = *it;
        if (!Flagged(x)) break;
        ++it;
    }
    uint32_t id = (it == cont->last) ? 0 : Id(*it);
    dest.Store(id);
    return dest.Get() != 0;
}

// --- mode 0x11 ------------------------------------------------------------------------------------------

bool Run::Mode11()
{
    uint16_t n = NextIndexZ();
    dest.Store(n);
    void *o = EntryAt(dest.Get(), 1);
    const uint16_t t = SelfParamU(0);
    for (;;) {
        if (dest.Get() == 0) return EndCheck();
        uint16_t u = VC<uint16_t>(o, 0x40, 0x3d);
        const bool differ = t != u;
        int32_t ref = VC<int32_t>(o, 0x3c);
        void *p = VC<void *>(C, 0xa8, ref);
        bool stop;
        if (!differ && p != nullptr) stop = Rejects(p);
        else stop = differ;
        if (!stop) return EndCheck();
        n = NextIndexS();
        dest.Store(n);
        o = EntryAt(dest.Get(), 1);
    }
}

// --- modes 0x13 and 0x14: forward and backward over an object's list of ids --------------------------------

bool Run::Mode13()
{
    void *so = ObjectById(self->mField96);
    if (!so || Flagged(so)) return FailZero();
    int32_t k = VC<int32_t>(so, 0xe4);
    if (k < 0) return false;
    void *L = VC<void *>(C, 0x14c, k);
    if (!L) return false;
    if (VC<int32_t>(L, 0x20) == 0) return false;
    int32_t j = 0;
    uint16_t found = 0;
    bool haveFound = false;
    if (dest.Get() == 0) {
        int32_t n = VC<int32_t>(L, 0x20);
        if (n > 0) {
            for (;;) {
                uint16_t id = VC<uint16_t>(L, 0x1c, j);
                void *o = ObjectById(static_cast<int16_t>(id));
                if (o && !Flagged(o)) {
                    found = id;
                    haveFound = true;
                    break;
                }
                ++j;
                if (!(j < VC<int32_t>(L, 0x20))) break;
            }
        }
    } else {
        int32_t n = VC<int32_t>(L, 0x20);
        if (n > 0) {
            for (;;) {
                uint16_t id = VC<uint16_t>(L, 0x1c, j);
                int16_t c = dest.Get();
                if (id == static_cast<uint16_t>(c)) {
                    dest.Store(0);
                    ++j;
                    if (!(j < VC<int32_t>(L, 0x20))) break;
                    for (;;) {
                        uint16_t id2 = VC<uint16_t>(L, 0x1c, j);
                        void *o = ObjectById(static_cast<int16_t>(id2));
                        if (o && !Flagged(o)) {
                            found = id2;
                            haveFound = true;
                            break;
                        }
                        ++j;
                        if (!(j < VC<int32_t>(L, 0x20))) break;
                    }
                    break;
                }
                ++j;
                if (!(j < VC<int32_t>(L, 0x20))) break;
            }
        }
    }
    if (haveFound) dest.Store(found);
    return dest.Get() != 0;
}

bool Run::Mode14()
{
    void *so = ObjectById(self->mField96);
    if (!so) return FailZero();
    int32_t k = VC<int32_t>(so, 0xe4);
    if (k < 0) return false;
    void *L = VC<void *>(C, 0x14c, k);
    if (!L) return false;
    if (VC<int32_t>(L, 0x20) == 0) return false;
    uint16_t found = 0;
    bool haveFound = false;
    if (dest.Get() == 0) {
        int32_t i = VC<int32_t>(L, 0x20) - 1;
        while (i >= 0) {
            uint16_t id = VC<uint16_t>(L, 0x1c, i);
            void *o = ObjectById(static_cast<int16_t>(id));
            if (o && !Flagged(o)) {
                found = id;
                haveFound = true;
                break;
            }
            --i;
        }
    } else {
        int32_t i = VC<int32_t>(L, 0x20) - 1;
        bool at = false;
        while (i >= 0) {
            uint16_t id = VC<uint16_t>(L, 0x1c, i);
            int16_t c = dest.Get();
            if (id == static_cast<uint16_t>(c)) {
                at = true;
                break;
            }
            --i;
        }
        if (at) {
            dest.Store(0);
            // ORIGINAL-BUG: after finding the current id at index i the scan restarts at i - 2, so the
            // entry just before it (i - 1) is never considered.
            i -= 1;
            if (i >= 0) {
                for (;;) {
                    int32_t j = i - 1;
                    i = j;
                    uint16_t id = VC<uint16_t>(L, 0x1c, j);
                    void *o = ObjectById(static_cast<int16_t>(id));
                    if (o && !Flagged(o)) {
                        found = id;
                        haveFound = true;
                        break;
                    }
                    if (!(i >= 0)) break;
                }
            }
        }
    }
    if (haveFound) dest.Store(found);
    return dest.Get() != 0;
}

// --- mode 0x15 ------------------------------------------------------------------------------------------

bool Run::Mode15()
{
    uint8_t flagA = 0;
    int16_t t = SelfParam(0);
    void *o = EntryAt(t, 1);
    if (!o) Throw(0x77);
    uint16_t id = VC<uint16_t>(o, 0x20);
    void *E = VC<void *>(D, 0x1c, static_cast<uint32_t>(id));
    if (!E) return flagA != 0;
    uint32_t pA = 0, pB = 0;
    uint8_t ok0 = VC<uint8_t>(E, 0xc, 0, &pA);
    uint8_t ok1 = VC<uint8_t>(E, 0xc, 1, &pB);
    if (ok0) {
        void *r = EntryAt(static_cast<uint16_t>(pA), 0);
        ok0 &= r ? 0xff : 0;
    }
    if (ok1) {
        void *r = EntryAt(static_cast<uint16_t>(pB), 0);
        ok1 &= r ? 0xff : 0;
    }
    flagA = ok1;
    int16_t c = dest.Get();
    if (c == static_cast<int32_t>(static_cast<uint16_t>(pA))) {
        dest.Store(static_cast<uint16_t>(pB));
    } else if (dest.Get() == static_cast<int32_t>(static_cast<uint16_t>(pB)) && dest.Get() != 0) {
        dest.Store(0);
        flagA = 0;
    } else {
        dest.Store(static_cast<uint16_t>(pA));
        flagA = ok0;
    }
    return flagA != 0;
}

// --- modes 0x16, 0x17: a related record's id list -----------------------------------------------------------

bool Run::Mode16(bool slot6)
{
    int16_t t = SelfParam(0);
    void *o = EntryAt(t, 1);
    if (!o) Throw(0x77);
    uint16_t id = VC<uint16_t>(o, 0x20);
    void *E = VC<void *>(D, 0x1c, static_cast<uint32_t>(id));
    if (!E) return FailZero();
    U16VecGuard g;
    uint8_t ok = VC<uint8_t>(E, slot6 ? 0x18 : 0x14, &g.v);
    if (!ok) return FailZero();
    U16Vec &vec = g.v;
    uint32_t sel = vec.Size();
    uint32_t j = 0;
    bool foundCur = false;
    if (vec.Size() != 0) {
        for (;;) {
            int16_t c = dest.Get();
            if (static_cast<int32_t>(c) == static_cast<int32_t>(vec.first[j])) {
                int16_t c2 = dest.Get();
                void *r = EntryAt(c2, 0);
                if (r) {
                    foundCur = true;
                    break;
                }
            }
            ++j;
            if (!(j < vec.Size())) break;
        }
    }
    if (!foundCur) j = 0xffffffffu;
    ++j;
    if (j < vec.Size()) {
        for (;;) {
            void *r = EntryAt(static_cast<int32_t>(static_cast<uint32_t>(vec.first[j])), 0);
            if (r) {
                sel = j;
                break;
            }
            ++j;
            if (!(j < vec.Size())) break;
        }
    }
    if (sel >= vec.Size()) {
        dest.Store(0);
        return false;
    }
    dest.Store(vec.first[sel]);
    return true;
}

// --- modes 0x1a, 0x1d, 0x1e ---------------------------------------------------------------------------------

bool Run::Mode1A()
{
    int16_t id = VC<int16_t>(frame, 0x30);
    void *o = ObjectById(id);
    if (!o) return false;
    if (Flag(o, 0x10000)) return false;
    return !Flag(o, 0x20000);
}

bool Run::Mode1D()
{
    int16_t id = VC<int16_t>(frame, 0x30);
    void *e = EntryAt(id, 0);
    return e != nullptr;
}

bool Run::Mode1E()
{
    int16_t id = VC<int16_t>(frame, 0x30);
    void *o = ObjectById(id);
    if (!o) return false;
    if (Flag(o, 0x10000)) return false;
    if (Flag(o, 0x20000)) return false;
    AutoRef r;
    return VC<bool>(o, 0, 0x5a69d3d5u, r.Reset());
}

// --- modes 0x1c, 0x23, 0x24, 0x27: scans of the list manager guided by a record -------------------------------

bool Run::Mode1C()
{
    int16_t t = SelfParam(0);
    void *o = EntryAt(t, 1);
    if (!o) Throw(0x77);
    uint16_t id = VC<uint16_t>(o, 0x20);
    void *e = EntryAt(static_cast<int16_t>(id), 1);
    void *Y = VC<void *>(e, 0x30);
    if (!Y) Throw(0x68);
    uint16_t n = NextIndexZ();
    dest.Store(n);
    for (;;) {
        if (dest.Get() == 0) return false;
        void *e2 = EntryAt(dest.Get(), 1);
        if (e2) {
            int16_t c2 = dest.Get();
            int32_t r = VC<int32_t>(Y, 0x24, static_cast<int32_t>(c2), 3);
            if (r != 0) return true;
        }
        n = NextIndexS();
        dest.Store(n);
    }
}

bool Run::Mode23()
{
    int16_t t = SelfParam(0);
    void *o = EntryAt(t, 1);
    const uint16_t k = SelfParamU(1);
    if (!o) Throw(0x77);
    uint16_t id = VC<uint16_t>(o, 0x20);
    void *e = EntryAt(static_cast<int16_t>(id), 1);
    void *Y = VC<void *>(e, 0x30);
    if (!Y) Throw(0x68);
    uint16_t n = NextIndexZ();
    dest.Store(n);
    for (;;) {
        if (dest.Get() == 0) return false;
        void *e2 = EntryAt(dest.Get(), 1);
        if (e2) {
            int16_t c2 = dest.Get();
            int32_t has = VC<int32_t>(Y, 0x14, static_cast<int32_t>(c2));
            if (has != 0) {
                int16_t c3 = dest.Get();
                int16_t v = static_cast<int16_t>(VC<int32_t>(Y, 0x24, static_cast<int32_t>(c3), 0));
                if (v > static_cast<int16_t>(k)) return true;
            }
        }
        n = NextIndexS();
        dest.Store(n);
    }
}

bool Run::Mode24()
{
    int16_t t = SelfParam(0);
    void *o = EntryAt(t, 1);
    if (!o) Throw(0x77);
    uint16_t id = VC<uint16_t>(o, 0x20);
    void *e = EntryAt(static_cast<int16_t>(id), 1);
    void *Y = VC<void *>(e, 0x30);
    if (!Y) Throw(0x68);
    uint16_t n = NextIndexZ();
    dest.Store(n);
    for (;;) {
        if (dest.Get() == 0) return false;
        void *e2 = EntryAt(dest.Get(), 1);
        if (e2) {
            int16_t c2 = dest.Get();
            uint32_t m = VC<uint32_t>(Y, 0x24, static_cast<int32_t>(c2), 1);
            int16_t mask = SelfParam(1);
            if ((m & static_cast<uint32_t>(static_cast<int32_t>(mask))) != 0) return true;
        }
        n = NextIndexS();
        dest.Store(n);
    }
}

bool Run::Mode27()
{
    int16_t t = SelfParam(0);
    void *o = EntryAt(t, 1);
    if (!o) Throw(0x77);
    void *Z = VC<void *>(o, 0x30);
    if (!Z) Throw(0x68);
    uint16_t n = NextIndexZ();
    dest.Store(n);
    for (;;) {
        if (dest.Get() == 0) return EndCheck();
        void *p = EntryAt(dest.Get(), 1);
        if (!p) return EndCheck();
        int16_t c = dest.Get();
        uint16_t q = VC<uint16_t>(o, 0x20);
        // The entry that is the anchor itself, or one whose record has bit 15 set, is stepped over.
        if (static_cast<int16_t>(c) != static_cast<int16_t>(q)) {
            int16_t c3 = dest.Get();
            uint32_t s = VC<uint32_t>(Z, 0x24, static_cast<int32_t>(c3), 1);
            if (!(s & 0x8000)) {
                int32_t ref = VC<int32_t>(p, 0x3c);
                void *r = VC<void *>(C, 0xa8, ref);
                if (!r) return EndCheck();
                if (!Rejects(r)) return EndCheck();
            }
        }
        n = NextIndexS();
        dest.Store(n);
    }
}

// --- mode 0x1f ------------------------------------------------------------------------------------------

bool Run::Mode1F()
{
    int16_t t = SelfParam(0);
    void *o = EntryAt(t, 1);
    if (!o) Throw(0x77);
    uint16_t id = VC<uint16_t>(o, 0x20);
    void *E = VC<void *>(D, 0x1c, static_cast<uint32_t>(id));
    if (!E) return false;
    uint32_t x = 0;
    if (!VC<uint8_t>(E, 0xc, 2, &x)) return false;
    void *r = EntryAt(static_cast<uint16_t>(x), 0);
    if (!r) return false;
    dest.Store(static_cast<uint16_t>(x));
    return true;
}

// --- mode 0x20 ------------------------------------------------------------------------------------------

bool Run::Mode20()
{
    void *so = ObjectById(self->mField96);
    if (!so) return false;
    if (Flagged(so)) Throw(0x17);
    void *R = VC<void *>(so, 0x1b4);
    if (!R) return false;
    uint16_t v;
    if (dest.Get() < 1) {
        v = VC<uint16_t>(R, 0x30);
    } else {
        v = VC<uint16_t>(R, 0x34, static_cast<uint32_t>(static_cast<uint16_t>(dest.Get())));
    }
    dest.Store(v);
    for (;;) {
        const int16_t cur = dest.Get();
        if (cur == -1) break;
        uint32_t cnt = VC<uint32_t>(R, 0x14, static_cast<uint32_t>(static_cast<uint16_t>(dest.Get())));
        if (cnt > 1) {
            uint32_t u = VC<uint32_t>(R, 0x24, static_cast<uint32_t>(static_cast<uint16_t>(dest.Get())), 1);
            if (u >= static_cast<uint32_t>(static_cast<int32_t>(self->mField98))) break;
        }
        v = VC<uint16_t>(R, 0x34, static_cast<uint32_t>(static_cast<uint16_t>(dest.Get())));
        dest.Store(v);
    }
    return dest.Get() != -1;
}

// --- mode 0x26 ------------------------------------------------------------------------------------------

bool Run::Mode26()
{
    const int32_t a = SelfParam(0);
    const int32_t b = SelfParam(1);
    const int32_t count = VC<int32_t>(B, 0x28);
    int32_t i = dest.Get() > 0 ? dest.Get() : 0;
    for (; i < count; ++i) {
        void *E = VC<void *>(B, 0x34, i);
        if (E) {
            void *F = VC<void *>(E, 0x10);
            if (F) {
                int32_t x = VC<int32_t>(F, 0xc, 0x4b);
                int32_t y = VC<int32_t>(F, 0xc, 0x4c);
                bool accept = false;
                if (a < 0) accept = true;
                else if (x == a) {
                    if (b < 0) accept = true;
                    else if (y == b) accept = true;
                }
                if (accept) {
                    int32_t v = VC<int32_t>(E, 0x70);
                    dest.Store(static_cast<uint16_t>(i + 1));
                    VC<void>(self, 0x50, 2, v);
                    VC<void>(self, 0x50, 3, v >> 16);
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace

uint32_t SetToNextObject::SetToNext(void *frame, const void *param)
{
    Run run(this, frame, static_cast<const uint8_t *>(param));
    return run.Execute() ? 1 : 0;
}
