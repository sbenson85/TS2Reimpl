// cTSGameTipService (lane KX). Layout and slots: src/ts/cTSGameTipService.h.
#include "ts/cTSGameTipService.h"

#include "game/vtable.h"
#include "ifaces/cIGZPersistResourceManager.h"
#include "ifaces/cITSPersistSystem.h"
#include "ifaces/cITSStringSet.h"
#include "rz/ServiceGetters.h"
#include "rz/StringKeyParser.h" // ParseNumber
#include "ts/cTSCustomContentSupport.h" // ContentString
#include "ts/cTSGameTipResourceFactory.h"

#include <new>

namespace {

// The persist system (service 0x056b906e, cITSPersistSystem 0x9af5e924) from its cached getter (0xb390c0).
ifaces::cITSPersistSystem *PersistSystem()
{
    return As<ifaces::cITSPersistSystem>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>());
}

ifaces::cIGZPersistResourceManager *ResourceManager(ifaces::cITSPersistSystem *persist)
{
    return reinterpret_cast<ifaces::cIGZPersistResourceManager *>(persist->GetResourceManager());
}

const Key16 *AsKey16(const cGZPersistResourceKey *key) { return reinterpret_cast<const Key16 *>(key); }

// A tip record's key in `group`.
cGZPersistResourceKey TipKey(uint32_t group, uint32_t instance) { return {cTSGameTip::kType, group, instance, 0}; }

// The persist resource face of a tip we made.
ifaces::cIGZPersistResource *AsResource(cTSGameTip *tip)
{
    return reinterpret_cast<ifaces::cIGZPersistResource *>(static_cast<cIGZPersistResource *>(tip));
}

// The tip records' keys (16 bytes each), from the allocator service.
using TipKeys = std::vector<cGZPersistResourceKey, GZServiceAllocator<cGZPersistResourceKey>>;

// CreateDefaultTips: how many tips of categories 0, 1 and 2 it makes, then the string table's rows it makes tips from
// (three strings each: string id 2, string id 1, and a number in hex).
constexpr int32_t kDefaultTipCounts[3] = {7, 10, 9};
constexpr uint32_t kFirstTipRow = 0x5a;
constexpr uint32_t kLastTipRow = 0x87;
constexpr uint32_t kTableTipKind = 3;

} // namespace

// 0xcb59c0: the base part (id 0x5181421b, priority 0), no strings, segment or group, the first instance 1.
cTSGameTipService::cTSGameTipService() : rz::cRZSystemService(kServiceId, kPriority), mGroup(0), mNextInstance(1) {}

// 0xcb5a60: the segment, then the strings released (the members' destructors), then the base part's destructor.
cTSGameTipService::~cTSGameTipService() noexcept = default;

// 0xcb5ce0
cTSGameTipService *__cdecl cTSGameTipService::Create() { return new (std::nothrow) cTSGameTipService; }

// ---- cIGZUnknown ----

// 0xcb6320: cITSGameTipService -> +0x1c (counted through the primary face's AddRef), else the service's.
bool cTSGameTipService::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kIid) return rz::cRZSystemService::QueryInterface(riid, out);
    *out = static_cast<cITSGameTipService *>(this);
    AddRef();
    return true;
}

uint32_t cTSGameTipService::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cTSGameTipService::Release() { return rz::cRZSystemService::Release(); }

// ---- cIGZSystemService ----

// 0xcb6170: the tip resource factory registered with the persist system's resource manager (factory id 0x318277de, type
// 0xb1827a47; the factory counted around it, the result ignored), then Load. False without the persist system or its
// manager.
bool cTSGameTipService::Init()
{
    if (!rz::cRZSystemService::Init()) return false;
    ifaces::cITSPersistSystem *persist = PersistSystem();
    if (!persist) return false;
    ifaces::cIGZPersistResourceManager *manager = ResourceManager(persist);
    if (!manager) return false;
    const cRZAutoRefCount<cTSGameTipResourceFactory> factory(new (std::nothrow) cTSGameTipResourceFactory);
    manager->RegisterFactory(cTSGameTipResourceFactory::kFactoryId, cTSGameTip::kType, factory.Get());
    Load();
    return true;
}

// 0xcb6de0: the strings, then the segment dropped (each nulled before its release); the base's Shutdown.
bool cTSGameTipService::Shutdown()
{
    mStrings.AsPPVoid();
    mSegment.AsPPVoid();
    return rz::cRZSystemService::Shutdown();
}

// ---- cITSGameTipService ----

// 0xcb5e70: the string table's string count (language 0); 0 without a table.
uint32_t cTSGameTipService::GetTextCount()
{
    if (!mStrings) return 0;
    return mStrings->GetStringCount(0);
}

// 0xcb5dd0: the tip record `instance` of the group as a cITSGameTip into `out` (nulled first); false without a group,
// the persist system or the resource.
bool cTSGameTipService::GetTip(uint32_t instance, cITSGameTip **out)
{
    *out = nullptr;
    if (!mGroup) return false;
    const cGZPersistResourceKey key = TipKey(mGroup, instance);
    ifaces::cITSPersistSystem *persist = PersistSystem();
    return persist && persist->GetResource(AsKey16(&key), cTSGameTip::kIid, reinterpret_cast<void **>(out), 0, 0);
}

// 0xcb5e90: every tip record of the tip type (in any group), as cITSGameTip, that `filter` (when given) includes, appended
// to `out` (emptied first unless `append`). The result is the number of records listed, not of tips added; 0 without the
// persist system or its manager.
uint32_t cTSGameTipService::GetTips(GameTipList *out, cGameTipFilterIface *filter, bool append)
{
    if (!append) out->clear();
    ifaces::cITSPersistSystem *persist = PersistSystem();
    if (!persist) return 0;
    ifaces::cIGZPersistResourceManager *manager = ResourceManager(persist);
    if (!manager) return 0;
    TipKeys keys;
    const uint32_t count = manager->GetResourceKeyListOfType(&keys, cTSGameTip::kType);
    for (uint32_t i = 0; i < count; ++i) {
        GameTipRef tip;
        if (persist->GetResource(AsKey16(keys.data() + i), cTSGameTip::kIid, tip.AsPPVoid(), 0, 0) &&
            (!filter || filter->IsIncluded(tip.Get())))
            out->push_back(tip);
    }
    return count;
}

// 0xcb5e40: string `id` of the table (any language) into `out`; false without a table.
bool cTSGameTipService::GetText(uint32_t id, cIGZString *out)
{
    return mStrings && mStrings->GetStringInto(out, id, -1);
}

// 0xcb5b30: a new tip (the next instance, category 0x11) in the group, uncounted.
cITSGameTip *cTSGameTipService::CreateTip()
{
    cTSGameTip *tip = nullptr;
    if (void *memory = ::operator new(sizeof(cTSGameTip), std::nothrow)) tip = ::new (memory) cTSGameTip(mNextInstance++, cTSGameTip::kUserTipKind);
    // ORIGINAL-BUG: a failed allocation is not checked: SetGroup is called on the null tip.
    static_cast<cIGZPersistResource *>(tip)->SetGroup(mGroup);
    return tip ? tip->Tip() : nullptr;
}

// 0xcb5c70: the tip record `instance` deleted from the persist system; true (nothing to do) when there is no such tip,
// false without the persist system or when the delete fails.
bool cTSGameTipService::DeleteTip(uint32_t instance)
{
    if (!HasTip(instance)) return true;
    ifaces::cITSPersistSystem *persist = PersistSystem();
    cGZPersistResourceKey key = TipKey(mGroup, instance);
    return persist && persist->DeleteResource(&key);
}

// 0xcb6490: the tip's three strings set to the given texts (each one whose current text differs, or is missing, is set
// in the table), the table committed when any was set, then the tip saved (slot 10). False without a tip or a table;
// otherwise SaveTip's byte as it is.
uint8_t cTSGameTipService::SetTipTexts(cITSGameTip *tip, const cIGZString *text0, const cIGZString *text1, const cIGZString *text2)
{
    if (!tip || !mStrings) return 0;
    // The table's string `id` replaced by `text` unless it already holds that text.
    auto update = [this](ContentString &current, uint32_t id, const cIGZString *text) {
        if (mStrings->GetStringInto(current.Interface(), id, -1) && current.s.IsEqualForward(*text, true)) return false;
        mStrings->SetString(const_cast<cIGZString *>(text), id, -1);
        return true;
    };
    ContentString current0;
    const bool changed0 = update(current0, static_cast<uint32_t>(tip->GetStringId0()), text0);
    ContentString current1;
    const bool changed1 = update(current1, static_cast<uint32_t>(tip->GetStringId1()), text1);
    ContentString current2;
    const bool changed2 = update(current2, static_cast<uint32_t>(tip->GetStringId2()), text2);
    if (changed0 || changed1 || changed2) mStrings->Commit();
    return SaveTip(tip);
}

// 0xcb63e0: the tip's persist resource face saved by the persist system (into the segment, null: its choice); false
// without a tip, that face or the persist system; otherwise SaveResource's byte as it is.
uint8_t cTSGameTipService::SaveTip(cITSGameTip *tip)
{
    if (!tip) return 0;
    cRZAutoRefCount<cIGZPersistResource> resource;
    tip->QueryInterface(cGZPersistResource::kIid, resource.AsPPVoid());
    uint8_t result = 0;
    if (resource) {
        if (ifaces::cITSPersistSystem *persist = PersistSystem())
            result = persist->SaveResource(reinterpret_cast<ifaces::cIGZPersistResource *>(resource.Get()), mSegment.Get());
    }
    return result;
}

// 0xcb6dc0: the group set and loaded, once (ignored while a group is set).
void cTSGameTipService::SetTipGroup(uint32_t group)
{
    if (mGroup) return;
    mGroup = group;
    Load();
}

// ---- helpers ----

// 0xcb5d50: whether GetTip (through this service's slot 4) finds the tip; the tip it got is released.
bool cTSGameTipService::HasTip(uint32_t instance)
{
    cRZAutoRefCount<cITSGameTip> tip;
    return TipService()->GetTip(instance, reinterpret_cast<cITSGameTip **>(tip.AsPPVoid()));
}

// 0xcb66a0: the tip records listed; with some, the group is the first one's and the next instance one past the highest,
// and the strings are loaded; with none, the strings are loaded (with the group set) and the default tips made.
void cTSGameTipService::Load()
{
    ifaces::cITSPersistSystem *persist = PersistSystem();
    if (!persist) return;
    ifaces::cIGZPersistResourceManager *manager = ResourceManager(persist);
    if (!manager) return;
    TipKeys keys;
    const uint32_t count = manager->GetResourceKeyListOfType(&keys, cTSGameTip::kType);
    if (count == 0) {
        LoadStrings();
        CreateDefaultTips();
        return;
    }
    mGroup = keys[0].group;
    uint32_t highest = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (highest < keys.data()[i].instance) highest = keys.data()[i].instance;
    }
    mNextInstance = highest + 1;
    LoadStrings();
}

// 0xcb6250: with a group, the strings (STR# 0x12d of the group) from the resource manager, as a private resource, into
// mStrings (the old ones dropped first).
void cTSGameTipService::LoadStrings()
{
    if (!mGroup) return;
    ifaces::cITSPersistSystem *persist = PersistSystem();
    if (!persist) return;
    ifaces::cIGZPersistResourceManager *manager = ResourceManager(persist);
    if (!manager) return;
    const cGZPersistResourceKey key{kStringSetType, mGroup, kStringSetInstance, 0};
    manager->GetPrivateResource(&key, kStringSetIid, mStrings.AsPPVoid(), 0, 0);
}

// 0xcb67e0: with strings and the persist system, the default tips saved: 7, 10 and 9 tips of categories 0, 1 and 2, then
// one tip for each row of the string table's rows 0x5a..0x87 (step 3: string id 1 the row + 1, string id 2 the row, the
// value the row + 2's text as a hex number, "0x" added when missing). Each saved tip takes the next instance; the first
// save that fails ends it.
void cTSGameTipService::CreateDefaultTips()
{
    if (!mStrings) return;
    ifaces::cITSPersistSystem *persist = PersistSystem();
    if (!persist) return;
    // Makes a tip, has `setup` fill it and saves it; false when the save failed.
    auto save = [this, persist](uint32_t kind, auto setup) {
        cTSGameTip *made = nullptr;
        if (void *memory = ::operator new(sizeof(cTSGameTip), std::nothrow)) made = ::new (memory) cTSGameTip(mNextInstance, kind);
        // ORIGINAL-BUG: a failed allocation is not checked (the calls below go to the null tip).
        const cRZAutoRefCount<cIGZPersistResource> tip(made);
        static_cast<cIGZPersistResource *>(made)->SetGroup(mGroup);
        setup(made->Tip());
        if (!persist->SaveResource(AsResource(made), mSegment.Get())) return false;
        ++mNextInstance;
        return true;
    };
    for (uint32_t category = 0; category < 3; ++category) {
        for (int32_t i = 0; i < kDefaultTipCounts[category]; ++i) {
            if (!save(category, [category](cITSGameTip *tip) { tip->SetCategory(static_cast<uint16_t>(category)); })) return;
        }
    }
    for (uint32_t row = kFirstTipRow; row <= kLastTipRow; row += 3) {
        const cIGZString *source = static_cast<const cIGZString *>(mStrings->GetString(row + 2, -1));
        const uint32_t length = source->Strlen();
        const char *data = source->Data();
        ContentString number;
        number.s.mString.assign(data, length);
        if (!number.s.StartsWith("0x", 2, true)) number.s.mString = "0x" + number.s.mString;
        const uint32_t value = StringKeyParser::ParseNumber(number.s.ToChar());
        if (!save(kTableTipKind, [row, value](cITSGameTip *tip) {
                tip->SetStringId1(static_cast<int16_t>(row + 1));
                tip->SetStringId2(static_cast<int16_t>(row));
                tip->SetValue(value);
            }))
            return;
    }
}

// 0xb39020
cITSGameTipService *GetGameTipService()
{
    return static_cast<cITSGameTipService *>(CachedServiceGetter<0x10faf8c, cTSGameTipService::kServiceId, cTSGameTipService::kIid>());
}
