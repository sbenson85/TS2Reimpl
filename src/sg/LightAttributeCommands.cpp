// The light attribute commands (lane 261108h, scenegraph LG3). See src/sg/LightAttributeCommands.h.
#include "sg/LightAttributeCommands.h"

#include "gz/MaterialCommands.h" // ParserFace, SetContextWithChildren
#include "gz/cArguments.h"

#include "game/game.h"
#include "game/vtable.h"

#include <cstring>
#include <iterator>
#include <string.h> // _stricmp

namespace cp = nGZCommandParser;
using nGZSceneGraph::BlockChild;
using nGZSceneGraph::ChildBase;
using nGZSceneGraph::ParserFace;

namespace nTSSG {

namespace {

const char *Text(uint32_t rva) { return GameAddress<char>(rva); }

// The parser face's slot 28: the namespace path the open blocks make.
const char *NamespacePath(void *face) { return VirtualCall<const char *(__thiscall *)(void *), 28>(face); }

void SetAttribute(void *attributes, void *face, const char *settings, const char *attribute, const char *value)
{
    VirtualCall<void(__thiscall *)(void *, void *, const char *, const char *, const char *), 7>(attributes, face, settings,
                                                                                                attribute, value);
}

} // namespace

void cLightAttribOverrideCommand::Execute(cp::cIArguments *args)
{
    const char *const *argv = args->PositionalExactly(3);
    std::string settings(NamespacePath(mParser));
    settings.append(argv[0], std::strlen(argv[0]));
    auto *state = static_cast<cLightingParserStateFields *>(mState);
    SetAttribute(state->mAttributes, mParser, settings.c_str(), argv[1], argv[2]);
}

const char *cLightAttribOverrideCommand::GetName() const { return Text(0xdb6790); }

void cLightAttributeCommand::Execute(cp::cIArguments *args)
{
    const char *const *argv = args->PositionalExactly(2);
    auto *state = static_cast<cLightingParserStateFields *>(mState);
    SetAttribute(state->mAttributes, mParser, state->mSettingsName.c_str(), argv[0], argv[1]);
}

void cLightAttributesCommand::SetContext(void *parser, void *stateInterface)
{
    static constexpr BlockChild kChildren[] = {{cLightAttributeCommand::kVTable, ChildBase::Command}};
    nGZSceneGraph::SetContextWithChildren(this, parser, stateInterface, kChildren, std::size(kChildren));
}

void cLightAttributesCommand::Execute(cp::cIArguments *args)
{
    uint32_t count;
    const char *const *argv = args->Positional(&count, 1, 3);
    std::string &settings = State()->mSettingsName;
    const char *path = NamespacePath(mParser);
    settings.assign(path, std::strlen(path));
    settings.append(argv[0], std::strlen(argv[0]));
    if (static_cast<int32_t>(count) > 1) {
        if (_stricmp(argv[1], Text(0xda9e7c)) != 0) cp::ThrowFormatted(Text(0xdb6824));    // "Expecting ':'"
        if (static_cast<int32_t>(count) < 3) cp::ThrowFormatted(Text(0xdb6834)); // "Expecting light settings name"
        VirtualCall<void(__thiscall *)(void *, const char *, const char *), 8>(State()->mAttributes, argv[2],
                                                                               State()->mSettingsName.c_str());
    }
    ParserFace(ParserFace::Direct{}, mParser).PushOpenBlock(this);
}

const char *cLightAttributesCommand::GetName() const { return Text(0xdb6808); }

} // namespace nTSSG
