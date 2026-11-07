// Differential tests of cBaseParser's virtual functions (lane HU): the cIGZUnknown face (+0x1b0), the primary
// vtable's slot 4 and the cIBaseParser face's slots (+0x1b8), each on two parsers built alike (tests/gz_base_parser_slots.h).
// The expression slots (30-39) are in gz_base_parser_eval.cpp.
#include "gz_base_parser_slots.h"

#include <algorithm>
#include <bit>

using namespace hu;

namespace {

// Through a volatile: the compiler must not see the adapter behind the cast (it would assume the adapter's own return
// type, e.g. a bool zero-extended, and drop the test's masking of eax).
template <class Fn> const void *Ours(Fn fn)
{
    const void *volatile p = reinterpret_cast<const void *>(fn);
    return p;
}

// Writes a word of the parser.
inline void Put(Side &s, size_t off, uint32_t v) { std::memcpy(s.parser->bytes + off, &v, 4); }
inline uint32_t Get(Side &s, size_t off) { return W(s.parser->bytes, off); }

// A vector at `off` made to hold `words` (a block from operator new, as the parser's own; its destructor frees it).
void PutVector(Side &s, size_t off, const std::vector<uint32_t> &words, size_t spare = 0)
{
    const size_t cap = words.size() + spare;
    auto *block = static_cast<uint32_t *>(cap ? ::operator new(cap * 4) : nullptr);
    for (size_t i = 0; i < words.size(); ++i) block[i] = words[i];
    Put(s, off, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(block)));
    Put(s, off + 4, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(block + words.size())));
    Put(s, off + 8, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(block + cap)));
}

uint32_t Addr(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// The face vtable of a side's parser replaced by a Fake (slot calls the body makes on its own face are logged).
void PatchFace(Side &s, Fake &vt) { Put(s, 0x1b8, Addr(vt.slots)); }

// Registers a command on a side with the original's slot 4 (setup only).
void Register(difftest::Context &ctx, Side &s, void *command)
{
    CallFace<void>(ctx.image.At(0xb059e0), s.parser->Face(), command);
}

const char *const kNames[] = {"alpha", "Alpha", "ALPHA", "beta", "gamma", "delta", "x", "Xy", "longer_command_name", "echo", "eChO", "set"};

} // namespace

// ---- the cIGZUnknown face ----

DIFF_TEST(cBaseParser_QueryInterface, 0xb05940)
{
    const uint32_t iids[] = {1, 0x95035f4, 0, 2, 0x95035f5, 0xffffffff};
    for (uint32_t iid : iids) {
        for (int withOut = 0; withOut < 2; ++withOut) {
            for (uint32_t count : {0u, 7u}) {
                uint32_t outA = 0xa5a5a5a5, outB = 0xa5a5a5a5;
                char what[64];
                std::snprintf(what, sizeof(what), "iid %x out %d count %u", iid, withOut, count);
                Compare(
                    ctx, what, [&](Side &s) { Put(s, 0xc, count); },
                    [&](Side &s) {
                        const uint32_t r = CallFace<uint8_t>(ctx.image.At(0xb05940), s.parser->UnknownFace(), iid, withOut ? &outA : nullptr);
                        s.extra = "out=" + Word(outA);
                        return r;
                    },
                    [&](Side &s) {
                        const uint32_t r = CallFace<uint8_t>(Ours(&cBaseParserAdapters::QueryInterface), s.parser->UnknownFace(), iid, withOut ? &outB : nullptr);
                        s.extra = "out=" + Word(outB);
                        return r;
                    });
            }
        }
    }
}

DIFF_TEST(cBaseParser_AddRef, 0xaff100)
{
    for (uint32_t count : {0u, 1u, 41u, 0xffffffffu}) {
        Compare(
            ctx, "AddRef", [&](Side &s) { Put(s, 0xc, count); },
            [&](Side &s) { return CallFace<uint32_t>(ctx.image.At(0xaff100), s.parser->UnknownFace()); },
            [&](Side &s) { return CallFace<uint32_t>(Ours(&cBaseParserAdapters::AddRef), s.parser->UnknownFace()); });
    }
}

// The last release deletes through primary slot 0 (a Fake here: logged with its flag argument); the primary vtable
// is put back before the parsers are freed.
DIFF_TEST(cBaseParser_Release, 0xb05c50)
{
    Fake primary(90);
    primary.SetArity<0>(1);
    for (uint32_t count : {1u, 2u, 9u, 0u}) {
        const uint32_t saved[1] = {Addr(ctx.image.At(0xe7db24))};
        Compare(
            ctx, "Release",
            [&](Side &s) {
                Put(s, 0xc, count);
                Put(s, 0, Addr(primary.slots));
            },
            [&](Side &s) {
                const uint32_t r = CallFace<uint32_t>(ctx.image.At(0xb05c50), s.parser->UnknownFace());
                s.extra = "vt=" + Word(Get(s, 0));
                Put(s, 0, saved[0]);
                return r;
            },
            [&](Side &s) {
                const uint32_t r = CallFace<uint32_t>(Ours(&cBaseParserAdapters::Release), s.parser->UnknownFace());
                s.extra = "vt=" + Word(Get(s, 0));
                Put(s, 0, saved[0]);
                return r;
            });
    }
}

// ---- the primary vtable: slot 4 ----

// The definitions (counted with slot 2, released with slot 1) whose level (+0x68) is the argument leave the vector;
// the others keep their order.
DIFF_TEST(cBaseParser_DropDefinitionsOfLevel, 0xb05c80)
{
    std::mt19937 gen(0xb05c80);
    for (int round = 0; round < 120; ++round) {
        Fakes fakes;
        const int n = round < 6 ? round : static_cast<int>(gen() % 14);
        std::vector<uint32_t> defs;
        for (int i = 0; i < n; ++i) defs.push_back(Addr(fakes.Add(i, "d" + std::to_string(i), static_cast<int32_t>(gen() % 3))));
        if (n && round % 5 == 0) defs.push_back(defs[0]); // the same definition twice
        if (n > 1 && round % 7 == 0) defs[1] = 0;          // a null element (not reached by the level test unless kept)
        const int32_t level = static_cast<int32_t>(gen() % 4);
        std::erase_if(defs, [](uint32_t d) { return d == 0; });
        const size_t spare = gen() % 3;
        Compare(
            ctx, "drop", [&](Side &s) { PutVector(s, 0x6c, defs, spare); },
            [&](Side &s) {
                CallFace<void>(ctx.image.At(0xb05c80), s.parser->bytes, level);
                return 0;
            },
            [&](Side &s) {
                CallFace<void>(Ours(&cBaseParserAdapters::DropDefinitionsOfLevel), s.parser->bytes, level);
                return 0;
            });
    }
}

// ---- the cIBaseParser face ----

DIFF_TEST(cBaseParser_SimpleSetters, 0xb06110)
{
    // 0xb06110 SetVersionRange, 0xb06230 SetContextState, 0xb02440 GetFunctions, 0xb03e20 CommandCount, 0xaff2e0
    // CommandAt, 0xb035b0 IsInitialised, 0xb035a0 IsCaseSensitive, 0xb04670 GetLine, 0xb068a0 GetSource.
    std::mt19937 gen(0xb06110);
    for (int round = 0; round < 40; ++round) {
        const int32_t lo = static_cast<int32_t>(gen()), hi = static_cast<int32_t>(gen());
        const uint32_t state = gen(), byte10 = gen() & 0xff, byte2c = gen() & 0xff, line = gen() % 3 ? gen() : 0;
        Fakes fakes;
        std::vector<uint32_t> list;
        for (int i = 0, n = static_cast<int>(gen() % 5); i < n; ++i) list.push_back(Addr(fakes.Add(i, "c")));
        auto setup = [&](Side &s) {
            s.parser->bytes[0x10] = static_cast<uint8_t>(byte10);
            s.parser->bytes[0x2c] = static_cast<uint8_t>(byte2c);
            Put(s, 0xe0, line);
            PutVector(s, 0x160, list, round % 3);
        };
        auto run = [&](Side &s, bool orig) {
            auto at = [&](uint32_t rva, const void *ours) { return orig ? ctx.image.At(rva) : ours; };
            void *face = s.parser->Face();
            std::string &x = s.extra;
            CallFace<void>(at(0xb06110, Ours(&cBaseParserAdapters::SetVersionRange)), face, lo, hi);
            CallFace<void>(at(0xb06230, Ours(&cBaseParserAdapters::SetContextState)), face, state);
            x += "functions=" + Word(Addr(CallFace<void *>(at(0xb02440, Ours(&cBaseParserAdapters::GetFunctions)), face))) + " ";
            const uint32_t count = CallFace<uint32_t>(at(0xb03e20, Ours(&cBaseParserAdapters::CommandCount)), face);
            x += "count=" + std::to_string(count) + " ";
            for (uint32_t i = 0; i < count; ++i) x += Word(Addr(CallFace<void *>(at(0xaff2e0, Ours(&cBaseParserAdapters::CommandAt)), face, i))) + ",";
            x += " init=" + std::to_string(CallFace<uint8_t>(at(0xb035b0, Ours(&cBaseParserAdapters::IsInitialised)), face));
            x += " case=" + std::to_string(CallFace<uint8_t>(at(0xb035a0, Ours(&cBaseParserAdapters::IsCaseSensitive)), face));
            x += " line=" + std::to_string(CallFace<uint32_t>(at(0xb04670, Ours(&cBaseParserAdapters::GetLine)), face));
            // The source is each parser's own heap object (its constructor's): checked to be the +0x1c word.
            const uint32_t source = Addr(CallFace<void *>(at(0xb068a0, Ours(&cBaseParserAdapters::GetSource)), face));
            x += " source=" + (source == Get(s, 0x1c) ? std::string("+1c") : Word(source)) + "\n";
            return 0;
        };
        Compare(ctx, "setters", setup, [&](Side &s) { return run(s, true); }, [&](Side &s) { return run(s, false); });
    }
}

DIFF_TEST(cBaseParser_TextGetters, 0xb035c0)
{
    // 0xb035c0 GetText (the game's "" when empty), 0xb04680 GetFileName (null unless a line and a name), 0xaff4d0
    // GetNamespacePath.
    const char *const texts[] = {"", "x", "fifteen chars..", "sixteen chars...", "a much longer text than sixteen characters"};
    for (const char *text : texts) {
        for (const char *file : texts) {
            for (uint32_t line : {0u, 3u}) {
                auto setup = [&](Side &s) {
                    reinterpret_cast<std::string *>(s.parser->bytes + 0x17c)->assign(text);
                    reinterpret_cast<std::string *>(s.parser->bytes + 0xc8)->assign(file);
                    reinterpret_cast<std::string *>(s.parser->bytes + 0x134)->assign(text);
                    Put(s, 0xe0, line);
                };
                auto run = [&](Side &s, bool orig) {
                    auto at = [&](uint32_t rva, const void *ours) { return orig ? ctx.image.At(rva) : ours; };
                    const uint8_t *base = s.parser->bytes;
                    void *face = s.parser->Face();
                    const char *t = CallFace<const char *>(at(0xb035c0, Ours(&cBaseParserAdapters::GetText)), face);
                    const char *f = CallFace<const char *>(at(0xb04680, Ours(&cBaseParserAdapters::GetFileName)), face);
                    const char *n = CallFace<const char *>(at(0xaff4d0, Ours(&cBaseParserAdapters::GetNamespacePath)), face);
                    char buf[96];
                    std::snprintf(buf, sizeof(buf), "%08x %08x %08x empty=%d", TextResult(t, base), TextResult(f, base), TextResult(n, base),
                                  t == ctx.image.At(0xd9b9d0));
                    s.extra = buf;
                    return 0;
                };
                Compare(ctx, "text getters", setup, [&](Side &s) { return run(s, true); }, [&](Side &s) { return run(s, false); });
            }
        }
    }
}

// Registration: the name (folded into the pool unless the parser is case sensitive) into the table, the command
// counted into the table slot and appended to the list (a temporary count around it), then SetContext(face, state).
DIFF_TEST(cBaseParser_RegisterCommand, 0xb059e0)
{
    std::mt19937 gen(0xb059e0);
    for (int round = 0; round < 150; ++round) {
        Fakes fakes;
        const int before = static_cast<int>(gen() % 12);
        std::vector<void *> objects;
        for (int i = 0; i < before; ++i) objects.push_back(fakes.Add(i, kNames[gen() % std::size(kNames)]));
        void *extra = fakes.Add(50, kNames[gen() % std::size(kNames)]);
        void *command = round % 6 == 0 && before ? objects[gen() % before] : extra; // sometimes one already there
        const uint8_t caseSensitive = round % 3 == 0;
        const uint32_t state = round % 4 == 0 ? Addr(fakes.Add(60)) : 0;
        auto setup = [&](Side &s) {
            s.parser->bytes[0x2c] = caseSensitive;
            for (void *o : objects) Register(ctx, s, o);
            Put(s, 0x68, state);
        };
        Compare(
            ctx, "register", setup,
            [&](Side &s) { CallFace<void>(ctx.image.At(0xb059e0), s.parser->Face(), command); return 0; },
            [&](Side &s) { CallFace<void>(Ours(&cBaseParserAdapters::RegisterCommand), s.parser->Face(), command); return 0; });
    }
}

// Unregistering by name: found in the table and in the list, the node leaves its bucket and the list closes the gap.
DIFF_TEST(cBaseParser_UnregisterCommand, 0xb072f0)
{
    std::mt19937 gen(0xb072f0);
    const char *const queries[] = {"alpha", "ALPHA", "beta", "Gamma", "nothere", "x", "XY", "longer_command_name", "echo", "set", ""};
    for (int round = 0; round < 200; ++round) {
        Fakes fakes;
        const int before = static_cast<int>(gen() % 12);
        std::vector<void *> objects;
        for (int i = 0; i < before; ++i) objects.push_back(fakes.Add(i, kNames[gen() % std::size(kNames)]));
        const uint8_t caseSensitive = round % 3 == 0;
        const bool dropList = round % 9 == 4; // in the table but not in the list
        const char *query = round % 4 == 0 && before ? fakes.names[gen() % before]->c_str() : queries[gen() % std::size(queries)];
        auto setup = [&](Side &s) {
            s.parser->bytes[0x2c] = caseSensitive;
            for (void *o : objects) Register(ctx, s, o);
            if (dropList) Put(s, 0x164, Get(s, 0x160));
        };
        Compare(
            ctx, "unregister", setup,
            [&](Side &s) { return CallFace<uint8_t>(ctx.image.At(0xb072f0), s.parser->Face(), query); },
            [&](Side &s) { return CallFace<uint8_t>(Ours(&cBaseParserAdapters::UnregisterCommand), s.parser->Face(), query); });
    }
}

// By object: the face's slot 5 (a Fake here, answering 0x80 or 1 or 0) with the command's name.
DIFF_TEST(cBaseParser_UnregisterCommandObject, 0xb072b0)
{
    for (uint32_t answer : {0u, 1u, 0x80u, 0x12345601u}) {
        Fakes fakes;
        void *command = fakes.Add(3, "named");
        Fake face(70);
        face.SetArity<5>(1);
        face.textArgs |= 1ull << 5;
        face.ret[5] = answer;
        Compare(
            ctx, "unregister object", [&](Side &s) { PatchFace(s, face); },
            [&](Side &s) {
                const uint32_t r = CallFace<uint32_t>(ctx.image.At(0xb072b0), s.parser->Face(), command);
                Put(s, 0x1b8, Addr(ctx.image.At(0xe7db4c)));
                return r & 0xff;
            },
            [&](Side &s) {
                const uint32_t r = CallFace<uint32_t>(Ours(&cBaseParserAdapters::UnregisterCommandObject), s.parser->Face(), command);
                Put(s, 0x1b8, Addr(ctx.image.At(0xe7db4c)));
                return r & 0xff;
            });
    }
}

// Meta commands: SetContext first, then the list, then (slot 4's function, slot 5's name) into the evaluator's sorted
// function table. The insert is 0xb09550: the original side runs the game's, ours runs InsertEvalFunction (its only
// caller), and the tables are compared, so this test covers it too.
DIFF_TEST(cBaseParser_RegisterMetaCommand, 0xb05b50)
{
    std::mt19937 gen(0xb05b50);
    const char *const names[] = {"abs", "zz", "AAA", "pow", "sqrt", "m", "strcmp", "", "~", "pox"};
    for (int round = 0; round < 80; ++round) {
        Fakes fakes;
        const int before = static_cast<int>(gen() % 6);
        std::vector<void *> objects;
        for (int i = 0; i < before; ++i) {
            objects.push_back(fakes.Add(i, names[gen() % std::size(names)]));
            fakes.vtables.back()->ret[4] = 0x7000 + i;
        }
        void *command = fakes.Add(40, names[gen() % std::size(names)]);
        fakes.vtables.back()->ret[4] = 0x7777;
        const uint32_t state = round % 3 == 0 ? Addr(fakes.Add(61)) : 0;
        auto setup = [&](Side &s) {
            for (void *o : objects) CallFace<void>(ctx.image.At(0xb05b50), s.parser->Face(), o);
            Put(s, 0x68, state);
        };
        Compare(
            ctx, "meta", setup,
            [&](Side &s) { CallFace<void>(ctx.image.At(0xb05b50), s.parser->Face(), command); return 0; },
            [&](Side &s) { CallFace<void>(Ours(&cBaseParserAdapters::RegisterMetaCommand), s.parser->Face(), command); return 0; });
    }
}

// Slot 8 is a tail call of the face's slot 9; slot 19 expands the text through face slot 29 and runs it with primary
// slot 1 (both Fakes here; slot 29's hook rewrites the string as the expansion would).
DIFF_TEST(cBaseParser_FaceCalls, 0xb05ea0)
{
    Fake face(71);
    face.SetArity<29>(2);
    face.hook = [](void *, int slot, const uint32_t *args) {
        if (slot != 29) return;
        auto *text = reinterpret_cast<std::string *>(static_cast<uintptr_t>(args[0]) + 4);
        gCalls.back().args += "[" + *text + "]";
        text->append(" expanded");
    };
    Fake primary(72);
    primary.SetArity<1>(2);
    primary.hook = [](void *, int slot, const uint32_t *args) {
        // The text is the expansion's own buffer (a local string: on the stack when short, on the heap when long):
        // logged by content, not by address.
        if (slot == 1) gCalls.back().args = Word(args[0]) + " [" + reinterpret_cast<const char *>(static_cast<uintptr_t>(args[1])) + "]";
    };
    face.stackArgs |= 1ull << 29;
    for (const char *text : {"", "short", "a text of more than sixteen characters"}) {
        auto setup = [&](Side &s) {
            PatchFace(s, face);
            Put(s, 0, Addr(primary.slots));
        };
        auto restore = [&](Side &s) {
            Put(s, 0x1b8, Addr(ctx.image.At(0xe7db4c)));
            Put(s, 0, Addr(ctx.image.At(0xe7db24)));
        };
        Compare(
            ctx, "face calls", setup,
            [&](Side &s) {
                CallFace<void>(ctx.image.At(0xb05ea0), s.parser->Face());
                CallFace<void>(ctx.image.At(0xb06350), s.parser->Face(), 0x1234u, text);
                restore(s);
                return 0;
            },
            [&](Side &s) {
                CallFace<void>(Ours(&cBaseParserAdapters::RestartViaReset), s.parser->Face());
                CallFace<void>(Ours(&cBaseParserAdapters::RunExpandedText), s.parser->Face(), 0x1234u, text);
                restore(s);
                return 0;
            });
    }
}

// A parser with something in every field Reset (slot 9) and ResetBlocks (slot 10) clear.
struct ClutterObjects {
    std::vector<uint32_t> defs;
    uint32_t current = 0;
    ClutterObjects(Fakes &fakes, int round)
    {
        for (int i = 0, n = round % 4; i < n; ++i) defs.push_back(Addr(fakes.Add(100 + i, "def")));
        if (round % 3) current = Addr(fakes.Add(110, "current"));
        // Pending calls (0x34 bytes: the definition, then a cArguments whose three vectors are empty, so its
        // destructor only stores its vtable); some without a definition.
        for (int i = 0, n = round % 5; i < n; ++i) pending.push_back(i == 2 ? 0 : Addr(fakes.Add(120 + i, "pending")));
    }
    std::vector<uint32_t> pending;
};

void Clutter(Side &s, const ClutterObjects &objects, std::mt19937 &gen, bool withNamespaces)
{
    PutVector(s, 0x6c, objects.defs, 1);
    std::vector<uint32_t> blocks;
    for (int i = 0, n = static_cast<int>(gen() % 4); i < n; ++i) blocks.push_back(gen());
    PutVector(s, 0xf8, blocks);
    PutVector(s, 0x104, blocks, 2);
    auto *conditions = reinterpret_cast<std::vector<cBaseParser::Condition> *>(s.parser->bytes + 0x110);
    for (int i = 0, n = static_cast<int>(gen() % 4); i < n; ++i) conditions->push_back({static_cast<int32_t>(gen() % 5), 1, 0, {}});
    if (gen() % 4 == 0) conditions->clear();
    reinterpret_cast<std::string *>(s.parser->bytes + 0x134)->assign(gen() % 2 ? "outer.inner" : "a namespace path longer than 15");
    std::vector<uint32_t> lengths{0, 5};
    PutVector(s, 0x128, lengths);
    if (withNamespaces) {
        auto *nss = reinterpret_cast<std::vector<void *> *>(s.parser->bytes + 0x14c);
        void *global = s.parser->bytes + 0x78;
        for (int i = 0, n = static_cast<int>(gen() % 3); i < n; ++i) nss->push_back(global);
        Put(s, 0x78 + 0x4c, 7);
    }
    Put(s, 0x15c, objects.current);
    std::vector<uint32_t> calls;
    for (uint32_t definition : objects.pending) {
        calls.push_back(definition);
        calls.push_back(0x5eed0000 + static_cast<uint32_t>(calls.size())); // the cArguments vtable word
        for (int w = 0; w < 11; ++w) calls.push_back(0);
    }
    PutVector(s, 0x11c, calls, 13);
    reinterpret_cast<std::string *>(s.parser->bytes + 0x17c)->assign("text in progress");
    reinterpret_cast<std::string *>(s.parser->bytes + 0xc8)->assign("file.txt");
    for (size_t off = 0xe0; off < 0xf8; ++off) s.parser->bytes[off] = static_cast<uint8_t>(gen());
    s.parser->bytes[0x198] = 1;
    Put(s, 0x158, gen());
}

DIFF_TEST(cBaseParser_Reset, 0xaff120)
{
    for (int round = 0; round < 60; ++round) {
        Fakes fakes;
        const ClutterObjects objects(fakes, round);
        Compare(
            ctx, "reset",
            [&](Side &s) {
                std::mt19937 g(round);
                Clutter(s, objects, g, true);
            },
            [&](Side &s) { CallFace<void>(ctx.image.At(0xaff120), s.parser->Face()); return 0; },
            [&](Side &s) { CallFace<void>(Ours(&cBaseParserAdapters::Reset), s.parser->Face()); return 0; });
    }
}

DIFF_TEST(cBaseParser_ResetBlocks, 0xb03e50)
{
    for (int round = 0; round < 60; ++round) {
        Fakes fakes;
        const ClutterObjects objects(fakes, round);
        Compare(
            ctx, "reset blocks",
            [&](Side &s) {
                std::mt19937 g(round);
                Clutter(s, objects, g, true);
            },
            [&](Side &s) { CallFace<void>(ctx.image.At(0xb03e50), s.parser->Face()); return 0; },
            [&](Side &s) { CallFace<void>(Ours(&cBaseParserAdapters::ResetBlocks), s.parser->Face()); return 0; });
    }
}

// Pushing blocks (slots 16 and 17; vectors growing) and popping meta blocks (slot 18; throws on an empty stack).
DIFF_TEST(cBaseParser_Blocks, 0xb068b0)
{
    std::mt19937 gen(0xb068b0);
    for (int round = 0; round < 60; ++round) {
        std::vector<int> ops;
        for (int i = 0, n = static_cast<int>(gen() % 20); i < n; ++i) ops.push_back(static_cast<int>(gen() % 3));
        std::vector<uint32_t> values;
        for (size_t i = 0; i < ops.size(); ++i) values.push_back(gen());
        auto run = [&](Side &s, bool orig) {
            auto at = [&](uint32_t rva, const void *ours) { return orig ? ctx.image.At(rva) : ours; };
            for (size_t i = 0; i < ops.size(); ++i) {
                if (ops[i] == 0) CallFace<void>(at(0xb068b0, Ours(&cBaseParserAdapters::PushOpenBlock)), s.parser->Face(), values[i]);
                else if (ops[i] == 1) CallFace<void>(at(0xb068f0, Ours(&cBaseParserAdapters::PushMetaBlock)), s.parser->Face(), values[i]);
                else CallFace<void>(at(0xaff880, Ours(&cBaseParserAdapters::PopMetaBlock)), s.parser->Face());
            }
            return 0;
        };
        Compare(ctx, "blocks", [](Side &) {}, [&](Side &s) { return run(s, true); }, [&](Side &s) { return run(s, false); });
    }
}

// Leaving a namespace: the trace lines (when +0xe9 is set), the path cut back to the length saved when it opened,
// the namespace's use count down and the stack popped.
DIFF_TEST(cBaseParser_PopNamespace, 0xb055b0)
{
    for (int round = 0; round < 24; ++round) {
        Fakes fakes;
        void *output = fakes.Add(80);
        const bool trace = round % 2;
        const char *path = round % 3 == 0 ? "outer.inner" : round % 3 == 1 ? "a namespace path longer than fifteen" : "x";
        const uint32_t cut = static_cast<uint32_t>(round / 6) * 1; // 0..3, all within the paths
        auto setup = [&](Side &s) {
            s.parser->bytes[0xe9] = trace;
            Put(s, 0x14, Addr(output));
            reinterpret_cast<std::string *>(s.parser->bytes + 0x134)->assign(path);
            PutVector(s, 0x128, {7, cut}, round % 2);
            auto *nss = reinterpret_cast<std::vector<void *> *>(s.parser->bytes + 0x14c);
            nss->push_back(s.parser->bytes + 0x78);
            Put(s, 0x78 + 0x4c, 3);
        };
        auto done = [&](Side &s) { Put(s, 0x14, 0); };
        Compare(
            ctx, "pop namespace", setup,
            [&](Side &s) { CallFace<void>(ctx.image.At(0xb055b0), s.parser->Face()); done(s); return 0; },
            [&](Side &s) { CallFace<void>(Ours(&cBaseParserAdapters::PopNamespace), s.parser->Face()); done(s); return 0; });
    }
}

// The counted members: output (24; null makes a cRZTextODSStream), handler (25), source (26; null makes a cFileSource).
DIFF_TEST(cBaseParser_CountedSetters, 0xb06250)
{
    for (int round = 0; round < 27; ++round) {
        Fakes fakes;
        void *objects[3] = {fakes.Add(1), fakes.Add(2), nullptr};
        void *first = objects[round % 3], *second = objects[round / 3 % 3];
        const int which = round / 9;
        const uint32_t rvas[3] = {0xb06250, 0xb06310, 0xb06140};
        const void *ours[3] = {Ours(&cBaseParserAdapters::SetOutput), Ours(&cBaseParserAdapters::SetHandler), Ours(&cBaseParserAdapters::SetSource)};
        auto setup = [&](Side &s) { CallFace<void>(ctx.image.At(rvas[which]), s.parser->Face(), first); };
        auto done = [&](Side &s) {
            // Objects made by the parser are left to it; the test's fakes are taken out before the parser is freed.
            for (size_t off : {0x14u, 0x18u, 0x1cu})
                if (FakeId(Get(s, off)) >= 0) Put(s, off, 0);
        };
        Compare(
            ctx, "counted setter", setup,
            [&](Side &s) { CallFace<void>(ctx.image.At(rvas[which]), s.parser->Face(), second); s.extra = Form(s.parser->bytes); done(s); return 0; },
            [&](Side &s) { CallFace<void>(ours[which], s.parser->Face(), second); s.extra = Form(s.parser->bytes); done(s); return 0; });
    }
}

// Shutdown (slot 1): nothing unless initialised; then the command table emptied, the face's Reset (the game's here),
// the output released and both command lists released and emptied.
DIFF_TEST(cBaseParser_Shutdown, 0xb06760)
{
    std::mt19937 gen(0xb06760);
    for (int round = 0; round < 40; ++round) {
        Fakes fakes;
        std::vector<void *> commands, metas;
        for (int i = 0, n = static_cast<int>(gen() % 6); i < n; ++i) commands.push_back(fakes.Add(i, kNames[gen() % std::size(kNames)]));
        for (int i = 0, n = static_cast<int>(gen() % 3); i < n; ++i) metas.push_back(fakes.Add(20 + i, "meta"));
        void *output = round % 3 ? fakes.Add(30) : nullptr;
        const uint8_t init = round % 4 != 0;
        auto setup = [&](Side &s) {
            // State Reset (slot 9, called through the face) clears.
            Put(s, 0xe0, 1000 + round);
            Put(s, 0xf0, 3);
            Put(s, 0xf4, 1);
            for (void *c : commands) Register(ctx, s, c);
            for (void *m : metas) {
                auto *list = reinterpret_cast<std::vector<void *> *>(s.parser->bytes + 0x16c);
                list->push_back(m);
            }
            Put(s, 0x14, Addr(output));
            s.parser->bytes[0x10] = init;
        };
        auto done = [&](Side &s) {
            if (FakeId(Get(s, 0x14)) >= 0) Put(s, 0x14, 0);
            Put(s, 0x170, Get(s, 0x16c)); // metas the test put in without counting
        };
        Compare(
            ctx, "shutdown", setup,
            [&](Side &s) { const uint32_t r = CallFace<uint8_t>(ctx.image.At(0xb06760), s.parser->Face()); s.extra = Form(s.parser->bytes); done(s); return r; },
            [&](Side &s) { const uint32_t r = CallFace<uint8_t>(Ours(&cBaseParserAdapters::Shutdown), s.parser->Face()); s.extra = Form(s.parser->bytes); done(s); return r; });
    }
}

// The definitions' order: shorter names first, then by the bytes from the last one backwards (signed).
bool DefinitionLess(const std::string &a, const std::string &b)
{
    if (a.size() != b.size()) return a.size() < b.size();
    for (size_t i = a.size(); i-- > 0;)
        if (static_cast<signed char>(a[i]) != static_cast<signed char>(b[i])) return static_cast<signed char>(a[i]) < static_cast<signed char>(b[i]);
    return false;
}

DIFF_TEST(cBaseParser_HasDefinitionAndCommand, 0xaff4f0)
{
    // 0xaff4f0 HasDefinition (binary search over the sorted definitions), 0xaff300 HasCommand (_stricmp over the list).
    std::mt19937 gen(0xaff4f0);
    const char *const pool[] = {"a", "b", "ab", "ba", "abc", "cba", "zz", "\xe9t\xe9", "Long_name", "long_name", "x1", "1x", "echo"};
    for (int round = 0; round < 100; ++round) {
        Fakes fakes;
        std::vector<std::string> names;
        for (int i = 0, n = static_cast<int>(gen() % 8); i < n; ++i) names.push_back(pool[gen() % std::size(pool)]);
        std::sort(names.begin(), names.end(), DefinitionLess);
        names.erase(std::unique(names.begin(), names.end()), names.end());
        std::vector<uint32_t> defs;
        std::vector<void *> commands;
        for (size_t i = 0; i < names.size(); ++i) {
            defs.push_back(Addr(fakes.Add(static_cast<int>(i), names[i])));
            commands.push_back(fakes.objects.back().get());
        }
        auto setup = [&](Side &s) {
            PutVector(s, 0x6c, defs);
            for (void *c : commands) Register(ctx, s, c);
        };
        auto run = [&](Side &s, bool orig) {
            auto at = [&](uint32_t rva, const void *ours) { return orig ? ctx.image.At(rva) : ours; };
            for (const char *q : pool) {
                s.extra += std::to_string(CallFace<uint8_t>(at(0xaff4f0, Ours(&cBaseParserAdapters::HasDefinition)), s.parser->Face(), q));
                s.extra += std::to_string(CallFace<uint8_t>(at(0xaff300, Ours(&cBaseParserAdapters::HasCommand)), s.parser->Face(), q));
            }
            for (const char *q : {"A", "ECHO", "LONG_NAME", ""}) s.extra += std::to_string(CallFace<uint8_t>(at(0xaff300, Ours(&cBaseParserAdapters::HasCommand)), s.parser->Face(), q));
            Put(s, 0x70, Get(s, 0x6c)); // the test's definitions, not counted
            return 0;
        };
        Compare(ctx, "has", setup, [&](Side &s) { return run(s, true); }, [&](Side &s) { return run(s, false); });
    }
}

// 0xafcc10: the counted-pointer copy, on its own (overlapping ranges as the callers make them, equal pointers, nulls).
DIFF_TEST(cBaseParser_MoveCountedPointers, 0xafcc10)
{
    std::mt19937 gen(0xafcc10);
    auto orig = ctx.Original<void **(__cdecl *)(void **, void **, void **)>(0xafcc10);
    for (int round = 0; round < 300; ++round) {
        Fakes fakes;
        void *objs[4] = {fakes.Add(1), fakes.Add(2), fakes.Add(3), nullptr};
        const int n = static_cast<int>(gen() % 10);
        void *a[12], *b[12];
        for (int i = 0; i < 12; ++i) a[i] = b[i] = objs[gen() % 4];
        const int from = static_cast<int>(gen() % (n + 1)), to = from + static_cast<int>(gen() % (n - from + 1));
        const int dest = static_cast<int>(gen() % (from + 1));
        gCalls.clear();
        void **ra = orig(a + from, a + to, a + dest);
        const std::string la = Describe(gCalls);
        gCalls.clear();
        void **rb = nGZCommandParser::MoveCountedPointers(b + from, b + to, b + dest);
        const std::string lb = Describe(gCalls);
        ++ctx.cases;
        if (ra - a != rb - b || la != lb || std::memcmp(a, b, sizeof(a)) != 0)
            ctx.Fail("round %d: result %d/%d calls\n  %s\n  %s", round, static_cast<int>(ra - a), static_cast<int>(rb - b), la.c_str(), lb.c_str());
    }
}
