#pragma once
// Fakes shared by the property-set serializer / factory tests (lane DU): a byte stream with the cIGZIStream /
// cIGZOStream slots the serializers use (5 u8, 7 u16, 8 s32, 9 u32, 12 f32, 17 raw bytes), an optional record face
// (QI 0xe56b8f03: slots 10 GetSize, 11 SetSize, 12 GetPosition, 13 SeekAbsolute), call logging, and failure injection
// (the n-th stream call fails). A read past the data fails and writes nothing, as cRZIStream does. Plus a record
// generator for the binary format and a dump of a string property set (via its game vtable).
#include "gz/cGZAny.h"
#include "gz/cGZPropertySet.h"
#include "gz/cGZPropertySetSerializer.h"
#include "gz/cIGZString.h"
#include "game/game.h"
#include "game/vtable.h"

#include <cctype>
#include <cstdint>
#include <iterator>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace psfake {

using Blob = std::vector<uint32_t>;

struct Fake {
    void **vt;
    uint32_t id;
    int32_t refs;
};

struct State {
    std::vector<uint8_t> in;
    size_t pos = 0;
    std::vector<uint8_t> out;
    Blob log;
    int failAt = -1; // stream call index that fails
    int tick = 0;
    bool hasRecord = true;
    bool seekResult = true;
};
inline State g;
inline void *gStreamVT[24], *gRecordVT[24];
inline Fake gStream{gStreamVT, 0x51, 100}, gRecord{gRecordVT, 0x52, 100};

inline uint32_t Hash(const void *data, size_t size)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < size; ++i) h = (h ^ static_cast<const uint8_t *>(data)[i]) * 16777619u;
    return h ^ static_cast<uint32_t>(size);
}
inline uint32_t Hash(const char *text) { return text ? Hash(text, std::strlen(text)) : 0xdead0000u; }
inline void Log(uint32_t a, uint32_t b = 0, uint32_t c = 0)
{
    g.log.push_back(a);
    g.log.push_back(b);
    g.log.push_back(c);
}
inline bool Fails() { return g.tick++ == g.failAt; }

inline uint8_t __fastcall StreamQI(Fake *, void *, uint32_t riid, void **out)
{
    Log(0x100, riid);
    if (riid == 0xe56b8f03 && g.hasRecord) {
        *out = &gRecord;
        ++gRecord.refs;
        return 1;
    }
    return 0;
}
inline uint32_t __fastcall AddRef(Fake *self, void *) { Log(0x101, self->id); return uint32_t(++self->refs); }
inline uint32_t __fastcall Release(Fake *self, void *) { Log(0x102, self->id); return uint32_t(--self->refs); }

template <class T, unsigned Slot> uint8_t __fastcall Get(Fake *, void *, T *out)
{
    Log(Slot, uint32_t(g.pos));
    if (Fails() || g.pos + sizeof(T) > g.in.size()) {
        Log(0xfa11);
        return 0;
    }
    std::memcpy(out, &g.in[g.pos], sizeof(T));
    g.pos += sizeof(T);
    return 1;
}
inline uint8_t __fastcall GetVoid(Fake *, void *, void *data, uint32_t size)
{
    Log(17, uint32_t(g.pos), size);
    if (Fails() || g.pos + size > g.in.size()) {
        Log(0xfa11);
        return 0;
    }
    std::memcpy(data, &g.in[g.pos], size);
    g.pos += size;
    return 1;
}
template <class T, unsigned Slot> uint8_t __fastcall Set(Fake *, void *, T value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(T));
    Log(Slot, bits);
    if (Fails()) return 0;
    const auto *p = reinterpret_cast<const uint8_t *>(&value);
    g.out.insert(g.out.end(), p, p + sizeof(T));
    return 1;
}
inline uint8_t __fastcall SetVoid(Fake *, void *, const void *data, uint32_t size)
{
    Log(17, size, Hash(data, size));
    if (Fails()) return 0;
    const auto *p = static_cast<const uint8_t *>(data);
    g.out.insert(g.out.end(), p, p + size);
    return 1;
}
inline uint32_t __fastcall RecGetSize(Fake *, void *) { Log(0x10a); return uint32_t(g.in.size()); }
inline uint32_t __fastcall RecSetSize(Fake *, void *, uint32_t size) { Log(0x10b, size); return 0x77; }
inline uint32_t __fastcall RecGetPosition(Fake *, void *) { Log(0x10c); return uint32_t(g.out.size()) + 0x1000; }
inline uint8_t __fastcall RecSeek(Fake *, void *, uint32_t position)
{
    Log(0x10d, position);
    if (!g.seekResult) return 0;
    g.pos = position;
    return 1;
}

// The stream is both an input and an output stream: the read slots and the write slots are the same numbers on the
// two interfaces, so a test picks the direction with `reading`.
inline void Begin(bool reading, const std::vector<uint8_t> &in, int failAt, bool hasRecord, bool seekResult = true)
{
    for (auto &p : gStreamVT) p = nullptr;
    for (auto &p : gRecordVT) p = nullptr;
    gStreamVT[0] = reinterpret_cast<void *>(&StreamQI);
    gStreamVT[1] = gRecordVT[1] = reinterpret_cast<void *>(&AddRef);
    gStreamVT[2] = gRecordVT[2] = reinterpret_cast<void *>(&Release);
    if (reading) {
        gStreamVT[5] = reinterpret_cast<void *>(&Get<uint8_t, 5>);
        gStreamVT[7] = reinterpret_cast<void *>(&Get<uint16_t, 7>);
        gStreamVT[8] = reinterpret_cast<void *>(&Get<int32_t, 8>);
        gStreamVT[9] = reinterpret_cast<void *>(&Get<uint32_t, 9>);
        gStreamVT[12] = reinterpret_cast<void *>(&Get<float, 12>);
        gStreamVT[17] = reinterpret_cast<void *>(&GetVoid);
    } else {
        gStreamVT[5] = reinterpret_cast<void *>(&Set<uint8_t, 5>);
        gStreamVT[7] = reinterpret_cast<void *>(&Set<uint16_t, 7>);
        gStreamVT[8] = reinterpret_cast<void *>(&Set<int32_t, 8>);
        gStreamVT[9] = reinterpret_cast<void *>(&Set<uint32_t, 9>);
        gStreamVT[12] = reinterpret_cast<void *>(&Set<float, 12>);
        gStreamVT[17] = reinterpret_cast<void *>(&SetVoid);
    }
    gRecordVT[10] = reinterpret_cast<void *>(&RecGetSize);
    gRecordVT[11] = reinterpret_cast<void *>(&RecSetSize);
    gRecordVT[12] = reinterpret_cast<void *>(&RecGetPosition);
    gRecordVT[13] = reinterpret_cast<void *>(&RecSeek);
    gStream.refs = gRecord.refs = 100;
    g = State{};
    g.in = in;
    g.failAt = failAt;
    g.hasRecord = hasRecord;
    g.seekResult = seekResult;
}

// ---- record generator ------------------------------------------------------------------------------------------

struct Bytes {
    std::vector<uint8_t> b;
    void U8(uint8_t v) { b.push_back(v); }
    void U16(uint16_t v) { b.insert(b.end(), reinterpret_cast<uint8_t *>(&v), reinterpret_cast<uint8_t *>(&v) + 2); }
    void U32(uint32_t v) { b.insert(b.end(), reinterpret_cast<uint8_t *>(&v), reinterpret_cast<uint8_t *>(&v) + 4); }
    void Text(const std::string &t)
    {
        U32(uint32_t(t.size()));
        b.insert(b.end(), t.begin(), t.end());
    }
};

inline const std::string kKeys[] = {"a", "A", "key", "Key", "", std::string("x\0y", 3), "x",
                                    "a key longer than fifteen chars", "binidx", "creatorid"};
inline const std::string kTexts[] = {"", "t", "text", std::string("n\0ul", 4), "a text longer than the small buffer",
                                     "GUID-0001", "x"};

inline std::string Normal(const std::string &key)
{
    std::string k(key.c_str());
    for (char &c : k) c = char(std::tolower(static_cast<unsigned char>(c)));
    return k;
}

// One property set record. Types: the six built-in ones, and (rarely, `allowUnknown`) an unknown id. A StringSet
// never goes under a key that already holds another type (the original then inserts into that value: ORIGINAL-BUG
// in the source).
inline std::vector<uint8_t> MakeRecord(std::mt19937 &rng, bool allowUnknown, bool oddHeader)
{
    Bytes r;
    const uint32_t magics[] = {gzpropser::kBinaryMagic, 0x2bea4862u, 0u, 0xcbe750e1u};
    r.U32(oddHeader && rng() % 3 == 0 ? magics[rng() % 4] : gzpropser::kBinaryMagic);
    const uint16_t versions[] = {0, 1, 2, 3, 0xffff};
    r.U16(oddHeader && rng() % 3 == 0 ? versions[rng() % 5] : uint16_t(rng() % 3));
    const uint32_t count = rng() % 7;
    r.U32(oddHeader && rng() % 4 == 0 ? count + 1 + rng() % 3 : count);
    const uint32_t types[] = {gzany::kTypeUint32, gzany::kTypeSint32, gzany::kTypeFloat32, gzany::kTypeBoolean,
                              gzany::kTypeString, gzany::kTypeStringSet};
    std::vector<std::pair<std::string, uint32_t>> seen;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t type = types[rng() % 6];
        if (allowUnknown && rng() % 12 == 0) type = 0x12345678;
        std::string key = kKeys[rng() % std::size(kKeys)];
        if (type == gzany::kTypeStringSet) {
            for (auto &s : seen)
                if (s.first == Normal(key) && s.second != gzany::kTypeStringSet) key = "set" + std::to_string(i);
        }
        seen.push_back({Normal(key), type});
        r.U32(type);
        r.Text(key);
        switch (type) {
        case gzany::kTypeUint32:
        case gzany::kTypeSint32: r.U32(rng() % 4 == 0 ? 0xffffffffu : rng()); break;
        case gzany::kTypeFloat32: r.U32(rng()); break;
        case gzany::kTypeBoolean: r.U8(uint8_t(rng() % 3 == 0 ? rng() : rng() % 2)); break;
        case gzany::kTypeString: r.Text(kTexts[rng() % std::size(kTexts)]); break;
        case gzany::kTypeStringSet: {
            const uint32_t n = rng() % 4;
            r.U32(n);
            for (uint32_t j = 0; j < n; ++j) r.Text(kTexts[rng() % std::size(kTexts)]);
            break;
        }
        default: r.U32(rng()); break;
        }
    }
    return r.b;
}

// ---- property-set dump -----------------------------------------------------------------------------------------

// Every property in the set's order: key (hash of the text), type, and the value (scalars' payload, a Boolean's low
// byte, a String's text, a StringSet's strings in their list order).
inline Blob DumpSet(void *set)
{
    Blob out;
    GZServiceString key;
    void *value = nullptr;
    using Step = uint8_t(__thiscall *)(void *, void *, void **);
    for (bool ok = VirtualSlot<Step>(set, 6)(set, &key, &value) != 0; ok; ok = VirtualSlot<Step>(set, 7)(set, &key, &value) != 0) {
        out.push_back(Hash(key.data(), key.size()));
        const uint32_t type = VirtualSlot<uint32_t(__thiscall *)(void *)>(value, 4)(value);
        out.push_back(type);
        switch (type) {
        case gzany::kTypeUint32:
        case gzany::kTypeSint32:
        case gzany::kTypeFloat32: out.push_back(static_cast<InlineAny *>(value)->mValue); break;
        case gzany::kTypeBoolean: out.push_back(static_cast<InlineAny *>(value)->mValue & 0xff); break;
        case gzany::kTypeString: {
            cIGZString *text = static_cast<AnyString *>(value)->mString;
            out.push_back(text ? Hash(text->ToChar()) : 0xdeadu);
            break;
        }
        case gzany::kTypeStringSet:
            out.push_back(uint32_t(static_cast<AnyStringSet *>(value)->mSet.size()));
            for (const GZString &s : static_cast<AnyStringSet *>(value)->mSet) out.push_back(Hash(s.data(), s.size()));
            break;
        default: out.push_back(0xcccc); break;
        }
    }
    return out;
}

} // namespace psfake
