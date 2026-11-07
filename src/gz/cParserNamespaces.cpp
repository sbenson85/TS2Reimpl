// nGZCommandParser::cBaseParser's namespaces and its destructor (lane HU): the namespace tree (each namespace keeps
// its child namespaces in a table of the command table's layout, values ParserNamespace *), slots 20 and 22 of the
// cIBaseParser face that open and purge namespaces, the helpers that tear namespaces and the variable tables down,
// and the destructor with its deleting form. RVAs are 1.18.0.187.
#include "gz/cBaseParser.h"
#include "gz/cParserTexts.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"

#include <algorithm>
#include <cstring>
#include <memory>

namespace nGZCommandParser {

namespace {

using CountFn = uint32_t(__thiscall *)(void *);

inline void Uncount(void *object) { VirtualCall<CountFn, 2>(object); }
inline void DefinitionRelease(void *definition) { VirtualCall<CountFn, 1>(definition); }
inline void Write(void *output, const char *text) { VirtualCall<void(__thiscall *)(void *, const char *), 14>(output, text); }

constexpr const char *kEnteringNamespace = ts2_rva_e7f34c; // "entering namespace '"
constexpr const char *kPurgingNamespace = ts2_rva_e7f378;  // "purging namespace '"
constexpr const char *kQuoteNewline = ts2_rva_d9f86c;      // the closing quote and the line end
constexpr const char *kPathSeparator = ts2_rva_da9e7c;     // ":"

// A node leaves its table: out of its bucket (the bucket's first and last node move past it), the count drops and
// the list closes over it. The node itself is not freed.
template <class Table, class Node> void UnlinkNode(Table &table, Node *node)
{
    Node **bucket = table.buckets.data() + 2 * (HashName(node->key, node->keyLength) & table.mask);
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
    Node *next = node->next;
    --table.size;
    node->prev->next = next;
    next->prev = node->prev;
}

// The bucket vector of a table freed (the three pointers then null), as the library's vector destructor leaves them.
template <class T> void FreeBuckets(std::vector<T> &buckets) { std::vector<T>().swap(buckets); }

// The child table's own teardown (the shared instantiation 0x472280, not hooked: ten callers elsewhere): the bucket
// vector freed, every node freed (the namespaces they point at are not: Clear has dealt with them), the sentinel freed.
void DestroyChildTable(CommandTable &table)
{
    FreeBuckets(table.buckets);
    table.head->prev->next = nullptr;
    for (CommandNode *node = table.head->next; node;) {
        CommandNode *next = node->next;
        ::operator delete(node, sizeof(CommandNode));
        node = next;
    }
    ::operator delete(table.head, sizeof(CommandNode));
}

ParserNamespace *ChildOf(CommandNode *node) { return static_cast<ParserNamespace *>(node->value); }

} // namespace

// ---- the variable table and the name pool ----

void __cdecl VariableTable::ClearNodes(void *, VariableNode *head)
{
    head->prev->next = nullptr;
    for (VariableNode *node = head->next; node;) {
        VariableNode *next = node->next;
        std::destroy_at(&node->value);
        ::operator delete(node, sizeof(VariableNode));
        node = next;
    }
}

void ResetNamePool(NamePool *pool)
{
    while (pool->count != 0) {
        NamePool::Chunk *last = pool->head->prev;
        // ORIGINAL-BUG (harmless): the 4 KB block is freed with a size of 1 (a scalar delete of its first byte).
        ::operator delete(last->block, 1);
        last = pool->head->prev;
        NamePool::Chunk *next = last->next;
        --pool->count;
        last->prev->next = next;
        next->prev = last->prev;
        ::operator delete(last, sizeof(NamePool::Chunk));
    }
    pool->used = 0x1000;
}

CommandNode **EraseChildNode(CommandTable *table, CommandNode **out, CommandNode *node)
{
    UnlinkNode(*table, node);
    CommandNode *next = node->next;
    ::operator delete(node, sizeof(CommandNode));
    *out = next;
    return out;
}

// ---- the namespace ----

void ParserNamespace::Clear(bool all)
{
    CommandNode *head = mCommands.head;
    for (CommandNode *node = head->next; node != head;) {
        ParserNamespace *child = ChildOf(node);
        if (!all && child->mUseCount != 0) {
            // Still open on the parser's stack: emptied, kept.
            child->Clear(false);
            node = node->next;
            continue;
        }
        if (child) {
            child->Destruct();
            ::operator delete(child, sizeof(ParserNamespace));
        }
        CommandNode *next = node->next;
        UnlinkNode(mCommands, node);
        ::operator delete(node, sizeof(CommandNode));
        node = next;
    }
    if (mVariables.size != 0) {
        VariableTable::ClearNodes(&mVariables.head, mVariables.head);
        mVariables.head->next = mVariables.head;
        mVariables.head->prev = mVariables.head;
        mVariables.size = 0;
        std::fill(mVariables.buckets.begin(), mVariables.buckets.end(), mVariables.head);
    }
    ResetNamePool(&mNames);
}

void ParserNamespace::Destruct()
{
    // ORIGINAL-BUG: Clear(false) keeps the children still open on the parser's stack; the child table's teardown
    // below then frees their nodes but not them (each such namespace, its tables and pool leak). It happens when a
    // parser is destroyed with namespaces open.
    Clear(false);
    mNames.Destruct();
    DestroyChildTable(mCommands);
    std::destroy_at(reinterpret_cast<VariableMap *>(&mVariables));
}

NamespaceLookup *ParserNamespace::FindPath(NamespaceLookup *out, const std::string &path)
{
    const size_t colon = path.find(':');
    const std::string first(path.data(), std::min(colon, path.size()));
    const KeyView key{first.c_str(), static_cast<uint32_t>(first.size())};
    FindResult found;
    mCommands.Find(&found, &key, HashName(key.ptr, key.length));
    CommandNode *node = found.found ? found.found : mCommands.head;
    if (node == mCommands.head) {
        out->owner = nullptr;
        out->node = node;
        return out;
    }
    if (colon >= path.size() - 1) { // the last name of the path (a ':' at its end ends it too)
        out->owner = this;
        out->node = node;
        return out;
    }
    const std::string rest(path.data() + colon + 1, path.size() - (colon + 1));
    return ChildOf(node)->FindPath(out, rest);
}

// ---- slots 20 and 22 ----

void cBaseParser::PushNamespace(const char *name)
{
    if (mTraceNamespaces) {
        void *output = mOutput;
        Write(output, kEnteringNamespace);
        Write(output, name);
        Write(output, kQuoteNewline);
    }
    ParserNamespace *child;
    {
        const std::string text(name);
        ParserNamespace *current = mNamespaces.back();
        const KeyView key{text.c_str(), static_cast<uint32_t>(text.size())};
        KeyView stored;
        // The name goes into the pool on every visit, also when the namespace exists already.
        current->mNames.Store(&stored, &key);
        InsertResult inserted;
        current->mCommands.Insert(&inserted, &stored);
        child = ChildOf(inserted.node);
        if (!child) {
            void *block = ::operator new(sizeof(ParserNamespace));
            child = block ? static_cast<ParserNamespace *>(block)->Construct() : nullptr;
            inserted.node->value = child;
        }
    }
    mNamespaces.push_back(child);
    ++child->mUseCount;
    mNamespaceLengths.push_back(static_cast<uint32_t>(mNamespacePath.size()));
    mNamespacePath.append(name);
    mNamespacePath.append(kPathSeparator, 1);
}

void cBaseParser::PurgeNamespace(const char *name)
{
    if (mTraceNamespaces) {
        void *output = mOutput;
        Write(output, kPurgingNamespace);
        Write(output, name);
        Write(output, kQuoteNewline);
    }
    const std::string path(name);
    NamespaceLookup found;
    mGlobalNamespace.FindPath(&found, path);
    if (!found.owner) return;
    ParserNamespace *child = ChildOf(found.node);
    if (child->mUseCount != 0) {
        child->Clear(false); // open on the stack: emptied only
        return;
    }
    child->Destruct();
    ::operator delete(child, sizeof(ParserNamespace));
    CommandNode *next;
    EraseChildNode(&found.owner->mCommands, &next, found.node);
}

// ---- the pending calls ----

void __cdecl DestroyPendingCalls(PendingCall *first, PendingCall *last, void *)
{
    for (; first != last; ++first) std::destroy_at(first); // the arguments, then the definition released
}

namespace {

// 0xafe000: the pending calls' vector destroyed: the elements, then the storage, the pointers null.
void DestroyPendingCallVector(cBaseParser::RawVector *calls)
{
    if (!calls->first) return;
    auto *first = static_cast<PendingCall *>(calls->first);
    DestroyPendingCalls(first, static_cast<PendingCall *>(calls->last), calls);
    ::operator delete(first, static_cast<size_t>(static_cast<PendingCall *>(calls->end) - first) * sizeof(PendingCall));
    *calls = {};
}

} // namespace

// ---- the destructor ----

void cBaseParser::Destruct()
{
    // The vtables of the class itself again (a derived class's destructor has run), through the vbtable as the
    // constructor does, and the vtordisps for the faces where they are.
    mVTable = &ts2_rva_e7db24;
    const int32_t unknownAt = mVbTable[1], parserAt = mVbTable[2];
    char *const self = reinterpret_cast<char *>(this);
    *reinterpret_cast<const void **>(self + 8 + unknownAt) = &ts2_rva_e7db3c;
    *reinterpret_cast<const void **>(self + 8 + parserAt) = &ts2_rva_e7db4c;
    *reinterpret_cast<int32_t *>(self + 4 + unknownAt) = unknownAt - kUnknownOffset;
    *reinterpret_cast<int32_t *>(self + 4 + parserAt) = parserAt - kParserOffset;
    if (mInitialised) Shutdown();
    std::destroy_at(&mList1A4);
    std::destroy_at(&mList19C);
    mText.Destruct();
    mMetaCommands.Destroy();
    mCommandList.Destroy();
    if (mCurrentDefinition) DefinitionRelease(mCurrentDefinition);
    std::destroy_at(&mNamespaces);
    std::destroy_at(&mNamespacePath);
    std::destroy_at(&mNamespaceLengths);
    DestroyPendingCallVector(&mPendingCalls);
    std::destroy_at(&mConditions);
    std::destroy_at(&mOpenBlocks);
    std::destroy_at(&mMetaBlocks);
    std::destroy_at(&mFileName);
    mGlobalNamespace.Destruct();
    // The definitions: each released (slot 1), the storage freed.
    if (mDefinitions.first) {
        for (void **p = static_cast<void **>(mDefinitions.first), **end = static_cast<void **>(mDefinitions.last); p != end; ++p)
            if (*p) DefinitionRelease(*p);
        ::operator delete(mDefinitions.first, static_cast<size_t>(static_cast<char *>(mDefinitions.end) - static_cast<char *>(mDefinitions.first)));
        mDefinitions = {};
    }
    mNames.Destruct();
    mCommands.Destruct();
    std::destroy_at(&mFunctions.mEntries);
    if (mSource) Uncount(mSource);
    if (mHandler) Uncount(mHandler);
    if (mOutput) Uncount(mOutput);
}

cBaseParser *cBaseParser::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cBaseParser));
    return this;
}

// ---- adapters ----

void __fastcall cBaseParserAdapters::PushNamespace(void *face, void *, const char *name) { cBaseParser::FromParserFace(face)->PushNamespace(name); }
void __fastcall cBaseParserAdapters::PurgeNamespace(void *face, void *, const char *name) { cBaseParser::FromParserFace(face)->PurgeNamespace(name); }
void __fastcall cBaseParserAdapters::ResetNamePool(NamePool *pool, void *) { nGZCommandParser::ResetNamePool(pool); }
CommandNode **__fastcall cBaseParserAdapters::EraseChildNode(CommandTable *table, void *, CommandNode **out, CommandNode *node)
{
    return nGZCommandParser::EraseChildNode(table, out, node);
}
InsertResult *__fastcall cBaseParserAdapters::InsertChild(CommandTable *table, void *, InsertResult *out, const KeyView *key)
{
    return table->Insert(out, key);
}
void __fastcall cBaseParserAdapters::RehashChildren(CommandTable *table, void *, uint32_t n) { table->Rehash(n); }
void __fastcall cBaseParserAdapters::DestroyPendingCallVector(cBaseParser::RawVector *calls, void *) { nGZCommandParser::DestroyPendingCallVector(calls); }

} // namespace nGZCommandParser
