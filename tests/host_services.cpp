// The system services dump (host/shared/services.cpp; the "services" control command, host/control_services.cpp) on
// fakes: the image's own RTTI and vtables for an image object (cCTGFramework's three faces, entry counts from
// db/classes.jsonl), a C++ class of ours with two faces, and hand-built RTTI in our module's data for the cases the
// compiler doesn't make on demand (a vtable of ours with an image slot, a virtual base with a vtordisp, no RTTI, a vtable
// in neither module). Not a differential test (no original function): rva 0.
#include "difftest.h"

#include "../host/shared/services.h"

#include "game/game.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace hostservices_test {

// A class of ours with two faces (+0 and +4), as a service with a hooks face would be.
struct IUnknownLike {
    virtual bool QueryInterface(uint32_t, void **) = 0;
    virtual uint32_t AddRef() = 0;
    virtual uint32_t Release() = 0;
};
struct IServiceLike : IUnknownLike {
    virtual bool Init() = 0;
};
struct IHooksLike : IUnknownLike {
    virtual bool PreInit() = 0;
};
struct OurService : IServiceLike, IHooksLike {
    bool QueryInterface(uint32_t, void **) override { return false; }
    uint32_t AddRef() override { return 1; }
    uint32_t Release() override { return 1; }
    bool Init() override { return true; }
    bool PreInit() override { return true; }
    uint32_t mData = 0;
};

} // namespace hostservices_test

namespace {

using namespace hostservices_test;

void __fastcall OurSlot(void *, void *) {}

uint32_t A(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
uint32_t OurOffset(const void *p) { return A(p) - A(GetModuleHandleW(nullptr)); }

services::Spaces RealSpaces()
{
    return services::Spaces{services::ModuleAt(GameAddress<void>(0)), services::ModuleAt(GetModuleHandleW(nullptr))};
}

// An image object: cCTGFramework's vtables (db/classes.jsonl: 0xe995cc at +0, 49 entries; 0xe99694 at +4, 7; 0xe996b4
// at +0x180, 4), nothing else filled in.
uint32_t gImageObject[0x198 / 4];
void FillImageObject()
{
    for (uint32_t &w : gImageObject) w = 0;
    gImageObject[0] = A(GameAddress<void>(0xe995cc));
    gImageObject[1] = A(GameAddress<void>(0xe99694));
    gImageObject[0x180 / 4] = A(GameAddress<void>(0xe996b4));
}

// Hand-built RTTI in our module (x86 layouts: type descriptor {vftable, spare, name}; locator {0, offset, cdOffset,
// type, hierarchy}; hierarchy {0, attributes, count, array}; base descriptor {type, contained, mdisp, pdisp, vdisp,
// attributes}).
struct TypeDescriptor {
    uint32_t vftable, spare;
    char name[32];
};
TypeDescriptor gMixedType{0, 0, ".?AVcFakeMixed@@"};
TypeDescriptor gVirtualType{0, 0, ".?AVcFakeVirtual@@"};
TypeDescriptor gOtherType{0, 0, ".?AVcFakeOther@@"};
uint32_t gMixedBases[2][6], gMixedArray[2], gMixedHierarchy[4], gMixedLocator0[5], gMixedLocator8[5];
uint32_t gVirtualBases[2][6], gVirtualArray[2], gVirtualHierarchy[4], gVirtualLocator0[5], gVirtualLocatorC[5];
uint32_t gOtherLocator[5];
// Vtables: [0] the locator pointer, then the slots, then 0.
uint32_t gMixedVt0[5], gMixedVt8[3], gVirtualVt0[3], gVirtualVtC[3], gNoRttiVt[4], gOtherVt[3];
int32_t gVbtable[2];
uint32_t gMixedObject[4], gVirtualObject[4], gNoRttiObject[2], gOtherObject[4];

void Locator(uint32_t *locator, int32_t offset, int32_t cdOffset, const TypeDescriptor &type, const uint32_t *hierarchy)
{
    locator[0] = 0, locator[1] = static_cast<uint32_t>(offset), locator[2] = static_cast<uint32_t>(cdOffset);
    locator[3] = A(&type), locator[4] = A(hierarchy);
}

void Base(uint32_t *base, const TypeDescriptor &type, int32_t mdisp, int32_t pdisp, int32_t vdisp)
{
    base[0] = A(&type), base[1] = 0, base[2] = static_cast<uint32_t>(mdisp), base[3] = static_cast<uint32_t>(pdisp);
    base[4] = static_cast<uint32_t>(vdisp), base[5] = 0;
}

void FillFakes()
{
    const uint32_t ours = A(reinterpret_cast<void *>(&OurSlot));
    const uint32_t image = A(GameAddress<void>(0x142700)); // cIGZFrameWorkW32::AsIGZFrameWork's folded body: image code

    // cFakeMixed: faces +0 (our vtable: our slot, an image slot, our slot) and +8 (one slot of ours).
    Base(gMixedBases[0], gMixedType, 0, -1, 0);
    Base(gMixedBases[1], gOtherType, 8, -1, 0);
    gMixedArray[0] = A(gMixedBases[0]), gMixedArray[1] = A(gMixedBases[1]);
    gMixedHierarchy[0] = 0, gMixedHierarchy[1] = 0, gMixedHierarchy[2] = 2, gMixedHierarchy[3] = A(gMixedArray);
    Locator(gMixedLocator0, 0, 0, gMixedType, gMixedHierarchy);
    Locator(gMixedLocator8, 8, 0, gMixedType, gMixedHierarchy);
    gMixedVt0[0] = A(gMixedLocator0), gMixedVt0[1] = ours, gMixedVt0[2] = image, gMixedVt0[3] = ours, gMixedVt0[4] = 0;
    gMixedVt8[0] = A(gMixedLocator8), gMixedVt8[1] = ours, gMixedVt8[2] = 0;
    gMixedObject[0] = A(&gMixedVt0[1]), gMixedObject[1] = 0x1234, gMixedObject[2] = A(&gMixedVt8[1]), gMixedObject[3] = 0;

    // cFakeVirtual: face +0, and a virtual base found through the vbtable at +4 (displacement 8: 4 + 8 = +0xc), whose
    // locator says offset 8 with a vtordisp of 4 at face - 4 (+8): complete = face - (8 + 4).
    Base(gVirtualBases[0], gVirtualType, 0, -1, 0);
    Base(gVirtualBases[1], gOtherType, 0, 4, 4);
    gVirtualArray[0] = A(gVirtualBases[0]), gVirtualArray[1] = A(gVirtualBases[1]);
    gVirtualHierarchy[0] = 0, gVirtualHierarchy[1] = 3, gVirtualHierarchy[2] = 2, gVirtualHierarchy[3] = A(gVirtualArray);
    Locator(gVirtualLocator0, 0, 0, gVirtualType, gVirtualHierarchy);
    Locator(gVirtualLocatorC, 8, 4, gVirtualType, gVirtualHierarchy);
    gVirtualVt0[0] = A(gVirtualLocator0), gVirtualVt0[1] = ours, gVirtualVt0[2] = 0;
    gVirtualVtC[0] = A(gVirtualLocatorC), gVirtualVtC[1] = ours, gVirtualVtC[2] = 0;
    gVbtable[0] = -4, gVbtable[1] = 8;
    gVirtualObject[0] = A(&gVirtualVt0[1]), gVirtualObject[1] = A(gVbtable), gVirtualObject[2] = 4;
    gVirtualObject[3] = A(&gVirtualVtC[1]);

    // No locator (the word before the vtable is 0): its only face, class "?". Two slots of ours.
    gNoRttiVt[0] = 0, gNoRttiVt[1] = ours, gNoRttiVt[2] = ours, gNoRttiVt[3] = 0;
    gNoRttiObject[0] = A(&gNoRttiVt[1]), gNoRttiObject[1] = 0;

    // cFakeOther: a class whose hierarchy (cFakeMixed's) puts a base at +8, where a cFakeMixed vtable sits (a member
    // object of another class): not one of its faces.
    Locator(gOtherLocator, 0, 0, gOtherType, gMixedHierarchy);
    gOtherVt[0] = A(gOtherLocator), gOtherVt[1] = ours, gOtherVt[2] = 0;
    gOtherObject[0] = A(&gOtherVt[1]), gOtherObject[1] = 0, gOtherObject[2] = A(&gMixedVt8[1]), gOtherObject[3] = 0;
}

std::string Faces(const services::Object &o)
{
    std::string s;
    char text[128];
    for (const services::Face &f : o.faces) {
        std::snprintf(text, sizeof(text), "[+0x%x %d 0x%x %u/%u", static_cast<uint32_t>(f.offset), static_cast<int>(f.where),
                      f.vtable, f.slots, f.imageSlots);
        s += text;
        for (const auto &at : f.imageAt) {
            std::snprintf(text, sizeof(text), " %u:0x%x", at.first, at.second);
            s += text;
        }
        s += "]";
    }
    return s;
}

// The containers hold FwRefs; the fakes have no AddRef/Release, so the references are set and cleared by hand.
struct Containers {
    std::multimap<int32_t, FwServiceEntry> services;
    std::unordered_map<uint32_t, FwRef> byId;
    std::list<FwRef> tick, idle;

    void Add(int32_t priority, uint32_t id, const void *object)
    {
        auto it = services.emplace(priority, FwServiceEntry{id, FwRef()});
        it->second.mService.mObject = const_cast<void *>(object);
    }
    void Id(uint32_t id, const void *object) { byId[id].mObject = const_cast<void *>(object); }
    static void Push(std::list<FwRef> &list, const void *object)
    {
        list.emplace_back();
        list.back().mObject = const_cast<void *>(object);
    }
    ~Containers()
    {
        for (auto &e : services) e.second.mService.mObject = nullptr;
        for (auto &e : byId) e.second.mObject = nullptr;
        for (auto &r : tick) r.mObject = nullptr;
        for (auto &r : idle) r.mObject = nullptr;
    }
};

} // namespace

DIFF_TEST(HostServices_Inspect, 0)
{
    const services::Spaces spaces = RealSpaces();
    FillImageObject();
    FillFakes();
    using services::Verdict;
    auto check = [&](const char *what, const void *face, const char *className, int32_t at, const void *complete,
                     Verdict verdict, const std::string &faces) {
        const services::Object o = services::Inspect(face, spaces);
        if (o.className != className || o.at != at || o.complete != reinterpret_cast<uintptr_t>(complete)
            || o.verdict != verdict || Faces(o) != faces)
            ctx.Fail("%s: class %s at %d complete %s verdict %s faces %s; expected %s at %d %s %s", what, o.className.c_str(),
                     o.at, o.complete == reinterpret_cast<uintptr_t>(complete) ? "ok" : "wrong",
                     services::VerdictName(o.verdict), Faces(o).c_str(), className, at, services::VerdictName(verdict),
                     faces.c_str());
        ++ctx.cases;
    };
    char expected[512];

    // The image's cCTGFramework through each of its faces: the same three faces, slot counts as db's entry lists.
    const std::string imageFaces = "[+0x0 1 0xe995cc 49/49][+0x4 1 0xe99694 7/7][+0x180 1 0xe996b4 4/4]";
    check("image +0", &gImageObject[0], "cCTGFramework", 0, gImageObject, Verdict::Image, imageFaces);
    check("image +4", &gImageObject[1], "cCTGFramework", 4, gImageObject, Verdict::Image, imageFaces);
    check("image +0x180", &gImageObject[0x60], "cCTGFramework", 0x180, gImageObject, Verdict::Image, imageFaces);
    // A word at a base's offset that is another class's vtable (cRZCmdLine's) is not a face.
    gImageObject[1] = A(GameAddress<void>(0xd9b4fc));
    check("image, +4 foreign", &gImageObject[0], "cCTGFramework", 0, gImageObject, Verdict::Image,
          "[+0x0 1 0xe995cc 49/49][+0x180 1 0xe996b4 4/4]");
    FillImageObject();

    // Hand-built: an image slot in one of our vtables, listed with its slot number and RVA.
    std::snprintf(expected, sizeof(expected), "[+0x0 2 0x%x 3/1 1:0x142700][+0x8 2 0x%x 1/0]", OurOffset(&gMixedVt0[1]),
                  OurOffset(&gMixedVt8[1]));
    check("mixed +0", &gMixedObject[0], "cFakeMixed", 0, gMixedObject, Verdict::Mixed, expected);
    check("mixed +8", &gMixedObject[2], "cFakeMixed", 8, gMixedObject, Verdict::Mixed, expected);
    // The inspected face counts even when the hierarchy doesn't list its base (here only the +0 base is read).
    gMixedHierarchy[2] = 1;
    check("mixed +8, short hierarchy", &gMixedObject[2], "cFakeMixed", 8, gMixedObject, Verdict::Mixed, expected);
    gMixedHierarchy[2] = 2;
    // A virtual base (vbtable) with a vtordisp (cdOffset).
    std::snprintf(expected, sizeof(expected), "[+0x0 2 0x%x 1/0][+0xc 2 0x%x 1/0]", OurOffset(&gVirtualVt0[1]),
                  OurOffset(&gVirtualVtC[1]));
    check("virtual +0xc", &gVirtualObject[3], "cFakeVirtual", 0xc, gVirtualObject, Verdict::Ours, expected);
    check("virtual +0", &gVirtualObject[0], "cFakeVirtual", 0, gVirtualObject, Verdict::Ours, expected);
    // No RTTI: one face, at 0 of itself.
    std::snprintf(expected, sizeof(expected), "[+0x0 2 0x%x 2/0]", OurOffset(&gNoRttiVt[1]));
    check("no rtti", gNoRttiObject, "?", 0, gNoRttiObject, Verdict::Ours, expected);
    // A member's vtable at a base offset belongs to another class: one face only.
    std::snprintf(expected, sizeof(expected), "[+0x0 2 0x%x 1/0]", OurOffset(&gOtherVt[1]));
    check("member vtable", gOtherObject, "cFakeOther", 0, gOtherObject, Verdict::Ours, expected);
    // A vtable in neither module; an unreadable object.
    uint32_t *heapVtable = new uint32_t[3]{0, A(reinterpret_cast<void *>(&OurSlot)), 0};
    const uint32_t heapObject[1] = {A(&heapVtable[1])};
    check("heap vtable", heapObject, "?", 0, heapObject, Verdict::Unknown, "[+0x0 0 0x0 0/0]");
    delete[] heapVtable;
    check("null", nullptr, "?", 0, nullptr, Verdict::Unknown, "");

    // A C++ class of ours (the compiler's RTTI): two faces, no image slot. Its slot counts depend on what the linker
    // puts after each vtable, so only their lower bound is checked.
    OurService service;
    const services::Object o = services::Inspect(static_cast<IHooksLike *>(&service), spaces);
    if (o.className != "hostservices_test::OurService" || o.at != 4 || o.complete != reinterpret_cast<uintptr_t>(&service)
        || o.verdict != Verdict::Ours || o.faces.size() != 2 || o.faces[0].offset != 0 || o.faces[1].offset != 4
        || o.faces[0].where != services::Where::Ours || o.faces[0].slots < 4 || o.faces[1].slots < 4
        || o.faces[0].imageSlots || o.faces[1].imageSlots
        || o.faces[0].vtable != OurOffset(*reinterpret_cast<void **>(&service)))
        ctx.Fail("our class: %s at %d %s faces %s", o.className.c_str(), o.at, services::VerdictName(o.verdict), Faces(o).c_str());
    ++ctx.cases;

    if (services::Undecorate(".?AVcTSSGSystem@nTSSG@@") != "nTSSG::cTSSGSystem" || services::Undecorate(".?AUcFoo@@") != "cFoo"
        || services::Undecorate(".?AV?$cRZAutoRef@VcFoo@@@@") != "?$cRZAutoRef@VcFoo@@@@")
        ctx.Fail("Undecorate");
    ++ctx.cases;
}

DIFF_TEST(HostServices_Dump, 0)
{
    const services::Spaces spaces = RealSpaces();
    FillImageObject();
    FillFakes();
    uint32_t *heapVtable = new uint32_t[3]{0, A(reinterpret_cast<void *>(&OurSlot)), 0};
    const uint32_t heapObject[1] = {A(&heapVtable[1])};
    {
        Containers c;
        c.Add(10000000, 0xa3cd8db3, &gImageObject[1]); // registered through its +4 face
        c.Add(5, 1, &gMixedObject[2]);
        c.Add(-3000000, 3, heapObject);
        c.Add(5, 2, gNoRttiObject); // after id 1: equal priorities keep their insertion order
        Containers::Push(c.tick, &gMixedObject[0]);      // another face of a service's object
        Containers::Push(c.tick, &gVirtualObject[3]);    // no service's object: listed once, though on both lists
        Containers::Push(c.idle, &gVirtualObject[0]);
        Containers::Push(c.idle, &gImageObject[0]);
        c.Id(0xa3cd8db3, &gImageObject[1]);
        c.Id(1, gNoRttiObject); // another object: differs
        c.Id(3, heapObject);
        c.Id(0x99, gNoRttiObject); // no service has this id: differs (and id 2 is missing: differs)
        const services::Lists lists{&c.services, &c.byId, &c.tick, &c.idle};
        std::string out;
        const services::Summary s = services::Dump(lists, spaces, out);

        char expected[2048];
        std::snprintf(expected, sizeof(expected),
                      "service id=00000003 prio=-3000000 tick=0 idle=0 class=? at=? unknown\n"
                      "  +0x0 unknown slots=0 image=0\n"
                      "service id=00000001 prio=5 tick=1 idle=0 class=cFakeMixed at=+0x8 mixed\n"
                      "  +0x0 ours:0x%x slots=3 image=1 image-at=1:0x142700\n"
                      "  +0x8 ours:0x%x slots=1 image=0\n"
                      "service id=00000002 prio=5 tick=0 idle=0 class=? at=? ours\n"
                      "  +0x0 ours:0x%x slots=2 image=0\n"
                      "service id=a3cd8db3 prio=10000000 tick=0 idle=1 class=cCTGFramework at=+0x4 image\n"
                      "  +0x0 image:0xe995cc slots=49 image=49\n"
                      "  +0x4 image:0xe99694 slots=7 image=7\n"
                      "  +0x180 image:0xe996b4 slots=4 image=4\n"
                      "listed tick=1 idle=1 class=cFakeVirtual at=+0xc ours\n"
                      "  +0x0 ours:0x%x slots=1 image=0\n"
                      "  +0xc ours:0x%x slots=1 image=0\n"
                      "idmap 4 entries, 3 differ\n"
                      "4 services: 1 ours, 1 image, 1 mixed (ours with image slots), 1 unknown\n",
                      OurOffset(&gMixedVt0[1]), OurOffset(&gMixedVt8[1]), OurOffset(&gNoRttiVt[1]),
                      OurOffset(&gVirtualVt0[1]), OurOffset(&gVirtualVtC[1]));
        if (out != expected) ctx.Fail("dump:\n%s\nexpected:\n%s", out.c_str(), expected);
        if (s.services != 4 || s.ours != 1 || s.image != 1 || s.mixed != 1 || s.unknown != 1 || s.listedOnly != 1
            || s.idMap != 4 || s.idMapDiffer != 3 || !s.complete)
            ctx.Fail("summary %u %u %u %u %u %u %u %u %d", s.services, s.ours, s.image, s.mixed, s.unknown, s.listedOnly,
                     s.idMap, s.idMapDiffer, s.complete);
        ++ctx.cases;
    }
    {
        // Every service ours: the line milestone I accepts on; no unknown part.
        Containers c;
        c.Add(1, 7, gNoRttiObject);
        c.Id(7, gNoRttiObject);
        const services::Lists lists{&c.services, &c.byId, &c.tick, &c.idle};
        std::string out;
        services::Dump(lists, spaces, out);
        const std::string tail = "idmap 1 entries, 0 differ\n1 services: 1 ours, 0 image, 0 mixed (ours with image slots)\n";
        if (out.size() < tail.size() || out.compare(out.size() - tail.size(), tail.size(), tail) != 0)
            ctx.Fail("all ours:\n%s", out.c_str());
        ++ctx.cases;
    }
    delete[] heapVtable;
}
