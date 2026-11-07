#pragma once
// A cRZString on the stack, built the way the scene-graph system's functions build theirs: the game's cRZString
// vtable, the std::string, reference count 0; destroyed without a vtable call. (Lane IR.)
#include "game/game.h"
#include "rz/StringKeyParser.h"
#include "rz/cRZString.h"

#include <new>
#include <string>

namespace nTSSG {

struct SGLocalString {
    cRZString value;
    SGLocalString() : SGLocalString("", 0) {}
    SGLocalString(const char *text, size_t length)
    {
        value.mVTable = cRZString::VTable();
        ::new (&value.mString) std::string(text, length);
        value.mRefCount = 0;
    }
    explicit SGLocalString(const char *text) : SGLocalString(text, std::char_traits<char>::length(text)) {}
    SGLocalString(const SGLocalString &) = delete;
    SGLocalString &operator=(const SGLocalString &) = delete;
    ~SGLocalString() { value.mString.~basic_string(); }

    const char *Text() const { return value.mString.c_str(); }
    cRZStringHelpers *Helpers() { return reinterpret_cast<cRZStringHelpers *>(&value); }
};

} // namespace nTSSG
