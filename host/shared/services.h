#pragma once
// The system services dump (the "services" control command, host/control_services.cpp; docs/roadmap.md milestone I):
// a read-only diagnostic that walks the framework's service containers and, for every service object, names its class
// and every vtable of its faces, says whether each vtable lies in the original image or in our module, and counts how
// many of its slots point into the image's code. Shared with the difftest runner (tests/host_services.cpp).
//
// It only reads memory, every read guarded (a fault ends the read as "not found"); it calls no slot and no image code.
//
// Faces (the vtable pointers of one object under multiple inheritance) come from the class's own RTTI, at run time, not
// from db/ or a scan of the object's words: the registered pointer's vtable has a complete object locator before it
// (vtable[-1]; x86: signature 0, offset, cdOffset, type descriptor, class hierarchy); the locator gives the complete
// object (offset, plus the vtordisp at -cdOffset when cdOffset is set); every base class descriptor in the hierarchy
// gives a candidate offset (mdisp, through the vbtable for a virtual base); a candidate is a face when the word there
// is a vtable whose locator names the same type descriptor and leads back to the same complete object. That finds
// exactly the compiler's faces in either module (the image's classes and ours both carry RTTI; a member object's
// vtable is not a face: its locator names its own class), and needs no facts kept in step with the build. A vtable
// without a locator (no RTTI) is reported as the object's only face, class "?".
//
// Slots: a vtable's slots run from its first entry up to the first entry that is not a pointer into an executable
// section of either module (the image or ours). The compiler puts a locator pointer (data) before every vtable, so the
// count ends at the next vtable; it can read past the end only into code pointers that follow a vtable without RTTI
// (tools/services.py compares image vtables with db/classes.jsonl's entry counts). A slot is an image slot when its
// entry points into the image's code (hooked or not: what counts for milestone I is the slot, not the hook).
//
// Verdict per object: "image" when any face's vtable lies in the image, "mixed" when every vtable is ours but some slot
// is an image slot, "ours" when every vtable is ours with no image slot, "unknown" when a vtable lies in neither module.
#include "gz/Framework.h" // FwRef, FwServiceEntry (the framework's container elements)

#include <cstdint>
#include <list>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace services {

constexpr uint32_t kMaxSlots = 1024;   // a vtable longer than this is cut (no class here has more than ~200)
constexpr uint32_t kMaxBases = 128;    // base class descriptors read per class
constexpr uint32_t kMaxEntries = 4096; // container entries read per container

// A loaded module: its base, SizeOfImage and executable sections (from its PE headers, which must be mapped).
struct Module {
    uintptr_t base = 0;
    uint32_t size = 0;
    std::vector<std::pair<uintptr_t, uintptr_t>> code; // [begin, end) of each executable section
};
Module ModuleAt(const void *base);

enum class Where : uint8_t { None, Image, Ours };

struct Spaces {
    Module image, ours;
    Where In(uintptr_t address) const;     // inside the module's image (any section, headers included)
    Where CodeIn(uintptr_t address) const; // inside an executable section
    uint32_t Offset(uintptr_t address) const; // RVA (image) or offset from our module's base; 0 for None
};

struct Face {
    int32_t offset = 0;                // of this face in the complete object
    Where where = Where::None;         // where its vtable lies
    uint32_t vtable = 0;               // RVA or our module offset (0 when None)
    uint32_t slots = 0, imageSlots = 0;
    std::vector<std::pair<uint32_t, uint32_t>> imageAt; // (slot, image RVA) of the image slots of one of our vtables
};

enum class Verdict : uint8_t { Ours, Image, Mixed, Unknown };
const char *VerdictName(Verdict verdict);

struct Object {
    std::string className = "?";       // undecorated RTTI name ("nTSSG::cTSSGSystem"), "?" without RTTI
    bool rtti = false;
    int32_t at = 0;                    // the inspected pointer's offset in the complete object (0 without RTTI)
    uintptr_t complete = 0;            // the complete object (the inspected pointer without RTTI)
    std::vector<Face> faces;           // ascending offset; empty when the object's first word can't be read
    Verdict verdict = Verdict::Unknown;
};

// What the dump knows about one object (registered face pointer -> class, faces, slots, verdict). Only reads.
Object Inspect(const void *face, const Spaces &spaces);

// The ".?AV...@@" type name made readable: "cTSSGSystem@nTSSG@@" -> "nTSSG::cTSSGSystem"; names with templates or
// other decorations stay as they are after the prefix.
std::string Undecorate(const std::string &decorated);

// The framework's containers (cGZFramework +0x84, +0x8c, +0xbc, +0xac; gz/Framework.h).
struct Lists {
    const std::multimap<int32_t, FwServiceEntry> *services = nullptr;
    const std::unordered_map<uint32_t, FwRef> *byId = nullptr;
    const std::list<FwRef> *tick = nullptr;
    const std::list<FwRef> *idle = nullptr;
};

struct Summary {
    uint32_t services = 0, ours = 0, image = 0, mixed = 0, unknown = 0;
    uint32_t listedOnly = 0;             // tick/idle list entries that are no registered service's object
    uint32_t idMap = 0, idMapDiffer = 0; // id map entries; ids missing from either container or naming another object
    bool complete = true;                // false when a container could not be read to its end
};

// Appends, in the priority container's order (ascending priority, insertion order within one):
//   service id=<hex8> prio=<int> tick=<0|1> idle=<0|1> class=<name> at=<+0xN|?> <verdict>
//     +0x<off> <image:0x<rva>|ours:0x<offset>|unknown> slots=<n> image=<n>[ image-at=<slot>:0x<rva>,...]
// then "listed tick=.. idle=.. class=.. at=.. <verdict>" (and its faces) for each tick/idle entry that is no service's
// object, "idmap <n> entries, <m> differ", "(containers not read to the end)" if so, and last the summary line
//   <N> services: <X> ours, <Y> image, <Z> mixed (ours with image slots)[, <U> unknown]
// Tick and idle membership compares complete objects, so any face of a service's object on a list counts.
Summary Dump(const Lists &lists, const Spaces &spaces, std::string &out);

} // namespace services
