// Owned statics for image-free programs (docs/standalone-shape.md, "Owned statics"). In the hooked game these objects
// are the image's (tools/origsyms.py defines each `ts2_rva_<hex>` at game base + RVA); ts2core_noimage drops those
// definitions, so a program linked against it defines them here, zero-initialised like the image's .bss, and runs from
// its own start-up what initialises them in the game.
#include "app/AppServices2.h"
#include "gz/cGZDBReadRecord.h"
#include "gz/cGZDBSerialRecordAsIGZFile.h"
#include "gz/cGZDBWriteRecordPackedFile.h"
#include "gz/cGZDBSegmentPackedFile.h"
#include "gz/cGZPersistBinaryReader.h"
#include "gz/cGZPersistDBSerialRecord.h"
#include "gz/cGZPropertySet.h"
#include "gz/cIStreamStringBehaviourAdaptor.h"
#include "gz/ExpressionEvaluator.h"
#include "gz/cBaseParser.h"
#include "gz/CommandParser.h"
#include "gz/MacroDefinition.h"
#include "gz/cParserTexts.h"
#include "gz/cOStreamStringBehaviourAdaptor.h"
#include "misc/UtilitiesC.h"
#include "rz/RZFastFixedAllocators.h"
#include "sg/DetailObject.h"
#include "sg/SGBehaviorCopies.h"
#include "sg/cRenderTargetTexture.h"
#include "sg/cSceneGraphFactory.h"
#include "sg/cShape.h"
#include "sg/cTextureManager.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZBlockPool.h"
#include "rz/PathFileOps.h"
#include "rz/RZFileStreams.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/RZMessageServer.h"
#include "rz/cRZRefCount.h"
#include "rz/RZTextConvert.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"
#include "rz/StringKeyParser.h"
#include "rz/WinApiTables.h"
#include "rz/cGZAllocatorService.h"
#include "rz/cRZCriticalSection.h"
#include "rz/cRZFastCompression3.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"
#include "rz/cRZStd.h"
#include "rz/cRZStopwatchCalibration.h"
#include "rz/cRZVariant.h"
#include "rz/DirectoryScan.h"
#include "std/StdInstances.h"

#include <windows.h>

// RZMemory's general allocator (built in game by an XC static initialiser, destroyed by its atexit entry).
TS2_OWNED_STATIC(GeneralAllocator, 10cb098){};

// RZMemory's start-up slot pointing at the general allocator (nothing reads it; the address is the value).
TS2_OWNED_STATIC(void *, 10cb5b0) = &ts2_rva_10cb098;

// The allocator's default assert and trace callbacks, stored in the object by its constructor (GeneralAllocator.h).
#pragma comment(linker, "/alternatename:_ts2_rva_1c6970=?DefaultAssert@GeneralAllocator@@SAXPBDPAX@Z")
#pragma comment(linker, "/alternatename:_ts2_rva_1c7cac=?DebugString@GeneralAllocator@@SAXPBD@Z")

// The allocator service: its instance and guard, and the callers' cached pointer and guard (cGZAllocatorService.h).
TS2_OWNED_STATIC(void *, 10cb5b4){};
TS2_OWNED_STATIC(int, 10cb5b8){};
TS2_OWNED_STATIC(void *, 10c95d4){};
TS2_OWNED_STATIC(int, 10c95d8){};

// Files, compression, keys. The CRC tables fill on first use (StringKeyParser.cpp), so they start as .bss does.
TS2_OWNED_STATIC(uint32_t, 10f51d0)[256]{};
TS2_OWNED_STATIC(uint32_t, 10f55d0)[256]{};
TS2_OWNED_STATIC(uint8_t, 10f59d0){};
TS2_OWNED_STATIC(void *, 10f8f2c){};                // the key parser's cached service object
TS2_OWNED_STATIC(void *, 10f9070){};                // GetCachedObjectB's cache
TS2_OWNED_STATIC(uint32_t, 10f5030) = 0x46504244;   // 'FPBD': the packed-file header magic
TS2_OWNED_STATIC(const float, db13e8) = 0.8f;       // keep compressed record data when it is under this share of the original
TS2_OWNED_STATIC(const uint32_t, e5f440) = 0x59535542; // "BUSY": written at offset 0 of the file while it is being changed

// The key parser's name table: 14 texts sorted by the hash CRC24(lowercased text) | 0xff000000 (checked at compile time below).
#define TS2_NAME_TABLE_ENTRIES                                                                       \
    {0xff099215, "paintinghorizontallandscape_canvasback_txmt"},                                      \
    {0xff1780dc, "aftophalter-dots0_lifo"},                                                           \
    {0xff216be1, "aftophalter-dots1_lifo"},                                                           \
    {0xff287e4f, "afhairmeg_black_txmt"},                                                             \
    {0xff3529ce, "ambodysuperchef_japanblack_reflected_txmt"},                                        \
    {0xff3ac8a6, "a-teach-see_anim"},                                                                 \
    {0xff44b838, "o2a-counter-sandwich2tray-door-counter-animation_anim"},                            \
    {0xff633703, "o-accessorytoolswrench-maintain-stop-lhand_anim"},                                  \
    {0xff795b54, "afhairhatfirefighter_kinky_red_txmt"},                                              \
    {0xff879a2f, "wall-wallpaperwcoffer-bump_txtr"},                                                  \
    {0xff952348, "afhairmeg_blond_txmt"},                                                             \
    {0xffa7c99f, "amhairpgskater_black_alpha3_txmt"},                                                 \
    {0xffcd70b3, "wall-wallpaperwcoffer-bump0_lifo"},                                                 \
    {0xfffb9b8e, "wall-wallpaperwcoffer-bump1_lifo"}
TS2_OWNED_STATIC(StringKeyParser::HashedText, 10718c8)[14] = {TS2_NAME_TABLE_ENTRIES};

namespace {

// The table's hashes are CRC24 of the lowercased text (the parser's own function, StringKeyParser.cpp), | 0xff000000;
// checked here at compile time so that the data above cannot drift from its texts.
constexpr uint32_t ConstexprCrc24(const char *text)
{
    uint32_t h = 0xb704ce;
    for (const char *p = text; *p; ++p) {
        uint32_t c = static_cast<uint32_t>(*p);
        if (c >= 'A' && c <= 'Z') c += 32;
        uint32_t v = ((h >> 16) ^ c) & 0xff;
        v <<= 16;
        for (int k = 0; k < 8; ++k) {
            v <<= 1;
            if (v & 0x1000000) v ^= 0x1864cfb;
        }
        h = (h << 8) ^ v;
    }
    return h & 0xffffff;
}

constexpr bool NameTableHashesMatch(const StringKeyParser::HashedText *table, int count)
{
    for (int i = 0; i < count; ++i) {
        if (table[i].hash != (ConstexprCrc24(table[i].text) | 0xff000000)) return false;
        if (i > 0 && table[i - 1].hash >= table[i].hash) return false; // sorted: the lookup is a bsearch
    }
    return true;
}

constexpr StringKeyParser::HashedText kNameTableCheck[14] = {TS2_NAME_TABLE_ENTRIES};
static_assert(NameTableHashesMatch(kNameTableCheck, 14), "name table hashes");

} // namespace

// The wide file function table (filled below).
TS2_OWNED_STATIC(rz::WideFileFunctions, 10cb5e0){};

// The wide directory function table (filled below), the as2 directory helpers' import slots (kernel32's own entries) and
// their wildcard text pointer (AppServices2.h).
TS2_OWNED_STATIC(rz::WideDirectoryFunctions, 10cb614){};
TS2_OWNED_STATIC(void *, d9a1f8) = reinterpret_cast<void *>(::FindFirstFileA);
TS2_OWNED_STATIC(void *, d9a1f4) = reinterpret_cast<void *>(::FindClose);
TS2_OWNED_STATIC(const char *, ff2478) = "*";

// PathDelete's DeleteFileA and GetTempDirectory's GetTempPathA import slots (rz/PathFileOps.h; the wide functions they
// prefer are in the table above), and RZText::SystemCodePage's cache (-3: not read yet).
TS2_OWNED_STATIC(void *, d9a1f0) = reinterpret_cast<void *>(::DeleteFileA);
TS2_OWNED_STATIC(void *, d9a21c) = reinterpret_cast<void *>(::GetTempPathA);
TS2_OWNED_STATIC(int32_t, ff23a4) = -3;

// cRZCriticalSection's TryEnterCriticalSection (in game set by an XC loader; every supported Windows has it).
TS2_OWNED_STATIC(cRZCriticalSection::TryEnterFn, 10caea0) = ::TryEnterCriticalSection;

// cGZDBSerialRecordAsIGZFile's GetPath: the function-local static path string (a cRZString built on first use) and its guard.
TS2_OWNED_STATIC(uint8_t, 10f500c)[0x20]{};
TS2_OWNED_STATIC(int, 10f502c){};

// The framework global (g_Framework and its second reference; gmdc_decode's StandUp stores the framework) and the root COM
// director's storage (built in place by GetCOMDirector, 0x64 bytes) with its init guard.
TS2_OWNED_STATIC(void *, 10c9378){};
TS2_OWNED_STATIC(void *, 10c937c){};
// The cached message server (rz/RZMessageServer.h; lane RW).
TS2_OWNED_STATIC(void *, 10f519c){};
// cRZRefCount's table (rz/cRZRefCountTable.cpp; lane RW).
#pragma comment(linker, "/alternatename:_ts2_rva_da2fa8=_ts2_own_vt_da2fa8")
// cRZStd's table (rz/cRZStdTable.cpp; lane FA).
#pragma comment(linker, "/alternatename:_ts2_rva_da3e6c=_ts2_own_vt_da3e6c")
// cVectorEvent<cVector<4, float>>'s table and cIEventHeader's (gz/cVector4fEvent.cpp; lane FD).
#pragma comment(linker, "/alternatename:_ts2_rva_db07d4=_ts2_own_vt_db07d4")
#pragma comment(linker, "/alternatename:_ts2_rva_d9ca58=_ts2_own_vt_d9ca58")
#pragma comment(linker, "/alternatename:_ts2_rva_d9b644=_ts2_own_vt_d9b644") // cRZLock
TS2_OWNED_STATIC(COMDirectorStorage, 10c9390){};
TS2_OWNED_STATIC(int, 10c93f4){};

// Owned vtables (owned.h, OwnedVTable): each symbol is our own class's vtable, the rzface face whose slots run our
// members. OwnVTable() is referenced below so the face's vtable is emitted in this program.
#pragma comment(linker, "/alternatename:_ts2_rva_d9b470=??_7cRZString@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_da2708=??_7cRZCriticalSection@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_da42a0=??_7cRZFile@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_e61b48=??_7cRZFastCompression3@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_e5e8c4=??_7cGZPersistDBSegment@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_e5fc58=??_7cGZDBSegmentPackedFile@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_e5fd48=??_7cGZDBSegmentPackedFileSub@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_e5f45c=??_7cIGZDBSegmentPackedFile@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_e5f534=??_7cGZDBReadRecordPackedFile@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_e5f644=??_7cGZDBReadRecordPackedFileRecord@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_e5f69c=??_7cGZDBReadRecordPackedFileOStream@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_e5f700=??_7cGZDBReadRecordPackedFileIStream@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_e5f764=??_7cGZDBReadRecordRAM@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_e5f868=??_7cGZDBReadRecordRAMRecord@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_e5f8c0=??_7cGZDBReadRecordRAMOStream@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_e5f924=??_7cGZDBReadRecordRAMIStream@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_e5f988=??_7cGZDBWriteRecordPackedFile@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_e5fa90=??_7cGZDBWriteRecordPackedFileRecord@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_e5fae8=??_7cGZDBWriteRecordPackedFileOStream@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_e5fb4c=??_7cGZDBWriteRecordPackedFileIStream@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_e5fbb0=??_7cGZDBSerialRecordAsIGZFile@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_e5fbd0=??_7cGZDBSerialRecordAsIGZFileFile@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_e60f34=??_7cGZPersistBinaryReader@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_e5e990=??_7cGZPersistDBRecord@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_da29bc=??_7cRZOStream@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_da2a20=??_7cRZIStream@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_da6098=??_7cGZPersistDBSerialRecord@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_da6194=??_7cGZPersistDBSerialRecordRecord@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_da61ec=??_7cGZPersistDBSerialRecordOStream@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_da6250=??_7cGZPersistDBSerialRecordIStream@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_e5e7dc=??_7cIGZPersistDBSerialRecord@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_e6c648=??_7cIStreamStringBehaviourAdaptor@rzface@@6B@")
#pragma comment(linker, "/alternatename:_ts2_rva_e6c5e4=??_7cOStreamStringBehaviourAdaptor@rzface@@6B@")
extern "C" const void *const ts2_own_vtables[] = {cRZString::OwnVTable(), cRZCriticalSection::OwnVTable(),
                                                 cRZFile::OwnVTable(), cRZFastCompression3::OwnVTable(),
                                                 cGZPersistDBSegment::OwnVTable(), cGZDBSegmentPackedFile::OwnVTable(),
                                                 cGZDBSegmentPackedFile::OwnSubVTable(),
                                                 cGZDBSegmentPackedFile::OwnInterfaceVTable(),
                                                 cGZDBReadRecordPackedFile::OwnVTable(), cGZDBReadRecordPackedFile::OwnRecordVTable(),
                                                 cGZDBReadRecordPackedFile::OwnOStreamVTable(), cGZDBReadRecordPackedFile::OwnIStreamVTable(),
                                                 cGZDBReadRecordRAM::OwnVTable(), cGZDBReadRecordRAM::OwnRecordVTable(),
                                                 cGZDBReadRecordRAM::OwnOStreamVTable(), cGZDBReadRecordRAM::OwnIStreamVTable(),
                                                 cGZDBWriteRecordPackedFile::OwnVTable(), cGZDBWriteRecordPackedFile::OwnRecordVTable(),
                                                 cGZDBWriteRecordPackedFile::OwnOStreamVTable(), cGZDBWriteRecordPackedFile::OwnIStreamVTable(),
                                                 cGZDBSerialRecordAsIGZFile::OwnVTable(), cGZDBSerialRecordAsIGZFile::OwnFileVTable(),
                                                 cGZPersistBinaryReader::OwnVTable(),
                                                 cGZPersistDBRecord::OwnVTable(), cRZOStream::OwnVTable(), cRZIStream::OwnVTable(),
                                                 cGZPersistDBSerialRecord::OwnVTable(), cGZPersistDBSerialRecord::OwnRecordVTable(),
                                                 cGZPersistDBSerialRecord::OwnOStreamVTable(),
                                                 cGZPersistDBSerialRecord::OwnIStreamVTable(),
                                                 cGZPersistDBSerialRecord::OwnInterfaceVTable(),
                                                 cIStreamStringBehaviourAdaptor::OwnVTable(),
                                                 cOStreamStringBehaviourAdaptor::OwnVTable()};

// The scene-graph factory's singleton (sg/cSceneGraphFactory.h; null until its first Get).
TS2_OWNED_STATIC(nGZSceneGraph::cSceneGraphFactory *, 10f61b0){};

// The scene graph's behaviour copies' connection-manager cells (sg/SGBehaviorCopies.h): zero in the image's .data, each
// filled by its copy's GetEventManager on first use; nothing initialises them at start-up.
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f5f98){}; // cICompositionTreeNode
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f5ffc){}; // cITransformNode
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f616c){}; // cIExplicitBoundNode
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10fc820){}; // cIRenderableNode
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f6080){}; // cIGeometryNode
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10da038){}; // cILight
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10da480){}; // cIStandardLight
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f614c){}; // cIDrawStateLight
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f6148){}; // cILinearFogLight
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f6144){}; // cITextureLight
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f6324){}; // cIShape
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f62dc){}; // cIShapeRefNode
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f6380){}; // cILightRefNode
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f63e0){}; // cIViewerRefNode
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f6424){}; // cIViewerRefNodeRecursive
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f63dc){}; // cIObjectGraphNode
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f6628){}; // cIDisplayList
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f6998){}; // cIMaterialManager
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f8c50){}; // cITextureManager
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10d9c7c){}; // nTSSG::cICameraEffectsSupport
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f61e8){}; // cSceneNode
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f61fc){}; // cResourceNode
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f6010){}; // cExtension
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f60f4){}; // cGeometryBuilder
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f624c){}; // cLooseOctreePartitionNode

// The scene-graph classes' fixed-size pools (constructed here, as the game's static initialisers construct them;
// sizes from app/StaticInitAllocators.cpp).
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x9c>, 10f5f54){}; // cCompositionTreePreorderIterator
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x9c>, 10f5f60){}; // cConstCompositionTreePreorderIterator
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x9c>, 10f5f6c){}; // cCompositionTreePostorderIterator
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x9c>, 10f5f78){}; // cConstCompositionTreePostorderIterator
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x10>, 10f6020){}; // cGeometrySubsetNameIterator
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0xc>, 10f602c){};  // cSubsetGeomPartIndexIterator
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0xc>, 10f617c){};  // cSceneNodeIterator
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x10>, 10f625c){}; // cShapeRefAttributeNameIterator
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0xc>, 10f62f0){};  // cSubsetBindingIterator
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x14>, 10f6340){}; // cLightRefAttributeNameIterator
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x14>, 10f639c){}; // cViewerRefAttributeNameIterator
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x98>, 10f6654){}; // cShader
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x58>, 10f6704){}; // cMaterialDefinition
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x10>, 10f6818){}; // cMaterialHandleIterator
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0xc>, 10f6824){};  // cRenderShaderContextIterator
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0xc>, 10f6830){};  // cShaderIterator
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x10>, 10f8bd4){}; // cTextureHandleIterator
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x10>, 10f8c60){}; // cStringHandleIterator

// The game's identity frames (sg/cDisplayList.h): word 0 zero, the identity matrix, no offset; in the image's .data and
// only read.
TS2_OWNED_STATIC(const AffineTransform3, 106e3d8){0, {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}, {}};
TS2_OWNED_STATIC(const AffineTransform3, 10728c8){0, {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}, {}};

// The viewer reference's start vector (sg/cViewerRefNodeBaseImpl.cpp) and the Lua wrapper caches (misc/UtilitiesE.cpp):
// zero in the image; the caches are filled on first use.
TS2_OWNED_STATIC(float, 10f8d1c)[4]{};
TS2_OWNED_STATIC(void *, 10f8fcc){};
TS2_OWNED_STATIC(void *, 10f8fd0){};

// The shape's preferred-level fallback and its guard (sg/cShape.h), the texture manager and detail object caches
// (sg/cTextureManager.h, sg/DetailObject.h): zero in the image, filled on first use.
TS2_OWNED_STATIC(void *, 10f6310){};
TS2_OWNED_STATIC(int, 10f6314){};
TS2_OWNED_STATIC(void *, 10f8f94){};
TS2_OWNED_STATIC(void *, 10f9028){};

// The clear colour a new render target starts with (sg/cRenderTargetTexture.h): transparent black in the image's .data;
// nothing in the game writes it.
TS2_OWNED_STATIC(uint32_t, 10f675c)[4]{};

// The floor colour grid's colour off the lit floor (sg/cFloorColorGrid.cpp, GetColor): black in the image's .bss and only
// read.
TS2_OWNED_STATIC(const float, 10da6fc)[3]{};

// The display list's log texts (sg/cDisplayListLog.cpp, sg/cDisplayListDraw.cpp): the item lines' labels, the lights'
// table, the formats and the shader context's description.
TS2_OWNED_STATIC(const char, e75524)[] = "   -----View (viewer render type = ";
TS2_OWNED_STATIC(const char, e7551c)[] = ")-----\n";
TS2_OWNED_STATIC(const char, e75548)[] = "   Num geom parts: ";
TS2_OWNED_STATIC(const char, e75564)[] = "      >>>";
TS2_OWNED_STATIC(const char, e7555c)[] = "<<<\n";
TS2_OWNED_STATIC(const char, e75570)[] = "      Layer:                 ";
TS2_OWNED_STATIC(const char, e75590)[] = "      Shader:                ";
TS2_OWNED_STATIC(const char, e755bc)[] = "      Material:              '";
TS2_OWNED_STATIC(const char, e755b0)[] = "', Handle: ";
TS2_OWNED_STATIC(const char, e755dc)[] = "      Geometry:              ";
TS2_OWNED_STATIC(const char, e755fc)[] = "      Global part index:     ";
TS2_OWNED_STATIC(const char, e7561c)[] = "      Part index:            ";
TS2_OWNED_STATIC(const char, e7563c)[] = "      Flags:                ";
TS2_OWNED_STATIC(const char, e7565c)[] = " 0";
TS2_OWNED_STATIC(const char, e75660)[] = " kFlagRenderAlways";
TS2_OWNED_STATIC(const char, e75674)[] = " kFlagClippingRequired";
TS2_OWNED_STATIC(const char, e7568c)[] = " kFlagModifiedThisFrame";
TS2_OWNED_STATIC(const char, e756a4)[] = " kFlagIlluminatePerPart";
TS2_OWNED_STATIC(const char, e756bc)[] = "      Alpha sort:            ";
TS2_OWNED_STATIC(const char, dc5190)[] = ", ";
TS2_OWNED_STATIC(const char, e756dc)[] = "      Renderable node:       ";
TS2_OWNED_STATIC(const char, e756fc)[] = "      Last modified frame:   ";
TS2_OWNED_STATIC(const char, e7571c)[] = "      Render shader context: ";
TS2_OWNED_STATIC(const char, e75824)[] = "      Ambient:               ";
TS2_OWNED_STATIC(const char, e75764)[] = "      Lights:                ";
TS2_OWNED_STATIC(const char, e75784)[] = "      Address:               ";
TS2_OWNED_STATIC(const char, e757a4)[] = "      Diffuse color:         ";
TS2_OWNED_STATIC(const char, e757c4)[] = "      Specular color:        ";
TS2_OWNED_STATIC(const char, e757e4)[] = "      Falloff, range:        ";
TS2_OWNED_STATIC(const char, e75804)[] = "      Cone angle, penumbra:  ";
TS2_OWNED_STATIC(const char, e75760)[] = "NA";
TS2_OWNED_STATIC(const char, db1fcc)[] = "%08x";
TS2_OWNED_STATIC(const char, e7573c)[] = "%.2g %.2g %.2g";
TS2_OWNED_STATIC(const char, e7574c)[] = "%.2g";
TS2_OWNED_STATIC(const char, e75754)[] = "%.2g, %.2g";
TS2_OWNED_STATIC(const char, e7d4b0)[] = "vf:%s vrt:%d tfs:%d morphs:%d";
TS2_OWNED_STATIC(const char, e7d4d0)[] = " flags:";
TS2_OWNED_STATIC(const char, e7d4d8)[] = "hwvp";
TS2_OWNED_STATIC(const char, e74ea4)[] = "skin";
TS2_OWNED_STATIC(const char, e799f4)[] = "morph";
TS2_OWNED_STATIC(const char, dbcb18)[] = "blend";
TS2_OWNED_STATIC(const char, e7d4e0)[] = "hwvp|";
TS2_OWNED_STATIC(const char, e7d4e8)[] = "skin|";
TS2_OWNED_STATIC(const char, e7d4f0)[] = "morph|";
TS2_OWNED_STATIC(const char, e7d4f8)[] = "blend|";
TS2_OWNED_STATIC(const char, e7d500)[] = " lights:";
TS2_OWNED_STATIC(const char, e7d50c)[] = "%s %d%s";
TS2_OWNED_STATIC(const char, daa6c0)[] = ",";

// The records cImageData's slots 18 and 19 answer (sg/cImageData.cpp).
TS2_OWNED_STATIC(const char, db0818)[] = "_txtr";
TS2_OWNED_STATIC(const char, da6450)[] = "6tx";

// The empty text, and the texts cRZVariant's conversions parse and print with (cRZString.h, cRZVariant.h).
TS2_OWNED_STATIC(const char, d9b9d0)[] = "";
TS2_OWNED_STATIC(const char, df42c4)[] = "t";
TS2_OWNED_STATIC(const char, db137c)[] = "f";
TS2_OWNED_STATIC(const char, da2d44)[] = "true";
TS2_OWNED_STATIC(const char, da2d3c)[] = "false";
TS2_OWNED_STATIC(const char, da4af4)[] = "0";
TS2_OWNED_STATIC(const char, da4fac)[] = "1";
TS2_OWNED_STATIC(const char, dd41a0)[] = "True";
TS2_OWNED_STATIC(const char, dd41a8)[] = "False";
TS2_OWNED_STATIC(const char, da9bd8)[] = "0x";
TS2_OWNED_STATIC(const char, e61a20)[] = "0x%02hhx";
TS2_OWNED_STATIC(const char, e61a2c)[] = "0x%04hx";
TS2_OWNED_STATIC(const char, dacd24)[] = "0x%08lx";
TS2_OWNED_STATIC(const char, d9d510)[] = "%u";
TS2_OWNED_STATIC(const char, d9b648)[] = "%d";
TS2_OWNED_STATIC(const char, da2994)[] = "%llu";
TS2_OWNED_STATIC(const char, dce3c8)[] = "%f";
TS2_OWNED_STATIC(const char, da3e60)[] = "0x%08x";

// The variant-tag to field-code table the serial record's SetFieldVariant reads (cGZPersistDBSerialRecord.h).
TS2_OWNED_STATIC(const uint32_t, e5e788)[20] = {0, 0xb, 1, 5, 2, 6, 3, 7, 4, 8, 9, 0xa, 0x11, 0x10, 0xc, 0x12, 0xd, 0xe, 0xf, 0x13};

// The command parser's texts (gz/cParserTexts.h; "\n", d9e644, is owned_graphics.cpp's). The separators and keywords are
// the language's and exact; the messages are ours, with the image's printf arguments in the same order.
TS2_OWNED_STATIC(const char, d9f86c)[] = "'\n";
TS2_OWNED_STATIC(const char, da9e7c)[] = ":";
TS2_OWNED_STATIC(const char, db12b0)[] = ")";
TS2_OWNED_STATIC(const char, db12b4)[] = "(";
TS2_OWNED_STATIC(const char, dddf9c)[] = "'";
TS2_OWNED_STATIC(const char, df7240)[] = "\n\t";
TS2_OWNED_STATIC(const char, e7eb00)[] = "' = '";
TS2_OWNED_STATIC(const char, e7f458)[] = "'#>' has no '#<' before it"; // own text
TS2_OWNED_STATIC(const char, e7f2a4)[] = "Unknown command";
TS2_OWNED_STATIC(const char, e7f2b4)[] = "%s\n      in command '%s' (parser temporary state cleared)"; // own text
TS2_OWNED_STATIC(const char, e7f2f4)[] = "%s (exception of unknown type)\n      in command '%s'"; // own text
TS2_OWNED_STATIC(const char, e7f1fc)[] = "%s(%d): error: ";
TS2_OWNED_STATIC(const char, e7f20c)[] = "Unbalanced block commands in '%s': the 'end's do not match the blocks.\n"; // own text
TS2_OWNED_STATIC(const char, e7f250)[] = "Unbalanced meta command in '%s': a closing tag (endif, enddef...) is missing\n"; // own text
TS2_OWNED_STATIC(const char, e7f1a4)[] = "Unsupported path type: %s"; // own text
TS2_OWNED_STATIC(const char, e7f1c0)[] = "File not found";
TS2_OWNED_STATIC(const char, e7f1d0)[] = "Errors while parsing files\n"; // own text
TS2_OWNED_STATIC(const char, e7f1e8)[] = "\n\tException of unknown type"; // own text
TS2_OWNED_STATIC(const char, e7f328)[] = "Not inside a meta command block"; // own text
TS2_OWNED_STATIC(const char, e7f34c)[] = "entering namespace '";
TS2_OWNED_STATIC(const char, e7f364)[] = "leaving namespace '";
TS2_OWNED_STATIC(const char, e7f378)[] = "purging namespace '";
TS2_OWNED_STATIC(const char, e7f38c)[] = "looking up variable '"; // own text
TS2_OWNED_STATIC(const char, e7f3a0)[] = ": found, value '"; // own text
TS2_OWNED_STATIC(const char, e7f3b8)[] = ": not found\n"; // own text
TS2_OWNED_STATIC(const char, e7e210)[] = "internal buffer overflow"; // own text
TS2_OWNED_STATIC(const char, e7f3c8)[] = "variable name missing"; // own text
TS2_OWNED_STATIC(const char, e7f3e0)[] = "invalid variable name"; // own text
TS2_OWNED_STATIC(const char, e7f3f4)[] = "'}' missing"; // own text
TS2_OWNED_STATIC(const char, e7f400)[] = "Unknown variable '%s'"; // own text
TS2_OWNED_STATIC(const char, e79e14)[] = "expression is not a scalar"; // own text
TS2_OWNED_STATIC(const char, e7f418)[] = "expression has too many elements"; // own text
TS2_OWNED_STATIC(const char, e7f438)[] = "expression has too few elements"; // own text
TS2_OWNED_STATIC(const char, de7c1c)[] = "expression is not 3 or 4 floats"; // own text
TS2_OWNED_STATIC(const char, e7f6d0)[] = ".-+(";
TS2_OWNED_STATIC(const char, e7f718)[] = "and";
TS2_OWNED_STATIC(const char, e7f71c)[] = "or";
TS2_OWNED_STATIC(const char, e5c4d4)[] = "not";
TS2_OWNED_STATIC(const char, e7f6ac)[] = "unexpected text after the expression: '%s'"; // own text
TS2_OWNED_STATIC(const char, e7f6d8)[] = "invalid numerical expression '%s'"; // own text
TS2_OWNED_STATIC(const char, e7f6f8)[] = "sequence has too many elements"; // own text
TS2_OWNED_STATIC(const char, e7f720)[] = "'=' expected"; // own text
TS2_OWNED_STATIC(const char, e7f730)[] = "division by zero";
TS2_OWNED_STATIC(const char, e7f744)[] = "Unknown operand: ";
TS2_OWNED_STATIC(const char, e7f758)[] = "invalid constant";
TS2_OWNED_STATIC(const char, e7f76c)[] = "'\"' missing"; // own text
TS2_OWNED_STATIC(const char, e7f778)[] = "'%c' expected"; // own text

// The command parser's commands (gz/cParserTexts.h; lane RS): names, words, separators and formats exact, help
// texts and messages ours (same printf arguments, help texts keep the leading '*').
TS2_OWNED_STATIC(const char, da2cd8)[] = ": ";
TS2_OWNED_STATIC(const char, da4ca4)[] = " ";
TS2_OWNED_STATIC(const char, df732c)[] = ")\n";
TS2_OWNED_STATIC(const char, dfd044)[] = "   ";
TS2_OWNED_STATIC(const char, e7e520)[] = "}\n";
TS2_OWNED_STATIC(const char, e7e524)[] = "if {";
TS2_OWNED_STATIC(const char, e7e5c8)[] = "else {";
TS2_OWNED_STATIC(const char, e7e6bc)[] = "endif\n";
TS2_OWNED_STATIC(const char, e7e7b0)[] = "enddef\n";
TS2_OWNED_STATIC(const char, e7ec08)[] = "arguments:\n";
TS2_OWNED_STATIC(const char, e7e2f8)[] = "(%g,%g)";
TS2_OWNED_STATIC(const char, e7e368)[] = "(%g,%g,%g)";
TS2_OWNED_STATIC(const char, e7d48c)[] = "(%g,%g,%g,%g)";
TS2_OWNED_STATIC(const char, e7e9f0)[] = "%s %s(%d %d)";
TS2_OWNED_STATIC(const char, e7e818)[] = "silent";
TS2_OWNED_STATIC(const char, e7eaf4)[] = "variables";
TS2_OWNED_STATIC(const char, e7eb08)[] = "definitions";
TS2_OWNED_STATIC(const char, e7eb14)[] = "scope";
TS2_OWNED_STATIC(const char, e7eb68)[] = "varAssignment";
TS2_OWNED_STATIC(const char, e7eb78)[] = "varLookup";
TS2_OWNED_STATIC(const char, e7eb84)[] = "conditionals";
TS2_OWNED_STATIC(const char, e7eb94)[] = "instantiations";
TS2_OWNED_STATIC(const char, daa7c8)[] = "alias";
TS2_OWNED_STATIC(const char, dbc1a4)[] = "version";
TS2_OWNED_STATIC(const char, dbc374)[] = "end";
TS2_OWNED_STATIC(const char, dcde40)[] = "debug";
TS2_OWNED_STATIC(const char, dd0b2c)[] = "trace";
TS2_OWNED_STATIC(const char, e09304)[] = "log";
TS2_OWNED_STATIC(const char, e48120)[] = "define";
TS2_OWNED_STATIC(const char, e7ae7c)[] = "else";
TS2_OWNED_STATIC(const char, e7dc94)[] = "include";
TS2_OWNED_STATIC(const char, e7dce4)[] = "sinclude";
TS2_OWNED_STATIC(const char, e7dd60)[] = "wildInclude";
TS2_OWNED_STATIC(const char, e7de60)[] = "ignoreErrors";
TS2_OWNED_STATIC(const char, e7dec8)[] = "list";
TS2_OWNED_STATIC(const char, e7df44)[] = "eval";
TS2_OWNED_STATIC(const char, e7dfa0)[] = "assert";
TS2_OWNED_STATIC(const char, e7e02c)[] = "set";
TS2_OWNED_STATIC(const char, e7e07c)[] = "sete";
TS2_OWNED_STATIC(const char, e7e108)[] = "setb";
TS2_OWNED_STATIC(const char, e7e164)[] = "seti";
TS2_OWNED_STATIC(const char, e7e1d8)[] = "setf";
TS2_OWNED_STATIC(const char, e7e258)[] = "setc";
TS2_OWNED_STATIC(const char, e7e2bc)[] = "setv2";
TS2_OWNED_STATIC(const char, e7e32c)[] = "setv3";
TS2_OWNED_STATIC(const char, e7e3a0)[] = "setv4";
TS2_OWNED_STATIC(const char, e7e414)[] = "namespace";
TS2_OWNED_STATIC(const char, e7e46c)[] = "purge";
TS2_OWNED_STATIC(const char, e7e500)[] = "if";
TS2_OWNED_STATIC(const char, e7e52c)[] = "elseif";
TS2_OWNED_STATIC(const char, e7e534)[] = "endif";
TS2_OWNED_STATIC(const char, e7e73c)[] = "enddef";
TS2_OWNED_STATIC(const char, e7e718)[] = "A definition inside a definition"; // own text
TS2_OWNED_STATIC(const char, e7e72c)[] = "Defined already"; // own text
TS2_OWNED_STATIC(const char, e7f474)[] = "A parameter has no name"; // own text
TS2_OWNED_STATIC(const char, e7f48c)[] = "A parameter name is not valid"; // own text
TS2_OWNED_STATIC(const char, e7f4a0)[] = "No such parameter: "; // own text
TS2_OWNED_STATIC(const char, e7e7e4)[] = "undefine";
TS2_OWNED_STATIC(const char, e7e878)[] = "create";
TS2_OWNED_STATIC(const char, e7e9a8)[] = "arrayCreate";
TS2_OWNED_STATIC(const char, e7eab8)[] = "dump";
TS2_OWNED_STATIC(const char, e7ebd0)[] = "showArguments";
TS2_OWNED_STATIC(const char, e7ec40)[] = "debuggerBreak";
TS2_OWNED_STATIC(const char, e7dc9c)[] = "*Reads and runs a script file"; // own text
TS2_OWNED_STATIC(const char, e7dcf0)[] = "*Reads and runs a script file; a missing file is not an error"; // own text
TS2_OWNED_STATIC(const char, e7dd70)[] = "*Reads and runs every script file whose name matches a wild card pattern ('subfolders' searches below too)"; // own text
TS2_OWNED_STATIC(const char, e7de10)[] = "*Closes a block"; // own text
TS2_OWNED_STATIC(const char, e7de70)[] = "*Keeps going after a script error"; // own text
TS2_OWNED_STATIC(const char, e7ded0)[] = "*Lists the commands and functions this parser knows"; // own text
TS2_OWNED_STATIC(const char, e7df4c)[] = "*Runs the argument as a command"; // own text
TS2_OWNED_STATIC(const char, e7dfa8)[] = "*Fails the script unless the argument is true"; // own text
TS2_OWNED_STATIC(const char, e7e030)[] = "*Assigns a variable"; // own text
TS2_OWNED_STATIC(const char, e7e088)[] = "*Assigns a variable one of a list of enumerated values"; // own text
TS2_OWNED_STATIC(const char, e7e110)[] = "*Assigns a variable a boolean"; // own text
TS2_OWNED_STATIC(const char, e7e16c)[] = "*Assigns a variable an integer"; // own text
TS2_OWNED_STATIC(const char, e7e1e0)[] = "*Assigns a variable a floating point number"; // own text
TS2_OWNED_STATIC(const char, e7e260)[] = "*Assigns a variable an RGB or ARGB colour"; // own text
TS2_OWNED_STATIC(const char, e7e2c4)[] = "*Assigns a variable a sequence of 2 numbers"; // own text
TS2_OWNED_STATIC(const char, e7e334)[] = "*Assigns a variable a sequence of 3 numbers"; // own text
TS2_OWNED_STATIC(const char, e7e3a8)[] = "*Assigns a variable a sequence of 4 numbers"; // own text
TS2_OWNED_STATIC(const char, e7e420)[] = "*Opens a namespace scope"; // own text
TS2_OWNED_STATIC(const char, e7e474)[] = "*Removes the variables of a namespace scope"; // own text
TS2_OWNED_STATIC(const char, e7e504)[] = "*Opens a conditional block"; // own text
TS2_OWNED_STATIC(const char, e7e568)[] = "*Another condition of a conditional block"; // own text
TS2_OWNED_STATIC(const char, e7e5fc)[] = "*The last branch of a conditional block"; // own text
TS2_OWNED_STATIC(const char, e7e680)[] = "*Closes a conditional block"; // own text
TS2_OWNED_STATIC(const char, e7e6f8)[] = "*Opens a macro definition"; // own text
TS2_OWNED_STATIC(const char, e7e770)[] = "*Closes a macro definition"; // own text
TS2_OWNED_STATIC(const char, e7e7f0)[] = "*Removes a macro definition"; // own text
TS2_OWNED_STATIC(const char, e7e880)[] = "*Runs a macro definition"; // own text
TS2_OWNED_STATIC(const char, e7e9b4)[] = "*Runs a macro definition a given number of times"; // own text
TS2_OWNED_STATIC(const char, e7ea2c)[] = "*Prints the argument"; // own text
TS2_OWNED_STATIC(const char, e7ea74)[] = "*Writes a log message"; // own text
TS2_OWNED_STATIC(const char, e7eac0)[] = "*Prints this parser's internal state"; // own text
TS2_OWNED_STATIC(const char, e7eb48)[] = "*Turns parser debugging aids on or off"; // own text
TS2_OWNED_STATIC(const char, e7ebe0)[] = "*Prints this command's arguments"; // own text
TS2_OWNED_STATIC(const char, e7ec50)[] = "*Stops in the debugger"; // own text
TS2_OWNED_STATIC(const char, e7ec98)[] = "*Declares the version of the running script"; // own text
TS2_OWNED_STATIC(const char, e7eda0)[] = "*[name] [replacement text] [brief help] [full help] Makes a command name stand for a piece of text."; // own text
TS2_OWNED_STATIC(const char, e7ee18)[] = "alias <name:string> <replacement:string> [<brief help:string> [<full help:string>]]\n    Makes a command name stand for a piece of text.\n"; // own text
TS2_OWNED_STATIC(const char, e7eeb8)[] = "alias for %s"; // own text
TS2_OWNED_STATIC(const char, e7eec8)[] = "%s <arguments>\n    Stands for '%s <arguments>'\n"; // own text
TS2_OWNED_STATIC(const char, db59cc)[] = "Wrong argument count"; // own text
TS2_OWNED_STATIC(const char, e7d628)[] = "No enumeration named '%s'"; // own text
TS2_OWNED_STATIC(const char, e7de20)[] = "Not in a block"; // own text
TS2_OWNED_STATIC(const char, e7e4b0)[] = "Invalid scope '%s'"; // own text
TS2_OWNED_STATIC(const char, e7ecbc)[] = "Script version too old: it has %d, the minimum is %d"; // own text
TS2_OWNED_STATIC(const char, e7ed00)[] = "Script version too new: it has %d, the maximum is %d"; // own text
TS2_OWNED_STATIC(const char, e7e0cc)[] = "No such enumeration value"; // own text
TS2_OWNED_STATIC(const char, e7e820)[] = "Not allowed inside a definition"; // own text
TS2_OWNED_STATIC(const char, e7e834)[] = "No definition named %s"; // own text
TS2_OWNED_STATIC(const char, e7e620)[] = "else with no if/elseif before it"; // own text
TS2_OWNED_STATIC(const char, e7e640)[] = "else after else"; // own text
TS2_OWNED_STATIC(const char, e7e58c)[] = "elseif with no if/elseif before it"; // own text
TS2_OWNED_STATIC(const char, e7e5b0)[] = "elseif after else"; // own text
TS2_OWNED_STATIC(const char, e7e698)[] = "endif with no if/elseif before it"; // own text
TS2_OWNED_STATIC(const char, e7dfe8)[] = "Assertion failed: '%s'"; // own text
TS2_OWNED_STATIC(const char, e7e790)[] = "enddef with no define before it"; // own text
TS2_OWNED_STATIC(const char, e7e8a4)[] = "No definition named '%s'"; // own text
TS2_OWNED_STATIC(const char, e7e8c0)[] = "Unbalanced meta commands: an endif or enddef may be missing."; // own text
TS2_OWNED_STATIC(const char, e7e954)[] = "%s\n      in definition '%s', line %d";
TS2_OWNED_STATIC(const char, e7e924)[] = "\n%s(%d): %s\n      in definition '%s', line %d";

// The parser functions (gz/cParserTexts.h; lane RS): names exact, help texts and the message ours.
TS2_OWNED_STATIC(const char, e83ec4)[] = "no property set";
TS2_OWNED_STATIC(const char, e7f090)[] = "minVersion";
TS2_OWNED_STATIC(const char, e7f104)[] = "maxVersion";
TS2_OWNED_STATIC(const char, e7ef44)[] = "varExists";
TS2_OWNED_STATIC(const char, e7f028)[] = "commandExists";
TS2_OWNED_STATIC(const char, e7efb8)[] = "definitionExists";
TS2_OWNED_STATIC(const char, e7f178)[] = "match";
TS2_OWNED_STATIC(const char, e8401c)[] = "propExists";
TS2_OWNED_STATIC(const char, e7f09c)[] = "*The oldest script version the parser accepts."; // own text
TS2_OWNED_STATIC(const char, e7f110)[] = "*The newest script version the parser accepts."; // own text
TS2_OWNED_STATIC(const char, e7ef50)[] = "*True when the variable has a value"; // own text
TS2_OWNED_STATIC(const char, e7f038)[] = "*True when the parser has the command"; // own text
TS2_OWNED_STATIC(const char, e7efcc)[] = "*True when the definition exists"; // own text
TS2_OWNED_STATIC(const char, e7f180)[] = "*True when the text matches the wild card pattern."; // own text
TS2_OWNED_STATIC(const char, e84028)[] = "<name:string>  true when the property set has the named property"; // own text

// The expression evaluator's built-in names: our table (gz/ExpressionEvaluator.h), the image's names and order.
#pragma comment(linker, "/alternatename:_ts2_rva_e7f590=_ts2_own_eval_builtins")

// cBaseParser's tables (gz/cBaseParser.h; our own, cBaseParserTables.cpp).
#pragma comment(linker, "/alternatename:_ts2_rva_e7db24=?kOwnVTable@cBaseParser@nGZCommandParser@@2QBQBXB")
#pragma comment(linker, "/alternatename:_ts2_rva_e7db3c=?kOwnUnknownVTable@cBaseParser@nGZCommandParser@@2QBQBXB")
#pragma comment(linker, "/alternatename:_ts2_rva_e7db4c=?kOwnParserVTable@cBaseParser@nGZCommandParser@@2QBQBXB")
#pragma comment(linker, "/alternatename:_ts2_rva_e7dc18=?kOwnVbTable@cBaseParser@nGZCommandParser@@2QBHB")
#pragma comment(linker, "/alternatename:_ts2_rva_e7dc24=?kOwnParserFaceVbTable@cBaseParser@nGZCommandParser@@2QBHB")
#pragma comment(linker, "/alternatename:_ts2_rva_d9b428=?kOwnPureUnknownVTable@cBaseParser@nGZCommandParser@@2QBQBXB")
#pragma comment(linker, "/alternatename:_ts2_rva_db581c=?kOwnPureUnknownVTable@cBaseParser@nGZCommandParser@@2QBQBXB")
#pragma comment(linker, "/alternatename:_ts2_rva_db574c=?kOwnPureParserVTable@cBaseParser@nGZCommandParser@@2QBQBXB")

// cLODLevelsTable's tables (sg/cLODLevels.h; our own, sg/cLODLevelsTables.cpp; lane VK) and cBranchNodesIIDFilter's
// vtable (misc/SmallVirtuals.cpp).
#pragma comment(linker, "/alternatename:_ts2_rva_e767b8=?kOwnVTable@cLODLevelsTable@nGZSceneGraph@@2QBQBXB")
#pragma comment(linker, "/alternatename:_ts2_rva_e767d0=?kOwnUnknownVTable@cLODLevelsTable@nGZSceneGraph@@2QBQBXB")
#pragma comment(linker, "/alternatename:_ts2_rva_e767e0=?kOwnParserVTable@cLODLevelsTable@nGZSceneGraph@@2QBQBXB")
#pragma comment(linker, "/alternatename:_ts2_rva_e768ac=?kOwnVbTable@cLODLevelsTable@nGZSceneGraph@@2QBHB")
#pragma comment(linker, "/alternatename:_ts2_rva_e768b8=?kOwnParserFaceVbTable@cLODLevelsTable@nGZSceneGraph@@2QBHB")
#pragma comment(linker, "/alternatename:_ts2_rva_e7cf54=_ts2_own_vt_e7cf54")
#pragma comment(linker, "/alternatename:_ts2_rva_e76914=?kOwnVTable@cLODCommand@nGZSceneGraph@@2QBQBXB")
TS2_OWNED_STATIC(const char, e6f73c)[] = "LOD"; // cLODCommand's name

// The command parser's own tables (gz/CommandParserTables.h, CommandParserTables.cpp; lane RS): the base and built-in
// commands', the functions' and their faces', cError's.
#pragma comment(linker, "/alternatename:_ts2_rva_e7d72c=_ts2_own_vt_e7d72c")
#pragma comment(linker, "/alternatename:_ts2_rva_e7d758=_ts2_own_vt_e7d758")
#pragma comment(linker, "/alternatename:_ts2_rva_e7d790=_ts2_own_vt_e7d790")
#pragma comment(linker, "/alternatename:_ts2_rva_e7d7c4=_ts2_own_vt_e7d7c4")
#pragma comment(linker, "/alternatename:_ts2_rva_e7d7d0=_ts2_own_vt_e7d7d0")
#pragma comment(linker, "/alternatename:_ts2_rva_e7eefc=_ts2_own_vt_e7eefc")
#pragma comment(linker, "/alternatename:_ts2_rva_e7dc6c=_ts2_own_vt_e7dc6c")
#pragma comment(linker, "/alternatename:_ts2_rva_e7dcbc=_ts2_own_vt_e7dcbc")
#pragma comment(linker, "/alternatename:_ts2_rva_e7dd38=_ts2_own_vt_e7dd38")
#pragma comment(linker, "/alternatename:_ts2_rva_e7dde8=_ts2_own_vt_e7dde8")
#pragma comment(linker, "/alternatename:_ts2_rva_e7de38=_ts2_own_vt_e7de38")
#pragma comment(linker, "/alternatename:_ts2_rva_e7dea0=_ts2_own_vt_e7dea0")
#pragma comment(linker, "/alternatename:_ts2_rva_e7df1c=_ts2_own_vt_e7df1c")
#pragma comment(linker, "/alternatename:_ts2_rva_e7df78=_ts2_own_vt_e7df78")
#pragma comment(linker, "/alternatename:_ts2_rva_e7e004=_ts2_own_vt_e7e004")
#pragma comment(linker, "/alternatename:_ts2_rva_e7e054=_ts2_own_vt_e7e054")
#pragma comment(linker, "/alternatename:_ts2_rva_e7e0e0=_ts2_own_vt_e7e0e0")
#pragma comment(linker, "/alternatename:_ts2_rva_e7e13c=_ts2_own_vt_e7e13c")
#pragma comment(linker, "/alternatename:_ts2_rva_e7e1b0=_ts2_own_vt_e7e1b0")
#pragma comment(linker, "/alternatename:_ts2_rva_e7e230=_ts2_own_vt_e7e230")
#pragma comment(linker, "/alternatename:_ts2_rva_e7e294=_ts2_own_vt_e7e294")
#pragma comment(linker, "/alternatename:_ts2_rva_e7e304=_ts2_own_vt_e7e304")
#pragma comment(linker, "/alternatename:_ts2_rva_e7e378=_ts2_own_vt_e7e378")
#pragma comment(linker, "/alternatename:_ts2_rva_e7e3e0=_ts2_own_vt_e7e3e0")
#pragma comment(linker, "/alternatename:_ts2_rva_e7e444=_ts2_own_vt_e7e444")
#pragma comment(linker, "/alternatename:_ts2_rva_e7e4d0=_ts2_own_vt_e7e4d0")
#pragma comment(linker, "/alternatename:_ts2_rva_e7e540=_ts2_own_vt_e7e540")
#pragma comment(linker, "/alternatename:_ts2_rva_e7e5d4=_ts2_own_vt_e7e5d4")
#pragma comment(linker, "/alternatename:_ts2_rva_e7e658=_ts2_own_vt_e7e658")
#pragma comment(linker, "/alternatename:_ts2_rva_e7e6c8=_ts2_own_vt_e7e6c8")
#pragma comment(linker, "/alternatename:_ts2_rva_e7e748=_ts2_own_vt_e7e748")
#pragma comment(linker, "/alternatename:_ts2_rva_e7e7bc=_ts2_own_vt_e7e7bc")
#pragma comment(linker, "/alternatename:_ts2_rva_e7e850=_ts2_own_vt_e7e850")
#pragma comment(linker, "/alternatename:_ts2_rva_e7e980=_ts2_own_vt_e7e980")
#pragma comment(linker, "/alternatename:_ts2_rva_e7ea90=_ts2_own_vt_e7ea90")
#pragma comment(linker, "/alternatename:_ts2_rva_e7eb20=_ts2_own_vt_e7eb20")
#pragma comment(linker, "/alternatename:_ts2_rva_e7eba8=_ts2_own_vt_e7eba8")
#pragma comment(linker, "/alternatename:_ts2_rva_e7ec18=_ts2_own_vt_e7ec18")
#pragma comment(linker, "/alternatename:_ts2_rva_e7ec70=_ts2_own_vt_e7ec70")
#pragma comment(linker, "/alternatename:_ts2_rva_e7ea04=_ts2_own_vt_e7ea04")
#pragma comment(linker, "/alternatename:_ts2_rva_e7ea4c=_ts2_own_vt_e7ea4c")
#pragma comment(linker, "/alternatename:_ts2_rva_e7ed78=_ts2_own_vt_e7ed78")
#pragma comment(linker, "/alternatename:_ts2_rva_e7ed4c=_ts2_own_vt_e7ed4c")
#pragma comment(linker, "/alternatename:_ts2_rva_e7ef1c=_ts2_own_vt_e7ef1c")
#pragma comment(linker, "/alternatename:_ts2_rva_e7ef28=_ts2_own_vt_e7ef28")
#pragma comment(linker, "/alternatename:_ts2_rva_e7ef90=_ts2_own_vt_e7ef90")
#pragma comment(linker, "/alternatename:_ts2_rva_e7ef9c=_ts2_own_vt_e7ef9c")
#pragma comment(linker, "/alternatename:_ts2_rva_e7f000=_ts2_own_vt_e7f000")
#pragma comment(linker, "/alternatename:_ts2_rva_e7f00c=_ts2_own_vt_e7f00c")
#pragma comment(linker, "/alternatename:_ts2_rva_e7f068=_ts2_own_vt_e7f068")
#pragma comment(linker, "/alternatename:_ts2_rva_e7f074=_ts2_own_vt_e7f074")
#pragma comment(linker, "/alternatename:_ts2_rva_e7f0dc=_ts2_own_vt_e7f0dc")
#pragma comment(linker, "/alternatename:_ts2_rva_e7f0e8=_ts2_own_vt_e7f0e8")
#pragma comment(linker, "/alternatename:_ts2_rva_e7f150=_ts2_own_vt_e7f150")
#pragma comment(linker, "/alternatename:_ts2_rva_e7f15c=_ts2_own_vt_e7f15c")
#pragma comment(linker, "/alternatename:_ts2_rva_da5060=_ts2_own_vt_da5060")

// cMacroDefinition's own table (gz/MacroDefinitionTable.cpp; lane RU), the next definition's id and
// RZFormattedErrorCtor's formatting buffer (rz/RZFrameworkHelpers.h).
#pragma comment(linker, "/alternatename:_ts2_rva_e7db0c=_ts2_own_vt_e7db0c")
TS2_OWNED_STATIC(uint32_t, 10749a0) = 6;
TS2_OWNED_STATIC(char, 10cb840)[0x800]{};

// The text streams' tables (rz/RZFileStreamsTables.cpp; lane RU), their format cells and hex dump texts and the
// OutputDebugStringA import slot (rz/RZFileStreams.h).
#pragma comment(linker, "/alternatename:_ts2_rva_da2b4c=_ts2_own_vt_da2b4c")
#pragma comment(linker, "/alternatename:_ts2_rva_da2bb0=_ts2_own_vt_da2bb0")
#pragma comment(linker, "/alternatename:_ts2_rva_da2c14=_ts2_own_vt_da2c14")
#pragma comment(linker, "/alternatename:_ts2_rva_da2c78=_ts2_own_vt_da2c78")
#pragma comment(linker, "/alternatename:_ts2_rva_dcf704=_ts2_own_vt_dcf704")
// cDebugStream's newline cell (rz/cDebugStream.h).
TS2_OWNED_STATIC(const char *, 1014980) = ts2_rva_d9e644;
// cRZMessage2Standard's table (rz/cRZMessage2StandardTable.cpp; lane RU).
#pragma comment(linker, "/alternatename:_ts2_rva_da4064=_ts2_own_vt_da4064")
// The device state commands' tables (gz/RenderStates.cpp; lane VU): cStateHeader, cClippingState, cDepthRangeState,
// cRenderTargetsState, cViewportState.
#pragma comment(linker, "/alternatename:_ts2_rva_d9c594=_ts2_own_vt_d9c594")
#pragma comment(linker, "/alternatename:_ts2_rva_d9c5e8=_ts2_own_vt_d9c5e8")
#pragma comment(linker, "/alternatename:_ts2_rva_d9c5dc=_ts2_own_vt_d9c5dc")
#pragma comment(linker, "/alternatename:_ts2_rva_d9ff54=_ts2_own_vt_d9ff54")
#pragma comment(linker, "/alternatename:_ts2_rva_d9c5d0=_ts2_own_vt_d9c5d0")
// The cIGZAny family's tables (gz/cGZAnyTables.cpp; lane VX) and the type names after the image's tables.
#pragma comment(linker, "/alternatename:_ts2_rva_d9c654=_ts2_own_vt_d9c654")
#pragma comment(linker, "/alternatename:_ts2_rva_d9f534=_ts2_own_vt_d9f534")
#pragma comment(linker, "/alternatename:_ts2_rva_d9c670=_ts2_own_vt_d9c670")
#pragma comment(linker, "/alternatename:_ts2_rva_da3d48=_ts2_own_vt_da3d48")
#pragma comment(linker, "/alternatename:_ts2_rva_da3d70=_ts2_own_vt_da3d70")
#pragma comment(linker, "/alternatename:_ts2_rva_da3d98=_ts2_own_vt_da3d98")
#pragma comment(linker, "/alternatename:_ts2_rva_da3e00=_ts2_own_vt_da3e00")
// nGZEvent::cAttributeIDIterator's table (gz/cAttributeIDIterator.cpp; lane VX).
#pragma comment(linker, "/alternatename:_ts2_rva_d9bd10=_ts2_own_vt_d9bd10")
// nGZGraphic4::cError's table (misc/SmallVirtuals.cpp; lane WI): the error cGraphicSystemDX and cDeviceDX throw.
#pragma comment(linker, "/alternatename:_ts2_rva_d9cd70=_ts2_own_vt_d9cd70")
// The block pools' four tables (rz/RZBlockPool.size.cpp; lane VX), the pools (null until first use) and their lock (built
// below, before the program's own statics, as the image's XC initialiser 0x138342 builds it).
#pragma comment(linker, "/alternatename:_ts2_rva_da3c68=_ts2_own_vt_da3c68")
#pragma comment(linker, "/alternatename:_ts2_rva_da3c74=_ts2_own_vt_da3c74")
#pragma comment(linker, "/alternatename:_ts2_rva_da3c80=_ts2_own_vt_da3c80")
#pragma comment(linker, "/alternatename:_ts2_rva_da3c8c=_ts2_own_vt_da3c8c")
// cRZThreadSignal's table (rz/cRZThreadSignalSlots.size.cpp; lane VX).
#pragma comment(linker, "/alternatename:_ts2_rva_e61b7c=_ts2_own_vt_e61b7c")
TS2_OWNED_STATIC(RZBlockPool *, 10cb088){};
TS2_OWNED_STATIC(RZBlockPool *, 10cb08c){};
TS2_OWNED_STATIC(cRZCriticalSection, 10cb090){};
TS2_OWNED_STATIC(const char, da3d60)[] = "AnySint32";
TS2_OWNED_STATIC(const char, da3db0)[] = "AnyString";
TS2_OWNED_STATIC(const char, da3e18)[] = "AnyAny";
TS2_OWNED_STATIC(const char *, ff2414) = ts2_rva_d9b648;
TS2_OWNED_STATIC(const char *, ff2418) = ts2_rva_d9d510;
TS2_OWNED_STATIC(const char *, ff241c) = "%lld";
TS2_OWNED_STATIC(const char *, ff2420) = "%llu";
TS2_OWNED_STATIC(const char *, ff2424) = "%g";
TS2_OWNED_STATIC(const char, da2dcc)[] = "\nHEXDUMP:\n";
TS2_OWNED_STATIC(const char, da2dd8)[] = "%02x ";
TS2_OWNED_STATIC(void *, d9a110) = reinterpret_cast<void *>(::OutputDebugStringA);

// The commands' data (gz/CommandParser.h). The log-level table is built in the initialisers below.
TS2_OWNED_STATIC(char, 10f6bc8)[0x800]{};
TS2_OWNED_STATIC(char, 10fa248)[0x800]{};
TS2_OWNED_STATIC(char, 10f9a48)[0x800]{};
TS2_OWNED_STATIC(uint32_t, 10748f4) = 2;
TS2_OWNED_STATIC(uint32_t, 1074994) = 6;
TS2_OWNED_STATIC(uint32_t, 1074998) = 4;
TS2_OWNED_STATIC(uint32_t, 107499c) = 5;

// The directory scan's special names (rz/DirectoryScan.h) and the FindNextFileA import slot it calls through.
TS2_OWNED_STATIC(const char *, ff2470) = ".";
TS2_OWNED_STATIC(const char *, ff2474) = "..";
TS2_OWNED_STATIC(void *, d9a238) = reinterpret_cast<void *>(::FindNextFileA);

// The stopwatch's timebase (rz/cRZStopwatchCalibration.h), filled by the calibration object's constructor below, and
// the import slots it and the stopwatch call through. The frequency cache (the cRZTimer section's view) stays null.
// cRZStd's slots (lane FA): their import slots, the debug level, the message box's lock (built and destroyed below, as
// XC slot 9 and its atexit entry do) and the assertions' log group.
TS2_OWNED_STATIC(void *, d9a164) = reinterpret_cast<void *>(::GetEnvironmentVariableA);
TS2_OWNED_STATIC(void *, d9a16c) = reinterpret_cast<void *>(::GetModuleFileNameA);
TS2_OWNED_STATIC(void *, d9a1bc) = reinterpret_cast<void *>(::GetCurrentProcessId);
TS2_OWNED_STATIC(void *, d9a1ec) = reinterpret_cast<void *>(::GetFullPathNameA);
TS2_OWNED_STATIC(void *, d9a568) = reinterpret_cast<void *>(::MessageBoxA);
TS2_OWNED_STATIC(void *, d9a5c0) = reinterpret_cast<void *>(::GetWindowThreadProcessId);
TS2_OWNED_STATIC(void *, d9a5c4) = reinterpret_cast<void *>(::IsWindow);
TS2_OWNED_STATIC(int32_t, 10cb5d4) = 0;
TS2_OWNED_STATIC(cRZCriticalSection, 10cb5cc){};
TS2_OWNED_STATIC(const char, da3f88)[] = "ASSERT";
TS2_OWNED_STATIC(cRZStopwatchCalibration, 10cb640){};
TS2_OWNED_STATIC(cRZStopwatchTimebase, 10cb648){};
TS2_OWNED_STATIC(cRZStopwatchTimebase, 10cb678){};
TS2_OWNED_STATIC(uint64_t, ff2480) = 1; // the image's initial value: a frequency of 1 until calibration
TS2_OWNED_STATIC(uint64_t, ff2488) = 1;
TS2_OWNED_STATIC(uint64_t *, 10cb63c){};
TS2_OWNED_STATIC(void *, d9a188) = reinterpret_cast<void *>(::SleepEx);
TS2_OWNED_STATIC(void *, d9a190) = reinterpret_cast<void *>(::GetCurrentThread);
TS2_OWNED_STATIC(void *, d9a198) = reinterpret_cast<void *>(::SetThreadPriority);
TS2_OWNED_STATIC(void *, d9a19c) = reinterpret_cast<void *>(::GetThreadPriority);
TS2_OWNED_STATIC(void *, d9a1b8) = reinterpret_cast<void *>(::GetCurrentProcess);
TS2_OWNED_STATIC(void *, d9a24c) = reinterpret_cast<void *>(::QueryPerformanceCounter);
TS2_OWNED_STATIC(void *, d9a250) = reinterpret_cast<void *>(::QueryPerformanceFrequency);
TS2_OWNED_STATIC(void *, d9a254) = reinterpret_cast<void *>(::SetPriorityClass);
TS2_OWNED_STATIC(void *, d9a258) = reinterpret_cast<void *>(::GetPriorityClass);

namespace {

// Runs before the program's own statics (init_seg(lib)), as the game's XC initialisers run before its user code.
#pragma warning(push)
#pragma warning(disable : 4073) // initializers put in library initialization area: intended
#pragma init_seg(lib)
#pragma warning(pop)

struct GeneralAllocatorLifetime {
    GeneralAllocatorLifetime() { ts2_rva_10cb098.Construct(nullptr, 0, 1, 0, nullptr, nullptr); }
    ~GeneralAllocatorLifetime() { ts2_rva_10cb098.Shutdown(); }
};
GeneralAllocatorLifetime gGeneralAllocatorLifetime;

// The block pools' lock (0x10cb090): constructed and destroyed as the image's XC initialiser and atexit entry do.
struct BlockPoolLockLifetime {
    BlockPoolLockLifetime() { ts2_rva_10cb090.Construct(); }
    ~BlockPoolLockLifetime() { ts2_rva_10cb090.Destruct(); }
};
BlockPoolLockLifetime gBlockPoolLockLifetime;

// The stopwatch calibration (0x10cb640, XC slot 105: `mov ecx, 0x10cb640; jmp 0x1c8d8e`, no atexit entry). Only here:
// in a hooked game the image's own initialiser constructs it, and nothing of ours may run it a second time.
struct StopwatchCalibrationInit {
    StopwatchCalibrationInit() { ts2_rva_10cb640.Construct(); }
};
StopwatchCalibrationInit gStopwatchCalibrationInit;

// cRZStd's message-box lock (0x10cb5cc): XC slot 9 (0x13836f) and its atexit entry 0xd90741.
struct StdAskLockLifetime {
    StdAskLockLifetime() { ts2_rva_10cb5cc.Construct(); }
    ~StdAskLockLifetime() { ts2_rva_10cb5cc.Destruct(); }
};
StdAskLockLifetime gStdAskLockLifetime;

// Fills the wide file function table as 0x1c0d9b does on an NT system (always now): kernel32's entries, linked directly.
struct WideFileFunctionsInit {
    WideFileFunctionsInit()
    {
        rz::WideFileFunctions &t = ts2_rva_10cb5e0;
        t.createFile = reinterpret_cast<rz::Proc>(::CreateFileW);
        t.copyFile = reinterpret_cast<rz::Proc>(::CopyFileW);
        t.moveFileEx = reinterpret_cast<rz::Proc>(::MoveFileExW);
        t.findFirstFile = reinterpret_cast<rz::Proc>(::FindFirstFileW);
        t.deleteFile = reinterpret_cast<rz::Proc>(::DeleteFileW);
        t.getFileAttributes = reinterpret_cast<rz::Proc>(::GetFileAttributesW);
        t.setFileAttributes = reinterpret_cast<rz::Proc>(::SetFileAttributesW);
        t.getLongPathNameA = reinterpret_cast<rz::Proc>(::GetLongPathNameA);
        t.getLongPathName = reinterpret_cast<rz::Proc>(::GetLongPathNameW);
        t.getTempPath = reinterpret_cast<rz::Proc>(::GetTempPathW);
        t.getDiskFreeSpaceExA = reinterpret_cast<rz::Proc>(::GetDiskFreeSpaceExA);
        t.getDiskFreeSpaceEx = reinterpret_cast<rz::Proc>(::GetDiskFreeSpaceExW);
        t.wide = true;
    }
};
WideFileFunctionsInit gWideFileFunctionsInit;

// Fills the wide directory function table as 0x1c4830 does on an NT system: kernel32's entries, linked directly (the
// shell32 one, SHGetFolderPathW, is not read by anything here and stays null).
struct WideDirectoryFunctionsInit {
    WideDirectoryFunctionsInit()
    {
        rz::WideDirectoryFunctions &t = ts2_rva_10cb614;
        t.moveFileEx = reinterpret_cast<rz::Proc>(::MoveFileExW);
        t.findFirstFile = reinterpret_cast<rz::Proc>(::FindFirstFileW);
        t.findNextFile = reinterpret_cast<rz::Proc>(::FindNextFileW);
        t.createDirectory = reinterpret_cast<rz::Proc>(::CreateDirectoryW);
        t.removeDirectory = reinterpret_cast<rz::Proc>(::RemoveDirectoryW);
        t.getWindowsDirectory = reinterpret_cast<rz::Proc>(::GetWindowsDirectoryW);
        t.wide = true;
    }
};
WideDirectoryFunctionsInit gWideDirectoryFunctionsInit;

// The command parser's token separators and log levels (gz/CommandParser.h), as the image's static data and its XC
// initialiser (0x13f530, a NameValueTable of the five level names) have them; destroyed at exit as the image's atexit does.
} // namespace
TS2_OWNED_STATIC(std::string, 107479c)(" \t\n\r");
TS2_OWNED_STATIC(nGZCommandParser::EnumTable, 10f9a30){};
namespace {
struct LogLevelsLifetime {
    LogLevelsLifetime()
    {
        static const NameValueEntry kLevels[] = {
            {"always", 4}, {"error", 2}, {"warning", 3}, {"info", 5}, {"debug", 6}, {nullptr, 0}};
        static_assert(sizeof(nGZCommandParser::EnumTable) == sizeof(NameValueTable), "the log levels are a name table");
        reinterpret_cast<NameValueTable *>(&ts2_rva_10f9a30)->Construct(kLevels);
    }
};
LogLevelsLifetime gLogLevelsLifetime;

} // namespace
