#pragma once
// Gimex, EA's image codec library as the game links it (lane LX): a table of codecs (one per file format) and the
// dispatchers the Gimex reader and writer (src/gz/cGZGimexReader.h) call them through. RVAs 1.18.0.187.
//
// A codec is nine __stdcall entry points (the table 0x105b990 holds 50 rows of nine words, ended by a row whose
// `about` is null): about (a new description: four-character code, file extensions, flags), is (how well a stream
// looks like this format: a score, 0 = not at all), open / info / read / close (read frames), wopen / write / wclose
// (write one). Rows: 0 the null codec, 1 FSH, 2 JPEG through libjpeg, 3 JPEG through Intel's IJL (ijl15.dll, loaded at
// run time), 4 PNG, 5 BMP, 6 TGA, 7..49 the null codec again. Every call goes through the codec of the current format
// (a global the dispatchers read): a caller sets it (SetFormat), then calls, holding the Gimex lock (0x10e6918).
//
// The codecs' entry points (0x87da81..0x88374d) are ours (lanes MN, MO, MP; src/gz/GimexFormats.h); the FSH back end
// below them (0x88374d..0x88d69a and 0x894e0e..0x89e722) is EA code nobody has replaced yet. The rest of
// 0x88d69a..0x8b7bb6 is libjpeg 6a / libpng 1.0.5 / zlib 1.1.3 (library code, db/library-matches.jsonl).
#include <cstddef>
#include <cstdint>

namespace ifaces {
class cIGZFile;
} // namespace ifaces

namespace gimex {

// The description a codec's `about` allocates (0x100 bytes from the codecs' allocator 0x87da65, zeroed, freed with
// FreeMemory). Only the fields the game reads are named.
struct About {
    uint32_t fourcc;         // 0x00
    uint32_t size;           // 0x04 0x100
    uint32_t version;        // 0x08 0x159
    uint32_t flags;          // 0x0c bit 1: the format's first extension is its own (a match scores higher)
    uint32_t unknown10;      // 0x10
    uint32_t unknown14;      // 0x14
    uint32_t defaultQuality; // 0x18 (tentative: BMP 100, FSH 75)
    uint32_t unknown1c;      // 0x1c a four-character code or a tag
    uint32_t unknown20;      // 0x20
    uint8_t unknown24[0x18]; // 0x24
    char extensions[8][8];   // 0x3c ".ext", up to eight
    char author[0x20];       // 0x7c
    char revision[8];        // 0x9c
    char shortName[8];       // 0xa4
    char typeName[0x10];     // 0xac
    char description[0x44];  // 0xbc
};
static_assert(sizeof(About) == 0x100, "Gimex About: 0x100 bytes");

// A frame's description (EA's GINFO): what info returns for reading and what the writer fills in for write. The
// reader's codecs allocate it; the factory's writer builds a 0x2a70-byte one on the stack (kWriteInfoSize).
struct Info {
    uint32_t signature;      // 0x000
    uint32_t size;           // 0x004 the struct's size (the writer: 0x2a70)
    uint32_t version;        // 0x008 (the writer: 0x157)
    uint32_t unknownC;       // 0x00c
    int32_t width;           // 0x010
    int32_t height;          // 0x014
    int32_t bitsPerPixel;    // 0x018 32 (B, G, R, A bytes) or 8 (indices into the palette)
    int32_t sourceBits;      // 0x01c the writer: 32
    uint32_t unknown20;      // 0x020
    uint32_t paletteCount;   // 0x024 the writer: 0; BMP / TGA: the palette's entries
    uint8_t palette[256][4]; // 0x028 B, G, R, A per index
    uint32_t variant;        // 0x428 the writer: 0; BMP: the header kind, TGA: the image type
    uint32_t jpegFlag;       // 0x42c the Gimex writer sets 1 before writing a format-2 (libjpeg) frame; BMP / TGA: run-length coded
    uint32_t quality;        // 0x430 the writer: its caller's quality
    uint32_t imageBytes;     // 0x434 the writer: width x height x 4
    uint32_t alphaBits;      // 0x438 the writer: 8 when the buffer has alpha, else 0
    uint32_t redBits;        // 0x43c the writer: 8
    uint32_t greenBits;      // 0x440 the writer: 8
    uint32_t blueBits;       // 0x444 the writer: 8
    uint32_t unknown448;     // 0x448
    uint32_t unknown44c;     // 0x44c
    int32_t originX;         // 0x450 TGA: the image origin (x, mirrored with the descriptor's right-to-left bit)
    int32_t originY;         // 0x454 TGA: the image origin (y)
    uint32_t unknown458;     // 0x458
    uint8_t unknown45c[0x2a5c - 0x45c];
    float dotsPerInch;       // 0x2a5c BMP: pixels per metre / 39.37008 (the writer's ratio back)
    uint8_t unknown2a60[0x2a70 - 0x2a60];
};
static_assert(sizeof(Info) == 0x2a70, "Gimex Info: the writer's 0x2a70 bytes");
static_assert(offsetof(Info, dotsPerInch) == 0x2a5c, "Gimex Info: dotsPerInch");
constexpr uint32_t kWriteInfoSize = 0x2a70;
constexpr uint32_t kWriteInfoVersion = 0x157;

// A codec's open-file state: the small block open and wopen allocate (0x18 bytes from the codecs' allocator, freed by
// close). The codecs of rows 2..6 share this head; JPEG and PNG use larger blocks of their own.
struct Instance {
    uint32_t fourcc;            // 0x00 the format's code (wopen leaves it 0)
    uint32_t size;              // 0x04 0x18 (wopen: 0)
    uint32_t frameCount;        // 0x08 1 (wopen: 0)
    uint32_t unknownC;          // 0x0c 0
    ifaces::cIGZFile *stream;   // 0x10
    uint32_t unknown14;         // 0x14 0
};
static_assert(sizeof(Instance) == 0x18, "Gimex Instance: 0x18 bytes");

using AboutFn = About *(__stdcall *)();
using IsFn = int32_t(__stdcall *)(ifaces::cIGZFile *stream);
using OpenFn = int32_t(__stdcall *)(Instance **out, ifaces::cIGZFile *stream, const char *path, int32_t flag);
using InfoFn = Info *(__stdcall *)(Instance *instance, int32_t frame);
using ReadFn = int32_t(__stdcall *)(Instance *instance, Info *info, void *pixels, int32_t pitch);
using CloseFn = int32_t(__stdcall *)(Instance *instance);
using WOpenFn = int32_t(__stdcall *)(Instance **out, ifaces::cIGZFile *stream, const char *path, int32_t flag);
using WriteFn = int32_t(__stdcall *)(Instance *instance, const Info *info, const void *pixels, int32_t pitch);
using WCloseFn = int32_t(__stdcall *)(Instance *instance);

struct Codec {
    AboutFn about;
    IsFn is;
    OpenFn open;
    InfoFn info;
    ReadFn read;
    CloseFn close;
    WOpenFn wopen;
    WriteFn write;
    WCloseFn wclose;
};
static_assert(sizeof(Codec) == 0x24, "Gimex codec row: nine words");

constexpr int32_t kRows = 50;
// Format ids the reader and writer take besides the rows: probe every codec, by the file name's extension, or both
// (the extension first).
constexpr uint32_t kProbe = 0x32;
constexpr uint32_t kByExtension = 0x33;
constexpr uint32_t kByExtensionOrProbe = 0x34;
constexpr uint32_t kFormatLibJpeg = 2;
constexpr uint32_t kFormatPng = 4;

// The codec table (ours: the formats' rows hold our codecs, the null rows our stubs).
const Codec *Codecs();
// The current format (0x10e693c in the original), ours.
int32_t CurrentFormat();

// The Gimex lock (the static cRZCriticalSection at 0x10e6918, built by its XC initialiser).
void Lock();
void Unlock();

// 0x87d7f6 (__cdecl): the rows before the first with no `about` (50).
int32_t __cdecl FormatCount();
// 0x87d96b (__stdcall): the current format; 0 for a negative id or one past the table.
void __stdcall SetFormat(int32_t format);
// 0x87d919 (__cdecl)
int32_t __cdecl GetFormat();
// 0x87d7d0: the current codec's about (a tail jump).
About *__stdcall CurrentAbout();
// 0x87d92c: the current codec's is (a tail jump).
int32_t __stdcall Is(ifaces::cIGZFile *stream);
// 0x87d939: the current codec's open (a tail jump); its frame count.
int32_t __stdcall Open(Instance **out, ifaces::cIGZFile *stream, const char *path, int32_t flag);
// 0x87d91f: the current codec's info (a tail jump).
Info *__stdcall GetInfo(Instance *instance, int32_t frame);
// 0x87d946: the current codec's read; true when it answered non-zero.
bool __stdcall Read(Instance *instance, Info *info, void *pixels, int32_t pitch);
// 0x87d7dd
bool __stdcall Close(Instance *instance);
// 0x87d9a0: the current codec's wopen (a tail jump).
int32_t __stdcall WOpen(Instance **out, ifaces::cIGZFile *stream, const char *path, int32_t flag);
// 0x87d9ad
bool __stdcall Write(Instance *instance, const Info *info, const void *pixels, int32_t pitch);
// 0x87d987
bool __stdcall WClose(Instance *instance);
// 0x87d78d (__cdecl): compares two strings ignoring the case of A-Z (others as signed chars); the first difference of
// the lowered characters, 0 when equal.
int32_t __cdecl CompareNoCase(const char *a, const char *b);
// 0x87d80f (__stdcall): the format whose extensions best match `extension` (a leading '-', '+' or '.' skipped; up to
// the first character below '!' or a '?'), 0 for none, made current. A match on a format's first extension scores 2,
// on another 1; for a format with flag bit 1, 4 and 3. Extension slots are tried in order across the formats, stopping
// once a score of 4 is found; the first format with the best score wins. *slot (when given) gets the slot that
// matched.
int32_t __stdcall FindFormatByExtension(const char *extension, int32_t *slot);
// The codecs' shared memory and stream helpers (0x87da65..0x87dbad, lane MP), called by every codec and by the FSH
// back end (0x88374d..0x88d69a, still the original's).
// 0x87da72 (the codecs' free): operator delete[]; answers 1 (in al: the original sets al only).
uint8_t __stdcall FreeMemory(void *memory);
// 0x87da65: a block of `size` bytes from operator new[] (the library's: it throws when there is none).
void *__stdcall Allocate(uint32_t size);
// 0x87da81: the stream's length (cIGZFile slot 7) widened to 64 bits (the high word 0; the FSH back end reads edx).
uint64_t __stdcall StreamLength(ifaces::cIGZFile *stream);
// 0x87da8f: reads up to `count` bytes (slot 15); the bytes read, 0 for a negative count (the slot's answer ignored).
int32_t __stdcall StreamRead(ifaces::cIGZFile *stream, void *buffer, int32_t count);
// 0x87dab6: seeks to `low` (slot 12; `high` unused); true when the slot answers `low`.
bool __stdcall StreamSeek(ifaces::cIGZFile *stream, uint32_t low, int32_t high);
// 0x87dacd: writes `count` bytes (slot 17); the count the slot leaves, 0 for a negative count.
int32_t __stdcall StreamWrite(ifaces::cIGZFile *stream, const void *buffer, int32_t count);
// 0x87daf4 (__cdecl): a little-endian number of 1..4 bytes; 0 for any other width.
uint32_t __cdecl ReadLittleEndian(const void *bytes, int32_t width);
// 0x87db43 (__cdecl): a big-endian number of 1..4 bytes; 0 for any other width.
uint32_t __cdecl ReadBigEndian(const void *bytes, int32_t width);

} // namespace gimex
