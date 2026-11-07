// cTSWantSimulator, lane GG: slot 53 (0x488820), the debug dump of the skills and careers the want system knows into
// WantSimulator_2d7ee26b.xml. Only the vtable refers to it (no adapter, no RVA hook: lane GG, coordinator decision
// 2026-10-18); tests/ts_want_simulator_gg_debug.cpp runs it through our vtable.
#include "ts/cTSWantSimulator.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "ifaces/cITSGlobals.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/RZFileStreams.h"
#include "rz/cRZString.h"

#include <new>
#include <set>

namespace wantsim_gg_debug {

// The XML pieces (the image's strings).
constexpr uint32_t kFileName = 0xdcb1cc;        // "WantSimulator_2d7ee26b.xml"
constexpr uint32_t kOpenSimulator = 0xdcb1e8;   // "<wantSimulator>"
constexpr uint32_t kCloseSimulator = 0xdcb260;  // "</wantSimulator>"
constexpr uint32_t kNewline = 0xd9e644;         // "\n"
constexpr uint32_t kTab = 0xdc47c4;             // "\t"
constexpr uint32_t kQuote = 0xda296c;           // "\""
constexpr uint32_t kEndTag = 0xdcb204;          // "/>"
constexpr uint32_t kNameAttr = 0xdcb208;        // " name="
constexpr uint32_t kTypeAttr = 0xdcb240;        // " type="
constexpr uint32_t kOpenSkills = 0xdcb1f8;      // "<skills>"
constexpr uint32_t kCloseSkills = 0xdcb21c;     // "</skills>"
constexpr uint32_t kSkill = 0xdcb210;           // "<skill id="
constexpr uint32_t kOpenCareers = 0xdcb228;     // "<careers>"
constexpr uint32_t kCloseCareers = 0xdcb254;    // "</careers>"
constexpr uint32_t kCareer = 0xdcb248;          // "<career id="
constexpr uint32_t kHexFormat = 0xda3e60;       // "0x%08x"
constexpr uint32_t kAdult = 0xdb42a4;           // "Adult"
constexpr uint32_t kTeenElder = 0xdcb234;       // "Teen/Elder"
constexpr uint32_t kMajor = 0xdcad04;           // "Major"

// Object kinds (an object's slot 13 with 0x3b): career tracks for adults, for teens and elders, and college majors.
constexpr int16_t kAdultCareer = 0x20;
constexpr int16_t kTeenElderCareer = 0x2e;
constexpr int16_t kMajorCareer = 0x61;
constexpr uint32_t kKindQuery = 0x3b;

const char *Text(uint32_t rva) { return GameAddress<const char>(rva); }

// The text file stream (cRZOFStream 0x1b4dd2 / 0x1b52d8, rz/RZFileStreams.h; 0x18 bytes on the stack): slot 8 writes an int, 14 a C
// string, 15 a cIGZString.
class TextFile {
public:
    explicit TextFile(cRZString *path) { Stream()->Construct(*path->AsInterface(), 0, 0, 0, 0); }
    TextFile(const TextFile &) = delete;
    TextFile &operator=(const TextFile &) = delete;
    ~TextFile() { Stream()->Destruct(); }
    cRZOFStream *Stream() { return reinterpret_cast<cRZOFStream *>(mBytes); }
    void Put(uint32_t textRva) { VirtualCall<void *(__thiscall *)(void *, const char *), 14>(mBytes, Text(textRva)); }
    void PutInt(int32_t value) { VirtualCall<void *(__thiscall *)(void *, int32_t), 8>(mBytes, value); }
    void PutString(cRZString *text) { VirtualCall<void *(__thiscall *)(void *, cRZString *), 15>(mBytes, text); }
    // `name="value"` with the value quoted.
    void PutQuoted(cRZString *value)
    {
        Put(kQuote);
        PutString(value);
        Put(kQuote);
    }

private:
    alignas(4) uint8_t mBytes[0x18];
};

// A cRZString on the stack, built and destroyed as the original's inline code does.
class LocalString {
public:
    LocalString() { Get()->Construct(); }
    explicit LocalString(const char *text) { Get()->Construct(text); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
    ~LocalString() { Get()->Destruct(); }
    cRZString *Get() { return reinterpret_cast<cRZString *>(mBytes); }

private:
    alignas(4) uint8_t mBytes[sizeof(cRZString)];
};

// 0x48de50: the skill ids the want system scores (both callers: this dump and the tuning loader).
void CollectSkillIds(std::set<uint16_t> &skills)
{
    for (uint16_t id : {uint16_t(9), uint16_t(10), uint16_t(12), uint16_t(11), uint16_t(17), uint16_t(18), uint16_t(15)})
        skills.insert(id);
}

// 0x48d470: the career objects of the object table's list, by kind.
struct ObjectNode {
    ObjectNode *next;
    ObjectNode *prev;
    void *object;
};
int16_t ObjectKind(void *object) { return VirtualCall<int16_t(__thiscall *)(void *, uint32_t), 13>(object, kKindQuery); }
void CollectCareerObjects(std::set<void *> &adult, std::set<void *> &teenElder, std::set<void *> &major)
{
    void *table = As<ifaces::cITSGlobals>(GetCachedSingleton())->GetObjectTable();
    auto *const head = *VirtualCall<ObjectNode **(__thiscall *)(void *), 31>(table); // the table's object list
    for (ObjectNode *node = head->next; node != head; node = node->next) {
        void *const object = node->object;
        const int16_t kind = ObjectKind(object);
        if (kind == kAdultCareer) adult.insert(object);
        else if (kind == kTeenElderCareer) teenElder.insert(object);
        else if (kind == kMajorCareer) major.insert(object);
    }
}

// 0x496af0 (__cdecl): a skill's display name (shared with slot 52, the tuning loader and cTSWantInstance; still the
// game's).
void SkillName(cRZString *out, uint16_t skill)
{
    OriginalFunction<cRZString *(__cdecl *)(cRZString *, uint32_t), 0x496af0>()(out, skill);
}

void *CareerDefinition(void *object) { return VirtualCall<void *(__thiscall *)(void *), 61>(object); }

} // namespace wantsim_gg_debug

using namespace wantsim_gg_debug;

// 0x488820 (slot 53): writes every skill (id, name) and every career object (its definition's id in hex, the kind,
// the definition's name) as XML into WantSimulator_2d7ee26b.xml. `this` is not used.
void cTSWantSimulator::WriteDebugXml()
{
    LocalString path(Text(kFileName));
    TextFile file(path.Get());
    file.Put(kOpenSimulator);
    file.Put(kNewline);

    std::set<uint16_t> skills;
    CollectSkillIds(skills);
    file.Put(kTab);
    file.Put(kOpenSkills);
    file.Put(kNewline);
    for (const uint16_t skill : skills) {
        alignas(4) uint8_t nameBytes[sizeof(cRZString)]; // built by SkillName in place
        cRZString &name = *reinterpret_cast<cRZString *>(nameBytes);
        SkillName(&name, skill);
        file.Put(kTab);
        file.Put(kTab);
        file.Put(kSkill);
        file.Put(kQuote);
        file.PutInt(int16_t(skill));
        file.Put(kQuote);
        file.Put(kNameAttr);
        file.PutQuoted(&name);
        file.Put(kEndTag);
        file.Put(kNewline);
        name.Destruct();
    }
    file.Put(kTab);
    file.Put(kCloseSkills);
    file.Put(kNewline);
    file.Put(kNewline);

    std::set<void *> adult, teenElder, major, careers;
    CollectCareerObjects(adult, teenElder, major);
    careers.insert(adult.begin(), adult.end());
    careers.insert(teenElder.begin(), teenElder.end());
    careers.insert(major.begin(), major.end());
    file.Put(kTab);
    file.Put(kOpenCareers);
    file.Put(kNewline);
    for (void *const object : careers) {
        const int16_t kind = ObjectKind(object);
        uint32_t kindName;
        if (kind == kAdultCareer) kindName = kAdult;
        else if (kind == kTeenElderCareer) kindName = kTeenElder;
        else if (kind == kMajorCareer) kindName = kMajor;
        else continue;
        const uint32_t id = VirtualCall<uint32_t(__thiscall *)(void *), 28>(CareerDefinition(object));
        LocalString idText;
        idText.Get()->Sprintf(Text(kHexFormat), id);
        LocalString name;
        VirtualCall<void(__thiscall *)(void *, cRZString *), 49>(CareerDefinition(object), name.Get());
        LocalString kindText(Text(kindName));
        file.Put(kTab);
        file.Put(kTab);
        file.Put(kCareer);
        file.PutQuoted(idText.Get());
        file.Put(kTypeAttr);
        file.PutQuoted(kindText.Get());
        file.Put(kNameAttr);
        file.PutQuoted(name.Get());
        file.Put(kEndTag);
        file.Put(kNewline);
    }
    file.Put(kTab);
    file.Put(kCloseCareers);
    file.Put(kNewline);
    file.Put(kCloseSimulator);
    file.Put(kNewline);
}
