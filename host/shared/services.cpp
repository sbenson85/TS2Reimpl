#include "services.h"

#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <set>

namespace services {
namespace {

// Every memory read goes through these: a fault is "not readable", never a crash.
bool Read32(uintptr_t address, uint32_t *value)
{
    __try {
        *value = *reinterpret_cast<const volatile uint32_t *>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// A NUL-terminated string of at most size-1 characters (false if longer or unreadable).
bool ReadText(uintptr_t address, char *buffer, size_t size)
{
    __try {
        for (size_t i = 0; i < size; ++i) {
            buffer[i] = *reinterpret_cast<const volatile char *>(address + i);
            if (!buffer[i]) return true;
        }
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The complete object locator before a vtable (x86 layout: signature, offset, cdOffset, type descriptor, hierarchy),
// accepted only when it and what it points at lie in the vtable's own module and the type is a class or struct.
struct Locator {
    Where where = Where::None;
    int32_t offset = 0, cdOffset = 0;
    uintptr_t type = 0, hierarchy = 0;
};

bool ReadLocator(uintptr_t vtable, const Spaces &spaces, Locator *out)
{
    const Where where = spaces.In(vtable);
    uint32_t col = 0, signature = 0, offset = 0, cdOffset = 0, type = 0, hierarchy = 0;
    if (where == Where::None || !Read32(vtable - 4, &col) || spaces.In(col) != where) return false;
    if (!Read32(col, &signature) || signature != 0 || !Read32(col + 4, &offset) || !Read32(col + 8, &cdOffset)
        || !Read32(col + 12, &type) || !Read32(col + 16, &hierarchy))
        return false;
    if (spaces.In(type) != where || spaces.In(hierarchy) != where) return false;
    char prefix[5] = {};
    for (int i = 0; i < 4; ++i) {
        uint32_t word = 0;
        if (!Read32(type + 8 + i, &word)) return false;
        prefix[i] = static_cast<char>(word & 0xff);
    }
    if (std::strcmp(prefix, ".?AV") != 0 && std::strcmp(prefix, ".?AU") != 0) return false;
    *out = Locator{where, static_cast<int32_t>(offset), static_cast<int32_t>(cdOffset), type, hierarchy};
    return true;
}

// The complete object a face belongs to, as dynamic_cast<void *> computes it.
bool CompleteOf(uintptr_t face, const Locator &locator, uintptr_t *complete)
{
    int32_t offset = locator.offset;
    if (locator.cdOffset) {
        uint32_t vtordisp = 0;
        if (!Read32(face - locator.cdOffset, &vtordisp)) return false;
        offset += static_cast<int32_t>(vtordisp);
    }
    *complete = face - offset;
    return true;
}

// The offsets of the class's base subobjects (and 0), from its hierarchy's base class descriptors
// (type, contained bases, mdisp, pdisp, vdisp, attributes).
std::set<int32_t> BaseOffsets(uintptr_t complete, const Locator &locator, const Spaces &spaces)
{
    std::set<int32_t> offsets{0};
    uint32_t count = 0, array = 0;
    if (!Read32(locator.hierarchy + 8, &count) || !Read32(locator.hierarchy + 12, &array) || spaces.In(array) != locator.where)
        return offsets;
    for (uint32_t i = 0; i < count && i < kMaxBases; ++i) {
        uint32_t base = 0, mdisp = 0, pdisp = 0, vdisp = 0;
        if (!Read32(array + 4 * i, &base) || spaces.In(base) != locator.where || !Read32(base + 8, &mdisp)
            || !Read32(base + 12, &pdisp) || !Read32(base + 16, &vdisp))
            continue;
        int32_t offset = static_cast<int32_t>(mdisp);
        if (static_cast<int32_t>(pdisp) >= 0) { // a virtual base: its displacement is in the vbtable at +pdisp
            uint32_t vbtable = 0, displacement = 0;
            if (!Read32(complete + pdisp, &vbtable) || !Read32(vbtable + vdisp, &displacement)) continue;
            offset += static_cast<int32_t>(pdisp) + static_cast<int32_t>(displacement);
        }
        offsets.insert(offset);
    }
    return offsets;
}

void CountSlots(uintptr_t vtable, const Spaces &spaces, Face *face)
{
    for (uint32_t i = 0; i < kMaxSlots; ++i) {
        uint32_t entry = 0;
        if (!Read32(vtable + 4 * i, &entry)) return;
        const Where where = spaces.CodeIn(entry);
        if (where == Where::None) return;
        ++face->slots;
        if (where == Where::Image) {
            ++face->imageSlots;
            if (face->where == Where::Ours) face->imageAt.emplace_back(i, spaces.Offset(entry));
        }
    }
}

Face MakeFace(int32_t offset, uintptr_t vtable, const Spaces &spaces)
{
    Face face;
    face.offset = offset;
    face.where = spaces.In(vtable);
    face.vtable = spaces.Offset(vtable);
    if (face.where != Where::None) CountSlots(vtable, spaces, &face);
    return face;
}

Verdict Judge(const std::vector<Face> &faces)
{
    if (faces.empty()) return Verdict::Unknown;
    bool image = false, imageSlots = false;
    for (const Face &face : faces) {
        if (face.where == Where::None) return Verdict::Unknown;
        image |= face.where == Where::Image;
        imageSlots |= face.imageSlots != 0;
    }
    return image ? Verdict::Image : imageSlots ? Verdict::Mixed : Verdict::Ours;
}

void Append(std::string &out, const char *format, ...)
{
    char text[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    out += text;
}

void WriteObject(const Object &object, std::string &out)
{
    out += " class=" + object.className;
    if (object.rtti) Append(out, " at=+0x%x", static_cast<uint32_t>(object.at));
    else out += " at=?";
    Append(out, " %s\n", VerdictName(object.verdict));
    for (const Face &face : object.faces) {
        Append(out, "  +0x%x ", static_cast<uint32_t>(face.offset));
        if (face.where == Where::Image) Append(out, "image:0x%x", face.vtable);
        else if (face.where == Where::Ours) Append(out, "ours:0x%x", face.vtable);
        else out += "unknown";
        Append(out, " slots=%u image=%u", face.slots, face.imageSlots);
        for (size_t i = 0; i < face.imageAt.size(); ++i)
            Append(out, "%s%u:0x%x", i ? "," : " image-at=", face.imageAt[i].first, face.imageAt[i].second);
        out += '\n';
    }
}

struct Entry {
    int32_t priority;
    uint32_t id;
    const void *object;
};

// Runs fn (which returns bool) with faults caught: false on a fault. fn's own frame holds the iterators, so this one
// has nothing to unwind.
template <typename Fn> bool Guarded(Fn &&fn)
{
    __try {
        return fn();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Copies a container's entries through push into a vector reserved beforehand (no allocation while reading), stopping
// at kMaxEntries; false if a read faulted or the container had more entries.
template <typename Container, typename Fn> bool Copy(const Container &container, Fn &&push)
{
    return Guarded([&] {
        uint32_t n = 0;
        for (const auto &e : container) {
            if (++n > kMaxEntries) return false;
            push(e);
        }
        return true;
    });
}

} // namespace

Module ModuleAt(const void *base)
{
    Module module;
    module.base = reinterpret_cast<uintptr_t>(base);
    const auto *dos = static_cast<const IMAGE_DOS_HEADER *>(base);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(module.base + dos->e_lfanew);
    module.size = nt->OptionalHeader.SizeOfImage;
    const IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const uint32_t size = section->Misc.VirtualSize ? section->Misc.VirtualSize : section->SizeOfRawData;
        module.code.emplace_back(module.base + section->VirtualAddress, module.base + section->VirtualAddress + size);
    }
    return module;
}

Where Spaces::In(uintptr_t address) const
{
    if (address - image.base < image.size) return Where::Image;
    if (address - ours.base < ours.size) return Where::Ours;
    return Where::None;
}

Where Spaces::CodeIn(uintptr_t address) const
{
    for (const auto &range : image.code)
        if (address >= range.first && address < range.second) return Where::Image;
    for (const auto &range : ours.code)
        if (address >= range.first && address < range.second) return Where::Ours;
    return Where::None;
}

uint32_t Spaces::Offset(uintptr_t address) const
{
    switch (In(address)) {
    case Where::Image: return static_cast<uint32_t>(address - image.base);
    case Where::Ours: return static_cast<uint32_t>(address - ours.base);
    default: return 0;
    }
}

const char *VerdictName(Verdict verdict)
{
    switch (verdict) {
    case Verdict::Ours: return "ours";
    case Verdict::Image: return "image";
    case Verdict::Mixed: return "mixed";
    default: return "unknown";
    }
}

std::string Undecorate(const std::string &decorated)
{
    std::string name = decorated;
    if (name.rfind(".?AV", 0) == 0 || name.rfind(".?AU", 0) == 0) name = name.substr(4);
    if (name.size() >= 2 && name.compare(name.size() - 2, 2, "@@") == 0 && name.find('?') == std::string::npos) {
        std::vector<std::string> parts;
        size_t start = 0;
        for (size_t at; (at = name.find('@', start)) != std::string::npos && at > start; start = at + 1)
            parts.push_back(name.substr(start, at - start));
        std::string out;
        for (auto it = parts.rbegin(); it != parts.rend(); ++it) out += (out.empty() ? "" : "::") + *it;
        return out;
    }
    return name;
}

Object Inspect(const void *face, const Spaces &spaces)
{
    Object object;
    const auto at = reinterpret_cast<uintptr_t>(face);
    object.complete = at;
    uint32_t vtable = 0;
    if (!face || !Read32(at, &vtable)) return object;
    Locator locator;
    uintptr_t complete = 0;
    if (ReadLocator(vtable, spaces, &locator) && CompleteOf(at, locator, &complete)) {
        char name[256];
        if (ReadText(locator.type + 8, name, sizeof(name))) {
            object.rtti = true;
            object.className = Undecorate(name);
            object.complete = complete;
            object.at = static_cast<int32_t>(at - complete);
            std::set<int32_t> offsets = BaseOffsets(complete, locator, spaces);
            offsets.insert(object.at);
            for (const int32_t offset : offsets) {
                uint32_t other = 0;
                Locator otherLocator;
                uintptr_t otherComplete = 0;
                if (!Read32(complete + offset, &other) || !ReadLocator(other, spaces, &otherLocator)
                    || otherLocator.type != locator.type || !CompleteOf(complete + offset, otherLocator, &otherComplete)
                    || otherComplete != complete)
                    continue;
                object.faces.push_back(MakeFace(offset, other, spaces));
            }
        }
    }
    if (!object.rtti) object.faces.push_back(MakeFace(0, vtable, spaces));
    object.verdict = Judge(object.faces);
    return object;
}

Summary Dump(const Lists &lists, const Spaces &spaces, std::string &out)
{
    Summary summary;
    std::vector<Entry> entries;
    std::vector<std::pair<uint32_t, const void *>> byId;
    std::vector<const void *> tick, idle;
    entries.reserve(kMaxEntries);
    byId.reserve(kMaxEntries);
    tick.reserve(kMaxEntries);
    idle.reserve(kMaxEntries);
    if (lists.services)
        summary.complete &= Copy(*lists.services, [&](const auto &e) {
            entries.push_back(Entry{e.first, e.second.mId, e.second.mService.mObject});
        });
    if (lists.byId)
        summary.complete &= Copy(*lists.byId, [&](const auto &e) { byId.emplace_back(e.first, e.second.mObject); });
    if (lists.tick) summary.complete &= Copy(*lists.tick, [&](const FwRef &r) { tick.push_back(r.mObject); });
    if (lists.idle) summary.complete &= Copy(*lists.idle, [&](const FwRef &r) { idle.push_back(r.mObject); });

    // Lists hold whichever face was added: compare complete objects.
    std::vector<uintptr_t> tickObjects, idleObjects;
    for (const void *p : tick) tickObjects.push_back(Inspect(p, spaces).complete);
    for (const void *p : idle) idleObjects.push_back(Inspect(p, spaces).complete);
    auto on = [](const std::vector<uintptr_t> &list, uintptr_t object) {
        return std::find(list.begin(), list.end(), object) != list.end();
    };

    std::vector<uintptr_t> serviceObjects;
    for (const Entry &entry : entries) {
        const Object object = Inspect(entry.object, spaces);
        serviceObjects.push_back(object.complete);
        Append(out, "service id=%08x prio=%d tick=%d idle=%d", entry.id, entry.priority, on(tickObjects, object.complete),
               on(idleObjects, object.complete));
        WriteObject(object, out);
        ++summary.services;
        switch (object.verdict) {
        case Verdict::Ours: ++summary.ours; break;
        case Verdict::Image: ++summary.image; break;
        case Verdict::Mixed: ++summary.mixed; break;
        default: ++summary.unknown; break;
        }
    }

    // Tick and idle entries that are no service's object, each once, in tick-then-idle order.
    std::vector<uintptr_t> listed;
    for (const std::vector<const void *> *list : {&tick, &idle}) {
        for (const void *p : *list) {
            const Object object = Inspect(p, spaces);
            if (on(serviceObjects, object.complete) || on(listed, object.complete)) continue;
            listed.push_back(object.complete);
            ++summary.listedOnly;
            Append(out, "listed tick=%d idle=%d", on(tickObjects, object.complete), on(idleObjects, object.complete));
            WriteObject(object, out);
        }
    }

    // The id map against the priority container: every service's id names its object, and nothing else is there.
    summary.idMap = static_cast<uint32_t>(byId.size());
    for (const Entry &entry : entries) {
        auto it = std::find_if(byId.begin(), byId.end(), [&](const auto &e) { return e.first == entry.id; });
        if (it == byId.end() || it->second != entry.object) ++summary.idMapDiffer;
    }
    for (const auto &e : byId)
        if (std::none_of(entries.begin(), entries.end(), [&](const Entry &entry) { return entry.id == e.first; }))
            ++summary.idMapDiffer;
    Append(out, "idmap %u entries, %u differ\n", summary.idMap, summary.idMapDiffer);
    if (!summary.complete) out += "(containers not read to the end)\n";

    Append(out, "%u services: %u ours, %u image, %u mixed (ours with image slots)", summary.services, summary.ours,
           summary.image, summary.mixed);
    if (summary.unknown) Append(out, ", %u unknown", summary.unknown);
    out += '\n';
    return summary;
}

} // namespace services
