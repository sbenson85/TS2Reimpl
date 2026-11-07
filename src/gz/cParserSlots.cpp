// nGZCommandParser::cBaseParser's virtual functions (lane HU), hook-shaped: members with `this` = the whole parser.
// The game's bodies take `this` as the slot's face (the cIBaseParser face's bodies at +0x1b8, the cIGZUnknown
// face's at +0x1b0) and reach the fields at fixed offsets from it, whatever the most derived class: the vtordisp
// thunks in front of them take the derived class's extra displacement off first. Calls the original makes through
// the parser's own faces go through the vtables it finds through the vbtable at +8, as here (a derived parser may
// override those slots). RVAs are 1.18.0.187.
#include "gz/cBaseParser.h"
#include "gz/ExpressionEvaluator.h"
#include "gz/cParserTexts.h"
#include "rz/RZFileStreams.h"

#include "app/AppDebugger.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"

#include <algorithm>
#include <bit>
#include <cstring>

namespace nGZCommandParser {

namespace {

using VoidFn = void(__thiscall *)(void *);
using CountFn = uint32_t(__thiscall *)(void *);
using NameFn = const char *(__thiscall *)(void *);

// The slots of the objects the parser keeps counted pointers to. Commands, the output and the source count with
// slot 1 and release with slot 2; the macro definitions (+0x6c, +0x15c) are the other way round.
inline void Count(void *object) { VirtualCall<CountFn, 1>(object); }
inline void Uncount(void *object) { VirtualCall<CountFn, 2>(object); }
inline void DefinitionAddRef(void *definition) { VirtualCall<CountFn, 2>(definition); }
inline void DefinitionRelease(void *definition) { VirtualCall<CountFn, 1>(definition); }

// Assigns a counted pointer as the original's smart pointer does: nothing when it is the same; otherwise the new one
// counted, stored, and the old one released.
inline void AssignCounted(void *&slot, void *value)
{
    void *old = slot;
    if (value == old) return;
    if (value) Count(value);
    slot = value;
    if (old) Uncount(old);
}

inline void Write(void *output, const char *text) { VirtualCall<void(__thiscall *)(void *, const char *), 14>(output, text); }

constexpr const char *kLeavingNamespace = ts2_rva_e7f364; // "leaving namespace '"
constexpr const char *kQuoteNewline = ts2_rva_d9f86c;     // the closing quote and the line end
constexpr const char *kNotScalar = ts2_rva_e79e14;        // "not a scalar expression"
constexpr const char *kTooMany = ts2_rva_e7f418;          // "too many elements in expression"
constexpr const char *kTooFew = ts2_rva_e7f438;           // "too few elements in expression"
constexpr const char *kNotColor = ts2_rva_de7c1c;         // "not a 3 or 4 element float expression"
constexpr const char *kNotInMetaBlock = ts2_rva_e7f328;   // "Not in a meta command parse block"
constexpr const char *kEmptyText = ts2_rva_d9b9d0;        // the game's empty string
constexpr uint32_t kIGZUnknownIid = 1;
constexpr uint32_t kIBaseParserIid = 0x95035f4;

// What the expression evaluator gives back: up to four floats and how many there are.
struct EvalResult {
    float values[4];
    int32_t count;
};

// The evaluator's entry point (0xb0a870, ours: gz/ExpressionEvaluator.h): evaluates `text` into *out and throws a
// cError when anything follows the expression. Returns out.
static_assert(sizeof(EvalResult) == sizeof(gzexpr::Value), "EvalResult is the evaluator's value");
EvalResult *Evaluate(EvalFunctionTable *functions, EvalResult *out, const char *text)
{
    nGZCommandParser::ExpressionEvaluator::Of(functions)->Evaluate(reinterpret_cast<gzexpr::Value *>(out), text);
    return out;
}

[[noreturn]] void ThrowText(const char *text) { ThrowFormatted(text); }

// A vector of `width` floats from the expression: at most `width` of them, at least `minimum`.
template <int Width> void EvalVector(EvalFunctionTable *functions, float *out, const char *text, uint32_t minimum)
{
    EvalResult result;
    Evaluate(functions, &result, text);
    if (result.count > Width) ThrowText(kTooMany);
    if (static_cast<uint32_t>(result.count) < minimum) ThrowText(kTooFew);
    for (int i = 0; i < Width; ++i) out[i] = result.values[i];
}

// The macro definitions are kept sorted by name: shorter names first, names of one length by their bytes compared
// from the last one backwards (as signed chars). Each definition holds its name as a std::string at +8.
inline const std::string &DefinitionName(const void *definition)
{
    return *reinterpret_cast<const std::string *>(static_cast<const char *>(definition) + 8);
}
// 0xafea90: the definition sorts before the key.
bool DefinitionBeforeKey(const void *definition, const char *keyBegin, const char *keyEnd)
{
    const std::string &name = DefinitionName(definition);
    const size_t keyLength = static_cast<size_t>(keyEnd - keyBegin);
    if (name.size() != keyLength) return name.size() < keyLength;
    const signed char *p = reinterpret_cast<const signed char *>(name.data()) + name.size();
    const signed char *begin = reinterpret_cast<const signed char *>(name.data());
    const signed char *k = reinterpret_cast<const signed char *>(keyEnd);
    const signed char *kBegin = reinterpret_cast<const signed char *>(keyBegin);
    for (;;) {
        if (p == begin) return k != kBegin;
        if (k == kBegin) return false;
        --p;
        --k;
        if (*p < *k) return true;
        if (*p > *k) return false;
    }
}
// 0xafeb10: the key sorts before the definition.
bool KeyBeforeDefinition(const char *keyBegin, const char *keyEnd, const void *definition)
{
    const std::string &name = DefinitionName(definition);
    const size_t keyLength = static_cast<size_t>(keyEnd - keyBegin);
    if (keyLength != name.size()) return keyLength < name.size();
    const signed char *p = reinterpret_cast<const signed char *>(name.data()) + name.size();
    const signed char *begin = reinterpret_cast<const signed char *>(name.data());
    const signed char *k = reinterpret_cast<const signed char *>(keyEnd);
    const signed char *kBegin = reinterpret_cast<const signed char *>(keyBegin);
    for (;;) {
        if (k == kBegin) return p != begin;
        if (p == begin) return false;
        --k;
        --p;
        if (*k < *p) return true;
        if (*k > *p) return false;
    }
}

// The hash table's node removal (the original's code inline in UnregisterCommand): the node leaves its bucket (the
// bucket's first and last node move past it), the count drops, the node is unlinked, its command released and the
// node freed.
void EraseCommandNode(CommandTable &table, CommandNode *node)
{
    CommandNode **bucket = table.buckets.data() + 2 * (HashName(node->key, node->keyLength) & table.mask);
    if (bucket[1] == node) {
        if (bucket[0] == node) {
            bucket[0] = table.head;
            bucket[1] = table.head;
        } else {
            bucket[1] = node->prev;
        }
    } else if (bucket[0] == node) {
        bucket[0] = node->next;
    }
    CommandNode *next = node->next;
    --table.size;
    node->prev->next = next;
    next->prev = node->prev;
    if (node->value) Uncount(node->value);
    ::operator delete(node, sizeof(CommandNode));
}

} // namespace

void InsertEvalFunction(EvalFunctionTable *functions, const char *name, cEvalFunction *function)
{
    auto &entries = functions->mEntries;
    auto where = std::lower_bound(entries.begin(), entries.end(), name,
                                  [](const EvalFunctionTable::Entry &e, const char *key) { return std::strcmp(e.first, key) < 0; });
    entries.insert(where, EvalFunctionTable::Entry{name, function});
}

void __fastcall cBaseParserAdapters::InsertEvalFunction(EvalFunctionTable *functions, void *, const char *name, cEvalFunction *function)
{
    nGZCommandParser::InsertEvalFunction(functions, name, function);
}

void **__cdecl MoveCountedPointers(void **first, void **last, void **dest)
{
    for (; first != last; ++first, ++dest) {
        if (first == dest) continue;
        void *value = *first;
        void *old = *dest;
        if (value == old) continue;
        if (value) DefinitionAddRef(value);
        *dest = value;
        if (old) DefinitionRelease(old);
    }
    return dest;
}

// ---- the cIGZUnknown face ----

bool cBaseParser::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == kIGZUnknownIid) *out = UnknownFace();
    else if (iid == kIBaseParserIid) *out = ParserFace();
    else return false;
    VirtualCall<CountFn, 1>(UnknownFace());
    return true;
}

uint32_t cBaseParser::AddRef() { return ++mRefCount; }

uint32_t cBaseParser::Release()
{
    const uint32_t count = --mRefCount;
    if (count != 0) return count;
    VirtualCall<void *(__thiscall *)(void *, uint32_t), 0>(this, 1); // the deleting destructor
    return 0;
}

// ---- the primary vtable ----

void cBaseParser::DropDefinitionsOfLevel(int32_t level)
{
    auto levelOf = [](void *definition) { return *reinterpret_cast<int32_t *>(static_cast<char *>(definition) + 0x68); };
    void **first = static_cast<void **>(mDefinitions.first);
    void **last = static_cast<void **>(mDefinitions.last);
    void **kept = first;
    while (kept != last && levelOf(*kept) != level) ++kept;
    if (kept != last) {
        for (void **p = kept + 1; p != last; ++p) {
            void *value = *p;
            if (levelOf(value) == level) continue;
            if (p != kept) {
                void *old = *kept;
                if (value != old) {
                    if (value) DefinitionAddRef(value);
                    *kept = value;
                    if (old) DefinitionRelease(old);
                }
            }
            ++kept;
        }
    }
    void **end = static_cast<void **>(mDefinitions.last);
    if (kept == end) return;
    void **newEnd = MoveCountedPointers(end, end, kept);
    for (void **p = newEnd; p != end; ++p)
        if (*p) DefinitionRelease(*p);
    mDefinitions.last = newEnd;
}

// ---- the cIBaseParser face ----

bool cBaseParser::Shutdown()
{
    if (!mInitialised) return false;
    mInitialised = 0;
    if (mCommands.size != 0) {
        CommandTable::ClearNodes(&mCommands.head, mCommands.head);
        mCommands.head->next = mCommands.head;
        mCommands.head->prev = mCommands.head;
        mCommands.size = 0;
        std::fill(mCommands.buckets.begin(), mCommands.buckets.end(), mCommands.head);
    }
    VirtualCall<VoidFn, 9>(ParserFace()); // Reset
    if (void *output = mOutput) {
        mOutput = nullptr;
        Uncount(output);
    }
    for (void **p = mCommandList.mFirst, **end = mCommandList.mLast; p != end; ++p)
        if (*p) Uncount(*p);
    mCommandList.mLast = mCommandList.mFirst;
    for (void **p = mMetaCommands.mFirst, **end = mMetaCommands.mLast; p != end; ++p)
        if (*p) Uncount(*p);
    mMetaCommands.mLast = mMetaCommands.mFirst;
    return true;
}

void cBaseParser::SetVersionRange(int32_t lowest, int32_t highest)
{
    mLowestVersion = lowest;
    mHighestVersion = highest;
    mVersionRangeSet = 1;
}

void cBaseParser::SetContextState(void *state) { mContextState = state; }

void cBaseParser::RegisterCommand(void *command)
{
    KeyView key{VirtualCall<NameFn, 5>(command), 0};
    key.length = static_cast<uint32_t>(std::strlen(key.ptr));
    if (!mCaseSensitive) {
        KeyView stored;
        key = *mNames.Store(&stored, &key);
        FoldName(&key); // folds the pool's copy
    }
    InsertResult inserted;
    mCommands.Insert(&inserted, &key);
    AssignCounted(inserted.node->value, command);
    void *held = command; // the original's temporary counted pointer
    Count(held);
    if (mCommandList.mLast == mCommandList.mEnd) {
        mCommandList.EmplaceReallocate(mCommandList.mLast, &held);
    } else {
        *mCommandList.mLast = command;
        Count(command);
        ++mCommandList.mLast;
    }
    Uncount(held);
    void *state = mContextState ? mContextState : &mStateId; // the parser's own cState
    VirtualCall<void(__thiscall *)(void *, void *, void *), 3>(command, ParserFace(), state); // SetContext
}

bool cBaseParser::UnregisterCommand(const char *name)
{
    KeyView key{name, static_cast<uint32_t>(std::strlen(name))};
    char folded[128];
    if (!mCaseSensitive) {
        // ORIGINAL-BUG: the name is copied into a 128-byte stack buffer without a length check; longer names overrun it.
        std::strcpy(folded, name);
        key = {folded, static_cast<uint32_t>(std::strlen(folded))};
        FoldName(&key);
    }
    FindResult found;
    mCommands.Find(&found, &key, HashName(key.ptr, key.length));
    CommandNode *node = found.found ? found.found : mCommands.head;
    if (node == mCommands.head) return false;
    void **first = mCommandList.mFirst, **last = mCommandList.mLast;
    void **at = std::find(first, last, node->value);
    if (at == last) return false;
    EraseCommandNode(mCommands, node);
    // The list closes the gap by assignment, then the last element is released.
    for (void **p = at; p + 1 != last; ++p) {
        void *value = p[1], *old = p[0];
        if (value == old) continue;
        if (value) Count(value);
        p[0] = value;
        if (old) Uncount(old);
    }
    if (void *tail = mCommandList.mLast[-1]) Uncount(tail);
    --mCommandList.mLast;
    return true;
}

bool cBaseParser::UnregisterCommandObject(void *command)
{
    return VirtualCall<bool(__thiscall *)(void *, const char *), 5>(ParserFace(), VirtualCall<NameFn, 5>(command));
}

void cBaseParser::RegisterMetaCommand(void *command)
{
    void *state = mContextState ? mContextState : &mStateId;
    VirtualCall<void(__thiscall *)(void *, void *, void *), 3>(command, ParserFace(), state); // SetContext
    void *held = command;
    Count(held);
    if (mMetaCommands.mLast == mMetaCommands.mEnd) {
        mMetaCommands.EmplaceReallocate(mMetaCommands.mLast, &held);
    } else {
        *mMetaCommands.mLast = command;
        Count(command);
        ++mMetaCommands.mLast;
    }
    Uncount(held);
    // A meta command is an expression function: slot 4 gives the evaluator's object, slot 5 its name.
    cEvalFunction *function = VirtualCall<cEvalFunction *(__thiscall *)(void *), 4>(command);
    InsertEvalFunction(&mFunctions, VirtualCall<NameFn, 5>(command), function);
}

void cBaseParser::RestartViaReset() { VirtualCall<VoidFn, 9>(ParserFace()); }

void cBaseParser::Reset()
{
    mFieldF4 = 0;
    mField158 = 0;
    for (void **p = static_cast<void **>(mDefinitions.first), **end = static_cast<void **>(mDefinitions.last); p != end; ++p)
        if (*p) DefinitionRelease(*p);
    if (mDefinitions.first != mDefinitions.last) mDefinitions.last = mDefinitions.first;
    mMetaBlocks.clear();
    mOpenBlocks.clear();
    mConditions.resize(1);
    mGlobalNamespace.Clear(true);
    mNamespacePath.clear();
    mNamespaceLengths.clear();
    mNamespaces.clear();
    mNamespaces.push_back(&mGlobalNamespace);
    if (void *definition = mCurrentDefinition) {
        mCurrentDefinition = nullptr;
        DefinitionRelease(definition);
    }
    if (mPendingCalls.first != mPendingCalls.last) {
        DestroyPendingCalls(static_cast<PendingCall *>(mPendingCalls.first), static_cast<PendingCall *>(mPendingCalls.last), &mPendingCalls);
        mPendingCalls.last = mPendingCalls.first;
    }
    mText.mString.clear();
    mField198 = 0;
    mFlagE8 = 0;
    mFileName.clear();
    mLine = 0;
    mTraceNamespaces = 0;
    mFieldEA = 0;
    mTraceVariables = 0;
    mTrace = 0;
    mFieldED = 0;
    mFieldEE = 0;
    mVersion = -1;
}

void cBaseParser::ResetBlocks()
{
    mMetaBlocks.clear();
    mOpenBlocks.clear();
    if (void *definition = mCurrentDefinition) {
        mCurrentDefinition = nullptr;
        DefinitionRelease(definition);
    }
    if (mPendingCalls.first != mPendingCalls.last) {
        DestroyPendingCalls(static_cast<PendingCall *>(mPendingCalls.first), static_cast<PendingCall *>(mPendingCalls.last), &mPendingCalls);
        mPendingCalls.last = mPendingCalls.first;
    }
    mConditions.resize(1);
    mNamespacePath.clear();
    mNamespaceLengths.clear();
    // Every namespace but the outermost is closed. ORIGINAL-BUG: with no namespace open at all (a parser that was
    // never reset) the loop starts below the vector's begin and runs on through memory.
    while (mNamespaces.size() != 1) {
        --mNamespaces.back()->mUseCount;
        mNamespaces.pop_back();
    }
    mFieldF4 = 0;
}

void cBaseParser::PushOpenBlock(void *block) { mOpenBlocks.push_back(block); }

void cBaseParser::PushMetaBlock(void *block) { mMetaBlocks.push_back(block); }

void cBaseParser::PopMetaBlock()
{
    ThrowUnless(!mMetaBlocks.empty(), kNotInMetaBlock);
    mMetaBlocks.pop_back();
}

void cBaseParser::RunExpandedText(uint32_t context, const char *text)
{
    cRZString expanded;
    expanded.Construct(text);
    VirtualCall<void(__thiscall *)(void *, cRZString *, uint32_t), 29>(ParserFace(), &expanded, 0);
    VirtualCall<void(__thiscall *)(void *, uint32_t, const char *), 1>(this, context, expanded.mString.c_str());
    expanded.Destruct();
}

void cBaseParser::PopNamespace()
{
    if (mTraceNamespaces) {
        void *output = mOutput;
        const char *path = mNamespacePath.c_str();
        Write(output, kLeavingNamespace);
        Write(output, path);
        Write(output, kQuoteNewline);
    }
    mNamespacePath.erase(mNamespaceLengths.back()); // throws std::out_of_range past the end, as the original
    mNamespaceLengths.pop_back();
    --mNamespaces.back()->mUseCount;
    mNamespaces.pop_back();
}

EvalFunctionTable *cBaseParser::GetFunctions() { return &mFunctions; }

void cBaseParser::SetOutput(void *output)
{
    AssignCounted(mOutput, output);
    if (mOutput) return;
    auto *stream = static_cast<appdebugger::OStreamBase *>(::operator new(sizeof(appdebugger::OStreamBase)));
    if (stream) {
        stream->ConstructTextOStream();
        stream->mVTable = &ts2_rva_da2bb0;
    }
    AssignCounted(mOutput, stream);
}

void cBaseParser::SetHandler(void *handler) { AssignCounted(mHandler, handler); }

void cBaseParser::SetSource(void *source)
{
    void *slot = mSource;
    AssignCounted(slot, source);
    mSource = static_cast<cIParserSource *>(slot);
    if (mSource) return;
    slot = mSource;
    AssignCounted(slot, NewFileSource());
    mSource = static_cast<cIParserSource *>(slot);
}

const char *cBaseParser::GetNamespacePath() { return mNamespacePath.c_str(); }

bool cBaseParser::EvalBool(const char *text)
{
    EvalResult result;
    Evaluate(&mFunctions, &result, text);
    if (result.count != 1) ThrowText(kNotScalar);
    return result.values[0] != 0.0f;
}

float cBaseParser::EvalFloat(const char *text)
{
    EvalResult result;
    Evaluate(&mFunctions, &result, text);
    if (result.count != 1) ThrowText(kNotScalar);
    return result.values[0];
}

int32_t cBaseParser::EvalInt(const char *text)
{
    EvalResult result;
    Evaluate(&mFunctions, &result, text);
    if (result.count != 1) ThrowText(kNotScalar);
    // Rounded to the nearest integer (ties to even) by the magic-number trick: 1.5 * 2^23 added in single precision
    // leaves the integer in the low mantissa bits.
    const float shifted = result.values[0] + 12582912.0f;
    return std::bit_cast<int32_t>(shifted) - 0x4b400000;
}

float *cBaseParser::EvalVector2(float *out, const char *text, uint32_t minimum)
{
    EvalVector<2>(&mFunctions, out, text, minimum);
    return out;
}
float *cBaseParser::EvalVector3(float *out, const char *text, uint32_t minimum)
{
    EvalVector<3>(&mFunctions, out, text, minimum);
    return out;
}
float *cBaseParser::EvalVector4(float *out, const char *text, uint32_t minimum)
{
    EvalVector<4>(&mFunctions, out, text, minimum);
    return out;
}

float *cBaseParser::EvalColorRGBA(float *out, const char *text)
{
    EvalResult result;
    Evaluate(&mFunctions, &result, text);
    if (result.count == 3) result.values[3] = 1.0f;
    else if (result.count != 4) ThrowText(kNotColor);
    for (int i = 0; i < 4; ++i) out[i] = result.values[i];
    return out;
}

float *cBaseParser::EvalColorARGB(float *out, const char *text)
{
    float rgba[4];
    VirtualCall<float *(__thiscall *)(void *, float *, const char *), 37>(ParserFace(), rgba, text);
    out[0] = rgba[3];
    out[1] = rgba[0];
    out[2] = rgba[1];
    out[3] = rgba[2];
    return out;
}

float *cBaseParser::EvalColorRGB(float *out, const char *text)
{
    EvalResult result;
    Evaluate(&mFunctions, &result, text);
    if (result.count != 3 && result.count != 4) ThrowText(kNotColor);
    for (int i = 0; i < 3; ++i) out[i] = result.values[i];
    return out;
}

uint32_t cBaseParser::CommandCount() { return mCommandList.Size(); }
void *cBaseParser::CommandAt(uint32_t index) { return mCommandList.mFirst[index]; }
uint8_t cBaseParser::IsInitialised() { return mInitialised; }
uint8_t cBaseParser::IsCaseSensitive() { return mCaseSensitive; }

const char *cBaseParser::GetText()
{
    if (mText.mString.empty()) return kEmptyText;
    return mText.mString.c_str();
}

const char *cBaseParser::GetFileName()
{
    if (mLine == 0 || mFileName.empty()) return nullptr;
    return mFileName.c_str();
}

uint32_t cBaseParser::GetLine() { return mLine; }

bool cBaseParser::HasDefinition(const char *name)
{
    const char *end = name + std::strlen(name);
    void **first = static_cast<void **>(mDefinitions.first);
    void **last = static_cast<void **>(mDefinitions.last);
    // lower_bound by the definitions' order
    int32_t count = static_cast<int32_t>(last - first);
    while (count > 0) {
        const int32_t half = static_cast<int32_t>(static_cast<uint32_t>(count) >> 1);
        if (DefinitionBeforeKey(first[half], name, end)) {
            first += half + 1;
            count -= half + 1;
        } else {
            count = half;
        }
    }
    return first != last && !KeyBeforeDefinition(name, end, *first);
}

bool cBaseParser::HasCommand(const char *name)
{
    for (void **p = mCommandList.mFirst; p != mCommandList.mLast; ++p)
        if (_stricmp(name, VirtualCall<NameFn, 5>(*p)) == 0) return true;
    return false;
}

void *cBaseParser::GetSource() { return mSource; }

// ---- adapters ----

bool __fastcall cBaseParserAdapters::QueryInterface(void *face, void *, uint32_t iid, void **out)
{
    return cBaseParser::FromUnknownFace(face)->QueryInterface(iid, out);
}
uint32_t __fastcall cBaseParserAdapters::AddRef(void *face, void *) { return cBaseParser::FromUnknownFace(face)->AddRef(); }
uint32_t __fastcall cBaseParserAdapters::Release(void *face, void *) { return cBaseParser::FromUnknownFace(face)->Release(); }
void __fastcall cBaseParserAdapters::DropDefinitionsOfLevel(cBaseParser *self, void *, int32_t level) { self->DropDefinitionsOfLevel(level); }

#define HU_FACE cBaseParser::FromParserFace(face)
bool __fastcall cBaseParserAdapters::Shutdown(void *face, void *) { return HU_FACE->Shutdown(); }
void __fastcall cBaseParserAdapters::SetVersionRange(void *face, void *, int32_t lowest, int32_t highest) { HU_FACE->SetVersionRange(lowest, highest); }
void __fastcall cBaseParserAdapters::SetContextState(void *face, void *, void *state) { HU_FACE->SetContextState(state); }
void __fastcall cBaseParserAdapters::RegisterCommand(void *face, void *, void *command) { HU_FACE->RegisterCommand(command); }
bool __fastcall cBaseParserAdapters::UnregisterCommand(void *face, void *, const char *name) { return HU_FACE->UnregisterCommand(name); }
bool __fastcall cBaseParserAdapters::UnregisterCommandObject(void *face, void *, void *command) { return HU_FACE->UnregisterCommandObject(command); }
void __fastcall cBaseParserAdapters::RegisterMetaCommand(void *face, void *, void *command) { HU_FACE->RegisterMetaCommand(command); }
void __fastcall cBaseParserAdapters::RestartViaReset(void *face, void *) { HU_FACE->RestartViaReset(); }
void __fastcall cBaseParserAdapters::Reset(void *face, void *) { HU_FACE->Reset(); }
void __fastcall cBaseParserAdapters::ResetBlocks(void *face, void *) { HU_FACE->ResetBlocks(); }
void __fastcall cBaseParserAdapters::PushOpenBlock(void *face, void *, void *block) { HU_FACE->PushOpenBlock(block); }
void __fastcall cBaseParserAdapters::PushMetaBlock(void *face, void *, void *block) { HU_FACE->PushMetaBlock(block); }
void __fastcall cBaseParserAdapters::PopMetaBlock(void *face, void *) { HU_FACE->PopMetaBlock(); }
void __fastcall cBaseParserAdapters::RunExpandedText(void *face, void *, uint32_t context, const char *text) { HU_FACE->RunExpandedText(context, text); }
void __fastcall cBaseParserAdapters::PopNamespace(void *face, void *) { HU_FACE->PopNamespace(); }
EvalFunctionTable *__fastcall cBaseParserAdapters::GetFunctions(void *face, void *) { return HU_FACE->GetFunctions(); }
void __fastcall cBaseParserAdapters::SetOutput(void *face, void *, void *output) { HU_FACE->SetOutput(output); }
void __fastcall cBaseParserAdapters::SetHandler(void *face, void *, void *handler) { HU_FACE->SetHandler(handler); }
void __fastcall cBaseParserAdapters::SetSource(void *face, void *, void *source) { HU_FACE->SetSource(source); }
const char *__fastcall cBaseParserAdapters::GetNamespacePath(void *face, void *) { return HU_FACE->GetNamespacePath(); }
bool __fastcall cBaseParserAdapters::EvalBool(void *face, void *, const char *text) { return HU_FACE->EvalBool(text); }
float __fastcall cBaseParserAdapters::EvalFloat(void *face, void *, const char *text) { return HU_FACE->EvalFloat(text); }
int32_t __fastcall cBaseParserAdapters::EvalInt(void *face, void *, const char *text) { return HU_FACE->EvalInt(text); }
float *__fastcall cBaseParserAdapters::EvalVector2(void *face, void *, float *out, const char *text, uint32_t minimum) { return HU_FACE->EvalVector2(out, text, minimum); }
float *__fastcall cBaseParserAdapters::EvalVector3(void *face, void *, float *out, const char *text, uint32_t minimum) { return HU_FACE->EvalVector3(out, text, minimum); }
float *__fastcall cBaseParserAdapters::EvalVector4(void *face, void *, float *out, const char *text, uint32_t minimum) { return HU_FACE->EvalVector4(out, text, minimum); }
float *__fastcall cBaseParserAdapters::EvalColorRGBA(void *face, void *, float *out, const char *text) { return HU_FACE->EvalColorRGBA(out, text); }
float *__fastcall cBaseParserAdapters::EvalColorARGB(void *face, void *, float *out, const char *text) { return HU_FACE->EvalColorARGB(out, text); }
float *__fastcall cBaseParserAdapters::EvalColorRGB(void *face, void *, float *out, const char *text) { return HU_FACE->EvalColorRGB(out, text); }
uint32_t __fastcall cBaseParserAdapters::CommandCount(void *face, void *) { return HU_FACE->CommandCount(); }
void *__fastcall cBaseParserAdapters::CommandAt(void *face, void *, uint32_t index) { return HU_FACE->CommandAt(index); }
uint8_t __fastcall cBaseParserAdapters::IsInitialised(void *face, void *) { return HU_FACE->IsInitialised(); }
uint8_t __fastcall cBaseParserAdapters::IsCaseSensitive(void *face, void *) { return HU_FACE->IsCaseSensitive(); }
const char *__fastcall cBaseParserAdapters::GetText(void *face, void *) { return HU_FACE->GetText(); }
const char *__fastcall cBaseParserAdapters::GetFileName(void *face, void *) { return HU_FACE->GetFileName(); }
uint32_t __fastcall cBaseParserAdapters::GetLine(void *face, void *) { return HU_FACE->GetLine(); }
bool __fastcall cBaseParserAdapters::HasDefinition(void *face, void *, const char *name) { return HU_FACE->HasDefinition(name); }
bool __fastcall cBaseParserAdapters::HasCommand(void *face, void *, const char *name) { return HU_FACE->HasCommand(name); }
void *__fastcall cBaseParserAdapters::GetSource(void *face, void *) { return HU_FACE->GetSource(); }
#undef HU_FACE

} // namespace nGZCommandParser
