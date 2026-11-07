#include "gz/cBaseParser.h"

#include "game/game.h"
#include "game/vtable.h"

#include <memory>
#include <new>

namespace nGZCommandParser {

namespace {

template <typename Node, typename Table> void ConstructTable(Table &table)
{
    table.maxLoad = 0;
    table.head = nullptr;
    table.size = 0;
    // The sentinel is raw memory (a VariableNode's string is never constructed): only its links are set.
    table.head = static_cast<Node *>(::operator new(sizeof(Node)));
    table.head->next = table.head;
    table.head->prev = table.head;
    std::construct_at(&table.buckets);
    table.mask = 7;
    table.bucketCount = 8;
    table.maxLoad = 1.0f;
    // On the empty vector the library's _Assign_grow allocates exactly 16 words, as assign does.
    table.buckets.assign(16, table.head);
}

} // namespace

void ConstructEmptyTable(CommandTable &table) { ConstructTable<CommandNode>(table); }
void ConstructEmptyTable(VariableTable &table) { ConstructTable<VariableNode>(table); }

ParserNamespace *ParserNamespace::Construct()
{
    ConstructEmptyTable(mVariables);
    ConstructEmptyTable(mCommands);
    mNames.Construct();
    mUseCount = 0;
    return this;
}

cFileSource *cBaseParser::NewFileSource()
{
    return new cFileSource;
}

cBaseParser *cBaseParser::Construct(uint32_t mostDerived)
{
    char *const self = reinterpret_cast<char *>(this);
    if (mostDerived != 0) {
        // The whole object: the vbptrs, then the virtual bases' own constructors (cIGZUnknown, then cIBaseParser,
        // which also stores its view of its virtual base cIGZUnknown, found through its own vbtable).
        mVbTable = ts2_rva_e7dc18;
        mParserFaceVbTable = ts2_rva_e7dc24;
        mUnknownVTable = &ts2_rva_d9b428;
        mParserFaceVTable = &ts2_rva_db574c;
        *reinterpret_cast<const void **>(self + offsetof(cBaseParser, mParserFaceVbTable) + mParserFaceVbTable[1]) =
            &ts2_rva_db581c;
    }
    mStateId = kStateId;
    mVTable = &ts2_rva_e7db24;
    // The faces' vtables and the vtordisp word before each: the virtual base's displacement beyond the one it has
    // in a whole cBaseParser.
    const int32_t unknownOffset = mVbTable[1];
    *reinterpret_cast<const void **>(self + 8 + unknownOffset) = &ts2_rva_e7db3c;
    const int32_t parserOffset = mVbTable[2];
    *reinterpret_cast<const void **>(self + 8 + parserOffset) = &ts2_rva_e7db4c;
    *reinterpret_cast<int32_t *>(self + 4 + unknownOffset) = unknownOffset - kUnknownOffset;
    *reinterpret_cast<int32_t *>(self + 4 + parserOffset) = parserOffset - kParserOffset;

    mRefCount = 0;
    mInitialised = 0;
    mOutput = nullptr;
    mHandler = nullptr;

    // The source, then one reference taken through its vtable.
    cFileSource *source = NewFileSource();
    mSource = source;
    if (source) VirtualCall<uint32_t(__thiscall *)(void *), 1>(source); // AddRef

    std::construct_at(&mFunctions);
    ConstructEmptyTable(mCommands);
    mNames.Construct();
    mVersionRangeSet = 0;
    mLowestVersion = -1;
    mHighestVersion = -1;
    mContextState = nullptr;
    mDefinitions = {};
    mGlobalNamespace.Construct();
    std::construct_at(&mFileName);
    mLine = 0;
    mFlagE8 = 0;
    mTraceNamespaces = 0;
    mFieldEA = 0;
    mTraceVariables = 0;
    mTrace = 0;
    mFieldED = 0;
    mFieldEE = 0;
    mVersion = -1;
    mFieldF4 = 0;
    std::construct_at(&mMetaBlocks);
    std::construct_at(&mOpenBlocks);
    std::construct_at(&mConditions);
    mPendingCalls = {};
    std::construct_at(&mNamespaceLengths);
    std::construct_at(&mNamespacePath);
    std::construct_at(&mNamespaces);
    mField158 = 0;
    mCurrentDefinition = nullptr;
    mCommandList = {};
    mMetaCommands = {};
    mText.Construct();
    mField198 = 0;
    std::construct_at(&mList19C);
    std::construct_at(&mList1A4);

    // The condition of the text outside any if: level 0, active.
    Condition outside{};
    outside.active = 1;
    mConditions.push_back(outside);
    return this;
}

} // namespace nGZCommandParser
