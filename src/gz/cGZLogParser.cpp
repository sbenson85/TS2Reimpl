#include "gz/cGZLogParser.h"

#include "game/game.h"
#include "game/vtable.h"

#include <cstring>
#include <new>

namespace nGZCommandParser {

namespace {

using UnknownFn = uint32_t(__thiscall *)(void *);
using SetStateFn = void(__thiscall *)(void *, void *);
using AddCommandFn = void(__thiscall *)(void *, void *);
using RegisterBuiltinsFn = void(__thiscall *)(void *, uint32_t);
using ShutdownFn = void(__thiscall *)(void *);

// cIGZUnknown slots 1 and 2 on the parser's cIGZUnknown face.
void AddRef(cBaseParser *parser) { VirtualCall<UnknownFn, 1>(parser->UnknownFace()); }
void Release(cBaseParser *parser) { VirtualCall<UnknownFn, 2>(parser->UnknownFace()); }

} // namespace

void cGZLogParser::SetParser(cBaseParser *parser)
{
    cBaseParser *previous = mParser;
    if (parser == previous) return;
    if (parser) AddRef(parser);
    mParser = parser;
    if (previous) Release(previous);
}

cGZLogParser::cGZLogParser() : mParser(nullptr)
{
    mState = new cGZLogParserState;
    void *memory = ::operator new(sizeof(cBaseParser));
    SetParser(memory ? static_cast<cBaseParser *>(memory)->Construct(1) : nullptr);
    VirtualCall<RegisterBuiltinsFn, 0>(mParser->ParserFace(), 0u);
    VirtualCall<SetStateFn, 3>(mParser->ParserFace(), mState);
    RegisterCommands(mParser ? mParser->ParserFace() : nullptr);
}

cGZLogParser::~cGZLogParser()
{
    VirtualCall<ShutdownFn, 1>(mParser->ParserFace());
    if (cBaseParser *parser = mParser) {
        mParser = nullptr;
        Release(parser);
    }
    delete mState;
    // The reference member's own destructor: a release when it is set (it is null here).
    if (mParser) Release(mParser);
}

void cGZLogParser::RegisterCommands(void *parser)
{
    VirtualCall<SetStateFn, 3>(parser, mState);
    for (uint32_t vtable : kCommandVTables) {
        void *memory = ::operator new(sizeof(cCommandT));
        cCommand *command = nullptr;
        if (memory) {
            std::memset(memory, 0, sizeof(cCommandT));
            command = static_cast<cCommand *>(memory)->Construct();
            *static_cast<const void **>(memory) = GameAddress(vtable);
        }
        VirtualCall<AddCommandFn, 4>(parser, command);
    }
    VirtualCall<SetStateFn, 3>(parser, nullptr);
}

cGZLogParser *__fastcall cGZLogParserAdapters::Construct(void *memory, void *) { return ::new (memory) cGZLogParser; }

void __fastcall cGZLogParserAdapters::Destruct(cGZLogParser *self, void *) { self->~cGZLogParser(); }

} // namespace nGZCommandParser
