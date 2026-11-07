// Differential test for SimAntics opcode 0x73, Debug (0x586560, PrimXObject::Prim73). Framework: tests/simantics_prims_x.h.
//
// The cached singleton, the debug console (cached service 0xb38f10) and the log service (found through the service
// object, game global 0x10c9378) are fakes pointed into the world through their game globals; the string-table loader
// 0x1d0df0 is redirected to a stand-in building the same 0x34-byte object from a scripted text on both sides. The
// console and log fakes log the strings they receive by content, so the commands the handler builds are compared.
#include "simantics_prims_x.h"

namespace {

// Console slot 7: one command line.
uint32_t __fastcall Console7(void *self, void *, const char *line)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(7u << 8 | 1u);
    LogString(line);
    return Scripted(self, 7);
}

// Framework slot 5 (query): (id, id, void **out) -> writes the scripted object (null: not found).
uint32_t __fastcall Framework5(void *self, void *, uint32_t a, uint32_t b, void **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(5u << 8 | 3u);
    gTrace.insert(gTrace.end(), {a, b, out ? 1u : 0u});
    const uint32_t v = Scripted(self, 5);
    if (out) *out = reinterpret_cast<void *>(static_cast<uintptr_t>(v));
    return 1;
}

// Log service slot 12: (name, void **out) -> al; may hand back a reference in *out.
uint32_t __fastcall Log12(void *self, void *, const char *name, void **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(12u << 8 | 2u);
    LogString(name);
    gTrace.push_back(out ? 1u : 0u);
    gTrace.push_back(*out ? 0xbad0u : 0u); // the holder was reset before the call
    const uint32_t ref = Scripted(self, 0x1c0);
    if (out) *out = reinterpret_cast<void *>(static_cast<uintptr_t>(ref));
    return Scripted(self, 12);
}

// Log service slot 9: (group, name, number).
uint32_t __fastcall Log9(void *self, void *, const char *a, const char *b, uint32_t n)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(9u << 8 | 3u);
    LogString(a);
    LogString(b);
    gTrace.push_back(n);
    return Scripted(self, 9);
}

const char *StrPick(std::mt19937 &r)
{
    return kNames[r() % (sizeof(kNames) / sizeof(kNames[0]))];
}

struct Globals {
    void *singleton, *console, *service;
};
Globals gSaved;

void Build73(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 731);
    Base(w, seed, r);
    SetParam(w, r);
    Fake &ctx = w.fk[0], &frame = w.fk[1], &singleton = w.fk[2], &settings = w.fk[3], &people = w.fk[4], &lists = w.fk[5], &person = w.fk[6],
         &named = w.fk[7], &owner = w.fk[8], &lang = w.fk[9], &group18 = w.fk[10], &entry = w.fk[11], &found = w.fk[12];
    Fake &console = w.sub[0], &framework = w.sub[1], &log = w.sub[2], &held = w.sub[3];

    uint8_t *obj = reinterpret_cast<uint8_t *>(&w.obj);
    const uint32_t driven = (r() % 6 == 0) ? 0u : 1u;
    std::memcpy(obj + 0x44, &driven, 4);

    // The parameter bytes: operand ids, the sub-command (0..9: 3 and 8, 9 do nothing), the language flags.
    w.param[0x11] = static_cast<uint8_t>(Pick(r, {0, 0, 1, 1, 2, 4, 5, 5, 6, 6, 7, 7, 3, 8, 255}));
    w.param[0x12] = static_cast<uint8_t>(r());
    gReadVals.clear();
    gReadPos = 0;
    for (int i = 0; i < 4; ++i) gReadVals.push_back(ReadWordX(r));
    w.vtSec[7] = reinterpret_cast<void *>(&ReadSeq);
    gLoaderText = kFormats[r() % (sizeof(kFormats) / sizeof(kFormats[0]))];

    // Singleton (slot 31), context holder, settings.
    Use(singleton.vt, 31, 0);
    RetValue(&singleton, 31, Pick(r, {1, 1, 1, 1, 0, 0x100, 0xff, 0x101}), w);
    Use(ctx.vt, 3, 0);
    RetObject(&ctx, 3, &settings, w);
    Use(ctx.vt, 24, 0);
    RetObject(&ctx, 24, &lists, w);
    Use(ctx.vt, 26, 0);
    RetObject(&ctx, 26, &people, w);
    Use(settings.vt, 15, 1);
    RetValue(&settings, 15, (r() % 4 == 0) ? (static_cast<uint32_t>(r()) & ~8u) : (static_cast<uint32_t>(r()) | 8u), w);

    // This object: slot 61 the named object, slot 47 its id. The person list: slot 29.
    Use(w.vtThis, 61, 0);
    RetObject(&w.obj, 61, &named, w);
    Use(w.vtThis, 47, 0);
    Use(named.vt, 8, 0);
    RetValue(&named, 8, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(StrPick(r))), w);
    Use(frame.vt, 12, 0);
    RetValue(&frame, 12, static_cast<uint32_t>(r()), w);
    Use(frame.vt, 20, 0);
    RetObject(&frame, 20, &owner, w);
    Use(owner.vt, 38, 0);
    Use(people.vt, 29, 1);
    const bool havePerson = r() % 5 != 0;
    // Sub-commands 2 and 4 use the person without a check (ORIGINAL-BUG): the world always gives one for them.
    if (!havePerson) {
        if (w.param[0x11] == 2 || w.param[0x11] == 4) w.param[0x11] = 5;
    }
    RetObject(&people, 29, havePerson ? &person : nullptr, w);
    Use(person.vt, 61, 0);
    RetObject(&person, 61, &named, w);
    Use(person.vt, 47, 0);
    Use(person.vt, 25, 1);
    Use(person.vt, 26, 2);
    Use(person.vt, 27, 1);
    Use(person.vt, 28, 0);
    RetValue(&person, 25, static_cast<uint32_t>(r()), w);
    RetValue(&person, 28, Pick(r, {0, 0x4000, 0x8000, 0xc000, 0xffffffffu}), w);

    // The language lists: 14 (language id) the entry owner, 18 the global group object, 19 (id) another.
    Use(lists.vt, 14, 1);
    Use(lists.vt, 18, 0);
    Use(lists.vt, 19, 1);
    const bool haveLang = r() % 5 != 0;
    // Sub-command 6 with only flag 0 of the language byte uses the entry owner without a check: always present then.
    if (!haveLang && w.param[0x11] == 6 && (w.param[0x12] & 3) == 1) w.param[0x12] |= 2;
    RetObject(&lists, 14, haveLang ? &lang : nullptr, w);
    RetObject(&lists, 18, (r() % 5 == 0) ? nullptr : &group18, w);
    RetObject(&lists, 19, (r() % 5 == 0) ? nullptr : &found, w);
    Use(group18.vt, 67, 0);
    RetObject(&group18, 67, (r() % 4 == 0) ? nullptr : &entry, w);
    Use(found.vt, 67, 0);
    RetObject(&found, 67, (r() % 4 == 0) ? nullptr : &entry, w);
    Use(lang.vt, 70, 0);
    Use(lang.vt, 18, 0);
    Use(lang.vt, 73, 0);
    RetObject(&lang, 70, (r() % 4 == 0) ? nullptr : &entry, w);
    RetObject(&lang, 18, (r() % 4 == 0) ? nullptr : &entry, w);
    Use(entry.vt, 11, 0);

    // The console (cached service) and the log service.
    console.vt = w.vtSub[0];
    w.vtSub[0][7] = reinterpret_cast<void *>(&Console7);
    framework.vt = w.vtSub[1];
    w.vtSub[1][5] = reinterpret_cast<void *>(&Framework5);
    RetObject(&framework, 5, (r() % 4 == 0) ? nullptr : &log, w);
    log.vt = w.vtSub[2];
    Use(w.vtSub[2], 2, 0);
    w.vtSub[2][12] = reinterpret_cast<void *>(&Log12);
    w.vtSub[2][9] = reinterpret_cast<void *>(&Log9);
    RetValue(&log, 12, Pick(r, {0, 0, 1, 0x100, 0xff}), w);
    RetObject(&log, 0x1c0, (r() % 3 == 0) ? &held : nullptr, w);
    held.vt = w.vtSub[3];
    Use(w.vtSub[3], 2, 0);
}

} // namespace

DIFF_TEST(PrimX_Prim73, 0x586560)
{
    gSaved = {*GameAddress<void *>(0x10cc288), *GameAddress<void *>(0x10faf68), *GameAddress<void *>(0x10c9378)};
    gBeforeRun = [](World &w) {
        *GameAddress<void *>(0x10cc288) = &w.fk[2];
        *GameAddress<void *>(0x10faf68) = &w.sub[0];
        *GameAddress<void *>(0x10c9378) = &w.sub[1];
        gReadPos = 0;
    };
    LoaderRedirect redirect(ctx);
    Stats st = RunHandler(ctx, "Prim73", 0x586560, 30000, Build73,
                          [](PrimMidObject *o, void *f, const PrimParamView *p) { return SelfX(o)->Prim73(f, p); });
    gBeforeRun = nullptr;
    *GameAddress<void *>(0x10cc288) = gSaved.singleton;
    *GameAddress<void *>(0x10faf68) = gSaved.console;
    *GameAddress<void *>(0x10c9378) = gSaved.service;
    if (st.throws < 500 || st.returns < 15000) ctx.Fail("coverage: %u throws, %u returns", st.throws, st.returns);
}
