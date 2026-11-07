// The speed-profile member of src/audio/MmxResampleTables.h (lane HQ).
#include "audio/MmxResampleTables.h"

#include "game/game.h"

#include <cstring>

void __cdecl MmxResampleTables::BuildTables()
{
    uint8_t *out = GameAddress<uint8_t>(kTables);
    const struct {
        uint32_t source, rows;
    } tables[] = {{kSourceA, 15}, {kSourceB, 17}};
    for (const auto &t : tables) {
        for (uint32_t shift = 0; shift < 16; ++shift) {
            const uint8_t *row = GameAddress<uint8_t>(t.source) - 2 * shift;
            for (uint32_t r = 0; r < t.rows; ++r, row += 0x40, out += 0x20) std::memcpy(out, row, 0x20);
        }
    }
}
