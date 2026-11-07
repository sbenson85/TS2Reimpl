// Differential tests for cRZStringResource (src/rz/cRZStringResource.h, lane LS): the string face's QueryInterface
// 0x9b63d0, AddRef 0x1eb500, Release 0x1ee2e0 and deleting destructor 0x1eb120, and the resource face's thunks 0x1ee218,
// 0x1eb508, 0x1ee2e8, 0x1eb109.
//
// Each case builds a game-shaped object (as the game's creators do: the string, the resource part, then the game's
// vtables 0xda6738 / 0xda67c4) with random text, counts and key, runs the original on one copy and ours on the other,
// and compares the result, the out-parameter (as "this", "the resource face", untouched), the object's words (the
// vtable words as codes: the game's string / resource vtables, cRZString's, cGZPersistResource's (the game's or ours:
// our destructor stores ours), the text (not its pointer) and the frees. The slots reached through the game's vtables
// (AddRef from QueryInterface, slot 12 from Release) run the original bodies on both sides, so what the hooked body
// itself does is what differs.
#include "difftest.h"
#include "free_recorder.h"

#include "game/game.h"
#include "gz/cGZPersistResource.h"
#include "rz/cRZStringResource.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

using Res = cRZStringResource;
using Blob = std::vector<std::string>;

constexpr uint32_t kQueryInterface = 0x9b63d0;
constexpr uint32_t kAddRef = 0x1eb500;
constexpr uint32_t kRelease = 0x1ee2e0;
constexpr uint32_t kDeletingDestructor = 0x1eb120;
constexpr uint32_t kThunkQueryInterface = 0x1ee218;
constexpr uint32_t kThunkAddRef = 0x1eb508;
constexpr uint32_t kThunkRelease = 0x1ee2e8;
constexpr uint32_t kThunkDeletingDestructor = 0x1eb109;
constexpr uint32_t kStringIid = 0x089b7dc8;

template <class Fn> Fn Game(uint32_t rva) { return reinterpret_cast<Fn>(GameAddress<void>(rva)); }
uint32_t GameWord(uint32_t rva) { return uint32_t(uintptr_t(GameAddress<void>(rva))); }
uint32_t Word(const void *p, uint32_t at) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(p) + at); }
void SetWord(void *p, uint32_t at, uint32_t v) { *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(p) + at) = v; }

std::string Hex(uint32_t v)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%x", v);
    return buf;
}

uint32_t OurPersistVTable()
{
    alignas(4) uint8_t raw[sizeof(cGZPersistResource)];
    ::new (raw) cGZPersistResource;
    return Word(raw, 0);
}

std::string VTableCode(uint32_t word)
{
    if (word == GameWord(Res::kStringVTable)) return "string-resource";
    if (word == GameWord(Res::kResourceVTable)) return "resource-face";
    if (word == GameWord(cRZString::kVTable)) return "rzstring";
    if (word == GameWord(cGZPersistResource::kOriginalVTable) || word == OurPersistVTable()) return "persist";
    return Hex(word);
}

struct Seed {
    std::string text;
    uint32_t stringCount, resourceCount, field28, key[4];
};

Seed MakeSeed(std::mt19937 &rng, uint32_t resourceCount)
{
    static const char *const kTexts[] = {"", "a", "fifteen chars..", "sixteen chars...",
                                         "a text long enough to live on the heap"};
    Seed s;
    s.text = kTexts[rng() % 5];
    s.stringCount = rng();
    s.resourceCount = resourceCount;
    s.field28 = rng();
    for (uint32_t &k : s.key) k = rng();
    return s;
}

// A game-shaped object, as the creators build it.
Res *Build(const Seed &s)
{
    void *memory = ::operator new(Res::kSize);
    std::memset(memory, 0xcd, Res::kSize);
    auto *object = static_cast<Res *>(memory);
    object->mString.Construct(s.text.c_str());
    cGZPersistResourceAdapters::Construct(&object->mResource, nullptr);
    SetWord(object, 0, GameWord(Res::kStringVTable));
    SetWord(object, 0x20, GameWord(Res::kResourceVTable));
    SetWord(object, 0x1c, s.stringCount);
    SetWord(object, 0x24, s.resourceCount);
    SetWord(object, 0x28, s.field28);
    for (int i = 0; i < 4; ++i) SetWord(object, 0x2c + 4 * i, s.key[i]);
    return object;
}

// The object's state: vtable codes, the text, size and capacity, and the other words.
void State(Blob &b, const Res *object)
{
    b.push_back("vt " + VTableCode(Word(object, 0)) + " / " + VTableCode(Word(object, 0x20)));
    const std::string &text = object->mString.mString;
    b.push_back("text [" + text + "] " + Hex(uint32_t(text.size())) + " cap " + Hex(uint32_t(text.capacity())));
    for (uint32_t at = 0x1c; at < Res::kSize; at += 4)
        if (at != 0x20) b.push_back(Hex(at) + "=" + Hex(Word(object, at)));
}

std::string OutCode(const Res *object, void *out, void *sentinel)
{
    if (out == object) return "this";
    if (out == &object->mResource) return "resource face";
    if (out == sentinel) return "untouched";
    return "other";
}

// Ends a case: frees the object unless the call did (the text through the string's own destructor).
void Finish(Blob &b, Res *object, const difftest::FreeRecorder &rec)
{
    const bool freed = rec.Freed(object);
    b.push_back(freed ? "freed" : "kept");
    b.push_back("frees " + std::to_string(rec.Count()));
    if (!freed) {
        State(b, object);
        object->mString.Destruct();
        ::operator delete(object);
    }
}

void Check(difftest::Context &ctx, const Blob (&runs)[2], const char *what, uint32_t a, uint32_t b)
{
    ++ctx.cases;
    if (runs[0] == runs[1]) return;
    std::string orig, ours;
    for (const std::string &s : runs[0]) orig += s + "; ";
    for (const std::string &s : runs[1]) ours += s + "; ";
    ctx.Fail("%s %x %x\n  orig: %s\n  ours: %s", what, a, b, orig.c_str(), ours.c_str());
}

} // namespace

// QueryInterface on both faces: the resource id (this + 0x20, AddRef through the string face's slot 1), cRZString's
// ids (forwarded to cRZString::QueryInterface: this, AddRef), other ids (false, untouched).
DIFF_TEST(cRZStringResource_QueryInterface, kQueryInterface)
{
    const uint32_t ids[] = {cGZPersistResource::kIid, 1, kStringIid, 0, cGZPersistResource::kIid + 1, 0x4bb0d269};
    std::mt19937 rng(0x9b63d0);
    for (int face = 0; face < 2; ++face)
        for (uint32_t id : ids)
            for (uint32_t count : {0u, 1u, 5u, 0x7fffffffu}) {
                const Seed seed = MakeSeed(rng, count);
                Blob runs[2];
                for (int side = 0; side < 2; ++side) {
                    Res *object = Build(seed);
                    void *sentinel = reinterpret_cast<void *>(0x5a5a5a5a);
                    void *out = sentinel;
                    bool result;
                    {
                        difftest::FreeRecorder rec;
                        if (face == 0)
                            result = side == 0 ? Game<bool(__thiscall *)(void *, uint32_t, void **)>(kQueryInterface)(
                                                     object, id, &out)
                                               : object->QueryInterface(id, &out);
                        else
                            result = side == 0 ? Game<bool(__thiscall *)(void *, uint32_t, void **)>(kThunkQueryInterface)(
                                                     &object->mResource, id, &out)
                                               : cRZStringResourceThunks::QueryInterface(&object->mResource, nullptr, id, &out);
                        runs[side].push_back("result " + std::to_string(result) + " out " + OutCode(object, out, sentinel));
                        rec.Stop();
                        Finish(runs[side], object, rec);
                    }
                }
                Check(ctx, runs, face ? "thunk QI" : "QI", id, count);
            }
}

// AddRef and Release on both faces, at counts around the last reference (Release at 1 deletes the object through the
// resource face's slot 12: the text and the object freed).
DIFF_TEST(cRZStringResource_Counting, kAddRef)
{
    std::mt19937 rng(0x1eb500);
    for (int slot = 0; slot < 4; ++slot)
        for (uint32_t count : {0u, 1u, 2u, 3u, 0x7fffffffu, 0xffffffffu}) {
            const Seed seed = MakeSeed(rng, count);
            Blob runs[2];
            for (int side = 0; side < 2; ++side) {
                Res *object = Build(seed);
                difftest::FreeRecorder rec;
                uint32_t result = 0;
                void *face = &object->mResource;
                switch (slot) {
                case 0:
                    result = side == 0 ? Game<uint32_t(__thiscall *)(void *)>(kAddRef)(object) : object->AddRef();
                    break;
                case 1:
                    result = side == 0 ? Game<uint32_t(__thiscall *)(void *)>(kRelease)(object) : object->Release();
                    break;
                case 2:
                    result = side == 0 ? Game<uint32_t(__thiscall *)(void *)>(kThunkAddRef)(face)
                                       : cRZStringResourceThunks::AddRef(face, nullptr);
                    break;
                default:
                    result = side == 0 ? Game<uint32_t(__thiscall *)(void *)>(kThunkRelease)(face)
                                       : cRZStringResourceThunks::Release(face, nullptr);
                    break;
                }
                rec.Stop();
                runs[side].push_back("result " + Hex(result));
                Finish(runs[side], object, rec);
            }
            Check(ctx, runs, "count slot", slot, count);
        }
}

// The deleting destructor directly and through the resource face's thunk, flags 0 and 1 (and other bits): the return,
// the vtables left (cRZString's, cGZPersistResource's), the text emptied, the frees.
DIFF_TEST(cRZStringResource_Destroy, kDeletingDestructor)
{
    std::mt19937 rng(0x1eb120);
    for (int face = 0; face < 2; ++face)
        for (uint32_t flags : {0u, 1u, 2u, 3u, 0xfffffffeu})
            for (int i = 0; i < 5; ++i) {
                const Seed seed = MakeSeed(rng, rng());
                Blob runs[2];
                for (int side = 0; side < 2; ++side) {
                    Res *object = Build(seed);
                    void *face0 = &object->mResource;
                    difftest::FreeRecorder rec;
                    Res *result;
                    if (face == 0)
                        result = side == 0 ? Game<Res *(__thiscall *)(void *, uint32_t)>(kDeletingDestructor)(object, flags)
                                           : object->DeletingDestructor(flags);
                    else
                        result = side == 0 ? Game<Res *(__thiscall *)(void *, uint32_t)>(kThunkDeletingDestructor)(face0, flags)
                                           : cRZStringResourceThunks::DeletingDestructor(face0, nullptr, flags);
                    rec.Stop();
                    runs[side].push_back(result == object ? "this" : "other");
                    const bool freed = rec.Freed(object);
                    runs[side].push_back(freed ? "freed" : "kept");
                    runs[side].push_back("frees " + std::to_string(rec.Count()));
                    if (!freed) {
                        State(runs[side], object);
                        ::operator delete(object); // the text is already gone
                    }
                }
                Check(ctx, runs, face ? "thunk destroy" : "destroy", flags, i);
            }
}
