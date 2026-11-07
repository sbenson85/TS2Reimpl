// cDebugStream (lane FB): see rz/cDebugStream.h. RVAs are 1.18.0.187.
#include "rz/cDebugStream.h"

#include "rz/cRZVariant.h" // the owned format strings "%d" d9b648, "%u" d9d510, "%f" dce3c8

#include <new>

namespace {

struct LocalString {
    cRZString text;
    LocalString() { text.Construct(); }
    ~LocalString() { text.Destruct(); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
};

} // namespace

cDebugStream *cDebugStream::Construct(const cIGZString &path)
{
    cRZOFStream::Construct(path, 0, 0, 0, 0);
    mVTable = &ts2_rva_dcf704;
    mLine.Construct();
    mUnused.Construct();
    return this;
}

void cDebugStream::Destruct()
{
    mUnused.Destruct();
    mLine.Destruct();
    cRZOFStream::Destruct();
}

cDebugStream *cDebugStream::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

uint8_t cDebugStream::SetSint8(int8_t value)
{
    LocalString text;
    text.text.Sprintf(ts2_rva_d9b648, static_cast<int>(value));
    mLine.Append(*text.text.AsInterface());
    return static_cast<uint8_t>(cRZOFStream::SetSint8(value));
}

uint8_t cDebugStream::SetUint8(uint8_t value)
{
    LocalString text;
    text.text.Sprintf(ts2_rva_d9d510, static_cast<uint32_t>(value));
    mLine.Append(*text.text.AsInterface());
    return static_cast<uint8_t>(cRZOFStream::SetUint8(value));
}

uint8_t cDebugStream::SetSint16(int16_t value)
{
    LocalString text;
    text.text.Sprintf(ts2_rva_d9b648, static_cast<int>(value));
    mLine.Append(*text.text.AsInterface());
    return static_cast<uint8_t>(cRZOFStream::SetSint16(value));
}

uint8_t cDebugStream::SetUint16(uint16_t value)
{
    LocalString text;
    text.text.Sprintf(ts2_rva_d9d510, static_cast<uint32_t>(value));
    mLine.Append(*text.text.AsInterface());
    return static_cast<uint8_t>(cRZOFStream::SetUint16(value));
}

uint8_t cDebugStream::SetSint32(int32_t value)
{
    LocalString text;
    text.text.Sprintf(ts2_rva_d9b648, value);
    mLine.Append(*text.text.AsInterface());
    return static_cast<uint8_t>(cRZOFStream::SetSint32(value));
}

uint8_t cDebugStream::SetUint32(uint32_t value)
{
    LocalString text;
    text.text.Sprintf(ts2_rva_d9d510, value);
    mLine.Append(*text.text.AsInterface());
    return static_cast<uint8_t>(cRZOFStream::SetUint32(value));
}

uint8_t cDebugStream::SetFloat32(float value)
{
    LocalString text;
    text.text.Sprintf(ts2_rva_dce3c8, static_cast<double>(value));
    mLine.Append(*text.text.AsInterface());
    return static_cast<uint8_t>(cRZOFStream::SetFloat32(value));
}

uint8_t cDebugStream::SetRZCharStr(const char *text)
{
    LocalString line;
    line.text.FromChar(text);
    if (line.text.IsEqual(ts2_rva_1014980, 0xffffffffu, true)) mLine.mString.clear();
    else mLine.Append(*line.text.AsInterface());
    return cRZOFStream::SetRZCharStr(text);
}

uint8_t cDebugStream::SetGZStr(cIGZString *text)
{
    if (text->IsEqual(ts2_rva_1014980, 0xffffffffu, true)) mLine.mString.clear();
    else mLine.Append(*text);
    return static_cast<uint8_t>(cRZOFStream::SetGZStr(text));
}
