// cGZLegacyControl's window-class registrations (lane ME, foundations F6 part 1): src/gz/cGZLegacyControlRegistrations.h.
// Built for size, as the game's. Each registration fills the shared script vocabulary (the class's script name, type
// name and property names, each with an id) and, for a window class that has a handler, the types and handlers tables.
// The names are image strings, read by RVA; the original calls them in the order the first constructor runs them.
#include "gz/cGZLegacyControlRegistrations.h"

#include "game/game.h"
#include "gz/cGZLegacyControl.h"
#include "gz/cGZLegacyHandlers.h"

namespace {

// One vocabulary entry: the name's RVA in the image (a C string) and its id.
struct LegacyName {
    uint32_t text;
    uint32_t id;
};

// One registration: its `count` vocabulary entries, the tables' entries added after the first `leading` of them (none
// when `handler` is 0: the common names), the handler's RVA, the class id (types table key) and the type id (the
// handlers table's key and the types table's value). A handler that is ours (F6 part 2) is `ours`, its `handler` RVA 0.
struct LegacyRegistration {
    const LegacyName *names;
    uint32_t count;
    uint32_t leading;
    uint32_t handler;
    uint32_t classId;
    uint32_t typeId;
    LegacyHandler ours = nullptr;
};

void AddNames(cIGZScriptVocab *vocab, const LegacyName *names, uint32_t from, uint32_t to)
{
    for (uint32_t i = from; i < to; ++i) {
        LegacyText text;
        text.mText.FromChar(GameAddress<const char>(names[i].text));
        vocab->AddId(*text.mText.AsInterface(), names[i].id);
    }
}

// The tables are the game's std::maps (MSVC 14.40, the same layout as ours), filled in place: a node ours allocates is
// freed by the game's destructor through the same process heap. An existing key keeps its value (try_emplace).
void Register(const LegacyRegistration &registration)
{
    cIGZScriptVocab *const vocab = *GameAddress<cIGZScriptVocab *>(cGZLegacyControl::kVocabCell);
    AddNames(vocab, registration.names, 0, registration.leading);
    if (registration.handler || registration.ours) {
        const LegacyHandler handler =
            registration.ours ? registration.ours : reinterpret_cast<LegacyHandler>(GameAddress<void>(registration.handler));
        GameAddress<LegacyHandlerMap>(cGZLegacyControl::kHandlersCell)->try_emplace(registration.typeId, handler);
        GameAddress<LegacyTypeMap>(cGZLegacyControl::kTypesCell)->try_emplace(registration.classId, registration.typeId);
    }
    AddNames(vocab, registration.names, registration.leading, registration.count);
}

// clsid (original 0x85e431)
constexpr LegacyName kCommonNames[] = {
    {0xe48340, 0xffffffff}, // clsid
    {0xe48348, 0xfffffffe}, // iid
    {0xdb2a58, 0x1}, // yes
    {0xda2d44, 0x1}, // true
    {0xdb2a5c, 0x0}, // no
    {0xda2d3c, 0x0}, // false
    {0xda50fc, 0x1}, // on
    {0xda5100, 0x0}, // off
    {0xe4834c, 0xe2ba00ee}, // GZWin
    {0xdcac20, 0x100}, // id
    {0xe48354, 0x101}, // area
    {0xda0fd8, 0x102}, // pos
    {0xdbbf88, 0x103}, // size
    {0xe4835c, 0x104}, // fillcolor
    {0xe48368, 0x105}, // caption
    {0xe48370, 0x106}, // captionres
    {0xe4837c, 0x107}, // transparent
    {0xe48388, 0xf000}, // font
    {0xe48390, 0xf001}, // bkgcolor
    {0xe4839c, 0xf002}, // forecolor
    {0xe483a8, 0xf003}, // notify
    {0xe483b0, 0xf004}, // gutters
    {0xe483b8, 0xf005}, // style
    {0xdf5ed8, 0xf006}, // left
    {0xdf5ee0, 0xf007}, // right
    {0xe483c0, 0xf008}, // center
    {0xe483c8, 0xf009}, // flat
    {0xdbcaf0, 0xf00a}, // normal
    {0xe483d0, 0xf00b}, // sunken
    {0xe483d8, 0xf00c}, // raised
    {0xe483e0, 0xf00d}, // fill
    {0xe483e8, 0xf00e}, // nofill
    {0xe483f0, 0xf00f}, // colorfontnormal
    {0xe48400, 0xf010}, // colorfontdisabled
    {0xe48414, 0xf011}, // colorfonthilited
    {0xe48428, 0xf012}, // colorfontnormalbkg
    {0xe4843c, 0xf013}, // colorfontdisabledbkg
    {0xe48454, 0xf014}, // colorfonthilitedbkg
    {0xe48468, 0xf015}, // align
    {0xe48470, 0xf016}, // image
    {0xe48478, 0xf017}, // imagetype
    {0xe48484, 0xf018}, // imagerect
    {0xe48490, 0xf019}, // outline
    {0xe48498, 0x108}, // comments
    {0xe484a4, 0x109}, // commentslmd
    {0xe484b0, 0x10a}, // commentsrmd
    {0xe484bc, 0x10b}, // commentslmu
    {0xe484c8, 0x10c}, // commentsrmu
    {0xe484d4, 0x10d}, // commentsmm
    {0xe484e0, 0x10e}, // commentsku
    {0xe484ec, 0x10f}, // commentskd
    {0xe484f8, 0xf01a}, // winflag_visible
    {0xe48508, 0xf01b}, // winflag_enabled
    {0xe48518, 0xf01c}, // winflag_moveable
    {0xe4852c, 0xf01d}, // winflag_sizeable
    {0xe48540, 0xf01e}, // winflag_sortable
    {0xe48554, 0xf01f}, // winflag_pbuff
    {0xe48564, 0xf020}, // winflag_pbufftrans
    {0xe48578, 0xf021}, // winflag_pbufferase
    {0xe4858c, 0xf022}, // winflag_pbuffvid
    {0xe485a0, 0xf026}, // winflag_alphablend
    {0xe485b4, 0xf027}, // winflag_premulalpha
    {0xe485c8, 0xf023}, // winflag_mousetrans
    {0xe485dc, 0xf024}, // winflag_ignoremouse
    {0xe485f0, 0xf025}, // winflag_acceptfocus
    {0xe48604, 0xf028}, // winflag_delayedplot
    {0xe48618, 0xf029}, // winflag_container
    {0xe4862c, 0xf02a}, // winflag_fadeeffects
    {0xe48640, 0xf02b}, // winflag_relativeright
    {0xe48658, 0xf02c}, // winflag_relativewidth
    {0xe48670, 0xf02d}, // winflag_relativebottom
    {0xe48688, 0xf02e}, // winflag_relativeheight
    {0xe486a0, 0x110}, // wparam
};
constexpr LegacyRegistration kCommonNamesReg = {kCommonNames, 73, 73, 0, 0, 0};

// GZWinFlatRect (original 0x86024e)
constexpr LegacyName kFlatRect[] = {
    {0xe48724, 0xc2afa76e}, // GZWinFlatRect
    {0xe48734, 0xc2afa76f}, // IGZWinFlatRect
    {0xe48744, 0x200}, // colorleft
    {0xe48750, 0x201}, // colortop
    {0xe4875c, 0x202}, // colorright
    {0xe48768, 0x203}, // colorbottom
    {0xdb7844, 0x204}, // top
    {0xdbcf50, 0x205}, // bottom
    {0xdd0600, 0x206}, // all
    {0xe48774, 0x207}, // lrb
    {0xe48778, 0x208}, // trb
};
constexpr LegacyRegistration kFlatRectReg = {kFlatRect, 11, 2, 0, 0xc2afa76e, 0xc2afa76f, &ApplyFlatRectProperties};

// GZWinText (original 0x86249d)
constexpr LegacyName kText[] = {
    {0xe4877c, 0x592}, // GZWinText
    {0xe48788, 0x212cdc1f}, // IGZWinText
    {0xe48794, 0x300}, // wrapped
    {0xe4879c, 0x301}, // opaque
    {0xe487a4, 0x302}, // defcolor
    {0xe487b0, 0x303}, // dbgdrawarea
    {0xe487bc, 0x304}, // autofit
    {0xe487c4, 0x0}, // lefttop
    {0xe487cc, 0x1}, // leftcenter
    {0xe487d8, 0x2}, // leftbottom
    {0xe487e4, 0x3}, // centertop
    {0xe483c0, 0x4}, // center
    {0xe487f0, 0x5}, // centerbottom
    {0xe48800, 0x6}, // righttop
    {0xe4880c, 0x7}, // rightcenter
    {0xe48818, 0x8}, // rightbottom
    {0xe48824, 0x305}, // textoffsets
};
constexpr LegacyRegistration kTextReg = {kText, 17, 2, 0, 0x592, 0x212cdc1f, &ApplyTextProperties};

// GZWinTextEdit (original 0x86288d)
constexpr LegacyName kTextEdit[] = {
    {0xe48830, 0x231a1c58}, // GZWinTextEdit
    {0xe48840, 0x231a1c57}, // IGZWinTextEdit
    {0xe48850, 0x400}, // editable
    {0xe4885c, 0x401}, // hscrollbar
    {0xe48868, 0x402}, // vscrollbar
    {0xe48874, 0x403}, // caretvisible
    {0xe48884, 0x404}, // allowinsert
    {0xe48890, 0x405}, // allowundo
    {0xe4889c, 0x406}, // singleline
    {0xe488a8, 0x407}, // caretcolor
    {0xe488b4, 0x408}, // highlightcolor
    {0xe488c4, 0x409}, // maxtext
    {0xe488cc, 0x40a}, // maxglyphs
    {0xe488d8, 0x40b}, // initvalue
    {0xe488e4, 0x40c}, // outlinecolor
    {0xe488f4, 0x40d}, // overwrite
    {0xe48900, 0x40e}, // insertindex
    {0xe4890c, 0x40f}, // insertpos
    {0xe48918, 0x410}, // caretperiod
    {0xe48924, 0x411}, // maxundo
    {0xe4892c, 0x414}, // hscrollimage
    {0xe4893c, 0x415}, // hscrollimageRect
    {0xe48950, 0x412}, // vscrollimage
    {0xe48960, 0x413}, // vscrollimageRect
    {0xe48974, 0x416}, // notifyonreturn
    {0xe48984, 0x417}, // notifyonchange
    {0xe48994, 0x418}, // notifyonlostfocus
    {0xe489a8, 0x419}, // enableclipboard
    {0xe489b8, 0x41a}, // passwordmode
    {0xe489c8, 0x41b}, // prompt
    {0xe489d0, 0x41c}, // promptres
};
constexpr LegacyRegistration kTextEditReg = {kTextEdit, 31, 2, 0, 0x231a1c58, 0x231a1c57, &ApplyTextEditProperties};

// GZWinListBox (original 0x861263)
constexpr LegacyName kListBox[] = {
    {0xe48a60, 0x598}, // GZWinListBox
    {0xe48a70, 0x4132242b}, // IGZWinListBox
    {0xe48a80, 0x500}, // scrollbargutters
    {0xe48a94, 0x501}, // initselection
    {0xe48aa4, 0x503}, // sort
    {0xe48aac, 0x504}, // drop
    {0xe48ab4, 0x505}, // setlinevisible
    {0xe48ac4, 0x506}, // setlineontop
    {0xe48ad4, 0x502}, // listelement
};
constexpr LegacyRegistration kListBoxReg = {kListBox, 9, 2, 0, 0x598, 0x4132242b, &ApplyListBoxProperties};

// GZWinCombo (original 0x85e28d)
constexpr LegacyName kCombo[] = {
    {0xe48af8, 0x59b}, // GZWinCombo
    {0xe48b04, 0x412ce496}, // IGZWinCombo
    {0xe48b10, 0x800}, // combodownarrowimage
    {0xe48b24, 0x801}, // combodownarrowrect
    {0xe48b38, 0x802}, // combodowncolor
    {0xe48b48, 0x803}, // buttongutter
};
constexpr LegacyRegistration kComboReg = {kCombo, 6, 2, 0, 0x59b, 0x412ce496, &ApplyComboProperties};

// GZWinBtn (original 0x85dba9)
constexpr LegacyName kButton[] = {
    {0xe48b58, 0x22ecfc47}, // GZWinBtn
    {0xe48b64, 0x8810}, // IGZWinBtn
    {0xe48b70, 0xa00}, // toggle
    {0xe48b78, 0xa01}, // triggerondown
    {0xe48b88, 0xa02}, // showcaption
    {0xe48b94, 0xa03}, // autosize
    {0xe48ba0, 0xa04}, // wrapcaption
    {0xe48bac, 0xa05}, // shiftcaption
    {0xe48bbc, 0xa06}, // tips
    {0xe48bc4, 0xa07}, // tipsdelay
    {0xe48bd0, 0xa08}, // tipstimeout
    {0xe48bdc, 0xa09}, // label
    {0xe48be4, 0xa0a}, // standard
    {0xe48bf0, 0xa0b}, // standardfixed
    {0xe48c00, 0xa0c}, // togglebtn
    {0xe48c0c, 0xa0d}, // togglebtnfixed
    {0xe48c1c, 0xa0e}, // checkbox
    {0xe48c28, 0xa0f}, // radiobtn
    {0xe48c34, 0xa10}, // tiptext
    {0xe48c3c, 0xa11}, // tipoffsets
    {0xe48c48, 0xa12}, // tipres
    {0xe48c50, 0xa13}, // noimage
    {0xe48c58, 0xa14}, // standardhilite
    {0xe48c68, 0xa15}, // standardhilitefixed
    {0xe48c7c, 0xa16}, // radiocheckall
    {0xde54e8, 0xa17}, // radio
    {0xe48c8c, 0xa18}, // tipflag
    {0xe48c94, 0xa19}, // btnclicksnd
    {0xe48ca0, 0xa1a}, // btnupsnd
    {0xe48cac, 0xa1b}, // radiocheck
};
constexpr LegacyRegistration kButtonReg = {kButton, 30, 2, 0, 0x22ecfc47, 0x8810, &ApplyButtonProperties};

// GZWinBMP (original 0x85da3d)
constexpr LegacyName kBitmap[] = {
    {0xe48cc0, 0x82fe68c4}, // GZWinBMP
    {0xe48ccc, 0xc12cea13}, // IGZWinBMP
    {0xdbb3a0, 0xb00}, // alpha
    {0xe48cd8, 0xb01}, // transparentbkg
    {0xe48ce8, 0xb02}, // edgeimage
};
constexpr LegacyRegistration kBitmapReg = {kBitmap, 5, 2, 0, 0x82fe68c4, 0xc12cea13, &ApplyBitmapProperties};

// GZWinSlider (original 0x861fd1)
constexpr LegacyName kSlider[] = {
    {0xe48cf4, 0x21325208}, // GZWinSlider
    {0xe48d00, 0x21325207}, // IGZWinSlider
    {0xe48d10, 0xc00}, // minmaxvalue
    {0xe48d1c, 0xc01}, // direction
    {0xde266c, 0xc02}, // horizontal
    {0xde2660, 0xc03}, // vertical
};
constexpr LegacyRegistration kSliderReg = {kSlider, 6, 2, 0, 0x21325208, 0x21325207, &ApplySliderProperties};

// GZWinScrollbar (original 0x861e67)
constexpr LegacyName kScrollbar[] = {
    {0xe48d28, 0x61325a2e}, // GZWinScrollbar
    {0xe48d38, 0x61325a2d}, // IGZWinScrollbar
    {0xe48d48, 0xd00}, // pagesize
    {0xe48d54, 0xd01}, // linesize
    {0xe48d60, 0xd02}, // linepagecount
};
constexpr LegacyRegistration kScrollbarReg = {kScrollbar, 5, 2, 0, 0x61325a2e, 0x61325a2d, &ApplyScrollbarProperties};

// GZWinLineInput (original 0x860f01)
constexpr LegacyName kLineInput[] = {
    {0xe48e14, 0x21335c5a}, // GZWinLineInput
    {0xe48e24, 0x21335c59}, // IGZWinLineInput
    {0xe48e34, 0xe00}, // filled
    {0xe48e3c, 0xe01}, // outline3d
    {0xe48e48, 0xe02}, // outline3dex
    {0xe48e54, 0xe03}, // colorlt
    {0xe48e5c, 0xe04}, // colorrb
    {0xe48e64, 0xe05}, // colorltinside
    {0xe48e74, 0xe06}, // colorrbinside
    {0xe48e84, 0xe07}, // fillcolorhilite
    {0xe48e94, 0xe08}, // fillalphahilite
    {0xe48ea4, 0xe09}, // caretpos
    {0xe48eb0, 0xe0a}, // charlimit
    {0xe48ebc, 0xe0b}, // selection
};
constexpr LegacyRegistration kLineInputReg = {kLineInput, 14, 2, 0, 0x21335c5a, 0x21335c59, &ApplyLineInputProperties};

// GZWinOptGrp (original 0x8614af)
constexpr LegacyName kOptionGroup[] = {
    {0xe48ec8, 0xa1336cc1}, // GZWinOptGrp
    {0xe48ed4, 0xa1336cc0}, // IGZWinOptGrp
    {0xe48ee4, 0xf00}, // maincaption
    {0xe48ef0, 0xf01}, // itemcaption
    {0xe48efc, 0xf02}, // drawfill
    {0xe48f08, 0xf03}, // labelfont
    {0xe48f14, 0xf04}, // buttonfont
    {0xe48f20, 0xf05}, // labelfontcolornormal
    {0xe48f38, 0xf06}, // labelfontcolordisabled
    {0xe48f50, 0xf07}, // buttonfontcolornormal
    {0xe48f68, 0xf08}, // buttonfontcolorhilited
    {0xe48f80, 0xf09}, // buttonfontcolordisabled
    {0xe48f98, 0xf0a}, // captiontextresid
    {0xe48fac, 0xf0b}, // backimage
    {0xe48fb8, 0xf0c}, // autofilltype
    {0xe48fc8, 0xf0d}, // option
    {0xe48fd0, 0xf0e}, // automatic
    {0xe48fdc, 0xf0f}, // manual
    {0xe48fe4, 0xf10}, // autofillsize
    {0xe48ff4, 0xf11}, // optionmoveto
    {0xe49004, 0xf12}, // optionsetsize
};
constexpr LegacyRegistration kOptionGroupReg = {kOptionGroup, 21, 2, 0, 0xa1336cc1, 0xa1336cc0, &ApplyOptionGroupProperties};

// GZWinSpinner (original 0x862173)
constexpr LegacyName kSpinner[] = {
    {0xe49040, 0x612ce0c4}, // GZWinSpinner
    {0xe49050, 0x612ce0c3}, // IGZWinSpinner
    {0xe49060, 0x1000}, // digits
    {0xe49068, 0x1001}, // autonumber
    {0xe49074, 0x1002}, // autonumbercomma
    {0xe49084, 0x1003}, // autonumbercurrency
    {0xe49098, 0x1004}, // minmax
    {0xe490a0, 0x1005}, // stepsize
    {0xe490ac, 0x1006}, // coloroutline
    {0xe490bc, 0x1007}, // coloroutlinel
    {0xe490cc, 0x1008}, // coloroutlinet
    {0xe490dc, 0x1009}, // coloroutliner
    {0xe490ec, 0x100a}, // coloroutlineb
};
constexpr LegacyRegistration kSpinnerReg = {kSpinner, 13, 2, 0, 0x612ce0c4, 0x612ce0c3, &ApplySpinnerProperties};

// GZWinOutline (original 0x861999)
constexpr LegacyName kOutline[] = {
    {0xe490fc, 0x4303e5b6}, // GZWinOutline
    {0xe4910c, 0xe303e5ae}, // IGZWinOutline
    {0xe4911c, 0x1100}, // colordark
    {0xe49128, 0x1101}, // colorlight
    {0xe49134, 0x1102}, // node
};
constexpr LegacyRegistration kOutlineReg = {kOutline, 5, 2, 0, 0x4303e5b6, 0xe303e5ae, &ApplyOutlineProperties};

// GZWinTextTicker (original 0x862fa9)
constexpr LegacyName kTextTicker[] = {
    {0xe4913c, 0xe32f0b31}, // GZWinTextTicker
    {0xe4914c, 0x32f0b3c}, // IGZWinTextTicker
    {0xe49160, 0x1200}, // speed
    {0xe49168, 0x1201}, // frequency
    {0xe49174, 0x1202}, // message
};
constexpr LegacyRegistration kTextTickerReg = {kTextTicker, 5, 2, 0, 0xe32f0b31, 0x32f0b3c, &ApplyTextTickerProperties};

// GZWinFileBrowser (original 0x85f31a)
constexpr LegacyName kFileBrowser[] = {
    {0xe492a4, 0x1aa52ea4}, // GZWinFileBrowser
    {0xe492b8, 0x3aa52e64}, // IGZWinFileBrowser
    {0xe492cc, 0x1301}, // defaultpath
    {0xe492d8, 0x1302}, // backdir
    {0xe492e0, 0x1303}, // dir
    {0xe492e4, 0x1304}, // file
    {0xe492ec, 0x1305}, // defaultselectiontype
    {0xe49304, 0x1306}, // defaultselectionname
    {0xe4931c, 0x1307}, // defaultselectionextension
    {0xe49338, 0x1308}, // bynameascending
    {0xe49348, 0x1309}, // bynamedescending
    {0xe4935c, 0x130a}, // byextensionascending
    {0xe49374, 0x130b}, // byextensiondescending
    {0xe4938c, 0x130c}, // filesortprimary
    {0xe4939c, 0x130d}, // filesortsecondary
    {0xe493b0, 0x130e}, // topascending
    {0xe493c0, 0x130f}, // topdescending
    {0xe493d0, 0x1310}, // bottomascending
    {0xe493e0, 0x1311}, // bottomdescending
    {0xe493f4, 0x1312}, // directorysort
    {0xe49404, 0x1313}, // icon
    {0xdcac08, 0x1314}, // name
    {0xe4940c, 0x1315}, // extension
    {0xdcb120, 0x1316}, // empty
    {0xdc8300, 0x1317}, // unused
    {0xdc9704, 0x1318}, // default
    {0xe49418, 0x1319}, // column1data
    {0xe49424, 0x131a}, // column2data
    {0xe49430, 0x131b}, // column3data
    {0xe4943c, 0x131c}, // filenamefont
    {0xe4944c, 0x131d}, // filenameforegroundcolor
    {0xe49464, 0x131e}, // filenamebackgroundcolor
    {0xe4947c, 0x131f}, // filenameforegroundcolordisabled
    {0xe4949c, 0x1320}, // filenamebackgroundcolordisabled
    {0xe494bc, 0x1321}, // filenameforegroundcolorhilited
    {0xe494dc, 0x1322}, // filenamebackgroundcolorhilited
    {0xe494fc, 0x1323}, // filenamealignment
    {0xe49510, 0x1324}, // fileextensionfont
    {0xe49524, 0x1325}, // fileextensionforegroundcolor
    {0xe49544, 0x1326}, // fileextensionbackgroundcolor
    {0xe49564, 0x1327}, // fileextensionforegroundcolordisabled
    {0xe4958c, 0x1328}, // fileextensionbackgroundcolordisabled
    {0xe495b4, 0x1329}, // fileextensionforegroundcolorhilited
    {0xe495d8, 0x132a}, // fileextensionbackgroundcolorhilited
    {0xe495fc, 0x132b}, // fileextensionalignment
    {0xe49614, 0x132c}, // drivenamefont
    {0xe49624, 0x132d}, // drivenameforegroundcolor
    {0xe49640, 0x132e}, // drivenamebackgroundcolor
    {0xe4965c, 0x132f}, // drivenameforegroundcolordisabled
    {0xe49680, 0x1330}, // drivenamebackgroundcolordisabled
    {0xe496a4, 0x1331}, // drivenameforegroundcolorhilited
    {0xe496c4, 0x1332}, // drivenamebackgroundcolorhilited
    {0xe496e4, 0x1333}, // drivenamealignment
    {0xe496f8, 0x1334}, // iconcolumnwidth
    {0xe49708, 0x1335}, // extensioncolumnwidth
    {0xe49720, 0x1336}, // rowheight
    {0xe4972c, 0x1337}, // linegridenabled
    {0xe4973c, 0x1338}, // linegridcolor
    {0xe4974c, 0x1339}, // columngridenabled
    {0xe49760, 0x133a}, // columngridcolor
    {0xe49770, 0x133b}, // highlightcolorforeground
    {0xe4978c, 0x133c}, // highlightcolorbackground
    {0xe497a8, 0x133d}, // backdirtext
    {0xe497b4, 0x133e}, // drivelistsize
    {0xe497c4, 0x133f}, // backgroundcolor
    {0xe497d4, 0x1340}, // backgroundimage
    {0xe497e4, 0x1341}, // backgroundtransparent
    {0xe497fc, 0x1342}, // filter
};
constexpr LegacyRegistration kFileBrowserReg = {kFileBrowser, 68, 2, 0, 0x1aa52ea4, 0x3aa52e64, &ApplyFileBrowserProperties};

// GZWinTreeView (original 0x863115)
constexpr LegacyName kTreeView[] = {
    {0xe49804, 0x3ae8bae1}, // GZWinTreeView
    {0xe49814, 0x3ae8bad2}, // IGZWinTreeView
    {0xe49824, 0x1401}, // displayroot
    {0xe49830, 0x1402}, // displaynodeicons
    {0xe49844, 0x1403}, // displaytreelines
    {0xe49858, 0x1404}, // multiselect
    {0xe49864, 0x1405}, // icongutter
    {0xe49870, 0x1406}, // minmaxboxsize
    {0xe49880, 0x1407}, // roottext
    {0xe4988c, 0x1408}, // rooticon
    {0xe49898, 0x1409}, // rooticonselected
    {0xe498ac, 0x140a}, // rooticonexpanded
    {0xe498c0, 0x140b}, // rooticonexpandedselected
    {0xe498dc, 0x140c}, // rootcolorforeground
    {0xe498f0, 0x140d}, // rootcolorbackground
    {0xe49904, 0x140e}, // rootcolorforegroundhilited
    {0xe49920, 0x140f}, // rootcolorbackgroundhilited
    {0xe4993c, 0x1410}, // rootfont
    {0xe49948, 0x1411}, // rootexpanded
    {0xe49958, 0x1412}, // rootselected
};
constexpr LegacyRegistration kTreeViewReg = {kTreeView, 20, 2, 0, 0x3ae8bae1, 0x3ae8bad2, &ApplyTreeViewProperties};

// GZWinFolders (original 0x860508)
constexpr LegacyName kFolders[] = {
    {0xe49988, 0x78e8055}, // GZWinFolders
    {0xe49998, 0xa13351b8}, // IGZWinFolders
};
constexpr LegacyRegistration kFoldersReg = {kFolders, 2, 2, 0, 0x78e8055, 0xa13351b8, &ApplyFoldersProperties};

// GZWinGrid (original 0x8605b7)
constexpr LegacyName kGrid[] = {
    {0xe499a8, 0xdaa6b9bf}, // GZWinGrid
    {0xe499b4, 0xdaa6b9be}, // IGZWinGrid
    {0xe499c0, 0x1601}, // fontcolor
    {0xe499cc, 0x1602}, // textalign
    {0xe499d8, 0x1603}, // textwrapping
    {0xe499e8, 0x1604}, // olinecolor
    {0xe499f4, 0x1605}, // selrule
    {0xe499fc, 0x1606}, // maxselcount
    {0xe49a08, 0x1607}, // dcolwidth
    {0xe49a14, 0x1608}, // drowheight
    {0xe49a20, 0x1609}, // enablehdr
    {0xe49a2c, 0x160a}, // colhdrsz
    {0xe49a38, 0x160b}, // rowhdrsz
    {0xe49a44, 0x160c}, // fnone
    {0xe49a4c, 0x160d}, // fedit
    {0xe49a54, 0x160e}, // fhscroll
    {0xe49a60, 0x160f}, // fvscroll
    {0xe49a6c, 0x1610}, // fcolheading
    {0xe49a78, 0x1611}, // frowheading
    {0xe49a84, 0x1612}, // fcolgrid
    {0xe49a90, 0x1613}, // frowgrid
    {0xe49a9c, 0x1614}, // finsmode
    {0xe49aa8, 0x1615}, // fopqbkgnd
    {0xe49ab4, 0x1616}, // fdoutline
    {0xe49ac0, 0x1617}, // fdgridoutline
    {0xe49ad0, 0x1618}, // fcellwrap
    {0xe49adc, 0x1619}, // fallownosel
    {0xe49ae8, 0x161a}, // ffixcolcnt
    {0xe49af4, 0x161b}, // ffixrowcnt
    {0xe49b00, 0x161c}, // fdpastlastcol
    {0xe49b10, 0x161d}, // fdpastlastrow
    {0xe49b20, 0x161e}, // fcelloverlap
    {0xe49b30, 0x161f}, // fdrpdnmenu
    {0xe49b3c, 0x1620}, // fhlcelldata
    {0xe49b48, 0x1621}, // fdefault
    {0xe49b54, 0x1622}, // fall
    {0xe49b5c, 0x1623}, // colgridclr
    {0xe49b68, 0x1624}, // rowgridclr
    {0xe49b74, 0x1625}, // wingridcell
    {0xe49b80, 0x1627}, // wingridrow
    {0xe49b8c, 0x1626}, // wingridcol
};
constexpr LegacyRegistration kGridReg = {kGrid, 41, 2, 0, 0xdaa6b9bf, 0xdaa6b9be, &ApplyGridProperties};

// GZWinCustom (original 0x85f26b)
constexpr LegacyName kCustom[] = {
    {0xe49be0, 0x478d1e6f}, // GZWinCustom
    {0xe49bec, 0x678d1e84}, // IGZWinCustom
};
constexpr LegacyRegistration kCustomReg = {kCustom, 2, 2, 0x140fe0, 0x478d1e6f, 0x678d1e84};

// GZWinScrollbar2 (original 0x861b05)
constexpr LegacyName kScrollbar2[] = {
    {0xe48d70, 0x4b42da5d}, // GZWinScrollbar2
    {0xe48d80, 0x4b42da53}, // IGZWinScrollbar2
    {0xe48d94, 0x1700}, // orientation
    {0xdd4190, 0x1701}, // value
    {0xe48da0, 0x1702}, // minvalue
    {0xe48dac, 0x1703}, // maxvalue
    {0xe48db8, 0x1704}, // visiblerange
    {0xe48dc8, 0x1705}, // pageoverlap
    {0xe48dd4, 0x1706}, // increment
    {0xe48de0, 0x1707}, // arrowsimage
    {0xe48dec, 0x1708}, // containerimage
    {0xe48dfc, 0x1709}, // thumbimage
    {0xde266c, 0xc02}, // horizontal
    {0xde2660, 0xc03}, // vertical
};
constexpr LegacyRegistration kScrollbar2Reg = {kScrollbar2, 14, 2, 0, 0x4b42da5d, 0x4b42da53, &ApplyScrollbar2Properties};

// SC3WinGen (original 0x8635c7)
constexpr LegacyName kGen[] = {
    {0xe4917c, 0x4386d516}, // SC3WinGen
    {0xe49188, 0x4386d516}, // GZWinGen
    {0xe49194, 0x5386d516}, // IGZWinGen
    {0xe491a0, 0x10000}, // userdata
    {0xe491ac, 0x10001}, // blttype
    {0xe491b4, 0x0}, // tiled
    {0xe491bc, 0x2}, // edge
    {0xe491c4, 0x10002}, // divider
    {0xe491cc, 0x10003}, // bluebar
    {0xe491d4, 0x10004}, // moveable
    {0xe491e0, 0x10005}, // sizeable
    {0xe491ec, 0x10006}, // defaultkeys
    {0xe491f8, 0x10007}, // closevisible
    {0xe49208, 0x10008}, // gobackvisible
    {0xe49218, 0x10009}, // minmaxvisible
    {0xe49228, 0x1000a}, // closedisabled
    {0xe49238, 0x1000b}, // gobackdisabled
    {0xe49248, 0x1000c}, // minmaxdisabled
    {0xe49258, 0x1000d}, // titlebar
    {0xdf75b0, 0x1000e}, // paint
    {0xe49264, 0x1000f}, // sidebar
};
constexpr LegacyRegistration kGenReg = {kGen, 21, 3, 0, 0x4386d516, 0x5386d516, &ApplyGenProperties};

} // namespace

// 0x85e431 (clsid)
void __cdecl RegisterCommonNames() { Register(kCommonNamesReg); }

// 0x86024e (GZWinFlatRect)
void __cdecl RegisterFlatRect() { Register(kFlatRectReg); }

// 0x86249d (GZWinText)
void __cdecl RegisterText() { Register(kTextReg); }

// 0x86288d (GZWinTextEdit)
void __cdecl RegisterTextEdit() { Register(kTextEditReg); }

// 0x861263 (GZWinListBox)
void __cdecl RegisterListBox() { Register(kListBoxReg); }

// 0x85e28d (GZWinCombo)
void __cdecl RegisterCombo() { Register(kComboReg); }

// 0x85dba9 (GZWinBtn)
void __cdecl RegisterButton() { Register(kButtonReg); }

// 0x85da3d (GZWinBMP)
void __cdecl RegisterBitmap() { Register(kBitmapReg); }

// 0x861fd1 (GZWinSlider)
void __cdecl RegisterSlider() { Register(kSliderReg); }

// 0x861e67 (GZWinScrollbar)
void __cdecl RegisterScrollbar() { Register(kScrollbarReg); }

// 0x860f01 (GZWinLineInput)
void __cdecl RegisterLineInput() { Register(kLineInputReg); }

// 0x8614af (GZWinOptGrp)
void __cdecl RegisterOptionGroup() { Register(kOptionGroupReg); }

// 0x862173 (GZWinSpinner)
void __cdecl RegisterSpinner() { Register(kSpinnerReg); }

// 0x861999 (GZWinOutline)
void __cdecl RegisterOutline() { Register(kOutlineReg); }

// 0x862fa9 (GZWinTextTicker)
void __cdecl RegisterTextTicker() { Register(kTextTickerReg); }

// 0x85f31a (GZWinFileBrowser)
void __cdecl RegisterFileBrowser() { Register(kFileBrowserReg); }

// 0x863115 (GZWinTreeView)
void __cdecl RegisterTreeView() { Register(kTreeViewReg); }

// 0x860508 (GZWinFolders)
void __cdecl RegisterFolders() { Register(kFoldersReg); }

// 0x8605b7 (GZWinGrid)
void __cdecl RegisterGrid() { Register(kGridReg); }

// 0x85f26b (GZWinCustom)
void __cdecl RegisterCustom() { Register(kCustomReg); }

// 0x861b05 (GZWinScrollbar2)
void __cdecl RegisterScrollbar2() { Register(kScrollbar2Reg); }

// 0x8635c7 (SC3WinGen)
void __cdecl RegisterGen() { Register(kGenReg); }
