#pragma once
#include <cstdint>

// EA's RefPack (QFS) compression, used for compressed resources in .package files and by
// cRZFastCompression3 (cIGZCompressor). RVAs are 1.18.0.187.
namespace RefPack {

// Decompresses one RefPack stream (header included) into dst. Returns the decompressed size from
// the header, or -1 if the output would exceed dstCapacity. Only the output is bounds-checked: a
// malformed stream can read past src or reference bytes before dst, like the original. If consumed
// is non-null it receives the number of src bytes read. Original: 0x9cc369 (__cdecl).
int32_t __cdecl Decompress(const uint8_t *src, uint32_t *consumed, uint8_t *dst, int32_t dstCapacity);

// Encodes src as a RefPack opcode stream (no header) into dst, which must hold the worst case;
// returns the bytes written, or 0 if the work tables can't be allocated. Greedy hash-chain matcher
// over a 128 KB window: a match replaces the current best only if it saves bytes after its opcode
// size. With `quick` set, only the first position of each match is added to the hash chains.
// Reads up to 2 bytes past the end of src, like the original. Original: 0x9cc611 (__cdecl).
int32_t __cdecl Encode(const uint8_t *src, int32_t length, uint8_t *dst, int32_t window, int32_t quick);

// Compress: writes a 5-byte header (0x10FB, 24-bit big-endian length) and the encoded stream.
// `flags` bit 0 selects quick mode. Returns the total size. Original: 0x9cc54d (__cdecl).
int32_t __cdecl Compress(const uint8_t *src, int32_t length, uint8_t *dst, const int32_t *flags);

// Decompressed size from a RefPack header. Unlike Decompress, which picks the header form from the
// flags' low bit, this treats exactly 0x10FB as the short form and anything else as the long one.
// Original: 0x9cca38 (__cdecl).
uint32_t __cdecl GetDecompressedSize(const uint8_t *src);

// 100 if src starts with a RefPack signature (0x10FB or 0x11FB), else 0. Original: 0x9cc5c6.
uint32_t __cdecl RecognizeHeader(const uint8_t *src);

// Writes the low `bytes` bytes of value big-endian. Original: 0x9cc597 (__cdecl).
void __cdecl WriteBigEndian(uint8_t *dst, uint32_t value, uint32_t bytes);

} // namespace RefPack
