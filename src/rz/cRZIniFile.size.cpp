#include "rz/cRZIniFile.h"
#include "game/vtable.h"
#include "rz/cRZFile.h"

#include <cstdio>
#include <new>

namespace {

// The original's vtable-slot calls, in the same order as its disassembly.
using BoolThiscall0 = bool(__thiscall *)(void *);
using UintThiscall0 = uint32_t(__thiscall *)(void *);
using BufferingFn = bool(__thiscall *)(void *, int32_t, int32_t);
using SetPathFn = void(__thiscall *)(void *, const cIGZString *);
using GetStringFn = bool(__thiscall *)(void *, const cIGZString *, const cIGZString *, cIGZString *);
using SetStringFn = bool(__thiscall *)(void *, const cIGZString *, const cIGZString *, const cIGZString *);

// The cRZString behind a cIGZString argument (same layout; the original reads its std::string).
const cRZString &AsRZString(const cIGZString &s)
{
    return *reinterpret_cast<const cRZString *>(&s);
}

} // namespace

// 0x9c6b06. Clears the entries first if the table is loaded (through the vtable), then copies the
// path. The original returns nothing useful in eax.
void cRZIniFile::SetPath(const cIGZString &path)
{
    if (mLoaded) VirtualCall<BoolThiscall0, 5>(this);
    mPath.mString = AsRZString(path).mString;
}

// 0x9c6a73. Makes the object's file `file` (a cIGZFile*), taking a reference on it and dropping
// the old one. A null file creates a cRZFile on the object's path, with a 4 KB read buffer and no
// write buffer. The original calls Clear through the vtable first and never checks mFile for
// null, so the file is always set in practice; we do the same.
bool cRZIniFile::SetFile(void *file)
{
    if (file == mFile) return true;
    VirtualCall<BoolThiscall0, 5>(this);
    if (file) VirtualCall<UintThiscall0, 1>(file); // AddRef
    if (mFile) VirtualCall<UintThiscall0, 2>(mFile); // Release the old file
    mFile = file;
    if (file) return true;

    cRZFile *created = static_cast<cRZFile *>(::operator new(sizeof(cRZFile)));
    created->Construct();
    mFile = created;
    VirtualCall<UintThiscall0, 1>(created); // AddRef
    VirtualCall<BufferingFn, 32>(created, 0x1000, 0);
    VirtualCall<SetPathFn, 29>(created, mPath.AsInterface());
    return true;
}

// 0x9c69ab. Looks up the string for (section, key) through the vtable (slot 6), then reads it with
// sscanf "%ld" into *value. Returns true only if the lookup succeeded and sscanf converted one value.
bool cRZIniFile::GetLong(const cIGZString &section, const cIGZString &key, int32_t *value)
{
    cRZString text;
    text.Construct();
    bool found = VirtualCall<GetStringFn, 6>(this, &section, &key, text.AsInterface());
    bool result = false;
    if (found) {
#pragma warning(suppress : 4996) // the original's CRT sscanf
        result = std::sscanf(text.ToChar(), "%ld", value) == 1;
    }
    text.Destruct();
    return result;
}

// 0x9c75f5. Writes the decimal value through the vtable (slot 10) as the string for (section, key).
// Returns false, without writing, if either name is empty.
bool cRZIniFile::SetLong(const cIGZString &section, const cIGZString &key, int32_t value)
{
    if (AsRZString(section).IsEmpty() || AsRZString(key).IsEmpty()) return false;
    cRZString text;
    text.Construct();
    text.Sprintf("%d", value);
    bool result = VirtualCall<SetStringFn, 10>(this, &section, &key, text.AsInterface());
    text.Destruct();
    return result;
}

// 0x9c6aec. Sets the enabled flag. Disabling clears the table (through the vtable). Returns true.
bool cRZIniFile::SetEnabled(bool enabled)
{
    if (mEnabled != enabled) {
        mEnabled = enabled;
        if (!enabled) VirtualCall<BoolThiscall0, 5>(this);
    }
    return true;
}
