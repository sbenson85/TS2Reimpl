#pragma once
// nTSWorld::cWallEdge (lane VR, campaign world-lot WL-0d): one directed edge of a wall section's quad-edge record
// (Guibas-Stolfi). RVAs 1.18.0.187. A cWallSection holds four of them in a row (index 0-3); Rot / InvRot / Sym move
// between the four (by the index at +8), Onext is the pointer at +0xc, and the compositions chain those through the
// virtual slots. cWallGraph (WL-2) builds the records through cWallSection.
//
// Layout (0x18 bytes): vtable +0, flag byte +4, index +8 (-1 until the section sets it), Onext +0xc, vertex (origin)
// +0x10, owner (the section) +0x14.
//
// Standalone shape (docs/standalone-shape.md): a real class on cIWallEdge, our vtable, every slot an override. The
// constructor hook (0x820be0, the section's eh-vector callback) and the destructor body hook (0x820c10) place ours;
// slots reach the game's objects through adapters. Bodies folded across unrelated classes (the single-word accessors
// 0x20daf0, 0x14a260, 0x1eeb80, 0x149c60, 0x145770, 0x149d00, 0x46390, 0x1458b0, 0x149cf0) are ours only in our vtable.
#include "world/cWallVertex.h"

#include <cstddef>
#include <cstdint>

namespace nTSWorld {

class cIWallEdge {
public:
    virtual void *SetOwner(void *owner) = 0;                  // 0 (+0x14); answers the value
    virtual int32_t SetIndex(int32_t index) = 0;              // 1 (+8)
    virtual cIWallVertex *SetVertex(cIWallVertex *v) = 0;     // 2 (+0x10)
    virtual cIWallVertex *SetDestVertex(cIWallVertex *v) = 0; // 3 0x821070: Sym()->SetVertex(v)
    virtual cIWallEdge *SetOnext(cIWallEdge *edge) = 0;       // 4 (+0xc)
    virtual void SetFlag(uint32_t value) = 0;                 // 5 0x821080: the low byte to +4
    virtual cIWallEdge *Rot() = 0;                            // 6 0x821050
    virtual cIWallEdge *RotC() const = 0;                     // 7 same body
    virtual cIWallEdge *InvRot() = 0;                         // 8 0x820cf0
    virtual cIWallEdge *InvRotC() const = 0;                  // 9
    virtual cIWallEdge *Sym() = 0;                            // 10 0x821090
    virtual cIWallEdge *SymC() const = 0;                     // 11
    virtual cIWallEdge *Onext() = 0;                          // 12 (+0xc)
    virtual cIWallEdge *OnextC() const = 0;                   // 13
    virtual cIWallEdge *RotOnextRot() = 0;                    // 14 0x820d90
    virtual cIWallEdge *RotOnextRotC() const = 0;             // 15 0x820d70
    virtual cIWallEdge *SymOnextSym() = 0;                    // 16 0x820c60
    virtual cIWallEdge *SymOnextSymC() const = 0;             // 17 0x820c40
    virtual cIWallEdge *InvRotOnextInvRot() = 0;              // 18 0x820ca0
    virtual cIWallEdge *InvRotOnextInvRotC() const = 0;       // 19 0x820c80
    virtual cIWallEdge *InvRotOnextRot() = 0;                 // 20 0x820d30
    virtual cIWallEdge *InvRotOnextRotC() const = 0;          // 21 0x820d10
    virtual cIWallEdge *OnextSym() = 0;                       // 22 0x820d60
    virtual cIWallEdge *OnextSymC() const = 0;                // 23 0x820d50
    virtual cIWallEdge *RotOnextInvRot() = 0;                 // 24 0x821010
    virtual cIWallEdge *RotOnextInvRotC() const = 0;          // 25 0x820ff0
    virtual cIWallEdge *SymOnext() = 0;                       // 26 0x821040
    virtual cIWallEdge *SymOnextC() const = 0;                // 27 0x821030
    virtual void *Owner() = 0;                                // 28 (+0x14)
    virtual int32_t Index() = 0;                              // 29 (+8)
    virtual float Angle() = 0;                                // 30 0x820db0: radians from the origin to the far vertex
    virtual float ReverseAngle() = 0;                         // 31 0x820cd0: Sym()->Angle()
    virtual int32_t AngleDegrees() = 0;                       // 32 0x820ee0: the same angle in whole degrees
    virtual int32_t ReverseAngleDegrees() = 0;                // 33 0x820ce0: Sym()->AngleDegrees()
    virtual uint8_t Flag() = 0;                               // 34 (+4)
    virtual cIWallVertex *Vertex() = 0;                       // 35 (+0x10)
    virtual cIWallVertex *DestVertex() = 0;                   // 36 0x820cc0: Sym()->Vertex()
    virtual ~cIWallEdge() = default;                          // 37 deleting destructor
};

class cWallEdge : public cIWallEdge {
public:
    static constexpr uint32_t kVTable = 0xe053f4;

    cWallEdge(); // 0x820be0
    cWallEdge(const cWallEdge &) = delete;
    cWallEdge &operator=(const cWallEdge &) = delete;
    ~cWallEdge() override; // slot 37 0x820c20 (deleting); the body 0x820c10 clears the owner

    void *SetOwner(void *owner) override { return mOwner = owner; }
    int32_t SetIndex(int32_t index) override { return mIndex = index; }
    cIWallVertex *SetVertex(cIWallVertex *v) override { return mVertex = v; }
    cIWallVertex *SetDestVertex(cIWallVertex *v) override;
    cIWallEdge *SetOnext(cIWallEdge *edge) override { return mOnext = edge; }
    void SetFlag(uint32_t value) override;
    cIWallEdge *Rot() override;
    cIWallEdge *RotC() const override;
    cIWallEdge *InvRot() override;
    cIWallEdge *InvRotC() const override;
    cIWallEdge *Sym() override;
    cIWallEdge *SymC() const override;
    cIWallEdge *Onext() override { return mOnext; }
    cIWallEdge *OnextC() const override { return mOnext; }
    cIWallEdge *RotOnextRot() override;
    cIWallEdge *RotOnextRotC() const override;
    cIWallEdge *SymOnextSym() override;
    cIWallEdge *SymOnextSymC() const override;
    cIWallEdge *InvRotOnextInvRot() override;
    cIWallEdge *InvRotOnextInvRotC() const override;
    cIWallEdge *InvRotOnextRot() override;
    cIWallEdge *InvRotOnextRotC() const override;
    cIWallEdge *OnextSym() override;
    cIWallEdge *OnextSymC() const override;
    cIWallEdge *RotOnextInvRot() override;
    cIWallEdge *RotOnextInvRotC() const override;
    cIWallEdge *SymOnext() override;
    cIWallEdge *SymOnextC() const override;
    void *Owner() override { return mOwner; }
    int32_t Index() override { return mIndex; }
    float Angle() override;
    float ReverseAngle() override;
    int32_t AngleDegrees() override;
    int32_t ReverseAngleDegrees() override;
    uint8_t Flag() override { return mFlag; }
    cIWallVertex *Vertex() override { return mVertex; }
    cIWallVertex *DestVertex() override;

    uint8_t mFlag = 0;                  // +4
    int32_t mIndex = -1;                // +8: 0-3 in its section
    cIWallEdge *mOnext = nullptr;       // +0xc
    cIWallVertex *mVertex = nullptr;    // +0x10
    void *mOwner = nullptr;             // +0x14
};
static_assert(sizeof(cWallEdge) == 0x18 && offsetof(cWallEdge, mFlag) == 4 && offsetof(cWallEdge, mIndex) == 8 &&
              offsetof(cWallEdge, mOnext) == 0xc && offsetof(cWallEdge, mVertex) == 0x10 && offsetof(cWallEdge, mOwner) == 0x14);

// The hooks' entry points: `this` is an edge with the game's vtable or ours; each calls our member without virtual
// dispatch of its own slot (the calls the body makes on other edges stay virtual). Construct places ours.
struct cWallEdgeAdapters {
    using Self = cWallEdge;
    static Self *__fastcall Construct(Self *self, void *);
    static void __fastcall DestructBody(Self *self, void *);
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);
    static cIWallVertex *__fastcall SetDestVertex(Self *self, void *, cIWallVertex *v);
    static void __fastcall SetFlag(Self *self, void *, uint32_t value);
    static cIWallEdge *__fastcall Rot(Self *self, void *);
    static cIWallEdge *__fastcall InvRot(Self *self, void *);
    static cIWallEdge *__fastcall Sym(Self *self, void *);
    static cIWallEdge *__fastcall RotOnextRot(Self *self, void *);
    static cIWallEdge *__fastcall RotOnextRotC(Self *self, void *);
    static cIWallEdge *__fastcall SymOnextSym(Self *self, void *);
    static cIWallEdge *__fastcall SymOnextSymC(Self *self, void *);
    static cIWallEdge *__fastcall InvRotOnextInvRot(Self *self, void *);
    static cIWallEdge *__fastcall InvRotOnextInvRotC(Self *self, void *);
    static cIWallEdge *__fastcall InvRotOnextRot(Self *self, void *);
    static cIWallEdge *__fastcall InvRotOnextRotC(Self *self, void *);
    static cIWallEdge *__fastcall OnextSym(Self *self, void *);
    static cIWallEdge *__fastcall OnextSymC(Self *self, void *);
    static cIWallEdge *__fastcall RotOnextInvRot(Self *self, void *);
    static cIWallEdge *__fastcall RotOnextInvRotC(Self *self, void *);
    static cIWallEdge *__fastcall SymOnext(Self *self, void *);
    static cIWallEdge *__fastcall SymOnextC(Self *self, void *);
    static float __fastcall Angle(Self *self, void *);
    static float __fastcall ReverseAngle(Self *self, void *);
    static int32_t __fastcall AngleDegrees(Self *self, void *);
    static int32_t __fastcall ReverseAngleDegrees(Self *self, void *);
    static cIWallVertex *__fastcall DestVertex(Self *self, void *);
};

} // namespace nTSWorld
