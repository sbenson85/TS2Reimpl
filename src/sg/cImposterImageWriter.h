#pragma once
// The lot imposter's image writer (lane 261111q, scenegraph row LT2b2): a 12-byte object the generation path builds on
// its stack (0x366980, 0x36b3e0) to save an imposter image to a file through a Gimex writer codec. No RTTI; names after
// the bodies.
//
//   +0 a cRZFile (counted), +4 the codec (cGZGimexWriter, counted), +8 both were made.
//
// Write's settings block is gimex::Info (0x2a70 bytes) on the stack, zeroed, then filled after the image's format:
//   1 (32-bit): 32 bits, alpha / red / green / blue 8   2 (24-bit): 24 bits, alpha 0, colour 8
//   3 (8-bit): 8 bits, alpha 8, colour 0                4 / 5: SetCodecFormat(1 / 3), bits = the source bits
// Other formats (6..9) leave the bit fields zero.
#include "gz/Gimex.h"

#include <cstdint>

class cIGZFile;
class cIGZGimexWriter;
namespace nGZSceneGraph {
class cIImageData;
}

namespace nTSSG {

class cImposterImageWriter {
public:
    static constexpr uint32_t kFileSize = 0x9c; // cRZFile
    // cIGZFile::Open for writing: access 2, create always 1, share 1.
    static constexpr uint32_t kOpenAccess = 2, kOpenCreation = 1, kOpenSharing = 1;
    static constexpr int32_t kCodecOpenFlag = 1;
    static constexpr uint32_t kQuality = 100;
    static constexpr int32_t kFormatLimit = 10; // image formats 1..9 are written

    // 0xb401c0: empties both, asks the framework for a writer codec and makes a new cRZFile; ready when both exist.
    cImposterImageWriter *Construct();
    // 0xb402a0: writes `level` of `image` to `path` with the codec row `format`; true when the codec wrote it and both
    // the codec and the file closed.
    bool Write(nGZSceneGraph::cIImageData *image, uint32_t level, const char *path, uint32_t format);
    // 0xb40540: Write of level 0.
    bool WriteTop(nGZSceneGraph::cIImageData *image, const char *path, uint32_t format);
    // 0xb40970 (__cdecl): the codec fields of the four-channel layouts: mode 1 a 25-bit source (alpha 1), 4 / 5 a
    // 27-bit source (alpha 3), anything else 28 (alpha 4); colour channels 8 bits; the codec's +0x42c word 3.
    static void __cdecl SetCodecFormat(int32_t mode, gimex::Info *info);

    cIGZFile *mFile;          // +0
    cIGZGimexWriter *mCodec;  // +4
    bool mReady;              // +8
};
static_assert(sizeof(cImposterImageWriter) == 12, "cImposterImageWriter");

} // namespace nTSSG
