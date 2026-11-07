#pragma once
#include "gz/cIGZUnknown.h"

#include <cstdarg>

// String interface used across the game. Slot order is ABI (see db/interface-slots.jsonl).
// MSVC places a group of overloads together at the first one's position, in REVERSE declaration
// order. The slot comments are the resulting layout, checked against the game's cRZString vtable
// (argument counts from each slot's `ret N`, and which slots call through the other string's
// vtable) and against `cl /d1reportSingleClassLayoutcIGZString`.
// Implemented by rzface::cRZString (cRZString's own vtable, src/rz/cRZString.h).
class cIGZString : public cIGZUnknown {
public:
    virtual void FromChar(const char *src) = 0;                           // slot 4
    virtual void FromChar(const char *src, uint32_t len) = 0;             // slot 3
    virtual const char *ToChar() const = 0;                               // slot 5
    virtual const char *Data() const = 0;                                 // slot 6
    virtual uint32_t Strlen() const = 0;                                  // slot 7
    virtual bool IsEmpty() const = 0;                                     // slot 8
    // Slots 11 and 12 take another string by pointer/reference (same ABI); 11 tail-calls 12.
    virtual bool IsEqual(const cIGZString *other, bool caseSensitive) const = 0;           // slot 12
    virtual bool IsEqual(const cIGZString &other, bool caseSensitive) const = 0;           // slot 11
    virtual bool IsEqual(const char *other, bool caseSensitive) const = 0;                 // slot 10
    virtual bool IsEqual(const char *other, uint32_t len, bool caseSensitive) const = 0;   // slot 9
    virtual int32_t CompareTo(const cIGZString &other, bool caseSensitive) const = 0;          // slot 15
    virtual int32_t CompareTo(const char *other, bool caseSensitive) const = 0;                // slot 14
    virtual int32_t CompareTo(const char *other, uint32_t len, bool caseSensitive) const = 0;  // slot 13
    virtual cIGZString &operator=(const cIGZString &other) = 0;           // slot 16
    virtual void Copy(const cIGZString &other) = 0;                       // slot 17
    virtual void Resize(uint32_t size) = 0;                               // slot 18
    virtual cIGZString *Append(const char *other) = 0;                            // slot 21
    virtual cIGZString *Append(const char *other, uint32_t len) = 0;              // slot 20
    virtual cIGZString *Append(const cIGZString &other) = 0;                      // slot 19
    virtual cIGZString *Insert(uint32_t pos, const char *other, uint32_t len) = 0;            // slot 23
    virtual cIGZString *Insert(uint32_t pos, const cIGZString &other) = 0;                    // slot 22
    virtual cIGZString *Replace(uint32_t pos, const char *other, uint32_t len) = 0;           // slot 25
    virtual cIGZString *Replace(uint32_t pos, const cIGZString &other) = 0;                   // slot 24
    virtual cIGZString *Erase(uint32_t start, uint32_t end) = 0;          // slot 26
    virtual int32_t Find(const char *other, uint32_t pos, bool caseSensitive) const = 0;         // slot 28
    virtual int32_t Find(const cIGZString &other, uint32_t pos, bool caseSensitive) const = 0;   // slot 27
    virtual int32_t RFind(const char *other, uint32_t pos, bool caseSensitive) const = 0;        // slot 30
    virtual int32_t RFind(const cIGZString &other, uint32_t pos, bool caseSensitive) const = 0;  // slot 29
    virtual cIGZString *Sprintf(const char *fmt, ...) = 0;                // slot 31 (__cdecl; returns this)
    virtual cIGZString *SprintfVaList(const char *fmt, va_list args) = 0; // slot 32 (returns this)
    // slot 33 in implementations: scalar deleting destructor
};
