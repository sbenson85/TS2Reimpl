#include "simantics/PrimGenericSimCall.h"
#include "simantics/PrimGenericSimCallMath.h"
#include "simantics/PrimGenericSimCallMessage.h"
#include "simantics/PrimXCommon.h"

#include <cmath>

using primX::Call;
using gscmath::RectCenter;
using gscmsg::MessageRef;
using gscmsg::MessageServer;

// GenericSimCall (opcode 0x01) cases, group E: a distance between two rectangles' centres (34) and a copy-then-post case (45).

namespace {
constexpr uint32_t kDefaultResult = 1;

} // namespace

// Case 34: argument 2 = the distance between the centres of the rectangles slot 12 of the two objects (found by arguments 0 and 1
// in the context's slot-6 list, slot 57) fills in, truncated, as an unsigned word; 0x7fff when either object is missing.
uint32_t GenericSimCall::Case34()
{
    uint16_t result = 0x7fff;
    const int32_t a = Arg(0);
    const int32_t b = Arg(1);
    void *const list = ContextSlot(6);
    void *const first = Call<void *>(list, 57, a);
    void *const second = Call<void *>(list, 57, b);
    if (first && second) {
        // The rectangles are 16 bytes of ints the callees write (the originals are left uninitialised until then).
        int32_t r1[4] = {};
        int32_t r2[4] = {};
        Call<void>(first, 12, r1);
        Call<void>(second, 12, r2);
        const gscmath::Pair c1 = RectCenter(r1);
        const gscmath::Pair c2 = RectCenter(r2);
        const int32_t dx = c1.x - c2.x;
        const int32_t dy = c1.y - c2.y;
        const int32_t squares = dx * dx + dy * dy;
        result = static_cast<uint16_t>(static_cast<int32_t>(std::sqrt(static_cast<double>(squares))));
    }
    SetArg(2, result);
    return kDefaultResult;
}

// Case 45: the stack object (none throws 0xa) and the object for argument 0 (none throws 0xa). A text is filled from the
// second object's slot-61 object's slot-54 object (slot 7) and handed to the stack object's (slot 12); a few calls on the
// stack object's slot-61 object chain follow (slot 4 of its slot-54 object with a value built from slots 4, 3, 67), then
// a message (id 0x93) goes out: value 0 and value 2 (slots 11 and 21) are two words from the context's slot-26 list's slot-39
// object (its slot-8 object's slot 47, and its slot 57), slot 16 with 1.
uint32_t GenericSimCall::Case45()
{
    void *const stackObject = StackObject();
    void *const other = Call<void *>(mL1c, 29, static_cast<int32_t>(Arg(0)));
    if (!other) Throw(0xa);

    primX::Text text;
    {
        void *const a = Call<void *>(other, 61);
        void *const b = Call<void *>(a, 54);
        Call<void>(b, 7, &text.mText);
    }
    {
        void *const a = Call<void *>(stackObject, 61);
        void *const b = Call<void *>(a, 54);
        Call<void>(b, 12, &text.mText);
    }
    {
        void *const a = Call<void *>(stackObject, 61);
        void *const t15 = Call<void *>(a, 4);
        void *const t16 = Call<void *>(stackObject, 61);
        void *const t17 = Call<void *>(stackObject, 61);
        void *const t18 = Call<void *>(t17, 54);
        void *const inner = Call<void *>(t15, 3, 0x29u);
        void *const value = Call<void *>(t16, 67, inner);
        Call<void>(t18, 4, value);
    }
    {
        void *const a = Call<void *>(stackObject, 61);
        Call<void>(a, 3);
    }

    MessageRef msg;
    msg.SetId(0x93);
    int32_t value0 = 0;
    int32_t value2 = 0;
    void *const holder = Call<void *>(Context(), 26);
    void *const info = Call<void *>(holder, 39);
    if (info) {
        void *const inner = Call<void *>(info, 8);
        value0 = Call<int16_t>(inner, 47);
        value2 = Call<int16_t>(info, 57);
    }
    Call<void>(msg.mMessage, 11, value0);
    Call<void>(msg.mMessage, 16, 1u);
    Call<void>(msg.mMessage, 21, value2);
    Call<void>(MessageServer(), 3, msg.mMessage);
    return kDefaultResult;
}
