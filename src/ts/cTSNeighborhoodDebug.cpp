// cTSNeighborhood: RunLotResetCommands (slot 17, 0x479cd0), lane EG.
#include "ts/cTSNeighborhood.h"
#include "game/game.h"
#include "game/vtable.h"

#include "game/original.h"
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "ifaces/SGDebugDraw.h"
#include "ifaces/cTSSGSystem.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"

#include <set>
#include <string>
#include <vector>

namespace {
constexpr uint32_t kResetCommandWord = 0x10daa30;  // a 16-bit game global handed to b's slot 41
constexpr uint32_t kResetLotTextCell = 0x1011d18;  // a char * cell: "reset lot"
constexpr uint32_t kSgSystemCache = 0x10cc458;
constexpr uint32_t kSgSystemIdA = 0x9a353ce8;
constexpr uint32_t kSgSystemIdB = 0xfa353cdb;
constexpr uint32_t kArchMaterial = 0xdca238;       // "RelationshipArchGreen" (game data)
constexpr float kCellSize = 10.0f;                 // 0xda8658
constexpr float kHalf = 0.5f;                      // 0xd9c694
constexpr float kArchDrop = 2.0f;                  // 0xd9c69c: the arch ends sit this far below the terrain
constexpr float kArchWidth = 1.0f;
constexpr uint32_t kArchSegments = 8;
constexpr uint32_t kArchArg7 = 0x20;
constexpr float kArchAngle = 45.0f;

// The vectors the family and neighbour managers fill: blocks of the game's allocator service.
using FamilyIdVector = std::vector<int16_t, GZServiceAllocator<int16_t>>;
using NeighborIdVector = std::vector<uint32_t, GZServiceAllocator<uint32_t>>;

struct LotRect {
    int32_t left, top, right, bottom;
};
struct Point3 {
    float x, y, z;
};

// 0x472e10 (thiscall, ret 8; not ours yet): highlights the lot through the SG render id registry (null name: the default
// highlight), keeping the render id in +0xa8.
void HighlightLot(cTSNeighborhood *self, int32_t lot, const char *name)
{
    OriginalFunction<void(__thiscall *)(cTSNeighborhood *, int32_t, const char *), 0x472e10>()(self, lot, name);
}

LotRect BoundsOf(void *lot)
{
    LotRect rect;
    VirtualCall<void(__thiscall *)(void *, LotRect *), 12>(lot, &rect);
    return rect;
}
// The terrain's height at a cell (cITSNHoodTerrain slot 10; an x87 float result).
float HeightAt(void *terrain, int32_t x, int32_t y) { return VirtualCall<float(__thiscall *)(void *, int32_t, int32_t), 10>(terrain, x, y); }
} // namespace

void cTSNeighborhood::RunLotResetCommands(void *a, void *b, void *c)
{
    VirtualCall<void(__thiscall *)(void *, uint32_t), 72>(a, 0);
    VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t, int32_t, int32_t, int32_t, int32_t), 41>(
        b, *GameAddress<uint16_t>(kResetCommandWord), 0, -1, -1, -1, -1);
    VirtualCall<void(__thiscall *)(void *), 42>(b);
    VirtualCall<void(__thiscall *)(void *, uint32_t), 25>(b, 1);
    cRZString text{GameAddress<void>(cRZString::kVTable), std::string(*GameAddress<const char *>(kResetLotTextCell)), 0};
    VirtualCall<void(__thiscall *)(void *, cRZString *, uint32_t, uint32_t, uint32_t, uint32_t), 54>(c, &text, 0, 0, 0, 0);
}

namespace {
// The centre of a lot's bounds in world units, at the terrain height of its first corner minus the drop.
Point3 LotCentre(void *terrain, const LotRect &rect)
{
    Point3 at;
    at.x = static_cast<float>(rect.right - rect.left) * kCellSize * kHalf + static_cast<float>(rect.left) * kCellSize;
    at.y = static_cast<float>(rect.bottom - rect.top) * kCellSize * kHalf + static_cast<float>(rect.top) * kCellSize;
    at.z = HeightAt(terrain, rect.left, rect.top) - kArchDrop;
    return at;
}
} // namespace

void cTSNeighborhood::DebugDrawFamilyArches(void *family)
{
    UnregisterRenderIds();
    auto *sg = As<ifaces::cTSSGSystem>(CachedServiceGetter<kSgSystemCache, kSgSystemIdA, kSgSystemIdB>());
    ifaces::SGDebugDraw *draw = sg->GetDebugDraw();
    if (!draw || !family) return;
    draw->Begin();
    void *app = GetCachedSingleton();
    void *terrain;
    void *home;
    void *families;
    if (app && (terrain = VirtualCall<void *(__thiscall *)(void *), 11>(app)) != nullptr &&
        (home = GetLot(VirtualCall<int32_t(__thiscall *)(void *), 42>(family))) != nullptr) {
        const Point3 from = LotCentre(terrain, BoundsOf(home));
        families = VirtualCall<void *(__thiscall *)(void *), 9>(app); // the family manager
        if (families) {
            std::set<uint32_t> lots; // the home lots of the families the relatives belong to
            FamilyIdVector familyIds;
            VirtualCall<void(__thiscall *)(void *, FamilyIdVector *), 19>(families, &familyIds);
            NeighborIdVector relatives;
            void *neighbors = VirtualCall<void *(__thiscall *)(void *), 10>(app); // the neighbour manager
            if (!neighbors) {
                draw->End();
                return;
            }
            VirtualCall<void(__thiscall *)(void *, void *, NeighborIdVector *, uint32_t), 27>(neighbors, family, &relatives, 1);
            for (uint32_t relative : relatives) {
                for (int16_t id : familyIds) {
                    void *other = VirtualCall<void *(__thiscall *)(void *, int32_t), 11>(families, id);
                    if (other && VirtualCall<bool(__thiscall *)(void *, uint32_t), 29>(other, relative)) {
                        lots.insert(VirtualCall<uint32_t(__thiscall *)(void *), 42>(other));
                        break;
                    }
                }
            }
            for (uint32_t number : lots) {
                if (void *lot = GetLot(static_cast<int32_t>(number))) {
                    const Point3 to = LotCentre(terrain, BoundsOf(lot));
                    draw->DrawArch(const_cast<Point3 *>(&from), to.x, to.y, to.z, GameAddress<const char>(kArchMaterial), kArchWidth,
                                   kArchSegments, kArchArg7, kArchAngle);
                    HighlightLot(this, static_cast<int32_t>(number), nullptr);
                }
            }
        }
    }
    draw->End();
}

// ---- adapters (the RVA hooks; src/hook/tables/cTSNeighborhood.cpp): next to the bodies they inline ----

using Adapters = cTSNeighborhoodAdapters;

void __fastcall Adapters::RunLotResetCommands(Self *self, void *, void *a, void *b, void *c)
{
    [[msvc::forceinline_calls]] self->Self::RunLotResetCommands(a, b, c);
}

void __fastcall Adapters::DebugDrawFamilyArches(Self *self, void *, void *family)
{
    [[msvc::forceinline_calls]] self->Self::DebugDrawFamilyArches(family);
}
