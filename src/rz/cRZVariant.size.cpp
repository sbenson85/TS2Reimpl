// cRZVariant (src/rz/cRZVariant.h). RVAs are 1.18.0.187.
#include "rz/cRZVariant.h"

#include "app/ReportBlockers.h" // appbase::AssignNarrowed
#include "game/game.h"
#include "game/original.h"
#include "gz/CommandParser.h"   // nGZCommandParser::FormatDecimal
#include "gz/cIGZString.h"
#include "misc/HexParse.h"      // hexparse::ParseHex32
#include "rz/RZTextParse.h"     // rztext::ParseHex64, ParseInt64, ParseDouble, SplitText
#include "rz/StringKeyParser.h" // StringKeyParser::ParseNumber
#include "rz/cRZIniFile.h"      // cRZIniStringHelpers::TrimBoth
#include "rz/cRZString.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

namespace {

constexpr uint32_t kIIDString = 0x089b7dc8; // cIGZString

// The game's texts (owned statics, cRZVariant.h).
constexpr const char *kTextT = ts2_rva_df42c4;          // "t"
constexpr const char *kTextF = ts2_rva_db137c;          // "f"
constexpr const char *kTextTrue = ts2_rva_da2d44;       // "true"
constexpr const char *kTextFalse = ts2_rva_da2d3c;      // "false"
constexpr const char *kTextZero = ts2_rva_da4af4;       // "0"
constexpr const char *kTextOne = ts2_rva_da4fac;        // "1"
constexpr const char *kTextTrueWord = ts2_rva_dd41a0;   // the bool as a word, true
constexpr const char *kTextFalseWord = ts2_rva_dd41a8;  // false
constexpr const char *kTextHexPrefix = ts2_rva_da9bd8;  // "0x"
constexpr const char *kFormatHex8 = ts2_rva_e61a20;     // two hexadecimal digits
constexpr const char *kFormatHex16 = ts2_rva_e61a2c;    // four
constexpr const char *kFormatHex32 = ts2_rva_dacd24;    // eight
constexpr const char *kFormatUnsigned = ts2_rva_d9d510; // decimal, unsigned
constexpr const char *kFormatSigned = ts2_rva_d9b648;   // decimal, signed
constexpr const char *kFormatUint64 = ts2_rva_da2994;   // 64-bit decimal (used for both 64-bit kinds)
constexpr const char *kFormatFloat = ts2_rva_dce3c8;    // the arrays' floats
constexpr const char *kFormatPointer = ts2_rva_da3e60;  // a pointer, eight hexadecimal digits

inline const char *Text(const char *text) { return text; }

// The shared text parsers (rz/RZTextParse.h).
using rztext::ParseDouble;
using rztext::ParseHex64;
using rztext::ParseInt64;
bool Split(const char *text, uint32_t size, char separator, cRZString *parts, uint32_t *count)
{
    return rztext::SplitText(text, size, separator, parts, count);
}

// 0x1b0ff4: strtod with an end pointer nobody reads.
double ParseStrtod(const char *text)
{
    char *end;
    return std::strtod(text, &end);
}

// The double to uint64 conversion the original inlines: values from 2^63 on are converted less 2^63 and get the top bit
// back, unless they are 2^64 or more (then the less-2^63 value is converted as it is: out of range, 0x8000000000000000).
uint64_t DoubleToUint64(double value)
{
    constexpr double kTop = 9223372036854775808.0;
    uint64_t adjust = 0;
    if (value >= kTop) {
        value -= kTop;
        if (value < kTop) adjust = 0x8000000000000000ull;
    }
    return static_cast<uint64_t>(static_cast<int64_t>(value)) + adjust;
}

// A local cRZString (the game's layout, our members), built and destroyed by its Construct / Destruct only.
struct LocalString {
    union {
        cRZString s;
    };
    LocalString() { s.Construct(); }
    explicit LocalString(const cIGZString &text) { s.Construct(text); }
    ~LocalString() { s.Destruct(); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
};

// The array formatter (0x9c7659 floats, 0x9c76d1 doubles, 0x9c7746 64-bit, 0x9c77b6 sint8, 0x9c7824 uint8, 0x9c7892
// sint16, 0x9c7900 uint16, 0x9c796e 32-bit): each element through a temporary string's Sprintf (widened as `Wide`), the
// texts appended, `separator` between them.
template <class T, class Wide> void FormatElements(const cRZVariant *from, const char *format, char separator, cRZString *text)
{
    const T *values = static_cast<const T *>(from->mArray);
    for (uint32_t i = 0; i < from->mCount; ++i) {
        LocalString piece;
        piece.s.Sprintf(format, static_cast<Wide>(values[i]));
        text->mString.append(piece.s.mString);
        if (i < from->mCount - 1) text->mString.push_back(separator);
    }
}

// Byte sizes of the array kinds (tag & 0x7fff), as IsEqual compares them.
uint32_t ElementSize(uint32_t kind)
{
    switch (kind) {
    case 1: case 2: case 3: case 0xc: return 1;
    case 4: case 5: case 0xd: return 2;
    case 6: case 7: case 0xa: case 0xf: return 4;
    case 8: case 9: case 0xb: return 8;
    default: return 0;
    }
}

} // namespace

// ---- construction ----

cRZVariant::cRZVariant()
{
    mUint64 = 0;
    mRefCount = 0;
    mType = kTypeEmpty;
    mCount = 0;
}

cRZVariant::cRZVariant(const cRZVariant &other) : cRZVariant() { Assign(other); }

cRZVariant::cRZVariant(const cIGZString &text) : cRZVariant()
{
    const uint32_t size = text.Strlen();
    SetRZCharArray(text.Data(), size); // 0x9ca862, called directly by the original
}

cRZVariant::cRZVariant(const cIGZVariant &other) : cRZVariant() { AssignFrom(other); }

cRZVariant::cRZVariant(int8_t value) : cRZVariant() { mSint8 = value; mType = kTypeSint8; }
cRZVariant::cRZVariant(uint8_t value) : cRZVariant() { mUint8 = value; mType = kTypeUint8; }
cRZVariant::cRZVariant(int16_t value) : cRZVariant() { mSint16 = value; mType = kTypeSint16; }
cRZVariant::cRZVariant(uint16_t value) : cRZVariant() { mUint16 = value; mType = kTypeUint16; }
cRZVariant::cRZVariant(int32_t value) : cRZVariant() { mSint32 = value; mType = kTypeSint32; }
cRZVariant::cRZVariant(uint32_t value) : cRZVariant() { mUint32 = value; mType = kTypeUint32; }
cRZVariant::cRZVariant(float value) : cRZVariant() { mFloat32 = value; mType = kTypeFloat32; }
cRZVariant::cRZVariant(double value) : cRZVariant() { mFloat64 = value; mType = kTypeFloat64; }
cRZVariant::cRZVariant(cIGZUnknown *value) : cRZVariant() { StoreUnknown(value); }
cRZVariant::cRZVariant(void *value) : cRZVariant() { mVoidPtr = value; mType = kTypeVoidPtr; }
cRZVariant::cRZVariant(int64_t value) : cRZVariant() { mSint64 = value; mType = kTypeSint64; }
cRZVariant::cRZVariant(uint64_t value) : cRZVariant() { mUint64 = value; mType = kTypeUint64; }
cRZVariant::cRZVariant(bool value) : cRZVariant() { mBool = value; mType = kTypeBool; }

cRZVariant::cRZVariant(const void *data, uint32_t size, uint32_t type) : cRZVariant()
{
    SetVoidArray(data, size); // 0x9caca1, called directly
    mType = static_cast<uint16_t>(type | kTypeArray);
}

cRZVariant::cRZVariant(const char *text) : cRZVariant()
{
    SetCharArray(text, static_cast<uint32_t>(std::strlen(text))); // 0x9ca715, called directly
}

cRZVariant::~cRZVariant() { Clear(); }

cRZVariant &cRZVariant::Assign(const cRZVariant &other)
{
    switch (other.mType) {
    case kTypeBool: StoreBool(other.mBool); break;
    case kTypeUint8: StoreUint8(other.mUint8); break;
    case kTypeSint8: StoreSint8(other.mSint8); break;
    case kTypeUint16: StoreUint16(other.mUint16); break;
    case kTypeSint16: StoreSint16(other.mSint16); break;
    case kTypeUint32: StoreUint32(other.mUint32); break;
    case kTypeSint32: StoreSint32(other.mSint32); break;
    case kTypeUint64: StoreUint64(other.mUint64); break;
    case kTypeSint64: StoreSint64(other.mSint64); break;
    case kTypeFloat32: StoreFloat32(other.mFloat32); break;
    case kTypeFloat64: StoreFloat64(other.mFloat64); break;
    case kTypeChar: StoreChar(other.mChar); break;
    case kTypeRZUnicodeChar: StoreRZUnicodeChar(other.mRZUnicodeChar); break;
    case kTypeRZChar: StoreRZChar(other.mRZChar); break;
    case kTypeVoidPtr: StoreVoidPtr(other.mVoidPtr); break;
    case kTypeUnknown: StoreUnknown(other.mUnknown); break;
    case 0x8000: SetVoidArray(other.mArray, other.mCount); break;
    case 0x8001: SetBoolArray(static_cast<const uint8_t *>(other.mArray), other.mCount); break;
    case 0x8002: SetUint8Array(static_cast<const uint8_t *>(other.mArray), other.mCount); break;
    case 0x8003: SetSint8Array(static_cast<const int8_t *>(other.mArray), other.mCount); break;
    case 0x8004: SetUint16Array(static_cast<const uint16_t *>(other.mArray), other.mCount); break;
    case 0x8005: SetSint16Array(static_cast<const int16_t *>(other.mArray), other.mCount); break;
    case 0x8006: SetUint32Array(static_cast<const uint32_t *>(other.mArray), other.mCount); break;
    case 0x8007: SetSint32Array(static_cast<const int32_t *>(other.mArray), other.mCount); break;
    case 0x8008: SetUint64Array(static_cast<const uint64_t *>(other.mArray), other.mCount); break;
    case 0x8009: SetSint64Array(static_cast<const int64_t *>(other.mArray), other.mCount); break;
    case 0x800a: SetFloat32Array(static_cast<const float *>(other.mArray), other.mCount); break;
    case 0x800b: SetFloat64Array(static_cast<const double *>(other.mArray), other.mCount); break;
    case 0x800c: SetCharArray(static_cast<const char *>(other.mArray), other.mCount); break;
    case 0x800d: SetRZUnicodeCharArray(static_cast<const uint16_t *>(other.mArray), other.mCount); break;
    case 0x800e: SetRZCharArray(static_cast<const char *>(other.mArray), other.mCount); break;
    case 0x800f: SetVoidPtrArray(static_cast<void *const *>(other.mArray), other.mCount); break;
    default: Clear(); break; // empty, and the tags with no kind
    }
    return *this;
}

// ---- cIGZUnknown ----

bool cRZVariant::QueryInterface(uint32_t riid, void **out)
{
    if (riid != 1 && riid != 0x60fd4d2b) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cRZVariant::AddRef() { return ++mRefCount; }

uint32_t cRZVariant::Release()
{
    // At a count of 0 or 1 the variant is deleted (slot 132), without the count going down first.
    if (mRefCount > 1) return --mRefCount;
    delete this;
    return 0;
}

// ---- state ----

uint32_t cRZVariant::GetType() const { return mType; }
uint32_t cRZVariant::GetCount() const { return mCount; }
bool cRZVariant::IsUndefined() const { return mType == kTypeEmpty; }

bool cRZVariant::Erase()
{
    Clear();
    return true;
}

void cRZVariant::Clear()
{
    if (mType & kTypeArray) {
        ::operator delete[](mArray);
    } else if (mType == kTypeUnknown) {
        if (mUnknown) mUnknown->Release();
    }
    mUint64 = 0;
    mType = kTypeEmpty;
    mCount = 0;
}

bool cRZVariant::CopyFrom(const cIGZVariant &other)
{
    cIGZVariant &source = const_cast<cIGZVariant &>(other); // the array getters are not const
    const uint32_t type = other.GetType();
    switch (type) {
    case kTypeEmpty: Clear(); return true;
    case kTypeBool: StoreBool(other.AsBool()); return true;
    case kTypeUint8: StoreUint8(other.AsUint8()); return true;
    case kTypeSint8: StoreSint8(other.AsSint8()); return true;
    case kTypeUint16: StoreUint16(other.AsUint16()); return true;
    case kTypeSint16: StoreSint16(other.AsSint16()); return true;
    case kTypeUint32: StoreUint32(other.AsUint32()); return true;
    case kTypeSint32: StoreSint32(other.AsSint32()); return true;
    case kTypeUint64: StoreUint64(other.AsUint64()); return true;
    case kTypeSint64: StoreSint64(other.AsSint64()); return true;
    case kTypeFloat32: StoreFloat32(other.AsFloat32()); return true;
    case kTypeFloat64: StoreFloat64(other.AsFloat64()); return true;
    case kTypeChar: StoreChar(other.AsChar()); return true;
    case kTypeRZUnicodeChar: StoreRZUnicodeChar(other.AsRZUnicodeChar()); return true;
    case kTypeRZChar: StoreRZChar(other.AsRZChar()); return true;
    case kTypeVoidPtr: StoreVoidPtr(other.AsVoidPtr()); return true;
    case kTypeUnknown: StoreUnknown(other.AsUnknown()); return true;
    }
    // Arrays: the count first, then the other's buffer, then this variant's setter.
    switch (type) {
    case 0x8000: { uint32_t n = other.GetCount(); SetVoidArray(source.GetVoidArray(), n); return true; }
    case 0x8001: { uint32_t n = other.GetCount(); SetBoolArray(source.GetBoolArray(), n); return true; }
    case 0x8002: { uint32_t n = other.GetCount(); SetUint8Array(source.GetUint8Array(), n); return true; }
    case 0x8003: { uint32_t n = other.GetCount(); SetSint8Array(source.GetSint8Array(), n); return true; }
    case 0x8004: { uint32_t n = other.GetCount(); SetUint16Array(source.GetUint16Array(), n); return true; }
    case 0x8005: { uint32_t n = other.GetCount(); SetSint16Array(source.GetSint16Array(), n); return true; }
    case 0x8006: { uint32_t n = other.GetCount(); SetUint32Array(source.GetUint32Array(), n); return true; }
    case 0x8007: { uint32_t n = other.GetCount(); SetSint32Array(source.GetSint32Array(), n); return true; }
    case 0x8008: { uint32_t n = other.GetCount(); SetUint64Array(source.GetUint64Array(), n); return true; }
    case 0x8009: { uint32_t n = other.GetCount(); SetSint64Array(source.GetSint64Array(), n); return true; }
    case 0x800a: { uint32_t n = other.GetCount(); SetFloat32Array(source.GetFloat32Array(), n); return true; }
    case 0x800b: { uint32_t n = other.GetCount(); SetFloat64Array(source.GetFloat64Array(), n); return true; }
    case 0x800c: { uint32_t n = other.GetCount(); SetCharArray(source.GetCharArray(), n); return true; }
    case 0x800d: { uint32_t n = other.GetCount(); SetRZUnicodeCharArray(source.GetRZUnicodeCharArray(), n); return true; }
    case 0x800e: { uint32_t n = other.GetCount(); SetRZCharArray(source.GetRZCharArray(), n); return true; }
    }
    // A pointer array (0x800f) and the tags with no kind: this variant is erased and the copy fails.
    Clear();
    return false;
}

bool cRZVariant::CreateCopy(cIGZVariant **out)
{
    cRZVariant *copy = new cRZVariant(*this);
    copy->AddRef();
    *out = copy;
    return true;
}

bool cRZVariant::CanGetVal(uint32_t) const { return true; }

// ---- the typed getters ----

// GetValX: the payload when the tag is the kind's; otherwise a temporary of the kind (zero) converted from this one.
template <class T> bool cRZVariant::GetValAs(uint16_t tag, T *out, T tagRZVariant::*field)
{
    if (mType == tag) {
        *out = this->*field;
        return true;
    }
    cRZVariant temp;
    temp.mType = tag;
    const bool ok = Convert(this, &temp);
    if (ok) *out = temp.*field;
    return ok;
}

uint8_t cRZVariant::ValBool() { uint8_t v = 0; GetValBool(&v); return v; }
bool cRZVariant::GetValBool(uint8_t *out) { return GetValAs(kTypeBool, out, &tagRZVariant::mBool); }
uint8_t cRZVariant::ValUint8() { uint8_t v = 0; GetValUint8(&v); return v; }
bool cRZVariant::GetValUint8(uint8_t *out) { return GetValAs(kTypeUint8, out, &tagRZVariant::mUint8); }
int8_t cRZVariant::ValSint8() { int8_t v = 0; GetValSint8(&v); return v; }
bool cRZVariant::GetValSint8(int8_t *out) { return GetValAs(kTypeSint8, out, &tagRZVariant::mSint8); }
uint16_t cRZVariant::ValUint16() { uint16_t v = 0; GetValUint16(&v); return v; }
bool cRZVariant::GetValUint16(uint16_t *out) { return GetValAs(kTypeUint16, out, &tagRZVariant::mUint16); }
int16_t cRZVariant::ValSint16() { int16_t v = 0; GetValSint16(&v); return v; }
bool cRZVariant::GetValSint16(int16_t *out) { return GetValAs(kTypeSint16, out, &tagRZVariant::mSint16); }
uint32_t cRZVariant::ValUint32() { uint32_t v = 0; GetValUint32(&v); return v; }
bool cRZVariant::GetValUint32(uint32_t *out) { return GetValAs(kTypeUint32, out, &tagRZVariant::mUint32); }
int32_t cRZVariant::ValSint32() { int32_t v = 0; GetValSint32(&v); return v; }
bool cRZVariant::GetValSint32(int32_t *out) { return GetValAs(kTypeSint32, out, &tagRZVariant::mSint32); }
uint64_t cRZVariant::ValUint64() { uint64_t v = 0; GetValUint64(&v); return v; }
bool cRZVariant::GetValUint64(uint64_t *out) { return GetValAs(kTypeUint64, out, &tagRZVariant::mUint64); }
int64_t cRZVariant::ValSint64() { int64_t v = 0; GetValSint64(&v); return v; }
bool cRZVariant::GetValSint64(int64_t *out) { return GetValAs(kTypeSint64, out, &tagRZVariant::mSint64); }
float cRZVariant::ValFloat32() { float v = 0.0f; GetValFloat32(&v); return v; }
bool cRZVariant::GetValFloat32(float *out) { return GetValAs(kTypeFloat32, out, &tagRZVariant::mFloat32); }
double cRZVariant::ValFloat64() { double v = 0.0; GetValFloat64(&v); return v; }
bool cRZVariant::GetValFloat64(double *out) { return GetValAs(kTypeFloat64, out, &tagRZVariant::mFloat64); }
char cRZVariant::ValChar() { char v = 0; GetValChar(&v); return v; }
bool cRZVariant::GetValChar(char *out) { return GetValAs(kTypeChar, out, &tagRZVariant::mChar); }
uint16_t cRZVariant::ValRZUnicodeChar() { uint16_t v = 0; GetValRZUnicodeChar(&v); return v; }
bool cRZVariant::GetValRZUnicodeChar(uint16_t *out) { return GetValAs(kTypeRZUnicodeChar, out, &tagRZVariant::mRZUnicodeChar); }
char cRZVariant::ValRZChar() { char v = 0; GetValRZChar(&v); return v; }
bool cRZVariant::GetValRZChar(char *out) { return GetValAs(kTypeRZChar, out, &tagRZVariant::mRZChar); }
void *cRZVariant::ValVoidPtr() { void *v = nullptr; GetValVoidPtr(&v); return v; }
bool cRZVariant::GetValVoidPtr(void **out) { return GetValAs(kTypeVoidPtr, out, &tagRZVariant::mVoidPtr); }

// ---- the scalar setters ----

template <class T> void cRZVariant::StoreScalar(uint16_t tag, T tagRZVariant::*field, T value)
{
    if (mType != tag) {
        if (mType != kTypeEmpty) Clear();
        mType = tag;
    }
    this->*field = value;
}

void cRZVariant::StoreBool(uint8_t value) { StoreScalar(kTypeBool, &tagRZVariant::mBool, value); }
void cRZVariant::StoreUint8(uint8_t value) { StoreScalar(kTypeUint8, &tagRZVariant::mUint8, value); }
void cRZVariant::StoreSint8(int8_t value) { StoreScalar(kTypeSint8, &tagRZVariant::mSint8, value); }
void cRZVariant::StoreUint16(uint16_t value) { StoreScalar(kTypeUint16, &tagRZVariant::mUint16, value); }
void cRZVariant::StoreSint16(int16_t value) { StoreScalar(kTypeSint16, &tagRZVariant::mSint16, value); }
void cRZVariant::StoreUint32(uint32_t value) { StoreScalar(kTypeUint32, &tagRZVariant::mUint32, value); }
void cRZVariant::StoreSint32(int32_t value) { StoreScalar(kTypeSint32, &tagRZVariant::mSint32, value); }
void cRZVariant::StoreUint64(uint64_t value) { StoreScalar(kTypeUint64, &tagRZVariant::mUint64, value); }
void cRZVariant::StoreSint64(int64_t value) { StoreScalar(kTypeSint64, &tagRZVariant::mSint64, value); }
void cRZVariant::StoreFloat32(float value) { StoreScalar(kTypeFloat32, &tagRZVariant::mFloat32, value); }
void cRZVariant::StoreFloat64(double value) { StoreScalar(kTypeFloat64, &tagRZVariant::mFloat64, value); }
void cRZVariant::StoreChar(char value) { StoreScalar(kTypeChar, &tagRZVariant::mChar, value); }
void cRZVariant::StoreRZUnicodeChar(uint16_t value) { StoreScalar(kTypeRZUnicodeChar, &tagRZVariant::mRZUnicodeChar, value); }
void cRZVariant::StoreRZChar(char value) { StoreScalar(kTypeRZChar, &tagRZVariant::mRZChar, value); }
void cRZVariant::StoreVoidPtr(void *value) { StoreScalar(kTypeVoidPtr, &tagRZVariant::mVoidPtr, value); }

void cRZVariant::StoreUnknown(cIGZUnknown *value)
{
    if (mType != kTypeUnknown) {
        if (mType != kTypeEmpty) Clear();
        mType = kTypeUnknown;
    }
    if (value == mUnknown) return;
    if (value) value->AddRef();
    if (mUnknown) mUnknown->Release();
    mUnknown = value;
}

void cRZVariant::SetValBool(uint8_t value) { StoreBool(value); }
void cRZVariant::SetValUint8(uint8_t value) { StoreUint8(value); }
void cRZVariant::SetValSint8(int8_t value) { StoreSint8(value); }
void cRZVariant::SetValUint16(uint16_t value) { StoreUint16(value); }
void cRZVariant::SetValSint16(int16_t value) { StoreSint16(value); }
void cRZVariant::SetValUint32(uint32_t value) { StoreUint32(value); }
void cRZVariant::SetValSint32(int32_t value) { StoreSint32(value); }
void cRZVariant::SetValUint64(uint64_t value) { StoreUint64(value); }
void cRZVariant::SetValSint64(int64_t value) { StoreSint64(value); }
void cRZVariant::SetValFloat32(float value) { StoreFloat32(value); }
void cRZVariant::SetValFloat64(double value) { StoreFloat64(value); }
void cRZVariant::SetValChar(char value) { StoreChar(value); }
void cRZVariant::SetValRZUnicodeChar(uint16_t value) { StoreRZUnicodeChar(value); }
void cRZVariant::SetValRZChar(char value) { StoreRZChar(value); }
void cRZVariant::SetValVoidPtr(void *value) { StoreVoidPtr(value); }

// ---- text ----

bool cRZVariant::CreateValString(cIGZString **out, uint32_t flags, char separator)
{
    cRZString *text = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
    text->Construct();
    cIGZString *face = text->AsInterface();
    face->AddRef();
    if (TextOf(*face, flags, separator)) {
        *out = face;
        return true;
    }
    face->Release();
    *out = nullptr;
    return false;
}

bool cRZVariant::GetValString(cIGZString &text, uint32_t flags, char separator) { return TextOf(text, flags, separator); }

bool cRZVariant::TextOf(cIGZString &text, uint32_t flags, char separator)
{
    if (mType == 0x800e) {
        text.FromChar(static_cast<const char *>(mArray), mCount);
        return true;
    }
    if (mType == kTypeUnknown) {
        // No null test: an unknown variant holding null crashes here, as the original.
        cIGZString *string;
        if (mUnknown->QueryInterface(kIIDString, reinterpret_cast<void **>(&string))) {
            const uint32_t size = string->Strlen();
            text.FromChar(string->Data(), size);
            string->Release();
            return true;
        }
    }
    // ORIGINAL-BUG: the text is written, but the result is false for every other kind (CreateValString then frees it).
    LocalString formatted;
    ToString(this, &formatted.s, flags, separator);
    text.FromChar(formatted.s.mString.c_str(), static_cast<uint32_t>(formatted.s.mString.size()));
    return false;
}

void cRZVariant::SetValString(const cIGZString &text) { StoreText(text); }

void cRZVariant::StoreText(const cIGZString &text)
{
    const uint32_t size = text.Strlen();
    SetRZCharArray(text.Data(), size);
}

bool cRZVariant::SetFromString(const cIGZString &text, uint32_t type, uint32_t count, char separator)
{
    if (type == 0x800e) {
        Erase();
        SetValString(text);
        return true;
    }
    if (type & kTypeArray) return ParseArray(text, type, count, separator);
    return ParseScalar(text, type);
}

bool cRZVariant::IsTrueText(const cRZString *text)
{
    const cIGZString *face = text->AsInterface();
    return !(face->IsEqual(Text(kTextF), 1, false) || face->IsEqual(Text(kTextFalse), 5, false) ||
             face->IsEqual(Text(kTextZero), 1, false));
}

void cRZVariant::ParsePointer(const cRZString *text, void *out)
{
    *static_cast<uint32_t *>(out) = hexparse::ParseHex32(text->mString.c_str());
}

void cRZVariant::FormatPointer(const void *pointer, cRZString *text)
{
    text->AsInterface()->Sprintf(Text(kFormatPointer), pointer);
}

bool cRZVariant::ParseScalar(const cIGZString &text, uint32_t type)
{
    Erase();
    if (type == 0x800e) {
        SetValString(text);
        return true;
    }
    if (type & kTypeArray) return false;
    LocalString trimmed(text);
    reinterpret_cast<cRZIniStringHelpers *>(&trimmed.s)->TrimBoth();
    if (text.Strlen() == 0 || type > kTypeUnknown) return false;
    const char *chars = trimmed.s.mString.c_str();
    switch (type) {
    case kTypeEmpty: break;
    case kTypeBool: StoreBool(IsTrueText(&trimmed.s)); break;
    case kTypeUint8: StoreUint8(static_cast<uint8_t>(StringKeyParser::ParseNumber(chars))); break;
    case kTypeSint8: StoreSint8(static_cast<int8_t>(StringKeyParser::ParseNumber(chars))); break;
    case kTypeUint16: StoreUint16(static_cast<uint16_t>(StringKeyParser::ParseNumber(chars))); break;
    case kTypeSint16: StoreSint16(static_cast<int16_t>(StringKeyParser::ParseNumber(chars))); break;
    case kTypeUint32: StoreUint32(StringKeyParser::ParseNumber(chars)); break;
    case kTypeSint32: StoreSint32(static_cast<int32_t>(StringKeyParser::ParseNumber(chars))); break;
    case kTypeUint64: StoreUint64(ParseInt64(chars)); break;
    case kTypeSint64: StoreSint64(static_cast<int64_t>(ParseInt64(chars))); break;
    case kTypeFloat32: StoreFloat32(static_cast<float>(ParseStrtod(chars))); break;
    case kTypeFloat64: StoreFloat64(ParseStrtod(chars)); break;
    case kTypeChar: StoreChar(chars[0]); break;
    case kTypeRZUnicodeChar: StoreRZUnicodeChar(static_cast<uint16_t>(static_cast<int16_t>(chars[0]))); break;
    case kTypeRZChar: StoreRZChar(chars[0]); break;
    case kTypeVoidPtr:
        ParsePointer(&trimmed.s, &mVoidPtr);
        mType = kTypeVoidPtr;
        break;
    case kTypeUnknown: {
        // The parsed number is taken for an object and AddRef'd.
        cIGZUnknown *pointer = nullptr;
        ParsePointer(&trimmed.s, &pointer);
        StoreUnknown(pointer);
        break;
    }
    }
    return true;
}

bool cRZVariant::ParseArray(const cIGZString &text, uint32_t type, uint32_t count, char separator)
{
    Erase();
    if (type == 0x800e || !(type & kTypeArray)) return false;
    LocalString trimmed(text);
    reinterpret_cast<cRZIniStringHelpers *>(&trimmed.s)->TrimBoth();
    const std::string &whole = trimmed.s.mString;
    if (whole.empty()) {
        if (count != 0) return false;
        mCount = 0;
        mType = static_cast<uint16_t>(type);
        return true;
    }
    const uint32_t separators = static_cast<uint32_t>(std::count(whole.begin(), whole.end(), separator));
    const uint32_t parts = separators + 1;
    const uint32_t wanted = count ? count : parts;

    // The pieces: a new[] array of cRZStrings (a count word before them), as the compiler builds one.
    uint32_t bytes = parts > 0xffffffffu / sizeof(cRZString) ? 0xffffffffu : parts * uint32_t(sizeof(cRZString));
    bytes = bytes > 0xffffffffu - 4 ? 0xffffffffu : bytes + 4;
    uint32_t *block = static_cast<uint32_t *>(::operator new[](bytes));
    *block = parts;
    cRZString *pieces = reinterpret_cast<cRZString *>(block + 1);
    for (uint32_t i = 0; i < parts; ++i) pieces[i].Construct();

    uint32_t found = parts;
    const uint32_t size = static_cast<uint32_t>(whole.size());
    bool result = false;
    if (Split(whole.c_str(), size, separator, pieces, &found)) {
        // Element i comes from piece i, or the last piece when there are fewer.
        auto piece = [&](uint32_t i) -> cRZString & { return pieces[i < parts ? i : separators]; };
        auto chars = [&](uint32_t i) { return piece(i).AsInterface()->ToChar(); };
        result = true;
        switch (type) {
        case 0x8001: {
            uint8_t *values = new uint8_t[wanted];
            for (uint32_t i = 0; i < wanted; ++i) values[i] = IsTrueText(&piece(i));
            SetBoolArray(values, wanted);
            delete[] values;
            break;
        }
        case 0x8002: {
            uint8_t *values = new uint8_t[wanted];
            for (uint32_t i = 0; i < wanted; ++i) values[i] = static_cast<uint8_t>(std::atoi(chars(i)));
            SetUint8Array(values, wanted);
            delete[] values;
            break;
        }
        case 0x8003: {
            int8_t *values = new int8_t[wanted];
            for (uint32_t i = 0; i < wanted; ++i) values[i] = static_cast<int8_t>(std::atoi(chars(i)));
            SetSint8Array(values, wanted);
            delete[] values;
            break;
        }
        case 0x8004: {
            uint16_t *values = new uint16_t[wanted];
            for (uint32_t i = 0; i < wanted; ++i) values[i] = static_cast<uint16_t>(std::atoi(chars(i)));
            SetUint16Array(values, wanted);
            delete[] values;
            break;
        }
        case 0x8005: {
            int16_t *values = new int16_t[wanted];
            for (uint32_t i = 0; i < wanted; ++i) values[i] = static_cast<int16_t>(std::atoi(chars(i)));
            SetSint16Array(values, wanted);
            delete[] values;
            break;
        }
        case 0x8006: {
            uint32_t *values = new uint32_t[wanted];
            for (uint32_t i = 0; i < wanted; ++i) values[i] = StringKeyParser::ParseNumber(chars(i));
            SetUint32Array(values, wanted);
            delete[] values;
            break;
        }
        case 0x8007: {
            int32_t *values = new int32_t[wanted];
            for (uint32_t i = 0; i < wanted; ++i) values[i] = std::atoi(chars(i));
            SetSint32Array(values, wanted);
            delete[] values;
            break;
        }
        case 0x8008: {
            uint64_t *values = new uint64_t[wanted];
            for (uint32_t i = 0; i < wanted; ++i) values[i] = ParseInt64(chars(i));
            SetUint64Array(values, wanted);
            delete[] values;
            break;
        }
        case 0x8009: {
            int64_t *values = new int64_t[wanted];
            for (uint32_t i = 0; i < wanted; ++i) values[i] = static_cast<int64_t>(ParseInt64(chars(i)));
            SetSint64Array(values, wanted);
            delete[] values;
            break;
        }
        case 0x800a: {
            float *values = new float[wanted];
            for (uint32_t i = 0; i < wanted; ++i) values[i] = static_cast<float>(ParseStrtod(chars(i)));
            SetFloat32Array(values, wanted);
            delete[] values;
            break;
        }
        case 0x800b: {
            double *values = new double[wanted];
            for (uint32_t i = 0; i < wanted; ++i) values[i] = ParseStrtod(chars(i));
            SetFloat64Array(values, wanted);
            delete[] values;
            break;
        }
        default: result = false; break; // the untyped, character and pointer arrays
        }
    }
    pieces->DeletingDestruct(3); // every piece destroyed, the block freed
    return result;
}

// ---- unknowns, raw buffers ----

bool cRZVariant::GetValVoid(void *out, uint32_t *size)
{
    if (mType != kTypeArray) return false;
    // The caller's size is the capacity; lowered to the stored size when larger, which is what is copied.
    uint32_t copied = *size;
    if (copied > mCount) {
        *size = mCount;
        copied = mCount;
    }
    std::memcpy(out, mArray, copied);
    return true;
}

void cRZVariant::SetValVoid(const void *data, uint32_t size) { StoreArray(data, size, kTypeArray, 1, false); }

cIGZUnknown *cRZVariant::ValUnknown() { return mType == kTypeUnknown ? mUnknown : nullptr; }
bool cRZVariant::GetValUnknown(uint32_t iid, void **out) { return QueryUnknown(iid, out); }
void cRZVariant::SetValUnknown(cIGZUnknown *value) { StoreUnknown(value); }
void cRZVariant::SetUnknownRef(cIGZUnknown *value) { StoreUnknown(value); }
bool cRZVariant::GetUnknownRef(uint32_t iid, void **out) { return QueryUnknown(iid, out); }

bool cRZVariant::QueryUnknown(uint32_t iid, void **out)
{
    if (mType != kTypeUnknown || !mUnknown) return false;
    return mUnknown->QueryInterface(iid, out);
}

// ---- comparison and assignment ----

bool cRZVariant::IsEqual(const cIGZVariant &other)
{
    cIGZVariant &source = const_cast<cIGZVariant &>(other);
    const uint32_t otherType = other.GetType();
    if (GetType() != otherType) return false;
    if (GetType() == 0x800e) {
        // Both as text (slot 56, no flags, ' ' between elements), compared as strings.
        LocalString mine, theirs;
        GetValString(*mine.s.AsInterface(), 0, ' ');
        source.GetValString(*theirs.s.AsInterface(), 0, ' ');
        return mine.s.mString == theirs.s.mString;
    }
    if (GetType() & kTypeArray) {
        const uint32_t otherCount = other.GetCount();
        if (GetCount() != otherCount) return false;
        const uint32_t kind = GetType() & ~uint32_t(kTypeArray);
        const void *a = nullptr;
        const void *b = nullptr;
        uint32_t bytes = 0;
        // The size first (this variant's count), then the other's buffer, then this one's.
        switch (kind) {
        case 1: bytes = GetCount(); b = source.GetBoolArray(); a = GetBoolArray(); break;
        case 2: bytes = GetCount(); b = source.GetUint8Array(); a = GetUint8Array(); break;
        case 3: bytes = GetCount(); b = source.GetSint8Array(); a = GetSint8Array(); break;
        case 4: bytes = GetCount() * 2; b = source.GetUint16Array(); a = GetUint16Array(); break;
        case 5: bytes = GetCount() * 2; b = source.GetSint16Array(); a = GetSint16Array(); break;
        case 6: bytes = GetCount() * 4; b = source.GetUint32Array(); a = GetUint32Array(); break;
        case 7: bytes = GetCount() * 4; b = source.GetSint32Array(); a = GetSint32Array(); break;
        case 8: bytes = GetCount() * 8; b = source.GetUint64Array(); a = GetUint64Array(); break;
        case 9: bytes = GetCount() * 8; b = source.GetSint64Array(); a = GetSint64Array(); break;
        case 0xa: bytes = GetCount() * 4; b = source.GetFloat32Array(); a = GetFloat32Array(); break;
        case 0xb: bytes = GetCount() * 8; b = source.GetFloat64Array(); a = GetFloat64Array(); break;
        case 0xc: bytes = GetCount(); b = source.GetCharArray(); a = GetCharArray(); break;
        case 0xd: bytes = GetCount() * 2; b = source.GetRZUnicodeCharArray(); a = GetRZUnicodeCharArray(); break;
        case 0xf: bytes = GetCount() * 4; b = source.GetVoidPtrArray(); a = GetVoidPtrArray(); break;
        default: return false; // untyped bytes and the tags with no kind are never equal
        }
        return std::memcmp(a, b, bytes) == 0;
    }
    // Scalars: the other's value first (the floats: this one's first), compared at the kind's width.
    switch (GetType()) {
    case kTypeEmpty: return true;
    case kTypeBool: { uint8_t v = other.AsBool(); return AsBool() == v; }
    case kTypeUint8: { uint8_t v = other.AsUint8(); return AsUint8() == v; }
    case kTypeSint8: { int8_t v = other.AsSint8(); return AsSint8() == v; }
    case kTypeUint16: { uint16_t v = other.AsUint16(); return AsUint16() == v; }
    case kTypeSint16: { int16_t v = other.AsSint16(); return AsSint16() == v; }
    case kTypeUint32: { uint32_t v = other.AsUint32(); return AsUint32() == v; }
    case kTypeSint32: { int32_t v = other.AsSint32(); return AsSint32() == v; }
    case kTypeUint64: { uint64_t v = other.AsUint64(); return AsUint64() == v; }
    case kTypeSint64: { int64_t v = other.AsSint64(); return AsSint64() == v; }
    case kTypeFloat32: { float v = AsFloat32(); return v == other.AsFloat32(); }
    case kTypeFloat64: { double v = AsFloat64(); return v == other.AsFloat64(); }
    case kTypeChar: { char v = other.AsChar(); return AsChar() == v; }
    case kTypeRZUnicodeChar: { uint16_t v = other.AsRZUnicodeChar(); return AsRZUnicodeChar() == v; }
    case kTypeRZChar: { char v = other.AsRZChar(); return AsRZChar() == v; }
    case kTypeVoidPtr: { void *v = other.AsVoidPtr(); return AsVoidPtr() == v; }
    case kTypeUnknown: { cIGZUnknown *v = other.AsUnknown(); return AsUnknown() == v; }
    default: return false;
    }
}

bool cRZVariant::IsNotEqual(const cIGZVariant &other) { return !IsEqual(other); }

cIGZVariant &cRZVariant::AssignFrom(const cIGZVariant &other)
{
    CopyFrom(other);
    return *this;
}

// ---- the payload as each kind ----

uint8_t cRZVariant::AsBool() const { return mBool; }
uint8_t *cRZVariant::AsBoolRef() { return &mBool; }
uint8_t cRZVariant::AsUint8() const { return mUint8; }
uint8_t *cRZVariant::AsUint8Ref() { return &mUint8; }
int8_t cRZVariant::AsSint8() const { return mSint8; }
int8_t *cRZVariant::AsSint8Ref() { return &mSint8; }
uint16_t cRZVariant::AsUint16() const { return mUint16; }
uint16_t *cRZVariant::AsUint16Ref() { return &mUint16; }
int16_t cRZVariant::AsSint16() const { return mSint16; }
int16_t *cRZVariant::AsSint16Ref() { return &mSint16; }
uint32_t cRZVariant::AsUint32() const { return mUint32; }
uint32_t *cRZVariant::AsUint32Ref() { return &mUint32; }
int32_t cRZVariant::AsSint32() const { return mSint32; }
int32_t *cRZVariant::AsSint32Ref() { return &mSint32; }
uint64_t cRZVariant::AsUint64() const { return mUint64; }
uint64_t *cRZVariant::AsUint64Ref() { return &mUint64; }
int64_t cRZVariant::AsSint64() const { return mSint64; }
int64_t *cRZVariant::AsSint64Ref() { return &mSint64; }
float cRZVariant::AsFloat32() const { return mFloat32; }
float *cRZVariant::AsFloat32Ref() { return &mFloat32; }
double cRZVariant::AsFloat64() const { return mFloat64; }
double *cRZVariant::AsFloat64Ref() { return &mFloat64; }
char cRZVariant::AsChar() const { return mChar; }
char *cRZVariant::AsCharRef() { return &mChar; }
uint16_t cRZVariant::AsRZUnicodeChar() const { return mRZUnicodeChar; }
uint16_t *cRZVariant::AsRZUnicodeCharRef() { return &mRZUnicodeChar; }
char cRZVariant::AsRZChar() const { return mRZChar; }
char *cRZVariant::AsRZCharRef() { return &mRZChar; }
void *cRZVariant::AsVoidPtr() const { return mVoidPtr; }
void *cRZVariant::AsVoidPtrValue() const { return mVoidPtr; }
cIGZUnknown *cRZVariant::AsUnknown() const { return mUnknown; }

// ---- arrays ----

void cRZVariant::StoreArray(const void *values, uint32_t count, uint16_t tag, uint32_t size, bool needValues)
{
    const uint32_t bytes = count * size; // wraps, as the original's shift
    if (mType == tag && mCount == count && mArray && (values || !needValues)) {
        std::memcpy(mArray, values, bytes);
        return;
    }
    Clear();
    mArray = ::operator new[](bytes);
    if (values) std::memcpy(mArray, values, bytes);
    mCount = count;
    mType = tag;
}

void cRZVariant::SetBoolArray(const uint8_t *values, uint32_t count) { StoreArray(values, count, 0x8001, 1); }
uint8_t *cRZVariant::GetBoolArray() { return static_cast<uint8_t *>(mArray); }
void cRZVariant::SetUint8Array(const uint8_t *values, uint32_t count) { StoreArray(values, count, 0x8002, 1); }
uint8_t *cRZVariant::GetUint8Array() { return static_cast<uint8_t *>(mArray); }
void cRZVariant::SetSint8Array(const int8_t *values, uint32_t count) { StoreArray(values, count, 0x8003, 1); }
int8_t *cRZVariant::GetSint8Array() { return static_cast<int8_t *>(mArray); }
void cRZVariant::SetUint16Array(const uint16_t *values, uint32_t count) { StoreArray(values, count, 0x8004, 2); }
uint16_t *cRZVariant::GetUint16Array() { return static_cast<uint16_t *>(mArray); }
void cRZVariant::SetSint16Array(const int16_t *values, uint32_t count) { StoreArray(values, count, 0x8005, 2); }
int16_t *cRZVariant::GetSint16Array() { return static_cast<int16_t *>(mArray); }
void cRZVariant::SetUint32Array(const uint32_t *values, uint32_t count) { StoreArray(values, count, 0x8006, 4); }
uint32_t *cRZVariant::GetUint32Array() { return static_cast<uint32_t *>(mArray); }
void cRZVariant::SetSint32Array(const int32_t *values, uint32_t count) { StoreArray(values, count, 0x8007, 4); }
int32_t *cRZVariant::GetSint32Array() { return static_cast<int32_t *>(mArray); }
void cRZVariant::SetUint64Array(const uint64_t *values, uint32_t count) { StoreArray(values, count, 0x8008, 8); }
uint64_t *cRZVariant::GetUint64Array() { return static_cast<uint64_t *>(mArray); }
void cRZVariant::SetSint64Array(const int64_t *values, uint32_t count) { StoreArray(values, count, 0x8009, 8); }
int64_t *cRZVariant::GetSint64Array() { return static_cast<int64_t *>(mArray); }
void cRZVariant::SetFloat32Array(const float *values, uint32_t count) { StoreArray(values, count, 0x800a, 4); }
float *cRZVariant::GetFloat32Array() { return static_cast<float *>(mArray); }
void cRZVariant::SetFloat64Array(const double *values, uint32_t count) { StoreArray(values, count, 0x800b, 8); }
double *cRZVariant::GetFloat64Array() { return static_cast<double *>(mArray); }
void cRZVariant::SetCharArray(const char *values, uint32_t count) { StoreArray(values, count, 0x800c, 1); }
char *cRZVariant::GetCharArray() { return static_cast<char *>(mArray); }
void cRZVariant::SetRZUnicodeCharArray(const uint16_t *values, uint32_t count) { StoreArray(values, count, 0x800d, 2); }
uint16_t *cRZVariant::GetRZUnicodeCharArray() { return static_cast<uint16_t *>(mArray); }
void cRZVariant::SetRZCharArray(const char *values, uint32_t count) { StoreArray(values, count, 0x800e, 1); }
char *cRZVariant::GetRZCharArray() { return static_cast<char *>(mArray); }
void cRZVariant::SetVoidPtrArray(void *const *values, uint32_t count) { StoreArray(values, count, 0x800f, 4); }
void **cRZVariant::GetVoidPtrArray() { return static_cast<void **>(mArray); }
void cRZVariant::SetVoidArray(const void *data, uint32_t size) { StoreArray(data, size, kTypeArray, 1); }
void *cRZVariant::GetVoidArray() { return mArray; }

// ---- conversions ----

bool cRZVariant::Convert(const cRZVariant *from, cRZVariant *to)
{
    const auto numeric = [](uint16_t tag) { return static_cast<uint16_t>(tag - 1) <= 0xd; };
    if (!numeric(from->mType) || !numeric(to->mType)) {
        // As text: ToString with no flags, then FromString's parse into `to`'s kind.
        LocalString text;
        return ToString(from, &text.s, 0, ' ') && FromString(&text.s, to);
    }
    const uint16_t kind = to->mType;
    if (kind == kTypeFloat64) return ToFloat64(from, to);
    cRZVariant temp;
    if (kind == kTypeFloat32) {
        ToFloat64(from, &temp);
        to->mFloat32 = static_cast<float>(temp.mFloat64);
        return true;
    }
    ToInt64(from, &temp);
    to->mRefCount = 0; // the original clears the reference count word too
    to->mCount = 0;
    const int64_t value = temp.mSint64;
    switch (kind) {
    case kTypeBool:
        to->mBool = value != 0;
        to->mType = kTypeBool;
        break;
    case kTypeUint8: case kTypeSint8: case kTypeChar: case kTypeRZChar: to->mUint8 = static_cast<uint8_t>(value); break;
    case kTypeUint16: case kTypeSint16: case kTypeRZUnicodeChar: to->mUint16 = static_cast<uint16_t>(value); break;
    case kTypeUint32: case kTypeSint32: to->mUint32 = static_cast<uint32_t>(value); break;
    case kTypeUint64: case kTypeSint64: to->mSint64 = value; break;
    default: to->mUint32 = 0; break; // not reached (the floats are above)
    }
    return true;
}

bool cRZVariant::ToFloat64(const cRZVariant *from, cRZVariant *to)
{
    to->mCount = 0;
    to->mRefCount = 0;
    to->mType = kTypeFloat64;
    double value;
    switch (from->mType) {
    case kTypeBool: case kTypeUint8: value = from->mUint8; break;
    case kTypeSint8: case kTypeChar: case kTypeRZChar: value = from->mSint8; break;
    case kTypeUint16: case kTypeRZUnicodeChar: value = from->mUint16; break;
    case kTypeSint16: value = from->mSint16; break;
    case kTypeUint32: value = from->mUint32; break;
    case kTypeSint32: value = from->mSint32; break;
    case kTypeUint64: value = static_cast<double>(from->mUint64); break;
    case kTypeSint64: value = static_cast<double>(from->mSint64); break;
    case kTypeFloat32: value = from->mFloat32; break;
    case kTypeFloat64: value = from->mFloat64; break;
    default: value = 0.0; break;
    }
    to->mFloat64 = value;
    return true;
}

bool cRZVariant::ToInt64(const cRZVariant *from, cRZVariant *to)
{
    to->mCount = 0;
    to->mRefCount = 0;
    int64_t value;
    uint16_t kind = kTypeSint64;
    switch (from->mType) {
    case kTypeBool: case kTypeUint8: value = from->mUint8; kind = kTypeUint64; break;
    case kTypeSint8: case kTypeChar: case kTypeRZChar: value = from->mSint8; break;
    case kTypeUint16: value = from->mUint16; kind = kTypeUint64; break;
    case kTypeSint16: value = from->mSint16; break;
    case kTypeUint32: value = from->mUint32; kind = kTypeUint64; break;
    case kTypeUint64: value = from->mSint64; kind = kTypeUint64; break;
    case kTypeSint64: value = from->mSint64; break;
    case kTypeFloat32: value = static_cast<int64_t>(from->mFloat32); break;
    case kTypeFloat64: value = static_cast<int64_t>(from->mFloat64); break;
    case kTypeRZUnicodeChar: value = from->mUint16; break;
    default: value = from->mSint32; break; // sint32, and the kinds with no integer (read as one anyway)
    }
    to->mSint64 = value;
    to->mType = kind;
    return true;
}

bool cRZVariant::ToString(const cRZVariant *from, cRZString *text, uint32_t flags, char separator)
{
    const bool hex = (flags & 1) != 0;
    const char *formatU8 = Text(hex ? kFormatHex8 : kFormatUnsigned);
    const char *formatS8 = Text(hex ? kFormatHex8 : kFormatSigned);
    const char *formatU16 = Text(hex ? kFormatHex16 : kFormatUnsigned);
    const char *formatS16 = Text(hex ? kFormatHex16 : kFormatSigned);
    const char *formatU32 = Text(hex ? kFormatHex32 : kFormatUnsigned);
    const char *formatS32 = Text(hex ? kFormatHex32 : kFormatSigned);
    cIGZString *face = text->AsInterface();
    std::string &out = text->mString;
    out.clear();
    switch (from->mType) {
    case kTypeEmpty: break;
    case kTypeBool:
        if (flags & 2) out.assign(Text(from->mBool ? kTextTrueWord : kTextFalseWord));
        else out.assign(Text(from->mBool ? kTextOne : kTextZero));
        break;
    case kTypeUint8: face->Sprintf(formatU8, uint32_t(from->mUint8)); break;
    case kTypeSint8: face->Sprintf(formatS8, int32_t(from->mSint8)); break;
    case kTypeUint16: face->Sprintf(formatU16, uint32_t(from->mUint16)); break;
    case kTypeSint16: face->Sprintf(formatS16, int32_t(from->mSint16)); break;
    case kTypeUint32: face->Sprintf(formatU32, from->mUint32); break;
    case kTypeSint32: face->Sprintf(formatS32, from->mSint32); break;
    case kTypeUint64: case kTypeSint64: face->Sprintf(Text(kFormatUint64), from->mUint64); break;
    case kTypeFloat32: case kTypeFloat64: {
        char digits[0x40];
        const double value = from->mType == kTypeFloat32 ? double(from->mFloat32) : from->mFloat64;
        nGZCommandParser::FormatDecimal(value, digits, sizeof(digits), 0x18);
        out.assign(digits, std::strlen(digits));
        break;
    }
    case kTypeChar: case kTypeRZChar: out = from->mChar; break;
    case kTypeRZUnicodeChar: out = from->mRZUnicodeChar <= 0xff ? char(from->mRZUnicodeChar) : '?'; break;
    case kTypeVoidPtr: FormatPointer(from->mVoidPtr, text); break;
    case kTypeUnknown: {
        cIGZString *string = nullptr;
        if (from->mUnknown && from->mUnknown->QueryInterface(kIIDString, reinterpret_cast<void **>(&string)))
            *face = *string;
        else
            FormatPointer(from->mUnknown, text);
        if (string) string->Release();
        break;
    }
    case 0x8001: {
        const uint8_t *values = static_cast<const uint8_t *>(from->mArray);
        for (uint32_t i = 0; i < from->mCount; ++i) {
            out.push_back(values[i] != 0 ? '1' : '0');
            if (i < from->mCount - 1) out.push_back(separator);
        }
        break;
    }
    case 0x8002: FormatElements<uint8_t, uint32_t>(from, formatU8, separator, text); break;
    case 0x8003: FormatElements<int8_t, int32_t>(from, formatS8, separator, text); break;
    case 0x8004: FormatElements<uint16_t, uint32_t>(from, formatU16, separator, text); break;
    case 0x8005: FormatElements<int16_t, int32_t>(from, formatS16, separator, text); break;
    case 0x8006: FormatElements<uint32_t, uint32_t>(from, formatU32, separator, text); break;
    case 0x8007: FormatElements<uint32_t, uint32_t>(from, formatS32, separator, text); break;
    case 0x8008: case 0x8009: FormatElements<uint64_t, uint64_t>(from, Text(kFormatUint64), separator, text); break;
    case 0x800a: FormatElements<float, double>(from, Text(kFormatFloat), separator, text); break;
    case 0x800b: FormatElements<double, double>(from, Text(kFormatFloat), separator, text); break;
    case 0x800c: case 0x800e:
        if (from->mArray) out.assign(static_cast<const char *>(from->mArray), from->mCount);
        else out.clear();
        break;
    case 0x800d:
        if (from->mArray) {
            const std::u16string wide(static_cast<const char16_t *>(from->mArray), from->mCount);
            appbase::AssignNarrowed(&wide, text);
        } else {
            out.clear();
        }
        break;
    case 0x800f: out.resize(0); break;
    case 0x8010: {
        // A pointer "array" (only the bytes constructor makes one): its first pointer, as for an unknown.
        void *const *values = static_cast<void *const *>(from->mArray);
        if (values && from->mCount != 0) {
            cIGZUnknown *first = static_cast<cIGZUnknown *>(values[0]);
            cIGZString *string = nullptr;
            if (first && first->QueryInterface(kIIDString, reinterpret_cast<void **>(&string)))
                *face = *string;
            else
                FormatPointer(values[0], text);
            if (string) string->Release();
        } else {
            FormatPointer(values, text);
        }
        break;
    }
    default: face->Resize(0); break; // the tags with no kind (0x8000 too)
    }
    return true;
}

bool cRZVariant::FromString(const cRZString *text, cRZVariant *to)
{
    enum Kind { kSigned = 1, kUnsigned = 2, kDouble = 3 };
    const cIGZString *face = text->AsInterface();
    const std::string &chars = text->mString;
    const auto startsWith = [&](const char *literal) { return face->Find(Text(literal), 0, true) == 0; };
    Kind kind;
    uint64_t u = 0;
    int64_t s = 0;
    double d = 0.0;
    const size_t size = chars.size();
    if (size == 1 && startsWith(kTextT)) {
        kind = kUnsigned;
        u = 1;
    } else if (size == 1 && startsWith(kTextF)) {
        kind = kUnsigned;
    } else if (size == 4 && startsWith(kTextTrue)) {
        kind = kUnsigned;
        u = 1;
    } else if (size == 5 && startsWith(kTextFalse)) {
        kind = kUnsigned;
    } else if (size >= 3 && std::isxdigit(static_cast<int>(chars[2])) && startsWith(kTextHexPrefix)) {
        kind = kUnsigned;
        u = ParseHex64(chars.c_str());
    } else {
        // What the characters look like: digits, a '.' or '-' before and after an exponent, an 'e' after a digit.
        bool digit = false, exponent = false, dot = false, dotAfter = false, minus = false, minusAfter = false;
        bool real = false;
        const int length = static_cast<int>(size);
        for (int i = 0; i < length; ++i) {
            const char c = chars[i];
            if (std::isdigit(static_cast<int>(c))) digit = true;
            if (c == '.') {
                if (exponent) {
                    if (dotAfter) break;
                    dotAfter = true;
                } else {
                    if (dot) {
                        real = true; // a second '.': parsed as a double at once
                        break;
                    }
                    dot = true;
                }
            }
            if (c == '-') {
                if (exponent) {
                    if (minusAfter) break;
                    minusAfter = true;
                } else {
                    if (minus) break;
                    minus = true;
                }
            }
            if (c == 'e' || c == 'E') {
                if (!digit || exponent) break;
                exponent = true;
            }
        }
        if (real || dot || exponent) {
            kind = kDouble;
            d = ParseDouble(chars.c_str());
        } else if (minus) {
            kind = kSigned;
            s = static_cast<int64_t>(ParseInt64(chars.c_str()));
        } else {
            kind = kUnsigned;
            u = ParseInt64(chars.c_str());
        }
    }

    // The value as each width (the integer kinds' low bits; a double truncated as the original's conversions do).
    const auto low32 = [&](int32_t fromDouble) -> uint32_t {
        return kind == kUnsigned ? uint32_t(u) : kind == kSigned ? uint32_t(s) : uint32_t(fromDouble);
    };
    const auto all64 = [&](uint64_t fromDouble) -> uint64_t {
        return kind == kUnsigned ? u : kind == kSigned ? uint64_t(s) : fromDouble;
    };
    const auto asDouble = [&]() -> double {
        return kind == kUnsigned ? static_cast<double>(u) : kind == kSigned ? static_cast<double>(s) : d;
    };
    const auto asFloat = [&]() -> float {
        return kind == kDouble ? static_cast<float>(d) : static_cast<float>(asDouble());
    };
    const auto isTrue = [&]() -> uint8_t {
        return kind == kUnsigned ? u != 0 : kind == kSigned ? s != 0 : d != 0.0; // NaN is true
    };
    // cvttsd2si (out of range: 0x80000000) and the CRT's 64-bit truncation.
    const int32_t truncated = static_cast<int32_t>(d);
    const uint32_t truncated64 = static_cast<uint32_t>(static_cast<int64_t>(d));

    switch (to->mType) {
    case kTypeBool: to->SetValBool(isTrue()); break;
    case kTypeUint8: to->SetValUint8(static_cast<uint8_t>(low32(truncated))); break;
    case kTypeSint8: to->SetValSint8(static_cast<int8_t>(low32(truncated))); break;
    case kTypeUint16: to->SetValUint16(static_cast<uint16_t>(low32(truncated))); break;
    case kTypeSint16: to->SetValSint16(static_cast<int16_t>(low32(truncated))); break;
    case kTypeUint32: to->SetValUint32(low32(static_cast<int32_t>(truncated64))); break;
    case kTypeSint32: to->SetValSint32(static_cast<int32_t>(low32(truncated))); break;
    case kTypeUint64: to->SetValUint64(all64(kind == kDouble ? DoubleToUint64(d) : 0)); break;
    case kTypeSint64: to->SetValSint64(static_cast<int64_t>(all64(kind == kDouble ? uint64_t(static_cast<int64_t>(d)) : 0))); break;
    case kTypeFloat32: to->SetValFloat32(asFloat()); break;
    case kTypeFloat64: to->SetValFloat64(asDouble()); break;
    // ORIGINAL-BUG: the character kinds get 0, not the parsed value.
    case kTypeChar: to->SetValChar(0); break;
    case kTypeRZUnicodeChar: to->SetValRZUnicodeChar(0); break;
    case kTypeRZChar: to->SetValRZChar(0); break;
    case kTypeVoidPtr: ParsePointer(text, &to->mVoidPtr); break;
    case kTypeUnknown: {
        cIGZString *string = nullptr;
        if (to->mUnknown && to->mUnknown->QueryInterface(kIIDString, reinterpret_cast<void **>(&string)))
            *string = *face;
        else
            ParsePointer(text, &to->mUnknown); // ORIGINAL-BUG: the old unknown is not released (nor the new AddRef'd)
        if (string) string->Release();
        break;
    }
    case 0x8001: { uint8_t v = isTrue(); to->SetBoolArray(&v, 1); break; }
    case 0x8002: { uint8_t v = static_cast<uint8_t>(low32(truncated)); to->SetUint8Array(&v, 1); break; }
    case 0x8003: { int8_t v = static_cast<int8_t>(low32(truncated)); to->SetSint8Array(&v, 1); break; }
    case 0x8004: { uint16_t v = static_cast<uint16_t>(low32(truncated)); to->SetUint16Array(&v, 1); break; }
    case 0x8005: { int16_t v = static_cast<int16_t>(low32(truncated)); to->SetSint16Array(&v, 1); break; }
    case 0x8006: { uint32_t v = low32(static_cast<int32_t>(truncated64)); to->SetUint32Array(&v, 1); break; }
    case 0x8007: { int32_t v = static_cast<int32_t>(low32(truncated)); to->SetSint32Array(&v, 1); break; }
    case 0x8008: { uint64_t v = all64(kind == kDouble ? DoubleToUint64(d) : 0); to->SetUint64Array(&v, 1); break; }
    case 0x8009: {
        int64_t v = static_cast<int64_t>(all64(kind == kDouble ? uint64_t(static_cast<int64_t>(d)) : 0));
        to->SetSint64Array(&v, 1);
        break;
    }
    case 0x800a: { float v = asFloat(); to->SetFloat32Array(&v, 1); break; }
    case 0x800b: { double v = asDouble(); to->SetFloat64Array(&v, 1); break; }
    case 0x800c: {
        const std::string copy = chars;
        to->SetCharArray(copy.c_str(), static_cast<uint32_t>(copy.size()));
        break;
    }
    case 0x800d: {
        // Each character widened with its sign (0x80..0xff become 0xff80..0xffff).
        std::u16string wide;
        for (int i = 0; i < static_cast<int>(chars.size()); ++i)
            wide.push_back(static_cast<char16_t>(static_cast<int16_t>(chars[i])));
        to->SetRZUnicodeCharArray(reinterpret_cast<const uint16_t *>(wide.c_str()), static_cast<uint32_t>(wide.size()));
        break;
    }
    case 0x800e: to->SetRZCharArray(chars.c_str(), static_cast<uint32_t>(chars.size())); break;
    case 0x800f: to->SetValVoidPtr(nullptr); break;
    case 0x8010: to->SetValUnknown(nullptr); break;
    default:
        to->SetValSint32(0); // empty, and the tags with no kind
        return false;
    }
    return true;
}

// ---- adapters ----

cRZVariant *__fastcall cRZVariantAdapters::Construct(void *memory, void *) { return ::new (memory) cRZVariant(); }
cRZVariant *__fastcall cRZVariantAdapters::ConstructCopy(void *memory, void *, const cRZVariant *other)
{
    return ::new (memory) cRZVariant(*other);
}
cRZVariant *__fastcall cRZVariantAdapters::ConstructText(void *memory, void *, const cIGZString *text)
{
    return ::new (memory) cRZVariant(*text);
}
cRZVariant *__fastcall cRZVariantAdapters::ConstructVariant(void *memory, void *, const cIGZVariant *other)
{
    return ::new (memory) cRZVariant(*other);
}
cRZVariant *__fastcall cRZVariantAdapters::ConstructSint8(void *memory, void *, int8_t value) { return ::new (memory) cRZVariant(value); }
cRZVariant *__fastcall cRZVariantAdapters::ConstructUint8(void *memory, void *, uint8_t value) { return ::new (memory) cRZVariant(value); }
cRZVariant *__fastcall cRZVariantAdapters::ConstructSint16(void *memory, void *, int16_t value) { return ::new (memory) cRZVariant(value); }
cRZVariant *__fastcall cRZVariantAdapters::ConstructUint16(void *memory, void *, uint16_t value) { return ::new (memory) cRZVariant(value); }
cRZVariant *__fastcall cRZVariantAdapters::ConstructSint32(void *memory, void *, int32_t value) { return ::new (memory) cRZVariant(value); }
cRZVariant *__fastcall cRZVariantAdapters::ConstructUint32(void *memory, void *, uint32_t value) { return ::new (memory) cRZVariant(value); }
cRZVariant *__fastcall cRZVariantAdapters::ConstructFloat32(void *memory, void *, float value) { return ::new (memory) cRZVariant(value); }
cRZVariant *__fastcall cRZVariantAdapters::ConstructFloat64(void *memory, void *, double value) { return ::new (memory) cRZVariant(value); }
cRZVariant *__fastcall cRZVariantAdapters::ConstructUnknown(void *memory, void *, cIGZUnknown *value)
{
    return ::new (memory) cRZVariant(value);
}
cRZVariant *__fastcall cRZVariantAdapters::ConstructBytes(void *memory, void *, const void *data, uint32_t size, uint32_t type)
{
    return ::new (memory) cRZVariant(data, size, type);
}
cRZVariant *__fastcall cRZVariantAdapters::ConstructChars(void *memory, void *, const char *text)
{
    return ::new (memory) cRZVariant(text);
}
cRZVariant *__fastcall cRZVariantAdapters::ConstructVoidPtr(void *memory, void *, void *value) { return ::new (memory) cRZVariant(value); }
cRZVariant *__fastcall cRZVariantAdapters::ConstructSint64(void *memory, void *, int64_t value) { return ::new (memory) cRZVariant(value); }
cRZVariant *__fastcall cRZVariantAdapters::ConstructUint64(void *memory, void *, uint64_t value) { return ::new (memory) cRZVariant(value); }
cRZVariant *__fastcall cRZVariantAdapters::ConstructBool(void *memory, void *, bool value) { return ::new (memory) cRZVariant(value); }

void __fastcall cRZVariantAdapters::Destruct(cRZVariant *self, void *) { self->cRZVariant::~cRZVariant(); }

cRZVariant *__fastcall cRZVariantAdapters::DeletingDestructor(cRZVariant *self, void *, uint32_t flags)
{
    self->cRZVariant::~cRZVariant();
    if (flags & 1) ::operator delete(self, sizeof(cRZVariant));
    return self;
}

cRZVariant *__fastcall cRZVariantAdapters::Assign(cRZVariant *self, void *, const cRZVariant *other)
{
    [[msvc::forceinline_calls]] return &self->cRZVariant::Assign(*other);
}

#define RZV_ADAPT(R, Name, Params, Args)                                                                               \
    R __fastcall cRZVariantAdapters::Name Params { [[msvc::forceinline_calls]] return self->cRZVariant::Name Args; }

RZV_ADAPT(bool, QueryInterface, (cRZVariant *self, void *, uint32_t riid, void **out), (riid, out))
RZV_ADAPT(uint32_t, AddRef, (cRZVariant *self, void *), ())
RZV_ADAPT(uint32_t, Release, (cRZVariant *self, void *), ())
RZV_ADAPT(uint32_t, GetType, (cRZVariant *self, void *), ())
RZV_ADAPT(bool, IsUndefined, (cRZVariant *self, void *), ())
RZV_ADAPT(bool, Erase, (cRZVariant *self, void *), ())
RZV_ADAPT(bool, CopyFrom, (cRZVariant *self, void *, const cIGZVariant *other), (*other))
RZV_ADAPT(bool, CreateCopy, (cRZVariant *self, void *, cIGZVariant **out), (out))
RZV_ADAPT(uint8_t, ValBool, (cRZVariant *self, void *), ())
RZV_ADAPT(bool, GetValBool, (cRZVariant *self, void *, uint8_t *out), (out))
RZV_ADAPT(uint8_t, ValUint8, (cRZVariant *self, void *), ())
RZV_ADAPT(bool, GetValUint8, (cRZVariant *self, void *, uint8_t *out), (out))
RZV_ADAPT(int8_t, ValSint8, (cRZVariant *self, void *), ())
RZV_ADAPT(bool, GetValSint8, (cRZVariant *self, void *, int8_t *out), (out))
RZV_ADAPT(uint16_t, ValUint16, (cRZVariant *self, void *), ())
RZV_ADAPT(bool, GetValUint16, (cRZVariant *self, void *, uint16_t *out), (out))
RZV_ADAPT(int16_t, ValSint16, (cRZVariant *self, void *), ())
RZV_ADAPT(bool, GetValSint16, (cRZVariant *self, void *, int16_t *out), (out))
RZV_ADAPT(uint32_t, ValUint32, (cRZVariant *self, void *), ())
RZV_ADAPT(bool, GetValUint32, (cRZVariant *self, void *, uint32_t *out), (out))
RZV_ADAPT(int32_t, ValSint32, (cRZVariant *self, void *), ())
RZV_ADAPT(bool, GetValSint32, (cRZVariant *self, void *, int32_t *out), (out))
RZV_ADAPT(uint64_t, ValUint64, (cRZVariant *self, void *), ())
RZV_ADAPT(bool, GetValUint64, (cRZVariant *self, void *, uint64_t *out), (out))
RZV_ADAPT(int64_t, ValSint64, (cRZVariant *self, void *), ())
RZV_ADAPT(bool, GetValSint64, (cRZVariant *self, void *, int64_t *out), (out))
RZV_ADAPT(float, ValFloat32, (cRZVariant *self, void *), ())
RZV_ADAPT(bool, GetValFloat32, (cRZVariant *self, void *, float *out), (out))
RZV_ADAPT(double, ValFloat64, (cRZVariant *self, void *), ())
RZV_ADAPT(bool, GetValFloat64, (cRZVariant *self, void *, double *out), (out))
RZV_ADAPT(char, ValChar, (cRZVariant *self, void *), ())
RZV_ADAPT(bool, GetValChar, (cRZVariant *self, void *, char *out), (out))
RZV_ADAPT(uint16_t, ValRZUnicodeChar, (cRZVariant *self, void *), ())
RZV_ADAPT(bool, GetValRZUnicodeChar, (cRZVariant *self, void *, uint16_t *out), (out))
RZV_ADAPT(char, ValRZChar, (cRZVariant *self, void *), ())
RZV_ADAPT(bool, GetValRZChar, (cRZVariant *self, void *, char *out), (out))
RZV_ADAPT(void *, ValVoidPtr, (cRZVariant *self, void *), ())
RZV_ADAPT(bool, GetValVoidPtr, (cRZVariant *self, void *, void **out), (out))
RZV_ADAPT(bool, CreateValString, (cRZVariant *self, void *, cIGZString **out, uint32_t flags, char separator),
          (out, flags, separator))
RZV_ADAPT(bool, SetFromString,
          (cRZVariant *self, void *, const cIGZString *text, uint32_t type, uint32_t count, char separator),
          (*text, type, count, separator))
RZV_ADAPT(bool, GetValVoid, (cRZVariant *self, void *, void *out, uint32_t *size), (out, size))
RZV_ADAPT(void, SetValVoid, (cRZVariant *self, void *, const void *data, uint32_t size), (data, size))
RZV_ADAPT(cIGZUnknown *, ValUnknown, (cRZVariant *self, void *), ())
RZV_ADAPT(bool, IsEqual, (cRZVariant *self, void *, const cIGZVariant *other), (*other))
RZV_ADAPT(bool, IsNotEqual, (cRZVariant *self, void *, const cIGZVariant *other), (*other))
RZV_ADAPT(uint64_t, AsUint64, (cRZVariant *self, void *), ())
RZV_ADAPT(void, SetBoolArray, (cRZVariant *self, void *, const uint8_t *values, uint32_t count), (values, count))
RZV_ADAPT(void, SetUint8Array, (cRZVariant *self, void *, const uint8_t *values, uint32_t count), (values, count))
RZV_ADAPT(void, SetSint8Array, (cRZVariant *self, void *, const int8_t *values, uint32_t count), (values, count))
RZV_ADAPT(void, SetUint16Array, (cRZVariant *self, void *, const uint16_t *values, uint32_t count), (values, count))
RZV_ADAPT(void, SetSint16Array, (cRZVariant *self, void *, const int16_t *values, uint32_t count), (values, count))
RZV_ADAPT(void, SetUint32Array, (cRZVariant *self, void *, const uint32_t *values, uint32_t count), (values, count))
RZV_ADAPT(void, SetSint32Array, (cRZVariant *self, void *, const int32_t *values, uint32_t count), (values, count))
RZV_ADAPT(void, SetUint64Array, (cRZVariant *self, void *, const uint64_t *values, uint32_t count), (values, count))
RZV_ADAPT(void, SetSint64Array, (cRZVariant *self, void *, const int64_t *values, uint32_t count), (values, count))
RZV_ADAPT(void, SetFloat32Array, (cRZVariant *self, void *, const float *values, uint32_t count), (values, count))
RZV_ADAPT(void, SetFloat64Array, (cRZVariant *self, void *, const double *values, uint32_t count), (values, count))
RZV_ADAPT(void, SetCharArray, (cRZVariant *self, void *, const char *values, uint32_t count), (values, count))
RZV_ADAPT(void, SetRZUnicodeCharArray, (cRZVariant *self, void *, const uint16_t *values, uint32_t count), (values, count))
RZV_ADAPT(void, SetRZCharArray, (cRZVariant *self, void *, const char *values, uint32_t count), (values, count))
RZV_ADAPT(void, SetVoidPtrArray, (cRZVariant *self, void *, void *const *values, uint32_t count), (values, count))
RZV_ADAPT(void, SetVoidArray, (cRZVariant *self, void *, const void *data, uint32_t size), (data, size))
RZV_ADAPT(void, Clear, (cRZVariant *self, void *), ())
RZV_ADAPT(void, StoreBool, (cRZVariant *self, void *, uint8_t value), (value))
RZV_ADAPT(void, StoreUint8, (cRZVariant *self, void *, uint8_t value), (value))
RZV_ADAPT(void, StoreSint8, (cRZVariant *self, void *, int8_t value), (value))
RZV_ADAPT(void, StoreUint16, (cRZVariant *self, void *, uint16_t value), (value))
RZV_ADAPT(void, StoreSint16, (cRZVariant *self, void *, int16_t value), (value))
RZV_ADAPT(void, StoreUint32, (cRZVariant *self, void *, uint32_t value), (value))
RZV_ADAPT(void, StoreSint32, (cRZVariant *self, void *, int32_t value), (value))
RZV_ADAPT(void, StoreUint64, (cRZVariant *self, void *, uint64_t value), (value))
RZV_ADAPT(void, StoreSint64, (cRZVariant *self, void *, int64_t value), (value))
RZV_ADAPT(void, StoreFloat32, (cRZVariant *self, void *, float value), (value))
RZV_ADAPT(void, StoreFloat64, (cRZVariant *self, void *, double value), (value))
RZV_ADAPT(void, StoreChar, (cRZVariant *self, void *, char value), (value))
RZV_ADAPT(void, StoreRZUnicodeChar, (cRZVariant *self, void *, uint16_t value), (value))
RZV_ADAPT(void, StoreRZChar, (cRZVariant *self, void *, char value), (value))
RZV_ADAPT(void, StoreVoidPtr, (cRZVariant *self, void *, void *value), (value))
RZV_ADAPT(void, StoreUnknown, (cRZVariant *self, void *, cIGZUnknown *value), (value))
RZV_ADAPT(bool, TextOf, (cRZVariant *self, void *, cIGZString *text, uint32_t flags, char separator), (*text, flags, separator))
RZV_ADAPT(void, StoreText, (cRZVariant *self, void *, const cIGZString *text), (*text))
RZV_ADAPT(bool, QueryUnknown, (cRZVariant *self, void *, uint32_t iid, void **out), (iid, out))
RZV_ADAPT(bool, ParseScalar, (cRZVariant *self, void *, const cIGZString *text, uint32_t type), (*text, type))
RZV_ADAPT(bool, ParseArray, (cRZVariant *self, void *, const cIGZString *text, uint32_t type, uint32_t count, char separator),
          (*text, type, count, separator))

#undef RZV_ADAPT

cIGZVariant *__fastcall cRZVariantAdapters::AssignFrom(cRZVariant *self, void *, const cIGZVariant *other)
{
    [[msvc::forceinline_calls]] return &self->cRZVariant::AssignFrom(*other);
}
