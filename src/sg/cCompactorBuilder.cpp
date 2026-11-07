// nGZSceneGraph::cCompactorBuilder (lane QE). Layout and the hook decision: src/sg/cCompactorBuilder.h.
#include "sg/cCompactorBuilder.h"

#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "sg/cGeometryDataContainerClass.h"
#include "sg/cObjectGraphNodeImpl.h" // NewTypeName

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace nGZSceneGraph {

static_assert(sizeof(cCompactorBuilder) == 0x2c, "cCompactorBuilder size (the factory allocates 0x2c)");
static_assert(offsetof(cCompactorBuilder, mPerGroupReset) == 0x14, "cCompactorBuilder per-group reset");
static_assert(offsetof(cCompactorBuilder, mEnabled) == 0x15, "cCompactorBuilder enabled");

namespace {

// A range the container hands out, its three iterators released as the original's (end, begin, then the cursor).
template <typename T> struct Range {
    cGeometryRange<T> r;
    Range() = default;
    Range(const Range &) = delete;
    Range &operator=(const Range &) = delete;
    ~Range()
    {
        if (r.mEnd) r.mEnd->Release();
        if (r.mBegin) r.mBegin->Release();
        if (r.mCurrent) r.mCurrent->Release();
    }
};

// The cursor put back at begin (a clone of it, counted) unless it is there already.
template <typename T> void Rewind(cGeometryRange<T> &r)
{
    if (r.mCurrent->IsEqual(r.mBegin)) return;
    cIGeometryIterator<T> *const begin = r.mBegin->Clone();
    if (begin) begin->AddRef();
    if (r.mCurrent) r.mCurrent->Release();
    r.mCurrent = begin;
}

} // namespace

// 0xa83930: new and the constructor (most-derived), answered as the cIGeometryBuilder (reached through the
// cICompactorBuilder's own virtual base, the same subobject).
cIGZUnknown *__cdecl cCompactorBuilder::Create()
{
    cCompactorBuilder *const builder = new cCompactorBuilder;
    cIGeometryBuilder *const face = static_cast<cICompactorBuilder *>(builder);
    return face;
}

// 0xa837e0: cGeometryBuilder's part (0xa44b50), then both flags set (one word store, 0x101).
cCompactorBuilder::cCompactorBuilder() = default;

// 0xa83870: nothing of its own; cGeometryBuilder's part.
cCompactorBuilder::~cCompactorBuilder() = default;

// 0xa83a00 (ret 8): a null out answers false; kIid gives the cICompactorBuilder face, counted through the virtual base;
// everything else is cGeometryBuilder's (0xa45310).
bool cCompactorBuilder::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid != cICompactorBuilder::kIid) return cGeometryBuilder::QueryInterface(iid, out);
    *out = static_cast<cICompactorBuilder *>(this);
    static_cast<cIGeometryBuilder *>(this)->AddRef();
    return true;
}

// 0x3b7bd0 (folded, ret 4): its own type-info record read, then cGeometryBuilder's Read (0x9dc310), then its own
// record's name released.
void cCompactorBuilder::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cGeometryBuilder::Read(reader);
    if (info.mName) info.mName->Release();
}

// 0xa84480 (hidden out): {"cCompactorBuilder" (a new counted cRZString), the class id, version 1}.
cPersistTypeInfo cCompactorBuilder::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = NewTypeName("cCompactorBuilder");
    info.mWord4 = kClassId;
    info.mVersion = kVersion;
    return info;
}

// 0xa84550 (ret 4): its record (0xa84480 directly) through the writer's slot 28, its name released, then
// cGeometryBuilder's Write (0xa45790). The flags are not persisted.
void cCompactorBuilder::Write(ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info = cCompactorBuilder::GetTypeInfo();
    writer->WriteTypeInfo(&info);
    if (info.mName) info.mName->Release();
    cGeometryBuilder::Write(writer);
}

// 0xa839cc (the vtordisp thunk of slot 16): the body only when the builder is enabled.
void cCompactorBuilder::Build(cIGeometryDataContainer *container)
{
    if (mEnabled) Compact(container);
}

// 0xa838f6.
const char *cCompactorBuilder::GetName() { return "CompactorBuilder"; }

// 0x9e0f30.
void cCompactorBuilder::SetPerGroupReset(bool reset) { mPerGroupReset = reset; }

// 0xa83a60.
//   1. Each linkage's groups, in group order, walk their vertex indices: a vertex not numbered yet in the linkage takes the
//      next new number, and every index is appended (renumbered) to one list; each group's start and count in it are
//      kept. With the per-group reset the remap entries from the group's lowest vertex to its last are forgotten
//      after each group (so the next group numbers them again).
//   2. Each linkage's new-to-old table: sized to its highest new number + 1 (zeros), each group's new index mapped to
//      the old index at the same position.
//   3. Each linkage's three alias lists rebuilt through that table (the old list's At of the old index), its referenced
//      size set to the table's size.
//   4. Each group with indices gets its renumbered run.
void cCompactorBuilder::Compact(cIGeometryDataContainer *container)
{
    const uint32_t groupCount = container->CountGroups();
    std::vector<std::pair<int32_t, int32_t>> runs(groupCount, std::pair<int32_t, int32_t>(-1, -1));
    std::vector<uint16_t> indices;

    {
        std::vector<int32_t> remap;
        for (uint32_t link = 0; link < container->CountLinkages(); ++link) {
            cGeometryLinkageHeader header;
            container->GetLinkageHeader(&header, link);
            remap.clear();
            remap.resize(header.mReferencedSize, -1);
            int32_t next = 0;
            for (uint32_t g = 0; g < container->CountGroups(); ++g) {
                cGeometryGroupInfo info;
                container->GetGroupInfo(&info, g);
                if (info.mLinkIndex != link) continue;
                runs[g].first = static_cast<int32_t>(indices.size());
                Range<uint16_t> vertices;
                container->GetGroupVertexIndices(&vertices.r, g);
                int32_t lowest = vertices.r.mCurrent->Get();
                int32_t last = lowest;
                Rewind(vertices.r);
                while (!vertices.r.mCurrent->IsEqual(vertices.r.mEnd)) {
                    const uint16_t vertex = vertices.r.mCurrent->Get();
                    if (remap[vertex] < 0) remap[vertex] = next++;
                    indices.push_back(static_cast<uint16_t>(remap[vertex]));
                    lowest = std::min<int32_t>(lowest, vertex);
                    last = vertex;
                    vertices.r.mCurrent->Next();
                }
                runs[g].second = static_cast<int32_t>(indices.size()) - runs[g].first;
                // ORIGINAL-BUG: the range forgotten runs from the group's lowest vertex to its last one (the original
                // restarts its "highest" from the lowest at each index), not to its highest, and stops short of that
                // vertex, whose new number the next group of the linkage reuses. The lowest includes the last vertex,
                // so the range is never reversed.
                if (mPerGroupReset) std::fill(remap.begin() + lowest, remap.begin() + last, -1);
            }
        }
    }

    const uint32_t linkCount = container->CountLinkages();
    std::vector<std::vector<uint16_t>> oldOf(linkCount);
    for (uint32_t link = 0; link < container->CountLinkages(); ++link) {
        int32_t highest = 0;
        for (uint32_t g = 0; g < container->CountGroups(); ++g) {
            cGeometryGroupInfo info;
            container->GetGroupInfo(&info, g);
            if (info.mLinkIndex != link) continue;
            for (int32_t i = 0; i < runs[g].second; ++i)
                highest = std::max<int32_t>(highest, indices[runs[g].first + i]);
        }
        oldOf[link].resize(highest + 1);
        for (uint32_t g = 0; g < container->CountGroups(); ++g) {
            cGeometryGroupInfo info;
            container->GetGroupInfo(&info, g);
            if (info.mLinkIndex != link) continue;
            Range<uint16_t> vertices;
            container->GetGroupVertexIndices(&vertices.r, g);
            for (uint32_t i = 0; i < static_cast<uint32_t>(runs[g].second); ++i)
                oldOf[link][indices[runs[g].first + i]] = vertices.r.mCurrent->At(static_cast<int32_t>(i));
        }
    }

    if (!indices.empty() && container->CountLinkages() != 0) {
        for (uint32_t link = 0; link < container->CountLinkages(); ++link) {
            const std::vector<uint16_t> &table = oldOf[link];
            const uint32_t size = static_cast<uint32_t>(table.size());
            std::vector<uint16_t> aliases[3] = {std::vector<uint16_t>(size), std::vector<uint16_t>(size),
                                                std::vector<uint16_t>(size)};
            for (uint32_t which = 0; which < 3; ++which) {
                Range<uint16_t> old;
                container->GetLinkageAliases(&old.r, link, which);
                for (uint32_t i = 0; i < size; ++i) aliases[which][i] = old.r.mCurrent->At(table[i]);
            }
            container->SetLinkageReferencedSize(link, size);
            for (uint32_t which = 0; which < 3; ++which)
                container->SetLinkageAliases(link, which, aliases[which].data(), size);
        }
    }

    for (uint32_t g = 0; g < groupCount; ++g) {
        if (runs[g].second <= 0) continue;
        container->SetGroupVertexIndices(g, indices.data() + runs[g].first, static_cast<uint32_t>(runs[g].second));
    }
}

} // namespace nGZSceneGraph
