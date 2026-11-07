// render_object: milestone O's standalone program (docs/roadmap.md, O; the lane list in docs/subsystems/scenegraph.md,
// "Milestone O"). Renders one static object from the game's packages into a 256x256 image with a fixed camera: the
// thumbnail path the game takes (cTSUIThumbnailFactorySceneBased, vtable 0xe022d4: set-up vf14 0x7cc088, load vf24
// 0x7cc350, render vf16 0x7cc8a3), with our classes where we have them and the classes' interfaces where we do not.
//
//   render_object <Sims3D dir> [cres-name [out.bmp]]
//     defaults: sculptureChinaVase_cres (group 0x1c0532fa), render_object.bmp
//
// Built from our sources with no game image, as gmdc_decode is. It is not expected to link yet: every GameAddress and
// OriginalFunction still reachable from here is an unresolved symbol, and that list is milestone O's work. Measure it with
// a forced link (cmake -DTS2_NOIMAGE_FORCE=ON, then tools/noimage.py on the map; never run the forced exe).
//
// The path:
//  1. Start-up (what WinMain, GZFrameWork_Startup and the directors' OnStart do): a small framework (function table,
//     slots nobody should call trap) that keeps the system services by id, the root COM director with the classes the
//     path creates (ours by their Create, the rest by their factory's RVA so the link names them), then the services
//     in the game's order: event system, message server, canvas (cCanvasW32, hidden, 256x256) and cGZCanvas, the DX
//     graphic system (real d3d9.dll) and cGZGraphicSystem, the persist resource manager with the object packages as
//     segments, and the scene-graph system (its Init registers the scene-graph factory, the material and texture
//     managers, the display list, the render group manager, the file parser and the LOD store).
//  2. The device: device 0's settings (windowed, one back buffer) applied, which creates the IDirect3DDevice9.
//  3. The scene, as the thumbnail factory's set-up builds it: a scene node, a viewer (cViewerRefNode) and a transform
//     node as its children; the viewer drawing to the device's own targets (mode 1 with no texture), cleared to black.
//  4. The model, as its load does: the CRES key from its name (StringKeyParser::ParseKey, type 0xe519c933), the resource
//     from the manager (slot 3, iid 0xe519c92c), cloned (slot 15) and added under the transform; the CRES's chain
//     (shape ref node -> SHPE -> GMND -> GMDC, TXMT -> TXTR, LIFO) is resolved by the scene-graph factory as the clone
//     is made and drawn. A viewer in the CRES (the thumbnail CRES has one) is used instead of ours, as the factory does.
//  5. One frame, as its render does: the viewer's render (slot 123) between the device's BeginScene and EndScene.
//  6. The back buffer read back (GetRenderTargetData) and written as a 24-bit BMP. The game reads a thumbnail back
//     through a cGZBuffer and the cached state service's ReadPixels (vf17 0x7cbd67); a BMP of the back buffer is this
//     program's glue, not the game's.
#include "gz/cCanvasW32.h"
#include "gz/cDeviceDX.h"
#include "gz/cDisplayMonitorW32.h"
#include "gz/cEventSystemService.h"
#include "gz/cFileParser.h"
#include "gz/cGZBuffer.h"
#include "gz/cGZCanvas.h"
#include "gz/cGZDBSegmentPackedFile.h"
#include "gz/cGZGraphicSystem.h"
#include "gz/cGZMessageServer2.h"
#include "gz/cGZPersistResourceManagerShape.h"
#include "gz/cGraphicSystemDX.h"
#include "gz/cSurfaceDX.h"
#include "gz/cIGZSystemService.h"
#include "rz/RZCOMDllDirectorShape.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/StaticGetters.h"
#include "rz/StringKeyParser.h"
#include "rz/cRZString.h"
#include "sg/SGNodeInterfaces.h"
#include "sg/cCompositionTreeNode.h"
#include "sg/cDisplayList.h"
#include "sg/cImageData.h"
#include "sg/cLevelInfo.h"
#include "sg/cMaterialAttributes.h"
#include "sg/cProceduralTexture.h"
#include "sg/cRenderGroupManager.h"
#include "sg/cRenderTargetTexture.h"
#include "sg/cShapeRefNode.h"
#include "sg/cTextureImage.h"
#include "sg/cViewerRefNode.h"
#include "sg/cGeometryDataContainerClass.h"
#include "sg/cGeometryNode.h"
#include "sg/cLODLevels.h"
#include "sg/cMaterialDefinition.h"
#include "sg/cResourceNode.h"
#include "sg/cShader.h"
#include "sg/cShaderInit.h"
#include "sg/cShape.h"
#include "sg/cSceneGraphFactory.h"
#include "sg/cSceneGraphSystem.h"
#include "sg/cSceneNode.h"
#include "sg/cTransformNode.h"

#include <windows.h>
#include <d3d9.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kWidth = 256, kHeight = 256; // the render target and window client size
constexpr uint32_t kCresType = 0xe519c933;       // CRES, also the resource node's class id
constexpr uint32_t kCresIid = nGZSceneGraph::cIResourceNode::kIid;       // what the thumbnail factory asks the manager and the clone for

// Interface ids the thumbnail factory asks the scene-graph classes for.
constexpr uint32_t kSceneIid = nGZSceneGraph::cISceneNode::kIid;
constexpr uint32_t kViewerIid = 0x5ca76db6;
constexpr uint32_t kTransformIid = nGZSceneGraph::cITransformNode::kIid;

// The scene-graph classes the path creates through COM, with their factories. Ours are named by their Create; the rest
// by the factory's RVA (OriginalFunction), so the forced link names each one (src/rz/COMDirectorData.cpp has the game's
// whole table; the table's own entries resolve through the image base and would hide them from the measurement).
using Factory = void *(__cdecl *)();
template <uint32_t Rva> void *__cdecl NotOursYet() { return OriginalFunction<Factory, Rva>()(); }

struct ClassEntry {
    uint32_t classId;
    Factory factory;
};

const ClassEntry kClasses[] = {
    // ours
    {nGZSceneGraph::cSceneNode::kClassId, reinterpret_cast<Factory>(&nGZSceneGraph::cSceneNode::Create)},
    {0x65246462, reinterpret_cast<Factory>(&nGZSceneGraph::cTransformNode::Create)},
    {nGZSceneGraph::cResourceNode::kClassId, reinterpret_cast<Factory>(&nGZSceneGraph::cResourceNode::Create)},
    {0x7ba3838c, reinterpret_cast<Factory>(&nGZSceneGraph::cGeometryNode::Create)},
    {0xac4f8687, reinterpret_cast<Factory>(&nGZSceneGraph::cGeometryDataContainer::Create)},
    {nGZSceneGraph::cCompositionTreeNode::kClassId, reinterpret_cast<Factory>(&nGZSceneGraph::cCompositionTreeNode::Create)},
    {nGZSceneGraph::cLODLevelsStore::kClassId, reinterpret_cast<Factory>(&nGZSceneGraph::cLODLevelsStore::Create)},
    {nGZCommandParser::cFileParser::kClassId, reinterpret_cast<Factory>(&nGZCommandParser::cFileParser::Create)},
    {cGZBuffer::kClassId, reinterpret_cast<Factory>(&cGZBuffer::Create)},
    {0x8c6828aa, &nGZSceneGraph::cLODLevelsTable::Create},                                // 0xab6fa0
    {0x65074b5e, reinterpret_cast<Factory>(&nGZSceneGraph::cSceneGraphSystem::Create)}, // 0xa4cb40
    {0xe5259c1b, reinterpret_cast<Factory>(&nGZSceneGraph::cSceneGraphFactory::Create)}, // 0xa4d600
    {0x3b75b71b, reinterpret_cast<Factory>(&nGZGraphic4::cImageSurfaceDX::New)},         // 0x161d40
    {0x7b75b73a, reinterpret_cast<Factory>(&nGZGraphic4::cDepthStencilSurfaceDX::New)},  // 0x161ce0
    {0x5b75b757, reinterpret_cast<Factory>(&nGZGraphic4::cRenderTargetDX::New)},         // 0x161e00
    {0x4dee64d7, reinterpret_cast<Factory>(&nGZGraphic4::cLockableRenderTargetDX::New)}, // 0x161da0
    {0x49596978, reinterpret_cast<Factory>(&nGZSceneGraph::cMaterialDefinition::Create)}, // 0xaadf20 (TXMT)
    {0xebfb3834, reinterpret_cast<Factory>(&nGZSceneGraph::cShader::Create)},             // 0xaa4fa0
    {0x7c17ff25, reinterpret_cast<Factory>(&nGZSceneGraph::cShaderInit::Create)},         // 0xaa82a0
    {0xfc6eb1f7, reinterpret_cast<Factory>(&nGZSceneGraph::cShape::Create)},              // 0xa5d3b0 (SHPE)
    {0xdca76dbb, reinterpret_cast<Factory>(&nGZSceneGraph::cViewerRefNode::Create)},            // 0xa65fb0
    {0x65245517, reinterpret_cast<Factory>(&nGZSceneGraph::cShapeRefNode::Create)},             // 0xa55270
    {0x1c4a276c, reinterpret_cast<Factory>(&nGZSceneGraph::cImageData::Create)},                // 0xab1590 (TXTR)
    {0xed534136, reinterpret_cast<Factory>(&nGZSceneGraph::cLevelInfo::Create)},                // 0xab15f0 (LIFO)
    {0x49d02f15, reinterpret_cast<Factory>(&nGZSceneGraph::cDisplayListAdapters::Create)},      // 0xaa0e00
    {0x4973819d, reinterpret_cast<Factory>(&nGZSceneGraph::cRenderGroupManagerAdapters::Create)}, // 0xaa37f0
    {0xaad7c8cf, reinterpret_cast<Factory>(&nGZSceneGraph::CreateMaterialAttributes)},          // 0xaaa3c0
    {0x2a956dad, reinterpret_cast<Factory>(&nGZSceneGraph::cTextureImage::NewTexture)},         // 0xab4e50
    {0x4a956dd2, reinterpret_cast<Factory>(&nGZSceneGraph::cTextureImage::NewCube)},            // 0xab4d30
    {0x2b96cc2b, reinterpret_cast<Factory>(&nGZSceneGraph::cRenderTargetTexture::New)},         // 0xab4df0
    {0x6b81dfe9, reinterpret_cast<Factory>(&nGZSceneGraph::cProceduralTexture::New)},           // 0xab4d90
    {nGZGraphic4::cDisplayMonitorW32::kClass, reinterpret_cast<Factory>(&nGZGraphic4::cDisplayMonitorW32::Create)}, // 0x1545c0
    // not ours yet: none (NotOursYet<Rva> stays for the next class the path needs)
};

// ---- the framework (cIGZFrameWork as the path uses it) ----

struct StandIn {
    void **vtable;
};

void __fastcall Unexpected(void *, void *)
{
    std::fprintf(stderr, "render_object: an unexpected framework call\n");
    std::abort();
}

StandIn gFramework;
StandIn gCOM;
void *gFrameworkTable[40];
void *gCOMTable[12];
std::map<uint32_t, cIGZSystemService *> gServices; // by service id, counted

bool __fastcall FrameworkQueryInterface(StandIn *, void *, uint32_t, void **ppv)
{
    // The canvas asks for the framework's window service (0x23ea70a1); this program has none.
    if (ppv) *ppv = nullptr;
    return false;
}
uint32_t __fastcall StaticCount(StandIn *, void *) { return 1; }

bool __fastcall FrameworkAddSystemService(StandIn *, void *, cIGZSystemService *service)
{
    if (!service) return false;
    const uint32_t id = service->GetServiceID();
    if (gServices.count(id)) return false;
    service->AddRef();
    gServices[id] = service;
    return true;
}

bool __fastcall FrameworkGetSystemService(StandIn *, void *, uint32_t serviceId, uint32_t iid, void **ppv)
{
    if (!ppv) return false;
    const auto found = gServices.find(serviceId);
    if (found == gServices.end()) return false;
    return found->second->QueryInterface(iid, ppv);
}

bool __fastcall FrameworkAddToTick(StandIn *, void *, cIGZSystemService *) { return true; } // one frame, no loop
void *__fastcall FrameworkGetCOMObject(StandIn *, void *) { return &gCOM; }

uint32_t __fastcall COMGetClassObject(StandIn *, void *, uint32_t classId, uint32_t iid, void **ppv)
{
    return reinterpret_cast<rz::cGZCOMDirector *>(GetCOMDirector())->GetClassObject(classId, iid, ppv);
}

void StandUpFramework()
{
    for (void *&slot : gFrameworkTable) slot = reinterpret_cast<void *>(&Unexpected);
    for (void *&slot : gCOMTable) slot = reinterpret_cast<void *>(&Unexpected);
    gFrameworkTable[0] = reinterpret_cast<void *>(&FrameworkQueryInterface);
    gFrameworkTable[1] = gFrameworkTable[2] = reinterpret_cast<void *>(&StaticCount);
    gFrameworkTable[3] = reinterpret_cast<void *>(&FrameworkAddSystemService);
    gFrameworkTable[5] = reinterpret_cast<void *>(&FrameworkGetSystemService);
    gFrameworkTable[9] = reinterpret_cast<void *>(&FrameworkAddToTick);
    gFrameworkTable[23] = reinterpret_cast<void *>(&FrameworkGetCOMObject);
    gCOMTable[1] = gCOMTable[2] = reinterpret_cast<void *>(&StaticCount);
    gCOMTable[3] = reinterpret_cast<void *>(&COMGetClassObject);
    gFramework.vtable = gFrameworkTable;
    gCOM.vtable = gCOMTable;
    ts2_rva_10c9378 = ts2_rva_10c937c = &gFramework;

    auto *director = reinterpret_cast<rz::cGZCOMDirector *>(GetCOMDirector());
    for (const ClassEntry &entry : kClasses) director->AddCls(entry.classId, reinterpret_cast<void *>(entry.factory));
}

template <typename Fn, int Slot, typename... Args> auto Call(void *object, Args... args)
{
    return reinterpret_cast<Fn>((*static_cast<void ***>(object))[Slot])(object, args...);
}

// A new object's cIGZSystemService face, added to the framework and started (Init), as the framework's start-up does.
cIGZSystemService *AddService(void *object, const char *name)
{
    cIGZSystemService *service = nullptr;
    if (!object || !Call<bool(__thiscall *)(void *, uint32_t, void **), 0>(object, cIGZSystemService::kIID,
                                                                            reinterpret_cast<void **>(&service))) {
        std::fprintf(stderr, "render_object: %s has no system service face\n", name);
        std::exit(1);
    }
    if (!FrameworkAddSystemService(&gFramework, nullptr, service) || !service->Init()) {
        std::fprintf(stderr, "render_object: %s did not start\n", name);
        std::exit(1);
    }
    service->SetServiceRunning(true);
    return service;
}

// ---- the resource manager and its segments ----

// One package as a segment of the manager, the way the game's AddPathSegment (0xca4f70) opens one; the manager's slot 29
// appends it to its list (slot 28 would put it first).
bool AddPackage(gz::cGZPersistResourceManager &manager, const std::string &path)
{
    auto *file = static_cast<cGZDBSegmentPackedFile *>(::operator new(sizeof(cGZDBSegmentPackedFile)));
    file->Construct();
    Call<uint32_t(__thiscall *)(void *), 1>(file);
    auto *segment = reinterpret_cast<SegmentView *>(reinterpret_cast<uint8_t *>(file) + 4);
    alignas(cRZString) uint8_t pathStorage[sizeof(cRZString)];
    auto *name = reinterpret_cast<cRZString *>(pathStorage);
    name->Construct(path.c_str());
    if (!segment->Init() || !segment->SetPath(name) || !segment->Open(1, 0)) {
        std::fprintf(stderr, "render_object: cannot open %s\n", path.c_str());
        return false;
    }
    return manager.vf29(segment) != 0;
}

// ---- the image ----

bool WriteBmp(const char *path, IDirect3DDevice9 *device)
{
    IDirect3DSurface9 *back = nullptr, *copy = nullptr;
    D3DSURFACE_DESC desc{};
    bool ok = SUCCEEDED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) &&
              SUCCEEDED(back->GetDesc(&desc)) &&
              SUCCEEDED(device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM,
                                                            &copy, nullptr)) &&
              SUCCEEDED(device->GetRenderTargetData(back, copy));
    D3DLOCKED_RECT locked{};
    if (ok) ok = SUCCEEDED(copy->LockRect(&locked, nullptr, D3DLOCK_READONLY));
    if (ok && desc.Format != D3DFMT_X8R8G8B8 && desc.Format != D3DFMT_A8R8G8B8) {
        std::fprintf(stderr, "render_object: back buffer format %u is not 32-bit\n", unsigned(desc.Format));
        ok = false;
    }
    if (ok) {
        const uint32_t row = (desc.Width * 3 + 3) & ~3u;
        std::vector<uint8_t> pixels(size_t(row) * desc.Height);
        for (uint32_t y = 0; y < desc.Height; ++y) {
            const auto *src = static_cast<const uint8_t *>(locked.pBits) + size_t(y) * locked.Pitch;
            uint8_t *dst = pixels.data() + size_t(desc.Height - 1 - y) * row; // bottom-up
            for (uint32_t x = 0; x < desc.Width; ++x) std::memcpy(dst + x * 3, src + x * 4, 3);
        }
        BITMAPFILEHEADER file{};
        BITMAPINFOHEADER info{};
        file.bfType = 0x4d42;
        file.bfOffBits = sizeof(file) + sizeof(info);
        file.bfSize = file.bfOffBits + uint32_t(pixels.size());
        info.biSize = sizeof(info);
        info.biWidth = LONG(desc.Width);
        info.biHeight = LONG(desc.Height);
        info.biPlanes = 1;
        info.biBitCount = 24;
        info.biSizeImage = uint32_t(pixels.size());
        FILE *out = nullptr;
        ok = fopen_s(&out, path, "wb") == 0 && out != nullptr;
        if (ok) {
            ok = std::fwrite(&file, sizeof(file), 1, out) == 1 && std::fwrite(&info, sizeof(info), 1, out) == 1 &&
                 std::fwrite(pixels.data(), 1, pixels.size(), out) == pixels.size();
            std::fclose(out);
        }
        copy->UnlockRect();
    }
    if (copy) copy->Release();
    if (back) back->Release();
    return ok;
}

// ---- the viewer (cViewerRefNode: its slots as the thumbnail factory calls them) ----

struct ViewerRect {
    int32_t left, top, right, bottom;
};
struct ViewerClear { // slot 91's argument: what to clear (2: colour and depth), the colour, the depth, the stencil
    uint32_t flags;
    float color[4];
    float depth;
    uint32_t stencil;
};

void SetUpViewer(void *viewer)
{
    const ViewerRect rect{0, 0, int32_t(kWidth), int32_t(kHeight)};
    const ViewerClear clear{2, {0.0f, 0.0f, 0.0f, 0.0f}, 1.0f, 0};
    Call<void(__thiscall *)(void *, uint32_t), 124>(viewer, 1);            // draw to the device's targets
    Call<void(__thiscall *)(void *, const ViewerRect *), 110>(viewer, &rect); // the target rectangle
    Call<void(__thiscall *)(void *, uint32_t, uint32_t), 130>(viewer, 0, 1);  // perspective
    Call<void(__thiscall *)(void *, uint32_t, float), 132>(viewer, 0, 0.3491f); // field of view (20 degrees)
    Call<void(__thiscall *)(void *, float), 131>(viewer, float(kWidth) / float(kHeight)); // aspect
    Call<void(__thiscall *)(void *, const ViewerRect *), 128>(viewer, &rect); // viewport
    Call<void(__thiscall *)(void *, const ViewerClear *), 91>(viewer, &clear);
}

// The CRES's own viewer, when it has one (a direct child that answers the viewer interface), as the factory's load asks.
void *FindViewer(nGZSceneGraph::cICompositionTreeNode *root)
{
    for (auto *child = root->GetFirstChild(); child; child = child->GetNextSibling()) {
        void *viewer = nullptr;
        if (Call<bool(__thiscall *)(void *, uint32_t, void **), 0>(child, kViewerIid, &viewer)) return viewer;
    }
    return nullptr;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: render_object <Sims3D dir> [cres-name [out.bmp]]\n");
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string dir = argv[1];
    const char *cresName = argc > 2 ? argv[2] : "sculptureChinaVase_cres";
    const char *outPath = argc > 3 ? argv[3] : "render_object.bmp";

    // 1. Start-up.
    StandUpFramework();
    AddService(nGZEvent::cEventSystemService::Create(), "the event system");
    AddService(cGZMessageServer2::Create(), "the message server");
    auto *canvas = nGZGraphic4::cCanvasW32::Create();
    AddService(canvas, "the window canvas");
    canvas->SetSize(int32_t(kWidth), int32_t(kHeight));
    ShowWindow(static_cast<HWND>(canvas->GetHwnd()), SW_HIDE); // glue: the canvas's Init shows its window
    AddService(nGZGraphic4::cGZCanvas::Create(), "the canvas");
    auto *graphics = nGZGraphic4::cGraphicSystemDX::Create();
    AddService(graphics, "the DX graphic system");
    AddService(nGZGraphic4::cGZGraphicSystem::Create(), "the graphic system");

    static gz::cGZPersistResourceManager manager;
    manager.AddRef();
    AddService(&manager, "the resource manager");
    // The packages the default object's chain spans (CRES and thumbnail CRES in 05, SHPE and TXTR in 06, GMND in 04,
    // GMDC in 03, TXMT in 02, LIFO in 09); every Objects0*.package so other objects work too.
    for (int i = 1; i <= 9; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "\\Objects0%d.package", i);
        const std::string path = dir + name;
        if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES && !AddPackage(manager, path)) return 1;
    }

    AddService(nGZSceneGraph::cSceneGraphSystem::Create(), "the scene-graph system");

    // 2. The device.
    nGZGraphic4::cDeviceDX *device = graphics->GetDevice(0);
    nGZGraphic4::cDeviceSettings settings = device->GetSettings();
    settings.mBackBufferCount = 1;
    if (!device->ApplySettings(&settings) || !device->GetD3DDevice()) {
        std::fprintf(stderr, "render_object: no Direct3D device\n");
        return 1;
    }

    // 3. The scene.
    auto *com = reinterpret_cast<rz::cGZCOMDirector *>(GetCOMDirector());
    nGZSceneGraph::cISceneNode *scene = nullptr;
    nGZSceneGraph::cITransformNode *transform = nullptr;
    void *viewer = nullptr;
    if (!com->GetClassObject(nGZSceneGraph::cSceneNode::kClassId, kSceneIid, reinterpret_cast<void **>(&scene)) ||
        !com->GetClassObject(0xdca76dbb, kViewerIid, &viewer) ||
        !com->GetClassObject(0x65246462, kTransformIid, reinterpret_cast<void **>(&transform))) {
        std::fprintf(stderr, "render_object: cannot create the scene nodes\n");
        return 1;
    }
    scene->AddChild(static_cast<nGZSceneGraph::cICompositionTreeNode *>(viewer));
    scene->AddChild(transform);
    SetUpViewer(viewer);

    // 4. The model.
    StringKeyParser::Result key{};
    StringKeyParser::ParseKey(&key, cresName, kCresType, nullptr);
    std::printf("CRES %s: type 0x%08x group 0x%08x instance 0x%08x resource 0x%08x\n", cresName, unsigned(key.value),
                unsigned(key.id), unsigned(key.hash24), unsigned(key.crc32));
    void *resource = nullptr;
    if (!manager.vf3(uint32_t(reinterpret_cast<uintptr_t>(&key)), kCresIid, uint32_t(reinterpret_cast<uintptr_t>(&resource)),
                     0, 0) ||
        !resource) {
        std::fprintf(stderr, "render_object: no resource for %s\n", cresName);
        return 1;
    }
    nGZSceneGraph::cIResourceNode *model = nullptr;
    static_cast<nGZSceneGraph::cIResourceNode *>(resource)->Clone(1, kCresIid, reinterpret_cast<void **>(&model),
                                                                     nullptr);
    if (!model) {
        std::fprintf(stderr, "render_object: cannot clone %s\n", cresName);
        return 1;
    }
    if (void *own = FindViewer(model)) { // the CRES carries its camera
        scene->RemoveChild(static_cast<nGZSceneGraph::cICompositionTreeNode *>(viewer));
        viewer = own;
        SetUpViewer(viewer);
    }
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    transform->SetTranslation(origin);
    transform->AddChild(model);

    // 5. One frame.
    IDirect3DDevice9 *d3d = device->GetD3DDevice();
    d3d->BeginScene(); // glue: the game's frame (cDeviceDX's render-scene event) brackets the viewer's render
    Call<void(__thiscall *)(void *), 123>(viewer);
    d3d->EndScene();

    // 6. The image.
    if (!WriteBmp(outPath, d3d)) {
        std::fprintf(stderr, "render_object: cannot write %s\n", outPath);
        return 1;
    }
    std::printf("wrote %s (%ux%u)\n", outPath, unsigned(kWidth), unsigned(kHeight));
    return 0;
}
