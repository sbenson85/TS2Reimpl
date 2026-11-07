// cMacroDefinition's own vtable (lane RU): what an image-free program's definitions store in place of the image's
// 0xe7db0c (standalone/owned_statics.cpp aliases ts2_rva_e7db0c to it). The image's 5 slots in order (db/classes.jsonl):
// slots 0 and 1 are the functions the image's entries are hooked to, slots 2-4 the members doing what the image's
// folded bodies do (tests/gz_macro_definition_table.cpp). RVAs are 1.18.0.187.
#include "gz/MacroDefinition.h"

#include <cstring>

// Filled by a dynamic initialiser (a member function's address is no constant expression) before any code of the
// program runs, as the image's table is there before its code.
#pragma warning(disable : 4073) // initializers put in library initialization area: intended
#pragma init_seg(lib)

namespace {

template <typename T> const void *Member(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance members only");
    const void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

} // namespace

using nGZCommandParser::cMacroDefinition;

extern "C" const void *ts2_own_vt_e7db0c[5] = {
    Member(&cMacroDefinition::DeletingDestructor), // 0 0xaff0d0 (hooked)
    Member(&cRZRefCount::Release),                 // 1 0x1b94af (hooked)
    Member(&cMacroDefinition::AddRef),             // 2 0x148fb0 (folded)
    Member(&cMacroDefinition::RemoveRef),          // 3 0x1b9538 (folded)
    Member(&cMacroDefinition::RefCount),           // 4 0x147a30 (folded)
};
