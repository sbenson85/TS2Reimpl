#include "simantics/SnapReport.h"
#include "simantics/SimGeometry.h"
#include "game/game.h"
#include "game/vtable.h"
#include "ifaces/cTSObject.h"
#include "game/original.h"
#include "rz/RZAutoRef.h"
#include "simantics/PrimXCommon.h"

#include <emmintrin.h>
#include <xmmintrin.h>

using namespace primX;

// SimAntics opcode 0x2e, Snap (0x5a5210).
//
// Moves (snaps) the object onto a target: a place beside, on a slot of, or on an object in the stack object's lot
// position. The target is worked out as a fixed-point tile position (16.16 x/y), a level, an optional object, a slot
// index and a rotation, then the worker 0x5a6780 does the placing and reports through a "snap state" (0x20 bytes of
// containers, 0x7f02f0 / 0x7f04e0) and an error report. The error branch only builds debug text (dropped) and stores
// the error code into the object's data word 0x34 (slot 14).

namespace {

struct Vec3 {
    float x, y, z;
};
struct Quat {
    float x, y, z, w;
};
// A fixed-point position: two 16.16 coordinates (x at +0, y at +4).
struct Pair {
    int32_t x, y;
};

// The snap state (0x7f02f0 constructs it, 0x7f04e0 destroys it): four containers, 0x20 bytes.
struct SnapState {
    alignas(4) uint8_t mBytes[0x20];
    SnapState() { OriginalFunction<void *(__thiscall *)(void *), 0x7f02f0>()(mBytes); }
    SnapState(const SnapState &) = delete;
    SnapState &operator=(const SnapState &) = delete;
    ~SnapState() { OriginalFunction<void(__thiscall *)(void *), 0x7f04e0>()(mBytes); }
};

// The error report the state builds (0x7f0ca0): the code at +0, a reference at +0x10 (released at the end), kind at +0x14.
struct SnapReport {
    alignas(4) uint8_t mBytes[0x34];
    SnapReport() { std::memset(mBytes, 0, sizeof(mBytes)); }
    SnapReport(const SnapReport &) = delete;
    SnapReport &operator=(const SnapReport &) = delete;
    ~SnapReport() noexcept { reinterpret_cast<RZRefField<0x10> *>(mBytes)->ReleaseRef(); } // 0x416c90
    int32_t Code() const { return *reinterpret_cast<const int32_t *>(mBytes); }                 // 0x2ecf00
    void *Reference() const { return *reinterpret_cast<void *const *>(mBytes + 0x10); }          // 0x149cf0
    // 0x7f0c00 (__thiscall, ret 4): looks up the error's message string; false when there is none.
    bool Message(void **out)
    {
        return reinterpret_cast<::SnapErrorReport *>(mBytes)->LookupMessage(out) != 0;
    }
};

// 0x45c160 (__thiscall on the output pair, ret 4): the unit tile offset of a rotation notch (low 3 bits).
Pair NotchOffset(uint32_t notch)
{
    static const int8_t kX[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
    static const int8_t kY[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    return {kX[notch & 7] * 65536, kY[notch & 7] * 65536};
}

// 0x57ce60: pair += other.
inline void AddPair(Pair &p, const Pair &o)
{
    p.y += o.y;
    p.x += o.x;
}

// 0x57c660 (the pair from two floats): each coordinate times 65536 (game global 0xdb1df4), truncated. The first
// argument is y, the second x.
inline Pair FromFloats(float y, float x)
{
    const float scale = 65536.0f;
    Pair r;
    r.y = _mm_cvttss_si32(_mm_set_ss(y * scale));
    r.x = _mm_cvttss_si32(_mm_set_ss(x * scale));
    return r;
}

// 0x7dcc50 (__cdecl): the position as floats (x, y times 2^-16, global 0xdc87b8) and the level minus one stored as an
// integer in the third float slot.
inline Vec3 ToWorld(const Pair &p, int32_t level)
{
    const float scale = 1.52587890625e-05f;
    Vec3 r;
    r.x = static_cast<float>(p.x) * scale;
    r.y = static_cast<float>(p.y) * scale;
    const int32_t z = level - 1;
    std::memcpy(&r.z, &z, 4);
    return r;
}

// 0x7dd090 (__cdecl, pointer to an int): (8 - value) modulo 8, in 0..7.
inline int32_t Complement8(const int32_t *value)
{
    int32_t v = (8 - *value) & static_cast<int32_t>(0x80000007);
    if (v < 0) v = ((v - 1) | static_cast<int32_t>(0xfffffff8)) + 1;
    return v;
}

// The notch (rotation 0..7) reduced the way the original does after adding an offset: & 0x80000007, negative
// results brought back into 0..7.
inline int32_t ReduceNotch(int32_t v)
{
    v &= static_cast<int32_t>(0x80000007);
    if (v < 0) v = ((v - 1) | static_cast<int32_t>(0xfffffff8)) + 1;
    return v;
}

inline void CopyVec(Vec3 &to, const float *from)
{
    to.x = from[0];
    to.y = from[1];
    to.z = from[2];
}

// 0x7dccd0 (__cdecl): the rotation quaternion of a notch.
inline void NotchQuat(Quat *out, uint32_t notch)
{
    simgeom::NotchQuaternion(reinterpret_cast<float *>(out), static_cast<int32_t>(notch));
}

// 0x1a6c40 (__cdecl): rotates the vector by the quaternion in place.
inline void Rotate(Vec3 *v, const Quat *q)
{
    simgeom::RotateByQuaternion(reinterpret_cast<float *>(v), reinterpret_cast<const float *>(q));
}

// The worker 0x5a6780 (__thiscall on the object, 56 stack bytes).
using SnapWorker = uint32_t(__thiscall *)(void *, Vec3, Vec3, void *, int32_t, uint32_t, uint32_t, int32_t, uint32_t, void *, int16_t *);

} // namespace

uint32_t PrimXObject::Prim2E(void *frame, const PrimParamView *param)
{
    const uint8_t *p = reinterpret_cast<const uint8_t *>(param);

    // The stack object (the frame's id in the context's slot-26 list, slot 30).
    void *table = Call<void *>(mContext, 26);
    void *target = Call<void *>(table, 30, static_cast<int32_t>(static_cast<int16_t>(Call<uint32_t>(frame, 12))));
    if (!target) Raise(0x15);
    // Both object flags must be clear (slot 25 tests 0x10000 and 0x20000).
    const bool flagsClear = (Call<uint32_t>(this, 25, 0x10000u) & 0xff) == 0 && (Call<uint32_t>(this, 25, 0x20000u) & 0xff) == 0;
    if (!flagsClear) Raise(0x17);

    Pair pos{0, 0};         // 0x57c690
    int32_t level = 0;      // v6c
    void *objectPtr = nullptr; // v70
    int32_t slot = 0;       // v20
    uint8_t byte22 = 0, byte21 = 0;
    int32_t rotation = 0;   // v38
    void *entry = nullptr;  // v18
    Vec3 offset{0, 0, 0};   // v68 (zeroed for the slot modes)
    const uint32_t thisFloat = Dword(this, 0x220);
    const int16_t mode = static_cast<int16_t>(Word(p, 6));

    auto takePosition = [&]() {
        const uint32_t *t = Call<const uint32_t *>(target, 33);
        pos.x = static_cast<int32_t>(t[0]);
        pos.y = static_cast<int32_t>(t[1]);
        level = static_cast<int16_t>(Call<uint32_t>(target, 36));
    };

    if (mode == 2) {
        // Beside the stack object: its tile plus the unit offset of its rotation notch.
        takePosition();
        objectPtr = nullptr;
        slot = 0;
        byte22 = 0;
        byte21 = 0;
        rotation = static_cast<int16_t>(Call<uint32_t>(this, 13, 1u));
        const Pair step = NotchOffset(static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(Call<uint32_t>(target, 13, 1u)))));
        AddPair(pos, step);
    } else if (mode == 1) {
        // On the stack object itself.
        takePosition();
        objectPtr = target;
        slot = 0;
        byte22 = 0;
        byte21 = 0;
        rotation = static_cast<int16_t>(Call<uint32_t>(this, 13, 1u));
    } else {
        const int16_t word4 = static_cast<int16_t>(Word(p, 4));
        auto checkSlot = [&]() {
            if (slot < 0 || !(slot < Call<int32_t>(target, 250))) Raise(0x1b);
        };
        if (mode == 0) {
            // A slot of the stack object by the node's slot number, translated through the frame's table.
            if (word4 < 0 || !(word4 < static_cast<int16_t>(Call<uint32_t>(frame, 26) & 0xff))) Raise(8);
            slot = static_cast<int16_t>(Call<uint32_t>(frame, 30, static_cast<int32_t>(word4)));
            checkSlot();
            entry = Call<void *>(target, 251, slot);
            {
                Vec3 buffer;
                CopyVec(offset, Call<const float *>(target, 76, &buffer, slot, thisFloat));
            }
        } else if (mode == 3) {
            // A slot by number: from the object's data word 0x96 (bit 3 of param word +8) or the node's word.
            slot = (static_cast<int16_t>(Word(p, 8)) >> 3 & 1) ? static_cast<int16_t>(static_cast<uint32_t>(As<ifaces::cTSObject>(this)->GetArgument(0u))) : static_cast<int16_t>(Word(p, 4));
            checkSlot();
            entry = Call<void *>(target, 251, slot);
            {
                Vec3 buffer;
                CopyVec(offset, Call<const float *>(target, 76, &buffer, slot, thisFloat));
            }
        } else {
            if (mode != 4) Raise(0x24);
            slot = (static_cast<int16_t>(Word(p, 8)) >> 3 & 1) ? static_cast<int16_t>(static_cast<uint32_t>(As<ifaces::cTSObject>(this)->GetArgument(0u))) : static_cast<int16_t>(Word(p, 4));
            // An entry of the stack object's list (slot 63's slot-52 vector of object pointers).
            if (slot < 0) Raise(0x1b);
            const uint32_t *list = Call<const uint32_t *>(Call<void *>(target, 63), 52);
            const int32_t count = static_cast<int32_t>(list[1] - list[0]) >> 2;
            if (!(static_cast<uint32_t>(slot) < static_cast<uint32_t>(count))) Raise(0x1b);
            const uint32_t *list2 = Call<const uint32_t *>(Call<void *>(target, 63), 52);
            entry = *reinterpret_cast<void **>(list2[0] + static_cast<uint32_t>(slot) * 4);
            void *entryObject = Call<void *>(entry, 3);
            {
                Vec3 buffer;
                CopyVec(offset, simgeom::ReadObjectVector(reinterpret_cast<float *>(&buffer), entryObject));
            }
        }

        if ((Call<uint32_t>(entry, 57) & 0xff) != 0) {
            rotation = static_cast<int16_t>(Call<uint32_t>(target, 13, 1u));
            void *entryObject = Call<void *>(entry, 3);
            if ((Call<uint32_t>(entryObject, 8) & 0x20000) != 0) {
                void *other = Call<void *>(entry, 3);
                const int32_t key = Call<int32_t>(target, 84, other, 0u);
                rotation += static_cast<int16_t>(Complement8(&key));
            } else {
                rotation += Call<int32_t>(entry, 58);
            }
            rotation = ReduceNotch(rotation);
        } else {
            rotation = static_cast<int16_t>(Call<uint32_t>(this, 13, 1u));
        }

        if (Call<int32_t>(entry, 56) >= 0) {
            // The entry names a slot of the stack object: it must exist (error 0x20).
            if (Call<void *>(target, 68, Call<int32_t>(entry, 56)) == nullptr) Raise(0x20);
            takePosition();
            objectPtr = target;
            slot = Call<int32_t>(entry, 56);
            byte22 = static_cast<uint8_t>(Call<uint32_t>(entry, 17));
            byte21 = static_cast<uint8_t>(Call<uint32_t>(entry, 19));
        } else {
            // A free offset from the stack object's position, turned by its rotation.
            level = static_cast<int16_t>(Call<uint32_t>(target, 36));
            Vec3 where{0, 0, 0};
            Call<uint32_t>(target, 40, &where);
            Quat q;
            NotchQuat(&q, static_cast<uint16_t>(Call<uint32_t>(target, 13, 1u)));
            Rotate(&offset, &q);
            where.x = where.x + offset.x;
            where.y = where.y + offset.y;
            where.z = where.z + offset.z;
            pos = FromFloats(where.y, where.x);
            objectPtr = nullptr;
            slot = 0;
            byte22 = static_cast<uint8_t>(Call<uint32_t>(entry, 17));
            byte21 = static_cast<uint8_t>(Call<uint32_t>(entry, 19));
        }
    }

    Vec3 goal = ToWorld(pos, level);
    Vec3 here{0, 0, 0};
    Call<uint32_t>(this, 42, &here);

    if (Word(p, 0xe) == 1) {
        // Start from the stack object's position plus the rotated slot offset named by this object's data word.
        const int32_t index = static_cast<int16_t>(static_cast<uint32_t>(As<ifaces::cTSObject>(this)->GetArgument(1u)));
        Vec3 slotOffset{0, 0, 0};
        if (index < 0 || !(index < Call<int32_t>(target, 250))) Raise(0x1b);
        {
            Vec3 buffer;
            CopyVec(slotOffset, Call<const float *>(target, 76, &buffer, index, thisFloat));
        }
        Quat q;
        NotchQuat(&q, static_cast<uint16_t>(Call<uint32_t>(target, 13, 1u)));
        Rotate(&slotOffset, &q);
        Vec3 scratch;
        const float *at = Call<const float *>(target, 42, &scratch);
        here.x = at[0];
        here.y = at[1];
        here.z = at[2];
        here.x = here.x + slotOffset.x;
        here.y = here.y + slotOffset.y;
    }

    const uint32_t flag = (static_cast<int16_t>(Word(p, 8)) >> 4) & 1; // 0x57e810
    int16_t outcome = 0;
    SnapState state;
    const uint32_t result = OriginalFunction<SnapWorker, 0x5a6780>()(this, here, goal, objectPtr, slot, byte22, byte21, rotation, flag,
                                                                    state.mBytes, &outcome);
    HeldRef service;
    Call<uint32_t>(this, 0, 0x5a69d3d5u, &service.mObject);
    Call<uint32_t>(this, 14, 0x34u, 0u);
    if (result == 1) return result;

    void *context = mContext;
    const bool describe = context != nullptr && (Call<uint32_t>(context, 31) & 0xff) != 0;
    int16_t errorId = 0; // v4c
    Text text;
    if (outcome != 0) {
        Call<uint32_t>(this, 14, 0x34u, 2u);
        if (describe) text.mText.Sprintf("Snap location not in same room");
    } else {
        SnapReport report;
        reinterpret_cast<const ::SnapStateReports *>(state.mBytes)->BuildReport(reinterpret_cast<::SnapErrorReport *>(report.mBytes));
        const int32_t code = report.Code();
        void *failing = report.Reference();
        if (code == 0xc && failing != nullptr) {
            errorId = static_cast<int16_t>(Call<uint32_t>(failing, 47));
            if (Call<int32_t>(failing, 98) == 2 && ((Word(p, 8) >> 1) & 1) != 0) {
                uint32_t reach = 0; // ve0
                HeldRef a;           // v30: this object's interface
                if ((Call<uint32_t>(this, 0, 0x5a69d3d5u, &a.mObject) & 0xff) != 0) {
                    void *x = Call<void *>(a.mObject, 132);
                    reach = Call<uint32_t>(x, 22);
                }
                HeldRef b; // v2c: the failing object's interface
                b.Reset();
                if ((Call<uint32_t>(failing, 0, 0x5a69d3d5u, &b.mObject) & 0xff) != 0) {
                    void *sub = Call<void *>(b.mObject, 8);
                    struct VirtualBaseRef {
                        RZAutoRef mRef{nullptr};
                        ~VirtualBaseRef() noexcept { mRef.ReleaseVirtualBase(); }
                    } holder;
                    // 0x57cf20: clears the holder (nothing to release yet).
                    if ((Call<uint32_t>(sub, 9, 0xbc66baecu, 0x3c66baf1u, &holder.mRef.mObject) & 0xff) != 0) {
                        if ((Call<uint32_t>(holder.mRef.mObject, 16, reach) & 0xff) != 0) {
                            if (describe) text.mText.Sprintf("Asked person to move (id = %d)", static_cast<int32_t>(errorId));
                            Call<uint32_t>(this, 14, 0x34u, 1u);
                        }
                    }
                }
            }
            if (static_cast<int16_t>(Call<uint32_t>(this, 13, 0x34u)) == 0) {
                if (describe) {
                    HeldRef message;
                    void *found = nullptr;
                    if (report.Message(&message.mObject) && (found = message.mObject) != nullptr && Call<uint32_t>(found, 7) > 0) {
                        text.mText.Sprintf("%s (id = %d)", Call<const char *>(found, 5), static_cast<int32_t>(errorId));
                    } else {
                        text.mText.mString.append("unknown error msg -- associated string table entry not found");
                    }
                }
                Call<uint32_t>(this, 14, 0x34u, static_cast<uint32_t>(code + 2));
            }
        }
        if (static_cast<int16_t>(Call<uint32_t>(this, 13, 0x34u)) == 0) {
            if (describe) {
                HeldRef message;
                if (report.Message(&message.mObject) && message.mObject != nullptr && Call<uint32_t>(message.mObject, 7) > 0) {
                    text.mText.mString.assign(Call<const char *>(message.mObject, 5));
                } else {
                    text.mText.mString.append("unknown error msg -- associated string table entry not found");
                }
            }
            Call<uint32_t>(this, 14, 0x34u, static_cast<uint32_t>(code + 2));
        }
    }

    if (describe && !text.mText.mString.empty()) {
        // The report line is built and dropped.
        Text nameText, scratch;
        void *people = Call<void *>(mContext, 26);
        void *person = Call<void *>(people, 29, static_cast<int32_t>(static_cast<int16_t>(Call<uint32_t>(frame, 12))));
        if (person) {
            void *named = Call<void *>(person, 61);
            nameText.str().assign(Call<const char *>(named, 8));
            scratch.mText.Sprintf(", ID %d", static_cast<int32_t>(static_cast<int16_t>(Call<uint32_t>(person, 47))));
            nameText.AppendText(scratch);
            Text line;
            const int16_t value = static_cast<int16_t>(Call<uint32_t>(this, 13, 0x34u));
            line.mText.Sprintf("Snap failed with error code %d : %s\n", static_cast<int32_t>(value), text.c_str());
        }
    }
    return result;
}
