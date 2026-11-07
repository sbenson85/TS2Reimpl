#include "simantics/PrimS.h"

#include "game/game.h"
#include "rz/Singleton_20ddfb.h"
#include "simantics/PrimSCommon.h"

using namespace primS;

// ---------------------------------------------------------------------------------------------------------
// 0x5a9f00
// ---------------------------------------------------------------------------------------------------------

int16_t __cdecl ResolveEntityId(int16_t id, uint8_t flag)
{
    void *service = GetCachedSingleton();
    int16_t result = 0;
    if (flag != 0) {
        void *checker = Call<void *>(service, 10);
        if (Call<uint32_t>(checker, 16, static_cast<int32_t>(id), 1u) != 0) result = id;
        return result;
    }
    void *table = Call<void *>(service, 26);
    void *object = Call<void *>(table, 29, static_cast<int32_t>(id));
    if (!object) Raise(0x17);
    Held held;
    constexpr uint32_t kInterface = 0x5a69d3d5;
    Call<uint32_t>(object, 0, kInterface, &held.mRef.mObject);
    if (held.mRef.mObject) result = Call<int16_t>(held.mRef.mObject, 57);
    return result;
}

// ---------------------------------------------------------------------------------------------------------
// Opcode 0x7c
// ---------------------------------------------------------------------------------------------------------

uint32_t PrimSObject::Prim7C(void * /*frame*/, const PrimParamView *param)
{
    const uint8_t flags = Byte(param, 0x11);
    const bool flagA = (flags & 1) != 0;
    const bool flagB = (flags & 2) != 0;

    const int16_t person = Read(this, Byte(param, 4), Word(param, 5));
    const int16_t entity = ResolveEntityId(person, flagA);
    if (entity == 0) Raise(0x1c);

    const uint32_t key = Dword(param, 7);
    void *registry = Call<void *>(mContext, 4);
    void *handler = Call<void *>(registry, 23, key);
    if (!handler) Raise(7);

    const int16_t second = Read(this, Byte(param, 0xb), Word(param, 0xc));
    const int16_t third = Read(this, Byte(param, 0xe), Word(param, 0xf));

    // The request: {kind, value, extra word}. 0x54e500 zeroes both dwords and the word.
    struct Request {
        uint32_t mKind;
        uint32_t mValue;
        uint16_t mExtra;
    } request = {0, 0, 0};
    uint16_t extraId = 0; // the last argument of the call below
    request.mKind = Call<uint32_t>(handler, 16);

    switch (request.mKind) {
    case 0:
        request.mExtra = static_cast<uint16_t>(second);
        break;
    case 1: {
        const int16_t other = ResolveEntityId(second, flagB);
        if (other == 0) Raise(0x1c);
        request.mValue = static_cast<uint16_t>(other); // only the low word is stored
        request.mExtra = static_cast<uint16_t>(third);
        break;
    }
    case 3:
        request.mKind = 2;
        [[fallthrough]];
    case 2:
        if (second != 0) {
            void *table = Call<void *>(mContext, 26);
            void *object = Call<void *>(table, 29, static_cast<int32_t>(second));
            if (object) {
                void *entry = Call<void *>(object, 61);
                if (entry) {
                    if (Call<void *>(entry, 27) != nullptr) entry = Call<void *>(entry, 27);
                    request.mValue = Call<uint32_t>(entry, 28);
                }
            }
            if (request.mValue == 0) Raise(0x17);
        } else {
            request.mValue = 0;
        }
        request.mExtra = static_cast<uint16_t>(third);
        extraId = static_cast<uint16_t>(second);
        break;
    case 4:
        request.mValue = static_cast<uint16_t>(second); // only the low word is stored
        request.mExtra = static_cast<uint16_t>(third);
        break;
    case 5:
    case 6: {
        void *table = Call<void *>(mContext, 26);
        void *object = Call<void *>(table, 29, static_cast<int32_t>(second));
        if (!object) Raise(0x17);
        void *entry = Call<void *>(object, 61);
        // ORIGINAL-BUG: the entry is used without a null check (kinds 5 and 6); an object without one crashes.
        request.mValue = Call<uint32_t>(entry, 28);
        request.mExtra = static_cast<uint16_t>(third);
        break;
    }
    default:
        break;
    }

    const uint8_t answer = Call<uint8_t>(registry, 34, static_cast<uint32_t>(static_cast<uint16_t>(entity)), key, &request,
                                         static_cast<uint32_t>(extraId));
    return answer != 0 ? 1u : 0u;
}

// ---------------------------------------------------------------------------------------------------------
// Opcode 0x31
// ---------------------------------------------------------------------------------------------------------

uint32_t PrimSObject::Prim31(void *frame, const PrimParamView * /*param*/)
{
    void *table = Call<void *>(mContext, 26);
    const int16_t id = Call<int16_t>(frame, 12);
    void *person = Call<void *>(table, 29, static_cast<int32_t>(id));
    if (!person) Raise(0x15);

    // Declared in the original's order, so the references are released in its order (last taken first).
    Held a, b, c, d, e;
    constexpr uint32_t kInterface = 0x99c7f5a;
    Call<uint32_t>(person, 0, kInterface, &a.mRef.mObject);

    // The interface is used without a check: a failed query is a crash (a null object plus 4).
    // ORIGINAL-BUG: the result of the interface query is not checked; without the interface the next call goes through a
    // bogus subobject at address 4.
    void *sub = static_cast<uint8_t *>(a.mRef.mObject) + 4;
    b.mRef.mObject = AddRefed(Call<void *>(sub, 21));
    if (b.mRef.mObject) {
        c.mRef.mObject = AddRefed(Call<void *>(b.mRef.mObject, 20));
        if (!c.mRef.mObject) Raise(0x44e);
        const uint16_t first = Call<uint16_t>(b.mRef.mObject, 9);
        d.mRef.mObject = AddRefed(Call<void *>(c.mRef.mObject, 23, static_cast<uint32_t>(first)));
        if (!d.mRef.mObject) Raise(0x44f);
        const uint16_t second = Call<uint16_t>(b.mRef.mObject, 14);
        e.mRef.mObject = AddRefed(Call<void *>(d.mRef.mObject, 22, static_cast<uint32_t>(second)));
        if (e.mRef.mObject) {
            if (Call<int16_t>(e.mRef.mObject, 3) == 0) {
                Call<void>(b.mRef.mObject, 32, 0, 0);
                // Slot 47 is a getter (cTSObject::vf47, no arguments); slot 71 takes the value and a 0, which the original
                // pushes before calling slot 47.
                const int16_t value = Call<int16_t>(person, 47);
                Call<void>(table, 71, static_cast<int32_t>(value), 0);
            } else if (Call<int16_t>(e.mRef.mObject, 3) == 0x11) {
                // The mode is asked a second time.
                Call<void>(b.mRef.mObject, 32, 0, 0);
            }
        }
    }
    return 1;
}

