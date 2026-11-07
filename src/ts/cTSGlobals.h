#pragma once
#include "gz/cIGZUnknown.h"
#include "rz/RZFrameworkHelpers.h" // RZUnknownSlot
#include "rz/RZSystemServiceBase.h"

#include <cstddef>
#include <cstdint>

// cTSGlobals (RVAs 1.18.0.187; docs/startup-map.md 4.5): the application service that holds the game's global interface
// pointers (the current neighbourhood, the managers, the catalog, the object tables...) behind getters and setters.
// Service id 0x7a3e667d, priority 0xfff0bdc0. Lane KE took it to the standalone shape (docs/standalone-shape.md, stage A).

// cITSGlobals (interface id 0x7a3e566d): the getters and setters (game vtable 0xe9944c, 59 slots; the interface's own
// purecall vtable 0xe9935c is what the original constructor stores first). cTSGlobals is its only implementer. The
// getters return the held pointer (folded `mov eax, [ecx + N]; ret` bodies), the setters assign it with RZUnknownSlot's
// Assign (0x142080: AddRef of the new one, Release of the old one) and return the field's address (`add ecx, N; jmp
// 0x142080`, ret 4). Names come from db/methods facts where the object is known; the others name the field.
class cITSGlobals : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x7a3e566d;

    virtual void *GetField20() = 0;              // slot 3
    virtual void *GetWantFearService() = 0;      // slot 4 (+0x24, cTSWantSimulator; tentative)
    virtual void *GetField28() = 0;              // slot 5
    virtual void *GetNeighborhood() = 0;         // slot 6 (+0x30)
    virtual void *GetField34() = 0;              // slot 7
    virtual void *GetNeighborhoodManager() = 0;  // slot 8 (+0x38; tentative)
    virtual void *GetFamilyManager() = 0;        // slot 9 (+0x60)
    virtual void *GetNeighborManager() = 0;      // slot 10 (+0x64)
    virtual void *GetNeighborhoodTerrain() = 0;  // slot 11 (+0x68)
    virtual void *GetField6C() = 0;              // slot 12
    virtual void *GetField2C() = 0;              // slot 13
    virtual void *GetField3C() = 0;              // slot 14
    virtual void *GetField44() = 0;              // slot 15
    virtual void *GetCatalog() = 0;              // slot 16 (+0x40)
    virtual void *GetGlobalInventory() = 0;      // slot 17 (+0x8c)
    virtual void *GetField48() = 0;              // slot 18
    virtual void *GetField4C() = 0;              // slot 19
    virtual void *GetField50() = 0;              // slot 20
    virtual void *GetField54() = 0;              // slot 21
    virtual void *GetField58() = 0;              // slot 22
    virtual void *GetGameStateController() = 0;  // slot 23 (+0x5c)
    virtual void *GetObjectDefTable() = 0;       // slot 24 (+0x70)
    virtual void *GetField74() = 0;              // slot 25
    virtual void *GetObjectTable() = 0;          // slot 26 (+0x78)
    virtual void *GetField7C() = 0;              // slot 27
    virtual void *GetField80() = 0;              // slot 28
    virtual void *GetField84() = 0;              // slot 29
    virtual void *GetField88() = 0;              // slot 30
    virtual bool TestingCheatsEnabled() = 0;     // slot 31 (0xc2cba3)
    virtual bool GetFlag90() = 0;                // slot 32 (the byte at +0x90, raw)
    virtual void *GetField94() = 0;              // slot 33
    virtual void *SetField20(void *object) = 0;             // slot 34
    virtual void *SetWantFearService(void *object) = 0;     // slot 35
    virtual void *SetField28(void *object) = 0;             // slot 36
    virtual void *SetNeighborhood(void *object) = 0;        // slot 37
    virtual void *SetField34(void *object) = 0;             // slot 38
    virtual void *SetNeighborhoodManager(void *object) = 0; // slot 39
    virtual void *SetNeighborhoodTerrain(void *object) = 0; // slot 40
    virtual void *SetField6C(void *object) = 0;             // slot 41
    virtual void *SetFamilyManager(void *object) = 0;       // slot 42
    virtual void *SetNeighborManager(void *object) = 0;     // slot 43
    virtual void *SetField3C(void *object) = 0;             // slot 44
    virtual void *SetField2C(void *object) = 0;             // slot 45
    virtual void *SetGlobalInventory(void *object) = 0;     // slot 46
    virtual void *SetField48(void *object) = 0;             // slot 47
    virtual void *SetField4C(void *object) = 0;             // slot 48
    virtual void *SetField50(void *object) = 0;             // slot 49
    virtual void *SetObjectDefTable(void *object) = 0;      // slot 50
    virtual void *SetField74(void *object) = 0;             // slot 51
    virtual void *SetObjectTable(void *object) = 0;         // slot 52
    virtual void *SetField80(void *object) = 0;             // slot 53
    virtual void *SetField84(void *object) = 0;             // slot 54
    virtual void *SetField88(void *object) = 0;             // slot 55
    virtual void SetTestingCheatsEnabled(uint32_t value) = 0; // slot 56 (0xc2c9f6, the low byte is the flag)
    virtual void SetFlag90(bool value) = 0;                 // slot 57 (stores the byte as given)
    virtual void *SetField94(void *object) = 0;             // slot 58
};

// A held interface pointer: null when built, assigned through RZUnknownSlot::Assign, Released (slot 2) when destroyed if
// set (the original's destructor releases the fields from the last one down without clearing them).
struct cTSGlobalsRef : RZUnknownSlot {
    cTSGlobalsRef() { mObject = nullptr; }
    ~cTSGlobalsRef();
    cTSGlobalsRef(const cTSGlobalsRef &) = delete;
    cTSGlobalsRef &operator=(const cTSGlobalsRef &) = delete;
};
static_assert(sizeof(cTSGlobalsRef) == 4, "cTSGlobalsRef: one pointer");

// The class (0x98 bytes): RTTI bases cITSGlobals at +0, cRZSystemService at +4 (cIGZSystemService +4, cRZUnknownMT +8).
// Game vtables 0xe9944c (59 slots), 0xe9953c (13: Init slot 8, Shutdown slot 9), 0xe99574 (6: slot 3 the deleting
// destructor, thunk 0xc2c811 into 0xc2c819).
//
// Creation: the constructor 0xc2c4cf is called only by cTSAppFW::AddApplicationServicesHere (ours, builds through the
// adapter). The game's vtables are referenced only by the constructor and the destructor 0xc2c58b (called only by the
// deleting destructor 0xc2c819, reached only through the game's +8 vtable).
class cTSGlobals : public cITSGlobals, public rz::cRZSystemService {
public:
    static constexpr uint32_t kVTable = 0xe9944c;               // the game's, for tests
    static constexpr uint32_t kSystemServiceVTable = 0xe9953c;
    static constexpr uint32_t kUnknownVTable = 0xe99574;
    static constexpr uint32_t kSize = 0x98;
    static constexpr uint32_t kServiceId = 0x7a3e667d;
    static constexpr int32_t kPriority = static_cast<int32_t>(0xfff0bdc0);
    static constexpr uint32_t kInterfaceId = cITSGlobals::kIid;

    // 0xc2c4cf: the service part (id, priority), every pointer null, the byte at +0x90 cleared.
    cTSGlobals();
    // 0xc2c58b (deleting 0xc2c819, +8 thunk 0xc2c811): Shutdown (called directly), then the fields' Releases from +0x94
    // down, then the service part.
    ~cTSGlobals() override;
    cTSGlobals(const cTSGlobals &) = delete;
    cTSGlobals &operator=(const cTSGlobals &) = delete;

    // cIGZUnknown (every face)
    bool QueryInterface(uint32_t riid, void **ppv) override; // 0xc2c948 (+4 thunk 0xc2c96a, +8 0xc2c972)
    uint32_t AddRef() override;                              // folded 0x145240 (the service part's)
    uint32_t Release() override;                             // folded 0x1a85b0

    // cIGZSystemService (`this` is the +4 face, as the original's)
    // 0xc2c83b: fetches five interfaces from the framework (0x1ae18d's object, slot 3 with an id pair and the field) after
    // Releasing what the field held: +0x44 (then its slot 3), +0x40, +0x7c, +0x58 (then its slots 15 and 17, the second's
    // result assigned to +0x54), +0x5c (then its slot 3). Returns true.
    bool Init() override;
    // 0xc2cac9: assigns null to every field that holds an object (in the original's order), calling slot 4 of +0x5c's
    // object, slot 16 of +0x58's and slot 4 of +0x44's first. Returns true.
    bool Shutdown() override;

    // cITSGlobals: the getters (folded bodies, ours through our vtable)
    void *GetField20() override { return mField20.mObject; }
    void *GetWantFearService() override { return mWantFearService.mObject; }
    void *GetField28() override { return mField28.mObject; }
    void *GetNeighborhood() override { return mNeighborhood.mObject; }
    void *GetField34() override { return mField34.mObject; }
    void *GetNeighborhoodManager() override { return mNeighborhoodManager.mObject; }
    void *GetFamilyManager() override { return mFamilyManager.mObject; }
    void *GetNeighborManager() override { return mNeighborManager.mObject; }
    void *GetNeighborhoodTerrain() override { return mNeighborhoodTerrain.mObject; }
    void *GetField6C() override { return mField6C.mObject; }
    void *GetField2C() override { return mField2C.mObject; }
    void *GetField3C() override { return mField3C.mObject; }
    void *GetField44() override { return mField44.mObject; }
    void *GetCatalog() override { return mCatalog.mObject; }
    void *GetGlobalInventory() override { return mGlobalInventory.mObject; }
    void *GetField48() override { return mField48.mObject; }
    void *GetField4C() override { return mField4C.mObject; }
    void *GetField50() override { return mField50.mObject; }
    void *GetField54() override { return mField54.mObject; }
    void *GetField58() override { return mField58.mObject; }
    void *GetGameStateController() override { return mGameStateController.mObject; }
    void *GetObjectDefTable() override { return mObjectDefTable.mObject; }
    void *GetField74() override { return mField74.mObject; }
    void *GetObjectTable() override { return mObjectTable.mObject; }
    void *GetField7C() override { return mField7C.mObject; }
    void *GetField80() override { return mField80.mObject; }
    void *GetField84() override { return mField84.mObject; }
    void *GetField88() override { return mField88.mObject; }
    bool TestingCheatsEnabled() override; // forwards to as2::TestingCheatsEnabled (0xc2cba3, group AppServices2)
    bool GetFlag90() override { return mFlag90; }
    void *GetField94() override { return mField94.mObject; }

    // cITSGlobals: the setters (0xc2c9ee and the other cTSGlobals::vfNN bodies, plus the folded ones)
    void *SetField20(void *object) override { return mField20.Assign(object); }
    void *SetWantFearService(void *object) override { return mWantFearService.Assign(object); }
    void *SetField28(void *object) override { return mField28.Assign(object); }
    void *SetNeighborhood(void *object) override { return mNeighborhood.Assign(object); }
    void *SetField34(void *object) override { return mField34.Assign(object); }
    void *SetNeighborhoodManager(void *object) override { return mNeighborhoodManager.Assign(object); }
    void *SetNeighborhoodTerrain(void *object) override { return mNeighborhoodTerrain.Assign(object); }
    void *SetField6C(void *object) override { return mField6C.Assign(object); }
    void *SetFamilyManager(void *object) override { return mFamilyManager.Assign(object); }
    void *SetNeighborManager(void *object) override { return mNeighborManager.Assign(object); }
    void *SetField3C(void *object) override { return mField3C.Assign(object); }
    void *SetField2C(void *object) override { return mField2C.Assign(object); }
    void *SetGlobalInventory(void *object) override { return mGlobalInventory.Assign(object); }
    void *SetField48(void *object) override { return mField48.Assign(object); }
    void *SetField4C(void *object) override { return mField4C.Assign(object); }
    void *SetField50(void *object) override { return mField50.Assign(object); }
    void *SetObjectDefTable(void *object) override { return mObjectDefTable.Assign(object); }
    void *SetField74(void *object) override { return mField74.Assign(object); }
    void *SetObjectTable(void *object) override { return mObjectTable.Assign(object); }
    void *SetField80(void *object) override { return mField80.Assign(object); }
    void *SetField84(void *object) override { return mField84.Assign(object); }
    void *SetField88(void *object) override { return mField88.Assign(object); }
    void SetTestingCheatsEnabled(uint32_t value) override; // forwards to as2::SetTestingCheatsEnabled (0xc2c9f6)
    void SetFlag90(bool value) override { mFlag90 = value; }
    void *SetField94(void *object) override { return mField94.Assign(object); }

    cTSGlobalsRef mField20;              // +0x20
    cTSGlobalsRef mWantFearService;      // +0x24
    cTSGlobalsRef mField28;              // +0x28
    cTSGlobalsRef mField2C;              // +0x2c
    cTSGlobalsRef mNeighborhood;         // +0x30
    cTSGlobalsRef mField34;              // +0x34
    cTSGlobalsRef mNeighborhoodManager;  // +0x38
    cTSGlobalsRef mField3C;              // +0x3c
    cTSGlobalsRef mCatalog;              // +0x40 (Init: ids 0xaa565060, 0xdadba5f9)
    cTSGlobalsRef mField44;              // +0x44 (Init: ids 0x1ab66100, 0x1ab66099)
    cTSGlobalsRef mField48;              // +0x48
    cTSGlobalsRef mField4C;              // +0x4c
    cTSGlobalsRef mField50;              // +0x50
    cTSGlobalsRef mField54;              // +0x54 (Init: slot 17 of +0x58's object)
    cTSGlobalsRef mField58;              // +0x58 (Init: ids 0x2d1a2550, 0xed1a2513)
    cTSGlobalsRef mGameStateController;  // +0x5c (Init: ids 0x0bea1c24, 0xabea1c19)
    cTSGlobalsRef mFamilyManager;        // +0x60
    cTSGlobalsRef mNeighborManager;      // +0x64
    cTSGlobalsRef mNeighborhoodTerrain;  // +0x68
    cTSGlobalsRef mField6C;              // +0x6c
    cTSGlobalsRef mObjectDefTable;       // +0x70
    cTSGlobalsRef mField74;              // +0x74
    cTSGlobalsRef mObjectTable;          // +0x78
    cTSGlobalsRef mField7C;              // +0x7c (Init: ids 0xac4761c4, 0xac4761be)
    cTSGlobalsRef mField80;              // +0x80
    cTSGlobalsRef mField84;              // +0x84
    cTSGlobalsRef mField88;              // +0x88
    cTSGlobalsRef mGlobalInventory;      // +0x8c
    bool mFlag90;                        // +0x90 (a byte handed out raw by slot 32)
    cTSGlobalsRef mField94;              // +0x94
};
static_assert(sizeof(cTSGlobals) == cTSGlobals::kSize, "cTSGlobals size");
static_assert(offsetof(cTSGlobals, mServiceId) == 0x10, "cTSGlobals layout: the service part at +4");
static_assert(offsetof(cTSGlobals, mField20) == 0x20, "cTSGlobals layout");
static_assert(offsetof(cTSGlobals, mGameStateController) == 0x5c, "cTSGlobals layout");
static_assert(offsetof(cTSGlobals, mGlobalInventory) == 0x8c, "cTSGlobals layout");
static_assert(offsetof(cTSGlobals, mFlag90) == 0x90, "cTSGlobals layout");
static_assert(offsetof(cTSGlobals, mField94) == 0x94, "cTSGlobals layout");

// The hooks' entry points (docs/standalone-shape.md, step 6). The other slots, destructors and thunks have no adapter: only
// the game's vtables reach their RVAs (in effect through callers).
struct cTSGlobalsAdapters {
    static cTSGlobals *__fastcall Construct(void *memory, void *edx); // 0xc2c4cf
    // 0x760d95 (slot 37): also called directly by cTSAlbumLoader::vf3 0x7609fd on its own object (the same assignment of
    // the field at +0x30), so it keeps its hook.
    static void *__fastcall SetNeighborhood(cITSGlobals *self, void *edx, void *object);
};
