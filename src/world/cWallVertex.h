#pragma once
// nTSWorld::cWallVertex (vtable 0xe0532c, 10 slots, 0x1c-byte objects; lane VR, campaign world-lot WL-0d): a wall-graph
// vertex. It keeps its position, a count of the edges that start at it, and the first of them (the one with the least
// angle); the edges link round the vertex through their Onext pointers (cWallEdge, a quad-edge record). cWallGraph (WL-2)
// builds it. RVAs 1.18.0.187.
//
// Layout: vtable +0, id +4 (-1 until set), position +8 (three floats; the default is -1, -1, 0), first edge +0x14,
// edge count +0x18.
//
// Standalone shape (docs/standalone-shape.md): a real class on cIWallVertex, our vtable. cWallEdge's angle slots read the
// position through slot 5, so the interface is declared first and cWallEdge.h includes this file. The one-word slots
// 0 (0x1489e0), 4 (0x147a30), 5 (0x142840), 6 (0x148ba0) and 7 (0x149d00) are shared bodies, ours only in our vtable.
#include <cstddef>
#include <cstdint>

namespace nTSWorld {

class cIWallEdge;

class cIWallVertex {
public:
    virtual int32_t SetId(int32_t id) = 0;                    // 0 0x1489e0 (shared) (+4)
    virtual uint32_t SetPosition(const void *position) = 0;   // 1 0x81ff30: 12 bytes copied to +8, answers the last dword
    virtual void AddEdge(cIWallEdge *edge) = 0;               // 2 0x81fe40
    virtual void RemoveEdge(cIWallEdge *edge) = 0;            // 3 0x81fef0
    virtual int32_t Id() = 0;                                 // 4 0x147a30 (shared) (+4)
    virtual float *Position() = 0;                            // 5 0x142840 (shared) (+8, three floats)
    virtual int32_t EdgeCount() = 0;                          // 6 0x148ba0 (shared) (+0x18)
    virtual cIWallEdge *FirstEdge() = 0;                      // 7 0x149d00 (shared) (+0x14)
    virtual cIWallEdge *EdgeForAngle(float angle) = 0;        // 8 0x81fe90
    virtual ~cIWallVertex() = default;                        // 9 0x81fe20 deleting destructor (0x1c)
};

class cWallVertex : public cIWallVertex {
public:
    static constexpr uint32_t kVTable = 0xe0532c;

    cWallVertex();                          // 0x81fdd0: position (-1, -1, 0)
    explicit cWallVertex(const float *pos); // 0x81fd90: position copied as three dwords
    cWallVertex(const cWallVertex &) = delete;
    cWallVertex &operator=(const cWallVertex &) = delete;
    ~cWallVertex() override = default; // slot 9 0x81fe20 (deleting); the body 0x81fe10 stores the vtable only

    int32_t SetId(int32_t id) override { return mId = id; }
    uint32_t SetPosition(const void *position) override;
    void AddEdge(cIWallEdge *edge) override;
    void RemoveEdge(cIWallEdge *edge) override;
    int32_t Id() override { return mId; }
    float *Position() override { return mPos; }
    int32_t EdgeCount() override { return mCount; }
    cIWallEdge *FirstEdge() override { return mFirst; }
    cIWallEdge *EdgeForAngle(float angle) override;

    int32_t mId = -1;              // +4
    float mPos[3] = {-1.0f, -1.0f, 0.0f}; // +8
    cIWallEdge *mFirst = nullptr;  // +0x14
    int32_t mCount = 0;            // +0x18
};
static_assert(sizeof(cWallVertex) == 0x1c && offsetof(cWallVertex, mId) == 4 && offsetof(cWallVertex, mPos) == 8 &&
              offsetof(cWallVertex, mFirst) == 0x14 && offsetof(cWallVertex, mCount) == 0x18);

// The hooks' entry points. The constructors place ours over the block; the calls the bodies make on other objects (the
// edges) and on this vertex's own count stay virtual.
struct cWallVertexAdapters {
    using Self = cWallVertex;
    static Self *__fastcall ConstructAt(Self *self, void *, const float *pos);
    static Self *__fastcall Construct(Self *self, void *);
    static void __fastcall DestructBody(Self *self, void *);
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);
    static uint32_t __fastcall SetPosition(Self *self, void *, const void *position);
    static void __fastcall AddEdge(Self *self, void *, cIWallEdge *edge);
    static void __fastcall RemoveEdge(Self *self, void *, cIWallEdge *edge);
    static cIWallEdge *__fastcall EdgeForAngle(Self *self, void *, float angle);
};

} // namespace nTSWorld
