// Differential tests for cGZFontSys as a whole class (src/gz/cGZFontSys.h; lane OA, rendering R5b step 2).
//
// Each case builds one font system per side: the original by the COM director's creator 0x909fd2 (odd seeds) or operator
// new and the constructor 0x90f626 (even seeds; the game's vtables), ours by cGZFontSys::Create or the constructor's
// adapter (our vtables). Every slot is called through the object's vtable (the generated interface: cIGZFontSys at +0,
// cIGZSystemService at +4), so the original side runs the game's slot functions and ours our members. The world:
// - the COM director (GetDirectorFrameworkObject 0x1ae18d stood in on both sides) hands out cIGZFont fakes from a pool
//   (class 0x1aa404ac; on some calls it fails with a word whose low byte is 0, and the font is then made by hand: the
//   constructor 0x90a7db over a fresh block on the original side and cGZFont::Create on ours, both stood in to answer
//   the next fake);
// - the font fakes derive their answers (family, style, group, whether slot 3 loads) from a hash of the path they were
//   given (slot 22), keep the size they were given (slot 11) and write their family / path into the strings handed to
//   slots 5 and 21; eight more are held by the test and given to AddFont;
// - the file system: cRZFile::PathExists 0x1c1242 (a set per case), GetWindowsDirectoryA (an import fake: a text per
//   case, or 0, or a length past the buffer), ConvertBytesToString 0x1afc9d and RZText::ConvertStringToString 0x1b025d
//   (copies), the directory scan (0x1c48da new, 0x1c63d4 SetPattern, 0x1c5bbd ListFiles, 0x1c49eb end: a listing per
//   pattern), the font file reader (0x90a6c5 / 0x90d6f7 / 0x90ccd5 / 0x90d27e / 0x90a934: stand-ins over the image's
//   functions and ours, since lane OC made the reader ours) and the kind detector 0x910b35 (a stand-in over the image
//   and over ours; DetectKind has its own test, tests/gz_font_sys_detect.cpp), answering from the path's hash.
// A case is 60 steps over every slot (and the service face), after each of which the log gets the result and the
// object: the scalars, the renderer, and every container by content (families, default faces, file records with their
// fonts, aliases, patterns, size ranges). At the end the reference count is released to zero (Shutdown and the deleting
// destructor) and the log gets the fakes' reference counts and the side's heap balance.
#include "code_patch.h"
#include "difftest.h"
#include "drawfx.h"
#include "impfx.h"

#include "gz/GZCOM.h"
#include "gz/cGZFont.h"
#include "gz/cGZFontSys.h"
#include "gz/cIGZString.h"
#include "rz/DirectoryScan.h"
#include "rz/RZTextConvert.h"
#include "rz/StaticGetters.h"
#include "rz/cRZFile.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace {

using drawfx::Ret;
constexpr drawfx::Slot U{0, Ret::Int, false, "", ""};
constexpr drawfx::Slot kQI{2, Ret::Int, true, "QueryInterface", "ui"};
constexpr drawfx::Slot kAddRef{0, Ret::Int, true, "AddRef", ""};
constexpr drawfx::Slot kRelease{0, Ret::Int, true, "Release", ""};

inline constexpr drawfx::Slot kComSlots[] = {kQI, kAddRef, kRelease, {3, Ret::Int, true, "GetClassObject", "uuW0"}};
inline constexpr drawfx::Iface kCom{"cIGZCOM", 4, kComSlots};

inline constexpr drawfx::Slot kRendererSlots[] = {kQI, kAddRef, kRelease};
inline constexpr drawfx::Iface kRenderer{"cIGZFontRenderer", 3, kRendererSlots};

// cIGZFont: the arities of the generated table, with the names and argument kinds of the slots the class calls.
struct FontSlotTable {
    drawfx::Slot s[66];
    constexpr FontSlotTable() : s{}
    {
        for (unsigned i = 0; i < 66; ++i) s[i] = fx_slots::kcIGZFontSlots[i];
        s[3] = {0, Ret::Int, true, "vf3", ""};
        s[5] = {1, Ret::Int, true, "vf5", "S"};
        s[7] = {0, Ret::Int, true, "vf7", ""};
        s[8] = {2, Ret::Int, true, "vf8", "uu"};
        s[9] = {0, Ret::Int, true, "vf9", ""};
        s[10] = {0, Ret::Int, true, "vf10", ""};
        s[11] = {1, Ret::Int, true, "vf11", "u"};
        s[14] = {2, Ret::Int, true, "vf14", "uu"};
        s[16] = {1, Ret::Int, true, "vf16", "u"};
        s[21] = {1, Ret::Int, true, "vf21", "S"};
        s[22] = {1, Ret::Int, true, "vf22", "S"};
        s[24] = {1, Ret::Int, true, "vf24", "u"};
        s[55] = {1, Ret::Int, true, "vf55", "i"};
    }
};
inline constexpr FontSlotTable kFontTable;
inline constexpr drawfx::Iface kFont{"cIGZFont", 66, kFontTable.s};

constexpr uint32_t kCreator = 0x909fd2, kConstructor = 0x90f626, kDestructor = 0x90f85c;
constexpr uint32_t kObjectTag = 0x0c000000;
constexpr uint32_t kPoolFonts = 56, kExternalFonts = 8, kAllFonts = kPoolFonts + kExternalFonts;
constexpr uint32_t kGameVTable = 0xe50968, kGameVTable2 = 0xe509d0;
constexpr uint32_t kPatternCell[4] = {0x10618b0, 0x10618b4, 0x10618b8, 0x10618bc};
const char *const kPatterns[4] = {"*.ttf", "*.fon", "*.otf", "*.ttc"};

drawfx::Fake gCom, gRenderer, gRenderer2, gFonts[kAllFonts];

const char *const kFamilies[] = {"Arial", "Times New Roman", "Verdana", "Comic Sans", "Tahoma", "Courier"};
const char kLongFamily[] =
    "A-family-name-of-more-than-a-hundred-and-twenty-seven-characters-which-no-font-has-but-the-list-of-families-copies-"
    "at-most-the-first-127-of-them-and-ends-them";
const char *const kBaseNames[] = {"arial.ttf", "times.ttf", "verdana.fon", "comic.otf", "tahoma.ttf", "cour.ttc", "x.ttf", "y.fon"};
const char *const kDirs[] = {"c:\\fonts\\", "D:\\Data\\Fonts\\", "e:/fonts/"};
const char *const kAliasNames[] = {"sans", "serif", "Mono", "Arial", "Tahoma", "Comic Sans"};

struct Case {
    std::string windowsDir;
    uint32_t windowsMode = 0; // 0 the text, 1 zero, 2 a length past the buffer, 3 the longest allowed
    std::set<std::string> exists;
    std::vector<std::string> listing[4];
    uint32_t failPhase = 9; // the director fails on every fifth call from this phase (9: never)
    uint32_t directorCalls = 0;
    uint32_t nextFont = 0;
    uint32_t marker = 0;
} gCase;

struct FontState {
    std::string path;
    uint32_t size = 0;
};
FontState gFontState[kAllFonts];

struct FileTraits {
    bool opens;
    bool loads;
    const char *family; // the family a font has
    const char *fileFamily; // what the file reader finds ("" for none)
    uint32_t style, group, charmap, kind;
};

uint32_t HashText(const std::string &s, bool lower = true)
{
    uint32_t h = 2166136261u;
    for (char c : s) {
        if (lower && c >= 'A' && c <= 'Z') c = char(c + 32);
        h = (h ^ uint8_t(c)) * 16777619u;
    }
    return h;
}

FileTraits TraitsOf(const std::string &path)
{
    const uint32_t h = HashText(path);
    FileTraits t;
    t.opens = h % 9 != 0;
    t.loads = h % 7 != 0;
    t.family = kFamilies[h % 6];
    // None, one that fills the list-families name field (0x80 bytes with the terminator) and more, or a family.
    t.fileFamily = h % 11 == 0 ? "" : h % 13 == 0 ? kLongFamily : kFamilies[h % 6];
    t.style = ((h >> 3) & 3) | (((h >> 5) & 1) ? 0x40 : 0);
    t.group = 1 + ((h >> 7) & 1);
    t.charmap = (h >> 9) & 1;
    t.kind = (h >> 11) & 7;
    return t;
}

std::string TextOf(uint32_t p)
{
    if (!p) return std::string();
    const cIGZString *s = reinterpret_cast<const cIGZString *>(uintptr_t(p));
    return std::string(s->Data(), s->Strlen());
}
void SetText(uint32_t p, const std::string &t)
{
    reinterpret_cast<cIGZString *>(uintptr_t(p))->FromChar(t.c_str(), uint32_t(t.size()));
}

// The test's own notes (stand-ins' calls): not counted by the heap balance.
void Log(const char *label, std::initializer_list<uint32_t> words)
{
    difftest::NotCounted bookkeeping;
    drawfx::Note(label, words);
}

// ---- the font fakes ----
unsigned IndexOf(const drawfx::Fake &f) { return unsigned(&f - gFonts); }

uint32_t FontVf3(drawfx::Fake &self, const uint32_t *)
{
    return TraitsOf(gFontState[IndexOf(self)].path).loads ? 1u : 0u;
}
uint32_t FontVf5(drawfx::Fake &self, const uint32_t *args)
{
    SetText(args[0], TraitsOf(gFontState[IndexOf(self)].path).family);
    return args[0];
}
uint32_t FontVf7(drawfx::Fake &self, const uint32_t *) { return TraitsOf(gFontState[IndexOf(self)].path).style; }
uint32_t FontVf9(drawfx::Fake &self, const uint32_t *) { return TraitsOf(gFontState[IndexOf(self)].path).group; }
uint32_t FontVf10(drawfx::Fake &self, const uint32_t *) { return gFontState[IndexOf(self)].size; }
uint32_t FontVf11(drawfx::Fake &self, const uint32_t *args)
{
    gFontState[IndexOf(self)].size = args[0];
    return 1;
}
uint32_t FontVf21(drawfx::Fake &self, const uint32_t *args)
{
    SetText(args[0], gFontState[IndexOf(self)].path);
    return args[0];
}
uint32_t FontVf22(drawfx::Fake &self, const uint32_t *args)
{
    gFontState[IndexOf(self)].path = TextOf(args[0]);
    return 1;
}

drawfx::Fake *NextFont()
{
    return gCase.nextFont < kPoolFonts ? &gFonts[gCase.nextFont++] : &gFonts[kPoolFonts - 1];
}

// The director.
uint32_t ComGetClassObject(drawfx::Fake &, const uint32_t *args)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(args[2]));
    if (args[0] == cIGZFont::kClassId && args[1] == cIGZFont::kIid) {
        const uint32_t call = gCase.directorCalls++;
        if (gCase.failPhase != 9 && call % 5 == gCase.failPhase) return 0xab00; // nonzero, low byte 0: a failure
        drawfx::Fake *f = NextFont();
        *out = f;
        ++f->refs;
        return 0xab01;
    }
    return 0;
}
void *__cdecl FakeDirectorObject() { return &gCom; }

// The hand-made font: the constructor 0x90a7db over a block that is no longer needed.
void *__fastcall StandFontConstructor(void *memory, void *)
{
    ::operator delete(memory);
    Log("fontCtor", {});
    return NextFont();
}
// Ours (lane OC): cGZFont::Create, the same new and constructor in one.
void *__cdecl StandFontCreate()
{
    Log("fontCtor", {});
    return NextFont();
}

// ---- the file system ----
bool __cdecl StandPathExists(const cIGZString &path)
{
    const std::string t(path.Data(), path.Strlen());
    Log("exists", {HashText(t, false), uint32_t(gCase.exists.count(t))});
    return gCase.exists.count(t) != 0;
}
uint32_t __cdecl StandConvertBytes(const void *src, uint32_t length, int32_t encoding, cIGZString *dst)
{
    const std::string t(static_cast<const char *>(src), length);
    Log("convertBytes", {HashText(t, false), length, uint32_t(encoding)});
    dst->FromChar(t.c_str(), length);
    return 1;
}
bool __cdecl StandConvertString(const cIGZString &src, cIGZString &dst, int32_t encoding)
{
    const std::string t(src.Data(), src.Strlen());
    Log("convertString", {HashText(t, false), uint32_t(encoding)});
    dst.FromChar(t.c_str(), uint32_t(t.size()));
    return true;
}

uint32_t gPatternIndex = 0;
DirectoryScan *__fastcall StandScanNew(void *self, void *, const cRZString *dir, bool a, bool b, bool c)
{
    std::memset(self, 0, DirectoryScan::kSize);
    Log("scanNew", {HashText(dir->mString, false), uint32_t(a), uint32_t(b), uint32_t(c)});
    return static_cast<DirectoryScan *>(self);
}
void __fastcall StandScanPattern(DirectoryScan *, void *, const cRZString *pattern, bool now)
{
    gPatternIndex = 0;
    for (unsigned i = 0; i < 4; ++i)
        if (pattern->mString == kPatterns[i]) gPatternIndex = i;
    Log("scanPattern", {HashText(pattern->mString, false), uint32_t(now)});
}
bool __fastcall StandScanFiles(DirectoryScan *, void *, dscan::PathList *out)
{
    const std::vector<std::string> &names = gCase.listing[gPatternIndex];
    Log("scanFiles", {uint32_t(names.size())});
    for (const std::string &n : names) {
        FontSysString s(n.c_str());
        out->emplace_back(s.s);
    }
    return !names.empty();
}
void __fastcall StandScanEnd(DirectoryScan *, void *) { Log("scanEnd", {}); }

// The windows directory import.
uint32_t OnWindowsDirectory(drawfx::Fake &, const uint32_t *args)
{
    char *const buffer = reinterpret_cast<char *>(uintptr_t(args[0]));
    switch (gCase.windowsMode) {
    case 1: buffer[0] = 0; return 0;
    case 2: buffer[0] = 0; return args[1] + 0x40;
    case 3:
        std::memset(buffer, 'w', 0x1ff);
        buffer[0x1ff] = 0;
        return 0x200;
    default:
        std::memcpy(buffer, gCase.windowsDir.c_str(), gCase.windowsDir.size() + 1);
        return uint32_t(gCase.windowsDir.size());
    }
}

// ---- the font file reader and the kind detector (original callees) ----
uint8_t gFaceBlock[0x80];
std::string gOpenedPath;
// Returns the reader (ours, cGZFontFileAdapters::Construct, uses the result; the original's callers do not).
void *__fastcall StandFileNew(void *self, void *)
{
    std::memset(self, 0, 0x30);
    *reinterpret_cast<void **>(static_cast<uint8_t *>(self) + 8) = gFaceBlock;
    Log("fileNew", {});
    return self;
}
void __fastcall StandFileEnd(void *, void *) { Log("fileEnd", {}); }
bool __fastcall StandFileOpen(void *, void *, const cIGZString *path)
{
    gOpenedPath = TextOf(uint32_t(uintptr_t(path)));
    const FileTraits t = TraitsOf(gOpenedPath);
    std::memset(gFaceBlock, 0, sizeof gFaceBlock);
    *reinterpret_cast<uint32_t *>(gFaceBlock + 0x5c) = t.charmap ? 0x1234u : 0u;
    Log("fileOpen", {HashText(gOpenedPath, false), uint32_t(t.opens)});
    return t.opens;
}
void __fastcall StandFileName(void *, void *, cIGZString *out, uint32_t which)
{
    Log("fileName", {which});
    SetText(uint32_t(uintptr_t(out)), TraitsOf(gOpenedPath).fileFamily);
}
uint32_t __fastcall StandFileStyle(void *, void *)
{
    Log("fileStyle", {});
    return TraitsOf(gOpenedPath).style;
}
uint32_t __fastcall StandDetectKind(void *, void *, const cIGZString *path)
{
    const std::string t = TextOf(uint32_t(uintptr_t(path)));
    Log("detectKind", {HashText(t, false)});
    return TraitsOf(t).kind;
}

// ---- the world of one side ----
void BuildWorld()
{
    gCom.Reset<kCom>(701);
    gCom.On(3, &ComGetClassObject);
    gRenderer.Reset<kRenderer>(702);
    gRenderer2.Reset<kRenderer>(703);
    for (unsigned i = 0; i < kAllFonts; ++i) {
        gFonts[i].Reset<kFont>(710 + i);
        gFonts[i].refs = 0;
        gFonts[i].On(3, &FontVf3);
        gFonts[i].On(5, &FontVf5);
        gFonts[i].On(7, &FontVf7);
        gFonts[i].On(9, &FontVf9);
        gFonts[i].On(10, &FontVf10);
        gFonts[i].On(11, &FontVf11);
        gFonts[i].On(21, &FontVf21);
        gFonts[i].On(22, &FontVf22);
        gFontState[i] = FontState();
    }
    // The fonts the test holds: a file and a size each.
    for (unsigned i = 0; i < kExternalFonts; ++i) {
        gFonts[kPoolFonts + i].refs = 1;
        gFontState[kPoolFonts + i].path = std::string(kDirs[i % 3]) + kBaseNames[i % 8];
        gFontState[kPoolFonts + i].size = 8 + (i % 3) * 4;
    }
    gCase.directorCalls = 0;
    gCase.nextFont = 0;
    gOpenedPath = std::string(); // each side starts without the buffer the other's last file left
}

std::string Upper(std::string s, std::mt19937 &rng)
{
    for (char &c : s)
        if (rng() % 6 == 0 && c >= 'a' && c <= 'z') c = char(c - 32);
    return s;
}

void NewCase(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 77);
    const char *const windows[] = {"C:\\WINDOWS", "C:\\Win\\", "d:/win/", "C:\\W"};
    gCase.windowsDir = windows[rng() % 4];
    const uint32_t m = rng() % 12;
    gCase.windowsMode = m == 0 ? 1 : m == 1 ? 2 : m == 2 ? 3 : 0;
    gCase.exists.clear();
    for (unsigned d = 0; d < 3; ++d)
        for (unsigned n = 0; n < 8; ++n)
            if (rng() % 3 != 0) gCase.exists.insert(std::string(kDirs[d]) + kBaseNames[n]);
    for (unsigned n = 0; n < 8; ++n) {
        if (rng() % 2) gCase.exists.insert(kBaseNames[n]);
        std::string w = gCase.windowsDir;
        if (w.back() != '\\' && w.back() != '/') w += '\\';
        if (rng() % 2) gCase.exists.insert(w + "Fonts\\" + kBaseNames[n]);
    }
    const std::string marker = std::string(GameAddress<const char>(0xe50a40));
    for (unsigned p = 0; p < 4; ++p) {
        gCase.listing[p].clear();
        const uint32_t n = rng() % 4;
        for (uint32_t i = 0; i < n; ++i) {
            std::string name = kBaseNames[rng() % 8];
            if (rng() % 3 == 0) name += marker;
            gCase.listing[p].push_back(name);
        }
    }
    gCase.failPhase = rng() % 3 == 0 ? rng() % 5 : 9;
}

// ---- reading the object ----
uint32_t Word(const void *object, uint32_t offset)
{
    uint32_t v;
    std::memcpy(&v, static_cast<const uint8_t *>(object) + offset, 4);
    return v;
}

// The object but its vtables and padding, every container by content.
void NoteObject(const void *object)
{
    difftest::NotCounted bookkeeping;
    const cGZFontSys &o = *static_cast<const cGZFontSys *>(object);
    drawfx::Call c;
    c.note = "object";
    auto W = [&](uint32_t v) { c.words.push_back(v); };
    auto T = [&](const FontSysString &s) {
        c.text += s.s.mString;
        c.text += '\x1f';
    };
    W(Word(object, 8) & 0xff);
    for (uint32_t off = 0xc; off < 0x24; off += 4) W(Word(object, off));
    W(drawfx::Norm(Word(object, 0x24)));
    W(uint32_t(o.mFamilies.size()));
    for (const FontSysString &f : o.mFamilies) T(f);
    W(uint32_t(o.mDefaults.size()));
    for (const auto &d : o.mDefaults) {
        W(uint32_t(d.first));
        T(d.second);
    }
    W(uint32_t(o.mFiles.size()));
    for (const auto &f : o.mFiles) {
        T(f.first);
        W(uint32_t(f.second.fonts.size()));
        for (const FontSysElement &e : f.second.fonts) {
            W(drawfx::Norm(drawfx::P(e.font)));
            W(e.style);
        }
        T(f.second.name);
        W(f.second.style);
        W(f.second.group);
        W(f.second.kind);
    }
    W(uint32_t(o.mAliases.size()));
    for (const auto &a : o.mAliases) {
        T(a.first);
        W(uint32_t(a.second.size()));
        for (const FontSysString &s : a.second) T(s);
    }
    W(uint32_t(o.mPatterns.size()));
    for (const FontSysString &p : o.mPatterns) T(p);
    W(uint32_t(o.mRanges.size()));
    for (const auto &r : o.mRanges) {
        T(r.first);
        W(r.second.low);
        W(r.second.high);
    }
    drawfx::gLog.push_back(std::move(c));
}

void *OriginalCreate(bool director)
{
    if (director) return reinterpret_cast<void *(__cdecl *)()>(GameAddress<void>(kCreator))();
    void *const block = ::operator new(cGZFontSys::kSize);
    return reinterpret_cast<void *(__thiscall *)(void *)>(GameAddress<void>(kConstructor))(block);
}
void *OurCreate(bool director)
{
    if (director) return static_cast<cIGZFontSys *>(cGZFontSys::Create());
    return static_cast<cIGZFontSys *>(cGZFontSysAdapters::Construct(::operator new(cGZFontSys::kSize), nullptr));
}

const cIGZString *I(const FontSysString &s) { return s.s.AsInterface(); }
uint32_t FontWord(const void *font) { return drawfx::Norm(drawfx::P(font)); }

// A path or name the slots are given: bare, in a directory, or under the Windows fonts folder, in mixed case.
std::string PickPath(std::mt19937 &rng)
{
    std::string base = kBaseNames[rng() % 8];
    switch (rng() % 4) {
    case 0: return Upper(base, rng);
    case 1: return Upper(std::string(kDirs[rng() % 3]) + base, rng);
    case 2: {
        std::string w = gCase.windowsDir;
        if (w.back() != '\\' && w.back() != '/') w += '\\';
        return w + "Fonts\\" + base;
    }
    default: return base;
    }
}
std::string PickNames(std::mt19937 &rng)
{
    std::string out;
    const uint32_t n = rng() % 4;
    for (uint32_t i = 0; i < n; ++i) {
        if (i) out += rng() % 5 ? "," : ", ";
        const uint32_t k = rng() % 8;
        out += Upper(k == 0 ? "Zzz" : k < 5 ? kFamilies[rng() % 6] : kAliasNames[rng() % 6], rng);
    }
    return out;
}

// One side: the object made by the side's creator, then the case's steps.
void RunSide(bool ours, uint32_t seed)
{
    drawfx::Cells cells;
    for (unsigned i = 0; i < 4; ++i) cells.Set(kPatternCell[i], static_cast<const void *>(kPatterns[i]));
    drawfx::gLog.reserve(1 << 15);
    std::mt19937 rng(seed);
    difftest::AllocationCounter heap;

    void *const object = ours ? OurCreate(seed % 2 != 0) : OriginalCreate(seed % 2 != 0);
    {
        difftest::NotCounted bookkeeping;
        drawfx::AddRange(object, cGZFontSys::kSize, kObjectTag);
    }
    cIGZFontSys *const sys = static_cast<cIGZFontSys *>(object);
    cIGZSystemService *const svc =
        reinterpret_cast<cIGZSystemService *>(static_cast<uint8_t *>(object) + 4);
    NoteObject(object);
    drawfx::Note("addref", sys->AddRef());
    std::vector<cIGZFont *> held;
    auto Hold = [&](cIGZFont *f) {
        if (f) held.push_back(f);
        drawfx::Note("font", FontWord(f));
    };
    if (rng() % 4) drawfx::Note("init", sys->Init());

    for (int step = 0; step < 60; ++step) {
        const uint32_t pick = rng() % 32;
        drawfx::Note("pick", pick);
        if (std::getenv("TS2_FONTSYS_TRACE")) std::fprintf(stderr, "%s step %d pick %u log %zu\n", ours ? "ours" : "orig", step, pick, drawfx::gLog.size());
        const uint32_t size = rng() % 5 == 0 ? 0 : 6 + rng() % 20;
        const uint32_t style = rng() % 3 == 0 ? rng() % 256 : rng() % 4;
        const bool shared = rng() % 4 != 0;
        if (pick < 2) {
            drawfx::Note("init", sys->Init());
        } else if (pick < 3) {
            if (rng() % 2) drawfx::Note("shutdown", sys->Shutdown());
            else drawfx::Note("shutdown2", svc->Shutdown());
            held.clear();
        } else if (pick < 4) {
            drawfx::Note("encoding", sys->SetEncoding(rng() % 5 ? rng() % 8 : rng()));
        } else if (pick < 5) {
            sys->SetFontRenderer(rng() % 5 == 0 ? nullptr : rng() % 2 ? static_cast<cIGZFontRenderer *>(static_cast<void *>(&gRenderer))
                                                                       : static_cast<cIGZFontRenderer *>(static_cast<void *>(&gRenderer2)));
        } else if (pick < 6) {
            sys->SetQuality(rng() % 4);
        } else if (pick < 8) {
            const uint32_t low = rng() % 3 ? rng() % 20 : rng() % 3 ? 0u : 0xfffffff0u;
            const uint32_t high = rng() % 3 ? rng() % 30 : rng() % 20;
            FontSysString family(Upper(kFamilies[rng() % 6], rng).c_str());
            sys->SetSizeRange(low, high, rng() % 3 ? I(family) : nullptr);
        } else if (pick < 10) {
            Hold(sys->GetFontByGroup(rng() % 4, size, style, rng(), shared));
        } else if (pick < 14) {
            FontSysString name(PickPath(rng).c_str());
            Hold(sys->LoadFont(I(name), size, style, shared));
        } else if (pick < 18) {
            FontSysString names(PickNames(rng).c_str());
            Hold(sys->FindFont(I(names), rng() % 4, size, style, shared));
        } else if (pick < 20) {
            cIGZFont *f = held.empty() || rng() % 6 == 0 ? static_cast<cIGZFont *>(static_cast<void *>(&gFonts[rng() % kAllFonts]))
                                                         : held[rng() % held.size()];
            drawfx::Note("releaseFont", sys->ReleaseFont(f));
        } else if (pick < 21) {
            cIGZFont *f = rng() % 8 == 0 ? nullptr : static_cast<cIGZFont *>(static_cast<void *>(&gFonts[kPoolFonts + rng() % kExternalFonts]));
            if (f && rng() % 3 == 0) gFontState[IndexOf(*reinterpret_cast<drawfx::Fake *>(f))].size = 6 + rng() % 20;
            drawfx::Note("addFont", sys->AddFont(f));
        } else if (pick < 24) {
            FontSysString path(PickPath(rng).c_str());
            const bool added = sys->AddFontFile(I(path));
            if (std::getenv("TS2_FONTSYS_TRACE"))
                std::fprintf(stderr, "  addFontFile '%s' -> %d (win '%s' mode %u)\n", path.s.mString.c_str(), int(added), gCase.windowsDir.c_str(), gCase.windowsMode);
            drawfx::Note("addFontFile", added);
        } else if (pick < 25) {
            FontSysString dir(rng() % 3 == 0 ? "" : kDirs[rng() % 3]);
            drawfx::Note("addFontDirectory", sys->AddFontDirectory(I(dir)));
        } else if (pick < 26) {
            drawfx::Note("vf16", sys->vf16(rng()));
        } else if (pick < 27) {
            FontSysString name(rng() % 2 ? Upper(kFamilies[rng() % 6], rng).c_str() : "Nothing");
            drawfx::Note("removeFamily", sys->RemoveFamily(I(name)));
        } else if (pick < 28) {
            const uint32_t count = rng() % 4 == 0 ? 0 : rng() % 6;
            std::vector<cGZFontFamilyInfo> out(count + 1);
            std::memset(out.data(), 0xcd, sizeof(cGZFontFamilyInfo) * out.size());
            const uint32_t n = sys->ListFamilies(count ? out.data() : nullptr, count);
            drawfx::Note("listFamilies", {n, HashText(std::string(reinterpret_cast<const char *>(out.data()),
                                                                   sizeof(cGZFontFamilyInfo) * out.size()), false)});
        } else if (pick < 29) {
            const uint32_t capacity = rng() % 4 == 0 ? 0 : rng() % 6;
            std::vector<cIGZFont *> out(capacity + 1, nullptr);
            const uint32_t n = sys->ListFonts(capacity ? out.data() : nullptr, capacity);
            drawfx::gLog.reserve(drawfx::gLog.size() + 4);
            std::vector<uint32_t> words{n};
            for (cIGZFont *f : out) words.push_back(FontWord(f));
            drawfx::Call c;
            c.note = "listFonts";
            c.words = words;
            drawfx::gLog.push_back(std::move(c));
        } else if (pick < 30) {
            FontSysString name(rng() % 2 ? kAliasNames[rng() % 6] : kFamilies[rng() % 6]);
            FontSysString alias(Upper(kAliasNames[rng() % 6], rng).c_str());
            drawfx::Note("addAlias", sys->AddAlias(I(name), I(alias)));
        } else if (pick < 31) {
            FontSysString name(rng() % 2 ? kAliasNames[rng() % 6] : kFamilies[rng() % 6]);
            switch (rng() % 3) {
            case 0: {
                // GetAliases writes `count` strings (a count past the aliases reads past their vector: the original's
                // bug), so it is first asked into nothing.
                cIGZString *none[8] = {};
                const uint32_t n = sys->GetAliases(I(name), none, 8);
                const uint32_t count = n ? 1 + rng() % n : 0;
                FontSysString strings[8];
                cIGZString *out[8] = {};
                for (uint32_t i = 0; i < count; ++i) out[i] = strings[i].s.AsInterface();
                const uint32_t m = sys->GetAliases(I(name), out, count);
                drawfx::Call c;
                c.note = "getAliases";
                c.words = {n, m};
                for (uint32_t i = 0; i < count; ++i) c.text += strings[i].s.mString + '\x1f';
                drawfx::gLog.push_back(std::move(c));
                break;
            }
            case 1: {
                FontSysString family(Upper(kFamilies[rng() % 6], rng).c_str());
                drawfx::Note("hasFamily", sys->HasFamily(I(family), rng(), style));
                break;
            }
            default: {
                FontSysString out("untouched");
                const bool r = sys->ResolveAlias(I(name), out.s.AsInterface());
                drawfx::Call c;
                c.note = "resolveAlias";
                c.words = {uint32_t(r)};
                c.text = out.s.mString;
                drawfx::gLog.push_back(std::move(c));
                break;
            }
            }
        } else {
            switch (rng() % 6) {
            case 5: {
                // The service face's own QueryInterface (0x91103b), AddRef (0x90ffa6) and Release (0x91105e).
                const uint32_t iids[] = {1, cIGZFontSys::kIid, cIGZSystemService::kIID, 0x12345678};
                void *out = reinterpret_cast<void *>(uintptr_t(0xdddd0006));
                const bool answer = svc->QueryInterface(iids[rng() % 4], &out);
                drawfx::Note("svcQuery", {uint32_t(answer),
                                          answer ? uint32_t(static_cast<uint8_t *>(out) - static_cast<uint8_t *>(object)) : drawfx::P(out)});
                if (answer) drawfx::Note("svcQueryRelease", static_cast<cIGZUnknown *>(out)->Release());
                drawfx::Note("svcAddref", svc->AddRef());
                drawfx::Note("svcRelease", svc->Release());
                break;
            }
            case 0: {
                const uint32_t iids[] = {1, cIGZFontSys::kIid, cIGZSystemService::kIID, 0x12345678};
                void *out = reinterpret_cast<void *>(uintptr_t(0xdddd0006));
                const bool answer = sys->QueryInterface(iids[rng() % 4], &out);
                drawfx::Note("query", {uint32_t(answer),
                                       answer ? uint32_t(static_cast<uint8_t *>(out) - static_cast<uint8_t *>(object)) : drawfx::P(out)});
                if (answer) drawfx::Note("queryRelease", static_cast<cIGZUnknown *>(out)->Release());
                break;
            }
            case 1: {
                drawfx::Note("addref", sys->AddRef());
                drawfx::Note("release", sys->Release());
                break;
            }
            case 2:
                drawfx::Note("service", {svc->GetServiceID(), uint32_t(svc->GetServicePriority()),
                                         uint32_t(svc->IsServiceRunning()), uint32_t(svc->GetServiceTickPriority())});
                break;
            case 3:
                svc->SetServiceID(rng());
                svc->SetServiceRunning(rng() % 2);
                drawfx::Note("tick", {uint32_t(svc->OnTick(rng())), uint32_t(svc->OnIdle(rng()))});
                break;
            default: drawfx::Note("serviceInit", svc->Init()); break;
            }
        }
        NoteObject(object);
        if (std::getenv("TS2_FONTSYS_TRACE"))
            std::fprintf(stderr, "%s after step %d: %d blocks %d bytes\n", ours ? "ours" : "orig", step, int(heap.Blocks()), int(heap.Bytes()));
    }

    const uint32_t count = Word(object, 0xc);
    drawfx::Note("count", count);
    for (uint32_t i = 1; i < count && i < 64; ++i) drawfx::Note("release", sys->Release());
    NoteObject(object);
    drawfx::Note("last", sys->Release());
    heap.Stop();
    {
        drawfx::Call c;
        c.note = "refs";
        c.words.push_back(uint32_t(gCom.refs));
        c.words.push_back(uint32_t(gRenderer.refs));
        c.words.push_back(uint32_t(gRenderer2.refs));
        for (unsigned i = 0; i < kAllFonts; ++i) c.words.push_back(uint32_t(gFonts[i].refs));
        drawfx::gLog.push_back(std::move(c));
    }
    drawfx::Note("heap", {uint32_t(heap.Blocks()), uint32_t(heap.Bytes())});
}

void RunCase(difftest::Context &ctx, uint32_t seed, bool &ok)
{
    NewCase(seed);
    ctx.Mark("orig");
    auto a = drawfx::Side(&BuildWorld, [&] { RunSide(false, seed); });
    NewCase(seed);
    ctx.Mark("ours");
    auto b = drawfx::Side(&BuildWorld, [&] { RunSide(true, seed); });
    ok = drawfx::Same(ctx, a.first, b.first, "font system", seed, a.second);
}

// The stand-ins of the world, for the scope of a test.
struct World {
    impfx::Imports imp;
    shape::StandIn director{0x1ae18d, &GetDirectorFrameworkObject, &FakeDirectorObject};
    shape::StandIn exists{0x1c1242, &cRZFile::PathExists, &StandPathExists};
    shape::StandIn convertBytes{0x1afc9d, &ConvertBytesToString, &StandConvertBytes};
    shape::StandIn convertString{0x1b025d, &RZText::ConvertStringToString, &StandConvertString};
    shape::StandIn scanNew{0x1c48da, &DirectoryScanAdapters::Construct, &StandScanNew};
    shape::StandIn scanPattern{0x1c63d4, &DirectoryScan::SetPattern, &StandScanPattern};
    shape::StandIn scanFiles{0x1c5bbd, &DirectoryScan::ListFiles, &StandScanFiles};
    shape::StandIn scanEnd{0x1c49eb, &DirectoryScanAdapters::Destruct, &StandScanEnd};
    shape::CodePatch fontConstructor{0x90a7db, reinterpret_cast<const void *>(&StandFontConstructor)};
    // Lane OC: ours makes the hand-made font with cGZFont::Create and reads files with cGZFontFile (src/gz/cGZFont.h),
    // so the same stand-ins go over those.
    shape::CodePatch fontCreate{shape::CodeOf(&cGZFont::Create), reinterpret_cast<const void *>(&StandFontCreate)};
    shape::StandIn fileNew{0x90a6c5, &cGZFontFileAdapters::Construct, &StandFileNew};
    shape::StandIn fileEnd{0x90a934, &cGZFontFileAdapters::Destruct, &StandFileEnd};
    shape::StandIn fileOpen{0x90d6f7, &cGZFontFile::Open, &StandFileOpen};
    shape::StandIn fileName{0x90ccd5, &cGZFontFile::Name, &StandFileName};
    shape::StandIn fileStyle{0x90d27e, &cGZFontFile::Style, &StandFileStyle};
    shape::StandIn detectKind{0x910b35, &cGZFontSys::DetectKind, &StandDetectKind}; // lane PD: ours too since DetectKind is ours
    explicit World(difftest::Context &ctx) : imp(ctx) { IMPFX_ADD(imp, GetWindowsDirectoryA).On(&OnWindowsDirectory); }
};

} // namespace

DIFF_TEST(GZFontSys_Slots, 0x910db5)
{
    // Every slot (QueryInterface 0x910ffb, AddRef 0x90ff98, Release 0x911043, Init 0x910db5, Shutdown 0x9111e3, the
    // setters, SetSizeRange 0x911142, GetFontByGroup 0x9109d4, LoadFont 0x91059a, FindFont 0x91025c, ReleaseFont
    // 0x910a26, AddFont 0x90fddc, AddFontFile 0x90fb04, AddFontDirectory 0x90f8c2, RemoveFamily 0x911066, ListFamilies
    // 0x9100c5, ListFonts 0x9101f1, AddAlias 0x90ffae, GetAliases 0x910148, HasFamily 0x910e90, ResolveAlias 0x910f4d,
    // the deleting destructor 0x90f8a3, the service face), the director's creator 0x909fd2 (odd seeds) or the
    // constructor 0x90f626 (even seeds), SetDefaultFace 0x910a9d and the destructor 0x90f85c. Also: SetFontRenderer
    // 0x91111b, GetServicePriority 0x910daf (the service face's slots 5 and 12), the service face's thunks Init 0x910e88
    // and Shutdown 0x9112b0, and the folded SetEncoding override 0x1f5250.
    World world(ctx);
    char seedText[16] = {};
    const uint32_t only = GetEnvironmentVariableA("TS2_TEST_SEED", seedText, sizeof(seedText)) ? uint32_t(atoi(seedText)) : 0;
    // A first use allocates (a 4 KB table in the game's text code) that the first side would keep: a throwaway case.
    for (bool ours : {false, true}) {
        NewCase(0);
        world.imp.BeginSide();
        drawfx::Side(&BuildWorld, [&] { RunSide(ours, 0); });
    }
    for (uint32_t seed = only ? only : 1; seed <= (only ? only : 600); ++seed) {
        bool ok = true;
        world.imp.BeginSide();
        RunCase(ctx, seed, ok);
        if (!ok) break;
    }
}

DIFF_TEST(GZFontSys_Construct, 0x90f626)
{
    // The constructor on a block of seeded bytes: every word it writes or leaves but the vtables, the padding after the
    // flag and the containers' heap pointers (compared by content), then the destructors (the original's 0x90f85c).
    World world(ctx);
    const auto original = ctx.Original<void *(__thiscall *)(void *)>(kConstructor);
    static uint8_t a[cGZFontSys::kSize], b[cGZFontSys::kSize];
    for (uint32_t seed = 0; seed < 100; ++seed) {
        drawfx::ResetAll();
        std::mt19937 rng(seed);
        for (auto &x : a) x = uint8_t(rng());
        std::memcpy(b, a, sizeof(a));
        drawfx::Cells cells;
        for (unsigned i = 0; i < 4; ++i) cells.Set(kPatternCell[i], static_cast<const void *>(kPatterns[i]));
        difftest::AllocationCounter heap;
        void *const ra = original(a);
        const int blocksA = int(heap.Blocks());
        void *const rb = cGZFontSysAdapters::Construct(b, nullptr);
        const int blocksB = int(heap.Blocks()) - blocksA;
        if (blocksA != blocksB) ctx.Fail("seed %u: the constructors allocate %d (orig) %d (ours) blocks", seed, blocksA, blocksB);
        ++ctx.cases;
        if (ra != a || rb != static_cast<void *>(b)) ctx.Fail("seed %u: the constructor returned another address", seed);
        if (Word(a, 0) != drawfx::P(GameAddress<void>(kGameVTable)) || Word(a, 4) != drawfx::P(GameAddress<void>(kGameVTable2)))
            ctx.Fail("seed %u: the original's vtables", seed);
        if (Word(b, 0) == drawfx::P(GameAddress<void>(kGameVTable)) || Word(b, 4) == drawfx::P(GameAddress<void>(kGameVTable2)))
            ctx.Fail("seed %u: ours has the game's vtables", seed);
        if ((Word(a, 8) & 0xff) != (Word(b, 8) & 0xff)) ctx.Fail("seed %u: the flag byte", seed);
        for (uint32_t off = 0xc; off < 0x28; off += 4)
            if (Word(a, off) != Word(b, off)) {
                ctx.Fail("seed %u: word +%#x orig %#x ours %#x", seed, off, Word(a, off), Word(b, off));
                break;
            }
        // The empty containers: size zero, the head pointing at itself, the head's colour and nil bytes alike.
        for (uint32_t tree : {0x28u, 0x30u, 0x38u, 0x40u, 0x54u}) {
            const uint32_t headA = Word(a, tree), headB = Word(b, tree);
            const uint32_t *const na = reinterpret_cast<const uint32_t *>(uintptr_t(headA));
            const uint32_t *const nb = reinterpret_cast<const uint32_t *>(uintptr_t(headB));
            if (Word(a, tree + 4) != 0 || Word(b, tree + 4) != 0 || na[0] != headA || na[1] != headA || na[2] != headA ||
                nb[0] != headB || nb[1] != headB || nb[2] != headB || (na[3] & 0xffff) != (nb[3] & 0xffff))
                ctx.Fail("seed %u: the container at +%#x is not an empty tree alike", seed, tree);
        }
        const cRZString *const pa = reinterpret_cast<const cRZString *>(uintptr_t(Word(a, 0x48)));
        const cRZString *const pb = reinterpret_cast<const cRZString *>(uintptr_t(Word(b, 0x48)));
        if (Word(a, 0x4c) - Word(a, 0x48) != Word(b, 0x4c) - Word(b, 0x48) ||
            Word(a, 0x50) - Word(a, 0x48) != Word(b, 0x50) - Word(b, 0x48) || Word(a, 0x4c) - Word(a, 0x48) != 4 * sizeof(cRZString))
            ctx.Fail("seed %u: the pattern vector's size or capacity", seed);
        else
            for (unsigned i = 0; i < 4; ++i)
                if (pa[i].mString != kPatterns[i] || pb[i].mString != kPatterns[i])
                    ctx.Fail("seed %u: pattern %u orig '%s' ours '%s'", seed, i, pa[i].mString.c_str(), pb[i].mString.c_str());
        const int before = int(heap.Blocks());
        ctx.Original<void(__thiscall *)(void *)>(kDestructor)(a);
        const int afterA = int(heap.Blocks());
        cGZFontSysAdapters::Destruct(reinterpret_cast<cGZFontSys *>(b), nullptr);
        if (before - afterA != afterA - int(heap.Blocks()))
            ctx.Fail("seed %u: the destructors free %d (orig) %d (ours) blocks", seed, before - afterA, afterA - int(heap.Blocks()));
        heap.Stop();
        if (heap.Blocks() != 0) ctx.Fail("seed %u: the constructors and destructors leave %d blocks", seed, int(heap.Blocks()));
        drawfx::ResetAll();
    }
}

DIFF_TEST(GZFontSys_SetDefaultFace, 0x910a9d)
{
    // SetDefaultFace (a new kind, or an existing one's name replaced; true) on constructed objects, original against
    // ours: the answer and the default-face map by content after every call. Only Init calls it in the class, once per
    // kind, so a replacement is reached here alone.
    const auto construct = ctx.Original<void *(__thiscall *)(void *)>(kConstructor);
    const auto setFace = ctx.Original<bool(__thiscall *)(void *, int, const cIGZString *)>(0x910a9d);
    static uint8_t a[cGZFontSys::kSize], b[cGZFontSys::kSize];
    for (uint32_t seed = 0; seed < 200; ++seed) {
        drawfx::Cells cells;
        for (unsigned i = 0; i < 4; ++i) cells.Set(kPatternCell[i], static_cast<const void *>(kPatterns[i]));
        std::mt19937 rng(seed);
        difftest::AllocationCounter heap;
        construct(a);
        cGZFontSys *const ours = cGZFontSysAdapters::Construct(b, nullptr);
        const cGZFontSys &orig = *reinterpret_cast<const cGZFontSys *>(a);
        const int kinds[] = {5, 9, 6, 10, 1, 0, -3, 0x7fffffff};
        for (int call = 0; call < 24; ++call) {
            const int kind = kinds[rng() % 8];
            std::string text;
            const uint32_t length = rng() % 3 ? rng() % 12 : rng() % 60;
            for (uint32_t i = 0; i < length; ++i) text += char('a' + rng() % 26);
            FontSysString name(text.c_str());
            const bool x = setFace(a, kind, I(name));
            const bool y = ours->SetDefaultFace(kind, *I(name));
            ++ctx.cases;
            if (x != y) ctx.Fail("seed %u call %d: the answers %d %d", seed, call, int(x), int(y));
            const cGZFontSys &mine = *ours;
            bool same = orig.mDefaults.size() == mine.mDefaults.size();
            for (auto i = orig.mDefaults.begin(), j = mine.mDefaults.begin(); same && i != orig.mDefaults.end(); ++i, ++j)
                same = i->first == j->first && i->second.s.mString == j->second.s.mString;
            if (!same) {
                ctx.Fail("seed %u call %d: the default faces differ", seed, call);
                break;
            }
        }
        ctx.Original<void(__thiscall *)(void *)>(kDestructor)(a);
        cGZFontSysAdapters::Destruct(ours, nullptr);
        heap.Stop();
        if (heap.Blocks() != 0) ctx.Fail("seed %u: %d blocks left", seed, int(heap.Blocks()));
    }
}
