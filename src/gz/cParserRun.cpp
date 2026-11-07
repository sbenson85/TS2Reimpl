// nGZCommandParser::cBaseParser: the run path (lane IH): running a file, the files a wildcard names, a text and a
// line (cIBaseParser slots 12, 13, 14, 15) and stripping a line's comments; setting up the built-in commands (slot
// 0); setting a variable (primary slot 1).
// RVAs are 1.18.0.187. The class is hook-shaped (cBaseParser.h): `this` is the whole parser, the adapters take the face.
#include "gz/cBaseParser.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"

#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "gz/cParserTexts.h"
#include "rz/DirectoryScan.h"
#include "rz/cRZFileName.h"

#include <cmath>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace nGZCommandParser {

namespace {

constexpr const char *kHashCloseWithoutOpen = ts2_rva_e7f458;  // "'#>' without matching '#<'"
constexpr const char *kUnknownCommand = ts2_rva_e7f2a4;        // "Unknown command"
constexpr const char *kErrorInCommand = ts2_rva_e7f2b4;        // "%s\n      in command '%s'. OnErrorClearParserTempState called."
constexpr const char *kUnknownErrorInCommand = ts2_rva_e7f2f4; // "%s (unknown exception type)\n      in command '%s'"

// The blanks of the game's character table (0xe7d528, bit 1): tab, line feed, carriage return and space.
inline bool IsBlank(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

using VoidFn = void(__thiscall *)(void *);
using MetaAcceptsFn = bool(__thiscall *)(void *, cRZString *, uint32_t);   // meta block slot 10 (ret 8)
using ExpandFn = void(__thiscall *)(void *, cRZString *, uint32_t);        // face slot 29 (ret 8)
using CommandLevelFn = int32_t(__thiscall *)(void *);                      // command slot 8
using CommandExecuteFn = void(__thiscall *)(void *, cIArguments *);        // command slot 4 (ret 4)
using CommandChecksSwitchesFn = bool(__thiscall *)(void *);                // command slot 7 (al)
using ErrorTextFn = const char *(__thiscall *)(const void *);              // cIGZExceptionBase slot 0
using SprintfFn = cRZString *(__cdecl *)(void *, const char *, const char *, const char *); // cRZString slot 31

} // namespace

bool cBaseParser::AppendUncommented(const char *text, std::string *out)
{
    const char *begin = text;
    const char *end = text + std::strlen(text);
    if (mFieldF4 == 0) {
        while (begin < end && IsBlank(*begin)) ++begin;
        if (begin < end)
            while (IsBlank(end[-1])) {
                --end;
                if (end <= begin) break;
            }
    }
    const char *hash = static_cast<const char *>(std::memchr(begin, '#', end - begin));
    for (;;) {
        const char *stop = end;
        if (hash) {
            const char *next = hash + 1;
            stop = hash;
            if (next != end && (*next == '<' || *next == '>')) {
                if (*next == '<') {
                    // Only the text before the outermost "#<" is kept (`begin` stays: the text after a "#<" inside a
                    // comment is not).
                    if (mFieldF4 == 0 && begin < hash) out->append(begin, hash - begin);
                    ++mFieldF4;
                } else {
                    if (mFieldF4 == 0) ThrowFormatted(kHashCloseWithoutOpen);
                    if (--mFieldF4 == 0) begin = next = hash + 2;
                }
                hash = static_cast<const char *>(std::memchr(next, '#', end - next));
                continue;
            }
        }
        // A plain '#' (or the end): the rest is a comment.
        if (mFieldF4 == 0 && stop != begin) out->append(begin, stop - begin);
        return out->size() != 0;
    }
}

void cBaseParser::RunLine(const char *text)
{
    mFieldE4 = static_cast<uint32_t>(-1);
    // Both handlers end the same way: the blocks reset, then a cError naming the line. ORIGINAL-BUG: the message is the
    // new error's format string (a '%' in the line or the error's text is read as a conversion again).
    auto rethrow = [&](const char *format, const cIGZExceptionBase *error) {
        VirtualCall<VoidFn, 10>(ParserFace()); // ResetBlocks
        const char *message = VirtualCall<ErrorTextFn, 0>(error);
        VirtualCall<SprintfFn, 31>(&mText, format, message, text);
        ThrowFormatted(mText.mString.c_str());
    };
    try {
        if (mList1A4.empty()) {
            ListItem spare;
            mList1A4.push_back(spare);
        }
        mList19C.splice(mList19C.begin(), mList1A4, mList1A4.begin());
        const auto node = mList19C.begin();
        ListItem &item = *node;
        try {
            item.text.mString.clear();
            if (mPendingCalls.first == mPendingCalls.last) AppendUncommented(text, &item.text.mString);
            else item.text.mString.assign(text); // inside a call being recorded: kept as it is
            if (!item.text.mString.empty() &&
                (mMetaBlocks.empty() || VirtualCall<MetaAcceptsFn, 10>(mMetaBlocks.back(), &item.text, mConditions.back().active))) {
                VirtualCall<ExpandFn, 29>(ParserFace(), &item.text, 1u);
                cArguments &arguments = item.arguments;
                arguments.Parse(item.text.mString.c_str(), 1, mCaseSensitive);
                if (arguments.Count() != 0) {
                    const int32_t length = arguments.ArgumentLength(0);
                    const char *name = arguments.Argument(0);
                    void *command = reinterpret_cast<cParserState *>(this)->FindCommand(name, static_cast<uint32_t>(length));
                    ThrowIfNull(command, kUnknownCommand);
                    const int32_t depth = static_cast<int32_t>(mConditions.size());
                    mFieldE4 = static_cast<uint32_t>(depth + VirtualCall<CommandLevelFn, 8>(command));
                    VirtualCall<CommandExecuteFn, 4>(command, &arguments);
                    if (VirtualCall<CommandChecksSwitchesFn, 7>(command)) arguments.CheckSwitchesUsed();
                }
            }
        } catch (...) {
            mList1A4.splice(mList1A4.begin(), mList19C, node);
            throw;
        }
        mList1A4.splice(mList1A4.begin(), mList19C, node);
    } catch (const cError &error) {
        rethrow(kErrorInCommand, &error);
    } catch (const cIGZExceptionBase &error) {
        rethrow(kUnknownErrorInCommand, &error);
    }
}

namespace {

constexpr const char *kLinePrefix = ts2_rva_e7f1fc;       // "%s(%d): error: "
constexpr const char *kUnbalancedBlock = ts2_rva_e7f20c;  // "Unbalanced block command in '%s': too many or two few 'end's.\n"
constexpr const char *kUnbalancedMeta = ts2_rva_e7f250;   // "Unbalanced meta command in '%s': look for missing close tag ..."
constexpr const char *kPathNotSupported = ts2_rva_e7f1a4; // "Path type not supported: %s"
constexpr const char *kFileNotFound = ts2_rva_e7f1c0;     // "File not found"
constexpr const char *kQuote = ts2_rva_dddf9c;            // "'"
constexpr const char *kQuoteEquals = ts2_rva_e7eb00;      // "' = '"
constexpr const char *kQuoteLine = ts2_rva_d9f86c;        // "'\n"

using RunLineFn = void(__thiscall *)(void *, const char *);                          // face slot 15 (ret 4)
using RunTextFn = void(__thiscall *)(void *, char *, uint32_t, const char *);        // face slot 14 (ret 0xc)
using ResolvePathFn = void(__thiscall *)(void *, const char *, const char *, cRZString *); // source slot 6
using AcceptsPathFn = bool(__thiscall *)(void *, const char *);                      // source slot 3 (al)
using FileData = std::vector<char, GZServiceAllocator<char>>;
using ReadFileFn = bool(__thiscall *)(void *, const char *, FileData *);             // source slot 5 (al)
using Sprintf1Fn = cRZString *(__cdecl *)(void *, const char *, const char *);       // cRZString slot 31
using WriteTextFn = void(__thiscall *)(void *, const char *);                        // output slot 14
using RegisterFn = void(__thiscall *)(void *, void *);                               // face slots 4 and 7
using SetOutputFn = void(__thiscall *)(void *, void *);                              // face slot 24

// A cRZString local, built and destroyed by the game's bodies (Construct / Destruct) as the original's are.
struct LocalString {
    union {
        cRZString value;
    };
    LocalString() { value.Construct(); }
    explicit LocalString(const char *text) { value.Construct(text); }
    // Built by a game function that constructs its out argument (e.g. cRZFileName::GetDrivePath).
    template <class Build> LocalString(std::in_place_t, Build build) { build(&value); }
    ~LocalString() { value.Destruct(); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
};

// The pieces of slot 13 (RunFiles).
constexpr const char *kErrorsParsingFiles = ts2_rva_e7f1d0; // "Errors parsing files!\n"
constexpr const char *kNewline = ts2_rva_d9e644;            // "\n"
constexpr const char *kNewlineTab = ts2_rva_df7240;         // "\n\t"
constexpr const char *kUnknownException = ts2_rva_e7f1e8;   // "\n\tUnknown exception"
using RunFileFn = void(__thiscall *)(void *, const char *, uint32_t);   // face slot 12 (ret 8)

// A cRZFileName local (the game's constructor and destructor bodies).
struct LocalFileName {
    union {
        cRZFileName value;
    };
    explicit LocalFileName(const cRZString *path) { value.Construct(path); }
    ~LocalFileName() { value.Destruct(); }
    LocalFileName(const LocalFileName &) = delete;
    LocalFileName &operator=(const LocalFileName &) = delete;
};

// A directory scan on the stack (our ::DirectoryScan, src/rz/DirectoryScan.h; lane LY switched it from the original
// callees): built for a directory, it finds the entries matching a name pattern; the matches are then listed by name,
// or by full path (with the directories below it). Built and destroyed through the class's adapters, the addressable
// entry points tests stand in for.
class FileScan {
public:
    // Built for the file name's drive and directory (a copy of them, gone again once the scan is built).
    explicit FileScan(cRZFileName *fileName)
    {
        LocalString directory(std::in_place, [&](cRZString *out) { fileName->GetDrivePath(out); });
        mScan = DirectoryScanAdapters::Construct(mRaw, nullptr, &directory.value, false, false, false);
    }
    ~FileScan() { DirectoryScanAdapters::Destruct(mScan, nullptr); }
    FileScan(const FileScan &) = delete;
    FileScan &operator=(const FileScan &) = delete;
    void Find(cRZString *pattern) { mScan->SetPattern(pattern, true); }
    void ListNames(dscan::PathList *out) { mScan->ListFiles(out); }
    void ListPaths(const cRZString *directory, dscan::PathList *out) { mScan->ListFilesBelow(directory, out, true); }

private:
    alignas(::DirectoryScan) unsigned char mRaw[sizeof(::DirectoryScan)];
    ::DirectoryScan *mScan;
};

// The built-in commands slot 0 registers, in its order: the object's size, which base constructor builds it, and
// the class's vtable (an owned static, CommandParserTables.h: the game's in the hooked game, ours in an image-free program).
enum class Base : uint8_t { kCommand, kBlock, kMeta };
struct BuiltIn {
    uint32_t size;
    Base base;
    const OwnedVTable *vtable;
};
const BuiltIn kBuiltIns[] = {
    {0x10, Base::kCommand, &ts2_rva_e7dc6c}, // cIncludeCommand
    {0x10, Base::kCommand, &ts2_rva_e7dcbc}, // cSincludeCommand
    {0x10, Base::kCommand, &ts2_rva_e7dd38}, // cWildIncludeCommand
    {0x10, Base::kCommand, &ts2_rva_e7dde8}, // cEndCommand
    {0x10, Base::kCommand, &ts2_rva_e7de38}, // cIgnoreErrorsCommand
    {0x10, Base::kCommand, &ts2_rva_e7dea0}, // cListCommandsCommand
    {0x10, Base::kCommand, &ts2_rva_e7df1c}, // cEvalCommand
    {0x10, Base::kCommand, &ts2_rva_e7df78}, // cAssertCommand
    {0x10, Base::kCommand, &ts2_rva_e7e004}, // cSetCommand
    {0x10, Base::kCommand, &ts2_rva_e7e054}, // cSetECommand
    {0x10, Base::kCommand, &ts2_rva_e7e0e0}, // cSetBCommand
    {0x10, Base::kCommand, &ts2_rva_e7e13c}, // cSetICommand
    {0x10, Base::kCommand, &ts2_rva_e7e1b0}, // cSetFCommand
    {0x10, Base::kCommand, &ts2_rva_e7e230}, // cSetCCommand
    {0x10, Base::kCommand, &ts2_rva_e7e294}, // cSetV2Command
    {0x10, Base::kCommand, &ts2_rva_e7e304}, // cSetV3Command
    {0x10, Base::kCommand, &ts2_rva_e7e378}, // cSetV4Command
    {0x44, Base::kBlock, &ts2_rva_e7e3e0},   // cNamespaceCommand
    {0x10, Base::kCommand, &ts2_rva_e7e444}, // cPurgeCommand
    {0x0c, Base::kMeta, &ts2_rva_e7e4d0},    // cIfCommand
    {0x10, Base::kCommand, &ts2_rva_e7e540}, // cElseIfCommand
    {0x10, Base::kCommand, &ts2_rva_e7e5d4}, // cElseCommand
    {0x10, Base::kCommand, &ts2_rva_e7e658}, // cEndIfCommand
    {0x0c, Base::kMeta, &ts2_rva_e7e6c8},    // cDefineCommand
    {0x10, Base::kCommand, &ts2_rva_e7e748}, // cEndDefCommand
    {0x10, Base::kCommand, &ts2_rva_e7e7bc}, // cUndefineCommand
    {0x10, Base::kCommand, &ts2_rva_e7e850}, // cCreateCommand
    {0x10, Base::kCommand, &ts2_rva_e7e980}, // cArrayCreateCommand
    {0x10, Base::kCommand, &ts2_rva_e7ea90}, // cDumpParserStateCommand
    {0x10, Base::kCommand, &ts2_rva_e7eb20}, // cDebugParserCommand
    {0x10, Base::kCommand, &ts2_rva_e7eba8}, // cShowArgumentsCommand
    {0x10, Base::kCommand, &ts2_rva_e7ec18}, // cDebuggerBreakCommand
    {0x10, Base::kCommand, &ts2_rva_e7ec70}, // cVersionCommand
    {0x10, Base::kCommand, &ts2_rva_e7ea04}, // cTraceCommand
    {0x10, Base::kCommand, &ts2_rva_e7ea4c}, // cLogCommand
    {0x10, Base::kCommand, &ts2_rva_e7ed78}, // cAliasCommand
};

// The evaluator functions slot 0 registers (cFunctionT<cBaseParser>, 0x10 bytes): the class's two vtables.
const OwnedVTable *const kFunctionTFaceVTable = &ts2_rva_e7eefc; // cFunctionT<cBaseParser>'s face vtable, written in between
struct BuiltInFunction {
    const OwnedVTable *vtable;
    const OwnedVTable *faceVTable;
};
const BuiltInFunction kBuiltInFunctions[] = {
    {&ts2_rva_e7ef1c, &ts2_rva_e7ef28}, // cVarExistsFunction
    {&ts2_rva_e7ef90, &ts2_rva_e7ef9c}, // cDefinitionExistsFunction
    {&ts2_rva_e7f000, &ts2_rva_e7f00c}, // cCommandExistsFunction
    {&ts2_rva_e7f068, &ts2_rva_e7f074}, // cMinVersionFunction
    {&ts2_rva_e7f0dc, &ts2_rva_e7f0e8}, // cMaxVersionFunction
    {&ts2_rva_e7f150, &ts2_rva_e7f15c}, // cMatchFunction
};

// Memory from operator new handed to a constructor body: freed again if the body throws.
template <class Body> void *ConstructIn(uint32_t size, Body body)
{
    void *raw = ::operator new(size);
    try {
        body(raw);
    } catch (...) {
        ::operator delete(raw, size);
        throw;
    }
    return raw;
}

} // namespace

void cBaseParser::RunText(char *text, uint32_t /*size*/, const char *fileName)
{
    const std::string saved(mFileName);
    mFileName.assign(fileName);
    const size_t openBlocks = mOpenBlocks.size();
    const size_t metaBlocks = mMetaBlocks.size();
    mFileName.assign(fileName); // the original assigns it twice
    for (uint32_t line = 1; text; ++line) {
        char *start = text;
        char *lineBreak = std::strchr(text, '\r');
        char *lineFeed = std::strchr(text, '\n');
        if (!lineBreak || (lineFeed && lineFeed < lineBreak)) lineBreak = lineFeed;
        if (!lineBreak) {
            text = nullptr;
        } else {
            const char c = *lineBreak;
            text = lineBreak + 1;
            *lineBreak = 0;
            if (*text == static_cast<char>(c ^ 7)) text = lineBreak + 2; // "\r\n" or "\n\r": one break
        }
        try {
            mLine = line;
            VirtualCall<RunLineFn, 15>(ParserFace(), start);
        } catch (const cIGZExceptionBase &) {
            if (!mField198) {
                LocalString prefix;
                prefix.value.Sprintf(kLinePrefix, fileName, line);
                mText.mString.insert(0, prefix.value.mString);
            }
            if (!mFlagE8) {
                mField198 = 1;
                // ORIGINAL-BUG: the text is the new cError's format string (a '%' in it is read as a conversion).
                ThrowFormatted(mText.mString.c_str());
            }
        }
    }
    if (mOpenBlocks.size() != openBlocks) ThrowFormatted(kUnbalancedBlock, fileName);
    if (mMetaBlocks.size() != metaBlocks) ThrowFormatted(kUnbalancedMeta, fileName);
    mFileName = saved;
}

void cBaseParser::RunFile(const char *path, uint32_t flags)
{
    LocalString resolved(path);
    if (flags & 1) VirtualCall<ResolvePathFn, 6>(mSource, path, mFileName.c_str(), &resolved.value);
    if (!VirtualCall<AcceptsPathFn, 3>(mSource, resolved.value.mString.c_str())) {
        VirtualCall<Sprintf1Fn, 31>(&mText, kPathNotSupported, path);
        // ORIGINAL-BUG: the message is the new cError's format string (a '%' in the path is read as a conversion).
        ThrowFormatted(mText.mString.c_str());
    }
    FileData data;
    if (VirtualCall<ReadFileFn, 5>(mSource, resolved.value.mString.c_str(), &data))
        VirtualCall<RunTextFn, 14>(ParserFace(), data.data(), static_cast<uint32_t>(data.size()) - 1, resolved.value.mString.c_str());
    else if (!(flags & 2))
        ThrowFormatted(kFileNotFound);
}

void cBaseParser::RunFiles(const char *pattern, uint32_t flags)
{
    LocalString resolved(pattern);
    if (flags & 1) VirtualCall<ResolvePathFn, 6>(mSource, pattern, mFileName.c_str(), &resolved.value);
    flags &= ~1u;
    LocalFileName fileName(&resolved.value);
    LocalString directory(std::in_place, [&](cRZString *out) { fileName.value.GetDrivePath(out); });
    FileScan files(&fileName.value);
    dscan::PathList list;
    {
        // The name and extension (0x1c8804: Assemble(0xc), then a copy of the assembled path).
        LocalString spec(std::in_place, [&](cRZString *out) {
            fileName.value.Assemble(0xc);
            out->Construct(fileName.value.mFull, 0, 0xFFFFFFFF);
        });
        files.Find(&spec.value);
    }
    const bool fullPaths = (flags & 4) != 0;
    if (fullPaths) files.ListPaths(&directory.value, &list);
    else files.ListNames(&list);

    LocalString errors;
    const auto noteFailure = [&](const std::string &path) {
        if (errors.value.mString.empty()) errors.value.mString.assign(kErrorsParsingFiles);
        errors.value.mString.append(kNewline);
        errors.value.mString.append(path);
    };
    for (const dscan::Path &node : list) {
        LocalString item;
        if (fullPaths) item.value.mString = node.s.mString;
        else item.value.mString = directory.value.mString + node.s.mString;
        try {
            VirtualCall<RunFileFn, 12>(ParserFace(), item.value.mString.c_str(), flags);
        } catch (const cIGZExceptionBase &error) {
            // The original has a cError handler and a cIGZExceptionBase one doing the same (cError is one).
            noteFailure(item.value.mString);
            errors.value.mString.append(kNewlineTab);
            errors.value.mString.append(VirtualCall<ErrorTextFn, 0>(&error));
        } catch (...) {
            noteFailure(item.value.mString);
            errors.value.mString.append(kUnknownException);
        }
    }
    // ORIGINAL-BUG: the collected text is the new cError's format string (a '%' in a path or message is a conversion).
    if (!errors.value.mString.empty()) ThrowFormatted(errors.value.mString.c_str());
}

bool cBaseParser::Initialise(uint8_t caseSensitive)
{
    if (mInitialised) return false;
    mInitialised = 1;
    VirtualCall<VoidFn, 8>(ParserFace()); // RestartViaReset
    mCaseSensitive = caseSensitive;
    VirtualCall<SetOutputFn, 24>(ParserFace(), nullptr);
    for (const BuiltIn &b : kBuiltIns) {
        void *command = ConstructIn(b.size, [&](void *raw) {
            if (b.base == Base::kCommand) static_cast<cCommand *>(raw)->Construct();
            else if (b.base == Base::kBlock) static_cast<cBlockCommandBase *>(raw)->Construct();
            else static_cast<cMetaCommand *>(raw)->Construct();
            *static_cast<const void **>(raw) = b.vtable;
        });
        VirtualCall<RegisterFn, 4>(ParserFace(), command);
    }
    for (const BuiltInFunction &f : kBuiltInFunctions) {
        auto *function = static_cast<cExpressionFunction *>(ConstructIn(0x10, [&](void *raw) {
            auto *base = static_cast<cExpressionFunction *>(raw)->Construct();
            base->mFaceVTable = kFunctionTFaceVTable;
            base->mVTable = f.vtable;
            base->mFaceVTable = f.faceVTable;
        }));
        VirtualCall<RegisterFn, 7>(ParserFace(), &function->mFaceVTable); // the cIExpressionFunction face
    }
    return true;
}

VariableTable::InsertResult *VariableTable::Insert(InsertResult *out, const KeyView *key)
{
    // The same table layout as CommandTable (the nodes share its first four words): its Find and Rehash.
    auto *table = reinterpret_cast<CommandTable *>(this);
    const uint32_t hash = HashName(key->ptr, key->length);
    FindResult where;
    table->Find(&where, key, hash);
    if (where.found) {
        out->node = reinterpret_cast<VariableNode *>(where.found);
        out->inserted = 0;
        return out;
    }
    if (size == 0x6666666) throw std::range_error("unordered_map/set too long");
    auto *node = static_cast<VariableNode *>(::operator new(sizeof(VariableNode)));
    node->key = key->ptr;
    node->keyLength = key->length;
    new (&node->value) std::string();
    CommandNode *position = where.position;
    // Grow first when the new node would pass the load limit (CommandTable::Insert's policy).
    const float afterInsert = static_cast<float>(size + 1);
    if (afterInsert / static_cast<float>(bucketCount) > maxLoad) {
        const float needed = static_cast<float>(std::ceil(static_cast<double>(afterInsert / maxLoad)));
        const uint32_t wanted = static_cast<uint32_t>(static_cast<int64_t>(needed));
        const uint32_t target = wanted > 8 ? wanted : 8;
        uint32_t request;
        if (bucketCount >= target) request = bucketCount;
        else if (bucketCount >= 0x200) request = target;
        else request = (bucketCount << 3) >= target ? (bucketCount << 3) : target;
        try {
            table->Rehash(request);
        } catch (...) {
            node->value.~basic_string();
            ::operator delete(node, sizeof(VariableNode));
            throw;
        }
        const KeyView nodeKey{node->key, node->keyLength};
        FindResult again;
        table->Find(&again, &nodeKey, hash);
        position = again.position;
    }
    auto *linked = reinterpret_cast<CommandNode *>(node);
    CommandNode *previous = position->prev;
    ++size;
    linked->prev = previous;
    linked->next = position;
    previous->next = linked;
    position->prev = linked;
    CommandNode **bucket = table->buckets.data() + 2 * (hash & mask);
    CommandNode *first = bucket[0];
    if (first == table->head) {
        bucket[0] = linked;
        bucket[1] = linked;
    } else if (first == position) {
        bucket[0] = linked;
    } else if (bucket[1] == previous) {
        bucket[1] = linked;
    }
    out->node = node;
    out->inserted = 1;
    return out;
}

void cBaseParser::SetVariable(const char *name, const char *value)
{
    if (mFieldEA) {
        std::string path(mNamespacePath);
        path.append(name);
        void *output = mOutput;
        VirtualCall<WriteTextFn, 14>(output, kQuote);
        VirtualCall<WriteTextFn, 14>(output, path.c_str());
        VirtualCall<WriteTextFn, 14>(output, kQuoteEquals);
        VirtualCall<WriteTextFn, 14>(output, value);
        VirtualCall<WriteTextFn, 14>(output, kQuoteLine);
    }
    ParserNamespace *ns = mNamespaces.back();
    // "a:b:name": each name before a ':' is a child namespace, made when missing (its name is copied into the pool
    // every time, as the original does).
    for (const char *colon = std::strchr(name, ':'); colon; colon = std::strchr(name, ':')) {
        const std::string part(name, static_cast<size_t>(colon - name));
        const KeyView partKey{part.c_str(), static_cast<uint32_t>(part.size())};
        KeyView stored;
        ns->mNames.Store(&stored, &partKey);
        InsertResult child;
        ns->mCommands.Insert(&child, &stored);
        auto *&next = reinterpret_cast<ParserNamespace *&>(child.node->value);
        if (!next)
            next = static_cast<ParserNamespace *>(
                ConstructIn(sizeof(ParserNamespace), [](void *raw) { static_cast<ParserNamespace *>(raw)->Construct(); }));
        ns = next;
        name = colon + 1;
    }
    const std::string copy(value);
    const KeyView key{name, static_cast<uint32_t>(std::strlen(name))};
    FindResult found;
    reinterpret_cast<CommandTable *>(&ns->mVariables)->Find(&found, &key, HashName(key.ptr, key.length));
    std::string *target;
    if (found.found && found.found != reinterpret_cast<CommandNode *>(ns->mVariables.head)) {
        target = &reinterpret_cast<VariableNode *>(found.found)->value;
    } else {
        KeyView stored;
        ns->mNames.Store(&stored, &key);
        VariableTable::InsertResult inserted;
        ns->mVariables.Insert(&inserted, &stored);
        target = &inserted.node->value;
    }
    *target = copy;
}

cExpressionFunction *cExpressionFunction::Construct()
{
    mVTable = &ts2_rva_e7d7c4;
    mFaceVTable = &ts2_rva_e7d7d0;
    mRefCount = 0;
    return this;
}

void cExpressionFunction::Destruct()
{
    mVTable = &ts2_rva_e7d7c4;
    mFaceVTable = &ts2_rva_e7d7d0;
}

void __fastcall cBaseParserAdapters::RunLine(void *face, void *, const char *text) { cBaseParser::FromParserFace(face)->RunLine(text); }
void __fastcall cBaseParserAdapters::RunText(void *face, void *, char *text, uint32_t size, const char *fileName)
{
    cBaseParser::FromParserFace(face)->RunText(text, size, fileName);
}
void __fastcall cBaseParserAdapters::RunFile(void *face, void *, const char *path, uint32_t flags) { cBaseParser::FromParserFace(face)->RunFile(path, flags); }
void __fastcall cBaseParserAdapters::RunFiles(void *face, void *, const char *pattern, uint32_t flags)
{
    cBaseParser::FromParserFace(face)->RunFiles(pattern, flags);
}
bool __fastcall cBaseParserAdapters::Initialise(void *face, void *, uint8_t caseSensitive) { return cBaseParser::FromParserFace(face)->Initialise(caseSensitive); }
void __fastcall cBaseParserAdapters::SetVariable(cBaseParser *self, void *, const char *name, const char *value) { self->SetVariable(name, value); }
VariableTable::InsertResult *__fastcall cBaseParserAdapters::InsertVariable(VariableTable *table, void *, VariableTable::InsertResult *out, const KeyView *key)
{
    return table->Insert(out, key);
}

bool __fastcall cBaseParserAdapters::AppendUncommented(cBaseParser *self, void *, const char *text, std::string *out)
{
    return self->AppendUncommented(text, out);
}

} // namespace nGZCommandParser
