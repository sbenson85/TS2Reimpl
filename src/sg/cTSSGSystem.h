#pragma once
#include "gz/cGZEventReflector.h"
#include "rz/cRZStopwatch.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

// nTSSG::cTSSGSystem (the Sims-specific scene-graph system service; RVAs 1.18.0.187). One object, 0x3b8 bytes, built by
// the TSSG COM director's OnStart (0x2454c0) with the constructor 0x246110 and destroyed by 0x246f00 (from slot 88).
// Three faces:
//   - the primary (vtable 0xdae264, 100 slots; RTTI cAbstractBehaviorTypeT<cITSSGSystem, cRefMultiThreaded>), `this`;
//   - cIGZMessageTarget2 (vtable 0xdae3f8, 4 slots) at +8: DoMessage 0x249130;
//   - cIGZSystemService (vtable 0xdae40c, 13 slots) at +0xc: Init 0x24afe0, Shutdown 0x255010.
//
// The class is still hook-shaped (lane IR): the members below are hooked at their RVAs, and the constructor and destructor
// (ours since lane KE) put the game's vtables on the object, so the game's slots it does not replace still run.
// The layout is the constructor's (0x246110), with the fields named from the members that use them. The managers at
// +0x68..+0x108 are the scene graph's sub-managers the start-up (CreateManagers 0x24d260) gets from the framework by class id
// (the class each id builds is in the comment: the factory registered for it in the TSSG director's table,
// src/rz/COMDirectorData.cpp).
namespace nTSSG {

// The registry of cTSSGSystem's event behaviour type (0x253de0, cdecl; src/sg/cTSSGSystemBehavior.cpp). Built on first use
// (its holder 0x2455e0: the base events, then 0xca7ccbbc and 0x2b5745f8 through this function; the map a copy of the base
// registry 0x1485f0); a non-zero id stores `attribute`. Not inlined: tests stand in for it.
struct cTSSGSystemBehaviorTraits {
    __declspec(noinline) static nGZEvent::cEventAttributeMap *__cdecl RegistryFor(uint32_t id,
                                                                                const nGZEvent::cEventAttribute *attribute);
    static void ResetRegistryForTest(); // tests only
};

// A reference to a framework object (AddRef slot 1, Release slot 2) as the system holds it.
struct SGRef {
    void *mObject;
};
static_assert(sizeof(SGRef) == 4);

// The 4-byte elements of the vector at +0x17c: texture handles (nGZSceneGraph::cTextureHandle; their destructor is 0xae82b0).
struct cTSSGEntryHandle {
    void *mValue;
    ~cTSSGEntryHandle();
};
static_assert(sizeof(cTSSGEntryHandle) == 4);

// A reference held through the object's virtual base (virtual-base table at +4, entry 1): released there (slot 2) when
// it goes, as the game's vector helpers 0x245720 / 0x245790 do for the cinematic vectors. A copy adds a reference there
// (slot 1), as the game's reallocating inserts 0x245830 / 0x245a60 do; with no move, our vectors copy then destroy the
// old elements when they grow, the same calls in the same order.
struct SGVirtualRef {
    void *mObject = nullptr;
    SGVirtualRef() = default;
    SGVirtualRef(const SGVirtualRef &other);
    SGVirtualRef &operator=(const SGVirtualRef &) = delete;
    ~SGVirtualRef();
};
static_assert(sizeof(SGVirtualRef) == 4);

// The 12-byte elements of the cinematic vectors at +0x12c and +0x138: the object (slot 97, the Cinematic1..3 cleanup)
// and, for the first vector, a flag asking for its slot 120 object's slot 47 value to go back to the app's slot 26.
struct cTSSGCinematicActor {
    SGVirtualRef mObject; // +0
    uint32_t mValue;      // +4 (not read here)
    uint8_t mRestore;     // +8
    uint8_t mPad[3];
};
static_assert(sizeof(cTSSGCinematicActor) == 12);

class cTSSGSystem {
public:
    // The game's vtables: the primary face, cIGZMessageTarget2 at +8, cIGZSystemService at +0xc, and the base the
    // destructor leaves (cAbstractBehaviorTypeT, 0x2460a0 / 0x246bd0).
    static constexpr uint32_t kVTable = 0xdae264;
    static constexpr uint32_t kMessageVTable = 0xdae3f8;
    static constexpr uint32_t kServiceVTable = 0xdae40c;
    static constexpr uint32_t kBaseVTable = 0xdae0fc;
    static constexpr uint32_t kSize = 0x3b8;

    // ---- construction and destruction (lane KE; src/sg/cTSSGSystemLifetime.cpp) ----

    // 0x246110 (__thiscall, returns this; hooked through cTSSGSystemAdapters::Construct): the game's vtables on the three
    // faces, every reference and counter zero, the two stopwatches in mode 4, the render options, statistics buckets,
    // lot tunables (1, 3, 21), the eight colours, the pulse (0.2, 1), the object fade (10, 4, 10, 0.25), the camera from
    // the game's default position and forward axis, empty actor bounds, then the 24 message ids. The padding bytes and
    // the dwords +0x1c and the stopwatches' +4 are left as they were.
    cTSSGSystem();

    // 0x246f00 (__thiscall; hooked; slot 88 calls it): the game's vtables back on the three faces, EndCinematic(1), then
    // every member released in reverse order (plain references through slot 2, those held through a virtual base there,
    // the overlays from the last, the vectors' elements first to last as their types say), the message ids, and the
    // base vtable last.
    void Destruct();

    // ---- non-virtual members (lane IR) ----
    // The members other members call are noinline: each is hooked at its own RVA and the tests stand them in on both
    // sides, which only works when the caller really calls them.

    // 0x24d260 (__thiscall, no arguments, EH; once: returns at once when mManagersCreated is set). Run from DoMessage on
    // message 0xcf. Registers the system service with the framework (slot 9), runs the two scene-graph set-up helpers
    // 0x3a18b0 / 0x3a3c30, starts the start-up stopwatch, creates overlays 0, 1, 5, 6 and 8, then gets every sub-manager
    // from the framework (slot 3) and initialises it (its slot 3), giving the light-rig manager, the +0xb4 object, the
    // command-file parser and the scene's +0x58 object the first file (or every file) of their search-path lists that
    // exists. Then the three animation-clock helpers (0x248f90), the three lot tunables (0x248720), the render-option
    // watches (the property notifier's slot 4, with the defaults as AnyBoolean / AnyUint32), the light-bounds default, and
    // with the debug-mode app and the asset-server switch on the command line, the asset collector. Sets mManagersCreated.
    __declspec(noinline) void CreateManagers();

    // 0x248c80 (__thiscall, ret 4): overlay `index` (of mOverlays): released and cleared, got again from the framework
    // (class 0xac22fa8d, interface 0xac22fa89) and, when there is one, initialised (its slot 3).
    __declspec(noinline) void CreateOverlay(uint32_t index);

    // 0x248720 (__thiscall, ret 4): the three lot tunables at +0x2b8 / +0x2bc / +0x2c0 from `properties` (the command-file
    // parser's property object), each through the float reader 0xaf7190 with the name at 0xdaf038 / 0xdaf048 / 0xdaf058.
    __declspec(noinline) void ReadLotTunables(void *properties);

    // 0x248f90 (__thiscall, EH): the three procedural animations: the objects the builders 0x3a27f0 (out) and 0x3a25e0(1,
    // out) / 0x3a25e0(0, out) give, each added (0x24a280) under its name (0xdae7c0 / 0xdae7e0 / 0xdae808) when the builder
    // succeeds. Each object is held through its virtual base (virtual-base table at +4, entry 1) and released before the
    // next build.
    __declspec(noinline) void AddProceduralAnimations();

    // 0x24a280 (__thiscall, ret 8): `animation` (virtual-base table at +4, entry 1 its base) gets `name` (its slot 12, with
    // 0), its key object (the base's slot 8) the key parsed from the name (StringKeyParser::ParseKey with the group
    // 0xfb00791d), and the animation system (mAnimSystem slot 13) the animation.
    __declspec(noinline) void AddProceduralAnimation(void *animation, const char *name);

    // ---- messages (lane IR; src/sg/cTSSGSystemMessages.cpp) ----

    // 0x249130 is cIGZMessageTarget2::DoMessage on the face at +8 (cTSSGSystemMessageFace below forwards here): the
    // message's data (its QueryInterface 0x65297976) and type (slot 4) choose an option to store, a manager to tell, or a
    // start-up step (0xcf CreateManagers, 0xd3 ReadShaderOptions 0x24ebd0, 0xd4 0x24c980, 0xd0 0x24f4e0). False for a
    // type the system does not handle (and for a null message).
    bool DoMessage(void *message);

    // 0x254dc0 (__thiscall, ret 8): the lot's level range [first, last] to the scene manager (its slots 19, 20, 5 and
    // slot 12(level, 0) for every level it did not have), and to the floor, wall, fence, roof, rug and awning managers;
    // the curved-pool and cutout managers get their slot 7. The placement service (0x20e0b2) gets its slots 85 and 86
    // first.
    __declspec(noinline) void SetLevelRange(int32_t first, int32_t last);

    // 0x2556a0 (__thiscall): the material-definition object (GetCachedQueriedObject) reloads ten materials (its slot 30
    // with each name), then RefreshMaterials.
    __declspec(noinline) void ReloadMaterials();

    // 0x24b160 (__thiscall): the detail object's (0xaead80) slot 42 object, as interface 0x2ca4c07d, gets its slot 6.
    __declspec(noinline) void RefreshMaterials();

    // 0x247fd0 (__thiscall, ret 4): the app's (GetCachedSingleton) slot 10 object's entry `id` (slot 16(id, 1), the id
    // sign-extended from 16 bits) gives, through its slot 10 and that one's slot 67, an object whose slot 12 fills a
    // cRZString that is then dropped.
    __declspec(noinline) void TouchEntry(uint32_t id);

    // ---- primary slots (lane IR; src/sg/cTSSGSystemSlots.cpp) ----

    // 0x253d70 slot 0 QueryInterface: null `out` gives false; the system's two interface ids (0xba353ce1, 0xfa353cdb)
    // give this, AddRef'd (slot 1); any other goes to the base's 0x148310 (a tail call).
    bool QueryInterface(uint32_t iid, void **out);

    // 0x2543e0 slot 2 Release (cAbstractBehaviorTypeT; 0x254470 jumps here): the count is the low 31 bits of +4. At one,
    // the event manager (GetEventManager), when there is one, forgets the object (its slot 5) and the object deletes
    // itself (slot 88 with 1); 0 is returned. Otherwise the count drops by one (RZAtomicAdd) and the new count is returned.
    uint32_t Release();

    // 0x24abf0 slot 3, the event handler (event, a, data, d): first the base's (BehaviorOnEvent), then for 0x2b5745f8
    // with data kind 2 RefreshDetailFloat's work; for 0xca7ccbbc with kind 1 the shadow manager's slot 7 and the persist
    // service's (0xb390c0) slot 39, then the byte +0x16c cleared; with kind 4 the byte set, then the persist slot 39.
    // (The original builds two small stack objects around the kind whose destructors run only on unwinding.)
    void OnEvent(uint32_t event, uint32_t a, void *data, uint32_t d);

    // 0x24abb0 (cAbstractBehaviorTypeT's slot 3): for event 0x5b51e806 the event manager's slot 6(this) says whether the
    // object is active; when that differs from the top bit of +4, the bit is flipped (RZAtomicAdd 0x80000000).
    __declspec(noinline) void BehaviorOnEvent(uint32_t event, uint32_t a, void *data, uint32_t d);

    // 0x248790 (static): the event manager, cached at 0x10cc8a8: filled once from the service (0x5b1e896f as 0x1b1e895a,
    // its slot 3), the service object released (0x1466e0).
    __declspec(noinline) static void *GetEventManager();

    // 0x248770 / 0x249110 (slots 8 / 9, cAbstractBehaviorTypeT): the event manager's slot 3 / 4 with (this, a, b, c).
    // 0x254c70 / 0x24a350 (slots 5 / 6, cAbstractBehaviorTypeT, ret 0xc): only while active (the top bit of the count).
    // The event manager's slot 10 gives the listeners of `kind` for this object as a range (iterator, a second
    // reference, end; each released, end first). Per listener (the iterator's slot 5 twice: the first pair's first word
    // the listener, the second pair's second word its value): slot 5 calls the listener's slot 3 (value, a, b, this);
    // slot 6 asks its slot 4 (value, a, b) and answers whether there was one and every one said yes.
    void NotifyListeners(uint32_t kind, uint32_t a, uint32_t b);
    bool AskListeners(uint32_t kind, uint32_t a, uint32_t b);
    void EventManagerSlot3(uint32_t a, uint32_t b, uint32_t c);
    void EventManagerSlot4(uint32_t a, uint32_t b, uint32_t c);

    // 0x254f80 slot 15: mRefC0 = `object` (references counted through the virtual base, table at +4, entry 1).
    void SetRefC0(void *object);

    // 0x247b00 slot 16: mCounter194 += delta unless it is 0x80000100; mCounter198 += delta when non-zero.
    void AdvanceCounters(int32_t delta);

    // 0x254d60 slot 18: +0x38 = value; when slot 52's object is there, mCounter198 = 0 for a null value, else its virtual
    // base's (table at +4, entry 2) slot 11.
    void SetValue38(void *value);

    // 0x252410 slot 19: clears the byte +0x16b; when the parser target's slot 6 differs from `value`, gives it the value
    // (slot 5), rereads the lot tunables from the parser (slot 9), and gives slot 49's object's slot 25 object (its slot 9)
    // slot 33's object's slot 17.
    void SetParserValue(uint32_t value);

    // 0x24b210 slot 56: mRefB4 and mRef124 there, mRef124's slot 33 object there and its slot 60 false.
    bool IsSlot56();

    // 0x247f80 / 0x24a930 slots 67 / 74: the app's (GetCachedSingleton) slot 22 object's slot 20 / 25.
    uint32_t AppObjectSlot20();
    uint32_t AppObjectSlot25();

    // 0x24b1e0 slot 72: bytes +0x18f and +0x18e both set.
    bool BothOptions18E();

    // 0x247ad0 slot 73: lot tunable 1 for 3, 2 for 4, else 0.
    float GetLotTunable(int32_t which);

    // 0x24ad30 slot 78: colour `index` (unsigned; 8 of them), or the default at 0xff796c.
    struct Color4 {
        float v[4];
    };
    Color4 GetColor(uint32_t index);

    // 0x24bed0 slot 79: with mObjectFadeSet and a lot world (GetCachedSlot23 slot 5 == 0x0bea28c2), the 20 bytes of
    // mObjectFade to `out` and true; else false.
    bool GetLotBounds(void *out);

    // 0x253dd0 slot 83: 0x274a50(target).
    void FillTarget(void *target);

    // 0x24a320 slot 87: with the bytes +0x16a and +0x16b set, the detail object (0xaead80) gets the float +0x3c (slot 17).
    __declspec(noinline) void RefreshDetailFloat();

    // 0x247aa0 slot 88: the destructor (Destruct), then with flags & 1 the 0x3b8-byte block freed.
    cTSSGSystem *DeletingDestructor(uint32_t flags);

    // ---- resources and save state (lane IR; src/sg/cTSSGSystemPersist.cpp). Calls to the object's own slots go
    // through its vtable, as the original's do. ----

    // 0x24a470 slot 25 (ret 0x14): the animation system's lookup (slot 17: key, name, out); found: a cache hit counted
    // (by the object's slot 59). Not found and (the lookup failed or `force`): slot 98, and when that fails the
    // animation system's slot 14(key). Whatever was found goes to the animation system's slot 15 with `arg3`.
    bool FindAnimResource(const uint32_t *key, const char *name, uint32_t arg3, void **out, uint8_t force);

    // 0x24b410 slot 98 (ret 0xc): the persist service's slot 7 with the key {0xfb00791e, 0x1c0532fa, key[0], key[1]},
    // type 0x7b007916, timed by mStartupWatch. With `name`, an object whose slot 17 name (through its virtual base,
    // table at +4, entry 1) differs from it (_stricmp) is dropped from *out (not released). Found: counted in the
    // current bucket (flagged or not) with the time.
    bool FindPersistResource(const uint32_t *key, const char *name, void **out);

    // 0x24b720 slot 76 (ret 8): loads the mRefB4 entry `name` (slot 36 of its slot 35 index) from the persist service's
    // record {0xec44bddc, group, Crc24(entry name) & 0xffff, 0} (slot 3's object, its slot 39); the entry's slot 11
    // takes the record's 0x51b11bd interface, and the record's slot 6 must agree.
    bool LoadEntry(uint32_t group, const char *name);

    // 0x2548b0 slot 77 (ret 8): saves the entry into the persist service's record (slot 34, type 0x656b8f0c; the
    // holder's slot 22 opens it, the entry's slot 10 writes the 0xc51b11cb interface, slots 24 and 10 close it).
    bool SaveEntry(uint32_t group, const char *name);

    // 0x24c890 slot 89 (ret 4): the three entries saved (slot 77 with the stream's slot 11 group), then the state
    // written (slot 97) into a stream the persist service makes (slot 70) and stores (slot 71).
    bool Save(void *stream);

    // 0x24bf20 slot 90 (ret 4): the three entries loaded (slot 76), then the stored state read (slot 96); without
    // one, mCounter194 = 50 * the live household's slot 18 (0x20df9a), handed to slot 52's object, mCounter198 = 0.
    bool Load(void *stream);

    // 0x24b9a0 slot 96 (ret 4): reads the id, version, name and mCounter194 (mCounter198 = 0); version 3 and up,
    // the animation clock offset and the resource keys (version 4 and up: pairs, each found with slot 25 under the
    // shifted clock).
    bool ReadState(void *stream);

    // 0x254a30 slot 97 (ret 4): the inverse of ReadState, at version 4.
    bool WriteState(void *stream);

    // 0x24b520 slot 94: the neighbourhood animation state, record {0x8cc0a14b, group, 0x8cc0a14b, 0} where the group
    // is the persist service's slot 11 for the name "NeighborhoodManager" (game text). With the service's slot 34
    // holder (type 0x656b8f0c): the record from slot 3's object (slot 39), its 0x51b11bd interface to the animation
    // system's slot 27, the record's slot 6. Always: the animation system's slot 40 with a 32-byte scratch buffer.
    void LoadNeighborhoodAnimation();

    // 0x2546a0 slot 95: the inverse into the same record: the holder's slot 22 (or 23 to create) opens it, the
    // 0xc51b11cb interface goes to the animation system's slot 28 (then slot 40), the holder's slots 24 and 10 close.
    void SaveNeighborhoodAnimation();

    // ---- resources by name (lane IR; src/sg/cTSSGSystemResources.cpp). Names get a suffix (game text "_anim",
    // "_cres", "_shpe") unless they end with it, and are keyed with StringKeyParser::ParseKey. ----

    // 0x24bba0 slot 21: with the render option byte +0x188 and the parser target, every name in the target's string
    // vector (+0x1ac..+0x1b0, 32-byte cRZStrings) is made an "_anim" key (lowercased; type 0xfb00791e) and, when its
    // hash is non-zero, found through slot 98 and handed to the animation system's slot 13. One resource reference
    // (through its virtual base, table at +4, entry 1) is held across the names and released at the end.
    void PreloadAnimations();

    // 0x249fd0 slot 22 (ret 8): whether the resource manager (0x20deb5, its slot 15) has the "_cres" model `name`
    // (type 0xe519c933), first with the group *group when given, then with the parsed one. "nomodel_cres" is never.
    bool HasModel(const char *name, const uint32_t *group);

    // 0x24a650 slot 23 (ret 0x18; the sixth argument unused): *out = 0, then the "_cres" model `name` (or `fallback`
    // when the name is empty) from the resource manager's slot 3 (type 0x4512dd13; with the group *group first, then
    // the parsed one, then `fallback` with "_cres" appended). A found resource's slot 15 object goes to the lighting
    // manager's slot 25 object (slot 9) and is asked for `iid` into `out`.
    void FindModel(const char *name, const uint32_t *group, const char *fallback, uint32_t iid, void **out,
                   uint32_t unused);

    // 0x24a970 slot 24 (ret 8): *out = 0, then the "_shpe" shape `name` (type 0xfc6eb1f7) from the resource manager's
    // slot 3 (type 0x3c6eb1f0); its slot 15 object asked for 0x3c6eb1f0 into `out`.
    bool FindShape(const char *name, void **out);

    // 0x24a510 slot 26 (ret 0xc): the "_anim" key of `name` (lowercased; empty for null) found through slot 25
    // (arg3 1).
    bool FindAnimation(const char *name, void **out, uint8_t force);

    // ---- the cinematic (lane IR; src/sg/cTSSGSystemCinematic.cpp) ----

    // 0x255250 slot 29 (ret 4): nothing without mCinematic. With the controller (mRef124): unless `abort`, mRefB0's
    // slot 2 (0) and slot 1 (mCinematicRestore); when its camera (slot 33) is gone, finished (slot 60) or `abort`, the
    // end: the global stopwatch 0x10cc848 stopped, mRefB0's slot 2 (1), the sound probe told (message 0xcbc79579 /
    // 0xbc7957d from the framework, slots 5 and 12), the saved values back to GetCachedSingletonB, "ObjectHiding"
    // restored, the wall manager's slots 21 (0) and 16, an unfinished camera handed mRefB0's view (slot 7, then 6),
    // the controller's slot 7 (0) and the controller dropped. Then, when `abort` or (not mCinematicHeld and no
    // controller), the actors are cleaned (slot 97 (0), 0xaeb0e0 for Cinematic1..3, the restore flag), the three
    // vectors cleared, mCinematicHeld cleared, +0x128 and the cinematic dropped.
    __declspec(noinline) void EndCinematic(uint8_t abort);

    // 0x2530c0 slot 27 (ret 0x1c; lane JD, src/sg/cTSSGSystemCinematicSetup.cpp): sets up the cinematic `name`. *error
    // = -1; nothing (false) unless the app, its slot 3 object and that object's slot 15 (0x2d) answer and no cinematic is
    // set. The name (trimmed, the cinematic suffix 0xdadff0 ensured) is looked up as a key (preset 0x4d51f042) through
    // the service's (GetServiceObject, 0x56b906e / 0x9af5e924) resource slot 5 (type 0x2d51f03a) into mCinematic (error
    // 0x87 when none). Its slot 6 parts (slot 7's name, numbered with the format 0xdaeff0 when more than one) are found
    // with slot 26 into mCinematicRefs (0x88). Its slot 16 actors: an id from `ids` (or, when slot 12 asks for it, the
    // lot's slot 16 id of the cached singleton's slot 24 / 14 object), the lot's (app slot 26) slot 29 object (its slot
    // 0 / 6 / 11 owner when slot 104), the interface 0xfa1c39f7 / 0xfa1c39ed of it (slot 9) into mCinematicActors with
    // slot 10's value and slot 12's flag, and of each of its slot 69 parts into mCinematicExtras; errors 0x89..0x8b,
    // 0x8e, 0x8f. mCinematicRef128 is slot 17's actor (0x8c, 0x8d). When mRefB4's animated entry (0xdadff8) answers
    // 0xad374e09 and the scene manager's camera passes 0xae1ed0, the flags are set (mField163, mObjectHiding,
    // mField161; the last two also when the cinematic's name is 0xdaeff8), mCinematicHeld = 1 and every actor's slot
    // 97 (1). Then slot 28 (0, error) when `playNow`; otherwise without mCinematicHeld slot 29 (1) and error 0x97.
    // Every error after the lookup ends with slot 29 (1). Returns mCinematicHeld, or slot 28's answer.
    uint8_t SetUpCinematic(const char *name, const std::vector<int16_t> *ids, uint8_t field163, uint8_t playNow,
                           uint8_t objectHiding, uint8_t field161, int32_t *error);

    // 0x250530 slot 28 (ret 8; lane JD, src/sg/cTSSGSystemCinematicPlay.cpp): plays the set-up cinematic from `start`.
    // *error = -1; an actor whose model (slot 120) is gone ends it (slot 29 (1), error 0x89). The camera is the scene
    // manager's (slot 18) animated face (0xae1ed0) when mCinematicHeld and mRefB4's animated entry answer as in slot 27;
    // mCinematicHeld is cleared and without a camera slot 29 (1) and error 0x99. Each actor with an object: its slot 78
    // action (mCinematic slot 14), and with an animated face its channel owner's (0xae42c0) channel set (kind 0x26b2,
    // the model's 0x5a69d3d5 channels 0x29..0x2c) unless of kind 0x26b1, its placement matched to mCinematicRef128's
    // (slot 13; slots 92, 42, 43, 171, 170, 54, 63 and the face's 35) and each animation of its slot 15 list (split at
    // 0xdaa6c0, trimmed, found with slot 26) blended (0xae4ef0), attached (0xae1f50) and recorded (0x26cbe0) one after
    // another; then its cleanups (0xaeb0e0 by the entry's flag bits). Extras: slot 97 (1) and their cleanups. Then
    // mCinematicRestore = mRefB0's slot 7, its slot 1 (1), the stopwatch 0x10cc848 restarted, the sound probe told,
    // mField2C4 = 0, GetCachedSingletonB's values saved (slots 26, 28, 23) and set (27, 29, 25; not with render option
    // 8), the hiding property saved and set, the wall manager given each actor's spot. Each part of mCinematicRefs is
    // attached to the camera from `start` on and recorded; the last part's track gets a new controller in mRef124
    // (framework 0x9d0ce69 / 0x9d0ce60). The camera's slot 1, and true.
    uint8_t PlayCinematic(int32_t start, int32_t *error);

    // 0x248810 slot 75 (src/sg/cTSSGSystemCapture.cpp): copies the "@@PostEffectsTexture@@" render target (when the
    // texture service, 0xae8750, has it and mCaptureSuspended is clear) into mRef2C8 and hands that to the image service
    // (0x18d120, slot 3 with `rect`, `a`, `b`). mRef2C8 (full size: the texture's level-0 width, height, format, slot 5
    // value) and mRef2CC (a 128x128 tile) are rebuilt from the framework (classes 0x3b75b71b / 0x4dee64d7) when missing
    // or mismatched. Under 0x24000 pixels the copy goes tile by tile through mRef2CC; otherwise, or when a tile fails,
    // directly (mRef2C8's slot 16). The image service's out value is never released (original).
    uint8_t CapturePostEffects(const int32_t *rect, uint32_t a, uint32_t b);

    // ---- lot unload (lane IR; src/sg/cTSSGSystemLot.cpp) ----

    // 0x24c610 slot 92: byte +0x16b cleared, GetCachedSlot23 touched, then the shared part (below; the lighting
    // manager's slot 9), mRef84, mNHoodOccupants, mRef94, mRef90 shut down (slot 4) and dropped, the resource
    // manager's slot 45 and the scene cache's (0x20de7c) slot 25 (0, 1).
    void ClearLot();

    // 0x2521c0 slot 93: as ClearLot without GetCachedSlot23, the lighting manager's slot 7, also mRef110; mRef90 and
    // mNHoodOccupants dropped before the car manager part; then the lot managers' clear slots (lot skirt 6, fence 5,
    // floor 12, wall 11, terrain 6, roof 5, pool 6, rug 5, curved pool 5, snow 7, cutout 5, awning 5), the resource
    // manager and scene cache as ClearLot, and last the light-map manager's slot 6.
    void ClearLotForLoad();

    // ---- after loading (lane JD; src/sg/cTSSGSystemLoad.cpp) ----

    // 0x251c00 slot 20 (ret 4; DoMessage 0xb3 with the lot id): the level range from the placement service (0x20e0b2, its
    // slots 85 / 86) to 0x254eb0, the lot managers refreshed, 0x252490 the first time; with a lot id, mRef90 (made when
    // missing) gets the lot when the lot impostor setting is on (default true) and the occupant manager starts when the
    // lot occupants setting is; then the lighting set for the world (the CAS or lot effects through the car manager into
    // mRef110), the lot-loaded cheat hook and, outside CAS, the camera service's view and two app values to 0x254bd0.
    // Sets mLotRefreshPending.
    void PostLoadLot(uint32_t lotId);

    // 0x24c030 slot 91 (ret 8; DoMessage 0xb8: the neighbourhood's name string and a flag): the scene manager's slot 5,
    // overlays through 0x248cd0(1), the car manager set up from the camera (the app's slot 6), mRef84, the lighting set
    // (day or night), the day/night and the neighbourhood's own effects (its name with a suffix) into mRef114 / mRef118,
    // the occupant manager, mRef90 (given the name and flag), mRef94 and its road path by neighbourhood kind, the
    // neighbourhood-loaded cheat hook. Sets mFlag16B and mNhoodRefreshPending.
    void NhoodEntered(void *name, uint32_t flag);

    // ---- the frame (lane JD; src/sg/cTSSGSystemFrame.cpp) ----

    // 0x24fb30 slot 17 (no arguments), with mFlag16A and mFlag16B: the frame time (mFrameWatch, at least mUnknown38;
    // none for the simulation while the live household's slot 26 is true), 0x24ab50, the settings' shader-reparse switch (cleared, and
    // the material object's slot 24), the scene's and the detail object's per-frame calls, mAnimClock advanced (wraps),
    // the clock's tick (slot 52's object: a new tick runs slot 51's slot 2 and slot 29) and simulation tick (with render
    // option 5), the statistics buckets, mRefB4's properties (the object fade; on a lot, day or night to the car manager
    // and the app), slot 99 for the cinematic camera's fade, then every manager's per-frame slot, mRef358's pulse, the
    // car manager's frame, the pool's simulation time, the neighbourhood refresh (0x24b870) and the material reload
    // countdown.
    void Update();

    // 0x248100 slot 99 (no arguments; slot 17 runs it while mObjectFade.cineCam is set): the camera (the scene manager's
    // slot 18; its slot 140 transform) gives mCameraPosition and mCameraDirection (0xff78f4 turned by its rotation); on a
    // lot, day or night to the car manager and the app as in slot 17; mActorBounds = the union of the live cinematic
    // actors' bounds (0xae7d00, trimmed top and bottom; small ones only of kind 2), zero when empty; then the object
    // fade's camera distance (to the bounds' centre along the direction, at least 0) and ratio (the bounds' radius
    // against the view's half-angle tangent, at most 1).
    void UpdateCinematicFade();

    // ---- the event behaviour type (lane JD; src/sg/cTSSGSystemBehavior.cpp). Slots 12-14 of the primary base
    // nGZEvent::cAbstractBehaviorTypeT<cITSSGSystem, cRefMultiThreaded>, shared with that base's vtable 0xdae0fc;
    // they read the attribute registry 0x253de0 (a function-local static map from id to a 24-byte entry). ----

    // 0x24be80 slot 12: the registry's size.
    uint32_t AttributeCount();
    // 0x247c10 slot 13 (ret 4; returns `out`): a reference-counted vector of every registered id (in order) and two
    // cAttributeIDIterator over it; `out` gets the first iterator's copy (its slot 3), the first and the last, each with
    // a reference.
    void *AttributeIds(void *out);
    // 0x247e80 slot 14 (ret 8; returns `out`): the 24-byte entry of `id`, or the default entry 0xff7848.
    void *AttributeInfo(void *out, uint32_t id);

    // ---- a data file (lane JD; src/sg/cTSSGSystemFiles.cpp), read by an unreplaced reader class (0x3a6a30) ----

    // 0x24ad90 slot 84 (ret 4): opens `path`; when it opens, slot 41's object (if any) gets its slot 8, and the reader
    // fills two record lists (0x3a90b0 / 0x3a7220, 0x3a87c0 / 0x3a71e0) and finishes (0x3a87f0); the lists are dropped.
    void LoadDataFile(const char *path);
    // 0x24b2d0 slot 85 (ret 4): 2 when `path` does not open, -1 when its header is current (0x3a8660), else 1.
    int32_t CheckDataFile(const char *path);

    // ---- start and stop (lane IR; src/sg/cTSSGSystemLifecycle.cpp) ----

    // 0x24f4e0 (__thiscall, EH; DoMessage 0xd0): CreateManagers undone. Returns at once unless mManagersCreated (which it
    // clears). The property notifier forgets the message face (its slot 6), the +0x3b0 object unregisters (0xb3a740) and
    // goes, the held objects are released and the entry handles destroyed, the +0xac animation leaves the animation system
    // (slot 31 its index, slot 11(index, 1)), then every reference is dropped, most after their slot 4 (shutdown; the
    // video-broadcast manager's is slot 5), with the scene-graph registrations (0x298070) and the app's slot 22 object
    // (slot 19(0)) between the neighbourhood objects and the managers. Clears the byte +0x16a and the dword +0x3ac.
    __declspec(noinline) void ReleaseManagers();

    // 0x255010 Shutdown (cIGZSystemService slot 9 on the face at +0xc; cTSSGSystemServiceSub::Shutdown forwards here):
    // the expression functions and commands Init's set-ups registered are unregistered (UnregisterExpressionFunction,
    // UnregisterCommands, 0x3a65a0), the references at +0x2c8 / +0x2cc and the scene, animation and persist references are
    // dropped, the message server forgets the message face for every id (KeyMapWalker over mMessageIds), and the holder
    // (service 0x85522803) forgets the system for 0xca7ccbbc and 0x2b5745f8 (its slot 9). Always true.
    bool Shutdown();

    // 0x255150 (static): the expression-function registry (0xae8980; its virtual base through the table at +4, entry 2)
    // unregisters the name at 0xdaedf4 (its slot 5). 0x255170 (static): the same for the eight command names Init's set-up
    // 0x253f80 registered.
    __declspec(noinline) static void UnregisterExpressionFunction();
    __declspec(noinline) static void UnregisterCommands();

    // 0x24afe0 Init (cIGZSystemService slot 8 on the face at +0xc; cTSSGSystemServiceSub::Init forwards here): the message
    // server (KeyMapSlot5Walker over mMessageIds) registers the message face for every id, the scene, animation system and
    // persist service references are taken, the message-server holder (service 0x85522803 as 0xa55227bb) gets the system for
    // 0xca7ccbbc and 0x2b5745f8 (its slot 8), and the three scene-graph registrations run (0x3a5de0, 0x253f80, 0x253ec0).
    // Always true.
    bool Init();

    // ---- layout (constructor 0x246110) ----
    const void *mVTable;          // 0x000 0xdae264
    int32_t mRefCount;            // 0x004 (cRefMultiThreaded)
    const void *mMessageVTable;   // 0x008 0xdae3f8 (cIGZMessageTarget2)
    const void *mServiceVTable;   // 0x00c 0xdae40c (cIGZSystemService)
    uint8_t mManagersCreated;     // 0x010 set by CreateManagers
    uint8_t mServiceRunning;      // 0x011 the service face's byte +5 (its slots 6 / 7)
    uint8_t mPad12[2];            // 0x012 (the constructor's word store zeroes 0x10..0x11 only)
    std::set<uint32_t> mMessageIds; // 0x014 the messages the system listens to (the constructor inserts 24)
    uint32_t mUnknown1C;          // 0x01c not written by the constructor
    cRZStopwatch mFrameWatch;     // 0x020 (mode 4) slot 17 reads and restarts it every frame
    uint32_t mUnknown38;          // 0x038 slot 18 sets it; slot 17 takes it as the least frame time when non-zero
    float mAnimClock;             // 0x03c seconds, advanced by slot 17 (wraps at 3600); slot 87 hands it on
    cRZStopwatch mStartupWatch;   // 0x040 (mode 4) started by CreateManagers
    SGRef mScene;                 // 0x058 from 0x20df26 (Init); its slot 12 object loads files (CreateManagers)
    SGRef mAnimSystem;            // 0x05c from 0x20dd94 (Init); DoMessage's animation options go to it
    SGRef mPersistService;        // 0x060 from the cached getter 0xb390c0 (Init)
    SGRef mRef64;                 // 0x064
    SGRef mSceneManager;          // 0x068 class 0x69512c6d (nTSSG::cSceneManager)
    SGRef mWallManager;           // 0x06c class 0xe989cf03 (nTSSG::cWallManager)
    SGRef mFenceManager;          // 0x070 class 0xeab6b59e (nTSSG::cFenceManager)
    SGRef mPoolManager;           // 0x074 class 0xac7486ae (nTSSG::cPoolManager)
    SGRef mLotSkirt;              // 0x078 class 0xec439394 (nTSSG::cLotSkirt)
    SGRef mFloorManager;          // 0x07c class 0xe9945f8d (nTSSG::cFloorManager)
    SGRef mTerrain;               // 0x080 class 0x6b3a33c7 (nTSSG::cTerrain)
    SGRef mRef84;                 // 0x084
    SGRef mNHoodOccupants;        // 0x088 class 0x6bd49281 (nTSSG::cNHoodOccupantManager; DoMessage)
    SGRef mVideoBroadcastManager; // 0x08c class 0x6c4f4a1a (nTSSG::cVideoBroadcastManager)
    SGRef mRef90;                 // 0x090
    SGRef mRef94;                 // 0x094
    SGRef mQueryStrategyManager;  // 0x098 class 0x4988a36a (nTSSG::cQueryStrategyManager; not initialised here)
    SGRef mLightRigManager;       // 0x09c class 0x696d3daf (nTSSG::cLightRigManager)
    SGRef mLightMapManager;       // 0x0a0 class 0x896d0177 (nTSSG::cLightMapManager)
    SGRef mLightingManager;       // 0x0a4 class 0x0b390bf7 (nTSSG::cLightingManager)
    SGRef mShadowManager;         // 0x0a8 class 0x0bf9c04f (nTSSG::cShadowManager)
    SGRef mRefAC;                 // 0x0ac
    SGRef mRefB0;                 // 0x0b0
    SGRef mRefB4;                 // 0x0b4 from the cached getter 0xb38ef0; its slot 17 takes the files of list 0x8c9f7a34
    SGRef mRoofManager;           // 0x0b8 class 0x2b940c24 (nTSSG::cRoofManager)
    SGRef mLotImposterManager;    // 0x0bc class 0x0bc4a38d (nTSSG::cLotImposterManager)
    SGRef mRefC0;                 // 0x0c0
    SGRef mOverlays[9];           // 0x0c4 class 0xac22fa8d (cTSOverlayManager), CreateOverlay
    SGRef mRugManager;            // 0x0e8 class 0xef5e4e99 (nTSSG::cRugManager)
    SGRef mCurvedPoolManager;     // 0x0ec class 0xd16de942 (nTSSG::cCurvedPoolManager)
    SGRef mAwningManager;         // 0x0f0 class 0x504d1938 (nTSSG::cAwningManager)
    SGRef mSeasonManager;         // 0x0f4 class 0xf24e3352 (nTSSG::cSeasonManager)
    SGRef mTerrainCutoutManager;  // 0x0f8 class 0x31884107 (nTSSG::cTerrainCutoutObjectManager)
    SGRef mTerrainSnowManager;    // 0x0fc class 0x5224dfdf (nTSSG::cTerrainSnowManager)
    SGRef m3DUIManager;           // 0x100 class 0x2c9a6255 (nTSSG::c3DUIManager)
    SGRef mRef104;                // 0x104
    SGRef mCommandParser;         // 0x108 class 0x5cf9459a (nGZCommandParser::cFileParser)
    // 0x10c: the 500-byte object CreateManagers builds (constructor 0x3a1ae0(1)). The pointer is to its start; its
    // reference counting is on the virtual base the virtual-base table at +8 places (entry 1).
    void *mParserTarget;          // 0x10c
    SGRef mRef110;                // 0x110 slot 93 shuts it down (slot 4 with 1) and drops it
    SGRef mRef114;                // 0x114 slots 92 / 93 shut it down (slot 4 with 1) and drop it
    SGRef mRef118;                // 0x118 slots 92 / 93 shut it down (slot 4 with 1) and drop it
    SGRef mRef11C;                // 0x11c released by the destructor
    // The cinematic (slot 29 ends it). References here are held through their virtual base (table at +4, entry 1).
    void *mCinematic;             // 0x120 the cinematic itself; slot 29 does nothing without it
    SGRef mRef124;                // 0x124 its controller: slot 54 gives it; slot 56 asks its slot 33 object (the camera)
    void *mCinematicRef128;       // 0x128 dropped when the cinematic is cleared
    std::vector<cTSSGCinematicActor> mCinematicActors; // 0x12c
    std::vector<cTSSGCinematicActor> mCinematicExtras; // 0x138
    std::vector<SGVirtualRef> mCinematicRefs;          // 0x144
    uint32_t mCinematicSaved[3];  // 0x150 values handed back to GetCachedSingletonB's slots 27, 29 and 25 (0x154 = 4)
    uint32_t mObjectHidingValue;  // 0x15c the "ObjectHiding" value restored when mObjectHiding is set
    uint8_t mObjectHiding;        // 0x160
    uint8_t mField161;            // 0x161
    uint8_t mCinematicHeld;       // 0x162 while set (and while the controller is there) slot 29 keeps the cinematic
    uint8_t mField163;            // 0x163
    uint32_t mCinematicRestore;   // 0x164 mRefB0's slot 1 value when the cinematic ends normally (100)
    uint8_t mLotFirstLoadDone;    // 0x168 slot 20 runs 0x252490 once, the first time a lot loads
    uint8_t mLotRefreshPending;   // 0x169 slot 20 sets it; slot 17 then refreshes mRefC0 (its slot 5) and clears it
    uint8_t mFlag16A;             // 0x16a slot 87 needs it and mFlag16B; cleared by the lifecycle code
    uint8_t mFlag16B;             // 0x16b messages 0x9a / 0xb4 set / clear it; ClearLot clears it
    uint8_t mCaptureSuspended;    // 0x16c OnEvent 0xca7ccbbc (kind 4 sets, kind 1 clears); slot 75 captures only when clear
    uint8_t mFields16D[3];        // 0x16d
    std::vector<void *> mHeldObjects;  // 0x170 references ReleaseManagers releases (slot 2 on each non-null one)
    std::vector<cTSSGEntryHandle> mEntryHandles; // 0x17c handles ReleaseManagers destroys (0xae82b0 on each)
    uint8_t mRenderOptions[0x194 - 0x188]; // 0x188 render options, one byte each, set by DoMessage (the bytes at +0x18a..0x18c, +0x18e are read by slots 68..71)
    int32_t mCounter194;          // 0x194 slot 16 advances it unless it is 0x80000100
    int32_t mCounter198;          // 0x198 slot 16 advances it when non-zero; slot 18 sets it
    int32_t mStatBucketStart[10]; // 0x19c the tick each statistics bucket started (kNoTick: none yet; slot 17)
    // Resource statistics, ten buckets each, indexed by mStatBucket (slots 25 and 98 add to them).
    uint32_t mPersistHitsFlagged[10]; // 0x1c4 slot 98: found by the persist service with its flag set
    uint32_t mCacheHits[10];          // 0x1ec slot 25: found by the animation system, its slot 59 false
    uint32_t mCacheHitsSlot59[10];    // 0x214 slot 25: found by the animation system, its slot 59 true
    uint32_t mPersistHits[10];        // 0x23c slot 98: found by the persist service, flag clear
    uint32_t mPersistTimeFlagged[10]; // 0x264 slot 98: stopwatch time of the flagged finds
    uint32_t mPersistTime[10];        // 0x28c slot 98: stopwatch time of the others
    uint32_t mStatBucket;             // 0x2b4
    float mLotTunables[3];       // 0x2b8 ReadLotTunables (defaults 1, 3, 21)
    uint32_t mField2C4;           // 0x2c4 slots 57 / 30
    SGRef mRef2C8;                // 0x2c8 dropped by Shutdown
    SGRef mRef2CC;                // 0x2cc dropped by Shutdown
    float mColors[8][4];          // 0x2d0 slot 78 (by index)
    float mPulseDepth;            // 0x350 slot 17: mRef358's value pulses between 1 - depth and 1 ...
    float mPulseRate;             // 0x354 ... at this many cycles a second of mStartupWatch
    SGRef mRef358;                // 0x358 (dropped by ReleaseManagers) slot 17 sets its slot 42 value (read by slot 52)
    uint8_t mObjectFadeSet;       // 0x35c slot 17: the fade distance property was there; slot 79 needs it
    uint8_t mPad35D[3];           // 0x35d
    // 0x360: the object fade (slot 79 copies these 20 bytes out). Distance and falloff are mRefB4's properties, the
    // flags from two more of them (all objects, cinematic camera; slot 17); the camera distance and ratio are slot 99's.
    struct ObjectFade {
        float distance;           // +0x0
        float falloff;            // +0x4
        uint8_t allObjects;       // +0x8
        uint8_t cineCam;          // +0x9 while set slot 17 runs slot 99 (the cinematic camera's fade)
        uint8_t pad[2];
        float cameraDistance;     // +0xc slot 99: from the camera to the centre of the actors' bounds (0 or more)
        float cameraRatio;        // +0x10 slot 99: how much of the view the bounds fill (at most 1)
    } mObjectFade;                // 0x360
    float mCameraPosition[3];     // 0x374 slot 99 (slot 80 gives its address)
    float mCameraDirection[3];    // 0x380 slot 99: the camera's forward vector (slot 81)
    float mActorBounds[6];        // 0x38c slot 99: min x, y, z, max x, y, z of the cinematic actors (slot 82)
    uint32_t mField3A4;           // 0x3a4 slot 86
    uint8_t mNhoodRefreshPending; // 0x3a8 slots 31 and 91 set it; slot 17 then runs 0x24b870 and clears it
    uint8_t mPad3A9[3];           // 0x3a9
    int32_t mMaterialReloadDelay; // 0x3ac frames until slot 17 reloads the materials (counts down while positive)
    SGRef mRef3B0;                // 0x3b0 the 12-byte object ReadShaderOptions builds (0xb39920)
    SGRef mAssetCollector;        // 0x3b4 class 0xeda217dc (cTSObjectAssetCollector), with the asset-server switch
};
static_assert(sizeof(cTSSGSystem) == 0x3b8);
static_assert(offsetof(cTSSGSystem, mMessageIds) == 0x14);
static_assert(offsetof(cTSSGSystem, mStartupWatch) == 0x40);
static_assert(offsetof(cTSSGSystem, mScene) == 0x58);
static_assert(offsetof(cTSSGSystem, mOverlays) == 0xc4);
static_assert(offsetof(cTSSGSystem, mCommandParser) == 0x108);
static_assert(offsetof(cTSSGSystem, mHeldObjects) == 0x170);
static_assert(offsetof(cTSSGSystem, mEntryHandles) == 0x17c);
static_assert(offsetof(cTSSGSystem, mLotTunables) == 0x2b8);
static_assert(offsetof(cTSSGSystem, mAssetCollector) == 0x3b4);
static_assert(offsetof(cTSSGSystem, mRef2C8) == 0x2c8);
static_assert(offsetof(cTSSGSystem, mColors) == 0x2d0);
static_assert(offsetof(cTSSGSystem, mRef358) == 0x358);
static_assert(offsetof(cTSSGSystem, mRef124) == 0x124);
static_assert(offsetof(cTSSGSystem, mCinematicActors) == 0x12c);
static_assert(offsetof(cTSSGSystem, mCinematicRefs) == 0x144);
static_assert(offsetof(cTSSGSystem, mObjectHiding) == 0x160);
static_assert(offsetof(cTSSGSystem, mCaptureSuspended) == 0x16c);
static_assert(offsetof(cTSSGSystem, mCinematicRestore) == 0x164);
static_assert(offsetof(cTSSGSystem, mCounter194) == 0x194);
static_assert(offsetof(cTSSGSystem, mObjectFade) == 0x360 && sizeof(cTSSGSystem::ObjectFade) == 20);
static_assert(offsetof(cTSSGSystem, mActorBounds) == 0x38c && offsetof(cTSSGSystem, mMaterialReloadDelay) == 0x3ac);
static_assert(offsetof(cTSSGSystem, mStatBucketStart) == 0x19c && offsetof(cTSSGSystem, mPulseDepth) == 0x350);
static_assert(offsetof(cTSSGSystem, mPersistHitsFlagged) == 0x1c4);
static_assert(offsetof(cTSSGSystem, mCacheHits) == 0x1ec);
static_assert(offsetof(cTSSGSystem, mPersistTime) == 0x28c);
static_assert(offsetof(cTSSGSystem, mStatBucket) == 0x2b4);

// The constructor's hook (0x246110, __thiscall) and the director's call: `::new (memory) cTSSGSystem`.
struct cTSSGSystemAdapters {
    static cTSSGSystem *__fastcall Construct(void *memory, void *edx);
};

// The cIGZMessageTarget2 face at +8 (`this` is the object + 8).
class cTSSGSystemMessageFace {
public:
    // 0x249130 (ret 4): cTSSGSystem::DoMessage on the object.
    bool DoMessage(void *message);

    // 0x253daa / 0x254475: QueryInterface / Release on the object (this - 8).
    bool QueryInterface(uint32_t iid, void **out);
    uint32_t Release();
};

// ---- the leaf slots (lane before IR, hook-shaped views of the same object) ----

// The primary face (`this` is the object).
class cTSSGSystemPrimary {
public:
    // 0x24ad20 (`ret`, 7 bytes): the dword at +0x2c4. Slot 57.
    uint32_t GetFieldDword2C4();

    // 0x254db0 (`ret 4`, 13 bytes): stores the argument at the dword +0x2c4. Slot 30.
    void SetFieldDword2C4(uint32_t value);

    // 0x255690 (`ret`, 8 bytes): stores 1 at the byte +0x3a8. Slot 31.
    void SetFlag3A8();

    // 0x254670 (`ret`, 7 bytes): the dword at +0x3a4. Slot 86.
    uint32_t GetFieldDword3A4();

    // 0x24b260 (`ret`, 7 bytes): the byte at +0x162, in al. Slot 55. Only al is defined.
    uint8_t GetFieldByte162();

    // 0x24b200 (`ret`, 7 bytes): the byte at +0x18e, in al. Slot 71. Only al is defined.
    uint8_t GetFieldByte18E();

    // 0x255000 (`ret`, 7 bytes): the byte at +0x18a, in al. Slot 68. Only al is defined.
    uint8_t GetFieldByte18A();

    // 0x254fe0 (`ret`, 7 bytes): the byte at +0x18b, in al. Slot 69. Only al is defined.
    uint8_t GetFieldByte18B();

    // 0x254ff0 (`ret`, 7 bytes): the byte at +0x18c, in al. Slot 70. Only al is defined.
    uint8_t GetFieldByte18C();

    // 0x247fb0 (`ret`, 7 bytes): the address object + 0x374 (lea). Slot 80.
    void *AddressOf374();

    // 0x247fa0 (`ret`, 7 bytes): the address object + 0x380 (lea). Slot 81.
    void *AddressOf380();

    // 0x247fc0 (`ret`, 7 bytes): the address object + 0x38c (lea). Slot 82.
    void *AddressOf38C();

    // 0x250510 (`ret 4`, 24 bytes): dword `index` of the array at +0xc4 (nine dwords). The original compares the index
    // as signed and returns 0 when index >= 9; below 9 (including negatives) it reads [this + index * 4 + 0xc4], with
    // the address computed modulo 2^32. Slot 45.
    uint32_t GetDwordArrayC4(int32_t index);
};

class cTSSGSystemServiceSub {
public:
    // 0x24a950 (`mov eax, imm32; ret`, 6 bytes): the service id constant. Slot 3 of cIGZSystemService (this = object + 0xc).
    uint32_t GetServiceID();

    // 0x24a960 (`mov eax, imm32; ret`, 6 bytes): the service priority constant. Slot 5 of cIGZSystemService.
    uint32_t GetServicePriority();

    // 0x24afe0: cTSSGSystem::Init on the object (this - 0xc).
    bool Init();

    // 0x255010: cTSSGSystem::Shutdown on the object (this - 0xc).
    bool Shutdown();

    // 0x253db2 / 0x25447d: QueryInterface / Release on the object (this - 0xc).
    bool QueryInterface(uint32_t iid, void **out);
    uint32_t Release();
};

} // namespace nTSSG
