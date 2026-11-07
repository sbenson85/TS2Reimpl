#pragma once
#include <cstdint>

// The vtables of the nGZGraphic4 pixel format objects, emitted by our build (not read from the game
// image): explicit constant tables of our slot functions in the original slot order.
//   * ClassVTable(c):  the vtable of cPixelFormat / cPixelFormatARGB / ... objects.
//   * BaseTVTable(c):  the cPixelFormatBaseT<> instantiation each class derives from. Deleting
//                      destructors write it (so the object is a bare base while its name is freed); it
//                      shares the QueryInterface/Less/id slots and has _purecall in the slots only the
//                      derived class fills.
//   * ManagerVTable(): cPixelFormatsManagerDX (13 slots; CreateIterator, slot 3, is reached through
//                      the PixelFormatStore, gz/PixelFormatStore.h).
// Each function returns a pointer to slot 0. The word before it (where MSVC keeps the RTTI locator) is
// null: the game contains no dynamic_cast/typeid use of these types (findings in PixelFormatStore.h), so
// nothing reads it.
//
// The original vtables these replace, for reference (RVAs, 1.18.0.187): classes 0xd9d130 (base),
// 0xd9d344 ABGRFloat, 0xd9d21c ARGB, 0xd9d2b4 AlphaLuminance, 0xd9d3d8 DXT, 0xd9d4d4 DepthStencil,
// 0xd9d198 Unknown, 0xd9d45c YUV; BaseT 0xd9d0fc, 0xd9d2fc, 0xd9d1cc, 0xd9d26c, 0xd9d38c, 0xd9d494,
// 0xd9d164, 0xd9d424 (same order); manager 0xd9d0c4. The differential tests map ours to these
// (tests/pixel_format_vtables.h).
namespace nGZGraphic4 {

enum class FormatClass : uint8_t { Base, ABGRFloat, ARGB, AlphaLuminance, DXT, DepthStencil, Unknown, YUV };
constexpr int kFormatClassCount = 8;

// Number of slots of each class's vtable (and of its BaseT one), in FormatClass order.
constexpr int kVTableSlots[kFormatClassCount] = {12, 17, 19, 17, 18, 15, 12, 13};
constexpr int kManagerVTableSlots = 13;

const void *ClassVTable(FormatClass c);
const void *BaseTVTable(FormatClass c);
const void *ManagerVTable();

} // namespace nGZGraphic4
