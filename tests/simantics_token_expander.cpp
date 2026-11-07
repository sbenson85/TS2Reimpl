// Differential test for cTSObject::vf262 (0x535130), the string-token expansion (src/simantics/TokenExpander*.cpp), as a whole
// function: the original (the mapped image) and ours run on identical recording-fake worlds (tests/simantics_gsc_world.h).
//
// The object under test is the world's "Self"; its context, the object table, the frame, the people, entry and definition tables,
// the services the stages reach through the game's cached getters (language, name, want/fear and group-name services, the
// neighborhood service) and the objects they hand out are fakes that log every call (object, slot, arguments) and answer from
// the world's seed. The string-table loader runs for real on both sides, against a fake resource manager (tests/ts_string_fake.h) that logs what it is
// asked and answers from the seed; the property formatter (0x5307b0) is original code that needs the resource system, redirected
// to a logging stand-in in the image, so both sides reach the same one.
// Each seed builds a text of random markers (random case, ":n" and ":n:m" parameters, filler) and random options; both sides must
// leave the same text, out pair and out pointer, make the same calls in the same order, and leave the fakes with the same
// reference counts. A fake that throws (the world's error injection) exercises the unwinding of the held references.
//
// A few stages crash in the original when their lookups fail (no frame for $Attrib, a missing object record for $CatalogLocal,
// ...: ORIGINAL-BUG comments in src/simantics/TokenExpander*.cpp). The worlds avoid those markers unless the seed asks for the
// crash, and then both sides must crash at the same point with the same log. $TokenNameLocal, which always ends in an error or a
// crash, is only put into some seeds.
#include "simantics_gsc_world.h"

#include "simantics/TokenExpander.h"
#include "ts/cTSString.h"
#include "ts_string_fake.h"

#include <string>

using namespace gscw;

namespace {

// ---- the world's options for one seed -------------------------------------------------------------------------------------------

struct Options {
    bool attribSafe = false;          // the frame's stack object exists: $Attrib and $NameAttrib may appear
    bool objectInfoMissing = false;   // objects may lack a record (then no $CatalogLocal / $JobTitle)
    bool languageMissing = false;     // the language service has no language (then no $TimeLocal)
    bool wantElementsMissing = false; // the want lists hold null elements (then only $WantDescriptionLocal)
    bool tokenNameLocal = false;      // $TokenNameLocal appears
    bool crashy = false;              // the seed asks for every marker that can crash the original (no frame, missing records ...)
    bool hasFrame = true;
    bool friendlyLocals = false;      // the frame's locals are small ids
    int localCount = 4;
    int argCount = 3;
};
Options gOptions;
uint32_t gSeed;

constexpr int kObjects = 5;

TokenObjectTableOwner gOwner;
void *gObjectEntries[8];
tokens::ElementList gLists[2][4]; // [fear][category & 3]
void *gElements[2][4][8];

// Texts the names may come back as: now and then one holds a marker a later stage expands.
const char *const kNested[] = {"$GlobalString:1:2", "$Money1", "Mr. $Cash0", "$GlobalString:2:0 x", "[$Local1]"};

const char *PickText(Call &c, uint32_t salt)
{
    const uint32_t r = c.Rand(salt);
    if (r % 7 == 0) return kNested[(r >> 8) % (sizeof(kNested) / sizeof(kNested[0]))];
    return kTexts[(r >> 4) % kTextCount];
}

// A slot that writes a text into its argument `arg` (a cRZString *) and returns `junk`.
std::function<uint32_t(Call &)> FillText(int arg, uint32_t salt)
{
    return [arg, salt](Call &c) -> uint32_t {
        std::string &text = reinterpret_cast<cRZString *>(static_cast<uintptr_t>(c.a[arg]))->mString;
        text = PickText(c, salt + static_cast<uint32_t>(arg));
        if (c.Rand(salt + 99) % 17 == 0) text.assign("ab\0cd", 5); // a NUL inside: the stages that replace by size or by strlen differ
        return Mix(c.Rand(salt), 9, 9);
    };
}

// Answers true for `percent` per cent of the (object, slot, first argument) keys, with junk in the other bytes.
uint32_t BoolAnswer(Call &c, uint32_t salt, uint32_t percentTrue)
{
    const uint32_t r = c.Rand(salt);
    return (Mix(r, 4, 5) & 0xffffff00u) | ((r >> 8) % 100 < percentTrue ? 1u : 0u);
}

void *PickFake(const char *type, uint32_t r)
{
    const int t = TypeIndex(type);
    return &gWorld->fk[gFirst[t] + static_cast<int>((r >> 8) % gTypes[t].count)];
}

// A slot answering an instance of `type`, or nothing when `missing` says so.
Slot Answer(int slot, const char *args, const char *type, std::function<bool(Call &)> missing)
{
    const std::string typeName = type;
    return S(slot, args, std::string("o:") + type + "?", "", [typeName, missing](Call &c) -> uint32_t {
        if (missing(c)) return 0;
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(PickFake(typeName.c_str(), c.Rand(c.desc->argWords > 0 ? NormWord(c.a[0]) : 0u))));
    });
}

// The token sets' accessors: fill the caller's scratch cell with a pointer to a holder (the first word of this set's scratch area
// holds the token) and return the cell.
uint32_t HandOutToken(Call &c, uint32_t salt)
{
    void **cell = reinterpret_cast<void **>(static_cast<uintptr_t>(c.a[0]));
    void **holder = reinterpret_cast<void **>(c.self->data);
    holder[0] = PickFake("TokenInst", c.Rand(NormWord(c.a[1]) + salt));
    *cell = holder;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(cell));
}

const char *const kIdPool = "h{0,1,2,3,4,5,6,7,8,9,10,-1,-2,23,24,100,0x7fff,-0x8000}";
const char *const kGroups = "i{0x7f96c284,0x7fd46cd0,0,5,0x12345678}";

std::vector<TypeSpec> Types()
{
    std::vector<TypeSpec> t;
    auto add = [&](const char *name, int count, std::vector<Slot> slots) {
        TypeSpec x;
        x.name = name;
        x.count = count;
        x.slots = std::move(slots);
        t.push_back(std::move(x));
    };
    // The object under test.
    add("Self", 1, {S(0, "Vp", "v", "", [](Call &c) -> uint32_t {
                        const bool succeed = gOptions.tokenNameLocal ? gSeed % 2 == 1 : c.Rand(8) % 3 != 0;
                        if (succeed) {
                            Fake *ui = static_cast<Fake *>(PickFake("Ui", c.Rand(7)));
                            *reinterpret_cast<void **>(static_cast<uintptr_t>(c.a[1])) = ui;
                            ++ui->refs;
                        }
                        return (Mix(c.Rand(9), 4, 5) & 0xffffff00u) | (succeed ? 1u : 0u);
                    }), Answer(61, "", "ObjInfo", [](Call &c) { return gOptions.objectInfoMissing && c.Rand(5) % 2 == 0; })});
    add("Ui", 2, {S(57, "", "h{0,1,2,3,5,0x1234,-1}")});
    add("Frame", 1,
        {S(12, "", "h", "", [](Call &c) -> uint32_t {
             static const int16_t safe[] = {1, 2, 3, 4, 5};
             static const int16_t any[] = {1, 2, 3, 4, 5, 0, -1, 6, 7, 0x100};
             const uint32_t r = c.Rand(3);
             const int16_t id = gOptions.attribSafe ? safe[r % 5] : any[r % 10];
             return (Mix(r, 1, 2) & 0xffff0000u) | static_cast<uint16_t>(id);
         }),
         S(26, "", "i", "", [](Call &c) -> uint32_t { return (Mix(c.Rand(1), 6, 7) & 0xffffff00u) | static_cast<uint32_t>(gOptions.argCount); }),
         S(27, "", "i", "", [](Call &c) -> uint32_t { return (Mix(c.Rand(2), 6, 7) & 0xffffff00u) | static_cast<uint32_t>(gOptions.localCount); }),
         S(30, "V", kIdPool), S(31, "V", "h", "", [](Call &c) -> uint32_t {
             // locals: small ids that mostly name existing things in the friendly worlds, else the wide pool
             static const int16_t friendly[] = {0, 1, 2, 3, 4, 5, 1, 2};
             static const int16_t wide[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, -1, -2, 23, 24, 100, 0x7fff, -0x7fff - 1};
             const uint32_t r = c.Rand(NormWord(c.a[0]));
             const int16_t v = gOptions.friendlyLocals ? friendly[(r >> 8) % 8] : wide[(r >> 8) % 18];
             return (Mix(r, 1, 2) & 0xffff0000u) | static_cast<uint16_t>(v);
         })});
    // The context and its tables.
    add("Context", 1, {S(6, "", "o:LookupList!"), S(8, "", "o:HoodTable!"), S(9, "", "o:Entries!"), S(10, "", "o:People!"), S(24, "", "o:Defs!")});
    add("LookupList", 1, {S(13, "", "o:LotList!"), Lookup(38, "V", "TokenSet", 25), Lookup(39, "V", "TokenSet", 25)});
    add("TokenSet", 2,
        {S(21, "pV", "p", "", [](Call &c) -> uint32_t { return HandOutToken(c, 0); }), S(39, "pV", "p", "", [](Call &c) -> uint32_t { return HandOutToken(c, 77); })});
    add("TokenInst", 3, {S(6, "", kGroups)});
    add("Defs", 1, {Lookup(14, "V", "DefRec", 30)});
    add("DefRec", 3,
        {S(49, "t", "b", "", [](Call &c) -> uint32_t {
             FillText(0, 11)(c);
             return BoolAnswer(c, 12, 70);
         }),
         S(53, "t", "v", "", FillText(0, 13))});
    add("People", 1, {Lookup(16, "VV", "Person", 35)});
    add("Person", 3, {Lookup(10, "", "PersonInfo", 20), S(16, "V", "h{0,1,2,3,4,5,-1,9}")});
    add("PersonInfo", 3, {S(49, "t", "v", "", FillText(0, 21)), S(54, "", "o:NameSrc!")});
    add("NameSrc", 1, {S(7, "t", "v", "", FillText(0, 22))});
    add("Entries", 1, {Lookup(11, "V", "FamEntry", 30)});
    add("FamEntry", 3, {S(37, "", "o:CStr!"), S(41, "", "h"), S(42, "", "i{0,1,2,3,0x10001,-1}"), S(60, "", "i{0,1,100,-5,123456,0x7fffffff,-2147483647,99999}")});
    add("CStr", 2, {S(5, "", "c")});
    add("HoodTable", 1, {Lookup(9, "V", "Hood", 30), Lookup(10, "V", "Hood", 30)});
    add("Hood", 2, {S(4, "", "o:CStr!")});
    add("Svc23", 1, {Lookup(6, "", "CurHood", 25)});
    add("CurHood", 1, {S(8, "", "o:KeyHolder!")});
    add("KeyHolder", 1, {S(5, "", "i{0,1,2,0x1111}")});
    add("LotList", 1, {Lookup(18, "V", "Lot", 30)});
    add("Lot", 2, {S(19, "", "o:CStr!")});
    // The objects of the object table.
    add("Obj", kObjects,
        {S(0, "VO", "b", "Ui"), S(15, "V", "h{0,1,2,3,4,5,6,-1,-5,7,12,100,0x7fff,-0x8000,10000}"), S(21, "", "h{0,1,2,3,4,5,8,-1,9}"),
         Answer(61, "", "ObjInfo", [](Call &c) { return gOptions.objectInfoMissing && c.Rand(5) % 2 == 0; }),
         S(280, "", "p", "", [](Call &c) -> uint32_t { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(c.self)); }),
         S(299, "tV", "v", "", FillText(0, 31))});
    add("ObjInfo", 3,
        {S(18, "", "o:StrOwner!"), S(27, "", "p", "", [](Call &c) -> uint32_t {
             uint32_t *calls = reinterpret_cast<uint32_t *>(c.self->data);
             const uint32_t r = c.Rand(++calls[0]);
             if (r % 100 < 35) return 0;
             return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(PickFake("ObjInfo", r)));
         }), S(49, "t", "v", "", FillText(0, 41)), S(53, "t", "v", "", FillText(0, 42)), S(66, "", kGroups),
         Lookup(70, "", "StrOwner", 30)});
    add("StrOwner", 2, {S(10, "", kGroups), S(11, "", kGroups)});
    // The services behind the game's cached getters.
    add("LangSvc", 1, {S(38, "V", "p", "", [](Call &c) -> uint32_t {
                           if (gOptions.languageMissing) return 0;
                           return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(PickFake("Language", c.Rand(1))));
                       })});
    add("Language", 1, {S(9, "VVVt", "v", "", FillText(3, 51)), S(18, "VVt", "v", "", FillText(2, 52)), S(28, "TTt", "v", "", FillText(2, 61))});
    add("Singleton", 1, {Lookup(4, "", "WantFear", 20), Lookup(6, "", "GroupNames", 20), S(24, "", "o:Defs!")});
    add("WantFear", 1, {S(21, "V", "p", "", [](Call &c) -> uint32_t { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gLists[0][c.a[0] & 3])); }),
                        S(22, "V", "p", "", [](Call &c) -> uint32_t { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gLists[1][c.a[0] & 3])); })});
    add("WantElem", 4, {S(6, "tVVV", "v", "", FillText(0, 71)), S(7, "t", "v", "", FillText(0, 72))});
    add("GroupNames", 1, {S(75, "Vt", "b", "", [](Call &c) -> uint32_t {
                              FillText(1, 81)(c);
                              return BoolAnswer(c, 82, 70);
                          })});
    return t;
}

// ---- the markers' static strings -----------------------------------------------------------------------------------------------------

// The markers are static std::string objects the game's initialisers build before the first frame; the harness maps the image
// without running them, so the test builds them (same layout: our std::string is the game's).
struct MarkerString {
    uint32_t rva;
    const char *text;
};
const MarkerString kMarkerStrings[] = {
    {0x1014f88, "$Neighbor"}, {0x1014fa0, "$FamilyAssets"}, {0x1014fb8, "$Family"}, {0x1014fd0, "$LastName"},
    {0x1014fe8, "$FullName"}, {0x1015000, "$Me"}, {0x1015018, "$Object"}, {0x1015030, "$Local"}, {0x1015048, "$Money"},
    {0x1015060, "$TimeLocal"}, {0x1015078, "$FullNameLocal"}, {0x1015090, "$NameLocal"}, {0x10150a8, "$NameAttrib"},
    {0x10150c0, "$NeighborLocal"}, {0x10150d8, "$Attrib"}, {0x10150f0, "$CatalogLocal"}, {0x1015108, "$TokenNameLocal"},
    {0x1015120, "$DayLocal"}, {0x1015138, "$JobTitle"}, {0x1015150, "$PrivateString"}, {0x1015168, "$BehaviorString"},
    {0x1015180, "$GlobalString"}, {0x1015198, "$LotNameLocal"}, {0x10151b0, "$DayFlags"}, {0x10151c8, "$Cash"},
    {0x10151e0, "$SkillName"}, {0x10151f8, "$WordPoints"}, {0x1015210, "$GroupName"}, {0x1015228, "$NameParam"},
    {0x10dad08, "$AspirationString"}, {0x10dad20, "$SemiGlobalString"}, {0x10dad38, "$LotNameLocalByLotId"},
    {0x10dad50, "$TokenPersonalCountedLocalName"}, {0x10dad68, "$TokenPersonalSingularLocalName"},
    {0x10dad80, "$TokenFamilyCountedLocalName"}, {0x10dad98, "$TokenFamilySingularLocalName"},
    {0x10dadb0, "$TokenPersonalCountedLocalDesc"}, {0x10dadc8, "$TokenPersonalSingularLocalDesc"},
    {0x10dade0, "$TokenFamilyCountedLocalDesc"}, {0x10dadf8, "$TokenFamilySingularLocalDesc"},
    {0x10dae10, "$NeighborhoodLocal"}, {0x10dae28, "$PrimaryNeighborhood"}, {0x10dae40, "$WantDescriptionLocal"},
    {0x10dae58, "$WantHelpTextLocal"}, {0x10dae70, "$FearDescriptionLocal"}, {0x10dae88, "$FearHelpTextLocal"},
    {0x10daea0, "$PropertyPrivateString"}, {0x10daeb8, "$PropertySemiGlobalString"}, {0x10daed0, "$PropertyGlobalString"},
    {0x10daee8, "$GUIDCatalogNameLocal"},
};
constexpr int kMarkerStringCount = sizeof(kMarkerStrings) / sizeof(kMarkerStrings[0]);

class MarkerStrings {
public:
    explicit MarkerStrings(difftest::Context &ctx) : mCtx(ctx)
    {
        for (int i = 0; i < kMarkerStringCount; ++i) {
            void *at = ctx.image.At<void>(kMarkerStrings[i].rva);
            std::memcpy(mSaved[i], at, sizeof(std::string));
            new (at) std::string(kMarkerStrings[i].text);
        }
    }
    MarkerStrings(const MarkerStrings &) = delete;
    ~MarkerStrings()
    {
        for (int i = 0; i < kMarkerStringCount; ++i) {
            void *at = mCtx.image.At<void>(kMarkerStrings[i].rva);
            static_cast<std::string *>(at)->~basic_string();
            std::memcpy(at, mSaved[i], sizeof(std::string));
        }
    }

private:
    difftest::Context &mCtx;
    uint8_t mSaved[kMarkerStringCount][sizeof(std::string)];
};

// ---- stand-ins for the original callees that need the resource system -----------------------------------------------------------

const char *LoaderText(uint32_t table, uint32_t entry, uint32_t group)
{
    return kTexts[Mix(gSeed, table, entry, group) % kTextCount];
}

// What the resource manager answers when the string-table loader (0x1d0e90, 0x1d10d0) asks for a text: logs the key and gives a
// text chosen by it (the log words are those the loader's own stand-in used to write, so the traces stay comparable).
bool AnswerText(const tsfake::Ask &ask, std::string &text)
{
    Tick();
    Log(0x1d0e90);
    Log(ask.instance >> 16);
    Log(ask.instance & 0xffff);
    Log(ask.group);
    Log(ask.type);
    text = LoaderText(ask.instance >> 16, ask.instance & 0xffff, ask.group);
    return true;
}

// 0x5307b0 (__cdecl): the property text formatter.
uint8_t __cdecl FormatProperty(const char *text, cRZString *out)
{
    Tick();
    Log(0x5307b0);
    LogString(text);
    const uint32_t r = Mix(gSeed, static_cast<uint32_t>(std::strlen(text)), text[0] ? static_cast<uint8_t>(text[0]) : 0u);
    const bool ok = r % 3 != 0;
    if (ok) out->mString = kTexts[(r >> 8) % kTextCount];
    return ok ? 1 : 0;
}

// ---- the texts -------------------------------------------------------------------------------------------------------------------

struct Marker {
    const char *name;
    int parameters; // how many ":n" the game gives it
};

const Marker kMarkers[] = {
    {"$TokenPersonalCountedLocalName", 2},  {"$TokenPersonalSingularLocalName", 2}, {"$TokenFamilyCountedLocalName", 2},  {"$TokenFamilySingularLocalName", 2},
    {"$TokenPersonalCountedLocalDesc", 2},  {"$TokenPersonalSingularLocalDesc", 2}, {"$TokenFamilyCountedLocalDesc", 2},  {"$TokenFamilySingularLocalDesc", 2},
    {"$FamilyAssets", 1},    {"$Family", 0},            {"$LastName", 0},          {"$NeighborLocal", 1},   {"$NeighborhoodLocal", 1}, {"$PrimaryNeighborhood", 0},
    {"$Neighbor", 0},        {"$Me", 0},                {"$Object", 0},            {"$Local", 1},           {"$Money", 1},             {"$Cash", 1},
    {"$TimeLocal", 1},       {"$Attrib", 1},            {"$NameAttrib", 1},        {"$NameLocal", 1},       {"$NameParam", 1},         {"$FullNameLocal", 1},
    {"$FullName", 0},        {"$TokenNameLocal", 1},    {"$LotNameLocalByLotId", 1}, {"$LotNameLocal", 1},  {"$CatalogLocal", 1},      {"$DayLocal", 1},
    {"$DayFlags", 1},        {"$JobTitle", 2},          {"$PrivateString", 2},     {"$BehaviorString", 2},  {"$AspirationString", 2},  {"$SemiGlobalString", 2},
    {"$GlobalString", 2},    {"$SkillName", 1},         {"$WordPoints", 1},        {"$WantDescriptionLocal", 2}, {"$FearDescriptionLocal", 2}, {"$WantHelpTextLocal", 2},
    {"$FearHelpTextLocal", 2}, {"$GroupName", 1},       {"$PropertyPrivateString", 2}, {"$PropertySemiGlobalString", 2}, {"$PropertyGlobalString", 2},
    {"$GUIDCatalogNameLocal", 2},
};
constexpr int kMarkerCount = sizeof(kMarkers) / sizeof(kMarkers[0]);

bool Allowed(const std::string &name)
{
    if (gOptions.crashy && name != "$TokenNameLocal") return true; // a crashy seed asks for every marker that can crash
    if (name == "$Attrib" || name == "$NameAttrib") return gOptions.crashy || (gOptions.attribSafe && gOptions.hasFrame);
    if (name == "$CatalogLocal" || name == "$JobTitle") return !gOptions.objectInfoMissing;
    if (name == "$TimeLocal") return !gOptions.languageMissing;
    if (name == "$WantHelpTextLocal" || name == "$FearDescriptionLocal" || name == "$FearHelpTextLocal") return !gOptions.wantElementsMissing;
    if (name == "$TokenNameLocal") return gOptions.tokenNameLocal;
    return true;
}

std::string RandomCase(std::mt19937 &r, std::string s)
{
    for (char &c : s) {
        if (r() % 4 == 0 && c >= 'a' && c <= 'z') c = static_cast<char>(c ^ 0x20);
    }
    return s;
}

std::string MakeText(std::mt19937 &r)
{
    std::string text;
    const int pieces = 2 + static_cast<int>(r() % 7);
    for (int i = 0; i < pieces; ++i) {
        static const char *const fillers[] = {"Hello ", " dear ", "x", "\n", "$", " ", "%d", "a $ b ", ":"};
        text += fillers[r() % (sizeof(fillers) / sizeof(fillers[0]))];
        const Marker *m;
        int guard = 0;
        do {
            m = &kMarkers[r() % kMarkerCount];
        } while (!Allowed(m->name) && ++guard < 64);
        if (!Allowed(m->name)) continue;
        std::string marker = RandomCase(r, m->name);
        const int given = (r() % 12 == 0) ? static_cast<int>(r() % 3) : m->parameters; // now and then too few / too many
        for (int p = 0; p < given; ++p) {
            marker += ':';
            if (r() % 14 != 0) marker += std::to_string(r() % 10);
            if (r() % 25 == 0) marker += "x";
        }
        text += marker;
    }
    return text;
}

// ---- building a world ------------------------------------------------------------------------------------------------------------

struct Call5 {
    std::string text;
    int16_t stackObjectId;
    bool frame;
    bool pair;
    bool person;
};

uint32_t gSaved[4];
constexpr uint32_t kCacheRvas[4] = {0x10f51c8, 0x10cc288, 0x10cc2a8, 0x10cc2dc};

void BuildWorld(difftest::Context &ctx, uint32_t seed, Call5 &call)
{
    std::mt19937 r(seed * 2654435761u + 17u);
    gSeed = seed;
    gOptions = Options();
    gOptions.attribSafe = r() % 100 < 35;
    gOptions.objectInfoMissing = r() % 100 < 12;
    gOptions.languageMissing = r() % 100 < 15;
    gOptions.wantElementsMissing = r() % 100 < 12;
    gOptions.tokenNameLocal = r() % 100 < 4;
    gOptions.crashy = r() % 100 < 6;
    gOptions.friendlyLocals = gOptions.crashy || r() % 100 < 30;
    gOptions.hasFrame = r() % 100 >= 6;
    if (gOptions.crashy) {
        gOptions.objectInfoMissing = r() % 2;
        gOptions.languageMissing = r() % 2;
        gOptions.wantElementsMissing = r() % 4 != 0;
    }
    gOptions.localCount = static_cast<int>(r() % 10);
    gOptions.argCount = static_cast<int>(r() % 6);

    // The object under test: its context and the object table.
    uint8_t *self = gWorld->self;
    *reinterpret_cast<void **>(self + 0x40) = InstancePtr("Context");
    *reinterpret_cast<void **>(self + 0x1c4) = &gOwner;
    const int entries = 5 + static_cast<int>(r() % 3);
    for (int i = 0; i < 8; ++i) {
        const bool present = i < entries && (gOptions.attribSafe ? i < kObjects : r() % 100 >= 20);
        gObjectEntries[i] = (present && i < kObjects) ? InstancePtr("Obj", i) : (present ? InstancePtr("Obj", i % kObjects) : nullptr);
    }
    gOwner.mFirst = gObjectEntries;
    gOwner.mLast = gObjectEntries + entries;

    // The want / fear lists: 0 to 6 elements each; null elements only where the options say.
    for (int fear = 0; fear < 2; ++fear) {
        for (int category = 0; category < 4; ++category) {
            const int n = static_cast<int>(r() % 7);
            for (int i = 0; i < 8; ++i) {
                const bool missing = gOptions.wantElementsMissing && (fear == 0 || gOptions.crashy) && r() % (gOptions.crashy ? 2 : 3) == 0;
                gElements[fear][category][i] = missing ? nullptr : InstancePtr("WantElem", static_cast<int>(r() % 4));
            }
            gLists[fear][category].mFirst = gElements[fear][category];
            gLists[fear][category].mLast = gElements[fear][category] + n;
        }
    }

    // The game's cached getters.
    *ctx.image.At<void *>(kCacheRvas[0]) = InstancePtr("LangSvc");
    *ctx.image.At<void *>(kCacheRvas[1]) = InstancePtr("Singleton");
    *ctx.image.At<void *>(kCacheRvas[2]) = InstancePtr("Svc23");
    *ctx.image.At<void *>(kCacheRvas[3]) = InstancePtr("LangSvc"); // the same service under its second cache

    // The call.
    if (gOptions.tokenNameLocal) {
        gOptions.hasFrame = true;
        gOptions.localCount = (std::max)(gOptions.localCount, 1);
    }
    call.text = MakeText(r);
    if (gOptions.tokenNameLocal) call.text = "$TokenNameLocal:0 " + call.text;
    // half of the crashy seeds are only crash-prone markers, with friendly locals
    if (gOptions.crashy && r() % 2 == 0) call.text.clear();
    for (int extra = 0; gOptions.crashy && extra < 3 && (r() % 10 < 8 || call.text.empty()); ++extra) {
        // one more of the markers whose lookups crash the original when they fail
        static const char *const crashProne[] = {"$FearDescriptionLocal", "$WantHelpTextLocal", "$FearHelpTextLocal", "$WantDescriptionLocal", "$CatalogLocal", "$JobTitle", "$TimeLocal"};
        call.text += std::string(" ") + crashProne[r() % 7] + ":" + std::to_string(r() % 3) + ":" + std::to_string(r() % 3);
    }
    switch (r() % 4) {
    case 0:
        call.stackObjectId = 0;
        break;
    case 1:
        call.stackObjectId = static_cast<int16_t>(1 + r() % 5);
        break;
    case 2:
        call.stackObjectId = static_cast<int16_t>(r() % 8);
        break;
    default:
        call.stackObjectId = static_cast<int16_t>(static_cast<int>(r() % 12) - 3);
        break;
    }
    call.frame = gOptions.hasFrame;
    call.pair = r() % 3 != 0;
    call.person = r() % 3 != 0;
}

struct Result {
    std::string text;
    int16_t pair[2];
    void *person;
};

// Runs one side on the world just built.
Outcome RunExpansion(const Call5 &call, bool ours, Result &result, const void *original)
{
    using Fn = void(__thiscall *)(void *, cIGZString *, int16_t, void *, int16_t *, void **);
    cRZString text;
    text.Construct(call.text.c_str());
    result.pair[0] = result.pair[1] = 0x5555;
    result.person = reinterpret_cast<void *>(0x55555555);
    void *frame = call.frame ? InstancePtr("Frame") : nullptr;
    void *self = gWorld->self;
    auto body = [&](const PrimParamView *) -> uint32_t {
        if (ours) {
            reinterpret_cast<TokenObject *>(self)->ExpandStringTokens(text.AsInterface(), call.stackObjectId, reinterpret_cast<ifaces::cTSTreeStackElem *>(frame),
                                                                      call.pair ? result.pair : nullptr, call.person ? &result.person : nullptr);
        } else {
            reinterpret_cast<Fn>(const_cast<void *>(original))(self, text.AsInterface(), call.stackObjectId, frame, call.pair ? result.pair : nullptr,
                                                                  call.person ? &result.person : nullptr);
        }
        return 0;
    };
    Outcome o;
    gSide = ours ? "ours" : "original";
    gTrace.clear();
    try {
        o.ret = Guarded(body, nullptr, o.crashed);
    } catch (short code) {
        o.thrown = code;
    }
    result.text = text.mString;
    // The result digest: the final text, the out pair and the out pointer (normalised).
    uint32_t digest = 2166136261u;
    for (unsigned char ch : result.text) digest = (digest ^ ch) * 16777619u;
    digest = (digest ^ static_cast<uint16_t>(result.pair[0])) * 16777619u;
    digest = (digest ^ static_cast<uint16_t>(result.pair[1])) * 16777619u;
    digest = (digest ^ NormWord(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(result.person)))) * 16777619u;
    if (!o.crashed && !o.thrown) o.ret = digest;
    o.trace = gTrace;
    for (int i = 0; i < kMaxFakes; ++i) o.refs.push_back(gWorld->fk[i].refs);
    text.Destruct();
    return o;
}

} // namespace

DIFF_TEST(simantics_token_expander, 0x535130)
{
    using Fn = void(__thiscall *)(void *, cIGZString *, int16_t, void *, int16_t *, void **);
    const void *original = ctx.Original<Fn>(0x535130);
    Setup(ctx, Types());

    MarkerStrings markerStrings(ctx);
    for (int i = 0; i < 4; ++i) gSaved[i] = *ctx.image.At<uint32_t>(kCacheRvas[i]);
    tsfake::ManagerFixture resourceManager(ctx);
    resourceManager.InstallManagerOnly();
    tsfake::gAnswerer = AnswerText;
    CodePatch patchProperty(ctx, 0x5307b0, reinterpret_cast<void *>(&FormatProperty));
    CodePatch patchPropertyOurs(reinterpret_cast<void *>(&tokens::FormatPropertyText), reinterpret_cast<void *>(&FormatProperty)); // ours is called directly by our stage

    uint32_t changed = 0, threw = 0, crashed = 0, unchanged = 0;
    const char *only = std::getenv("TOKENS_ONLY");
    const uint32_t worlds = 6000;
    for (uint32_t seed = only ? static_cast<uint32_t>(std::atoi(only)) : 1; seed <= (only ? static_cast<uint32_t>(std::atoi(only)) : worlds); ++seed) {
        ++ctx.cases;
        Call5 call;
        Result ra, rb;
        ResetWorld(seed);
        BuildWorld(ctx, seed, call);
        const Outcome a = RunExpansion(call, false, ra, original);
        ResetWorld(seed);
        BuildWorld(ctx, seed, call);
        const Outcome b = RunExpansion(call, true, rb, original);
        std::string why;
        if (!Equal(a, b, why) || ra.text != rb.text) {
            ctx.Fail("seed %u: %s%s\n      input    '%s'\n      original '%s'\n      ours     '%s'", seed, why.empty() ? "final text differs" : why.c_str(),
                     gOptions.crashy ? " (crashy)" : "", call.text.c_str(), ra.text.c_str(), rb.text.c_str());
            static int dumped = 0;
            if (dumped++ < 3 || only) {
                size_t at = 0;
                while (at < a.trace.size() && at < b.trace.size() && a.trace[at] == b.trace[at]) ++at;
                const size_t from = at > 16 ? at - 16 : 0;
                for (int side = 0; side < 2; ++side) {
                    const Outcome &o = side ? b : a;
                    std::printf("      %s log from word %zu:", side ? "ours    " : "original", from);
                    for (size_t i = from; i < o.trace.size() && i < at + 16; ++i) std::printf(" %x", o.trace[i]);
                    std::printf("\n");
                }
            }
            if (only) break;
            continue;
        }
        if (a.crashed) ++crashed;
        else if (a.thrown) ++threw;
        else if (ra.text != call.text) ++changed;
        else ++unchanged;
    }
    for (int i = 0; i < 4; ++i) *ctx.image.At<uint32_t>(kCacheRvas[i]) = gSaved[i];
    std::printf("      %u texts changed, %u unchanged, %u threw, %u both crashed\n", changed, unchanged, threw, crashed);
    Report(Summary());
    if (!only && (changed < worlds / 3 || threw == 0 || crashed == 0)) ctx.Fail("the worlds did not reach every outcome");
}
