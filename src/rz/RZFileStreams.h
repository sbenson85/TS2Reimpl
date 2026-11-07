#pragma once
#include "game/owned.h"
#include "rz/cRZIStream.h"
#include "rz/cRZOStream.h"

#include <cstddef>
#include <cstdint>
#include <fstream>

class cIGZString;
class cRZString;

// The text and file streams built on cRZOStream and cRZIStream (hook shape, like their bases: the
// game's vtables stay, every slot is hooked at its RVA). RVAs are 1.18.0.187.
//
// cRZTextOStream (0x10 bytes, vtable 0xda2b4c) writes every value as text: each setter formats it
// into a local cRZString with cRZString::Sprintf and a format string the game keeps behind a pointer
// cell, then writes the text through slot 14 (SetRZCharStr), which this class leaves pure
// (_purecall); subclasses supply it. Its constructor is 0x1b4fab (OStreamBase::ConstructTextOStream,
// app/AppDebugger.h).
//
// cRZTextODSStream (0x10 bytes, vtable 0xda2bb0) is a text stream whose slot 14 is
// OutputDebugStringA.
//
// cRZOFStream (0x18 bytes, vtable 0xda2c14) owns a heap std::ofstream. Constructed binary, each
// setter is cRZOStream's (raw bytes through slot 17, its SetVoid = ofstream::write); otherwise
// cRZTextOStream's (text through slot 14 = operator<<). cDebugStream (sim code, vtable 0xdcf704)
// derives from it and calls these members directly.
//
// cRZIFStream (0x18 bytes, vtable 0xda2c78) owns a heap std::ifstream (skipws cleared). Binary,
// each getter is cRZIStream's (raw bytes through slot 17, its GetVoid = istream::read); otherwise
// the value is extracted as text with operator>>. Every getter returns rdstate() == 0.
//
// Our std::ofstream/ifstream are the same MSVC 14.40 classes as the game's (the size checks are in
// RZFileStreams.size.cpp); the game never touches the stream pointer outside these members.

class cRZTextOStream : public cRZOStream {
public:
    static constexpr uint32_t kVTable = 0xda2b4c;
    static constexpr int kSetRZCharStrSlot = 14;
    // The format strings are read through pointer cells (owned statics, below), SetVoid's hex dump strings are owned
    // statics too.
    static constexpr uint32_t kHexDumpBytesPerLine = 25;
    static constexpr uint32_t kHexDumpBufferSize = 0x104;

    // Each setter formats its value and returns slot 14's result (al).
    uint8_t SetSint8(int8_t value);     // slots 4 of the text stream, 0x1b63f6
    uint8_t SetUint8(uint8_t value);    // slot 5, 0x1b65c3
    uint8_t SetSint16(int16_t value);   // slot 6, 0x1b624d
    uint8_t SetUint16(uint16_t value);  // slot 7, 0x1b648d
    uint8_t SetSint32(int32_t value);   // slot 8, 0x1b62dd
    uint8_t SetUint32(uint32_t value);  // slot 9, 0x1b64f0
    uint8_t SetSint64(int64_t value);   // slot 10, 0x1b6383 (ret 8)
    uint8_t SetUint64(uint64_t value);  // slot 11, 0x1b655f (ret 8)
    uint8_t SetFloat32(float value);    // slot 12, 0x1b600b
    // Slot 13, 0x1b609a (ret 8). The value is narrowed to float before it is formatted.
    uint8_t SetFloat64(double value);
    // Slot 15, 0x1b6153: slot 14 with the string's ToChar (its slot 5). Returns slot 14's eax.
    uint32_t SetGZStr(cIGZString *text);
    // Slot 17, 0x1b6648 (ret 8): a hex dump. The header, then each byte (as a sign-extended int)
    // with the byte format, a newline after every 25th byte, and a final newline; each through
    // slot 14. Stops at the first failed write. Returns 1, or 0 when a write failed.
    uint8_t SetVoid(const void *data, uint32_t size);

    // 0x1b5319: vtable reset to this class's, then cRZOStream::Destruct.
    void Destruct();
    // Slot 21, 0x1b5848 (ret 4): Destruct, then (flags & 1) sized delete of 0x10. Returns this.
    cRZTextOStream *DeletingDestructor(uint32_t flags);

    // Slots of our tables (rz/RZFileStreamsTables.cpp) doing what the image's folded bodies do (each shared with other
    // classes, never hooked).
    uint32_t AddRef();       // slot 1, 0x148fb0: the count plus one, returned
    void Flush();            // slot 3, 0x140fe0: nothing
    bool ReturnsTrue();      // slots 18 and 23, 0x144910: true
    void *GetOwner();        // slot 20, 0x145770: the owner at +0xc

private:
    uint8_t WriteText(const char *text);                      // slot 14
    uint8_t WriteFormatted(const char *format, int32_t value);
};
static_assert(sizeof(cRZTextOStream) == 0x10, "cRZTextOStream adds no fields");

class cRZTextODSStream : public cRZTextOStream {
public:
    static constexpr uint32_t kVTable = 0xda2bb0;

    // 0x1b4f99: the text stream's constructor, then this vtable. Returns this.
    cRZTextODSStream *Construct();
    // Slot 14, 0x1b61fe (ret 4): OutputDebugStringA(text); returns 1.
    uint8_t SetRZCharStr(const char *text);
    // 0x1b530e: vtable reset to this class's, then cRZTextOStream::Destruct.
    void Destruct();
    // Slot 21, 0x1b5829 (ret 4): Destruct, then (flags & 1) sized delete of 0x10. Returns this.
    cRZTextODSStream *DeletingDestructor(uint32_t flags);
};
static_assert(sizeof(cRZTextODSStream) == 0x10, "cRZTextODSStream adds no fields");

class cRZOFStream : public cRZTextOStream {
public:
    static constexpr uint32_t kVTable = 0xda2c14;
    static constexpr int kFlushSlot = 3;
    static constexpr int kSetVoidSlot = 17;
    static constexpr int kShareDenyNone = 0x40; // _SH_DENYNO

    // 0x1b4dd2 (ret 0x14): opens `path` (converted with RZText::ConvertStringToString, encoding -1).
    // The open mode is computed from the raw argument bytes: ((append ^ 1) * 8 + 8) | (binary << 5)
    // | out, so append 0 is trunc, 1 is app. Then the flags and SetByteOrder(order). Returns this.
    cRZOFStream *Construct(const cIGZString &path, uint8_t binary, uint32_t order, uint8_t autoFlush, uint8_t append);
    // 0x1b4e80 (ret 0x14): the same from a char string (a cRZString built from it first).
    cRZOFStream *Construct(const char *path, uint8_t binary, uint32_t order, uint8_t autoFlush, uint8_t append);
    // 0x1b52d8: vtable reset, delete the std::ofstream (if any), then cRZTextOStream::Destruct.
    void Destruct();
    // Slot 21, 0x1b57eb (ret 4): Destruct, then (flags & 1) sized delete of 0x18. Returns this.
    cRZOFStream *DeletingDestructor(uint32_t flags);

    void Flush();                         // slot 3, 0x1b5939: ostream::flush
    // Binary: cRZOStream's setter; text: cRZTextOStream's. The result is the callee's eax.
    uint32_t SetSint8(int8_t value);      // slot 4, 0x1b63d8
    uint32_t SetUint8(uint8_t value);     // slot 5, 0x1b65b4
    uint32_t SetSint16(int16_t value);    // slot 6, 0x1b620d
    uint32_t SetUint16(uint16_t value);   // slot 7, 0x1b644a
    uint32_t SetSint32(int32_t value);    // slot 8, 0x1b62a1
    uint32_t SetUint32(uint32_t value);   // slot 9, 0x1b64e1
    uint32_t SetSint64(int64_t value);    // slot 10, 0x1b632f (ret 8)
    uint32_t SetUint64(uint64_t value);   // slot 11, 0x1b6542 (ret 8)
    uint32_t SetFloat32(float value);     // slot 12, 0x1b5fdb
    uint32_t SetFloat64(double value);    // slot 13, 0x1b6069 (ret 8)
    // Slot 14, 0x1b616d: text: operator<< and !fail(); binary: slot 17 with strlen(text). Then, with
    // auto-flush, slot 3. Returns the write's result (al).
    uint8_t SetRZCharStr(const char *text);
    uint32_t SetGZStr(cIGZString *text);  // slot 15, 0x1b6109
    // Slot 17, 0x1b661f (ret 8): ofstream::write; returns rdstate() == 0.
    uint8_t SetVoid(const void *data, uint32_t size);
    // Slot 18 of both file streams, 0x1b5941 (cDebugStream's too): rdstate() != 0. On a cRZIFStream
    // the pointer at +0x10 is its std::ifstream; both reach their basic_ios the same way (see the
    // .cpp).
    uint8_t GetError();

    std::ofstream *mStream;  // +0x10
    uint8_t mBinary;         // +0x14
    uint8_t mAutoFlush;      // +0x15
};
static_assert(sizeof(cRZOFStream) == 0x18, "cRZOFStream is 0x18 bytes");
static_assert(offsetof(cRZOFStream, mStream) == 0x10, "cRZOFStream: stream at +0x10");
static_assert(offsetof(cRZOFStream, mBinary) == 0x14, "cRZOFStream: binary flag at +0x14");
static_assert(offsetof(cRZOFStream, mAutoFlush) == 0x15, "cRZOFStream: auto-flush flag at +0x15");
// Member pointer types naming each constructor overload (hook tables, stand-ins).
using cRZOFStreamFromString = cRZOFStream *(cRZOFStream::*)(const cIGZString &, uint8_t, uint32_t, uint8_t, uint8_t);
using cRZOFStreamFromChars = cRZOFStream *(cRZOFStream::*)(const char *, uint8_t, uint32_t, uint8_t, uint8_t);

class cRZIFStream : public cRZIStream {
public:
    static constexpr uint32_t kVTable = 0xda2c78;
    static constexpr int kGetUint8Slot = 5;
    static constexpr int kShareDenyNone = 0x40; // _SH_DENYNO
    // The text-mode 64-bit getters collect at most this many characters after the first.
    static constexpr int kMaxDigits = 20;
    static constexpr int kDigitBufferSize = 27;

    // 0x1b4cdc (ret 0xc): cRZIStream::Construct(order), then opens `path` (converted as in
    // cRZOFStream) with mode (binary << 5) | in, computed from the raw byte, and clears skipws.
    cRZIFStream *Construct(const cIGZString &path, uint8_t binary, uint32_t order);
    // 0x1b528c: vtable reset, delete the std::ifstream (if any), then cRZIStream::Destruct.
    void Destruct();
    // Slot 21, 0x1b576f (ret 4): Destruct, then (flags & 1) sized delete of 0x18. Returns this.
    cRZIFStream *DeletingDestructor(uint32_t flags);

    uint8_t Skip(uint32_t count);              // slot 3, 0x1b6704: ignore(count, EOF), either mode
    uint8_t GetSint8(void *dst);               // slots 4/5, 0x1b5ddd: >> char
    uint8_t GetSint16(void *dst);              // slot 6, 0x1b5c2b: >> short
    uint8_t GetUint16(void *dst);              // slot 7, 0x1b5e1d: >> unsigned short
    uint8_t GetSint32(void *dst);              // slot 8, 0x1b5c9a: >> long
    uint8_t GetUint32(void *dst);              // slot 9, 0x1b5e4d: >> unsigned long
    // Slot 10, 0x1b5d04. Text: a sign or digit, then up to 20 more characters of any kind (read
    // through slot 5), parsed with rztext::ParseInt64.
    // ORIGINAL-BUG: when the first character is neither a sign nor a digit, the original tests the
    // uninitialised first byte of its buffer and, if nonzero, goes on collecting characters. Ours
    // starts from a zeroed buffer, so it stops there (the original's common outcome).
    uint8_t GetSint64(void *dst);
    // Slot 11, 0x1b5e7d. Text: a digit or '+' (not kept), then characters up to 20 in the buffer.
    uint8_t GetUint64(void *dst);
    uint8_t GetFloat32(void *dst);             // slot 12, 0x1b5952: >> float
    uint8_t GetFloat64(void *dst);             // slot 13, 0x1b5990: >> double
    // Slot 14, 0x1b5b46 (ret 8). Text: istream::get(buf, size, '\0').
    uint8_t GetRZCharStr(char *buf, uint32_t size);
    // Slot 15, 0x1b59db. Text: characters up to a NUL, then the string's slot 17 (copy from a
    // cIGZString) with them.
    // ORIGINAL-BUG: the original's character is uninitialised and a failed read leaves it as it
    // was, so a stream that ends before a NUL loops forever, appending the last character (or
    // garbage). Ours stops at the first failed read.
    uint8_t GetGZStr(cIGZString *out);
    // Slot 17, 0x1b5f0a (ret 8): istream::read, either mode.
    uint8_t GetVoid(void *dst, uint32_t size);
    // Slot 18 is cRZOFStream::GetError (0x1b5941, one body for both: each keeps its stream at +0x10).

    std::ifstream *mStream;  // +0x10
    uint8_t mBinary;         // +0x14

private:
    uint8_t Good() const;
};
static_assert(sizeof(cRZIFStream) == 0x18, "cRZIFStream is 0x18 bytes");
static_assert(offsetof(cRZIFStream, mStream) == 0x10, "cRZIFStream: stream at +0x10");
static_assert(offsetof(cRZIFStream, mBinary) == 0x14, "cRZIFStream: binary flag at +0x14");

namespace rzfilestreams {
// 0x1b5fa4 (__cdecl): constructs `out` empty, converts `path` into it (RZText::ConvertStringToString,
// encoding -1) and returns out.
cRZString *__cdecl ConvertPath(cRZString *out, const cIGZString &path);
} // namespace rzfilestreams

// The text streams' data, owned (lane RU): the hooked build's are the image's, an image-free program's are
// standalone/owned_statics.cpp's (the tables aliased to ours, rz/RZFileStreamsTables.cpp).
TS2_OWNED_STATIC(const char *, ff2414); // "%d": 8/16/32-bit signed (sign-extended)
TS2_OWNED_STATIC(const char *, ff2418); // "%u": 8/16/32-bit unsigned (zero-extended)
TS2_OWNED_STATIC(const char *, ff241c); // "%lld"
TS2_OWNED_STATIC(const char *, ff2420); // "%llu"
TS2_OWNED_STATIC(const char *, ff2424); // "%g": both float sizes, passed as a double
TS2_OWNED_STATIC(const char, da2dcc)[]; // SetVoid's header: a newline, "HEXDUMP:", a newline
TS2_OWNED_STATIC(const char, da2dd8)[]; // SetVoid's byte: two hex digits and a space
TS2_OWNED_STATIC(const char, d9e644)[]; // "\n"
TS2_OWNED_STATIC(void *, d9a110);       // OutputDebugStringA's import slot
TS2_OWNED_STATIC(const OwnedVTable, da2b4c);
TS2_OWNED_STATIC(const OwnedVTable, da2bb0);
TS2_OWNED_STATIC(const OwnedVTable, da2c14); // cRZOFStream's
TS2_OWNED_STATIC(const OwnedVTable, da2c78); // cRZIFStream's
extern "C" const void *ts2_own_vt_da2c14[24];
extern "C" const void *ts2_own_vt_da2c78[24];
extern "C" const void *ts2_own_vt_da2b4c[24];
extern "C" const void *ts2_own_vt_da2bb0[24];
