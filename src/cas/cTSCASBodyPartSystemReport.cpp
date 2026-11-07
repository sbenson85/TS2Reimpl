// nTSCASCore::cTSCASBodyPartSystem (lane JY): the vertex-id reports (slots 85, 86): one line per id of the list at +0x78
// saying whether the part entry has data and vertex ids, handed to the caller's string.
#include "cas/cTSCASBodyPartSystem.h"

#include "game/original.h"
#include "game/vtable.h"
#include "rz/cRZIniFile.h" // cRZIniStringHelpers::SplitOff

#include <cstring>

namespace nTSCASCore {

namespace {

cTSCASBodyPartSystem *SystemOf(BodyPartSystemIface *iface)
{
    return reinterpret_cast<cTSCASBodyPartSystem *>(reinterpret_cast<uint8_t *>(iface) - 0x1c);
}

// The report's texts, the image's own (read where they are rather than copied).
constexpr uint32_t kRvaLineFormat = 0xeba768;    // the line's start, from the id's name
constexpr uint32_t kRvaNoData = 0xeba774;        // 13 characters
constexpr uint32_t kRvaHasIds = 0xeba784;        // 17 characters
constexpr uint32_t kRvaCountsFormat = 0xeba798;  // three %d
constexpr uint32_t kRvaNoIds = 0xeba7b0;         // 24 characters

const char *IdName(uint32_t id)
{
    return OriginalFunction<const char *(__cdecl *)(uint32_t), 0xb5fd70>()(id);
}

void Append(LocalRZString &to, uint32_t rva, size_t length)
{
    to.mText.mString.append(GameAddress<const char>(rva), length);
}

// What the reports share: the caller's string emptied (its slot 26 with 0, -1); for a non-zero age one line per id
// (`line` fills it from the id's part entry), the whole text handed over through its slot 16.
template <typename Line> void Report(BodyPartSystemIface *self, void *out, Line line)
{
    VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t), 26>(out, 0, 0xffffffffu);
    if (self->mAge == 0) return;
    LocalRZString report;
    for (const uint32_t *id = self->mIdsFirst; id != self->mIdsLast; ++id) {
        LocalRZString text;
        line(*id, text);
        report.mText.mString.append(text.mText.mString.c_str(), text.mText.mString.size());
    }
    VirtualCall<void(__thiscall *)(void *, cRZString *), 16>(out, &report.mText);
}

// The id's name, then whether the entry has points (`first` / `last` of its vector), then (`hasIds`) the counts.
void ReportLine(LocalRZString &text, uint32_t id, bool hasData, bool hasIds, uint32_t a, uint32_t b, uint32_t c)
{
    text.mText.Sprintf(GameAddress<const char>(kRvaLineFormat), IdName(id));
    if (!hasData) {
        Append(text, kRvaNoData, 0xd);
    } else if (!hasIds) {
        Append(text, kRvaNoIds, 0x18);
    } else {
        Append(text, kRvaHasIds, 0x11);
        if (c < b) {
            LocalRZString counts;
            counts.mText.Sprintf(GameAddress<const char>(kRvaCountsFormat), a, b, c);
            text.mText.mString.append(counts.mText.mString.c_str(), counts.mText.mString.size());
        }
    }
}

} // namespace

void BodyPartSystemIface::ReportVertexIds(const char *name, void *out)
{
    Report(this, out, [&](uint32_t id, LocalRZString &text) {
        const uint8_t *const e = OriginalFunction<const uint8_t *(__thiscall *)(void *, const char *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t), 0xcec500>()(
            SystemOf(this), name, 3, 0, id, 0, 1, 0);
        const auto word = [e](uint32_t offset) { return *reinterpret_cast<const uint32_t *>(e + offset); };
        ReportLine(text, id, word(0x40) != word(0x44), e[0x31] != 0, word(0x38), word(0x34), word(0x3c));
    });
}

void BodyPartSystemIface::ReportGroupVertexIds(const char *name, uint32_t group, void *out)
{
    Report(this, out, [&](uint32_t id, LocalRZString &text) {
        const uint8_t *const e = OriginalFunction<const uint8_t *(__thiscall *)(void *, const char *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t), 0xcf07a0>()(
            SystemOf(this), name, 3, 0, id, 0, 1);
        const uint32_t at = group * 0xc;
        const auto word = [e, at](uint32_t offset) { return *reinterpret_cast<const uint32_t *>(e + at + offset); };
        // the original tests the address of the entry's +0x44 member for null
        const bool hasIds = reinterpret_cast<uintptr_t>(e) + 0x44 != 0;
        ReportLine(text, id, word(0x60) != word(0x64), hasIds, word(0x4c), word(0x48), word(0x50));
    });
}

// ---- Slot 87 (lane JZ): the region mask report -------------------------------------------------------------------------

namespace {

constexpr uint32_t kRvaRegionDelimiter = 0xdab158;   // the region list's delimiter
constexpr uint32_t kRvaRegionLine = 0xeba7d0;        // a line's start (14 characters)
constexpr uint32_t kRvaMissingForPart = 0xeba7e0;    // 19 characters
constexpr uint32_t kRvaMissingIds = 0xeba7fc;        // 31 characters
constexpr uint32_t kRvaLineEnd = 0xeba7f4;           // 4 characters
constexpr uint32_t kRvaRegionCounts = 0xeba820;      // the region, three counts and the id's name

// The region prefixes a part without a mask of its own (+0x42 clear) is expected to cover, per part id: {text, length}.
struct RegionPrefix {
    uint32_t mRva;
    uint32_t mLength;
};
constexpr RegionPrefix kHeadRegions[] = {{0xdf70ac, 4}, {0xdce374, 4}, {0xeba7cc, 3}, {0xdf70bc, 7}, {0xe87f6c, 3}, {0xdf70b4, 3}};
constexpr RegionPrefix kBodyRegion = {0xdbcf58, 4};
constexpr RegionPrefix kPart4Region = {0xdf70a4, 4};

// SplitOff's out: raw memory until the callee constructs it.
struct SplitText {
    union {
        cRZString mText;
    };
    SplitText() {}
    SplitText(const SplitText &) = delete;
    ~SplitText() { mText.Destruct(); }
};

bool StartsWithAny(const cRZString &region, const RegionPrefix *prefixes, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (region.StartsWith(GameAddress<const char>(prefixes[i].mRva), prefixes[i].mLength, false)) return true;
    }
    return false;
}

// Whether the region is one part `id` must have a mask for: id 2 any of the six head regions, id 3 the first five,
// id 1 the body, id 4 its own.
bool RegionExpected(const cRZString &region, uint32_t id)
{
    switch (id) {
    case 2: return StartsWithAny(region, kHeadRegions, 6);
    case 3: return StartsWithAny(region, kHeadRegions, 5);
    case 1: return StartsWithAny(region, &kBodyRegion, 1);
    case 4: return StartsWithAny(region, &kPart4Region, 1);
    default: return false;
    }
}

} // namespace

void BodyPartSystemIface::ReportRegionMasks(const char *regions, void *out)
{
    VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t), 26>(out, 0, 0xffffffffu);
    if (mAge == 0) return;
    LocalRZString report;
    std::string &r = report.mText.mString;
    LocalRZString rest(regions);
    const char *const delimiter = GameAddress<const char>(kRvaRegionDelimiter);
    SplitText region;
    reinterpret_cast<cRZIniStringHelpers *>(&rest.mText)->SplitOff(&region.mText, delimiter);
    const std::string &name = region.mText.mString;
    while (region.mText.Strlen() != 0) {
        for (const uint32_t *p = mIdsFirst; p != mIdsLast; ++p) {
            const uint32_t id = *p;
            const uint8_t *const e = OriginalFunction<const uint8_t *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, const char *), 0xcf29b0>()(
                SystemOf(this), 3, 0, id, 0, name.c_str());
            const auto word = [e](uint32_t offset) { return *reinterpret_cast<const uint32_t *>(e + offset); };
            if (word(0x44) == word(0x48)) continue; // no weights
            const auto missing = [&](uint32_t whatRva, size_t whatLength) {
                Append(report, kRvaRegionLine, 0xe);
                r.append(name.c_str(), name.size());
                Append(report, whatRva, whatLength);
                const char *const partName = IdName(id);
                r.append(partName, std::strlen(partName));
                Append(report, kRvaLineEnd, 4);
            };
            if (e[0x42] == 0) {
                if (RegionExpected(region.mText, id)) missing(kRvaMissingForPart, 0x13);
            } else if (e[0x30] == 0) {
                missing(kRvaMissingIds, 0x1f);
            } else if (word(0x3c) < word(0x34)) {
                LocalRZString counts;
                const char *const partName = IdName(id);
                counts.mText.Sprintf(GameAddress<const char>(kRvaRegionCounts), name.c_str(), word(0x38), word(0x34), word(0x3c), partName);
                r.append(counts.mText.mString.c_str(), counts.mText.mString.size());
            }
        }
        SplitText next;
        reinterpret_cast<cRZIniStringHelpers *>(&rest.mText)->SplitOff(&next.mText, delimiter);
        region.mText.mString.assign(next.mText.mString.c_str(), next.mText.mString.size());
    }
    VirtualCall<void(__thiscall *)(void *, cRZString *), 16>(out, &report.mText);
}

} // namespace nTSCASCore
