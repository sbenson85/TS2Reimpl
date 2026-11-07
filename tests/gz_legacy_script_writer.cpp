// Differential tests of the legacy script writer's formatting helpers (lane FI, foundations): src/gz/cGZLegacyScriptWriter.h.
// The writer runs 0 times in game (GetScript is the editor's), so these are its evidence.
//
// LegacyScriptWriter_Format: each seed runs one of the pure formatters (0x86f25b..0x86f235, 0x86e546) or the escape
// 0x86ebb8 on both sides, with names from a pool (empty, short, past the 15-character small buffer), values drawn at
// random and at the edges, a prefilled script text, and flag tables built here. Compared: the script text's contents
// (not its capacity: the original's std::string grows by another rule) and the escape's copy.
//
// LegacyScriptWriter_Properties: the property readers (0x86e5f8, 0x86e655, 0x86e6f8, 0x87b080, 0x87b0d4) on a winfx window
// whose GetProperty (slot 126) hands out one stored value: none, a number, a text, an untyped byte array, a uint32 array, a
// signed number or an empty value, under the asked id or another; the UI defaults fake's slot 37 gives a font entry fake
// (slot 5 its name) or null. Compared: the result (al), the outputs, the stored value's count afterwards and the fakes' log.
#include "code_patch.h"
#include "difftest.h"
#include "legacy_win_faces.h"
#include "shape.h"
#include "ts_services_fixture.h"
#include "winfx.h"

#include "game/game.h"
#include "gz/cGZLegacyControl.h"
#include "gz/cGZLegacyScriptWriter.h"
#include "gz/cIGZString.h"
#include "ifaces/cIGZWin.h"
#include "rz/cIGZVariant.h"
#include "rz/cRZString.h"
#include "rz/cRZVariant.h"

#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace legacy_script_writer_test {

using svcfx::Blob;

constexpr uint32_t kDefaultsCell = 0x10f51b0;
constexpr uint32_t kUnnamedFontId = 0x68963c4c;
constexpr uint32_t kFontProperty = 0xfaa3be85;
constexpr uint32_t kImagePairProperty = 0xdaa3b860;

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }
uint32_t Edge()
{
    static const uint32_t kEdges[] = {0, 1, 0xffffffff, 0x80000000, 0x7fffffff, 255, 256, 0x12345678};
    return Draw(3) ? uint32_t(gRng()) : kEdges[Draw(8)];
}

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

void PushText(Blob &b, const std::string &s)
{
    b.push_back(uint32_t(s.size()));
    for (char c : s) b.push_back(uint8_t(c));
}

// ---- the formatters ------------------------------------------------------------------------------------------------------

const char *const kNames[] = {"", "image", "notify", "font", "a", "transparentbkg", "listelementwithalongname", "x y"};
const char *const kPrefills[] = {"", "<LEGACY ", "<LEGACY clsid=0x12345678 iid=0x9abcdef0 area=(0,0,10,10) "};
const char *const kTexts[] = {"",          "plain",       "a&b",         "<tag>",          "'\"",
                              "&&&&&&&&&&&&&&&&&&&&", "it's \"x\" & <y>", "&amp;",        "no specials here at all, ok"};

std::string RandomText()
{
    if (Draw(2)) return kTexts[Draw(std::size(kTexts))];
    static const char kChars[] = "ab &<>'\"z";
    std::string s(Draw(40), ' ');
    for (char &c : s) c = kChars[Draw(sizeof kChars - 1)];
    return s;
}

using TextFn = void(__cdecl *)(cRZString *, const cRZString *, const cRZString *);
using FlagsFn = void(__cdecl *)(cRZString *, uint32_t, uint32_t, const LegacyFlagName *);
using BoolFn = void(__cdecl *)(cRZString *, const cRZString *, bool);
using ChangedBoolFn = void(__cdecl *)(cRZString *, const cRZString *, bool, bool);
using ColorFn = void(__cdecl *)(cRZString *, const cRZString *, uint8_t, uint8_t, uint8_t);
using Color2Fn = void(__cdecl *)(cRZString *, const cRZString *, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t);
using Color4Fn = void(__cdecl *)(cRZString *, const cRZString *, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t,
                                 uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t);
using WordFn = void(__cdecl *)(cRZString *, const cRZString *, uint32_t);
using KeyFn = void(__cdecl *)(cRZString *, const cRZString *, uint32_t, uint32_t);
using IntFn = void(__cdecl *)(cRZString *, const cRZString *, int32_t);
using ImageItemFn = void(__cdecl *)(cRZString *, const cRZString *, uint32_t, const cRZString *, uint32_t, uint32_t,
                                    uint32_t, int32_t, int32_t, int32_t, int32_t);
using NumberedPairFn = void(__cdecl *)(cRZString *, const cRZString *, uint32_t, int32_t, int32_t);
using PairFn = void(__cdecl *)(cRZString *, const cRZString *, int32_t, int32_t);
using RectFn = void(__cdecl *)(cRZString *, const cRZString *, int32_t, int32_t, int32_t, int32_t);
using Key3Fn = void(__cdecl *)(cRZString *, const cRZString *, const uint32_t *);
using NameFn = void(__cdecl *)(cRZString *, const cRZString *);
using EscapeFn = cRZString *(__cdecl *)(cRZString *, const cRZString *);

constexpr unsigned kFormatters = 18;

DIFF_TEST(LegacyScriptWriter_Format, 0x86f25b)
{
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    for (uint32_t seed = 1; seed <= 6000; ++seed) {
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            gRng.seed(seed);
            const unsigned which = seed % kFormatters;
            LegacyText out, name, value;
            out.mText.FromChar(kPrefills[Draw(std::size(kPrefills))]);
            name.mText.FromChar(kNames[Draw(std::size(kNames))]);
            const std::string text = RandomText();
            value.mText.FromChar(text.c_str());
            uint32_t w[12];
            for (uint32_t &x : w) x = Edge();
            uint8_t c[12];
            for (uint8_t &x : c) x = uint8_t(gRng());
            const bool b0 = Draw(2) != 0, b1 = Draw(2) != 0;
            // A flag table of 0..6 rows (masks of one bit, several bits or none; some written always).
            static const char *const kFlagNames[] = {"notify", "transparentbkg", "edgeimage", "", "a long flag name here"};
            LegacyFlagName table[7];
            const unsigned rows = Draw(7);
            for (unsigned i = 0; i < rows; ++i)
                table[i] = {Draw(4) ? 1u << Draw(32) : uint32_t(gRng()) & (Draw(2) ? 0xffffffff : 0), kFlagNames[Draw(5)],
                            Draw(4) == 0};
            table[rows] = {uint32_t(gRng()), nullptr, Draw(2) != 0};
            auto *o = &out.mText;
            const cRZString *n = &name.mText;
            const auto i32 = [&](unsigned k) { return int32_t(w[k]); };
            switch (which) {
            case 0:
                if (side) AppendTextAttribute(o, n, &value.mText);
                else ctx.Original<TextFn>(0x86f25b)(o, n, &value.mText);
                break;
            case 1:
                if (side) AppendFlagAttributes(o, w[0], Draw(2) ? w[0] ^ w[1] : w[1], table);
                else ctx.Original<FlagsFn>(0x86e546)(o, w[0], Draw(2) ? w[0] ^ w[1] : w[1], table);
                break;
            case 2:
                if (side) AppendBoolAttribute(o, n, b0);
                else ctx.Original<BoolFn>(0x86ec88)(o, n, b0);
                break;
            case 3:
                if (side) AppendChangedBoolAttribute(o, n, b0, b1);
                else ctx.Original<ChangedBoolFn>(0x86ece8)(o, n, b0, b1);
                break;
            case 4:
                if (side) AppendColorAttribute(o, n, c[0], c[1], c[2]);
                else ctx.Original<ColorFn>(0x86ed50)(o, n, c[0], c[1], c[2]);
                break;
            case 5:
                if (side) AppendColor2Attribute(o, n, c[0], c[1], c[2], c[3], c[4], c[5]);
                else ctx.Original<Color2Fn>(0x86edad)(o, n, c[0], c[1], c[2], c[3], c[4], c[5]);
                break;
            case 6:
                if (side) AppendColor4Attribute(o, n, c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8], c[9], c[10], c[11]);
                else
                    ctx.Original<Color4Fn>(0x86ee19)(o, n, c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8], c[9], c[10],
                                                     c[11]);
                break;
            case 7:
                if (side) AppendHexAttribute(o, n, w[0]);
                else ctx.Original<WordFn>(0x86eea3)(o, n, w[0]);
                break;
            case 8:
                if (side) AppendKeyAttribute(o, n, w[0], w[1]);
                else ctx.Original<KeyFn>(0x86eef4)(o, n, w[0], w[1]);
                break;
            case 9:
                if (side) AppendIntAttribute(o, n, i32(0));
                else ctx.Original<IntFn>(0x86ef48)(o, n, i32(0));
                break;
            case 10: {
                const uint32_t group = Draw(3) ? w[1] : 0, instance = Draw(3) ? w[2] : 0;
                if (side) AppendImageItemAttribute(o, n, w[0], &value.mText, w[3], group, instance, i32(4), i32(5), i32(6), i32(7));
                else
                    ctx.Original<ImageItemFn>(0x86ef99)(o, n, w[0], &value.mText, w[3], group, instance, i32(4), i32(5), i32(6),
                                                        i32(7));
                break;
            }
            case 11:
                if (side) AppendNumberedPairAttribute(o, n, w[0], i32(1), i32(2));
                else ctx.Original<NumberedPairFn>(0x86f075)(o, n, w[0], i32(1), i32(2));
                break;
            case 12:
                if (side) AppendPairAttribute(o, n, i32(0), i32(1));
                else ctx.Original<PairFn>(0x86f0cc)(o, n, i32(0), i32(1));
                break;
            case 13:
                if (side) AppendRectAttribute(o, n, i32(0), i32(1), i32(2), i32(3));
                else ctx.Original<RectFn>(0x86f120)(o, n, i32(0), i32(1), i32(2), i32(3));
                break;
            case 14:
                if (side) AppendQuotedAttribute(o, n, &value.mText);
                else ctx.Original<TextFn>(0x86f17a)(o, n, &value.mText);
                break;
            case 15:
                if (side) AppendKey3Attribute(o, n, w);
                else ctx.Original<Key3Fn>(0x86f1dc)(o, n, w);
                break;
            case 16:
                if (side) AppendWordAttribute(o, n);
                else ctx.Original<NameFn>(0x86f235)(o, n);
                break;
            case 17: {
                alignas(cRZString) unsigned char raw[sizeof(cRZString)];
                std::memset(raw, 0xcd, sizeof raw);
                auto *copy = reinterpret_cast<cRZString *>(raw);
                cRZString *returned = side ? EscapeScriptText(copy, &value.mText)
                                           : ctx.Original<EscapeFn>(0x86ebb8)(copy, &value.mText);
                results[side].push_back(returned == copy);
                results[side].push_back(copy->mVTable == GameAddress<void>(cRZString::kVTable));
                results[side].push_back(copy->mRefCount);
                PushText(results[side], copy->mString);
                copy->Destruct();
                break;
            }
            }
            PushText(results[side], out.mText.mString);
            PushText(results[side], name.mText.mString); // the name and value are left as they were
            PushText(results[side], value.mText.mString);
        }
        if (!svcfx::Same(ctx, results[0], results[1], "LegacyScriptWriter_Format", seed)) break;
    }
}

// ---- the property readers -----------------------------------------------------------------------------------------------

winfx::World gWorld;
drawfx::Fake gDefaults, gFontEntry;
bool gNamed; // whether the defaults have an entry for the fonts asked (ids with bit 2 clear) this seed
uint32_t gStoreId;
cIGZVariant *gStored;

uint32_t GetStoredProperty(drawfx::Fake &, const uint32_t *a)
{
    drawfx::Call &call = drawfx::gLog.back();
    call.words[1] = 1; // the out pointer is a stack address
    if (a[0] != gStoreId || !gStored) return 0;
    gStored->AddRef();
    *reinterpret_cast<void **>(uintptr_t(a[1])) = gStored;
    return 1;
}
uint32_t FindFontEntry(drawfx::Fake &, const uint32_t *a)
{
    return gNamed && !(a[0] & 4) ? drawfx::P(&gFontEntry) : 0;
}

const char *const kFontNames[] = {"Arial Bold", "", "Comic Sans MS 12", "x"};
const char *const kValueTexts[] = {"Arial Bold 12", "0123456789abcdef", "a font name long past sixteen chars", "fontname"};

cRZVariant *MakeValue(unsigned kind, std::vector<uint8_t> &bytes)
{
    switch (kind) {
    case 0: return nullptr;
    case 1: return new cRZVariant(Draw(3) ? uint32_t(gRng()) : (Draw(2) ? kUnnamedFontId : Draw(16)));
    case 2: {
        LegacyText s;
        s.mText.FromChar(kValueTexts[Draw(std::size(kValueTexts))]);
        return new cRZVariant(*s.mText.AsInterface());
    }
    case 3: {
        // Untyped bytes: whole 16-byte entries or not, at least 8 (the pair reader reads two words unchecked).
        const uint32_t size = Draw(3) ? 16 * (1 + Draw(5)) : 8 + Draw(40);
        bytes.resize(size);
        for (uint8_t &b : bytes) b = uint8_t(gRng());
        auto *v = new cRZVariant;
        v->SetVoidArray(bytes.data(), size);
        return v;
    }
    case 4: {
        uint32_t words[16];
        for (uint32_t &x : words) x = gRng();
        auto *v = new cRZVariant;
        v->SetUint32Array(words, Draw(2) ? 16 : 2 + Draw(14));
        return v;
    }
    case 5: return new cRZVariant(int32_t(gRng()));
    default: return new cRZVariant;
    }
}

using FontIdFn = bool(__cdecl *)(uint32_t, cIGZString *);
using FontPropertyFn = bool(__cdecl *)(ifaces::cIGZWin *, cIGZString *, int32_t);
using ImagePairFn = bool(__cdecl *)(ifaces::cIGZWin *, uint32_t *, uint32_t *, int32_t);
using KeyPropertyFn = bool(__cdecl *)(ifaces::cIGZWin *, uint32_t, uint32_t *);
using KeyListFn = bool(__cdecl *)(ifaces::cIGZWin *, uint32_t, int32_t, uint32_t *);

DIFF_TEST(LegacyScriptWriter_Properties, 0x86e655)
{
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    globals.Set(kDefaultsCell, &gDefaults);
    for (uint32_t seed = 1; seed <= 5000; ++seed) {
        drawfx::Log logs[2];
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            drawfx::ResetAll();
            gRng.seed(seed);
            gDefaults.Reset<legacy_win_faces::kDefaultsFace>(560);
            gDefaults.On(37, &FindFontEntry);
            gFontEntry.Reset<legacy_win_faces::kFontEntryFace>(561);
            gFontEntry.Returns(5, kFontNames[Draw(std::size(kFontNames))]);
            gNamed = Draw(2) != 0;
            gWorld.Build(seed, 2);
            winfx::Win &w = gWorld[1];
            w.f.On(126, &GetStoredProperty);
            auto *window = reinterpret_cast<ifaces::cIGZWin *>(&w.f);
            const unsigned which = seed % 5;
            const int32_t index = int32_t(Draw(6)) - 1;
            const uint32_t ids[] = {0, kFontProperty + index, kImagePairProperty + index, 0x2b343bec, 0x6b4bcbd4};
            const uint32_t asked = which >= 3 ? ids[which] : 0;
            gStoreId = Draw(5) ? (which == 0 ? 0 : ids[which]) : gRng();
            std::vector<uint8_t> bytes;
            gStored = MakeValue(Draw(7), bytes);
            if (gStored) gStored->AddRef();
            LegacyText out;
            out.mText.FromChar(Draw(2) ? "" : "prefilled ");
            uint32_t words[16];
            for (uint32_t &x : words) x = gRng();
            bool result = false;
            ifaces::cIGZWin *target = Draw(12) ? window : nullptr; // the key readers take a null window
            switch (which) {
            case 0: {
                const uint32_t fontId = Draw(4) ? (Draw(2) ? kUnnamedFontId : Draw(16)) : uint32_t(gRng());
                result = side ? FormatFontId(fontId, out.mText.AsInterface())
                              : ctx.Original<FontIdFn>(0x86e5f8)(fontId, out.mText.AsInterface());
                break;
            }
            case 1:
                result = side ? FormatFontProperty(window, out.mText.AsInterface(), index)
                              : ctx.Original<FontPropertyFn>(0x86e655)(window, out.mText.AsInterface(), index);
                break;
            case 2:
                result = side ? GetImagePairProperty(window, &words[0], &words[1], index)
                              : ctx.Original<ImagePairFn>(0x86e6f8)(window, &words[0], &words[1], index);
                break;
            case 3:
                result = side ? GetKeyProperty(target, asked, words) : ctx.Original<KeyPropertyFn>(0x87b080)(target, asked, words);
                break;
            case 4: {
                const int32_t entry = int32_t(Draw(8)) - 2;
                result = side ? GetKeyListEntry(target, asked, entry, words)
                              : ctx.Original<KeyListFn>(0x87b0d4)(target, asked, entry, words);
                break;
            }
            }
            results[side].push_back(result);
            PushText(results[side], out.mText.mString);
            for (uint32_t x : words) results[side].push_back(x);
            if (gStored) {
                results[side].push_back(gStored->AddRef()); // the count after the call (each GetProperty AddRef released)
                gStored->Release();
                gStored->Release();
            }
            gStored = nullptr;
            logs[side] = drawfx::gLog;
        }
        if (!svcfx::Same(ctx, results[0], results[1], "LegacyScriptWriter_Properties", seed)) break;
        if (!drawfx::Same(ctx, logs[0], logs[1], "LegacyScriptWriter_Properties log", seed)) break;
    }
}

} // namespace legacy_script_writer_test
