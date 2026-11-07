#pragma once
// cRZVariant: a tagged value (cIGZVariant), used by properties, messages and serialization. Standalone shape
// (docs/standalone-shape.md): a C++ class deriving from cIGZVariant (+0) and tagRZVariant (+4), as the original's RTTI;
// the compiler builds the vtable. The game's constructors, destructor and assignment are hooked to adapters that build or
// destroy our class, and every slot body and helper the game reaches directly is hooked to an adapter too (objects some
// other group's code still builds with the game's vtable, such as UtilObjectB::Construct 0x9c7b3d's, reach those).
// RVAs are 1.18.0.187; the game's vtable is 0xe6180c (133 slots).
//
// Tags (mType): 0 empty; 1 bool (byte); 2 uint8; 3 sint8; 4 uint16; 5 sint16; 6 uint32; 7 sint32; 8 uint64; 9 sint64;
// 0xa float32; 0xb float64; 0xc char; 0xd RZUnicodeChar (uint16); 0xe RZChar (char); 0xf void pointer; 0x10 unknown (a
// cIGZUnknown* the variant holds a reference on). The top bit 0x8000 marks an array of the kind in the low bits (0x8000
// itself: untyped bytes), owned (new[] / delete[]) and mCount elements long. Any other tag holds nothing owned.
//
// Conversions: a typed getter (GetValX) on a variant of another kind converts through a temporary of kind X (Convert):
// integers and floats directly between the kinds 1..0xe; anything else as text (ToString, then FromString's parse).
#include "game/owned.h"
#include "rz/cIGZVariant.h"

#include <cstddef>
#include <cstdint>

class cIGZString;
class cRZString;

#pragma pack(push, 4)
// The value (the original's tagRZVariant, at +4 in cRZVariant). Packed to 4: the original's payload starts at +4.
struct tagRZVariant {
    union {
        uint8_t mPayload[8];
        uint8_t mBool;
        uint8_t mUint8;
        int8_t mSint8;
        uint16_t mUint16;
        int16_t mSint16;
        uint32_t mUint32;
        int32_t mSint32;
        uint64_t mUint64;
        int64_t mSint64;
        float mFloat32;
        double mFloat64;
        char mChar;
        uint16_t mRZUnicodeChar;
        char mRZChar;
        void *mVoidPtr;
        cIGZUnknown *mUnknown;
        void *mArray; // the arrays' buffer
    };
    uint16_t mRefCount; // +8 (+0xc in the variant)
    uint16_t mType;     // +0xa (+0xe)
    uint32_t mCount;    // +0xc (+0x10): the arrays' element count (bytes for 0x8000)
};
#pragma pack(pop)

static_assert(sizeof(tagRZVariant) == 0x10, "tagRZVariant size");

class cRZVariant : public cIGZVariant, public tagRZVariant {
public:
    static constexpr uint32_t kVTable = 0xe6180c;
    static constexpr uint16_t kTypeEmpty = 0;
    static constexpr uint16_t kTypeBool = 1;
    static constexpr uint16_t kTypeUint8 = 2;
    static constexpr uint16_t kTypeSint8 = 3;
    static constexpr uint16_t kTypeUint16 = 4;
    static constexpr uint16_t kTypeSint16 = 5;
    static constexpr uint16_t kTypeUint32 = 6;
    static constexpr uint16_t kTypeSint32 = 7;
    static constexpr uint16_t kTypeUint64 = 8;
    static constexpr uint16_t kTypeSint64 = 9;
    static constexpr uint16_t kTypeFloat32 = 0xA;
    static constexpr uint16_t kTypeFloat64 = 0xB;
    static constexpr uint16_t kTypeChar = 0xC;
    static constexpr uint16_t kTypeRZUnicodeChar = 0xD;
    static constexpr uint16_t kTypeRZChar = 0xE;
    static constexpr uint16_t kTypeVoidPtr = 0xF;
    static constexpr uint16_t kTypeUnknown = 0x10;
    static constexpr uint16_t kTypeArray = 0x8000; // flag; alone: untyped bytes
    static constexpr uint16_t kTypeBuffer = 0x8000;

    // Constructors: every field zeroed (reference count 0), then the value.
    cRZVariant();                                        // 0x9c7c75
    cRZVariant(const cRZVariant &other);                 // 0x9c79da: then Assign(other)
    explicit cRZVariant(const cIGZString &text);         // 0x9c7a04: an RZChar array of its text (Strlen, then Data)
    explicit cRZVariant(const cIGZVariant &other);       // 0x9c7a40: then AssignFrom (slot 66)
    explicit cRZVariant(int8_t value);                   // 0x9c7a6a
    explicit cRZVariant(uint8_t value);                  // 0x9c7a94
    explicit cRZVariant(int16_t value);                  // 0x9c7abe
    explicit cRZVariant(uint16_t value);                 // 0x9c7aea
    explicit cRZVariant(int32_t value);                  // 0x9c7b16
    explicit cRZVariant(uint32_t value);                 // 0x9c7b3d (the game's is UtilObjectB::Construct's hook)
    explicit cRZVariant(float value);                    // 0x9c7b64
    explicit cRZVariant(double value);                   // 0x9c7b8f
    explicit cRZVariant(cIGZUnknown *value);             // 0x9c7bb6: SetValUnknown's helper (AddRef)
    // 0x9c7be0: `size` bytes copied (SetVoidArray), then the tag is (type | 0x8000) whatever the type is.
    cRZVariant(const void *data, uint32_t size, uint32_t type);
    explicit cRZVariant(const char *text);               // 0x9c7c17: a Char array of the text (strlen)
    explicit cRZVariant(void *value);                    // 0x9c7c4e
    explicit cRZVariant(int64_t value);                  // 0x9c7c8e
    explicit cRZVariant(uint64_t value);                 // 0x9c7cb8
    explicit cRZVariant(bool value);                     // 0x9c7ce2
    ~cRZVariant() override;                              // 0x9c7d0c (slot 132: 0x9c8603)

    // 0x9c7d18 (non-virtual, ret 4): the other's value by its raw fields (scalars through the setter helpers, arrays
    // through this variant's own array slots, other tags erase). Returns this.
    cRZVariant &Assign(const cRZVariant &other);
    cRZVariant &operator=(const cRZVariant &other) { return Assign(other); }

    bool QueryInterface(uint32_t riid, void **out) override;  // 0: 0x9ca68f (ids 1 and 0x60fd4d2b)
    uint32_t AddRef() override;                                // 1: 0x9c8622 (16-bit count)
    uint32_t Release() override;                               // 2: 0x9cad76
    uint32_t GetType() const override;                         // 3: 0x9c9f50
    uint32_t GetCount() const override;                        // 4: 0x149cf0 (folded)
    bool IsUndefined() const override;                         // 5: 0x9ca686
    bool Erase() override;                                     // 6: 0x9c9f48
    bool CopyFrom(const cIGZVariant &other) override;          // 7: 0x9c9a1e
    bool CreateCopy(cIGZVariant **out) override;               // 8: 0x9c9e86
    bool CanGetVal(uint32_t type) const override;              // 9: 0x148260 (folded)

    uint8_t ValBool() override;                                // 10: 0x9c9fa8
    bool GetValBool(uint8_t *out) override;                    // 11: 0x9c9f55
    void SetValBool(uint8_t value) override;                   // 12: 0x9cb3fb -> 0x9cb85f
    uint8_t ValUint8() override;                               // 13: 0x9ca569
    bool GetValUint8(uint8_t *out) override;                   // 14: 0x9ca57f
    void SetValUint8(uint8_t value) override;                  // 15: 0x9cb476 -> 0x9cbac6
    int8_t ValSint8() override;                                // 16: 0x9ca39d
    bool GetValSint8(int8_t *out) override;                    // 17: 0x9ca3b3
    void SetValSint8(int8_t value) override;                   // 18: 0x9cb452 -> 0x9cb9ec
    uint16_t ValUint16() override;                             // 19: 0x9ca414
    bool GetValUint16(uint16_t *out) override;                 // 20: 0x9ca42b
    void SetValUint16(uint16_t value) override;                // 21: 0x9cb45c -> 0x9cba3f
    int16_t ValSint16() override;                              // 22: 0x9ca21f
    bool GetValSint16(int16_t *out) override;                  // 23: 0x9ca236
    void SetValSint16(int16_t value) override;                 // 24: 0x9cb438 -> 0x9cb965
    uint32_t ValUint32() override;                             // 25: 0x9ca481
    bool GetValUint32(uint32_t *out) override;                 // 26: 0x9ca497
    void SetValUint32(uint32_t value) override;                // 27: 0x9cb461 -> 0x9cba6b
    int32_t ValSint32() override;                              // 28: 0x9ca29b
    bool GetValSint32(int32_t *out) override;                  // 29: 0x9ca2b1
    void SetValSint32(int32_t value) override;                 // 30: 0x9cb43d -> 0x9cb991
    uint64_t ValUint64() override;                             // 31: 0x9ca4e9
    bool GetValUint64(uint64_t *out) override;                 // 32: 0x9ca50a
    void SetValUint64(uint64_t value) override;                // 33: 0x9cb466 -> 0x9cba95
    int64_t ValSint64() override;                              // 34: 0x9ca30d
    bool GetValSint64(int64_t *out) override;                  // 35: 0x9ca32e
    void SetValSint64(int64_t value) override;                 // 36: 0x9cb442 -> 0x9cb9bb
    float ValFloat32() override;                               // 37: 0x9ca033
    bool GetValFloat32(float *out) override;                   // 38: 0x9ca04c
    void SetValFloat32(float value) override;                  // 39: 0x9cb405 -> 0x9cb8b3
    double ValFloat64() override;                              // 40: 0x9ca0a8
    bool GetValFloat64(double *out) override;                  // 41: 0x9ca0c6
    void SetValFloat64(double value) override;                 // 42: 0x9cb419 -> 0x9cb8e1
    char ValChar() override;                                   // 43: 0x9c9fbe
    bool GetValChar(char *out) override;                       // 44: 0x9c9fd7
    void SetValChar(char value) override;                      // 45: 0x9cb400 -> 0x9cb889
    uint16_t ValRZUnicodeChar() override;                      // 46: 0x9ca1a0
    bool GetValRZUnicodeChar(uint16_t *out) override;          // 47: 0x9ca1ba
    void SetValRZUnicodeChar(uint16_t value) override;         // 48: 0x9cb433 -> 0x9cb939
    char ValRZChar() override;                                 // 49: 0x9ca12b
    bool GetValRZChar(char *out) override;                     // 50: 0x9ca144
    void SetValRZChar(char value) override;                    // 51: 0x9cb42e -> 0x9cb90f
    void *ValVoidPtr() override;                               // 52: 0x9ca61b
    bool GetValVoidPtr(void **out) override;                   // 53: 0x9ca634
    void SetValVoidPtr(void *value) override;                  // 54: 0x9cb4dc -> 0x9cbb36

    bool CreateValString(cIGZString **out, uint32_t flags, char separator) override;          // 55: 0x9c9eb6
    bool GetValString(cIGZString &text, uint32_t flags, char separator) override;             // 56: 0x9ca40f -> 0x9cb791
    void SetValString(const cIGZString &text) override;                                       // 57: 0x9cb457 -> 0x9cba16
    bool SetFromString(const cIGZString &text, uint32_t type, uint32_t count, char separator) override; // 58: 0x9cb3af
    bool GetValVoid(void *out, uint32_t *size) override;                                      // 59: 0x9ca5e4
    void SetValVoid(const void *data, uint32_t size) override;                                // 60: 0x9cb47b
    cIGZUnknown *ValUnknown() override;                                                       // 61: 0x9ca5d1
    bool GetValUnknown(uint32_t iid, void **out) override;                                    // 62: 0x9ca5df -> 0x9cb848
    void SetValUnknown(cIGZUnknown *value) override;                                          // 63: 0x9ca85d -> 0x9cbaf0
    bool IsEqual(const cIGZVariant &other) override;                                          // 64: 0x9c804e
    bool IsNotEqual(const cIGZVariant &other) override;                                       // 65: 0x9c85ee
    cIGZVariant &AssignFrom(const cIGZVariant &other) override;                               // 66: 0x9c803c

    // 67-96: the payload as each kind, no tag check (the folded getters: 0x1458b0, 0x18ea60, 0x4db550, 0x147a30,
    // 0x559920; 0x9c862f for 81 / 83, 0x9c862b for 87).
    uint8_t AsBool() const override;
    uint8_t *AsBoolRef() override;
    uint8_t AsUint8() const override;
    uint8_t *AsUint8Ref() override;
    int8_t AsSint8() const override;
    int8_t *AsSint8Ref() override;
    uint16_t AsUint16() const override;
    uint16_t *AsUint16Ref() override;
    int16_t AsSint16() const override;
    int16_t *AsSint16Ref() override;
    uint32_t AsUint32() const override;
    uint32_t *AsUint32Ref() override;
    int32_t AsSint32() const override;
    int32_t *AsSint32Ref() override;
    uint64_t AsUint64() const override;
    uint64_t *AsUint64Ref() override;
    int64_t AsSint64() const override;
    int64_t *AsSint64Ref() override;
    float AsFloat32() const override;
    float *AsFloat32Ref() override;
    double AsFloat64() const override;
    double *AsFloat64Ref() override;
    char AsChar() const override;
    char *AsCharRef() override;
    uint16_t AsRZUnicodeChar() const override;
    uint16_t *AsRZUnicodeCharRef() override;
    char AsRZChar() const override;
    char *AsRZCharRef() override;
    void *AsVoidPtr() const override;
    void *AsVoidPtrValue() const override;

    // 97-128: the arrays (odd: copy in, 0x9ca6b3 ... 0x9caca1; even: the buffer, folded 0x147a30).
    void SetBoolArray(const uint8_t *values, uint32_t count) override;            // 97: 0x9ca6b3
    uint8_t *GetBoolArray() override;
    void SetUint8Array(const uint8_t *values, uint32_t count) override;           // 99: 0x9cac3f
    uint8_t *GetUint8Array() override;
    void SetSint8Array(const int8_t *values, uint32_t count) override;            // 101: 0x9caa88
    int8_t *GetSint8Array() override;
    void SetUint16Array(const uint16_t *values, uint32_t count) override;         // 103: 0x9caaea
    uint16_t *GetUint16Array() override;
    void SetSint16Array(const int16_t *values, uint32_t count) override;          // 105: 0x9ca933
    int16_t *GetSint16Array() override;
    void SetUint32Array(const uint32_t *values, uint32_t count) override;         // 107: 0x9cab59
    uint32_t *GetUint32Array() override;
    void SetSint32Array(const int32_t *values, uint32_t count) override;          // 109: 0x9ca9a2
    int32_t *GetSint32Array() override;
    void SetUint64Array(const uint64_t *values, uint32_t count) override;         // 111: 0x9cabcc
    uint64_t *GetUint64Array() override;
    void SetSint64Array(const int64_t *values, uint32_t count) override;          // 113: 0x9caa15
    int64_t *GetSint64Array() override;
    void SetFloat32Array(const float *values, uint32_t count) override;           // 115: 0x9ca777
    float *GetFloat32Array() override;
    void SetFloat64Array(const double *values, uint32_t count) override;          // 117: 0x9ca7ea
    double *GetFloat64Array() override;
    void SetCharArray(const char *values, uint32_t count) override;               // 119: 0x9ca715
    char *GetCharArray() override;
    void SetRZUnicodeCharArray(const uint16_t *values, uint32_t count) override;  // 121: 0x9ca8c4
    uint16_t *GetRZUnicodeCharArray() override;
    void SetRZCharArray(const char *values, uint32_t count) override;             // 123: 0x9ca862
    char *GetRZCharArray() override;
    void SetVoidPtrArray(void *const *values, uint32_t count) override;           // 125: 0x9cad03
    void **GetVoidPtrArray() override;
    void SetVoidArray(const void *data, uint32_t size) override;                  // 127: 0x9caca1
    void *GetVoidArray() override;
    void SetUnknownRef(cIGZUnknown *value) override;                              // 129: 0x9ca85d
    bool GetUnknownRef(uint32_t iid, void **out) override;                        // 130: 0x9ca5df
    cIGZUnknown *AsUnknown() const override;                                      // 131: 0x147a30 (folded)

    // ---- helpers (non-virtual; the game's code calls some of them directly) ----

    // 0x9c9f0d: frees what the value owns (delete[] for an array tag, Release for a non-null unknown), then clears the
    // payload, the tag and the count. The reference count stays.
    void Clear();

    // The scalar setter helpers (ret 4 / 8): when the tag is not the kind's, erase (unless empty) and set it; then only
    // the kind's bytes of the payload are written.
    void StoreBool(uint8_t value);              // 0x9cb85f
    void StoreUint8(uint8_t value);             // 0x9cbac6
    void StoreSint8(int8_t value);              // 0x9cb9ec
    void StoreUint16(uint16_t value);           // 0x9cba3f
    void StoreSint16(int16_t value);            // 0x9cb965
    void StoreUint32(uint32_t value);           // 0x9cba6b
    void StoreSint32(int32_t value);            // 0x9cb991
    void StoreUint64(uint64_t value);           // 0x9cba95
    void StoreSint64(int64_t value);            // 0x9cb9bb
    void StoreFloat32(float value);             // 0x9cb8b3
    void StoreFloat64(double value);            // 0x9cb8e1
    void StoreChar(char value);                 // 0x9cb889
    void StoreRZUnicodeChar(uint16_t value);    // 0x9cb939
    void StoreRZChar(char value);               // 0x9cb90f
    void StoreVoidPtr(void *value);             // 0x9cbb36
    // 0x9cbaf0: the unknown kind; a different pointer is AddRef'd (when not null) before the old one is released (when
    // not null).
    void StoreUnknown(cIGZUnknown *value);

    // 0x9cb791 (ret 0xc): slot 56's body. An RZChar array: its characters (FromChar), true. An unknown: when it is a
    // string (QueryInterface 0x89b7dc8), its text (Strlen before Data), true. Otherwise ToString's text, false.
    bool TextOf(cIGZString &text, uint32_t flags, char separator);
    // 0x9cba16 (ret 4): slot 57's body: SetRZCharArray (through this variant's slot) of the text (Strlen, then Data).
    void StoreText(const cIGZString &text);
    // 0x9cb848 (ret 8): slot 62's body: the unknown's QueryInterface; false when it is not an unknown or null.
    bool QueryUnknown(uint32_t iid, void **out);

    // 0x9cb4e1 (ret 8): SetFromString for a scalar type (Erase first, through slot 6).
    bool ParseScalar(const cIGZString &text, uint32_t type);
    // 0x9cad97 (ret 0x10): SetFromString for an array type (Erase first, through slot 6).
    bool ParseArray(const cIGZString &text, uint32_t type, uint32_t count, char separator);

    // 0x9c986c (__cdecl): `to`'s value (its tag says the kind) from `from`'s; see the header comment.
    static bool Convert(const cRZVariant *from, cRZVariant *to);
    // 0x9c91d0 (__cdecl): `from` as a float64 into `to` (tag 0xb; reference count and count zeroed). True.
    static bool ToFloat64(const cRZVariant *from, cRZVariant *to);
    // 0x9c979c (__cdecl): `from` as a 64-bit integer into `to` (tag 8 or 9 by the source's signedness). True.
    static bool ToInt64(const cRZVariant *from, cRZVariant *to);
    // 0x9c92ac (__cdecl): the value as text into `text` (flags bit 0: integers in hexadecimal; bit 1: a bool as
    // "True"/"False"); array elements separated by `separator`. True.
    static bool ToString(const cRZVariant *from, cRZString *text, uint32_t flags, char separator);
    // 0x9c8636 (__cdecl): parses `text` as a number (or "t"/"f"/"true"/"false", or hexadecimal after "0x") and stores
    // it as `to`'s kind (through `to`'s slots). False (and an int32 0 stored) for a tag it has no kind for.
    static bool FromString(const cRZString *text, cRZVariant *to);
    // 0x9c9169 (__cdecl): false for "f", "false" or "0" (any case, slot 9), otherwise true.
    static bool IsTrueText(const cRZString *text);
    // 0x9c91b3 (__cdecl): *out = the text's hexadecimal value (hexparse::ParseHex32).
    static void ParsePointer(const cRZString *text, void *out);
    // 0x9c9152 (__cdecl): text = "0x%08x" of the pointer (through the string's Sprintf slot).
    static void FormatPointer(const void *pointer, cRZString *text);

private:
    template <class T> bool GetValAs(uint16_t tag, T *out, T tagRZVariant::*field);
    template <class T> void StoreScalar(uint16_t tag, T tagRZVariant::*field, T value);
    // The array setters' body: `count` elements of `size` bytes; the buffer is kept when the tag and count match (and
    // `values` is not null, when `needValues`).
    void StoreArray(const void *values, uint32_t count, uint16_t tag, uint32_t size, bool needValues = true);
};

static_assert(sizeof(cRZVariant) == 0x14, "cRZVariant size");

// The game's entry points (`this` in ecx; __fastcall with the dummy edx = __thiscall).
struct cRZVariantAdapters {
    static cRZVariant *__fastcall Construct(void *memory, void *);
    static cRZVariant *__fastcall ConstructCopy(void *memory, void *, const cRZVariant *other);
    static cRZVariant *__fastcall ConstructText(void *memory, void *, const cIGZString *text);
    static cRZVariant *__fastcall ConstructVariant(void *memory, void *, const cIGZVariant *other);
    static cRZVariant *__fastcall ConstructSint8(void *memory, void *, int8_t value);
    static cRZVariant *__fastcall ConstructUint8(void *memory, void *, uint8_t value);
    static cRZVariant *__fastcall ConstructSint16(void *memory, void *, int16_t value);
    static cRZVariant *__fastcall ConstructUint16(void *memory, void *, uint16_t value);
    static cRZVariant *__fastcall ConstructSint32(void *memory, void *, int32_t value);
    static cRZVariant *__fastcall ConstructUint32(void *memory, void *, uint32_t value);
    static cRZVariant *__fastcall ConstructFloat32(void *memory, void *, float value);
    static cRZVariant *__fastcall ConstructFloat64(void *memory, void *, double value);
    static cRZVariant *__fastcall ConstructUnknown(void *memory, void *, cIGZUnknown *value);
    static cRZVariant *__fastcall ConstructBytes(void *memory, void *, const void *data, uint32_t size, uint32_t type);
    static cRZVariant *__fastcall ConstructChars(void *memory, void *, const char *text);
    static cRZVariant *__fastcall ConstructVoidPtr(void *memory, void *, void *value);
    static cRZVariant *__fastcall ConstructSint64(void *memory, void *, int64_t value);
    static cRZVariant *__fastcall ConstructUint64(void *memory, void *, uint64_t value);
    static cRZVariant *__fastcall ConstructBool(void *memory, void *, bool value);
    static void __fastcall Destruct(cRZVariant *self, void *);
    static cRZVariant *__fastcall DeletingDestructor(cRZVariant *self, void *, uint32_t flags);
    static cRZVariant *__fastcall Assign(cRZVariant *self, void *, const cRZVariant *other);

    static bool __fastcall QueryInterface(cRZVariant *self, void *, uint32_t riid, void **out);
    static uint32_t __fastcall AddRef(cRZVariant *self, void *);
    static uint32_t __fastcall Release(cRZVariant *self, void *);
    static uint32_t __fastcall GetType(cRZVariant *self, void *);
    static bool __fastcall IsUndefined(cRZVariant *self, void *);
    static bool __fastcall Erase(cRZVariant *self, void *);
    static bool __fastcall CopyFrom(cRZVariant *self, void *, const cIGZVariant *other);
    static bool __fastcall CreateCopy(cRZVariant *self, void *, cIGZVariant **out);

    static uint8_t __fastcall ValBool(cRZVariant *self, void *);
    static bool __fastcall GetValBool(cRZVariant *self, void *, uint8_t *out);
    static uint8_t __fastcall ValUint8(cRZVariant *self, void *);
    static bool __fastcall GetValUint8(cRZVariant *self, void *, uint8_t *out);
    static int8_t __fastcall ValSint8(cRZVariant *self, void *);
    static bool __fastcall GetValSint8(cRZVariant *self, void *, int8_t *out);
    static uint16_t __fastcall ValUint16(cRZVariant *self, void *);
    static bool __fastcall GetValUint16(cRZVariant *self, void *, uint16_t *out);
    static int16_t __fastcall ValSint16(cRZVariant *self, void *);
    static bool __fastcall GetValSint16(cRZVariant *self, void *, int16_t *out);
    static uint32_t __fastcall ValUint32(cRZVariant *self, void *);
    static bool __fastcall GetValUint32(cRZVariant *self, void *, uint32_t *out);
    static int32_t __fastcall ValSint32(cRZVariant *self, void *);
    static bool __fastcall GetValSint32(cRZVariant *self, void *, int32_t *out);
    static uint64_t __fastcall ValUint64(cRZVariant *self, void *);
    static bool __fastcall GetValUint64(cRZVariant *self, void *, uint64_t *out);
    static int64_t __fastcall ValSint64(cRZVariant *self, void *);
    static bool __fastcall GetValSint64(cRZVariant *self, void *, int64_t *out);
    static float __fastcall ValFloat32(cRZVariant *self, void *);
    static bool __fastcall GetValFloat32(cRZVariant *self, void *, float *out);
    static double __fastcall ValFloat64(cRZVariant *self, void *);
    static bool __fastcall GetValFloat64(cRZVariant *self, void *, double *out);
    static char __fastcall ValChar(cRZVariant *self, void *);
    static bool __fastcall GetValChar(cRZVariant *self, void *, char *out);
    static uint16_t __fastcall ValRZUnicodeChar(cRZVariant *self, void *);
    static bool __fastcall GetValRZUnicodeChar(cRZVariant *self, void *, uint16_t *out);
    static char __fastcall ValRZChar(cRZVariant *self, void *);
    static bool __fastcall GetValRZChar(cRZVariant *self, void *, char *out);
    static void *__fastcall ValVoidPtr(cRZVariant *self, void *);
    static bool __fastcall GetValVoidPtr(cRZVariant *self, void *, void **out);

    static bool __fastcall CreateValString(cRZVariant *self, void *, cIGZString **out, uint32_t flags, char separator);
    static bool __fastcall SetFromString(cRZVariant *self, void *, const cIGZString *text, uint32_t type, uint32_t count,
                                         char separator);
    static bool __fastcall GetValVoid(cRZVariant *self, void *, void *out, uint32_t *size);
    static void __fastcall SetValVoid(cRZVariant *self, void *, const void *data, uint32_t size);
    static cIGZUnknown *__fastcall ValUnknown(cRZVariant *self, void *);
    static bool __fastcall IsEqual(cRZVariant *self, void *, const cIGZVariant *other);
    static bool __fastcall IsNotEqual(cRZVariant *self, void *, const cIGZVariant *other);
    static cIGZVariant *__fastcall AssignFrom(cRZVariant *self, void *, const cIGZVariant *other);
    static uint64_t __fastcall AsUint64(cRZVariant *self, void *);

    static void __fastcall SetBoolArray(cRZVariant *self, void *, const uint8_t *values, uint32_t count);
    static void __fastcall SetUint8Array(cRZVariant *self, void *, const uint8_t *values, uint32_t count);
    static void __fastcall SetSint8Array(cRZVariant *self, void *, const int8_t *values, uint32_t count);
    static void __fastcall SetUint16Array(cRZVariant *self, void *, const uint16_t *values, uint32_t count);
    static void __fastcall SetSint16Array(cRZVariant *self, void *, const int16_t *values, uint32_t count);
    static void __fastcall SetUint32Array(cRZVariant *self, void *, const uint32_t *values, uint32_t count);
    static void __fastcall SetSint32Array(cRZVariant *self, void *, const int32_t *values, uint32_t count);
    static void __fastcall SetUint64Array(cRZVariant *self, void *, const uint64_t *values, uint32_t count);
    static void __fastcall SetSint64Array(cRZVariant *self, void *, const int64_t *values, uint32_t count);
    static void __fastcall SetFloat32Array(cRZVariant *self, void *, const float *values, uint32_t count);
    static void __fastcall SetFloat64Array(cRZVariant *self, void *, const double *values, uint32_t count);
    static void __fastcall SetCharArray(cRZVariant *self, void *, const char *values, uint32_t count);
    static void __fastcall SetRZUnicodeCharArray(cRZVariant *self, void *, const uint16_t *values, uint32_t count);
    static void __fastcall SetRZCharArray(cRZVariant *self, void *, const char *values, uint32_t count);
    static void __fastcall SetVoidPtrArray(cRZVariant *self, void *, void *const *values, uint32_t count);
    static void __fastcall SetVoidArray(cRZVariant *self, void *, const void *data, uint32_t size);

    // The helpers (the slot stubs 12, 15, ... jump to them; the same adapters serve the stubs).
    static void __fastcall Clear(cRZVariant *self, void *);
    static void __fastcall StoreBool(cRZVariant *self, void *, uint8_t value);
    static void __fastcall StoreUint8(cRZVariant *self, void *, uint8_t value);
    static void __fastcall StoreSint8(cRZVariant *self, void *, int8_t value);
    static void __fastcall StoreUint16(cRZVariant *self, void *, uint16_t value);
    static void __fastcall StoreSint16(cRZVariant *self, void *, int16_t value);
    static void __fastcall StoreUint32(cRZVariant *self, void *, uint32_t value);
    static void __fastcall StoreSint32(cRZVariant *self, void *, int32_t value);
    static void __fastcall StoreUint64(cRZVariant *self, void *, uint64_t value);
    static void __fastcall StoreSint64(cRZVariant *self, void *, int64_t value);
    static void __fastcall StoreFloat32(cRZVariant *self, void *, float value);
    static void __fastcall StoreFloat64(cRZVariant *self, void *, double value);
    static void __fastcall StoreChar(cRZVariant *self, void *, char value);
    static void __fastcall StoreRZUnicodeChar(cRZVariant *self, void *, uint16_t value);
    static void __fastcall StoreRZChar(cRZVariant *self, void *, char value);
    static void __fastcall StoreVoidPtr(cRZVariant *self, void *, void *value);
    static void __fastcall StoreUnknown(cRZVariant *self, void *, cIGZUnknown *value);
    static bool __fastcall TextOf(cRZVariant *self, void *, cIGZString *text, uint32_t flags, char separator);
    static void __fastcall StoreText(cRZVariant *self, void *, const cIGZString *text);
    static bool __fastcall QueryUnknown(cRZVariant *self, void *, uint32_t iid, void **out);
    static bool __fastcall ParseScalar(cRZVariant *self, void *, const cIGZString *text, uint32_t type);
    static bool __fastcall ParseArray(cRZVariant *self, void *, const cIGZString *text, uint32_t type, uint32_t count,
                                      char separator);
};

// The texts the conversions use (owned.h): the image's literals in the hooked game, standalone/owned_statics.cpp's
// otherwise (same characters). Parse side: "t", "f", "true", "false", "0", "1", "0x". Format side: the bool words and the
// printf formats (two, four and eight hexadecimal digits; decimal unsigned, signed, 64-bit; floats; a pointer).
TS2_OWNED_STATIC(const char, df42c4)[];
TS2_OWNED_STATIC(const char, db137c)[];
TS2_OWNED_STATIC(const char, da2d44)[];
TS2_OWNED_STATIC(const char, da2d3c)[];
TS2_OWNED_STATIC(const char, da4af4)[];
TS2_OWNED_STATIC(const char, da4fac)[];
TS2_OWNED_STATIC(const char, dd41a0)[];
TS2_OWNED_STATIC(const char, dd41a8)[];
TS2_OWNED_STATIC(const char, da9bd8)[];
TS2_OWNED_STATIC(const char, e61a20)[];
TS2_OWNED_STATIC(const char, e61a2c)[];
TS2_OWNED_STATIC(const char, dacd24)[];
TS2_OWNED_STATIC(const char, d9d510)[];
TS2_OWNED_STATIC(const char, d9b648)[];
TS2_OWNED_STATIC(const char, da2994)[];
TS2_OWNED_STATIC(const char, dce3c8)[];
TS2_OWNED_STATIC(const char, da3e60)[];
