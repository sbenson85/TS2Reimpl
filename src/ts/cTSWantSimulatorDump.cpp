// cTSWantSimulator slot 52 (0x489210, lane GF): the scored-wants dump, and the small original functions it is built on
// (WantSimContext::Assign 0x485e40, WantMemoryStore::AdvanceToTick 0x551320 (lane GG), IndexOfMatch 0x48ce50, WantFloatText 0x499000).
// The dump is a debug tool: it runs the sim's want update to the end (as RunUpdate does for one sim), then writes one row
// per want and fear slot (and per memory) with the scores the update computed, and opens the file with the shell.
#include "ts/cTSWantSimulator.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/CommandParser.h"
#include "rz/CountedRefVector.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters2.h"
#include "rz/RZFileStreams.h"
#include "rz/cRZString.h"
#include "simantics/cTSBehaviorTree.h" // RefPtrVector (Clear, 0x176f00)

#include <bit>
#include <cstdint>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

namespace {

// ---- calls through other objects' vtables (slot numbers from the original's call sites) ----
template <int N, typename R = uint32_t, typename... A> R Slot(const void *object, A... args)
{
    return VirtualCall<R(__thiscall *)(void *, A...), N>(object, args...);
}

void ReleaseRef(void *object) { Slot<2>(object); }

// A cRZString this function builds (inline in the original: vtable, text, refcount 0) and destroys on scope exit.
struct Text {
    cRZString s;
    Text() { s.Construct(); }
    explicit Text(const char *text) { s.Construct(text); }
    Text(const Text &) = delete;
    Text &operator=(const Text &) = delete;
    ~Text() { s.Destruct(); }
    void operator=(const char *text) { s.mString.assign(text); }
};

// A cRZString a callee builds in place (0x499000, 0x498d20, 0x496af0), destroyed on scope exit.
struct Built {
    alignas(4) unsigned char raw[sizeof(cRZString)];
    Built() = default;
    Built(const Built &) = delete;
    Built &operator=(const Built &) = delete;
    cRZString *get() { return reinterpret_cast<cRZString *>(raw); }
    ~Built() { get()->Destruct(); }
};

// The text file (0x18 bytes, the file stream class cRZOFStream 0x1b4dd2 / 0x1b52d8, rz/RZFileStreams.h; the writes go
// through its vtable). Its error test 0x1b5941 (a body the game folds across the stream classes) is ours, called directly.
struct DumpFile {
    alignas(4) unsigned char mBytes[0x18];
    explicit DumpFile(cRZString *path) { Stream()->Construct(*path->AsInterface(), 0, 0, 0, 0); }
    cRZOFStream *Stream() { return reinterpret_cast<cRZOFStream *>(mBytes); }
    bool Fail() { return Stream()->GetError() != 0; }
    void Flush() { Stream()->Flush(); }
    void Close() { Stream()->Destruct(); }
    void Put(const char *text) { Slot<14, uint8_t>(mBytes, text); }
    void Put(cRZString &text) { Slot<15, uint8_t>(mBytes, static_cast<void *>(&text)); }
    void Put(Text &text) { Put(text.s); }
    void PutI16(int16_t value) { Slot<6, uint8_t>(mBytes, int32_t(value)); }
    void PutI32(int32_t value) { Slot<8, uint8_t>(mBytes, value); }
    void PutU32(uint32_t value) { Slot<9, uint8_t>(mBytes, value); }
    void Cell(const char *text) { Put(text), Put(","); }
    void Cell(Text &text) { Put(text), Put(","); }
    void Cell(cRZString &text) { Put(text), Put(","); }
};

// Ticks to hours as the dump shows them (the original's constant, about 1/3600).
const float kTicksToHours = std::bit_cast<float>(0x3991a2b4u);

// ---- the original's callees still in the game (each in the slot 52 entry's original_calls) ----
void CopyRefs(WantRefList &to, const WantRefList &from) // 0x1d89b0 ret 4: copy constructor (zeroes, then copies, counted)
{
    OriginalFunction<void(__thiscall *)(WantRefList *, const WantRefList *), 0x1d89b0>()(&to, &from);
}
void ResizeRefs(WantRefList &list, uint32_t slots) // 0x4992c0 ret 8: resize(slots, null)
{
    void *null = nullptr;
    OriginalFunction<void(__thiscall *)(WantRefList *, uint32_t, void *const *), 0x4992c0>()(&list, slots, &null);
}
void AssignRefs(WantRefList &to, void **first, uint32_t count) // 0x483930 ret 8: assign (counted)
{
    OriginalFunction<void(__thiscall *)(WantRefList *, void **, uint32_t), 0x483930>()(&to, first, count);
}
void AppendRefs(WantRefList &to, const WantRefList &from) // insert(end, first, count) (counted; library code, 0x1d6520)
{
    AppendCounted(&to, from.mFirst, from.Size());
}
void ClearRefs(WantRefList &list) { reinterpret_cast<RefPtrVector *>(&list)->Clear(); }

} // namespace

// ======================================================================================================================
// Helpers (original functions of their own)
// ======================================================================================================================

// 0x485e40: simId copied, the person and the neighbour assigned as counted pointers (AddRef new, then Release old; nothing
// when the same), the attribute block copied.
WantSimContext *WantSimContext::Assign(const WantSimContext &other)
{
    simId = other.simId;
    auto assign = [](void *&slot, void *value) {
        void *const old = slot;
        if (value == old) return;
        if (value) Slot<1>(value);
        slot = value;
        if (old) ReleaseRef(old);
    };
    if (&other.person != &person) assign(person, other.person);
    if (&other.neighbor != &neighbor) assign(reinterpret_cast<void *&>(neighbor), other.neighbor);
    attributes = other.attributes;
    return this;
}

// 0x551320 (the store's clock) is WantMemoryStore::AdvanceToTick, lane GG (src/ts/cTSWantSimulatorGG.cpp).

// 0x48ce50.
int32_t cTSWantSimulator::IndexOfMatch(void *want, WantRefList *list, void **out)
{
    for (uint32_t i = 0; i < list->Size(); ++i) {
        void *entry = list->mFirst[i];
        *out = entry;
        if (entry && Slot<3, uint8_t>(entry, want)) return int32_t(i);
    }
    *out = nullptr;
    return -1;
}

// 0x499000.
cRZString *__cdecl WantFloatText(cRZString *out, float value)
{
    char text[0x20];
    nGZCommandParser::FormatDecimal(double(value), text, int(sizeof(text)), 4);
    return out->Construct(text);
}

// ======================================================================================================================
// Slot 52
// ======================================================================================================================

void cTSWantSimulator::DumpScoredWants(int16_t simId, uint8_t withMemories, uint8_t plainName, uint8_t keepOptions)
{
    FlushUpdates();
    mMap50.clear();
    mMap58.clear();
    auto *options = reinterpret_cast<uint8_t *>(&m104);
    options[1] = keepOptions;
    if (keepOptions) {
        options[2] = withMemories;
        options[3] = plainName;
    }

    WantSimContext context;
    context.Construct(simId);
    struct ContextRelease { // the context released last, after every list
        WantSimContext &context;
        ~ContextRelease() { context.Destruct(); }
    } releaseContext{context};
    WantMemoryStore *const store = GetMemoryStore(context.simId);

    // The sim's update, run to the end on copies of its lists (wants padded to 4 slots, fears to 3).
    WantRefList newWants, newFears, wants, fears;
    CopyRefs(wants, mWants[simId]);
    CopyRefs(fears, mFears[simId]);
    if (wants.Size() < 4) ResizeRefs(wants, 4);
    if (fears.Size() < 3) ResizeRefs(fears, 3);
    ResizeRefs(newWants, wants.Size());
    ResizeRefs(newFears, fears.Size());
    mPendingPriority = 0;
    reinterpret_cast<WantSimContext *>(&mCurrentSim)->Assign(context);
    mCurrentStore = store;
    mD0 = uint32_t(store->AdvanceToTick());
    if (&mVecD4 != &newWants) AssignRefs(mVecD4, newWants.mFirst, newWants.Size());
    if (&mVecE0 != &newFears) AssignRefs(mVecE0, newFears.mFirst, newFears.Size());
    ClearRefs(mVecEC);
    mF8 = **static_cast<uint32_t **>(Slot<6, void *>(mDefinitions.object));
    while (!OriginalFunction<bool(__thiscall *)(cTSWantSimulator *, int32_t), 0x4978e0>()(this, 100)) {
    }
    OriginalFunction<void(__thiscall *)(cTSWantSimulator *, WantSimContext *, WantRefList *), 0x492160>()(this, &context, &mVecEC);
    OriginalFunction<void(__thiscall *)(cTSWantSimulator *, WantSimContext *, WantRefList *, WantRefList *, WantRefList *, uint32_t, uint32_t,
                                        uint32_t),
                     0x495650>()(this, &context, &mVecEC, &newWants, &newFears, 1, 1, 0);

    // The rows: the wants, the fears, what the update left in mVecEC, then (on request) the sim's memories, oldest first.
    WantRefList rows;
    AssignRefs(rows, wants.mFirst, wants.Size());
    AppendRefs(rows, fears);
    AppendRefs(rows, mVecEC);
    mCurrentSim = 0;
    ClearRefs(mVecEC);
    if (withMemories) {
        MemoryList list;
        store->CollectMemories(&list, true);
        WantRefList memories;
        OriginalFunction<void(__thiscall *)(WantRefList *, MemoryNode *, MemoryNode *), 0x4850c0>()(&memories, list.head->next,
                                                                                                  list.head);
        OriginalFunction<void(__cdecl *)(void **, void **, int32_t, void *), 0x484e00>()(
            memories.mFirst, memories.mLast, int32_t(memories.Size()), OriginalAddress<void *, 0x487bc0>());
        AppendRefs(rows, memories);
    }

    void *const globals = GetCachedSingleton();
    void *const person = context.person;
    void *const neighbor = context.neighbor;
    Text name;
    if (person) name.s.mString.assign(Slot<92, const char *>(Slot<8, void *>(person)));
    else if (neighbor) Slot<49>(Slot<10, void *>(neighbor), static_cast<void *>(&name.s));

    Text fileName;
    if (plainName) fileName.s.Sprintf("%s%03d_ScoredWants.dwh", name.s.ToChar(), int32_t(context.simId));
    else {
        const char *const text = name.s.ToChar();
        const int32_t clock = store->AdvanceToTick();
        fileName.s.Sprintf("%s%03d_ScoredWants_%d.csv", text, int32_t(context.simId), clock);
    }

    {
        DumpFile file(&fileName.s);
        if (file.Fail()) {
            file.Close();
        } else {
            // ORIGINAL-BUG: the attribute block is read whether or not the sim has one (a sim id with neither a person nor a
            // neighbour crashes here).
            const auto *attributes = reinterpret_cast<const uint8_t *>(context.attributes);
            auto attribute = [&](uint32_t offset) { return *reinterpret_cast<const int16_t *>(attributes + offset); };

            Text age("unknown");
            const int32_t ageCode = Slot<74, int32_t>(GetCachedSingletonC(), int32_t(attribute(0x74)));
            switch (ageCode) {
            case 0: age = "Adult"; break;
            case 1: age = "Child"; break;
            case 2: age = "Toddler"; break;
            case 3: age = "Teen"; break;
            case 4: age = "Elder"; break;
            case 13: age = "Baby"; break;
            }
            Text aspiration("unknown");
            Text secondary("None");
            const int32_t primary = attribute(0x5c);
            // ORIGINAL-BUG: 0x54add0 is called with a null person when the sim is not instantiated.
            const int32_t second = OriginalFunction<int16_t(__cdecl *)(void *), 0x54add0>()(person);
            auto aspirationName = [](int32_t value) -> const char * {
                switch (value) {
                case 1: return "Romance";
                case 2: return "Family";
                case 4: return "Fortune";
                case 16: return "Popularity";
                case 32: return "Knowledge";
                case 64: return "Grow Up";
                case 128: return "Pleasure Seeker";
                case 256: return "Grilled Cheese";
                }
                return nullptr;
            };
            if (const char *n = aspirationName(second > 0 ? primary ^ second : primary)) aspiration = n;
            if (const char *n = aspirationName(second)) secondary = n;
            uint32_t score = 0;
            if (person) score = ScoreAttributeA2(person, 0);
            else if (neighbor) score = ScoreMotiveA2(neighbor, 0);
            const int32_t aspirationScore = ScoreForValue(ageCode, int32_t(score));
            const int32_t mood = person ? int32_t(Slot<13, float>(person, int32_t(3))) : 0;

            file.Put("Scored Wants"), file.Put("\n");
            file.Cell("Name"), file.Put(name), file.Put("\n");
            file.Cell("Neighbor ID"), file.PutI16(context.simId), file.Put("\n");
            file.Cell("Personal Clock");
            file.PutI32(store->AdvanceToTick());
            file.Put("\n");
            file.Cell("Age"), file.Cell(age), file.PutI16(attribute(0x74)), file.Put("\n");
            file.Cell("Aspiration"), file.Put(aspiration), file.Put("\n");
            file.Cell("Secondary Aspiration"), file.Put(secondary), file.Put("\n");
            file.Cell("Aspiration Score"), file.PutI32(aspirationScore), file.Put(","), file.PutI32(int32_t(score)), file.Put("\n");
            file.Cell("Mood"), file.PutI32(mood), file.Put("\n");
            file.Put("Personality"), file.Put("\n");
            const std::pair<const char *, uint32_t> traits[] = {
                {"Mean / Nice", 0x4}, {"Lazy / Active", 0x6}, {"Serious / Playful", 0xa}, {"Shy / Outgoing", 0xc}, {"Sloppy / Neat", 0xe}};
            for (const auto &trait : traits) file.Put(","), file.Cell(trait.first), file.PutI16(attribute(trait.second)), file.Put("\n");
            file.Put("\n");
            static const char *const kColumns[] = {
                "want ID", "want category", "subject type", "subject ID", "subject name", "rel", "number", "op", "description",
                "achievement", "purged", "in panel", "priority", "CT", "chosen", "treeID", "node ID", "min", "max", "elapsed",
                "lifetime", "predecessor", "age wt", "age %", "asp wt", "asp %", "mood wt", "mood %", "pers wt", "pers %",
                "rel wt", "rel %", "score wt", "score %", "data wt", "data %", "data index", "excluded", "factors %",
                "base priority", "lifetime %"};
            for (const char *column : kColumns) file.Cell(column);
            file.Put("\n");

            uint32_t row = 0;
            for (void **it = rows.mFirst; it != rows.mLast; ++it, ++row) {
                void *const want = *it;
                if (!want) {
                    file.Put("empty"), file.Put("\n");
                    continue;
                }
                const bool shown = !Slot<23, uint8_t>(want) && row >= wants.Size() + fears.Size();
                void *const definition = Slot<12, void *>(want);
                Text id;
                id.s.Sprintf("0x%08x", Slot<3>(definition));
                const bool isFear = Slot<8, uint8_t>(definition) != 0;
                void *const wantContext = Slot<20, void *>(want);

                // "+"/"-": the update chose it this time.
                const char *chosen = "";
                if (wantContext) {
                    const WantRefList &picked = isFear ? newFears : newWants;
                    for (void **p = picked.mFirst; p != picked.mLast; ++p)
                        if (*p == want) {
                            chosen = isFear ? "-" : "+";
                            break;
                        }
                }
                Text chosenText(chosen);
                // "++"/"--": in the panel; "+"/"-": purged from the copy of the panel (its slot released and emptied).
                Text panel;
                if (row < wants.Size() + fears.Size()) panel = isFear ? "--" : "++";
                else if (wantContext) {
                    void *match = nullptr;
                    WantRefList &list = isFear ? fears : wants;
                    const int32_t index = IndexOfMatch(want, &list, &match);
                    if (index >= 0) {
                        if (void *const old = list.mFirst[index]) {
                            list.mFirst[index] = nullptr;
                            ReleaseRef(old);
                        }
                        panel = isFear ? "-" : "+";
                    }
                }
                const float priority = Slot<23, uint8_t>(want) ? -1.0f : Slot<25, float>(want);
                Text achievement(Slot<28, uint8_t>(want) ? "*" : (Slot<27, uint8_t>(want) ? "-" : ""));
                Text kind;
                switch (Slot<26>(want)) {
                case 0:
                    if (Slot<9, uint8_t>(Slot<13, void *>(definition), "", int32_t(-1), int32_t(1))) kind = "n/a";
                    break;
                case 1: kind = "+"; break;
                case 2: kind = "-"; break;
                }

                Text node("no context");
                Text elapsed;
                Text lifetime;
                int32_t minimum = 0;
                int32_t maximum = 100;
                std::vector<float> factors;
                if (wantContext) {
                    node.s.Sprintf("0x%08x", Slot<10>(wantContext));
                    Slot<25>(wantContext, &minimum, &maximum);
                    const int32_t ticks = Slot<21, int32_t>(wantContext);
                    void *const predecessor = Slot<21, void *>(want);
                    if (predecessor && Slot<23, uint8_t>(predecessor)) {
                        const int32_t clock = GetMemoryStore(simId)->AdvanceToTick();
                        const int32_t since = clock - Slot<24, int32_t>(predecessor);
                        Built hours;
                        WantFloatText(hours.get(), float(since) * kTicksToHours);
                        elapsed.s.mString.assign(hours.get()->mString);
                    }
                    if (ticks <= 0) lifetime = "infinite";
                    else lifetime.s.Sprintf("%.0f", double(float(ticks) * kTicksToHours));
                    OriginalFunction<float(__thiscall *)(cTSWantSimulator *, WantSimContext *, void *, std::vector<float> *), 0x4865b0>()(
                        this, &context, want, &factors);
                }
                Text predecessorText("no predecessor");
                if (void *const predecessor = Slot<21, void *>(want)) predecessorText.s.Sprintf("0x%08x", Slot<13>(predecessor));
                Text category("unknown");
                switch (Slot<9>(definition)) {
                case 0: category = "transient"; break;
                case 1: category = "memorable"; break;
                case 2: category = "power want"; break;
                }

                auto param = [&] { return Slot<14, const WantInstanceParam *>(want); };
                const uint32_t paramKind = param()->kind;
                Text subjectType("unknown"), subjectId("unknown"), subjectName("unknown"), rel, number;
                auto numberFromParam = [&] { number.s.Sprintf("%d", int32_t(param()->extra)); };
                switch (paramKind) {
                case 0:
                    subjectType = "none", subjectId = "none", subjectName = "none";
                    if (param()->extra != 0) numberFromParam();
                    break;
                case 1: {
                    subjectType = "person";
                    subjectId.s.Sprintf("%d", int32_t(Slot<15, int16_t>(want)));
                    void *const table = Slot<10, void *>(globals);
                    if (void *const object = Slot<16, void *>(table, int32_t(Slot<15, int16_t>(want)), int32_t(1))) {
                        Slot<49>(Slot<10, void *>(object), static_cast<void *>(&subjectName.s));
                        // ORIGINAL-BUG: the sim's neighbour is not checked (a want about a person, of a sim without one, crashes).
                        int32_t value = 0;
                        if (void *const relationships = Slot<12, void *>(neighbor)) {
                            if (Slot<5>(relationships, int32_t(Slot<8, int16_t>(object))) > 2)
                                value = Slot<9, int32_t>(relationships, int32_t(Slot<8, int16_t>(object)), int32_t(2));
                        }
                        rel.s.Sprintf("%d", value);
                    }
                    numberFromParam();
                    break;
                }
                case 2:
                    subjectType = "object";
                    subjectId.s.Sprintf("0x%08x", Slot<16>(want));
                    numberFromParam();
                    if (void *const object = Slot<17, void *>(want)) {
                        void *const named = Slot<27, void *>(object) ? Slot<27, void *>(object) : object;
                        Slot<49>(named, static_cast<void *>(&subjectName.s));
                    }
                    break;
                case 3:
                    subjectType = "object type";
                    subjectId.s.Sprintf("%d", param()->value);
                    numberFromParam();
                    GetWantText(uint16_t(param()->value), 1, reinterpret_cast<uint32_t>(&subjectName.s));
                    break;
                case 4:
                    subjectType = "skill";
                    subjectId.s.Sprintf("%d", int32_t(int16_t(param()->value)));
                    numberFromParam();
                    {
                        Built skill;
                        OriginalFunction<cRZString *(__cdecl *)(cRZString *, uint32_t), 0x496af0>()(skill.get(),
                                                                                                  uint16_t(param()->value));
                        if (&subjectName.s != skill.get()) subjectName.s.mString.assign(skill.get()->mString);
                    }
                    if (subjectName.s.mString.size() == 0)
                        subjectName = uint16_t(param()->value) != 0 ? "<unspecified skill>" : "<unknown skill>";
                    break;
                case 5:
                case 6: {
                    subjectType = paramKind == 5 ? "career" : "badge";
                    subjectId.s.Sprintf("0x%08x", Slot<16>(want));
                    numberFromParam();
                    void *const registry = Slot<26, void *>(globals);
                    if (void *const object = Slot<42, void *>(registry, Slot<16>(want)))
                        Slot<49>(Slot<61, void *>(object), static_cast<void *>(&subjectName.s));
                    break;
                }
                }

                Text op;
                switch (Slot<17>(definition)) {
                case 1: op = "="; break;
                case 2: op = "<"; break;
                case 3: op = ">"; break;
                case 4: op = "<="; break;
                case 5: op = ">="; break;
                }
                Text description;
                Slot<6>(want, static_cast<void *>(&description.s), int32_t(1), int32_t(0x20), int32_t(0x20));
                {
                    using Quote = cRZString *(__cdecl *)(cRZString *, cRZString *);
                    Built quotedDescription, quotedName;
                    cRZString *const descriptionCell = OriginalFunction<Quote, 0x498d20>()(quotedDescription.get(), &description.s);
                    cRZString *const nameCell = OriginalFunction<Quote, 0x498d20>()(quotedName.get(), &subjectName.s);
                    file.Cell(id), file.Cell(category), file.Cell(subjectType), file.Cell(subjectId), file.Cell(*nameCell);
                    file.Cell(rel), file.Cell(number), file.Cell(op), file.Cell(*descriptionCell);
                    file.PutI32(Slot<24, int32_t>(want)), file.Put(",");
                    file.Cell(achievement), file.Cell(panel);
                }

                if (wantContext) {
                    if (shown) {
                        {
                            Built priorityText;
                            WantFloatText(priorityText.get(), priority);
                            const uint32_t tree = Slot<9>(wantContext);
                            file.Cell(*priorityText.get()), file.Cell(kind), file.Cell(chosenText), file.Cell(node);
                            file.PutU32(tree), file.Put(",");
                            file.PutI32(minimum), file.Put(",");
                            file.PutI32(maximum), file.Put(",");
                            file.Cell(elapsed), file.Cell(lifetime), file.Cell(predecessorText);
                        }
                        const uint32_t count = uint32_t(factors.size());
                        for (uint32_t i = 0; i < count; ++i) {
                            if (i == 8) {
                                file.Cell(factors[8] != 0.0f ? "*" : "");
                                continue;
                            }
                            if (i < 7) {
                                file.PutI32(Slot<28, int32_t>(wantContext, int32_t(i))), file.Put(",");
                                if (Slot<27, uint8_t>(wantContext, int32_t(i))) file.Put("*");
                            }
                            Built factor;
                            WantFloatText(factor.get(), factors[i]);
                            file.Cell(*factor.get());
                        }
                    } else {
                        file.Cell(""), file.Cell(""), file.Cell(""), file.Cell(node);
                        file.PutU32(Slot<9>(wantContext));
                    }
                }
                file.Put("\n");
            }
            file.Flush();
            file.Close();

            // The file opened with the shell (quoted), unless the debugger service (0x10cc294, slot 5) says not to.
            void *const debugger = *GameAddress<void *>(0x10cc294);
            if (debugger && !Slot<5, uint8_t>(debugger)) {
                {
                    Text quote("\"");
                    fileName.s.Insert(0, *quote.s.AsInterface());
                }
                {
                    Text quote("\"");
                    fileName.s.Insert(fileName.s.Strlen(), *quote.s.AsInterface());
                }
                ShellExecuteA(nullptr, "open", fileName.s.ToChar(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        }
    }
}
