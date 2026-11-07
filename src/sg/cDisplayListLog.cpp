// The display list's item log (LogDrawItems, DrawPass's call when the log is open) and the shader context's description
// it writes (DescribeShaderContext). Every text is the game's (owned statics, game/owned.h) or one the functions
// build: numbers through cRZString's Sprintf (the CRT's formatting), the lights' table in std::strings padded with spaces.
#include "sg/cDisplayListLog.h"

#include "game/game.h"
#include "game/owned.h"
#include "gz/cParserTexts.h" // the line end 0xd9e644
#include "gz/GraphicServices.h" // GetVertexFormatsManagerCached
#include "gz/cIGZString.h"
#include "gz/cVertexFormat.h"
#include "misc/UtilitiesE.h" // GetCachedQueriedObject
#include "rz/cRZString.h"
#include "rz/cRZVariant.h" // the format 0xd9b648
#include "sg/SGLightInterfaces.h"
#include "sg/cDisplayList.h"
#include "sg/cLightRefNode.h"
#include "sg/cMaterialManager.h"
#include "sg/cShapeRefNode.h"
#include "sg/cValidateRenderShaderContext.h" // LightTypeName

#include <algorithm>
#include <cstring>
#include <string>

// The log's texts (the game's texts; standalone/owned_statics.cpp holds them for an image-free build).
TS2_OWNED_STATIC(const char, e75564)[];
TS2_OWNED_STATIC(const char, e7555c)[];
TS2_OWNED_STATIC(const char, e75570)[];
TS2_OWNED_STATIC(const char, e75590)[];
TS2_OWNED_STATIC(const char, e755bc)[];
TS2_OWNED_STATIC(const char, e755b0)[];
TS2_OWNED_STATIC(const char, e755dc)[];
TS2_OWNED_STATIC(const char, e755fc)[];
TS2_OWNED_STATIC(const char, e7561c)[];
TS2_OWNED_STATIC(const char, e7563c)[];
TS2_OWNED_STATIC(const char, e7565c)[];
TS2_OWNED_STATIC(const char, e75660)[];
TS2_OWNED_STATIC(const char, e75674)[];
TS2_OWNED_STATIC(const char, e7568c)[];
TS2_OWNED_STATIC(const char, e756a4)[];
TS2_OWNED_STATIC(const char, e756bc)[];
TS2_OWNED_STATIC(const char, dc5190)[];
TS2_OWNED_STATIC(const char, e756dc)[];
TS2_OWNED_STATIC(const char, e756fc)[];
TS2_OWNED_STATIC(const char, e7571c)[];
TS2_OWNED_STATIC(const char, e75824)[];
TS2_OWNED_STATIC(const char, e75764)[];
TS2_OWNED_STATIC(const char, e75784)[];
TS2_OWNED_STATIC(const char, e757a4)[];
TS2_OWNED_STATIC(const char, e757c4)[];
TS2_OWNED_STATIC(const char, e757e4)[];
TS2_OWNED_STATIC(const char, e75804)[];
TS2_OWNED_STATIC(const char, e75760)[];
TS2_OWNED_STATIC(const char, db1fcc)[];
TS2_OWNED_STATIC(const char, e7573c)[];
TS2_OWNED_STATIC(const char, e7574c)[];
TS2_OWNED_STATIC(const char, e75754)[];
TS2_OWNED_STATIC(const char, e7d4b0)[];
TS2_OWNED_STATIC(const char, e7d4d0)[];
TS2_OWNED_STATIC(const char, e7d4d8)[];
TS2_OWNED_STATIC(const char, e74ea4)[];
TS2_OWNED_STATIC(const char, e799f4)[];
TS2_OWNED_STATIC(const char, dbcb18)[];
TS2_OWNED_STATIC(const char, e7d4e0)[];
TS2_OWNED_STATIC(const char, e7d4e8)[];
TS2_OWNED_STATIC(const char, e7d4f0)[];
TS2_OWNED_STATIC(const char, e7d4f8)[];
TS2_OWNED_STATIC(const char, e7d500)[];
TS2_OWNED_STATIC(const char, e7d50c)[];
TS2_OWNED_STATIC(const char, daa6c0)[];

namespace nGZSceneGraph {

namespace {

constexpr uint32_t kLastLightType = 12;

// The texts (the game's strings), by the line they label.
constexpr const char *kItemText = ts2_rva_e75564;      // before the item's index
constexpr const char *kItemEndText = ts2_rva_e7555c;   // after it
constexpr const char *kSortKeyText = ts2_rva_e75570;
constexpr const char *kLineEnd = ts2_rva_d9e644;
constexpr const char *kVariantText = ts2_rva_e75590;
constexpr const char *kMaterialText = ts2_rva_e755bc;
constexpr const char *kMaterialIndexText = ts2_rva_e755b0;
constexpr const char *kGeometryText = ts2_rva_e755dc;
constexpr const char *kUserDataText = ts2_rva_e755fc;
constexpr const char *kSubIndexText = ts2_rva_e7561c;
constexpr const char *kSubFlagsText = ts2_rva_e7563c;
constexpr const char *kNoFlagsText = ts2_rva_e7565c;
constexpr const char *kFlag4Text = ts2_rva_e75660;
constexpr const char *kFlag2Text = ts2_rva_e75674;
constexpr const char *kFlag1Text = ts2_rva_e7568c;
constexpr const char *kFlag8Text = ts2_rva_e756a4;
constexpr const char *kLayerText = ts2_rva_e756bc;
constexpr const char *kDepthText = ts2_rva_dc5190;
constexpr const char *kNodeText = ts2_rva_e756dc;
constexpr const char *kNodeFrameText = ts2_rva_e756fc; // also the lights' node row
constexpr const char *kContextText = ts2_rva_e7571c;
constexpr const char *kAmbientText = ts2_rva_e75824;
// The lights' table: a row label per column (the node row's is kNodeFrameText), the cell of a light with no standard
// face (two characters, in each parameter column).
constexpr const char *const kRowText[7] = {ts2_rva_e75764, ts2_rva_e75784, kNodeFrameText, ts2_rva_e757a4, ts2_rva_e757c4, ts2_rva_e757e4, ts2_rva_e75804};
constexpr const char *kNoParamsText = ts2_rva_e75760;
// The formats.
constexpr const char *kPointerFormat = ts2_rva_db1fcc;
constexpr const char *kFrameFormat = ts2_rva_d9b648;
constexpr const char *kThreeFormat = ts2_rva_e7573c;
constexpr const char *kOneFormat = ts2_rva_e7574c;
constexpr const char *kTwoFormat = ts2_rva_e75754;

// DescribeShaderContext's texts and formats.
constexpr const char *kContextFormat = ts2_rva_e7d4b0; // the vertex format's text, then three counts
constexpr const char *kFlagsText = ts2_rva_e7d4d0;     // 7 characters
// Per flag (bits 0-3): the text when it is the last one left, and otherwise (one character longer).
constexpr const char *const kFlagLastText[4] = {ts2_rva_e7d4d8, ts2_rva_e74ea4, ts2_rva_e799f4, ts2_rva_dbcb18};
constexpr uint32_t kFlagLastLength[4] = {4, 4, 5, 5};
constexpr const char *const kFlagMoreText[4] = {ts2_rva_e7d4e0, ts2_rva_e7d4e8, ts2_rva_e7d4f0, ts2_rva_e7d4f8};
constexpr const char *kLightsText = ts2_rva_e7d500; // 8 characters
constexpr const char *kLightFormat = ts2_rva_e7d50c; // a type's name, its count, then the separator
constexpr const char *kLastSeparator = ts2_rva_d9b9d0;
constexpr const char *kMoreSeparator = ts2_rva_daa6c0;

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// The cumulative light count through type `type` (the state's nibble `type`): type t's lights are those from
// LightsThrough(t - 1) to LightsThrough(t).
uint32_t LightsThrough(const uint32_t state[3], uint32_t type)
{
    return state[type >> 3] >> ((type & 7) * 4) & 0xf;
}

// A cRZString on the stack (constructed and destroyed with cRZString's own bodies).
struct LocalString {
    union {
        cRZString s;
    };
    LocalString() { s.Construct(); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
    ~LocalString() { s.Destruct(); }
    const char *Chars() const { return s.mString.c_str(); }
    void AppendTo(std::string &to) const { to.append(s.mString.c_str(), s.mString.size()); }
};

// The lights' table of an item: seven columns, one cell per light, every column padded with spaces after each light
// to one more than the longest.
void LightTable(const DrawItem &item, LocalString &text, std::string (&columns)[7])
{
    const LightRecord *const *const lights = reinterpret_cast<const LightRecord *const *>(item.mWord3c);
    for (uint32_t type = 1; type <= kLastLightType; ++type) {
        const uint32_t first = LightsThrough(item.mState, type - 1);
        for (uint32_t j = 0; j < LightsThrough(item.mState, type) - first; ++j) {
            columns[0].append(LightTypeName(type));
            const LightRecord *const record = lights[first + j];
            text.s.Sprintf(kPointerFormat, Word(record->mLight));
            text.AppendTo(columns[1]);
            text.s.Sprintf(kFrameFormat, record->mNode->GetDetailLimit());
            text.AppendTo(columns[2]);
            if (record->mLight->GetStandardLightConst()) {
                float params[8];
                record->mLight->GetStandardLightConst()->GetParams(params, Word(&item));
                text.s.Sprintf(kThreeFormat, static_cast<double>(params[0]), static_cast<double>(params[1]),
                               static_cast<double>(params[2]));
                text.AppendTo(columns[3]);
                text.s.Sprintf(kOneFormat, static_cast<double>(params[3]));
                text.AppendTo(columns[4]);
                text.s.Sprintf(kTwoFormat, static_cast<double>(params[4]), static_cast<double>(params[5]));
                text.AppendTo(columns[5]);
                text.s.Sprintf(kTwoFormat, static_cast<double>(params[6]), static_cast<double>(params[7]));
                text.AppendTo(columns[6]);
            } else {
                for (unsigned c = 3; c < 7; ++c) columns[c].append(kNoParamsText, 2);
            }
            size_t widest = 0;
            for (const std::string &column : columns) widest = std::max(widest, column.size());
            for (std::string &column : columns) column.append(widest - column.size() + 1, ' ');
        }
    }
}

void LogItem(cIDisplayListLog *log, const DrawItem &item, int32_t index)
{
    log->SetRZCharStr(kItemText);
    log->SetUint32(static_cast<uint32_t>(index));
    log->SetRZCharStr(kItemEndText);
    log->SetRZCharStr(kSortKeyText);
    log->SetSint32(item.mSortKey);
    log->SetRZCharStr(kLineEnd);

    LocalString text;
    text.s.Sprintf(kPointerFormat, Word(item.mMaterial));
    log->SetRZCharStr(kVariantText);
    log->SetRZCharStr(text.Chars());
    log->SetRZCharStr(kLineEnd);

    cIMaterialManager *const materials = static_cast<cIMaterialManager *>(GetCachedQueriedObject());
    log->SetRZCharStr(kMaterialText);
    // The info's first word is the material's name text.
    log->SetRZCharStr(reinterpret_cast<const char *>(
        static_cast<uintptr_t>(materials->GetInfo(item.mMaterialIndex).nameHash)));
    log->SetRZCharStr(kMaterialIndexText);
    log->SetUint32(static_cast<uint32_t>(item.mMaterialIndex));
    log->SetRZCharStr(kLineEnd);

    text.s.Sprintf(kPointerFormat, Word(item.mGeometry));
    log->SetRZCharStr(kGeometryText);
    log->SetRZCharStr(text.Chars());
    log->SetRZCharStr(kLineEnd);

    log->SetRZCharStr(kUserDataText);
    log->SetUint32(item.mUserData);
    log->SetRZCharStr(kLineEnd);

    log->SetRZCharStr(kSubIndexText);
    log->SetUint16(static_cast<uint16_t>(item.mSubKey));
    log->SetRZCharStr(kLineEnd);

    log->SetRZCharStr(kSubFlagsText);
    const uint32_t flags = item.mSubKey >> 16;
    if (flags == 0) log->SetRZCharStr(kNoFlagsText);
    if (flags & 4) log->SetRZCharStr(kFlag4Text);
    if (flags & 2) log->SetRZCharStr(kFlag2Text);
    if (flags & 1) log->SetRZCharStr(kFlag1Text);
    if (flags & 8) log->SetRZCharStr(kFlag8Text);
    log->SetRZCharStr(kLineEnd);

    log->SetRZCharStr(kLayerText);
    log->SetSint32(item.mLayer);
    log->SetRZCharStr(kDepthText);
    log->SetFloat32(item.mDepth);
    log->SetRZCharStr(kLineEnd);

    const NodeRecord *const record = static_cast<const NodeRecord *>(item.mRecord);
    text.s.Sprintf(kPointerFormat, Word(record->mNode));
    log->SetRZCharStr(kNodeText);
    log->SetRZCharStr(text.Chars());
    log->SetRZCharStr(kLineEnd);

    log->SetRZCharStr(kNodeFrameText);
    log->SetUint32(static_cast<cIShapeRefNode *>(record->mNode)->GetDetailLimit());
    log->SetRZCharStr(kLineEnd);

    DescribeShaderContext(text.s.AsInterface(), item.mState);
    log->SetRZCharStr(kContextText);
    log->SetRZCharStr(text.Chars());
    log->SetRZCharStr(kLineEnd);

    std::string columns[7];
    LightTable(item, text, columns);
    if (!columns[0].empty()) {
        for (unsigned c = 0; c < 7; ++c) {
            log->SetRZCharStr(kRowText[c]);
            log->SetRZCharStr(columns[c].c_str());
            log->SetRZCharStr(kLineEnd);
        }
    }

    // The ambient colour: the three floats before the light block (none: zero).
    float ambient[3] = {0.0f, 0.0f, 0.0f};
    if (item.mWord3c) std::memcpy(ambient, reinterpret_cast<const float *>(item.mWord3c) - 3, sizeof ambient);
    text.s.Sprintf(kThreeFormat, static_cast<double>(ambient[0]), static_cast<double>(ambient[1]),
                   static_cast<double>(ambient[2]));
    log->SetRZCharStr(kAmbientText);
    log->SetRZCharStr(text.Chars());
    log->SetRZCharStr(kLineEnd);
}

} // namespace

void __cdecl LogDrawItems(cIDisplayListLog *log, const DrawItem *items, int32_t count)
{
    for (int32_t i = 0; i < count; ++i) LogItem(log, items[i], i);
}

void __cdecl DescribeShaderContext(cIGZString *out, const uint32_t state[3])
{
    using nGZGraphic4::cIVertexFormat;
    using nGZGraphic4::cIVertexFormatsManager;
    cIVertexFormatsManager *const formats =
        static_cast<cIVertexFormatsManager *>(nGZGraphic4::GetVertexFormatsManagerCached());
    cIVertexFormat *const format = formats->GetFormat(state[2] >> 4 & 0x7f);
    LocalString text;
    format->Describe(text.s.AsInterface());
    out->Sprintf(kContextFormat, text.Chars(), state[2] >> 11 & 0xf, state[2] >> 15 & 0x7f,
                 state[2] >> 22 & 0x3f);

    // The flags, the last one set written without its separator: the count of those left decides.
    const uint32_t flags = state[2] & 0xf;
    uint32_t left = (flags & 1) + (flags >> 1 & 1) + (flags >> 2 & 1) + (flags >> 3 & 1);
    if (left) out->Append(kFlagsText, 7);
    for (unsigned bit = 0; bit < 4; ++bit) {
        if (!(flags >> bit & 1)) continue;
        if (left == 1) {
            out->Append(kFlagLastText[bit], kFlagLastLength[bit]);
        } else {
            out->Append(kFlagMoreText[bit], kFlagLastLength[bit] + 1);
            --left;
        }
    }

    if (!(state[1] & 0xf0000)) return;
    out->Append(kLightsText, 8);
    uint32_t remaining = state[1] >> 16 & 0xf;
    for (uint32_t type = 1; type <= kLastLightType; ++type) {
        const uint32_t count = LightsThrough(state, type) - LightsThrough(state, type - 1);
        remaining -= count;
        if (!count) continue;
        LocalString line;
        line.s.Sprintf(kLightFormat, LightTypeName(type), count,
                       remaining == 0 ? kLastSeparator : kMoreSeparator);
        out->Append(*line.s.AsInterface());
    }
}

} // namespace nGZSceneGraph
