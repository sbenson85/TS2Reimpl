#include "ts/cTSNeighborhood.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/ComFramework.h"
#include "ifaces/NeighborhoodDelegate.h"
#include "ifaces/NeighborhoodInStream.h"
#include "ifaces/NeighborhoodLot.h"
#include "ifaces/NeighborhoodLotKind.h"
#include "ifaces/NeighborhoodMember.h"
#include "ifaces/NeighborhoodOutStream.h"
#include "ifaces/NeighborhoodOwnerFace.h"
#include "misc/UtilitiesC.h"
#include "rz/MiscHelpersB.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"
#include "simantics/ServiceRefVector.h"

#include <algorithm>
#include <cstring>
#include <new>

static_assert(sizeof(NeighborhoodLotMap::iterator) == 4);

namespace {
constexpr uint32_t kUnknownInterface = 1;
constexpr uint32_t kSerializableInterface = 0xe4fda3d4;
constexpr uint32_t kOwnInterface = 0x7aa51c46;
constexpr uint32_t kPersistResourceInterface = 0x456b8f1d; // answered with the face at +4
constexpr uint32_t kContainerInterface = 0x8db5bf48;       // answered with the face at +8
constexpr uint32_t kLotKindSkipped = 0x10;                 // lots whose kind object is of this type are not searched

// The data the constructor copies for the default name: 8 bytes and 1 byte in the game's read-only data.
constexpr uint32_t kDefaultNameWord = 0xdca1fc;
constexpr uint32_t kDefaultNameByte = 0xdca204;
} // namespace

// The game's globals read by the constant slots (RVAs of 1.18.0.187).
namespace {
constexpr uint32_t kGlobalFloatA = 0x10daa38;
constexpr uint32_t kGlobalWord = 0x10daa3c;
constexpr uint32_t kGlobalFloatB = 0x10daa40;
constexpr uint32_t kTenRva = 0xda8658;

// A counted pointer held by a local of the original: released when it goes out of scope.
struct Held {
    void *object = nullptr;
    Held() = default;
    Held(const Held &) = delete;
    Held &operator=(const Held &) = delete;
    ~Held() noexcept(false) { if (object) As<cIGZUnknown>(object)->Release(); }
};

void AddRefObject(void *object) { if (object) As<cIGZUnknown>(object)->AddRef(); }
void ReleaseObject(void *object) { if (object) As<cIGZUnknown>(object)->Release(); }

// Replaces a counted pointer slot: counts the new object, stores it, releases the old one (the order of the original).
void Replace(void *&slot, void *object)
{
    void *old = slot;
    if (object == old) return;
    AddRefObject(object);
    slot = object;
    ReleaseObject(old);
}

// Takes a counted pointer out of a slot (the slot is cleared first), releasing it.
void Drop(void *&slot)
{
    if (void *old = slot) {
        slot = nullptr;
        ReleaseObject(old);
    }
}

// The object the stream is handed to remember the version of the record it reads: 0x9c7b16 builds the same 0x14 bytes as
// UtilObjectB::Construct (0x9c7b3d) except that the word at +0xe is 7.
void *NewReadMetadata(uint32_t version)
{
    auto *metadata = static_cast<UtilObjectB *>(::operator new(sizeof(UtilObjectB)));
    if (!metadata) return nullptr;
    metadata->Construct(version);
    metadata->mWordE = 7;
    return metadata;
}

// Reads one helper map: its previous contents go, then a count and that many (id, object) records; each object is made by
// the framework, given its kind and id, reads itself and replaces what the map held under the id's low 16 bits.
void ReadMemberMap(NeighborhoodMemberMap &map, ifaces::NeighborhoodInStream *in, void *stream, uint32_t kind)
{
    map.clear();
    int32_t count = 0;
    in->GetSint32(&count);
    for (int32_t i = 0; i < count; ++i) {
        uint32_t id = 0;
        in->GetSint32(&id);
        Held member;
        auto *framework = As<ifaces::ComFramework>(GetDirectorFrameworkObject());
        if (framework->GetClassObject(cTSNeighborhood::kHelperClass, cTSNeighborhood::kHelperInterface, &member.object)) {
            As<ifaces::NeighborhoodMember>(member.object)->SetIdentity(kind, id);
            As<ifaces::NeighborhoodMember>(member.object)->Read(stream);
            map[static_cast<int16_t>(id & 0xffff)].Assign(member.object);
        }
    }
}

void WriteMemberMap(NeighborhoodMemberMap &map, ifaces::NeighborhoodOutStream *out, void *stream)
{
    out->SetSint32(static_cast<uint32_t>(map.size()));
    for (auto &entry : map) {
        out->SetSint32(static_cast<uint32_t>(static_cast<int32_t>(entry.first)));
        As<ifaces::NeighborhoodMember>(entry.second.Get())->Write(stream);
    }
}

// The bounds of a lot: left, top, right, bottom.
struct Bounds { int32_t left, top, right, bottom; };

// Whether the lot takes part in the searches: valid, and its kind object is not of type 0x10. Fills the bounds when it does.
// The lot's own record (non-null for a lot that takes part) is handed back through *record.
bool SearchableLot(void *lot, Bounds *bounds, void **record = nullptr)
{
    auto *object = As<ifaces::NeighborhoodLot>(lot);
    void *value = object->GetLotObject();
    if (!value) return false;
    if (record) *record = value;
    if (object->GetKind()->IsKind(kLotKindSkipped)) return false;
    object->GetBounds(&bounds->left);
    return true;
}

// The overlap test of slots 59 / 60 against the rectangle {left, top, right, bottom}: the bounds must pass the plain
// containment tests first, then the clipped rectangle must have a positive size (a difference that wraps counts as
// negative, as in the original's signed arithmetic).
bool OverlapsPositively(const Bounds &lot, const int32_t *rect)
{
    if (!(lot.left <= rect[2] && rect[0] <= lot.right && lot.top <= rect[3] && rect[1] <= lot.bottom)) return false;
    const int32_t clippedLeft = std::max(lot.left, rect[0]);
    const int32_t clippedTop = std::max(lot.top, rect[1]);
    const int32_t clippedRight = std::min(lot.right, rect[2]);
    const int32_t clippedBottom = std::min(lot.bottom, rect[3]);
    const auto positive = [](int32_t high, int32_t low) {
        const int32_t difference = static_cast<int32_t>(static_cast<uint32_t>(high) - static_cast<uint32_t>(low));
        return high != low && -1 < difference;
    };
    return positive(clippedRight, clippedLeft) && positive(clippedBottom, clippedTop);
}
} // namespace

NeighborhoodTextEntry::NeighborhoodTextEntry(const cIGZString &name, void *held)
    : mText{GameAddress<void>(cRZString::kVTable), std::string(), 0}, mHeld(held)
{
    mText.Construct(name);
    AddRefObject(mHeld);
}

NeighborhoodTextEntry::NeighborhoodTextEntry(const NeighborhoodTextEntry &other) : mText(other.mText), mHeld(other.mHeld)
{
    AddRefObject(mHeld);
}

NeighborhoodTextEntry::~NeighborhoodTextEntry() noexcept(false)
{
    ReleaseObject(mHeld);
    mText.Destruct();
}

cGZPersistResourceKey cTSNeighborhood::GetKey()
{
    cGZPersistResourceKey key;
    std::memcpy(&key, mKey, sizeof(mKey));
    return key;
}
bool cTSNeighborhood::SetKey(const cGZPersistResourceKey &key)
{
    std::memcpy(mKey, &key, sizeof(mKey));
    return true;
}
bool cTSNeighborhood::SetInstance(uint32_t instance)
{
    mKey[2] = instance;
    return true;
}

cTSNeighborhood *cTSNeighborhood::Create() { return new (std::nothrow) cTSNeighborhood; }

cTSNeighborhood::cTSNeighborhood()
{
    mRefCount = 0;
    mWidth = 0x80;
    mHeight = 0x80;
    // The name: a cRZString holding the 9 characters stored in the game's data (the buffer is zeroed first).
    auto *name = reinterpret_cast<uint8_t *>(&mName);
    std::memset(name + 4, 0, 16);
    *reinterpret_cast<uint32_t *>(name + 0x14) = 9;
    *reinterpret_cast<uint32_t *>(name + 0x18) = 0xf;
    std::memcpy(name + 4, GameAddress<uint8_t>(kDefaultNameWord), 8);
    name[0xc] = *GameAddress<uint8_t>(kDefaultNameByte);
    name[0xd] = 0;
    *reinterpret_cast<const void **>(name) = GameAddress(cRZString::kVTable);
    *reinterpret_cast<uint32_t *>(name + 0x1c) = 0;
    new (&mLots) NeighborhoodLotMap();
    new (&mTexts) NeighborhoodTextSet();
    mField6C = 1;
    mLotCursor = mLots.end();
    mStateHelper = nullptr;
    new (&mMembers1) NeighborhoodMemberMap();
    new (&mMembers2) NeighborhoodMemberMap();
    new (&mMembers3) NeighborhoodMemberMap();
    mKey[0] = mKey[1] = mKey[2] = mKey[3] = 0;
    mFlags = 1;
    mField_A4 = nullptr;
    new (&mPlain) NeighborhoodPlainMap();
    mInitialized = 0;
    mFlagByte = 0;
    new (&mServiceIds) NeighborhoodIdSet();
    mDelegate = nullptr;
    mSecondOwner = nullptr;
    mOwner = nullptr;
    mByteC8 = 0;
    std::memset(mCode, 0, sizeof(mCode));
    mServiceIds.insert(0xa4);
    mServiceIds.insert(0xaf);
    mServiceIds.insert(0x9e);
}

cTSNeighborhood::~cTSNeighborhood()
{
    ReleaseObject(mOwner);
    ReleaseObject(mSecondOwner);
    ReleaseObject(mDelegate);
    mServiceIds.~NeighborhoodIdSet();
    mPlain.~NeighborhoodPlainMap();
    ReleaseObject(mField_A4);
    mMembers3.~NeighborhoodMemberMap();
    mMembers2.~NeighborhoodMemberMap();
    mMembers1.~NeighborhoodMemberMap();
    ReleaseObject(mStateHelper);
    mTexts.~NeighborhoodTextSet();
    mLots.~NeighborhoodLotMap();
    mName.Destruct();
}

bool cTSNeighborhood::QueryInterface(uint32_t id, void **out)
{
    void *face;
    switch (id) {
    case kOwnInterface:
    case kUnknownInterface:
    case kSerializableInterface: face = this; break;
    case kPersistResourceInterface: face = static_cast<cIGZPersistResource *>(this); break;
    case kContainerInterface: face = static_cast<cITSDataContainer *>(this); break;
    default: return false;
    }
    *out = face;
    AddRef();
    return true;
}

uint32_t cTSNeighborhood::Release()
{
    if (mRefCount > 1) return --mRefCount;
    delete this; // slot 76, flags 1
    return 0;
}

uint32_t cTSNeighborhood::GetGZCLSID() { return kClassId; }

void __fastcall cTSNeighborhood::EnsureStateHelper(cTSNeighborhood *self)
{
    if (!self->mStateHelper) {
        auto *framework = As<ifaces::ComFramework>(GetDirectorFrameworkObject());
        Drop(self->mStateHelper);
        if (framework->GetClassObject(kHelperClass, kHelperInterface, &self->mStateHelper))
            As<ifaces::NeighborhoodMember>(self->mStateHelper)->SetIdentity(0, 0);
        if (!self->mStateHelper) return;
    }
    uint32_t value;
    if (self->mDelegate) {
        auto *face = As<ifaces::NeighborhoodDelegate>(self->mDelegate)->GetOwnerFace();
        value = face->GetOwnerValue();
    } else {
        value = As<ifaces::NeighborhoodOwnerFace>(self->PersistFace())->GetOwnerValue();
    }
    if (value) As<ifaces::NeighborhoodMember>(self->mStateHelper)->SetOwnerValue(value);
}

bool cTSNeighborhood::Write(ifaces::cIGZOStream *stream)
{
    auto *out = As<ifaces::NeighborhoodOutStream>(stream);
    out->SetUint32(kClassId);
    out->SetUint32(kVersion);
    out->SetUint32(0);
    auto *metadata = static_cast<UtilObjectB *>(::operator new(sizeof(UtilObjectB)));
    if (metadata) metadata->Construct(kVersion);
    out->SetMetadata(metadata);
    out->SetUint32(mWidth);
    out->SetUint32(mHeight);
    out->SetString(&mName);
    for (uint16_t word : mCode) out->SetUint16(word);
    As<ifaces::NeighborhoodMember>(mStateHelper)->Write(stream);
    WriteMemberMap(mMembers1, out, stream);
    WriteMemberMap(mMembers2, out, stream);
    WriteMemberMap(mMembers3, out, stream);
    out->SetUint8(mFlagByte);
    out->SetUint32(mFlags);
    return out->GetError() == 0;
}

bool cTSNeighborhood::Read(ifaces::cIGZIStream *stream)
{
    auto *in = As<ifaces::NeighborhoodInStream>(stream);
    // The class id and the spare word go to a stack word of the original; a failing read leaves it alone (zero here).
    uint32_t spare = 0;
    int32_t version = 0;
    in->GetUint32(&spare);
    in->GetSint32(&version);
    in->GetUint32(&spare);
    in->SetMetadata(NewReadMetadata(static_cast<uint32_t>(version)));
    EnsureStateHelper(this);
    if (version > 0x6a) {
        in->GetUint32(&mWidth);
        in->GetUint32(&mHeight);
    }
    if (version > 0x6d) in->GetString(&mName);
    if (version < 0x14) {
        in->GetUint16(&mCode[0]);
    } else {
        for (uint16_t &word : mCode) in->GetUint16(&word);
    }
    if (version > 0x65) {
        // ORIGINAL-BUG: when the framework cannot create the state helper (EnsureStateHelper left it null), this call
        // goes through a null pointer and crashes. Reproduced.
        As<ifaces::NeighborhoodMember>(mStateHelper)->Read(stream);
        ReadMemberMap(mMembers1, in, stream, 1);
        ReadMemberMap(mMembers2, in, stream, 2);
        ReadMemberMap(mMembers3, in, stream, 3);
    }
    if (version > 0x6e) {
        // The original reads into a byte of its stack frame that holds heap-address bits when nothing wrote it before; a failed
        // read there would store that. It starts at zero here (the test streams always deliver the byte).
        uint8_t flag = 0;
        in->GetUint8(&flag);
        mFlagByte = flag != 0;
    }
    if (version < 0x70) mFlags = 0;
    else in->GetUint32(&mFlags);
    return in->GetError() == 0;
}

cIGZPersistResource *cTSNeighborhood::GetPersistResource() { return static_cast<cIGZPersistResource *>(this); }

uint8_t cTSNeighborhood::GetFlag0() { return mFlags & 1; }
void cTSNeighborhood::SetFlag0() { mFlags |= 1; }
void cTSNeighborhood::SetDelegate(void *object) { Replace(mDelegate, object); }
void cTSNeighborhood::SetSecondOwner(void *object) { Replace(mSecondOwner, object); }
uint16_t cTSNeighborhood::GetCodeWord(int32_t index) { return mCode[index]; }
void cTSNeighborhood::SetCodeWord(int32_t index, uint16_t value) { mCode[index] = value; }

void *cTSNeighborhood::GetStateHelperOrDelegated()
{
    if (mDelegate) return As<ifaces::NeighborhoodDelegate>(mDelegate)->GetStateHelper();
    return mStateHelper;
}

void cTSNeighborhood::ClearMemberMap1() { mMembers1.clear(); }
void cTSNeighborhood::ClearMemberMap2() { mMembers2.clear(); }
void cTSNeighborhood::ClearMemberMap3() { mMembers3.clear(); }

namespace {
template <class Map> void StoreIterator(void **out, typename Map::iterator it) { std::memcpy(out, &it, sizeof(void *)); }
} // namespace

void **cTSNeighborhood::MemberMap1Begin(void **out) { StoreIterator<NeighborhoodMemberMap>(out, mMembers1.begin()); return out; }
void **cTSNeighborhood::MemberMap1End(void **out) { StoreIterator<NeighborhoodMemberMap>(out, mMembers1.end()); return out; }
void **cTSNeighborhood::MemberMap2Begin(void **out)
{
    if (mDelegate) As<ifaces::NeighborhoodDelegate>(mDelegate)->MemberMap2Begin(out);
    else StoreIterator<NeighborhoodMemberMap>(out, mMembers2.begin());
    return out;
}
void **cTSNeighborhood::MemberMap2End(void **out)
{
    if (mDelegate) As<ifaces::NeighborhoodDelegate>(mDelegate)->MemberMap2End(out);
    else StoreIterator<NeighborhoodMemberMap>(out, mMembers2.end());
    return out;
}
void **cTSNeighborhood::MemberMap3Begin(void **out)
{
    if (mDelegate) As<ifaces::NeighborhoodDelegate>(mDelegate)->MemberMap3Begin(out);
    else StoreIterator<NeighborhoodMemberMap>(out, mMembers3.begin());
    return out;
}
void **cTSNeighborhood::MemberMap3End(void **out)
{
    if (mDelegate) As<ifaces::NeighborhoodDelegate>(mDelegate)->MemberMap3End(out);
    else StoreIterator<NeighborhoodMemberMap>(out, mMembers3.end());
    return out;
}

void cTSNeighborhood::SetSize(uint32_t width, uint32_t height)
{
    mWidth = width;
    mHeight = height;
}

void *cTSNeighborhood::RewindLots()
{
    mLotCursor = mLots.begin();
    return NextLot();
}

void *cTSNeighborhood::NextLot()
{
    if (mLotCursor == mLots.end()) return nullptr;
    void *lot = mLotCursor->second.Get();
    ++mLotCursor;
    return lot;
}

void *cTSNeighborhood::GetLot(int32_t number)
{
    // A lower bound on the (signed) number accepted when the node's key is not above it: an exact match; the head gives null.
    auto it = mLots.lower_bound(number);
    if (it != mLots.end() && it->first <= number) return it->second.Get();
    return nullptr;
}

void *cTSNeighborhood::FindLotAt(int32_t x, int32_t y)
{
    for (auto it = mLots.begin(); it != mLots.end(); ++it) {
        void *lot = it->second.Get();
        Bounds b;
        if (!SearchableLot(lot, &b)) continue;
        if (x >= b.left && y >= b.top && x <= b.right && y <= b.bottom) return lot;
    }
    return nullptr;
}

void *cTSNeighborhood::FindLotOverlapping(const int32_t *rect)
{
    for (auto it = mLots.begin(); it != mLots.end(); ++it) {
        void *lot = it->second.Get();
        Bounds b;
        if (SearchableLot(lot, &b) && OverlapsPositively(b, rect)) return lot;
    }
    return nullptr;
}

void cTSNeighborhood::CollectLotsOverlapping(const int32_t *rect, std::vector<void *> *out)
{
    out->clear();
    for (auto it = mLots.begin(); it != mLots.end(); ++it) {
        void *lot = it->second.Get();
        Bounds b;
        void *record = nullptr;
        // The collected values are the records the lots hand out (slot 18), not the lots.
        if (SearchableLot(lot, &b, &record) && OverlapsPositively(b, rect) && std::find(out->begin(), out->end(), record) == out->end())
            out->push_back(record);
    }
}

uint8_t cTSNeighborhood::GetFlagByte() { return mFlagByte; }
uint32_t cTSNeighborhood::GetGlobalWord() { return *GameAddress<uint32_t>(kGlobalWord); }
float cTSNeighborhood::GetGlobalFloatA() { return *GameAddress<float>(kGlobalFloatA); }
float cTSNeighborhood::GetGlobalFloatB() { return *GameAddress<float>(kGlobalFloatB); }
float cTSNeighborhood::GetTen() { return *GameAddress<float>(kTenRva); }

// ---- initialisation, persistence of the lots, counters and string tables ----

namespace {
// A virtual function declared with its real narrow result type: MSVC then loads the slot into a register for the call (VirtualCall
// declares narrow results as 32-bit to get `call [reg+off]` instead; the original's bool and byte results use the register form).
template <int N, class R, class... A> struct NarrowSlot : vtable_detail::Pad<N> {
    virtual R __thiscall call(A...) = 0;
};

// The object viewed as a class whose slot `N` has the narrow result type, for a call written out at the use site (so that the vtable
// pointer is read before the arguments are evaluated, as the original does).
template <int N, class R, class... A> NarrowSlot<N, R, A...> *Narrow(const void *object)
{
    return static_cast<NarrowSlot<N, R, A...> *>(const_cast<void *>(object));
}

// The cRZString printf member (slot 31, __cdecl, `this` first on the stack).
struct PrintfSlot : vtable_detail::Pad<31> {
    virtual void *__cdecl call(const char *format, ...) = 0;
};

template <class R> constexpr bool kNarrowResult = std::is_integral_v<R> && std::conjunction_v<std::bool_constant<(sizeof(std::conditional_t<std::is_void_v<R>, char, R>) < 4)>>;

// A call through slot `Slot` of the object's own vtable with stack arguments `A...`.
template <class R, int Slot, class... A> R CallSlot(const void *object, A... args)
{
    if constexpr (kNarrowResult<R>)
        return static_cast<NarrowSlot<Slot, R, A...> *>(const_cast<void *>(object))->call(args...);
    else
        return VirtualCall<R(__thiscall *)(void *, A...), Slot>(object, args...);
}

constexpr uint32_t kCounterRecordType = 0x0fdfb4f5;
constexpr uint32_t kLotRecordType = 0x0bf999e7;
constexpr uint32_t kDbSegmentInterface = 0x656b8f0c;
constexpr uint32_t kReaderInterface = 0x051b11bd;
constexpr uint32_t kWriterInterface = 0xc51b11cb;
constexpr uint32_t kStringTableType = 0x53545223;
constexpr uint32_t kStringTableInstance = 0x1f4;
constexpr uint32_t kStringTableClass = 0xdb4cb0f9;
constexpr uint32_t kStringTableInterface = 0xfb4cb0de;
constexpr uint32_t kEntryClassId = 0xafe3c527;
constexpr uint32_t kNumberFormatRva = 0xdca02c;

__forceinline void *PersistSystem() { return CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>(); }
__forceinline void *ResourceManager() { return OriginalFunction<void *(__cdecl *)(), 0x20deb5>()(); }

// A resource key as the persist system takes it (16 bytes). A struct rather than an array: arrays on the stack would get a
// stack-protector cookie the original does not have.
struct RecordKey {
    uint32_t type, group, instance, high;
};

// The group of the neighbourhood's persist face (slot 6).
uint32_t PersistGroup(void *face) { return CallSlot<uint32_t, 6>(face); }
} // namespace

uint8_t cTSNeighborhood::Initialize(void *owner)
{
    if (!mInitialized) {
        mInitialized = 1;
        Replace(mOwner, owner);
        EnsureStateHelper(this);
        // The message face through the base conversion (null-tested: the original's compare-and-select).
        reinterpret_cast<KeyMapSlot5Walker *>(&mServiceIds)
            ->CallSlot5ForEachKey(reinterpret_cast<uint32_t>(static_cast<cIGZMessageTarget2 *>(this)), nullptr);
    }
    return 1;
}

uint8_t cTSNeighborhood::Shutdown()
{
    mInitialized = 0;
    Drop(mStateHelper);
    Drop(mDelegate);
    Drop(mSecondOwner);
    Drop(mOwner);
    mLots.clear();
    mTexts.clear();
    mField6C = 1;
    reinterpret_cast<KeyMapWalker *>(&mServiceIds)
        ->CallSlot6ForEachKey(reinterpret_cast<uint32_t>(static_cast<cIGZMessageTarget2 *>(this)), nullptr);
    return 1;
}

uint8_t cTSNeighborhood::Save()
{
    void *persist = PersistSystem();
    return Narrow<16, uint8_t, void *, uint32_t>(persist)->call(GetPersistResource(), 0u);
}

uint8_t cTSNeighborhood::FormatNumber(cRZString *out, uint32_t value, void *numberService)
{
    if (!numberService) numberService = CallSlot<void *, 6>(GetCachedSlot23());
    static_cast<PrintfSlot *>(static_cast<void *>(out))
        ->call(GameAddress<const char>(kNumberFormatRva), CallSlot<const char *, 5>(CallSlot<void *, 8>(numberService)), value);
    return value != 0;
}

uint8_t cTSNeighborhood::FormatNumberOf(cRZString *out, void *object, void *numberService)
{
    return FormatNumber(out, CallSlot<uint32_t, 18>(object), numberService);
}

const char *cTSNeighborhood::GetNameText() { return VirtualSlot<const char *(__thiscall *)(void *)>(&mName, 5)(&mName); }

void cTSNeighborhood::NotifyMembers(uint32_t value)
{
    if (mDelegate) CallSlot<void, 43>(mDelegate, value);
    if (GetCachedSingleton()) {
        if (CallSlot<void *, 17>(GetCachedSingleton())) {
            void *next = CallSlot<void *, 17>(GetCachedSingleton());
            CallSlot<void, 17>(next, value);
        }
    }
    if (mStateHelper) CallSlot<void, 17>(mStateHelper, value);
    for (auto &entry : mMembers1)
        if (void *member = entry.second.Get()) CallSlot<void, 17>(member, value);
    for (auto &entry : mMembers2)
        if (void *member = entry.second.Get()) CallSlot<void, 17>(member, value);
    for (auto &entry : mMembers3)
        if (void *member = entry.second.Get()) CallSlot<void, 17>(member, value);
}

void *cTSNeighborhood::GetStringTable()
{
    if (mDelegate) return CallSlot<void *, 73>(mDelegate);
    if (!mField_A4) {
        const uint32_t group = PersistGroup(PersistFace());
        const RecordKey key = {kStringTableType, group, kStringTableInstance, 1};
        auto *framework = GetDirectorFrameworkObject();
        Drop(mField_A4);
        CallSlot<uint8_t, 3>(framework, kStringTableClass, kStringTableInterface, &mField_A4);
        CallSlot<void, 17>(mField_A4, &key);
        CallSlot<void, 19>(mField_A4);
    }
    return mField_A4;
}

int32_t cTSNeighborhood::AdjustCounter(int32_t delta)
{
    if (mDelegate) return CallSlot<int32_t, 74>(mDelegate, delta);
    void *persist = PersistSystem();
    const RecordKey key = {kCounterRecordType, PersistGroup(PersistFace()), 0, 0};
    Held segment;
    CallSlot<uint8_t, 34>(persist, &key, kDbSegmentInterface, &segment.object);
    if (!segment.object) return 0;
    int32_t value = 0;
    if (!CallSlot<uint8_t, 20>(segment.object, &key)) {
        Held record;
        if (CallSlot<uint8_t, 23>(segment.object, &key, &record.object)) {
            value = 1;
            Held writer;
            if (As<cIGZUnknown>(record.object)->QueryInterface(kWriterInterface, &writer.object))
                CallSlot<void, 8>(writer.object, value);
            CallSlot<void, 24>(segment.object, record.object);
            CallSlot<void, 10>(segment.object);
        }
    } else {
        Held record;
        if (CallSlot<uint8_t, 22>(segment.object, &key, &record.object, 3u)) {
            Held reader;
            Held writer;
            if (As<cIGZUnknown>(record.object)->QueryInterface(kReaderInterface, &reader.object))
                CallSlot<void, 8>(reader.object, &value);
            if (delta) {
                value += delta;
                CallSlot<void, 13>(record.object, 0u);
                if (As<cIGZUnknown>(record.object)->QueryInterface(kWriterInterface, &writer.object))
                    CallSlot<void, 8>(writer.object, value);
            }
            CallSlot<void, 24>(segment.object, record.object);
            CallSlot<void, 10>(segment.object);
            Drop(record.object);
        }
    }
    return value;
}

uint8_t cTSNeighborhood::LookupEntryText(int32_t entryId, void *out)
{
    if (mDelegate) return CallSlot<uint8_t, 75>(mDelegate, entryId, out);
    if (mStateHelper) {
        Held table;
        table.object = GetStringTable();
        AddRefObject(table.object);
        if (table.object) {
            void **first;
            void **last;
            CallSlot<void, 28>(mStateHelper, &first);
            CallSlot<void, 29>(mStateHelper, &last);
            for (; first != last; ++first) {
                void *entry = *first;
                if (entry && CallSlot<int32_t, 6>(entry) == static_cast<int32_t>(kEntryClassId)) {
                    CallSlot<int16_t, 21>(entry, 0);
                    const int16_t id = CallSlot<int16_t, 21>(entry, 1);
                    const int16_t text = CallSlot<int16_t, 21>(entry, 2);
                    if (id == entryId) return CallSlot<uint8_t, 7>(table.object, out, static_cast<int32_t>(text), -1);
                }
            }
        }
    }
    return 0;
}

uint8_t cTSNeighborhood::SaveLot(void *lot)
{
    if (!lot) return 0;
    void *persist = PersistSystem();
    const uint32_t instance = CallSlot<uint32_t, 18>(lot);
    const RecordKey key = {kLotRecordType, PersistGroup(PersistFace()), instance, 0};
    CallSlot<void, 17>(persist, &key);
    CallSlot<void, 4>(CallSlot<void *, 25>(lot), &key);
    uint8_t saved = CallSlot<uint8_t, 16>(persist, CallSlot<void *, 25>(lot), 0u);
    if (saved) {
        const uint32_t group = PersistGroup(PersistFace());
        saved &= CallSlot<uint8_t, 29>(lot, CallSlot<uint32_t, 18>(lot), group);
        if (saved) CallSlot<void, 23>(CallSlot<void *, 4>(lot), 0x20u);
    }
    return saved;
}

uint8_t cTSNeighborhood::SaveLots()
{
    if (!mByteC8) return 1;
    void *persist = PersistSystem();
    ServiceBlockVector keys{};
    CallSlot<void, 36>(persist, &keys, PersistGroup(PersistFace()), kLotRecordType);
    uint8_t saved = 1;
    for (auto &entry : mLots) {
        void *lot = entry.second.Get();
        if (!lot) continue;
        if (CallSlot<uint8_t, 24>(CallSlot<void *, 4>(lot), 0x20u)) saved &= SaveLot(lot);
        RecordKey scratch;
        const RecordKey *lotKey = CallSlot<const RecordKey *, 3>(CallSlot<void *, 25>(lot), &scratch);
        auto *begin = static_cast<RecordKey *>(keys.mFirst);
        auto *end = static_cast<RecordKey *>(keys.mLast);
        auto *found = begin;
        while (found != end && std::memcmp(found, lotKey, sizeof(RecordKey)) != 0) ++found;
        if (found != end) {
            std::memmove(found, found + 1, reinterpret_cast<uint8_t *>(end) - reinterpret_cast<uint8_t *>(found + 1));
            keys.mLast = end - 1;
        }
    }
    for (auto *key = static_cast<RecordKey *>(keys.mFirst); key != static_cast<RecordKey *>(keys.mLast); ++key)
        CallSlot<void, 17>(persist, key);
    keys.FreeBlock();
    return saved;
}

void cTSNeighborhood::ResetLots()
{
    mByteC8 = 0;
    void *resources = ResourceManager();
    for (auto &entry : mLots) {
        void *lot = entry.second.Get();
        if (!lot) continue;
        RecordKey scratch;
        CallSlot<void, 10>(resources, CallSlot<const RecordKey *, 3>(CallSlot<void *, 25>(lot), &scratch));
    }
    mLots.clear();
    mTexts.clear();
    mField6C = 1;
}

void *cTSNeighborhood::GetOrCreateMember2(uint32_t id)
{
    if (mDelegate) return CallSlot<void *, 38>(mDelegate, id);
    const int16_t key = static_cast<int16_t>(id);
    auto found = mMembers2.lower_bound(key);
    if (found == mMembers2.end() || key < found->first) {
        Held member;
        auto *framework = GetDirectorFrameworkObject();
        Drop(member.object);
        CallSlot<uint8_t, 3>(framework, kHelperClass, kHelperInterface, &member.object);
        mMembers2[key].Assign(member.object);
        if (member.object) {
            CallSlot<void, 6>(member.object, 2u, id);
            CallSlot<void, 45>(member.object, PersistGroup(PersistFace()));
        }
    }
    return mMembers2[key].Get();
}

void *cTSNeighborhood::GetOrCreateMember3(uint32_t id)
{
    if (mDelegate) return CallSlot<void *, 39>(mDelegate, id);
    const int16_t key = static_cast<int16_t>(id);
    auto found = mMembers3.lower_bound(key);
    if (found == mMembers3.end() || key < found->first) {
        Held member;
        auto *framework = GetDirectorFrameworkObject();
        Drop(member.object);
        CallSlot<uint8_t, 3>(framework, kHelperClass, kHelperInterface, &member.object);
        mMembers3[key].Assign(member.object);
        if (member.object) {
            CallSlot<void, 6>(member.object, 3u, id);
            if (void *first = CallSlot<void *, 10>(GetCachedSingleton())) {
                if (void *second = CallSlot<void *, 16>(first, static_cast<int32_t>(key), 1u)) {
                    void *third = CallSlot<void *, 10>(second);
                    void *fourth = CallSlot<void *, 67>(third);
                    CallSlot<void, 45>(member.object, CallSlot<uint32_t, 11>(fourth));
                }
            }
        }
    }
    return mMembers3[key].Get();
}

void cTSNeighborhood::RefreshLotFlags()
{
    void *checker = CallSlot<void *, 14>(GetCachedSlot23());
    if (!checker) return;
    auto *list = static_cast<void ***>(CallSlot<void *, 45>(CallSlot<void *, 26>(GetCachedSingleton())));
    for (void **item = list[0]; item != list[1]; ++item) {
        const uint16_t value = CallSlot<uint16_t, 10>(*item, 0x20u, 1u);
        const uint8_t blocked = CallSlot<uint8_t, 30>(checker, *item);
        CallSlot<void, 11>(*item, 0x20u, static_cast<uint32_t>(blocked ? 0 : value), 1u);
    }
}

// ---- adapters (the RVA hooks; src/hook/tables/cTSNeighborhood.cpp) ----

using Adapters = cTSNeighborhoodAdapters;

Adapters::Self *__fastcall Adapters::Construct(Self *memory, void *) { return ::new (memory) Self; }

void __fastcall Adapters::Destruct(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->Self::~Self();
}

// The scalar deleting destructor has no name to call without dispatch: the two steps by hand.
Adapters::Self *__fastcall Adapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    [[msvc::forceinline_calls]] self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}


bool __fastcall Adapters::QueryInterface(Self *self, void *, uint32_t id, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::QueryInterface(id, out);
}

uint32_t __fastcall Adapters::Release(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::Release();
}

bool __fastcall Adapters::Write(Self *self, void *, ifaces::cIGZOStream *stream)
{
    [[msvc::forceinline_calls]] return self->Self::Write(stream);
}

bool __fastcall Adapters::Read(Self *self, void *, ifaces::cIGZIStream *stream)
{
    [[msvc::forceinline_calls]] return self->Self::Read(stream);
}

uint32_t __fastcall Adapters::GetGZCLSID(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetGZCLSID();
}

uint8_t __fastcall Adapters::Initialize(Self *self, void *, void *owner)
{
    [[msvc::forceinline_calls]] return self->Self::Initialize(owner);
}

uint8_t __fastcall Adapters::Shutdown(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::Shutdown();
}

uint8_t __fastcall Adapters::Save(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::Save();
}

uint8_t __fastcall Adapters::GetFlag0(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetFlag0();
}

void __fastcall Adapters::SetFlag0(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->Self::SetFlag0();
}

void __fastcall Adapters::SetDelegate(Self *self, void *, void *object)
{
    [[msvc::forceinline_calls]] self->Self::SetDelegate(object);
}

void __fastcall Adapters::SetSecondOwner(Self *self, void *, void *object)
{
    [[msvc::forceinline_calls]] self->Self::SetSecondOwner(object);
}



void __fastcall Adapters::ResetLots(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->Self::ResetLots();
}

uint8_t __fastcall Adapters::SaveLots(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::SaveLots();
}


uint8_t __fastcall Adapters::SaveLot(Self *self, void *, void *lot)
{
    [[msvc::forceinline_calls]] return self->Self::SaveLot(lot);
}


uint16_t __fastcall Adapters::GetCodeWord(Self *self, void *, int32_t index)
{
    [[msvc::forceinline_calls]] return self->Self::GetCodeWord(index);
}

void __fastcall Adapters::SetCodeWord(Self *self, void *, int32_t index, uint16_t value)
{
    [[msvc::forceinline_calls]] self->Self::SetCodeWord(index, value);
}

void __fastcall Adapters::RefreshLotFlags(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->Self::RefreshLotFlags();
}





uint32_t __fastcall Adapters::GetGlobalWord(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetGlobalWord();
}

float __fastcall Adapters::GetGlobalFloatB(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetGlobalFloatB();
}

float __fastcall Adapters::GetGlobalFloatA(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetGlobalFloatA();
}


void * __fastcall Adapters::GetStateHelperOrDelegated(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetStateHelperOrDelegated();
}


void * __fastcall Adapters::GetOrCreateMember2(Self *self, void *, uint32_t id)
{
    [[msvc::forceinline_calls]] return self->Self::GetOrCreateMember2(id);
}

void * __fastcall Adapters::GetOrCreateMember3(Self *self, void *, uint32_t id)
{
    [[msvc::forceinline_calls]] return self->Self::GetOrCreateMember3(id);
}

void __fastcall Adapters::ClearMemberMap1(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->Self::ClearMemberMap1();
}

void __fastcall Adapters::ClearMemberMap2(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->Self::ClearMemberMap2();
}

void __fastcall Adapters::ClearMemberMap3(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->Self::ClearMemberMap3();
}

void __fastcall Adapters::NotifyMembers(Self *self, void *, uint32_t value)
{
    [[msvc::forceinline_calls]] self->Self::NotifyMembers(value);
}

void ** __fastcall Adapters::MemberMap1Begin(Self *self, void *, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::MemberMap1Begin(out);
}

void ** __fastcall Adapters::MemberMap1End(Self *self, void *, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::MemberMap1End(out);
}

void ** __fastcall Adapters::MemberMap2Begin(Self *self, void *, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::MemberMap2Begin(out);
}

void ** __fastcall Adapters::MemberMap2End(Self *self, void *, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::MemberMap2End(out);
}

void ** __fastcall Adapters::MemberMap3Begin(Self *self, void *, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::MemberMap3Begin(out);
}

void ** __fastcall Adapters::MemberMap3End(Self *self, void *, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::MemberMap3End(out);
}

float __fastcall Adapters::GetTen(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetTen();
}

void __fastcall Adapters::SetSize(Self *self, void *, uint32_t width, uint32_t height)
{
    [[msvc::forceinline_calls]] self->Self::SetSize(width, height);
}

void * __fastcall Adapters::RewindLots(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::RewindLots();
}

void * __fastcall Adapters::NextLot(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::NextLot();
}


void * __fastcall Adapters::GetLot(Self *self, void *, int32_t number)
{
    [[msvc::forceinline_calls]] return self->Self::GetLot(number);
}

void * __fastcall Adapters::FindLotAt(Self *self, void *, int32_t x, int32_t y)
{
    [[msvc::forceinline_calls]] return self->Self::FindLotAt(x, y);
}

void * __fastcall Adapters::FindLotOverlapping(Self *self, void *, const int32_t *rect)
{
    [[msvc::forceinline_calls]] return self->Self::FindLotOverlapping(rect);
}

void __fastcall Adapters::CollectLotsOverlapping(Self *self, void *, const int32_t *rect, std::vector<void *> *out)
{
    [[msvc::forceinline_calls]] self->Self::CollectLotsOverlapping(rect, out);
}




const char * __fastcall Adapters::GetNameText(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetNameText();
}




uint8_t __fastcall Adapters::FormatNumber(Self *self, void *, cRZString *out, uint32_t value, void *numberService)
{
    [[msvc::forceinline_calls]] return self->Self::FormatNumber(out, value, numberService);
}

uint8_t __fastcall Adapters::FormatNumberOf(Self *self, void *, cRZString *out, void *object, void *numberService)
{
    [[msvc::forceinline_calls]] return self->Self::FormatNumberOf(out, object, numberService);
}

uint8_t __fastcall Adapters::GetFlagByte(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetFlagByte();
}

void * __fastcall Adapters::GetStringTable(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetStringTable();
}

int32_t __fastcall Adapters::AdjustCounter(Self *self, void *, int32_t delta)
{
    [[msvc::forceinline_calls]] return self->Self::AdjustCounter(delta);
}

uint8_t __fastcall Adapters::LookupEntryText(Self *self, void *, int32_t entryId, void *out)
{
    [[msvc::forceinline_calls]] return self->Self::LookupEntryText(entryId, out);
}

// The key by value: the hidden return pointer is the stack argument, returned in eax (as the thiscall slot).
cGZPersistResourceKey *__fastcall Adapters::GetKey(cIGZPersistResource *face, void *, cGZPersistResourceKey *out)
{
    __assume(face != nullptr); // a slot's this is never null: no null test in the face-to-object-to-face adjustment
    [[msvc::forceinline_calls]] *out = static_cast<Self &>(*face).Self::GetKey();
    return out;
}



bool __fastcall Adapters::SetKey(cIGZPersistResource *face, void *, const cGZPersistResourceKey &key)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::SetKey(key);
}

bool __fastcall Adapters::SetInstance(cIGZPersistResource *face, void *, uint32_t instance)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::SetInstance(instance);
}
