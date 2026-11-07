#pragma once
// Helpers the legacy script writer's class writers share (foundations FI..FL): the stack-built name and value texts, the
// yes/no flag attribute and the window behind a face. Only the writers' .cpp files include this.
#include "gz/cGZLegacyControl.h" // LegacyText
#include "gz/cGZLegacyScriptWriter.h"
#include "ifaces/cIGZWin.h"

namespace legacy_script {

// An attribute name or value as the writers build them: a cRZString local of the text.
struct Text : LegacyText {
    explicit Text(const char *s) { mText.FromChar(s); }
};
inline const cRZString *Of(const LegacyText &t) { return &t.mText; }

// "name=yes " or "name=no ".
inline void FlagAttribute(cRZString *out, const char *name, bool set)
{
    Text value(set ? "yes" : "no");
    AppendTextAttribute(out, Of(Text(name)), Of(value));
}

inline ifaces::cIGZWin *AsWin(void *window) { return static_cast<ifaces::cIGZWin *>(window); }

} // namespace legacy_script
