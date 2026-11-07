#include "simantics/PrimS.h"
#include "game/vtable.h"
#include "ifaces/cTSObject.h"

#include "game/original.h"
#include "simantics/PrimSCommon.h"

using namespace primS;

// ---------------------------------------------------------------------------------------------------------
// Opcode 0x77, Message (0x59de90)
// ---------------------------------------------------------------------------------------------------------

namespace {

// 0x57ea20 (__cdecl, four stack arguments, the answer in al): whether an object matches the message's filter.
// Not replaced yet. `kind` is the param byte +4: 0 compares the object's slot 131 with `word`, 1 its slot 36 with
// `value`, 3 and 4 compare positions through the cached services, anything else matches.
using MatchFn = uint8_t(__cdecl *)(void *object, uint32_t kind, uint32_t word, int32_t value);

} // namespace

uint32_t PrimSObject::Prim77(void *frame, const PrimParamView *param)
{
    const uint32_t mode = Byte(param, 7);
    const uint32_t kind = Byte(param, 4);

    // The message: six 16-bit words, built in the frame in this order.
    uint16_t message[6];
    message[0] = static_cast<uint16_t>(Read(this, Byte(param, 0x13), Word(param, 5)));
    message[1] = static_cast<uint16_t>(Call<int16_t>(frame, 12));
    message[2] = Byte(param, 0xc);
    message[3] = static_cast<uint16_t>(Read(this, Byte(param, 0xd), Word(param, 0xe)));
    message[4] = static_cast<uint16_t>(Read(this, Byte(param, 0x10), Word(param, 0x11)));
    message[5] = static_cast<uint16_t>(As<ifaces::cTSObject>(this)->GetArgument(0));

    void *table = Call<void *>(mContext, 26);

    // Param byte +8: bit 1 delivers to the one object operand 4 names.
    if ((Byte(param, 8) >> 1) & 1) {
        const int16_t id = Read(this, Byte(param, 9), Word(param, 0xa));
        void *target = Call<void *>(table, 29, static_cast<int32_t>(id));
        if (!target) Raise(0x17);
        Call<void>(this, 317, target, static_cast<void *>(message));
        return 1;
    }

    const int32_t first = Call<int16_t>(this, 36);
    uint16_t second = static_cast<uint16_t>(Call<int16_t>(this, 131));
    if (Byte(param, 8) & 1) second = static_cast<uint16_t>(Read(this, Byte(param, 9), Word(param, 0xa)));

    uint32_t delivered = 0;
    const MatchFn match = OriginalFunction<MatchFn, 0x57ea20>();

    if (mode != 6 && mode != 7) {
        // A vector of object pointers: begin/end are read again on every pass.
        void *list = Call<void *>(this, 63);
        void *const *container = Call<void *const *>(list, 45);
        void *const *entry = static_cast<void *const *>(container[0]);
        while (entry != static_cast<void *const *>(container[1])) {
            // The element is read from the vector again at every use, as the original does.
            bool take = false;
            if (mode == 5) {
                take = true;
            } else {
                const int32_t state = Call<int16_t>(*entry, 10, 0x20, 1);
                if (state == 0 && (mode == 0 || mode == 1)) take = true;
                else if (state == 2 && (mode == 2 || mode == 4)) take = true;
                else if (mode == 1 || mode == 3) take = true;
            }
            if (take) {
                // The slot takes no arguments: the three pushed values stay on the stack for the match call.
                void *other = Call<void *>(*entry, 8);
                if (match(other, kind, second, first) != 0) {
                    void *again = Call<void *>(*entry, 8);
                    Call<void>(this, 317, again, static_cast<void *>(message));
                    delivered = 1;
                }
            }
            ++entry;
        }
    }

    if (mode == 6 || mode == 7) {
        // A std::list of object pointers: the list object's first field is the sentinel node, whose next pointer
        // starts the walk. A node is {next, prev, value}; the end is the sentinel again, read on every pass.
        uint8_t *holder = *reinterpret_cast<uint8_t **>(reinterpret_cast<uint8_t *>(this) + 0x1c4);
        void *list = Call<void *>(holder, 31);
        uint8_t *const *head = static_cast<uint8_t *const *>(list);
        uint8_t *node = *reinterpret_cast<uint8_t **>(*head);
        while (node != *head) {
            void *element = *reinterpret_cast<void **>(node + 8);
            Held held;
            Call<uint32_t>(element, 0, 0x5a69d3d5u, &held.mRef.mObject);
            if ((held.mRef.mObject == nullptr || mode == 7) && match(*reinterpret_cast<void **>(node + 8), kind, second, first) != 0) {
                Call<void>(this, 317, *reinterpret_cast<void **>(node + 8), static_cast<void *>(message));
                delivered = 1;
            }
            node = *reinterpret_cast<uint8_t **>(node);
        }
    }
    return delivered;
}
