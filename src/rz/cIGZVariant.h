#pragma once
// cIGZVariant: the tagged-value interface (cRZVariant implements it). Slot order is ABI: the game's vtable at RVA 0xe6180c
// (1.18.0.187), 133 slots; the original's slot names are in db/interface-slots.jsonl ("cIGZVariant"). Every name here is
// unique (MSVC groups overloads of one name and reverses them, so the original's overloaded #1 / #2 names are spelled
// differently). Slot numbers are in the comments; all slots are __thiscall.
//
// Per value kind X (Bool, Uint8, Sint8, Uint16, Sint16, Uint32, Sint32, Uint64, Sint64, Float32, Float64, Char,
// RZUnicodeChar, RZChar, VoidPtr) there are three slots: ValX (the original's GetValX#1: the value, 0 when it can't be
// had), GetValX (#2: the value into *out, converting from another kind when the variant holds one; false when that fails)
// and SetValX. AsX / AsXRef read the payload as X without looking at the tag (its value, its address); the arrays are
// SetXArray (the original's RefX#1: copy `count` elements in) and GetXArray (RefX#2: the buffer).
#include "gz/cIGZUnknown.h"

#include <cstdint>

class cIGZString;

class cIGZVariant : public cIGZUnknown {
public:
    virtual uint32_t GetType() const = 0;                                   // 3: the tag (zero-extended)
    virtual uint32_t GetCount() const = 0;                                  // 4: the element count of an array
    virtual bool IsUndefined() const = 0;                                   // 5: tag 0
    virtual bool Erase() = 0;                                               // 6: frees the value; true
    virtual bool CopyFrom(const cIGZVariant &other) = 0;                    // 7 (ret 4)
    virtual bool CreateCopy(cIGZVariant **out) = 0;                         // 8 (ret 4)
    virtual bool CanGetVal(uint32_t type) const = 0;                        // 9 (ret 4): always true

    virtual uint8_t ValBool() = 0;                                          // 10
    virtual bool GetValBool(uint8_t *out) = 0;                              // 11
    virtual void SetValBool(uint8_t value) = 0;                             // 12
    virtual uint8_t ValUint8() = 0;                                         // 13
    virtual bool GetValUint8(uint8_t *out) = 0;                             // 14
    virtual void SetValUint8(uint8_t value) = 0;                            // 15
    virtual int8_t ValSint8() = 0;                                          // 16
    virtual bool GetValSint8(int8_t *out) = 0;                              // 17
    virtual void SetValSint8(int8_t value) = 0;                             // 18
    virtual uint16_t ValUint16() = 0;                                       // 19
    virtual bool GetValUint16(uint16_t *out) = 0;                           // 20
    virtual void SetValUint16(uint16_t value) = 0;                          // 21
    virtual int16_t ValSint16() = 0;                                        // 22
    virtual bool GetValSint16(int16_t *out) = 0;                            // 23
    virtual void SetValSint16(int16_t value) = 0;                           // 24
    virtual uint32_t ValUint32() = 0;                                       // 25
    virtual bool GetValUint32(uint32_t *out) = 0;                           // 26
    virtual void SetValUint32(uint32_t value) = 0;                          // 27
    virtual int32_t ValSint32() = 0;                                        // 28
    virtual bool GetValSint32(int32_t *out) = 0;                            // 29
    virtual void SetValSint32(int32_t value) = 0;                           // 30
    virtual uint64_t ValUint64() = 0;                                       // 31
    virtual bool GetValUint64(uint64_t *out) = 0;                           // 32
    virtual void SetValUint64(uint64_t value) = 0;                          // 33
    virtual int64_t ValSint64() = 0;                                        // 34
    virtual bool GetValSint64(int64_t *out) = 0;                            // 35
    virtual void SetValSint64(int64_t value) = 0;                           // 36
    virtual float ValFloat32() = 0;                                         // 37
    virtual bool GetValFloat32(float *out) = 0;                             // 38
    virtual void SetValFloat32(float value) = 0;                            // 39
    virtual double ValFloat64() = 0;                                        // 40
    virtual bool GetValFloat64(double *out) = 0;                            // 41
    virtual void SetValFloat64(double value) = 0;                           // 42
    virtual char ValChar() = 0;                                             // 43
    virtual bool GetValChar(char *out) = 0;                                 // 44
    virtual void SetValChar(char value) = 0;                                // 45
    virtual uint16_t ValRZUnicodeChar() = 0;                                // 46
    virtual bool GetValRZUnicodeChar(uint16_t *out) = 0;                    // 47
    virtual void SetValRZUnicodeChar(uint16_t value) = 0;                   // 48
    virtual char ValRZChar() = 0;                                           // 49
    virtual bool GetValRZChar(char *out) = 0;                               // 50
    virtual void SetValRZChar(char value) = 0;                              // 51
    virtual void *ValVoidPtr() = 0;                                         // 52
    virtual bool GetValVoidPtr(void **out) = 0;                             // 53
    virtual void SetValVoidPtr(void *value) = 0;                            // 54

    // 55 (ret 0xc): a new string (AddRef'd) holding GetValString's text in *out; null when GetValString says false.
    virtual bool CreateValString(cIGZString **out, uint32_t flags, char separator) = 0;
    // 56 (ret 0xc): the value as text into `text` (flags: bit 0 hexadecimal integers, bit 1 "True"/"False" for a bool;
    // `separator` between array elements). True only for an RZChar array or an unknown that is a string.
    virtual bool GetValString(cIGZString &text, uint32_t flags, char separator) = 0;
    virtual void SetValString(const cIGZString &text) = 0;                  // 57 (ret 4): an RZChar array of its text
    // 58 (ret 0x10): parses `text` as a value of `type` (an array: `count` elements, 0 for as many as the text has,
    // split at `separator`).
    virtual bool SetFromString(const cIGZString &text, uint32_t type, uint32_t count, char separator) = 0;
    virtual bool GetValVoid(void *out, uint32_t *size) = 0;                 // 59 (ret 8): copies the untyped buffer
    virtual void SetValVoid(const void *data, uint32_t size) = 0;           // 60 (ret 8)
    virtual cIGZUnknown *ValUnknown() = 0;                                  // 61
    virtual bool GetValUnknown(uint32_t iid, void **out) = 0;               // 62 (ret 8): QueryInterface on it
    virtual void SetValUnknown(cIGZUnknown *value) = 0;                     // 63 (ret 4)
    virtual bool IsEqual(const cIGZVariant &other) = 0;                     // 64 (ret 4): ==
    virtual bool IsNotEqual(const cIGZVariant &other) = 0;                  // 65 (ret 4): !=
    virtual cIGZVariant &AssignFrom(const cIGZVariant &other) = 0;          // 66 (ret 4): =

    virtual uint8_t AsBool() const = 0;                                     // 67
    virtual uint8_t *AsBoolRef() = 0;                                       // 68
    virtual uint8_t AsUint8() const = 0;                                    // 69
    virtual uint8_t *AsUint8Ref() = 0;                                      // 70
    virtual int8_t AsSint8() const = 0;                                     // 71
    virtual int8_t *AsSint8Ref() = 0;                                       // 72
    virtual uint16_t AsUint16() const = 0;                                  // 73
    virtual uint16_t *AsUint16Ref() = 0;                                    // 74
    virtual int16_t AsSint16() const = 0;                                   // 75
    virtual int16_t *AsSint16Ref() = 0;                                     // 76
    virtual uint32_t AsUint32() const = 0;                                  // 77
    virtual uint32_t *AsUint32Ref() = 0;                                    // 78
    virtual int32_t AsSint32() const = 0;                                   // 79
    virtual int32_t *AsSint32Ref() = 0;                                     // 80
    virtual uint64_t AsUint64() const = 0;                                  // 81
    virtual uint64_t *AsUint64Ref() = 0;                                    // 82
    virtual int64_t AsSint64() const = 0;                                   // 83
    virtual int64_t *AsSint64Ref() = 0;                                     // 84
    virtual float AsFloat32() const = 0;                                    // 85
    virtual float *AsFloat32Ref() = 0;                                      // 86
    virtual double AsFloat64() const = 0;                                   // 87
    virtual double *AsFloat64Ref() = 0;                                     // 88
    virtual char AsChar() const = 0;                                        // 89
    virtual char *AsCharRef() = 0;                                          // 90
    virtual uint16_t AsRZUnicodeChar() const = 0;                           // 91
    virtual uint16_t *AsRZUnicodeCharRef() = 0;                             // 92
    virtual char AsRZChar() const = 0;                                      // 93
    virtual char *AsRZCharRef() = 0;                                        // 94
    virtual void *AsVoidPtr() const = 0;                                    // 95
    virtual void *AsVoidPtrValue() const = 0;                               // 96: the pointer again (not its address)

    virtual void SetBoolArray(const uint8_t *values, uint32_t count) = 0;            // 97 (ret 8)
    virtual uint8_t *GetBoolArray() = 0;                                             // 98
    virtual void SetUint8Array(const uint8_t *values, uint32_t count) = 0;           // 99
    virtual uint8_t *GetUint8Array() = 0;                                            // 100
    virtual void SetSint8Array(const int8_t *values, uint32_t count) = 0;            // 101
    virtual int8_t *GetSint8Array() = 0;                                             // 102
    virtual void SetUint16Array(const uint16_t *values, uint32_t count) = 0;         // 103
    virtual uint16_t *GetUint16Array() = 0;                                          // 104
    virtual void SetSint16Array(const int16_t *values, uint32_t count) = 0;          // 105
    virtual int16_t *GetSint16Array() = 0;                                           // 106
    virtual void SetUint32Array(const uint32_t *values, uint32_t count) = 0;         // 107
    virtual uint32_t *GetUint32Array() = 0;                                          // 108
    virtual void SetSint32Array(const int32_t *values, uint32_t count) = 0;          // 109
    virtual int32_t *GetSint32Array() = 0;                                           // 110
    virtual void SetUint64Array(const uint64_t *values, uint32_t count) = 0;         // 111
    virtual uint64_t *GetUint64Array() = 0;                                          // 112
    virtual void SetSint64Array(const int64_t *values, uint32_t count) = 0;          // 113
    virtual int64_t *GetSint64Array() = 0;                                           // 114
    virtual void SetFloat32Array(const float *values, uint32_t count) = 0;           // 115
    virtual float *GetFloat32Array() = 0;                                            // 116
    virtual void SetFloat64Array(const double *values, uint32_t count) = 0;         // 117
    virtual double *GetFloat64Array() = 0;                                           // 118
    virtual void SetCharArray(const char *values, uint32_t count) = 0;               // 119
    virtual char *GetCharArray() = 0;                                                // 120
    virtual void SetRZUnicodeCharArray(const uint16_t *values, uint32_t count) = 0;  // 121
    virtual uint16_t *GetRZUnicodeCharArray() = 0;                                   // 122
    virtual void SetRZCharArray(const char *values, uint32_t count) = 0;             // 123
    virtual char *GetRZCharArray() = 0;                                              // 124
    virtual void SetVoidPtrArray(void *const *values, uint32_t count) = 0;           // 125
    virtual void **GetVoidPtrArray() = 0;                                            // 126
    virtual void SetVoidArray(const void *data, uint32_t size) = 0;                  // 127
    virtual void *GetVoidArray() = 0;                                                // 128
    virtual void SetUnknownRef(cIGZUnknown *value) = 0;                     // 129: slot 63 again (the same function)
    virtual bool GetUnknownRef(uint32_t iid, void **out) = 0;               // 130: slot 62 again
    virtual cIGZUnknown *AsUnknown() const = 0;                             // 131: the payload pointer, no tag check

    virtual ~cIGZVariant() = default;                                       // 132: the deleting destructor (ret 4)
};
