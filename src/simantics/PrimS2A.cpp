#include "rz/RZMessageServer.h"
#include "simantics/SimGeometry.h"
#include "simantics/PrimS.h"
#include "game/vtable.h"
#include "ifaces/cTSObject.h"

#include "game/game.h"
#include "game/original.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/RZMemory.h"
#include "rz/Singleton_20ddfb.h"
#include "simantics/PrimSCommon.h"

using namespace primS;

// Opcode 0x2a, CreateObject (0x585a60). Everything is reached through vtable slots; the position helpers and the
// placement checks are original code (called through OriginalFunction, declared in the status entry).

namespace {

constexpr uint32_t kUiInterface = 0x5a69d3d5;
constexpr uint32_t kBindInterface = 0x99c7f5a;

// A position: 16.16 fixed-point x and y, as the secondary subobject's slot 33 returns it.
struct Position {
    int32_t mX;
    int32_t mY;
};

// A 12-byte float position (0x7dcc50's result): x, y, z.
struct FloatPosition {
    float mX, mY, mZ;
};

// The three signed tile coordinates the placement service takes (0x7dcb60's result).
struct TilePosition {
    int32_t mX, mY, mZ;
};

using OffsetFn = int32_t *(__thiscall *)(void *out, int32_t direction);        // 0x45c160, ret 4
using RotateFn = void(__cdecl *)(int32_t octant, int32_t *first, int32_t *second); // 0x5795f0
using ServiceFn = void *(__cdecl *)();                                           // 0x20e0b2, 0x9c5a89

} // namespace

uint32_t PrimSObject::Prim2A(void *frame, const PrimParamView *param)
{
    void *registry = Call<void *>(mContext, 10);
    void *table = Call<void *>(mContext, 26);
    const uint8_t flags = Byte(param, 9);
    void *secondary = &mSecondary;

    // ---- the object definition ------------------------------------------------------------------------------------
    void *definition;
    if (flags & 4) {
        const int16_t id = Call<int16_t>(frame, 12);
        void *found = Call<void *>(registry, 16, static_cast<int32_t>(id), 0);
        if (!found) return 0;
        definition = Call<void *>(found, 10);
        void *cache = *GameAddress<void *>(0x10cc8ec);
        if (!cache) {
            // The cached object is acquired from the service (0x57c370: the holder starts empty, then the service
            // is asked through slot 5), stored, and the holder released (0x1466e0).
            RZAutoRef holder{nullptr};
            void *service = GetServiceObject();
            if (service) Call<uint32_t>(service, 5, 0x4cf22ab5u, 0x2cf22a9bu, &holder.mObject);
            *GameAddress<void *>(0x10cc8ec) = holder.mObject;
            holder.ReleaseObject();
            cache = *GameAddress<void *>(0x10cc8ec);
        }
        if (cache) {
            // Slot 15 takes no arguments; the 1 pushed before it is slot 9's second argument.
            const uint32_t value = Call<uint32_t>(found, 15);
            Call<void>(cache, 9, value, 1);
        }
    } else if (flags & 0x40) {
        void *list = Call<void *>(mContext, 24);
        void *holder = *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(this) + 0x1c0);
        const uint32_t key = Call<uint32_t>(holder, 6);
        void *entry = Call<void *>(list, 14, key);
        definition = Call<void *>(entry, 27);
    } else if (static_cast<int8_t>(flags) < 0) {
        void *list = Call<void *>(mContext, 24);
        const int32_t high = As<ifaces::cTSObject>(this)->GetArgument(1);
        const uint32_t low = static_cast<uint16_t>(As<ifaces::cTSObject>(this)->GetArgument(0));
        definition = Call<void *>(list, 14, low | (static_cast<uint32_t>(high) << 16));
    } else {
        void *list = Call<void *>(mContext, 24);
        definition = Call<void *>(list, 14, Dword(param, 4));
    }
    if (!definition) return 0;

    if (Call<void *>(definition, 4) != nullptr) {
        if (Call<int32_t>(Call<void *>(definition, 4), 3, 9) == 0x10) Raise(0x90);
        if (Call<int32_t>(Call<void *>(definition, 4), 3, 9) == 0x11) return 0;
    }

    // ---- where it goes --------------------------------------------------------------------------------------------
    int32_t z = Call<int16_t>(this, 36);
    Position pos = {0, 0};
    void *source = nullptr;
    uint32_t dword = 0;
    bool place = true;
    uint32_t extra = 0xffffffffu;
    int32_t direction = -1;

    auto toPosition = [&](const Position *p) {
        pos.mX = p->mX;
        pos.mY = p->mY;
    };
    auto frameObject = [&]() -> void * {
        const int16_t id = Call<int16_t>(frame, 12);
        return Call<void *>(table, 29, static_cast<int32_t>(id));
    };

    switch (static_cast<int8_t>(Byte(param, 8))) {
    case 0: {
        const Position *p = Call<const Position *>(this, 33);
        toPosition(p);
        const int16_t facing = Call<int16_t>(this, 13, 1);
        int32_t offset[2];
        const int32_t *o = OriginalFunction<OffsetFn, 0x45c160>()(offset, static_cast<int32_t>(facing));
        pos.mY += o[1];
        pos.mX += o[0];
        break;
    }
    case 1: {
        const Position *p = Call<const Position *>(this, 33);
        source = this;
        toPosition(p);
        break;
    }
    case 2: {
        const Position *p = Call<const Position *>(this, 33);
        source = this;
        dword = 0;
        toPosition(p);
        break;
    }
    case 3: {
        void *target = frameObject();
        if (!target) return 0;
        z = Call<int16_t>(target, 36);
        const Position *p = Call<const Position *>(target, 33);
        toPosition(p);
        const int16_t facing = Call<int16_t>(target, 13, 1);
        int32_t offset[2];
        const int32_t *o = OriginalFunction<OffsetFn, 0x45c160>()(offset, static_cast<int32_t>(facing));
        pos.mY += o[1];
        pos.mX += o[0];
        extra = Call<uint16_t>(target, 131);
        break;
    }
    case 4:
    case 10: {
        void *target;
        if (static_cast<int8_t>(Byte(param, 8)) == 4) {
            target = frameObject();
        } else {
            const int16_t id = As<ifaces::cTSObject>(this)->GetArgument(0);
            target = Call<void *>(table, 29, static_cast<int32_t>(id));
        }
        source = target;
        if (!target) return 0;
        z = Call<int16_t>(target, 36);
        Call<void>(target, 34, static_cast<void *>(&pos));
        dword = Byte(param, 0xd);
        extra = Call<uint16_t>(target, 131);
        break;
    }
    case 5: {
        const Position *p = Call<const Position *>(this, 33);
        toPosition(p);
        break;
    }
    case 6:
        z = 1;
        pos.mY = static_cast<int32_t>(0xffff0000u);
        pos.mX = static_cast<int32_t>(0xffff0000u);
        place = false;
        break;
    case 7: {
        if (!Call<uint8_t>(frame, 26)) Raise(8);
        const int16_t id = Call<int16_t>(frame, 30, 0);
        void *target = Call<void *>(table, 29, static_cast<int32_t>(id));
        if (!target) return 0;
        z = Call<int16_t>(target, 36);
        Call<void>(target, 34, static_cast<void *>(&pos));
        break;
    }
    case 8: {
        const int8_t index = static_cast<int8_t>(Byte(param, 0xa));
        if (index < 0) Raise(8);
        const uint32_t count = Call<uint8_t>(frame, 27);
        if (index >= static_cast<int32_t>(count)) Raise(8);
        const int16_t id = Call<int16_t>(frame, 31, static_cast<int32_t>(index));
        void *target = Call<void *>(table, 29, static_cast<int32_t>(id));
        if (!target) return 0;
        z = Call<int16_t>(target, 36);
        Call<void>(target, 34, static_cast<void *>(&pos));
        break;
    }
    case 9: {
        const int8_t slot = static_cast<int8_t>(Byte(param, 0xa));
        const int16_t read = Call<int16_t>(secondary, 7, 0x19u, static_cast<uint32_t>(static_cast<uint16_t>(static_cast<int16_t>(slot))), 0u, 0u, 0u,
                                           0u, 0u);
        int32_t first = 0, second = -1;
        const int32_t facing = Call<int16_t>(this, 13, 1);
        const int32_t octant = (facing + read) % 8;
        OriginalFunction<RotateFn, 0x5795f0>()(octant, &first, &second);
        const Position *p = Call<const Position *>(this, 33);
        pos.mY = p->mY + static_cast<int32_t>(static_cast<uint32_t>(first) << 16);
        pos.mX = p->mX + static_cast<int32_t>(static_cast<uint32_t>(second) << 16);
        direction = (Call<int16_t>(this, 13, 1) + 4 + read) % 8;
        break;
    }
    default:
        break;
    }

    // ---- is the spot free -----------------------------------------------------------------------------------------
    uint8_t tile[3];
    tile[0] = static_cast<uint8_t>(pos.mY >> 16);
    tile[1] = static_cast<uint8_t>(pos.mX >> 16);
    tile[2] = static_cast<uint8_t>(z);
    TilePosition tilePos;
    simgeom::TileFromBytes(reinterpret_cast<int32_t *>(&tilePos), tile);
    void *placement = OriginalFunction<ServiceFn, 0x20e0b2>()();
    void *service = placement;
    uint32_t leftover = 0; // the dword slot the flag below is later merged into (the loop's count, else stack garbage)

    if (flags & 1) {
        const int32_t count = Call<int32_t>(service, 65, static_cast<void *>(&tilePos));
        leftover = static_cast<uint32_t>(count);
        for (int32_t i = 0; i < count; ++i) {
            void *other = OriginalFunction<void *(__cdecl *)(int32_t, TilePosition *, int32_t), 0x7e3690>()(i, &tilePos, 1);
            if (other != nullptr && Call<void *>(other, 61) == definition) return 0;
        }
    }
    if (flags & 8) {
        if (Call<int32_t>(service, 65, static_cast<void *>(&tilePos)) > 0) return 0;
    }

    FloatPosition floatPos;
    if (place) {
        if (!Call<uint8_t>(service, 71, static_cast<void *>(&tilePos))) return 0;
        simgeom::PositionToFloats(reinterpret_cast<float *>(&floatPos), reinterpret_cast<const int32_t *>(&pos), z);
        if (static_cast<uint16_t>(extra) != 0xffff) {
            void *verifier = Call<void *>(service, 39);
            const uint16_t seen = Call<uint16_t>(verifier, 80, static_cast<void *>(&floatPos), 2, 1);
            if (seen != static_cast<uint16_t>(extra)) return 0;
        }
    } else {
        simgeom::PositionToFloats(reinterpret_cast<float *>(&floatPos), reinterpret_cast<const int32_t *>(&pos), z);
    }

    if (direction != -1) {
        if (OriginalFunction<uint8_t(__cdecl *)(uint8_t *, int32_t), 0x546af0>()(tile, direction) & 1) return 0;
    }

    // ---- make it --------------------------------------------------------------------------------------------------
    void *maker = Call<void *>(this, 63);
    leftover = (leftover & ~0xffu) | (Byte(param, 0xe) & 1u);
    const uint16_t newId = Call<uint16_t>(maker, 16, definition, leftover, 0, 0);
    if (newId == 0) return 0;
    void *object = Call<void *>(table, 29, static_cast<int32_t>(static_cast<int16_t>(newId)));

    if (flags & 0x20) {
        void *reference = frameObject();
        if (reference) {
            const uint16_t wanted = Call<uint16_t>(reference, 13, 1);
            if (Call<uint16_t>(object, 13, 1) != wanted) {
                uint32_t facing = static_cast<uint32_t>(simgeom::FacingToNotchOffset(static_cast<int32_t>(static_cast<int16_t>(wanted))));
                Call<void>(object, 170, static_cast<void *>(&facing), 1);
            }
        }
    }

    if (place) {
        if (As<ifaces::cTSObject>(object)->ForwardsPrimitives()) {
            // A holder is cleared and released here in the original; it was empty.
        }
        simgeom::PositionToFloats(reinterpret_cast<float *>(&floatPos), reinterpret_cast<const int32_t *>(&pos), z);
        bool placed;
        if (source != nullptr) {
            placed = OriginalFunction<uint8_t(__cdecl *)(void *, void *, uint32_t, uint32_t), 0x7e8aa0>()(object, source, dword, 0) != 0;
        } else {
            placed = OriginalFunction<uint8_t(__cdecl *)(void *, void *, uint32_t, uint32_t, uint32_t, uint32_t), 0x7e8c10>()(object, &floatPos, 0, 0, 0, 0) != 0;
        }
        if (!placed) {
            void *remover = Call<void *>(this, 63);
            Call<void>(remover, 17, static_cast<uint32_t>(newId));
            return 0;
        }
        if (source != nullptr) Call<void>(object, 174, source, dword, 1);
        else Call<void>(object, 171, static_cast<void *>(&floatPos), 0, 1);
        if (Call<uint8_t>(object, 116)) {
            OriginalFunction<void(__stdcall *)(void *, void *, uint32_t), 0x5416a0>()(object, &floatPos, 1);
        }
        if (As<ifaces::cTSObject>(object)->ObjectKind() == 0x13) {
            // A message announcing the new object goes to the cached dispatcher.
            void *memory = RZMemory::Malloc(0x2c);
            void *message = memory ? static_cast<cRZMessage2Standard *>(memory)->Construct() : nullptr;
            Held held;
            held.mRef.mObject = AddRefed(message);
            Call<void>(held.mRef.mObject, 5, 0xaf6129b0u);
            Call<void>(held.mRef.mObject, 12, object);
            void *dispatcher = GetCachedMessageServer();
            Call<void>(dispatcher, 3, held.mRef.mObject);
        }
    }

    // ---- bind it ----------------------------------------------------------------------------------------------------
    Held bound;
    Call<uint32_t>(object, 0, kBindInterface, &bound.mRef.mObject);
    if (flags & 4) {
        Held ignored;
        Call<uint32_t>(object, 0, kUiInterface, &ignored.mRef.mObject);
    }
    if (flags & 2) {
        if (flags & 0x10) Raise(0x36);
        void *sub = static_cast<uint8_t *>(bound.mRef.mObject) + 4;
        if (Call<int16_t>(sub, 23) > 0) {
            void *target = Call<void *>(sub, 22, 0);
            if (target) {
                const uint16_t who = Call<uint16_t>(this, 47);
                Call<void>(target, 32, 0, static_cast<uint32_t>(who));
                const uint16_t from = Call<uint16_t>(frame, 12);
                Call<void>(target, 13, static_cast<uint32_t>(from));
            }
        }
    } else if (flags & 0x10) {
        void *sub = static_cast<uint8_t *>(bound.mRef.mObject) + 4;
        if (Call<int16_t>(sub, 23) > 0) {
            void *target = Call<void *>(sub, 22, 0);
            if (target) {
                const uint16_t who = static_cast<uint16_t>(As<ifaces::cTSObject>(this)->GetArgument(0));
                Call<void>(target, 32, 0, static_cast<uint32_t>(who));
            }
        }
    }
    if (Byte(param, 0xe) & 2) {
        const int16_t slot = As<ifaces::cTSObject>(this)->GetArgument(5);
        void *linked = Call<void *>(table, 29, static_cast<int32_t>(slot));
        if (!linked) Raise(0xa);
        Call<void>(bound.mRef.mObject, 294, linked);
    }
    Call<void>(frame, 13, static_cast<uint32_t>(newId));
    return 1;
}
