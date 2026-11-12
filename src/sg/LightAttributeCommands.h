#pragma once
// The light attribute commands of the scene graph's lighting script (lane 261108h, scenegraph LG3): three commands of
// nTSSG::cLightingParserState in an anonymous namespace of the lighting manager's translation unit
// (`.?AUcLightAttribOverrideCommand@?A0xa5581f72@@` and so on); here in nTSSG. RVAs are 1.18.0.187.
//
// Hook shape, as the lighting state's commands (gz/LightingCommands.h): the parser (not ours) creates them with the
// game's vtables, so each slot is an RVA hook. What they reach of the parser state: the object at +0x404 (class not
// identified) whose slot 7 (parser face, settings name, attribute, value) takes one attribute and slot 8 (base, settings
// name) a block's `: base`; the std::string at +0x40c names the open lightAttributes block's settings (the namespace
// path and the block's name).
#include "gz/CommandParser.h"

#include <cstdint>
#include <string>

namespace nGZCommandParser {
class cIArguments;
}

namespace nTSSG {

// The lighting parser state as these commands use it (nTSSG::cLightingParserState; its start is the lighting parser,
// gz/LightingCommands.h cTSLightingParserFields).
struct cLightingParserStateFields {
    uint8_t mFields000[0x404];
    void *mAttributes;        // +0x404 slots 7 and 8 (above)
    uint8_t mFields408[0x40c - 0x408];
    std::string mSettingsName; // +0x40c
};
static_assert(offsetof(cLightingParserStateFields, mAttributes) == 0x404 &&
                  offsetof(cLightingParserStateFields, mSettingsName) == 0x40c,
              "cLightingParserStateFields layout");

// `lightAttribOverride name attribute value` (vtable 0xdb6768, cCommandT, 16 bytes): sets an attribute of the light
// settings named by the namespace path (the parser face's slot 28) followed by `name`.
class cLightAttribOverrideCommand : public nGZCommandParser::cCommandT {
public:
    static constexpr uint32_t kVTable = 0xdb6768;
    void Execute(nGZCommandParser::cIArguments *args); // slot 4, 0x2d4e80
    const char *GetName() const;                       // slot 5, 0x2df260: "lightAttribOverride"
};

// `attribute name value` inside a lightAttributes block (vtable 0xdb67a8, cCommandT, 16 bytes): sets an attribute of
// the block's settings (the state's +0x40c). Its slot 5 (shared_vf_2df270, "attribute") is folded with the material
// script's attribute command and is not hooked.
class cLightAttributeCommand : public nGZCommandParser::cCommandT {
public:
    static constexpr uint32_t kVTable = 0xdb67a8;
    void Execute(nGZCommandParser::cIArguments *args); // slot 4, 0x2d4f90
};

// `lightAttributes name [: base]` (vtable 0xdb67d4, cBlockCommandT, 0x44 bytes): opens a block of attribute commands
// for the settings named by the namespace path followed by `name` (stored at the state's +0x40c); with `: base` the
// attributes object's slot 8 gets (base, that name). A second word other than ":" (case-insensitive) throws "Expecting
// ':'", a colon without a base "Expecting light settings name"; the block is pushed last.
class cLightAttributesCommand : public nGZCommandParser::cBlockCommandT {
public:
    static constexpr uint32_t kVTable = 0xdb67d4;
    cLightingParserStateFields *State() const { return static_cast<cLightingParserStateFields *>(mState); }
    void SetContext(void *parser, void *stateInterface); // slot 3, 0x2df3b0: one attribute command
    void Execute(nGZCommandParser::cIArguments *args);   // slot 4, 0x2d4fd0
    const char *GetName() const;                         // slot 5, 0x2df280: "lightAttributes"
};

} // namespace nTSSG
