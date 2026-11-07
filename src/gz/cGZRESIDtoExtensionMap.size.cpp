// cGZRESIDtoExtensionMap (standalone shape, slot-hooked; src/gz/cGZRESIDtoExtensionMap.h).
#include "gz/cGZRESIDtoExtensionMap.h"

#include "misc/HexParse.h"

#include <cstring>
#include <new>
#include <tuple>
#include <utility>

bool cGZRESIDtoExtensionMap::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != 1 && riid != cIGZPersistRESIDtoExtensionMap::kIid) return false;
    *ppv = static_cast<cIGZPersistRESIDtoExtensionMap *>(this);
    AddRef();
    return true;
}

uint32_t cGZRESIDtoExtensionMap::AddRef()
{
    return ++mRefCount;
}

uint32_t cGZRESIDtoExtensionMap::Release()
{
    if (mRefCount > 1) return --mRefCount;
    delete this;
    return 0;
}

bool cGZRESIDtoExtensionMap::AddExtension(uint32_t type, const cIGZString &extension)
{
    const uint32_t length = extension.Strlen();
    const char *text = extension.Data();
    mMap.emplace(std::piecewise_construct, std::forward_as_tuple(type), std::forward_as_tuple(text, length));
    return true;
}

bool cGZRESIDtoExtensionMap::GetTypeForExtension(const cIGZString &extension, uint32_t *type)
{
    const uint32_t length = extension.Strlen();
    for (const auto &entry : mMap) {
        const std::string &stored = entry.second.s.mString;
        const char *given = nullptr;
        uint32_t count = 0;
        if (stored.size() == length) {
            given = extension.Data();
            count = length;
        } else if (stored.size() + 1 == length && length != 0 && extension.Data()[0] == '.') {
            given = extension.Data() + 1;
            count = length - 1;
        } else {
            continue;
        }
        if (_strnicmp(given, stored.c_str(), count) == 0) {
            *type = entry.first;
            return true;
        }
    }
    // Not a known extension: the type id itself, as eight hexadecimal digits.
    const char *text = extension.ToChar();
    const uint32_t dot = text[0] == '.' ? 1 : 0;
    if (length - dot != kHexTypeLength) return false;
    *type = hexparse::ParseHex32(text + dot);
    return true;
}

bool cGZRESIDtoExtensionMap::GetExtensionForType(const uint32_t &type, cIGZString *extension)
{
    const auto found = mMap.find(type); // the first entry of that type
    if (found == mMap.end()) return false;
    extension->FromChar(found->second.s.mString.c_str());
    return true;
}

void cGZRESIDtoExtensionMap::Clear()
{
    mMap.clear();
}

void cGZRESIDtoExtensionMap::CopyFrom(const cIGZPersistRESIDtoExtensionMap &other)
{
    // The original reads the other map's tree directly: `other` must be a cGZRESIDtoExtensionMap.
    const auto &source = static_cast<const cGZRESIDtoExtensionMap &>(other);
    Clear();
    for (const auto &entry : source.mMap) AddExtension(entry.first, *entry.second.s.AsInterface());
}

uint32_t cGZRESIDtoExtensionMap::GetCount()
{
    return static_cast<uint32_t>(mMap.size());
}

// ---------------------------------------------------------------------------------------------
// Adapters (the slot hooks on the game's vtable; one statement each, the deleting destructor as standalone-shape.md)

bool __fastcall cGZRESIDtoExtensionMapAdapters::QueryInterface(cGZRESIDtoExtensionMap *self, void *, uint32_t riid,
                                                               void **ppv)
{
    [[msvc::forceinline_calls]] return self->cGZRESIDtoExtensionMap::QueryInterface(riid, ppv);
}
bool __fastcall cGZRESIDtoExtensionMapAdapters::AddExtension(cGZRESIDtoExtensionMap *self, void *, uint32_t type,
                                                             const cIGZString *extension)
{
    [[msvc::forceinline_calls]] return self->cGZRESIDtoExtensionMap::AddExtension(type, *extension);
}
bool __fastcall cGZRESIDtoExtensionMapAdapters::GetTypeForExtension(cGZRESIDtoExtensionMap *self, void *,
                                                                    const cIGZString *extension, uint32_t *type)
{
    [[msvc::forceinline_calls]] return self->cGZRESIDtoExtensionMap::GetTypeForExtension(*extension, type);
}
bool __fastcall cGZRESIDtoExtensionMapAdapters::GetExtensionForType(cGZRESIDtoExtensionMap *self, void *,
                                                                    const uint32_t *type, cIGZString *extension)
{
    [[msvc::forceinline_calls]] return self->cGZRESIDtoExtensionMap::GetExtensionForType(*type, extension);
}
void __fastcall cGZRESIDtoExtensionMapAdapters::Clear(cGZRESIDtoExtensionMap *self, void *)
{
    [[msvc::forceinline_calls]] self->cGZRESIDtoExtensionMap::Clear();
}
void __fastcall cGZRESIDtoExtensionMapAdapters::CopyFrom(cGZRESIDtoExtensionMap *self, void *,
                                                         const cGZRESIDtoExtensionMap *other)
{
    [[msvc::forceinline_calls]] self->cGZRESIDtoExtensionMap::CopyFrom(*other);
}
cGZRESIDtoExtensionMap *__fastcall cGZRESIDtoExtensionMapAdapters::DeletingDestructor(cGZRESIDtoExtensionMap *self,
                                                                                    void *, uint32_t flags)
{
    [[msvc::forceinline_calls]] self->cGZRESIDtoExtensionMap::~cGZRESIDtoExtensionMap();
    if (flags & 1) ::operator delete(self, sizeof(cGZRESIDtoExtensionMap));
    return self;
}
