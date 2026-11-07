// Differential tests for the owned XC entries of the cIGZAny statics (src/app/StaticInitAny.cpp, lane GZ;
// docs/subsystems/startup.md "Done in lane GZ"): 36 AnyBoolean / AnyUint32 / AnyFloat32 objects.
//
// StaticInitAny_Form: what each image entry does, on the object and 8 bytes on each side filled with two junk
// patterns: nothing written, exactly one atexit registration, of the thunk our table names; the image's .data holds
// the class's vtable and our table's value at the object; the thunk writes only the first word (cIGZAny's vtable).
//
// StaticInitAny_Entries: each entry against the image's in its slot, from the same start state: the object as the
// image's file holds it (vtable and value: the image's entry writes neither, it relies on .data; with junk in the
// value our entry would differ only because it writes the value it owns, which the Form test shows the image never
// writes), the three bytes after an AnyBoolean's byte and 8 bytes on each side junk. Compared: the window after the
// entry and after its registered thunk (the image's / ours), byte for byte except the vtable word, which is compared
// as a code (the image's: the game's class vtable, then cIGZAny's 0xd9c654; ours: our class's vtable, then our
// cIGZAny's, by their RTTI names), the registrations (one each) and the frees (none).
//
// StaticInitAny_OnImageTable: the runner over a copy of the XC table with only these slots: 36 ours, 36
// registrations, and after each side's exit list run LIFO the same windows.
#include "difftest.h"
#include "free_recorder.h"
#include "app/StaticInit.h"
#include "app/StaticInitAny.h"
#include "app/WinMainStart.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

using staticinit::AnyKind;
using staticinit::AnyStatic;

constexpr uint32_t kXcFirst = 0xd9a724, kXcLast = 0xd9b3a8;
constexpr uint32_t kAtExit = 0x824cbe, kAnyVTable = 0xd9c654;
constexpr int kMargin = 8, kWindow = 8 + 2 * kMargin;

using Thunk = void(__cdecl *)();

std::vector<Thunk> gRegistered;
int __cdecl RecordAtExit(Thunk f)
{
    gRegistered.push_back(f);
    return 0;
}

// A 5-byte jmp over an image function's entry for the duration of a test.
struct Redirect {
    uint8_t *site;
    uint8_t saved[5];
    Redirect(const difftest::Context &ctx, uint32_t rva, const void *target) : site(ctx.image.At<uint8_t>(rva))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    ~Redirect() { std::memcpy(site, saved, 5); }
};

// Both sides' atexit registrations go to gRegistered: the image's atexit redirected, ours bound.
struct AtExitCapture {
    Redirect image;
    staticinit::RegisterAtExit saved;
    explicit AtExitCapture(const difftest::Context &ctx)
        : image(ctx, kAtExit, reinterpret_cast<const void *>(&RecordAtExit)), saved(staticinit::BoundAtExit())
    {
        staticinit::BindAtExit(&RecordAtExit);
        gRegistered.clear();
    }
    ~AtExitCapture() { staticinit::BindAtExit(saved); }
};

bool SlotHolds(difftest::Context &ctx, uint32_t slot, uint32_t rva)
{
    uint32_t held = *ctx.image.At<uint32_t>(kXcFirst + 4 * slot) - static_cast<uint32_t>(ctx.image.Base());
    if (held == rva) return true;
    ctx.Fail("slot %u holds %08x, our table says %08x", slot, held, rva);
    return false;
}

std::vector<uint8_t> Window(const difftest::Context &ctx, uint32_t storage)
{
    const uint8_t *p = ctx.image.At<uint8_t>(storage - kMargin);
    return std::vector<uint8_t>(p, p + kWindow);
}

void Put(const difftest::Context &ctx, uint32_t storage, const std::vector<uint8_t> &w)
{
    std::memcpy(ctx.image.At<uint8_t>(storage - kMargin), w.data(), w.size());
}

uint32_t GameVTable(AnyKind kind)
{
    switch (kind) {
    case AnyKind::Boolean: return 0xd9c670;
    case AnyKind::Uint32: return 0xd9f534;
    default: return 0xda3d70;
    }
}

const char *OurName(AnyKind kind)
{
    switch (kind) {
    case AnyKind::Boolean: return ".?AUAnyBoolean@gz@@";
    case AnyKind::Uint32: return ".?AUAnyUint32@gz@@";
    default: return ".?AUAnyFloat32@gz@@";
    }
}

// The vtable word of a window, replaced by a code: 1 the game's class vtable, 2 ours, 3 the game's cIGZAny, 4 ours,
// 0 anything else (then the word itself is left for the comparison to show).
int CodeVTable(const difftest::Context &ctx, AnyKind kind, std::vector<uint8_t> &w)
{
    uint32_t word;
    std::memcpy(&word, w.data() + kMargin, 4);
    const uintptr_t at = word;
    int code = 0;
    if (at == reinterpret_cast<uintptr_t>(ctx.image.At<void>(GameVTable(kind)))) code = 1;
    else if (at == reinterpret_cast<uintptr_t>(ctx.image.At<void>(kAnyVTable))) code = 3;
    else if (at && (at < ctx.image.Base() || at >= ctx.image.Base() + ctx.image.Size())) {
        auto *col = reinterpret_cast<const uint32_t *const *>(at)[-1];
        std::string name(reinterpret_cast<const char *>(static_cast<uintptr_t>(col[3])) + 8);
        if (name == OurName(kind)) code = 2;
        else if (name == ".?AVcIGZAny@@") code = 4;
    }
    if (code) std::memset(w.data() + kMargin, 0, 4);
    return code;
}

} // namespace

// The 36 image initialisers, in slot order (db/initializers.jsonl: class-inline, class cIGZAny), checked against
// our table independently of it.
constexpr uint32_t kImageEntries[36] = {
    0x1392c0, 0x139360, 0x139370, 0x139310, 0x139320, 0x139350, 0x139380, 0x139330, 0x139390, 0x139300, 0x1392f0, 0x139340,
    0x139560, 0x13caed, 0x13cae1, 0x13cad5, 0x13ccd5, 0x13cc99, 0x13ccbd, 0x13ccc9, 0x13cca5, 0x13ccb1, 0x140100, 0x1400d0,
    0x1400f0, 0x1400a0, 0x1400c0, 0x1400e0, 0x140110, 0x140090, 0x1400b0, 0x140120, 0x140080, 0x1409d0, 0x140aa0, 0x140ab0,
};

DIFF_TEST(StaticInitAny_Form, 0x1392c0)
{
    AtExitCapture capture(ctx);
    size_t n = 0;
    const AnyStatic *entries = staticinit::AnyStatics(&n);
    if (n != 36) ctx.Fail("%u cIGZAny entries, expected 36", static_cast<unsigned>(n));
    for (size_t i = 0; i < n; ++i) {
        const AnyStatic &e = entries[i];
        if (i < 36 && e.imageRva != kImageEntries[i]) ctx.Fail("row %u: entry %06x, expected %06x", static_cast<unsigned>(i), e.imageRva, kImageEntries[i]);
        if (!SlotHolds(ctx, e.slot, e.imageRva)) continue;
        const std::vector<uint8_t> saved = Window(ctx, e.storage);
        // The file's object: the class's vtable and our value.
        uint32_t file[2];
        std::memcpy(file, saved.data() + kMargin, 8);
        ++ctx.cases;
        const uint32_t value = e.kind == AnyKind::Boolean ? file[1] & 0xff : file[1];
        if (file[0] != static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx.image.At<void>(GameVTable(e.kind)))) ||
            value != e.bits)
            ctx.Fail("slot %u: .data holds %08x %08x at %06x; our table: kind %d, %08x", e.slot, file[0], file[1],
                     e.storage, static_cast<int>(e.kind), e.bits);
        auto image = ctx.Original<void(__cdecl *)()>(e.imageRva);
        for (uint8_t pattern : {uint8_t(0x5a), uint8_t(0xc3)}) {
            std::vector<uint8_t> junk(kWindow, pattern);
            for (int k = 0; k < kWindow; ++k) junk[k] = static_cast<uint8_t>(pattern + k * 7);
            Put(ctx, e.storage, junk);
            gRegistered.clear();
            image();
            ++ctx.cases;
            if (Window(ctx, e.storage) != junk) ctx.Fail("slot %u: the image's entry wrote the object", e.slot);
            if (gRegistered.size() != 1 || reinterpret_cast<void *>(gRegistered[0]) != ctx.image.At<void>(e.thunk)) {
                ctx.Fail("slot %u: the image's entry registered %u thunks, not %06x", e.slot,
                         static_cast<unsigned>(gRegistered.size()), e.thunk);
                continue;
            }
            gRegistered[0]();
            std::vector<uint8_t> want = junk;
            const uint32_t anyVTable = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx.image.At<void>(kAnyVTable)));
            std::memcpy(want.data() + kMargin, &anyVTable, 4);
            ++ctx.cases;
            if (Window(ctx, e.storage) != want) ctx.Fail("slot %u: the image's thunk wrote more than cIGZAny's vtable", e.slot);
        }
        Put(ctx, e.storage, saved);
    }
}

namespace {

struct AnyRun {
    std::vector<uint8_t> built, destroyed;
    int builtCode = -1, destroyedCode = -1;
    size_t registrations = 0, frees = 0;
};

template <typename Init> AnyRun RunAny(const difftest::Context &ctx, const AnyStatic &e, Init init)
{
    AnyRun r;
    gRegistered.clear();
    init();
    r.built = Window(ctx, e.storage);
    r.builtCode = CodeVTable(ctx, e.kind, r.built);
    r.registrations = gRegistered.size();
    if (r.registrations == 1) {
        difftest::FreeRecorder rec;
        gRegistered[0]();
        rec.Stop();
        r.frees = rec.Count();
        r.destroyed = Window(ctx, e.storage);
        r.destroyedCode = CodeVTable(ctx, e.kind, r.destroyed);
    }
    return r;
}

} // namespace

DIFF_TEST(StaticInitAny_Entries, 0x13caed)
{
    AtExitCapture capture(ctx);
    size_t n = 0;
    const AnyStatic *entries = staticinit::AnyStatics(&n);
    std::mt19937 rng(5);
    for (size_t i = 0; i < n; ++i) {
        const AnyStatic &e = entries[i];
        if (!SlotHolds(ctx, e.slot, e.imageRva)) continue;
        const std::vector<uint8_t> saved = Window(ctx, e.storage);
        auto image = ctx.Original<void(__cdecl *)()>(e.imageRva);
        for (int round = 0; round < 4; ++round) {
            std::vector<uint8_t> start(kWindow);
            for (auto &x : start) x = static_cast<uint8_t>(rng());
            std::memcpy(start.data() + kMargin, saved.data() + kMargin, 8); // the file's object
            if (e.kind == AnyKind::Boolean && round) std::memset(start.data() + kMargin + 5, static_cast<int>(rng()), 3);
            Put(ctx, e.storage, start);
            AnyRun a = RunAny(ctx, e, [&] { image(); });
            Put(ctx, e.storage, start);
            AnyRun b = RunAny(ctx, e, [&] { staticinit::InitAny(&e); });
            ++ctx.cases;
            if (a.registrations != 1 || b.registrations != 1) {
                ctx.Fail("slot %u: %u registrations by the image's entry, %u by ours", e.slot,
                         static_cast<unsigned>(a.registrations), static_cast<unsigned>(b.registrations));
                continue;
            }
            if (a.builtCode != 1 || b.builtCode != 2 || a.built != b.built)
                ctx.Fail("slot %u round %d: built object differs (vtable codes %d/%d)", e.slot, round, a.builtCode, b.builtCode);
            if (a.destroyedCode != 3 || b.destroyedCode != 4 || a.destroyed != b.destroyed)
                ctx.Fail("slot %u round %d: destroyed object differs (vtable codes %d/%d)", e.slot, round, a.destroyedCode,
                         b.destroyedCode);
            if (a.frees || b.frees)
                ctx.Fail("slot %u: the destructors freed %u / %u blocks", e.slot, static_cast<unsigned>(a.frees),
                         static_cast<unsigned>(b.frees));
        }
        Put(ctx, e.storage, saved);
    }
}

DIFF_TEST(StaticInitAny_OnImageTable, 0x140100)
{
    AtExitCapture capture(ctx);
    size_t ownedCount = 0, n = 0;
    const winmainstart::OwnedInitialiser *owned = staticinit::OwnedAnys(&ownedCount);
    const AnyStatic *entries = staticinit::AnyStatics(&n);
    const uint32_t slots = (kXcLast - kXcFirst) / 4;
    std::vector<winmainstart::Initialiser> table(slots, nullptr);
    for (size_t i = 0; i < ownedCount; ++i) {
        if (owned[i].slot < slots) table[owned[i].slot] = ctx.image.At<winmainstart::Initialiser>(kXcFirst)[owned[i].slot];
    }
    std::vector<std::vector<uint8_t>> saved;
    for (size_t i = 0; i < n; ++i) saved.push_back(Window(ctx, entries[i].storage));
    auto restore = [&] {
        for (size_t i = 0; i < n; ++i) Put(ctx, entries[i].storage, saved[i]);
    };
    // Every object's 8 bytes (the margins zeroed: the rows are 8 or 12 bytes apart, so a margin holds a neighbour's
    // vtable word; the Entries test covers the margins), its vtable word as a code (the code appended).
    auto windows = [&] {
        std::vector<uint8_t> all;
        for (size_t i = 0; i < n; ++i) {
            std::vector<uint8_t> w = Window(ctx, entries[i].storage);
            std::fill(w.begin(), w.begin() + kMargin, uint8_t(0));
            std::fill(w.end() - kMargin, w.end(), uint8_t(0));
            w.push_back(static_cast<uint8_t>(CodeVTable(ctx, entries[i].kind, w)));
            all.insert(all.end(), w.begin(), w.end());
        }
        return all;
    };
    auto lifo = [&] {
        for (auto it = gRegistered.rbegin(); it != gRegistered.rend(); ++it) (*it)();
    };

    restore();
    gRegistered.clear();
    for (uint32_t s = 0; s < slots; ++s) {
        if (table[s] != nullptr) table[s]();
    }
    const size_t imageRegistrations = gRegistered.size();
    std::vector<uint8_t> builtA = windows();
    lifo();
    std::vector<uint8_t> destroyedA = windows();

    restore();
    gRegistered.clear();
    winmainstart::XcRunCounts c = winmainstart::RunInitialisersOwned(table.data(), table.data() + slots, ctx.image.Base(),
                                                                      owned, ownedCount);
    const size_t ourRegistrations = gRegistered.size();
    std::vector<uint8_t> builtB = windows();
    lifo();
    std::vector<uint8_t> destroyedB = windows();
    restore();

    ++ctx.cases;
    if (c.owned != 36 || c.image || c.mismatched || c.unused)
        ctx.Fail("counts: %d ours, %d image, %d mismatched, %d unused", c.owned, c.image, c.mismatched, c.unused);
    if (imageRegistrations != 36 || ourRegistrations != 36)
        ctx.Fail("registrations: %u by the image's entries, %u by ours (36)", static_cast<unsigned>(imageRegistrations),
                 static_cast<unsigned>(ourRegistrations));
    // The codes differ by design (1 / 2 built, 3 / 4 destroyed): compare them shifted.
    for (size_t i = kWindow; i < builtA.size(); i += kWindow + 1) {
        if (builtA[i] != 1 || builtB[i] != 2 || destroyedA[i] != 3 || destroyedB[i] != 4)
            ctx.Fail("object %u: vtable codes %d/%d built, %d/%d destroyed", static_cast<unsigned>(i / (kWindow + 1)),
                     builtA[i], builtB[i], destroyedA[i], destroyedB[i]);
        builtB[i] = builtA[i];
        destroyedB[i] = destroyedA[i];
    }
    if (builtA != builtB) ctx.Fail("the objects differ after the run");
    if (destroyedA != destroyedB) ctx.Fail("the objects differ after the exit list");
}
