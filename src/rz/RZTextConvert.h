#pragma once
#include "game/owned.h"
#include "gz/cIGZString.h"

#include <cstdint>
#include <string>

// Text encoding conversion used for file paths and other strings (1.18.0.187).
//
// Encodings are identified by number: 0 = 7-bit ASCII, 8 = UTF-8, 16 = UTF-16 (little-endian,
// lengths in 16-bit units), anything else = a Windows code page passed to MultiByteToWideChar /
// WideCharToMultiByte. Two special values are resolved first: -2 = the default string encoding
// (UTF-8, what cIGZString holds) and -1 = the system ANSI code page (GetACP, cached).
//
// The UTF-8 codec is the game's own, not the Windows one, with its quirks: decoding accepts the
// old 5- and 6-byte forms and keeps only the low 16 bits of anything outside the BMP; encoding
// writes each UTF-16 unit (surrogates included) as 1-3 bytes. Lengths are in units of the
// encoding (bytes, or 16-bit units for UTF-16).
namespace RZText {

constexpr int32_t kAscii = 0;
constexpr int32_t kUtf8 = 8;
constexpr int32_t kUtf16 = 16;
constexpr int32_t kDefault = -2;
constexpr int32_t kSystemCodePage = -1;

// 0x1a9200 (a 4-byte function shared with unrelated classes): the encoding of cIGZString text.
inline int32_t DefaultEncoding()
{
    return kUtf8;
}

// 0x1b0690: GetACP(), cached in a game global (RVA 0xff23a4, -3 until first use).
int32_t SystemCodePage();

// 0x1afd58: converts srcLength units of src (encoding srcEncoding) into dst, which has room for
// *dstLength units of dstEncoding. Sets *dstLength to the units written and returns the source
// units consumed (0 on failure). Pure-ASCII input is treated as encoding 0 whatever its label.
uint32_t Convert(const void *src, uint32_t srcLength, int32_t srcEncoding, void *dst, uint32_t *dstLength,
                 int32_t dstEncoding);

// 0x1b0455: Convert through a temporary UTF-16 buffer, for pairs Convert can't do directly.
// Returns srcLength if both steps consumed everything, else 0 with *dstLength = 0.
uint32_t ConvertViaUtf16(const void *src, uint32_t srcLength, int32_t srcEncoding, void *dst, uint32_t *dstLength,
                         int32_t dstEncoding);

// 0x1afc1f: converts the text of a cIGZString (UTF-8) like Convert, with the same result.
uint32_t ConvertString(const cIGZString &src, void *dst, uint32_t *dstLength, int32_t dstEncoding);

// 0x1b025d: converts src's text to dstEncoding and stores it in dst (as bytes). True if all of
// it converted. Same encoding (or -2): a plain copy.
bool ConvertStringToString(const cIGZString &src, cIGZString &dst, int32_t dstEncoding);

// 0x1c2ad8: the UTF-16 form of a cIGZString, as a std::u16string (constructed in *out).
std::u16string *ToUtf16(std::u16string *out, const cIGZString &src);

} // namespace RZText

// SystemCodePage's cache (owned.h): the image's word in the hooked game, -3 (not read yet) in an image-free program
// (standalone/owned_statics.cpp).
TS2_OWNED_STATIC(int32_t, ff23a4);
