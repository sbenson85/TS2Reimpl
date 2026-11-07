#include "hook/hooks.h"

// The registry alone (no installer), so the replay harness can map RVAs to replacements too.
std::vector<const HookSpec *> &RegisteredHooks()
{
    static std::vector<const HookSpec *> hooks;
    return hooks;
}
