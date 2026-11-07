// cRZIniFile's writer, slot 10 SetString (0x9c6bbc). Lane MA (F4). See cRZIniFile.h for the layout and
// cRZIniFileTables.h for the tables; the reader is cRZIniFileRead.size.cpp, the loader cRZIniFileLoad.size.cpp.
#include "rz/cRZIniFile.h"

#include "game/vtable.h"
#include "ifaces/cIGZFile.h"
#include "rz/StringKeyParser.h" // cRZStringHelpers::LowerCase
#include "rz/cRZIniFileTables.h"

#include <cstdlib>
#include <utility>

namespace {

using rzini::IniKey;
using rzini::SecondTable;
using rzini::SectionTable;

// This object's own slots, called through its vtable like the original (subclasses override some).
constexpr int kSlotClear = 5;
constexpr int kSlotLoad = 17;
constexpr int kSlotReadLine = 18;
constexpr int kSlotOpen = 19;
using ClearFn = bool(__thiscall *)(void *);
using LoadFn = bool(__thiscall *)(void *, bool);
using ReadLineFn = bool(__thiscall *)(void *, cIGZString *);
using OpenFn = bool(__thiscall *)(void *, bool);

constexpr char kLineEnd[] = "\r\n";
constexpr char kSectionFormat[] = "[%s]%s";
constexpr char kKeyFormat[] = "%s = %s%s";

const cRZString &AsRZ(const cIGZString &s)
{
    return *reinterpret_cast<const cRZString *>(&s);
}

ifaces::cIGZFile *File(const cRZIniFile *self)
{
    return As<ifaces::cIGZFile>(self->mFile);
}

void WriteText(cRZIniFile *self, const cRZString &text)
{
    File(self)->Write(const_cast<char *>(text.mString.data()), static_cast<uint32_t>(text.mString.size()));
}

void ClearIfDisabled(cRZIniFile *self)
{
    if (!self->mEnabled) VirtualCall<ClearFn, kSlotClear>(self);
}

void LowerCase(cRZString &s)
{
    reinterpret_cast<cRZStringHelpers *>(&s)->LowerCase();
}

// "key = value" and its line end, at the file's position.
void WriteKeyLine(cRZIniFile *self, const cIGZString &key, const cIGZString &value)
{
    cRZString line;
    line.Construct();
    line.Sprintf(kKeyFormat, AsRZ(key).ToChar(), AsRZ(value).ToChar(), kLineEnd);
    WriteText(self, line);
    line.Destruct();
}

// The rest of the file from `from` read into memory, the key line written at `at`, the rest after it, and the file
// cut where that ends. A failed read changes nothing. Either way the index is stale afterwards (mLoaded cleared).
void RewriteFrom(cRZIniFile *self, uint32_t from, int32_t at, bool seekFirst, const cIGZString &key, const cIGZString &value)
{
    const uint32_t count = File(self)->Length() - from;
    void *rest = std::malloc(count);
    if (seekFirst) File(self)->SeekToPosition(at);
    if (File(self)->Read(rest, count)) {
        File(self)->SeekToPosition(at);
        WriteKeyLine(self, key, value);
        File(self)->Write(rest, count);
        File(self)->SetLength(static_cast<uint32_t>(File(self)->Position()));
    }
    std::free(rest);
    self->mLoaded = false;
}

} // namespace

// 0x9c6bbc. The section and key are written as given, the lookups are lowercased. A new file or section is not
// indexed for a new file (mLoaded cleared), and indexed at its header for a new section; a rewrite in the middle
// of the file clears mLoaded so the next read indexes again.
bool cRZIniFile::SetString(const cIGZString &section, const cIGZString &key, const cIGZString &value)
{
    if (AsRZ(section).mString.empty() || AsRZ(key).mString.empty()) return false;
    const bool havePath = !mPath.mString.empty();
    if (havePath) File(this)->SetPath(mPath.AsInterface());

    if (havePath && !File(this)->Exists()) {
        // A new file: the section header and the line.
        mLoaded = false;
        if (File(this)->IsOpen()) File(this)->Close();
        if (!VirtualCall<OpenFn, kSlotOpen>(this, true)) return false;
        cRZString header;
        header.Construct();
        header.Sprintf(kSectionFormat, AsRZ(section).ToChar(), kLineEnd);
        WriteText(this, header);
        header.Destruct();
        WriteKeyLine(this, key, value);
        ClearIfDisabled(this);
        return true;
    }

    if (!mLoaded && !VirtualCall<LoadFn, kSlotLoad>(this, true)) return false;
    if (!mOpenForWrite && File(this)->IsOpen()) File(this)->Close();
    if (!File(this)->IsOpen() && !VirtualCall<OpenFn, kSlotOpen>(this, true)) return false;

    cRZString sectionLower;
    sectionLower.Construct(AsRZ(section), 0, 0xffffffff);
    LowerCase(sectionLower);
    cRZString keyLowerUnused; // the original makes this copy and never reads it
    keyLowerUnused.Construct(AsRZ(key), 0, 0xffffffff);
    LowerCase(keyLowerUnused);

    bool result = true;
    SectionTable &sections = *reinterpret_cast<SectionTable *>(mEntries);
    auto found = sections.find(*reinterpret_cast<const IniKey *>(&sectionLower));
    if (found == sections.end()) {
        // A new section at the end of the file, after a blank line.
        File(this)->SeekToEnd();
        cRZString text;
        text.Construct();
        bool endsWithBreak = false;
        if (File(this)->Position() != 0) {
            char last = 0;
            File(this)->SeekToRelativePosition(-1);
            endsWithBreak = File(this)->Read(&last, 1) && (last == '\r' || last == '\n');
        }
        if (endsWithBreak)
            text.Sprintf("%s", kLineEnd);
        else
            text.Sprintf("%s%s", kLineEnd, kLineEnd);
        WriteText(this, text);
        const uint32_t headerAt = static_cast<uint32_t>(File(this)->Position());
        text.Sprintf(kSectionFormat, AsRZ(section).ToChar(), kLineEnd);
        WriteText(this, text);
        text.Destruct();
        WriteKeyLine(this, key, value);
        {
            std::pair<IniKey, uint32_t> entry{IniKey(sectionLower), headerAt};
            sections.insert(entry);
        }
        {
            std::pair<IniKey, IniKey> entry{IniKey(sectionLower), IniKey(AsRZ(section))};
            reinterpret_cast<SecondTable *>(mSecondTable)->insert(entry);
        }
        ClearIfDisabled(this);
    } else if (static_cast<uint32_t>(File(this)->SeekToPosition(static_cast<int32_t>(found->second))) >= File(this)->Length()) {
        ClearIfDisabled(this);
        result = false;
    } else {
        cRZString lineKey;
        lineKey.Construct();
        cRZString keyLower;
        keyLower.Construct(AsRZ(key), 0, 0xffffffff);
        cRZString line;
        line.Construct();
        LowerCase(keyLower);
        auto readLine = [this, &line] { return VirtualCall<ReadLineFn, kSlotReadLine>(this, line.AsInterface()); };
        if (!readLine()) { // the section's own header line
            ClearIfDisabled(this);
            result = false;
        } else {
            auto *helpers = reinterpret_cast<cRZIniStringHelpers *>(&line);
            int32_t lineAt = File(this)->Position();
            bool placed = false;
            while (!placed && readLine()) {
                helpers->TrimLeading();
                if (!line.mString.empty() && line.mString[0] != ';') {
                    if (line.mString[0] == '[') {
                        // The next section: the line goes before it.
                        RewriteFrom(this, static_cast<uint32_t>(lineAt), lineAt, true, key, value);
                        placed = true;
                        break;
                    }
                    cRZString name;
                    helpers->SplitOff(&name, "=");
                    if (&lineKey.mString != &name.mString) lineKey.mString.assign(name.mString.c_str(), name.mString.size());
                    name.Destruct();
                    reinterpret_cast<cRZIniStringHelpers *>(&lineKey)->TrimTrailing();
                    LowerCase(lineKey);
                    if (lineKey.mString == keyLower.mString) {
                        // The key's line: replaced (with the blank lines after it).
                        RewriteFrom(this, static_cast<uint32_t>(File(this)->Position()), lineAt, false, key, value);
                        placed = true;
                        break;
                    }
                }
                lineAt = File(this)->Position();
            }
            if (!placed) {
                // The section is the last one: the line goes at the end of the file.
                File(this)->SeekToEnd();
                WriteKeyLine(this, key, value);
            }
            ClearIfDisabled(this);
        }
        line.Destruct();
        keyLower.Destruct();
        lineKey.Destruct();
    }
    keyLowerUnused.Destruct();
    sectionLower.Destruct();
    return result;
}
