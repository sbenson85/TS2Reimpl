#pragma once
// The entry points of the Gimex codecs that are ours (lane MN), one namespace per format. Each is a row's column of the
// codec table (src/gz/Gimex.h; our table is src/gz/Gimex.size.cpp). RVAs 1.18.0.187.
#include "gz/Gimex.h"

namespace gimex {

// 0x87f3e4 (wopen of rows 2..6): a zeroed instance block (0x18 bytes) holding `stream`, to *out. 1, or 0 when the
// allocation fails (*out untouched). The codecs of rows 2..6 share it.
int32_t __stdcall OpenWriter(Instance **out, ifaces::cIGZFile *stream, const char *path, int32_t flag);
// 0x87fe5d (close of rows 5 / 6, wclose of rows 3 / 5 / 6): frees the instance block; 0 for a null instance, else the
// free's answer (1).
int32_t __stdcall FreeInstance(Instance *instance);

// FSH, EA's shape files (row 1, 0x87dbad..0x87ec26, lane MP; src/gz/GimexFsh.size.cpp). The instance's unknownC is the
// next frame to write, unknown14 the shape directory the back end reads or fills (still the original's code).
namespace fsh {
About *__stdcall NewAbout();                                                      // 0x87dbad
int32_t __stdcall Probe(ifaces::cIGZFile *stream);                                // 0x87ea4e (is): 100 'ShpF', 0x62 'FntF'
int32_t __stdcall Open(Instance **out, ifaces::cIGZFile *stream, const char *path, int32_t flag); // 0x87ea9f: frames
Info *__stdcall GetInfo(Instance *instance, int32_t frame);                       // 0x87dc98 (a 0x2a7c-byte block)
int32_t __stdcall ReadFrame(Instance *instance, Info *info, void *pixels, int32_t pitch);        // 0x87eb11
int32_t __stdcall Close(Instance *instance);                                      // 0x87dc79
int32_t __stdcall OpenWriter(Instance **out, ifaces::cIGZFile *stream, const char *path, int32_t frames); // 0x87eb94
int32_t __stdcall WriteFrame(Instance *instance, const Info *info, const void *pixels, int32_t pitch); // 0x87ebf5
int32_t __stdcall CloseWriter(Instance *instance);                                // 0x87eb4b (wclose)
} // namespace fsh

// JPEG through the libjpeg in the image (row 2, 0x87ec26..0x87f790: the glue around the library, which stays the
// original's).
namespace jpeg {
About *__stdcall NewAbout();                                                      // 0x87ec26
int32_t __stdcall Probe(ifaces::cIGZFile *stream);                                // 0x87ef8c (is)
int32_t __stdcall Open(Instance **out, ifaces::cIGZFile *stream, const char *path, int32_t flag); // 0x87efdb
Info *__stdcall GetInfo(Instance *instance, int32_t frame);                       // 0x87ed18
int32_t __stdcall ReadFrame(Instance *instance, Info *info, void *pixels, int32_t pitch);        // 0x87f013
int32_t __stdcall WriteFrame(Instance *instance, const Info *info, const void *pixels, int32_t pitch); // 0x87f413
int32_t __stdcall Close(Instance *instance);                                      // 0x87ed09 (close and wclose; PNG's wclose too)
// Helpers.
int32_t __cdecl AlphaMarkerHandler(void *cinfo);                                  // 0x87f177
void __cdecl ConvertReadRow(const uint8_t *source, uint8_t *dest, int32_t count, int32_t components, uint32_t colorSpace,
                            int32_t alphaFirst);                                  // 0x87f255
void __cdecl FixColorSpace(void *cinfo);                                          // 0x87f333
void __cdecl CmykToRgb(const uint8_t *cmyk, uint8_t *out);                        // 0x87f353
void __cdecl DecompressErrorExit(void *cinfo);                                    // 0x87f3b5
void __cdecl CompressErrorExit(void *cinfo);                                      // 0x87f3c9
void __cdecl ConvertWriteRow(const uint8_t *source, uint8_t *dest, int32_t count, uint32_t colorSpace, const Info *info); // 0x87f62d
int32_t __cdecl IsGrayPalette(const uint8_t *palette, int32_t count);             // 0x87f761
} // namespace jpeg

// PNG through the libpng 1.0.5 / zlib in the image (row 4, 0x8800aa..0x880c8e: the glue around the library, which stays
// the original's).
namespace png {
About *__stdcall NewAbout();                                                      // 0x8800be
int32_t __stdcall Probe(ifaces::cIGZFile *stream);                                // 0x8803e9 (is)
int32_t __stdcall Open(Instance **out, ifaces::cIGZFile *stream, const char *path, int32_t flag); // 0x880435
Info *__stdcall GetInfo(Instance *instance, int32_t frame);                       // 0x88019a
int32_t __stdcall ReadFrame(Instance *instance, Info *info, void *pixels, int32_t pitch);        // 0x880536
int32_t __stdcall WriteFrame(Instance *instance, const Info *info, const void *pixels, int32_t pitch); // 0x8807bb
int32_t __stdcall Close(Instance *instance);                                      // 0x880168
// Helpers and the callbacks the library is given.
void __cdecl FreeCallback(void *png, void *memory);                               // 0x8800aa
void *__cdecl AllocateCallback(void *png, uint32_t size);                         // 0x8800b4
void __cdecl ReadCallback(void *png, void *buffer, int32_t count);               // 0x88062e
int32_t __cdecl ReadRows(const Info *info, void *pngInfo, void *png, uint8_t *pixels, int32_t pitch); // 0x88065c
void __cdecl WriteCallback(void *png, const void *buffer, int32_t count);        // 0x880a3a
int32_t __cdecl WriteRows(const Info *info, void *pngInfo, void *png, const uint8_t *pixels, int32_t pitch); // 0x880a68
int32_t __cdecl IsGrayPalette(const uint8_t *palette, int32_t count);             // 0x880c5d
} // namespace png

// JPEG through Intel's IJL (row 3, 0x87f790..0x880168): the DLL load, about, is, open, info, read, write and close.
namespace ijl {
int32_t __cdecl Unload();                                                         // 0x87f790
int32_t __cdecl Load();                                                           // 0x87f7a8
About *__stdcall NewAbout();                                                      // 0x87f864
int32_t __stdcall Probe(ifaces::cIGZFile *stream);                                // 0x87fb72 (is)
int32_t __stdcall Open(Instance **out, ifaces::cIGZFile *stream, const char *path, int32_t flag); // 0x87fbcb
Info *__stdcall GetInfo(Instance *instance, int32_t frame);                       // 0x87f9e0
int32_t __stdcall Close(Instance *instance);                                      // 0x87f9b2
int32_t __stdcall ReadFrame(Instance *instance, Info *info, void *pixels, int32_t pitch);        // 0x87fc52
int32_t __stdcall WriteFrame(Instance *instance, const Info *info, const void *pixels, int32_t pitch); // 0x87fe74
} // namespace ijl

// BMP (row 5, 0x880c8e..0x882958: Windows and OS/2 bitmaps, run-length coded 8-bit ones too).
namespace bmp {
About *__stdcall NewAbout();                                                      // 0x881267
int32_t __stdcall Probe(ifaces::cIGZFile *stream);                                // 0x881730 (is)
int32_t __stdcall Open(Instance **out, ifaces::cIGZFile *stream, const char *path, int32_t flag); // 0x88176a
Info *__stdcall GetInfo(Instance *instance, int32_t frame);                       // 0x881316
int32_t __stdcall ReadFrame(Instance *instance, Info *info, void *pixels, int32_t pitch);        // 0x8817a2
int32_t __stdcall WriteFrame(Instance *instance, const Info *info, const void *pixels, int32_t pitch); // 0x881d40
} // namespace bmp

// TGA (row 6, 0x882380..0x88374d: Targa images, indexed / gray, 15 / 16, 24 and 32 bits, run-length coded ones too).
namespace tga {
About *__stdcall NewAbout();                                                      // 0x882862
int32_t __stdcall Probe(ifaces::cIGZFile *stream);                                // 0x882db5 (is)
int32_t __stdcall Open(Instance **out, ifaces::cIGZFile *stream, const char *path, int32_t flag); // 0x882e91
Info *__stdcall GetInfo(Instance *instance, int32_t frame);                       // 0x882958
int32_t __stdcall ReadFrame(Instance *instance, Info *info, void *pixels, int32_t pitch);        // 0x882ec9
int32_t __stdcall WriteFrame(Instance *instance, const Info *info, const void *pixels, int32_t pitch); // 0x8833ac
} // namespace tga

} // namespace gimex
