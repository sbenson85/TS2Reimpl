#include "app/AppServices3.h"

#include "app/AppInit.h"
#include "game/vtable.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/cRZCmdLine.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"
#include "rz/cTSIniFile.h"

#include <new>

namespace appservices3 {

namespace {
constexpr uint32_t kIniPathOption = 0x6c253729;
constexpr int kFrameworkSlotCommandLine = 21;
constexpr int kCommandLineSlotArgC = 3;
constexpr int kCommandLineSlotInsertArgument = 13;
} // namespace

// 0xc2b078.
void __cdecl AppendIniCommandLine()
{
    cRZString path;
    path.Construct();
    appinit::CommonImplSlot4(kIniPathOption, reinterpret_cast<uint32_t>(&path), 1, 0, 0xffffffffu);
    if (cRZFile::PathExists(*path.AsInterface())) {
        cRZString section;
        section.Construct("CommandLine");
        alignas(cTSIniFile) unsigned char iniBytes[sizeof(cTSIniFile)];
        cTSIniFile *ini = reinterpret_cast<cTSIniFile *>(iniBytes);
        ini->Construct(*path.AsInterface(), nullptr);
        cRZString value;
        value.Construct();
        cRZString key;
        key.Construct("Append");
        bool found = ini->GetString(*section.AsInterface(), *key.AsInterface(), value.AsInterface());
        key.Destruct();
        if (found) {
            void *framework = GetServiceObject();
            void *commandLine = VirtualCall<void *(__thiscall *)(void *), kFrameworkSlotCommandLine>(framework);
            alignas(cRZCmdLine) unsigned char parsedBytes[sizeof(cRZCmdLine)];
            cRZCmdLine *parsed = reinterpret_cast<cRZCmdLine *>(parsedBytes);
            parsed->Construct(value.ToChar());
            for (int32_t i = 0; parsed->ArgC() > 0 && i < parsed->ArgC(); ++i) {
                cRZString argument;
                argument.Construct(parsed->ArgV(i));
                int32_t count = VirtualCall<int32_t(__thiscall *)(void *), kCommandLineSlotArgC>(commandLine);
                VirtualCall<bool(__thiscall *)(void *, const void *, int32_t), kCommandLineSlotInsertArgument>(commandLine, argument.AsInterface(), count);
                argument.Destruct();
            }
            parsed->Destruct();
        }
        value.Destruct();
        ini->Destruct();
        section.Destruct();
    }
    path.Destruct();
}

} // namespace appservices3
