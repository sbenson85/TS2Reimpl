// The canonical summary of a decoded GMDC (milestone N-e): what gmdc_decode prints and what the acceptance difftest
// (tests/gmdc_decode_acceptance.cpp) computes for the original decoder and ours, so the three can be compared as text.
//
// One line, `E<n>:<h> L<n>:<h> G<n>:<h> T<n>:<h> N<n>:<h> S<n>:<h> B<n>:<h> all:<h>`: per section the record count and
// a 64-bit FNV-1a hash (16 hex digits) of the section's serialisation, then the hash of the seven section hashes.
//  - Order: the vectors' own order, as the decoder left them (the order is part of what is compared); within a record
//    the fields in declaration order (src/sg/cGeometryDataContainerClass.h).
//  - Serialisation: every integer as 4 little-endian bytes (16-bit values widened), a vector as its count then its
//    items, a text as its length then its bytes, an element's data as its byte count then its bytes (as decoded,
//    whatever its block format says).
//  - Floats: their IEEE-754 bit patterns, not values: no rounding, -0 and +0 differ, a NaN keeps its payload. Both
//    decoders copy the stream's floats, so the bits must agree exactly.
//  - Sections: E elements (index, number, identity, repetition, block and set format, data, indices); L linkages (index,
//    active count, referenced size, element indices, the three alias lists); G groups (index, primitive type, index
//    count, link index, word +0x10, word +0x14, bone count, name, vertex indices, bones); T transforms (translation,
//    rotation); N name pairs; S subset meshes and B the bounding mesh (a triangle set as its vertex count, index count,
//    vertices, indices; a null set as 0xffffffff; B's count is 0 or 1).
// Reads the container's fields directly: the game's container has the same layout (the class is verified), so the
// difftest applies it to the original's objects too. A triangle set is read through its slot 40 (GetTriangleSetConst).
#pragma once

#include "sg/cGeometryDataContainerClass.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace gmdcsum {

struct Hash {
    uint64_t h = 0xcbf29ce484222325ull;
    void Bytes(const void *p, size_t n)
    {
        const auto *b = static_cast<const uint8_t *>(p);
        for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 0x100000001b3ull;
    }
    void U32(uint32_t v)
    {
        const uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
        Bytes(b, 4);
    }
    void F(float f)
    {
        uint32_t u;
        std::memcpy(&u, &f, 4);
        U32(u);
    }
    void Text(const std::string &s)
    {
        U32(uint32_t(s.size()));
        Bytes(s.data(), s.size());
    }
    void U16s(const std::vector<uint16_t> &v)
    {
        U32(uint32_t(v.size()));
        for (uint16_t x : v) U32(x);
    }
};

struct Summary {
    static constexpr int kSections = 7;
    static constexpr char kNames[kSections + 1] = "ELGTNSB";
    uint32_t counts[kSections] = {};
    uint64_t hashes[kSections] = {};
    uint64_t all = 0;

    std::string Line() const
    {
        std::string out;
        char part[48];
        for (int i = 0; i < kSections; ++i) {
            std::snprintf(part, sizeof(part), "%c%u:%016llx ", kNames[i], unsigned(counts[i]),
                          static_cast<unsigned long long>(hashes[i]));
            out += part;
        }
        std::snprintf(part, sizeof(part), "all:%016llx", static_cast<unsigned long long>(all));
        return out + part;
    }
};

inline void Mesh(Hash &h, nGZCompGeom::cITriangleSetShape *mesh)
{
    if (mesh == nullptr) {
        h.U32(0xffffffffu);
        return;
    }
    const nGZCompGeom::cTriangleSetData *d = mesh->GetTriangleSetConst();
    h.U32(uint32_t(d->mVertexEnd - d->mVertexBegin));
    h.U32(uint32_t(d->mIndexEnd - d->mIndexBegin));
    for (const auto *v = d->mVertexBegin; v != d->mVertexEnd; ++v) {
        h.F(v->x);
        h.F(v->y);
        h.F(v->z);
    }
    for (const uint16_t *i = d->mIndexBegin; i != d->mIndexEnd; ++i) h.U32(*i);
}

inline Summary Summarize(const nGZSceneGraph::cGeometryDataContainer &c)
{
    Summary s;
    Hash h[Summary::kSections];

    s.counts[0] = uint32_t(c.mElements.size());
    for (const auto &e : c.mElements) {
        for (uint32_t w : {e.mIndex, e.mNumber, e.mIdentity, e.mRepetition, e.mBlockFormat, e.mSetFormat}) h[0].U32(w);
        h[0].U32(uint32_t(e.mData.size()));
        h[0].Bytes(e.mData.data(), e.mData.size());
        h[0].U16s(e.mIndices);
    }
    s.counts[1] = uint32_t(c.mLinkages.size());
    for (const auto &l : c.mLinkages) {
        for (uint32_t w : {l.mIndex, l.mActiveCount, l.mReferencedSize}) h[1].U32(w);
        h[1].U16s(l.mElementIndices);
        for (const auto &a : l.mAliases) h[1].U16s(a);
    }
    s.counts[2] = uint32_t(c.mGroups.size());
    for (const auto &g : c.mGroups) {
        for (uint32_t w : {g.mIndex, g.mPrimType, g.mIndexCount, g.mLinkIndex, uint32_t(g.mWord10), g.mWord14,
                           g.mBoneCount})
            h[2].U32(w);
        h[2].Text(g.mName);
        h[2].U16s(g.mVertexIndices);
        h[2].U16s(g.mBones);
    }
    s.counts[3] = uint32_t(c.mTransforms.size());
    for (const auto &t : c.mTransforms) {
        for (float f : t.mTranslation) h[3].F(f);
        for (float f : t.mRotation) h[3].F(f);
    }
    s.counts[4] = uint32_t(c.mNamePairs.size());
    for (const auto &p : c.mNamePairs) {
        h[4].Text(p.first);
        h[4].Text(p.second);
    }
    s.counts[5] = uint32_t(c.mSubsetMeshes.size());
    for (const auto &m : c.mSubsetMeshes) Mesh(h[5], m.mMesh);
    s.counts[6] = c.mBoundingMesh != nullptr ? 1u : 0u;
    Mesh(h[6], c.mBoundingMesh);

    Hash all;
    for (int i = 0; i < Summary::kSections; ++i) {
        s.hashes[i] = h[i].h;
        all.U32(uint32_t(h[i].h));
        all.U32(uint32_t(h[i].h >> 32));
    }
    s.all = all.h;
    return s;
}

} // namespace gmdcsum
