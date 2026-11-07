#include "ts/cTSEffectsCollection.h"

#include <cctype>
#include <new>

// cTSEffectsCollection (lane IT). The bodies follow the originals' order of container operations: the containers are the
// game's own standard-library types, so the same operations leave the same nodes, buckets and capacities.

namespace {

// Lower-cases a string in place, as the original's loops (tolower on each sign-extended char).
void LowerInPlace(std::string &text)
{
    for (char &c : text) c = static_cast<char>(tolower(c));
}

} // namespace

// 0xbd7210 (returns this): the effects map (8 buckets), the effects and both lists empty, 13 description tables, the flag at
// +0x58 cleared, the scratch key empty.
cTSEffectsCollection::cTSEffectsCollection() : mRefCount(0), mTables(kTypeCount), mFlag58(false) {}

// 0xbd7670: members destroyed in reverse order (scratch key, triggers, aliases, tables, effects, effects map).
cTSEffectsCollection::~cTSEffectsCollection() = default;

// 0xbd8820 (cdecl): operator new(0x74), the constructor; null when the allocation fails.
cTSEffectsCollection *cTSEffectsCollection::Create()
{
    return new (std::nothrow) cTSEffectsCollection;
}

// 0xbd8a40 (ret 8): 1 and the collection's id give this, counted through slot 1.
bool cTSEffectsCollection::QueryInterface(uint32_t riid, void **out)
{
    if (out && (riid == 1 || riid == kInterfaceId)) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

// Folded 0x148fb0: ++count, the new count.
uint32_t cTSEffectsCollection::AddRef()
{
    return ++mRefCount;
}

// Folded 0x244060: --count; at zero the scalar deleting destructor (slot 12) with 1, and 0.
uint32_t cTSEffectsCollection::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

// Folded 0x140fe0 (`ret 0`): nothing to link in a plain collection.
void cTSEffectsCollection::Link() {}

void cTSEffectsCollection::SetScratchLower(const char *name)
{
    mScratch.assign(name);
    LowerInPlace(mScratch);
}

// 0xbd8680: empties everything and leaves 13 empty description tables (capacities kept).
void cTSEffectsCollection::Clear()
{
    mEffectsByName.clear();
    mEffects.clear();
    mTables.clear();
    mTables.resize(kTypeCount);
    mMessageTriggers.clear();
    mMessageAliases.clear();
}

// 0xbd87b0 (ret 8): walks the effects of that name; the variant compared as the stored 16 bits widened.
int32_t cTSEffectsCollection::FindEffectIndex(const std::string &name, uint32_t variant)
{
    const auto range = mEffectsByName.equal_range(name);
    for (auto it = range.first; it != range.second; ++it)
        if (mEffects[it->second].mVariant == variant) return static_cast<int32_t>(it->second);
    return -1;
}

// 0xbd7c00 (ret 8): adds (or replaces) the effect `name` with the effect's variant, then resolves the components'
// descriptions by type and name. A new effect is appended and indexed under the lower-case name.
void cTSEffectsCollection::AddEffect(const char *name, const EffectEntry *effect)
{
    SetScratchLower(name);
    int32_t index;
    {
        const std::string key(mScratch.c_str());
        index = FindEffectIndex(key, effect->mVariant);
    }
    if (index < 0) {
        index = static_cast<int32_t>(mEffects.size());
        mEffects.resize(index + 1);
        const std::pair<const std::string, uint32_t> entry(mScratch, static_cast<uint32_t>(index));
        mEffectsByName.insert(entry);
    }
    EffectEntry &stored = mEffects[index];
    stored = *effect;
    for (auto component = stored.mComponents.begin(); component != stored.mComponents.end(); ++component) {
        if (component->mIndex >= 0) continue;
        LowerInPlace(component->mName);
        const int32_t type = component->mType;
        if (type == 0) {
            component->mIndex = 0;
        } else if (type > 0) {
            EffectDescriptionTable &table = mTables[type - 1];
            const auto found = table.mNames.find(component->mName);
            if (found != table.mNames.end()) component->mIndex = static_cast<int32_t>(found->second);
        }
    }
}

// 0xbd8970 (ret 8): true when an effect has that name; `out` (when given) gets a copy of the first one found.
bool cTSEffectsCollection::GetEffect(const char *name, EffectEntry *out)
{
    SetScratchLower(name);
    const auto found = mEffectsByName.find(mScratch);
    if (found == mEffectsByName.end()) return false;
    if (out) *out = mEffects[found->second];
    return true;
}

// 0xbd7840 (ret 8): stores the description in its type's table and returns its index. Unnamed descriptions are appended;
// a new name is appended and indexed; a known name replaces the description at its index.
uint32_t cTSEffectsCollection::AddDescription(const char *name, cITSComponentDescription *description)
{
    EffectDescriptionTable &table = mTables[description->GetType() - 1];
    if (!name) {
        const uint32_t index = static_cast<uint32_t>(table.mDescriptions.size());
        table.mDescriptions.push_back(EffectDescriptionRef(description));
        return index;
    }
    SetScratchLower(name);
    const auto found = table.mNames.find(mScratch);
    if (found == table.mNames.end()) {
        const uint32_t index = static_cast<uint32_t>(table.mDescriptions.size());
        table.mDescriptions.push_back(EffectDescriptionRef(description));
        table.mNames[mScratch] = index;
        return index;
    }
    const uint32_t index = found->second;
    table.mDescriptions[index] = description;
    return index;
}

// 0xbd8890 (ret 0xc): true when the type's table has a description of that name; `out` (when given) gets it, counted.
bool cTSEffectsCollection::GetDescription(const char *name, int32_t type, cITSComponentDescription **out)
{
    EffectDescriptionTable &table = mTables[type - 1];
    SetScratchLower(name);
    const auto found = table.mNames.find(mScratch);
    if (found == table.mNames.end()) return false;
    if (out) {
        *out = table.mDescriptions[found->second].Get();
        (*out)->AddRef();
    }
    return true;
}

// 0xbd7b40 (ret 0xc): appends (effect, first, second), the name copied from the C string.
void cTSEffectsCollection::AddMessageAlias(const char *effect, uint32_t first, uint32_t second)
{
    mMessageAliases.push_back(EffectMessageAlias{std::string(effect), first, second});
}

// 0xbd7ef0 (ret 4): appends a copy of the trigger.
void cTSEffectsCollection::AddMessageTrigger(const EffectMessageTrigger &trigger)
{
    mMessageTriggers.push_back(trigger);
}

// 0xbd8b50 (ret 0xc): gives the first effect named `name` the variant (its low 16 bits), and indexes it under `newName`
// (or `name`) too unless an effect with that name and variant exists already. False when no effect has the name; when one
// with the new name and variant exists, whether it is the same effect.
bool cTSEffectsCollection::SetEffectVariant(const char *name, uint32_t variant, const char *newName)
{
    SetScratchLower(name);
    const auto range = mEffectsByName.equal_range(mScratch);
    // The original counts the range's distinct indices (a node counts when it is the first or its index differs from the
    // first's) and gives up at zero, so only an empty range fails.
    uint32_t distinct = 0;
    for (auto it = range.first; it != range.second; ++it)
        if (it == range.first || it->second != range.first->second) ++distinct;
    if (distinct == 0) return false;
    const uint32_t index = range.first->second;
    mEffects[index].mVariant = static_cast<uint16_t>(variant);
    if (newName) mScratch.assign(newName);
    LowerInPlace(mScratch);
    int32_t existing;
    {
        const std::string key(mScratch.c_str());
        existing = FindEffectIndex(key, variant);
    }
    if (existing >= 0) return static_cast<uint32_t>(existing) == index;
    const std::pair<const std::string, uint32_t> entry(mScratch, index);
    mEffectsByName.insert(entry);
    return true;
}

// ---- merging (cTSEffectsResource's slot 5) ----

// 0xbd7f30 (ret 4)
void cTSEffectsCollection::MergeInto(cTSEffectsCollection *target)
{
    if (target->mEffects.empty()) {
        mEffectsByName.swap(target->mEffectsByName);
        mEffects.swap(target->mEffects);
        mTables.swap(target->mTables);
        mMessageTriggers.swap(target->mMessageTriggers);
        mMessageAliases.swap(target->mMessageAliases);
        return;
    }
    if (mFlag58)
        AddAllTo(target);
    else
        AppendTo(target);
}

// 0xbd8080 (ret 4)
void cTSEffectsCollection::AddAllTo(cTSEffectsCollection *target)
{
    for (uint32_t t = 0; t < mTables.size(); ++t) {
        EffectDescriptionTable &table = mTables[t];
        for (auto it = table.mNames.begin(); it != table.mNames.end(); ++it)
            target->AddDescription(it->first.c_str(), table.mDescriptions[it->second].Get());
    }
    for (auto it = mEffectsByName.begin(); it != mEffectsByName.end(); ++it)
        target->AddEffect(it->first.c_str(), &mEffects[it->second]);
}

// 0xbd8160 (ret 4)
void cTSEffectsCollection::AppendTo(cTSEffectsCollection *target)
{
    // Where each table's descriptions start in the target.
    std::vector<uint32_t> firstDescription(mTables.size());
    for (uint32_t t = 0; t < mTables.size(); ++t) {
        std::vector<EffectDescriptionRef> &to = target->mTables[t].mDescriptions;
        firstDescription[t] = static_cast<uint32_t>(to.size());
        const std::vector<EffectDescriptionRef> &from = mTables[t].mDescriptions;
        to.insert(to.end(), from.begin(), from.end());
    }
    // This collection's components then index past the target's (type 0: an effect index; 1..13: a description index).
    const uint32_t firstEffect = static_cast<uint32_t>(target->mEffects.size());
    for (uint32_t e = 0; e < mEffects.size(); ++e) {
        std::vector<EffectComponent> &components = mEffects[e].mComponents;
        for (auto c = components.begin(); c != components.end(); ++c) {
            if (c->mType == 0)
                c->mIndex += static_cast<int32_t>(firstEffect);
            else if (c->mType > 0)
                c->mIndex += static_cast<int32_t>(firstDescription[c->mType - 1]);
        }
    }
    target->mEffects.insert(target->mEffects.end(), mEffects.begin(), mEffects.end());
    // Each name replaces the target's first entry of that name with the same variant.
    for (auto it = mEffectsByName.begin(); it != mEffectsByName.end(); ++it) {
        const uint16_t variant = mEffects[it->second].mVariant;
        auto range = target->mEffectsByName.equal_range(std::string(it->first.c_str()));
        for (auto m = range.first; m != range.second; ++m) {
            if (static_cast<int16_t>(target->mEffects[m->second].mVariant) == static_cast<int16_t>(variant)) {
                target->mEffectsByName.erase(m);
                break;
            }
        }
        const std::pair<std::string, uint32_t> entry(it->first, it->second + firstEffect);
        target->mEffectsByName.emplace(entry);
    }
    target->mMessageTriggers.insert(target->mMessageTriggers.end(), mMessageTriggers.begin(), mMessageTriggers.end());
    target->mMessageAliases.insert(target->mMessageAliases.end(), mMessageAliases.begin(), mMessageAliases.end());
}

// ---- adapters (RVA hooks) ----

using Adapters = cTSEffectsCollectionAdapters;

cTSEffectsCollection *__fastcall Adapters::Construct(Self *memory, void *)
{
    return ::new (memory) cTSEffectsCollection;
}

void __fastcall Adapters::Destruct(Self *self, void *)
{
    self->cTSEffectsCollection::~cTSEffectsCollection();
}

cTSEffectsCollection *__fastcall Adapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    self->cTSEffectsCollection::~cTSEffectsCollection();
    if (flags & 1) ::operator delete(self, sizeof(cTSEffectsCollection));
    return self;
}

bool __fastcall Adapters::QueryInterface(Self *self, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return self->cTSEffectsCollection::QueryInterface(riid, out);
}

void __fastcall Adapters::Clear(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->cTSEffectsCollection::Clear();
}

void __fastcall Adapters::AddEffect(Self *self, void *, const char *name, const EffectEntry *effect)
{
    [[msvc::forceinline_calls]] self->cTSEffectsCollection::AddEffect(name, effect);
}

bool __fastcall Adapters::GetEffect(Self *self, void *, const char *name, EffectEntry *out)
{
    [[msvc::forceinline_calls]] return self->cTSEffectsCollection::GetEffect(name, out);
}

uint32_t __fastcall Adapters::AddDescription(Self *self, void *, const char *name, cITSComponentDescription *description)
{
    [[msvc::forceinline_calls]] return self->cTSEffectsCollection::AddDescription(name, description);
}

bool __fastcall Adapters::GetDescription(Self *self, void *, const char *name, int32_t type, cITSComponentDescription **out)
{
    [[msvc::forceinline_calls]] return self->cTSEffectsCollection::GetDescription(name, type, out);
}

void __fastcall Adapters::AddMessageAlias(Self *self, void *, const char *effect, uint32_t first, uint32_t second)
{
    [[msvc::forceinline_calls]] self->cTSEffectsCollection::AddMessageAlias(effect, first, second);
}

void __fastcall Adapters::AddMessageTrigger(Self *self, void *, const EffectMessageTrigger &trigger)
{
    [[msvc::forceinline_calls]] self->cTSEffectsCollection::AddMessageTrigger(trigger);
}

bool __fastcall Adapters::SetEffectVariant(Self *self, void *, const char *name, uint32_t variant, const char *newName)
{
    [[msvc::forceinline_calls]] return self->cTSEffectsCollection::SetEffectVariant(name, variant, newName);
}

int32_t __fastcall Adapters::FindEffectIndex(Self *self, void *, const std::string &name, uint32_t variant)
{
    [[msvc::forceinline_calls]] return self->cTSEffectsCollection::FindEffectIndex(name, variant);
}

void __fastcall Adapters::MergeInto(Self *self, void *, Self *target)
{
    [[msvc::forceinline_calls]] self->cTSEffectsCollection::MergeInto(target);
}

void __fastcall Adapters::AddAllTo(Self *self, void *, Self *target)
{
    [[msvc::forceinline_calls]] self->cTSEffectsCollection::AddAllTo(target);
}

void __fastcall Adapters::AppendTo(Self *self, void *, Self *target)
{
    [[msvc::forceinline_calls]] self->cTSEffectsCollection::AppendTo(target);
}
