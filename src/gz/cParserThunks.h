#pragma once
// cBaseParser's vtordisp thunks (lane IM). cBaseParser's two virtual-base faces (cIGZUnknown at +0x1b0, cIBaseParser at
// +0x1b8) each have a vtordisp word just before them; every slot of their vtables (0xe7db3c, 0xe7db4c) is an 8-byte thunk
// that takes that word off `this` and goes on to the slot's body (a class derived from cBaseParser whose virtual bases sit
// elsewhere stores the distance there). The bodies are hooked to cBaseParserAdapters (src/gz/cBaseParser.h); each thunk is
// hooked (src/hook/tables/cParserThunks.cpp) to the forwarder below for its body's adapter. RVAs are 1.18.0.187.
#include "gz/cBaseParser.h"

#include <cstdint>

namespace nGZCommandParser {

// The body's `this`: the face less the vtordisp word stored just before it.
inline void *VtordispAdjusted(void *face)
{
    char *at = static_cast<char *>(face);
    return at - *reinterpret_cast<const int32_t *>(at - 4);
}

// The forwarder for a thunk whose body's replacement is `Adapter` (a cBaseParserAdapters entry: the face in ecx, the slot's
// stack arguments): the same arguments, `this` adjusted.
template <auto Adapter> struct VtordispThunk;
template <class R, class... A, R(__fastcall *Adapter)(void *, void *, A...)> struct VtordispThunk<Adapter> {
    static R __fastcall Call(void *face, void *, A... args) { return Adapter(VtordispAdjusted(face), nullptr, args...); }
};

} // namespace nGZCommandParser
