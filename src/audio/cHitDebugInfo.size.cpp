// cHitDebugInfo (lane LH): see cHitDebugInfo.h. The game built this module for size (/O1).
#include "audio/cHitDebugInfo.h"

#include "game/vtable.h"
#include "gz/cIGZFrameWork.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject, ScopedRef
#include "rz/cRZFileName.h"
#include "rz/cRZString.h"

#include <new>
#include <string>

FILE *(__cdecl *cHitDebugInfo::sOpen)(const char *path, const char *mode) = &fopen;
char *(__cdecl *cHitDebugInfo::sGets)(char *buffer, int size, FILE *file) = &fgets;
int(__cdecl *cHitDebugInfo::sScan)(const char *text, const char *format, ...) = &sscanf;
int(__cdecl *cHitDebugInfo::sClose)(FILE *file) = &fclose;

namespace {

// A cRZString local built from an interface (0x144f2a) and destroyed at the end of its scope.
struct LocalString {
    union {
        cRZString s;
    };
    explicit LocalString(const cIGZString &text) { s.Construct(text); }
    ~LocalString() { s.Destruct(); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
};

// A cRZFileName local (0x1c7ef4 / 0x1c0eb2).
struct LocalFileName {
    union {
        cRZFileName f;
    };
    explicit LocalFileName(const cRZString &path) { f.Construct(&path); }
    ~LocalFileName() { f.Destruct(); }
    LocalFileName(const LocalFileName &) = delete;
    LocalFileName &operator=(const LocalFileName &) = delete;
};

// The file's drive and directory (GetDrivePath 0x1c87c7, into a temporary string).
std::string DrivePath(cRZFileName &file)
{
    union Raw {
        cRZString s;
        Raw() {}
        ~Raw() { s.Destruct(); }
    } drive;
    return file.GetDrivePath(&drive.s)->mString;
}

// The hit manager service (0x22b0b2: null unless the framework hands it out).
void *HitMan()
{
    void *out = nullptr;
    if (auto *framework = static_cast<cIGZFrameWork *>(GetServiceObject()))
        framework->GetSystemService(cHitDebugInfo::kHitManId, cHitDebugInfo::kHitManIid, &out);
    return out;
}

// The hit manager's slot 14: the object and offset of `key`.
bool HitManLookup(void *hitMan, uint32_t key, void **object, int32_t *offset)
{
    return VirtualCall<bool(__thiscall *)(void *, uint32_t, void **, int32_t *), 14>(hitMan, key, object, offset);
}

} // namespace

// ---- HitDebugModule ----

// 0xb31577
HitDebugModule::HitDebugModule(const cIGZString &name, uint32_t base, uint32_t size)
{
    *mName.Interface() = name;
    mBase = base;
    mSize = size;
}

// 0xb316f2
uint32_t HitDebugModule::LineAddress(int32_t line) const
{
    const int32_t offset = LineOffset(line);
    return offset ? mBase + static_cast<uint32_t>(offset) : 0;
}

// 0xb31bc4. ORIGINAL-BUG: with no lines and `line` -1 the original's loop test matches its initial -1 and it returns the
// list head node's (uninitialised) value; ours returns 0.
int32_t HitDebugModule::LineOffset(int32_t line) const
{
    for (const HitDebugLine &pair : mLines)
        if (pair.line == line) return pair.offset;
    return 0;
}

// 0xb3186b
int32_t HitDebugModule::LineAt(uint32_t address) const
{
    if (address < mBase || address >= mBase + mSize) return 0;
    return LineAtOffset(static_cast<int32_t>(address - mBase));
}

// 0xb3188e
int32_t HitDebugModule::LineAtOffset(int32_t offset) const
{
    bool below = false, above = false;
    for (const HitDebugLine &pair : mLines) {
        below |= pair.offset <= offset;
        above |= pair.offset >= offset;
        if (below && above) return pair.line;
    }
    return 0;
}

// ---- cHitDebugInfo ----

// 0xb31526: the base part (id 0xfbc9cd04, priority 0), count 0, no modules, no current one.
cHitDebugInfo::cHitDebugInfo()
    : rz::cRZSystemService(kServiceId, kPriority), mCount(0), mCurrent(nullptr), mWord50(0), mWord54(0)
{
}

// 0xb315a6: every module deleted, then the string at +0x30, the list and the base part.
cHitDebugInfo::~cHitDebugInfo() noexcept
{
    for (HitDebugModule *module : mModules) delete module;
}

// 0xb2a30c
cIGZSystemService *__cdecl cHitDebugInfo::Create() { return new (std::nothrow) cHitDebugInfo; }

// 0xb31bed: false for a null `out`; 1 and cIHitDebugInfo -> +0, cIGZSystemService -> +4 (each counted through slot 1),
// else the base service's.
bool cHitDebugInfo::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid == 1 || riid == kIid) {
        *out = static_cast<cIHitDebugInfo *>(this);
    } else if (riid == cIGZSystemService::kIID) {
        *out = static_cast<cIGZSystemService *>(this);
    } else {
        return rz::cRZSystemService::QueryInterface(riid, out);
    }
    AddRef();
    return true;
}

// 0x85c074
uint32_t cHitDebugInfo::AddRef() { return ++mCount; }

// 0xb31c4e: at a count of 1 the object is deleted (the count left as it is) and 0 returned; else the count less one.
uint32_t cHitDebugInfo::Release()
{
    if (mCount == 1) {
        delete this;
        return 0;
    }
    return --mCount;
}

// 0xb31865
int32_t cHitDebugInfo::GetServicePriority() { return kServicePriority; }

// 0x144910
bool cHitDebugInfo::Shutdown() { return true; }

// 0xb31681. ORIGINAL-BUG: a failed new puts first (and makes current) a null module, which the other slots use untested. The
// name is also copied into a local string that is dropped.
void cHitDebugInfo::AddModule(const cIGZString &name, uint32_t base, uint32_t size)
{
    HitDebugModule *module = new (std::nothrow) HitDebugModule(name, base, size);
    mModules.push_front(module);
    ContentString unused;
    unused.s.Copy(name);
    mCurrent = module;
}

// 0xb3176a. ORIGINAL-BUG: each module's name is compared with itself (its slot 12 given its own string), not with `file`,
// so the first module with the line answers. When none does, `file` is copied into a local string that is dropped.
bool cHitDebugInfo::FindLineAddress(const cIGZString &file, int32_t line, uint32_t *address)
{
    for (HitDebugModule *module : mModules) {
        cIGZString *name = module->mName.Interface();
        if (!name->IsEqual(name, true)) continue;
        if (const uint32_t found = module->LineAddress(line)) {
            *address = found;
            return true;
        }
    }
    ContentString unused;
    unused.s.Copy(file);
    return false;
}

// 0xb317d5: the name copied through `file`'s slot 17.
bool cHitDebugInfo::FindLine(uint32_t address, cIGZString *file, int32_t *line)
{
    for (HitDebugModule *module : mModules) {
        if (const int32_t found = module->LineAt(address)) {
            file->Copy(*module->mName.Interface());
            *line = found;
            return true;
        }
    }
    return false;
}

// 0xb31711. ORIGINAL-BUG: the hit manager is used without a null test.
bool cHitDebugInfo::HitManAddress(uint32_t key, uint32_t *address)
{
    ScopedRef hitMan{HitMan()};
    bool ok = false;
    void *object = nullptr;
    int32_t offset = 0;
    if (HitManLookup(hitMan.mObject, key, &object, &offset) && object) {
        if (const uint32_t base = RawSlot<3>(object)) {
            *address = base + static_cast<uint32_t>(offset);
            ok = true;
        }
    }
    hitMan.ReleaseObject();
    return ok;
}

// 0xb31817. ORIGINAL-BUG: the hit manager is used without a null test.
bool cHitDebugInfo::HitManSet(uint32_t key, uint32_t value)
{
    ScopedRef hitMan{HitMan()};
    bool ok = false;
    void *object = nullptr;
    int32_t offset = 0;
    if (HitManLookup(hitMan.mObject, key, &object, &offset) && object) {
        RawSlot<6, uint32_t>(object, value);
        ok = true;
    }
    hitMan.ReleaseObject();
    return ok;
}

// 0xb318de. The line buffer, the scanned name and the two numbers live across the lines: a line that does not scan keeps
// the previous values (ORIGINAL-BUG: before the first scan the original's are uninitialised stack; ours are zero). A pair
// before any SOURCEFILE line goes to the current module from earlier calls (none: AddLine's null use).
bool cHitDebugInfo::LoadLineInfo(const cIGZString &path, uint32_t base, uint32_t size)
{
    LocalFileName file{LocalString(path).s};
    const std::string hdb = DrivePath(file.f) + file.f.GetName()->mString + ".HDB";
    FILE *stream = sOpen(hdb.c_str(), "rt");
    if (!stream) return false;
    char text[0x400];
    char source[0x400] = {};
    int32_t line = 0, offset = 0;
    while (sGets(text, 0x400, stream)) {
        if (text[0] == 'S') {
            sScan(text, "SOURCEFILE %s  {line,offset}", source);
            ContentString name(source);
            name.s.mString = DrivePath(file.f) + name.s.mString;
            AddModule(*name.Interface(), base, size);
        } else if (text[0] == '{') {
            sScan(text, "{%d,%d}", &line, &offset);
            AddLine(line, offset);
        }
    }
    sClose(stream);
    return true;
}

// 0xb31b28. ORIGINAL-BUG: the file is never closed.
bool cHitDebugInfo::ReadSymbolFile(const cIGZString &path)
{
    LocalString name{path};
    FILE *stream = sOpen(name.s.mString.c_str(), "rt");
    if (!stream) return false;
    char text[0x400];
    char symbol[0x400];
    long value = 0;
    while (sGets(text, 0x400, stream)) sScan(text, "%s %ld (", symbol, &value);
    return true;
}

// 0xb316ea / 0xb3165e. ORIGINAL-BUG: no current module (none added, or a failed new) is used untested.
void cHitDebugInfo::AddLine(int32_t line, int32_t offset) { mCurrent->mLines.push_back({line, offset}); }
