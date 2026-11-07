#include "app/AppCOMDirectors.h"

#include "app/AppInit.h"
#include "game/vtable.h"
#include "rz/COMDirectors.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"

namespace appcom {

namespace {

constexpr int kFrameworkSlotCommandLine = 21; // framework service object: the command line
constexpr int kCmdLineSlotSwitchPresent = 9;  // cIGZCmdLine::IsSwitchPresent(const cIGZString &)
constexpr int kDirectorSlotOnStart = 4;

// The string of the switch test, destroyed when an exception leaves the call (the original's unwind
// action for the state in which the string exists).
struct SwitchName {
    cRZString text;
    SwitchName() { text.Construct("nosound"); }
    SwitchName(const SwitchName &) = delete;
    ~SwitchName() { text.Destruct(); }
};

void AddToRoot(rz::cRZCOMDllDirector *director)
{
    // The root director's AddDirector (the getter is called first, then GetCOMDirector, as in the original).
    reinterpret_cast<rz::cRZCOMDllDirector *>(GetCOMDirector())->AddDirector(director);
}

// Slot 4 (OnStart) of `director` with the root director's framework object; the vtable is read first.
void StartWithFramework(void *director)
{
    auto start = VirtualSlot<bool(__thiscall *)(void *, void *)>(director, kDirectorSlotOnStart);
    start(director, GetDirectorFrameworkObject());
}

} // namespace

void AddAudioDirector()
{
    void *framework = GetServiceObject();
    using GetCmdLine = void *(__thiscall *)(void *);
    void *commandLine = VirtualCall<GetCmdLine, kFrameworkSlotCommandLine>(framework);
    bool present;
    {
        SwitchName name;
        using IsSwitch = uint32_t(__thiscall *)(void *, const void *);
        present = static_cast<uint8_t>(VirtualCall<IsSwitch, kCmdLineSlotSwitchPresent>(commandLine, &name.text)) != 0;
    }
    if (present) return;
    comdir::AudioDirector *audio = comdir::GetAudioDirector();
    AddToRoot(audio->AsShape());
    StartWithFramework(comdir::GetAudioDirector());
}

} // namespace appcom
