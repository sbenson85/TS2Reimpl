#pragma once
// What the files of nGZGraphic4::cDeviceStateDX share (cDeviceStateDX.cpp, cDeviceStateDXBlocks.cpp): the report of a
// failed device call, the maps from the engine's state indices to Direct3D's, and the word copy that tells whether
// anything changed.
#include <cstdint>

#include "game/game.h"
#include "game/owned.h"
#include "gz/cDeviceStateDX.h"
#include "rz/MiscHelpersA.h"

// The state tables (12-byte entries: the engine's index, Direct3D's in the low bytes of the next word, then the state's
// default value). The constructor builds the index maps from them, SetUpDevice the defaults.
struct nGZGraphic4StateName {
    int32_t index;
    uint32_t d3d;
    uint32_t defaultValue;
};
TS2_OWNED_STATIC(const nGZGraphic4StateName, feddb8)[0x59]; // render states
TS2_OWNED_STATIC(const nGZGraphic4StateName, fee1e8)[0x12]; // texture stage states
TS2_OWNED_STATIC(const nGZGraphic4StateName, fee2c0)[0xd];  // sampler states
// The engine's state index to Direct3D's, built by the constructor.
TS2_OWNED_STATIC(uint16_t, 10ca448)[0x59];
TS2_OWNED_STATIC(uint8_t, 10ca4fc)[0x12];
TS2_OWNED_STATIC(uint8_t, 10ca510)[0xd];

namespace nGZGraphic4::devstate_detail {

// LogD3DError for a failed device call (`line` is the original's source line, which is what the report carries).
inline void Report(HRESULT hr, uint32_t line)
{
    if (FAILED(hr))
        LogD3DError(static_cast<uint32_t>(hr),
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ts2_rva_d9fba8)),
                    line);
}

using StateName = ::nGZGraphic4StateName;
inline const StateName *RenderStateNames() { return ts2_rva_feddb8; }  // 0x59 entries
inline const StateName *StageStateNames() { return ts2_rva_fee1e8; }   // 0x12 entries
inline const StateName *SamplerStateNames() { return ts2_rva_fee2c0; } // 0xd entries

// The maps (devstate::kRenderStateMap and the others).
inline uint16_t *RenderStateMap() { return ts2_rva_10ca448; }
inline uint8_t *StageStateMap() { return ts2_rva_10ca4fc; }
inline uint8_t *SamplerStateMap() { return ts2_rva_10ca510; }

// Copies `count` words; true if any differed (the bit patterns: a float field holding NaN compares equal to itself).
inline bool CopyWords(void *dst, const void *src, uint32_t count)
{
    uint32_t *to = static_cast<uint32_t *>(dst);
    const uint32_t *from = static_cast<const uint32_t *>(src);
    uint32_t differ = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t word = from[i];
        differ |= to[i] ^ word;
        to[i] = word;
    }
    return differ != 0;
}

} // namespace nGZGraphic4::devstate_detail
