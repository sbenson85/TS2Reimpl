#pragma once
#include "rz/nGZLog.h"
#include "ts/cTSAppCommonImpl.h"

#include <cstdint>
#include <list>

// The foundations campaign's last five helpers (lane WG; docs/subsystems/foundations.md, the FO row). Each is an
// out-of-line copy of code our callers already write inline (all their callers are verified and ours), so these are
// in effect for whatever else might still reach them. RVAs 1.18.0.187.
//
// Three are STL-shaped (a pair constructor, unordered_map::find, std::find) over the game's own element types; the
// stlprobe objects carry no instantiation over those types, so they are treated as game code (as TreeEraser in
// rz/MiscHelpersA.h), not as library matches.

// A held cIGZUnknown reference: one pointer, released through slot 2.
struct ServiceRefHolder {
    void *mObject;

    // 0x99e700 (__thiscall, plain ret): when mObject is set, its Release (slot 2) inside `try { } catch (...) {}`
    // built with /EHa (FuncInfo flags 0), so both C++ and SEH exceptions from Release are swallowed. mObject is not
    // cleared. Callers: cGZDBSegmentDirectoryFiles::Open and cTSAppCommonImpl's registry/section handlers.
    void Destroy();

    // 0xc260bc (__thiscall, plain ret): mObject cleared, then the framework service object (0x1427b5, through the
    // thunk 0x14323f) asked for id 0x9b3798f7 / iid 0xfb3798e8 through its slot 5, output mObject (slot 5's result
    // unused). Returns this. The only caller is cTSAppFW::Init (0xc27c3e), for the +0x80 delegate.
    ServiceRefHolder *AcquireSettingsDelegate();
};
static_assert(sizeof(ServiceRefHolder) == 4, "ServiceRefHolder: one pointer");

// 0x141533's 12-byte value: two words copied from the key, then a held reference. cGZFramework::AddSystemService
// (0x14234f) builds one from {the service's priority-container word, GetServiceID} and a null reference and inserts it
// with 0x141681.
struct KeyRefPair {
    uint32_t mKey[2]; // +0x00
    void *mRef;       // +0x08 AddRef'd (slot 1) when not null

    // 0x141533 (__thiscall, ret 8): the pair(key, ref) constructor. Returns this.
    KeyRefPair *Construct(const uint32_t *key, void *const *ref);
};
static_assert(sizeof(KeyRefPair) == 12, "KeyRefPair: key words, reference");

namespace nGZLog {

// 0xc9bff9 (__thiscall on the table, ret 8): unordered_map<LogString, ...>::find(key) of the log tables (the level
// table and the alias table share it: find does not touch the mapped value). The key's text (slot 5) is hashed with
// HashChars (0x9cbcdb), the bucket searched (0x97b024), and *out set to the node, or to end() (the list head, +4)
// when the key is absent. Returns out. Callers: cLogFilterGroupLevels::GroupLevel (0xc9d322), LookupAlias (0xc9d452).
LevelMap::iterator *__fastcall FindLogTableEntry(LevelMap *self, void *edx, LevelMap::iterator *out, const LogString &key);

} // namespace nGZLog

// 0xca4ac0 (__cdecl, plain ret): std::find over cTSAppCommonImpl's std::list<OptionEntry> nodes from first up to last,
// testing OptionEntry::operator== (the masks, then the searched-for entry's text IsEqual, slot 11, case-sensitive).
// *out is the first match or last. Returns out. The only caller is cTSAppCommonImpl::AddOption (0xca7230).
using OptionEntryNode = std::_List_node<ts::OptionEntry, void *>;
OptionEntryNode **__cdecl FindOptionEntry(OptionEntryNode **out, OptionEntryNode *first, OptionEntryNode *last,
                                          const ts::OptionEntry &value);
