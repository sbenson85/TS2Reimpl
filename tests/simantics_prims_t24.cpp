// Differential test for the SimAntics primitive handler of src/simantics/PrimT.h: opcode 0x24, Dialog (0x5879a0).
// Framework: tests/simantics_prims_mid.h, tests/simantics_prims_r.h (queue-driven fakes), tests/simantics_prims_s_text.h
// (the string-table fixture: the loaders 0x1d0e90 / 0x1d0df0 run ours against the original, the number parser 0x1b0e2c
// is original code that runs on both sides), tests/simantics_prims_t.h.
//
// Fakes (several roles share an object where their slots don't collide; the roles are named in Build24):
//   fk0 context, fk1 frame (also this's owner), fk2 text-group list, fk3 object table, fk4 owner / misc object,
//   fk5 locale, fk6 person, fk7 stack object, fk8 state service (0x211da3's cache), fk9 registry (0xb39020) /
//   pending entry, fk10 string service and fk11 provider (the fixture), fk12 dialog service (0x20e031's cache),
//   fk13 the service's dialog list, sub0 UI / popup holder, sub1 text file object, sub2 locale group object,
//   sub3 the context's slot-10 object.
// Text arguments are logged by content (the strings are stack objects).
#include "simantics/PrimT.h"
#include "simantics_prims_s_text.h"


#include "rz/cRZString.h"

namespace {

cRZString gCopySource;
bool gNoTipService = false; // this run's game tip service getter finds none (Build24)

PrimTObject *SelfT(PrimMidObject *o)
{
    return reinterpret_cast<PrimTObject *>(o);
}

void LogText(const void *p)
{
    const std::string &s = static_cast<const cRZString *>(p)->mString;
    gTrace.push_back(static_cast<uint32_t>(s.size()));
    for (char c : s) gTrace.push_back(static_cast<uint8_t>(c));
}

// Stack object slot 262: (text*, 0, frame, 0, extra*); logs the text, answers the extra from the script.
uint32_t __fastcall Slot262(void *self, void *, const void *text, uint32_t a2, void *frame, uint32_t a4, void **extra)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(262u << 8 | 5u);
    LogText(text);
    gTrace.insert(gTrace.end(), {a2, Norm(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(frame))), a4, extra ? 1u : 0u});
    if (extra) *extra = reinterpret_cast<void *>(static_cast<uintptr_t>(Scripted(self, 262)));
    return 0;
}

// Dialog list slot 3: (kind, text*, reference, id) -> handle (ax).
uint32_t __fastcall Dlg3(void *self, void *, uint32_t kind, const void *text, uint32_t reference, uint32_t id)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(3u << 8 | 4u);
    gTrace.push_back(kind);
    LogText(text);
    gTrace.insert(gTrace.end(), {Norm(reference), id});
    return Scripted(self, 3);
}

// Dialog list slot 4: (handle, text*, reference, id, flag) -> al.
uint32_t __fastcall Dlg4(void *self, void *, uint32_t handle, const void *text, uint32_t reference, uint32_t id, uint32_t flag)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(4u << 8 | 5u);
    gTrace.push_back(handle);
    LogText(text);
    gTrace.insert(gTrace.end(), {Norm(reference), id, flag});
    return Scripted(self, 4);
}

// UI slot 16: (text*, text*, a3, a4, a5, a6).
uint32_t __fastcall Ui16(void *self, void *, const void *t1, const void *t2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(16u << 8 | 6u);
    LogText(t1);
    LogText(t2);
    gTrace.insert(gTrace.end(), {Norm(a3), a4, a5, a6});
    return Scripted(self, 16);
}

// Registry slot 4: (word, holder*): hands out the holder object (or nothing) and answers al.
uint32_t __fastcall Reg4(void *self, void *, uint32_t word, void **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(4u << 8 | 2u);
    gTrace.insert(gTrace.end(), {word, out ? 1u : 0u});
    if (out) *out = reinterpret_cast<void *>(static_cast<uintptr_t>(Scripted(self, 1004)));
    return Scripted(self, 4);
}

// Holder slots 10 and 11: (string*): fill the string.
template <int S> uint32_t __fastcall HoldFill(void *self, void *, void *text)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(static_cast<uint32_t>(S) << 8 | 1u);
    static_cast<cRZString *>(text)->mString = S == 10 ? "holder name" : "holder text";
    return 0;
}

// Frame slot 26 (this's owner): (string*) logged by content.
uint32_t __fastcall Owner26(void *self, void *, const void *text)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(26u << 8 | 1u);
    LogText(text);
    return 0;
}

void Build24(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 24);
    Base(w, seed, r);
    SetParam(w, r);

    BuildStrings(w, r, 3);
    gNullUi = false;
    gManagerOn = false;
    Fake &ctx = w.fk[0], &frame = w.fk[1], &list = w.fk[2], &table = w.fk[3], &owner = w.fk[4], &locale = w.fk[5], &person = w.fk[6],
         &stackObj = w.fk[7], &stateSvc = w.fk[8], &registry = w.fk[9], &dialogSvc = w.fk[12], &dlgList = w.fk[13];
    Fake &ui = w.sub[0], &file = w.sub[1], &group = w.sub[2], &ctx10 = w.sub[3];

    // The parameter.
    static const uint8_t ops[] = {9, 8, 8, 10, 10, 0x1c, 0x1c, 0x39, 0x16, 0x19, 0x12, 5, 6, 7, 0xf, 0x21, 0xb, 0xc, 0xd, 0x33, 0x34, 0x46, 2, 3, 4, 0x20};
    w.param[9] = (r() % 40 == 0) ? static_cast<uint8_t>(r()) : ops[r() % sizeof(ops)];
    if (r() % 3 != 0) w.param[0xb] = static_cast<uint8_t>((r() & 0xfe) | (r() % 6 == 0));
    w.param[0x10] = static_cast<uint8_t>(Pick(r, {0, 1, 2, 3, 4, 4, 5, 5, 6, 255}));

    // The context.
    Use(ctx.vt, 26, 0);
    RetObject(&ctx, 26, &table, w);
    Use(ctx.vt, 24, 0);
    RetObject(&ctx, 24, &list, w);
    Use(ctx.vt, 3, 0);
    Use(ctx.vt, 10, 0);
    RetObject(&ctx, 10, &ctx10, w);

    // The frame (also this's owner, slot 26).
    Use(frame.vt, 18, 0);
    RetValue(&frame, 18, (r() % 2) ? static_cast<uint32_t>(r()) | 1u : 0u, w);
    Use(frame.vt, 19, 1);
    Use(frame.vt, 12, 0);
    Use(frame.vt, 3, 0);
    RetValue(&frame, 12, ReadWord(r), w);
    RetValue(&frame, 3, ReadWord(r), w);
    Use(frame.vt, 20, 0);
    RetObject(&frame, 20, &owner, w);
    w.obj.mOwner = &owner;
    owner.vt[26] = reinterpret_cast<void *>(&Owner26);

    // This.
    Use(w.vtThis, 19, 1);
    RetValue(&w.obj, 19, ReadWord(r), w);
    Use(w.vtThis, 20, 2);

    // The object table: slot 76 the pending entry (registry), 29 the person / stack object, 78 the runner.
    Use(table.vt, 76, 1);
    RetObject(&table, 76, (r() % 3 == 0) ? nullptr : &registry, w);
    table.vt[29] = reinterpret_cast<void *>(&Lookup29);
    {
        const bool noPerson = r() % 12 == 0, noStack = r() % 4 == 0;
        // The messages (0x16, 0x19) use the stack object without a check, so one of the two must exist there.
        const bool needObject = w.param[9] == 0x16 || w.param[9] == 0x19;
        LookupObj((noPerson && !(needObject && noStack)) ? nullptr : &person, w);
        LookupObj(noStack ? nullptr : &stackObj, w);
    }
    Use(table.vt, 78, 4);

    // The pending entry.
    Use(registry.vt, 7, 0);
    RetValue(&registry, 7, static_cast<uint32_t>(r() % 7), w);
    Use(registry.vt, 9, 0);
    Use(registry.vt, 10, 0);
    Use(registry.vt, 11, 0);
    RetValue(&registry, 11, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gCopySource)), w);
    registry.vt[4] = reinterpret_cast<void *>(&Reg4);
    // (a "yes" always comes with the holder object: the original uses it without a check)
    const bool found = r() % 4 != 0;
    RetValue(&registry, 4, found ? static_cast<uint32_t>(r()) | 1u : static_cast<uint32_t>(r()) & ~0xffu, w);
    RetObject(&registry, 1004, (!found && r() % 2) ? nullptr : &ui, w);

    // Text lookup chain: list slot 14 (language) -> locale, locale slots 18 / 70, group slot 16, list slot 6.
    Use(owner.vt, 38, 0);
    RetValue(&owner, 38, static_cast<uint32_t>(r()), w);
    Use(list.vt, 14, 1);
    RetObject(&list, 14, &locale, w);
    Use(list.vt, 6, 0);
    RetObject(&list, 6, (r() % 4 == 0) ? nullptr : &file, w);
    Use(locale.vt, 18, 0);
    RetObject(&locale, 18, &group, w);
    Use(locale.vt, 70, 0);
    RetObject(&locale, 70, (r() % 4 == 0) ? nullptr : &file, w);
    Use(w.vtSub[2], 16, 0);
    RetObject(&group, 16, (r() % 4 == 0) ? nullptr : &file, w);
    Use(w.vtSub[1], 11, 0);
    RetValue(&file, 11, (r() % 2) ? static_cast<uint32_t>(r()) : Pick(r, {0, 1, 0x7fd46cd0u, 0x2026960bu}), w);

    // Person and stack object.
    Use(person.vt, 61, 0);
    RetObject(&person, 61, &owner, w);
    Use(owner.vt, 27, 0);
    Use(owner.vt, 10, 0);
    RetValue(&owner, 27, (r() % 3 == 0) ? 0u : static_cast<uint32_t>(r()), w);
    RetValue(&owner, 10, static_cast<uint32_t>(r()), w);
    Use(person.vt, 47, 0);
    RetValue(&person, 47, ReadWord(r), w);
    Use(person.vt, 266, 0);
    RetValue(&person, 266, static_cast<uint32_t>(Pick(r, {0, 1, 2, 0xffff, 0x8000})) | (r() & 0xffff0000u), w);
    // (the stack object falls back to the person: both answer slots 19 and 262)
    for (Fake *f : {&stackObj, &person}) {
        Use(f->vt, 19, 1);
        RetValue(f, 19, ReadWord(r), w);
        f->vt[262] = reinterpret_cast<void *>(&Slot262);
        if (r() % 3 == 0) RetObject(f, 262, &owner, w);
        else RetValue(f, 262, (r() % 2) ? static_cast<uint32_t>(r()) : 0u, w);
    }

    // The context's slot-10 object.
    Use(w.vtSub[3], 16, 2);
    RetObject(&ctx10, 16, (r() % 5 == 0) ? nullptr : &owner, w);

    // The services.
    Use(stateSvc.vt, 55, 0);
    Use(stateSvc.vt, 56, 0);
    RetValue(&stateSvc, 55, (r() % 6 == 0) ? static_cast<uint32_t>(r()) | 1u : static_cast<uint32_t>(r()) & ~0xffu, w);
    RetValue(&stateSvc, 56, (r() % 6 == 0) ? static_cast<uint32_t>(r()) | 1u : static_cast<uint32_t>(r()) & ~0xffu, w);
    Use(dialogSvc.vt, 3, 0);
    RetObject(&dialogSvc, 3, (r() % 8 == 0) ? nullptr : &dlgList, w);
    Use(dialogSvc.vt, 12, 0);
    RetObject(&dialogSvc, 12, (r() % 6 == 0) ? nullptr : &ui, w);
    dlgList.vt[3] = reinterpret_cast<void *>(&Dlg3);
    RetValue(&dlgList, 3, (r() % 5 == 0) ? 0xffffu : (ReadWord(r) & 0xffffu) | (r() & 0xffff0000u), w);
    dlgList.vt[4] = reinterpret_cast<void *>(&Dlg4);
    RetValue(&dlgList, 4, (r() % 4 == 0) ? static_cast<uint32_t>(r()) & ~0xffu : static_cast<uint32_t>(r()) | 1u, w);
    Use(dlgList.vt, 6, 2);
    Use(dlgList.vt, 7, 2);
    Use(dlgList.vt, 8, 1);
    RetValue(&dlgList, 8, (r() % 3 == 0) ? static_cast<uint32_t>(r()) & ~0xffu : static_cast<uint32_t>(r()) | 1u, w);

    // The UI object and the popup holder share an object.
    ui.vt[16] = reinterpret_cast<void *>(&Ui16);
    Use(w.vtSub[0], 17, 0);
    Use(w.vtSub[0], 8, 0);
    RetValue(&ui, 8, static_cast<uint32_t>(r()), w);
    w.vtSub[0][10] = reinterpret_cast<void *>(&HoldFill<10>);
    w.vtSub[0][11] = reinterpret_cast<void *>(&HoldFill<11>);
    Use(w.vtSub[0], 2, 0);

    // Some runs have no game tip service (its cache 0x10faf8c empty and no framework at 0x10c9378, so the getter
    // 0xb39020 finds none): opcode 0x39 must then return 1 before asking for the tip.
    gNoTipService = r() % 8 == 0;
}

} // namespace

DIFF_TEST(PrimT_Prim24, 0x5879a0)
{
    TextFixture fixture(ctx);
    gCopySource.Construct("copy source text");
    void *const savedRegistry = *ctx.image.At<void *>(0x10faf8c);
    void *const savedFramework = *ctx.image.At<void *>(0x10c9378);
    gBeforeRun = [&](World &w) {
        SetTextGlobals(w);

        *ctx.image.At<void *>(0x10cc458) = &w.fk[8];
        *ctx.image.At<void *>(0x10cc2a0) = &w.fk[12];
        *ctx.image.At<void *>(0x10faf8c) = gNoTipService ? nullptr : &w.fk[9];
        *ctx.image.At<void *>(0x10c9378) = gNoTipService ? nullptr : savedFramework;
    };
    Stats st = RunHandler(ctx, "Prim24", 0x5879a0, 80000, Build24,
                          [](PrimMidObject *o, void *f, const PrimParamView *p) { return SelfT(o)->Prim24(f, p); });
    gBeforeRun = nullptr;
    *ctx.image.At<void *>(0x10faf8c) = savedRegistry;
    *ctx.image.At<void *>(0x10c9378) = savedFramework;
    gCopySource.Destruct();
    if (st.throws < 1000 || st.byRet[0] < 1000 || st.byRet[1] < 5000 || st.byRet[2] < 3000)
        ctx.Fail("coverage: %u throws, returns 0/1/2: %u/%u/%u", st.throws, st.byRet[0], st.byRet[1], st.byRet[2]);
}
