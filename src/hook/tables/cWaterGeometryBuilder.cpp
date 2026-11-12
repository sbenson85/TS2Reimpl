// nTSSG::cWaterGeometryBuilder (lane 261108f, src/sg/cWaterGeometryBuilder.h): the constructor and the methods cTerrain
// calls, each through an adapter (the class has a virtual base, so MemberAddress can't take its members); the rest is in
// effect through these and our vtables.
#include "hook/hooks.h"
#include "sg/cWaterGeometryBuilder.h"

#include <new>

namespace {

using nTSSG::cWaterGeometryBuilder;
using Coord = cWaterGeometryBuilder::TriangleCoord;

// 0x3b5970 (ret 4): cTerrain's only call passes mostDerived 1 (the class is final), so the virtual base is always built.
cWaterGeometryBuilder *__fastcall Construct(cWaterGeometryBuilder *self, void *, uint32_t)
{
    return ::new (self) cWaterGeometryBuilder;
}
void __fastcall RemoveTriangle(cWaterGeometryBuilder *self, void *, const Coord *coord) { self->RemoveTriangle(coord); }
void __fastcall Setup(cWaterGeometryBuilder *self, void *, uint32_t width, uint32_t height, void *terrain)
{
    self->Setup(width, height, terrain);
}
bool __fastcall IsTriangleDry(cWaterGeometryBuilder *self, void *, Coord coord) { return self->IsTriangleDry(coord); }
void __fastcall GrowDirtyRect(cWaterGeometryBuilder *self, void *, int32_t x, int32_t y) { self->GrowDirtyRect(x, y); }
void __fastcall ClearSubsetsChanged(cWaterGeometryBuilder *self, void *) { self->ClearSubsetsChanged(); }
void __fastcall Update(cWaterGeometryBuilder *self, void *) { self->Update(); }
float __fastcall GetVertexHeight(cWaterGeometryBuilder *self, void *, uint32_t x, uint32_t y)
{
    return self->GetVertexHeight(x, y);
}
void __fastcall SetTriangleSubset(cWaterGeometryBuilder *self, void *, const Coord *coord, const char *name)
{
    self->SetTriangleSubset(coord, name);
}
void __fastcall SetVertexHeight(cWaterGeometryBuilder *self, void *, uint32_t x, uint32_t y, float z)
{
    self->SetVertexHeight(x, y, z);
}

const HookSpec kHooks[] = {
    {"nTSSG_cWaterGeometryBuilder_cWaterGeometryBuilder", 0x3b5970, 32, 0x3a8036a7, reinterpret_cast<void *>(&Construct)},
    {"nTSSG_cWaterGeometryBuilder_RemoveTriangle", 0x3b6510, 32, 0x41f9a7a5, reinterpret_cast<void *>(&RemoveTriangle)},
    {"nTSSG_cWaterGeometryBuilder_Setup", 0x3b6c50, 32, 0x455434b1, reinterpret_cast<void *>(&Setup)},
    {"nTSSG_cWaterGeometryBuilder_IsTriangleDry", 0x3b7080, 32, 0x24cbfac1, reinterpret_cast<void *>(&IsTriangleDry)},
    {"nTSSG_cWaterGeometryBuilder_GrowDirtyRect", 0x3b7110, 32, 0x54aca7d5, reinterpret_cast<void *>(&GrowDirtyRect)},
    {"nTSSG_cWaterGeometryBuilder_ClearSubsetsChanged", 0x3b7190, 8, 0x9c953736,
     reinterpret_cast<void *>(&ClearSubsetsChanged)},
    {"nTSSG_cWaterGeometryBuilder_Update", 0x3b7850, 32, 0x95e7c065, reinterpret_cast<void *>(&Update)},
    {"nTSSG_cWaterGeometryBuilder_GetVertexHeight", 0x3b8e10, 31, 0xc3408212, reinterpret_cast<void *>(&GetVertexHeight)},
    {"nTSSG_cWaterGeometryBuilder_SetTriangleSubset", 0x3b9060, 32, 0x0f0d4d7b,
     reinterpret_cast<void *>(&SetTriangleSubset)},
    {"nTSSG_cWaterGeometryBuilder_SetVertexHeight", 0x3b9140, 32, 0x159c4017, reinterpret_cast<void *>(&SetVertexHeight)},
};
REGISTER_HOOKS(kHooks);

} // namespace
