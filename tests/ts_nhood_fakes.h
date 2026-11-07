#pragma once
// Fake streams for the lane DF tests (c2DArray, cTSNHoodTerrain): a scripted input stream and a recording output stream with
// the cRZIStream / cRZOStream slot layout (5 u8, 8 s32, 9 u32, 12 f32, 15 GZStr, 18 error). Every call is logged; a call can
// be made to throw (EH unwinding of the callers' local cRZString). A read past the script raises the error flag and writes
// zero into a stack local (the original reads uninitialised stack when a real stream fails: the fake gives it a defined
// value instead); a field or element outside the stack is left as it was, as a real stream leaves it.
#include "rz/cRZString.h"

#include <intrin.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace nhoodfx {

enum : uint32_t { evGet = 0x70, evSet = 0x71, evGetStr = 0x72, evSetStr = 0x73, evErr = 0x74, evRelease = 0x75, evMisc = 0x76 };

struct Item {
    uint32_t bits = 0;
    std::string text;
    bool isText = false;
};

struct Stub {
    void **vt;
    uint32_t id;
};

inline std::vector<uint32_t> gLog;
inline std::vector<Item> gScript;
inline size_t gCursor;
inline bool gInError;      // raised by a read past the script
inline bool gOutError;     // what the output stream's GetError answers
inline int gFailAt = -1;   // the n-th stream call throws
inline int gTick;
inline uint64_t gThrown;
inline void *gInVT[24]{}, *gOutVT[24]{};
inline Stub gIn{gInVT, 1}, gOut{gOutVT, 2};

inline void Note(uint32_t a, uint32_t b = 0, uint32_t c = 0)
{
    gLog.push_back(a);
    gLog.push_back(b);
    gLog.push_back(c);
}
inline void Step()
{
    if (gTick++ == gFailAt) {
        ++gThrown;
        throw 73;
    }
}
inline uint32_t Hash(const std::string &s)
{
    uint32_t h = 2166136261u;
    for (unsigned char c : s) h = (h ^ c) * 16777619u;
    return h ^ uint32_t(s.size());
}

template <class T> uint8_t __fastcall Get(Stub *, void *, T *out)
{
    Step();
    if (gCursor < gScript.size()) {
        const Item &item = gScript[gCursor++];
        if constexpr (std::is_same_v<T, float>) std::memcpy(out, &item.bits, 4);
        else *out = static_cast<T>(item.bits);
        Note(evGet, item.bits);
        return 0x41;
    }
    // A failed read leaves an object's own field or element untouched, as cRZIStream's GetVoid does (DL): the zeros a resize
    // stored stay observable. Stack locals still read zero (the original's are uninitialised).
    const uintptr_t at = reinterpret_cast<uintptr_t>(out);
    if (at >= __readfsdword(8) && at < __readfsdword(4)) *out = 0;
    gInError = true;
    Note(evGet, 0xdead);
    return 0;
}
inline uint8_t __fastcall GetText(Stub *, void *, void *out)
{
    Step();
    auto *string = static_cast<cRZString *>(out);
    if (gCursor < gScript.size()) {
        const Item &item = gScript[gCursor++];
        string->mString = item.isText ? item.text : std::string();
        Note(evGetStr, Hash(string->mString));
        return 1;
    }
    string->mString.clear();
    gInError = true;
    Note(evGetStr, 0xdead);
    return 0;
}
inline uint8_t __fastcall InError(Stub *, void *)
{
    Note(evErr, gInError);
    return gInError;
}
inline uint32_t __fastcall Release(Stub *self, void *)
{
    Note(evRelease, self->id);
    return 1;
}

inline uint8_t __fastcall SetU32(Stub *, void *, uint32_t value) { Step(); Note(evSet, value); return 0x41; }
inline uint8_t __fastcall SetU8(Stub *, void *, uint32_t value) { Step(); Note(evSet, value & 0xff, 8); return 0x41; }
inline uint8_t __fastcall SetU16(Stub *, void *, uint32_t value) { Step(); Note(evSet, value & 0xffff, 7); return 0x41; }
inline uint8_t __fastcall SetF32(Stub *, void *, float value)
{
    Step();
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    Note(evSet, bits, 12);
    return 0x41;
}
inline uint8_t __fastcall SetText(Stub *, void *, void *value)
{
    Step();
    Note(evSetStr, value ? Hash(static_cast<cRZString *>(value)->mString) : 0xdead0000u);
    return 0x41;
}
inline uint8_t __fastcall OutError(Stub *, void *)
{
    Note(evErr, gOutError, 2);
    return gOutError;
}

inline void SetupStreams()
{
    gInVT[2] = gOutVT[2] = reinterpret_cast<void *>(&Release);
    gInVT[5] = reinterpret_cast<void *>(&Get<uint8_t>);
    gInVT[7] = reinterpret_cast<void *>(&Get<uint16_t>);
    gOutVT[7] = reinterpret_cast<void *>(&SetU16);
    gInVT[8] = reinterpret_cast<void *>(&Get<int32_t>);
    gInVT[9] = reinterpret_cast<void *>(&Get<uint32_t>);
    gInVT[12] = reinterpret_cast<void *>(&Get<float>);
    gInVT[15] = reinterpret_cast<void *>(&GetText);
    gInVT[18] = reinterpret_cast<void *>(&InError);
    gOutVT[5] = reinterpret_cast<void *>(&SetU8);
    gOutVT[8] = gOutVT[9] = reinterpret_cast<void *>(&SetU32);
    gOutVT[12] = reinterpret_cast<void *>(&SetF32);
    gOutVT[15] = reinterpret_cast<void *>(&SetText);
    gOutVT[18] = reinterpret_cast<void *>(&OutError);
}

// Resets the logs and the failure injection for one run.
inline void Begin(const std::vector<Item> &script, int failAt, bool outError = false)
{
    SetupStreams();
    gLog.clear();
    gScript = script;
    gCursor = 0;
    gInError = false;
    gOutError = outError;
    gTick = 0;
    gFailAt = failAt;
}
inline Item U(uint32_t v) { Item i; i.bits = v; return i; }
inline Item F(float v) { Item i; std::memcpy(&i.bits, &v, 4); return i; }
inline Item S(const char *t) { Item i; i.text = t; i.isText = true; return i; }

} // namespace nhoodfx
