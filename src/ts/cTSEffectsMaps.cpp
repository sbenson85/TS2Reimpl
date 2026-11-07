#include "ts/cTSEffectsMaps.h"

#include <new>

cTSEffectsMaps::cTSEffectsMaps() : mFlag(false) {}

cTSEffectsMaps::~cTSEffectsMaps() = default;

cTSEffectsMaps *__cdecl cTSEffectsMaps::Create() { return new (std::nothrow) cTSEffectsMaps; }

bool cTSEffectsMaps::QueryInterface(uint32_t riid, void **out)
{
    if (riid == kIid) {
        *out = static_cast<cITSEffectsMaps *>(this);
        AddRef();
        return true;
    }
    return rz::cRZUnknown::QueryInterface(riid, out);
}

bool cTSEffectsMaps::Init()
{
    mFlag = false;
    return true;
}

bool cTSEffectsMaps::Shutdown()
{
    mMaps.clear();
    return true;
}

uint32_t cTSEffectsMaps::GetCount() { return static_cast<uint32_t>(mMaps.size()); }

void cTSEffectsMaps::SetMap(uint32_t index, cIGZUnknown *map)
{
    // An index of -1 wraps to an empty vector, and writes nothing.
    if (index >= mMaps.size()) mMaps.resize(static_cast<uint32_t>(index + 1));
    if (static_cast<int32_t>(index) >= 0) mMaps[index].Assign(map);
}

cIGZUnknown *cTSEffectsMaps::GetMap(int32_t index)
{
    if (index < 0 || static_cast<uint32_t>(index) >= mMaps.size()) return nullptr;
    return mMaps[index].Get();
}

cTSEffectsMaps *__fastcall cTSEffectsMapsAdapters::Construct(void *self, void *) { return ::new (self) cTSEffectsMaps; }
