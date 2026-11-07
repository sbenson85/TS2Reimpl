#pragma once
// cTSWantSimulatorTuningLoader (lane IL): the expat content handler that reads the want system's tuning resource
// (WantSimulator, type 0xcda53b6f, group 0x2d7ee26b) into a cTSWantSimulator at start-up: the influence curve and its
// inverse, the category wants, the default wants (lifetime goals and wants), the level thresholds, the category icons and
// string sets, and which skills and careers the tuning names. Game vtable 0xdcae44 (12 slots), 0x3c bytes, built inline by
// the simulator's LoadTuning (0x494030, called by Init 0x4905e0), which is its only creation site.
//
// Standalone shape (docs/standalone-shape.md): a real class on cIGZExpatContentHandler; LoadTuning (hooked) builds ours, so
// the game's vtable is only referenced by the replaced original; the slots run through our vtable ("in_effect": "callers").
#include "gz/cIGZPropertySetSerializer.h"

#include <cstdint>
#include <set>
#include <string>

class cTSWantSimulator;

class cTSWantSimulatorTuningLoader : public cIGZExpatContentHandler {
public:
    static constexpr uint32_t kIid = 0x4b6b9c9b; // cIGZExpatContentHandler

    // Modes between the lifetime element tags (+0xc).
    enum Mode : uint32_t { kModeNone = 0, kModeLifetimeGoals = 1, kModeLifetimeWants = 2 };

    // LoadTuning's inline construction: reference count and mode zero, four empty sets. The original leaves the aspiration,
    // the level key and the careers flag unset until the elements that set them; ours zeroes them (only an ill-formed
    // tuning, a level or career element before the elements that set them, would read them).
    cTSWantSimulatorTuningLoader() = default;
    cTSWantSimulatorTuningLoader(const cTSWantSimulatorTuningLoader &) = delete;
    cTSWantSimulatorTuningLoader &operator=(const cTSWantSimulatorTuningLoader &) = delete;

    // 0x494030 (__thiscall on the simulator; Init's step): clears the category wants and the default wants, then looks up
    // the tuning record. Found: its stream is read into a buffer and parsed by the framework's expat parser with a new
    // loader as the handler (the category wants, level tables and category map are cleared first). Not found: every level
    // key (0..4, 13) gets the default thresholds.
    static void LoadTuning(cTSWantSimulator *simulator);

    // 0x48de50 (__cdecl): the skill ids the want system scores.
    static void CollectSkillIds(std::set<int16_t> *skills);
    // 0x48d470 (__cdecl): the object table's career objects by kind (adult careers, teen/elder careers, college majors).
    static void CollectCareerObjects(std::set<void *> *adult, std::set<void *> *teenElder, std::set<void *> *major);

    // slot 0 (shared_vf_492810, folded with cTSWantTreeLoader: never hooked): iid 1 or the handler's iid answer this.
    bool QueryInterface(uint32_t iid, void **out) override;
    uint32_t AddRef() override;  // slot 1 (shared 0x145260): ++count at +8
    uint32_t Release() override; // slot 2 (0x494830): the last release destroys the sets and frees the 0x3c bytes
    // slot 3 (0x48ece0): `count` is the number of attribute strings (name, value pairs). Always true.
    bool StartElement(const char *name, const char **attributes, uint32_t count, uint32_t unused) override;
    bool EndElement(const char *name) override; // slot 4 (0x48e390): always true
    // slots 5-11: the loader accepts everything else (shared bodies returning true: 0x1477b0 for 5/8/10, 0x144910 for 6/7,
    // 0x148260 for 9, 0x1ac160 for 11).
    bool CharacterData(const char *, uint32_t) override { return true; }
    bool Callback6() override { return true; }
    bool Callback7() override { return true; }
    bool Callback8(uint32_t, uint32_t) override { return true; }
    bool Callback9(uint32_t) override { return true; }
    bool Callback10(uint32_t, uint32_t) override { return true; }
    bool Callback11(uint32_t, uint32_t, uint32_t, uint32_t) override { return true; }

    // The element names neither handler knows, each kept once (the original's function-local static sets 0x10daa7c for
    // StartElement and 0x10daa88 for EndElement, std::set<cRZString>; nothing reads them). Exposed for the tests.
    static std::set<std::string> &UnknownStartTags();
    static std::set<std::string> &UnknownEndTags();

    cTSWantSimulator *mOwner = nullptr; // 0x04 (set after the first AddRef; the original leaves it unset before)
    int32_t mRefCount = 0;              // 0x08
    uint32_t mMode = kModeNone;         // 0x0c
    uint32_t mAspiration = 0;           // 0x10 category flag of the current aspiration element (1 .. 0x100)
    int32_t mLevelKey = 0;              // 0x14 key into the simulator's level tables (life stage)
    std::set<int16_t> mSkills;          // 0x18 skills the tuning has not named yet
    std::set<void *> mAdultCareers;     // 0x20 career objects of kind 0x20 the tuning has not named
    std::set<void *> mTeenElderCareers; // 0x28 kind 0x2e
    std::set<void *> mMajorCareers;     // 0x30 kind 0x61
    bool mHasCareers = false;           // 0x38 the object table had adult careers at the wantSimulator element

private:
    ~cTSWantSimulatorTuningLoader() = default; // Release's `delete this` (the original: sized delete of 0x3c)
};

static_assert(sizeof(cTSWantSimulatorTuningLoader) == 0x3c, "cTSWantSimulatorTuningLoader size");

// The hooked functions' entry points (thiscall on the simulator as __fastcall).
struct cTSWantSimulatorTuningLoaderAdapters {
    static void __fastcall LoadTuning(cTSWantSimulator *simulator, void *);
};
