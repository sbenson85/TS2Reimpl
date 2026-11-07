#include "gz/cGZRegistry.h"

#include "game/game.h"
#include "game/vtable.h"

#include <string>

// cGZRegistry slots 3..7: the registry operations on the tree.

namespace {

const std::string &SchemeSeparator() { return GameAddress<cRZString>(cGZRegistry::kSchemeSeparatorRva)->mString; }
const std::string &PathSeparator() { return GameAddress<cRZString>(cGZRegistry::kPathSeparatorRva)->mString; }

} // namespace

bool cGZRegistry::Add(const cIGZString *url)
{
    // The original builds a copy of the url, then three strings (scheme, host, path), splits the
    // copy and inserts one entry. Temporaries and their copies are not observable; kept in order.
    cGZRegistryString line(*url);
    cGZRegistryString scheme, path, host; // constructed in this order in the original (0x8c, 0x6c, 0x4c)
    std::string &s = line.Str().mString;
    const std::string &sep1 = SchemeSeparator();
    const std::string &sep2 = PathSeparator();

    // Up to "://". Without one, find gives npos and the remainder starts at npos + 3 = 2 (substr
    // throws out_of_range for text shorter than 2, as in the original).
    size_t pos = s.find(sep1, 0);
    {
        std::string part(s, 0, pos);
        scheme.Str().mString.append(part);
    }
    {
        std::string rest(s, sep1.size() + pos, std::string::npos);
        s = rest;
    }
    // Then the host up to the next "/" (assign(c_str) stops at a NUL), then the path segment.
    pos = s.find(sep2, 0);
    {
        std::string part(s, 0, pos);
        host.Str().mString.assign(part.c_str());
    }
    {
        std::string rest(s, sep2.size() + pos, std::string::npos);
        s = rest;
    }
    pos = s.find(sep2, 0);
    {
        std::string part(s, 0, pos);
        path.Str().mString.assign(part.c_str());
    }

    cGZRegistryMap::value_type value(scheme, cGZRegistryValues(host, path));
    Map().insert(value);
    return true;
}

bool cGZRegistry::RemoveEmpty(void *)
{
    // ORIGINAL-BUG: erases whatever find(empty key) returns. When no entry has an empty key that
    // is the head node (corrupting the tree); the argument is never used.
    cGZRegistryString key;
    cGZRegistryMap &map = Map();
    map.erase(map.find(key));
    return true;
}

bool cGZRegistry::Lookup(const char *scheme, const cIGZString *host, cIGZString *pathOut)
{
    if (!scheme) return false;
    cGZRegistryString key(scheme);
    cGZRegistryMap &map = Map();
    auto it = map.find(key);
    auto end = map.end();
    if (it == end) return false;
    do {
        // The original works on copies of the key and of the value pair (the pair copy is never
        // destroyed: ORIGINAL-BUG, a leak for strings over 15 characters; not reproduced). Their
        // vtables are the game's cRZString vtable, so the calls below go there too.
        cGZRegistryValues values(it->second);
        auto isEqual = VirtualSlot<bool(__thiscall *)(void *, const cIGZString *, bool)>(&values.first, 12);
        if (isEqual(&values.first, host, true)) {
            const std::string &second = values.second.Str().mString;
            VirtualCall<void(__thiscall *)(void *, const char *, uint32_t), 3>(pathOut, second.c_str(), static_cast<uint32_t>(second.size()));
            return true;
        }
        ++it;
    } while (it != end);
    return false;
}

bool cGZRegistry::Format(const char *scheme, const cIGZString *host, const cIGZString *path, cIGZString *out)
{
    if (!scheme) return false;
    cGZRegistryString text;
    std::string &s = text.Str().mString;
    s.append(scheme);
    s.append(SchemeSeparator());
    s.append(VirtualCall<const char *(__thiscall *)(const void *), 5>(host));
    s.append(PathSeparator());
    s.append(VirtualCall<const char *(__thiscall *)(const void *), 5>(path));
    VirtualCall<void(__thiscall *)(void *, const char *), 4>(out, s.c_str());
    return true;
}

bool cGZRegistry::Enumerate(const char *scheme, EnumerateCallback callback, void *userData)
{
    if (!callback || !userData) return false;
    cGZRegistryString key(scheme);
    cGZRegistryMap &map = Map();
    auto it = map.find(key);
    auto end = map.end();
    if (it != end) {
        do {
            // Copies, as in the original (which leaks the value copy: ORIGINAL-BUG, not reproduced).
            cGZRegistryValues values(it->second);
            if (it->first.Str().IsEqual(*key.Str().AsInterface(), true)) callback(values.first.Str().AsInterface(), values.second.Str().AsInterface(), userData);
            ++it;
        } while (it != end);
    }
    return true;
}
