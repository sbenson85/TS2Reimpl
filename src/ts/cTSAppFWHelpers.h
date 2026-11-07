#pragma once
// The application's free helpers (lane MB, foundations F4e): __cdecl functions only cTSAppFW calls. RVAs 1.18.0.187.
// Its member helpers (the exception uploader, the fatal report, the crash data, the neighbourhood files' unlocking) are
// declared in ts/cTSAppFW.h.
#include <cstdint>

class cIGZString;

namespace appfw {

// 0xc27961: `out` = `source` up to and including its first "2" (the whole text cleared when there is none), then the
// edition suffix of `language` (the language service's slot 7 id): " Legacy" for most, a translated word for 3, 4, 6,
// 7, 10, 14 and 16 (Latin-1 texts), "??" placeholders for 17 to 19.
void __cdecl AppendEditionSuffix(const cIGZString *source, cIGZString *out, uint32_t language);

// 0xb417c0: `out` assigned the Body Shop product name of the current language (one of twenty cRZString globals at
// 0x10fb08c, 0x20 apart; the first for languages without their own). Returns 1.
bool __cdecl BodyShopProductName(cIGZString *out);

// 0xc27b49: the process's PagefileUsage (GetProcessMemoryInfo from psapi.dll, loaded once on first use, not on the
// Win9x line); 0 when psapi or the call is not there.
uint32_t __cdecl ProcessPagefileUsage();

// 0x1b1a3f: _spawnv(_P_WAIT, path, argv): runs the program and waits for it; its exit code (-1 when it could not run).
intptr_t __cdecl SpawnAndWait(const char *path, const char *const *argv);

// 0xc2896d: whether the user's token (the thread's, else the process's) passes an access check against a security
// descriptor granting the Administrators group access; 1 on the Win9x line (no tokens there), 0 when any step fails.
// Init calls it and ignores the answer.
uint32_t __cdecl IsUserAdministrator();

} // namespace appfw
