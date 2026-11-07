#pragma once
// Recorders for the log parser's virtual calls (lane HP; tests/gz_log_parser.cpp, tests/static_init_parser.cpp).
//
// The parser's calls go to recorders: either the game's cBaseParser face vtables are patched for the test
// (PatchedParserVTables: cIBaseParser 0xe7db4c slots 0, 1, 3, 4; cIGZUnknown 0xe7db3c slots 1, 2), so a parser built
// by either side's constructor calls them, or a fake parser carries recording vtables (FakeParser). The log holds each
// call's slot, receiver and argument; Normal() turns them into offsets into that side's parser or "the state". An added
// command is logged as its 16 bytes (the vtable as an RVA) and freed by the recorder.
#include "difftest.h"
#include "gz/cGZLogParser.h"

#include <windows.h>

#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace logfakes {

inline uint32_t Addr(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

struct Call {
    uint32_t slot;
    uint32_t self;
    uint32_t arg;
    uint32_t command[4]; // slot 4: the command's words
};
inline std::vector<Call> gCalls;
inline uint32_t gImageBase;

inline void Record(uint32_t slot, void *self, uint32_t arg) { gCalls.push_back({slot, Addr(self), arg, {}}); }

constexpr uint32_t kAddRef = 0x101, kRelease = 0x102;
inline uint32_t __fastcall UnknownAddRef(void *self, void *)
{
    Record(kAddRef, self, 0);
    return 2;
}
inline uint32_t __fastcall UnknownRelease(void *self, void *)
{
    Record(kRelease, self, 0);
    return 1;
}
inline uint32_t __fastcall ParserSlot0(void *self, void *, uint32_t a)
{
    Record(0, self, a);
    return 0;
}
inline void __fastcall ParserSlot1(void *self, void *) { Record(1, self, 0); }
inline void __fastcall ParserSlot3(void *self, void *, void *state) { Record(3, self, Addr(state)); }
inline void __fastcall ParserSlot4(void *self, void *, void *command)
{
    Record(4, self, Addr(command));
    if (command) {
        std::memcpy(gCalls.back().command, command, 16);
        gCalls.back().command[0] -= gImageBase;
        ::operator delete(command);
    }
}

// The calls as offsets: a receiver or argument inside `parser` (0x200 bytes from it) as 0x10000 + offset, the state
// as 0x5000, null as 0, anything else as 0xbad. Slot 0's argument is a plain value; slot 4's is "a command or null".
inline std::vector<uint32_t> Normal(const std::vector<Call> &calls, const void *parser, const void *state)
{
    auto map = [&](uint32_t p) -> uint32_t {
        if (p == 0) return 0;
        if (p == Addr(state)) return 0x5000;
        if (parser && p >= Addr(parser) && p < Addr(parser) + 0x200) return 0x10000 + (p - Addr(parser));
        return 0xbad;
    };
    std::vector<uint32_t> out;
    for (const Call &c : calls) {
        out.push_back(c.slot);
        out.push_back(map(c.self));
        out.push_back(c.slot == 4 ? (c.arg != 0) : c.slot == 0 ? c.arg : map(c.arg));
        out.insert(out.end(), c.command, c.command + 4);
    }
    return out;
}
constexpr size_t kWordsPerCall = 7;

// Overwrites a dword of the game's data (a vtable slot) for the scope of the test.
struct DataPatch {
    uint32_t *site;
    uint32_t saved;
    DataPatch(void *at, const void *value) : site(static_cast<uint32_t *>(at))
    {
        DWORD old;
        VirtualProtect(site, 4, PAGE_READWRITE, &old);
        saved = *site;
        *site = Addr(value);
    }
    DataPatch(const DataPatch &) = delete;
    DataPatch &operator=(const DataPatch &) = delete;
    ~DataPatch() { *site = saved; }
};

struct PatchedParserVTables {
    uint32_t *parserVt, *unknownVt;
    DataPatch p0, p1, p3, p4, u1, u2;
    explicit PatchedParserVTables(const difftest::Context &ctx)
        : parserVt(ctx.image.At<uint32_t>(nGZCommandParser::cBaseParser::kParserVTable)),
          unknownVt(ctx.image.At<uint32_t>(nGZCommandParser::cBaseParser::kUnknownVTable)),
          p0(parserVt + 0, reinterpret_cast<const void *>(&ParserSlot0)),
          p1(parserVt + 1, reinterpret_cast<const void *>(&ParserSlot1)),
          p3(parserVt + 3, reinterpret_cast<const void *>(&ParserSlot3)),
          p4(parserVt + 4, reinterpret_cast<const void *>(&ParserSlot4)),
          u1(unknownVt + 1, reinterpret_cast<const void *>(&UnknownAddRef)),
          u2(unknownVt + 2, reinterpret_cast<const void *>(&UnknownRelease))
    {
        gImageBase = static_cast<uint32_t>(ctx.image.Base());
    }
};

// The state's id and its std::string's 24 bytes (a default-constructed one has no heap block).
inline std::vector<uint8_t> StateForm(const nGZCommandParser::cGZLogParserState *s)
{
    std::vector<uint8_t> out(reinterpret_cast<const uint8_t *>(s), reinterpret_cast<const uint8_t *>(s) + 4);
    const auto *name = reinterpret_cast<const uint8_t *>(&s->mName);
    out.insert(out.end(), name, name + sizeof(std::string));
    return out;
}

// A fake parser for RegisterCommands and the destructor: a vbtable at +8 placing the cIGZUnknown face at +0x18 and the
// cIBaseParser face at +0x20, each holding a recording vtable.
struct FakeParser {
    uint8_t bytes[0x40];
};
inline const int32_t kFakeVbTable[3] = {-8, 0x10, 0x18};
inline void *const kFakeUnknownVTable[3] = {nullptr, reinterpret_cast<void *>(&UnknownAddRef), reinterpret_cast<void *>(&UnknownRelease)};
inline void *const kFakeParserVTable[5] = {reinterpret_cast<void *>(&ParserSlot0), reinterpret_cast<void *>(&ParserSlot1), nullptr,
                                           reinterpret_cast<void *>(&ParserSlot3), reinterpret_cast<void *>(&ParserSlot4)};
inline void BuildFake(FakeParser &f, std::mt19937 &gen)
{
    for (auto &c : f.bytes) c = static_cast<uint8_t>(gen());
    const uint32_t vb = Addr(kFakeVbTable), unknown = Addr(kFakeUnknownVTable), parser = Addr(kFakeParserVTable);
    std::memcpy(f.bytes + 8, &vb, 4);
    std::memcpy(f.bytes + 0x18, &unknown, 4);
    std::memcpy(f.bytes + 0x20, &parser, 4);
}

} // namespace logfakes
