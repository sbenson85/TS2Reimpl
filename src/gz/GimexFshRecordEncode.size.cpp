// The FSH back end's record encoder (lane MT; src/gz/GimexFshCluster.h, 0x89d7b1).
//
// Writes one block-compressed FSH record: an 8-byte header and the 4x4 blocks of the image's bytes (a width-4 image of count / 16 rows,
// 16 bytes apart; the block encoder 0x8988f3 does the work). Header bytes:
//   0: the form: 0x1a (flag 0x100), 0x1c (flag 0x200), 0x28 (flag 8: the 4:4:4:4 form of the other flags' codes) or 0x18 (none of them),
//      plus 0x80 when the count is over 16,000,000 (a 4-byte size follows instead of a 3-byte one);
//   1: 0xfb;
//   2..4 the count as 24-bit big-endian, then byte 5 the type bits; or, over 16,000,000, 2..5 the count as 32-bit big-endian and byte 6 the
//      type bits (byte 7 / 6 is not written);
// type bits = (flag 0x100: 1) | (flag 4: 4), none at all when flag 8 is set (flag 0x200 would be 0x200: its low byte is zero).
// The block encoder gets flag 4 cleared (it takes it as its own argument, with the weights 0.3, 0.4, 0.3), flag 8 cleared in the 0x28 form
// only (the other forms pass it on and also give the encoder "big endian" = flag 8). `flags` may be null (no flags); it is only read.
// Returns the bytes the block encoder wrote plus the 8 of the header.
#include "gz/GimexFshCluster.h"

#include "gz/GimexFshEncode.h"
#include "gz/GimexFshLeaves.h"

namespace gimex {

namespace fsh {

namespace {

constexpr uint32_t kFlagType1 = 0x100;
constexpr uint32_t kFlagType2 = 0x200;
constexpr uint32_t kFlagFourFour = 4;
constexpr uint32_t kFlagBigEndian = 8;
constexpr uint8_t kFormPlain = 0x18;
constexpr uint8_t kFormType1 = 0x1a;
constexpr uint8_t kFormType2 = 0x1c;
constexpr uint8_t kFormBigEndianFlag = 0x28;
constexpr uint8_t kLargeBit = 0x80;
constexpr uint8_t kSecondByte = 0xfb;
constexpr int32_t kLargeFrom = 16000000;       // over this many bytes the size takes four bytes
constexpr int32_t kHeaderBytes = 8;
constexpr int32_t kBlockWidth = 4;
constexpr uint32_t kBlockPitch = 16;
constexpr int32_t kBytesPerRow = 16;
constexpr float kWeights[3] = {0.3f, 0.4f, 0.3f};

} // namespace

// 0x89d7b1
int32_t __stdcall EncodeRecord(uint8_t *out, const uint8_t *image, int32_t byteCount, uint32_t *flags)
{
    uint32_t mode = flags ? *flags : 0;
    const bool large = byteCount > kLargeFrom;
    const bool fourFour = (mode & kFlagFourFour) != 0;
    const bool bigEndian = (mode & kFlagBigEndian) != 0;

    uint8_t type = 0;
    if (mode & kFlagType1) type = 1;
    if (fourFour) type |= 4;
    if (bigEndian) type = 0;

    uint8_t form;
    if (mode & kFlagType1) {
        form = kFormType1;
    } else if (mode & kFlagType2) {
        form = kFormType2;
    } else if (bigEndian) {
        form = kFormBigEndianFlag;
        mode &= ~kFlagBigEndian;
    } else {
        form = kFormPlain;
    }
    out[0] = large ? static_cast<uint8_t>(form | kLargeBit) : form;
    out[1] = kSecondByte;
    if (large) {
        PutBigEndian(out + 2, static_cast<uint32_t>(byteCount), 4);
        out[6] = type;
    } else {
        out[2] = static_cast<uint8_t>(static_cast<uint32_t>(byteCount) >> 16);
        out[3] = static_cast<uint8_t>(static_cast<uint32_t>(byteCount) >> 8);
        out[4] = static_cast<uint8_t>(byteCount);
        out[5] = type;
    }

    const int32_t rows = (byteCount + ((byteCount >> 31) & (kBytesPerRow - 1))) >> 4;
    return EncodeBlocks(kBlockWidth, rows, kBlockPitch, image, out + kHeaderBytes, mode & ~kFlagFourFour, kWeights, fourFour, bigEndian) +
           kHeaderBytes;
}

} // namespace fsh

} // namespace gimex
