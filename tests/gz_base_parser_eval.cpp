// Differential tests of cBaseParser's expression slots (lane HU; cIBaseParser face slots 30-39): each evaluates its
// text with the evaluator (0xb0a870, the game's on both sides) and checks the element count. Results, out buffers and
// the errors thrown (message and type) are compared (tests/gz_base_parser_slots.h).
#include "gz_base_parser_slots.h"

#include <bit>

using namespace hu;

namespace {

const char *const kExpressions[] = {
    "0", "1", "-1", "2.5", "3.5", "-2.5", "-0.5", "0.49999997", "1e3", "16777217", "8388609.5", "1e9", "-1e9", "1e30",
    "1+2", "2*3.25", "(1)", "1,2", "1,2,3", "1,2,3,4", "1,2,3,4,5", "(1,2)", "(1,2,3)", "(1,2,3,4)", "1 2", "1 2 3",
    "abc", "", "1+", "3.5 junk", "pow(2,10)", "sqrt(2)", "0/0", "1/0", "-1/0", "0.1,0.2,0.3", "-0", "0x10", "1e-40",
};

// Through a volatile: the compiler must not see the adapter behind the cast (it would assume the adapter's own return
// type, e.g. a bool zero-extended, and drop the test's masking of eax).
template <class Fn> const void *Ours(Fn fn)
{
    const void *volatile p = reinterpret_cast<const void *>(fn);
    return p;
}

std::string Floats(const float *v, int n)
{
    std::string s;
    char b[16];
    for (int i = 0; i < n; ++i) {
        std::snprintf(b, sizeof(b), "%08x,", std::bit_cast<uint32_t>(v[i]));
        s += b;
    }
    return s;
}

} // namespace

DIFF_TEST(cBaseParser_EvalScalars, 0xb03f30)
{
    // 0xb03f30 EvalBool, 0xb050c0 EvalFloat, 0xb05120 EvalInt (slots 32 and 33).
    for (const char *text : kExpressions) {
        for (int which = 0; which < 3; ++which) {
            const uint32_t rvas[3] = {0xb03f30, 0xb050c0, 0xb05120};
            const void *ours[3] = {Ours(&cBaseParserAdapters::EvalBool), Ours(&cBaseParserAdapters::EvalFloat), Ours(&cBaseParserAdapters::EvalInt)};
            auto run = [&](Side &s, const void *fn) -> uint32_t {
                if (which == 0) return CallFace<uint8_t>(fn, s.parser->Face(), text);
                if (which == 1) return std::bit_cast<uint32_t>(CallFace<float>(fn, s.parser->Face(), text));
                return static_cast<uint32_t>(CallFace<int32_t>(fn, s.parser->Face(), text));
            };
            char what[64];
            std::snprintf(what, sizeof(what), "slot %x '%s'", rvas[which], text);
            Compare(
                ctx, what, [](Side &) {}, [&](Side &s) { return run(s, ctx.image.At(rvas[which])); },
                [&](Side &s) { return run(s, ours[which]); });
        }
    }
}

DIFF_TEST(cBaseParser_EvalVectors, 0xb052e0)
{
    // 0xb052e0 EvalVector2, 0xb05370 EvalVector3, 0xb05410 EvalVector4 (minimum 0..5), 0xb05190 EvalColorRGBA,
    // 0xb05200 EvalColorARGB (slot 37 through the game's vtable), 0xb05260 EvalColorRGB.
    const uint32_t rvas[6] = {0xb052e0, 0xb05370, 0xb05410, 0xb05190, 0xb05200, 0xb05260};
    const void *ours[6] = {Ours(&cBaseParserAdapters::EvalVector2), Ours(&cBaseParserAdapters::EvalVector3), Ours(&cBaseParserAdapters::EvalVector4),
                           Ours(&cBaseParserAdapters::EvalColorRGBA), Ours(&cBaseParserAdapters::EvalColorARGB), Ours(&cBaseParserAdapters::EvalColorRGB)};
    for (const char *text : kExpressions) {
        for (int which = 0; which < 6; ++which) {
            for (uint32_t minimum = 0; minimum < (which < 3 ? 6u : 1u); ++minimum) {
                auto run = [&](Side &s, const void *fn) -> uint32_t {
                    float out[6] = {-7.0f, -7.0f, -7.0f, -7.0f, -7.0f, -7.0f};
                    struct Restore {
                        Side &s;
                        float *out;
                        ~Restore() { s.extra = "out=" + Floats(out, 6); }
                    } restore{s, out};
                    // The slots return `out` (callers copy from eax: the lighting script's commands, 0xb3db90 and
                    // others): compared as "returned out or not".
                    float *r;
                    if (which < 3) r = CallFace<float *>(fn, s.parser->Face(), out, text, minimum);
                    else r = CallFace<float *>(fn, s.parser->Face(), out, text);
                    return r == out ? 1u : 0u;
                };
                char what[64];
                std::snprintf(what, sizeof(what), "slot %x '%s' min %u", rvas[which], text, minimum);
                Compare(
                    ctx, what, [](Side &) {}, [&](Side &s) { return run(s, ctx.image.At(rvas[which])); },
                    [&](Side &s) { return run(s, ours[which]); });
            }
        }
    }
}
