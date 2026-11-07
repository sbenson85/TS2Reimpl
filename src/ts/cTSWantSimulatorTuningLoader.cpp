// cTSWantSimulatorTuningLoader (lane IL): see the header. Element and attribute names are the image's strings, read through
// their pointer cells at run time.
#include "ts/cTSWantSimulatorTuningLoader.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "ifaces/cITSGlobals.h"
#include "misc/HexParse.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"
#include "simantics/ServiceRefVector.h"
#include "ts/cTSWantSimulator.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

static_assert(offsetof(cTSWantSimulatorTuningLoader, mOwner) == 0x04, "mOwner");
static_assert(offsetof(cTSWantSimulatorTuningLoader, mMode) == 0x0c, "mMode");
static_assert(offsetof(cTSWantSimulatorTuningLoader, mLevelKey) == 0x14, "mLevelKey");
static_assert(offsetof(cTSWantSimulatorTuningLoader, mSkills) == 0x18, "mSkills");
static_assert(offsetof(cTSWantSimulatorTuningLoader, mMajorCareers) == 0x30, "mMajorCareers");
static_assert(offsetof(cTSWantSimulatorTuningLoader, mHasCareers) == 0x38, "mHasCareers");

namespace wanttuning {

// Pointer cells of the image's strings (0x1012d28 ..): element names, attribute names, attribute values.
enum Cell : uint32_t {
    kWantSimulator = 0x1012d28, kInfluence = 0x1012d2c, kInfluenceTuning = 0x1012d30, kLifetimeGoals = 0x1012d34,
    kLifetimeWants = 0x1012d38, kAspiration = 0x1012d3c, kWant = 0x1012d40, kLevels = 0x1012d44, kLevel = 0x1012d48,
    kThreshold = 0x1012d4c, kCategories = 0x1012d50, kCategory = 0x1012d54, kSkills = 0x1012d58, kSkill = 0x1012d5c,
    kBadges = 0x1012d60, kBadge = 0x1012d64, kCareers = 0x1012d68, kCareer = 0x1012d6c, kPersonData = 0x1012d70,
    // attributes
    kName = 0x1012d74, kGuid = 0x1012d78, kScore = 0x1012d7c, kId = 0x1012d80, kPrimaryIcon = 0x1012d84,
    kSecondaryIcon = 0x1012d88, kStringSet = 0x1012d8c, kType = 0x1012d90, kCurve = 0x1012d94, kObject = 0x1012d98,
    kNumber = 0x1012d9c,
    // career type values
    kAdult = 0x1012dfc, kMajor = 0x1012e00,
};

const char *Text(uint32_t cell) { return *GameAddress<const char *>(cell); }
bool Is(const char *text, uint32_t cell) { return std::strcmp(text, Text(cell)) == 0; }

// The aspiration names (attribute `name` of an aspiration element) and their category flags, as the original tests them.
struct Mapping {
    uint32_t cell;
    uint32_t value;
};
constexpr Mapping kAspirations[] = {
    {0x1012da4, 2},    {0x1012da0, 1},    {0x1012da8, 4},    {0x1012dac, 4},    {0x1012db0, 0x10},
    {0x1012db4, 0x10}, {0x1012db8, 0x20}, {0x1012dbc, 0x40}, {0x1012dc0, 0x80}, {0x1012dc4, 0x100},
};
// Life stage names (attribute `name` of a level element) and their level keys.
constexpr Mapping kLevelNames[] = {
    {0x1012dc8, 0xd}, {0x1012dcc, 2}, {0x1012dd0, 1}, {0x1012dd4, 3}, {0x1012dd8, 0}, {0x1012ddc, 4},
};
// Threshold names (attribute `name` of a threshold element) and their index in the level's table.
constexpr Mapping kThresholds[] = {
    {0x1012df8, 0}, {0x1012df4, 1}, {0x1012df0, 2}, {0x1012dec, 3}, {0x1012de8, 4}, {0x1012de4, 5}, {0x1012de0, 6},
};

// The value of the first mapping whose name is `text`; `previous` when none is.
template <size_t N> uint32_t Map(const Mapping (&map)[N], const char *text, uint32_t previous)
{
    for (const Mapping &m : map)
        if (Is(text, m.cell)) return m.value;
    return previous;
}

constexpr uint32_t kLevelCount = 7;                // entries of a level table
constexpr int32_t kLowestScore = -0x50000;         // a threshold's score is clamped to [kLowestScore, kHighestScore]
constexpr int32_t kHighestScore = 0x4fff6;
constexpr int32_t kUnset = int32_t(0x80000000);    // threshold index and score before their attributes
constexpr uint32_t kWantInstanceSize = 0x3c;       // cTSWantInstance (pooled, 0x54e570)
constexpr uint32_t kCurveSize = 0x24;              // cTSPiecewiseFunction (constructor 0x54b890)
constexpr uint32_t kCurveRange = 0x20;
constexpr int32_t kAnySim = -1;

// Object kinds of career objects (an object's slot 13 with 0x3b).
constexpr int16_t kAdultCareer = 0x20;
constexpr int16_t kTeenElderCareer = 0x2e;
constexpr int16_t kMajorCareer = 0x61;
constexpr uint32_t kKindQuery = 0x3b;

// The tuning record's key and the parser's class and interface.
constexpr uint32_t kTuningType = 0xcda53b6f;
constexpr uint32_t kTuningGroup = 0x2d7ee26b;
constexpr uint32_t kStreamIid = 0xc56b8f08;
constexpr uint32_t kParserClass = 0x6b6b01d1;
constexpr uint32_t kParserIid = 0x6b6b01cd;
// The thresholds a level gets when there is no tuning record, and the levels that get them.
constexpr int32_t kDefaultThresholds[kLevelCount] = {-1000, -500, 0, 4000, 8000, 12000, 16000};
constexpr int32_t kDefaultLevelKeys[] = {0, 1, 2, 3, 4, 0xd};

void AddRefObject(void *object) { VirtualCall<uint32_t(__thiscall *)(void *), 1>(object); }
void ReleaseObject(void *object) { VirtualCall<uint32_t(__thiscall *)(void *), 2>(object); }

// A piecewise function (cTSPiecewiseFunction, still the game's class): a new one, counted, or null when the allocation
// fails. Slot 3 resets it, slot 4 sets its range, slot 5 adds a point, slot 6 reads points from text, slot 9 counts them,
// slot 10 reads one.
struct CurvePoint {
    float x;
    float y;
};
void *NewCurve()
{
    void *memory = ::operator new(kCurveSize);
    void *curve = OriginalFunction<void *(__thiscall *)(void *), 0x54b890>()(memory);
    if (curve) AddRefObject(curve);
    VirtualCall<void(__thiscall *)(void *), 3>(curve);
    VirtualCall<void(__thiscall *)(void *, uint32_t), 4>(curve, kCurveRange);
    return curve;
}

// A counted pointer field of the simulator takes `object` (the same object: nothing happens).
void Assign(WantRef &field, void *object)
{
    void *const old = field.object;
    if (object == old) return;
    if (object) AddRefObject(object);
    field.object = object;
    if (old) ReleaseObject(old);
}

int16_t ObjectKind(void *object) { return VirtualCall<int16_t(__thiscall *)(void *, uint32_t), 13>(object, kKindQuery); }
void *ObjectTable() { return As<ifaces::cITSGlobals>(GetCachedSingleton())->GetObjectTable(); }
void *CareerDefinition(void *object) { return VirtualCall<void *(__thiscall *)(void *), 61>(object); }

// The object list of the object table (slot 31): a ring of {next, prev, object} with a head node.
struct ObjectNode {
    ObjectNode *next;
    ObjectNode *prev;
    void *object;
};

} // namespace wanttuning

using namespace wanttuning;

void cTSWantSimulatorTuningLoader::CollectSkillIds(std::set<int16_t> *skills)
{
    for (int16_t id : {int16_t(9), int16_t(10), int16_t(12), int16_t(11), int16_t(17), int16_t(18), int16_t(15)})
        skills->insert(skills->end(), id);
}

void cTSWantSimulatorTuningLoader::CollectCareerObjects(std::set<void *> *adult, std::set<void *> *teenElder,
                                                        std::set<void *> *major)
{
    void *const table = ObjectTable();
    ObjectNode *const head = *VirtualCall<ObjectNode **(__thiscall *)(void *), 31>(table);
    for (ObjectNode *node = head->next; node != head; node = node->next) {
        void *const object = node->object;
        const int16_t kind = ObjectKind(object);
        if (kind == kAdultCareer) adult->insert(object);
        else if (kind == kTeenElderCareer) teenElder->insert(object);
        else if (kind == kMajorCareer) major->insert(object);
    }
}

std::set<std::string> &cTSWantSimulatorTuningLoader::UnknownStartTags()
{
    static std::set<std::string> tags;
    return tags;
}

std::set<std::string> &cTSWantSimulatorTuningLoader::UnknownEndTags()
{
    static std::set<std::string> tags;
    return tags;
}

bool cTSWantSimulatorTuningLoader::QueryInterface(uint32_t iid, void **out)
{
    if (iid != 1 && iid != kIid) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cTSWantSimulatorTuningLoader::AddRef() { return uint32_t(++mRefCount); }

uint32_t cTSWantSimulatorTuningLoader::Release()
{
    if (mRefCount > 1) return uint32_t(--mRefCount);
    delete this;
    return 0;
}

bool cTSWantSimulatorTuningLoader::StartElement(const char *name, const char **attributes, uint32_t count, uint32_t)
{
    cTSWantSimulator *const owner = mOwner;
    if (Is(name, kWantSimulator)) {
        CollectSkillIds(&mSkills);
        CollectCareerObjects(&mAdultCareers, &mTeenElderCareers, &mMajorCareers);
        mHasCareers = !mAdultCareers.empty();
    } else if (Is(name, kInfluence)) {
    } else if (Is(name, kInfluenceTuning)) {
        // Every curve attribute: the influence curve from its points, and its inverse (each point's x and y swapped).
        for (uint32_t i = 0; i < count; i += 2) {
            if (!Is(attributes[i], kCurve)) continue;
            void *const curve = NewCurve();
            VirtualCall<void(__thiscall *)(void *, const char *), 6>(curve, attributes[i + 1]);
            void *const inverse = NewCurve();
            for (int32_t p = 0; p < VirtualCall<int32_t(__thiscall *)(void *), 9>(curve); ++p) {
                CurvePoint point;
                VirtualCall<void(__thiscall *)(void *, CurvePoint *, int32_t), 10>(curve, &point, p);
                CurvePoint swapped{point.y, point.x};
                VirtualCall<void(__thiscall *)(void *, CurvePoint *), 5>(inverse, &swapped);
            }
            Assign(owner->mRef1c, curve);
            Assign(owner->mRef20, inverse);
            if (inverse) ReleaseObject(inverse);
            if (curve) ReleaseObject(curve);
        }
    } else if (Is(name, kLifetimeGoals)) {
        mMode = kModeLifetimeGoals;
    } else if (Is(name, kLifetimeWants)) {
        mMode = kModeLifetimeWants;
    } else if (Is(name, kAspiration)) {
        for (uint32_t i = 0; i < count; i += 2)
            if (Is(attributes[i], kName)) mAspiration = Map(kAspirations, attributes[i + 1], mAspiration);
    } else if (Is(name, kWant)) {
        if (mMode == kModeLifetimeGoals) {
            // A category want for the current aspiration per guid attribute.
            for (uint32_t i = 0; i < count; i += 2)
                if (Is(attributes[i], kGuid)) {
                    const uint32_t guid = hexparse::ParseHex32(attributes[i + 1]);
                    owner->AddCategoryWant(mAspiration, guid);
                }
        } else if (mMode == kModeLifetimeWants) {
            // A default want (any sim) of that guid, with the object and number as its parameter.
            uint32_t guid = 0, object = 0, number = 0;
            for (uint32_t i = 0; i < count; i += 2) {
                const char *const value = attributes[i + 1];
                if (Is(attributes[i], kGuid)) guid = hexparse::ParseHex32(value);
                else if (Is(attributes[i], kObject)) object = hexparse::ParseHex32(value);
                else if (Is(attributes[i], kNumber)) number = uint16_t(std::atoi(value));
            }
            if (!guid) return true;
            void *const definition = reinterpret_cast<void *>(uintptr_t(owner->GetWantDefinition(guid)));
            if (!definition) return true;
            WantInstanceParam param;
            OriginalFunction<WantInstanceParam *(__thiscall *)(WantInstanceParam *), 0x54e500>()(&param);
            param.value = object;
            param.extra = int16_t(number);
            param.kind = VirtualCall<uint32_t(__thiscall *)(void *), 16>(definition);
            void *want = nullptr;
            if (void *block = OriginalFunction<void *(__cdecl *)(uint32_t), 0x54e570>()(kWantInstanceSize))
                want = OriginalFunction<void *(__thiscall *)(void *, int32_t, uint32_t, WantInstanceParam *, void *, void *),
                                        0x54e2b0>()(block, kAnySim, guid, &param, nullptr, nullptr);
            if (want) AddRefObject(want);
            reinterpret_cast<ServiceRefVector *>(&owner->mVec68)->PushBack(want);
            if (want) ReleaseObject(want);
        }
    } else if (Is(name, kLevels)) {
    } else if (Is(name, kLevel)) {
        for (uint32_t i = 0; i < count; i += 2)
            if (Is(attributes[i], kName)) mLevelKey = int32_t(Map(kLevelNames, attributes[i + 1], uint32_t(mLevelKey)));
        owner->mLevelTables[mLevelKey].resize(kLevelCount, 0);
    } else if (Is(name, kThreshold)) {
        int32_t index = kUnset, score = kUnset;
        for (uint32_t i = 0; i < count; i += 2) {
            if (Is(attributes[i], kName)) index = int32_t(Map(kThresholds, attributes[i + 1], uint32_t(index)));
            else if (Is(attributes[i], kScore)) score = std::strtol(attributes[i + 1], nullptr, 0);
        }
        if (score < kLowestScore) score = kLowestScore;
        else if (score > kHighestScore) score = kHighestScore;
        std::vector<int32_t> &table = owner->mLevelTables[mLevelKey];
        // ORIGINAL-BUG: the index is only bounded above. A threshold without a known name keeps kUnset, whose byte offset
        // (index * 4) wraps to 0, so it overwrites the level's first entry; an empty table (no level element) is written
        // through its null begin.
        if (index < int32_t(kLevelCount))
            *reinterpret_cast<int32_t *>(reinterpret_cast<uintptr_t>(table.data()) + uint32_t(index) * 4u) = score;
    } else if (Is(name, kCategories)) {
    } else if (Is(name, kCategory)) {
        // The category's icons and string set (the string set defaults to the id).
        uint16_t id = 0, stringSet = 0;
        uint32_t primary = 0, secondary = 0;
        for (uint32_t i = 0; i < count; i += 2) {
            const char *const key = attributes[i], *const value = attributes[i + 1];
            if (Is(key, kId)) {
                id = uint16_t(std::strtol(value, nullptr, 0));
                if (!stringSet) stringSet = id;
            }
            if (Is(key, kPrimaryIcon)) primary = std::strtoul(value, nullptr, 0);
            if (Is(key, kSecondaryIcon)) secondary = std::strtoul(value, nullptr, 0);
            if (Is(key, kStringSet)) stringSet = uint16_t(std::strtol(value, nullptr, 0));
        }
        if (int16_t(id) > 0) {
            // The original stores the string set as a word into a dword (its high half is whatever its stack held);
            // GetWantText reads the low word only.
            WantInstancePair &entry = owner->mMap7c[int16_t(id)];
            entry.first = primary;
            entry.second = secondary;
            entry.third = stringSet;
        }
    } else if (Is(name, kSkills)) {
    } else if (Is(name, kSkill)) {
        // The tuning names this skill (its name attribute is read and not used).
        int16_t id = 0;
        for (uint32_t i = 0; i < count; i += 2)
            if (Is(attributes[i], kId)) id = int16_t(std::strtol(attributes[i + 1], nullptr, 0));
        mSkills.erase(id);
    } else if (Is(name, kBadges) || Is(name, kBadge) || Is(name, kCareers)) {
    } else if (Is(name, kCareer)) {
        // The tuning names this career (the type attribute says which set; the name is read and not used).
        bool adult = true, major = false;
        uint32_t guid = 0;
        for (uint32_t i = 0; i < count; i += 2) {
            const char *const key = attributes[i], *const value = attributes[i + 1];
            if (Is(key, kId)) {
                guid = std::strtoul(value, nullptr, 0);
            } else if (Is(key, kType)) {
                adult = Is(value, kAdult);
                major = Is(value, kMajor);
            }
        }
        if (mHasCareers) {
            void *const object = VirtualCall<void *(__thiscall *)(void *, uint32_t), 42>(ObjectTable(), guid);
            if (major) mMajorCareers.erase(object);
            else if (adult) mAdultCareers.erase(object);
            else mTeenElderCareers.erase(object);
        }
    } else if (Is(name, kPersonData)) {
    } else {
        UnknownStartTags().insert(name);
    }
    return true;
}

bool cTSWantSimulatorTuningLoader::EndElement(const char *name)
{
    if (Is(name, kWantSimulator)) {
        // Every skill and career the tuning did not name: its display name is looked up (and dropped), and each career's
        // definition is asked for its id and its name.
        for (int16_t skill : mSkills) {
            alignas(4) uint8_t bytes[sizeof(cRZString)];
            auto *const text = reinterpret_cast<cRZString *>(bytes);
            OriginalFunction<cRZString *(__cdecl *)(cRZString *, uint32_t), 0x496af0>()(text, uint16_t(skill));
            text->Destruct();
        }
        for (std::set<void *> *careers : {&mAdultCareers, &mTeenElderCareers, &mMajorCareers})
            for (void *object : *careers) {
                VirtualCall<uint32_t(__thiscall *)(void *), 28>(CareerDefinition(object));
                alignas(4) uint8_t bytes[sizeof(cRZString)];
                auto *const text = reinterpret_cast<cRZString *>(bytes);
                text->Construct();
                VirtualCall<void(__thiscall *)(void *, cRZString *), 49>(CareerDefinition(object), text);
                text->Destruct();
            }
    } else if (Is(name, kInfluence) || Is(name, kInfluenceTuning)) {
    } else if (Is(name, kLifetimeGoals) || Is(name, kLifetimeWants)) {
        mMode = kModeNone;
    } else if (Is(name, kAspiration) || Is(name, kWant) || Is(name, kLevels)) {
    } else if (Is(name, kLevel)) {
        // The level's thresholds become strictly increasing.
        int32_t *const t = mOwner->mLevelTables[mLevelKey].data();
        for (uint32_t i = 0; i + 1 < kLevelCount; ++i)
            if (t[i] >= t[i + 1]) t[i + 1] = t[i] + 1;
    } else if (Is(name, kThreshold) || Is(name, kCategories) || Is(name, kCategory) || Is(name, kSkills) ||
               Is(name, kSkill) || Is(name, kBadges) || Is(name, kBadge) || Is(name, kCareers) || Is(name, kCareer) ||
               Is(name, kPersonData)) {
    } else {
        UnknownEndTags().insert(name);
    }
    return true;
}

void cTSWantSimulatorTuningLoader::LoadTuning(cTSWantSimulator *simulator)
{
    simulator->mCategoryWants.clear();
    reinterpret_cast<RefPtrVector *>(&simulator->mVec68)->Clear();

    void *const resources = OriginalFunction<void *(__cdecl *)(), 0x20deb5>()();
    struct RecordKey {
        uint32_t words[4];
    } key{{0, kTuningType, kTuningGroup, 0}};
    void *record = nullptr;
    const bool found =
        VirtualCall<bool(__thiscall *)(void *, RecordKey *, void **, uint32_t), 39>(resources, &key, &record, 0);
    if (found) {
        void *stream = nullptr;
        if (VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(record, kStreamIid, &stream)) {
            const uint32_t size = VirtualCall<uint32_t(__thiscall *)(void *), 52>(stream);
            auto *const buffer = new uint8_t[size];
            VirtualCall<bool(__thiscall *)(void *, void *, uint32_t), 27>(stream, buffer, size);
            void *const read = stream;
            stream = nullptr;
            ReleaseObject(read);

            void *parser = nullptr;
            void *const framework = GetDirectorFrameworkObject();
            if (VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, void **), 3>(framework, kParserClass, kParserIid,
                                                                                        &parser)) {
                auto *const loader = new cTSWantSimulatorTuningLoader;
                loader->AddRef();
                loader->mOwner = simulator;
                simulator->mCategoryWants.clear();
                simulator->mLevelTables.clear();
                simulator->mMap7c.clear();
                VirtualCall<void(__thiscall *)(void *, cIGZExpatContentHandler *), 8>(parser, loader);
                VirtualCall<bool(__thiscall *)(void *, const void *, uint32_t, uint32_t), 3>(parser, buffer, size, 1);
                loader->Release();
            }
            delete[] buffer;
            if (parser) ReleaseObject(parser);
        }
        VirtualCall<void(__thiscall *)(void *), 6>(record);
        if (stream) ReleaseObject(stream);
    } else {
        const std::vector<int32_t> thresholds(std::begin(kDefaultThresholds), std::end(kDefaultThresholds));
        for (int32_t level : kDefaultLevelKeys) simulator->mLevelTables[level] = thresholds;
    }
    if (record) ReleaseObject(record);
}

void __fastcall cTSWantSimulatorTuningLoaderAdapters::LoadTuning(cTSWantSimulator *simulator, void *)
{
    [[msvc::forceinline_calls]] cTSWantSimulatorTuningLoader::LoadTuning(simulator);
}
