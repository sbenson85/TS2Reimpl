// cGZFontSys (lane OA): the whole class in the standalone shape (src/gz/cGZFontSys.h).
#include "gz/cGZFontSys.h"

#include "game/game.h"
#include "game/import.h"
#include "game/original.h"
#include "gz/GZCOM.h"
#include "gz/cGZFont.h"
#include "gz/cIGZCOM.h"
#include "rz/DirectoryScan.h"
#include "rz/RZTextConvert.h"
#include "rz/RZTextParse.h"
#include "rz/StaticGetters.h"
#include "rz/StringKeyParser.h" // cRZStringHelpers::LowerCase
#include "rz/cRZFile.h"
#include "rz/cRZRefCount.h"

#include <Windows.h>

#include <cstring>
#include <list>
#include <new>
#include <string>
#include <utility>

namespace {

constexpr uint32_t kIatGetWindowsDirectoryA = 0xd9a440;
constexpr uint32_t kFontsFolderRva = 0xe50a38; // "Fonts" (5 characters; the folder under the Windows directory)
constexpr uint32_t kFontsMarkerRva = 0xe50a40; // a marker AddFontDirectory counts a file name for once more
constexpr uint32_t kPatternGlobalRva[4] = {0x10618b0, 0x10618b4, 0x10618b8, 0x10618bc}; // the file patterns (char *)
constexpr uint32_t kDefaultFaceRva[4] = {0xe50a04, 0xe50a14, 0xe50a1c, 0xe50a28};      // Init's default faces
constexpr int kDefaultFaceKind[4] = {5, 9, 6, 10};
constexpr uint32_t kBufferChars = 0x200;
constexpr uint32_t kAtlasFlags = 0x8000000; // GetFontByGroup's second argument to the font's slot 14

void Lower(FontSysString &s)
{
    reinterpret_cast<cRZStringHelpers *>(&s.s)->LowerCase();
}

// The original reads the last character raw (an empty string reads the byte before the buffer).
void EndWithSeparator(std::string &s)
{
    const char last = s.data()[s.size() - 1];
    if (last != '\\' && last != '/') s.push_back('\\');
}

// "<Windows directory>\Fonts\" into `path` (the Windows directory replaces it) when GetWindowsDirectoryA answers.
// `convert`: through ConvertBytesToString (AddFontFile), else a plain assign (the others).
// Returns whether the Windows directory was available (the separator, "Fonts" and the last separator are added then).
bool WindowsFontsFolder(FontSysString &path, bool convert)
{
    char buffer[kBufferChars];
    const uint32_t n = ImportFunction<UINT(WINAPI *)(LPSTR, UINT), kIatGetWindowsDirectoryA>()(buffer, kBufferChars);
    if (n - 1 > 0x1ff) return false;
    const uint32_t length = static_cast<uint32_t>(std::strlen(buffer));
    if (convert) ConvertBytesToString(buffer, length, -1, path.s.AsInterface());
    else path.s.mString.assign(buffer, length);
    EndWithSeparator(path.s.mString);
    path.s.mString.append(GameAddress<const char>(kFontsFolderRva), 5);
    path.s.mString.push_back('\\');
    return true;
}

// The font file reader (lane OC's cGZFontFile, src/gz/cGZFont.h), built and destroyed through its adapters.
struct FontFile {
    alignas(cGZFontFile) uint8_t raw[cGZFontFile::kSize];
    cGZFontFile *file;
    FontFile() : file(cGZFontFileAdapters::Construct(raw, nullptr)) {}
    ~FontFile() { cGZFontFileAdapters::Destruct(file, nullptr); }
    FontFile(const FontFile &) = delete;
    FontFile &operator=(const FontFile &) = delete;
    bool Open(const cIGZString *path) { return file->Open(path); }
    void Name(cIGZString *out, uint32_t which) { file->Name(out, which); }
    uint32_t Style() { return file->Style(); }
    bool HasCharmap() const { return file->HasCharmap(); }
};

// A cRZFile on the stack (0x1c0c07 / 0x1c0e6d), built and destroyed by hand as the original does.
struct LocalFile {
    union {
        cRZFile f;
    };
    explicit LocalFile(const cIGZString &path) { f.Construct(path); }
    ~LocalFile() { f.Destruct(); }
    LocalFile(const LocalFile &) = delete;
    LocalFile &operator=(const LocalFile &) = delete;
};

// A directory scan on the stack: our ::DirectoryScan (src/rz/DirectoryScan.h), built and destroyed through its adapters.
struct LocalScan {
    alignas(::DirectoryScan) unsigned char raw[sizeof(::DirectoryScan)];
    ::DirectoryScan *scan;
    explicit LocalScan(const cRZString *directory)
        : scan(DirectoryScanAdapters::Construct(raw, nullptr, directory, false, false, false))
    {
    }
    ~LocalScan() { DirectoryScanAdapters::Destruct(scan, nullptr); }
    ::DirectoryScan *operator->() const { return scan; }
    LocalScan(const LocalScan &) = delete;
    LocalScan &operator=(const LocalScan &) = delete;
};

// The new font: the COM director's class 0x1aa404ac, else the class made by hand (cGZFont, lane OC).
cIGZFont *NewFont()
{
    cIGZFont *font = nullptr;
    cIGZCOM *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    if (!static_cast<uint8_t>(com->GetClassObject(cIGZFont::kClassId, cIGZFont::kIid, reinterpret_cast<void **>(&font)))) {
        font = cGZFont::Create();
        font->AddRef();
    }
    return font;
}

} // namespace

// ---- construction ----

// 0x90f626: the maps empty, the four file patterns from the game's globals, the encoding of cIGZString text.
cGZFontSys::cGZFontSys()
{
    for (uint32_t rva : kPatternGlobalRva) mPatterns.push_back(FontSysString(*GameAddress<const char *>(rva)));
    mEncoding = static_cast<uint32_t>(RZText::DefaultEncoding());
}

// 0x90f85c (the members in reverse order); the deleting destructor is slot 24 (0x90f8a3).
cGZFontSys::~cGZFontSys() = default;

// 0x909fd2.
cGZFontSys *__cdecl cGZFontSys::Create()
{
    return new cGZFontSys;
}

cGZFontSys *__fastcall cGZFontSysAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cGZFontSys;
}

void __fastcall cGZFontSysAdapters::Destruct(cGZFontSys *self, void *)
{
    self->cGZFontSys::~cGZFontSys();
}

// ---- cIGZUnknown ----

// 0x910ffb: 1 and 0x673960 -> this, the service's id -> the service face; counted through AddRef. Nothing is written
// to `out` for another id.
bool cGZFontSys::QueryInterface(uint32_t riid, void **out)
{
    switch (riid) {
    case 1:
    case cIGZFontSys::kIid: *out = static_cast<cIGZFontSys *>(this); break;
    case cIGZSystemService::kIID: *out = static_cast<cIGZSystemService *>(this); break;
    default: return false;
    }
    AddRef();
    return true;
}

// 0x90ff98: interlocked.
uint32_t cGZFontSys::AddRef()
{
    return static_cast<uint32_t>(RZAtomicAdd(&mRefCount, 1));
}

// 0x911043: the last reference (a count of 1 or 0) shuts the system down and deletes it (0x910249), answering 0.
uint32_t cGZFontSys::Release()
{
    if (static_cast<uint32_t>(mRefCount) > 1) return static_cast<uint32_t>(RZAtomicAdd(&mRefCount, -1));
    Shutdown();
    delete this;
    return 0;
}

// ---- life ----

// 0x910a9d: the default face of a kind: a new entry, or the existing one's name replaced.
bool cGZFontSys::SetDefaultFace(int kind, const cIGZString &name)
{
    const auto it = mDefaults.find(kind);
    if (it == mDefaults.end()) {
        mDefaults.emplace(kind, FontSysString(name));
    } else {
        const uint32_t length = name.Strlen();
        it->second.s.mString.assign(name.Data(), length);
    }
    return true;
}

// 0x910db5: once, the default faces of kinds 5, 9, 6 and 10.
bool cGZFontSys::Init()
{
    if (!mInitialised) {
        for (unsigned i = 0; i < 4; ++i) {
            FontSysString face(GameAddress<const char>(kDefaultFaceRva[i]));
            SetDefaultFace(kDefaultFaceKind[i], *face.s.AsInterface());
        }
        mInitialised = true;
    }
    return true;
}

// 0x9111e3: when running, every font of every file released (and the family asked for once more when it lives on),
// the lists emptied and the maps but the sizes cleared, the renderer released.
bool cGZFontSys::Shutdown()
{
    if (mInitialised) {
        for (auto &file : mFiles) {
            for (const FontSysElement &element : file.second.fonts) {
                cIGZFont *const font = element.font;
                if (!font) continue;
                if (font->Release()) {
                    FontSysString family;
                    font->vf5(family.s.AsInterface());
                }
            }
            file.second.fonts.clear();
        }
        mFiles.clear();
        mDefaults.clear();
        mFamilies.clear();
        mAliases.clear();
        if (mRenderer) {
            mRenderer->Release();
            mRenderer = nullptr;
        }
        mInitialised = false;
    }
    return true;
}

bool cGZFontSys::SetEncoding(uint32_t encoding)
{
    mEncoding = encoding;
    return true;
}

// 0x91111b: counted both ways, the new renderer first.
void cGZFontSys::SetFontRenderer(cIGZFontRenderer *renderer)
{
    if (renderer) renderer->AddRef();
    if (mRenderer) mRenderer->Release();
    mRenderer = renderer;
}

void cGZFontSys::SetQuality(uint32_t mode)
{
    mQuality = mode;
}

// 0x911142: no family: the default range. A family: low < high (signed) adds its range unless it has one, else forgets it.
void cGZFontSys::SetSizeRange(uint32_t low, uint32_t high, const cIGZString *family)
{
    if (!family) {
        mSizeLow = low;
        mSizeHigh = high;
        return;
    }
    FontSysString key(*family);
    if (static_cast<int32_t>(low) < static_cast<int32_t>(high)) {
        mRanges.emplace(FontSysString(key), FontSysRange{low, high});
    } else {
        mRanges.erase(key);
    }
}

// ---- fonts ----

// 0x9109d4: the best font of the group (no names), tinted with `colour` (and the atlas flag).
cIGZFont *cGZFontSys::GetFontByGroup(uint32_t group, uint32_t size, uint32_t style, uint32_t colour, bool shared)
{
    FontSysString names;
    cIGZFont *const font = FindFont(names.s.AsInterface(), group, size, style, shared);
    if (font) font->vf14(colour, kAtlasFlags);
    return font;
}

// 0x91059a: the font of a file at a size and style. A name that is no file is looked for in the Windows fonts folder.
// A shared font is found among those loaded for the file (the last file that matches wins); a new one is made, set up
// and checked by its slot 3, and kept in the file's record. The caller gets a counted reference.
cIGZFont *cGZFontSys::LoadFont(const cIGZString *name, uint32_t size, uint32_t style, bool shared)
{
    FontSysString path(*name);
    if (!cRZFile::PathExists(*path.s.AsInterface())) {
        FontSysString original(path);
        char buffer[kBufferChars];
        const uint32_t n = ImportFunction<UINT(WINAPI *)(LPSTR, UINT), kIatGetWindowsDirectoryA>()(buffer, kBufferChars);
        if (n - 1 <= 0x1ff) {
            path.s.mString.assign(buffer, std::strlen(buffer));
            EndWithSeparator(path.s.mString);
            path.s.mString.append(GameAddress<const char>(kFontsFolderRva), 5);
            path.s.mString.push_back('\\');
            path.s.mString.append(original.s.mString);
        }
    }
    if (size == 0) return nullptr;

    cIGZFont *font = nullptr;
    uint32_t mode = 1;
    if (shared) {
        for (auto &file : mFiles) {
            if (!file.first.s.AsInterface()->IsEqual(path.s.AsInterface(), true)) continue;
            for (const FontSysElement &element : file.second.fonts) {
                if (element.font->vf10() == size && element.style == style) {
                    font = element.font;
                    break;
                }
            }
        }
    }
    if (!font) {
        font = NewFont();
        font->vf22(path.s.AsInterface());
        font->vf11(size);
        font->vf8(style, 1);
        font->vf24(mEncoding);
        font->vf55(mRenderer);
        mode = mQuality;
        if (mSizeLow > size || size > mSizeHigh) {
            FontSysString family;
            font->vf5(family.s.AsInterface());
            const auto range = mRanges.find(family.s);
            if (range != mRanges.end() && !(range->second.low > size) && size <= range->second.high) mode = 1;
        } else {
            mode = 1;
        }
        font->vf16(mode);
        if (!font->vf3()) {
            font->Release();
            return nullptr;
        }
        if (shared) {
            FontSysString key(path);
            const auto file = mFiles.find(key.s);
            if (file == mFiles.end()) {
                std::list<FontSysElement> list;
                list.push_back({font, style});
                FontSysString family;
                font->vf5(family.s.AsInterface());
                const uint32_t recordStyle = font->vf7();
                const uint32_t group = font->vf9();
                mFiles.emplace(FontSysString(path), FontSysRecord(std::move(list), family, recordStyle, group, 0));
                Lower(family);
                mFamilies.insert(family);
            } else {
                file->second.fonts.push_back({font, style});
            }
        }
    }
    font->AddRef();
    if (!shared) font->Release();
    return font;
}

// 0x91025c: the best record for a list of family names (aliases that are no family replaced by their family), then
// a font of it. A record scores 0 for the first name, 2 for a later one, 0x20 for none (without names: 0 for the group
// asked, 4 otherwise), plus 8 (a style it lacks 0x10) when its style differs from the one asked (italic and bold bits
// only); the lowest score wins, the first among equals.
cIGZFont *cGZFontSys::FindFont(const cIGZString *names, uint32_t group, uint32_t size, uint32_t style, bool shared)
{
    FontSysString wanted(*names);
    FontSysString recordName;
    const uint32_t flags = style & 0xffffffc3;
    Lower(wanted);
    FontSysString parts[32];
    uint32_t count = 32;
    rztext::SplitText(wanted.s.mString.c_str(), static_cast<uint32_t>(wanted.s.mString.size()), ',',
                      reinterpret_cast<cRZString *>(parts), &count);
    for (int32_t i = static_cast<int32_t>(count) - 1; i >= 0; --i) {
        FontSysString &part = parts[i];
        for (auto &entry : mAliases) {
            std::vector<FontSysString> &aliases = entry.second;
            for (uint32_t j = 0; j < aliases.size(); ++j) {
                if (!part.s.AsInterface()->IsEqual(aliases[j].s.AsInterface(), false)) continue;
                FontSysString lowered(part);
                Lower(lowered);
                if (mFamilies.find(lowered.s) == mFamilies.end()) {
                    part.s.mString = entry.first.s.mString;
                    Lower(part);
                    j = static_cast<uint32_t>(aliases.size()) - 1;
                }
            }
        }
    }

    int32_t bestScore = 1000;
    FontSysRecord *best = nullptr;
    const FontSysString *bestKey = nullptr;
    for (auto &file : mFiles) {
        FontSysRecord &record = file.second;
        int32_t score;
        if (count != 0) {
            recordName.s.mString = record.name.s.mString;
            Lower(recordName);
            uint32_t k = 0;
            while (k < count && !(parts[k].s.mString == recordName.s.mString)) ++k;
            score = k < count ? (k > 0 ? 2 : 0) : 0x20;
        } else {
            score = record.group == group ? 0 : 4;
        }
        if (record.style != flags) score += (record.style & ~flags) ? 0x10 : 8;
        if (score < bestScore) {
            best = &record;
            bestKey = &file.first;
            bestScore = score;
        }
    }
    if (!best) return nullptr;
    if (shared) {
        for (const FontSysElement &element : best->fonts) {
            if (element.font->vf10() == size && element.style == style) {
                element.font->AddRef();
                return element.font;
            }
        }
    }
    return LoadFont(bestKey->s.AsInterface(), size, style, shared);
}

// 0x910a26: a reference given back; at the last one (the record's) the record lets go of it. Answers the last count.
uint32_t cGZFontSys::ReleaseFont(cIGZFont *font)
{
    uint32_t result = 0;
    for (auto &file : mFiles) {
        std::list<FontSysElement> &fonts = file.second.fonts;
        for (auto it = fonts.begin(); it != fonts.end(); ++it) {
            if (it->font != font) continue;
            result = font->Release();
            if (result == 1) {
                result = font->Release();
                fonts.erase(it);
            }
            break;
        }
    }
    return result;
}

// 0x90fddc: a font made elsewhere, kept under its own file: a new record, or added to the file's when no font there is
// of its size.
bool cGZFontSys::AddFont(cIGZFont *font)
{
    if (!font) return false;
    FontSysString path;
    font->vf21(path.s.AsInterface());
    const auto file = mFiles.find(path.s);
    if (file == mFiles.end()) {
        std::list<FontSysElement> list;
        FontSysString family;
        font->vf5(family.s.AsInterface());
        const uint32_t recordStyle = font->vf7();
        const uint32_t group = font->vf9();
        font->AddRef();
        list.push_back({font, font->vf7()});
        mFiles.emplace(FontSysString(path), FontSysRecord(std::move(list), family, recordStyle, group, 0));
        Lower(family);
        mFamilies.insert(family);
        return true;
    }
    for (const FontSysElement &element : file->second.fonts) {
        const uint32_t size = font->vf10();
        if (element.font->vf10() == size) return false;
    }
    font->AddRef();
    file->second.fonts.push_back({font, font->vf7()});
    return true;
}

// 0x90fb04: a font file (a name that is no file is looked for in the Windows fonts folder): a record with the family,
// style and group the file says (kept only when it has a family name). False for a file known already or unreadable.
bool cGZFontSys::AddFontFile(const cIGZString *path)
{
    bool added = false;
    FontSysString original(*path);
    FontSysString key(original);
    if (mFiles.find(key.s) != mFiles.end()) return false;
    if (!cRZFile::PathExists(*original.s.AsInterface())) {
        char buffer[kBufferChars];
        const uint32_t n = ImportFunction<UINT(WINAPI *)(LPSTR, UINT), kIatGetWindowsDirectoryA>()(buffer, kBufferChars);
        if (n - 1 <= 0x1ff) {
            ConvertBytesToString(buffer, static_cast<uint32_t>(std::strlen(buffer)), -1, original.s.AsInterface());
            EndWithSeparator(original.s.mString);
            original.s.mString.append(GameAddress<const char>(kFontsFolderRva), 5);
            original.s.mString.push_back('\\');
            const uint32_t length = path->Strlen();
            const char *const data = path->Data();
            original.s.mString.append(data, length);
        }
    }
    if (!cRZFile::PathExists(*original.s.AsInterface())) return false;
    FontSysString converted;
    RZText::ConvertStringToString(*original.s.AsInterface(), *converted.s.AsInterface(), -1);
    FontSysString family;
    FontFile file;
    if (file.Open(original.s.AsInterface())) {
        const uint32_t kind = DetectKind(*original.s.AsInterface());
        file.Name(family.s.AsInterface(), 1);
        const uint32_t style = file.Style();
        added = !family.s.mString.empty();
        const uint32_t group = file.HasCharmap() ? 2 : 1;
        if (!family.s.mString.empty()) {
            mFiles.emplace(FontSysString(original),
                           FontSysRecord(std::list<FontSysElement>(), family, style, group, kind));
            Lower(family);
            mFamilies.insert(family);
        }
    }
    return added;
}

// 0x90f8c2: every file the patterns find in a directory (an empty one: the Windows fonts folder), added. The count is
// one for each file added and one more for each name that has the marker.
uint32_t cGZFontSys::AddFontDirectory(const cIGZString *directory)
{
    FontSysString dir(*directory);
    if (dir.s.mString.empty()) WindowsFontsFolder(dir, false);
    EndWithSeparator(dir.s.mString);
    uint32_t total = 0;
    const size_t patterns = mPatterns.size();
    for (size_t i = 0; i < patterns; ++i) {
        LocalScan scan(&dir.s);
        dscan::PathList files;
        scan->SetPattern(&mPatterns[i].s, false);
        if (scan->ListFiles(&files)) {
            for (const dscan::Path &entry : files) {
                FontSysString full;
                full.s.mString = dir.s.mString + entry.s.mString;
                if (entry.s.mString.find(GameAddress<const char>(kFontsMarkerRva), 0) != std::string::npos) ++total;
                if (AddFontFile(full.s.AsInterface())) ++total;
            }
        }
    }
    return total;
}

// 0x911066: always false. The family forgotten and every record of that name dropped (its fonts released).
bool cGZFontSys::RemoveFamily(const cIGZString *name)
{
    FontSysString lowered(*name);
    Lower(lowered);
    mFamilies.erase(lowered);
    for (auto it = mFiles.begin(); it != mFiles.end();) {
        if (it->second.name.s.AsInterface()->IsEqual(name, false)) {
            for (const FontSysElement &element : it->second.fonts) element.font->Release();
            it = mFiles.erase(it);
        } else {
            ++it;
        }
    }
    return false;
}

// 0x9100c5: the families (name, style, group), up to `count` of them; without room: how many there are.
uint32_t cGZFontSys::ListFamilies(cGZFontFamilyInfo *out, uint32_t count)
{
    uint32_t n = static_cast<uint32_t>(mFiles.size());
    if (count == 0) return n;
    if (n > count) n = count;
    auto it = mFiles.begin();
    for (uint32_t i = 0; i < n; ++i, ++it) {
        std::memset(&out[i], 0, 0x80);
        std::strncpy(out[i].name, it->second.name.s.mString.c_str(), 0x7f);
        out[i].style = it->second.style;
        out[i].group = it->second.group;
    }
    return n;
}

// 0x9101f1: the loaded fonts, up to `capacity` of them into `out`; the count of all (without room) or of those stored.
uint32_t cGZFontSys::ListFonts(cIGZFont **out, uint32_t capacity)
{
    uint32_t total = 0, stored = 0;
    for (auto &file : mFiles) {
        for (const FontSysElement &element : file.second.fonts) {
            if (capacity == 0) {
                ++total;
            } else if (stored < capacity) {
                out[stored++] = element.font;
                ++total;
            }
        }
    }
    return total;
}

// 0x90ffae: an alias of a family name (the name's list made when it has none).
bool cGZFontSys::AddAlias(const cIGZString *name, const cIGZString *alias)
{
    FontSysString key(*name);
    auto it = mAliases.find(key.s);
    if (it == mAliases.end()) {
        mAliases.emplace(FontSysString(key), std::vector<FontSysString>());
        it = mAliases.find(key.s);
    }
    it->second.push_back(FontSysString(*alias));
    return true;
}

// 0x910148: the aliases of a name into the strings of `out`, up to `count` of them; the number of aliases returned.
uint32_t cGZFontSys::GetAliases(const cIGZString *name, cIGZString **out, uint32_t count)
{
    FontSysString key(*name);
    const auto it = mAliases.find(key.s);
    if (it == mAliases.end()) return 0;
    const std::vector<FontSysString> &aliases = it->second;
    uint32_t n = static_cast<uint32_t>(aliases.size());
    if (n > count) n = count;
    // ORIGINAL-BUG: the loop runs to `count`, not `n`: with more strings than aliases it reads past the vector.
    for (uint32_t i = 0; i < count; ++i) {
        if (!out[i]) continue;
        const FontSysString &alias = aliases.data()[i];
        out[i]->FromChar(alias.s.mString.c_str(), static_cast<uint32_t>(alias.s.mString.size()));
    }
    return n;
}

// 0x910e90: whether a record of that family has exactly this style (the italic and bold bits of `style`).
bool cGZFontSys::HasFamily(const cIGZString *name, uint32_t, uint32_t style)
{
    FontSysString wanted(*name);
    Lower(wanted);
    if (mFamilies.find(wanted.s) == mFamilies.end()) return false;
    for (auto &file : mFiles) {
        if (file.second.style != (style & 3)) continue;
        FontSysString recordName(file.second.name);
        Lower(recordName);
        if (recordName.s.mString == wanted.s.mString) return true;
    }
    return false;
}

// 0x910f4d: the first alias of a name that is a family (of any style), into `out`.
bool cGZFontSys::ResolveAlias(const cIGZString *name, cIGZString *out)
{
    FontSysString key(*name);
    const auto it = mAliases.find(key.s);
    if (it == mAliases.end()) return false;
    const std::vector<FontSysString> &aliases = it->second;
    const uint32_t n = static_cast<uint32_t>(aliases.size());
    for (uint32_t i = 0; i < n; ++i) {
        if (HasFamily(aliases[i].s.AsInterface(), 0, 0)) {
            out->FromChar(aliases[i].s.mString.c_str(), static_cast<uint32_t>(aliases[i].s.mString.size()));
            return true;
        }
    }
    return false;
}

// 0x910b35: the kind of a font file. The extension (lowercased, with its '.') decides: .ttf 1, .ttc and .otf 4, .pfb 2;
// any other extension looks in the file's first 32 bytes: "%!PS" anywhere is 2 (Type 1), 00 01 00 00 at the start 1
// (TrueType), "ttcf" at the start 4 (a collection); an unreadable file or no match is 0. `this` is not used.
uint32_t cGZFontSys::DetectKind(const cIGZString &path)
{
    FontSysString extension;
    OriginalFunction<bool(__cdecl *)(const cIGZString *, cIGZString *), 0x1c1e0e>()(&path, extension.s.AsInterface());
    Lower(extension);
    const std::string &ext = extension.s.mString;
    uint32_t kind = 0;
    if (ext == ".ttf") kind = 1;
    else if (ext == ".ttc" || ext == ".otf") kind = 4;
    else if (ext == ".pfb") kind = 2;
    if (kind != 0) return kind;

    LocalFile file(path);
    if (file.f.Open(1, 2, 1)) {
        constexpr uint32_t kHeaderBytes = 0x20;
        std::string header(kHeaderBytes, '\0');
        if (file.f.Read(header.data(), kHeaderBytes)) {
            // The original searches with memchr and memcmp for the first byte of each pattern, which finds what
            // std::string::find finds (all three patterns lie inside the 32 bytes).
            if (header.find("%!PS", 0, 4) != std::string::npos) kind = 2;
            else if (header.compare(0, 4, "\x00\x01\x00\x00", 4) == 0) kind = 1;
            else if (header.compare(0, 4, "ttcf", 4) == 0) kind = 4;
        }
        file.f.Close();
    }
    return kind;
}
