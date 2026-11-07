#pragma once
// nGZSceneGraph::cExtension as a whole class (lane OY, scenegraph D1a part 2): the base of every extension a scene-graph
// node keeps in its extension list (cTagExtension, cSkinExtension, cBoneDataExtension, cDataListExtension, the anim
// channel outlets, cAnimManager, cChoreoQueue, cTSEffectsExtension and the nTSSG extensions: about 17 classes). The
// behaviour level is the event template over a virtual base (docs/standalone-shape.md, "Virtual bases and vtordisp"):
//
//   class cExtensionV : public virtual cIExtension {};                       (only a vbptr)
//   class cExtension : public cAbstractBehaviorTypeT<cExtensionV, cRefSingleThreaded>,
//                      public cIGZPersistSerializableReferent
//
//   +0x00 the behaviour template's own vtable (0xe694f4: one slot, the deleting destructor 0xa3b910, delete 0x20)
//   +0x04 the vbptr (cExtensionV; vbtable {-4, 0x18} in cExtension alone)
//   +0x08 the count (low 31 bits) and the active bit (the template's)
//   +0x0c cIGZPersistSerializableReferent (vtable 0xe694fc, 7 slots)
//   +0x10 mOwner      the node holding the extension (uncounted)
//   +0x14 mNext       the next extension of the node's list (counted)
//   +0x18 the vtordisp, +0x1c the virtual base cIExtension (vtable 0xe6951c, 27 slots, all vtordisp thunks) in cExtension
//         alone; a derived class moves both to its end (cTagExtension +0x20 / +0x24, cDataListExtension +0x1c / +0x20).
//
// Bodies and their `this` (the static positions every vtordisp thunk delivers, whatever the derived class):
//   object + 0x0c  the referent face: QueryInterface 0xa3be60, AddRef (folded adjustor 0x2582b0), Release 0xa3bf50,
//                  Read (folded 0x9dc310), GetTypeInfo 0xa3c160, Write 0xa3c210, GetOwnerInfo 0xa3bf60
//   object + 0x10  the virtual base where the behaviour template alone puts it: its QueryInterface 0xa3bde0, AddRef
//                  0x258280, Release 0xa3bee0, OnEvent 0xa3bd10, OnQuery 0xa3bd80, Notify 0xa3bfb0, Ask 0xa3bbc0,
//                  Connect / Disconnect 0xa3ba80 / 0xa3bb50, IsActive 0xa3bdb0, GetEventAttribute 0xa3b990; slots 12 / 13
//                  are another instantiation's folded bodies (0x1481c0 / 0x146a70, the same registry 0x1485f0)
//   object + 0x1c  cIExtension's own slots: 16 0xa3b950, 17-20 folded getters (0x9e8850 / 0x9e7e90), 21 0xa3c0d0,
//                  22 0xa3c0f0, 23 0xa3c130, 24 the folded `mov al, 1` 0x144910, 26 0xa3bb90; 15 and 25 pure.
//
// The game's derived classes' vtables hold their own thunks into these bodies, so the bodies only cExtension's family
// reaches are hooked at their RVAs (src/hook/tables/cExtension.cpp, cExtensionAdapters below); the folded ones are left
// (0xa3bde0 and 0xa3bd80 are shared with the geometry builders and cGenericController's behaviour). Constructors as
// cSGResource's (lane OE): the game's derived constructors call 0xa3b7d0 / 0xa3b750 with the most-derived flag 0, and
// the hooks are our compiler's own constructors (same ABI: thiscall, the flag on the stack after the arguments).
#include "game/owned.h"
#include "gz/cAbstractBehaviorTypeST.h"
#include "gz/cGZEventReflector.h"
#include "sg/SGNodeInterfaces.h"

#include <cstdint>

namespace ifaces {
class cIGZPersistReader;
class cIGZPersistWriter;
} // namespace ifaces

namespace nGZSceneGraph {

// cExtensionV: the behaviour template's interface argument, cIExtension as a virtual base (RTTI: at +4 of the template).
class cExtensionV : public virtual cIExtension {};

// The behaviour template's instantiation: its own event-manager cell and missing record; the registry is the plain
// behaviour type's (0x1485f0 with id 0: GetEventAttribute 0xa3b990 asks it, and slots 12 / 13 are that instantiation's).
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f6010); // the cell below (game/owned.h)
struct cExtensionBehaviorTraits {
    static constexpr uint32_t kEventManagerCell = 0x10f6010; // 0xa3bac0's cache
    static nGZEvent::cIConnectionManager *&EventManagerCell() { return ts2_rva_10f6010; }
    static nGZEvent::cEventAttributeMap *Registry() { return nGZEvent::cAbstractBehaviorType::Registry(); }
};
using cExtensionBehavior = nGZEvent::cAbstractBehaviorTypeST<cExtensionV, cExtensionBehaviorTraits>;

class cExtension : public cExtensionBehavior, public cIGZPersistSerializableReferent {
public:
    static constexpr uint32_t kVersion = 3;                    // the type-info record's version (0xa3c160)
    static constexpr uint32_t kIidReferent = 0x5b0aaa74;       // cIGZPersistSerializableReferent: the +0xc face, counted
    static constexpr uint32_t kIidSerializable2 = 0xbb11a53e;  // cIGZPersistSerializable2: the same
    static constexpr uint32_t kIidBehavior = 0x3b253dc7;       // the behaviour type: the virtual base, counted
    static constexpr uint32_t kIidBehaviorUncounted = 0xfb859324; // the virtual base, NOT counted (0xa3bde0)
    static constexpr uint32_t kEmptyText = 0xd9b9d0;           // the image's empty string (GetDescription's answer)
    static constexpr int kReferentOffset = 0x0c;  // the referent face's bodies' `this`
    static constexpr int kBehaviorOffset = 0x10;  // the behaviour template's bodies' `this`
    static constexpr int kExtensionOffset = 0x1c; // cIExtension's own bodies' `this`

    cExtension();                            // 0xa3b7d0 (only ever a base part: the class is abstract)
    cExtension(const cExtension &other);     // 0xa3b750: copies nothing (count, owner and next start empty)
    cExtension &operator=(const cExtension &) = delete;
    ~cExtension() override;                  // 0xa3b880; the template's slot 0 the deleting destructor 0xa3b910

    // ---- cIGZUnknown (both faces; the virtual base's are vtordisp thunks 0xa3be4b / 0xa3b930 / 0xa3bf44) ----
    bool QueryInterface(uint32_t iid, void **out) override; // 0xa3be60
    uint32_t AddRef() override;                             // 0x2582b0 (folded: add ecx, 4; jmp 0x258280)
    uint32_t Release() override;                            // 0xa3bf50 (add ecx, 4; jmp 0xa3bee0)

    // ---- cIGZPersistSerializableReferent (+0xc) ----
    void Read(ifaces::cIGZPersistReader *reader) override;   // 0x9dc310 (folded): the type-info record only
    cPersistTypeInfo GetTypeInfo() override;                  // 0xa3c160: {"cExtension", 0, 3}
    void Write(ifaces::cIGZPersistWriter *writer) override;  // 0xa3c210 (WriteExtension; see there)
    uint32_t GetOwnerInfo() override;                         // 0xa3bf60: the owner node's, or 0

    // ---- cIExtension (15 Clone and 25 stay pure) ----
    cIGZPersistSerializableReferent *GetPersistReferent() override; // 0xa3b950
    cIObjectGraphNode *GetOwnerConst() override;                    // 0x9e8850 (folded)
    cIObjectGraphNode *GetOwner() override;                         // 0x9e8850 (folded)
    cIExtension *GetNextConst() override;                           // 0x9e7e90 (folded)
    cIExtension *GetNext() override;                                // 0x9e7e90 (folded)
    cIObjectGraphNode *SetOwner(cIObjectGraphNode *owner) override; // 0xa3c0d0
    uint32_t SetNext(cIExtension *next) override;                   // 0xa3c0f0 (eax: the last count answer)
    bool IsKindOf(uint32_t iid) override;                           // 0xa3c130
    bool IsPersistent() override;                                   // 0x144910 (folded `mov al, 1`)
    void GetDescription(cIGZString *text) override;                 // 0xa3bb90: the empty string

    // 0xa3bde0's body (the behaviour template's QueryInterface, folded with other families' and left unhooked):
    // 1 and the behaviour type give the virtual base counted, 0xfb859324 gives it uncounted.
    bool BehaviorQueryInterface(uint32_t iid, void **out);

    // 0xa3c210's body: the type-info record (0xa3c160 directly, not slot 4) through the writer's slot 28. It returns what
    // the original leaves in eax: the record name's Release answer, or slot 28's when there is no name. Write (the slot)
    // returns nothing because the shared referent interface declares it so (sg/SGNodeInterfaces.h); the game's derived
    // Writes call 0xa3c210 directly and get this result through the adapter.
    uint32_t WriteExtension(ifaces::cIGZPersistWriter *writer);

    cIObjectGraphNode *mOwner = nullptr; // +0x10
    cIExtension *mNext = nullptr;        // +0x14 counted
};

// The RVA hooks' entry points (src/hook/tables/cExtension.cpp). Each takes the `this` the game's thunks and derived classes
// deliver (the static positions above, never through the vbptr: C++ has no static_cast from a virtual base) and calls
// cExtension's body qualified (inlined). None for the deleting destructor 0xa3b910, the behaviour template's constructor,
// destructor and deleting destructor (0xa3b710 / 0xa3b850 / 0xa3b8f0), its Release 0xa3bee0 and the event-manager getter
// 0xa3bac0: with the constructors, the destructor, 0xa3bf50 and the behaviour bodies hooked nothing runs them (in effect
// through their callers).
struct cExtensionAdapters {
    using Self = cExtension;
    static Self *FromReferent(void *face) { return reinterpret_cast<Self *>(static_cast<uint8_t *>(face) - Self::kReferentOffset); }
    static Self *FromBehavior(void *face) { return reinterpret_cast<Self *>(static_cast<uint8_t *>(face) - Self::kBehaviorOffset); }
    static Self *FromExtension(void *face) { return reinterpret_cast<Self *>(static_cast<uint8_t *>(face) - Self::kExtensionOffset); }

    static void __fastcall Destruct(Self *self, void *);                                              // 0xa3b880
    // The referent face (object + 0xc).
    static bool __fastcall QueryInterface(void *face, void *, uint32_t iid, void **out);              // 0xa3be60
    static uint32_t __fastcall Release(void *face, void *);                                           // 0xa3bf50
    static cPersistTypeInfo *__fastcall GetTypeInfo(void *face, void *, cPersistTypeInfo *out);       // 0xa3c160
    static uint32_t __fastcall Write(void *face, void *, ifaces::cIGZPersistWriter *writer);          // 0xa3c210
    static uint32_t __fastcall GetOwnerInfo(void *face, void *);                                      // 0xa3bf60
    // The behaviour template's bodies (object + 0x10).
    static void __fastcall OnEvent(void *face, void *, uint32_t type, uint32_t arg1, void *data, uint32_t arg3); // 0xa3bd10
    static void __fastcall NotifyListeners(void *face, void *, uint32_t type, uint32_t arg1, void *data);       // 0xa3bfb0
    static bool __fastcall AskListeners(void *face, void *, uint32_t type, uint32_t arg1, void *data);          // 0xa3bbc0
    static void __fastcall Connect(void *face, void *, uint32_t event, cIGZUnknown *target, uint32_t targetEvent);    // 0xa3ba80
    static void __fastcall Disconnect(void *face, void *, uint32_t event, cIGZUnknown *target, uint32_t targetEvent); // 0xa3bb50
    static nGZEvent::cEventAttribute *__fastcall GetEventAttribute(void *face, void *, nGZEvent::cEventAttribute *out,
                                                                   uint32_t event);                   // 0xa3b990
    // cIExtension's own (object + 0x1c).
    static cIGZPersistSerializableReferent *__fastcall GetPersistReferent(void *face, void *);         // 0xa3b950
    static cIObjectGraphNode *__fastcall SetOwner(void *face, void *, cIObjectGraphNode *owner);      // 0xa3c0d0
    static uint32_t __fastcall SetNext(void *face, void *, cIExtension *next);                        // 0xa3c0f0
    static bool __fastcall IsKindOf(void *face, void *, uint32_t iid);                                // 0xa3c130
    static void __fastcall GetDescription(void *face, void *, cIGZString *text);                      // 0xa3bb90
};

} // namespace nGZSceneGraph

// 0xa3b7d0's and 0xa3b750's hooks: the addresses of our constructors (`??0cExtension@nGZSceneGraph@@QAE@XZ`, thiscall with
// the most-derived flag on the stack, ret 4; the copy constructor `...@QAE@ABV01@@Z`, the source then the flag, ret 8),
// which C++ cannot name: C names the linker resolves to the decorated ones (/alternatename in cExtension.cpp; a wrong
// decoration fails the link). Code addresses, never data; the hook rows take them directly so the table stays a static
// initializer.
extern "C" const uint8_t OY_cExtension_Constructor;
extern "C" const uint8_t OY_cExtension_CopyConstructor;

namespace nGZSceneGraph {
inline const void *ExtensionBasePartConstructor() { return &OY_cExtension_Constructor; }
inline const void *ExtensionBasePartCopyConstructor() { return &OY_cExtension_CopyConstructor; }
} // namespace nGZSceneGraph
