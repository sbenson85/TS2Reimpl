// Differential test for cBaseParser's vtordisp thunks (lane IM; src/gz/cParserThunks.h, src/hook/tables/cParserThunks.cpp).
//
// For every thunk: the image's thunk is `sub ecx, [ecx-4]; jmp body` (its bytes are checked and the body read from the jump),
// the body's registered hook is the adapter this list names (so the list matches the image), and the thunk's registered hook
// (the forwarder the game would run) is what our side calls. The body is stood in for on both sides (the image's body and our
// adapter) by a recorder of the slot's signature that logs the `this` it gets (as an offset in the test's buffer) and every
// argument, and returns a value of the slot's type. Each thunk is called on a face with a vtordisp word before it (0, small
// and large, negative, random) and random arguments: the log and the result compared.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"

#include "gz/cParserThunks.h"
#include "hook/hooks.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <tuple>
#include <type_traits>

namespace cp = nGZCommandParser;
using A = cp::cBaseParserAdapters;

namespace {

uint32_t P(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

alignas(16) uint8_t gBuffer[0x400];
std::string gSeen;
uint32_t gResult = 0;

std::string Hex(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof(b), "%x", v);
    return b;
}

template <class T> uint32_t WordOf(T v)
{
    if constexpr (std::is_pointer_v<T>) return P(v);
    else return static_cast<uint32_t>(v);
}

template <class T> T Make(std::mt19937 &g)
{
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(static_cast<uintptr_t>(g()));
    else if constexpr (std::is_same_v<T, bool>) return (g() & 1) != 0;
    else return static_cast<T>(g());
}

template <class R> R Result()
{
    if constexpr (std::is_void_v<R>) return;
    else if constexpr (std::is_pointer_v<R>) return reinterpret_cast<R>(static_cast<uintptr_t>(gResult));
    else if constexpr (std::is_same_v<R, bool>) return (gResult & 1) != 0;
    else return static_cast<R>(gResult);
}

// The stand-in for a body: logs `this` (from the buffer) and the arguments.
template <auto Adapter> struct Recorder;
template <class R, class... Args, R(__fastcall *Adapter)(void *, void *, Args...)> struct Recorder<Adapter> {
    static R __fastcall Call(void *self, void *, Args... args)
    {
        gSeen = "this " + Hex(P(self) - P(gBuffer));
        ((gSeen += " " + Hex(WordOf(args))), ...);
        return Result<R>();
    }
};

const HookSpec *HookAt(uint32_t rva)
{
    for (const HookSpec *h : RegisteredHooks())
        if (h->rva == rva) return h;
    return nullptr;
}

template <class R> std::string ResultText(R r)
{
    if constexpr (std::is_same_v<R, float>) {
        uint32_t bits;
        std::memcpy(&bits, &r, 4);
        return "f" + Hex(bits);
    } else return Hex(WordOf(r));
}

template <auto Adapter> struct Check;
template <class R, class... Args, R(__fastcall *Adapter)(void *, void *, Args...)> struct Check<Adapter> {
    static void Run(difftest::Context &ctx, uint32_t thunk, const char *name, std::mt19937 &g)
    {
        using Fn = R(__fastcall *)(void *, void *, Args...);
        const uint8_t *code = ctx.image.At<uint8_t>(thunk);
        if (!(code[0] == 0x2b && code[1] == 0x49 && code[2] == 0xfc && code[3] == 0xe9)) {
            ctx.Fail("%s %x: not a vtordisp thunk", name, thunk);
            return;
        }
        int32_t rel;
        std::memcpy(&rel, code + 4, 4);
        const uint32_t body = thunk + 8 + rel;
        const HookSpec *bodyHook = HookAt(body), *thunkHook = HookAt(thunk);
        if (!bodyHook || bodyHook->replacement != reinterpret_cast<void *>(Adapter)) {
            ctx.Fail("%s %x: the body %x is not hooked to this adapter", name, thunk, body);
            return;
        }
        if (!thunkHook) {
            ctx.Fail("%s %x: the thunk is not hooked", name, thunk);
            return;
        }
        shape::StandIn stand(body, Adapter, &Recorder<Adapter>::Call);
        const int32_t fixed[] = {0, 4, 8, 0x10, -8, -0x14, 0x1b8, -0x100};
        for (int i = 0; i < 48; ++i) {
            const int32_t d = i < 8 ? fixed[i] : static_cast<int32_t>(g() % 129) * 4 - 0x100;
            uint8_t *face = gBuffer + 0x200;
            std::memcpy(face - 4, &d, 4);
            const std::tuple<Args...> args{Make<Args>(g)...};
            gResult = g();
            std::string got[2];
            for (int side = 0; side < 2; ++side) {
                gSeen = "not reached";
                const Fn fn = side == 0 ? ctx.Original<Fn>(thunk) : reinterpret_cast<Fn>(thunkHook->replacement);
                if constexpr (std::is_void_v<R>) {
                    std::apply([&](Args... a) { fn(face, nullptr, a...); }, args);
                    got[side] = gSeen;
                } else {
                    const R r = std::apply([&](Args... a) { return fn(face, nullptr, a...); }, args);
                    got[side] = gSeen + " -> " + ResultText(r);
                }
            }
            ++ctx.cases;
            if (got[0] != got[1]) ctx.Fail("%s %x vtordisp %d: %s / %s", name, thunk, d, got[0].c_str(), got[1].c_str());
        }
    }
};

} // namespace

// Every thunk of the two faces (+0x1b0: 0xe7db3c slots 0..2; +0x1b8: 0xe7db4c slots 0..50 but the folded 11 and 45, and 33
// shares 32's), each with its body's adapter.
DIFF_TEST(cBaseParser_VtordispThunks, 0xb0286f)
{
    std::mt19937 g(71);
#define X(rva, adapter) Check<&A::adapter>::Run(ctx, rva, #adapter, g);
    X(0xb05933, QueryInterface)
    X(0xaff0f3, AddRef)
    X(0xb05c43, Release)
    X(0xb0286f, Initialise)
    X(0xb0674e, Shutdown)
    X(0xb06107, SetVersionRange)
    X(0xb0621b, SetContextState)
    X(0xb059d5, RegisterCommand)
    X(0xb0729c, UnregisterCommand)
    X(0xb07294, UnregisterCommandObject)
    X(0xb05b40, RegisterMetaCommand)
    X(0xb05e98, RestartViaReset)
    X(0xaff10e, Reset)
    X(0xb03e44, ResetBlocks)
    X(0xb04485, RunFile)
    X(0xb04934, RunFiles)
    X(0xb046a7, RunText)
    X(0xb03f92, RunLine)
    X(0xb068a7, PushOpenBlock)
    X(0xb068dc, PushMetaBlock)
    X(0xaff878, PopMetaBlock)
    X(0xb06345, RunExpandedText)
    X(0xb05764, PushNamespace)
    X(0xb0559e, PopNamespace)
    X(0xb05631, PurgeNamespace)
    X(0xb02438, GetFunctions)
    X(0xb0623d, SetOutput)
    X(0xb06304, SetHandler)
    X(0xb0612e, SetSource)
    X(0xb07516, FindVariable)
    X(0xaff4c1, GetNamespacePath)
    X(0xb06f9d, ExpandVariables)
    X(0xb03f23, EvalBool)
    X(0xb050ae, EvalFloat)
    X(0xb05109, EvalInt)
    X(0xb052d0, EvalVector2)
    X(0xb05367, EvalVector3)
    X(0xb05402, EvalVector4)
    X(0xb05182, EvalColorRGBA)
    X(0xb051f2, EvalColorARGB)
    X(0xb05256, EvalColorRGB)
    X(0xb03e16, CommandCount)
    X(0xaff2d3, CommandAt)
    X(0xb035a7, IsInitialised)
    X(0xb03593, IsCaseSensitive)
    X(0xb035b7, GetText)
    X(0xb04677, GetFileName)
    X(0xb04661, GetLine)
    X(0xaff4df, HasDefinition)
    X(0xaff2ed, HasCommand)
    X(0xb06892, GetSource)
#undef X
}
