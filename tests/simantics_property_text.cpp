// Differential test for FormatPropertyText (0x5307b0, src/simantics/PropertyText.cpp): the display form of a property text.
// Every object it reaches is a fake with a recording vtable: the framework service (game global 0x10c9378: the property
// service query), the property service (parses the text it is given as a std::string whose storage comes from the allocator
// service, answers a typed value object), the value objects, the language service (cache global 0x10f51c8: the integer and
// float formatters) and its output string, the allocator service (cache 0x10c95d4, guard 0x10c95d8: logs sizes and frees).
// Both sides run the same fakes; the logs of all of them, the result byte and thrown errors are compared.
#include "difftest.h"

#include "game/game.h"
#include "rz/cRZString.h"
#include "simantics/TokenExpander.h"

#include <windows.h>

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kFrameworkGlobal = 0x10c9378, kLanguageCache = 0x10f51c8, kAllocCache = 0x10c95d4, kAllocGuard = 0x10c95d8;
constexpr uint32_t kTypeInt32 = 0x0c264712, kTypeUint32 = 0xeb61e4f7, kTypeFloat = 0xabc78708, kTypeText = 0x0b8bea18, kTypeOther = 0xcba908e1;

std::vector<uint32_t> gLog;
void L(uint32_t w) { gLog.push_back(w); }

// ---- the script of one run ----------------------------------------------------------------------------------------

struct Script {
    bool parseReturnsNull = false;
    int throwAt = 0;     // the Nth fake call that may throw does (0: none)
    int throwCalls = 0;  // calls so far
    uint32_t formatAnswer = 0;
    bool textObjectNull = false;
} gScript;

void MaybeThrow(uint32_t what)
{
    if (++gScript.throwCalls == gScript.throwAt) {
        L(0x7000u | what);
        throw 0x55;
    }
}

// ---- the allocator service -----------------------------------------------------------------------------------------

struct Block {
    void *p;
    uint32_t id;
    bool live;
};
Block gBlocks[32];
size_t gBlockCount = 0;
uint32_t gNextBlock = 1;

void *__fastcall AllocSlot(void *, void *, uint32_t size)
{
    void *p = std::malloc(size);
    if (gBlockCount < 32) gBlocks[gBlockCount++] = {p, gNextBlock, true};
    L(0x1000000u | size);
    L(0x1100000u | gNextBlock++);
    return p;
}
void __fastcall FreeSlot(void *, void *, void *p)
{
    uint32_t id = 0xdead;
    for (size_t i = 0; i < gBlockCount; ++i)
        if (gBlocks[i].p == p && gBlocks[i].live) {
            gBlocks[i].live = false;
            id = gBlocks[i].id;
        }
    L(0x2100000u | id);
    std::free(p);
}
void *gAllocVt[8] = {nullptr, nullptr, nullptr, reinterpret_cast<void *>(&AllocSlot), reinterpret_cast<void *>(&FreeSlot)};
struct Fake {
    void **vt;
    uint32_t data[8];
};
Fake gAllocService = {gAllocVt, {}};

// ---- the language service, its language and the output --------------------------------------------------------------

Fake gLanguageService, gLanguage;
void *gLangServiceVt[48], *gLanguageVt[32];

void *__fastcall GetLanguageSlot(void *, void *, uint32_t index)
{
    L(0x3800000u | index);
    return &gLanguage;
}

uint32_t __fastcall FormatIntegerSlot(void *, void *, int32_t low, int32_t high, void *out)
{
    L(0x4000000u);
    L(static_cast<uint32_t>(low));
    L(static_cast<uint32_t>(high));
    L(out == nullptr ? 0 : 1);
    MaybeThrow(18);
    return gScript.formatAnswer;
}

uint32_t __fastcall FormatRealSlot(void *, void *, double value, void *out, cRZString *format)
{
    L(0x4100000u);
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    L(static_cast<uint32_t>(bits));
    L(static_cast<uint32_t>(bits >> 32));
    L(out == nullptr ? 0 : 1);
    // The format: an object of the game's string class holding "%f".
    L(*reinterpret_cast<void *const *>(format) == GameAddress<void>(0xd9b470) ? 1 : 0);
    L(static_cast<uint32_t>(format->mString.size()));
    L(static_cast<uint32_t>(format->mString.capacity()));
    L(static_cast<uint32_t>(format->mRefCount));
    for (char c : format->mString) L(static_cast<uint8_t>(c));
    MaybeThrow(19);
    return gScript.formatAnswer;
}

// The output string object: its slot 4 (FromChar) records the text.
void *gOutVt[8];
void __fastcall FromCharSlot(void *, void *, const char *text)
{
    L(0x5000000u | static_cast<uint32_t>(std::strlen(text)));
    for (const char *p = text; *p; ++p) L(static_cast<uint8_t>(*p));
}

// A text object a text-typed value points to: slot 5 is ToChar.
struct FakeText {
    void **vt;
    const char *chars;
};
void *gTextVt[8];
std::string gTextContent;
const char *__fastcall ToCharSlot(FakeText *self, void *)
{
    L(0x6000000u);
    MaybeThrow(5);
    return self->chars;
}
FakeText gTextObject = {gTextVt, nullptr};

// ---- the property service and its value ----------------------------------------------------------------------------

struct Value {
    void **vt;
    uint32_t value;
};
void *gValueVt[8];
Value gValueObject = {gValueVt, 0};
uint32_t gValueType = 0;

uint32_t __fastcall GetTypeIdSlot(Value *, void *)
{
    L(0x8000000u);
    return gValueType;
}

struct RawText {
    char buffer[16]; // or the pointer
    uint32_t size, capacity;
};

Value *__fastcall ParseSlot(void *, void *, RawText *text)
{
    L(0x9000000u);
    L(text->size);
    L(text->capacity);
    const char *chars = text->capacity > 15 ? *reinterpret_cast<const char *const *>(text) : text->buffer;
    L(chars[text->size] == 0 ? 1 : 0);
    for (uint32_t i = 0; i < text->size; ++i) L(static_cast<uint8_t>(chars[i]));
    MaybeThrow(8);
    return gScript.parseReturnsNull ? nullptr : &gValueObject;
}

uint32_t __fastcall ReleaseSlot(void *, void *)
{
    L(0xa000000u);
    return 0;
}

void *gSettingsVt[16];
Fake gSettings;

uint32_t __fastcall QuerySlot(void *, void *, uint32_t first, uint32_t second, void **out)
{
    L(0xb000000u);
    L(first);
    L(second);
    *out = &gSettings;
    return 0;
}
void *gFrameworkVt[16];
Fake gFramework;

void BuildFakes()
{
    gAllocService.vt = gAllocVt;
    gLangServiceVt[38] = reinterpret_cast<void *>(&GetLanguageSlot);
    gLanguageVt[18] = reinterpret_cast<void *>(&FormatIntegerSlot);
    gLanguageVt[19] = reinterpret_cast<void *>(&FormatRealSlot);
    gLanguageService.vt = gLangServiceVt;
    gLanguage.vt = gLanguageVt;
    gOutVt[4] = reinterpret_cast<void *>(&FromCharSlot);
    gTextVt[5] = reinterpret_cast<void *>(&ToCharSlot);
    gValueVt[4] = reinterpret_cast<void *>(&GetTypeIdSlot);
    gSettingsVt[2] = reinterpret_cast<void *>(&ReleaseSlot);
    gSettingsVt[8] = reinterpret_cast<void *>(&ParseSlot);
    gSettings.vt = gSettingsVt;
    gFrameworkVt[5] = reinterpret_cast<void *>(&QuerySlot);
    gFramework.vt = gFrameworkVt;
}

// Puts the fakes into the image's globals while it lives.
struct Globals {
    void **framework, **language, **allocator;
    int *guard;
    void *savedFramework, *savedLanguage, *savedAllocator;
    int savedGuard;
    explicit Globals(difftest::Context &ctx)
        : framework(ctx.image.At<void *>(kFrameworkGlobal)), language(ctx.image.At<void *>(kLanguageCache)), allocator(ctx.image.At<void *>(kAllocCache)),
          guard(ctx.image.At<int>(kAllocGuard))
    {
        savedFramework = *framework;
        savedLanguage = *language;
        savedAllocator = *allocator;
        savedGuard = *guard;
        *framework = &gFramework;
        *language = &gLanguageService;
        *allocator = &gAllocService;
        *guard = INT32_MIN;
    }
    Globals(const Globals &) = delete;
    Globals &operator=(const Globals &) = delete;
    ~Globals()
    {
        *framework = savedFramework;
        *language = savedLanguage;
        *allocator = savedAllocator;
        *guard = savedGuard;
    }
};

struct Outcome {
    uint32_t result = 0;
    int thrown = 0;
    std::vector<uint32_t> log;
};

uint32_t Random32(std::mt19937 &r)
{
    const uint32_t edges[] = {0, 1, 0xffffffffu, 0x80000000u, 0x7fffffffu, 0x3f800000u, 0x7fc00000u, 0x7f800000u, 0xff800000u, 0x00000001u, 0x80000000u, 0xc2f6e979u, 0xdeadbeefu};
    return r() % 3 ? r() : edges[r() % 13];
}

using FormatFn = uint8_t(__cdecl *)(const char *, cRZString *);

} // namespace

DIFF_TEST(simantics_property_text, 0x5307b0)
{
    BuildFakes();
    Globals globals(ctx);
    FormatFn original = ctx.Original<FormatFn>(0x5307b0);
    FormatFn ours = &tokens::FormatPropertyText;
    uint32_t byType[6] = {}, parseNull = 0, thrown = 0, longText = 0, answered = 0, refused = 0, textNull = 0;
    for (uint32_t seed = 0; seed < 20000; ++seed) {
        Outcome out[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed * 2654435761u + 3);
            gLog.clear();
            gBlockCount = 0;
            gNextBlock = 1;
            gScript = Script();
            gScript.parseReturnsNull = r() % 10 == 0;
            gScript.throwAt = r() % 5 == 0 ? 1 + static_cast<int>(r() % 3) : 0;
            gScript.formatAnswer = r() % 2 ? (r() & ~0xffu) : (r() | 1u);
            std::string source;
            const uint32_t length = r() % 4 == 0 ? 14 + r() % 4 : r() % 40;
            for (uint32_t i = 0; i < length; ++i) source.push_back(static_cast<char>('a' + r() % 26));
            const uint32_t types[] = {kTypeInt32, kTypeUint32, kTypeFloat, kTypeText, kTypeOther, 0x12345678u};
            const uint32_t typeIndex = r() % 6;
            gValueType = types[typeIndex];
            gValueObject.value = Random32(r);
            gTextContent.clear();
            const uint32_t textLength = r() % 20;
            for (uint32_t i = 0; i < textLength; ++i) gTextContent.push_back(static_cast<char>('A' + r() % 26));
            gTextObject.chars = gTextContent.c_str();
            gScript.textObjectNull = r() % 4 == 0;
            if (gValueType == kTypeText) gValueObject.value = gScript.textObjectNull ? 0u : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gTextObject));

            struct OutObject {
                void **vt;
                uint8_t padding[0x30];
            } output = {gOutVt, {}};

            try {
                const uint32_t res = side == 0 ? original(source.c_str(), reinterpret_cast<cRZString *>(&output)) : ours(source.c_str(), reinterpret_cast<cRZString *>(&output));
                out[side].result = res & 0xff; // callers read al
            } catch (int) {
                out[side].thrown = 1;
            }
            out[side].log = gLog;
            for (size_t i = 0; i < gBlockCount; ++i) {
                if (gBlocks[i].live) {
                    out[side].log.push_back(0xeee00000u | gBlocks[i].id); // leaked block
                    std::free(gBlocks[i].p);
                }
            }
            if (side == 0) {
                const bool reached = !out[0].thrown && !gScript.parseReturnsNull;
                if (reached) ++byType[typeIndex];
                parseNull += gScript.parseReturnsNull;
                thrown += out[0].thrown;
                longText += length > 15;
                textNull += gValueType == kTypeText && gScript.textObjectNull && reached;
                if (reached && out[0].result != 0) ++answered;
                if (reached && out[0].result == 0) ++refused;
            }
        }
        ++ctx.cases;
        if (out[0].result != out[1].result || out[0].thrown != out[1].thrown || out[0].log != out[1].log)
            ctx.Fail("seed %u: original result %02x thrown %d (%zu log words), ours %02x thrown %d (%zu log words)", seed, out[0].result, out[0].thrown, out[0].log.size(), out[1].result,
                     out[1].thrown, out[1].log.size());
    }
    for (uint32_t i = 0; i < 6; ++i)
        if (!byType[i]) ctx.Fail("coverage: type %u never reached", i);
    if (!parseNull || !thrown || !longText || !answered || !refused || !textNull)
        ctx.Fail("coverage: parse null %u thrown %u long %u answered %u refused %u null text %u", parseNull, thrown, longText, answered, refused, textNull);
}
