#pragma once
// Recording cRZStrings for the language tests (tests/rz_language_utility.cpp, tests/rz_language_manager.cpp): a copy of
// the game's string vtable whose mutating slots log (slot, the count arguments, which recording string they were handed,
// the text after the call) before and after forwarding to the game's bodies, and the log they write into.
#include "difftest.h"
#include "rz/cRZString.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace recstr {

// ---- the log -------------------------------------------------------------------------------------------------------

inline std::vector<std::string> gLog;

inline void Note(const char *fmt, ...)
{
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    gLog.emplace_back(buffer);
}

// A text with its bytes made visible (NULs and bytes past 0x7e as \xNN).
inline std::string Shown(const char *data, size_t size)
{
    std::string out;
    for (size_t i = 0; i < size; ++i) {
        const unsigned char c = static_cast<unsigned char>(data[i]);
        if (c >= 0x20 && c < 0x7f && c != '\\') {
            out += static_cast<char>(c);
        } else {
            char hex[8];
            std::snprintf(hex, sizeof(hex), "\\x%02x", c);
            out += hex;
        }
    }
    return out;
}

inline std::string TextOf(const cRZString &s) { return Shown(s.mString.data(), s.mString.size()); }

// ---- recording strings ---------------------------------------------------------------------------------------------

inline void *gGameString[34];
inline void *gRecordingString[34];

inline cRZString *Str(void *self) { return static_cast<cRZString *>(self); }
inline void After(unsigned slot, void *self) { Note("  s%u -> [%s]", slot, TextOf(*Str(self)).c_str()); }
// Which recording string a slot was handed (the tag a test puts in its count word; 0 by default).
inline void From(const void *other)
{
    const cRZString *o = static_cast<const cRZString *>(other);
    if (o->mVTable == gRecordingString) Note("  from #%x", o->mRefCount);
}

template <unsigned Slot, class Fn> Fn Game() { return reinterpret_cast<Fn>(gGameString[Slot]); }

inline void __fastcall S3(void *self, void *, const char *text, uint32_t length)
{
    Note("s3 FromChar(%u)", length);
    Game<3, void(__thiscall *)(void *, const char *, uint32_t)>()(self, text, length);
    After(3, self);
}
inline void __fastcall S4(void *self, void *, const char *text)
{
    Note("s4 FromChar");
    Game<4, void(__thiscall *)(void *, const char *)>()(self, text);
    After(4, self);
}
inline void *__fastcall S16(void *self, void *, const void *other)
{
    Note("s16 Assign");
    From(other);
    void *r = Game<16, void *(__thiscall *)(void *, const void *)>()(self, other);
    After(16, self);
    return r;
}
inline void __fastcall S17(void *self, void *, const void *other)
{
    Note("s17 Copy");
    From(other);
    Game<17, void(__thiscall *)(void *, const void *)>()(self, other);
    After(17, self);
}
inline void __fastcall S18(void *self, void *, uint32_t size)
{
    Note("s18 Resize(%u)", size);
    Game<18, void(__thiscall *)(void *, uint32_t)>()(self, size);
    After(18, self);
}
inline void *__fastcall S19(void *self, void *, const void *other)
{
    Note("s19 Append");
    From(other);
    void *r = Game<19, void *(__thiscall *)(void *, const void *)>()(self, other);
    After(19, self);
    return r;
}
inline void *__fastcall S20(void *self, void *, const char *text, uint32_t length)
{
    Note("s20 Append(%u)", length);
    void *r = Game<20, void *(__thiscall *)(void *, const char *, uint32_t)>()(self, text, length);
    After(20, self);
    return r;
}
inline void *__fastcall S21(void *self, void *, const char *text)
{
    Note("s21 Append");
    void *r = Game<21, void *(__thiscall *)(void *, const char *)>()(self, text);
    After(21, self);
    return r;
}
inline void *__fastcall S22(void *self, void *, uint32_t at, const void *other)
{
    Note("s22 Insert(%u)", at);
    void *r = Game<22, void *(__thiscall *)(void *, uint32_t, const void *)>()(self, at, other);
    After(22, self);
    return r;
}
inline void *__fastcall S23(void *self, void *, uint32_t at, const char *text, uint32_t length)
{
    Note("s23 Insert(%u, %u)", at, length);
    void *r = Game<23, void *(__thiscall *)(void *, uint32_t, const char *, uint32_t)>()(self, at, text, length);
    After(23, self);
    return r;
}
inline void *__fastcall S24(void *self, void *, uint32_t at, const void *other)
{
    Note("s24 Replace(%u)", at);
    void *r = Game<24, void *(__thiscall *)(void *, uint32_t, const void *)>()(self, at, other);
    After(24, self);
    return r;
}
inline void *__fastcall S25(void *self, void *, uint32_t at, const char *text, uint32_t count)
{
    Note("s25 Replace(%u, %u)", at, count);
    void *r = Game<25, void *(__thiscall *)(void *, uint32_t, const char *, uint32_t)>()(self, at, text, count);
    After(25, self);
    return r;
}
inline void *__fastcall S26(void *self, void *, uint32_t at, uint32_t count)
{
    Note("s26 Erase(%u, %u)", at, count);
    void *r = Game<26, void *(__thiscall *)(void *, uint32_t, uint32_t)>()(self, at, count);
    After(26, self);
    return r;
}
inline void *__cdecl S31(void *self, const char *format, ...)
{
    Note("s31 Sprintf [%s]", format);
    va_list args;
    va_start(args, format);
    void *r = Game<32, void *(__thiscall *)(void *, const char *, va_list)>()(self, format, args);
    va_end(args);
    After(31, self);
    return r;
}
inline void *__fastcall S32(void *self, void *, const char *format, va_list args)
{
    Note("s32 SprintfVaList [%s]", format);
    void *r = Game<32, void *(__thiscall *)(void *, const char *, va_list)>()(self, format, args);
    After(32, self);
    return r;
}

inline void SetUpStrings(const difftest::Context &ctx)
{
    std::memcpy(gGameString, ctx.image.At(cRZString::kVTable), sizeof(gGameString));
    std::memcpy(gRecordingString, gGameString, sizeof(gRecordingString));
    gRecordingString[3] = reinterpret_cast<void *>(&S3);
    gRecordingString[4] = reinterpret_cast<void *>(&S4);
    gRecordingString[16] = reinterpret_cast<void *>(&S16);
    gRecordingString[17] = reinterpret_cast<void *>(&S17);
    gRecordingString[18] = reinterpret_cast<void *>(&S18);
    gRecordingString[19] = reinterpret_cast<void *>(&S19);
    gRecordingString[20] = reinterpret_cast<void *>(&S20);
    gRecordingString[21] = reinterpret_cast<void *>(&S21);
    gRecordingString[22] = reinterpret_cast<void *>(&S22);
    gRecordingString[23] = reinterpret_cast<void *>(&S23);
    gRecordingString[24] = reinterpret_cast<void *>(&S24);
    gRecordingString[25] = reinterpret_cast<void *>(&S25);
    gRecordingString[26] = reinterpret_cast<void *>(&S26);
    gRecordingString[31] = reinterpret_cast<void *>(&S31);
    gRecordingString[32] = reinterpret_cast<void *>(&S32);
}

// A cRZString on the game's vtable, destroyed once (Destruct only: the union keeps the implicit destructor away).
struct PlainString {
    union {
        cRZString s;
    };
    PlainString() { s.Construct(); }
    explicit PlainString(const char *text) { s.Construct(text); }
    ~PlainString() { s.Destruct(); }
    PlainString(const PlainString &) = delete;
    PlainString &operator=(const PlainString &) = delete;
};

// A cRZString on the recording vtable; the game's back for its destruction.
struct RecString {
    union {
        cRZString s;
    };
    explicit RecString(const char *text)
    {
        s.Construct(text);
        s.mVTable = gRecordingString;
    }
    RecString(const char *text, size_t size)
    {
        s.Construct();
        s.mString.assign(text, size);
        s.mVTable = gRecordingString;
    }
    ~RecString()
    {
        s.mVTable = gGameString;
        s.Destruct();
    }
    RecString(const RecString &) = delete;
    RecString &operator=(const RecString &) = delete;
    cIGZString *I() { return s.AsInterface(); }
};

} // namespace recstr
