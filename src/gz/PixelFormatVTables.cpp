#include "gz/PixelFormatVTables.h"

#include "gz/PixelFormat.h"
#include "gz/PixelFormatsManager.h"

extern "C" int __cdecl _purecall(void); // the CRT's pure-virtual handler: what the original BaseT tables hold

namespace nGZGraphic4 {

namespace {

// Each slot is a free __fastcall function that forwards to the member the slot names. A __thiscall
// member and a __fastcall free function with a dummy second parameter take `this` in ecx and the
// remaining arguments on the stack and pop them themselves, so they are interchangeable for callers
// that go through a vtable (the same trick the tests' fake vtables use). Taking the member pointer as a
// template argument keeps the table a constant expression of function addresses.
template <typename Pmf, Pmf P> struct SlotThunk;
template <typename R, typename C, typename... A, R (C::*P)(A...)>
struct SlotThunk<R (C::*)(A...), P> {
    static R __fastcall Call(C *self, void *, A... args) { return (self->*P)(args...); }
};

#define SLOT(...) reinterpret_cast<const void *>(&SlotThunk<decltype(__VA_ARGS__), __VA_ARGS__>::Call)
#define PURE reinterpret_cast<const void *>(&_purecall)

// The word before slot 0 is where MSVC keeps the RTTI locator; the game never reads it (no
// dynamic_cast/typeid on these types), so it is null.
template <int N> struct VTable {
    const void *rtti;
    const void *slot[N];
};

#define COMMON_ID SLOT(&cPixelFormat::GetFormatId), SLOT(&cPixelFormat::GetName)
#define ADDREF_RELEASE SLOT(&cPixelFormat::ReturnOne), SLOT(&cPixelFormat::ReturnOne)
#define ONE SLOT(&cPixelFormat::ReturnOne)

// ---------------------------------------------------------------------------------------------------
// The class vtables

constinit const VTable<12> kBaseVTable = {nullptr,
                                          {SLOT(&cPixelFormat::QueryInterface), ADDREF_RELEASE, COMMON_ID, SLOT(&cPixelFormat::GetClassId), ONE, ONE, ONE,
                                           SLOT(&cPixelFormat::GetRowBytes), SLOT(&cPixelFormat::Less), SLOT(&cPixelFormat::DeletingDestructor)}};

constinit const VTable<17> kABGRFloatVTable = {nullptr,
                                               {SLOT(&cPixelFormatABGRFloat::QueryInterface), ADDREF_RELEASE, COMMON_ID, SLOT(&cPixelFormatABGRFloat::GetClassId),
                                                SLOT(&cPixelFormatABGRFloat::GetBitsPerPixel), ONE, ONE, SLOT(&cPixelFormatABGRFloat::GetRowBytes),
                                                SLOT(&cPixelFormatABGRFloat::Less), SLOT(&cPixelFormatABGRFloat::GetAlphaBits), SLOT(&cPixelFormatABGRFloat::GetRedBits),
                                                SLOT(&cPixelFormatABGRFloat::GetGreenBits), SLOT(&cPixelFormatABGRFloat::GetBlueBits),
                                                SLOT(&cPixelFormatABGRFloat::GetChannelBitsOr), SLOT(&cPixelFormatABGRFloat::DeletingDestructor)}};

constinit const VTable<19> kARGBVTable = {nullptr,
                                          {SLOT(&cPixelFormatARGB::QueryInterface), ADDREF_RELEASE, COMMON_ID, SLOT(&cPixelFormatARGB::GetClassId),
                                           SLOT(&cPixelFormatARGB::GetBitsPerPixel), ONE, ONE, SLOT(&cPixelFormatARGB::GetRowBytes), SLOT(&cPixelFormatARGB::Less),
                                           SLOT(&cPixelFormatARGB::GetAlphaBits), SLOT(&cPixelFormatARGB::GetRedBits), SLOT(&cPixelFormatARGB::GetGreenBits),
                                           SLOT(&cPixelFormatARGB::GetBlueBits), SLOT(&cPixelFormatARGB::GetUnusedBits), SLOT(&cPixelFormatARGB::PackColor),
                                           SLOT(&cPixelFormatARGB::UnpackColor), SLOT(&cPixelFormatARGB::DeletingDestructor)}};

constinit const VTable<17> kAlphaLuminanceVTable = {nullptr,
                                                    {SLOT(&cPixelFormatAlphaLuminance::QueryInterface), ADDREF_RELEASE, COMMON_ID,
                                                     SLOT(&cPixelFormatAlphaLuminance::GetClassId), SLOT(&cPixelFormatAlphaLuminance::GetBitsPerPixel), ONE, ONE,
                                                     SLOT(&cPixelFormatAlphaLuminance::GetRowBytes), SLOT(&cPixelFormatAlphaLuminance::Less),
                                                     SLOT(&cPixelFormatAlphaLuminance::GetAlphaBits), SLOT(&cPixelFormatAlphaLuminance::GetLuminanceBits),
                                                     SLOT(&cPixelFormatAlphaLuminance::GetLuminanceBits), SLOT(&cPixelFormatAlphaLuminance::GetLuminanceBits),
                                                     SLOT(&cPixelFormatAlphaLuminance::GetLuminanceBits), SLOT(&cPixelFormatAlphaLuminance::DeletingDestructor)}};

constinit const VTable<18> kDXTVTable = {nullptr,
                                         {SLOT(&cPixelFormatDXT::QueryInterface), ADDREF_RELEASE, COMMON_ID, SLOT(&cPixelFormatDXT::GetClassId),
                                          SLOT(&cPixelFormatDXT::GetBitsPerPixel), SLOT(&cPixelFormatDXT::GetBlockSize), SLOT(&cPixelFormatDXT::GetBlockSize),
                                          SLOT(&cPixelFormatDXT::GetRowBytes), SLOT(&cPixelFormatDXT::Less), SLOT(&cPixelFormatDXT::GetAlphaBits),
                                          SLOT(&cPixelFormatDXT::GetColorBits), SLOT(&cPixelFormatDXT::GetColorBits), SLOT(&cPixelFormatDXT::GetColorBits),
                                          SLOT(&cPixelFormatDXT::GetVersion), SLOT(&cPixelFormatDXT::GetBlockBytes), SLOT(&cPixelFormatDXT::DeletingDestructor)}};

constinit const VTable<15> kDepthStencilVTable = {nullptr,
                                                  {SLOT(&cPixelFormatDepthStencil::QueryInterface), ADDREF_RELEASE, COMMON_ID,
                                                   SLOT(&cPixelFormatDepthStencil::GetClassId), SLOT(&cPixelFormatDepthStencil::GetBitsPerPixel), ONE, ONE,
                                                   SLOT(&cPixelFormatDepthStencil::GetRowBytes), SLOT(&cPixelFormatDepthStencil::Less),
                                                   SLOT(&cPixelFormatDepthStencil::GetDepthBits), SLOT(&cPixelFormatDepthStencil::IsFloatDepth),
                                                   SLOT(&cPixelFormatDepthStencil::GetStencilBits), SLOT(&cPixelFormatDepthStencil::DeletingDestructor)}};

constinit const VTable<12> kUnknownVTable = {nullptr,
                                             {SLOT(&cPixelFormatUnknown::QueryInterface), ADDREF_RELEASE, COMMON_ID, SLOT(&cPixelFormatUnknown::GetClassId),
                                              SLOT(&cPixelFormatUnknown::ReturnZero), SLOT(&cPixelFormatUnknown::ReturnZero), SLOT(&cPixelFormatUnknown::ReturnZero),
                                              SLOT(&cPixelFormatUnknown::GetRowBytes), SLOT(&cPixelFormatUnknown::Less),
                                              SLOT(&cPixelFormatUnknown::DeletingDestructor)}};

constinit const VTable<13> kYUVVTable = {nullptr,
                                         {SLOT(&cPixelFormatYUV::QueryInterface), ADDREF_RELEASE, COMMON_ID, SLOT(&cPixelFormatYUV::GetClassId),
                                          SLOT(&cPixelFormatYUV::GetBitsPerPixel), ONE, SLOT(&cPixelFormatYUV::GetBlockWidth), SLOT(&cPixelFormatYUV::GetRowBytes),
                                          SLOT(&cPixelFormatYUV::Less), SLOT(&cPixelFormatYUV::GetFourCC), SLOT(&cPixelFormatYUV::DeletingDestructor)}};

// ---------------------------------------------------------------------------------------------------
// The cPixelFormatBaseT<> vtables: the id/name/QueryInterface/Less slots, _purecall where the derived
// class fills in a slot, and a deleting destructor that frees only the 0x20-byte base part.

#define BASET_DTOR(cls) SLOT(&cPixelFormat::DeletingDestructorT<FormatClass::cls, cPixelFormat::kSize>)

constinit const VTable<12> kBaseTBaseVTable = {nullptr,
                                               {SLOT(&cPixelFormat::QueryInterface), ADDREF_RELEASE, COMMON_ID, SLOT(&cPixelFormat::GetClassId), PURE, PURE, PURE, PURE,
                                                SLOT(&cPixelFormat::Less), SLOT(&cPixelFormat::DeletingDestructor)}};

constinit const VTable<17> kBaseTABGRFloatVTable = {nullptr,
                                                    {SLOT(&cPixelFormat::QueryInterfaceT<kIidABGRFloat>), ADDREF_RELEASE, COMMON_ID,
                                                     SLOT(&cPixelFormatABGRFloat::GetClassId), PURE, PURE, PURE, PURE, SLOT(&cPixelFormat::Less), PURE, PURE, PURE, PURE,
                                                     PURE, BASET_DTOR(ABGRFloat)}};

constinit const VTable<19> kBaseTARGBVTable = {nullptr,
                                               {SLOT(&cPixelFormat::QueryInterfaceT<kIidARGB>), ADDREF_RELEASE, COMMON_ID, SLOT(&cPixelFormatARGB::GetClassId), PURE,
                                                PURE, PURE, PURE, SLOT(&cPixelFormat::Less), PURE, PURE, PURE, PURE, PURE, PURE, PURE, BASET_DTOR(ARGB)}};

constinit const VTable<17> kBaseTAlphaLuminanceVTable = {nullptr,
                                                         {SLOT(&cPixelFormat::QueryInterfaceT<kIidAlphaLuminance>), ADDREF_RELEASE, COMMON_ID,
                                                          SLOT(&cPixelFormatAlphaLuminance::GetClassId), PURE, PURE, PURE, PURE, SLOT(&cPixelFormat::Less), PURE, PURE,
                                                          PURE, PURE, PURE, BASET_DTOR(AlphaLuminance)}};

constinit const VTable<18> kBaseTDXTVTable = {nullptr,
                                              {SLOT(&cPixelFormat::QueryInterfaceT<kIidDXT>), ADDREF_RELEASE, COMMON_ID, SLOT(&cPixelFormatDXT::GetClassId), PURE, PURE,
                                               PURE, PURE, SLOT(&cPixelFormat::Less), PURE, PURE, PURE, PURE, PURE, PURE, BASET_DTOR(DXT)}};

constinit const VTable<15> kBaseTDepthStencilVTable = {nullptr,
                                                       {SLOT(&cPixelFormatDepthStencil::QueryInterface), ADDREF_RELEASE, COMMON_ID,
                                                        SLOT(&cPixelFormatDepthStencil::GetClassId), PURE, PURE, PURE, PURE, SLOT(&cPixelFormat::Less), PURE, PURE, PURE,
                                                        BASET_DTOR(DepthStencil)}};

// Unknown's own deleting destructor is the BaseT one (it frees 0x20 bytes either way).
constinit const VTable<12> kBaseTUnknownVTable = {nullptr,
                                                  {SLOT(&cPixelFormatUnknown::QueryInterface), ADDREF_RELEASE, COMMON_ID, SLOT(&cPixelFormatUnknown::GetClassId), PURE, PURE,
                                                   PURE, PURE, SLOT(&cPixelFormat::Less), SLOT(&cPixelFormatUnknown::DeletingDestructor)}};

constinit const VTable<13> kBaseTYUVVTable = {nullptr,
                                              {SLOT(&cPixelFormatYUV::QueryInterface), ADDREF_RELEASE, COMMON_ID, SLOT(&cPixelFormatYUV::GetClassId), PURE, PURE, PURE,
                                               PURE, SLOT(&cPixelFormat::Less), PURE, BASET_DTOR(YUV)}};

// ---------------------------------------------------------------------------------------------------
// The manager

constinit const VTable<13> kManagerVTable = {nullptr,
                                             {SLOT(&cPixelFormatsManagerDX::QueryInterface), SLOT(&cPixelFormatsManagerDX::ReturnOne),
                                              SLOT(&cPixelFormatsManagerDX::ReturnOne), SLOT(&cPixelFormatsManagerDX::CreateIterator),
                                              SLOT(&cPixelFormatsManagerDX::GetFormat), SLOT(&cPixelFormatsManagerDX::GetUnknownFormat),
                                              SLOT(&cPixelFormatsManagerDX::GetARGB), SLOT(&cPixelFormatsManagerDX::GetAlphaLuminance),
                                              SLOT(&cPixelFormatsManagerDX::GetABGRFloat), SLOT(&cPixelFormatsManagerDX::GetYUV),
                                              SLOT(&cPixelFormatsManagerDX::GetDXT), SLOT(&cPixelFormatsManagerDX::GetDepthStencil),
                                              SLOT(&cPixelFormatsManagerDX::DeletingDestructor)}};

#undef SLOT
#undef PURE

// Every table has the slot count the original's has (the tests also compare each slot's behaviour).
static_assert(sizeof(kBaseVTable.slot) / sizeof(void *) == kVTableSlots[int(FormatClass::Base)]);
static_assert(sizeof(kABGRFloatVTable.slot) / sizeof(void *) == kVTableSlots[int(FormatClass::ABGRFloat)]);
static_assert(sizeof(kARGBVTable.slot) / sizeof(void *) == kVTableSlots[int(FormatClass::ARGB)]);
static_assert(sizeof(kAlphaLuminanceVTable.slot) / sizeof(void *) == kVTableSlots[int(FormatClass::AlphaLuminance)]);
static_assert(sizeof(kDXTVTable.slot) / sizeof(void *) == kVTableSlots[int(FormatClass::DXT)]);
static_assert(sizeof(kDepthStencilVTable.slot) / sizeof(void *) == kVTableSlots[int(FormatClass::DepthStencil)]);
static_assert(sizeof(kUnknownVTable.slot) / sizeof(void *) == kVTableSlots[int(FormatClass::Unknown)]);
static_assert(sizeof(kYUVVTable.slot) / sizeof(void *) == kVTableSlots[int(FormatClass::YUV)]);
static_assert(sizeof(kBaseTBaseVTable.slot) / sizeof(void *) == kVTableSlots[int(FormatClass::Base)]);
static_assert(sizeof(kBaseTABGRFloatVTable.slot) / sizeof(void *) == kVTableSlots[int(FormatClass::ABGRFloat)]);
static_assert(sizeof(kBaseTARGBVTable.slot) / sizeof(void *) == kVTableSlots[int(FormatClass::ARGB)]);
static_assert(sizeof(kBaseTAlphaLuminanceVTable.slot) / sizeof(void *) == kVTableSlots[int(FormatClass::AlphaLuminance)]);
static_assert(sizeof(kBaseTDXTVTable.slot) / sizeof(void *) == kVTableSlots[int(FormatClass::DXT)]);
static_assert(sizeof(kBaseTDepthStencilVTable.slot) / sizeof(void *) == kVTableSlots[int(FormatClass::DepthStencil)]);
static_assert(sizeof(kBaseTUnknownVTable.slot) / sizeof(void *) == kVTableSlots[int(FormatClass::Unknown)]);
static_assert(sizeof(kBaseTYUVVTable.slot) / sizeof(void *) == kVTableSlots[int(FormatClass::YUV)]);
static_assert(sizeof(kManagerVTable.slot) / sizeof(void *) == kManagerVTableSlots);

} // namespace

const void *ClassVTable(FormatClass c)
{
    static const void *const tables[kFormatClassCount] = {kBaseVTable.slot,         kABGRFloatVTable.slot,    kARGBVTable.slot, kAlphaLuminanceVTable.slot,
                                                          kDXTVTable.slot,          kDepthStencilVTable.slot, kUnknownVTable.slot, kYUVVTable.slot};
    return tables[static_cast<int>(c)];
}

const void *BaseTVTable(FormatClass c)
{
    static const void *const tables[kFormatClassCount] = {kBaseTBaseVTable.slot,         kBaseTABGRFloatVTable.slot,    kBaseTARGBVTable.slot,
                                                          kBaseTAlphaLuminanceVTable.slot, kBaseTDXTVTable.slot,        kBaseTDepthStencilVTable.slot,
                                                          kBaseTUnknownVTable.slot,      kBaseTYUVVTable.slot};
    return tables[static_cast<int>(c)];
}

const void *ManagerVTable()
{
    return kManagerVTable.slot;
}

} // namespace nGZGraphic4
