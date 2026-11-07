#include "game/game.h"
#include "game/vtable.h"
#include "rz/Singleton_20ddfb.h"
#include "simantics/PrimGenericSimCall.h"
#include "simantics/PrimGenericSimCallMisc.h"
#include "simantics/PrimGenericSimCallOriginalsB.h"
#include "simantics/PrimGenericSimCallRefs.h"
#include "simantics/PrimXCommon.h"

#include <cstring>

using gscref::PlainRef;
using gscref::VBaseRef;
using primX::Call;

// GenericSimCall (opcode 0x01) cases, group L: 52 (the tile in front of an object) and 43 (can the object stand where it is).

namespace {
constexpr uint32_t kNoInterface = 0x1c;

inline float GameFloat(uint32_t rva)
{
    return *GameAddress<float>(rva);
}
} // namespace

// Case 52 (result 1 when a tile was found, else 0): the own attribute interface (none throws 0x1c) gives its owner's position;
// the point 1.5 in front of it (the offset (0, 1.5, 0) turned by the direction notch of argument 0) is tested by the grid's
// level 3 (slot 19: from the position to the point, filling a box). The two tiles the box touches are compared by the squared
// distance of the owner's position to their centres (half a tile in): the nearer one that the tile service accepts (slot 71)
// and that has an object on it gives that object's slot 47 value into argument 1 (argument 1 is 0 otherwise).
uint32_t GenericSimCall::Case52()
{
    const uint16_t notch = static_cast<uint16_t>(Arg(0));
    PlainRef iface;
    Call<void>(mSelf, 0, gscref::kAttributeIid, iface.Reset());
    if (!iface.Get()) Throw(kNoInterface);
    void *const tileService = gscorig::TileService();
    void *const grid = Call<void *>(tileService, 39);
    void *const level = Call<void *>(grid, 40, 3u);
    void *const owner = Call<void *>(iface.Get(), 8);
    float position[3];
    Call<void>(owner, 42, position);
    float offset[3] = {0.0f, GameFloat(0xdb32cc), 0.0f};
    float rotation[4];
    gscorig::NotchQuaternion(rotation, notch);
    gscorig::RotateVector(offset, rotation);
    float target[3];
    target[0] = position[0] + offset[0];
    target[1] = position[1] + offset[1];
    std::memcpy(&target[2], &position[2], sizeof(float)); // the third word is carried over untouched
    // The box the line test fills: {-1, -1, 0, -1, -1, 0} to start with (0x350d80).
    float box[6] = {-1.0f, -1.0f, 0.0f, -1.0f, -1.0f, 0.0f};
    if ((Call<uint32_t>(level, 19, position, target, box, 0u, 0u) & 0xff) == 0) return 0;
    (void)Call<void *>(GetCachedSingleton(), 26); // the original reads the object table here and does not use it
    int32_t scratchA[4];
    int32_t scratchB[4];
    const int32_t *const rawA = gscorig::FirstTileOfBox(scratchA, box);
    const int32_t tileA[3] = {rawA[0], rawA[1], rawA[2]};
    const int32_t *const rawB = gscorig::SecondTileOfBox(scratchB, box);
    const int32_t tileB[3] = {rawB[0], rawB[1], rawB[2]};
    const float half = GameFloat(0xd9c694);
    const float ax = position[0] - static_cast<float>(tileA[0]) - half;
    const float ay = position[1] - static_cast<float>(tileA[1]) - half;
    const float distanceA = ax * ax + ay * ay;
    const float bx = position[0] - static_cast<float>(tileB[0]) - half;
    const float by = position[1] - static_cast<float>(tileB[1]) - half;
    const float distanceB = bx * bx + by * by;
    SetArg(1, 0);
    const int32_t *const chosen = (distanceB > distanceA) ? tileA : tileB;
    if ((Call<uint32_t>(tileService, 71, chosen) & 0xff) != 0) {
        PlainRef standing(gscorig::ObjectOnTile(chosen, 0));
        const int32_t value = standing.Get() ? static_cast<int32_t>(Call<int16_t>(standing.Get(), 47)) : 0;
        SetArg(1, static_cast<uint16_t>(value));
    }
    return 1;
}

// Case 43 (result 1 when the object may stand where it is, else 0): the own attribute interface (none throws 0x1c), its owner's
// "footprint" interface (0xbc66baec / 0x3c66baf1) says whether the object is blocked outright (slot 0) or too small (slot 13
// below 1.5). Then every other object of the owner's list (vf63 slot 45) whose footprint is solid (slots 6 and 22 both 0) must
// be at least the threshold at 0xdaf06c away (distance of the cells, slot 33). With argument 0 not 1 the position itself must
// not be blocked (0x7e7e00 with the radius at 0xdb099c) and, for a door-like object (slot 48 or 49 of the attribute interface itself,
// not of its owner, answers yes), the point in front of it (offset by the kind, slot 21 of the interface: 0.5 for kind 2, the
// constant at 0xda03e0 for 4 and 8) must not be either; with argument 0 equal to 1 the same point test uses a radius that depends on
// the kind. At last the placement worker (0x7e8c60, mode 5; arguments: the owner, its reported position, the address of its direction
// handle) must accept the object. Difftested against 0x58bc50 (tests/simantics_prims_gsc_case43.cpp).
uint32_t GenericSimCall::Case43()
{
    PlainRef self;
    Call<void>(mSelf, 0, gscref::kAttributeIid, self.Reset());
    if (!self.Get()) Throw(kNoInterface);
    bool ok = true;
    bool blocked = false;
    const bool modeOne = Arg(0) == 1;
    VBaseRef footprint;
    if ((Call<uint32_t>(Call<void *>(self.Get(), 8), 9, 0xbc66baecu, 0x3c66baf1u, footprint.Reset()) & 0xff) != 0) {
        if ((Call<uint32_t>(footprint.Get(), 0) & 0xff) != 0) {
            blocked = true;
        } else if (GameFloat(0xdb32cc) > Call<float>(footprint.Get(), 13)) {
            blocked = true;
        }
    }
    if (ok && !blocked) {
        void *const list = Call<void *>(Call<void *>(Call<void *>(self.Get(), 8), 63), 45); // {first, last} of object pointers
        void **const bounds = static_cast<void **>(list);
        for (void **it = static_cast<void **>(bounds[0]); it != static_cast<void **>(bounds[1]); ++it) {
            void *const other = *it;
            if (other == self.Get()) continue;
            VBaseRef otherFootprint;
            if ((Call<uint32_t>(Call<void *>(other, 8), 9, 0xbc66baecu, 0x3c66baf1u, otherFootprint.Reset()) & 0xff) == 0) continue;
            if (Call<uint32_t>(otherFootprint.Get(), 6) != 0) continue;
            if (Call<uint32_t>(otherFootprint.Get(), 22) != 0) continue;
            const float threshold = GameFloat(0xdaf06c);
            // The original fetches both owners first (the other's, then ours), then asks each for its cell (the other's first).
            void *const otherOwner = Call<void *>(other, 8);
            void *const ownOwner = Call<void *>(self.Get(), 8);
            const int32_t *const otherCell = Call<const int32_t *>(otherOwner, 33);
            const int32_t *const ownCell = Call<const int32_t *>(ownOwner, 33);
            const float distance = gscmisc::FloatSqrt(gscmisc::ScaledDistanceSquared(ownCell, otherCell, GameFloat(0xdc87b8)));
            if (threshold > distance) {
                ok = false;
                break;
            }
        }
    }
    // The point `reach` in front of the owner's position, turned by the owner's direction (slot 43 quarter-turns scaled).
    auto frontPoint = [&](float reach, float *point) {
        Call<void>(Call<void *>(self.Get(), 8), 42, point);
        const int32_t turns = Call<int32_t>(Call<void *>(self.Get(), 8), 43);
        const float angle = (static_cast<float>(turns) * GameFloat(0xda77d8)) / GameFloat(0xda03e4);
        const float sine = gscorig::SineOf(angle);
        point[0] = point[0] + sine * reach;
        const float cosine = gscorig::CosineOf(angle);
        point[1] = point[1] + (-reach) * cosine;
    };
    if (ok && !blocked) {
        if (!modeOne) {
            float position[3];
            void *const reported = Call<void *>(Call<void *>(self.Get(), 8), 42, position);
            if (gscorig::PositionBlocked(static_cast<const float *>(reported), GameFloat(0xdb099c), 1, 1)) ok = false;
            if ((Call<uint32_t>(self.Get(), 48) & 0xff) != 0 || (Call<uint32_t>(self.Get(), 49) & 0xff) != 0) {
                const int32_t kind = Call<int32_t>(self.Get(), 21);
                float reach = 0.0f;
                if (kind == 2) reach = GameFloat(0xd9c694);
                else if (kind == 4 || kind == 8) reach = GameFloat(0xda03e0);
                float point[3];
                frontPoint(reach, point);
                if (gscorig::PositionBlocked(point, GameFloat(0xdb099c), 1, 1)) ok = false;
            }
        } else {
            if ((Call<uint32_t>(self.Get(), 48) & 0xff) != 0 || (Call<uint32_t>(self.Get(), 49) & 0xff) != 0) {
                const int32_t kind = Call<int32_t>(self.Get(), 21);
                float reach = 0.0f;
                float radius = GameFloat(0xd9c698);
                if (kind == 2) {
                    reach = GameFloat(0xda03e0);
                    radius = GameFloat(0xdb32cc);
                } else if (kind == 4 || kind == 8) {
                    reach = GameFloat(0xda74f4);
                    radius = GameFloat(0xdac534);
                }
                float point[3];
                frontPoint(reach, point);
                if (gscorig::PositionBlocked(point, radius, 1, 1)) ok = false;
            }
        }
    }
    if (ok && !blocked) {
        int32_t handle = Call<int32_t>(Call<void *>(self.Get(), 8), 43);
        float position[3];
        void *const reported = Call<void *>(Call<void *>(self.Get(), 8), 42, position);
        if (!gscorig::PlaceObjectMode(Call<void *>(self.Get(), 8), reported, &handle, 5)) ok = false;
    }
    return ok ? 1u : 0u;
}
