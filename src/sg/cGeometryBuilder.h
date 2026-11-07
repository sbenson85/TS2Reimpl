#pragma once
// nGZSceneGraph::cGeometryBuilder (lane QE, scenegraph D2 part 1): the base of the geometry builders the GMDC loader runs
// over a fresh container (cCompactorBuilder, cBoundingVolumeBuilder, cTangentSpaceBuilder, cIndexedMeshBuilder). Each is
// a COM class made by the director; its cIGeometryBuilder face's slot 16 (Build) rewrites or completes the container.
//
// The behaviour level is the event template over a virtual base, as cExtension's (src/sg/cExtension.h):
//
//   class cGeometryBuilderV : public virtual cIGeometryBuilder {};
//   class cGeometryBuilder : public cAbstractBehaviorTypeT<cGeometryBuilderV, cRefSingleThreaded>,
//                            public cIGZPersistSerializable2
//
//   +0x00 the behaviour template's own vtable (one slot, the deleting destructor)
//   +0x04 the vbptr
//   +0x08 the count (low 31 bits) and the active bit (the template's)
//   +0x0c cIGZPersistSerializable2 (6 slots)
//   +0x10 mListener   one more listener told of every notification (uncounted; slot 15 sets it)
//   then the derived class's fields, then the vtordisp and the virtual base cIGeometryBuilder, then the derived class's
//   own interface (virtual, with its own vbptr to cIGeometryBuilder).
//
// The game's bodies (their `this` the static positions their thunks deliver): QueryInterface 0xa45310, Release 0xa45530
// (-> the template's 0xa454c0), Write 0xa45790 (persist face, object + 0xc), OnEvent 0xa45240, AskListeners 0xa450f0,
// Connect / Disconnect 0xa44fd0 / 0xa450a0 (the template's bodies, object + 0x10), NotifyListeners 0xa45660 (-> the
// template's 0xa45540) and SetListener 0xa456c0 (object + 0x18), and the registry slots 0xa452c0 / 0xa44c60 / 0xa44ee0
// (`this` unused; the registry 0xa453a0). All nine derived classes reach them: the four ours and the five the game
// still builds (cAlphaSortBuilder, cOptimizedGeometryBuilder, cProcessDeformationsBuilder, nTSSG's
// cNHoodTerrainGeometryBuilder and cWaterGeometryBuilder). cGeometryBuilderAdapters (below) is what the hook table
// (src/hook/tables/cGeometryBuilder.cpp) points them at. Left original (folded across unrelated classes): Read 0x9dc310
// and AddRef 0x2582b0, covered only through our vtables.
#include "game/owned.h"
#include "gz/cAbstractBehaviorTypeST.h"
#include "gz/cGZEventReflector.h"
#include "sg/SGNodeInterfaces.h" // cPersistTypeInfo

#include <cstdint>

namespace ifaces {
class cIGZPersistReader;
class cIGZPersistWriter;
} // namespace ifaces

// cIGZPersistSerializable2 (the builders' +0xc face): cIGZUnknown, then Read, GetTypeInfo, Write (no owner slot).
class cIGZPersistSerializable2 : public cIGZUnknown {
public:
    virtual void Read(ifaces::cIGZPersistReader *reader) = 0;   // 3
    virtual cPersistTypeInfo GetTypeInfo() = 0;                  // 4 (hidden pointer)
    virtual void Write(ifaces::cIGZPersistWriter *writer) = 0;  // 5
};

namespace nGZSceneGraph {

class cIGeometryDataContainer; // sg/cGeometryDataContainerClass.h

// nGZSceneGraph::cIGeometryBuilder (interface id 0xbbb79c90): the behaviour's slots, then three of its own.
class cIGeometryBuilder : public nGZEvent::cIAbstractBehavior {
public:
    static constexpr uint32_t kIid = 0xbbb79c90;
    // 15 (0xa456c0): stored uncounted; eax is the argument.
    virtual nGZEvent::cIAbstractBehaviorType *SetListener(nGZEvent::cIAbstractBehaviorType *listener) = 0;
    virtual void Build(cIGeometryDataContainer *container) = 0; // 16
    virtual const char *GetName() = 0;                          // 17: a static text
};

class cGeometryBuilderV : public virtual cIGeometryBuilder {};

// The builders' instantiation of the behaviour template: its event-manager cell (0xa45010's cache), the record its
// GetEventAttribute 0xa44ee0 copies for an unknown event, and its registry 0xa453a0 (a function-local static map seeded
// from the plain behaviour type's).
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f60f4); // the cell below (game/owned.h)
struct cGeometryBuilderBehaviorTraits {
    static constexpr uint32_t kEventManagerCell = 0x10f60f4;
    static nGZEvent::cIConnectionManager *&EventManagerCell() { return ts2_rva_10f60f4; }
    static nGZEvent::cEventAttributeMap *Registry();
    // 0xa453a0 (cdecl): the registry itself. A function-local static map built on first use (its holder 0xa44a20: the base
    // events, then 0xbbb112f6 (record 0x106ab4c) through this function; the map a copy of the base registry 0x1485f0); a
    // non-zero id stores `attribute`. Not inlined: tests stand in for it.
    __declspec(noinline) static nGZEvent::cEventAttributeMap *__cdecl RegistryFor(uint32_t id,
                                                                                const nGZEvent::cEventAttribute *attribute);
    static void ResetRegistryForTest(); // tests only
};
using cGeometryBuilderBehavior = nGZEvent::cAbstractBehaviorTypeST<cGeometryBuilderV, cGeometryBuilderBehaviorTraits>;

class cGeometryBuilder : public cGeometryBuilderBehavior, public cIGZPersistSerializable2 {
public:
    static constexpr uint32_t kVersion = 3;                     // the type-info record's version
    static constexpr uint32_t kIidSerializable = 0x5b0aaa74;    // the +0xc face, counted
    static constexpr uint32_t kIidBehavior = 0x3b253dc7;        // the virtual base, counted
    static constexpr uint32_t kIidBehaviorUncounted = 0xfb859324; // the virtual base, not counted

    cGeometryBuilder() = default;
    cGeometryBuilder(const cGeometryBuilder &) = delete;
    cGeometryBuilder &operator=(const cGeometryBuilder &) = delete;
    ~cGeometryBuilder() override = default;

    // ---- cIGZUnknown (both faces) ----
    bool QueryInterface(uint32_t iid, void **out) override; // 0xa45310
    uint32_t AddRef() override;                             // 0x2582b0 (folded adjustor)
    uint32_t Release() override;                            // 0xa45530

    // ---- cIGZPersistSerializable2 (+0xc) ----
    void Read(ifaces::cIGZPersistReader *reader) override;  // 0x9dc310 (folded): the type-info record only
    cPersistTypeInfo GetTypeInfo() override;                 // {"cGeometryBuilder", 0, 3}
    void Write(ifaces::cIGZPersistWriter *writer) override; // 0xa45790

    // ---- the behaviour ----
    void NotifyListeners(uint32_t type, uint32_t arg1, void *data) override; // 0xa45660
    nGZEvent::cIAbstractBehaviorType *SetListener(nGZEvent::cIAbstractBehaviorType *listener) override; // 0xa456c0

protected:
    // 0xa3bde0 (the template's QueryInterface, folded with cExtension's family): 1 and the behaviour type give the virtual
    // base counted, 0xfb859324 gives it uncounted.
    bool BehaviorQueryInterface(uint32_t iid, void **out);
    // 0xa45790's body: the base's own record (its GetTypeInfo directly), its name released.
    void WriteBuilder(ifaces::cIGZPersistWriter *writer);

public:
    nGZEvent::cIAbstractBehaviorType *mListener = nullptr; // +0x10 uncounted
};

// The hook adapters (lane QM): the game's bodies at their RVAs, as __fastcall functions taking the `this` their thunks
// deliver (the object plus the face's static offset). Each calls the class's own body qualified (no virtual dispatch to
// a derived override), as the game's bodies do.
struct cGeometryBuilderAdapters {
    using Self = cGeometryBuilder;
    static constexpr int kPersistOffset = 0x0c;  // QueryInterface, Release, Write
    static constexpr int kBehaviorOffset = 0x10; // OnEvent, AskListeners, Connect, Disconnect, the registry slots
    static constexpr int kListenerOffset = 0x18; // NotifyListeners, SetListener (the cIGeometryBuilder face of the bare base)
    static Self *FromFace(void *face, int offset) { return reinterpret_cast<Self *>(static_cast<uint8_t *>(face) - offset); }

    static bool __fastcall QueryInterface(void *face, void *, uint32_t iid, void **out); // 0xa45310
    static uint32_t __fastcall Release(void *face, void *);                              // 0xa45530
    static void __fastcall Write(void *face, void *, ifaces::cIGZPersistWriter *writer); // 0xa45790
    static void __fastcall OnEvent(void *face, void *, uint32_t type, uint32_t arg1, void *data, uint32_t arg3); // 0xa45240
    static bool __fastcall AskListeners(void *face, void *, uint32_t type, uint32_t arg1, void *data);          // 0xa450f0
    static void __fastcall Connect(void *face, void *, uint32_t event, cIGZUnknown *target, uint32_t targetEvent);    // 0xa44fd0
    static void __fastcall Disconnect(void *face, void *, uint32_t event, cIGZUnknown *target, uint32_t targetEvent); // 0xa450a0
    static uint32_t __fastcall GetEventCount(void *face, void *);                                                // 0xa452c0
    static nGZEvent::cAttributeIDRange *__fastcall GetEventIds(void *face, void *, nGZEvent::cAttributeIDRange *out); // 0xa44c60
    static nGZEvent::cEventAttribute *__fastcall GetEventAttribute(void *face, void *, nGZEvent::cEventAttribute *out,
                                                                   uint32_t event);                            // 0xa44ee0
    static void __fastcall NotifyListeners(void *face, void *, uint32_t type, uint32_t arg1, void *data);       // 0xa45660
    static nGZEvent::cIAbstractBehaviorType *__fastcall SetListener(void *face, void *,
                                                                    nGZEvent::cIAbstractBehaviorType *listener); // 0xa456c0
};

} // namespace nGZSceneGraph
