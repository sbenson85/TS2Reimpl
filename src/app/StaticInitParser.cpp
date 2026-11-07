#include "app/StaticInitParser.h"

#include "app/StaticInit.h"
#include "game/game.h"
#include "gz/EvalFunctionTable.h"
#include "gz/cGZLogParser.h"

#include <array>
#include <iterator>
#include <memory>
#include <new>

namespace staticinit {

namespace {

using nGZCommandParser::cGZLogParser;
using nGZCommandParser::EvalFunctionTable;

constexpr uint32_t kEvalTableSlot = 675, kLogParserSlot = 773;

constexpr ParserStatic kParsers[] = {
    {kEvalTableSlot, 0x13ff80, 0x10fb30c, 0xd97450},
    {kLogParserSlot, 0x1409f0, 0x1107510, 0xd98bc0},
};

void __cdecl DestroyEvalTable() { std::destroy_at(GameAddress<EvalFunctionTable>(kParsers[0].storage)); }
void __cdecl DestroyLogParser() { std::destroy_at(GameAddress<cGZLogParser>(kParsers[1].storage)); }

constexpr auto kOwned = [] {
    std::array<winmainstart::OwnedInitialiser, std::size(kParsers)> out{};
    size_t n = 0;
    for (const auto &e : kParsers) out[n++] = {e.slot, e.imageRva, &InitParserStatic, &e};
    return out;
}();

} // namespace

void __cdecl InitParserStatic(const void *parserStatic)
{
    const auto &e = *static_cast<const ParserStatic *>(parserStatic);
    if (e.slot == kEvalTableSlot) {
        ::new (GameAddress<void>(e.storage)) EvalFunctionTable;
        BoundAtExit()(&DestroyEvalTable);
    } else {
        ::new (GameAddress<void>(e.storage)) cGZLogParser;
        BoundAtExit()(&DestroyLogParser);
    }
}

const ParserStatic *ParserStatics(size_t *count)
{
    *count = std::size(kParsers);
    return kParsers;
}

const winmainstart::OwnedInitialiser *OwnedParsers(size_t *count)
{
    *count = kOwned.size();
    return kOwned.data();
}

} // namespace staticinit
