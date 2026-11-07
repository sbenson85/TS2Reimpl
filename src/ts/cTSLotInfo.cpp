#include "rz/RZMessageServer.h"
#include "ts/cTSLotInfo.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZAutoRef.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"
#include "app/AppInit.h"
#include "simantics/PrimGenericSimCallMessage.h"
#include "simantics/ServiceRefVector.h"
#include "simantics/TokenExpander.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/ServiceGetters.h"
#include "rz/cGZAllocatorService.h"

#include <cstring>
#include <new>

namespace {

constexpr uint32_t kPersistInterface = 0x456b8f1d;      // the +0x10 face
constexpr uint32_t kSerializableInterface = 0xe4fda3d4; // the +0xc face
constexpr uint32_t kDescribeInterface = 0x51f85085;     // the +0x14 face

constexpr uint32_t kLotTypeDeferred = 9; // connections live in another lot info (slot 57)
constexpr uint32_t kLotTypeOwn = 8;      // connections live here

constexpr uint32_t kLotResourceKind = 0x49ff7d76;   // slot 26's third argument to the persist system's slot 36
constexpr uint32_t kLotResourceType = 0x53545223;   // the resource type of slot 31's key
constexpr uint32_t kResourceInterface = 0xfb4cb0de; // the interface slot 31 asks the found resource for
constexpr uint32_t kUnknownElevation = 0xda1eb8;    // the game's float the elevation starts at (-FLT_MAX): not computed yet
constexpr uint32_t kExtraInfoKind = 0xb21be28b;      // the extra info object's class id and the first word of its key
constexpr uint32_t kExtraInfoInterface = 0xd21be2b7; // the interface slot 35 asks the finder and the framework for
constexpr uint32_t kExtraInfoFinderId = 0x5058ffb2;  // what the current lot object (cache 0x10cc2a8) is asked for (its slot 34)
constexpr uint32_t kExtraInfoTitleFormat = 0xdccd40; // RVA of the title format string (one %s: the base name)
constexpr uint32_t kNoticeText = 0xdccd58;           // RVA of the text slot 41 gives the new lot's settings (0x2d characters)
constexpr uint32_t kNoticeLength = 0x2d;
constexpr uint32_t kListingQuery = 0x6ced477c;       // what the common implementation is asked (slot 4, with an empty text)
constexpr uint32_t kValueChangedMessage = 0xeb425393; // the message slot 56 posts (slot 11 = 7, slot 16 = the id)

// The base's slots (cITSBaseLotInfo face).
namespace base {
constexpr int kGetSize = 5;
constexpr int kGetType = 8;
constexpr int kGetRotation = 10;
constexpr int kHasEdge = 14;
constexpr int kSetName = 17;
constexpr int kGetName = 19;
constexpr int kMarkChanged = 22;
constexpr int kSetCount = 22; // the same slot: old records hand it a word read from the stream
constexpr int kGetByte7C = 31;
constexpr int kSerializable = 37;
} // namespace base

uint32_t Slot0(void *object, int slot) { return VirtualSlot<uint32_t(__thiscall *)(void *)>(object, slot)(object); }
void Slot1(void *object, int slot, uint32_t value)
{
    VirtualSlot<void(__thiscall *)(void *, uint32_t)>(object, slot)(object, value);
}
uint8_t Byte0(void *object, int slot) { return VirtualSlot<uint8_t(__thiscall *)(void *)>(object, slot)(object); }

void AddRefObject(void *object) { As<cIGZUnknown>(object)->AddRef(); }
void ReleaseObject(void *object) { As<cIGZUnknown>(object)->Release(); }

// Builds a new cTSBaseLotInfo (a copy of the source lot info's base, or a default one) and returns its +8 face; null when the
// allocation fails. 0x4c86c0 is cTSBaseLotInfo's default constructor, 0x4c83d0 its copy constructor (__thiscall, ret 4:
// the source base's +8 face). The source's base comes through its slot 4, asked only once the memory is there.
void *NewBase(const cTSLotInfo *source)
{
    void *memory = ::operator new(cTSLotInfo::kBaseSize);
    void *object = nullptr;
    if (memory) {
        if (source) {
            void *sourceBase = const_cast<cTSLotInfo *>(source)->GetBaseLotInfo();
            object = OriginalFunction<void *(__thiscall *)(void *, void *), 0x4c83d0>()(memory, sourceBase);
        } else
            object = OriginalFunction<void *(__thiscall *)(void *), 0x4c86c0>()(memory);
    }
    return object ? static_cast<char *>(object) + 8 : nullptr;
}

// The stream slots (cIGZIStream / cIGZOStream).
namespace stream {
constexpr int kBool = 4;
constexpr int kUint8 = 5;
constexpr int kSint16 = 6;
constexpr int kUint16 = 7;
constexpr int kSint32 = 8;
constexpr int kUint32 = 9;
constexpr int kFloat = 12;
constexpr int kString = 15;
} // namespace stream

uint8_t Get(void *in, int slot, void *out) { return VirtualSlot<uint8_t(__thiscall *)(void *, void *)>(in, slot)(in, out); }
uint8_t Put(void *out, int slot, uint32_t value)
{
    return VirtualSlot<uint8_t(__thiscall *)(void *, uint32_t)>(out, slot)(out, value);
}
uint8_t PutFloat(void *out, float value)
{
    return VirtualCall<uint8_t(__thiscall *)(void *, float), stream::kFloat>(out, value);
}

} // namespace

void cTSLotInfo::MarkChanged() { Slot1(mBase, base::kMarkChanged, kChanged); }

// ---- construction and destruction ----

// 0x4c8a40 (called by the creator 0x4c9180): the compiler installs our five vtables after cRZUnknown's constructor; the texts
// are built by cRZString's (the game's string vtable, an empty string, a zero count).
cTSLotInfo::cTSLotInfo()
    : mOriginX(0), mOriginY(0), mElevation(-3.4028234663852886e38f), mField44(-1), mField4C(0), mKey{}, mBase(nullptr),
      mField84(nullptr), mLotId(0), mByteA0(0), mFieldA4(0), mFieldA8(0)
{
    mName.Construct();
    mDescription.Construct();
    new (&mConnections) LotInfoConnections();
    new (&mValues) LotInfoValueMap();
    cTSLotInfo::SetBaseLotInfo(NewBase(nullptr));
}

// 0x4c8800 (called by Clone 0x4c90b0)
cTSLotInfo::cTSLotInfo(const cTSLotInfo &other)
    : rz::cRZUnknown(), cITSLotInfo(), cIGZSerializable(), cIGZPersistResource(), cIGZSelfDescribing(), mOriginX(other.mOriginX),
      mOriginY(other.mOriginY), mElevation(other.mElevation), mField44(other.mField44), mField4C(other.mField4C), mKey{},
      mBase(nullptr), mField84(nullptr), mLotId(other.mLotId), mByteA0(other.mByteA0), mFieldA4(other.mFieldA4),
      mFieldA8(other.mFieldA8)
{
    mName.Construct();
    mDescription.Construct(other.mDescription, 0, 0xffffffffu);
    new (&mConnections) LotInfoConnections();
    new (&mValues) LotInfoValueMap();
    cTSLotInfo::SetBaseLotInfo(NewBase(&other));
}

// 0x4c8c30: the members in reverse order, then cRZUnknown's destructor (the compiler's).
cTSLotInfo::~cTSLotInfo()
{
    mValues.~LotInfoValueMap();
    mConnections.~LotInfoConnections();
    if (mField84) ReleaseObject(mField84);
    if (mBase) ReleaseObject(mBase);
    mDescription.Destruct();
    mName.Destruct();
}

// 0x4c9180
cITSLotInfo *cTSLotInfo::Create()
{
    return new (std::nothrow) cTSLotInfo;
}

// ---- cIGZUnknown ----

// 0x4ca090: the faces by interface id with the null test of a base-class conversion, counted through slot 1; the describe face
// counted with cRZUnknown's AddRef called directly; other ids go to cRZUnknown's answer (id 1).
bool cTSLotInfo::QueryInterface(uint32_t riid, void **out)
{
    switch (riid) {
    case kInterfaceId:
        *out = static_cast<cITSLotInfo *>(this);
        break;
    case kPersistInterface:
        *out = static_cast<cIGZPersistResource *>(this);
        break;
    case kDescribeInterface:
        *out = static_cast<cIGZSelfDescribing *>(this);
        rz::cRZUnknown::AddRef();
        return true;
    case kSerializableInterface:
        *out = static_cast<cIGZSerializable *>(this);
        break;
    default:
        return rz::cRZUnknown::QueryInterface(riid, out);
    }
    AddRef();
    return true;
}

// folded 0x146a50 / 0x1f9aa0: cRZUnknown's (Release deletes through the destructor slot at a count of 1 or below).
uint32_t cTSLotInfo::AddRef()
{
    return rz::cRZUnknown::AddRef();
}

uint32_t cTSLotInfo::Release()
{
    return rz::cRZUnknown::Release();
}

// ---- the cITSLotInfo face ----

// folded 0x4656a0
void cTSLotInfo::SetBaseLotInfo(void *face)
{
    void *old = mBase;
    if (face == old) return;
    if (face) AddRefObject(face);
    mBase = face;
    if (old) ReleaseObject(old);
}

void *cTSLotInfo::GetBaseLotInfo() { return mBase; }

cITSLotInfo *cTSLotInfo::Clone()
{
    return new (std::nothrow) cTSLotInfo(*this);
}

void cTSLotInfo::SetName(void *text)
{
    VirtualSlot<void(__thiscall *)(void *, void *)>(&mName, 17)(&mName, text);
    MarkChanged();
}

void cTSLotInfo::SetNameText(const char *text)
{
    VirtualSlot<void(__thiscall *)(void *, const char *, uint32_t)>(&mName, 3)(&mName, text, uint32_t(std::strlen(text)));
    MarkChanged();
}

cRZString *cTSLotInfo::GetName() { return &mName; }

void cTSLotInfo::SetOrigin(int32_t x, int32_t y)
{
    mOriginX = x;
    mOriginY = y;
    MarkChanged();
}

// folded 0x4c9530
void cTSLotInfo::GetOrigin(int32_t *x, int32_t *y)
{
    *x = mOriginX;
    *y = mOriginY;
}

void cTSLotInfo::GetSize(int32_t *width, int32_t *depth)
{
    VirtualSlot<void(__thiscall *)(void *, int32_t *, int32_t *)>(mBase, base::kGetSize)(mBase, width, depth);
    const uint32_t baseRotation = Slot0(mBase, base::kGetRotation); // the whole register is subtracted
    if ((uint32_t(mRotation) - baseRotation) & 1) {
        const int32_t swap = *width;
        *width = *depth;
        *depth = swap;
    }
}

void cTSLotInfo::GetBounds(int32_t *rect)
{
    int32_t width, depth;
    GetSize(&width, &depth);
    rect[0] = mOriginX;
    rect[1] = mOriginY;
    rect[2] = mOriginX + width;
    rect[3] = mOriginY + depth;
}

namespace {
// Shared walk of slots 13 / 14: side i (0..3) of the base, turned by the relative rotation.
template <class Adjust> void AdjustBySides(cTSLotInfo *self, int32_t *rect, Adjust adjust)
{
    self->GetBounds(rect);
    const uint32_t turn = self->GetRelativeRotation();
    void *baseFace = self->mBase;
    for (int32_t side = 0; side < 4; ++side) {
        if (VirtualCall<uint8_t(__thiscall *)(void *, int32_t), base::kHasEdge>(baseFace, side))
            adjust(rect, (side + turn) & 3);
    }
}
} // namespace

void cTSLotInfo::GetInnerBounds(int32_t *rect)
{
    AdjustBySides(this, rect, [](int32_t *r, uint32_t which) {
        switch (which) {
        case 0: r[0] += 1; break;
        case 1: r[3] -= 1; break;
        case 2: r[2] -= 1; break;
        case 3: r[1] += 1; break;
        }
    });
}

void cTSLotInfo::GetEdgeStrip(int32_t *rect)
{
    AdjustBySides(this, rect, [](int32_t *r, uint32_t which) {
        switch (which) {
        case 0: r[2] = r[0] + 1; break;
        case 1: r[1] = r[3] - 1; break;
        case 2: r[0] = r[2] - 1; break;
        case 3: r[3] = r[1] + 1; break;
        }
    });
}

void cTSLotInfo::SetElevation(float value)
{
    mElevation = value;
    MarkChanged();
}

float cTSLotInfo::GetElevation()
{
    if (mElevation == *GameAddress<float>(kUnknownElevation)) {
        void *singleton = GetCachedSingleton();
        void *terrain = reinterpret_cast<void *>(Slot0(singleton, 11));
        if (terrain) {
            int32_t rect[4];
            GetBounds(rect);
            float height, other;
            VirtualCall<void(__thiscall *)(void *, int32_t *, float *, float *), 12>(terrain, rect, &height, &other);
            mElevation = height;
        }
    }
    return mElevation;
}

void cTSLotInfo::SetField44(int32_t value)
{
    mField44 = value;
    MarkChanged();
}

int32_t cTSLotInfo::GetField44() { return mField44; }

void *cTSLotInfo::GetBaseName() { return reinterpret_cast<void *>(Slot0(mBase, base::kGetName)); }

void *cTSLotInfo::GetEffectiveBaseName()
{
    if (GetLotType() == kLotTypeDeferred) {
        cITSLotInfo *other = GetDeferredLotInfo();
        if (other) return other->GetBaseName();
    }
    return GetBaseName();
}

void cTSLotInfo::SetRotation(uint8_t value)
{
    mRotation = value;
    MarkChanged();
}

uint8_t cTSLotInfo::GetRotation() { return mRotation; }

uint8_t cTSLotInfo::GetRelativeRotation()
{
    const uint8_t baseRotation = Byte0(mBase, base::kGetRotation);
    return uint8_t(mRotation - baseRotation) & 3;
}

cIGZSerializable *cTSLotInfo::AsSerializable() { return static_cast<cIGZSerializable *>(this); }

cIGZPersistResource *cTSLotInfo::AsPersistResource() { return static_cast<cIGZPersistResource *>(this); }

void cTSLotInfo::SetDescription(void *text)
{
    VirtualSlot<void(__thiscall *)(void *, void *)>(&mDescription, 17)(&mDescription, text);
    MarkChanged();
}

cRZString *cTSLotInfo::GetDescription() { return &mDescription; }

uint32_t cTSLotInfo::GetLotType() { return Slot0(mBase, base::kGetType); }

int32_t cTSLotInfo::GetField4C() { return mField4C; }

void cTSLotInfo::SetField4C(int32_t value)
{
    if (mField4C == value) return;
    mField4C = value;
    MarkChanged();
}

int32_t cTSLotInfo::GetLotId() { return mLotId; }

void cTSLotInfo::SetLotId(int32_t value)
{
    if (mLotId == value) return;
    mLotId = value;
    MarkChanged();
}

namespace {
// The type test the connection slots start with: the deferred-to lot info for type 9 (null when there is none or the type is
// neither), or this lot's own list for type 8 (own = true).
cITSLotInfo *DeferredFor(cTSLotInfo *self, bool &own)
{
    own = false;
    if (Slot0(self->GetBaseLotInfo(), base::kGetType) == kLotTypeDeferred) return self->GetDeferredLotInfo();
    own = Slot0(self->GetBaseLotInfo(), base::kGetType) == kLotTypeOwn;
    return nullptr;
}

void MarkChangedThroughSlot4(cTSLotInfo *self)
{
    Slot1(self->GetBaseLotInfo(), base::kMarkChanged, cTSLotInfo::kChanged);
}
} // namespace

uint8_t cTSLotInfo::AddConnection(int32_t a, int32_t b)
{
    bool own;
    cITSLotInfo *other = DeferredFor(this, own);
    if (other) return other->AddConnection(a, b);
    if (!own) return 0;
    mConnections.push_back(LotInfoConnection{a, b, 0, 0});
    MarkChangedThroughSlot4(this);
    return 1;
}

namespace {
template <class Forward, class Match>
uint8_t RemoveConnection(cTSLotInfo *self, int32_t value, Forward forward, Match match)
{
    bool own;
    cITSLotInfo *other = DeferredFor(self, own);
    if (other) return forward(other, value);
    if (!own) return 0;
    auto &list = self->mConnections;
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (match(*it, value)) {
            list.erase(it);
            MarkChangedThroughSlot4(self);
            return 1;
        }
    }
    return 0;
}

template <class Forward, class Get> int32_t ConnectionWord(cTSLotInfo *self, int32_t index, Forward forward, Get get)
{
    bool own;
    cITSLotInfo *other = DeferredFor(self, own);
    if (other) return forward(other, index);
    if (!own) return 0;
    return get(self->mConnections.data()[index]);
}

template <class Forward, class Set>
uint8_t SetConnectionWord(cTSLotInfo *self, int16_t key, int16_t value, Forward forward, Set set)
{
    bool own;
    cITSLotInfo *other = DeferredFor(self, own);
    if (other) return forward(other, key, value);
    if (!own) return 0;
    auto &list = self->mConnections;
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].mField1 == int32_t(key)) {
            set(list[i], int32_t(value));
            MarkChangedThroughSlot4(self);
            return 1;
        }
    }
    return 0;
}
} // namespace

uint8_t cTSLotInfo::RemoveConnectionBySecond(int32_t value)
{
    return RemoveConnection(
        this, value, [](cITSLotInfo *other, int32_t v) { return other->RemoveConnectionBySecond(v); },
        [](const LotInfoConnection &c, int32_t v) { return c.mField1 == v; });
}

uint8_t cTSLotInfo::RemoveConnectionByFirst(int32_t value)
{
    return RemoveConnection(
        this, value, [](cITSLotInfo *other, int32_t v) { return other->RemoveConnectionByFirst(v); },
        [](const LotInfoConnection &c, int32_t v) { return c.mField0 == v; });
}

int32_t cTSLotInfo::GetConnectionCount()
{
    bool own;
    cITSLotInfo *other = DeferredFor(this, own);
    if (other) return other->GetConnectionCount();
    if (!own) return 0;
    return int32_t(mConnections.size());
}

int32_t cTSLotInfo::GetConnectionField0(int32_t index)
{
    return ConnectionWord(
        this, index, [](cITSLotInfo *other, int32_t i) { return other->GetConnectionField0(i); },
        [](const LotInfoConnection &c) { return c.mField0; });
}
int32_t cTSLotInfo::GetConnectionField1(int32_t index)
{
    return ConnectionWord(
        this, index, [](cITSLotInfo *other, int32_t i) { return other->GetConnectionField1(i); },
        [](const LotInfoConnection &c) { return c.mField1; });
}
int32_t cTSLotInfo::GetConnectionField2(int32_t index)
{
    return ConnectionWord(
        this, index, [](cITSLotInfo *other, int32_t i) { return other->GetConnectionField2(i); },
        [](const LotInfoConnection &c) { return c.mField2; });
}
int32_t cTSLotInfo::GetConnectionField3(int32_t index)
{
    return ConnectionWord(
        this, index, [](cITSLotInfo *other, int32_t i) { return other->GetConnectionField3(i); },
        [](const LotInfoConnection &c) { return c.mField3; });
}

uint8_t cTSLotInfo::GetCappedBaseByte()
{
    const uint8_t value = Byte0(GetBaseLotInfo(), base::kGetByte7C);
    if (GetConnectionCount() > 0 && int8_t(value) > 4) return 4;
    return value;
}

void cTSLotInfo::SetBaseByte88(uint8_t value)
{
    VirtualCall<void(__thiscall *)(void *, uint8_t), 32>(GetBaseLotInfo(), value);
}
uint8_t cTSLotInfo::GetBaseByte88() { return Byte0(GetBaseLotInfo(), 33); }
void cTSLotInfo::SetBaseWord90(uint32_t value) { Slot1(GetBaseLotInfo(), 34, value); }
uint32_t cTSLotInfo::GetBaseWord90() { return Slot0(GetBaseLotInfo(), 35); }
void cTSLotInfo::SetBaseWord8C(uint32_t value) { Slot1(GetBaseLotInfo(), 36, value); }

uint8_t cTSLotInfo::SetConnectionField2(int16_t key, int16_t value)
{
    return SetConnectionWord(
        this, key, value, [](cITSLotInfo *other, int16_t k, int16_t v) { return other->SetConnectionField2(k, v); },
        [](LotInfoConnection &c, int32_t v) { c.mField2 = v; });
}

uint8_t cTSLotInfo::SetConnectionField3(int16_t key, int16_t value)
{
    return SetConnectionWord(
        this, key, value, [](cITSLotInfo *other, int16_t k, int16_t v) { return other->SetConnectionField3(k, v); },
        [](LotInfoConnection &c, int32_t v) { c.mField3 = v; });
}

uint32_t cTSLotInfo::GetMappedValue(int16_t id)
{
    if (id == 0) return 100;
    const auto it = mValues.find(id);
    if (it == mValues.end()) return 100;
    return uint16_t(it->second);
}

// 0x4cb630: the map's operator[] (0x485ec0 in the game), then a standard message (RZMalloc 0x1c03b7, constructor 0x1c033d;
// counted while it is used) posted to the cached message server.
void cTSLotInfo::SetMappedValue(int16_t id, int16_t value)
{
    mValues[id] = value;
    gscmsg::MessageRef message;
    message.SetId(kValueChangedMessage);
    VirtualCall<void(__thiscall *)(void *, uint32_t), 11>(message.mMessage, 7);
    VirtualCall<void(__thiscall *)(void *, int32_t), 16>(message.mMessage, int32_t(id));
    void *server = GetCachedMessageServer();
    VirtualCall<void(__thiscall *)(void *, void *, uint32_t), 4>(server, message.mMessage, 0);
}

cITSLotInfo *cTSLotInfo::GetDeferredLotInfo()
{
    void *singleton = GetCachedSingleton();
    void *lots = reinterpret_cast<void *>(Slot0(singleton, 6));
    if (!lots) return nullptr;
    return VirtualCall<cITSLotInfo *(__thiscall *)(void *, int32_t), 57>(lots, mLotId);
}

uint8_t cTSLotInfo::HasLotResources()
{
    void *persist = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    struct Block {
        void *mBegin = nullptr, *mEnd = nullptr, *mCapacity = nullptr;
    } found;
    void *name = GetName();
    // cITSPersistSystem slot 11 takes one argument (ret 4) and slot 36 three (ret 0xc); the original pushes
    // slot 36's last argument before the slot 11 call, so the stack only balances with these arities.
    const uint32_t id = VirtualCall<uint32_t(__thiscall *)(void *, void *), 11>(persist, name);
    using List = uint32_t(__thiscall *)(void *, void *, uint32_t, uint32_t);
    const bool any = VirtualCall<List, 36>(persist, &found, id, kLotResourceKind) != 0;
    if (found.mBegin) FreeThroughAllocatorService(found.mBegin, 0);
    return any;
}

uint8_t cTSLotInfo::QueryLotResource(uint32_t instance, uint32_t group, uint32_t interfaceId, void **out)
{
    void *persist = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    void *resource = nullptr;
    uint32_t key[4] = {kLotResourceType, group, instance | 0x8000, 0};
    uint8_t answer = 0;
    using Find = uint8_t(__thiscall *)(void *, uint32_t *, uint32_t, void **, uint32_t, uint32_t);
    if (VirtualCall<Find, 5>(persist, key, kResourceInterface, &resource, 0, 0))
        answer = VirtualCall<uint8_t(__thiscall *)(void *, uint32_t, void **), 0>(resource, interfaceId, out);
    if (resource) ReleaseObject(resource);
    return answer;
}

namespace {
// Finds the lot resource {0x53545223, group, instance | 0x8000, 0} (slots 30 / 31); the found object, or null.
uint8_t FindLotResource(uint32_t instance, uint32_t group, void **resource)
{
    void *persist = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    uint32_t key[4] = {kLotResourceType, group, instance | 0x8000, 0};
    using Find = uint8_t(__thiscall *)(void *, uint32_t *, uint32_t, void **, uint32_t, uint32_t);
    return VirtualCall<Find, 5>(persist, key, kResourceInterface, resource, 0, 0);
}

// A copy of the text the resource's slot 6 hands out for the index (length through slot 7, then the characters through
// slot 6 of the text).
void CopyResourceText(cRZString &copy, void *resource, uint32_t index)
{
    void *text = VirtualCall<void *(__thiscall *)(void *, uint32_t, uint32_t), 6>(resource, index, 0xffffffffu);
    const uint32_t length = Slot0(text, 7);
    const char *data = reinterpret_cast<const char *>(Slot0(text, 6));
    copy.Construct();
    copy.mString.assign(data, length);
}
} // namespace

uint8_t cTSLotInfo::LoadNamesFromResource(uint32_t instance, uint32_t group)
{
    uint8_t loaded = 0;
    void *resource = nullptr;
    if (FindLotResource(instance, group, &resource)) {
        cRZString name, description;
        CopyResourceText(name, resource, 1);
        CopyResourceText(description, resource, 2);
        if (name.Strlen()) VirtualCall<void(__thiscall *)(void *, void *), base::kSetName>(mBase, &name);
        if (description.Strlen()) VirtualCall<void(__thiscall *)(void *, void *), 20>(mBase, &description);
        loaded = 1;
    }
    if (resource) ReleaseObject(resource);
    return loaded;
}

uint8_t cTSLotInfo::StoreNamesInResource(uint32_t instance, uint32_t group)
{
    void *name = reinterpret_cast<void *>(Slot0(mBase, 19));
    void *description = reinterpret_cast<void *>(Slot0(mBase, 21));
    void *persist = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    void *resource = nullptr;
    uint32_t key[4] = {kLotResourceType, group, instance | 0x8000, 0};
    using Find = uint8_t(__thiscall *)(void *, uint32_t *, uint32_t, void **, uint32_t, uint32_t);
    if (!VirtualCall<Find, 5>(persist, key, kResourceInterface, &resource, 0, 0)) {
        void *framework = GetDirectorFrameworkObject();
        if (resource) {
            void *old = resource;
            resource = nullptr;
            ReleaseObject(old);
        }
        VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t, void **), 3>(framework, 0xdb4cb0f9, kResourceInterface, &resource);
        void *created = reinterpret_cast<void *>(Slot0(resource, 3));
        VirtualCall<void(__thiscall *)(void *, uint32_t *), 4>(created, key);
    }
    using SetText = void(__thiscall *)(void *, void *, uint32_t, int32_t);
    VirtualCall<SetText, 10>(resource, name, 1, -1);
    VirtualCall<SetText, 10>(resource, description, 2, -1);
    void *languages = tokens::GetSecondLanguageService();
    if (Slot0(languages, 7) != 1) {
        VirtualCall<SetText, 10>(resource, name, 1, 0);
        VirtualCall<SetText, 10>(resource, description, 2, 0);
    }
    const uint8_t stored = Slot0(resource, 21) == 0;
    if (resource) ReleaseObject(resource);
    return stored;
}

uint8_t cTSLotInfo::GetOrCreateExtraInfo(void **out, uint8_t create)
{
    void **slot = &mField84;
    void *finder = nullptr; // counted, the object that finds / stores the extra info
    bool built = false;
    uint32_t key[4] = {0, 0, 0, 0};
    uint8_t result;
    bool have = *slot != nullptr;
    if (!have) {
        void *persist = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
        void *lotName = GetName();
        const uint32_t resolved = VirtualCall<uint32_t(__thiscall *)(void *, void *), 11>(persist, lotName);
        key[0] = kExtraInfoKind;
        key[1] = resolved;
        key[2] = 1;
        key[3] = 2;
        void *current = GetCachedSlot23();
        if (current) AddRefObject(current);
        if (current) {
            finder = VirtualCall<void *(__thiscall *)(void *, uint32_t), 34>(current, kExtraInfoFinderId);
            if (finder) {
                AddRefObject(finder);
                if (*slot) {
                    void *old = *slot;
                    *slot = nullptr;
                    ReleaseObject(old);
                }
                VirtualCall<void(__thiscall *)(void *, uint32_t *, uint32_t, void **), 3>(finder, key, kExtraInfoInterface, slot);
                if (*slot) {
                    have = true;
                } else if (create) {
                    void *framework = GetDirectorFrameworkObject();
                    reinterpret_cast<RZAutoRef *>(slot)->Reset();
                    VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t, void **), 3>(framework, kExtraInfoKind, kExtraInfoInterface, slot);
                    if (*slot) {
                        cRZString title;
                        title.Construct();
                        void *name = GetBaseName();
                        const uint32_t text = Slot0(name, 5);
                        title.Sprintf(GameAddress<const char>(kExtraInfoTitleFormat), text);
                        VirtualCall<void(__thiscall *)(void *, void *), 13>(*slot, &title);
                        built = true;
                        have = true;
                    }
                }
            }
        }
        if (current) ReleaseObject(current);
    }
    if (!have) {
        result = 0;
    } else {
        void *info = *slot;
        int32_t level = 1;
        const uint32_t configured = Slot0(info, 19);
        OriginalFunction<uint8_t(__cdecl *)(uint32_t, int32_t *), 0x55d500>()(configured, &level);
        VirtualCall<void(__thiscall *)(void *, int32_t, uint32_t), 18>(info, level, 0);
        if (built) {
            if (Slot0(info, 17) == 1) {
                VirtualCall<void(__thiscall *)(void *, int32_t, uint32_t), 22>(info, 1, 0);
            } else {
                int32_t value = 5;
                // The slot takes no arguments: the original leaves the pointer it pushed first on the stack for the helper.
                const uint32_t fetched = Slot0(info, 17);
                OriginalFunction<uint8_t(__cdecl *)(uint32_t, int32_t *), 0x55d4d0>()(fetched, &value);
                VirtualCall<void(__thiscall *)(void *, int32_t, uint32_t), 22>(info, int16_t(value), 0);
            }
            VirtualCall<void(__thiscall *)(void *, uint32_t *), 4>(info, key);
            VirtualCall<void(__thiscall *)(void *, void *, uint32_t, uint32_t, int32_t), 4>(finder, *slot, 0, 0, -1);
        }
        *out = *slot;
        AddRefObject(*slot);
        result = 1;
    }
    if (finder) ReleaseObject(finder);
    return result;
}

namespace {
// One virtual call, __thiscall, with the arguments as the slot takes them.
template <class R = uint32_t, class... Args> R CallSlot(void *object, int slot, Args... args)
{
    return VirtualSlot<R(__thiscall *)(void *, Args...)>(object, slot)(object, args...);
}
uint32_t W(const void *pointer) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer)); }
void ReleaseIf(void *object)
{
    if (object) ReleaseObject(object);
}
} // namespace

uint8_t cTSLotInfo::CreateNewLotInfo(void **out, uint32_t flag)
{
    void *app = GetCachedSingleton();
    void *persist = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    Slot0(app, 14);
    void *lots = reinterpret_cast<void *>(Slot0(app, 6));
    void *current = reinterpret_cast<void *>(Slot0(app, 23));
    if (!lots || !current) return 0;
    void *framework = GetDirectorFrameworkObject();
    CallSlot<void>(framework, 3, cTSLotInfo::kClassId, kInterfaceId, out);
    const uint32_t lotName = W(GetName());
    const uint32_t nameKey = CallSlot(persist, 11, lotName);

    cRZString heading, notice, listing;
    heading.Construct();
    notice.Construct();
    notice.mString.assign(GameAddress<const char>(kNoticeText), kNoticeLength);
    const uint32_t kind = Slot0(lots, 68);
    const uint32_t zone = Slot0(current, 6);
    CallSlot<void>(lots, 69, W(&heading), kind, zone);
    CallSlot<void>(*out, 17, kind);
    CallSlot<void>(*out, 6, W(&heading));
    listing.Construct();
    appinit::CommonImplSlot4(kListingQuery, W(&listing), 1, 0, 0xffffffffu);

    void *found = nullptr;
    CallSlot<void>(persist, 22, 0x3b2271bb, W(&found), 1u, W(&listing), W(&heading), 1u, 1u);
    ServiceRefVector shown = {};
    void *first = reinterpret_cast<void *>(CallSlot(found, 3));
    if (first) AddRefObject(first);
    shown.PushBack(first);
    ReleaseIf(first);
    CallSlot<void>(persist, 49, W(&shown));
    const uint32_t lotId = CallSlot(persist, 11, W(&heading));
    void *record = reinterpret_cast<void *>(CallSlot(found, 3));
    uint8_t result = 0;
    bool failed = false;
    if (record) {
        GetCachedSingleton();
        void *scan = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
        ServiceBlockVector blocks = {};
        CallSlot<void>(record, 19, W(&blocks), 0u);
        const uint32_t count = static_cast<uint32_t>((static_cast<char *>(blocks.mLast) - static_cast<char *>(blocks.mFirst)) >> 4);
        for (uint32_t i = 0; i < count; ++i)
            CallSlot<void>(record, 26, W(static_cast<char *>(blocks.mFirst) + i * 16));
        void *list = nullptr;
        framework = GetDirectorFrameworkObject();
        if (!CallSlot<uint8_t>(framework, 3, 0xedbf32a7, 0xdbf32ca, W(&list))) {
            ReleaseIf(list);
            blocks.FreeBlock();
            failed = true;
        } else {
            CallSlot<void>(list, 4, nameKey, lotId);
            CallSlot<void>(scan, 52);
            ServiceRefVector others = {};
            CallSlot<void>(scan, 48, nameKey, W(&others));
            for (void **it = others.mFirst, **end = others.mLast; it != end; ++it) {
                const uint32_t other = W(*it);
                const uint32_t second = CallSlot(list, 3, 0u);
                CallSlot<void>(scan, 38, W(record), other, second);
            }
            CallSlot<void>(scan, 53);
            others.ReleaseAndFree();
            ReleaseIf(list);
            blocks.FreeBlock();
        }
    }
    if (!failed) {
        CallSlot<void>(persist, 50, W(&shown), 1u);
        void *holder = nullptr;   // the object that reads the lot's file
        void *settings = nullptr; // the object the new lot info gets attached
        framework = GetDirectorFrameworkObject();
        if (CallSlot<uint8_t>(framework, 3, 0xb54e37d, 0xeb54e473, W(&holder)) && CallSlot(holder, 6, W(&heading), 0u) == 0) {
            void *loaded = nullptr;
            if (CallSlot<uint8_t>(persist, 69, W(holder), 0x6c589723, 0u, 0x51b11bd, W(&loaded), 0u)) {
                framework = GetDirectorFrameworkObject();
                if (CallSlot<uint8_t>(framework, 3, 0x6c589723, 0x6c58971c, W(&settings))) {
                    void *target = reinterpret_cast<void *>(CallSlot(settings, 37));
                    CallSlot<void>(target, 4, W(loaded));
                }
            }
            ReleaseIf(loaded);
        }
        int32_t x = 0, y = 0;
        GetOrigin(&x, &y);
        CallSlot<void>(*out, 9, x, y);
        // The slot takes a byte. The original pushes the whole register the byte getter returned in (its upper bytes are then
        // the getter's code address); ours pushes the byte zero-extended.
        const uint32_t rotation = GetRotation();
        CallSlot<void>(*out, 21, rotation);
        CallSlot<void>(*out, 3, W(settings));
        CallSlot<void>(settings, 23, 1u);
        CallSlot<void>(settings, 17, flag);
        CallSlot<void>(settings, 20, W(&notice));
        void *owner = GetBaseLotInfo();
        const uint32_t ownerWord = Slot0(owner, 10);
        CallSlot<void>(settings, 9, ownerWord);
        CallSlot<void>(settings, 22, 0x10u);
        void *extra = nullptr;
        framework = GetDirectorFrameworkObject();
        CallSlot<uint8_t>(framework, 3, 0xcdcbe0e3, 0xcdcbe0dd, W(&extra));
        if (extra) {
            CallSlot<void>(extra, 3);              // cITSSaveGameInfo::Fill
            CallSlot<void>(extra, 10, lotId, 1u);  // cITSSaveGameInfo::Save(group, instance) (bool result unused)
        }
        CallSlot<void>(lots, 61, W(*out));
        result = 1;
        ReleaseIf(extra);
        ReleaseIf(settings);
        ReleaseIf(holder);
    }
    shown.ReleaseAndFree();
    ReleaseIf(found);
    return result;
}

// ---- the cIGZSerializable face ----

uint32_t cTSLotInfo::GetGZCLSID() { return kClassId; }

bool cTSLotInfo::Write(ifaces::cIGZOStream *stream)
{
    void *out = stream;
    Put(out, stream::kUint16, kVersion);
    void *baseRecord = reinterpret_cast<void *>(Slot0(mBase, base::kSerializable));
    VirtualCall<uint8_t(__thiscall *)(void *, void *), 3>(baseRecord, out);
    Put(out, stream::kUint32, uint32_t(mOriginX));
    Put(out, stream::kUint32, uint32_t(mOriginY));
    PutFloat(out, mElevation);
    Put(out, stream::kSint32, uint32_t(mField44));
    Put(out, stream::kUint8, mRotation);
    VirtualCall<uint8_t(__thiscall *)(void *, void *), stream::kString>(out, &mDescription);
    Put(out, stream::kUint8, 0);
    Put(out, stream::kSint32, uint32_t(mField4C));
    Put(out, stream::kSint32, uint32_t(mLotId));
    Put(out, stream::kBool, mByteA0);
    Put(out, stream::kSint32, uint32_t(mFieldA4));
    Put(out, stream::kSint32, uint32_t(mFieldA8));
    const int32_t count = int32_t(mConnections.size());
    Put(out, stream::kSint32, uint32_t(count));
    for (int32_t i = 0; i < count; ++i) {
        const LotInfoConnection &c = mConnections[i];
        Put(out, stream::kSint32, uint32_t(c.mField0));
        Put(out, stream::kSint32, uint32_t(c.mField1));
        Put(out, stream::kSint32, uint32_t(c.mField2));
        Put(out, stream::kSint32, uint32_t(c.mField3));
    }
    Put(out, stream::kSint32, uint32_t(mValues.size()));
    for (const auto &entry : mValues) {
        Put(out, stream::kSint16, uint16_t(entry.first));
        Put(out, stream::kSint16, uint16_t(entry.second));
    }
    return true;
}

bool cTSLotInfo::Read(ifaces::cIGZIStream *stream)
{
    void *in = stream;
    uint16_t version = 0;
    bool result = true; // the base record's answer when it is read (its byte as it is: declared bool, not normalised)
    Get(in, stream::kUint16, &version);
    if (version > 6) {
        void *baseRecord = reinterpret_cast<void *>(Slot0(mBase, base::kSerializable));
        result = VirtualCall<bool(__thiscall *)(void *, void *), 4>(baseRecord, in);
        if (!result) return false;
        if (version < 10) {
            // Old records carried the base's name here.
            cRZString name;
            name.Construct();
            Get(in, stream::kString, &name);
            VirtualSlot<void(__thiscall *)(void *, void *)>(mBase, base::kSetName)(mBase, &name);
        }
        Get(in, stream::kUint32, &mOriginX);
        Get(in, stream::kUint32, &mOriginY);
        Get(in, stream::kFloat, &mElevation);
        if (version < 0xc) {
            uint32_t word = 0; // a stack word of the original; the test streams always deliver it
            Get(in, stream::kUint32, &word);
            Slot1(mBase, base::kSetCount, word);
        }
        Get(in, stream::kSint32, &mField44);
        if (version < 8)
            mRotation = Byte0(mBase, base::kGetRotation);
        else
            Get(in, stream::kUint8, &mRotation);
        if (version < 0xb)
            mDescription.mString.clear();
        else
            Get(in, stream::kString, &mDescription);
    }
    if (version > 0xc) {
        uint8_t unused = 0;
        Get(in, stream::kUint8, &unused);
    }
    if (version > 0xd) Get(in, stream::kSint32, &mField4C);
    if (version > 0xe) {
        mConnections.clear();
        Get(in, stream::kSint32, &mLotId);
        if (version > 0xf) {
            Get(in, stream::kBool, &mByteA0);
            Get(in, stream::kSint32, &mFieldA4);
            Get(in, stream::kSint32, &mFieldA8);
        }
        int32_t count = 0;
        Get(in, stream::kSint32, &count);
        int32_t read = 0;
        for (; read < count; ++read) {
            LotInfoConnection c{};
            Get(in, stream::kSint32, &c.mField0);
            Get(in, stream::kSint32, &c.mField1);
            if (version < 0x10) c.mField2 = 0;
            else Get(in, stream::kSint32, &c.mField2);
            if (version < 0x11) c.mField3 = 0;
            else Get(in, stream::kSint32, &c.mField3);
            mConnections.push_back(c);
        }
        if (version > 0x11) {
            mValues.clear();
            int32_t entries = 0;
            Get(in, stream::kSint32, &entries);
            // ORIGINAL-BUG: the original reads the id into its stream argument's stack slot and the value into the word that counted the
            // connections, and keeps both across entries; a failed read leaves what they held. Kept.
            uint32_t idWord = uint32_t(reinterpret_cast<uintptr_t>(in));
            uint32_t valueWord = uint32_t(read);
            for (int32_t i = 0; i < entries; ++i) {
                Get(in, stream::kSint16, &idWord);
                Get(in, stream::kSint16, &valueWord);
                mValues[int16_t(idWord)] = int16_t(valueWord);
            }
        }
    }
    return result;
}

// ---- the cIGZPersistResource face ----

cGZPersistResourceKey cTSLotInfo::GetKey() { return mKey; }

bool cTSLotInfo::SetKey(const cGZPersistResourceKey &key)
{
    mKey = key;
    return true;
}

uint32_t cTSLotInfo::GetType() { return mKey.type; }
uint32_t cTSLotInfo::GetGroup() { return mKey.group; }
uint32_t cTSLotInfo::GetInstance() { return mKey.instance; }

bool cTSLotInfo::SetType(uint32_t type)
{
    mKey.type = type;
    return true;
}

bool cTSLotInfo::SetGroup(uint32_t group)
{
    mKey.group = group;
    return true;
}

bool cTSLotInfo::SetInstance(uint32_t instance)
{
    mKey.instance = instance;
    return true;
}

uint32_t cTSLotInfo::GetRefCount() { return rz::cRZUnknown::RefCount(); }

// ---- the cIGZSelfDescribing face ----

bool cTSLotInfo::Describe(void *target)
{
    Slot1(target, 16, uint32_t(reinterpret_cast<uintptr_t>(GetBaseName())));
    return true;
}

bool cTSLotInfo::DescribeMore(void *) { return false; }

// ---- adapters (the RVA hooks) ----

using Adapters = cTSLotInfoAdapters;

Adapters::Self *__fastcall Adapters::Construct(Self *memory, void *) { return ::new (memory) Self; }

Adapters::Self *__fastcall Adapters::CopyConstruct(Self *memory, void *, const Self *other) { return ::new (memory) Self(*other); }

void __fastcall Adapters::Destruct(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->Self::~Self();
}

// 0x4c8ee0: the destructor and, with bit 0, the sized delete (the compiler's scalar deleting destructor, by hand).
Adapters::Self *__fastcall Adapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    [[msvc::forceinline_calls]] self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}

bool __fastcall Adapters::QueryInterface(Self *self, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::QueryInterface(riid, out);
}

// The +8 face's slots.
cITSLotInfo *__fastcall Adapters::Clone(cITSLotInfo *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::Clone();
}

void __fastcall Adapters::SetName(cITSLotInfo *face, void *, void *text)
{
    [[msvc::forceinline_calls]] static_cast<Self *>(face)->Self::SetName(text);
}

void __fastcall Adapters::SetNameText(cITSLotInfo *face, void *, const char *text)
{
    [[msvc::forceinline_calls]] static_cast<Self *>(face)->Self::SetNameText(text);
}

void __fastcall Adapters::SetOrigin(cITSLotInfo *face, void *, int32_t x, int32_t y)
{
    [[msvc::forceinline_calls]] static_cast<Self *>(face)->Self::SetOrigin(x, y);
}

void __fastcall Adapters::GetSize(cITSLotInfo *face, void *, int32_t *width, int32_t *depth)
{
    [[msvc::forceinline_calls]] static_cast<Self *>(face)->Self::GetSize(width, depth);
}

void __fastcall Adapters::GetBounds(cITSLotInfo *face, void *, int32_t *rect)
{
    [[msvc::forceinline_calls]] static_cast<Self *>(face)->Self::GetBounds(rect);
}

void __fastcall Adapters::GetInnerBounds(cITSLotInfo *face, void *, int32_t *rect)
{
    [[msvc::forceinline_calls]] static_cast<Self *>(face)->Self::GetInnerBounds(rect);
}

void __fastcall Adapters::GetEdgeStrip(cITSLotInfo *face, void *, int32_t *rect)
{
    [[msvc::forceinline_calls]] static_cast<Self *>(face)->Self::GetEdgeStrip(rect);
}

void __fastcall Adapters::SetElevation(cITSLotInfo *face, void *, float value)
{
    [[msvc::forceinline_calls]] static_cast<Self *>(face)->Self::SetElevation(value);
}

float __fastcall Adapters::GetElevation(cITSLotInfo *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetElevation();
}

void __fastcall Adapters::SetField44(cITSLotInfo *face, void *, int32_t value)
{
    [[msvc::forceinline_calls]] static_cast<Self *>(face)->Self::SetField44(value);
}

void *__fastcall Adapters::GetBaseName(cITSLotInfo *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetBaseName();
}

void *__fastcall Adapters::GetEffectiveBaseName(cITSLotInfo *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetEffectiveBaseName();
}

void __fastcall Adapters::SetRotation(cITSLotInfo *face, void *, uint8_t value)
{
    [[msvc::forceinline_calls]] static_cast<Self *>(face)->Self::SetRotation(value);
}

uint8_t __fastcall Adapters::GetRotation(cITSLotInfo *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetRotation();
}

uint8_t __fastcall Adapters::GetRelativeRotation(cITSLotInfo *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetRelativeRotation();
}

uint8_t __fastcall Adapters::HasLotResources(cITSLotInfo *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::HasLotResources();
}

void __fastcall Adapters::SetDescription(cITSLotInfo *face, void *, void *text)
{
    [[msvc::forceinline_calls]] static_cast<Self *>(face)->Self::SetDescription(text);
}

uint8_t __fastcall Adapters::StoreNamesInResource(cITSLotInfo *face, void *, uint32_t instance, uint32_t group)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::StoreNamesInResource(instance, group);
}

uint8_t __fastcall Adapters::LoadNamesFromResource(cITSLotInfo *face, void *, uint32_t instance, uint32_t group)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::LoadNamesFromResource(instance, group);
}

uint8_t __fastcall Adapters::QueryLotResource(cITSLotInfo *face, void *, uint32_t instance, uint32_t group, uint32_t interfaceId, void **out)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::QueryLotResource(instance, group, interfaceId, out);
}

uint32_t __fastcall Adapters::GetLotType(cITSLotInfo *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetLotType();
}

void __fastcall Adapters::SetField4C(cITSLotInfo *face, void *, int32_t value)
{
    [[msvc::forceinline_calls]] static_cast<Self *>(face)->Self::SetField4C(value);
}

uint8_t __fastcall Adapters::GetOrCreateExtraInfo(cITSLotInfo *face, void *, void **out, uint8_t create)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetOrCreateExtraInfo(out, create);
}

void __fastcall Adapters::SetLotId(cITSLotInfo *face, void *, int32_t value)
{
    [[msvc::forceinline_calls]] static_cast<Self *>(face)->Self::SetLotId(value);
}

uint8_t __fastcall Adapters::AddConnection(cITSLotInfo *face, void *, int32_t a, int32_t b)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::AddConnection(a, b);
}

uint8_t __fastcall Adapters::RemoveConnectionBySecond(cITSLotInfo *face, void *, int32_t value)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::RemoveConnectionBySecond(value);
}

uint8_t __fastcall Adapters::RemoveConnectionByFirst(cITSLotInfo *face, void *, int32_t value)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::RemoveConnectionByFirst(value);
}

uint8_t __fastcall Adapters::CreateNewLotInfo(cITSLotInfo *face, void *, void **out, uint32_t flag)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::CreateNewLotInfo(out, flag);
}

int32_t __fastcall Adapters::GetConnectionCount(cITSLotInfo *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetConnectionCount();
}

int32_t __fastcall Adapters::GetConnectionField0(cITSLotInfo *face, void *, int32_t index)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetConnectionField0(index);
}

int32_t __fastcall Adapters::GetConnectionField1(cITSLotInfo *face, void *, int32_t index)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetConnectionField1(index);
}

int32_t __fastcall Adapters::GetConnectionField2(cITSLotInfo *face, void *, int32_t index)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetConnectionField2(index);
}

int32_t __fastcall Adapters::GetConnectionField3(cITSLotInfo *face, void *, int32_t index)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetConnectionField3(index);
}

uint8_t __fastcall Adapters::GetCappedBaseByte(cITSLotInfo *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetCappedBaseByte();
}

void __fastcall Adapters::SetBaseByte88(cITSLotInfo *face, void *, uint8_t value)
{
    [[msvc::forceinline_calls]] static_cast<Self *>(face)->Self::SetBaseByte88(value);
}

uint8_t __fastcall Adapters::GetBaseByte88(cITSLotInfo *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetBaseByte88();
}

void __fastcall Adapters::SetBaseWord90(cITSLotInfo *face, void *, uint32_t value)
{
    [[msvc::forceinline_calls]] static_cast<Self *>(face)->Self::SetBaseWord90(value);
}

uint32_t __fastcall Adapters::GetBaseWord90(cITSLotInfo *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetBaseWord90();
}

void __fastcall Adapters::SetBaseWord8C(cITSLotInfo *face, void *, uint32_t value)
{
    [[msvc::forceinline_calls]] static_cast<Self *>(face)->Self::SetBaseWord8C(value);
}

uint8_t __fastcall Adapters::SetConnectionField2(cITSLotInfo *face, void *, int16_t key, int16_t value)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::SetConnectionField2(key, value);
}

uint8_t __fastcall Adapters::SetConnectionField3(cITSLotInfo *face, void *, int16_t key, int16_t value)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::SetConnectionField3(key, value);
}

uint32_t __fastcall Adapters::GetMappedValue(cITSLotInfo *face, void *, int16_t id)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetMappedValue(id);
}

void __fastcall Adapters::SetMappedValue(cITSLotInfo *face, void *, int16_t id, int16_t value)
{
    [[msvc::forceinline_calls]] static_cast<Self *>(face)->Self::SetMappedValue(id, value);
}

cITSLotInfo *__fastcall Adapters::GetDeferredLotInfo(cITSLotInfo *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetDeferredLotInfo();
}

bool __fastcall Adapters::Write(cIGZSerializable *face, void *, ifaces::cIGZOStream *stream)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::Write(stream);
}

bool __fastcall Adapters::Read(cIGZSerializable *face, void *, ifaces::cIGZIStream *stream)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::Read(stream);
}

uint32_t __fastcall Adapters::GetGZCLSID(cIGZSerializable *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetGZCLSID();
}

bool __fastcall Adapters::Describe(cIGZSelfDescribing *face, void *, void *target)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::Describe(target);
}
