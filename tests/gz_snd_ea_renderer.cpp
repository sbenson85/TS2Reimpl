// Differential test for cGZSndEaRenderer (lane KJ, src/gz/cGZSndEaRenderer.h): the whole class in the standalone shape.
//
// SndEaRenderer_Run: a renderer built by the original constructor (0xb12ce3, game vtables 0xe7ff6c / 0xe7ffa8 / 0xe7ffbc /
// 0xe7ffe0 / 0xe80048 / 0xe80078 / 0xe800b0) or factory (0xb12be6) against one built by ours (the Construct adapter /
// Create), each driven through its own vtables by the same random operations, then destroyed by the last Release (0xb134b5,
// reaching the deleting destructor 0xb12e37 and the destructor 0xb12de1) or by the deleting destructor directly.
// Operations, through every face: QueryInterface 0xb133f0 / AddRef 0xb12e59 / Release 0xb134b5 and their thunks (0xb13485,
// 0xb1348d, 0xb134ad, 0xb13495, 0xb1349d, 0xb134a5; 0xb12e67, 0xb12e6f, 0xb12e8f, 0xb12e77, 0xb12e7f, 0xb12e87; 0xb134d6,
// 0xb134de, 0xb134fe, 0xb134e6, 0xb134ee, 0xb134f6); the service face (GetServiceID 0xb13262, the folded accessors, Init
// 0xb132a5, Shutdown 0xb13c35, also through the thunks 0xb133b8 / 0xb13ce7 / 0xb133b0 / 0xb13cdf); Update 0xb13d27 (the
// listener rebuild 0xb13d42, its callback 0xb12fa6 run by the service on its sounds); cIGZSndRenderer (0xb13a6e with
// 0xb13a4b, 0xb13102 with 0xb130f4, 0xb133e4, 0xb13d1d, the folded IsPaused); cIGZSndEaRenderer slots 5..24 (0xb133c0,
// 0xb13cef, 0xb13d08, 0xb13268, 0xb136af, 0xb13082, 0xb12ea3, 0xb13b5d, 0xb131a5, 0xb12eca, 0xb13b7c, 0xb131c4, 0xb12ef0,
// 0xb13afa, 0xb13133, 0xb13ae8, 0xb1359a, 0xb13746, 0xb13071, 0xb130cf); cIGZSnd3DListener (0xb131e6, 0xb13ba5, 0xb13156,
// 0xb13b11, 0xb12e97, the folded slot 5); cIGZSnd3DListenerEx (0xb1327b, 0xb13c08, 0xb12fda, 0xb13506, 0xb130bc, 0xb13733,
// 0xb13210, 0xb13bde, 0xb130a9, 0xb13720); cIGZSndEaRendererEffects (0xb13119, 0xb13a88, 0xb12f0b, 0xb12f68, 0xb136e3).
//
// Every object the renderer reaches is a recording fake (tests/ts_services_fixture.h): the framework (GetServiceObject's
// cell 0x10c9378), the sound renderer service (slots 30 / 31 / 32 / 35 / 36; 32 runs the callback on three sounds) and
// sounds that answer QueryInterface 0x6b0446eb by a mask. EA's library functions are stand-ins on both sides (they log
// their arguments, write every byte of their out arguments and answer from the op's random stream); the 3 x 3 inverse
// 0x1a62e0 runs for real. The game's operator new / delete (0x824632 / 0x824624) are ours. Compared after every operation:
// the result and out words, every field (the never-written bytes on the 0xc5 route, filled with 0xc5 after a factory),
// the vtable words, the buffer and voice table (size, capacity, contents), the event log and every fake's count.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cGZSndEaRenderer.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace sndea_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{2, "d"}, {3, "d"}, {4, "d"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using R = cGZSndEaRenderer;

constexpr uint32_t kTag = 0x7d000000;      // the renderer under test
constexpr uint32_t kCallbackTag = 0x7c000000; // the listener callback (the original's 0xb12fa6 or ours)
constexpr uint32_t kAwareIid = 0x6b0446eb;

enum : unsigned { kFramework = 0, kService, kSound0, kSounds = 3 };

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }

struct World {
    bool service;      // the framework has the sound renderer service
    uint32_t failRate; // EA calls fail one time in failRate (0: never)
};
World gWorld;
uint8_t *gObject;

uint32_t Word(const uint8_t *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(m + off); }
uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
float Float(uint32_t b)
{
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

// ---- EA's library, stood in for on both sides ----

bool Fails() { return gWorld.failRate && Draw(gWorld.failRate) == 0; }
int Status() { return Fails() ? -1 - int(Draw(3)) : int(Draw(4) == 0 ? Draw(5) : 0); }
void Note(uint32_t rva) { Ev(0xea000000u | rva); }

void __cdecl EaLock() { Note(0xe7362); }
void __cdecl EaUnlock() { Note(0xe7370); }
int __cdecl EaSetLevel(int level) { Note(0xe737d), Ev(uint32_t(level)); return Status(); }
int __cdecl Ea7442(uint32_t v) { Note(0xe7442), Ev(v); return Status(); }
int __cdecl Ea746e(uint32_t v, uint8_t *flag)
{
    Note(0xe746e), Ev(v);
    *flag = uint8_t(Draw(2));
    return Status();
}
// Init needs 0 for success here (0xb1334a: jne fails).
int __cdecl Ea74b2(uint32_t v) { Note(0xe74b2), Ev(v); return Fails() ? int(Draw(2)) * 2 - 1 : 0; }
int __cdecl Ea74f7(uint32_t *out) { Note(0xe74f7); *out = gRng(); return Status(); }
int __cdecl Ea7506(uint32_t zero, uint32_t v, uint32_t *out) { Note(0xe7506), Ev(zero), Ev(v); *out = gRng(); return Status(); }
int __cdecl Ea754c(uint32_t zero, uint32_t v) { Note(0xe754c), Ev(zero), Ev(v); return Status(); }
int __cdecl Ea75ab(uint32_t zero, uint32_t *out) { Note(0xe75ab), Ev(zero); *out = gRng(); return Status(); }
int __cdecl Ea75e1(uint32_t v, uint32_t *out) { Note(0xe75e1), Ev(v); *out = gRng(); return Status(); }
int __cdecl Ea7617(uint32_t a, uint32_t b) { Note(0xe7617), Ev(a), Ev(b); return Status(); }
int __cdecl Ea7668(uint32_t v, uint32_t *out) { Note(0xe7668), Ev(v); *out = gRng(); return Status(); }
// The work buffer: its size, whether there is one and the sum of its bytes (value-initialised: 0).
int __cdecl Ea7748(uint32_t zero, const uint8_t *buffer, uint32_t size)
{
    Note(0xe7748), Ev(zero), Ev(size), Ev(buffer != nullptr);
    uint32_t sum = 0;
    for (uint32_t i = 0; i < size; ++i) sum += buffer[i];
    Ev(sum);
    return Fails() ? int(Draw(2)) * 2 - 1 : 0;
}
bool __cdecl Ea772c() { Note(0xe772c); return Draw(2) != 0; }
int __cdecl Ea7699(uint32_t flags) { Note(0xe7699), Ev(flags); return Status(); }
void __cdecl Ea7739() { Note(0xe7739); }
void __cdecl Ea7768(uint32_t v) { Note(0xe7768), Ev(v); }
// The voice count: 0..6, past the table's size and under it.
int __cdecl Ea7779(uint32_t *out) { Note(0xe7779); *out = Draw(7); return Status(); }
int __cdecl Ea7788(uint32_t count) { Note(0xe7788), Ev(count); return Status(); }
int __cdecl Ea779d(uint32_t zero, uint32_t index, void **out)
{
    Note(0xe779d), Ev(zero), Ev(index), Ev(P(*out));
    *out = Draw(8) == 0 ? nullptr : reinterpret_cast<void *>(uintptr_t(0x4a000000u + Draw(0x1000) * 4));
    return Status();
}
// thiscall on the voice handle
void __fastcall Ea7895(void *voice, void *, uint32_t v) { Note(0xe7895), Ev(P(voice)), Ev(v); }
void __fastcall Ea78bc(void *voice, void *) { Note(0xe78bc), Ev(P(voice)); }
int __fastcall Ea7811(void *voice, void *, float level) { Note(0xe7811), Ev(P(voice)), Ev(Bits(level)); return Status(); }

// ---- fakes ----

uint32_t GetSystemService(Obj *, uint32_t *a)
{
    if (a[0] != 0x23cecba0 || a[1] != 0x106077d2 || !gWorld.service) return 0;
    ++F(kService).refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &F(kService);
    return 1;
}
uint32_t ServiceResult(Obj *, uint32_t *) { return Fails() ? 0x100u : 0x101u; }
// Slot 32 (function, 0, 0): runs the function on each sound, logging its result byte.
uint32_t ServiceForEach(Obj *, uint32_t *a)
{
    auto fn = reinterpret_cast<uint32_t(__cdecl *)(void *)>(uintptr_t(a[0]));
    for (unsigned i = 0; i < kSounds; ++i) Ev(fn(&F(kSound0 + i)) & 0xff);
    return 0;
}
// A sound answers 0x6b0446eb by its mask bit (value[0]); its slot 3 is logged.
uint32_t SoundQuery(Obj *self, uint32_t *a)
{
    if (a[0] == kAwareIid && (self->value[0] & 1)) {
        ++self->refs;
        *reinterpret_cast<void **>(uintptr_t(a[1])) = self;
        return 0x101;
    }
    return 0x100;
}

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

void BuildFakes(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 5);
    svcfx::ResetAll();
    gWorld.service = rng() % 6 != 0;
    static const uint32_t kRates[] = {0, 0, 3, 6, 12};
    gWorld.failRate = kRates[rng() % 5];
    F(kFramework).Set(5, "ddp", 0, &GetSystemService);
    Obj &service = F(kService);
    service.Set(30, "pd", 0, &ServiceResult);
    service.Set(31, "p", 0x3131);
    service.Set(32, "p--", 0, &ServiceForEach);
    service.Set(35, "p", 0, &ServiceResult);
    service.Set(36, "p", 0x3636);
    for (unsigned i = 0; i < kSounds; ++i) {
        Obj &sound = F(kSound0 + i);
        sound.Set(0, "dp", 0, &SoundQuery);
        sound.Set(3, 0, 0x303);
        sound.value[0] = rng() % 2;
    }
    *GameAddress<void *>(0x10c9378) = &F(kFramework);
}

template <class Ret, class... A> Ret Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<Ret(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }

bool gFactory;
// The bytes no code writes (padding and the two vectors the constructor leaves): 0xc5 after a factory's build.
const uint32_t kUnwritten[][2] = {{0x21, 3}, {0x4c, 12}, {0x70, 12}, {0xb6, 2}};

void *Build(difftest::Context &ctx, bool ours)
{
    if (gFactory) {
        void *object = ours ? static_cast<void *>(R::Create()) : ctx.Original<void *(__cdecl *)()>(0xb12be6)();
        for (const auto &u : kUnwritten) std::memset(Face(object, u[0]), 0xc5, u[1]);
        return object;
    }
    void *memory = ::operator new(R::kSize);
    std::memset(memory, 0xc5, R::kSize);
    if (ours) cGZSndEaRendererAdapters::Construct(memory, nullptr);
    else ctx.Original<void *(__thiscall *)(void *)>(0xb12ce3)(memory);
    return memory;
}

const uint32_t kFaces[7][2] = {{0, 0xe7ff6c}, {4, 0xe7ffa8}, {8, 0xe7ffbc}, {0xc, 0xe7ffe0},
                               {0x10, 0xe80048}, {0x14, 0xe80078}, {0x18, 0xe800b0}};

// A std::vector's words: size, capacity, whether it has storage.
void DumpVector(Blob &b, const uint8_t *at, uint32_t element)
{
    const uint32_t first = Word(at, 0), last = Word(at, 4), end = Word(at, 8);
    b.push_back((last - first) / element);
    b.push_back((end - first) / element);
    b.push_back(first != 0);
}

void Dump(Blob &b, void *object, bool ours)
{
    const auto *m = static_cast<const uint8_t *>(object);
    for (const auto &f : kFaces) {
        const bool game = Word(m, f[0]) == P(GameAddress<void>(f[1]));
        b.push_back(ours ? !game : game);
    }
    b.push_back(Word(m, 0x1c));
    b.push_back(Word(m, 0x20));
    DumpVector(b, m + 0x24, 1);
    for (uint32_t off = 0x30; off < 0xb8; off += 4) b.push_back(Word(m, off));
    b.push_back(svcfx::Norm(Word(m, 0xb8)));
    DumpVector(b, m + 0xbc, 4);
    const auto *voices = reinterpret_cast<const uint32_t *>(uintptr_t(Word(m, 0xbc)));
    for (uint32_t i = 0, n = (Word(m, 0xc0) - Word(m, 0xbc)) / 4; i < n; ++i) b.push_back(voices[i]);
}

// Floats: nice values, any bits (NaNs, infinities, denormals, both zeros), or a field moved by -40..40 units in the last
// place (the setters' 32-unit test, across zero).
float PickFloat(uint32_t a, uint32_t around)
{
    static const uint32_t kNice[] = {0, 0x80000000u, 0x3f800000u, 0xbf800000u, 0x7f800000u, 0x7fc00000u, 0x7f800001u,
                                     0x00000001u, 0x80000001u, 0x42fe0000u, 0x4f000000u, 0xcf000000u, 0x3dcccccdu};
    switch (a % 4) {
    case 0: return Float(kNice[(a >> 2) % 13]);
    case 1: return Float(gRng());
    default: {
        int32_t step = int32_t((a >> 2) % 81) - 40;
        if ((a >> 9) % 3 == 0) step = int32_t((a >> 2) % 3) * 32 - 33 + int32_t((a >> 4) % 3); // -33..-31, -1..1, 31..33
        const uint32_t bits = around;
        int32_t ordered = bits & 0x80000000u ? -int32_t(bits & 0x7fffffffu) : int32_t(bits);
        ordered += step;
        return Float(ordered < 0 ? 0x80000000u | uint32_t(-ordered) : uint32_t(ordered));
    }
    }
}
uint32_t FieldBits(const uint8_t *m, uint32_t off) { return Word(m, off); }

struct Op {
    unsigned kind;
    uint32_t arg, arg2;
};

std::vector<Op> MakeOps(uint32_t seed)
{
    std::mt19937 rng(seed * 31 + 11);
    std::vector<Op> ops;
    const unsigned n = 8 + rng() % 40;
    if (rng() % 5) ops.push_back({0, rng(), 0}); // Init
    for (unsigned i = 0; i < n; ++i) ops.push_back({rng() % 17, rng(), rng()});
    return ops;
}

constexpr uint32_t kFill = 0x5e5e5e5e;

void RunOp(Blob &b, void *object, const Op &op)
{
    const auto *m = static_cast<const uint8_t *>(object);
    void *const service = object;
    void *const update = Face(object, 4);
    void *const renderer = Face(object, 8);
    void *const ea = Face(object, 0xc);
    void *const listener = Face(object, 0x10);
    void *const listenerEx = Face(object, 0x14);
    void *const effects = Face(object, 0x18);
    const uint32_t a = op.arg, c = op.arg2;
    uint32_t out[12];
    for (uint32_t &w : out) w = kFill;
    auto outs = [&](unsigned i) { return (c >> i) % 5 == 0 ? nullptr : reinterpret_cast<float *>(&out[i]); };
    switch (op.kind) {
    case 0: { // Init: the service face or the thunks of the EA renderer / listener faces
        void *const faces[] = {service, ea, listener};
        const unsigned which = a % 3;
        b.push_back(Slot<uint32_t>(faces[which], which ? 3 : 8) & 0xff);
        break;
    }
    case 1: { // Shutdown, the same three ways
        void *const faces[] = {service, ea, listener};
        const unsigned which = a % 3;
        b.push_back(Slot<uint32_t>(faces[which], which ? 4 : 9) & 0xff);
        break;
    }
    case 2: // Update (the rebuild derefs the service unchecked: skipped when dirty without one, 0xb13de7)
        if (m[0xb5] && !Word(m, 0xb8)) return;
        Slot<void>(update, 3, c);
        break;
    case 3: { // cIGZSndRenderer
        static const int32_t kLevels[] = {0, 1, -1, 100, 127, 0x7fffffff, int32_t(0x80000000u), 0x1000000, 0x1000001};
        switch (a % 5) {
        case 0: b.push_back(Slot<uint32_t>(renderer, 3, (c >> 4) % 2 ? kLevels[c % 9] : int32_t(c)) & 0xff); break;
        case 1: b.push_back(Slot<uint32_t>(renderer, 4)); break;
        case 2: b.push_back(Slot<uint32_t>(renderer, 5, c) & 0xff); break;
        case 3: b.push_back(Slot<uint32_t>(renderer, 6) & 0xff); break;
        default: b.push_back(Slot<uint32_t>(renderer, 7) & 0xff); break;
        }
        break;
    }
    case 4: // EA slots 5..7
        switch (a % 3) {
        case 0: b.push_back(Slot<uint32_t>(ea, 5) & 0xff); break;
        case 1: b.push_back(Slot<uint32_t>(ea, 6, c) & 0xff); break;
        default: b.push_back(Slot<uint32_t>(ea, 7) & 0xff); break;
        }
        break;
    case 5: // the matrix; the factors
        if (a % 3 == 0) {
            Slot<void>(ea, 8, static_cast<void *>(out));
        } else if (a % 3 == 1) {
            Slot<void>(ea, 9, PickFloat(c, Word(m, 0xa0)), PickFloat(c >> 7, Word(m, 0xa4)), PickFloat(c >> 14, Word(m, 0xa8)));
        } else {
            Slot<void>(ea, 10, static_cast<void *>(&out[0]), static_cast<void *>(&out[1]), static_cast<void *>(&out[2]));
        }
        break;
    case 6: { // EA slots 11..20
        const uint32_t v = c % 4 ? c % 7 : c;
        switch (a % 10) {
        case 0: b.push_back(Slot<uint32_t>(ea, 11, v) & 0xff); break;
        case 1: b.push_back(Slot<uint32_t>(ea, 12, v) & 0xff); break;
        case 2: b.push_back(Slot<uint32_t>(ea, 13, static_cast<void *>(out)) & 0xff); break;
        case 3: b.push_back(Slot<uint32_t>(ea, 14, v, static_cast<void *>(out)) & 0xff); break;
        case 4: b.push_back(Slot<uint32_t>(ea, 15, v) & 0xff); break;
        case 5: b.push_back(Slot<uint32_t>(ea, 16, static_cast<void *>(out)) & 0xff); break;
        case 6: b.push_back(Slot<uint32_t>(ea, 17, v, a) & 0xff); break;
        case 7: b.push_back(Slot<uint32_t>(ea, 18, v, a >> 8) & 0xff); break;
        case 8: b.push_back(Slot<uint32_t>(ea, 19, v, static_cast<void *>(out)) & 0xff); break;
        default: b.push_back(Slot<uint32_t>(ea, 20, v) & 0xff); break;
        }
        break;
    }
    case 7: { // the second position (slot 21 / 23)
        if (a % 3 == 0) {
            Slot<void>(ea, 23, static_cast<void *>(out));
            break;
        }
        float v[3]; // one component moved (or all), the others as they are
        for (unsigned i = 0; i < 3; ++i) {
            const uint32_t now = Word(m, 0x64 + 4 * i);
            v[i] = i == (a >> 2) % 3 || (a >> 4) % 4 == 0 ? PickFloat(c >> (i * 9), now) : Float(now);
        }
        Slot<void>(ea, 21, static_cast<void *>(v));
        break;
    }
    case 8: { // the listener vectors (slot 22 / 24)
        if (a % 3 == 0) {
            Slot<void>(ea, 24, static_cast<void *>(&out[0]), static_cast<void *>(&out[3]), static_cast<void *>(&out[6]));
            break;
        }
        float v[9];
        static const uint32_t kAt[3] = {0x58, 0x34, 0x40};
        for (unsigned i = 0; i < 9; ++i) {
            const bool move = (a >> (2 + i)) % 3 == 0;
            v[i] = move ? PickFloat(c >> (i * 3), Word(m, kAt[i / 3] + 4 * (i % 3))) : Float(Word(m, kAt[i / 3] + 4 * (i % 3)));
        }
        Slot<void>(ea, 22, static_cast<void *>(&v[0]), static_cast<void *>(&v[3]), static_cast<void *>(&v[6]));
        break;
    }
    case 9: { // cIGZSnd3DListener
        switch (a % 6) {
        case 0: Slot<void>(listener, 6, outs(0), outs(1), outs(2)); break;
        case 1:
            b.push_back(Slot<uint32_t>(listener, 7, PickFloat(c, Word(m, 0x58)), PickFloat(c >> 8, Word(m, 0x5c)),
                                       PickFloat(c >> 16, Word(m, 0x60))) & 0xff);
            break;
        case 2: Slot<void>(listener, 8, outs(0), outs(1), outs(2), outs(3), outs(4), outs(5)); break;
        case 3:
            if ((a >> 6) % 4 == 0) {
                // NaNs with different payloads (quiet and signalling, both signs): the matrix products' operand order
                // decides which survives (0xb13d42). Either all six, or only top y and front x (the operands of one
                // product, with the other products finite) while the rest are ordinary values.
                float v[6];
                const bool pair = (a >> 8) % 2 != 0;
                for (unsigned i = 0; i < 6; ++i)
                    v[i] = pair && i != 1 && i != 3
                               ? Float(0x3f000000u + (c >> (i * 4)) % 16 * 0x00200000u | (c >> (20 + i)) % 2 << 31)
                               : Float(((c >> i) & 1 ? 0xffc00000u : 0x7f800000u) | (i + 1) << ((c >> 8) % 20) | 1u);
                b.push_back(Slot<uint32_t>(listener, 9, v[0], v[1], v[2], v[3], v[4], v[5]) & 0xff);
                break;
            }
            b.push_back(Slot<uint32_t>(listener, 9, PickFloat(c, Word(m, 0x34)), PickFloat(c >> 5, Word(m, 0x38)),
                                       PickFloat(c >> 10, Word(m, 0x3c)), PickFloat(c >> 15, Word(m, 0x40)),
                                       PickFloat(c >> 20, Word(m, 0x44)), PickFloat(c >> 25, Word(m, 0x48))) & 0xff);
            break;
        case 4: Slot<void>(listener, 5); break;
        default: b.push_back(svcfx::Norm(Slot<uint32_t>(listener, 10))); break;
        }
        break;
    }
    case 10: { // cIGZSnd3DListenerEx
        switch (a % 10) {
        case 0: Slot<void>(listenerEx, 3, outs(0), outs(1), outs(2)); break;
        case 1:
            b.push_back(Slot<uint32_t>(listenerEx, 4, PickFloat(c, 0), PickFloat(c >> 8, 0x3f800000u), PickFloat(c >> 16, 0)) &
                        0xff);
            break;
        case 2:
            Slot<void>(listenerEx, 5, outs(0), outs(1), outs(2), outs(3), outs(4), outs(5), outs(6), outs(7), outs(8), outs(9),
                       outs(10), outs(11));
            break;
        case 3: {
            float v[12];
            for (unsigned i = 0; i < 12; ++i) v[i] = PickFloat(c >> (i * 2) ^ i * 77, 0x3f800000u);
            b.push_back(Slot<uint32_t>(listenerEx, 6, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11]) &
                        0xff);
            break;
        }
        case 4: Slot<void>(listenerEx, 7, outs(0)); break;
        case 5: b.push_back(Slot<uint32_t>(listenerEx, 8, PickFloat(c, 0)) & 0xff); break;
        case 6: Slot<void>(listenerEx, 9, outs(0)); break;
        case 7: b.push_back(Slot<uint32_t>(listenerEx, 10, PickFloat(c, Word(m, 0xa0))) & 0xff); break;
        case 8: Slot<void>(listenerEx, 11, outs(0)); break;
        default: b.push_back(Slot<uint32_t>(listenerEx, 12, PickFloat(c, 0)) & 0xff); break;
        }
        break;
    }
    case 11: case 12: { // cIGZSndEaRendererEffects: indexes around the table's size
        const uint32_t size = (Word(m, 0xc0) - Word(m, 0xbc)) / 4;
        const uint32_t index = (c >> 8) % 4 == 0 ? c : size + (c % 4) - 2;
        switch (a % 5) {
        case 0: b.push_back(Slot<uint32_t>(effects, 3)); break;
        case 1: b.push_back(Slot<uint32_t>(effects, 4, (c >> 3) % 8 ? (c % 9) : c) & 0xff); break;
        case 2: b.push_back(Slot<uint32_t>(effects, 5, index, (a >> 4) % 2 ? a : 0u) & 0xff); break;
        case 3: b.push_back(Slot<uint32_t>(effects, 6, index) & 0xff); break;
        default: b.push_back(Slot<uint32_t>(effects, 7, index, PickFloat(a >> 3, 0x3f800000u)) & 0xff); break;
        }
        break;
    }
    case 13: { // QueryInterface on a face, AddRef / Release through others
        static const uint32_t kAsk[] = {1, 0x287fb697, 0xc9d25f29, 0x899b182d, 0x406077d2, 0x5b30e7b3, 0x5b34f0af, 0xab79e31c,
                                        0x899b182c, 0x12345678, 0, 0xe99b3207};
        void *faces[] = {service, update, renderer, ea, listener, listenerEx, effects};
        void *q = reinterpret_cast<void *>(uintptr_t(kFill));
        const uint32_t ask = (a >> 4) % 13 == 12 ? c : kAsk[(a >> 4) % 13 % 12];
        const uint32_t ok = Slot<uint32_t>(faces[a % 7], 0, ask, &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::Norm(P(q)));
        b.push_back(Slot<uint32_t>(faces[(a >> 8) % 7], 1));
        b.push_back(Slot<uint32_t>(faces[(a >> 12) % 7], 2));
        if (ok) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 14: { // the count at its edges (above 1 unsigned after the AddRef: the decrement path, 2 to 1 last), then back
        static const uint32_t kCounts[] = {2, 3, 0x7fffffff, 0x80000000u, 0xfffffffe, 0xfffffffd};
        auto &count = *reinterpret_cast<uint32_t *>(Face(object, 0x1c));
        const uint32_t saved = count;
        count = kCounts[a % 6];
        b.push_back(Slot<uint32_t>(Face(object, 4 * ((c % 7))), 1));
        b.push_back(Slot<uint32_t>(Face(object, 4 * ((c >> 4) % 7)), 2));
        b.push_back(Slot<uint32_t>(Face(object, 4 * ((c >> 8) % 7)), 2));
        b.push_back(count);
        count = saved;
        break;
    }
    case 15: // the cIGZSystemService accessors
        b.push_back(Slot<uint32_t>(service, 3));
        Slot<void>(service, 4, c);
        b.push_back(Slot<uint32_t>(service, 5));
        b.push_back(Slot<uint32_t>(service, 6) & 0xff);
        Slot<void>(service, 7, a % 2);
        b.push_back(Slot<uint32_t>(service, 10, c) & 0xff);
        b.push_back(Slot<uint32_t>(service, 11, c) & 0xff);
        b.push_back(Slot<uint32_t>(service, 12));
        break;
    default: { // the world changes
        static const uint32_t kRates[] = {0, 0, 3, 6, 12};
        gWorld.service = a % 6 != 0;
        gWorld.failRate = kRates[(a >> 3) % 5];
        for (unsigned i = 0; i < kSounds; ++i) F(kSound0 + i).value[0] = (c >> i) & 1;
        break;
    }
    }
    for (uint32_t w : out) b.push_back(w);
}

} // namespace sndea_test

using namespace sndea_test;

DIFF_TEST(SndEaRenderer_Run, 0xb12ce3)
{
    // Also reached (see the file comment): 0xb12be6 (factory), 0xb12de1, 0xb12e37, 0xb133f0, 0xb12e59, 0xb134b5, 0xb13262,
    // 0xb132a5, 0xb13c35, 0xb13d27, 0xb13d42, 0xb12fa6, 0xb13a6e, 0xb13a4b, 0xb13102, 0xb130f4, 0xb133e4, 0xb13d1d, 0xb133c0,
    // 0xb13cef, 0xb13d08, 0xb13268, 0xb136af, 0xb13082, 0xb12ea3, 0xb13b5d, 0xb131a5, 0xb12eca, 0xb13b7c, 0xb131c4, 0xb12ef0,
    // 0xb13afa, 0xb13133, 0xb13ae8, 0xb1359a, 0xb13746, 0xb13071, 0xb130cf, 0xb131e6, 0xb13ba5, 0xb13156, 0xb13b11, 0xb12e97,
    // 0xb1327b, 0xb13c08, 0xb12fda, 0xb13506, 0xb130bc, 0xb13733, 0xb13210, 0xb13bde, 0xb130a9, 0xb13720, 0xb13119, 0xb13a88,
    // 0xb12f0b, 0xb12f68, 0xb136e3, the adjustor thunks 0xb13485, 0xb1348d, 0xb134ad, 0xb13495, 0xb1349d, 0xb134a5, 0xb12e67,
    // 0xb12e6f, 0xb12e8f, 0xb12e77, 0xb12e7f, 0xb12e87, 0xb134d6, 0xb134de, 0xb134fe, 0xb134e6, 0xb134ee, 0xb134f6, 0xb133b8,
    // 0xb13ce7, 0xb133b0, 0xb13cdf.
    svcfx::Globals globals;
    globals.SetWord(0x10c9378, 0);
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::CodePatch p1(0xe7362, shape::CodeOf(&EaLock)), p2(0xe7370, shape::CodeOf(&EaUnlock)),
        p3(0xe737d, shape::CodeOf(&EaSetLevel)), p4(0xe7442, shape::CodeOf(&Ea7442)), p5(0xe746e, shape::CodeOf(&Ea746e)),
        p6(0xe74b2, shape::CodeOf(&Ea74b2)), p7(0xe74f7, shape::CodeOf(&Ea74f7)), p8(0xe7506, shape::CodeOf(&Ea7506)),
        p9(0xe754c, shape::CodeOf(&Ea754c)), p10(0xe75ab, shape::CodeOf(&Ea75ab)), p11(0xe75e1, shape::CodeOf(&Ea75e1)),
        p12(0xe7617, shape::CodeOf(&Ea7617)), p13(0xe7668, shape::CodeOf(&Ea7668)), p14(0xe7748, shape::CodeOf(&Ea7748)),
        p15(0xe772c, shape::CodeOf(&Ea772c)), p16(0xe7699, shape::CodeOf(&Ea7699)), p17(0xe7739, shape::CodeOf(&Ea7739)),
        p18(0xe7768, shape::CodeOf(&Ea7768)), p19(0xe7779, shape::CodeOf(&Ea7779)), p20(0xe7788, shape::CodeOf(&Ea7788)),
        p21(0xe779d, shape::CodeOf(&Ea779d)), p22(0xe7895, shape::CodeOf(&Ea7895)), p23(0xe78bc, shape::CodeOf(&Ea78bc)),
        p24(0xe7811, shape::CodeOf(&Ea7811));

    for (uint32_t seed = 1; seed <= 2500; ++seed) {
        const std::vector<Op> ops = MakeOps(seed);
        gFactory = seed % 7 == 0;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            void *object = Build(ctx, side == 1);
            gObject = static_cast<uint8_t *>(object);
            svcfx::AddRange(object, R::kSize, kTag);
            svcfx::AddRange(GameAddress<void>(0xb12fa6), 1, kCallbackTag);
            svcfx::AddRange(shape::CodeOf(&R::NotifySound), 1, kCallbackTag);
            svcfx::AddRange(reinterpret_cast<void *>(&R::NotifySound), 1, kCallbackTag);
            Slot<uint32_t>(object, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                if (i < ops.size()) {
                    if (svcfx::gTrace) fprintf(stderr, "seed %u side %d op %u kind %u\n", seed, side, unsigned(i), ops[i].kind);
                    RunOp(b, object, ops[i]);
                    Dump(b, object, side == 1);
                } else if (seed % 9 == 0) {
                    // The deleting destructor directly (slot 13): flags 1 frees, flags 0 only destroys (freed here).
                    const uint32_t flags = seed % 2;
                    b.push_back(svcfx::Norm(Slot<uint32_t>(object, 13, flags)));
                    if (!flags) ::operator delete(object);
                } else {
                    // The last release: from a count of 1, or 0 (unsigned: not above 1, so destroyed too).
                    *reinterpret_cast<uint32_t *>(Face(object, 0x1c)) = seed % 4 == 0 ? 0u : 1u;
                    b.push_back(Slot<uint32_t>(Face(object, 4 * (seed % 7)), 2));
                }
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                svcfx::CaptureFakes(b, kValueKinds);
                results[side].push_back(std::move(b));
            }
            gObject = nullptr;
            svcfx::gRanges.clear();
        }
        for (size_t i = 0; i < results[0].size(); ++i) {
            if (!svcfx::Same(ctx, results[0][i], results[1][i], "SndEaRenderer_Run", seed)) {
                if (svcfx::gTrace)
                    for (size_t w = 0; w < results[0][i].size() || w < results[1][i].size(); ++w)
                        fprintf(stderr, "%3u %08x %08x\n", unsigned(w), w < results[0][i].size() ? results[0][i][w] : 0,
                                w < results[1][i].size() ? results[1][i][w] : 0);
                ctx.Fail("  at op %u of %u (kind %u)", unsigned(i), unsigned(ops.size()), i < ops.size() ? ops[i].kind : 99u);
                return;
            }
        }
    }
}
