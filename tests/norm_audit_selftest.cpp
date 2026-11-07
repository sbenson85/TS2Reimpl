// Self-test of the normalisation audit (tests/norm_audit.h, lane LU) for drawfx, which still guesses pointers by value: a
// data word that lies inside one side's registered block is normalised on that side only, and drawfx::Same must name it.
// (svcfx no longer guesses: tests/norm_words_selftest.cpp.)
#include "difftest.h"
#include "drawfx_core.h"
#include "norm_audit.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

alignas(16) uint8_t gBlockA[0x40];
alignas(16) uint8_t gBlockB[0x40];
constexpr uint32_t kTag = 0x7a000000;

bool Contains(const std::vector<std::string> &lines, const char *text)
{
    for (const std::string &l : lines)
        if (l.find(text) != std::string::npos) return true;
    return false;
}

} // namespace

DIFF_TEST(NormAudit_SelfTest, 0)
{
    bool hooked = false;
    for (const difftest::FailureNotes &s : difftest::FailureNoteSources()) hooked |= s.report == &normaudit::Report;
    if (!hooked) ctx.Fail("the audit is not registered with the runner");

    normaudit::Begin();
    const uint32_t data = drawfx::P(gBlockA) + 8;
    drawfx::Log logs[2];
    for (int side = 0; side < 2; ++side) {
        drawfx::ResetAll();
        drawfx::AddRange(side ? gBlockB : gBlockA, sizeof(gBlockA), kTag);
        drawfx::Call c;
        c.note = "probe";
        c.words = {7, drawfx::Norm(data)};
        logs[side].push_back(c);
    }
    ++ctx.cases;
    {
        difftest::Context scratch{ctx.image};
        drawfx::Same(scratch, logs[0], logs[1], "probe", 3);
        if (!Contains(scratch.failures, "a data word normalised as a pointer on one side only"))
            ctx.Fail("drawfx::Same did not explain the one-sided word");
    }
    drawfx::ResetAll();
    normaudit::Begin();
}
