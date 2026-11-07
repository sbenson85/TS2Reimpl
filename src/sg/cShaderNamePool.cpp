// nGZSceneGraph::NamePool, the name pool of a cShaderInit and of each of its descriptors (src/sg/cShaderInit.h). Its two
// halves are a std::vector<char> and a std::vector of dword offsets, laid out as this toolchain lays them out, so the
// functions work on them through Storage; growth, reserve and the large-block frees are the library's.
#include "sg/cShaderInit.h"

#include <cstring>
#include <vector>

namespace nGZSceneGraph {

namespace {

struct NamePoolStorage {
    std::vector<char> chars;
    std::vector<uint32_t> offsets;
};
static_assert(sizeof(NamePoolStorage) == sizeof(NamePool), "two vectors of three pointers");

NamePoolStorage &Storage(NamePool *pool) { return *reinterpret_cast<NamePoolStorage *>(pool); }
const NamePoolStorage &Storage(const NamePool *pool) { return *reinterpret_cast<const NamePoolStorage *>(pool); }

cIShaderName *EntryAt(char *base, uint32_t offset) { return reinterpret_cast<cIShaderName *>(base + offset); }

} // namespace

// 0xc62f50
NamePool *NamePool::Construct()
{
    std::memset(w, 0, sizeof(w));
    return this;
}

// 0xc62ee0
NamePool *NamePool::CopyConstruct(const NamePool *src)
{
    std::memset(w, 0, sizeof(w));
    Assign(src);
    return this;
}

// 0xc630e0
void NamePool::Clear()
{
    NamePoolStorage &s = Storage(this);
    // An entry's destructor may in principle touch the pool, so the ends are read again on every step.
    for (const uint32_t *p = s.offsets.data(); p != s.offsets.data() + s.offsets.size(); ++p)
        EntryAt(s.chars.data(), *p)->Destruct(0);
    s.chars.clear();
    s.offsets.clear();
}

// 0xc62f90
void NamePool::Destroy()
{
    Clear();
    NamePoolStorage &s = Storage(this);
    // Each buffer goes to a temporary that frees it, leaving the pool's pointers zero (the offsets first, then the
    // characters).
    std::vector<uint32_t>().swap(s.offsets);
    std::vector<char>().swap(s.chars);
}

// 0xc63030
void NamePool::Assign(const NamePool *src)
{
    if (src == this) return;
    Clear();
    NamePoolStorage &s = Storage(this);
    const NamePoolStorage &from = Storage(src);
    s.chars.reserve(from.chars.size());
    s.offsets.reserve(from.offsets.size());
    for (uint32_t i = 0; i < src->Count(); ++i) Add(reinterpret_cast<const cIShaderName *>(static_cast<uintptr_t>(src->At(i))));
}

// 0xc63120
void NamePool::Add(const cIShaderName *name)
{
    NamePoolStorage &s = Storage(this);
    const uint32_t offset = static_cast<uint32_t>(s.chars.size());
    s.offsets.push_back(offset);
    s.chars.resize(offset + name->mSize);
    name->CopyTo(s.chars.data() + offset);
}

} // namespace nGZSceneGraph
