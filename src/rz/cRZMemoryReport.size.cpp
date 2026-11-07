#include "rz/cRZMemoryReport.h"

#include "app/ReporterDump.h"
#include "game/game.h"

cRZMemoryHook *cRZMemoryHook::Construct(const char *)
{
    vtable = GameAddress<void>(kVTable);
    return this;
}

void cRZMemoryHook::Destruct()
{
    vtable = GameAddress<void>(kVTable);
}

cRZMemoryReport *cRZMemoryReport::Construct()
{
    vtable = GameAddress<void>(kVTable);
    lock.Construct();
    words14[0] = words14[1] = words14[2] = 0;
    flag4 = 0;
    // The original tests operator new's result before constructing (a null pointer is stored then); its operator new
    // throws instead of returning null, as ours does.
    *GameAddress<CountFromOne *>(kCountRva) = (new CountFromOne)->Construct();
    LoadGlobalDbgHelp();
    word20 = 0;
    enabled = 1;
    *GameAddress<cRZMemoryReport *>(kReportRva) = this;
    return this;
}

uint8_t __cdecl LoadGlobalDbgHelp()
{
    return GameAddress<appbase::DbgHelpTable>(appbase::DbgHelpTable::kGlobal)->Load();
}
