// cGZXmlWriter, cGZXmlBufferWriter and cGZXmlFileWriter (standalone shape; src/gz/cGZXmlWriter.h).
#include "gz/cGZXmlWriter.h"

#include "game/game.h"
#include "ifaces/cIGZFile.h"
#include "rz/RZFrameworkHelpers.h"

#include <cstring>
#include <new>

namespace {

const char *GameText(uint32_t rva)
{
    return GameAddress<const char>(rva);
}

uint32_t Length(const char *text)
{
    return static_cast<uint32_t>(std::strlen(text));
}

} // namespace

// ---- cGZXmlWriter ----

cGZXmlWriter::cGZXmlWriter(bool indent)
    : mRefCount(0), mIndent(indent), mInline(false), mState(kNone), mDepth(0), mIndentWidth(kIndentWidth)
{
}

cGZXmlWriter::~cGZXmlWriter() = default;

bool cGZXmlWriter::QueryInterface(uint32_t riid, void **ppv)
{
    if (ppv == nullptr) return false;
    if (riid != 1 && riid != cIGZXmlWriter::kIid) return false;
    *ppv = static_cast<cIGZXmlWriter *>(this);
    AddRef();
    return true;
}

uint32_t cGZXmlWriter::AddRef()
{
    return ++mRefCount;
}

uint32_t cGZXmlWriter::Release()
{
    const uint32_t count = --mRefCount;
    if (count != 0) return count;
    // ORIGINAL-BUG (see the header): destroyed and freed as a cGZXmlWriter whatever the class.
    this->~cGZXmlWriter();
    ::operator delete(this, kSize);
    return 0;
}

bool cGZXmlWriter::CloseOpen()
{
    const char *close;
    uint32_t length;
    switch (mState) {
    case kStartTag:
        close = GameText(gzxml::kCloseTag);
        length = 1;
        break;
    case kProcessingInstruction:
        close = GameText(gzxml::kClosePI);
        length = 2;
        break;
    case kCData:
        close = GameText(gzxml::kCloseCData);
        length = 3;
        break;
    default:
        mState = kNone;
        return true;
    }
    if (!Write(close, length)) return false;
    mState = kNone;
    return true;
}

bool cGZXmlWriter::WriteEscaped(const char *text)
{
    const char *start = text;
    const char *p = text;
    for (; *p != '\0'; ++p) {
        const char *entity;
        uint32_t length;
        switch (*p) {
        case '"':
            entity = GameText(gzxml::kQuotEntity);
            length = 6;
            break;
        case '&':
            entity = GameText(gzxml::kAmpEntity);
            length = 5;
            break;
        case '\'':
            entity = GameText(gzxml::kAposEntity);
            length = 6;
            break;
        case '<':
            entity = GameText(gzxml::kLtEntity);
            length = 4;
            break;
        case '>':
            entity = GameText(gzxml::kGtEntity);
            length = 4;
            break;
        default:
            continue;
        }
        if (start < p && !Write(start, static_cast<uint32_t>(p - start))) return false;
        if (!Write(entity, length)) return false;
        start = p + 1;
    }
    if (start < p) return Write(start, static_cast<uint32_t>(p - start));
    return true;
}

bool cGZXmlWriter::WriteIndent()
{
    if (!mIndent) return true;
    uint32_t remaining = mIndentWidth * mDepth;
    uint32_t chunk = remaining < kMaxIndentChunk ? remaining : kMaxIndentChunk;
    bool ok = Write(GameText(gzxml::kNewlineIndent), chunk + 1);
    while (ok) {
        remaining -= chunk;
        if (static_cast<int32_t>(remaining) < 1) return true;
        chunk = remaining < kMaxIndentChunk ? remaining : kMaxIndentChunk;
        ok = Write(GameText(gzxml::kIndentSpaces), chunk);
    }
    return false;
}

bool cGZXmlWriter::StartDocument()
{
    return StartProcessingInstruction(GameText(gzxml::kXmlTarget)) &&
           Attribute(GameText(gzxml::kVersionName), GameText(gzxml::kVersionValue)) &&
           Attribute(GameText(gzxml::kEncodingName), GameText(gzxml::kEncodingValue)) && EndProcessingInstruction();
}

bool cGZXmlWriter::StartElement(const char *name)
{
    if (!CloseOpen() || !WriteIndent() || !Write(GameText(gzxml::kOpenTag), 1) || !Write(name, Length(name))) {
        return false;
    }
    ++mDepth;
    mState = kStartTag;
    mInline = true;
    return true;
}

bool cGZXmlWriter::EndElement(const char *name)
{
    const State state = mState;
    --mDepth;
    switch (state) {
    case kNone:
        break;
    case kStartTag:
        mState = kNone;
        mInline = false;
        return Write(GameText(gzxml::kCloseEmptyTag), 2);
    case kCData:
        CloseOpen();
        break;
    default:
        return false;
    }
    if (!mInline && !WriteIndent()) return false;
    mInline = false;
    return Write(GameText(gzxml::kOpenEndTag), 2) && Write(name, Length(name)) && Write(GameText(gzxml::kCloseTag), 1);
}

bool cGZXmlWriter::StartProcessingInstruction(const char *target)
{
    if (!CloseOpen() || !Write(GameText(gzxml::kOpenPI), 2) || !Write(target, Length(target))) return false;
    mInline = false;
    mState = kProcessingInstruction;
    return true;
}

bool cGZXmlWriter::EndProcessingInstruction()
{
    const State state = mState;
    mInline = false;
    if (state == kNone || state == kStartTag) {
        mState = kNone;
        return false;
    }
    if (state != kProcessingInstruction) return false;
    mState = kNone;
    return Write(GameText(gzxml::kClosePI), 2);
}

bool cGZXmlWriter::Attribute(const char *name, const char *value)
{
    if (mState != kStartTag && mState != kProcessingInstruction) return false;
    return Write(GameText(gzxml::kSpace), 1) && Write(name, Length(name)) && Write(GameText(gzxml::kEqualsQuote), 2) &&
           WriteEscaped(value) && Write(GameText(gzxml::kQuote), 1);
}

bool cGZXmlWriter::Text(const char *text)
{
    return CloseOpen() && WriteEscaped(text);
}

bool cGZXmlWriter::CData(const char *text)
{
    if (mState != kCData) {
        if (!CloseOpen() || !Write(GameText(gzxml::kOpenCData), 9)) return false;
        mState = kCData;
    }
    return Write(text, Length(text));
}

bool cGZXmlWriter::Comment(const char *text)
{
    if (!CloseOpen() || !Write(GameText(gzxml::kOpenComment), 5)) return false;
    const char *p = text;
    char c = *p;
    if (c != '\0') {
        do {
            ++p;
            if (c == '-') {
                // ORIGINAL-BUG (see the header): `text` is never moved past what was written.
                if (text < p && !Write(text, static_cast<uint32_t>(p - text + 1))) return false;
                while (*p == '-') ++p;
            }
            c = *p;
        } while (c != '\0');
        if (text < p && !Write(text, static_cast<uint32_t>(p - text))) return false;
    }
    return Write(GameText(gzxml::kCloseComment), 4);
}

// ---- cGZXmlBufferWriter ----

cGZXmlBufferWriter::cGZXmlBufferWriter() : cGZXmlWriter(true)
{
    mText.Construct();
}

cGZXmlBufferWriter::~cGZXmlBufferWriter()
{
    mText.Destruct();
}

bool cGZXmlBufferWriter::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != cIGZXmlBufferWriter::kIid) return cGZXmlWriter::QueryInterface(riid, ppv);
    *ppv = static_cast<cIGZXmlBufferWriter *>(this);
    AddRef();
    return true;
}

uint32_t cGZXmlBufferWriter::AddRef()
{
    return cGZXmlWriter::AddRef();
}

uint32_t cGZXmlBufferWriter::Release()
{
    return cGZXmlWriter::Release();
}

bool cGZXmlBufferWriter::Write(const char *text, uint32_t length)
{
    mText.mString.append(text, length);
    return true;
}

cIGZString *cGZXmlBufferWriter::GetString()
{
    return mText.AsInterface();
}

const char *cGZXmlBufferWriter::GetText()
{
    return mText.AsInterface()->ToChar();
}

uint32_t cGZXmlBufferWriter::GetLength()
{
    return mText.AsInterface()->Strlen();
}

// ---- cGZXmlFileWriter ----

cGZXmlFileWriter::cGZXmlFileWriter() : cGZXmlWriter(true), mStream(nullptr)
{
}

bool cGZXmlFileWriter::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != cIGZXmlFileWriter::kIid) return cGZXmlWriter::QueryInterface(riid, ppv);
    *ppv = static_cast<cIGZXmlFileWriter *>(this);
    AddRef();
    return true;
}

uint32_t cGZXmlFileWriter::AddRef()
{
    return cGZXmlWriter::AddRef();
}

uint32_t cGZXmlFileWriter::Release()
{
    return cGZXmlWriter::Release();
}

bool cGZXmlFileWriter::Write(const char *text, uint32_t length)
{
    return reinterpret_cast<ifaces::cIGZFile *>(mStream)->Write(const_cast<char *>(text), length) != 0;
}

void cGZXmlFileWriter::SetStream(cIGZUnknown *stream, bool indent)
{
    reinterpret_cast<RZUnknownSlot *>(&mStream)->Assign(stream);
    mIndent = indent;
}

// ---- adapters ----

cGZXmlBufferWriter *__cdecl cGZXmlWriterAdapters::CreateBufferWriter()
{
    return new (std::nothrow) cGZXmlBufferWriter;
}

cGZXmlFileWriter *__cdecl cGZXmlWriterAdapters::CreateFileWriter()
{
    return new (std::nothrow) cGZXmlFileWriter;
}

cGZXmlBufferWriter *__fastcall cGZXmlWriterAdapters::ConstructBufferWriter(void *memory, void *)
{
    return ::new (memory) cGZXmlBufferWriter;
}

cGZXmlFileWriter *__fastcall cGZXmlWriterAdapters::ConstructFileWriter(void *memory, void *)
{
    return ::new (memory) cGZXmlFileWriter;
}
