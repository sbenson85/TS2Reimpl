#pragma once
// cIGZFontSys (iid 0x673960; 24 slots): the font system, implemented by cGZFontSys (src/gz/cGZFontSys.h; lane OA,
// docs/subsystems/rendering.md R5b). It keeps the fonts the game has loaded, by font file, and the family names,
// default faces and aliases, and loads fonts on request. Slot names are tentative, from what the one implementation does.
#include "gz/cIGZFont.h"
#include "gz/cIGZFontRenderer.h"
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"

#include <cstdint>

// One entry of ListFamilies (0x88 bytes).
struct cGZFontFamilyInfo {
    char name[0x80]; // the family name, cut at 0x7f characters
    uint32_t style;  // +0x80
    uint32_t group;  // +0x84
};
static_assert(sizeof(cGZFontFamilyInfo) == 0x88, "cGZFontFamilyInfo: 0x88 bytes");

class cIGZFontSys : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x673960;
    static constexpr uint32_t kServiceId = 0x20673960;

    // 3 and 4 are also cIGZSystemService's Init and Shutdown (slots 8 and 9): one body each.
    virtual bool Init() = 0;                                              // 3
    virtual bool Shutdown() = 0;                                          // 4
    virtual bool SetEncoding(uint32_t encoding) = 0;                      // 5 (shared 0x1f5250: stores, true)
    virtual void SetFontRenderer(cIGZFontRenderer *renderer) = 0;         // 6
    virtual void SetQuality(uint32_t mode) = 0;                           // 7 (shared 0x20daf0)
    // 8: sizes lo <= size <= hi (family null: the default range) are drawn in mode 1; lo >= hi forgets a family's range.
    virtual void SetSizeRange(uint32_t low, uint32_t high, const cIGZString *family) = 0;
    virtual cIGZFont *GetFontByGroup(uint32_t group, uint32_t size, uint32_t style, uint32_t colour, bool shared) = 0; // 9
    virtual cIGZFont *LoadFont(const cIGZString *name, uint32_t size, uint32_t style, bool shared) = 0;                // 10
    virtual cIGZFont *FindFont(const cIGZString *names, uint32_t group, uint32_t size, uint32_t style, bool shared) = 0; // 11
    virtual uint32_t ReleaseFont(cIGZFont *font) = 0;                     // 12
    virtual bool AddFont(cIGZFont *font) = 0;                             // 13
    virtual bool AddFontFile(const cIGZString *path) = 0;                 // 14
    virtual uint32_t AddFontDirectory(const cIGZString *directory) = 0;   // 15
    virtual bool vf16(uint32_t) = 0;                                      // 16 (shared 0x18a7a0: false)
    virtual bool RemoveFamily(const cIGZString *name) = 0;                // 17 (always false)
    virtual uint32_t ListFamilies(cGZFontFamilyInfo *out, uint32_t count) = 0; // 18
    virtual uint32_t ListFonts(cIGZFont **out, uint32_t capacity) = 0;    // 19
    virtual bool AddAlias(const cIGZString *name, const cIGZString *alias) = 0; // 20
    virtual uint32_t GetAliases(const cIGZString *name, cIGZString **out, uint32_t count) = 0; // 21
    virtual bool HasFamily(const cIGZString *name, uint32_t unused, uint32_t style) = 0; // 22
    virtual bool ResolveAlias(const cIGZString *name, cIGZString *out) = 0; // 23
    // slot 24: the deleting destructor
};
