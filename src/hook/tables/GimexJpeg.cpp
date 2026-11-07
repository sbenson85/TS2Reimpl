// Gimex's JPEG codec through the libjpeg in the image (row 2 of the codec table; lane MO): about, is, open, info, read,
// write and close, with the row converters and the callbacks the game gives the library. The table's cells are ours
// already (src/gz/Gimex.size.cpp); these hooks keep the original entry points from running.
#include "hook/hooks.h"
#include "gz/GimexFormats.h"

namespace {

const HookSpec kHooks[] = {
    {"GimexJpeg_About", 0x87ec26, 32, 0x500a6509, reinterpret_cast<void *>(&gimex::jpeg::NewAbout)},
    {"GimexJpeg_Close", 0x87ed09, 15, 0x8a786528, reinterpret_cast<void *>(&gimex::jpeg::Close)},
    {"GimexJpeg_Info", 0x87ed18, 32, 0x0ffe8d09, reinterpret_cast<void *>(&gimex::jpeg::GetInfo)},
    {"GimexJpeg_Is", 0x87ef8c, 32, 0x64c64b0d, reinterpret_cast<void *>(&gimex::jpeg::Probe)},
    {"GimexJpeg_Open", 0x87efdb, 32, 0xc58314f5, reinterpret_cast<void *>(&gimex::jpeg::Open)},
    {"GimexJpeg_Read", 0x87f013, 32, 0x8983579d, reinterpret_cast<void *>(&gimex::jpeg::ReadFrame)},
    {"GimexJpeg_AlphaMarker", 0x87f177, 32, 0xeee5b7a8, reinterpret_cast<void *>(&gimex::jpeg::AlphaMarkerHandler)},
    {"GimexJpeg_ConvertReadRow", 0x87f255, 32, 0xbf9beb22, reinterpret_cast<void *>(&gimex::jpeg::ConvertReadRow)},
    {"GimexJpeg_FixColorSpace", 0x87f333, 32, 0x7444baa2, reinterpret_cast<void *>(&gimex::jpeg::FixColorSpace)},
    {"GimexJpeg_CmykToRgb", 0x87f353, 32, 0x8f427143, reinterpret_cast<void *>(&gimex::jpeg::CmykToRgb)},
    {"GimexJpeg_DecompressErrorExit", 0x87f3b5, 20, 0xb0fadd84, reinterpret_cast<void *>(&gimex::jpeg::DecompressErrorExit)},
    {"GimexJpeg_CompressErrorExit", 0x87f3c9, 27, 0x737774de, reinterpret_cast<void *>(&gimex::jpeg::CompressErrorExit)},
    {"GimexJpeg_Write", 0x87f413, 32, 0x276927ec, reinterpret_cast<void *>(&gimex::jpeg::WriteFrame)},
    {"GimexJpeg_ConvertWriteRow", 0x87f62d, 32, 0xaab35aa0, reinterpret_cast<void *>(&gimex::jpeg::ConvertWriteRow)},
    {"GimexJpeg_IsGrayPalette", 0x87f761, 32, 0x4ffd0f1d, reinterpret_cast<void *>(&gimex::jpeg::IsGrayPalette)},
};
REGISTER_HOOKS(kHooks);

} // namespace
