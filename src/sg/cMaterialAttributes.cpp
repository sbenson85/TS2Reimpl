// nGZSceneGraph::cMaterialAttributes (lane RO). See the header.
#include "sg/cMaterialAttributes.h"

#include "rz/StaticGetters2.h"

#include <algorithm>
#include <cstddef>
#include <string.h>

namespace nGZSceneGraph {

namespace {

// The group's order: the names, case-insensitively.
bool NameLess(const cMaterialAttributes::Attribute &a, const cMaterialAttributes::Attribute &b)
{
    return _stricmp(a.mName.c_str(), b.mName.c_str()) < 0;
}

static_assert(offsetof(cMaterialAttributes, mRefCount) == 4, "refcount");
static_assert(offsetof(cMaterialAttributes, mName) == 8, "name");
static_assert(offsetof(cMaterialAttributes, mGroups) == 0x20, "groups");

} // namespace

cMaterialAttributes::cMaterialAttributes() = default;

cMaterialAttributes::~cMaterialAttributes() = default;

bool cMaterialAttributes::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid != 1 && riid != kIidAttributes) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cMaterialAttributes::AddRef() { return ++mRefCount; }

uint32_t cMaterialAttributes::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

std::string *cMaterialAttributes::SetName(const char *name)
{
    mName.assign(name);
    return &mName;
}

void cMaterialAttributes::AddAttribute(const char *name, cIMaterialAttributeSource *source)
{
    Group &group = mGroups[source->GetGroupKey()];
    group.emplace_back();
    Attribute &attribute = group.back();
    attribute.mName.assign(name);
    attribute.mWidth = source->GetWidth();
    attribute.mData.resize(source->GetDataSize());
    source->FillData(attribute.mData.data());
    std::sort(group.begin(), group.end(), NameLess);
}

const char *cMaterialAttributes::GetName() { return mName.empty() ? nullptr : mName.c_str(); }

bool cMaterialAttributes::IsEmpty() { return mGroups.empty(); }

uint32_t cMaterialAttributes::GetGroupCount() { return uint32_t(mGroups.size()); }

uint32_t cMaterialAttributes::GetGroupKey(uint32_t n)
{
    auto it = mGroups.begin();
    for (; n != 0; --n) ++it;
    return it->first;
}

uint32_t cMaterialAttributes::GetAttributeCount(uint32_t key)
{
    auto it = mGroups.find(key);
    return it == mGroups.end() ? 0 : uint32_t(it->second.size());
}

uint32_t cMaterialAttributes::GetValueCount(uint32_t key)
{
    auto it = mGroups.find(key);
    return it == mGroups.end() ? 0 : uint32_t(it->second.size()) * it->second.front().mWidth;
}

uint32_t cMaterialAttributes::GetTotalValueCount()
{
    uint32_t total = 0;
    for (const auto &entry : mGroups) total += uint32_t(entry.second.size()) * entry.second.front().mWidth;
    return total;
}

uint32_t cMaterialAttributes::ValuesBefore(uint32_t key, std::map<uint32_t, Group>::iterator &at)
{
    uint32_t offset = 0;
    at = mGroups.begin();
    while (at != mGroups.end() && at->first != key) {
        offset += uint32_t(at->second.size()) * at->second.front().mWidth;
        ++at;
    }
    return offset;
}

AttributeRef *cMaterialAttributes::FindAttribute(AttributeRef *out, const char *name, uint32_t key)
{
    *out = {};
    std::map<uint32_t, Group>::iterator at;
    const uint32_t offset = ValuesBefore(key, at);
    if (at == mGroups.end()) return out;
    Group &group = at->second;
    const auto first = std::lower_bound(group.begin(), group.end(), name, [](const Attribute &a, const char *n) {
        return _stricmp(a.mName.c_str(), n) < 0;
    });
    if (first == group.end() || _stricmp(first->mName.c_str(), name) != 0) return out;
    const uint32_t index = uint32_t(first - group.begin());
    out->mName = first->mName.c_str();
    out->mIndex = index;
    out->mWidth = first->mWidth;
    out->mPosition = index * first->mWidth + offset;
    return out;
}

AttributeRef *cMaterialAttributes::GetAttribute(AttributeRef *out, uint32_t index, uint32_t key)
{
    *out = {};
    std::map<uint32_t, Group>::iterator at;
    const uint32_t offset = ValuesBefore(key, at);
    if (at == mGroups.end()) return out;
    const Attribute &attribute = at->second[index];
    out->mName = attribute.mName.c_str();
    out->mIndex = index;
    out->mWidth = attribute.mWidth;
    out->mPosition = attribute.mWidth * index + offset;
    return out;
}

uint32_t cMaterialAttributes::ReadAttribute(cIMaterialAttributeSource *source, uint32_t index)
{
    auto it = mGroups.find(source->GetGroupKey());
    if (it == mGroups.end()) return 0;
    return source->ConsumeData(it->second[index].mData.data());
}

cIMaterialAttributes *CreateMaterialAttributes() { return new cMaterialAttributes; }

ServiceCell *OpenNameCell(ServiceCell *cell, uint32_t name)
{
    if (name != 0)
        cell->Open(name, 0);
    else
        cell->mValue = 0xffffffffu;
    return cell;
}

} // namespace nGZSceneGraph
