#include "ts/cTSEffectCheats.h"

// The effects manager constructor's two cheat makers (lane YB).

// ORIGINAL-BUG: `cheats` (the manager's cheat list) is never used and the results are not kept, so both cheats leak and
// nothing registers them: `effect` and `effectsManager` never reach the cheat parser.
void __cdecl MakeEffectCheats(void *cheats, cITSEffectsManager *manager)
{
    (void)cheats;
    new cTSEffectCheat(manager);
    new cTSEffectsManagerCheat(manager);
}

// ORIGINAL-BUG: likewise, `overlay` is made, never registered, and leaks.
void __cdecl MakeOverlayCheat(void *cheats, cITSEffectsManager *manager)
{
    (void)cheats;
    new cOverlayCheat(manager);
}
