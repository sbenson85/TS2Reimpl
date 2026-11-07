// cTSAppCommonImpl in the standalone shape (lane EP): the option store. The game built these functions for speed.
#include "ts/cTSAppCommonImpl.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZFrameWork.h"
#include "gz/cIGZString.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/cRZFileName.h"

#include <algorithm>
#include <new>

extern "C" int __cdecl _purecall();

namespace ts {
namespace {

constexpr uint32_t kIidCommonImpl = 0xebabd35c;

// A cRZString on the stack, built and torn down by its own (hook-shaped) members.
struct LocalString {
    union {
        cRZString s;
    };
    LocalString() { s.Construct(); }
    explicit LocalString(const char *text) { s.Construct(text); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
    ~LocalString() { s.Destruct(); }
    cIGZString *Face() { return s.AsInterface(); }
};

// 0x1c2269 (__cdecl, not ours yet): whether `prefix` is a leading path of `path` (case-insensitive, '\\' and '/' alike,
// a whole component). Only al is defined.
bool IsPathPrefix(const char *prefix, const char *path)
{
    return OriginalFunction<bool(__cdecl *)(const char *, const char *), 0x1c2269>()(prefix, path);
}

} // namespace

// ---- OptionEntry ----

OptionEntry::OptionEntry(const cRZString &text, uint32_t mask) : mMask(mask)
{
    mText.Construct(text, 0, std::string::npos);
}

OptionEntry::OptionEntry(const OptionEntry &other) : mMask(other.mMask)
{
    mText.Construct(other.mText, 0, std::string::npos);
}

OptionEntry::~OptionEntry()
{
    mText.Destruct();
}

// std::find calls `element == value`; the original's test (0xca4ac0) asks the searched-for entry (`other`) whether it
// equals the element's text, after comparing the masks.
bool OptionEntry::operator==(const OptionEntry &other) const
{
    return other.mMask == mMask && other.mText.AsInterface()->IsEqual(*mText.AsInterface(), true);
}

// ---- 0xca4cb0, 0xca4de0 ----

cTSAppCommonImpl::cTSAppCommonImpl() : mFlags(0) {}

cTSAppCommonImpl::~cTSAppCommonImpl()
{
    // Emptied first, the overrides before the defaults; the members' destructors then free the (empty) trees' heads.
    mOverrides.clear();
    mDefaults.clear();
}

// ---- 0xca6ff0; the three slots the game's own vtable leaves pure ----

bool cTSAppCommonImpl::QueryInterface(uint32_t riid, void **out)
{
    if (out && riid == kIidCommonImpl) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

uint32_t cTSAppCommonImpl::AddRef() { return static_cast<uint32_t>(_purecall()); }
uint32_t cTSAppCommonImpl::Release() { return static_cast<uint32_t>(_purecall()); }
bool cTSAppCommonImpl::InitOptions() { return _purecall() != 0; }

// ---- the store ----

OptionValue *cTSAppCommonImpl::Find(uint32_t id, uint32_t defaultsOnly)
{
    if (!static_cast<uint8_t>(defaultsOnly)) {
        auto it = mOverrides.find(id);
        if (it != mOverrides.end()) return &it->second;
    }
    auto it = mDefaults.find(id);
    return it != mDefaults.end() ? &it->second : nullptr;
}

// 0xca7230
bool cTSAppCommonImpl::AddOption(uint32_t key, uint32_t alias, const char *text, uint32_t toOverrides, int32_t mask)
{
    OptionMap &map = static_cast<uint8_t>(toOverrides) ? mOverrides : mDefaults;
    if (!text) {
        map.erase(key);
        return false;
    }
    OptionEntry entry = [&] {
        LocalString temporary(text);
        return OptionEntry(temporary.s, static_cast<uint32_t>(mask));
    }();
    auto it = map.find(key);
    if (it != map.end()) {
        std::list<OptionEntry> &entries = it->second.mEntries;
        if (std::find(entries.begin(), entries.end(), entry) == entries.end()) entries.push_front(entry);
        return false;
    }
    std::list<OptionEntry> entries;
    entries.push_front(entry);
    OptionValue value{alias, entries};
    map[key] = std::move(value);
    return false;
}

// 0xca5e40
bool cTSAppCommonImpl::GetOptionText(uint32_t id, cIGZString *out, uint32_t a, uint32_t defaultsOnly, int32_t mask)
{
    if (!out) return false;
    out->Erase(0, 0xffffffffu);
    return ReadOptionText(id, out, a, defaultsOnly, mask);
}

// 0xca5e80
bool cTSAppCommonImpl::GetOptionList(uint32_t id, void *list, uint32_t a, uint32_t defaultsOnly, int32_t mask)
{
    if (!list) return false;
    static_cast<OptionStringList *>(list)->clear();
    return ReadOptionList(id, list, a, defaultsOnly, mask);
}

// 0xca6bd0
bool cTSAppCommonImpl::HasOption(uint32_t key)
{
    return mOverrides.find(key) != mOverrides.end();
}

// 0xca69f0
bool cTSAppCommonImpl::IsNameUnused(cIGZString *name)
{
    uint32_t length = name->Strlen();
    const char *data = name->Data();
    LocalString path;
    path.s.mString.assign(data, length);
    cRZFileName::NormaliseSeparators(&path.s, '/');
    for (auto it = mOverrides.begin(); it != mOverrides.end(); ++it) {
        LocalString text;
        if (GetOptionText(it->first, text.Face(), 1, 0, -1) && IsPathPrefix(path.s.ToChar(), text.s.ToChar())) return false;
    }
    return true;
}

// 0xca6920
bool cTSAppCommonImpl::IsOptionNameUnused(uint32_t id)
{
    LocalString text;
    if (GetOptionText(id, text.Face(), 1, 1, -1)) return IsNameUnused(text.Face());
    return true;
}

void cTSAppCommonImpl::SetAppName(const char *) {}

// 0xca5ed0
bool cTSAppCommonImpl::ReadOptionText(uint32_t id, cIGZString *out, uint32_t a, uint32_t defaultsOnly, int32_t mask)
{
    OptionValue *value = Find(id, defaultsOnly);
    if (!value) return false;
    const uint32_t want = static_cast<uint32_t>(mask);
    if (value->mAlias != 0) {
        bool found = false;
        std::list<OptionEntry> entries(value->mEntries);
        for (auto it = entries.begin(); it != entries.end() && !found; ++it) {
            OptionEntry entry(*it);
            if (want & entry.mMask) found = ReadOptionText(value->mAlias, out, a, defaultsOnly, int32_t(entry.mMask & want));
        }
        if (!found) return false;
    }
    std::list<OptionEntry> entries(value->mEntries);
    for (OptionEntry entry : entries) {
        if (want & entry.mMask) {
            out->Append(*entry.mText.AsInterface());
            return true;
        }
    }
    return false;
}

// 0xca6200
bool cTSAppCommonImpl::ReadOptionList(uint32_t id, void *list, uint32_t a, uint32_t defaultsOnly, int32_t mask)
{
    OptionValue *value = Find(id, defaultsOnly);
    if (!value) return false;
    auto *out = static_cast<OptionStringList *>(list);
    const uint32_t alias = value->mAlias;
    const uint32_t want = static_cast<uint32_t>(mask);
    bool found = false;
    std::list<OptionEntry> entries(value->mEntries);
    for (OptionEntry entry : entries) {
        const uint32_t met = entry.mMask & want;
        if (!met) continue;
        if (alias == 0) {
            out->push_back(OptionString(entry.mText.ToChar()));
            found = true;
            continue;
        }
        OptionStringList prefixes;
        if (ReadOptionList(alias, &prefixes, a, defaultsOnly, int32_t(met))) {
            for (const OptionString &prefix : prefixes) {
                OptionString text(prefix);
                text += OptionString(entry.mText.ToChar());
                out->push_back(text);
                found = true;
            }
        }
    }
    return found;
}

// ---- 0xca5d20 ----

// The registry service (0x25076b9e, interface 0xa237613c) gets two handlers: OnResourceFactoryEntry (0xca5db0) for the
// table named by the game word 0x1085efc ("CN_PRF") and OnPathRegistryEntry (0xca5170) for 0x1085ef8 ("CN_PDBS"), the
// second only when the first registration succeeds.
void cTSAppCommonImpl::RegisterRegistryHandlers()
{
    constexpr uint32_t kWordA = 0x1085efc;
    constexpr uint32_t kWordB = 0x1085ef8;
    void *service = nullptr;
    auto *framework = reinterpret_cast<cIGZFrameWork *>(RZGetGlobal10C9378());
    if (framework) framework->GetSystemService(0x25076b9e, 0xa237613c, &service);
    using Register = uint32_t(__thiscall *)(void *, uint32_t, void *, void *);
    void *const factories = reinterpret_cast<void *>(&cTSAppCommonImplAdapters::OnResourceFactoryEntry);
    void *const segments = reinterpret_cast<void *>(&cTSAppCommonImplAdapters::OnPathRegistryEntry);
    // No null test on the service: the original faults when it is missing.
    if (VirtualCall<Register, 7>(service, *GameAddress<uint32_t>(kWordA), factories, this) & 0xff) {
        VirtualCall<Register, 7>(service, *GameAddress<uint32_t>(kWordB), segments, this);
    }
    // The holder's destructor (0x99e700): Release through slot 2, no handler.
    if (service) VirtualCall<uint32_t(__thiscall *)(void *), 2>(service);
}

// ---- adapters ----

cTSAppCommonImpl *__fastcall cTSAppCommonImplAdapters::Construct(void *memory, void *) { return ::new (memory) cTSAppCommonImpl; }

void __fastcall cTSAppCommonImplAdapters::Destruct(cTSAppCommonImpl *self, void *) { self->cTSAppCommonImpl::~cTSAppCommonImpl(); }

cTSAppCommonImpl *__fastcall cTSAppCommonImplAdapters::DeletingDestructor(cTSAppCommonImpl *self, void *, uint32_t flags)
{
    self->cTSAppCommonImpl::~cTSAppCommonImpl();
    if (flags & 1) ::operator delete(self, sizeof(cTSAppCommonImpl));
    return self;
}

[[msvc::forceinline_calls]] bool __fastcall cTSAppCommonImplAdapters::QueryInterface(cTSAppCommonImpl *self, void *, uint32_t riid, void **out)
{
    return self->cTSAppCommonImpl::QueryInterface(riid, out);
}

[[msvc::forceinline_calls]] bool __fastcall cTSAppCommonImplAdapters::AddOption(cTSAppCommonImpl *self, void *, uint32_t key, uint32_t alias,
                                                                                const char *text, uint32_t toOverrides, int32_t mask)
{
    return self->cTSAppCommonImpl::AddOption(key, alias, text, toOverrides, mask);
}

[[msvc::forceinline_calls]] bool __fastcall cTSAppCommonImplAdapters::GetOptionText(cTSAppCommonImpl *self, void *, uint32_t id, cIGZString *out,
                                                                                    uint32_t a, uint32_t defaultsOnly, int32_t mask)
{
    return self->cTSAppCommonImpl::GetOptionText(id, out, a, defaultsOnly, mask);
}

[[msvc::forceinline_calls]] bool __fastcall cTSAppCommonImplAdapters::GetOptionList(cTSAppCommonImpl *self, void *, uint32_t id, void *list,
                                                                                    uint32_t a, uint32_t defaultsOnly, int32_t mask)
{
    return self->cTSAppCommonImpl::GetOptionList(id, list, a, defaultsOnly, mask);
}

[[msvc::forceinline_calls]] bool __fastcall cTSAppCommonImplAdapters::HasOption(cTSAppCommonImpl *self, void *, uint32_t key)
{
    return self->cTSAppCommonImpl::HasOption(key);
}

[[msvc::forceinline_calls]] bool __fastcall cTSAppCommonImplAdapters::IsNameUnused(cTSAppCommonImpl *self, void *, cIGZString *name)
{
    return self->cTSAppCommonImpl::IsNameUnused(name);
}

[[msvc::forceinline_calls]] bool __fastcall cTSAppCommonImplAdapters::IsOptionNameUnused(cTSAppCommonImpl *self, void *, uint32_t id)
{
    return self->cTSAppCommonImpl::IsOptionNameUnused(id);
}

[[msvc::forceinline_calls]] bool __fastcall cTSAppCommonImplAdapters::ReadOptionText(cTSAppCommonImpl *self, void *, uint32_t id, cIGZString *out,
                                                                                     uint32_t a, uint32_t defaultsOnly, int32_t mask)
{
    return self->cTSAppCommonImpl::ReadOptionText(id, out, a, defaultsOnly, mask);
}

[[msvc::forceinline_calls]] bool __fastcall cTSAppCommonImplAdapters::ReadOptionList(cTSAppCommonImpl *self, void *, uint32_t id, void *list,
                                                                                     uint32_t a, uint32_t defaultsOnly, int32_t mask)
{
    return self->cTSAppCommonImpl::ReadOptionList(id, list, a, defaultsOnly, mask);
}

[[msvc::forceinline_calls]] void __fastcall cTSAppCommonImplAdapters::RegisterRegistryHandlers(cTSAppCommonImpl *self, void *)
{
    self->cTSAppCommonImpl::RegisterRegistryHandlers();
}

} // namespace ts
