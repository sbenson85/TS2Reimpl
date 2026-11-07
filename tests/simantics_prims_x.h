#pragma once
// Shared pieces of the lane X primitive tests (src/simantics/PrimX.h): the recording-fake framework of
// tests/simantics_prims_mid.h plus the operand reader that answers from a sequence, string-logging fakes and the
// string-table loader stand-in.
#include "simantics_prims_mid.h"
#include "simantics/PrimX.h"
#include "ts_string_fake.h"

#include <windows.h>

namespace {

PrimXObject *SelfX(PrimMidObject *o)
{
    return reinterpret_cast<PrimXObject *>(o);
}

// The operand reader (secondary slot 7), answering from a sequence; gReadPos restarts for each run.
std::vector<uint32_t> gReadVals;
size_t gReadPos = 0;

uint32_t __fastcall ReadSeq(void *self, void *, uint32_t scope, uint32_t id, SimDataRef *dest, float **fdest, uint32_t *a5, uint32_t a6,
                            uint32_t a7)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(7u << 8 | 7u);
    gTrace.insert(gTrace.end(), {scope, id, dest ? 1u : 0u, fdest ? 1u : 0u, a5 ? 1u : 0u, a6, a7});
    return gReadPos < gReadVals.size() ? gReadVals[gReadPos++] : 0u;
}

uint32_t ReadWordX(std::mt19937 &r)
{
    return static_cast<uint32_t>(static_cast<uint16_t>(Edge16(r))) | (r() & 0xffff0000u);
}

// Logs a C string by content (the handlers pass pointers to their own stack text).
void LogString(const char *s)
{
    if (!s) {
        gTrace.push_back(0xdead5001u);
        return;
    }
    const size_t n = std::strlen(s);
    gTrace.push_back(static_cast<uint32_t>(n));
    for (size_t i = 0; i < n; ++i) gTrace.push_back(static_cast<uint8_t>(s[i]));
}

// ---- the string-table loader's resource manager (tests/ts_string_fake.h) ----------------------------------------------------------
//
// The loader (cTSString, 0x1d0df0) runs for real on both sides; the fake resource manager logs the key it is asked for and answers
// with a scripted text, so the log and the text are the same on both sides.

std::string gLoaderText;

struct LoaderRedirect {
    tsfake::ManagerFixture manager;
    explicit LoaderRedirect(difftest::Context &ctx) : manager(ctx)
    {
        manager.InstallManagerOnly();
        tsfake::gAnswerer = [](const tsfake::Ask &ask, std::string &text) {
            gTrace.push_back(0x10adu);
            gTrace.insert(gTrace.end(), {ask.instance >> 16, ask.instance & 0xffff, Norm(ask.group), static_cast<uint32_t>(ask.loadFlags) & 0xff, ask.type});
            text = gLoaderText;
            return true;
        };
    }
};

// Names the fakes hand out (static: the same address in both worlds).
const char *const kNames[] = {"Bella Goth", "", "x", "Obj \"quoted\" 100%", "A very long object name that goes on and on for a while"};
const char *const kFormats[] = {"%d", "a %d b %d c %d d %d", "", "plain text", "%d/%d", "[%d %d %d %d]", "%d %d %d"};

} // namespace
