#include "rz/RZMessageServer.h"
#include "simantics/PrimR.h"

#include "game/game.h"
#include "game/original.h"
#include "rz/RZMemory.h"
#include "rz/StaticGetters.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZRandom.h"
#include "simantics/PrimRCommon.h"

#include <cstring>

using namespace primr;

// ---------------------------------------------------------------------------------------------------------
// Opcode 0x10
// ---------------------------------------------------------------------------------------------------------

namespace {

// The routing request the walker is asked to plan (0x57c890 builds it, 0x24 bytes): where to, which way to face and
// a few flags.
struct RouteRequest {
    uint8_t mActive;      // +0x00
    uint8_t mPad1[3];
    int32_t mX;           // +0x04 (16.16 position)
    int32_t mY;           // +0x08
    int32_t mLevel;       // +0x0c
    int32_t mDirection;   // +0x10 (-1: any)
    uint8_t mFlag14;      // +0x14
    uint8_t mFlag15;      // +0x15
    uint8_t mFlag16;      // +0x16
    uint8_t mFlag17;      // +0x17
    uint8_t mFlag18;      // +0x18
    uint8_t mFlag19;      // +0x19
    int16_t mWord1a;      // +0x1a
    uint16_t mWord1c;     // +0x1c (the constructor clears the dword)
    uint16_t mWord1e;     // +0x1e
    uint8_t mByte20;      // +0x20
    uint8_t mPad21[3];

    void Construct() // 0x57c890: only the fields it writes are set
    {
        mActive = 0;
        mWord1a = -1;
        mWord1c = 0;
        mWord1e = 0;
        mX = 0;
        mY = 0;
        mDirection = -1;
        mFlag14 = 1;
        mFlag15 = 1;
        mFlag16 = 0;
        mFlag17 = 0;
        mFlag18 = 0;
        mFlag19 = 0;
        mByte20 = 0xff;
    }
};
static_assert(sizeof(RouteRequest) == 0x24, "RouteRequest layout");
static_assert(offsetof(RouteRequest, mWord1c) == 0x1c, "RouteRequest layout");

constexpr uint32_t kIdRouteStart = 0xaf6129c0;
constexpr uint32_t kIdRouteDone = 0xaf6129b0;

// The signed remainder by 8 the compiler writes out by hand.
inline int32_t Mod8(int32_t value)
{
    return value % 8;
}

// A virtual-base reference (the `57c290` holder), released through the virtual base.
struct HeldVirtualBase {
    RZAutoRef mRef{nullptr};
    HeldVirtualBase() = default;
    HeldVirtualBase(const HeldVirtualBase &) = delete;
    ~HeldVirtualBase() noexcept { mRef.ReleaseVirtualBase(); }
};

// Posts a framework message (cRZMessage2Standard, 0x2c bytes) with `id` and `walker` as its value.
void PostMessageTo(void *walker, uint32_t id)
{
    auto *message = static_cast<uint8_t *>(RZMemory::Malloc(cRZMessage2Standard::kSize));
    if (message) {
        // The message constructor (0x1c033d): vtable, a zero count, a clear flag byte and the rest zero.
        *reinterpret_cast<const void **>(message) = &ts2_rva_da4064;
        *reinterpret_cast<uint32_t *>(message + 4) = 0;
        message[8] = 0;
        for (uint32_t offset = 0x0c; offset <= 0x28; offset += 4) *reinterpret_cast<uint32_t *>(message + offset) = 0;
    }
    Held holder;
    holder.mRef.mObject = message;
    if (message) Call<uint32_t>(message, 1); // AddRef (the 0x57c2f0 holder constructor)
    Call<void>(holder.Get(), 5, id);
    Call<void>(holder.Get(), 12, walker);
    void *server = GetCachedMessageServer();
    Call<void>(server, 3, holder.Get());
}

// The walker's route check (0x7e8c60, __cdecl, seven stack arguments).
inline bool RouteAllowed(void *walker, uint32_t *position, uint32_t *flags)
{
    using Fn = uint8_t(__cdecl *)(void *, void *, void *, uint32_t, uint32_t, uint32_t, uint32_t);
    return OriginalFunction<Fn, 0x7e8c60>()(walker, position, flags, 0, 0, 0, 0) != 0;
}

// The commit sequence after an accepted route.
void CommitRoute(void *walker, uint32_t *position, uint32_t *flags)
{
    Call<void>(walker, 215);
    Call<void>(walker, 171, position, 0u, 1u);
    Call<void>(walker, 170, flags, 1u);
    Call<void>(walker, 216);
}

} // namespace

uint32_t PrimRObject::Prim10(void *frame, const PrimParamView *param)
{
    // The goal: a 16.16 position and a floor; the default is (1.0, 1.0) on floor 1.
    uint32_t goalX = 0x10000, goalY = 0x10000;
    int32_t goalLevel = 1;

    void *table = Call<void *>(mContext, 26);
    const uint8_t f6 = Byte(param, 6);
    void *walker; // the object that walks (ebx)
    void *dest;   // the object walked to, or this
    if (!(f6 & 8)) {
        walker = Call<void *>(table, 29, static_cast<int32_t>(Call<int16_t>(frame, 12)));
        if (!walker) Raise(0x15);
        if (f6 & 1) {
            const uint32_t index = Byte(param, 5);
            if (index >= Call<uint8_t>(frame, 27)) Raise(0x33);
            const int16_t id = Call<int16_t>(frame, 31, index);
            dest = Call<void *>(table, 29, static_cast<int32_t>(id));
            if (!dest) Raise(0x17);
            const uint32_t *position = Call<const uint32_t *>(dest, 33);
            goalX = position[0];
            goalY = position[1];
            goalLevel = Call<int16_t>(dest, 36);
        } else {
            dest = this;
        }
    } else {
        const int16_t first = Read(this, Byte(param, 8), Word(param, 9), nullptr, nullptr);
        walker = Call<void *>(table, 29, static_cast<int32_t>(first));
        if (!walker) Raise(0x17);
        const int16_t second = Read(this, Byte(param, 0xb), Word(param, 0xc), nullptr, nullptr);
        dest = Call<void *>(table, 29, static_cast<int32_t>(second));
        if (!dest) Raise(0x17);
        const uint32_t *position = Call<const uint32_t *>(dest, 33);
        goalX = position[0];
        goalY = position[1];
        goalLevel = Call<int16_t>(dest, 36);
    }

    if (Call<uint8_t>(walker, 25, 0x10000u)) return 0;
    if (Call<uint8_t>(walker, 25, 0x20000u)) return 0;
    if (Call<int32_t>(Call<void *>(walker, 64), 3, 9u) == 0x13) PostMessageTo(walker, kIdRouteStart);

    {
        HeldVirtualBase tracer;
        if (Call<uint8_t>(walker, 12, 0xfa1c39f7u, 0x5d4f2bc3u, &tracer.mRef.mObject)) {
            // The object's virtual-base subobject (the second entry of its virtual-base table).
            uint8_t *object = static_cast<uint8_t *>(tracer.mRef.mObject);
            const uint8_t *vbTable = *reinterpret_cast<uint8_t *const *>(object + 4);
            const int32_t offset = *reinterpret_cast<const int32_t *>(vbTable + 8);
            void *sub = object + offset + 4;
            Call<void>(sub, 86, 1u, GameAddress<const char>(0xdb0924));
        }
    }

    const uint8_t kind = Byte(param, 4);

    if (kind == 1) {
        const uint8_t wasTurning = Call<uint8_t>(walker, 188);
        const uint8_t busy = Call<uint8_t>(walker, 104);
        if (busy) {
            Held trace;
            if (Call<uint8_t>(walker, 0, 0x5a6216f6u, &trace.mRef.mObject)) {
                if (Call<uint8_t>(trace.Get(), 15)) Call<void>(trace.Get(), 17);
            }
        }
        Call<void>(walker, 175, 1u, 0u);
        if (wasTurning && !busy) {
            Held own;
            if (Call<uint8_t>(walker, 0, 0x5a69d3d5u, &own.mRef.mObject)) {
                void *registry = *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(this) + 0x1c4);
                Call<void>(registry, 106, own.Get());
            }
        }
        return 1;
    }

    // The kinds that walk a path of the destination's (a list of waypoints with headings).
    if (kind == 5 || kind == 6 || kind == 7 || kind == 8) {
        int32_t number;
        if (kind == 8) {
            number = static_cast<int16_t>(Word(param, 0xe));
            if (number < 0) Raise(0x1b);
            void *holder = Call<void *>(dest, 63);
            void *vector = Call<void *>(holder, 52);
            const uint32_t count = static_cast<uint32_t>((*reinterpret_cast<int32_t *>(static_cast<uint8_t *>(vector) + 4) -
                                                          *reinterpret_cast<int32_t *>(vector)) >> 2);
            if (static_cast<uint32_t>(number) >= count) Raise(0x1b);
        } else {
            if (kind == 5 || kind == 6) {
                const int32_t slot = static_cast<int16_t>(Word(param, 0xe));
                if (slot < 0) Raise(8);
                if (kind == 5) {
                    if (slot >= Call<uint8_t>(frame, 26)) Raise(8);
                    number = Call<int16_t>(frame, 30, static_cast<uint32_t>(slot));
                } else {
                    if (slot >= Call<uint8_t>(frame, 27)) Raise(8);
                    number = Call<int16_t>(frame, 31, static_cast<uint32_t>(slot));
                }
            } else {
                number = static_cast<int16_t>(Word(param, 0xe));
            }
            if (number < 0 || number >= Call<int32_t>(dest, 250)) Raise(0x1b);
        }
        void *waypoints;
        if (kind == 8) {
            void *holder = Call<void *>(dest, 63);
            void *vector = Call<void *>(holder, 52);
            waypoints = *(reinterpret_cast<void **>(*reinterpret_cast<uint8_t **>(vector)) + number);
        } else {
            waypoints = Call<void *>(dest, 251, static_cast<uint32_t>(number));
        }
        if (!waypoints) return 0;

        Held steps;
        {
            void *director = GetDirectorFrameworkObject();
            steps.mRef.Reset();
            Call<uint8_t>(director, 3, 0x9bfae65du, 0x9bfae65cu, &steps.mRef.mObject);
        }
        Call<void>(steps.Get(), 3, walker, dest, dest, waypoints, 0u, 0u, 1u);
        Call<void>(steps.Get(), 4);
        Call<void>(steps.Get(), 8);
        int32_t index = 0;
        if (Call<int32_t>(steps.Get(), 61) > 0) {
            do {
                uint8_t *item = static_cast<uint8_t *>(Call<void *>(steps.Get(), 62, static_cast<uint32_t>(index)));
                uint32_t heading = static_cast<uint16_t>(Call<int16_t>(steps.Get(), 9, item));
                // The waypoint's position in floats (0x7dcc50): two 16.16 values and the floor minus one.
                uint32_t position[3];
                const float scale = *GameAddress<float>(0xdc87b8);
                const float px = static_cast<float>(*reinterpret_cast<int32_t *>(item)) * scale;
                const float py = static_cast<float>(*reinterpret_cast<int32_t *>(item + 4)) * scale;
                std::memcpy(&position[0], &px, 4);
                std::memcpy(&position[1], &py, 4);
                position[2] = static_cast<uint32_t>(static_cast<int32_t>(*reinterpret_cast<int16_t *>(item + 8)) - 1);
                if (static_cast<uint16_t>(heading) == 0xffff) {
                    // No heading given: try the eight, starting from a random one.
                    void *generator = Call<void *>(mOwner, 55);
                    const uint32_t drawn = static_cast<cRZRandom *>(generator)->RandomUint32Uniform(8);
                    const int32_t start = static_cast<int16_t>(static_cast<uint16_t>(drawn));
                    const int32_t destFacing = Call<int32_t>(dest, 43);
                    Call<int32_t>(walker, 43);
                    const uint32_t allowedMask = static_cast<uint8_t>(~Byte(param, 7));
                    for (int16_t i = 0; i < 8; ++i) {
                        int32_t a = Mod8(static_cast<int32_t>(i) + start);
                        int32_t b = Mod8(a + destFacing);
                        const int32_t bit = Mod8(8 - a);
                        if (allowedMask & (1u << (bit & 31))) {
                            if (Call<uint8_t>(walker, 104) || Call<int16_t>(walker, 13, 0x17u) > 1) {
                                if (b & 1) continue;
                            }
                            uint32_t facing = static_cast<uint32_t>(b);
                            if (RouteAllowed(walker, position, &facing)) {
                                CommitRoute(walker, position, &facing);
                                return 1;
                            }
                        }
                    }
                } else {
                    uint32_t facing = static_cast<uint32_t>(Mod8(10 - static_cast<int16_t>(heading)));
                    bool skip = false;
                    if (Call<uint8_t>(walker, 104) || Call<int16_t>(walker, 13, 0x17u) > 1) {
                        if (facing & 1) skip = true;
                    }
                    if (!skip && RouteAllowed(walker, position, &facing)) {
                        CommitRoute(walker, position, &facing);
                        return 1;
                    }
                }
                ++index;
            } while (index < Call<int32_t>(steps.Get(), 61));
        }
        return 0;
    }

    // The remaining kinds plan a single route to the goal. Kinds 3 and 4 face the destination's heading (kind 4
    // the opposite way) and fail at once if the route is refused; the others try again with the first flag cleared.
    RouteRequest request;
    request.Construct();
    request.mActive = 1;
    request.mX = static_cast<int32_t>(goalX);
    request.mY = static_cast<int32_t>(goalY);
    request.mLevel = goalLevel;
    request.mFlag15 = ((f6 >> 1) & 1) ^ 1;
    request.mFlag17 = (f6 >> 2) & 1;
    request.mFlag18 = (f6 >> 4) & 1;
    request.mFlag19 = (f6 >> 5) & 1;
    request.mFlag16 = f6 >> 7;
    request.mByte20 = static_cast<uint8_t>(~Byte(param, 7));
    uint32_t outPosition[3] = {0xbf800000u, 0xbf800000u, 0u};

    if (kind == 3 || kind == 4) {
        if (!dest) Raise(0x3d);
        int32_t facing = Call<int16_t>(dest, 13, 1u);
        if (kind == 4) facing = Mod8(facing + 2);
        request.mDirection = facing;
        request.mWord1c = static_cast<uint16_t>(Call<int16_t>(dest, 47));
        request.mWord1e = static_cast<uint16_t>(Call<int16_t>(dest, 43));
        request.mWord1a = -1;
        // The flags word the route fills in: the original's slot is the frame argument's (stale until written).
        uint32_t flags = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(frame));
        if (!Call<uint8_t>(walker, 186, &request, outPosition, &flags)) return 0;
        if (Call<uint8_t>(walker, 104) || Call<int16_t>(walker, 13, 0x17u) > 1) {
            if (flags & 1) return 0;
        }
        if (!RouteAllowed(walker, outPosition, &flags)) return 0;
        CommitRoute(walker, outPosition, &flags);
        return 1;
    }

    request.mWord1c = dest ? static_cast<uint16_t>(Call<int16_t>(dest, 47)) : 0;
    request.mWord1e = dest ? static_cast<uint16_t>(Call<int16_t>(dest, 43)) : 0;
    if (f6 & 0x40) request.mWord1a = static_cast<int16_t>(Mod8(Call<int16_t>(dest, 13, 1u)) + 8);
    else request.mWord1a = -1;

    uint32_t flags = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(param));
    uint8_t ok = Call<uint8_t>(walker, 186, &request, outPosition, &flags);
    if (!ok) {
        if (kind == 2) return 0;
        request.mFlag14 = ok;
        ok = Call<uint8_t>(walker, 186, &request, outPosition, &flags);
        if (!ok) return 0;
    }
    if (Call<uint8_t>(walker, 104) || Call<int16_t>(walker, 13, 0x17u) > 1) {
        if (flags & 1) return 0;
    }
    if (!RouteAllowed(walker, outPosition, &flags)) return 0;
    CommitRoute(walker, outPosition, &flags);

    if (Call<int32_t>(walker, 98) == 2) {
        void *tableAgain = Call<void *>(mContext, 26);
        if (Call<int32_t>(tableAgain, 39) == 0) {
            Held own;
            if (Call<uint8_t>(walker, 0, 0x5a69d3d5u, &own.mRef.mObject)) Call<void>(tableAgain, 40, own.Get());
        }
    }
    if (Call<int32_t>(Call<void *>(walker, 64), 3, 9u) == 0x13) PostMessageTo(walker, kIdRouteDone);
    return 1;
}
