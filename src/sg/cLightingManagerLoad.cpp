// nTSSG::cLightingManager, the lighting files' loading (lane 261110r, scenegraph LG3c rest): ReadLightingResources
// 0x2d0ae0 and slot 5 LoadLightingFiles 0x2df8a0. See sg/cLightingManager.h.
//
// Still the game's: nothing (the mask getter 0xb39360 is the constant 0x20000; the lists' and the allocator service's
// helpers are ours).
#include "sg/cLightingManager.h"
#include "sg/cLightingManagerDetail.h"

#include "app/AppDebugger.h"
#include "app/AppInit.h"
#include "gz/cFileParser.h"
#include "gz/cGZAny.h"
#include "rz/ServiceGetters.h"
#include "rz/cGZAllocatorService.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace nTSSG {

namespace {

constexpr uint32_t kLightingPaths = 0x6c9f7a1f;  // the app option listing the lighting script directories
constexpr uint32_t kScriptType = 0xadee8d84;     // the resource type of the lighting scripts
constexpr uint32_t kPathMask = 0x20000;          // 0xb39360's answer: the option's mask word
constexpr uint32_t kRequired = 5;                // the flags every script file is added with (required, watched)

// A node of the app option's list of strings (see cLightingManagerLife.cpp's).
struct PathNode {
    PathNode *mNext;
    PathNode *mPrev;
    GZString mText;
};
static_assert(sizeof(PathNode) == 0x20, "PathNode: a std::list<std::string> node");

// The resource manager (the cached service 0xb390c0's slot 3; class not identified).
void *ResourceManager() { return Call<3, void *>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>()); }

// The existing paths of the list go to the file parser as required files, last path first (the loop stops once the first
// node has been read). The manager's file parser word is read again for every file (the existence check may run code that
// changes it), so it is passed as the word's address.
void AddExistingFiles(nGZCommandParser::cIFileParser *const *parserSlot, PathNode *sentinel)
{
    if (sentinel->mNext == sentinel) return;
    PathNode *cursor = sentinel;
    do {
        PathNode *const node = cursor->mPrev;
        cRZString path;
        path.Construct(node->mText.c_str());
        if (cRZFile::PathExists(*path.AsInterface())) (*parserSlot)->AddFile(path.mString.c_str(), kRequired);
        path.Destruct();
        cursor = node;
    } while (cursor != sentinel->mNext);
}

} // namespace

void cLightingManager::ReadLightingResources(cLightingKeyList *keys)
{
    void *const manager = ResourceManager();
    nGZCommandParser::cIBaseParser *const parser = mFileParser->GetParser(); // receiver: nGZCommandParser::cIFileParser
    if (parser) AddRefThroughBase(parser);
    if (parser) {
        std::vector<char> buffer;
        const uint32_t count = static_cast<uint32_t>(keys->mEnd - keys->mBegin);
        for (uint32_t i = 0; i < count; ++i) {
            cLightingKey *const key = &keys->mBegin[i];
            if (key->mType != kScriptType) continue;
            void *resource = nullptr;
            // receiver: the resource manager; slot 39 opens (key, &resource, 0).
            if (Call<39, uint8_t>(manager, key, &resource, 0u)) {
                const uint32_t size = Call<10>(resource); // receiver: the resource; the size in bytes
                buffer.resize(size + 1);
                // receiver: the resource; slot 8 reads (buffer, size).
                if (Call<8, uint8_t>(resource, buffer.data(), size)) {
                    void *name = nullptr;
                    Call<34, uint8_t>(manager, key, &name); // receiver: the resource manager; the answer is not used
                    if (name) {
                        cRZString text;
                        text.Construct();
                        Call<11, uint32_t>(name, text.AsInterface()); // receiver: the key's name object; fills the text
                        // receiver: the parser's primary face; slot 14 takes (script, size, file name).
                        Call<14, uint32_t>(parser, buffer.data(), size, text.mString.c_str());
                        text.Destruct();
                    }
                    if (name) RawSlot<2, void>(name); // receiver: the name object (Release)
                }
                Call<6, uint32_t>(resource); // receiver: the resource; slot 6 closes it
                buffer.clear();
            }
            if (resource) RawSlot<2, void>(resource); // receiver: the resource (Release)
        }
    }
    if (parser) ReleaseThroughBase(parser);
}

void cLightingManager::LoadLightingFiles()
{
    appdebugger::ListHeader list{nullptr, 0};
    auto *const sentinel = static_cast<PathNode *>(appdebugger::AllocateListNodes(1, 0));
    sentinel->mNext = sentinel;
    sentinel->mPrev = sentinel;
    list.mHead = sentinel;
    appinit::CommonImplSlot5(kLightingPaths, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&list)), 1, 0, ~kPathMask);
    AddExistingFiles(&mFileParser, sentinel);
    appinit::CommonImplSlot5(kLightingPaths, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&list)), 1, 0, kPathMask);
    AddExistingFiles(&mFileParser, sentinel);
    mFileParser->RunFiles(); // receiver: nGZCommandParser::cIFileParser
    cLightingKeyList keys{nullptr, nullptr, nullptr};
    Call<19, uint32_t>(ResourceManager(), &keys, kScriptType); // receiver: the resource manager; lists the keys of a type
    ReadLightingResources(&keys);
    if (keys.mBegin) FreeThroughAllocatorService(keys.mBegin, 0);
    appdebugger::DestroyStringListNodes(&list, list.mHead);
    FreeThroughAllocatorService(list.mHead, 1);
}

} // namespace nTSSG
