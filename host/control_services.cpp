// The "services" control command (docs/roadmap.md, milestone I: "the service spine is ours"): a diagnostic that reads
// the framework's system services, changing nothing, and appends them to ts2services.txt next to the host log: per
// service its id, priority, tick/on-idle membership, class, every vtable of its faces (image RVA, or our module offset
// tagged "ours") and how many slots point into the image's code; last the line
// "N services: X ours, Y image, Z mixed (ours with image slots)", also logged. tools/services.py reads the file.
// The walk is host/shared/services.cpp (unit-tested on fakes, tests/host_services.cpp).
//
// Where the services are: g_Framework (0x10c9378) points at the framework (the cCTGFramework at 0x10fdb50: the game's
// object in mode 0, ours in mode 2); its containers are read through the layout view (gz/Framework.h: the priority
// multimap +0x84, the id map +0x8c, the on-idle list +0xac, the tick list +0xbc) with our STL's iteration (the same
// layout our framework code uses on the game's object). No framework slot is called, so any frame is safe, and the
// command runs on the main thread between messages, where no framework call is in progress.
#include "control.h"

#include "shared/services.h"

#include "game/game.h"
#include "gz/Framework.h"
#include "hook/log.h"

#include <windows.h>

#include <share.h>

#include <cstdio>
#include <string>

void Services(const std::wstring &directory, const char *label)
{
    auto *framework = *GameAddress<const ::cGZFramework *>(::cGZFramework::kGlobalFramework);
    if (!framework) {
        Log("control: services: no framework yet (frame %u)", ControlFrame());
        return;
    }
    services::Spaces spaces{services::ModuleAt(GameAddress<void>(0)), services::ModuleAt(GetModuleHandleW(nullptr))};
    const services::Lists lists{&framework->mServices, &framework->mServiceById, &framework->mTickList,
                                &framework->mIdleList};
    std::string text;
    const services::Summary summary = services::Dump(lists, spaces, text);

    // The framework object's own class and vtable, so the file says which framework was read.
    const services::Object self = services::Inspect(framework, spaces);
    char where[48] = "unknown";
    if (!self.faces.empty() && self.faces[0].where != services::Where::None)
        std::snprintf(where, sizeof(where), "%s:0x%x", self.faces[0].where == services::Where::Image ? "image" : "ours",
                      self.faces[0].vtable);
    char head[256];
    std::snprintf(head, sizeof(head), "services %u %s framework=%s %s\n", ControlFrame(), *label ? label : "-",
                  self.className.c_str(), where);

    const std::wstring path = directory + L"ts2services.txt";
    FILE *f = _wfsopen(path.c_str(), L"ab", _SH_DENYNO);
    if (!f) {
        Log("control: services: cannot open ts2services.txt");
        return;
    }
    std::fputs(head, f);
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
    Log("control: services \"%s\": %u services: %u ours, %u image, %u mixed (ours with image slots), %u unknown; "
        "%u listed only; idmap %u differ%s (frame %u, ts2services.txt)",
        label, summary.services, summary.ours, summary.image, summary.mixed, summary.unknown, summary.listedOnly,
        summary.idMapDiffer, summary.complete ? "" : "; containers not read to the end", ControlFrame());
}
