#pragma once
// For tests that ask "was p freed by the call under test" through a helper (Live(p), IsFreed(p)): BeginFrees()
// right before the call starts a difftest::FreeRecorder (replacing the previous one); FreedSinceBegin(p) stops it
// and asks it. Exact, unlike HeapValidate on a block that may have been freed and reused (docs/lane/test.md).
// Read the answers for one call before the next BeginFrees(): the recorder's results are global.
#include "free_recorder.h"

#include <cstdlib>
#include <optional>

namespace difftest {

inline std::optional<FreeRecorder> &CurrentFrees()
{
    static std::optional<FreeRecorder> recorder;
    return recorder;
}

inline void BeginFrees()
{
    CurrentFrees().reset();
    CurrentFrees().emplace();
}

inline bool FreedSinceBegin(const void *p)
{
    std::optional<FreeRecorder> &recorder = CurrentFrees();
    if (!recorder) std::abort(); // a test asked without BeginFrees(): a test bug, not an answer
    recorder->Stop();
    return recorder->Freed(p);
}

} // namespace difftest
