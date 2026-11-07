#pragma once
// Our cFileSource's vtables against the game's (lane IM; src/gz/cFileSource.h). The parser's own tests compare the source
// a constructor or SetSource made: since cFileSource is in the standalone shape, ours carries our vtables and the original
// the game's, the only bytes that differ by design (docs/standalone-shape.md, Tests). GameWord maps ours to the game's.
#include "game/game.h"
#include "gz/cFileSource.h"

#include <cstdint>
#include <cstring>

namespace filesource {

constexpr uint32_t kGameVTable = 0xe7dc30;
constexpr uint32_t kGameRefCountVTable = 0xe7dc54;

// Our two vtable words (the +0 and +4 faces), read from a probe our constructor built.
inline const uint32_t *OurVTables()
{
    static uint32_t words[2] = {};
    if (!words[0]) {
        auto *probe = new nGZCommandParser::cFileSource;
        std::memcpy(words, probe, sizeof(words));
        delete probe;
    }
    return words;
}

// A word of a source object as the game's: our vtable words become the game's, everything else stays.
inline uint32_t GameWord(uint32_t word)
{
    const uint32_t *ours = OurVTables();
    if (word == ours[0]) return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GameAddress<void>(kGameVTable)));
    if (word == ours[1]) return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GameAddress<void>(kGameRefCountVTable)));
    return word;
}

// The 12 bytes of a source with the vtable words mapped.
inline void SourceBytes(const void *source, uint8_t (&out)[12])
{
    uint32_t words[3];
    std::memcpy(words, source, sizeof(words));
    words[0] = GameWord(words[0]);
    words[1] = GameWord(words[1]);
    std::memcpy(out, words, sizeof(words));
}

} // namespace filesource
