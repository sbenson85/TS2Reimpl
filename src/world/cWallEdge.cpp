#include "world/cWallEdge.h"

#include <cmath>
#include <cstring>
#include <new>

namespace nTSWorld {
namespace {

constexpr float kPi = 3.14159274101257f;      // 0x40490fdb
constexpr float kHalfPi = 1.57079637050629f;  // 0x3fc90fdb
constexpr double kPiDouble = 3.1415927410125732;
constexpr float kDegreesPerRadian = 57.2957763671875f;
constexpr float kRoundMagic = 12582912.0f; // 0x4b400000: adding it leaves the rounded integer in the low mantissa bits

// The two endpoints of an edge as (x0, y0) -> (x1, y1): the origin vertex's position and the far vertex's.
struct Segment {
    float x0, y0, x1, y1;
};

Segment SegmentOf(cWallEdge *edge)
{
    const float *from = edge->Vertex()->Position();
    const float *to = edge->DestVertex()->Position();
    return {from[0], from[1], to[0], to[1]};
}

} // namespace

cWallEdge::cWallEdge() = default;

cWallEdge::~cWallEdge() { mOwner = nullptr; }

cIWallVertex *cWallEdge::SetDestVertex(cIWallVertex *v) { return SymC()->SetVertex(v); } // through the const slot, as the original

void cWallEdge::SetFlag(uint32_t value) { mFlag = static_cast<uint8_t>(value); }

// The four edges of a section sit in a row, 0x18 bytes apart; the index at +8 says which one this is.
cIWallEdge *cWallEdge::Rot() { return mIndex >= 3 ? this - 3 : this + 1; }
cIWallEdge *cWallEdge::RotC() const { return mIndex >= 3 ? const_cast<cWallEdge *>(this) - 3 : const_cast<cWallEdge *>(this) + 1; }
cIWallEdge *cWallEdge::InvRot() { return mIndex > 0 ? this - 1 : this + 3; }
cIWallEdge *cWallEdge::InvRotC() const { return mIndex > 0 ? const_cast<cWallEdge *>(this) - 1 : const_cast<cWallEdge *>(this) + 3; }
cIWallEdge *cWallEdge::Sym() { return mIndex >= 2 ? this - 2 : this + 2; }
cIWallEdge *cWallEdge::SymC() const { return mIndex >= 2 ? const_cast<cWallEdge *>(this) - 2 : const_cast<cWallEdge *>(this) + 2; }

cIWallEdge *cWallEdge::RotOnextRot() { return Rot()->Onext()->Rot(); }
cIWallEdge *cWallEdge::RotOnextRotC() const { return RotC()->OnextC()->RotC(); }
cIWallEdge *cWallEdge::SymOnextSym() { return Sym()->Onext()->Sym(); }
cIWallEdge *cWallEdge::SymOnextSymC() const { return SymC()->OnextC()->SymC(); }
cIWallEdge *cWallEdge::InvRotOnextInvRot() { return InvRot()->Onext()->InvRot(); }
cIWallEdge *cWallEdge::InvRotOnextInvRotC() const { return InvRotC()->OnextC()->InvRotC(); }
cIWallEdge *cWallEdge::InvRotOnextRot() { return InvRot()->Onext()->Rot(); }
cIWallEdge *cWallEdge::InvRotOnextRotC() const { return InvRotC()->OnextC()->RotC(); }
cIWallEdge *cWallEdge::OnextSym() { return Onext()->Sym(); }
cIWallEdge *cWallEdge::OnextSymC() const { return OnextC()->SymC(); }
cIWallEdge *cWallEdge::RotOnextInvRot() { return Rot()->Onext()->InvRot(); }
cIWallEdge *cWallEdge::RotOnextInvRotC() const { return RotC()->OnextC()->InvRotC(); }
cIWallEdge *cWallEdge::SymOnext() { return Sym()->Onext(); }
cIWallEdge *cWallEdge::SymOnextC() const { return SymC()->OnextC(); }

cIWallVertex *cWallEdge::DestVertex() { return Sym()->Vertex(); }

float cWallEdge::Angle()
{
    const Segment s = SegmentOf(this);
    if (s.y0 == s.y1) return s.x1 > s.x0 ? 0.0f : kPi;
    if (s.x0 == s.x1) return s.y1 > s.y0 ? kHalfPi : -kHalfPi;
    const float dx = s.x1 - s.x0;
    const float dy = s.y1 - s.y0;
    // Both diagonals are exact quarter turns of the double pi constant, narrowed to float.
    if (dx == dy) return static_cast<float>((dx > 0.0f ? 0.25 : -0.75) * kPiDouble);
    if (dx == -dy) return static_cast<float>((dx > 0.0f ? -0.25 : 0.75) * kPiDouble);
    return static_cast<float>(std::atan2(static_cast<double>(dy), static_cast<double>(dx)));
}

float cWallEdge::ReverseAngle() { return Sym()->Angle(); }

int32_t cWallEdge::AngleDegrees()
{
    const Segment s = SegmentOf(this);
    if (s.y0 == s.y1) return s.x1 > s.x0 ? 0 : 180;
    if (s.x0 == s.x1) return s.y1 > s.y0 ? 90 : -90;
    const float dx = s.x1 - s.x0;
    const float dy = s.y1 - s.y0;
    if (dx == dy) return dx > 0.0f ? 45 : -135;
    if (dx == -dy) return dx > 0.0f ? -45 : 135;
    // Degrees rounded to nearest by the magic-number add: the sum's bits minus the constant's are the integer.
    float degrees = static_cast<float>(std::atan2(static_cast<double>(dy), static_cast<double>(dx))) * kDegreesPerRadian;
    degrees += kRoundMagic;
    int32_t bits;
    std::memcpy(&bits, &degrees, sizeof(bits));
    return bits - 0x4b400000;
}

int32_t cWallEdge::ReverseAngleDegrees() { return Sym()->AngleDegrees(); }

// ---- adapters ----

cWallEdge *__fastcall cWallEdgeAdapters::Construct(Self *self, void *) { return new (self) Self(); }
void __fastcall cWallEdgeAdapters::DestructBody(Self *self, void *) { self->Self::~Self(); }
cWallEdge *__fastcall cWallEdgeAdapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}
cIWallVertex *__fastcall cWallEdgeAdapters::SetDestVertex(Self *self, void *, cIWallVertex *v) { return self->Self::SetDestVertex(v); }
void __fastcall cWallEdgeAdapters::SetFlag(Self *self, void *, uint32_t value) { self->Self::SetFlag(value); }
cIWallEdge *__fastcall cWallEdgeAdapters::Rot(Self *self, void *) { return self->Self::Rot(); }
cIWallEdge *__fastcall cWallEdgeAdapters::InvRot(Self *self, void *) { return self->Self::InvRot(); }
cIWallEdge *__fastcall cWallEdgeAdapters::Sym(Self *self, void *) { return self->Self::Sym(); }
cIWallEdge *__fastcall cWallEdgeAdapters::RotOnextRot(Self *self, void *) { return self->Self::RotOnextRot(); }
cIWallEdge *__fastcall cWallEdgeAdapters::RotOnextRotC(Self *self, void *) { return self->Self::RotOnextRotC(); }
cIWallEdge *__fastcall cWallEdgeAdapters::SymOnextSym(Self *self, void *) { return self->Self::SymOnextSym(); }
cIWallEdge *__fastcall cWallEdgeAdapters::SymOnextSymC(Self *self, void *) { return self->Self::SymOnextSymC(); }
cIWallEdge *__fastcall cWallEdgeAdapters::InvRotOnextInvRot(Self *self, void *) { return self->Self::InvRotOnextInvRot(); }
cIWallEdge *__fastcall cWallEdgeAdapters::InvRotOnextInvRotC(Self *self, void *) { return self->Self::InvRotOnextInvRotC(); }
cIWallEdge *__fastcall cWallEdgeAdapters::InvRotOnextRot(Self *self, void *) { return self->Self::InvRotOnextRot(); }
cIWallEdge *__fastcall cWallEdgeAdapters::InvRotOnextRotC(Self *self, void *) { return self->Self::InvRotOnextRotC(); }
cIWallEdge *__fastcall cWallEdgeAdapters::OnextSym(Self *self, void *) { return self->Self::OnextSym(); }
cIWallEdge *__fastcall cWallEdgeAdapters::OnextSymC(Self *self, void *) { return self->Self::OnextSymC(); }
cIWallEdge *__fastcall cWallEdgeAdapters::RotOnextInvRot(Self *self, void *) { return self->Self::RotOnextInvRot(); }
cIWallEdge *__fastcall cWallEdgeAdapters::RotOnextInvRotC(Self *self, void *) { return self->Self::RotOnextInvRotC(); }
cIWallEdge *__fastcall cWallEdgeAdapters::SymOnext(Self *self, void *) { return self->Self::SymOnext(); }
cIWallEdge *__fastcall cWallEdgeAdapters::SymOnextC(Self *self, void *) { return self->Self::SymOnextC(); }
float __fastcall cWallEdgeAdapters::Angle(Self *self, void *) { return self->Self::Angle(); }
float __fastcall cWallEdgeAdapters::ReverseAngle(Self *self, void *) { return self->Self::ReverseAngle(); }
int32_t __fastcall cWallEdgeAdapters::AngleDegrees(Self *self, void *) { return self->Self::AngleDegrees(); }
int32_t __fastcall cWallEdgeAdapters::ReverseAngleDegrees(Self *self, void *) { return self->Self::ReverseAngleDegrees(); }
cIWallVertex *__fastcall cWallEdgeAdapters::DestVertex(Self *self, void *) { return self->Self::DestVertex(); }

} // namespace nTSWorld
