#pragma once
#include "gz/cGZPersistResourceManager.h" // cGZPersistResourceKey
#include "gz/cIGZPersistResource.h"
#include "gz/cIGZSelfDescribing.h"
#include "gz/cIGZSerializable.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZUnknownBase.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

// cTSLotInfo: the neighbourhood's record of one lot (RTTI cTSLotInfo; bases cRZUnknown at +0 (vtable 0xdccbd4, 6 slots, its
// count at +4), cITSLotInfo at +8 (0xdccbf0, 58), cIGZSerializable at +0xc (0xdcccdc, 6), cIGZPersistResource at +0x10
// (0xdcccf8, 12), cIGZSelfDescribing at +0x14 (0xdccd2c, 5)). 0xac bytes.
//
// Much of the lot's data lives in a cTSBaseLotInfo (0xa4 bytes, held counted through its cITSBaseLotInfo face at +0x80):
// the lot info forwards to it and tells it when something changed (its slot 22 with 0x20, which ors the bits into its dirty
// word). The base's slot 8 is the lot type: type 9 lots defer the connection list to another lot info found through the
// app singleton (slot 57 here), type 8 lots keep it here (+0x88).
//
// Standalone shape (docs/standalone-shape.md, lane ED): a real C++ class deriving from cRZUnknown (a real base,
// rz/RZUnknownBase.h, primary as in the original) and its four interfaces. The compiler emits the five vtables (slot order =
// declaration order), the adjustor thunks and the RTTI; the destructor is cRZUnknown's virtual one (slot 3 of the +0 vtable).
// The game creates lot infos at one site, its creator 0x4c9180 (`operator new(0xac)`, the default constructor 0x4c8a40, the +8
// face; its address is registered with the framework), and copies them in Clone (slot 5, ours). The constructor, the copy
// constructor and the creator are hooked, so every lot info carries our vtables. The game's vtables are referenced only by the
// two constructors and the destructor 0x4c8c30. The RVA hooks on the old slot functions are adapters (cTSLotInfoAdapters).
// RTTI: the type descriptor 0x10a6204 has no code references.

// A 16-byte entry of the connection list (+0x88): four words, the first two set when added, the last two by slots 53 / 54.
struct LotInfoConnection {
    int32_t mField0;
    int32_t mField1;
    int32_t mField2;
    int32_t mField3;
};
static_assert(sizeof(LotInfoConnection) == 16);

using LotInfoConnections = std::vector<LotInfoConnection>;
static_assert(sizeof(LotInfoConnections) == 12);
// Signed 16-bit id -> 16-bit value (node 0x14 bytes); slot 55 looks values up (100 when missing), slot 56 sets them.
using LotInfoValueMap = std::map<int16_t, int16_t>;
static_assert(sizeof(LotInfoValueMap) == 8);

// The lot info's own interface (id 0x6bf97e03; the original's name, global as its RTTI). "Field44"/"Field4C"/"BaseByte88"/...
// are words whose meaning is not known. Folded slots name their shared body.
class cITSLotInfo : public cIGZUnknown {
public:
    // slot 3, folded 0x4656a0 (ret 4): replaces the base (+0x80), counting the new one before releasing the old.
    virtual void SetBaseLotInfo(void *base) = 0;
    virtual void *GetBaseLotInfo() = 0; // slot 4, folded 0x24bdc0
    // 0x4c90b0 (slot 5): a new copy of the lot info (built by the copy constructor), as this face; null when the allocation fails.
    virtual cITSLotInfo *Clone() = 0;
    // 0x4cb410 (slot 6, ret 4): assigns the name (+0x20) through its slot 17, then marks the base changed.
    virtual void SetName(void *text) = 0;
    // 0x4cb440 (slot 7, ret 4): assigns the name from C text (slot 3 with the length), then marks the base changed.
    virtual void SetNameText(const char *text) = 0;
    virtual cRZString *GetName() = 0; // slot 8, folded 0x158f90: &name
    // 0x4cb390 (slot 9, ret 8): stores the origin (+0x18, +0x1c) and marks the base changed.
    virtual void SetOrigin(int32_t x, int32_t y) = 0;
    virtual void GetOrigin(int32_t *x, int32_t *y) = 0; // slot 10, folded 0x4c9530 (ret 8)
    // 0x4c93b0 (slot 11, ret 8): the base's size (its slot 5), swapped when the rotation differs from the base's by an odd step.
    virtual void GetSize(int32_t *width, int32_t *depth) = 0;
    // 0x4c9230 (slot 12, ret 4): {x, y, x + width, y + depth} with the size from slot 11.
    virtual void GetBounds(int32_t *rect) = 0;
    // 0x4c9280 / 0x4c9300 (slots 13 / 14, ret 4): the bounds from slot 12 adjusted on each side the base flags (its slot 14 per
    // side, turned by slot 23): slot 13 moves the side in by one, slot 14 makes a one-wide strip on that side.
    virtual void GetInnerBounds(int32_t *rect) = 0;
    virtual void GetEdgeStrip(int32_t *rect) = 0;
    // 0x4cb5b0 (slot 15, ret 4): stores the elevation (+0x40) and marks the base changed.
    virtual void SetElevation(float value) = 0;
    // 0x4c9f90 (slot 16): the elevation; while it still holds the unset value (-FLT_MAX, the game's float at 0xda1eb8), it is
    // computed first when the app singleton's slot 11 object exists: its slot 12 gets the bounds (slot 12) and two float outs,
    // of which the first becomes the elevation.
    virtual float GetElevation() = 0;
    // 0x4cb480 (slot 17, ret 4): stores the word at +0x44 and marks the base changed.
    virtual void SetField44(int32_t value) = 0;
    virtual int32_t GetField44() = 0; // slot 18, folded 0x1674b0
    // 0x4c9f50 (slot 19): the base's slot 19.
    virtual void *GetBaseName() = 0;
    // 0x4c9f60 (slot 20): the slot 19 of the deferred-to lot info for type 9 lots that have one, else this one's slot 19.
    virtual void *GetEffectiveBaseName() = 0;
    // 0x4cb230 / 0x4c9110 (slots 21 / 22): store (and mark changed) / read the rotation byte (+0x48).
    virtual void SetRotation(uint8_t value) = 0;
    virtual uint8_t GetRotation() = 0;
    // 0x4cafc0 (slot 23): the rotation relative to the base's (its slot 10), modulo 4. AL only.
    virtual uint8_t GetRelativeRotation() = 0;
    virtual cIGZSerializable *AsSerializable() = 0;       // slot 24, folded 0x1f9910: this ? +0xc : 0
    virtual cIGZPersistResource *AsPersistResource() = 0; // slot 25, folded 0x4735d0: this ? +0x10 : 0
    // 0x4c9cc0 (slot 26): whether the persist system lists anything (its slot 36, into a block freed through the allocator
    // service) for the id its slot 11 gives for the name (slot 8) and kind 0x49ff7d76. AL only.
    virtual uint8_t HasLotResources() = 0;
    // 0x4cb4f0 (slot 27, ret 4): assigns the description (+0x50) through its slot 17, then marks the base changed.
    virtual void SetDescription(void *text) = 0;
    virtual cRZString *GetDescription() = 0; // slot 28, folded 0x225280: &description
    // 0x4cafe0 (slot 29, ret 8): stores the base's name (slot 19) and description (slot 21) as texts 1 and 2 of the lot resource
    // {0x53545223, group, instance | 0x8000, 0}, built through the framework when the persist system has none; for a second
    // language service whose slot 7 isn't 1 also as texts of language 0. Returns whether the resource's slot 21 answers 0.
    virtual uint8_t StoreNamesInResource(uint32_t instance, uint32_t group) = 0;
    // 0x4c9db0 (slot 30, ret 8): finds the lot resource (as slot 31) and, when found, copies its texts 1 and 2 (its slot 6)
    // into the base's name (slot 17) and description (slot 20) when not empty. Returns whether it was found.
    virtual uint8_t LoadNamesFromResource(uint32_t instance, uint32_t group) = 0;
    // 0x4c9550 (slot 31, ret 0x10): finds the resource {0x53545223, group, instance | 0x8000, 0} through the persist system
    // (slot 5, interface 0xfb4cb0de) and answers the resource's QueryInterface(interfaceId, out); 0 when there is none.
    // Doesn't use the lot info.
    virtual uint8_t QueryLotResource(uint32_t instance, uint32_t group, uint32_t interfaceId, void **out) = 0;
    // 0x4c9c50 (slot 32): the base's slot 8 (the lot type).
    virtual uint32_t GetLotType() = 0;
    virtual int32_t GetField4C() = 0; // slot 33, folded 0x1674d0
    // 0x4cb5d0 / 0x4cb1d0 (slots 34 / 37, ret 4): store the word at +0x4c / the lot id at +0x94 when it differs, and then mark
    // the base changed.
    virtual void SetField4C(int32_t value) = 0;
    // 0x4c9970 (slot 35, ret 8): hands out (through *out, counted) the extra info object at +0x84, getting it first when it is
    // missing: from the object the current lot cache (0x10cc2a8) gives for 0x5058ffb2, by key {0xb21be28b, the persist
    // system's slot 11 of this lot's slot 8, 1, 2}; with `create` set and no such object, built through the framework and
    // named after the base name (a format string at 0xdccd40). A freshly built one is also configured (slots 17 / 22 with the
    // helper 0x55d4d0) and stored back in the finder. Returns 0 when it cannot be found or built.
    virtual uint8_t GetOrCreateExtraInfo(void **out, uint8_t create) = 0;
    virtual int32_t GetLotId() = 0; // slot 36, folded 0x22fa70
    virtual void SetLotId(int32_t value) = 0;
    // 0x4c8f10 (slot 38, ret 8): adds a connection {a, b, 0, 0} (type 8; type 9 forwards to the deferred-to lot info).
    virtual uint8_t AddConnection(int32_t a, int32_t b) = 0;
    // 0x4ca6b0 / 0x4ca750 (slots 39 / 40, ret 4): remove the first connection whose second / first word matches.
    virtual uint8_t RemoveConnectionBySecond(int32_t value) = 0;
    virtual uint8_t RemoveConnectionByFirst(int32_t value) = 0;
    // 0x4ca7f0 (slot 41, ret 8): makes a new lot info (framework class 0xbf999e7) into *out for this lot: names it after the lot
    // manager's (app singleton slot 6) answer for slot 69, registers the lot's listing with the persist system, reads the lot's
    // file through a framework-made reader and attaches a settings object (class 0x6c589723) to the new lot, which is then
    // handed to the lot manager (its slot 61). `flag` becomes the settings' slot 17. Returns 0 with nothing done when the app
    // singleton has no lot manager (slot 6) or no current lot (slot 23), and 0 when the framework can't make the object for
    // slot 19's list of the found resource.
    virtual uint8_t CreateNewLotInfo(void **out, uint32_t flag) = 0;
    // 0x4c96b0 (slot 42): the number of connections; 0x4c9910 / 0x4c98b0 / 0x4c9850 / 0x4c9780 (slots 43..46, ret 4): one word
    // of the connection at an index (no range check).
    virtual int32_t GetConnectionCount() = 0;
    virtual int32_t GetConnectionField0(int32_t index) = 0;
    virtual int32_t GetConnectionField1(int32_t index) = 0;
    virtual int32_t GetConnectionField2(int32_t index) = 0;
    virtual int32_t GetConnectionField3(int32_t index) = 0;
    // 0x4c9610 (slot 47): the base's slot 31, at most 4 when there are connections. AL only.
    virtual uint8_t GetCappedBaseByte() = 0;
    // 0x4cb540 / 0x4c9650 / 0x4cb570 / 0x4c9660 / 0x4cb5a0 (slots 48..52): the base's slots 32..36 (setters of a byte and two
    // words, and their getters).
    virtual void SetBaseByte88(uint8_t value) = 0;
    virtual uint8_t GetBaseByte88() = 0;
    virtual void SetBaseWord90(uint32_t value) = 0;
    virtual uint32_t GetBaseWord90() = 0;
    virtual void SetBaseWord8C(uint32_t value) = 0;
    // 0x4cb130 / 0x4cb700 (slots 53 / 54, ret 8): set the third / fourth word of the first connection whose second word is the
    // (sign-extended) key.
    virtual uint8_t SetConnectionField2(int16_t key, int16_t value) = 0;
    virtual uint8_t SetConnectionField3(int16_t key, int16_t value) = 0;
    // 0x4c9710 (slot 55, ret 4): the value map's entry for the id (zero-extended), 100 for id 0 or a missing id.
    virtual uint32_t GetMappedValue(int16_t id) = 0;
    // 0x4cb630 (slot 56, ret 8): stores the value map's entry for the id, then posts (message server 0x9c5a89, its slot 4 with
    // 0) a standard message 0xeb425393 with slot 11 = 7 and slot 16 = the (sign-extended) id.
    virtual void SetMappedValue(int16_t id, int16_t value) = 0;
    // 0x4c9200 (slot 57): the lot info the app singleton's slot 6 object gives for this lot id (its slot 57), or null.
    virtual cITSLotInfo *GetDeferredLotInfo() = 0;
};

class cTSLotInfo : public rz::cRZUnknown,
                   public cITSLotInfo,
                   public cIGZSerializable,
                   public cIGZPersistResource,
                   public cIGZSelfDescribing {
public:
    // The game's vtables (the tests relate ours to them; nothing in src/ installs them any more).
    static constexpr uint32_t kOriginalVTable = 0xdccbd4;
    static constexpr uint32_t kOriginalLotInfoVTable = 0xdccbf0;
    static constexpr uint32_t kOriginalSerialVTable = 0xdcccdc;
    static constexpr uint32_t kOriginalPersistVTable = 0xdcccf8;
    static constexpr uint32_t kOriginalDescribeVTable = 0xdccd2c;
    static constexpr uint32_t kClassId = 0xbf999e7;
    static constexpr uint32_t kInterfaceId = 0x6bf97e03;
    static constexpr uint32_t kVersion = 0x12;
    static constexpr uint32_t kBaseSize = 0xa4; // cTSBaseLotInfo
    static constexpr uint32_t kChanged = 0x20;  // what the base's slot 22 is told
    static constexpr uint32_t kSize = 0xac;

    // 0x4c8a40: default lot info; a new default cTSBaseLotInfo (0x4c86c0) becomes the base. The rotation byte (+0x48) and the
    // padding after +0xa0 are left unset.
    cTSLotInfo();
    // 0x4c8800: copy of the origin, elevation, the words at +0x44 / +0x4c / +0x94 / +0xa4 / +0xa8, the byte at +0xa0, the
    // description and a copy of the base (0x4c83d0); the name, connections, value map and key start empty.
    cTSLotInfo(const cTSLotInfo &other);
    // 0x4c8c30: destroys the members in reverse order; the scalar deleting destructor 0x4c8ee0 (slot 3 of +0, ret 4) is the
    // compiler's.
    ~cTSLotInfo() override;
    cTSLotInfo &operator=(const cTSLotInfo &) = delete;
    // 0x4c9180 (cdecl): new, the +8 face; null when the allocation fails.
    static cITSLotInfo *Create();

    // cIGZUnknown (every face; the other faces' slots 0-2 are adjustor thunks, as in the original: QueryInterface 0x4ca0fd,
    // 0x4ca115, 0x4ca105, 0x4ca10d; AddRef/Release the folded 0x146a55/0x1f9aa5 and their kin)
    bool QueryInterface(uint32_t riid, void **out) override; // slot 0, 0x4ca090 (ret 8)
    uint32_t AddRef() override;                              // slot 1, folded 0x146a50: cRZUnknown's
    uint32_t Release() override;                             // slot 2, folded 0x1f9aa0: cRZUnknown's

    // cITSLotInfo (+8)
    void SetBaseLotInfo(void *base) override;
    void *GetBaseLotInfo() override;
    cITSLotInfo *Clone() override;
    void SetName(void *text) override;
    void SetNameText(const char *text) override;
    cRZString *GetName() override;
    void SetOrigin(int32_t x, int32_t y) override;
    void GetOrigin(int32_t *x, int32_t *y) override;
    void GetSize(int32_t *width, int32_t *depth) override;
    void GetBounds(int32_t *rect) override;
    void GetInnerBounds(int32_t *rect) override;
    void GetEdgeStrip(int32_t *rect) override;
    void SetElevation(float value) override;
    float GetElevation() override;
    void SetField44(int32_t value) override;
    int32_t GetField44() override;
    void *GetBaseName() override;
    void *GetEffectiveBaseName() override;
    void SetRotation(uint8_t value) override;
    uint8_t GetRotation() override;
    uint8_t GetRelativeRotation() override;
    cIGZSerializable *AsSerializable() override;
    cIGZPersistResource *AsPersistResource() override;
    uint8_t HasLotResources() override;
    void SetDescription(void *text) override;
    cRZString *GetDescription() override;
    uint8_t StoreNamesInResource(uint32_t instance, uint32_t group) override;
    uint8_t LoadNamesFromResource(uint32_t instance, uint32_t group) override;
    uint8_t QueryLotResource(uint32_t instance, uint32_t group, uint32_t interfaceId, void **out) override;
    uint32_t GetLotType() override;
    int32_t GetField4C() override;
    void SetField4C(int32_t value) override;
    uint8_t GetOrCreateExtraInfo(void **out, uint8_t create) override;
    int32_t GetLotId() override;
    void SetLotId(int32_t value) override;
    uint8_t AddConnection(int32_t a, int32_t b) override;
    uint8_t RemoveConnectionBySecond(int32_t value) override;
    uint8_t RemoveConnectionByFirst(int32_t value) override;
    uint8_t CreateNewLotInfo(void **out, uint32_t flag) override;
    int32_t GetConnectionCount() override;
    int32_t GetConnectionField0(int32_t index) override;
    int32_t GetConnectionField1(int32_t index) override;
    int32_t GetConnectionField2(int32_t index) override;
    int32_t GetConnectionField3(int32_t index) override;
    uint8_t GetCappedBaseByte() override;
    void SetBaseByte88(uint8_t value) override;
    uint8_t GetBaseByte88() override;
    void SetBaseWord90(uint32_t value) override;
    uint32_t GetBaseWord90() override;
    void SetBaseWord8C(uint32_t value) override;
    uint8_t SetConnectionField2(int16_t key, int16_t value) override;
    uint8_t SetConnectionField3(int16_t key, int16_t value) override;
    uint32_t GetMappedValue(int16_t id) override;
    void SetMappedValue(int16_t id, int16_t value) override;
    cITSLotInfo *GetDeferredLotInfo() override;

    // cIGZSerializable (+0xc; these run with this = the face)
    // 0x4cb8f0: version 0x12 record. True.
    bool Write(ifaces::cIGZOStream *stream) override;
    // 0x4ca2a0: the reverse, version dependent (see the body). False only when the base fails to read; the base record's
    // answer byte is passed through as it is.
    bool Read(ifaces::cIGZIStream *stream) override;
    uint32_t GetGZCLSID() override; // 0x4c91f0

    // cIGZPersistResource (+0x10)
    cGZPersistResourceKey GetKey() override;                // folded 0x4c9520
    bool SetKey(const cGZPersistResourceKey &key) override; // folded 0x4cb380
    uint32_t GetType() override;                            // folded 0x230120
    uint32_t GetGroup() override;                           // folded 0x22fca0
    uint32_t GetInstance() override;                        // folded 0x254bb0
    bool SetType(uint32_t type) override;                   // folded 0x4cb7c0
    bool SetGroup(uint32_t group) override;                 // folded 0x4cb260
    bool SetInstance(uint32_t instance) override;           // folded 0x4cb370
    uint32_t GetRefCount() override;                        // folded 0x4a96e0 (add ecx, -0x10; jmp cRZUnknown's count getter)

    // cIGZSelfDescribing (+0x14)
    bool Describe(void *target) override;     // 0x4c9690: the +8 face's slot 19 result to the target's slot 16; true
    bool DescribeMore(void *target) override; // folded 0x18a7a0: false

    // Tells the base that something changed (its slot 22 with 0x20).
    void MarkChanged();

    // +0x00..+0x04: cRZUnknown (vtable, count); +0x08..+0x14: the four interface vtable pointers.
    // The texts are cRZString in its hook shape (non-polymorphic; built and destroyed by Construct / Destruct, which store the
    // game's string vtable): unions keep the compiler from constructing or destroying their std::string a second time. The
    // connection list and the value map are unions too so the destructor can destroy the members in the original's order
    // (values, connections, the two counted objects, description, name).
    int32_t mOriginX;                             // +0x18
    int32_t mOriginY;                             // +0x1c
    union { cRZString mName; };                   // +0x20 not saved (its count at +0x3c)
    float mElevation;                             // +0x40 -FLT_MAX until computed (slot 16)
    int32_t mField44;                             // +0x44 -1 by default
    uint8_t mRotation;                            // +0x48 left unset by both constructors
    uint8_t mPad49[3];
    int32_t mField4C;                             // +0x4c
    union { cRZString mDescription; };            // +0x50 (its count at +0x6c)
    cGZPersistResourceKey mKey;                   // +0x70 the persist face's resource key
    void *mBase;                                  // +0x80 cTSBaseLotInfo's +8 face, counted
    void *mField84;                               // +0x84 counted (released in the destructor)
    union { LotInfoConnections mConnections; };   // +0x88
    int32_t mLotId;                               // +0x94
    union { LotInfoValueMap mValues; };           // +0x98
    uint8_t mByteA0;                              // +0xa0
    int32_t mFieldA4;                             // +0xa4
    int32_t mFieldA8;                             // +0xa8
};
static_assert(sizeof(cTSLotInfo) == cTSLotInfo::kSize, "cTSLotInfo layout");
static_assert(offsetof(cTSLotInfo, mRefCount) == 0x04, "the cRZUnknown count");
static_assert(offsetof(cTSLotInfo, mOriginX) == 0x18);
static_assert(offsetof(cTSLotInfo, mName) == 0x20);
static_assert(offsetof(cTSLotInfo, mElevation) == 0x40);
static_assert(offsetof(cTSLotInfo, mRotation) == 0x48);
static_assert(offsetof(cTSLotInfo, mDescription) == 0x50);
static_assert(offsetof(cTSLotInfo, mKey) == 0x70);
static_assert(offsetof(cTSLotInfo, mBase) == 0x80);
static_assert(offsetof(cTSLotInfo, mConnections) == 0x88);
static_assert(offsetof(cTSLotInfo, mLotId) == 0x94);
static_assert(offsetof(cTSLotInfo, mValues) == 0x98);
static_assert(offsetof(cTSLotInfo, mFieldA8) == 0xa8);

// Where each interface sits in the object (RTTI; base offsets are not constant expressions: the tests check them).
namespace lotinfo {
constexpr uint32_t kLotInfoFace = 0x8;
constexpr uint32_t kSerialFace = 0xc;
constexpr uint32_t kPersistFace = 0x10;
constexpr uint32_t kDescribeFace = 0x14;
} // namespace lotinfo

// The RVA hooks' entry points (src/hook/tables/cTSLotInfo.cpp). Every lot info the game creates carries our vtables, so these
// run only where the game calls an old entry point directly or holds a lot info built with its own vtables (none while the
// constructor and Create hooks are on). Original calling convention (this in ecx = the face of the slot), our functions called
// without virtual dispatch, bodies inlined.
struct cTSLotInfoAdapters {
    using Self = cTSLotInfo;
    static Self *__fastcall Construct(Self *memory, void *);
    static Self *__fastcall CopyConstruct(Self *memory, void *, const Self *other);
    static void __fastcall Destruct(Self *self, void *);
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t riid, void **out);
    static cITSLotInfo *__fastcall Clone(cITSLotInfo *face, void *);
    static void __fastcall SetName(cITSLotInfo *face, void *, void *text);
    static void __fastcall SetNameText(cITSLotInfo *face, void *, const char *text);
    static void __fastcall SetOrigin(cITSLotInfo *face, void *, int32_t x, int32_t y);
    static void __fastcall GetSize(cITSLotInfo *face, void *, int32_t *width, int32_t *depth);
    static void __fastcall GetBounds(cITSLotInfo *face, void *, int32_t *rect);
    static void __fastcall GetInnerBounds(cITSLotInfo *face, void *, int32_t *rect);
    static void __fastcall GetEdgeStrip(cITSLotInfo *face, void *, int32_t *rect);
    static void __fastcall SetElevation(cITSLotInfo *face, void *, float value);
    static float __fastcall GetElevation(cITSLotInfo *face, void *);
    static void __fastcall SetField44(cITSLotInfo *face, void *, int32_t value);
    static void *__fastcall GetBaseName(cITSLotInfo *face, void *);
    static void *__fastcall GetEffectiveBaseName(cITSLotInfo *face, void *);
    static void __fastcall SetRotation(cITSLotInfo *face, void *, uint8_t value);
    static uint8_t __fastcall GetRotation(cITSLotInfo *face, void *);
    static uint8_t __fastcall GetRelativeRotation(cITSLotInfo *face, void *);
    static uint8_t __fastcall HasLotResources(cITSLotInfo *face, void *);
    static void __fastcall SetDescription(cITSLotInfo *face, void *, void *text);
    static uint8_t __fastcall StoreNamesInResource(cITSLotInfo *face, void *, uint32_t instance, uint32_t group);
    static uint8_t __fastcall LoadNamesFromResource(cITSLotInfo *face, void *, uint32_t instance, uint32_t group);
    static uint8_t __fastcall QueryLotResource(cITSLotInfo *face, void *, uint32_t instance, uint32_t group, uint32_t interfaceId,
                                               void **out);
    static uint32_t __fastcall GetLotType(cITSLotInfo *face, void *);
    static void __fastcall SetField4C(cITSLotInfo *face, void *, int32_t value);
    static uint8_t __fastcall GetOrCreateExtraInfo(cITSLotInfo *face, void *, void **out, uint8_t create);
    static void __fastcall SetLotId(cITSLotInfo *face, void *, int32_t value);
    static uint8_t __fastcall AddConnection(cITSLotInfo *face, void *, int32_t a, int32_t b);
    static uint8_t __fastcall RemoveConnectionBySecond(cITSLotInfo *face, void *, int32_t value);
    static uint8_t __fastcall RemoveConnectionByFirst(cITSLotInfo *face, void *, int32_t value);
    static uint8_t __fastcall CreateNewLotInfo(cITSLotInfo *face, void *, void **out, uint32_t flag);
    static int32_t __fastcall GetConnectionCount(cITSLotInfo *face, void *);
    static int32_t __fastcall GetConnectionField0(cITSLotInfo *face, void *, int32_t index);
    static int32_t __fastcall GetConnectionField1(cITSLotInfo *face, void *, int32_t index);
    static int32_t __fastcall GetConnectionField2(cITSLotInfo *face, void *, int32_t index);
    static int32_t __fastcall GetConnectionField3(cITSLotInfo *face, void *, int32_t index);
    static uint8_t __fastcall GetCappedBaseByte(cITSLotInfo *face, void *);
    static void __fastcall SetBaseByte88(cITSLotInfo *face, void *, uint8_t value);
    static uint8_t __fastcall GetBaseByte88(cITSLotInfo *face, void *);
    static void __fastcall SetBaseWord90(cITSLotInfo *face, void *, uint32_t value);
    static uint32_t __fastcall GetBaseWord90(cITSLotInfo *face, void *);
    static void __fastcall SetBaseWord8C(cITSLotInfo *face, void *, uint32_t value);
    static uint8_t __fastcall SetConnectionField2(cITSLotInfo *face, void *, int16_t key, int16_t value);
    static uint8_t __fastcall SetConnectionField3(cITSLotInfo *face, void *, int16_t key, int16_t value);
    static uint32_t __fastcall GetMappedValue(cITSLotInfo *face, void *, int16_t id);
    static void __fastcall SetMappedValue(cITSLotInfo *face, void *, int16_t id, int16_t value);
    static cITSLotInfo *__fastcall GetDeferredLotInfo(cITSLotInfo *face, void *);
    static bool __fastcall Write(cIGZSerializable *face, void *, ifaces::cIGZOStream *stream);
    static bool __fastcall Read(cIGZSerializable *face, void *, ifaces::cIGZIStream *stream);
    static uint32_t __fastcall GetGZCLSID(cIGZSerializable *face, void *);
    static bool __fastcall Describe(cIGZSelfDescribing *face, void *, void *target);
};
