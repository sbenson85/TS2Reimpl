// nTSCASCore::cTSCASBodyPartSystem (lane KB): slot 84, the export of a part's mesh as a Wavefront .obj file (and its
// composite texture as a .bmp). The face's `this` is the subobject at +0x1c; see cTSCASBodyPartSystem.h.
#include "sg/cTextureManager.h"
#include "cas/cTSCASBodyPartSystem.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/GraphicServices.h"
#include "misc/UtilitiesE.h"
#include "rz/cRZTextFile.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace nTSCASCore {

namespace {

cTSCASBodyPartSystem *SystemOf(BodyPartSystemIface *iface)
{
    return reinterpret_cast<cTSCASBodyPartSystem *>(reinterpret_cast<uint8_t *>(iface) - 0x1c);
}

// The lines, the image's own texts.
constexpr uint32_t kRvaHeader = 0xeba6a8;        // "# Export of face from CAS"
constexpr uint32_t kRvaPositionCount = 0xeba6c4; // "#  %d position vertices"
constexpr uint32_t kRvaPosition = 0xeba6dc;      // "v %f %f %f"
constexpr uint32_t kRvaBlank = 0xd9b9d0;         // ""
constexpr uint32_t kRvaUvCount = 0xeba6e8;       // "# Next %d UV texture coordinates"
constexpr uint32_t kRvaUv = 0xeba70c;            // "vt %f %f"
constexpr uint32_t kRvaNormalCount = 0xeba718;   // "# Next %d normals"
constexpr uint32_t kRvaNormal = 0xeba72c;        // "vn %f %f %f"
constexpr uint32_t kRvaFaceCount = 0xeba738;     // "# Next %d faces"
constexpr uint32_t kRvaFace = 0xeba748;          // "f %d/%d/%d %d/%d/%d %d/%d/%d"
constexpr uint32_t kRvaFaceName = 0xeb9f30;      // "face"
constexpr uint32_t kRvaTextureName = 0xe8ba20;   // "compositeBaseTextureName"

// 0xeba938 (0x358637be, one step above 1e-6f): the squared distance above which a corner gets its own coordinate.
constexpr float kUvTolerance = 0x1.0c6f7cp-20f;
static_assert(kUvTolerance > 1e-6f, "the image's constant is the float after 1e-6f");
constexpr uint32_t kImageIid = 0x5b891e1e;
// cRZTextFile::Open's arguments as the original passes them (write, create, no sharing).
constexpr uint32_t kOpenAccess = 2, kOpenCreation = 1, kOpenSharing = 0;

// cRZTextFile's formatted write (0x1c4396, __cdecl, not ours): printf into a line, written with its end of line.
template <typename... Args> void WriteLine(cRZTextFile &file, uint32_t format, Args... args)
{
    OriginalFunction<uint8_t(__cdecl *)(cRZTextFile *, const char *, ...), 0x1c4396>()(&file, GameAddress<const char>(format), args...);
}

// A text file on the stack, closed and destroyed at the end of its scope.
struct LocalTextFile {
    union {
        cRZTextFile mFile;
    };
    explicit LocalTextFile(const char *path) { mFile.Construct(path); }
    LocalTextFile(const LocalTextFile &) = delete;
    ~LocalTextFile() { mFile.Destruct(); }
};

// ORIGINAL-BUG (not reproduced): the original fills missing indices with nodes whose value it never writes, so their
// lines print whatever the heap held; ours print zeros.
struct Point3 {
    float mXyz[3] = {};
};
struct Uv {
    float mU, mV;
};

// What the export collects from the entry's vertex and face groups.
struct ObjMesh {
    std::map<int32_t, Point3> mPositions;  // by the vertex's position index
    std::map<int32_t, Point3> mNormals;    // by its normal index
    std::map<int32_t, int32_t> mUvOfPosition; // the first coordinate seen for a position index
    std::vector<Uv> mUvs;
    std::vector<uint32_t> mCornerUvs;      // per face corner, in face order
    uint16_t mLastPosition = 0, mLastNormal = 0; // the largest indices seen

    void AddCorner(const PartMeshVertex &v)
    {
        mPositions[v.mPositionIndex] = Point3{{v.mPosition[0], v.mPosition[1], v.mPosition[2]}};
        if (mLastPosition < v.mPositionIndex) mLastPosition = v.mPositionIndex;
        mNormals[v.mNormalIndex] = Point3{{v.mNormal[0], v.mNormal[1], v.mNormal[2]}};
        if (mLastNormal < v.mNormalIndex) mLastNormal = v.mNormalIndex;
        uint16_t uv;
        const auto seen = mUvOfPosition.find(v.mPositionIndex);
        if (seen == mUvOfPosition.end()) {
            mUvs.push_back(Uv{v.mUv[0], v.mUv[1]});
            uv = static_cast<uint16_t>(mUvs.size() - 1);
            mUvOfPosition[v.mPositionIndex] = uv;
        } else {
            uv = static_cast<uint16_t>(seen->second);
            const Uv &kept = mUvs[uv];
            const float du = kept.mU - v.mUv[0], dv = kept.mV - v.mUv[1];
            if (du * du + dv * dv > kUvTolerance) {
                // its own coordinate, not remembered for the position
                mUvs.push_back(Uv{v.mUv[0], v.mUv[1]});
                uv = static_cast<uint16_t>(mUvs.size() - 1);
            }
        }
        mCornerUvs.push_back(uv);
    }
};

// Indices 0..last each get an entry (when the map has any).
void FillGaps(std::map<int32_t, Point3> &points, uint16_t last)
{
    if (points.empty()) return;
    for (int32_t i = 0; i <= last; ++i) points.try_emplace(i);
}

void WritePoints(cRZTextFile &file, uint32_t countFormat, uint32_t lineFormat, const std::map<int32_t, Point3> &points)
{
    WriteLine(file, countFormat, static_cast<uint32_t>(points.size()));
    for (const auto &[index, p] : points) {
        WriteLine(file, lineFormat, static_cast<double>(p.mXyz[0]), static_cast<double>(p.mXyz[1]), static_cast<double>(p.mXyz[2]));
    }
    WriteLine(file, kRvaBlank);
}

// The part's composite texture, saved by the image service as `path` + ".bmp" (each step may find nothing).
void SaveTexture(void *part, const char *path)
{
    const uint32_t faceId = VirtualCall<uint32_t(__thiscall *)(void *, const char *), 53>(part, GameAddress<const char>(kRvaFaceName));
    if (faceId == 0) return;
    void *const textures = nGZSceneGraph::TextureManager();
    void *const names = GetCachedQueriedObject();
    const uint32_t index = VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 19>(names, faceId);
    struct NameEntry {
        uint32_t mId;
        void *mProperties;
        uint32_t mRest[3];
    } entry; // the 0x14 bytes slot 33 writes
    VirtualCall<void *(__thiscall *)(void *, NameEntry *, uint32_t), 33>(names, &entry, index);
    const char *const textureName =
        VirtualCall<const char *(__thiscall *)(void *, const char *), 23>(entry.mProperties, GameAddress<const char>(kRvaTextureName));
    if (textureName == nullptr) return;
    if (!VirtualCall<bool(__thiscall *)(void *, const char *), 25>(textures, textureName)) return;
    const uint32_t textureId = VirtualCall<uint32_t(__thiscall *)(void *, const char *), 17>(textures, textureName);
    void *const texture = VirtualCall<void *(__thiscall *)(void *, uint32_t), 23>(textures, textureId);
    if (texture == nullptr) return;
    void *const level = VirtualCall<void *(__thiscall *)(void *, uint32_t), 8>(texture, 0);
    if (level == nullptr) return;
    void *image = nullptr;
    if (VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(level, kImageIid, &image) &&
        VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 28>(image, 0) != 0) {
        const std::string bmpName = std::string(path) + GameAddress<const char>(0xe4a650); // ".bmp"
        void *const imageService = nGZGraphic4::GetImageManipCached();
        void *const pixels = VirtualCall<void *(__thiscall *)(void *, uint32_t, uint32_t), 26>(image, 0, 0);
        VirtualCall<void(__thiscall *)(void *, const char *, void *), 11>(imageService, bmpName.c_str(), pixels);
    }
    if (image) VirtualCall<void(__thiscall *)(void *), 2>(image);
}

} // namespace

bool BodyPartSystemIface::ExportFaceObj(const char *path, uint32_t kind, uint32_t key)
{
    LocalTextFile obj((std::string(path) + GameAddress<const char>(0xeba6a0)).c_str()); // ".obj"
    cRZTextFile &file = obj.mFile;
    if (!file.Open(kOpenAccess, kOpenCreation, kOpenSharing)) return false;
    WriteLine(file, kRvaHeader);

    cTSCASBodyPartSystem *const system = SystemOf(this);
    const uint32_t id = mAge != 0 ? 2 : 0;
    auto *const entry = OriginalFunction<PartMeshEntry *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t), 0xcf04b0>()(
        system, kind, key, id, 0);
    OriginalFunction<void(__thiscall *)(void *, void *), 0xcfbbb0>()(system, entry);
    OriginalFunction<void(__thiscall *)(void *, void *), 0xce7430>()(system, entry);

    // Face group g indexes vertex group g.
    ObjMesh mesh;
    const PartMeshGroup *vertices = entry->mGroupsFirst;
    for (const PartMeshFaceGroup *faces = entry->mFaceGroupsFirst; faces != entry->mFaceGroupsLast; ++faces, ++vertices) {
        for (const PartMeshFace *face = faces->mFirst; face != faces->mLast; ++face) {
            for (int c = 0; c < 3; ++c) mesh.AddCorner(vertices->mFirst[face->mCorners[c]]);
        }
    }
    FillGaps(mesh.mPositions, mesh.mLastPosition);
    FillGaps(mesh.mNormals, mesh.mLastNormal);

    WritePoints(file, kRvaPositionCount, kRvaPosition, mesh.mPositions);
    WriteLine(file, kRvaUvCount, static_cast<uint32_t>(mesh.mUvs.size()));
    for (const Uv &uv : mesh.mUvs) WriteLine(file, kRvaUv, static_cast<double>(uv.mU), static_cast<double>(1.0f - uv.mV));
    WriteLine(file, kRvaBlank);
    WritePoints(file, kRvaNormalCount, kRvaNormal, mesh.mNormals);
    // ORIGINAL-BUG: the face count line gives the number of face groups, not of faces.
    WriteLine(file, kRvaFaceCount, static_cast<uint32_t>(entry->mFaceGroupsLast - entry->mFaceGroupsFirst));
    const uint32_t *cornerUv = mesh.mCornerUvs.data();
    vertices = entry->mGroupsFirst;
    for (const PartMeshFaceGroup *faces = entry->mFaceGroupsFirst; faces != entry->mFaceGroupsLast; ++faces, ++vertices) {
        for (const PartMeshFace *face = faces->mFirst; face != faces->mLast; ++face, cornerUv += 3) {
            const PartMeshVertex &a = vertices->mFirst[face->mCorners[0]];
            const PartMeshVertex &b = vertices->mFirst[face->mCorners[1]];
            const PartMeshVertex &c = vertices->mFirst[face->mCorners[2]];
            // 1-based: position / coordinate / normal per corner
            WriteLine(file, kRvaFace, a.mPositionIndex + 1, cornerUv[0] + 1, a.mNormalIndex + 1, b.mPositionIndex + 1, cornerUv[1] + 1,
                      b.mNormalIndex + 1, c.mPositionIndex + 1, cornerUv[2] + 1, c.mNormalIndex + 1);
        }
    }
    WriteLine(file, kRvaBlank);
    file.Close();

    void *const part = VirtualCall<void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t), 52>(this, kind, key, id);
    if (part) {
        VirtualCall<uint32_t(__thiscall *)(void *), 1>(part); // AddRef
        SaveTexture(part, path);
        VirtualCall<uint32_t(__thiscall *)(void *), 2>(part); // Release
    }
    return true;
}

} // namespace nTSCASCore
