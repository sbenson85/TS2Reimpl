// nGZCommandParser::cFileParser (lane IM): the command files a parser runs, and the watchers of their directories.
// src/gz/cFileParser.h has the layout.
#include "gz/cFileParser.h"

#include "app/IniFile.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/CommandParser.h"
#include "gz/cIGZCOM.h"
#include "rz/RZMemory.h"
#include "rz/RZMessageServer.h"
#include "rz/StaticGetters.h"
#include "rz/StopwatchMore.h"
#include "rz/cRZFileName.h"
#include "rz/cRZMessage2Standard.h"

#include <cctype>
#include <new>
#include <string>

namespace nGZCommandParser {

namespace {

// The parser's slots the file parser calls (its cIBaseParser face, 0xe7db4c).
constexpr int kParserInitialise = 0; // (caseSensitive)
constexpr int kParserShutdown = 1;
constexpr int kParserRestart = 8;
constexpr int kParserFinished = 11;  // after every file ran without a required one failing
constexpr int kParserRunFile = 12;   // (path, flags)
constexpr int kParserGetSource = 50;

// The source's slot 7: `out` = the source's name for `path`.
constexpr int kSourceCanonicalPath = 7;

// The message's slots (cRZMessage2Standard, the game's vtable 0xda4064).
constexpr int kMessageGetId = 4;
constexpr int kMessageSetId = 5;
constexpr int kMessageSetPtr24 = 33; // a counted object pointer
// The message server's slot 3: posts a message.
constexpr int kServerPost = 3;

template <class R = uint32_t, class... A> R Call(const void *object, int slot, A... args)
{
    using Fn = R(__thiscall *)(const void *, A...);
    return VirtualSlot<Fn>(object, slot)(object, args...);
}

// The parser's cIGZUnknown: the face's vbptr (+4) and that vbtable's second entry, from the vbptr.
cIGZUnknown *UnknownOf(cIBaseParser *parser)
{
    char *vbptr = reinterpret_cast<char *>(parser) + 4;
    const int32_t offset = (*reinterpret_cast<int32_t *const *>(vbptr))[1];
    return reinterpret_cast<cIGZUnknown *>(vbptr + offset);
}

// A cRZString local built and destroyed by the game's bodies (Construct / Destruct).
struct LocalString {
    union {
        cRZString value;
    };
    explicit LocalString(const char *text) { value.Construct(text); }
    ~LocalString() { value.Destruct(); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
};

// A cRZFileName local from a path (0x1c7ef4), destroyed by 0x1c0eb2.
struct LocalFileName {
    union {
        cRZFileName value;
    };
    explicit LocalFileName(const cRZString *path) { value.Construct(path); }
    ~LocalFileName() { value.Destruct(); }
    LocalFileName(const LocalFileName &) = delete;
    LocalFileName &operator=(const LocalFileName &) = delete;
};

} // namespace

// ---- the members' helpers ----

FileString::FileString()
{
    value.mVTable = const_cast<void *>(cRZString::VTable());
    ::new (&value.mString) std::string();
    value.mRefCount = 0;
}

FileString::FileString(const std::string &text)
{
    ::new (&value.mString) std::string(text);
    value.mVTable = const_cast<void *>(cRZString::VTable());
    value.mRefCount = 0;
}

FileString::FileString(const FileString &other)
{
    ::new (&value.mString) std::string(other.value.mString);
    value.mVTable = const_cast<void *>(cRZString::VTable());
    value.mRefCount = 0;
}

FileString::~FileString()
{
    value.mString.~basic_string();
}

ParserRef::~ParserRef()
{
    if (p) UnknownOf(p)->Release();
}

WatcherRef::~WatcherRef()
{
    if (p) p->Release();
}

void WatcherRef::Assign(cIGZPersistDirectoryWatcher *other)
{
    cIGZPersistDirectoryWatcher *old = p;
    if (other == old) return;
    if (other) other->AddRef();
    p = other;
    if (old) old->Release();
}

// ---- the class ----

rz::cRZUnknown *__cdecl cFileParser::Create()
{
    return new cFileParser;
}

// What Shutdown does when still initialised, then the members: the stopwatch, the watchers (each released), the file list,
// the parser (released), and cRZUnknown.
cFileParser::~cFileParser()
{
    if (mInitialised) cFileParser::Shutdown();
}

// 0x1cf94583: this (the first face), counted through slot 1; anything else as cRZUnknown answers.
bool cFileParser::QueryInterface(uint32_t riid, void **out)
{
    if (riid == kInterfaceId) {
        *out = static_cast<cIFileParser *>(this);
        AddRef();
        return true;
    }
    return rz::cRZUnknown::QueryInterface(riid, out);
}

uint32_t cFileParser::AddRef()
{
    return rz::cRZUnknown::AddRef();
}

uint32_t cFileParser::Release()
{
    return rz::cRZUnknown::Release();
}

bool cFileParser::Init()
{
    mInitialised = true;
    return true;
}

// Once initialised: the parser shut down (its slot 1) and let go, every watcher shut down (its slot 5) and the watchers
// dropped (released). False when not initialised.
bool cFileParser::Shutdown()
{
    if (!mInitialised) return false;
    mInitialised = false;
    if (mParser.p) {
        Call(mParser.p, kParserShutdown);
        if (cIBaseParser *parser = mParser.p) {
            mParser.p = nullptr;
            UnknownOf(parser)->Release();
        }
    }
    for (auto &watcher : mWatchers) watcher.second.p->Shutdown();
    mWatchers.clear();
    return true;
}

// The current parser is shut down first (even when it is the one given), the new one held (counted), then initialised with
// `caseSensitive` (its slot 0).
void cFileParser::SetParser(cIBaseParser *parser, uint32_t caseSensitive)
{
    if (mParser.p) Call(mParser.p, kParserShutdown);
    cIBaseParser *old = mParser.p;
    if (parser != old) {
        if (parser) UnknownOf(parser)->AddRef();
        mParser.p = parser;
        if (old) UnknownOf(old)->Release();
    }
    if (mParser.p) Call(mParser.p, kParserInitialise, caseSensitive);
}

// The path is made absolute (inifile::AbsolutePathString) unless letters and ":/" start it (a URL-like "scheme:/"); the
// entry gets that path, the source's name for it (the parser's slot 50, then the source's slot 7) and flags bit 0. With
// flags bit 1 and a non-empty name, the name's drive and directory get a watcher unless they have one: the framework's
// class object (0xe953da68, 0xe953da64) initialised, pointed at this (the message target face), given the directory (filter
// 0x18, subdirectories) and started, then kept. The list is dirty after.
void cFileParser::AddFile(const char *path, uint32_t flags)
{
    LocalString full(path);
    const char *at = path;
    while (isalpha(*at)) ++at;
    if (!(at[0] == ':' && at[1] == '/')) inifile::AbsolutePathString(&full.value, &full.value);

    FileEntry entry{FileString(full.value.mString), FileString(), static_cast<bool>(flags & 1)};
    void *source = Call<void *>(mParser.p, kParserGetSource);
    Call(source, kSourceCanonicalPath, full.value.mString.c_str(), entry.resolved.Face());
    mFiles.push_back(entry);

    if ((flags & 2) && !entry.resolved.value.mString.empty()) {
        LocalFileName name(entry.resolved.RZ());
        cRZString *directory = SplitDirectory(&name.value);
        cRZString *drive = SplitDrive(&name.value);
        FileString key(drive->mString + directory->mString);
        if (mWatchers.find(key.value.mString) == mWatchers.end()) {
            WatcherRef watcher;
            auto *com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
            com->GetClassObject(cIGZPersistDirectoryWatcher::kClassId, cIGZPersistDirectoryWatcher::kIID,
                                reinterpret_cast<void **>(&watcher.p));
            if (watcher.p) {
                watcher.p->Init();
                watcher.p->SetTarget(static_cast<cIGZMessageTarget2 *>(this));
                watcher.p->SetDirectory(key.Face(), 0x18, true);
                watcher.p->Start();
                mWatchers.try_emplace(key.value.mString).first->second.Assign(watcher.p);
            }
        }
    }
    mDirty = true;
}

// With a parser: restarted (its slot 8), each file run in turn (slot 12 with the path and 0) under the stopwatch; a
// cIGZExceptionBase from a required file marks the run failed (others are ignored; any other exception goes on up). When
// none failed the parser's slot 11; then the message server, when there is one, gets a "files ran" message carrying the
// parser's cIGZUnknown. The list is clean after. True when no required file failed.
bool cFileParser::RunFiles()
{
    bool failed = false;
    if (mParser.p) {
        Call(mParser.p, kParserRestart);
        for (uint32_t i = 0; i < mFiles.size(); ++i) {
            mStopwatch.value.Start();
            FileEntry &entry = mFiles[i];
            try {
                Call(mParser.p, kParserRunFile, entry.path.value.mString.c_str(), 0u);
            } catch (const cIGZExceptionBase &) {
                if (entry.required) failed = true;
            }
            float seconds;
            stopwatch_more::ElapsedAsFloat(&mStopwatch.value, nullptr, &seconds);
            ResetStopwatch(&mStopwatch.value);
        }
        if (!failed) Call(mParser.p, kParserFinished);
        if (void *server = GetCachedMessageServer()) {
            // A cRZMessage2Standard built as 0x1c033d builds one, in RZMemory (0x1c03b7).
            auto *message = static_cast<cRZMessage2Standard *>(RZMemory::Malloc(cRZMessage2Standard::kSize));
            if (message) {
                message->mVTable = &ts2_rva_da4064;
                message->mRefCount = 0;
                message->mFlags = 0;
                message->mId = 0;
                for (uint32_t &value : message->mValue) value = 0;
                message->mPtr20 = message->mPtr24 = message->mPtr28 = nullptr;
                Call(message, 1);
            }
            Call(message, kMessageSetId, kRanMessage);
            Call(message, kMessageSetPtr24, mParser.p ? UnknownOf(mParser.p) : nullptr);
            Call(server, kServerPost, message);
            if (message) Call(message, 2);
        }
    }
    mDirty = false;
    return !failed;
}

void cFileParser::SetDirty(bool dirty)
{
    mDirty = dirty;
}

cIBaseParser *cFileParser::GetParser()
{
    return mParser.p;
}

bool cFileParser::IsDirty()
{
    return mDirty;
}

// A watcher's "changed" message marks the list dirty.
bool cFileParser::DoMessage(void *message)
{
    if (Call(message, kMessageGetId) != kChangedMessage) return false;
    mDirty = true;
    return true;
}

// ---- helpers ----

cRZString *SplitDirectory(cRZFileName *name)
{
    if (name->mDir.mString.empty() && !name->mFull.mString.empty())
        cRZFileName::SplitInto(&name->mFull, nullptr, &name->mDir, nullptr, nullptr);
    cRZFileName::NormaliseDirectory(&name->mDir);
    return &name->mDir;
}

cRZString *SplitDrive(cRZFileName *name)
{
    if (name->mDrive.mString.empty() && !name->mFull.mString.empty())
        cRZFileName::SplitInto(&name->mFull, &name->mDrive, nullptr, nullptr, nullptr);
    cRZFileName::NormaliseDrive(&name->mDrive);
    return &name->mDrive;
}

void ResetStopwatch(cRZStopwatch *stopwatch)
{
    stopwatch->mStart = 0;
    stopwatch->mAccumulated = 0;
}

// ---- adapters ----

// The deleting destructor's two steps by hand (no name to call it by without dispatch).
cFileParser *__fastcall cFileParserAdapters::DeletingDestructor(cFileParser *self, void *, uint32_t flags)
{
    self->cFileParser::~cFileParser();
    if (flags & 1) ::operator delete(self, sizeof(cFileParser));
    return self;
}
void __fastcall cFileParserAdapters::Destructor(cFileParser *self, void *)
{
    self->cFileParser::~cFileParser();
}
bool __fastcall cFileParserAdapters::QueryInterface(cFileParser *self, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return self->cFileParser::QueryInterface(riid, out);
}
bool __fastcall cFileParserAdapters::Init(cFileParser *self, void *)
{
    [[msvc::forceinline_calls]] return self->cFileParser::Init();
}
bool __fastcall cFileParserAdapters::Shutdown(cFileParser *self, void *)
{
    [[msvc::forceinline_calls]] return self->cFileParser::Shutdown();
}
void __fastcall cFileParserAdapters::SetParser(cFileParser *self, void *, cIBaseParser *parser, uint32_t caseSensitive)
{
    [[msvc::forceinline_calls]] self->cFileParser::SetParser(parser, caseSensitive);
}
void __fastcall cFileParserAdapters::AddFile(cFileParser *self, void *, const char *path, uint32_t flags)
{
    [[msvc::forceinline_calls]] self->cFileParser::AddFile(path, flags);
}
bool __fastcall cFileParserAdapters::RunFiles(cFileParser *self, void *)
{
    [[msvc::forceinline_calls]] return self->cFileParser::RunFiles();
}
bool __fastcall cFileParserAdapters::DoMessage(cIGZMessageTarget2 *face, void *, void *message)
{
    [[msvc::forceinline_calls]] return static_cast<cFileParser *>(face)->cFileParser::DoMessage(message);
}
cRZString *__fastcall cFileParserAdapters::SplitDirectory(cRZFileName *name, void *)
{
    return nGZCommandParser::SplitDirectory(name);
}
cRZString *__fastcall cFileParserAdapters::SplitDrive(cRZFileName *name, void *)
{
    return nGZCommandParser::SplitDrive(name);
}
void __fastcall cFileParserAdapters::ResetStopwatch(cRZStopwatch *stopwatch, void *)
{
    nGZCommandParser::ResetStopwatch(stopwatch);
}

} // namespace nGZCommandParser
