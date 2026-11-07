// Differential tests for nGZLog's output back ends (src/rz/nGZLogOutput.cpp, lane LY): the base's destructor
// (0xc9f3a8), deleting destructor (slot 24, 0xc9f3e6) and configuration text (slot 19, 0xc9f506); the default back
// end's constructor (0xc9f5df), destructor (0xc9f65e), deleting destructor (0xc9f683) and Output (slot 5, 0xc9f6c1);
// the log file back end's constructor (0xc9f60a), destructor (0xc9f669), deleting destructor (0xc9f6a2), Output
// (slot 5, 0xc9f716) and Reopen (slot 25, 0xc9f763); the caption back end's destructor (0xc9b9c7), deleting
// destructor (0xc9b9ef), Output (slot 5, 0xc9baa4) and Flush (slot 21, 0xc9ba0e); and the service's slots 21
// (0xc9ce52) and 22 (0xc9caba) that make default and file back ends.
//
// Side A runs the image's code, side B ours, on objects built alike (our verified constructors, or Pair memory for
// the constructors under test). The stream, filter, formatter, framework, window and service objects are fakes
// sharing one vtable whose slots log their calls; each side starts from the same fake state. The log file stream's
// two constructors (cRZOFStream 0x1b4e80 / 0x1b4dd2, ours too) are stood in on both sides by a fake that logs its
// arguments, so no file is opened; IsWindow and SetWindowTextA are faked through the image's import slots.
#include "gzlog_support.h" // first: it defines NOMINMAX before windows.h
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/vtable.h"
#include "rz/RZFileStreams.h"
#include "rz/nGZLog.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <optional>
#include <random>
#include <string>
#include <vector>

using namespace gzlog_test;
using namespace nGZLog;

namespace {

constexpr uint32_t kBaseDtor = 0xc9f3a8, kBaseDeleting = 0xc9f3e6, kWriteConfig = 0xc9f506;
constexpr uint32_t kDefaultCtor = 0xc9f5df, kDefaultDtor = 0xc9f65e, kDefaultDeleting = 0xc9f683, kDefaultOutput = 0xc9f6c1;
constexpr uint32_t kFileCtor = 0xc9f60a, kFileDtor = 0xc9f669, kFileDeleting = 0xc9f6a2, kFileOutput = 0xc9f716, kFileReopen = 0xc9f763;
constexpr uint32_t kCaptionDtor = 0xc9b9c7, kCaptionDeleting = 0xc9b9ef, kCaptionOutput = 0xc9baa4, kCaptionFlush = 0xc9ba0e;
constexpr uint32_t kAddStreamOutput = 0xc9ce52, kAddFileOutput = 0xc9caba;
constexpr uint32_t kOFStreamFromChars = 0x1b4e80, kOFStreamFromString = 0x1b4dd2;
constexpr uint32_t kFrameworkGlobal = 0x10c9378;
constexpr uint32_t kIatIsWindow = 0xd9a5c4, kIatSetWindowTextA = 0xd9a598;
constexpr uint32_t kFlagSet[8] = {0, 1, 2, 3, 0x100, 0x101, 0xfffffffe, 0xffffffff};

std::string gLog;

void Log(const char *fmt, ...)
{
    char buf[400];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    gLog += buf;
    gLog += ';';
}

// ---- the fakes ----

void *gVTable[26];

// Fits in the 0x18 bytes of a cRZOFStream (the stood-in stream constructors make fakes in place).
struct Fake {
    void *const *vtable;
    int32_t id;
    cRZString *text;   // a formatter's text for a record
    const char *name;  // a filter's or formatter's name
    int32_t refs;
};
static_assert(sizeof(Fake) <= 0x18, "a fake fits a cRZOFStream");

bool IsFake(const void *p) { return p && static_cast<const Fake *>(p)->vtable == gVTable; }
std::string Id(const void *p)
{
    if (!p) return "null";
    return IsFake(p) ? "#" + std::to_string(static_cast<const Fake *>(p)->id) : "other";
}

// The script both sides read.
struct Script {
    bool writeOk = true;      // stream slot 15's answer
    bool hasWindow = false;   // the framework's QueryInterface answer
    uint32_t handle = 0;      // the window's handle
    bool isWindow = false;    // IsWindow's answer
    uint32_t addResult = 0;   // the service's slot 11 answer
    int nextStream = 100;     // ids of the streams the stood-in constructors make
} gScript;
Fake *gWindow = nullptr;

bool __fastcall SlotQuery(Fake *self, void *, uint32_t riid, void **ppv)
{
    Log("#%d.query(%x)", self->id, riid);
    if (!gScript.hasWindow) return false;
    *ppv = gWindow;
    ++gWindow->refs;
    return true;
}
uint32_t __fastcall SlotAddRef(Fake *self, void *)
{
    Log("#%d.addref", self->id);
    return static_cast<uint32_t>(++self->refs);
}
uint32_t __fastcall SlotRelease(Fake *self, void *)
{
    Log("#%d.release", self->id);
    return static_cast<uint32_t>(--self->refs);
}
void __fastcall SlotFlush(Fake *self, void *) { Log("#%d.flush", self->id); }
cRZString *__fastcall SlotFormat(Fake *self, void *, void *record)
{
    Log("#%d.format(%p)", self->id, record);
    return self->text;
}
HWND __fastcall SlotHandle(Fake *self, void *)
{
    Log("#%d.handle", self->id);
    return reinterpret_cast<HWND>(static_cast<uintptr_t>(gScript.handle));
}
const char *__fastcall SlotName(Fake *self, void *)
{
    Log("#%d.name", self->id);
    return self->name;
}
void __fastcall SlotInteger(Fake *self, void *, int32_t value) { Log("#%d.int(%d)", self->id, value); }
uint32_t __fastcall SlotAddOutput(Fake *self, void *, const char *name, void *output);
void __fastcall SlotText(Fake *self, void *, const char *text) { Log("#%d.t'%s'", self->id, text); }
bool __fastcall SlotString(Fake *self, void *, const cRZString *text)
{
    Log("#%d.s'%s'", self->id, text->mString.c_str());
    return gScript.writeOk;
}

BOOL WINAPI FakeIsWindow(HWND handle)
{
    Log("IsWindow(%p)", static_cast<void *>(handle));
    return gScript.isWindow;
}
BOOL WINAPI FakeSetWindowTextA(HWND handle, const char *text)
{
    Log("SetWindowTextA(%p, '%s')", static_cast<void *>(handle), text);
    return TRUE;
}

// The stood-in cRZOFStream constructors: a fake stream in place, its arguments logged.
cRZOFStream *__fastcall StandStreamFromChars(void *self, void *, const char *path, uint8_t binary, uint32_t order, uint8_t autoFlush, uint8_t append)
{
    Log("ofstream('%s', %u, %u, %u, %u)", path, binary, order, autoFlush, append);
    Fake *f = static_cast<Fake *>(self);
    *f = Fake{gVTable, gScript.nextStream++, nullptr, nullptr, 0};
    return static_cast<cRZOFStream *>(self);
}
cRZOFStream *__fastcall StandStreamFromString(void *self, void *, const cIGZString *path, uint8_t binary, uint32_t order, uint8_t autoFlush, uint8_t append)
{
    Log("ofstream(s'%s', %u, %u, %u, %u)", reinterpret_cast<const cRZString *>(path)->mString.c_str(), binary, order, autoFlush, append);
    Fake *f = static_cast<Fake *>(self);
    *f = Fake{gVTable, gScript.nextStream++, nullptr, nullptr, 0};
    return static_cast<cRZOFStream *>(self);
}
using FromCharsFn = cRZOFStream *(cRZOFStream::*)(const char *, uint8_t, uint32_t, uint8_t, uint8_t);
using FromStringFn = cRZOFStream *(cRZOFStream::*)(const cIGZString &, uint8_t, uint32_t, uint8_t, uint8_t);

struct Setup {
    Setup()
    {
        gVTable[0] = reinterpret_cast<void *>(&SlotQuery);
        gVTable[1] = reinterpret_cast<void *>(&SlotAddRef);
        gVTable[2] = reinterpret_cast<void *>(&SlotRelease);
        gVTable[3] = reinterpret_cast<void *>(&SlotFlush);
        gVTable[4] = reinterpret_cast<void *>(&SlotFormat);
        gVTable[5] = reinterpret_cast<void *>(&SlotHandle);
        gVTable[7] = reinterpret_cast<void *>(&SlotName);
        gVTable[8] = reinterpret_cast<void *>(&SlotInteger);
        gVTable[11] = reinterpret_cast<void *>(&SlotAddOutput);
        gVTable[14] = reinterpret_cast<void *>(&SlotText);
        gVTable[15] = reinterpret_cast<void *>(&SlotString);
    }
} gSetup;

// The fakes a case uses: stream #1, filter #2, formatter #3, framework #4, window #5, service #6.
struct Fakes {
    cRZString text;
    Fake stream{gVTable, 1, nullptr, nullptr, 0};
    Fake filter{gVTable, 2, nullptr, "FilterName", 0};
    Fake formatter{gVTable, 3, nullptr, "FormatterName", 0};
    Fake framework{gVTable, 4, nullptr, nullptr, 0};
    Fake window{gVTable, 5, nullptr, nullptr, 0};
    Fake service{gVTable, 6, nullptr, nullptr, 0};
    Fakes() { text.Construct(); formatter.text = &text; gWindow = &window; }
    ~Fakes() { text.Destruct(); }
    // Back to the case's start before each side runs.
    void Reset(const Script &script)
    {
        for (Fake *f : {&stream, &filter, &formatter, &framework, &window, &service}) f->refs = 0;
        gScript = script;
        gLog.clear();
    }
};

std::string RandomText(std::mt19937 &rng, bool lineBreaks = false)
{
    static const size_t lengths[] = {0, 1, 5, 15, 16, 17, 40, 79, 80, 81, 200};
    std::string s(lengths[rng() % 11], 'a');
    for (char &c : s) c = static_cast<char>(lineBreaks && rng() % 6 == 0 ? '\n' : 'a' + rng() % 26);
    return s;
}

uint32_t Rva(difftest::Context &ctx, const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(ctx.image.At(0)));
}

std::string Canon(difftest::Context &ctx, const cLogOutputBackEnd *o)
{
    char buf[600];
    std::snprintf(buf, sizeof(buf), "vt=%x ref=%u stream=%s filter=%s formatter=%s textvt=%x text='%s' en=%u synch=%u flush=%d count=%u w40=%u",
                  Rva(ctx, o->mVTable), o->mRefCount, Id(o->mStream.mObject).c_str(), Id(o->mFilter).c_str(), Id(o->mFormatter).c_str(),
                  Rva(ctx, o->mText.mVTable), o->mText.mString.c_str(), o->mEnabled, o->mFlag, o->mInterval, o->mCounter, o->mField40);
    return buf;
}
// withAppend false for objects from operator new: the constructor leaves the byte at +0x64 as allocated.
std::string Canon(difftest::Context &ctx, const cLogFileOutputBackEnd *o, bool withAppend = true)
{
    return Canon(ctx, &o->mDefault.mBase) + " pathvt=" + std::to_string(Rva(ctx, o->mPath.mVTable)) + " path='" + o->mPath.mString +
           "' append=" + (withAppend ? std::to_string(o->mAppend) : std::string("?")) + " defer=" + std::to_string(o->mDeferOpen);
}
std::string Canon(difftest::Context &ctx, const cLogCaptionOutputBackEnd *o)
{
    return Canon(ctx, &o->mBase) + " caption='" + o->mCaption.mString + "' lockvt=" + std::to_string(Rva(ctx, o->mLock.mVTable));
}

// A filled block from operator new (the same bytes on both sides for the same seed).
uint8_t *NewFilled(size_t size, uint32_t seed)
{
    uint8_t *m = static_cast<uint8_t *>(::operator new(size));
    std::mt19937 rng(seed);
    for (size_t i = 0; i < size; ++i) m[i] = static_cast<uint8_t>(rng());
    return m;
}

// The base's fields from the case's script (the fakes held without AddRef, as a test fixture).
struct BaseFields {
    bool stream, filter, formatter;
    std::string text;
    uint8_t enabled, synch;
    int32_t interval;
    uint32_t counter, w40;
    BaseFields(std::mt19937 &rng)
    {
        stream = rng() % 5 != 0;
        filter = rng() % 2 == 0;
        formatter = rng() % 4 != 0;
        text = RandomText(rng);
        static const uint8_t bytes[] = {0, 1, 2, 0xff};
        enabled = bytes[rng() % 4];
        synch = bytes[rng() % 4];
        static const int32_t intervals[] = {1, 2, 3, 5, -3, 0x7fffffff};
        interval = intervals[rng() % 6];
        counter = rng() % 12;
        w40 = rng();
    }
    void Apply(cLogOutputBackEnd *o, Fakes &f) const
    {
        o->mStream.mObject = stream ? &f.stream : nullptr;
        o->mFilter = filter ? &f.filter : nullptr;
        o->mFormatter = formatter ? &f.formatter : nullptr;
        o->mText.mString = text;
        o->mEnabled = enabled;
        o->mFlag = synch;
        o->mInterval = interval;
        o->mCounter = counter;
        o->mField40 = w40;
    }
};

} // namespace

// ---- the base: configuration text ------------------------------------------------------------------------

DIFF_TEST(GZLogOutput_WriteConfig, kWriteConfig)
{
    auto original = ctx.Original<void(__thiscall *)(void *, void *)>(kWriteConfig);
    std::mt19937 rng(kWriteConfig);
    Fakes f;
    for (int i = 0; i < 600; ++i) {
        BaseFields fields(rng);
        static const char *const names[] = {"", "Name", "a much longer formatter name"};
        f.filter.name = names[rng() % 3];
        f.formatter.name = names[rng() % 3];
        uint8_t *a = NewFilled(cLogOutputBackEnd::kSize, i);
        uint8_t *b = NewFilled(cLogOutputBackEnd::kSize, i);
        auto *oa = reinterpret_cast<cLogOutputBackEnd *>(a)->Construct();
        auto *ob = reinterpret_cast<cLogOutputBackEnd *>(b)->Construct();
        fields.Apply(oa, f);
        fields.Apply(ob, f);
        Fake out{gVTable, 9, nullptr, nullptr, 0};
        f.Reset(Script{});
        original(oa, &out);
        std::string logA = gLog;
        f.Reset(Script{});
        ob->WriteConfig(&out);
        ++ctx.cases;
        if (logA != gLog) ctx.Fail("case %d: writes differ\n  A: %s\n  B: %s", i, logA.c_str(), gLog.c_str());
        if (Canon(ctx, oa) != Canon(ctx, ob)) ctx.Fail("case %d: object differs", i);
        oa->mStream.mObject = oa->mFilter = oa->mFormatter = nullptr;
        ob->mStream.mObject = ob->mFilter = ob->mFormatter = nullptr;
        oa->DeletingDestructor(1);
        ob->DeletingDestructor(1);
    }
}

// ---- destructors ------------------------------------------------------------------------------------------

namespace {

enum class Kind { Base, Default, File, Caption };

size_t SizeOf(Kind k)
{
    switch (k) {
    case Kind::Base: return cLogOutputBackEnd::kSize;
    case Kind::Default: return cLogDefaultOutputBackEnd::kSize;
    case Kind::File: return cLogFileOutputBackEnd::kSize;
    default: return cLogCaptionOutputBackEnd::kSize;
    }
}

// Builds a back end of the kind in a filled heap block with our (verified) constructors, then the case's fields.
cLogOutputBackEnd *Build(Kind k, uint32_t seed, const BaseFields &fields, const std::string &extra, Fakes &f)
{
    uint8_t *m = NewFilled(SizeOf(k), seed);
    cLogOutputBackEnd *base = reinterpret_cast<cLogOutputBackEnd *>(m);
    switch (k) {
    case Kind::Base: base->Construct(); break;
    case Kind::Default: reinterpret_cast<cLogDefaultOutputBackEnd *>(m)->Construct("x", nullptr); break;
    case Kind::File: {
        auto *file = reinterpret_cast<cLogFileOutputBackEnd *>(m);
        file->Construct("x", "unused", 0, 1);
        file->mPath.mString = extra;
        break;
    }
    case Kind::Caption: {
        auto *caption = reinterpret_cast<cLogCaptionOutputBackEnd *>(m);
        caption->Construct("x");
        caption->mCaption.mString = extra;
        break;
    }
    }
    fields.Apply(base, f);
    return base;
}

// The heap buffers a back end's strings own (freed by its destructor).
std::vector<const void *> OwnedBuffers(Kind k, const cLogOutputBackEnd *o)
{
    std::vector<const void *> v;
    auto add = [&v](const std::string &s) {
        if (s.capacity() > 15) v.push_back(s.data());
    };
    add(o->mText.mString);
    if (k == Kind::File) add(reinterpret_cast<const cLogFileOutputBackEnd *>(o)->mPath.mString);
    if (k == Kind::Caption) add(reinterpret_cast<const cLogCaptionOutputBackEnd *>(o)->mCaption.mString);
    return v;
}

// The bytes a destroyed object keeps that both sides must agree on: the vtables and the strings' size fields
// and first bytes (heap pointers masked), and the lock's vtable.
std::vector<std::pair<size_t, size_t>> DestroyedMasks(Kind k)
{
    std::vector<std::pair<size_t, size_t>> skip = {{0x19, 3}};
    if (k == Kind::File || k == Kind::Caption) skip.push_back({0x49, 3});
    if (k == Kind::Caption) skip.push_back({0x68, 4}); // the lock's block pointer
    return skip;
}

void CallOurs(Kind k, cLogOutputBackEnd *o, std::optional<uint32_t> flags)
{
    switch (k) {
    case Kind::Base:
        if (flags) o->DeletingDestructor(*flags); else o->Destruct();
        break;
    case Kind::Default: {
        auto *d = reinterpret_cast<cLogDefaultOutputBackEnd *>(o);
        if (flags) d->DeletingDestructor(*flags); else d->Destruct();
        break;
    }
    case Kind::File: {
        auto *d = reinterpret_cast<cLogFileOutputBackEnd *>(o);
        if (flags) d->DeletingDestructor(*flags); else d->Destruct();
        break;
    }
    case Kind::Caption: {
        auto *d = reinterpret_cast<cLogCaptionOutputBackEnd *>(o);
        if (flags) d->DeletingDestructor(*flags); else d->Destruct();
        break;
    }
    }
}

// One destructor test: `dtor` (no flags) or `deleting` over the flag set.
void DestructorCases(difftest::Context &ctx, Kind k, uint32_t dtorRva, uint32_t deletingRva)
{
    auto dtor = ctx.Original<void(__thiscall *)(void *)>(dtorRva);
    auto deleting = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(deletingRva);
    std::mt19937 rng(dtorRva);
    Fakes f;
    for (int i = 0; i < 320; ++i) {
        const bool viaDeleting = i % 2 == 1;
        const uint32_t flags = kFlagSet[(i / 2) % 8];
        BaseFields fields(rng);
        const std::string extra = RandomText(rng);
        const uint32_t seed = rng();
        cLogOutputBackEnd *a = Build(k, seed, fields, extra, f);
        cLogOutputBackEnd *b = Build(k, seed, fields, extra, f);
        std::vector<const void *> ownedA = OwnedBuffers(k, a), ownedB = OwnedBuffers(k, b);
        bool freedA, freedB;
        size_t liveA = 0, liveB = 0;
        f.Reset(Script{});
        void *ra = a;
        {
            difftest::FreeRecorder frees;
            if (viaDeleting) ra = deleting(a, flags); else dtor(a);
            frees.Stop();
            freedA = frees.Freed(a);
            for (const void *p : ownedA) liveA += !frees.Freed(p);
        }
        std::string logA = gLog;
        f.Reset(Script{});
        {
            difftest::FreeRecorder frees;
            if (viaDeleting) CallOurs(k, b, flags); else CallOurs(k, b, std::nullopt);
            frees.Stop();
            freedB = frees.Freed(b);
            for (const void *p : ownedB) liveB += !frees.Freed(p);
        }
        ++ctx.cases;
        if (logA != gLog) ctx.Fail("case %d: releases differ\n  A: %s\n  B: %s", i, logA.c_str(), gLog.c_str());
        if (viaDeleting && ra != a) ctx.Fail("case %d: deleting destructor returns %p, want the object", i, ra);
        const bool wantFreed = viaDeleting && (flags & 1);
        if (freedA != wantFreed || freedB != wantFreed) ctx.Fail("case %d (flags %x): object freed %d/%d", i, flags, freedA, freedB);
        if (liveA || liveB) ctx.Fail("case %d: string buffers still allocated %zu/%zu", i, liveA, liveB);
        if (!wantFreed) {
            int at = FirstDifference(reinterpret_cast<uint8_t *>(a), reinterpret_cast<uint8_t *>(b), SizeOf(k), DestroyedMasks(k));
            if (at >= 0) ctx.Fail("case %d: destroyed object bytes differ at 0x%x", i, at);
            ::operator delete(a);
            ::operator delete(b);
        }
    }
}

} // namespace

DIFF_TEST(GZLogOutput_BaseDestructors, kBaseDtor) // and kBaseDeleting (0xc9f3e6)
{
    DestructorCases(ctx, Kind::Base, kBaseDtor, kBaseDeleting);
}

DIFF_TEST(GZLogOutput_DefaultDestructors, kDefaultDtor) // and kDefaultDeleting (0xc9f683)
{
    DestructorCases(ctx, Kind::Default, kDefaultDtor, kDefaultDeleting);
}

DIFF_TEST(GZLogOutput_FileDestructors, kFileDtor) // and kFileDeleting (0xc9f6a2)
{
    DestructorCases(ctx, Kind::File, kFileDtor, kFileDeleting);
}

DIFF_TEST(GZLogOutput_CaptionDestructors, kCaptionDtor) // and kCaptionDeleting (0xc9b9ef)
{
    DestructorCases(ctx, Kind::Caption, kCaptionDtor, kCaptionDeleting);
}

// ---- the default back end -------------------------------------------------------------------------------

DIFF_TEST(GZLogOutput_DefaultConstruct, kDefaultCtor)
{
    auto original = ctx.Original<cLogDefaultOutputBackEnd *(__thiscall *)(void *, const char *, void *)>(kDefaultCtor);
    std::mt19937 rng(kDefaultCtor);
    Fakes f;
    for (int i = 0; i < 400; ++i) {
        const std::string text = RandomText(rng);
        void *stream = rng() % 4 ? &f.stream : nullptr;
        Pair p(cLogDefaultOutputBackEnd::kSize, rng);
        f.Reset(Script{});
        auto *ra = original(p.a, text.c_str(), stream);
        std::string logA = gLog;
        f.Reset(Script{});
        auto *rb = reinterpret_cast<cLogDefaultOutputBackEnd *>(p.b)->Construct(text.c_str(), stream);
        ++ctx.cases;
        if (reinterpret_cast<uint8_t *>(ra) != p.a || reinterpret_cast<uint8_t *>(rb) != p.b) ctx.Fail("case %d: returns this", i);
        if (logA != gLog) ctx.Fail("case %d: calls differ\n  A: %s\n  B: %s", i, logA.c_str(), gLog.c_str());
        auto *a = reinterpret_cast<cLogDefaultOutputBackEnd *>(p.a);
        auto *b = reinterpret_cast<cLogDefaultOutputBackEnd *>(p.b);
        if (Canon(ctx, &a->mBase) != Canon(ctx, &b->mBase)) ctx.Fail("case %d: objects differ\n  A: %s\n  B: %s", i, Canon(ctx, &a->mBase).c_str(), Canon(ctx, &b->mBase).c_str());
        int at = FirstDifference(p.a, p.b, cLogDefaultOutputBackEnd::kSize, {{0x18, a->mBase.mText.mString.capacity() > 15 ? 4u : 0u}});
        if (at >= 0) ctx.Fail("case %d: bytes differ at 0x%x", i, at);
        if (!p.CanariesIntact()) ctx.Fail("case %d: wrote past the object", i);
        a->mBase.mStream.mObject = b->mBase.mStream.mObject = nullptr;
        a->mBase.mText.Destruct();
        b->mBase.mText.Destruct();
    }
}

DIFF_TEST(GZLogOutput_DefaultOutput, kDefaultOutput)
{
    auto original = ctx.Original<void(__thiscall *)(void *, void *)>(kDefaultOutput);
    std::mt19937 rng(kDefaultOutput);
    Fakes f;
    for (int i = 0; i < 1500; ++i) {
        BaseFields fields(rng);
        fields.formatter = true; // the original reads the formatter unchecked when there is a stream
        f.text.mString = RandomText(rng, true);
        Script script;
        script.writeOk = rng() % 4 != 0;
        void *record = reinterpret_cast<void *>(static_cast<uintptr_t>(0x1000 + rng() % 0x1000));
        const uint32_t seed = rng();
        cLogOutputBackEnd *a = Build(Kind::Default, seed, fields, "", f);
        cLogOutputBackEnd *b = Build(Kind::Default, seed, fields, "", f);
        f.Reset(script);
        original(a, record);
        std::string logA = gLog, canonA = Canon(ctx, a);
        f.Reset(script);
        reinterpret_cast<cLogDefaultOutputBackEnd *>(b)->Output(record);
        ++ctx.cases;
        if (logA != gLog) ctx.Fail("case %d: calls differ\n  A: %s\n  B: %s", i, logA.c_str(), gLog.c_str());
        if (canonA != Canon(ctx, b)) ctx.Fail("case %d: objects differ\n  A: %s\n  B: %s", i, canonA.c_str(), Canon(ctx, b).c_str());
        for (cLogOutputBackEnd *o : {a, b}) {
            o->mStream.mObject = o->mFilter = o->mFormatter = nullptr;
            reinterpret_cast<cLogDefaultOutputBackEnd *>(o)->DeletingDestructor(1);
        }
    }
}

// ---- the log file back end ------------------------------------------------------------------------------

DIFF_TEST(GZLogOutput_FileConstruct, kFileCtor)
{
    auto original = ctx.Original<cLogFileOutputBackEnd *(__thiscall *)(void *, const char *, const char *, uint8_t, uint8_t)>(kFileCtor);
    shape::StandIn fromChars(kOFStreamFromChars, static_cast<FromCharsFn>(&cRZOFStream::Construct), &StandStreamFromChars);
    shape::StandIn fromString(kOFStreamFromString, static_cast<FromStringFn>(&cRZOFStream::Construct), &StandStreamFromString);
    std::mt19937 rng(kFileCtor);
    Fakes f;
    for (int i = 0; i < 400; ++i) {
        const std::string text = RandomText(rng), path = RandomText(rng);
        static const uint8_t bytes[] = {0, 1, 2, 0x80};
        const uint8_t append = bytes[rng() % 4], defer = bytes[rng() % 4];
        Pair p(cLogFileOutputBackEnd::kSize, rng);
        f.Reset(Script{});
        auto *ra = original(p.a, text.c_str(), path.c_str(), append, defer);
        std::string logA = gLog;
        f.Reset(Script{});
        auto *rb = reinterpret_cast<cLogFileOutputBackEnd *>(p.b)->Construct(text.c_str(), path.c_str(), append, defer);
        ++ctx.cases;
        if (reinterpret_cast<uint8_t *>(ra) != p.a || reinterpret_cast<uint8_t *>(rb) != p.b) ctx.Fail("case %d: returns this", i);
        if (logA != gLog) ctx.Fail("case %d: calls differ\n  A: %s\n  B: %s", i, logA.c_str(), gLog.c_str());
        auto *a = reinterpret_cast<cLogFileOutputBackEnd *>(p.a);
        auto *b = reinterpret_cast<cLogFileOutputBackEnd *>(p.b);
        if (Canon(ctx, a) != Canon(ctx, b)) ctx.Fail("case %d: objects differ\n  A: %s\n  B: %s", i, Canon(ctx, a).c_str(), Canon(ctx, b).c_str());
        int at = FirstDifference(p.a, p.b, cLogFileOutputBackEnd::kSize, {{0x08, 4}, {0x18, a->mDefault.mBase.mText.mString.capacity() > 15 ? 4u : 0u}});
        if (at >= 0) ctx.Fail("case %d: bytes differ at 0x%x", i, at);
        if (!p.CanariesIntact()) ctx.Fail("case %d: wrote past the object", i);
        for (cLogFileOutputBackEnd *o : {a, b}) {
            ::operator delete(o->mDefault.mBase.mStream.mObject);
            o->mDefault.mBase.mStream.mObject = nullptr;
            o->mPath.Destruct();
            o->mDefault.mBase.mText.Destruct();
        }
    }
}

DIFF_TEST(GZLogOutput_FileOutput, kFileOutput) // and kFileReopen (0xc9f763)
{
    auto output = ctx.Original<void(__thiscall *)(void *, void *)>(kFileOutput);
    auto reopen = ctx.Original<void(__thiscall *)(void *)>(kFileReopen);
    shape::StandIn fromChars(kOFStreamFromChars, static_cast<FromCharsFn>(&cRZOFStream::Construct), &StandStreamFromChars);
    shape::StandIn fromString(kOFStreamFromString, static_cast<FromStringFn>(&cRZOFStream::Construct), &StandStreamFromString);
    std::mt19937 rng(kFileOutput);
    Fakes f;
    for (int i = 0; i < 1500; ++i) {
        const bool viaReopen = i % 3 == 0;
        BaseFields fields(rng);
        fields.formatter = true;
        f.text.mString = RandomText(rng);
        const std::string path = RandomText(rng);
        static const uint8_t bytes[] = {0, 1, 2, 0x80};
        const uint8_t append = bytes[rng() % 4], defer = bytes[rng() % 4];
        Script script;
        script.writeOk = rng() % 4 != 0;
        void *record = reinterpret_cast<void *>(static_cast<uintptr_t>(0x1000 + rng() % 0x1000));
        const uint32_t seed = rng();
        auto *a = reinterpret_cast<cLogFileOutputBackEnd *>(Build(Kind::File, seed, fields, path, f));
        auto *b = reinterpret_cast<cLogFileOutputBackEnd *>(Build(Kind::File, seed, fields, path, f));
        for (cLogFileOutputBackEnd *o : {a, b}) {
            o->mAppend = append;
            o->mDeferOpen = defer;
        }
        f.Reset(script);
        if (viaReopen) reopen(a); else output(a, record);
        std::string logA = gLog, canonA = Canon(ctx, a);
        f.Reset(script);
        if (viaReopen) b->Reopen(); else b->Output(record);
        ++ctx.cases;
        if (logA != gLog) ctx.Fail("case %d (%s): calls differ\n  A: %s\n  B: %s", i, viaReopen ? "reopen" : "output", logA.c_str(), gLog.c_str());
        if (canonA != Canon(ctx, b)) ctx.Fail("case %d: objects differ\n  A: %s\n  B: %s", i, canonA.c_str(), Canon(ctx, b).c_str());
        for (cLogFileOutputBackEnd *o : {a, b}) {
            void *s = o->mDefault.mBase.mStream.mObject;
            if (s && s != &f.stream) ::operator delete(s);
            o->mDefault.mBase.mStream.mObject = o->mDefault.mBase.mFilter = o->mDefault.mBase.mFormatter = nullptr;
            o->DeletingDestructor(1);
        }
    }
}

// ---- the caption back end ---------------------------------------------------------------------------------

DIFF_TEST(GZLogOutput_CaptionOutput, kCaptionOutput)
{
    auto original = ctx.Original<void(__thiscall *)(void *, void *)>(kCaptionOutput);
    std::mt19937 rng(kCaptionOutput);
    Fakes f;
    for (int i = 0; i < 1500; ++i) {
        BaseFields fields(rng);
        fields.formatter = true;
        f.text.mString = RandomText(rng, true);
        const std::string before = RandomText(rng, true);
        void *record = reinterpret_cast<void *>(static_cast<uintptr_t>(0x1000 + rng() % 0x1000));
        const uint32_t seed = rng();
        auto *a = reinterpret_cast<cLogCaptionOutputBackEnd *>(Build(Kind::Caption, seed, fields, before, f));
        auto *b = reinterpret_cast<cLogCaptionOutputBackEnd *>(Build(Kind::Caption, seed, fields, before, f));
        f.Reset(Script{});
        original(a, record);
        std::string logA = gLog, canonA = Canon(ctx, a);
        f.Reset(Script{});
        b->Output(record);
        ++ctx.cases;
        if (logA != gLog) ctx.Fail("case %d: calls differ\n  A: %s\n  B: %s", i, logA.c_str(), gLog.c_str());
        if (canonA != Canon(ctx, b)) ctx.Fail("case %d: objects differ\n  A: %s\n  B: %s", i, canonA.c_str(), Canon(ctx, b).c_str());
        if (a->mLock.mBlock->lockCount != 0 || b->mLock.mBlock->lockCount != 0) ctx.Fail("case %d: lock left held", i);
        for (cLogCaptionOutputBackEnd *o : {a, b}) {
            o->mBase.mStream.mObject = o->mBase.mFilter = o->mBase.mFormatter = nullptr;
            o->DeletingDestructor(1);
        }
    }
}

DIFF_TEST(GZLogOutput_CaptionFlush, kCaptionFlush)
{
    auto original = ctx.Original<void(__thiscall *)(void *)>(kCaptionFlush);
    std::mt19937 rng(kCaptionFlush);
    Fakes f;
    Slots slots(ctx);
    slots.Set(kFrameworkGlobal, &f.framework);
    slots.Set(kIatIsWindow, reinterpret_cast<void *>(&FakeIsWindow));
    slots.Set(kIatSetWindowTextA, reinterpret_cast<void *>(&FakeSetWindowTextA));
    for (int i = 0; i < 1000; ++i) {
        BaseFields fields(rng);
        const std::string caption = rng() % 4 ? RandomText(rng) : std::string();
        Script script;
        script.hasWindow = rng() % 4 != 0;
        script.handle = rng() % 4 ? 0x10000 + rng() % 0x1000 : 0;
        script.isWindow = rng() % 4 != 0;
        const uint32_t seed = rng();
        auto *a = reinterpret_cast<cLogCaptionOutputBackEnd *>(Build(Kind::Caption, seed, fields, caption, f));
        auto *b = reinterpret_cast<cLogCaptionOutputBackEnd *>(Build(Kind::Caption, seed, fields, caption, f));
        f.Reset(script);
        original(a);
        std::string logA = gLog, canonA = Canon(ctx, a);
        const int32_t windowRefsA = f.window.refs;
        f.Reset(script);
        b->Flush();
        ++ctx.cases;
        if (logA != gLog) ctx.Fail("case %d: calls differ\n  A: %s\n  B: %s", i, logA.c_str(), gLog.c_str());
        if (canonA != Canon(ctx, b)) ctx.Fail("case %d: objects differ\n  A: %s\n  B: %s", i, canonA.c_str(), Canon(ctx, b).c_str());
        if (windowRefsA != f.window.refs) ctx.Fail("case %d: window references %d/%d", i, windowRefsA, f.window.refs);
        if (a->mLock.mBlock->lockCount != 0 || b->mLock.mBlock->lockCount != 0) ctx.Fail("case %d: lock left held", i);
        for (cLogCaptionOutputBackEnd *o : {a, b}) {
            o->mBase.mStream.mObject = o->mBase.mFilter = o->mBase.mFormatter = nullptr;
            o->DeletingDestructor(1);
        }
    }
}

// ---- the service's slots 21 and 22 ----------------------------------------------------------------------------

namespace {
difftest::Context *gCtx = nullptr;
enum class Made { Default, File } gMade;
cLogOutputBackEnd *gLastOutput = nullptr;

uint32_t __fastcall SlotAddOutput(Fake *self, void *, const char *name, void *output)
{
    gLastOutput = static_cast<cLogOutputBackEnd *>(output);
    std::string canon = !output ? "null"
                        : gMade == Made::File ? Canon(*gCtx, static_cast<cLogFileOutputBackEnd *>(output), false)
                                              : Canon(*gCtx, static_cast<cLogOutputBackEnd *>(output));
    Log("#%d.add('%s', %s)", self->id, name, canon.c_str());
    return gScript.addResult;
}

} // namespace

DIFF_TEST(GZLogOutput_ServiceAddOutputs, kAddStreamOutput) // and kAddFileOutput (0xc9caba)
{
    auto addStream = ctx.Original<uint32_t(__thiscall *)(void *, const char *, void *)>(kAddStreamOutput);
    auto addFile = ctx.Original<uint32_t(__thiscall *)(void *, const char *, const char *, uint8_t, uint8_t)>(kAddFileOutput);
    shape::StandIn fromChars(kOFStreamFromChars, static_cast<FromCharsFn>(&cRZOFStream::Construct), &StandStreamFromChars);
    shape::StandIn fromString(kOFStreamFromString, static_cast<FromStringFn>(&cRZOFStream::Construct), &StandStreamFromString);
    gCtx = &ctx;
    std::mt19937 rng(kAddStreamOutput);
    Fakes f;
    for (int i = 0; i < 600; ++i) {
        const bool file = i % 2 == 1;
        gMade = file ? Made::File : Made::Default;
        const std::string name = RandomText(rng), path = RandomText(rng);
        void *stream = rng() % 4 ? &f.stream : nullptr;
        const uint8_t append = static_cast<uint8_t>(rng() % 3);
        const uint8_t defer = static_cast<uint8_t>(rng() % 3);
        Script script;
        script.addResult = rng();
        cLogOutputBackEnd *made[2];
        uint32_t results[2];
        std::string logs[2];
        for (int side = 0; side < 2; ++side) {
            f.Reset(script);
            gLastOutput = nullptr;
            if (file) {
                // The slot logs the new back end's canon (the file one without the byte at +0x64, left as allocated).
                results[side] = side == 0 ? addFile(&f.service, name.c_str(), path.c_str(), append, defer)
                                          : ServiceAddFileOutput(&f.service, nullptr, name.c_str(), path.c_str(), append, defer);
            } else {
                results[side] = side == 0 ? addStream(&f.service, name.c_str(), stream) : ServiceAddStreamOutput(&f.service, nullptr, name.c_str(), stream);
            }
            logs[side] = gLog;
            made[side] = gLastOutput;
        }
        ++ctx.cases;
        if (results[0] != results[1]) ctx.Fail("case %d: returns %x/%x", i, results[0], results[1]);
        if (logs[0] != logs[1]) ctx.Fail("case %d: calls differ\n  A: %s\n  B: %s", i, logs[0].c_str(), logs[1].c_str());
        for (cLogOutputBackEnd *o : made) {
            if (!o) continue;
            void *s = o->mStream.mObject;
            if (s && s != &f.stream) ::operator delete(s);
            o->mStream.mObject = nullptr;
            if (file) reinterpret_cast<cLogFileOutputBackEnd *>(o)->DeletingDestructor(1);
            else reinterpret_cast<cLogDefaultOutputBackEnd *>(o)->DeletingDestructor(1);
        }
    }
    gCtx = nullptr;
}
