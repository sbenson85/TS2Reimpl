// cTSIniFile (vtable 0xeb7220): constructor 0xcb2a20, destructor body 0xcb2cc0, deleting destructor 0xcb2ce0,
// GetString 0xcb2e90, the section walk 0xcb2d00, HasSection with prefixes 0xcb30f0, the base HasSection
// 0x9c6a0e, and the token reader 0x1ad676. See cTSIniFile.h.
#include "rz/cTSIniFile.h"

#include "app/AppInit.h"
#include "game/game.h"
#include "game/vtable.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StringKeyParser.h"

#include <cstring>
#include <map>
#include <new>

namespace {

// The section table of cRZIniFile (+0x28): see cRZIniFileRead.cpp. Only lookups are needed here.
struct IniKey {
    cRZString key;
    ~IniKey() { key.Destruct(); }
    bool operator<(const IniKey &other) const { return key.mString < other.key.mString; }
};
static_assert(sizeof(IniKey) == 0x20, "IniKey is a cRZString");
using SectionTable = std::map<IniKey, uint32_t>;

using BoolThiscall0 = bool(__thiscall *)(void *);
using LoadFn = bool(__thiscall *)(void *, int32_t);
using FindFn = int32_t(__thiscall *)(void *, const char *, uint32_t, bool);
using SwitchFn = bool(__thiscall *)(void *, const void *, void *, bool);

constexpr int kFrameworkSlotCommandLine = 21;
constexpr int kCommandLineSlotIsSwitchPresent = 8;
constexpr char kContextSwitch[] = "iniContext";
constexpr char kGameDataDirToken[] = "%GameDataDir%"; // 0xda6370
constexpr uint32_t kGameDataDirTokenLength = 13;

const cRZString &AsRZ(const cIGZString &s)
{
    return *reinterpret_cast<const cRZString *>(&s);
}

// A cRZString holding "<prefix>-<section>", the name the prefixed lookups use (the original builds it from the
// prefix's string with _Construct and two appends; the text is all that matters).
struct PrefixedName {
    cRZString name;
    PrefixedName(const cRZString &prefix, const cRZString &section)
    {
        name.Construct(prefix, 0, 0xffffffff);
        name.mString.append("-", 1);
        name.mString.append(section.mString.c_str(), section.mString.size());
    }
    ~PrefixedName() { name.Destruct(); }
    PrefixedName(const PrefixedName &) = delete;
};

} // namespace

// 0x1ad676.
bool cRZStringTokenizer::NextToken(char delimiter, cRZString *out)
{
    std::string &text = reinterpret_cast<cRZString *>(this)->mString;
    for (;;) {
        size_t found = text.find(delimiter, 0);
        if (found == std::string::npos) {
            if (text.size() == 0) return false;
            out->mString.erase(0);
            out->mString.swap(text);
            return true;
        }
        size_t next = text.find_first_not_of(delimiter, found);
        if (found != 0) {
            // 0x1ada98 is the library's assign(str, off, count) here (the library match names append and assign
            // alike; the differential test shows out is replaced).
            out->mString.assign(text, 0, found);
            text.erase(0, next);
            return true;
        }
        text.erase(0, next); // leading delimiters
    }
}

// 0xcb2a20.
cTSIniFile *cTSIniFile::Construct(const cIGZString &path, const cRZString *extra)
{
    cRZIniFile::Construct(path);
    mVTable = GameAddress(kVTable);
    mPrefixes.mFirst = nullptr;
    mPrefixes.mLast = nullptr;
    mPrefixes.mEnd = nullptr;

    void *framework = GetServiceObject();
    void *commandLine = VirtualCall<void *(__thiscall *)(void *), kFrameworkSlotCommandLine>(framework);
    if (extra) mPrefixes.PushBackAny(*extra);

    cRZString value;
    value.Construct();
    cRZString name;
    name.Construct(kContextSwitch);
    bool present = VirtualCall<SwitchFn, kCommandLineSlotIsSwitchPresent>(commandLine, name.AsInterface(), value.AsInterface(), true);
    name.Destruct();
    if (present) {
        cRZString token;
        token.Construct();
        while (reinterpret_cast<cRZStringTokenizer *>(&value)->NextToken(';', &token)) mPrefixes.PushBackAny(token);
        token.Destruct();
    }
    value.Destruct();
    return this;
}

// 0xcb2cc0.
void cTSIniFile::Destruct()
{
    mVTable = GameAddress(kVTable);
    mPrefixes.Tidy();
    cRZIniFile::Destruct();
}

// 0xcb2ce0.
cTSIniFile *cTSIniFile::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

// 0x9c6a0e. Returns without clearing when the file cannot be loaded; otherwise a disabled file is cleared again.
bool cTSIniFile::HasSection(const cIGZString &section)
{
    if (!mLoaded) {
        if (!VirtualCall<LoadFn, 17>(this, 0)) return false;
    }
    cRZString lower;
    lower.Construct(section);
    reinterpret_cast<cRZStringHelpers *>(&lower)->LowerCase();
    if (!mEnabled) VirtualCall<BoolThiscall0, 5>(this);
    SectionTable &sections = *reinterpret_cast<SectionTable *>(mEntries);
    bool found = sections.find(*reinterpret_cast<const IniKey *>(&lower)) != sections.end();
    lower.Destruct();
    return found;
}

// 0xcb2e90.
bool cTSIniFile::GetString(const cIGZString &sectionArg, const cIGZString &keyArg, cIGZString *outArg)
{
    const cRZString &section = AsRZ(sectionArg);
    cRZString *out = reinterpret_cast<cRZString *>(outArg);
    for (cRZString *prefix = mPrefixes.mFirst; prefix != mPrefixes.mLast; ++prefix) {
        PrefixedName name(*prefix, section);
        if (!cRZIniFile::GetString(*name.name.AsInterface(), keyArg, outArg)) continue;

        // A hit: "%GameDataDir%" in the value is replaced by the application's text option (found
        // case-insensitively through the string's own Find, slot 28).
        cRZString replacement;
        replacement.Construct();
        bool haveText = static_cast<uint8_t>(appinit::CommonImplSlot4(appinit::kOptionQueryId,reinterpret_cast<uint32_t>(&replacement), 0, 0, 0xffffffffu)) != 0;
        int32_t at = VirtualCall<FindFn, 28>(out, kGameDataDirToken, 0, false);
        // ORIGINAL-BUG: a replacement text that itself contains the token makes this loop run forever.
        while (at != -1 && haveText) {
            out->mString.replace(static_cast<size_t>(at), kGameDataDirTokenLength, replacement.mString.data(), replacement.mString.size());
            at = VirtualCall<FindFn, 28>(out, kGameDataDirToken, 0, false);
        }
        replacement.Destruct();
        return true;
    }
    return cRZIniFile::GetString(sectionArg, keyArg, outArg);
}

// 0xcb2d00.
bool cTSIniFile::SectionWalk(const cIGZString &sectionArg, void *callback, void *context)
{
    const cRZString &section = AsRZ(sectionArg);
    const auto walk = reinterpret_cast<KeyCallback>(callback);
    for (cRZString *prefix = mPrefixes.mFirst; prefix != mPrefixes.mLast; ++prefix) {
        PrefixedName name(*prefix, section);
        if (HasSection(*name.name.AsInterface())) return ForEachKey(*name.name.AsInterface(), walk, context);
    }
    return ForEachKey(sectionArg, walk, context);
}

// 0xcb30f0.
bool cTSIniFile::HasSectionPrefixed(const cIGZString &sectionArg)
{
    const cRZString &section = AsRZ(sectionArg);
    for (cRZString *prefix = mPrefixes.mFirst; prefix != mPrefixes.mLast; ++prefix) {
        PrefixedName name(*prefix, section);
        if (HasSection(*name.name.AsInterface())) return true;
    }
    return HasSection(sectionArg);
}
