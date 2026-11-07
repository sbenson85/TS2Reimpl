// nGZSceneGraph::cSkinExtension (lane QP). Layout and the hook decision: src/sg/cSkinExtension.h.
#include "sg/cSkinExtension.h"

#include "gz/cIGZString.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "sg/cObjectGraphNodeImpl.h" // NewTypeName

#include <cstring>

namespace nGZSceneGraph {

static_assert(sizeof(cSkinExtension) == 0x74, "cSkinExtension size (the factory and Clone allocate 0x74)");

// 0xa97720: new and the constructor (most-derived), answered as the cIExtension.
cIGZUnknown *__cdecl cSkinExtension::Create()
{
    cSkinExtension *const extension = new cSkinExtension;
    cIExtension *const face = static_cast<cISkinExtension *>(extension);
    return face;
}

// 0xa97240: cExtension's part (0xa3b7d0), an empty name, the word 0 and three empty lists.
cSkinExtension::cSkinExtension() { mName.Construct(); }

// 0xa970e0: cExtension's copy (0xa3b750: nothing copied), then `other`'s word and name through its face.
// ORIGINAL-BUG: the three lists are not copied, so a Clone has none (kept: the game's clones behave so).
cSkinExtension::cSkinExtension(const cSkinExtension &other) : cExtension(other)
{
    mName.Construct();
    cSkinExtension &source = const_cast<cSkinExtension &>(other);
    mWord = source.GetWord();
    mName.FromChar(source.GetName());
}

// 0xa97340: the lists emptied in their order (pairs: second released, then first; then the name map; then the
// pointers), then the name and cExtension's part.
cSkinExtension::~cSkinExtension()
{
    mPairs.clear();
    mNameMap.clear();
    mRefs.clear();
    mName.Destruct();
}

// ---- the referent face ----

// 0xa97af0 (ret 8): a null out answers false; kIid the cISkinExtension face, counted; anything else cExtension's.
bool cSkinExtension::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid != cISkinExtension::kIid) return cExtension::QueryInterface(iid, out);
    *out = static_cast<cISkinExtension *>(this);
    cExtension::AddRef();
    return true;
}

// 0xa97b50 (ret 4): the class's record read (its name released last), cExtension's (0x9dc310), the name (reader slot 22
// into the string) and the word. No version test.
void cSkinExtension::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cExtension::Read(reader);
    reader->GetGZStr(mName.AsInterface());
    mWord = reader->GetUint32();
    if (info.mName) info.mName->Release();
}

// 0xa97e50 (hidden out): {"cSkinExtension" (a new counted cRZString), the class id, version 1}.
cPersistTypeInfo cSkinExtension::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = NewTypeName("cSkinExtension");
    info.mWord4 = kClassId;
    info.mVersion = kVersion;
    return info;
}

// 0xa97f50 (ret 4): the class's record (0xa97e50 directly) through the writer's slot 28, its name released;
// cExtension's Write (0xa3c210); then the name (slot 23) and the word (slot 10) through the cISkinExtension face's
// getters. The lists are not written.
void cSkinExtension::Write(ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info = cSkinExtension::GetTypeInfo();
    writer->WriteTypeInfo(&info);
    if (info.mName) info.mName->Release();
    cExtension::WriteExtension(writer);
    writer->WriteRZCharStr(const_cast<char *>(GetName()));
    writer->SetUint32(GetWord());
}

// ---- cIExtension ----

// 0xa97660: a copy (the copy constructor 0xa970e0), uncounted, as its cIExtension.
cIExtension *cSkinExtension::Clone()
{
    cSkinExtension *const copy = new cSkinExtension(*this);
    return static_cast<cISkinExtension *>(copy);
}

// 0xa97e30 (ret 4): kIid, else cExtension's 0xa3c130.
bool cSkinExtension::IsKindOf(uint32_t iid)
{
    if (iid == cISkinExtension::kIid) return true;
    return cExtension::IsKindOf(iid);
}

// 0xa97700: the extension's type name.
const char *cSkinExtension::GetTypeName() { return "skin"; }

// ---- cISkinExtension ----

void cSkinExtension::SetName(const char *name) { mName.mString.assign(name); }
void cSkinExtension::SetWord(uint32_t value) { mWord = value; }

cIGZUnknown *cSkinExtension::GetPairFirstConst(uint32_t index) { return mPairs[index].first.Get(); }
cIGZUnknown *cSkinExtension::GetPairFirst(uint32_t index) { return mPairs[index].first.Get(); }
cIGZUnknown *cSkinExtension::GetPairSecondConst(uint32_t index) { return mPairs[index].second.Get(); }
cIGZUnknown *cSkinExtension::GetPairSecond(uint32_t index) { return mPairs[index].second.Get(); }

// 0xa974c0 (ret 8): a counted pair built first (first, then second counted), its copy appended (counted again), the
// pair released (second, then first).
void cSkinExtension::AddPair(cIGZUnknown *first, cIGZUnknown *second)
{
    const std::pair<cSkinRef, cSkinRef> entry(first, second);
    mPairs.push_back(entry);
}

// 0xa97be0 (ret 4): the later pairs assigned down one place (first, then second of each), the last one released.
void cSkinExtension::ErasePair(uint32_t index) { mPairs.erase(mPairs.begin() + index); }

// 0xa97efe (ret 8): the pair's second pointer replaced (counted; nothing when it is the same).
void cSkinExtension::SetPairSecond(uint32_t index, cIGZUnknown *second) { mPairs[index].second.Assign(second); }

// 0xa97880 (ret 8): both strings lowercased (_strlwr, as cRZString's helper 0x1ad167). With no key the name map is
// emptied and {name, ""} appended. Otherwise the first element whose key equals the key gets the name; with none,
// {name, key} is appended.
void cSkinExtension::MapName(const char *name, const char *key)
{
    std::string lowerName(name);
    _strlwr(&lowerName[0]);
    if (!key) {
        mNameMap.clear();
        mNameMap.push_back(std::pair<std::string, std::string>(lowerName, std::string()));
        return;
    }
    std::string lowerKey(key);
    _strlwr(&lowerKey[0]);
    for (std::pair<std::string, std::string> &element : mNameMap) {
        if (std::strcmp(element.second.c_str(), lowerKey.c_str()) == 0) {
            element.first.assign(lowerName.c_str());
            return;
        }
    }
    mNameMap.push_back(std::pair<std::string, std::string>(lowerName, lowerKey));
}

cIGZUnknown *cSkinExtension::GetRef(uint32_t index) { return mRefs[index].Get(); }

// 0xa975a0 (ret 4): as AddPair, with one pointer.
void cSkinExtension::PushRef(cIGZUnknown *ref)
{
    const cSkinRef entry(ref);
    mRefs.push_back(entry);
}

// 0xa97ce0 (ret 4): as ErasePair.
void cSkinExtension::EraseRef(uint32_t index) { mRefs.erase(mRefs.begin() + index); }

// 0xa977d2: the name string's characters (its slot 5).
const char *cSkinExtension::GetName() { return mName.ToChar(); }
uint32_t cSkinExtension::GetWord() { return mWord; }
uint32_t cSkinExtension::GetPairCount() { return static_cast<uint32_t>(mPairs.size()); }
const char *cSkinExtension::GetMappedName(uint32_t index) { return mNameMap[index].first.c_str(); }
const char *cSkinExtension::GetMappedKey(uint32_t index) { return mNameMap[index].second.c_str(); }
uint32_t cSkinExtension::GetNameMapCount() { return static_cast<uint32_t>(mNameMap.size()); }
uint32_t cSkinExtension::GetRefCount() { return static_cast<uint32_t>(mRefs.size()); }

cISkinExtension *__cdecl NextSkinExtension(cIObjectGraphNode *node, cISkinExtension *previous)
{
    cIExtension *after = nullptr;
    if (previous) {
        after = static_cast<cIExtension *>(previous)->GetNext();
        if (!after) return nullptr;
    }
    void *found = nullptr;
    cISkinExtension *result = node->FindExtension(cISkinExtension::kIid, &found, after) ? static_cast<cISkinExtension *>(found)
                                                                                        : nullptr;
    if (found) static_cast<cIExtension *>(static_cast<cISkinExtension *>(found))->Release();
    return result;
}

} // namespace nGZSceneGraph
