#pragma once
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "gz/cIGZUnknown.h"

#include <cstdint>
#include <string>

class cIGZString;

// The string keys the string-keyed set takes and hands out (RTTI: ibasic_string<char>): MSVC's std::string layout
// (0x18 bytes), storage from the allocator service (0x1be72c assign, 0x1bf753 tidy). The set only reads a key it is
// given, so a plain std::string of the same layout works as an argument too (TreeSimRunNode passes one).
using GZServiceString = std::basic_string<char, std::char_traits<char>, GZServiceAllocator<char>>;
static_assert(sizeof(GZServiceString) == 0x18);

// The string key traits, by the original's template names (RTTI: cKeyStringTraitsT<ibasic_string<char>, 0x4bb0d269,
// 0x6bb22f38>): only template arguments here, so the classes built on them carry the original's RTTI names. (The mangled
// name's `$0GLLCCPDI@` is 0x6bb22f38: MSVC's digits A-P are 0-15, so D is 3; cGZPropertySet.h's comments had read them as hex
// until lane EF.)
template <class Char> class ibasic_string;
template <class Key, unsigned long Iid, unsigned long Hash> struct cKeyStringTraitsT;
using cKeyStringTraits = cKeyStringTraitsT<ibasic_string<char>, 0x4bb0d269, 0x6bb22f38>;

// cIGZPropertySetT<cKeyStringTraits> (interface id 0x4bb0d269): the string-keyed property set, for classes in the standalone
// shape (docs/standalone-shape.md) to derive from. Slots as the set's vtables have them (cGZPropertySetString 0xd9f718, its
// RTTI base 0xda3dc0, the resource's set face 0xdd42c4); keys are GZServiceString. Callers of other objects keep using the
// generated ifaces::cIGZPropertySetString view (same ABI). Lane EL.
template <class Traits> class cIGZPropertySetT;
using cIGZPropertySetString = cIGZPropertySetT<cKeyStringTraits>;

template <> class cIGZPropertySetT<cKeyStringTraits> : public cIGZUnknown {
public:
    virtual bool SetProperty(const GZServiceString *key, void *value) = 0;     // slot 3 (ret 8)
    virtual bool RemoveProperty(const GZServiceString *key) = 0;               // slot 4
    virtual void Clear() = 0;                                                  // slot 5
    virtual bool GetFirst(GZServiceString *key, void **value) = 0;             // slot 6
    virtual bool GetNext(GZServiceString *key, void **value) = 0;              // slot 7
    virtual void *GetProperty(const GZServiceString *key) = 0;                 // slot 8
    virtual bool GetPropertyValue(const GZServiceString *key, void *out) = 0;  // slot 9
    virtual bool HasProperty(const GZServiceString *key) = 0;                  // slot 10
    virtual uint32_t GetCount() = 0;                                           // slot 11
    virtual void CopyFrom(cIGZPropertySetT *other) = 0;                        // slot 12
    virtual ~cIGZPropertySetT() = default;                                     // slot 13: the scalar deleting destructor
    virtual const char *KeyToString(uint32_t keyOffset, cIGZString *out) = 0;  // slot 14 (ret 8)
};

// The uint32 key traits (RTTI: cKeyUint32TraitsT<unsigned long, 0xebb10cdd, 0x6bb22f3c>; the mangled `$0?BEEOPDCD@` is the
// negative -0x144ef323, so the parameters are signed: declared `long` here so our RTTI name is the original's). Lane EQ.
template <class Key, long Iid, long Hash> struct cKeyUint32TraitsT;
using cKeyUint32Traits = cKeyUint32TraitsT<unsigned long, static_cast<long>(0xebb10cdd), 0x6bb22f3c>;

// cIGZPropertySetT<cKeyUint32Traits> (interface id 0xebb10cdd): the uint32-keyed property set (vtables: cGZPropertySetUint32
// 0xda3e24, the uint32 resource's set face 0xe992e8). Lane EQ.
using cIGZPropertySetUint32 = cIGZPropertySetT<cKeyUint32Traits>;

template <> class cIGZPropertySetT<cKeyUint32Traits> : public cIGZUnknown {
public:
    virtual bool SetProperty(const uint32_t *key, void *value) = 0;            // slot 3 (ret 8)
    virtual bool RemoveProperty(const uint32_t *key) = 0;                      // slot 4
    virtual void Clear() = 0;                                                  // slot 5
    virtual bool GetFirst(uint32_t *key, void **value) = 0;                    // slot 6
    virtual bool GetNext(uint32_t *key, void **value) = 0;                     // slot 7
    virtual void *GetProperty(const uint32_t *key) = 0;                        // slot 8
    virtual bool GetPropertyValue(const uint32_t *key, void *out) = 0;         // slot 9
    virtual bool HasProperty(const uint32_t *key) = 0;                         // slot 10
    virtual uint32_t GetCount() = 0;                                           // slot 11
    virtual void CopyFrom(cIGZPropertySetT *other) = 0;                        // slot 12
    virtual ~cIGZPropertySetT() = default;                                     // slot 13: the scalar deleting destructor
    virtual const char *KeyToString(uint32_t key, cIGZString *out) = 0;        // slot 14 (ret 8)
};
