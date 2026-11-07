#include "rz/RZFileStreams.h"
#include "app/AppDebugger.h"
#include "game/game.h"
#include "game/import.h"
#include "game/vtable.h"
#include "gz/cIGZString.h"
#include "rz/RZTextConvert.h"
#include "rz/RZTextParse.h"
#include "rz/cRZString.h"

#include <cstdio>
#include <cstring>
#include <new>

// The game allocates the streams with these sizes; ours are the same classes.
static_assert(sizeof(std::ofstream) == 0xb0, "std::ofstream matches the game's 0xb0 bytes");
static_assert(sizeof(std::ifstream) == 0xb8, "std::ifstream matches the game's 0xb8 bytes");

namespace {

using WriteTextFn = uint8_t(__thiscall *)(void *, const char *);
using ReadByteFn = uint8_t(__thiscall *)(void *, void *);
using FlushFn = void(__thiscall *)(void *);
using WriteRawFn = uint8_t(__thiscall *)(void *, const void *, uint32_t);

// A cRZString on the stack (constructed and destroyed like the original's locals).
struct LocalString {
    cRZString text;
    LocalString() { text.Construct(); }
    ~LocalString() { text.Destruct(); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
};

// rdstate() of the stream behind the pointer at +0x10: an std::istream or std::ostream subobject at
// offset 0 of either file stream, each reaching the same basic_ios through its virtual base.
std::ios::iostate StateOf(const std::ios *ios)
{
    return ios->rdstate();
}

uint8_t StreamGood(const std::ios &ios)
{
    return StateOf(&ios) == 0 ? 1 : 0;
}

// The open mode bits as the original computes them, from the raw argument bytes.
int OutputMode(uint8_t binary, uint8_t append)
{
    const int appendOrTrunc = ((append ^ 1) * 8) + 8;  // std::ios::app (8) for 1, trunc (0x10) for 0
    return appendOrTrunc | (binary << 5) | std::ios::out;
}

int InputMode(uint8_t binary)
{
    return (binary << 5) | std::ios::in;
}

} // namespace

// ---- 0x1b5fa4 ----

cRZString *__cdecl rzfilestreams::ConvertPath(cRZString *out, const cIGZString &path)
{
    out->Construct();
    RZText::ConvertStringToString(path, *out->AsInterface(), -1);
    return out;
}

// ---- cRZTextOStream ----

uint8_t cRZTextOStream::WriteText(const char *text)
{
    return VirtualCall<WriteTextFn, kSetRZCharStrSlot>(this, text);
}

uint8_t cRZTextOStream::WriteFormatted(const char *format, int32_t value)
{
    LocalString text;
    text.text.Sprintf(format, value);
    return WriteText(text.text.ToChar());
}

uint8_t cRZTextOStream::SetSint8(int8_t value)
{
    return WriteFormatted(ts2_rva_ff2414, value);
}

uint8_t cRZTextOStream::SetUint8(uint8_t value)
{
    return WriteFormatted(ts2_rva_ff2418, value);
}

uint8_t cRZTextOStream::SetSint16(int16_t value)
{
    return WriteFormatted(ts2_rva_ff2414, value);
}

uint8_t cRZTextOStream::SetUint16(uint16_t value)
{
    return WriteFormatted(ts2_rva_ff2418, value);
}

uint8_t cRZTextOStream::SetSint32(int32_t value)
{
    return WriteFormatted(ts2_rva_ff2414, value);
}

uint8_t cRZTextOStream::SetUint32(uint32_t value)
{
    return WriteFormatted(ts2_rva_ff2418, static_cast<int32_t>(value));
}

uint8_t cRZTextOStream::SetSint64(int64_t value)
{
    LocalString text;
    text.text.Sprintf(ts2_rva_ff241c, value);
    return WriteText(text.text.ToChar());
}

uint8_t cRZTextOStream::SetUint64(uint64_t value)
{
    LocalString text;
    text.text.Sprintf(ts2_rva_ff2420, value);
    return WriteText(text.text.ToChar());
}

uint8_t cRZTextOStream::SetFloat32(float value)
{
    LocalString text;
    text.text.Sprintf(ts2_rva_ff2424, static_cast<double>(value));
    return WriteText(text.text.ToChar());
}

uint8_t cRZTextOStream::SetFloat64(double value)
{
    LocalString text;
    // The original narrows the value to float and widens it back before formatting.
    const float narrowed = static_cast<float>(value);
    text.text.Sprintf(ts2_rva_ff2424, static_cast<double>(narrowed));
    return WriteText(text.text.ToChar());
}

uint32_t cRZTextOStream::SetGZStr(cIGZString *text)
{
    return VirtualCall<uint32_t(__thiscall *)(void *, const char *), kSetRZCharStrSlot>(this, text->ToChar());
}

uint8_t cRZTextOStream::SetVoid(const void *data, uint32_t size)
{
    const auto *bytes = static_cast<const int8_t *>(data);
    bool ok = WriteText(ts2_rva_da2dcc) != 0;
    for (uint32_t i = 1; i <= size; ++i) {
        if (!ok) return 0;
        char buffer[kHexDumpBufferSize];
        std::snprintf(buffer, sizeof(buffer), ts2_rva_da2dd8, static_cast<int>(bytes[i - 1]));
        const bool wrote = WriteText(buffer) != 0;
        ok = wrote;
        if (i % kHexDumpBytesPerLine == 0) ok = wrote && WriteText(ts2_rva_d9e644) != 0;
    }
    if (!ok) return 0;
    if (!WriteText(ts2_rva_d9e644)) return 0;
    return 1;
}

void cRZTextOStream::Destruct()
{
    mVTable = &ts2_rva_da2b4c;
    cRZOStream::Destruct();
}

cRZTextOStream *cRZTextOStream::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cRZTextOStream));
    return this;
}

// ---- cRZTextODSStream ----

cRZTextODSStream *cRZTextODSStream::Construct()
{
    reinterpret_cast<appdebugger::OStreamBase *>(this)->ConstructTextOStream();
    mVTable = &ts2_rva_da2bb0;
    return this;
}

uint8_t cRZTextODSStream::SetRZCharStr(const char *text)
{
    reinterpret_cast<void(__stdcall *)(const char *)>(ts2_rva_d9a110)(text);
    return 1;
}

void cRZTextODSStream::Destruct()
{
    mVTable = &ts2_rva_da2bb0;
    cRZTextOStream::Destruct();
}

cRZTextODSStream *cRZTextODSStream::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cRZTextODSStream));
    return this;
}

// ---- cRZOFStream ----

cRZOFStream *cRZOFStream::Construct(const cIGZString &path, uint8_t binary, uint32_t order, uint8_t autoFlush,
                                    uint8_t append)
{
    reinterpret_cast<appdebugger::OStreamBase *>(this)->ConstructTextOStream();
    mVTable = &ts2_rva_da2c14;
    std::ofstream *stream = static_cast<std::ofstream *>(::operator new(sizeof(std::ofstream), std::nothrow));
    if (stream) {
        cRZString name;
        rzfilestreams::ConvertPath(&name, path);
        stream = ::new (stream) std::ofstream(name.AsInterface()->ToChar(), OutputMode(binary, append), kShareDenyNone);
        name.Destruct();
    }
    mStream = stream;
    mBinary = binary;
    mAutoFlush = autoFlush;
    SetByteOrder(order);
    return this;
}

cRZOFStream *cRZOFStream::Construct(const char *path, uint8_t binary, uint32_t order, uint8_t autoFlush,
                                    uint8_t append)
{
    reinterpret_cast<appdebugger::OStreamBase *>(this)->ConstructTextOStream();
    mVTable = &ts2_rva_da2c14;
    std::ofstream *stream = static_cast<std::ofstream *>(::operator new(sizeof(std::ofstream), std::nothrow));
    if (stream) {
        cRZString source;
        source.Construct(path);
        cRZString name;
        rzfilestreams::ConvertPath(&name, *source.AsInterface());
        stream = ::new (stream) std::ofstream(name.ToChar(), OutputMode(binary, append), kShareDenyNone);
        name.Destruct();
        source.Destruct();
    }
    mStream = stream;
    mBinary = binary;
    mAutoFlush = autoFlush;
    SetByteOrder(order);
    return this;
}

void cRZOFStream::Destruct()
{
    mVTable = &ts2_rva_da2c14;
    delete mStream;
    cRZTextOStream::Destruct();
}

cRZOFStream *cRZOFStream::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cRZOFStream));
    return this;
}

void cRZOFStream::Flush()
{
    mStream->flush();
}

uint32_t cRZOFStream::SetSint8(int8_t value)
{
    if (mBinary) return cRZOStream::SetSint8(static_cast<uint8_t>(value));
    return cRZTextOStream::SetSint8(value);
}

uint32_t cRZOFStream::SetUint8(uint8_t value)
{
    if (mBinary) return cRZOStream::SetSint8(value);
    return cRZTextOStream::SetUint8(value);
}

uint32_t cRZOFStream::SetSint16(int16_t value)
{
    if (mBinary) return cRZOStream::SetSint16(value);
    return cRZTextOStream::SetSint16(value);
}

uint32_t cRZOFStream::SetUint16(uint16_t value)
{
    if (mBinary) return cRZOStream::SetUint16(value);
    return cRZTextOStream::SetUint16(value);
}

uint32_t cRZOFStream::SetSint32(int32_t value)
{
    if (mBinary) return cRZOStream::SetSint32(value);
    return cRZTextOStream::SetSint32(value);
}

uint32_t cRZOFStream::SetUint32(uint32_t value)
{
    if (mBinary) return cRZOStream::SetSint32(static_cast<int32_t>(value));
    return cRZTextOStream::SetUint32(value);
}

uint32_t cRZOFStream::SetSint64(int64_t value)
{
    if (mBinary) return cRZOStream::SetSint64(value);
    return cRZTextOStream::SetSint64(value);
}

uint32_t cRZOFStream::SetUint64(uint64_t value)
{
    if (mBinary) return cRZOStream::SetSint64(static_cast<int64_t>(value));
    return cRZTextOStream::SetUint64(value);
}

uint32_t cRZOFStream::SetFloat32(float value)
{
    if (mBinary) return cRZOStream::SetFloat32(value);
    return cRZTextOStream::SetFloat32(value);
}

uint32_t cRZOFStream::SetFloat64(double value)
{
    if (mBinary) return cRZOStream::SetFloat64(value);
    return cRZTextOStream::SetFloat64(value);
}

uint8_t cRZOFStream::SetRZCharStr(const char *text)
{
    uint8_t result;
    if (mBinary) {
        result = VirtualCall<WriteRawFn, kSetVoidSlot>(this, text, static_cast<uint32_t>(std::strlen(text)));
    } else {
        *mStream << text;
        result = StreamGood(*mStream);
    }
    if (mAutoFlush) VirtualCall<FlushFn, kFlushSlot>(this);
    return result;
}

uint32_t cRZOFStream::SetGZStr(cIGZString *text)
{
    if (mBinary) return cRZOStream::SetGZStr(text);
    return cRZTextOStream::SetGZStr(text);
}

uint8_t cRZOFStream::SetVoid(const void *data, uint32_t size)
{
    mStream->write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
    return StreamGood(*mStream);
}

uint8_t cRZOFStream::GetError()
{
    // Also cRZIFStream's slot 18: its +0x10 holds an std::ifstream, whose basic_ios is reached the
    // same way through the virtual base (both stream classes start with their vbptr).
    return StateOf(mStream) != 0 ? 1 : 0;
}

// ---- cRZIFStream ----

cRZIFStream *cRZIFStream::Construct(const cIGZString &path, uint8_t binary, uint32_t order)
{
    cRZIStream::Construct(order);
    mVTable = &ts2_rva_da2c78;
    std::ifstream *stream = static_cast<std::ifstream *>(::operator new(sizeof(std::ifstream), std::nothrow));
    if (stream) {
        cRZString name;
        rzfilestreams::ConvertPath(&name, path);
        stream = ::new (stream) std::ifstream(name.AsInterface()->ToChar(), InputMode(binary), kShareDenyNone);
        name.Destruct();
    }
    mStream = stream;
    mBinary = binary;
    mStream->unsetf(std::ios::skipws);
    return this;
}

void cRZIFStream::Destruct()
{
    mVTable = &ts2_rva_da2c78;
    delete mStream;
    cRZIStream::Destruct();
}

cRZIFStream *cRZIFStream::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cRZIFStream));
    return this;
}

uint8_t cRZIFStream::Good() const
{
    return StreamGood(*mStream);
}

uint8_t cRZIFStream::Skip(uint32_t count)
{
    mStream->ignore(static_cast<std::streamsize>(count), std::char_traits<char>::eof());
    return Good();
}

uint8_t cRZIFStream::GetSint8(void *dst)
{
    if (mBinary) return cRZIStream::GetSint8(dst);
    *mStream >> *static_cast<char *>(dst);
    return Good();
}

uint8_t cRZIFStream::GetSint16(void *dst)
{
    if (mBinary) return cRZIStream::GetSint16(dst);
    *mStream >> *static_cast<short *>(dst);
    return Good();
}

uint8_t cRZIFStream::GetUint16(void *dst)
{
    if (mBinary) return cRZIStream::GetSint16(dst);
    *mStream >> *static_cast<unsigned short *>(dst);
    return Good();
}

uint8_t cRZIFStream::GetSint32(void *dst)
{
    if (mBinary) return cRZIStream::GetSint32(dst);
    *mStream >> *static_cast<long *>(dst);
    return Good();
}

uint8_t cRZIFStream::GetUint32(void *dst)
{
    if (mBinary) return cRZIStream::GetSint32(dst);
    *mStream >> *static_cast<unsigned long *>(dst);
    return Good();
}

uint8_t cRZIFStream::GetSint64(void *dst)
{
    if (mBinary) return cRZIStream::GetSint64(dst);
    char digits[kDigitBufferSize] = {};
    int length = 0;
    int count = 0;
    char c;
    if (VirtualCall<ReadByteFn, kGetUint8Slot>(this, &c)) {
        bool collect;
        if (c == '-' || c == '+') {
            length = 1;
            digits[0] = c;
            collect = true;
        } else if (static_cast<uint8_t>(c - '0') <= 9) {
            length = 1;
            count = 1;
            digits[0] = c;
            collect = true;
        } else {
            collect = digits[0] != 0; // see the header: the original reads an uninitialised byte here
        }
        if (collect) {
            do {
                if (!VirtualCall<ReadByteFn, kGetUint8Slot>(this, &c)) break;
                digits[length++] = c;
                ++count;
            } while (count <= kMaxDigits);
        }
    }
    digits[length] = 0;
    *static_cast<uint64_t *>(dst) = rztext::ParseInt64(digits);
    return Good();
}

uint8_t cRZIFStream::GetUint64(void *dst)
{
    if (mBinary) return cRZIStream::GetSint64(dst);
    char digits[kDigitBufferSize];
    int length = 0;
    char c;
    if (VirtualCall<ReadByteFn, kGetUint8Slot>(this, &c)) {
        const bool digit = static_cast<uint8_t>(c - '0') <= 9;
        if (digit || c == '+') {
            if (c != '+') digits[length++] = c;
            do {
                if (!VirtualCall<ReadByteFn, kGetUint8Slot>(this, &c)) break;
                digits[length++] = c;
            } while (length <= kMaxDigits);
        }
    }
    digits[length] = 0;
    *static_cast<uint64_t *>(dst) = rztext::ParseInt64(digits);
    return Good();
}

uint8_t cRZIFStream::GetFloat32(void *dst)
{
    if (mBinary) return cRZIStream::GetFloat32(dst);
    *mStream >> *static_cast<float *>(dst);
    return Good();
}

uint8_t cRZIFStream::GetFloat64(void *dst)
{
    if (mBinary) return cRZIStream::GetFloat64(dst);
    *mStream >> *static_cast<double *>(dst);
    return Good();
}

uint8_t cRZIFStream::GetRZCharStr(char *buf, uint32_t size)
{
    if (mBinary) return cRZIStream::GetRZCharStr(buf, size);
    mStream->get(buf, static_cast<std::streamsize>(size), '\0');
    return Good();
}

uint8_t cRZIFStream::GetGZStr(cIGZString *out)
{
    if (mBinary) return cRZIStream::GetGZStr(out);
    LocalString text;
    char c = 0;
    // ORIGINAL-BUG (see the header): the original keeps looping after a failed read.
    while (*mStream >> c, !mStream->fail() && c != 0) text.text.mString.push_back(c);
    out->Copy(*text.text.AsInterface());
    return Good();
}

uint8_t cRZIFStream::GetVoid(void *dst, uint32_t size)
{
    mStream->read(static_cast<char *>(dst), static_cast<std::streamsize>(size));
    return Good();
}
