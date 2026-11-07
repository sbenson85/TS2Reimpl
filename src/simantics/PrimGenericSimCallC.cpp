#include "game/game.h"
#include "game/original.h"
#include "rz/ServiceGetters.h"
#include "simantics/PrimGenericSimCall.h"
#include "simantics/PrimGenericSimCallMessage.h"
#include "simantics/PrimXCommon.h"

using primX::Call;
using primX::HeldRef;
using gscmsg::MessageRef;
using gscmsg::MessageServer;

// GenericSimCall (opcode 0x01) cases, group C: the cases that post framework messages (cRZMessage2Standard, 0x2c bytes, built
// and posted to the message server 0x9c5a89) and the ones that call into cached services.

namespace {
constexpr uint32_t kDefaultResult = 1;

// The cached service behind the getter 0x259c20.
inline void *Service259C20()
{
    return CachedServiceGetter<0x10cc8ec, 0x4cf22ab5, 0x2cf22a9b>();
}
} // namespace

// Case 0: posts a message (id 0xcbea26d7) with flags 0x40000 (| 4 if the context's slot-23 object's slot 6 answers nonzero and
// then zero on a second ask), then either the thing the context's slot-8 object's slot 9 finds for argument 0 (slot 33) or
// the id 0xebf16aac (slot 5) as a fallback; posted with slot 4 of the server (message, 0).
uint32_t GenericSimCall::Case00()
{
    const uint32_t id = static_cast<uint16_t>(Arg(0));
    MessageRef msg;
    msg.SetId(0xcbea26d7u);
    uint32_t flags = 0x40000;
    void *const holder = ContextSlot(23);
    if (Call<uint32_t>(holder, 6) != 0) {
        if (Call<uint32_t>(holder, 6) == 0) flags |= 4;
    }
    Call<void>(msg.mMessage, 11, flags);
    void *const list = ContextSlot(8);
    void *const found = Call<void *>(list, 9, id);
    if (found) {
        Call<void>(msg.mMessage, 33, found);
    } else {
        msg.SetId(0xebf16aacu);
    }
    Call<void>(MessageServer(), 4, msg.mMessage, 0u);
    return kDefaultResult;
}

// Cases 28 and 72 post the same kind of message (id 0xd6): its value slots get the *addresses* of two locals (slot 12: a value
// the case computes, slot 17: the block slot 86 of the object found in the context's slot-26 list fills in), and it goes to the
// server with slot 3. With no such object nothing is posted.

// Case 28: value = argument 0 times argument 2 (a word, 1 when it is 0); the object is the one for argument 1.
uint32_t GenericSimCall::Case28()
{
    MessageRef msg;
    msg.SetId(0xd6);
    int32_t value = Arg(0);
    const int16_t objectId = Arg(1);
    int16_t scale = Arg(2);
    if (scale == 0) scale = 1;
    value = static_cast<int32_t>(scale) * value;
    void *const list = ContextSlot(26);
    void *const object = Call<void *>(list, 29, static_cast<int32_t>(objectId));
    if (object) {
        alignas(4) uint8_t block[64] = {}; // the callee fills a small block (its size is not known)
        Call<void>(object, 86, block);
        Call<void>(msg.mMessage, 12, &value);
        Call<void>(msg.mMessage, 17, block);
        Call<void>(MessageServer(), 3, msg.mMessage);
    }
    return kDefaultResult;
}

// Case 72: value = argument 0 * 10000 + argument 1 (both sign-extended); the object is the one for argument 2.
uint32_t GenericSimCall::Case72()
{
    MessageRef msg;
    msg.SetId(0xd6);
    const int32_t a0 = Arg(0);
    const int32_t a1 = Arg(1);
    const int16_t objectId = Arg(2);
    int32_t value = a0 * 0x2710 + a1;
    void *const list = ContextSlot(26);
    void *const object = Call<void *>(list, 29, static_cast<int32_t>(objectId));
    if (object) {
        alignas(4) uint8_t block[64] = {};
        Call<void>(object, 86, block);
        Call<void>(msg.mMessage, 12, &value);
        Call<void>(msg.mMessage, 17, block);
        Call<void>(MessageServer(), 3, msg.mMessage);
    }
    return kDefaultResult;
}

// Case 37: posts a message with id 0xad750caa (the server is fetched first).
uint32_t GenericSimCall::Case37()
{
    void *const server = MessageServer();
    MessageRef msg;
    msg.SetId(0xad750caau);
    Call<void>(server, 3, msg.mMessage);
    return kDefaultResult;
}

// Case 40: posts two messages (ids 0xcd91ece9 and 0xed9217de), each with argument 0 (sign-extended) as value 0.
uint32_t GenericSimCall::Case40()
{
    void *const server = MessageServer();
    {
        MessageRef msg;
        msg.SetId(0xcd91ece9u);
        Call<void>(msg.mMessage, 11, static_cast<int32_t>(Arg(0)));
        Call<void>(server, 3, msg.mMessage);
    }
    {
        MessageRef msg;
        msg.SetId(0xed9217deu);
        Call<void>(msg.mMessage, 11, static_cast<int32_t>(Arg(0)));
        Call<void>(server, 3, msg.mMessage);
    }
    return kDefaultResult;
}

// Case 44: posts a message with id 0x6cc07514, value 0 = 1 when argument 0 is zero (else 0), slot 16 with 1.
uint32_t GenericSimCall::Case44()
{
    MessageRef msg;
    msg.SetId(0x6cc07514u);
    const uint32_t flag = (Arg(0) == 0) ? 1u : 0u;
    Call<void>(msg.mMessage, 11, flag);
    Call<void>(msg.mMessage, 16, 1u);
    Call<void>(MessageServer(), 3, msg.mMessage);
    return kDefaultResult;
}

// Case 51: posts a message with id 0x4dd5c36c and argument 0 (sign-extended) as value 0, with slot 4 of the server (message, 0).
uint32_t GenericSimCall::Case51()
{
    const int16_t a0 = Arg(0);
    MessageRef msg;
    msg.SetId(0x4dd5c36cu);
    Call<void>(msg.mMessage, 11, static_cast<int32_t>(a0));
    Call<void>(MessageServer(), 4, msg.mMessage, 0u);
    return kDefaultResult;
}

// Case 78: asks the object for its interface 0x5a69d3d5 (failing throws 0x1c) and holds it for the call; builds a key
// {0x6c4f359d, 0x4f184aa9, argument 0 | argument 1 << 16, 0} and hands it to slot 10 of the cached service 0x259c20 with the
// value slot 28 of the object's slot-61 object returns and a flag (argument 2 non-zero).
uint32_t GenericSimCall::Case78()
{
    HeldRef iface;
    Call<uint32_t>(mSelf, 0, 0x5a69d3d5u, &iface.mObject);
    if (!iface.mObject) Throw(0x1c);
    void *const owner = Call<void *>(mSelf, 61);
    const uint32_t extra = Call<uint32_t>(owner, 28);
    const int16_t lo = Arg(0);
    const int16_t hi = Arg(1);
    const uint32_t packed = (static_cast<uint32_t>(static_cast<int32_t>(lo)) & 0xffffu) | (static_cast<uint32_t>(static_cast<int32_t>(hi)) << 16);
    const uint8_t flag = Arg(2) != 0 ? 1 : 0;
    const Key16 key{{0x6c4f359du, 0x4f184aa9u, packed, 0u}};
    // The branch result is the service's answer (1 when its low byte is nonzero, else 0); lane Z4: ours dropped it and always gave 1.
    return (Call<uint32_t>(Service259C20(), 10, &key, extra, static_cast<uint32_t>(flag)) & 0xff) != 0 ? 1u : 0u;
}

// Case 79: the object for argument 0 in the object table is referenced for the call; when there is one, slot 201 gets
// (0, 1, 1, 0).
uint32_t GenericSimCall::Case79()
{
    void *const object = Call<void *>(mL1c, 29, static_cast<int32_t>(Arg(0)));
    if (object) Call<uint32_t>(object, 1); // the holder's AddRef
    struct Release {
        void *mObject;
        ~Release() noexcept
        {
            if (mObject) Call<uint32_t>(mObject, 2);
        }
    } guard{object};
    if (object) Call<void>(object, 201, 0u, 1u, 1u, 0u);
    return kDefaultResult;
}
