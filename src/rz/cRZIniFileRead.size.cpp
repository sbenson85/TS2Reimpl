// cRZIniFile construction, destruction and the value read (slot 6), with the string helpers the read uses.
// See cRZIniFile.h for the layout; the first half of the class (slots 2, 4, 7, 11, 16) is in cRZIniFile.cpp.
#include "game/game.h"
#include "game/vtable.h"
#include "rz/StringKeyParser.h"
#include "rz/cRZFile.h"
#include "rz/cRZIniFile.h"
#include "rz/cRZIniFileTables.h"

#include <cstring>
#include <map>
#include <new>

namespace {

using rzini::IniKey;
using rzini::SecondTable;
using rzini::SectionTable;

using BoolThiscall0 = bool(__thiscall *)(void *);
using UintThiscall0 = uint32_t(__thiscall *)(void *);
using BufferingFn = bool(__thiscall *)(void *, int32_t, int32_t);
using SeekFn = uint32_t(__thiscall *)(void *, uint32_t);
using LoadFn = bool(__thiscall *)(void *, int32_t);
using ReadLineFn = bool(__thiscall *)(void *, cRZString *);

const cRZString &AsRZ(const cIGZString &s)
{
    return *reinterpret_cast<const cRZString *>(&s);
}

// 0x14205a: assign through (data, size), and not at all to itself.
void AssignString(std::string &dst, const std::string &src)
{
    if (&dst != &src) dst.assign(src.c_str(), src.size());
}

constexpr const char kBlanks[] = " \t"; // 0xda2624

} // namespace

// 0x1ad11d.
void cRZIniStringHelpers::TrimLeading()
{
    std::string &s = reinterpret_cast<cRZString *>(this)->mString;
    s.erase(0, s.find_first_not_of(kBlanks, 0));
}

// 0x1ad307: find_last_not_of from the end; +1 wraps to 0 (erase everything) when there is none.
void cRZIniStringHelpers::TrimTrailing()
{
    std::string &s = reinterpret_cast<cRZString *>(this)->mString;
    s.erase(s.find_last_not_of(kBlanks, std::string::npos) + 1);
}

// 0x1ad83c.
void cRZIniStringHelpers::TrimBoth()
{
    TrimLeading();
    TrimTrailing();
}

// 0x1ad555.
cRZString *cRZIniStringHelpers::SplitOff(cRZString *out, const char *delimiter)
{
    cRZString *self = reinterpret_cast<cRZString *>(this);
    size_t length = delimiter ? std::strlen(delimiter) : 0;
    if (length == 0) {
        out->Construct(*self, 0, 0xffffffff);
        return out;
    }
    size_t found = self->mString.find(delimiter, 0, length);
    if (found == std::string::npos) {
        out->Construct();
        return out;
    }
    cRZString prefix;
    prefix.Construct(*self, 0, static_cast<uint32_t>(found));
    self->mString.erase(0, found + length);
    out->Construct(prefix, 0, 0xffffffff);
    prefix.Destruct();
    return out;
}

// 0x9c5d11. The game allocates the file with operator new(0x9c) and does not check for failure.
cRZIniFile *cRZIniFile::Construct(const cIGZString &path)
{
    mVTable = GameAddress(kVTable);
    mPath.Construct(AsRZ(path), 0, 0xffffffff);
    mEnabled = true;
    mLoaded = false;
    new (static_cast<void *>(mEntries)) SectionTable();
    new (static_cast<void *>(mSecondTable)) SecondTable();
    mFile = nullptr;
    cRZFile *file = static_cast<cRZFile *>(::operator new(sizeof(cRZFile)));
    file->Construct(path);
    mFile = file;
    VirtualCall<UintThiscall0, 1>(file); // AddRef
    VirtualCall<BufferingFn, 32>(file, 0x1000, 0);
    return this;
}

// 0x9c5e5d. Like the original, never checks mFile for null before the first call.
void cRZIniFile::Destruct()
{
    mVTable = GameAddress(kVTable);
    if (VirtualCall<BoolThiscall0, 4>(mFile)) VirtualCall<BoolThiscall0, 5>(mFile); // IsOpen -> Close
    if (mFile) {
        VirtualCall<UintThiscall0, 2>(mFile); // Release
        mFile = nullptr;
    }
    reinterpret_cast<SecondTable *>(mSecondTable)->~SecondTable();
    reinterpret_cast<SectionTable *>(mEntries)->~SectionTable();
    mPath.Destruct();
}

// 0x9c5eac.
cRZIniFile *cRZIniFile::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, 0x40);
    return this;
}

// 0x9c679f.
bool cRZIniFile::GetString(const cIGZString &sectionArg, const cIGZString &keyArg, cIGZString *outArg)
{
    cRZString *out = reinterpret_cast<cRZString *>(outArg);
    auto clearIfDisabled = [this]() {
        if (!mEnabled) VirtualCall<BoolThiscall0, 5>(this);
    };

    if (!mLoaded) {
        if (!VirtualCall<LoadFn, 17>(this, 0)) {
            clearIfDisabled();
            return false;
        }
    }

    cRZString sectionLower;
    sectionLower.Construct(AsRZ(sectionArg), 0, 0xffffffff);
    reinterpret_cast<cRZStringHelpers *>(&sectionLower)->LowerCase();
    cRZString keyLower;
    keyLower.Construct(AsRZ(keyArg), 0, 0xffffffff);
    reinterpret_cast<cRZStringHelpers *>(&keyLower)->LowerCase();

    bool result = false;
    SectionTable &sections = *reinterpret_cast<SectionTable *>(mEntries);
    auto it = sections.find(*reinterpret_cast<const IniKey *>(&sectionLower));
    bool usable = false;
    if (it != sections.end()) {
        void *file = mFile;
        uint32_t position = VirtualCall<SeekFn, 12>(file, it->second);
        file = mFile;
        uint32_t length = VirtualCall<UintThiscall0, 7>(file);
        usable = position < length;
    }
    if (!usable) {
        clearIfDisabled();
    } else {
        cRZString lineKey;
        lineKey.Construct();
        cRZString line;
        line.Construct();
        auto *lineHelpers = reinterpret_cast<cRZIniStringHelpers *>(&line);
        auto readLine = [this, &line]() { return VirtualCall<ReadLineFn, 18>(this, &line); };
        bool more = readLine(); // the section's own header line
        while (more) {
            more = readLine();
            if (!more) break;
            lineHelpers->TrimLeading();
            if (line.mString.empty()) continue;
            char first = line.mString[0];
            if (first == ';') continue;
            if (first == '[') {
                line.mString.resize(0, 0);
                break;
            }
            cRZString prefix;
            lineHelpers->SplitOff(&prefix, "=");
            AssignString(lineKey.mString, prefix.mString);
            prefix.Destruct();
            reinterpret_cast<cRZIniStringHelpers *>(&lineKey)->TrimTrailing();
            reinterpret_cast<cRZStringHelpers *>(&lineKey)->LowerCase();
            if (lineKey.mString == keyLower.mString) {
                lineHelpers->TrimBoth();
                AssignString(out->mString, line.mString);
                result = true;
                break;
            }
        }
        clearIfDisabled();
        line.Destruct();
        lineKey.Destruct();
    }
    keyLower.Destruct();
    sectionLower.Destruct();
    return result;
}
