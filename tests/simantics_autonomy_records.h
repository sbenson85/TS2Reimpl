#pragma once
// The record vectors of the FindBestAction tests: random ActionRecords / ScoreRecords / RecentActions built in real
// std::vectors inside the person object (their heap addresses differ between the two worlds, so a run reduces them to a
// description in the trace and destroys them), and the helpers to describe and tear them down.
#include "simantics_autonomy_world.h"

#include "game_allocator.h"
#include "rz/GeneralAllocator.h"

#include "simantics/FindBestAction.h"
#include "simantics/PrimPerson.h"

namespace {

// The game's global allocator (the image's object at 0x10cb098, unconstructed in the harness) built for the length of a test,
// so the original's RZMemory::Malloc / Free and ours (the recent-action vector's allocator) work on the same blocks.
// If an earlier test left it live it is used as it is (its state changes with every allocation: it is not saved and put back).
struct GameAllocatorFixture {
    GeneralAllocator *allocator;
    bool built = false;
    GameAllocatorFixture() : allocator(GameAddress<GeneralAllocator>(0x10cb098))
    {
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(allocator);
        bool zero = true;
        for (size_t i = 0; i < sizeof(GeneralAllocator); ++i) zero = zero && bytes[i] == 0;
        if (zero) {
            allocator->Construct(nullptr, 0, 0, 0, nullptr, nullptr);
            built = true;
        }
    }
    ~GameAllocatorFixture()
    {
        if (built) difftest_alloc::ShutdownAndRelease(allocator);
    }
};

constexpr int kHeld = 12; // first of the fakes that stand for held interactions (kHeld .. kHeld + 3)
constexpr int kHeldCount = 4;

// While building, calls into fakes must neither throw nor depend on the (absent) world.
struct QuietBuild {
    uint32_t savedThrow;
    QuietBuild() : savedThrow(gThrowAt) { gThrowAt = 0; }
    ~QuietBuild() { gThrowAt = savedThrow; }
};

PrimPersonObject *Person(WorldP &w)
{
    return reinterpret_cast<PrimPersonObject *>(w.obj);
}

uint32_t TextCode(const std::string &s)
{
    uint32_t h = 2166136261u;
    for (char c : s) h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
    return h ^ static_cast<uint32_t>(s.size());
}

float RandomScore(std::mt19937 &r)
{
    switch (r() % 9) {
    case 0: return 0.0f;
    case 1: return -1.0f;
    case 2: return 0.5f;
    case 3: return 0.5f + 5e-8f;  // within the tolerance of 0.5
    case 4: return 0.5f - 2e-7f;  // outside it
    case 5: return static_cast<float>(r() % 20) / 4.0f;
    default: return static_cast<float>(static_cast<int32_t>(r() % 2000) - 1000) / 37.0f;
    }
}

ActionRecord RandomAction(std::mt19937 &r, WorldP &w, void *held, bool allowNull = true)
{
    ActionRecord a;
    a.mStackObjectId = static_cast<uint16_t>(r());
    a.mEntryIndex = static_cast<uint16_t>(r() % 6);
    a.mScore = RandomScore(r);
    a.mParameterA = r();
    a.mParameterB = r();
    a.mParameterC = static_cast<int16_t>(r());
    a.mFlag12 = (r() % 4 == 0) ? 1 : 0;
    for (uint16_t &word : a.mArguments) word = static_cast<uint16_t>(r() % 5 == 0 ? 0xffff : r());
    a.mMotiveScore = RandomScore(r);
    a.mDistanceScale = RandomScore(r);
    a.mDistance = RandomScore(r);
    static const char *const names[] = {"", "Chat", "Watch TV", "A rather long interaction name that is certainly longer than sixteen", "x"};
    a.mName.mText = names[r() % 5];
    a.mChecked = (r() % 3 == 0) ? 1 : 0;
    a.mInteraction.Hold(allowNull && r() % 7 == 0 ? nullptr : held);
    return a;
}

ActionRecord RandomAction(std::mt19937 &r, WorldP &w)
{
    return RandomAction(r, w, &w.fk[kHeld + r() % kHeldCount]);
}

ScoreRecord RandomScoreRecord(std::mt19937 &r)
{
    ScoreRecord s;
    s.mScore = RandomScore(r);
    s.mFlags = r() % 32;
    s.mObjectBonus = RandomScore(r);
    s.mInteractionFactor = RandomScore(r);
    s.mInput = r();
    s.mObjectCategory = static_cast<int32_t>(r() % 5) - 1;
    s.mProximityFactor = RandomScore(r);
    for (uint32_t &v : s.mSkillValues) v = (r() % 4 == 0) ? 0xffffffffu : r();
    const uint32_t ads = r() % 4;
    for (uint32_t i = 0; i < ads; ++i) {
        TreeTableAd ad;
        ad.mFirst = r();
        ad.mSecond = static_cast<uint16_t>(r());
        s.mTableAds.push_back(ad);
    }
    const uint32_t groups = r() % 3;
    for (uint32_t i = 0; i < groups; ++i) {
        std::vector<TraitAdValue> group;
        const uint32_t n = r() % 4;
        for (uint32_t k = 0; k < n; ++k) {
            TraitAdValue v;
            v.mFirst = r();
            v.mSecond = static_cast<uint16_t>(r());
            group.push_back(v);
        }
        s.mTraitAds.push_back(std::move(group));
    }
    return s;
}

void Describe(const ActionRecord &a, std::vector<uint32_t> &out)
{
    out.push_back(a.mStackObjectId | (static_cast<uint32_t>(a.mEntryIndex) << 16));
    out.push_back(Bits(a.mScore));
    out.push_back(a.mParameterA);
    out.push_back(a.mParameterB);
    out.push_back(static_cast<uint16_t>(a.mParameterC) | (static_cast<uint32_t>(a.mFlag12) << 16) | (static_cast<uint32_t>(a.mChecked) << 24));
    for (int i = 0; i < 8; i += 2) out.push_back(a.mArguments[i] | (static_cast<uint32_t>(a.mArguments[i + 1]) << 16));
    out.push_back(Bits(a.mMotiveScore));
    out.push_back(Bits(a.mDistanceScale));
    out.push_back(Bits(a.mDistance));
    out.push_back(TextCode(a.mName.mText));
    out.push_back(a.mInteraction.Get() ? Off(a.mInteraction.Get()) : 0u);
}

void Describe(const ScoreRecord &s, std::vector<uint32_t> &out)
{
    out.push_back(Bits(s.mScore));
    out.push_back(s.mFlags);
    out.push_back(Bits(s.mObjectBonus));
    out.push_back(Bits(s.mInteractionFactor));
    out.push_back(s.mInput);
    out.push_back(static_cast<uint32_t>(s.mObjectCategory));
    out.push_back(Bits(s.mProximityFactor));
    for (uint32_t v : s.mSkillValues) out.push_back(v);
    out.push_back(static_cast<uint32_t>(s.mTableAds.size()));
    for (const TreeTableAd &ad : s.mTableAds) {
        out.push_back(ad.mFirst);
        out.push_back(ad.mSecond);
    }
    out.push_back(static_cast<uint32_t>(s.mTraitAds.size()));
    for (const std::vector<TraitAdValue> &group : s.mTraitAds) {
        out.push_back(static_cast<uint32_t>(group.size()));
        for (const TraitAdValue &v : group) {
            out.push_back(v.mFirst);
            out.push_back(v.mSecond);
        }
    }
}

void Describe(const RecentAction &a, std::vector<uint32_t> &out)
{
    out.push_back(a.mStackObjectId | (static_cast<uint32_t>(a.mEntryIndex) << 16));
    out.push_back(static_cast<uint32_t>(a.mExpiryTick));
    out.push_back(TextCode(a.mName.mText));
}

template <typename V> void DescribeVector(const V &v, std::vector<uint32_t> &out)
{
    out.push_back(0xa5a50000u | static_cast<uint32_t>(v.size()));
    for (const auto &e : v) Describe(e, out);
}

// Emplaces an empty vector of the right type over `field` (random bytes there before).
template <typename V> V &Fresh(V &field)
{
    return *new (&field) V();
}

// Destroys a vector emplaced with Fresh and zeroes its three words.
template <typename V> void Retire(V &field)
{
    field.~V();
    std::memset(&field, 0, sizeof(field));
}

} // namespace
