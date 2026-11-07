#include "ts/cTSEffectsResource.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "rz/cRZString.h"
#include "rz/cRZVariant.h"
#include "ts/cTSCompDescription.h"
#include "ts/cTSParticlesDescription.h"

#include <cstring>
#include <new>

// cTSEffectsResource (lane IT). The stream calls go through the streams' vtables (cIGZOStream: 7 SetUint16, 9 SetUint32,
// 14 the C string; cIGZIStream: 7 GetUint16, 9 GetUint32, 15 the cRZString, 19 the metadata). Calls into code nobody has
// replaced yet go through OriginalFunction (the status entries list them).

namespace {

void PutUint16(void *stream, uint16_t value)
{
    VirtualSlot<bool(__thiscall *)(void *, uint16_t)>(stream, 7)(stream, value);
}
void PutUint32(void *stream, uint32_t value)
{
    VirtualSlot<bool(__thiscall *)(void *, uint32_t)>(stream, 9)(stream, value);
}
void PutText(void *stream, const char *text)
{
    VirtualSlot<bool(__thiscall *)(void *, const char *)>(stream, 14)(stream, text);
}
void GetUint16(void *stream, uint16_t *value)
{
    VirtualSlot<bool(__thiscall *)(void *, uint16_t *)>(stream, 7)(stream, value);
}
void GetUint32(void *stream, uint32_t *value)
{
    VirtualSlot<bool(__thiscall *)(void *, uint32_t *)>(stream, 9)(stream, value);
}
void GetText(void *stream, cRZString *text)
{
    VirtualSlot<bool(__thiscall *)(void *, cRZString *)>(stream, 15)(stream, text);
}

// The text the reads go through (the original's is a function-local static shared by Read and 0xbd8a70; a string read that
// fails leaves the previous text in it, which both keep).
cRZString &ReadBuffer()
{
    struct Buffer {
        cRZString text;
        Buffer() { text.Construct(); } // the cRZString vtable, empty, count 0
    };
    static Buffer buffer;
    return buffer.text;
}

// The description factories by table (type - 1): each news its cTSCompDescriptionBaseT<D, type> (all ours: the particle
// kinds in src/ts/cTSParticlesDescription.h, the others in src/ts/cTSCompDescription.h).
cITSComponentDescription *CreateDescription(uint32_t table)
{
    switch (table) {
    case 0: return cTSParticlesComponentDescription::Create();     // particles (0xbe7ba0)
    case 1: return cTSMetaParticlesComponentDescription::Create(); // meta particles (0xbf1a70)
    case 2: return cTSDecalComponentDescription::Create();
    case 3: return cTSBrushComponentDescription::Create();
    case 4: return cTSScrubberComponentDescription::Create();
    case 5: return cTSSequenceComponentDescription::Create();
    case 6: return cTSSoundComponentDescription::Create();
    case 7: return cTSCameraComponentDescription::Create();
    case 8: return cTSGameComponentDescription::Create();
    case 9: return cTSModelComponentDescription::Create();
    case 10: return cTSScreenComponentDescription::Create();
    case 11: return cTSWaterComponentDescription::Create();
    default: return cTSLightComponentDescription::Create();
    }
}

} // namespace

// The same buffer for the description serializers (src/ts/cTSCompDescription.cpp), as the originals share theirs.
cRZString &EffectsReadBuffer()
{
    return ReadBuffer();
}

// ---- construction ----

// 0xbd2ba0: the collection, the persist resource, the name and the string list empty.
cTSEffectsResource::cTSEffectsResource() = default;

// 0xbd2da0: the string list, the name, then the persist resource and the collection.
cTSEffectsResource::~cTSEffectsResource() = default;

// 0xbd2f90 (cdecl)
cTSEffectsResource *cTSEffectsResource::Create()
{
    return new (std::nothrow) cTSEffectsResource;
}

// ---- cIGZUnknown ----

// 0xbd3090 (ret 8): the collection's id gives +4, the resource's +0 (both counted through slot 1); anything else is the
// persist face's (1 and its id give +0x78).
bool cTSEffectsResource::QueryInterface(uint32_t riid, void **out)
{
    if (riid == cTSEffectsCollection::kInterfaceId) {
        *out = static_cast<cTSEffectsCollection *>(this);
        AddRef();
        return true;
    }
    if (riid == kInterfaceId) {
        *out = static_cast<cITSEffectsResource *>(this);
        AddRef();
        return true;
    }
    return cGZPersistResource::QueryInterface(riid, out);
}

// 0xbd2ea0 / 0xbd4010: the persist face's count (atomic).
uint32_t cTSEffectsResource::AddRef()
{
    return cGZPersistResource::AddRef();
}

uint32_t cTSEffectsResource::Release()
{
    return cGZPersistResource::Release();
}

// ---- cITSEffectsResource ----

// 0xbd4190 (ret 4)
void cTSEffectsResource::SetName(const char *name)
{
    mName.assign(name, std::strlen(name));
}

// 0xbd3070
const char *cTSEffectsResource::GetName()
{
    return mName.c_str();
}

// 0xbd2ec0 (ret 4): the collection's merge.
void cTSEffectsResource::MergeInto(cTSEffectsCollection *target)
{
    cTSEffectsCollection::MergeInto(target);
}

// 0xbd41c0 (ret 4): version 0, the 13 description tables (count, then each description's slot 5); version 3, the effects
// (0xbf9ee0 each); the name index in list order ending with "end" and -1; the aliases; version 0, the triggers.
bool cTSEffectsResource::Write(void *stream)
{
    PutUint16(stream, 0);
    for (uint32_t t = 0; t < kTypeCount; ++t) {
        const std::vector<EffectDescriptionRef> &descriptions = mTables[t].mDescriptions;
        PutUint32(stream, static_cast<uint32_t>(descriptions.size()));
        for (uint32_t i = 0; i < descriptions.size(); ++i) {
            cITSComponentDescription *description = descriptions[i].Get();
            description->Write(stream);
        }
    }
    PutUint16(stream, 3);
    PutUint32(stream, static_cast<uint32_t>(mEffects.size()));
    for (uint32_t i = 0; i < mEffects.size(); ++i)
        OriginalFunction<void(__cdecl *)(void *, const EffectEntry *), 0xbf9ee0>()(stream, &mEffects[i]);
    for (auto it = mEffectsByName.begin(); it != mEffectsByName.end(); ++it) {
        PutText(stream, it->first.c_str());
        PutUint32(stream, it->second);
    }
    PutText(stream, "end");
    PutUint32(stream, 0xffffffffu);
    PutUint32(stream, static_cast<uint32_t>(mMessageAliases.size()));
    for (uint32_t i = 0; i < mMessageAliases.size(); ++i) {
        const EffectMessageAlias &alias = mMessageAliases[i];
        PutText(stream, alias.mEffect.c_str());
        PutUint32(stream, alias.mFirst);
        PutUint32(stream, alias.mSecond);
    }
    PutUint16(stream, 0);
    PutUint32(stream, static_cast<uint32_t>(mMessageTriggers.size()));
    for (uint32_t i = 0; i < mMessageTriggers.size(); ++i) WriteEffectTrigger(stream, &mMessageTriggers[i]);
    return true;
}

// 0xbd30f0 (ret 4): the reverse of Write. A first version other than 0 fails before anything is read. Each table is resized
// to its count and every entry replaced by a new description of the table's type that reads itself (slot 4); the second
// version goes to the stream as its metadata (a cRZVariant of it); the effects are read by 0xbf9da0 with it; the name index
// is read until "end" with -1 (entries added to the map); the aliases; the third version is the triggers'.
bool cTSEffectsResource::Read(void *stream)
{
    uint16_t version = 0;
    GetUint16(stream, &version);
    if (version != 0) return false;
    for (uint32_t t = 0; t < kTypeCount; ++t) {
        std::vector<EffectDescriptionRef> &descriptions = mTables[t].mDescriptions;
        uint32_t count = 0;
        GetUint32(stream, &count);
        descriptions.resize(count);
        for (uint32_t i = 0; i < descriptions.size(); ++i) {
            cITSComponentDescription *created = CreateDescription(t);
            descriptions[i] = created;
            cITSComponentDescription *description = descriptions[i].Get();
            description->Read(stream);
        }
    }
    GetUint16(stream, &version);
    void *metadata = ::operator new(0x14);
    if (metadata) metadata = static_cast<cIGZVariant *>(::new (metadata) cRZVariant(version));
    VirtualSlot<void(__thiscall *)(void *, void *)>(stream, 19)(stream, metadata);
    uint32_t count = 0;
    GetUint32(stream, &count);
    mEffects.resize(count);
    for (uint32_t i = 0; i < mEffects.size(); ++i)
        OriginalFunction<void(__cdecl *)(void *, uint32_t, EffectEntry *), 0xbf9da0>()(stream, version, &mEffects[i]);
    for (;;) {
        cRZString &text = ReadBuffer();
        GetText(stream, &text);
        std::string name(text.mString);
        uint32_t index = 0;
        GetUint32(stream, &index);
        if (name == "end" && index == 0xffffffffu) break;
        const std::pair<std::string, uint32_t> entry(name, index);
        mEffectsByName.emplace(entry);
    }
    GetUint32(stream, &count);
    mMessageAliases.resize(count);
    for (uint32_t i = 0; i < mMessageAliases.size(); ++i) {
        EffectMessageAlias &alias = mMessageAliases[i];
        cRZString &text = ReadBuffer();
        GetText(stream, &text);
        if (&alias.mEffect != &text.mString) alias.mEffect.assign(text.mString.c_str(), text.mString.size());
        GetUint32(stream, &alias.mFirst);
        GetUint32(stream, &alias.mSecond);
    }
    GetUint16(stream, &version);
    GetUint32(stream, &count);
    mMessageTriggers.resize(count);
    for (uint32_t i = 0; i < mMessageTriggers.size(); ++i) ReadEffectTrigger(stream, version, &mMessageTriggers[i]);
    return true;
}

// 0xbd3050
uint32_t cTSEffectsResource::GetClassId()
{
    return kClassId;
}

// ---- the trigger serializers ----

// 0xbd8dc0 (cdecl)
void WriteEffectTrigger(void *stream, const EffectMessageTrigger *trigger)
{
    PutUint32(stream, trigger->mMessage);
    PutText(stream, trigger->mEffect.c_str());
}

// 0xbd8a70 (cdecl)
void ReadEffectTrigger(void *stream, uint32_t, EffectMessageTrigger *trigger)
{
    GetUint32(stream, &trigger->mMessage);
    cRZString &text = ReadBuffer();
    GetText(stream, &text);
    if (&trigger->mEffect != &text.mString) trigger->mEffect.assign(text.mString.c_str(), text.mString.size());
}

// ---- adapters (RVA hooks) ----

using Adapters = cTSEffectsResourceAdapters;
using Res = cTSEffectsResource;

Res *__fastcall Adapters::Construct(Self *memory, void *)
{
    return ::new (memory) Res;
}

void __fastcall Adapters::Destruct(Self *self, void *)
{
    self->Res::~Res();
}

Res *__fastcall Adapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    self->Res::~Res();
    if (flags & 1) ::operator delete(self, sizeof(Res));
    return self;
}

#define IT_ADAPT(ret, name, params, args)                                                                                   \
    ret __fastcall Adapters::name params { [[msvc::forceinline_calls]] return self->Res::name args; }

IT_ADAPT(bool, QueryInterface, (Self * self, void *, uint32_t riid, void **out), (riid, out))
IT_ADAPT(uint32_t, AddRef, (Self * self, void *), ())
IT_ADAPT(uint32_t, Release, (Self * self, void *), ())
IT_ADAPT(void, SetName, (Self * self, void *, const char *name), (name))
IT_ADAPT(const char *, GetName, (Self * self, void *), ())
IT_ADAPT(void, MergeInto, (Self * self, void *, cTSEffectsCollection *target), (target))
IT_ADAPT(bool, Write, (Self * self, void *, void *stream), (stream))
IT_ADAPT(bool, Read, (Self * self, void *, void *stream), (stream))
IT_ADAPT(uint32_t, GetClassId, (Self * self, void *), ())
#undef IT_ADAPT
