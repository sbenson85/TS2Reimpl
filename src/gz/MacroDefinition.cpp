// The command parser's macro definitions (lane NV). See gz/MacroDefinition.h.
#include "gz/MacroDefinition.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cBaseParser.h"
#include "gz/cIGZString.h"
#include "gz/cParserTexts.h"
#include "ifaces/cIBaseParser.h"
#include "ifaces/cIGZOStream.h"
#include "ifaces/cRZRefCount.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <string_view>

namespace nGZCommandParser {

namespace {

// The parser's character classes (the game's table at 0xe7d528: bit 2 letters, bit 4 digits; bytes from 0x80 up are in
// none).
bool IsDigit(unsigned char c) { return c >= '0' && c <= '9'; }
bool IsLetterOrDigit(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || IsDigit(c); }
bool IsNameChar(unsigned char c) { return IsLetterOrDigit(c) || c == '_'; }

ifaces::cIBaseParser *ParserFace(cBaseParser *parser) { return As<ifaces::cIBaseParser>(parser->ParserFace()); }
ifaces::cIGZOStream *Output(cBaseParser *parser) { return As<ifaces::cIGZOStream>(parser->mOutput); }
void Write(cBaseParser *parser, const char *text) { Output(parser)->SetRZCharStr(text); }

DefinitionVector &Definitions(cBaseParser *parser) { return *reinterpret_cast<DefinitionVector *>(&parser->mDefinitions); }
PendingCallVector &PendingCalls(cBaseParser *parser) { return *reinterpret_cast<PendingCallVector *>(&parser->mPendingCalls); }

// The parameter names of a held definition (+0x20), called through its vtable as the original does.
cIArguments *Parameters(void *definition) { return reinterpret_cast<cIArguments *>(static_cast<char *>(definition) + 0x20); }
// A pending call's arguments (+4), likewise through their vtable (taken from the address, so the compiler does not
// bind the call to cArguments' own member).
cIArguments *CallArguments(PendingCall &call) { return reinterpret_cast<cIArguments *>(reinterpret_cast<char *>(&call) + 4); }

} // namespace

// ---- cMacroDefinition -----------------------------------------------------------------------------------------------

cMacroDefinition::cMacroDefinition()
{
    mVTable = &ts2_rva_e7db0c;
    mRefCount = 0;
}

void cMacroDefinition::Destruct()
{
    mFile.~basic_string();
    mLines.~vector();
    mBody.~vector();
    mParameters.~cArguments();
    mName.~basic_string();
    cRZRefCount::Destruct();
}

cMacroDefinition *cMacroDefinition::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

uint32_t cMacroDefinition::AddRef()
{
    return ++mRefCount;
}

uint32_t cMacroDefinition::RemoveRef()
{
    if (mRefCount != 0) --mRefCount;
    return mRefCount;
}

uint32_t cMacroDefinition::RefCount() const
{
    return mRefCount;
}

// ---- held definitions -------------------------------------------------------------------------------------------------

DefinitionRef::DefinitionRef(void *definition) : mDefinition(definition)
{
    if (mDefinition) As<ifaces::cRZRefCount>(mDefinition)->AddRef();
}

DefinitionRef::DefinitionRef(const DefinitionRef &other) : mDefinition(other.mDefinition)
{
    if (mDefinition) As<ifaces::cRZRefCount>(mDefinition)->AddRef();
}

DefinitionRef &DefinitionRef::operator=(const DefinitionRef &other)
{
    if (this != &other && mDefinition != other.mDefinition) {
        if (other.mDefinition) As<ifaces::cRZRefCount>(other.mDefinition)->AddRef();
        void *old = mDefinition;
        mDefinition = other.mDefinition;
        if (old) As<ifaces::cRZRefCount>(old)->Release();
    }
    return *this;
}

DefinitionRef::~DefinitionRef()
{
    if (mDefinition) As<ifaces::cRZRefCount>(mDefinition)->Release();
}

DefinitionRef **DefinitionVector::Insert(DefinitionRef **out, DefinitionRef *where, const DefinitionRef *value)
{
    *out = &*mVector.emplace(mVector.begin() + (where - mVector.data()), *value);
    return out;
}

DefinitionRef *DefinitionVector::EmplaceReallocate(DefinitionRef *where, const DefinitionRef *value)
{
    // Precondition (Insert's, the only caller): size() == capacity(), so emplace takes the reallocating path.
    return &*mVector.emplace(mVector.begin() + (where - mVector.data()), *value);
}

DefinitionRef *__cdecl CopyDefinitions(const DefinitionRef *first, const DefinitionRef *last, DefinitionRef *dest, void * /*allocator*/)
{
    DefinitionRef *copied = dest;
    try {
        for (; first != last; ++first, ++copied) new (copied) DefinitionRef(*first);
    } catch (...) {
        for (DefinitionRef *p = dest; p != copied; ++p) p->~DefinitionRef();
        throw;
    }
    return copied;
}

// ---- the definition searches ------------------------------------------------------------------------------------------

bool NameBefore(const char *aBegin, const char *aEnd, const char *bBegin, const char *bEnd)
{
    if (aEnd - aBegin != bEnd - bBegin) return static_cast<size_t>(aEnd - aBegin) < static_cast<size_t>(bEnd - bBegin);
    while (aEnd != aBegin) {
        const signed char x = *--aEnd, y = *--bEnd;
        if (x < y) return true;
        if (x > y) return false;
    }
    return false;
}

bool DefinitionKey::DefinitionBefore(const DefinitionRef *entry, uint32_t /*unused*/) const
{
    const std::string &name = entry->Name();
    return NameBefore(name.data(), name.data() + name.size(), begin, end);
}

bool DefinitionKey::BeforeDefinition(uint32_t /*unused*/, const DefinitionRef *entry) const
{
    const std::string &name = entry->Name();
    return NameBefore(begin, end, name.data(), name.data() + name.size());
}

DefinitionRef *__cdecl LowerBoundDefinition(DefinitionRef *first, DefinitionRef *last, const void * /*keyWord*/, const DefinitionKey *key)
{
    return std::lower_bound(first, last, *key,
                            [](const DefinitionRef &entry, const DefinitionKey &k) { return k.DefinitionBefore(&entry, 0); });
}

DefinitionRange *__cdecl EqualRangeDefinitions(DefinitionRange *out, DefinitionRef *first, DefinitionRef *last, const void * /*keyWord*/,
                                               DefinitionKey key)
{
    struct Order {
        bool operator()(const DefinitionRef &entry, const DefinitionKey &k) const { return k.DefinitionBefore(&entry, 0); }
        bool operator()(const DefinitionKey &k, const DefinitionRef &entry) const { return k.BeforeDefinition(0, &entry); }
    };
    const auto found = std::equal_range(first, last, key, Order{});
    out->first = found.first;
    out->last = found.second;
    return out;
}

// ---- define -----------------------------------------------------------------------------------------------------------

void cDefineCommand::Execute(cIArguments *args)
{
    // define name "parameters": starts recording a definition (not inside another one, not under a name defined
    // already). This command becomes the innermost meta block, so the lines up to enddef come to HandleLine.
    args->CheckSwitchesUsed();
    cBaseParser *parser = static_cast<cBaseParser *>(mState);
    ThrowUnless(parser->mPendingCalls.first == parser->mPendingCalls.last, ts2_rva_e7e718);
    const char *const *a = args->PositionalExactly(2);
    const DefinitionKey key{a[0] + std::strlen(a[0]), a[0]};
    parser = static_cast<cBaseParser *>(mState);
    DefinitionVector &definitions = Definitions(parser);
    DefinitionRef *last = definitions.mVector.data() + definitions.mVector.size();
    DefinitionRef *place = LowerBoundDefinition(definitions.mVector.data(), last, a, &key);
    if (place != last && !key.BeforeDefinition(reinterpret_cast<uint32_t>(a[0]), place))
        ThrowFormatted(ts2_rva_e7e72c);
    ParserFace(static_cast<cBaseParser *>(mState))->PushMetaBlock(this);
    parser = static_cast<cBaseParser *>(mState);
    if (parser->mFieldED) {
        // Echo: the command's name, then name(parameters).
        ifaces::cIGZOStream *output = Output(parser);
        output->SetRZCharStr(args->Argument(0));
        output->SetRZCharStr(ts2_rva_da4ca4);
        output->SetRZCharStr(a[0]);
        output->SetRZCharStr(ts2_rva_db12b4);
        output->SetRZCharStr(a[1]);
        output->SetRZCharStr(ts2_rva_db12b0);
        output->SetRZCharStr(ts2_rva_d9e644);
    }
    // The new definition becomes the one being recorded (a held pointer's assignment: the new one referenced, then the
    // old one dropped).
    cMacroDefinition *definition = new cMacroDefinition();
    void *&recorded = static_cast<cBaseParser *>(mState)->mCurrentDefinition;
    if (definition != recorded) {
        void *old = recorded;
        As<ifaces::cRZRefCount>(definition)->AddRef();
        recorded = definition;
        if (old) As<ifaces::cRZRefCount>(old)->Release();
    }
    auto *current = static_cast<cMacroDefinition *>(static_cast<cBaseParser *>(mState)->mCurrentDefinition);
    current->mName.assign(a[0], std::strlen(a[0]));
    Parameters(static_cast<cBaseParser *>(mState)->mCurrentDefinition)->Parse(a[1], 0, 1);
    parser = static_cast<cBaseParser *>(mState);
    current = static_cast<cMacroDefinition *>(parser->mCurrentDefinition);
    if (&current->mFile != &parser->mFileName) current->mFile.assign(parser->mFileName.data(), parser->mFileName.size());
    current->mFirstLine = static_cast<int32_t>(parser->mLine);
    current->mLevel = static_cast<int32_t>(parser->mField158);
}

bool cDefineCommand::HandleLine(void *line, uint8_t running)
{
    // Every line while the definition records: "enddef" (the first word) ends it: the line runs when the text around
    // it runs (the enddef command files the definition); otherwise the recording is dropped here. Any other line with
    // a word is added to the body when the text runs, and echoed when the parser echoes.
    cIGZString *text = static_cast<cIGZString *>(line);
    uint32_t length;
    const char *word = NextToken(&length, text->ToChar());
    if (word && length == ts2_rva_10749a0 && std::strncmp(word, ts2_rva_e7e73c, length) == 0) {
        if (running) return true;
        ParserFace(static_cast<cBaseParser *>(mState))->PopMetaBlock();
        return false;
    }
    if (running && word) {
        auto *definition = static_cast<cMacroDefinition *>(static_cast<cBaseParser *>(mState)->mCurrentDefinition);
        const char *chars = text->ToChar();
        definition->mLines.push_back(MacroLine{static_cast<int32_t>(static_cast<cBaseParser *>(mState)->mLine), MacroLine::kNotRun});
        const uint32_t size = text->Strlen();
        definition->mBody.insert(definition->mBody.end(), chars, chars + size + 1);
        cBaseParser *parser = static_cast<cBaseParser *>(mState);
        if (parser->mFieldED) {
            Write(parser, ts2_rva_dfd044);
            Write(parser, chars);
            Write(parser, ts2_rva_d9e644);
        }
    }
    return false;
}

// ---- parameter substitution -------------------------------------------------------------------------------------------

void cMacroParser::ExpandParameters(std::string *text)
{
    cBaseParser *parser = reinterpret_cast<cBaseParser *>(this);
    PendingCallVector &calls = PendingCalls(parser);
    if (calls.empty()) return;
    constexpr size_t npos = std::string::npos;
    size_t amp = text->size() ? text->find('&') : npos;
    if (amp == npos) return;
    std::string result;
    size_t start = 0;
    for (;;) {
        // The text up to the '&' (or the rest of it) is copied as it is.
        result.append(text->data() + start, std::min(amp - start, text->size() - start));
        if (amp == npos) break;
        size_t at = amp + 1;
        size_t next, searchFrom;
        if (text->data()[at] == '&') {
            // "&&": the second '&' starts the next copy.
            next = at;
            searchFrom = at + 1;
        } else {
            const bool braced = text->data()[at] == '{';
            if (braced) ++at;
            const size_t size = text->size();
            if (at >= size) ThrowFormatted(ts2_rva_e7f474);
            const unsigned char first = static_cast<unsigned char>(text->data()[at]);
            if (!IsNameChar(first) || IsDigit(first)) ThrowFormatted(ts2_rva_e7f48c);
            size_t end = at + 1;
            while (end < size && IsNameChar(static_cast<unsigned char>(text->data()[end]))) ++end;
            if (braced && (end >= size || text->data()[end] != '}')) ThrowFormatted(ts2_rva_e7f3f4);
            // The parameter's index among the definition's parameter names; the call's argument of that index.
            PendingCall &call = calls.back();
            cIArguments *names = Parameters(call.held.definition);
            const std::string_view name(text->data() + at, end - at);
            uint32_t index = 0;
            if (names->Count() != 0) {
                for (;;) {
                    if (name == names->Argument(index)) {
                        if (!braced) result.push_back('(');
                        result.append(CallArguments(call)->Argument(index));
                        if (!braced) result.push_back(')');
                        break;
                    }
                    ++index;
                    if (index >= static_cast<uint32_t>(names->Count())) break;
                }
            }
            if (index == static_cast<uint32_t>(names->Count())) {
                // The message is the error's format string (a name holds no '%').
                std::string message(ts2_rva_e7f4a0);
                message.append(*text, at, end - at);
                ThrowFormatted(message.c_str());
            }
            if (braced) ++end;
            next = end;
            searchFrom = end;
        }
        const size_t size = text->size();
        amp = searchFrom < size ? text->find('&', searchFrom) : npos;
        if (next >= size) break;
        start = next;
    }
    if (text != &result) text->assign(result.data(), result.size());
}

} // namespace nGZCommandParser
