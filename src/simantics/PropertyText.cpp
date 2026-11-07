#include "app/AppInit.h"
#include "app/AppServices2.h"
#include "game/vtable.h"
#include "ifaces/ifaces.h"
#include "rz/Singleton_20ddfb.h"
#include "simantics/TokenExpander.h"

#include <cstring>

// FormatPropertyText (0x5307b0): the display form of a property text. Lane AJ.
//
// The framework service hands out the property service (query ids 0xac1887e / 0x4bb0d269); it parses the text (given as a
// string whose storage comes from the allocator service) into a typed value object, whose type id says how to print it:
// integers and floats go through the language service's formatters, a text is copied.

namespace tokens {

namespace {

constexpr uint32_t kQueryFirstId = 0x0ac1887e;
constexpr uint32_t kQuerySecondId = 0x4bb0d269;
constexpr int kQuerySlot = 5;

// The type ids of the value objects (tentative names).
constexpr uint32_t kTypeInt32 = 0x0c264712;
constexpr uint32_t kTypeUint32 = 0xeb61e4f7;
constexpr uint32_t kTypeFloat = 0xabc78708;
constexpr uint32_t kTypeText = 0x0b8bea18;

// The value objects: a vtable and one dword (a number, a float, or a pointer to the text object).
struct ValueObject {
    void **mVTable;
    uint32_t mValue;
};

// The reference to the property service; released through its slot 2 when the function leaves (also by an exception).
struct HeldPropertyService {
    ScopedRef mRef{nullptr};
    HeldPropertyService()
    {
        if (void *framework = GetServiceObject()) {
            using Query = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
            VirtualCall<Query, kQuerySlot>(framework, kQueryFirstId, kQuerySecondId, &mRef.mObject);
        }
    }
    HeldPropertyService(const HeldPropertyService &) = delete;
    HeldPropertyService &operator=(const HeldPropertyService &) = delete;
    ~HeldPropertyService() noexcept(false) { mRef.ReleaseObject(); }
};

// The property text as the service wants it: a std::string whose storage comes from the allocator service (the game's
// string constructor 0x1773e0 zeroes the object, then assigns). Tidied (buffer freed through the service) when it goes.
struct ServiceText {
    as2::RawString mString;
    explicit ServiceText(const char *text)
    {
        std::memset(&mString, 0, sizeof(mString));
        as2::AssignServiceString(&mString, nullptr, text, static_cast<uint32_t>(std::strlen(text)));
    }
    ServiceText(const ServiceText &) = delete;
    ServiceText &operator=(const ServiceText &) = delete;
    ~ServiceText() { as2::TidyString(&mString, nullptr); }
};

// The format the float is printed with.
struct FloatFormat {
    cRZString mText;
    FloatFormat() { mText.Construct("%f"); }
    FloatFormat(const FloatFormat &) = delete;
    FloatFormat &operator=(const FloatFormat &) = delete;
    ~FloatFormat() noexcept { mText.Destruct(); }
};

} // namespace

uint8_t __cdecl FormatPropertyText(const char *source, cRZString *out)
{
    using namespace ifaces;
    HeldPropertyService service;

    // ORIGINAL-BUG: the service reference is not tested; with no property service the call below dereferences null.
    ValueObject *value;
    {
        ServiceText text(source);
        value = reinterpret_cast<ValueObject *>(As<cIGZPropertySetString>(service.mRef.mObject)->GetProperty(&text.mString));
    }
    if (!value) return 0;

    switch (As<PropertyValue>(value)->GetTypeId()) {
    case kTypeInt32: {
        // A signed 32-bit number as a 64-bit one (the high half is the sign).
        const int32_t number = static_cast<int32_t>(value->mValue);
        using Format = uint8_t(__thiscall *)(void *, int32_t, int32_t, void *);
        void *language = As<TokenLanguageService>(appinit::GetLanguageService())->GetLanguage(0);
        return VirtualCall<Format, 18>(language, number, number >> 31, out);
    }
    case kTypeUint32: {
        using Format = uint8_t(__thiscall *)(void *, int32_t, int32_t, void *);
        void *language = As<TokenLanguageService>(appinit::GetLanguageService())->GetLanguage(0);
        return VirtualCall<Format, 18>(language, static_cast<int32_t>(value->mValue), 0, out);
    }
    case kTypeFloat: {
        void *language = As<TokenLanguageService>(appinit::GetLanguageService())->GetLanguage(0);
        FloatFormat format;
        float number;
        std::memcpy(&number, &value->mValue, sizeof(number));
        using Format = uint8_t(__thiscall *)(void *, double, void *, void *);
        return VirtualCall<Format, 19>(language, static_cast<double>(number), out, &format.mText);
    }
    case kTypeText: {
        // The value holds a pointer to a text object; nothing printable when it is null.
        void *text = reinterpret_cast<void *>(static_cast<uintptr_t>(value->mValue));
        if (!text) return 0;
        As<cIGZString>(out)->FromChar(As<cIGZString>(text)->ToChar());
        return 1;
    }
    default:
        return 0;
    }
}

} // namespace tokens
