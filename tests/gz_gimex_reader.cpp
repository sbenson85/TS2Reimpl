// Differential tests for src/gz/cGZGimexReader.* (standalone shape): the Gimex reader's and writer's slots, constructors,
// creators, Release and FinalRelease.
//
// The original side's object comes from the image's creator (0x87b428 / 0x87b43e, through the constructors 0x87b4b4 /
// 0x87b4d1) with the game's operator new (0x824632) standing in for ours, ours from our creator. A script of slot calls
// (through each object's own vtable) runs on each side from the same seeds, and the sides' traces are compared: every
// slot's answer and out values, the object's words after each call (+4 / +8 as the fake they point at, the vtable left
// out: the side's own class's table), the Gimex lock's count (0x10e6918, constructed for the test), the codecs' calls
// (gz_gimex_fakes.h, every entry point of rows 1..6 a recording fake; rows 0 and 7..49 the null codec) and the fakes'
// own logs. The fakes:
//   * streams (cIGZFile): IsOpen (junk above al), Close, GetPath (a cRZString: "c:\dir\pic.png" and so on),
//     GetOpenModes, AddRef / Release with counts; any other slot logs a trap;
//   * GetDirectorFrameworkObject (0x1ae18d, both sides) answers a COM double whose GetClassObject (slot 3) hands out
//     record files (cIGZDBSerialRecordAsIGZFile: SetRecord, AsFile = a stream face) or fails, from a script;
//   * cRZFileName's constructor, destructor and GetExtension (0x1c7ef4, 0x1c0eb2, 0x1c87ed) stand in on both sides:
//     the real constructor completes relative paths from the working directory (an import); the stand-in fills only the
//     extension (+0x60), the text after the last dot of the name.
#include "code_patch.h"
#include "difftest.h"
#include "game/game.h"
#include "gz/cGZGimexReader.h"
#include "gz_gimex_fakes.h"
#include "impfx.h"
#include "rz/StaticGetters.h"
#include "rz/cRZCriticalSection.h"
#include "rz/cRZFileName.h"
#include "rz/cRZString.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace gimexfx;

constexpr uint32_t kCreateReader = 0x87b428;
constexpr uint32_t kCreateWriter = 0x87b43e;
constexpr uint32_t kConstructReader = 0x87b4b4;
constexpr uint32_t kConstructWriter = 0x87b4d1;
constexpr uint32_t kReaderVTable = 0xe4a398;
constexpr uint32_t kWriterVTable = 0xe4a3d0;
constexpr uint32_t kGameNew = 0x824632;
constexpr uint32_t kGameSizedDelete = 0x824624;
constexpr uint32_t kCodecAlloc = 0x87da65; // the codecs' allocator (__stdcall, size): what FreeInfo frees
constexpr uint32_t kGimexLock = 0x10e6918;

template <class F> void *VFn(F f)
{
    return reinterpret_cast<void *>(f);
}

// ---- the trace ----

std::vector<std::string> gLog;

void Log(const char *format, ...)
{
    char text[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    gLog.emplace_back(text);
}

// ---- the heap: the game's operator new to ours; the sized deletes (the image's and ours) note the watched block ----

void *gWatched = nullptr;
size_t gFreedSize = 0;

void *__cdecl GameNew(size_t n)
{
    return ::operator new(n);
}

void __cdecl NoteSizedDelete(void *p, size_t n)
{
    if (p != nullptr && p == gWatched) {
        gFreedSize = n;
        gWatched = nullptr;
    }
    std::free(p);
}

struct Heap {
    shape::CodePatch gameNew{kGameNew, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch gameDelete{kGameSizedDelete, reinterpret_cast<const void *>(&NoteSizedDelete)};
    shape::CodePatch oursDelete{reinterpret_cast<void *>(static_cast<void (*)(void *, size_t)>(&::operator delete)),
                                reinterpret_cast<const void *>(&NoteSizedDelete)};
    Heap() = default;
    ~Heap() { gWatched = nullptr; }
    Heap(const Heap &) = delete;
    Heap &operator=(const Heap &) = delete;
};

// ---- the Gimex lock, constructed for the test ----

cRZCriticalSection *GimexLock()
{
    return GameAddress<cRZCriticalSection>(kGimexLock);
}

struct LockScope {
    LockScope() { GimexLock()->Construct(); }
    ~LockScope() { GimexLock()->Destruct(); }
    LockScope(const LockScope &) = delete;
    LockScope &operator=(const LockScope &) = delete;
};

int32_t LockCount()
{
    return GimexLock()->mBlock->lockCount;
}

// ---- fake streams ----

struct FakeStream {
    void **vt;
    int id;
    uint32_t refs;
    uint32_t open;   // what IsOpen answers (junk above al)
    uint32_t access; // what GetOpenModes writes first
    cRZString *path;
};

constexpr int kStreamSlots = 34;
void *gStreamVt[kStreamSlots];

uint32_t __fastcall StreamAddRef(FakeStream *s, void *)
{
    Log("s%d.AddRef -> %u", s->id, s->refs + 1);
    return ++s->refs;
}
uint32_t __fastcall StreamRelease(FakeStream *s, void *)
{
    Log("s%d.Release -> %u", s->id, s->refs - 1);
    return --s->refs;
}
uint32_t __fastcall StreamIsOpen(FakeStream *s, void *)
{
    Log("s%d.IsOpen", s->id);
    return s->open;
}
uint32_t __fastcall StreamClose(FakeStream *s, void *)
{
    Log("s%d.Close", s->id);
    s->open &= ~0xffu;
    return 1;
}
void *__fastcall StreamGetPath(FakeStream *s, void *)
{
    Log("s%d.GetPath", s->id);
    return s->path;
}
uint32_t __fastcall StreamGetOpenModes(FakeStream *s, void *, uint32_t *access, uint32_t *creation, uint32_t *sharing)
{
    Log("s%d.GetOpenModes", s->id);
    *access = s->access;
    *creation = 0x33;
    *sharing = 0x44;
    return 0x101;
}
template <int N> uint32_t __fastcall StreamTrap(FakeStream *s, void *)
{
    Log("s%d.TRAP slot %d", s->id, N);
    return 0;
}
template <size_t... I> void FillStreamTraps(std::index_sequence<I...>)
{
    ((gStreamVt[I] = VFn(&StreamTrap<static_cast<int>(I)>)), ...);
}
void BuildStreamVt()
{
    FillStreamTraps(std::make_index_sequence<kStreamSlots>());
    gStreamVt[1] = VFn(&StreamAddRef);
    gStreamVt[2] = VFn(&StreamRelease);
    gStreamVt[4] = VFn(&StreamIsOpen);
    gStreamVt[5] = VFn(&StreamClose);
    gStreamVt[25] = VFn(&StreamGetPath);
    gStreamVt[30] = VFn(&StreamGetOpenModes);
}

// ---- fake record files and the framework ----

struct FakeRecordFile {
    void **vt;
    int id;
    uint32_t refs;
    FakeStream face;
};

uint32_t __fastcall RecordAddRef(FakeRecordFile *r, void *)
{
    Log("r%d.AddRef -> %u", r->id, r->refs + 1);
    return ++r->refs;
}
uint32_t __fastcall RecordRelease(FakeRecordFile *r, void *)
{
    Log("r%d.Release -> %u", r->id, r->refs - 1);
    return --r->refs;
}
uint32_t __fastcall RecordSetRecord(FakeRecordFile *r, void *, void *record)
{
    Log("r%d.SetRecord(%08x)", r->id, shape::P(record));
    return 0x5501; // the real one answers 1; the callers ignore it
}
FakeStream *__fastcall RecordAsFile(FakeRecordFile *r, void *)
{
    Log("r%d.AsFile", r->id);
    return &r->face;
}
template <int N> uint32_t __fastcall RecordTrap(FakeRecordFile *r, void *)
{
    Log("r%d.TRAP slot %d", r->id, N);
    return 0;
}
void *gRecordVt[8] = {VFn(&RecordTrap<0>), VFn(&RecordAddRef),    VFn(&RecordRelease), VFn(&RecordSetRecord),
                      VFn(&RecordTrap<4>), VFn(&RecordAsFile),     VFn(&RecordTrap<6>), VFn(&RecordTrap<7>)};

constexpr int kStreams = 3;
constexpr int kRecords = 3;
FakeStream gStreams[kStreams];
FakeRecordFile gRecords[kRecords];
uint32_t gRecordToken[4]; // the record SetRecord passes on
std::mt19937 gScript;     // the framework's answers
int gNextRecord = 0;

uint32_t __fastcall ComGetClassObject(void *, void *, uint32_t classId, uint32_t iid, void **out)
{
    const bool make = gScript() % 4 != 0;
    Log("com.GetClassObject(%08x, %08x) -> %d", classId, iid, make);
    if (!make) return 0;
    FakeRecordFile &r = gRecords[gNextRecord++ % kRecords];
    ++r.refs;
    *out = &r;
    return 1;
}
template <int N> uint32_t __fastcall ComTrap(void *, void *)
{
    Log("com.TRAP slot %d", N);
    return 0;
}
void *gComVt[6] = {VFn(&ComTrap<0>), VFn(&ComTrap<1>), VFn(&ComTrap<2>), VFn(&ComGetClassObject), VFn(&ComTrap<4>),
                   VFn(&ComTrap<5>)};
struct {
    void **vt;
} gCom{gComVt};
void *__cdecl FakeDirectorObject()
{
    return &gCom;
}

// ---- cRZFileName stand-ins ----

constexpr uint32_t kFileNameExt = 0x60;
cRZString *Ext(void *self)
{
    return reinterpret_cast<cRZString *>(static_cast<uint8_t *>(self) + kFileNameExt);
}
void *__fastcall FileNameConstruct(void *self, void *, const cRZString *path)
{
    std::string text = path->mString;
    const size_t slash = text.find_last_of("/\\:");
    if (slash != std::string::npos) text.erase(0, slash + 1);
    const size_t dot = text.rfind('.');
    text = dot == std::string::npos ? std::string() : text.substr(dot + 1);
    Log("filename(%s) ext %s", path->mString.c_str(), text.c_str());
    std::memset(self, 0xab, 0xa0);
    Ext(self)->Construct(text.c_str());
    return self;
}
void __fastcall FileNameDestruct(void *self, void *)
{
    Log("filename.Destruct");
    Ext(self)->Destruct();
}
cRZString *__fastcall FileNameGetExtension(void *self, void *)
{
    return Ext(self);
}

// ---- the world for one side ----

struct StreamConfig {
    uint32_t open, access;
    const char *path;
};

struct World {
    shape::StandIn director{0x1ae18d, &GetDirectorFrameworkObject, &FakeDirectorObject};
    shape::StandIn fileName{0x1c7ef4, &cRZFileName::Construct, &FileNameConstruct};
    shape::StandIn fileNameDestruct{0x1c0eb2, &cRZFileName::Destruct, &FileNameDestruct};
    shape::StandIn extension{0x1c87ed, &cRZFileName::GetExtension, &FileNameGetExtension};
    CodecFakes codecs;
    Heap heap;
    LockScope lock;
    alignas(cRZString) uint8_t pathMemory[kStreams + kRecords][sizeof(cRZString)];
    cRZString *Path(int i) { return reinterpret_cast<cRZString *>(pathMemory[i]); }
    StreamConfig configs[kStreams + kRecords];

    explicit World(std::mt19937 &rng)
    {
        BuildStreamVt();
        static const char *const kPaths[] = {"c:\\dir\\pic.png", "x.BMP",       "noext",  "",
                                             "d:/q.tga.jpg",     "f.xyz",       "a.fsh",  "dir.jpg\\file",
                                             "photo.JPEG",       "c:\\t\\i.tga", "s.-png", "m.jpg"};
        for (int i = 0; i < kStreams + kRecords; ++i) {
            const uint32_t junk = (rng() % 3) << 8;
            configs[i] = {junk | (rng() % 4 ? 1u : 0u), rng() % 4, kPaths[rng() % (sizeof(kPaths) / sizeof(*kPaths))]};
            Path(i)->Construct(configs[i].path);
        }
    }
    ~World()
    {
        for (int i = 0; i < kStreams + kRecords; ++i) Path(i)->Destruct();
    }
    World(const World &) = delete;
    World &operator=(const World &) = delete;

    void Reset(uint32_t seed)
    {
        for (int i = 0; i < kStreams; ++i) gStreams[i] = {gStreamVt, i, 1, configs[i].open, configs[i].access, Path(i)};
        for (int i = 0; i < kRecords; ++i) {
            const int k = kStreams + i;
            gRecords[i] = {gRecordVt, i, 0, {gStreamVt, 10 + i, 1, configs[k].open, configs[k].access, Path(k)}};
        }
        gNextRecord = 0;
        gScript.seed(seed);
        gAnswers.seed(seed ^ 0x9e3779b9);
        gEvents.clear();
        gLog.clear();
    }
};

// ---- normalising words for the trace ----

const uint8_t *gObject = nullptr;
int gSide = 0; // 0 the original's object, 1 ours

// The side's current Gimex format: the original's global (0x10e693c) or ours.
int32_t &OriginalCurrentFormat()
{
    return *GameAddress<int32_t>(0x10e693c);
}
int32_t SideCurrentFormat()
{
    return gSide == 0 ? OriginalCurrentFormat() : gimex::CurrentFormat();
}
uint32_t gPixels[16];
uint32_t gFrames;
void *gQiOut;
gimex::Info gWriteInfo;

uint32_t Norm(uint32_t w)
{
    if (w == 0) return 0;
    if (gObject && w - shape::P(gObject) < 0x1c) return 0xb0b00000 | (w - shape::P(gObject));
    for (int i = 0; i < kStreams; ++i)
        if (w == shape::P(&gStreams[i])) return 0x51000000 | i;
    for (int i = 0; i < kRecords; ++i) {
        if (w == shape::P(&gRecords[i])) return 0x52000000 | i;
        if (w == shape::P(&gRecords[i].face)) return 0x53000000 | i;
    }
    if (w == shape::P(gInfoBlock)) return 0x1f000000;
    if (w == shape::P(gPixels)) return 0x1e000000;
    if (w == shape::P(gRecordToken)) return 0x1d000000;
    if (w == shape::P(&gFrames)) return 0x1c000000;
    if (w == shape::P(&gWriteInfo)) return 0x1b000000;
    return w;
}

uint32_t Word(const void *p, uint32_t offset)
{
    uint32_t w;
    std::memcpy(&w, static_cast<const uint8_t *>(p) + offset, 4);
    return w;
}

// The object's words (the vtable left out: the side's own class's), the lock's count, then the codecs' calls.
void Snapshot(const char *what, uint32_t result)
{
    for (const Event &e : gEvents)
        Log("  codec %06x(%08x, %08x, %08x, %08x)", e.entry, Norm(e.words[0]), Norm(e.words[1]), Norm(e.words[2]),
            Norm(e.words[3]));
    gEvents.clear();
    Log("%s -> %08x | %08x %08x %02x %08x %08x %u | lock %d | current %d", what, Norm(result), Norm(Word(gObject, 4)),
        Norm(Word(gObject, 8)), gObject[0xc], Word(gObject, 0x10), Word(gObject, 0x14), Word(gObject, 0x18), LockCount(),
        SideCurrentFormat());
}

// ---- the script ----

const uint32_t kFormats[] = {0, 1, 2, 3, 4, 5, 6, 7, 30, 49, 50, 51, 52, 53, 0xffffffff, 0x80000000, 1000};
const char *const kExtensions[] = {"png", ".bmp", "TGA", "jpg", "fsh", "xyz", "", "-jpeg", "+tga"};
const uint32_t kIids[] = {0, 1, cIGZGimexReader::kIid, cIGZGimexWriter::kIid, 0xca316bea};

template <class T> T Pick(std::mt19937 &rng, const T *items, size_t n)
{
    return items[rng() % n];
}
#define PICK(rng, a) Pick(rng, a, sizeof(a) / sizeof(*(a)))

void *PickStream(std::mt19937 &rng)
{
    switch (rng() % 6) {
    case 0: return nullptr;
    case 1: return &gRecords[0].face;
    default: return &gStreams[rng() % kStreams];
    }
}

// One script on one side: `object` is the side's reader or writer.
template <bool Writer> void RunScript(difftest::Context &ctx, void *object, uint32_t seed, int steps)
{
    using Face = std::conditional_t<Writer, cIGZGimexWriter, cIGZGimexReader>;
    Face *o = static_cast<Face *>(object);
    gObject = static_cast<const uint8_t *>(object);
    std::mt19937 rng(seed);
    const int32_t baseLock = LockCount();
    o->AddRef();
    Snapshot("AddRef", 0);
    char what[96];
    for (int step = 0; step < steps; ++step) {
        uint32_t result = 0;
        const uint32_t op = rng() % 12;
        gFrames = 0x5a5a5a5a;
        switch (op) {
        case 0:
            result = o->SetRecord(reinterpret_cast<cIGZUnknown *>(gRecordToken));
            std::snprintf(what, sizeof(what), "SetRecord");
            break;
        case 1:
        case 2: {
            void *stream = PickStream(rng);
            result = o->SetStream(static_cast<ifaces::cIGZFile *>(stream));
            std::snprintf(what, sizeof(what), "SetStream(%08x)", Norm(shape::P(stream)));
            break;
        }
        case 3: {
            const char *ext = PICK(rng, kExtensions);
            if constexpr (Writer) {
                const int32_t flag = static_cast<int32_t>(rng() % 3);
                result = o->OpenByExtension(ext, flag);
                std::snprintf(what, sizeof(what), "OpenByExtension(%s, %d)", ext, flag);
            } else {
                result = o->OpenByExtension(ext, reinterpret_cast<int32_t *>(&gFrames));
                std::snprintf(what, sizeof(what), "OpenByExtension(%s) frames %08x", ext, gFrames);
            }
            break;
        }
        case 4:
        case 5: {
            const uint32_t format = PICK(rng, kFormats);
            if constexpr (Writer) {
                const int32_t flag = static_cast<int32_t>(rng() % 3);
                result = o->Open(format, flag);
                std::snprintf(what, sizeof(what), "Open(%u, %d)", format, flag);
            } else {
                result = o->Open(format, reinterpret_cast<int32_t *>(&gFrames));
                std::snprintf(what, sizeof(what), "Open(%u) frames %08x", format, gFrames);
            }
            break;
        }
        case 6:
            if constexpr (Writer) {
                std::memset(&gWriteInfo, 0x3c, sizeof(gWriteInfo));
                gWriteInfo.jpegFlag = 0x77;
                const int32_t pitch = static_cast<int32_t>(rng() % 64);
                result = o->Write(&gWriteInfo, gPixels, pitch);
                std::snprintf(what, sizeof(what), "Write(%d) jpegFlag %08x", pitch, gWriteInfo.jpegFlag);
            } else {
                const int32_t frame = static_cast<int32_t>(rng() % 3);
                result = shape::P(o->GetInfo(frame));
                std::snprintf(what, sizeof(what), "GetInfo(%d)", frame);
            }
            break;
        case 7:
            if constexpr (Writer) {
                result = o->Close();
                std::snprintf(what, sizeof(what), "Close");
            } else {
                gimex::Info *info = rng() % 4 ? reinterpret_cast<gimex::Info *>(gInfoBlock) : nullptr;
                const int32_t pitch = static_cast<int32_t>(rng() % 64);
                result = o->Read(info, gPixels, pitch);
                std::snprintf(what, sizeof(what), "Read(%08x, %d)", Norm(shape::P(info)), pitch);
            }
            break;
        case 8:
            result = o->Close();
            std::snprintf(what, sizeof(what), "Close");
            break;
        case 9: {
            const uint32_t iid = PICK(rng, kIids);
            gQiOut = reinterpret_cast<void *>(0x5a5a5a5a);
            result = o->QueryInterface(iid, &gQiOut);
            std::snprintf(what, sizeof(what), "QueryInterface(%08x) out %08x", iid, Norm(shape::P(gQiOut)));
            break;
        }
        case 10: {
            const uint32_t a = o->AddRef();
            result = o->Release();
            std::snprintf(what, sizeof(what), "AddRef %u, Release", a);
            break;
        }
        default:
            if constexpr (Writer) {
                result = o->SetStream(nullptr);
                std::snprintf(what, sizeof(what), "SetStream(null)");
            } else {
                // FreeInfo (0x87b9f5): a block from the codecs' allocator, or null.
                void *block = rng() % 4 ? ctx.Original<void *(__stdcall *)(size_t)>(kCodecAlloc)(0x40) : nullptr;
                result = static_cast<uint32_t>(o->FreeInfo(static_cast<gimex::Info *>(block)));
                std::snprintf(what, sizeof(what), "FreeInfo(%s)", block ? "block" : "null");
            }
            break;
        }
        Snapshot(what, result);
    }
    // Release to the end: FinalRelease closes an open stream and lets it go, then deletes the object.
    gWatched = object;
    gFreedSize = 0;
    for (int i = 0; i < 8 && gWatched; ++i) {
        const uint32_t left = o->Release();
        if (gWatched) {
            Snapshot("Release", left);
        } else {
            for (const Event &e : gEvents)
                Log("  codec %06x(%08x, %08x, %08x, %08x)", e.entry, Norm(e.words[0]), Norm(e.words[1]), Norm(e.words[2]),
                    Norm(e.words[3]));
            gEvents.clear();
            Log("Release -> %u: freed 0x%zx, lock %d", left, gFreedSize, LockCount());
        }
    }
    gObject = nullptr;
    for (int i = 0; i < kStreams; ++i) Log("s%d refs %u open %08x", i, gStreams[i].refs, gStreams[i].open);
    for (int i = 0; i < kRecords; ++i) Log("r%d refs %u face refs %u", i, gRecords[i].refs, gRecords[i].face.refs);
    // The writer's OpenByExtension can leave the lock taken (an ORIGINAL-BUG both sides share): the next script starts
    // from the same count.
    while (LockCount() > baseLock) GimexLock()->Unlock();
}

template <bool Writer> void Scripts(difftest::Context &ctx, uint32_t seed, int scripts, int steps)
{
    // OpenByExtension runs the codecs' real about entries; the IJL row's asks for ijl15.dll the first time.
    impfx::Imports imp(ctx);
    imp.Libraries();
    struct KeepCurrent { // both current formats as the test found them
        int32_t original = OriginalCurrentFormat();
        int32_t ours = gimex::CurrentFormat();
        ~KeepCurrent()
        {
            OriginalCurrentFormat() = original;
            gimex::SetFormat(ours);
        }
    } keep;
    std::mt19937 rng(seed);
    World world(rng);
    for (int i = 0; i < scripts; ++i) {
        if (i % 8 == 0 && i) {
            // a fresh set of stream configurations now and then
            for (int k = 0; k < kStreams + kRecords; ++k) {
                world.configs[k].open = ((rng() % 3) << 8) | (rng() % 4 ? 1u : 0u);
                world.configs[k].access = rng() % 4;
            }
        }
        const uint32_t scriptSeed = rng();
        std::vector<std::string> traces[2];
        for (int side = 0; side < 2; ++side) {
            world.Reset(scriptSeed);
            gSide = side;
            OriginalCurrentFormat() = 0;
            gimex::SetFormat(0);
            void *object;
            if (side == 0) {
                object = ctx.Original<void *(__cdecl *)()>(Writer ? kCreateWriter : kCreateReader)();
            } else {
                object = Writer ? static_cast<void *>(cGZGimexReaderAdapters::CreateWriter())
                                : static_cast<void *>(cGZGimexReaderAdapters::CreateReader());
            }
            RunScript<Writer>(ctx, object, scriptSeed, steps);
            traces[side] = gLog;
        }
        ++ctx.cases;
        if (traces[0] != traces[1]) {
            size_t k = 0;
            while (k < traces[0].size() && k < traces[1].size() && traces[0][k] == traces[1][k]) ++k;
            ctx.Fail("%s script %d line %zu:\n    original: %s\n    ours:     %s", Writer ? "writer" : "reader", i, k,
                     k < traces[0].size() ? traces[0][k].c_str() : "(end)",
                     k < traces[1].size() ? traces[1][k].c_str() : "(end)");
            if (ctx.failures.size() >= 10) return;
        }
    }
}

// The reader's slots through its vtable: QueryInterface 0x87baea, AddRef (folded 0x165e30), Release 0x87bb46,
// SetRecord 0x87bbd7 (shared with the writer), SetStream 0x87bb70, OpenByExtension 0x87b6ea, Open 0x87b552, GetInfo
// 0x87b9fa, Read 0x87ba2e, FreeInfo 0x87b9f5, Close 0x87b8f7, the destructor 0x87b4ee and its deleting slot 0x87b514,
// FinalRelease 0x87b997, OpenCurrent 0x87b890, GetStream 0x87ba76; the creator 0x87b428.
DIFF_TEST(GimexReader_Script, 0x87b552)
{
    Scripts<false>(ctx, 101, 160, 40);
}

// The writer's: QueryInterface 0x87bb22, AddRef (folded 0x165e30), Release 0x87bb5b, SetRecord 0x87bbd7, SetStream
// 0x87bc1f, OpenByExtension 0x87b83e, Open 0x87b73d, Write 0x87ba8c, Close 0x87b947, the destructor 0x87b501 and its
// deleting slot 0x87b533, FinalRelease 0x87b9c6, OpenCurrent 0x87b8b6, GetStream 0x87ba76; the creator 0x87b43e.
DIFF_TEST(GimexWriter_Script, 0x87b73d)
{
    Scripts<true>(ctx, 202, 160, 40);
}

// The constructors on filled memory, and the creators' allocation size.
DIFF_TEST(GimexReader_Construct, 0x87b4b4)
{
    using Ctor = void *(__thiscall *)(void *);
    for (int writer = 0; writer < 2; ++writer) {
        for (const uint8_t fill : {uint8_t(0x00), uint8_t(0xcd), uint8_t(0xff)}) {
            alignas(8) uint8_t a[0x24], b[0x24];
            std::memset(a, fill, sizeof(a));
            std::memset(b, fill, sizeof(b));
            void *ra = ctx.Original<Ctor>(writer ? kConstructWriter : kConstructReader)(a);
            void *rb = writer ? static_cast<void *>(cGZGimexReaderAdapters::ConstructWriter(b, nullptr))
                              : static_cast<void *>(cGZGimexReaderAdapters::ConstructReader(b, nullptr));
            ++ctx.cases;
            if (ra != a || rb != b) ctx.Fail("writer %d fill %02x: the constructors' results", writer, fill);
            if (Word(a, 0) != shape::P(GameAddress<void>(writer ? kWriterVTable : kReaderVTable)))
                ctx.Fail("writer %d: the original's vtable", writer);
            if (std::memcmp(a + 4, b + 4, sizeof(a) - 4) != 0) ctx.Fail("writer %d fill %02x: objects differ", writer, fill);
        }
    }
    // The creators (0x87b428, 0x87b43e, 0x87b4d1): the size they ask for and the object they return.
    static size_t requested;
    struct Sized {
        static void *__cdecl New(size_t n)
        {
            requested = n;
            return ::operator new(n);
        }
    };
    shape::CodePatch gameNew{kGameNew, reinterpret_cast<const void *>(&Sized::New)};
    for (int writer = 0; writer < 2; ++writer) {
        requested = 0;
        auto *a = static_cast<uint8_t *>(ctx.Original<void *(__cdecl *)()>(writer ? kCreateWriter : kCreateReader)());
        const size_t sizeA = requested;
        auto *b = writer ? reinterpret_cast<uint8_t *>(cGZGimexReaderAdapters::CreateWriter())
                         : reinterpret_cast<uint8_t *>(cGZGimexReaderAdapters::CreateReader());
        ++ctx.cases;
        if (sizeA != 0x1c) ctx.Fail("writer %d: the creator allocates 0x%zx", writer, sizeA);
        // +0xd..+0xf (the bytes after the open flag) are left as the heap had them (the constructor test above shows it).
        if (std::memcmp(a + 4, b + 4, 9) != 0 || std::memcmp(a + 0x10, b + 0x10, 0xc) != 0)
            ctx.Fail("writer %d: created objects differ", writer);
        ::operator delete(a);
        ::operator delete(b);
    }
}

} // namespace
