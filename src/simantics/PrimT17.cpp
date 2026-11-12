#include "rz/RZMessageServer.h"
#include "simantics/SimGeometry.h"
#include "game/game.h"
#include "game/original.h"
#include "world/WorldTileMath.h"
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"
#include "sg/SGUtilities.h"
#include "simantics/PrimTCommon.h"

#include <cstring>
#include <xmmintrin.h>

using namespace primT;

namespace {

struct Vec3 {
    float x, y, z;
};
static_assert(sizeof(Vec3) == 12, "Vec3 layout");

// What a world-transform object (slot 54) writes: a position and a rotation quaternion.
struct Placement {
    Vec3 mPosition;
    float mRotation[4];
};
static_assert(sizeof(Placement) == 28, "Placement layout");

constexpr uint32_t kInterfaceNamed = 0x5a69d3d5;

// The cached message server (0x9c5a89, __cdecl).
inline void *MessageServer()
{
    return GetCachedMessageServer();
}

// A cRZString local, destroyed when the handler leaves.
struct Name {
    cRZString mString;
    Name() { mString.Construct(); }
    Name(const Name &) = delete;
    Name &operator=(const Name &) = delete;
    ~Name() noexcept { mString.Destruct(); }
};

// The float triple at a game global (three consecutive 32-bit values).
inline Vec3 GlobalVector(uint32_t rva)
{
    const float *f = GameAddress<float>(rva);
    return {f[0], f[1], f[2]};
}

} // namespace

uint32_t PrimTObject::Prim17(void *frame, const PrimParamView *param)
{
    int16_t soundIndex = static_cast<int16_t>(Word(param, 4));
    const uint8_t flags8 = Byte(param, 8);
    const bool hasPtr20 = ((flags8 >> 6) & 1) != 0; // 0x1c05e0
    int16_t ownerWord;
    if (flags8 == 0xff) ownerWord = Call<int16_t>(this, 47);
    else if (((flags8 >> 1) & 1) != 0) ownerWord = Call<int16_t>(frame, 12); // 0x1c05a0
    else ownerWord = Call<int16_t>(this, 47);
    void *table = Call<void *>(mContext, 26);
    void *ownerObject = Call<void *>(table, 29, static_cast<int32_t>(ownerWord));
    const bool verbose = Call<uint8_t>(mOwner, 39) != 0;

    Held lookup;
    void *framework = GetDirectorFrameworkObject();
    if (Call<uint8_t>(framework, 3, 0x1c03c383u, 0x1c03c382u, &lookup.mRef.mObject) == 0) return 1;

    // The sound number: negative means "read it from an operand"; 10000 and 20000 select the object kind.
    bool fromOperand = false;
    if (soundIndex < 0) {
        fromOperand = true;
        soundIndex = static_cast<int16_t>(-static_cast<int32_t>(soundIndex));
    }
    uint32_t kind = 0;
    if (soundIndex >= 20000) {
        void *owner = Call<void *>(frame, 20);
        kind = Call<uint32_t>(owner, 15);
        soundIndex = static_cast<int16_t>(soundIndex - 20000);
    } else if (soundIndex >= 10000) {
        void *owner = Call<void *>(frame, 20);
        kind = Call<uint32_t>(owner, 14);
        soundIndex = static_cast<int16_t>(soundIndex - 10000);
    } else {
        void *owner = Call<void *>(frame, 20);
        kind = Call<uint32_t>(owner, 16);
    }
    if (fromOperand) soundIndex = Read(this, Byte(param, 0xa), Word(param, 0xb));

    // (the original builds a log line " Sound: id %d, " here when the owner is verbose, and throws it away)
    void *lookupObject = lookup.mRef.mObject;
    if (Call<uint8_t>(lookupObject, 3, kind, static_cast<int32_t>(soundIndex)) == 0) return 1;

    Name name;
    Call<void>(lookupObject, 5, &name.mString);
    // (verbose: more of the discarded log line; the owner's slot 92 is asked for the source text)
    if (verbose && ownerObject != nullptr) (void)Call<const char *>(ownerObject, 92);
    _strlwr(const_cast<char *>(name.mString.mString.c_str())); // 0x1ad167

    if (OriginalFunction<void *(__cdecl *)(), 0x9c5b7e>()() == nullptr) return 1;

    Held message;
    void *framework2 = GetDirectorFrameworkObject();
    (void)Call<uint8_t>(framework2, 3, 0xcbc79579u, 0x0bc7957du, &message.mRef.mObject);
    if (message.mRef.mObject == nullptr) return 1;

    void *table2 = Call<void *>(mContext, 26);
    if (table2 != nullptr) {
        if (ownerWord == 0) ownerWord = Call<int16_t>(this, 47);
        if (ownerWord != 0) {
            void *objectX = Call<void *>(table2, 29, static_cast<int32_t>(ownerWord));
            if (objectX) Call<uint32_t>(objectX, 1); // AddRef
            Held objectHeld;
            objectHeld.mRef.mObject = objectX;
            if (objectX != nullptr) {
                void *relay = Call<void *>(message.mRef.mObject, 6);
                Call<void>(relay, 33, objectX);
                const uint16_t objectFlags = Call<uint16_t>(objectX, 13, 0x22u);
                if ((objectFlags & 2) != 0) return 1;
                if ((objectFlags & 0x10) != 0 && name.mString.StartsWith(GameAddress<const char>(0xda8854), 3, true)) return 1;

                Call<void>(message.mRef.mObject, 12, 0xc683d79u, static_cast<int32_t>(Call<int16_t>(objectX, 131)));
                Call<void>(message.mRef.mObject, 15, 0xc683d7du, static_cast<int32_t>(Call<int16_t>(objectX, 36)) - 1);

                // ORIGINAL-BUG: when the interface exists but the query on slot 12 fails, the position and direction
                // are sent uninitialised (stack garbage); ours sends zeros.
                Vec3 position = {0, 0, 0};
                Vec3 direction = {0, 0, 0};
                Held iface;
                if (Call<uint8_t>(objectX, 0, kInterfaceNamed, &iface.mRef.mObject) == 0) {
                    const int16_t level = Call<int16_t>(objectX, 36);
                    const int32_t *cell = Call<const int32_t *>(objectX, 33);
                    Vec3 made;
                    const Vec3 *converted = reinterpret_cast<Vec3 *>(worldmath::FixedToPosition(reinterpret_cast<float *>(&made), cell, level)); // 0x7dcc00
                    position = *converted;
                    const int32_t turns = Call<int32_t>(objectX, 43);
                    const float angle = static_cast<float>(turns) * *GameAddress<float>(0xda77d8) * *GameAddress<float>(0xda03e0);
                    direction.x = -simgeom::SineOfFloat(angle);
                    direction.y = simgeom::CosineOfFloat(angle);
                    direction.z = 0.0f;
                } else {
                    if (Call<uint8_t>(objectX, 188) == 0) return 1;
                    Call<void>(message.mRef.mObject, 12, 0xabd5ce41u, static_cast<int32_t>(Call<int16_t>(iface.mRef.mObject, 10, 0x3au, 1u)));
                    Call<void>(message.mRef.mObject, 12, 0x8bd5ce44u, static_cast<int32_t>(Call<int16_t>(iface.mRef.mObject, 10, 0x41u, 1u)));
                    Call<void>(message.mRef.mObject, 12, 0x2bd5ce4du, static_cast<int32_t>(Call<int16_t>(iface.mRef.mObject, 10, 0x58u, 1u)));
                    {
                        HeldVirtualBase skeleton;
                        skeleton.mRef.ResetVirtualBase();
                        if (Call<uint8_t>(objectX, 12, 0xfa1c39f7u, 0xfa1c39edu, &skeleton.mRef) != 0) {
                            Vec3 axis = GlobalVector(0x10dbdd8);
                            void *node = nGZSceneGraph::FindNodeByName( // 0xae7490
                                "root_rot", static_cast<nGZSceneGraph::cICompositionTreeNode *>(skeleton.mRef.mObject), 0xf);
                            if (node == nullptr) {
                                node = skeleton.mRef.mObject;
                                axis = GlobalVector(0x10dbdcc);
                            }
                            Placement placement;
                            Call<void>(node, 54, &placement);
                            position = placement.mPosition;
                            simgeom::RotateByQuaternion(reinterpret_cast<float *>(&axis), placement.mRotation);
                            direction = axis;
                        }
                        if (Call<int16_t>(iface.mRef.mObject, 10, 0x40u, 1u) != 0) {
                            // max(position.z, 0): a NaN or negative value gives 0
                            if (_mm_comigt_ss(_mm_set_ss(position.z), _mm_setzero_ps()) == 0) position.z = 0.0f;
                        }
                    }
                }
                Call<void>(message.mRef.mObject, 18, 0xabcb73f3u, &position);
                Call<void>(message.mRef.mObject, 18, 0xcbcb73f8u, &direction);
            }
        }
    }

    Call<void>(message.mRef.mObject, 21, 0x2bcb73fcu, &name.mString);
    Call<void>(message.mRef.mObject, 12, 0xabd5d0a2u, static_cast<int32_t>(ownerWord));
    Call<void>(message.mRef.mObject, 5, hasPtr20 ? 0x2bc78968u : 0xcbc78960u);
    Held reply;
    (void)Call<uint8_t>(message.mRef.mObject, 0, 0xa52294b4u, &reply.mRef.mObject);
    Call<void>(reply.mRef.mObject, 5, 0xcc1533b9u);
    void *server = MessageServer();
    Call<void>(server, 3, reply.mRef.mObject);
    return 1;
}
