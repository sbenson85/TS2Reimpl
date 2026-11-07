// Differential test for cTSNeighborhood::GetLotValue (0x474ac0), lane CQ. Fixture: tests/ts_neighborhood_lots_d.h.
// The original callees 0x7e2c80 (value from the size) and 0x54ca20 (the lot's business bonus) run for real on both sides; the
// second one reads the app singleton, the persist context and the tuning, so those are fakes shaped to give it a value.
// Fakes: 0 app singleton, 1 lot, 2 its base info, 3 persist system, 4 framework, 5 simulator record, 6 file reference, 7 catalog,
// 8 state controller, 9 persist context, 10 owner info, 11 service C, 12 tuning, 13 business record, 14 the app's slot-7 object,
// 15 its lot (answers the persist resource), 16 the lot's persist resource, 17 the record's int.
#include "ts_neighborhood_lots_d.h"

namespace {
using namespace svcfx;
using namespace lotsd;

cRZString gFileText;
uint32_t gBonusInt = 0;
uint32_t gScale = 0;

uint32_t Wide16b(Obj *o, uint32_t low)
{
    o->value[7] = o->value[7] * 1664525u + 1013904223u;
    return (o->value[7] & 0xffff0000u) | (low & 0xffff);
}
float __fastcall ScaleFn(Obj *self, void *)
{
    Ev(evCall); Ev(self->id); Ev(79); Ev(0);
    float f;
    std::memcpy(&f, &self->value[0], 4);
    return f;
}
uint32_t CreateFn(Obj *, uint32_t *a)   // (class, iid, out)
{
    Obj &made = F(a[0] == 0x53494d49 ? 5 : 6);
    ++made.refs;
    *reinterpret_cast<void **>(a[2]) = &made;
    return 1;
}
uint32_t Bounds(Obj *o, uint32_t *a)
{
    auto *rect = reinterpret_cast<uint32_t *>(a[0]);
    for (int i = 0; i < 4; ++i) rect[i] = o->value[i];
    return 0;
}
uint32_t FromValue0(Obj *o, uint32_t *) { return o->value[0]; }
uint32_t FromValue1(Obj *o, uint32_t *) { return o->value[1]; }
uint32_t FromValue2(Obj *o, uint32_t *) { return o->value[2]; }
uint32_t FromValue3(Obj *o, uint32_t *) { return o->value[3]; }
uint32_t FromValue4(Obj *o, uint32_t *) { return o->value[4]; }
uint32_t GlobalFn(Obj *o, uint32_t *a) { return Wide16b(o, a[0] == 0x16 ? o->value[4] : o->value[5]); }
uint32_t HasFlag(Obj *o, uint32_t *a) { return o->value[0] >> (a[0] & 7); }
uint32_t TuningFloat(Obj *o, uint32_t *a)
{
    if (o->value[1]) *reinterpret_cast<uint32_t *>(a[1]) = o->value[0];
    return o->value[1];
}
uint32_t OpenOut(Obj *, uint32_t *a)   // the business record's resource
{
    ++F(13).refs;
    *reinterpret_cast<void **>(a[2]) = &F(13);
    return 1;
}
uint32_t BonusPtr(Obj *, uint32_t *) { return P(&gBonusInt); }
} // namespace

DIFF_TEST(NeighborhoodLotsD_GetLotValue, 0x474ac0)
{
    using Fn = uint8_t(__thiscall *)(Nb *, int32_t, int32_t *, uint8_t *, uint8_t *, uint8_t);
    unsigned ok = 0;
    for (uint32_t seed = 0; seed < 600; ++seed) {
        if (Pair(ctx, "GetLotValue", seed, [&](Globals &g, Nb *nb, std::mt19937 &rng, bool original) {
                gFileText = MakeString("lot.package");
                const bool found = (seed % 9) != 0;
                F(kNbFake).Set(57, "d", found ? P(&F(1)) : 0);
                // the lot
                F(1).value[0] = int32_t(rng() % 400) - 100;
                F(1).value[1] = int32_t(rng() % 400) - 100;
                F(1).value[2] = F(1).value[0] + int32_t(rng() % 90) - (seed % 7 == 0 ? 120 : 10);
                F(1).value[3] = F(1).value[1] + int32_t(rng() % 90);
                F(1).value[4] = rng() % 5;   // lot type
                F(1).Set(4, 0, P(&F(2)));
                F(1).Set(8, 0, P(&gFileText));
                F(1).Set(13, "p", 0, Bounds);
                F(1).Set(32, 0, 0, FromValue4);
                F(2).value[0] = rng();
                F(2).Set(24, "d", 0, HasFlag);
                F(3).value[0] = (seed % 4 == 0) ? 1 : 0;   // reading the record fails
                F(3).Set(73, "ppd-", 0, FromValue0);
                gFrameworkIndex = 4;
                F(4).Set(3, "ddp", 0, CreateFn);
                F(5).value[0] = rng() % 100000 - 20000;
                F(5).value[1] = rng() % 50000;
                F(5).value[2] = rng() % 1000 - 100;
                F(5).value[3] = rng() % 5000;
                F(5).value[4] = rng() & 0xffff;
                F(5).value[5] = rng() & 0xffff;
                F(5).Set(36, 0, 0, FromValue0);   // AC
                F(5).Set(33, 0, 0, FromValue1);   // A4
                F(5).Set(38, 0, 0, FromValue2);   // A0
                F(5).Set(34, 0, 0, FromValue3);   // A8
                F(5).Set(15, "d", 0, GlobalFn);
                F(6).value[0] = (seed % 5 == 1) ? 3 : 0;   // binding fails
                F(6).Set(6, "pd", 0, FromValue0);
                const float scales[] = {0.5f, 1.0f, 1.25f, 0.0f, 3.7f, 0.333f};
                std::memcpy(&F(7).value[0], &scales[rng() % 6], 4);
                F(7).table[79] = reinterpret_cast<void *>(&ScaleFn);
                F(0).Set(16, 0, P(&F(7)));
                // 0x54ca20 finds a record (then it needs the tuning) or nothing
                F(0).Set(7, 0, ((seed % 3) && (seed % 8 != 7)) ? P(&F(14)) : 0);
                F(0).Set(10, 0);
                F(14).Set(18, "d", P(&F(15)));
                F(15).Set(25, 0, P(&F(16)));
                F(15).Set(18, 0, 7);
                F(16).Set(6, 0, 0x4000);
                F(8).Set(34, "d", P(&F(9)));
                F(9).Set(3, "pdp", 0, OpenOut);
                gBonusInt = rng() % 500;
                F(13).Set(16, 0, 0, BonusPtr);
                F(13).Set(29, "p");
                F(13).Set(30, 1);
                // the owner and the tuning
                nb->mOwner = (seed % 6 == 5) ? nullptr : &F(10);
                F(10).value[0] = (seed % 4 == 3) ? 1 : 2;   // the owner's kind
                F(10).Set(18, 0, 0, FromValue0);
                const float factors[] = {0.5f, 2.0f, 1.5f};
                std::memcpy(&F(12).value[0], &factors[rng() % 3], 4);
                F(12).value[1] = (seed % 5) != 0;           // GetFloat answers
                F(12).Set(5, "dp", 0, TuningFloat);
                F(11).Set(64, 0, (seed % 8 == 7) ? 0 : P(&F(12)));
                g.Set(kAppCell, &F(0));
                g.Set(kPersistCell, &F(3));
                g.Set(kSlot23Cache, &F(8));
                g.Set(0x10cc290, &F(11));
                g.SetWord(0x10cc2d4, 0);
                int32_t value = 0x7777;
                uint8_t flagA = 0x55, flagB = 0x66;
                const int32_t number = 5 + int32_t(seed % 3);
                const uint8_t apply = (seed % 10 == 4) ? 0 : uint8_t(1 + seed % 3);
                const uint8_t r = nbshape::Call<Fn>(ctx, original, nb, 0, 0x474ac0, NB_ADAPTER(GetLotValue), number, &value, &flagA, &flagB, apply);
                Ev(uint32_t(value));
                Ev(flagA);
                Ev(flagB);
                return uint32_t(r);
            }))
            ++ok;
    }
    std::printf("  GetLotValue: %u worlds ok\n", ok);
}
