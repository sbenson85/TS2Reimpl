#include "simantics/cTSTreeTableEntry.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZString.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "rz/StaticGetters2.h"

#include <map>
#include <new>

// cTSTreeTableEntry (src/simantics/cTSTreeTableEntry.h). Every body was compiled /O2 in the original (tools/flagprofile.py).

namespace {

using Self = cTSTreeTableEntry;

// Motives the advertisement functions leave alone (no advertisement slot): 2, 10, 11, 12 and anything past 15.
bool IsUnadvertised(uint32_t motive)
{
    return motive == 2 || motive == 10 || motive == 12 || motive == 11 || motive >= 0x10;
}

// One row of the motive table (0xdc4314, constant data): the person data index an ad's type reads (valid below 0xe7) and
// whether the score inverts it.
struct MotiveTableRow {
    uint32_t personDataIndex;
    int32_t inverted;
    uint32_t unused;
};
const MotiveTableRow &MotiveRow(int16_t type)
{
    return GameAddress<const MotiveTableRow>(Self::kMotiveTable)[type];
}

// The value `which` (0..2) of a triple: +4 + which * 2.
int16_t &Value(cTSTriple<int16_t> &triple, int32_t which)
{
    return (&triple.mFirst)[which];
}

// The person calls (cITSPerson-like slots of the object handed in).
uint32_t PersonUsesMotiveAds(void *person) // slot 38 (the low byte is the answer)
{
    return VirtualCall<uint32_t(__thiscall *)(void *), 38>(person);
}
uint32_t PersonStageA(void *person) // slot 19
{
    return VirtualCall<uint32_t(__thiscall *)(void *), 19>(person);
}
uint32_t PersonStageB(void *person) // slot 20
{
    return VirtualCall<uint32_t(__thiscall *)(void *), 20>(person);
}
int16_t PersonData(void *person, uint32_t index, uint32_t flag) // slot 10
{
    return VirtualCall<int16_t(__thiscall *)(void *, uint32_t, uint32_t), 10>(person, index, flag);
}

// The sum of the trait value of each element picked by two bits of `mask` (bit 2j: the first, bit 2j+1: the second, else
// the third; the shift counts wrap at 32 as the original's shl does), in single precision, element by element.
float SumPicked(const cTSTraitAdVector &values, uint32_t mask)
{
    float sum = 0.0f;
    uint32_t bit = 0;
    for (const cTSTraitAdValue &value : values) {
        int16_t picked;
        if ((mask & (1u << (bit & 0x1f))) != 0) {
            picked = value.mFirst;
        } else if ((mask & (1u << ((bit + 1) & 0x1f))) != 0) {
            picked = value.mSecond;
        } else {
            picked = value.mThird;
        }
        sum += static_cast<float>(picked);
        bit += 2;
    }
    return sum;
}

} // namespace

// ---- construction ----

cTSTreeTableEntry::cTSTreeTableEntry()
    : mRefCount(0), mCheckTreeId(0), mActionTreeId(0), mUIDisplayType(0), mMotiveAds(), mTraitAds(), mAttenuation(3),
      mCustomAttenuation(0.0f), mFlags(0x50), mIndex(0), mAutonomyThreshold(0x32), mJoinIndex(0xffffffffu), mFacialAnimType(2),
      mMemoryIterativeMultiplier(0.5f), mName(), mIconTableIndex(1), mIconGuid(0), mUses()
{
}

cTSTreeTableEntry::cTSTreeTableEntry(const cTSTreeTableEntry &other)
    : mRefCount(0), mCheckTreeId(other.mCheckTreeId), mActionTreeId(other.mActionTreeId), mUIDisplayType(other.mUIDisplayType),
      mMotiveAds(other.mMotiveAds), mTraitAds(other.mTraitAds), mAttenuation(other.mAttenuation),
      mCustomAttenuation(other.mCustomAttenuation), mFlags(other.mFlags), mIndex(other.mIndex),
      mAutonomyThreshold(other.mAutonomyThreshold), mJoinIndex(other.mJoinIndex), mFacialAnimType(other.mFacialAnimType),
      mMemoryIterativeMultiplier(other.mMemoryIterativeMultiplier), mName(other.mName), mIconTableIndex(other.mIconTableIndex),
      mIconGuid(other.mIconGuid), mUses()
{
}

cTSTreeTableEntry::~cTSTreeTableEntry() = default;

// ---- cIGZUnknown, cIGZSerializable ----

bool cTSTreeTableEntry::QueryInterface(uint32_t riid, void **out)
{
    if (riid == kSerializableId || riid == 1 || riid == kClassId || riid == kInterfaceId) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

uint32_t cTSTreeTableEntry::AddRef()
{
    return ++mRefCount;
}

uint32_t cTSTreeTableEntry::Release()
{
    if (mRefCount > 1) {
        return --mRefCount;
    }
    delete this;
    return 0;
}

uint32_t cTSTreeTableEntry::GetGZCLSID()
{
    return 0;
}

// Writes a trimmed copy: the entry itself keeps its trailing empty advertisements.
bool cTSTreeTableEntry::Write(ifaces::cIGZOStream *stream)
{
    cTSTreeTableEntry copy(*this);
    return copy.WriteBody(stream, true);
}

bool cTSTreeTableEntry::WriteBody(ifaces::cIGZOStream *stream, bool trim)
{
    if (trim) {
        Trim();
    }
    stream->SetSint16(static_cast<int16_t>(mActionTreeId));
    stream->SetSint16(static_cast<int16_t>(mCheckTreeId));
    stream->SetUint32(mFlags);
    stream->SetSint32(static_cast<int32_t>(mIndex));
    stream->SetSint32(static_cast<int32_t>(mAttenuation));
    stream->SetFloat32(mCustomAttenuation);
    stream->SetSint32(static_cast<int32_t>(mAutonomyThreshold));
    stream->SetSint32(static_cast<int32_t>(mJoinIndex));
    stream->SetSint16(mUIDisplayType);
    stream->SetSint32(static_cast<int32_t>(mFacialAnimType));
    stream->SetFloat32(mMemoryIterativeMultiplier);
    stream->SetSint32(static_cast<int32_t>(mIconGuid));
    stream->SetSint32(static_cast<int32_t>(mIconTableIndex));
    const uint32_t stages = static_cast<uint32_t>(mMotiveAds.size());
    stream->SetUint32(stages);
    for (uint32_t stage = 0; stage < stages; ++stage) {
        const cTSTreeTableAdVector &ads = mMotiveAds[stage];
        const uint32_t count = static_cast<uint32_t>(ads.size());
        stream->SetUint32(count);
        for (uint32_t i = 0; i < count; ++i) {
            stream->SetSint16(ads[i].mFirst);
            stream->SetSint16(ads[i].mSecond);
            stream->SetSint16(ads[i].mThird);
        }
    }
    const uint32_t traitStages = static_cast<uint32_t>(mTraitAds.size());
    stream->SetUint32(traitStages);
    for (uint32_t stage = 0; stage < traitStages; ++stage) {
        const cTSTraitAdList &list = mTraitAds[stage];
        const uint32_t traits = static_cast<uint32_t>(list.size());
        stream->SetUint32(traits);
        for (uint32_t trait = 0; trait < traits; ++trait) {
            const cTSTraitAdVector &values = list[trait];
            const uint32_t count = static_cast<uint32_t>(values.size());
            stream->SetUint32(count);
            for (uint32_t i = 0; i < count; ++i) {
                stream->SetSint16(values[i].mFirst);
                stream->SetSint16(values[i].mSecond);
                stream->SetSint16(values[i].mThird);
            }
        }
    }
    return stream->GetError() == 0;
}

// The format version comes from the stream's owner (stream slot 20, its slot 78: a pointer to the version). Versions below
// 0x44 hold one count and stage-0 ads; up to 0x53 per-stage counts (7, or 11 for 0x53) and the ads stored as min, delta,
// type, set through slot 59 (so empty ones are skipped); from 0x54 the whole vectors.
bool cTSTreeTableEntry::Read(ifaces::cIGZIStream *stream)
{
    void *const owner = VirtualCall<void *(__thiscall *)(void *), 20>(stream);
    uint32_t version = *VirtualCall<uint32_t *(__thiscall *)(void *), 78>(owner);
    if (version < 0x44) {
        stream->GetSint16(reinterpret_cast<int16_t *>(&mActionTreeId));
        stream->GetSint16(reinterpret_cast<int16_t *>(&mCheckTreeId));
        // The original reads the count into a slot that held `this`; a stream that leaves it unwritten would loop on that.
        int32_t count = 0;
        stream->GetSint32(&count);
        stream->GetUint32(&mFlags);
        stream->GetSint32(reinterpret_cast<int32_t *>(&mIndex));
        stream->GetSint32(reinterpret_cast<int32_t *>(&mAttenuation));
        stream->GetFloat32(&mCustomAttenuation);
        stream->GetSint32(reinterpret_cast<int32_t *>(&mAutonomyThreshold));
        stream->GetSint32(reinterpret_cast<int32_t *>(&mJoinIndex));
        for (int16_t i = 0; i < count; ++i) {
            cTSTreeTableAd ad;
            stream->GetSint16(&ad.mSecond);
            stream->GetSint16(&ad.mThird);
            stream->GetSint16(&ad.mFirst);
            if (ad.mSecond != 0 || ad.mThird != 0) {
                SetMotiveAd(i, 0, &ad, false);
            }
        }
        return stream->GetError() == 0;
    }
    stream->GetSint16(reinterpret_cast<int16_t *>(&mActionTreeId));
    stream->GetSint16(reinterpret_cast<int16_t *>(&mCheckTreeId));
    uint32_t stageCount = 0;
    if (version < 0x53) {
        stageCount = 7;
    } else if (version < 0x54) {
        stageCount = 11;
    }
    std::vector<int32_t> counts;
    if (stageCount != 0) {
        counts.resize(stageCount);
        for (uint32_t i = 0; i < stageCount; ++i) {
            stream->GetSint32(&counts[i]);
        }
    }
    stream->GetUint32(&mFlags);
    if (version < 0x52) {
        uint32_t flags = mFlags;
        flags = (flags & 0x10) != 0 ? flags & ~0x10u : flags | 0x10;
        flags = (flags & 0x40) != 0 ? flags & ~0x40u : flags | 0x40;
        mFlags = flags;
    }
    stream->GetSint32(reinterpret_cast<int32_t *>(&mIndex));
    stream->GetSint32(reinterpret_cast<int32_t *>(&mAttenuation));
    stream->GetFloat32(&mCustomAttenuation);
    stream->GetSint32(reinterpret_cast<int32_t *>(&mAutonomyThreshold));
    stream->GetSint32(reinterpret_cast<int32_t *>(&mJoinIndex));
    bool whole = false;
    if (version < 0x45) {
        mUIDisplayType = 0;
        mFacialAnimType = 3;
    } else {
        stream->GetSint16(&mUIDisplayType);
        if (version < 0x46) {
            mFacialAnimType = 3;
        } else {
            stream->GetSint32(reinterpret_cast<int32_t *>(&mFacialAnimType));
            if (version > 0x49) {
                stream->GetFloat32(&mMemoryIterativeMultiplier);
            }
            if (version > 0x4b) {
                stream->GetSint32(reinterpret_cast<int32_t *>(&mIconGuid));
                stream->GetSint32(reinterpret_cast<int32_t *>(&mIconTableIndex));
            }
            whole = version >= 0x54;
        }
    }
    if (!whole) {
        const int32_t stages = static_cast<int32_t>(counts.size());
        for (int32_t stage = 0; stage < stages; ++stage) {
            for (int16_t i = 0; i < counts[stage]; ++i) {
                cTSTreeTableAd ad;
                stream->GetSint16(&ad.mSecond);
                stream->GetSint16(&ad.mThird);
                stream->GetSint16(&ad.mFirst);
                if (ad.mSecond != 0 || ad.mThird != 0) {
                    SetMotiveAd(i, stage, &ad, false);
                }
            }
        }
        return stream->GetError() == 0;
    }
    // The counts land in reused locals: an unwritten read keeps the version or the previous count.
    uint32_t outer = version;
    uint32_t inner = 0;
    stream->GetUint32(&outer);
    mMotiveAds.resize(outer);
    for (uint32_t stage = 0; stage < outer; ++stage) {
        stream->GetUint32(&inner);
        mMotiveAds[stage].resize(inner);
        for (uint32_t i = 0; i < inner; ++i) {
            cTSTreeTableAd &ad = mMotiveAds[stage][i];
            stream->GetSint16(&ad.mFirst);
            stream->GetSint16(&ad.mSecond);
            stream->GetSint16(&ad.mThird);
        }
    }
    uint32_t traitStages = 0; // the original's slot held `this`
    stream->GetUint32(&traitStages);
    mTraitAds.resize(traitStages);
    for (uint32_t stage = 0; stage < traitStages; ++stage) {
        stream->GetUint32(&outer);
        mTraitAds[stage].resize(outer);
        for (uint32_t trait = 0; trait < outer; ++trait) {
            stream->GetUint32(&inner);
            mTraitAds[stage][trait].resize(inner);
            for (uint32_t i = 0; i < inner; ++i) {
                cTSTraitAdValue &value = mTraitAds[stage][trait][i];
                stream->GetSint16(&value.mFirst);
                stream->GetSint16(&value.mSecond);
                stream->GetSint16(&value.mThird);
            }
        }
    }
    return stream->GetError() == 0;
}

// ---- simple fields and flags ----

bool cTSTreeTableEntry::ResetToDefaults(uint32_t)
{
    mFlags = 0x50;
    mCheckTreeId = 0;
    mActionTreeId = 0;
    mIconGuid = 0;
    mAutonomyThreshold = 0x32;
    mJoinIndex = 0xffffffffu;
    mMemoryIterativeMultiplier = 0.5f;
    mIconTableIndex = 1;
    mAttenuation = 3;
    return true;
}

uint32_t cTSTreeTableEntry::GetIndex() { return mIndex; }
void cTSTreeTableEntry::SetIndex(uint32_t index) { mIndex = index; }
uint32_t cTSTreeTableEntry::GetFlags() { return mFlags; }
bool cTSTreeTableEntry::HasFlags(uint32_t mask) { return (mFlags & mask) == mask; }
void cTSTreeTableEntry::SetMaskedFlags(uint32_t values, uint32_t mask) { mFlags = (~mask & mFlags) | (values & mask); }

void cTSTreeTableEntry::SetFlags(uint32_t bits, bool on)
{
    if (on) {
        mFlags |= bits;
    } else {
        mFlags &= ~bits;
    }
}

#define TS_TREE_TABLE_FLAG(Get, Set, bit)                                                                                    \
    bool cTSTreeTableEntry::Get() { return ((mFlags >> (bit)) & 1) != 0; }                                                   \
    void cTSTreeTableEntry::Set(bool on)                                                                                     \
    {                                                                                                                         \
        mFlags &= ~(1u << (bit));                                                                                             \
        if (on) {                                                                                                             \
            mFlags |= 1u << (bit);                                                                                            \
        }                                                                                                                     \
    }
TS_TREE_TABLE_FLAG(GetCanJoin, SetCanJoin, 1)
TS_TREE_TABLE_FLAG(GetRunImmediately, SetRunImmediately, 2)
TS_TREE_TABLE_FLAG(GetFlag5, SetFlag5, 5)
TS_TREE_TABLE_FLAG(GetDebugOnly, SetDebugOnly, 7)
TS_TREE_TABLE_FLAG(GetAutoFirstSelect, SetAutoFirstSelect, 8)
TS_TREE_TABLE_FLAG(GetAllowSubQueue, SetAllowSubQueue, 14)
TS_TREE_TABLE_FLAG(GetTypeSubQueue, SetTypeSubQueue, 15)
TS_TREE_TABLE_FLAG(GetParentalControl, SetParentalControl, 17)
TS_TREE_TABLE_FLAG(GetAffectsOwnership, SetAffectsOwnership, 18)
TS_TREE_TABLE_FLAG(GetResetMotiveDecay, SetResetMotiveDecay, 19)
TS_TREE_TABLE_FLAG(GetAllowOnSelf, SetAllowOnSelf, 20)
TS_TREE_TABLE_FLAG(GetOverrideDisable, SetOverrideDisable, 21)
TS_TREE_TABLE_FLAG(GetFlag22, SetFlag22, 22)
TS_TREE_TABLE_FLAG(GetPreferredByFurious, SetPreferredByFurious, 23)
#undef TS_TREE_TABLE_FLAG

uint32_t cTSTreeTableEntry::GetAgeFlags() { return mFlags & 0x3f013e51; }
int16_t cTSTreeTableEntry::GetUIDisplayType() { return mUIDisplayType; }
void cTSTreeTableEntry::SetUIDisplayType(int16_t type) { mUIDisplayType = type; }
uint32_t cTSTreeTableEntry::GetJoinIndex() { return mJoinIndex; }
void cTSTreeTableEntry::SetJoinIndex(uint32_t index) { mJoinIndex = index; }
bool cTSTreeTableEntry::IsAutonomyDisabled() { return static_cast<int32_t>(mAutonomyThreshold) >= 100; }
uint32_t cTSTreeTableEntry::GetFacialAnimType() { return mFacialAnimType; }
void cTSTreeTableEntry::SetFacialAnimType(uint32_t type) { mFacialAnimType = type; }
uint32_t cTSTreeTableEntry::GetIconTableIndex() { return mIconTableIndex; }
void cTSTreeTableEntry::SetIconTableIndex(uint32_t index) { mIconTableIndex = index; }
uint32_t cTSTreeTableEntry::GetIconGuid() { return mIconGuid; }
void cTSTreeTableEntry::SetIconGuid(uint32_t guid) { mIconGuid = guid; }
uint16_t cTSTreeTableEntry::GetActionTreeId() { return mActionTreeId; }
void cTSTreeTableEntry::SetActionTreeId(uint16_t id) { mActionTreeId = id; }
uint16_t cTSTreeTableEntry::GetCheckTreeId() { return mCheckTreeId; }
void cTSTreeTableEntry::SetCheckTreeId(uint16_t id) { mCheckTreeId = id; }
uint32_t cTSTreeTableEntry::GetAttenuation() { return mAttenuation; }
void cTSTreeTableEntry::SetAttenuation(uint32_t code) { mAttenuation = code; }
float cTSTreeTableEntry::GetMemoryIterativeMultiplier() { return mMemoryIterativeMultiplier; }
void cTSTreeTableEntry::SetMemoryIterativeMultiplier(float value) { mMemoryIterativeMultiplier = value; }
float cTSTreeTableEntry::GetCustomAttenuation() { return mCustomAttenuation; }
uint32_t cTSTreeTableEntry::GetAutonomyThreshold() { return mAutonomyThreshold; }
void cTSTreeTableEntry::SetAutonomyThreshold(uint32_t threshold) { mAutonomyThreshold = threshold; }
cRZString *cTSTreeTableEntry::GetNameString() { return &mName.mString; }
cITSTreeTableEntry *cTSTreeTableEntry::AsTreeTableEntry() { return this; }
cITSTreeTableEntry *cTSTreeTableEntry::AsTreeTableEntry2() { return this; }

void cTSTreeTableEntry::SetCustomAttenuation(float value)
{
    mAttenuation = 0;
    mCustomAttenuation = value;
}

// The tuning service (GetCachedSingletonC, slot 4: value by id into the out-parameter) holds the preset attenuations.
float cTSTreeTableEntry::GetAttenuationValue(bool alternate)
{
    void *const tuning = GetCachedSingletonC();
    if (tuning == nullptr || mAttenuation > 4) {
        return 0.02f;
    }
    uint32_t id = 0;
    switch (mAttenuation) {
    case 1:
        return 0.0f;
    case 2:
        id = 1000;
        break;
    case 3:
        id = alternate ? 0x3ec : 0x3e9;
        break;
    case 4:
        id = alternate ? 0x3ed : 0x3ea;
        break;
    default:
        return mCustomAttenuation;
    }
    float value = 0.0f;
    VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, float *), 4>(tuning, id, &value);
    return value;
}

bool cTSTreeTableEntry::GetName(cIGZString *out)
{
    out->Copy(reinterpret_cast<const cIGZString &>(mName.mString));
    return true;
}

cIGZString *cTSTreeTableEntry::SetName(const cIGZString &name)
{
    return &(reinterpret_cast<cIGZString &>(mName.mString) = name);
}

// The tab-separated debug dump (1.9 KB of stream writes) is still the game's: a forwarder, not hooked.
void cTSTreeTableEntry::DumpText(void *stream, uint32_t unused)
{
    OriginalFunction<uint32_t(__thiscall *)(void *, void *, uint32_t), 0x432060>()(this, stream, unused);
}

// ---- motive advertisements (+0x10) ----

uint32_t cTSTreeTableEntry::GetMotiveAdCount(int32_t stage)
{
    if (stage >= 5) {
        if (stage == 13 || stage == 14) {
            stage = 0;
        }
    } else if (stage < 0) {
        return 0;
    }
    if (stage < static_cast<int32_t>(mMotiveAds.size())) {
        return static_cast<uint32_t>(mMotiveAds[stage].size());
    }
    return 0;
}

int16_t *cTSTreeTableEntry::GetMotiveAdDelta(uint32_t motive, uint32_t stage)
{
    cTSTreeTableAd *const ad = GetMotiveAd(motive, stage, true, true);
    return ad != nullptr ? &ad->mThird : nullptr;
}

int16_t *cTSTreeTableEntry::GetMotiveAdMin(uint32_t motive, uint32_t stage)
{
    cTSTreeTableAd *const ad = GetMotiveAd(motive, stage, true, true);
    return ad != nullptr ? &ad->mSecond : nullptr;
}

int16_t *cTSTreeTableEntry::GetMotiveAdType(uint32_t motive, uint32_t stage)
{
    cTSTreeTableAd *const ad = GetMotiveAd(motive, stage, true, true);
    return ad != nullptr ? &ad->mFirst : nullptr;
}

// Stages 0..4 (and 13, 14 as 0): a motive ad with a minimum or a delta; stages 5..12: a trait ad that is not all zero.
bool cTSTreeTableEntry::HasAdvertisement(uint32_t motive, uint32_t stage)
{
    if (IsUnadvertised(motive)) {
        return false;
    }
    if (stage == 13 || stage == 14) {
        stage = 0;
    } else if (static_cast<int32_t>(stage) > 4) {
        const uint32_t traitStage = stage - 5;
        if (traitStage < mTraitAds.size()) {
            const cTSTraitAdList &list = mTraitAds[traitStage];
            if (static_cast<int32_t>(motive) < static_cast<int32_t>(list.size())) {
                return !IsAllZero(&list[motive]);
            }
        }
        return false;
    }
    if (stage < mMotiveAds.size()) {
        const cTSTreeTableAdVector &ads = mMotiveAds[stage];
        if (motive < ads.size() && (ads[motive].mSecond != 0 || ads[motive].mThird != 0)) {
            return true;
        }
    }
    return false;
}

cTSTreeTableAd *cTSTreeTableEntry::GetMotiveAd(uint32_t motive, uint32_t stage, bool babyAsAdult, bool create)
{
    if (babyAsAdult && (stage == 13 || stage == 14)) {
        stage = 0;
    } else if (static_cast<int32_t>(stage) > 4) {
        return nullptr;
    }
    if (mMotiveAds.size() <= stage) {
        if (!create) {
            return nullptr;
        }
        // ORIGINAL-BUG: a negative stage passes the signed check above; it empties the stages (stage + 1 == 0) and the
        // line below reads the vector before the storage.
        mMotiveAds.resize(stage + 1);
    }
    cTSTreeTableAdVector &ads = mMotiveAds[stage];
    if (ads.size() <= motive) {
        if (!create) {
            return nullptr;
        }
        ads.resize(motive + 1);
    }
    return &ads[motive];
}

cTSTreeTableAd *cTSTreeTableEntry::FindMotiveAd(int32_t motive, int32_t stage, bool babyAsAdult)
{
    if (babyAsAdult && (stage == 13 || stage == 14)) {
        stage = 0;
    } else if (stage < 0) {
        return nullptr;
    }
    if (stage < static_cast<int32_t>(mMotiveAds.size())) {
        cTSTreeTableAdVector &ads = mMotiveAds[stage];
        const int32_t count = static_cast<int32_t>(ads.size());
        if (count > 0 && motive < count) {
            return &ads[motive];
        }
    }
    return nullptr;
}

void cTSTreeTableEntry::SetMotiveAd(int32_t motive, int32_t stage, const cTSTreeTableAd *ad, bool trim)
{
    if (stage == 13 || stage == 14) {
        stage = 0;
    } else if (stage < 0 || stage > 4) {
        return;
    }
    if (static_cast<int32_t>(mMotiveAds.size()) <= stage) {
        if (ad->mSecond == 0 && ad->mThird == 0) {
            return;
        }
        mMotiveAds.resize(stage + 1);
    }
    cTSTreeTableAdVector &ads = mMotiveAds[stage];
    if (static_cast<int32_t>(ads.size()) <= motive) {
        if (ad->mSecond == 0 && ad->mThird == 0) {
            return;
        }
        ads.resize(motive + 1);
    }
    cTSTreeTableAd &target = ads[motive];
    target.mFirst = ad->mFirst;
    target.mSecond = ad->mSecond;
    target.mThird = ad->mThird;
    if (trim && ad->mSecond == 0 && ad->mThird == 0) {
        TrimMotiveAds(&mMotiveAds);
    }
}

cTSTreeTableAdVector cTSTreeTableEntry::GetMotiveAds(int32_t stage)
{
    if (stage >= 0 && stage < static_cast<int32_t>(mMotiveAds.size())) {
        return mMotiveAds[stage];
    }
    return cTSTreeTableAdVector();
}

// ---- trait advertisements (+0x1c) ----

cTSTraitAdList cTSTreeTableEntry::GetTraitAds(uint32_t stage)
{
    if (stage - 5 < mTraitAds.size()) {
        return mTraitAds[stage - 5];
    }
    return cTSTraitAdList();
}

int16_t cTSTreeTableEntry::GetTraitAdValue(int32_t trait, uint32_t stage, uint32_t element, int32_t which)
{
    const uint32_t traitStage = stage - 5;
    if (traitStage < mTraitAds.size()) {
        cTSTraitAdList &list = mTraitAds[traitStage];
        if (trait < static_cast<int32_t>(list.size())) {
            cTSTraitAdVector &values = list[trait];
            if (element < values.size()) {
                return Value(values[element], which);
            }
        }
    }
    return 0;
}

void cTSTreeTableEntry::SetTraitAdValue(int32_t trait, uint32_t stage, uint32_t element, int32_t which, int16_t value, bool trim)
{
    const uint32_t traitStage = stage - 5;
    if (mTraitAds.size() <= traitStage) {
        if (value == 0) {
            return;
        }
        mTraitAds.resize(stage - 4);
    }
    cTSTraitAdList &list = mTraitAds[traitStage];
    if (static_cast<int32_t>(list.size()) <= trait) {
        if (value == 0) {
            return;
        }
        list.resize(trait + 1);
    }
    cTSTraitAdVector &values = list[trait];
    if (values.size() <= element) {
        if (value == 0) {
            return;
        }
        values.resize(element + 1);
    }
    cTSTraitAdValue &target = values[element];
    Value(target, which) = value;
    if (trim && target.mFirst == 0 && target.mSecond == 0 && target.mThird == 0) {
        TrimTraitAds(&mTraitAds);
    }
}

// ---- scaling and scoring ----

void cTSTreeTableEntry::ScaleAds(void *person, float factor, uint32_t motive)
{
    if (person == nullptr || factor == 1.0f) {
        return;
    }
    if (static_cast<uint8_t>(PersonUsesMotiveAds(person)) != 0) {
        if (motive < 0x10) {
            ScaleMotiveMin(person, factor, motive);
            return;
        }
        for (uint32_t each = 0; static_cast<int32_t>(each) < 0x10; ++each) {
            if (!IsUnadvertised(each) && each != 4) {
                cTSTreeTableAd *const ad = GetMotiveAd(each, PersonStageA(person), true, false);
                if (ad != nullptr) {
                    ad->mSecond = static_cast<int16_t>(static_cast<int32_t>(static_cast<float>(ad->mSecond) * factor));
                }
            }
        }
        return;
    }
    if (motive < 0x10) {
        ScaleTraitAds(person, factor, static_cast<int32_t>(motive));
        return;
    }
    for (uint32_t each = 0; static_cast<int32_t>(each) < 0x10; ++each) {
        if (!IsUnadvertised(each) && each != 4) {
            ScaleTraitAds(person, factor, static_cast<int32_t>(each));
        }
    }
}

void cTSTreeTableEntry::ScaleMotiveMin(void *person, float factor, uint32_t motive)
{
    cTSTreeTableAd *const ad = GetMotiveAd(motive, PersonStageA(person), true, false);
    if (ad != nullptr) {
        ad->mSecond = static_cast<int16_t>(static_cast<int32_t>(static_cast<float>(ad->mSecond) * factor));
    }
}

// The person's trait mask (data 0xc7) picks, per element (at most five), which of the three values is scaled.
void cTSTreeTableEntry::ScaleTraitAds(void *person, float factor, int32_t trait)
{
    const uint32_t traitStage = PersonStageA(person) - 5;
    if (traitStage >= mTraitAds.size()) {
        return;
    }
    cTSTraitAdList &list = mTraitAds[traitStage];
    if (trait >= static_cast<int32_t>(list.size()) || list[trait].empty()) {
        return;
    }
    const int32_t mask = PersonData(person, 0xc7, 1);
    for (uint32_t element = 0, bit = 0; static_cast<int32_t>(bit) < 10; ++element, bit += 2) {
        cTSTraitAdVector &values = list[trait];
        if (element < values.size()) {
            int32_t which = 2;
            if ((mask & (1 << bit)) != 0) {
                which = 0;
            } else if ((mask & (1 << (bit + 1))) != 0) {
                which = 1;
            }
            int16_t &value = Value(values[element], which);
            value = static_cast<int16_t>(static_cast<int32_t>(static_cast<float>(value) * factor));
        }
    }
}

// The person's advertisement value for a motive, in thousandths: from the motive ads of its life stage (slot 20) when it
// uses them (slot 38), else the sum of its trait values picked by its trait mask (person data 0xc7).
float cTSTreeTableEntry::GetAdValue(void *person, uint32_t motive)
{
    if (IsUnadvertised(motive) || motive == 4) {
        return 0.0f;
    }
    if (static_cast<uint8_t>(PersonUsesMotiveAds(person)) == 0) {
        if (motive == 13) {
            return 0.0f;
        }
        const uint32_t traitStage = PersonStageB(person) - 5;
        if (traitStage >= mTraitAds.size()) {
            return 0.0f;
        }
        const cTSTraitAdList &list = mTraitAds[traitStage];
        if (static_cast<int32_t>(motive) >= static_cast<int32_t>(list.size())) {
            return 0.0f;
        }
        const int32_t mask = PersonData(person, 0xc7, 1);
        return SumPicked(list[motive], static_cast<uint32_t>(mask)) * 0.001f;
    }
    if (motive == 1) {
        return 0.0f;
    }
    uint32_t stage = PersonStageB(person);
    if (stage == 13 || stage == 14) {
        stage = 0;
    }
    if (stage >= mMotiveAds.size()) {
        return 0.0f;
    }
    const cTSTreeTableAdVector &ads = mMotiveAds[stage];
    if (static_cast<int32_t>(motive) >= static_cast<int32_t>(ads.size())) {
        return 0.0f;
    }
    const cTSTreeTableAd &ad = ads[motive];
    if (ad.mFirst == 0) {
        return static_cast<float>(static_cast<int16_t>(ad.mThird + ad.mSecond)) * 0.001f;
    }
    const MotiveTableRow &row = MotiveRow(ad.mFirst);
    int32_t personality = 0;
    if (row.personDataIndex <= 0xe6) {
        personality = PersonData(person, row.personDataIndex, 1);
    }
    const int32_t weight = row.inverted != 0 ? 1000 - personality : personality;
    return (static_cast<float>(weight) * 0.001f * static_cast<float>(ad.mThird) + static_cast<float>(ad.mSecond)) * 0.001f;
}

// The ad's score in thousandths: min + delta, or with a personality type min + delta * (the person's personality value,
// from `personalityMap` (std::map<int, int>), / 1000, inverted for some types).
float cTSTreeTableEntry::GetMotiveScore(uint32_t motive, uint32_t stage, void *personalityMap)
{
    if (IsUnadvertised(motive) || motive == 4 || motive == 1 || stage >= mMotiveAds.size()) {
        return 0.0f;
    }
    const cTSTreeTableAdVector &ads = mMotiveAds[stage];
    if (static_cast<int32_t>(motive) >= static_cast<int32_t>(ads.size())) {
        return 0.0f;
    }
    const cTSTreeTableAd &ad = ads[motive];
    if (ad.mFirst == 0) {
        return static_cast<float>(static_cast<int16_t>(ad.mThird + ad.mSecond)) * 0.001f;
    }
    const MotiveTableRow &row = MotiveRow(ad.mFirst);
    int32_t personality = 0;
    if (row.personDataIndex <= 0xe6) {
        const auto &values = *static_cast<const std::map<int32_t, int32_t> *>(personalityMap);
        const auto found = values.find(static_cast<int32_t>(row.personDataIndex));
        if (found != values.end()) {
            personality = found->second;
        }
    }
    const int32_t weight = row.inverted != 0 ? 1000 - personality : personality;
    return (static_cast<float>(weight) * 0.001f * static_cast<float>(ad.mThird) + static_cast<float>(ad.mSecond)) * 0.001f;
}

// The trait advertisement for a motive at a trait life stage (5 and up), the values picked by `mask`, in thousandths. (The
// original's last multiply is x87, left unrounded in st0: the product of two floats is exact there, so a caller that
// stores it, as callers do, gets this single-precision product.)
float cTSTreeTableEntry::GetTraitAdFor(uint32_t motive, uint32_t stage, uint32_t mask)
{
    if (IsUnadvertised(motive) || motive == 13 || motive == 4) {
        return 0.0f;
    }
    const uint32_t traitStage = stage - 5;
    if (traitStage >= mTraitAds.size()) {
        return 0.0f;
    }
    const cTSTraitAdList &list = mTraitAds[traitStage];
    if (static_cast<int32_t>(motive) >= static_cast<int32_t>(list.size())) {
        return 0.0f;
    }
    return SumPicked(list[motive], mask) * 0.001f;
}

// ---- whole stages ----

void cTSTreeTableEntry::CopyStageToAll(uint32_t stage, bool trim)
{
    if (stage < 5) {
        if (stage < mMotiveAds.size()) {
            const cTSTreeTableAdVector copy(mMotiveAds[stage]);
            if (mMotiveAds.size() < 5) {
                mMotiveAds.resize(5);
            }
            for (uint32_t each = 0; each < 5; ++each) {
                if (each != stage) {
                    mMotiveAds[each] = copy;
                }
            }
        } else {
            ClearMotiveAds();
        }
    } else if (stage - 5 < 8) {
        const uint32_t traitStage = stage - 5;
        if (traitStage < mTraitAds.size()) {
            const cTSTraitAdList copy(mTraitAds[traitStage]);
            if (mTraitAds.size() < 8) {
                mTraitAds.resize(8);
            }
            for (uint32_t each = 0; each < 8; ++each) {
                if (each != traitStage) {
                    mTraitAds[each] = copy;
                }
            }
        } else {
            ClearTraitAds();
        }
    }
    if (trim) {
        Trim();
    }
}

void cTSTreeTableEntry::ClearAds()
{
    ClearMotiveAds();
    ClearTraitAds();
}

void cTSTreeTableEntry::ClearStage(uint32_t stage, bool trim)
{
    if (stage < 5) {
        if (stage < mMotiveAds.size()) {
            mMotiveAds[stage].clear();
        }
    } else if (stage - 5 <= 7) {
        if (stage - 5 < mTraitAds.size()) {
            mTraitAds[stage - 5].clear();
        }
    }
    if (trim) {
        Trim();
    }
}

void cTSTreeTableEntry::ClearMotiveAds() { mMotiveAds.clear(); }
void cTSTreeTableEntry::ClearTraitAds() { mTraitAds.clear(); }

void cTSTreeTableEntry::Trim()
{
    TrimMotiveAds(&mMotiveAds);
    TrimTraitAds(&mTraitAds);
}

bool cTSTreeTableEntry::IsAllZero(const cTSTraitAdVector *values)
{
    for (const cTSTraitAdValue &value : *values) {
        if (value.mFirst != 0 || value.mSecond != 0 || value.mThird != 0) {
            return false;
        }
    }
    return true;
}

namespace {
bool IsEmptyAd(const cTSTreeTableAd &ad)
{
    return ad.mSecond == 0 && ad.mThird == 0;
}
} // namespace

// Drops the trailing stages whose ads are all empty (no minimum, no delta), then each stage's trailing empty ads.
void cTSTreeTableEntry::TrimMotiveAds(cTSTreeTableAdStages *stages)
{
    const int32_t count = static_cast<int32_t>(stages->size());
    int32_t empty = 0;
    for (int32_t stage = count - 1; stage >= 0; --stage) {
        bool allEmpty = true;
        for (const cTSTreeTableAd &ad : (*stages)[stage]) {
            if (!IsEmptyAd(ad)) {
                allEmpty = false;
                break;
            }
        }
        if (!allEmpty) {
            break;
        }
        ++empty;
    }
    if (empty != 0) {
        stages->resize(count - empty);
    }
    for (int32_t stage = static_cast<int32_t>(stages->size()) - 1; stage >= 0; --stage) {
        cTSTreeTableAdVector &ads = (*stages)[stage];
        const int32_t ads_count = static_cast<int32_t>(ads.size());
        int32_t trailing = 0;
        for (int32_t i = ads_count - 1; i >= 0 && IsEmptyAd(ads[i]); --i) {
            ++trailing;
        }
        if (trailing != 0) {
            ads.resize(ads_count - trailing);
        }
    }
}

// The same for the trait ads, one level deeper: trailing all-zero stages, then per stage the trailing all-zero traits,
// then per trait the trailing all-zero values.
void cTSTreeTableEntry::TrimTraitAds(cTSTraitAdStages *stages)
{
    const int32_t count = static_cast<int32_t>(stages->size());
    int32_t empty = 0;
    for (int32_t stage = count - 1; stage >= 0; --stage) {
        bool allZero = true;
        for (const cTSTraitAdVector &values : (*stages)[stage]) {
            if (!IsAllZero(&values)) {
                allZero = false;
                break;
            }
        }
        if (!allZero) {
            break;
        }
        ++empty;
    }
    if (empty != 0) {
        stages->resize(count - empty);
    }
    for (int32_t stage = static_cast<int32_t>(stages->size()) - 1; stage >= 0; --stage) {
        cTSTraitAdList &list = (*stages)[stage];
        const int32_t traits = static_cast<int32_t>(list.size());
        int32_t trailing = 0;
        for (int32_t trait = traits - 1; trait >= 0 && IsAllZero(&list[trait]); --trait) {
            ++trailing;
        }
        if (trailing != 0) {
            list.resize(traits - trailing);
        }
        for (int32_t trait = static_cast<int32_t>(list.size()) - 1; trait >= 0; --trait) {
            cTSTraitAdVector &values = list[trait];
            const int32_t valueCount = static_cast<int32_t>(values.size());
            int32_t zero = 0;
            for (int32_t i = valueCount - 1;
                 i >= 0 && values[i].mFirst == 0 && values[i].mSecond == 0 && values[i].mThird == 0; --i) {
                ++zero;
            }
            if (zero != 0) {
                values.resize(valueCount - zero);
            }
        }
    }
}

// ---- recent uses (+0x70) ----

void cTSTreeTableEntry::RecordUse(int32_t tick, uint16_t actor, uint32_t objectId)
{
    if (objectId < mUses.size()) {
        mUses[objectId].mTick = tick;
        mUses[objectId].mActor = static_cast<int16_t>(actor);
        return;
    }
    if (objectId > mUses.size()) {
        mUses.resize(objectId);
    }
    mUses.push_back(cTSTreeTableUse{tick, static_cast<int16_t>(actor), 0});
}

bool cTSTreeTableEntry::WasRecentlyUsed(int32_t tick, int32_t actor, uint32_t objectId)
{
    return objectId < mUses.size() && mUses[objectId].mTick == tick && mUses[objectId].mActor == actor;
}

void cTSTreeTableEntry::ClearUses() { mUses.clear(); }

// ---- copying from another entry ----

// Another cTSTreeTableEntry (it answers the class id) is copied field by field; any other implementation through its
// interface, slot by slot (its slot 57 creates the ads it is asked for).
void cTSTreeTableEntry::CopyFrom(cITSTreeTableEntry *other)
{
    cTSTreeTableEntry *source = nullptr;
    if (!other->QueryInterface(kClassId, reinterpret_cast<void **>(&source))) {
        mCheckTreeId = other->GetCheckTreeId();
        mActionTreeId = other->GetActionTreeId();
        ClearAds();
        for (int32_t stage = 0; stage < 5; ++stage) {
            const int32_t count = static_cast<int32_t>(other->GetMotiveAdCount(stage));
            for (int32_t motive = 0; motive < count; ++motive) {
                cTSTreeTableAd *const ad = other->GetMotiveAd(motive, stage, true, true);
                if (ad != nullptr) {
                    SetMotiveAd(motive, stage, ad, false);
                }
            }
        }
        for (int32_t trait = 0; trait < 0x10; ++trait) {
            for (uint32_t stage = 5; static_cast<int32_t>(stage) < 13; ++stage) {
                for (uint32_t element = 0; static_cast<int32_t>(element) < 5; ++element) {
                    for (int32_t which = 0; which < 3; ++which) {
                        SetTraitAdValue(trait, stage, element, which, other->GetTraitAdValue(trait, stage, element, which),
                                        false);
                    }
                }
            }
        }
        SetIndex(other->GetIndex());
        SetAttenuation(other->GetAttenuation());
        SetCustomAttenuation(other->GetCustomAttenuation());
        mFlags = other->GetFlags();
        SetJoinIndex(other->GetJoinIndex());
        SetName(reinterpret_cast<const cIGZString &>(*other->GetNameString()));
        SetUIDisplayType(other->GetUIDisplayType());
        SetFacialAnimType(other->GetFacialAnimType());
        SetAutonomyThreshold(other->GetAutonomyThreshold());
        SetMemoryIterativeMultiplier(other->GetMemoryIterativeMultiplier());
        SetIconGuid(other->GetIconGuid());
        SetIconTableIndex(other->GetIconTableIndex());
    } else {
        mCheckTreeId = source->mCheckTreeId;
        mActionTreeId = source->mActionTreeId;
        mMotiveAds = source->mMotiveAds;
        mTraitAds = source->mTraitAds;
        mIndex = source->mIndex;
        mAttenuation = source->mAttenuation;
        mCustomAttenuation = source->mCustomAttenuation;
        mFlags = source->mFlags;
        mJoinIndex = source->mJoinIndex;
        mName.mString.mString = source->mName.mString.mString;
        mUIDisplayType = source->mUIDisplayType;
        mFacialAnimType = source->mFacialAnimType;
        mAutonomyThreshold = source->mAutonomyThreshold;
        mMemoryIterativeMultiplier = source->mMemoryIterativeMultiplier;
        mIconGuid = source->mIconGuid;
        mIconTableIndex = source->mIconTableIndex;
    }
    Trim();
    if (source != nullptr) {
        source->Release();
    }
}

// ---- adapters (the RVA hooks) ----

using Adapters = cTSTreeTableEntryAdapters;

Self *__fastcall Adapters::Construct(Self *memory, void *, uint32_t) { return ::new (memory) Self; }
Self *__fastcall Adapters::CopyConstruct(Self *memory, void *, const Self *other) { return ::new (memory) Self(*other); }
void __fastcall Adapters::Destruct(Self *self, void *) { self->Self::~Self(); }

Self *__fastcall Adapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    self->Self::~Self();
    if ((flags & 1) != 0) {
        ::operator delete(self, sizeof(Self));
    }
    return self;
}

#define TS_TREE_TABLE_ADAPTER(R, Name, params, args)                                                                          \
    R __fastcall Adapters::Name params                                                                                        \
    {                                                                                                                         \
        [[msvc::forceinline_calls]] return self->Self::Name args;                                                            \
    }
TS_TREE_TABLE_ADAPTER(bool, QueryInterface, (Self *self, void *, uint32_t riid, void **out), (riid, out))
TS_TREE_TABLE_ADAPTER(uint32_t, Release, (Self *self, void *), ())
TS_TREE_TABLE_ADAPTER(bool, Write, (Self *self, void *, ifaces::cIGZOStream *stream), (stream))
TS_TREE_TABLE_ADAPTER(bool, Read, (Self *self, void *, ifaces::cIGZIStream *stream), (stream))
TS_TREE_TABLE_ADAPTER(bool, ResetToDefaults, (Self *self, void *, uint32_t unused), (unused))
TS_TREE_TABLE_ADAPTER(void, CopyFrom, (Self *self, void *, cITSTreeTableEntry *other), (other))
TS_TREE_TABLE_ADAPTER(bool, HasFlags, (Self *self, void *, uint32_t mask), (mask))
TS_TREE_TABLE_ADAPTER(void, SetMaskedFlags, (Self *self, void *, uint32_t values, uint32_t mask), (values, mask))
TS_TREE_TABLE_ADAPTER(void, SetFlags, (Self *self, void *, uint32_t bits, bool on), (bits, on))
TS_TREE_TABLE_ADAPTER(bool, GetCanJoin, (Self *self, void *), ())
TS_TREE_TABLE_ADAPTER(void, SetCanJoin, (Self *self, void *, bool on), (on))
TS_TREE_TABLE_ADAPTER(bool, GetRunImmediately, (Self *self, void *), ())
TS_TREE_TABLE_ADAPTER(void, SetRunImmediately, (Self *self, void *, bool on), (on))
TS_TREE_TABLE_ADAPTER(uint32_t, GetAgeFlags, (Self *self, void *), ())
TS_TREE_TABLE_ADAPTER(bool, GetAllowSubQueue, (Self *self, void *), ())
TS_TREE_TABLE_ADAPTER(void, SetAllowSubQueue, (Self *self, void *, bool on), (on))
TS_TREE_TABLE_ADAPTER(bool, GetAllowOnSelf, (Self *self, void *), ())
TS_TREE_TABLE_ADAPTER(void, SetAllowOnSelf, (Self *self, void *, bool on), (on))
TS_TREE_TABLE_ADAPTER(bool, GetOverrideDisable, (Self *self, void *), ())
TS_TREE_TABLE_ADAPTER(void, SetOverrideDisable, (Self *self, void *, bool on), (on))
TS_TREE_TABLE_ADAPTER(bool, GetPreferredByFurious, (Self *self, void *), ())
TS_TREE_TABLE_ADAPTER(void, SetPreferredByFurious, (Self *self, void *, bool on), (on))
TS_TREE_TABLE_ADAPTER(bool, GetTypeSubQueue, (Self *self, void *), ())
TS_TREE_TABLE_ADAPTER(void, SetTypeSubQueue, (Self *self, void *, bool on), (on))
TS_TREE_TABLE_ADAPTER(bool, GetResetMotiveDecay, (Self *self, void *), ())
TS_TREE_TABLE_ADAPTER(void, SetResetMotiveDecay, (Self *self, void *, bool on), (on))
TS_TREE_TABLE_ADAPTER(bool, GetParentalControl, (Self *self, void *), ())
TS_TREE_TABLE_ADAPTER(void, SetParentalControl, (Self *self, void *, bool on), (on))
TS_TREE_TABLE_ADAPTER(bool, GetFlag5, (Self *self, void *), ())
TS_TREE_TABLE_ADAPTER(void, SetFlag5, (Self *self, void *, bool on), (on))
TS_TREE_TABLE_ADAPTER(bool, GetDebugOnly, (Self *self, void *), ())
TS_TREE_TABLE_ADAPTER(void, SetDebugOnly, (Self *self, void *, bool on), (on))
TS_TREE_TABLE_ADAPTER(bool, GetAutoFirstSelect, (Self *self, void *), ())
TS_TREE_TABLE_ADAPTER(void, SetAutoFirstSelect, (Self *self, void *, bool on), (on))
TS_TREE_TABLE_ADAPTER(bool, IsAutonomyDisabled, (Self *self, void *), ())
TS_TREE_TABLE_ADAPTER(bool, GetFlag22, (Self *self, void *), ())
TS_TREE_TABLE_ADAPTER(void, SetFlag22, (Self *self, void *, bool on), (on))
TS_TREE_TABLE_ADAPTER(uint32_t, GetMotiveAdCount, (Self *self, void *, int32_t stage), (stage))
TS_TREE_TABLE_ADAPTER(int16_t *, GetMotiveAdDelta, (Self *self, void *, uint32_t motive, uint32_t stage), (motive, stage))
TS_TREE_TABLE_ADAPTER(int16_t *, GetMotiveAdMin, (Self *self, void *, uint32_t motive, uint32_t stage), (motive, stage))
TS_TREE_TABLE_ADAPTER(int16_t *, GetMotiveAdType, (Self *self, void *, uint32_t motive, uint32_t stage), (motive, stage))
TS_TREE_TABLE_ADAPTER(bool, HasAdvertisement, (Self *self, void *, uint32_t motive, uint32_t stage), (motive, stage))
TS_TREE_TABLE_ADAPTER(cTSTreeTableAd *, GetMotiveAd,
                      (Self *self, void *, uint32_t motive, uint32_t stage, bool babyAsAdult, bool create),
                      (motive, stage, babyAsAdult, create))
TS_TREE_TABLE_ADAPTER(cTSTreeTableAd *, FindMotiveAd, (Self *self, void *, int32_t motive, int32_t stage, bool babyAsAdult),
                      (motive, stage, babyAsAdult))
TS_TREE_TABLE_ADAPTER(void, SetMotiveAd, (Self *self, void *, int32_t motive, int32_t stage, const cTSTreeTableAd *ad, bool trim),
                      (motive, stage, ad, trim))
TS_TREE_TABLE_ADAPTER(int16_t, GetTraitAdValue,
                      (Self *self, void *, int32_t trait, uint32_t stage, uint32_t element, int32_t which),
                      (trait, stage, element, which))
TS_TREE_TABLE_ADAPTER(void, SetTraitAdValue,
                      (Self *self, void *, int32_t trait, uint32_t stage, uint32_t element, int32_t which, int16_t value,
                       bool trim),
                      (trait, stage, element, which, value, trim))
TS_TREE_TABLE_ADAPTER(void, ScaleAds, (Self *self, void *, void *person, float factor, uint32_t motive),
                      (person, factor, motive))
TS_TREE_TABLE_ADAPTER(float, GetAdValue, (Self *self, void *, void *person, uint32_t motive), (person, motive))
TS_TREE_TABLE_ADAPTER(float, GetMotiveScore, (Self *self, void *, uint32_t motive, uint32_t stage, void *personalityMap),
                      (motive, stage, personalityMap))
TS_TREE_TABLE_ADAPTER(float, GetTraitAdFor, (Self *self, void *, uint32_t motive, uint32_t stage, uint32_t mask),
                      (motive, stage, mask))
TS_TREE_TABLE_ADAPTER(void, CopyStageToAll, (Self *self, void *, uint32_t stage, bool trim), (stage, trim))
TS_TREE_TABLE_ADAPTER(void, ClearAds, (Self *self, void *), ())
TS_TREE_TABLE_ADAPTER(void, ClearStage, (Self *self, void *, uint32_t stage, bool trim), (stage, trim))
TS_TREE_TABLE_ADAPTER(void, ClearMotiveAds, (Self *self, void *), ())
TS_TREE_TABLE_ADAPTER(void, ClearTraitAds, (Self *self, void *), ())
TS_TREE_TABLE_ADAPTER(void, RecordUse, (Self *self, void *, int32_t tick, uint16_t actor, uint32_t objectId),
                      (tick, actor, objectId))
TS_TREE_TABLE_ADAPTER(bool, WasRecentlyUsed, (Self *self, void *, int32_t tick, int32_t actor, uint32_t objectId),
                      (tick, actor, objectId))
TS_TREE_TABLE_ADAPTER(void, ClearUses, (Self *self, void *), ())
TS_TREE_TABLE_ADAPTER(float, GetAttenuationValue, (Self *self, void *, bool alternate), (alternate))
TS_TREE_TABLE_ADAPTER(void, SetCustomAttenuation, (Self *self, void *, float value), (value))
TS_TREE_TABLE_ADAPTER(bool, GetName, (Self *self, void *, cIGZString *out), (out))
TS_TREE_TABLE_ADAPTER(cIGZString *, SetName, (Self *self, void *, const cIGZString &name), (name))
TS_TREE_TABLE_ADAPTER(void, Trim, (Self *self, void *), ())
TS_TREE_TABLE_ADAPTER(bool, GetAffectsOwnership, (Self *self, void *), ())
TS_TREE_TABLE_ADAPTER(void, SetAffectsOwnership, (Self *self, void *, bool on), (on))
#undef TS_TREE_TABLE_ADAPTER

cTSTreeTableAdVector *__fastcall Adapters::GetMotiveAds(Self *self, void *, cTSTreeTableAdVector *out, int32_t stage)
{
    return ::new (out) cTSTreeTableAdVector(self->Self::GetMotiveAds(stage));
}

cTSTraitAdList *__fastcall Adapters::GetTraitAds(Self *self, void *, cTSTraitAdList *out, uint32_t stage)
{
    return ::new (out) cTSTraitAdList(self->Self::GetTraitAds(stage));
}
