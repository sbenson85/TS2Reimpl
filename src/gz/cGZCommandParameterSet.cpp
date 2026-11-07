#include "gz/cGZCommandParameterSet.h"

#include "rz/cRZString.h"

#include <cstring>
#include <iterator>
#include <new>

cGZCommandParameterSet::cGZCommandParameterSet() : mRefCount(0), mValues(), mDefault() {}

cGZCommandParameterSet::~cGZCommandParameterSet() = default;

bool cGZCommandParameterSet::QueryInterface(uint32_t iid, void **out)
{
    if (iid != 1 && iid != 0x4b8c39e4 && iid != 0x4b8c39e5) return false;
    *out = static_cast<cIGZCommandParameterSet *>(this);
    AddRef();
    return true;
}

uint32_t cGZCommandParameterSet::AddRef()
{
    return ++mRefCount;
}

uint32_t cGZCommandParameterSet::Release()
{
    if (mRefCount > 1) return --mRefCount;
    cGZCommandParameterSetAdapters::DeletingDestructor(this, nullptr, 1);
    return 0;
}

uint32_t cGZCommandParameterSet::GetParameterCount() const
{
    return static_cast<uint32_t>(mValues.size());
}

void cGZCommandParameterSet::SetParameterCount(uint32_t count)
{
    // ORIGINAL-BUG: if the largest key is UINT32_MAX and key zero exists, slot 8 wraps to zero;
    // repeated growth overwrites that entry forever without increasing the tree size.
    while (mValues.size() < count) {
        cRZVariant value;
        cGZCommandParameterSet::AppendParameter(&value);
    }
    while (mValues.size() > count) mValues.erase(std::prev(mValues.end()));
}

cRZVariant *cGZCommandParameterSet::GetParameter(uint32_t index)
{
    auto it = mValues.find(index);
    return it == mValues.end() ? &mDefault : &it->second;
}

cRZVariant *cGZCommandParameterSet::FindParameter(uint32_t index)
{
    auto it = mValues.find(index);
    return it == mValues.end() ? nullptr : &it->second;
}

void cGZCommandParameterSet::SetParameter(uint32_t index, const cIGZVariant *value)
{
    auto result = mValues.try_emplace(index);
    result.first->second.AssignFrom(*value);
}

void cGZCommandParameterSet::AppendParameter(const cIGZVariant *value)
{
    const uint32_t index = mValues.empty() ? 0 : mValues.rbegin()->first + 1;
    cGZCommandParameterSet::SetParameter(index, value);
}

void cGZCommandParameterSet::RemoveParameter(uint32_t index)
{
    mValues.erase(index);
}

void cGZCommandParameterSet::SetVersion(int32_t version)
{
    cRZVariant value(version);
    cGZCommandParameterSet::SetParameter(0x2b999d2b, &value);
}

bool cGZCommandParameterSet::FindText(int32_t index, const char *text, uint32_t flags)
{
    if (index == -2) {
        for (const auto &entry : mValues) {
            if (cGZCommandParameterSet::FindText(static_cast<int32_t>(entry.first), text, flags)) return true;
        }
        return false;
    }

    cRZVariant *value = cGZCommandParameterSet::FindParameter(static_cast<uint32_t>(index));
    if (value == nullptr) return false;
    cRZString converted;
    converted.Construct();
    const bool gotText = value->GetValString(*converted.AsInterface(), 0, ' ');
    bool found = false;
    if (gotText) {
        const int32_t at = converted.Find(text, 0, static_cast<uint8_t>(flags) != 0) /* bool: the callee reads the low byte */;
        found = at != -1 && converted.mString.size() == std::strlen(text);
    }
    converted.Destruct();
    return found;
}

bool cGZCommandParameterSet::IsSpecialParameter(uint32_t index, uint32_t type)
{
    return FindParameter(index) != nullptr && type == 0xff;
}

int32_t cGZCommandParameterSet::GetFirstParameterIndex() const
{
    return mValues.empty() ? -1 : static_cast<int32_t>(mValues.begin()->first);
}

int32_t cGZCommandParameterSet::GetNextParameterIndex(uint32_t index)
{
    auto it = mValues.find(index);
    if (it == mValues.end()) return -1;
    ++it;
    return it == mValues.end() ? -1 : static_cast<int32_t>(it->first);
}

bool __fastcall cGZCommandParameterSetAdapters::QueryInterface(cGZCommandParameterSet *self, void *, uint32_t iid, void **out)
{
    [[msvc::forceinline_calls]] return self->cGZCommandParameterSet::QueryInterface(iid, out);
}
uint32_t __fastcall cGZCommandParameterSetAdapters::GetParameterCount(cGZCommandParameterSet *self, void *)
{ [[msvc::forceinline_calls]] return self->cGZCommandParameterSet::GetParameterCount(); }

#define CGZ_ADAPTER_VOID1(Name, Type) \
void __fastcall cGZCommandParameterSetAdapters::Name(cGZCommandParameterSet *self, void *, Type value) \
{ [[msvc::forceinline_calls]] self->cGZCommandParameterSet::Name(value); }

CGZ_ADAPTER_VOID1(SetParameterCount, uint32_t)
CGZ_ADAPTER_VOID1(AppendParameter, const cIGZVariant *)
CGZ_ADAPTER_VOID1(RemoveParameter, uint32_t)
CGZ_ADAPTER_VOID1(SetVersion, int32_t)

#undef CGZ_ADAPTER_VOID1

cRZVariant *__fastcall cGZCommandParameterSetAdapters::GetParameter(cGZCommandParameterSet *self, void *, uint32_t index)
{ [[msvc::forceinline_calls]] return self->cGZCommandParameterSet::GetParameter(index); }
cRZVariant *__fastcall cGZCommandParameterSetAdapters::FindParameter(cGZCommandParameterSet *self, void *, uint32_t index)
{ [[msvc::forceinline_calls]] return self->cGZCommandParameterSet::FindParameter(index); }
void __fastcall cGZCommandParameterSetAdapters::SetParameter(cGZCommandParameterSet *self, void *, uint32_t index, const cIGZVariant *value)
{ [[msvc::forceinline_calls]] self->cGZCommandParameterSet::SetParameter(index, value); }
bool __fastcall cGZCommandParameterSetAdapters::FindText(cGZCommandParameterSet *self, void *, int32_t index, const char *text, uint32_t flags)
{ [[msvc::forceinline_calls]] return self->cGZCommandParameterSet::FindText(index, text, flags); }
bool __fastcall cGZCommandParameterSetAdapters::IsSpecialParameter(cGZCommandParameterSet *self, void *, uint32_t index, uint32_t type)
{ [[msvc::forceinline_calls]] return self->cGZCommandParameterSet::IsSpecialParameter(index, type); }
int32_t __fastcall cGZCommandParameterSetAdapters::GetFirstParameterIndex(cGZCommandParameterSet *self, void *)
{ [[msvc::forceinline_calls]] return self->cGZCommandParameterSet::GetFirstParameterIndex(); }
int32_t __fastcall cGZCommandParameterSetAdapters::GetNextParameterIndex(cGZCommandParameterSet *self, void *, uint32_t index)
{ [[msvc::forceinline_calls]] return self->cGZCommandParameterSet::GetNextParameterIndex(index); }
cGZCommandParameterSet *__fastcall cGZCommandParameterSetAdapters::DeletingDestructor(cGZCommandParameterSet *self, void *, uint32_t flags)
{
    self->cGZCommandParameterSet::~cGZCommandParameterSet();
    if (flags & 1) ::operator delete(self, sizeof(cGZCommandParameterSet));
    return self;
}
