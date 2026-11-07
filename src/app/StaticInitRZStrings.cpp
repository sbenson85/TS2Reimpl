// Owned XC entries of the "class-inline" kind whose class is cRZString (lane GY; docs/subsystems/startup.md "Stage 3:
// the static initialisers"): 135 entries, 219 strings. Each image entry builds a cRZString (128 entries), an array of
// them (5) or an array of 36-byte {cRZString, dword} pairs (2) with the constructor inlined, then registers a thunk
// with the CRT's atexit that destroys it (the destructor inlined: vtable stored, the string emptied; an array's thunk
// runs `eh vector destructor iterator` 0x824e6f with the destructor 0x141193 or the pair's 0x1d99b0, last element
// first). The forms of each element (staticinit::RZStringForm) and its text come from a junk-filled run of each image
// entry with two patterns; the difftest checks each entry against the image's. Two entries (slots 643, 644) also
// store the object's address in a pointer global, and register their thunk before constructing (no visible
// difference: the thunk runs at exit).
//
// Ours writes the same bytes in the image's storage and registers its own thunk (one per entry) through
// BoundAtExit() in the same slot, so the LIFO order of the exit list is unchanged; the thunk runs our
// cRZString::Destruct on each element, last first. The objects carry the game's vtable, as cRZString's constructor
// stores it (the class is hook-shaped: every slot of that vtable but the folded 1, 5/6, 7 and 11 is hooked to ours).
// Standalone, each becomes a static cRZString (a `const` one where nothing writes it).
#include "app/StaticInit.h"

#include "game/game.h"
#include "rz/cRZString.h"

#include <array>
#include <cstring>
#include <iterator>
#include <new>
#include <utility>

namespace staticinit {

namespace {

// Each entry's elements, in entry order, then element order.
constexpr RZStringElement kElements[] = {
    {RZStringForm::Construct, 0, 0, "", 0, false}, // [0] slot 108
    {RZStringForm::Characters, 11, 0, "ter_canvas_", 0, false}, // [1] slot 139
    {RZStringForm::Heap, 17, 31, "ter_canvas_cutout", 0, false}, // [2] slot 140
    {RZStringForm::Characters, 6, 0, "NPaint", 0, false}, // [3] slot 141
    {RZStringForm::None, 0, 0, nullptr, 0, false}, // [4] slot 142
    {RZStringForm::Heap, 30, 31, "neighborhood-canvas-outOfWater", 0, false}, // [5] slot 143
    {RZStringForm::Heap, 30, 31, "neighborhood-canvas-underWater", 0, false}, // [6] slot 144
    {RZStringForm::Heap, 28, 31, "neighborhood-canvas-straddle", 0, false}, // [7] slot 145
    {RZStringForm::Heap, 28, 31, "nhoodTerrainLightingMaterial", 0, false}, // [8] slot 146
    {RZStringForm::Heap, 31, 31, "neighborhood-underwater-terrain", 0, false}, // [9] slot 147
    {RZStringForm::Heap, 37, 47, "DefaultResidentialLotImposterMaterial", 0, false}, // [10] slot 149
    {RZStringForm::Heap, 35, 47, "DefaultCommunityLotImposterMaterial", 0, false}, // [11] slot 150
    {RZStringForm::Characters, 14, 0, "lotoutline-res", 0, false}, // [12] slot 151
    {RZStringForm::Characters, 14, 0, "lotoutline-com", 0, false}, // [13] slot 152
    {RZStringForm::Characters, 15, 0, "lotoutline-dorm", 0, false}, // [14] slot 153
    {RZStringForm::Heap, 21, 31, "fxNeighborhoodPlumbob", 0, false}, // [15] slot 154
    {RZStringForm::Heap, 22, 31, "EP3_NhoodPlumbobNoGlow", 0, false}, // [16] slot 155
    {RZStringForm::Heap, 23, 31, "EP3_NhoodSimoleanNoGlow", 0, false}, // [17] slot 156
    {RZStringForm::Heap, 17, 31, "EP3_NhoodSimolean", 0, false}, // [18] slot 157
    {RZStringForm::Heap, 19, 31, "EP6_fxNhoodSuitcase", 0, false}, // [19] slot 158
    {RZStringForm::Heap, 25, 31, "EP6_fxNhoodSuitcaseNoGlow", 0, false}, // [20] slot 159
    {RZStringForm::Characters, 12, 0, "EP6_NhoodBed", 0, false}, // [21] slot 160
    {RZStringForm::Heap, 18, 31, "EP6_NhoodBedNoGlow", 0, false}, // [22] slot 161
    {RZStringForm::Heap, 18, 31, "EP8_NhoodApartment", 0, false}, // [23] slot 162
    {RZStringForm::Heap, 24, 31, "EP8_NhoodApartmentNoGlow", 0, false}, // [24] slot 163
    {RZStringForm::Heap, 36, 47, "nhoodTerrainLightingMaterialImposter", 0, false}, // [25] slot 164
    {RZStringForm::Characters, 14, 0, "lotSkirt_Plain", 0, false}, // [26] slot 187
    {RZStringForm::Heap, 17, 31, "lotSkirt_Neighbor", 0, false}, // [27] slot 188
    {RZStringForm::None, 0, 0, nullptr, 0, false}, // [28] slot 196
    {RZStringForm::Heap, 24, 31, "lotSegmentForUpload.sims", 0, false}, // [29] slot 198
    {RZStringForm::Heap, 27, 31, "familySegmentForUpload.sims", 0, false}, // [30] slot 199
    {RZStringForm::Heap, 35, 47, "characterSegmentForUpload_%08x.sims", 0, false}, // [31] slot 200
    {RZStringForm::None, 0, 0, nullptr, 0, false}, // [32] slot 201
    {RZStringForm::Characters, 3, 0, "sim", 0, false}, // [33] slot 202
    {RZStringForm::Characters, 7, 0, "Unknown", 0, false}, // [34] slot 203
    {RZStringForm::Construct, 6, 0, "Object", 0, false}, // [35] slot 203
    {RZStringForm::Construct, 3, 0, "Sim", 0, false}, // [36] slot 203
    {RZStringForm::Construct, 3, 0, "Lot", 0, false}, // [37] slot 203
    {RZStringForm::Construct, 11, 0, "CompleteSim", 0, false}, // [38] slot 203
    {RZStringForm::Construct, 5, 0, "Floor", 0, false}, // [39] slot 203
    {RZStringForm::Construct, 4, 0, "Wall", 0, false}, // [40] slot 203
    {RZStringForm::Construct, 4, 0, "Roof", 0, false}, // [41] slot 203
    {RZStringForm::Construct, 5, 0, "Fence", 0, false}, // [42] slot 203
    {RZStringForm::Construct, 13, 0, "DesignTexture", 0, false}, // [43] slot 203
    {RZStringForm::Construct, 12, 0, "TerrainPaint", 0, false}, // [44] slot 203
    {RZStringForm::Construct, 18, 0, "NeighborhoodObject", 0, false}, // [45] slot 203
    {RZStringForm::Construct, 3, 0, "Pet", 0, false}, // [46] slot 203
    {RZStringForm::Characters, 11, 0, "UnknownType", 0, false}, // [47] slot 204
    {RZStringForm::Construct, 3, 0, "Lot", 0, false}, // [48] slot 204
    {RZStringForm::Construct, 6, 0, "Family", 0, false}, // [49] slot 204
    {RZStringForm::Construct, 6, 0, "Person", 0, false}, // [50] slot 204
    {RZStringForm::Construct, 7, 0, "SimSkin", 0, false}, // [51] slot 204
    {RZStringForm::Construct, 6, 0, "Object", 0, false}, // [52] slot 204
    {RZStringForm::Construct, 11, 0, "WallPattern", 0, false}, // [53] slot 204
    {RZStringForm::Construct, 11, 0, "RoofPattern", 0, false}, // [54] slot 204
    {RZStringForm::Construct, 12, 0, "FloorPattern", 0, false}, // [55] slot 204
    {RZStringForm::Construct, 12, 0, "TerrainPaint", 0, false}, // [56] slot 204
    {RZStringForm::Construct, 5, 0, "Fence", 0, false}, // [57] slot 204
    {RZStringForm::Construct, 9, 0, "FenceArch", 0, false}, // [58] slot 204
    {RZStringForm::Construct, 18, 0, "DesignModeResource", 0, false}, // [59] slot 204
    {RZStringForm::Construct, 18, 0, "NeighborhoodObject", 0, false}, // [60] slot 204
    {RZStringForm::Construct, 8, 0, "Skintone", 0, false}, // [61] slot 204
    {RZStringForm::Construct, 8, 0, "Hairtone", 0, false}, // [62] slot 204
    {RZStringForm::Construct, 3, 0, "Top", 0, false}, // [63] slot 204
    {RZStringForm::Construct, 8, 0, "Fullbody", 0, false}, // [64] slot 204
    {RZStringForm::Construct, 6, 0, "Bottom", 0, false}, // [65] slot 204
    {RZStringForm::Construct, 8, 0, "Accesory", 0, false}, // [66] slot 204
    {RZStringForm::Construct, 10, 0, "Facialhair", 0, false}, // [67] slot 204
    {RZStringForm::Construct, 8, 0, "Eyebrows", 0, false}, // [68] slot 204
    {RZStringForm::Construct, 8, 0, "Lipcolor", 0, false}, // [69] slot 204
    {RZStringForm::Construct, 8, 0, "Eyecolor", 0, false}, // [70] slot 204
    {RZStringForm::Construct, 14, 0, "FullFaceMakeup", 0, false}, // [71] slot 204
    {RZStringForm::Construct, 14, 0, "FaceAccesories", 0, false}, // [72] slot 204
    {RZStringForm::Construct, 5, 0, "Blush", 0, false}, // [73] slot 204
    {RZStringForm::Construct, 9, 0, "EyeMakeup", 0, false}, // [74] slot 204
    {RZStringForm::Construct, 5, 0, "Breed", 0, false}, // [75] slot 204
    {RZStringForm::Construct, 4, 0, "Misc", 0, false}, // [76] slot 204
    {RZStringForm::Heap, 22, 31, "terrain-seabed-shallow", 0, false}, // [77] slot 209
    {RZStringForm::Heap, 19, 31, "terrain-seabed-deep", 0, false}, // [78] slot 210
    {RZStringForm::Characters, 13, 0, "terrain-beach", 0, false}, // [79] slot 211
    {RZStringForm::None, 0, 0, nullptr, 0, false}, // [80] slot 221
    {RZStringForm::Characters, 9, 0, "Temperate", 0, false}, // [81] slot 222
    {RZStringForm::Characters, 3, 0, "cx_", 0, false}, // [82] slot 226
    {RZStringForm::None, 0, 0, nullptr, 0, false}, // [83] slot 228
    {RZStringForm::None, 0, 0, nullptr, 0, false}, // [84] slot 228
    {RZStringForm::Characters, 7, 0, "Grouchy", 0, false}, // [85] slot 228
    {RZStringForm::Construct, 6, 0, "Active", 0, false}, // [86] slot 228
    {RZStringForm::Construct, 4, 0, "Lazy", 0, false}, // [87] slot 228
    {RZStringForm::Construct, 8, 0, "Generous", 0, false}, // [88] slot 228
    {RZStringForm::Construct, 7, 0, "Selfish", 0, false}, // [89] slot 228
    {RZStringForm::Construct, 7, 0, "Playful", 0, false}, // [90] slot 228
    {RZStringForm::Construct, 7, 0, "Serious", 0, false}, // [91] slot 228
    {RZStringForm::Construct, 8, 0, "Outgoing", 0, false}, // [92] slot 228
    {RZStringForm::Construct, 3, 0, "Shy", 0, false}, // [93] slot 228
    {RZStringForm::Construct, 4, 0, "Neat", 0, false}, // [94] slot 228
    {RZStringForm::Construct, 6, 0, "Sloppy", 0, false}, // [95] slot 228
    {RZStringForm::Construct, 8, 0, "Cleaning", 0, false}, // [96] slot 228
    {RZStringForm::Construct, 7, 0, "Cooking", 0, false}, // [97] slot 228
    {RZStringForm::Construct, 6, 0, "Social", 0, false}, // [98] slot 228
    {RZStringForm::Construct, 6, 0, "Repair", 0, false}, // [99] slot 228
    {RZStringForm::Construct, 9, 0, "Gardening", 0, false}, // [100] slot 228
    {RZStringForm::Construct, 5, 0, "Music", 0, false}, // [101] slot 228
    {RZStringForm::Construct, 8, 0, "Creative", 0, false}, // [102] slot 228
    {RZStringForm::Construct, 8, 0, "Literacy", 0, false}, // [103] slot 228
    {RZStringForm::Construct, 8, 0, "Physical", 0, false}, // [104] slot 228
    {RZStringForm::Construct, 5, 0, "Logic", 0, false}, // [105] slot 228
    {RZStringForm::Characters, 5, 0, "Crush", 0, false}, // [106] slot 229
    {RZStringForm::Construct, 4, 0, "Love", 0, false}, // [107] slot 229
    {RZStringForm::Construct, 7, 0, "Engaged", 0, false}, // [108] slot 229
    {RZStringForm::Construct, 7, 0, "Married", 0, false}, // [109] slot 229
    {RZStringForm::Construct, 6, 0, "Friend", 0, false}, // [110] slot 229
    {RZStringForm::Construct, 11, 0, "Best Friend", 0, false}, // [111] slot 229
    {RZStringForm::Construct, 12, 0, "Going Steady", 0, false}, // [112] slot 229
    {RZStringForm::Construct, 5, 0, "Enemy", 0, false}, // [113] slot 229
    {RZStringForm::None, 0, 0, nullptr, 0, false}, // [114] slot 232
    {RZStringForm::None, 0, 0, nullptr, 0, false}, // [115] slot 344
    {RZStringForm::Construct, 23, 0, "ResidentialLotHighlight", 0, false}, // [116] slot 347
    {RZStringForm::Construct, 21, 0, "CommunityLotHighlight", 0, false}, // [117] slot 348
    {RZStringForm::Construct, 16, 0, "DormLotHighlight", 0, false}, // [118] slot 349
    {RZStringForm::Construct, 17, 0, "NotOKLotHighlight", 0, false}, // [119] slot 350
    {RZStringForm::Construct, 23, 0, "ResidentialLotHighlight", 0, false}, // [120] slot 351
    {RZStringForm::Construct, 21, 0, "CommunityLotHighlight", 0, false}, // [121] slot 352
    {RZStringForm::Construct, 16, 0, "DormLotHighlight", 0, false}, // [122] slot 353
    {RZStringForm::Construct, 17, 0, "NotOKLotHighlight", 0, false}, // [123] slot 354
    {RZStringForm::Construct, 23, 0, "ResidentialLotHighlight", 0, false}, // [124] slot 355
    {RZStringForm::Construct, 21, 0, "CommunityLotHighlight", 0, false}, // [125] slot 356
    {RZStringForm::Construct, 16, 0, "DormLotHighlight", 0, false}, // [126] slot 357
    {RZStringForm::Construct, 17, 0, "NotOKLotHighlight", 0, false}, // [127] slot 358
    {RZStringForm::Construct, 23, 0, "ResidentialLotHighlight", 0, false}, // [128] slot 359
    {RZStringForm::Construct, 21, 0, "CommunityLotHighlight", 0, false}, // [129] slot 360
    {RZStringForm::Construct, 16, 0, "DormLotHighlight", 0, false}, // [130] slot 361
    {RZStringForm::Construct, 17, 0, "NotOKLotHighlight", 0, false}, // [131] slot 362
    {RZStringForm::Construct, 0, 0, "", 0, false}, // [132] slot 365
    {RZStringForm::Construct, 5, 0, "raise", 0, false}, // [133] slot 365
    {RZStringForm::Construct, 5, 0, "lower", 0, false}, // [134] slot 365
    {RZStringForm::Construct, 6, 0, "smooth", 0, false}, // [135] slot 365
    {RZStringForm::Construct, 25, 0, "housePlacementOKHighlight", 0, false}, // [136] slot 369
    {RZStringForm::Construct, 28, 0, "housePlacementNotOKHighlight", 0, false}, // [137] slot 370
    {RZStringForm::Construct, 25, 0, "housePlacementOKHighlight", 0, false}, // [138] slot 371
    {RZStringForm::Construct, 28, 0, "housePlacementNotOKHighlight", 0, false}, // [139] slot 372
    {RZStringForm::Construct, 25, 0, "housePlacementOKHighlight", 0, false}, // [140] slot 373
    {RZStringForm::Construct, 28, 0, "housePlacementNotOKHighlight", 0, false}, // [141] slot 374
    {RZStringForm::Construct, 1, 0, "t", 0, false}, // [142] slot 465
    {RZStringForm::Construct, 1, 0, "f", 0, false}, // [143] slot 466
    {RZStringForm::Construct, 1, 0, "m", 0, false}, // [144] slot 467
    {RZStringForm::Construct, 1, 0, "n", 0, false}, // [145] slot 468
    {RZStringForm::Construct, 1, 0, "t", 0, false}, // [146] slot 469
    {RZStringForm::Construct, 1, 0, "f", 0, false}, // [147] slot 470
    {RZStringForm::Construct, 1, 0, "m", 0, false}, // [148] slot 471
    {RZStringForm::Construct, 1, 0, "n", 0, false}, // [149] slot 472
    {RZStringForm::Construct, 1, 0, "t", 0, false}, // [150] slot 473
    {RZStringForm::Construct, 1, 0, "f", 0, false}, // [151] slot 474
    {RZStringForm::Construct, 1, 0, "m", 0, false}, // [152] slot 475
    {RZStringForm::Construct, 1, 0, "n", 0, false}, // [153] slot 476
    {RZStringForm::Construct, 25, 0, "housePlacementOKHighlight", 0, false}, // [154] slot 486
    {RZStringForm::Construct, 28, 0, "housePlacementNotOKHighlight", 0, false}, // [155] slot 487
    {RZStringForm::Construct, 15, 0, "roadIndicatorOK", 0, false}, // [156] slot 488
    {RZStringForm::Construct, 18, 0, "roadIndicatorNotOK", 0, false}, // [157] slot 489
    {RZStringForm::Construct, 6, 0, "Desert", 0, false}, // [158] slot 491
    {RZStringForm::Construct, 9, 0, "Temperate", 0, false}, // [159] slot 492
    {RZStringForm::Construct, 4, 0, "Dirt", 0, false}, // [160] slot 493
    {RZStringForm::Construct, 8, 0, "Concrete", 0, false}, // [161] slot 494
    {RZStringForm::Characters, 13, 0, "terrain-cliff", 0, false}, // [162] slot 498
    {RZStringForm::Heap, 17, 31, "f_hair_constraint", 0x00000002, true}, // [163] slot 623
    {RZStringForm::Construct, 17, 0, "b_hair_constraint", 0x00000002, true}, // [164] slot 623
    {RZStringForm::Construct, 17, 0, "l_hair_constraint", 0x00000002, true}, // [165] slot 623
    {RZStringForm::Construct, 17, 0, "r_hair_constraint", 0x00000002, true}, // [166] slot 623
    {RZStringForm::Construct, 13, 0, "l_hair_bounce", 0x00000001, true}, // [167] slot 623
    {RZStringForm::Construct, 13, 0, "r_hair_bounce", 0x00000001, true}, // [168] slot 623
    {RZStringForm::Construct, 11, 0, "b_hair_boom", 0x00000000, true}, // [169] slot 623
    {RZStringForm::Construct, 11, 0, "l_hair_boom", 0x00000000, true}, // [170] slot 623
    {RZStringForm::Construct, 11, 0, "r_hair_boom", 0x00000000, true}, // [171] slot 623
    {RZStringForm::Characters, 6, 0, "auskel", 0, false}, // [172] slot 624
    {RZStringForm::Construct, 6, 0, "tuskel", 0x3f800000, true}, // [173] slot 624
    {RZStringForm::Construct, 6, 0, "cuskel", 0x3f19999a, true}, // [174] slot 624
    {RZStringForm::Construct, 6, 0, "puskel", 0x3ecccccd, true}, // [175] slot 624
    {RZStringForm::Construct, 14, 0, "AllowTextAsKey", 0, false}, // [176] slot 643
    {RZStringForm::Construct, 16, 0, "HideGameSegments", 0, false}, // [177] slot 644
    {RZStringForm::Construct, 4, 0, ".mp3", 0, false}, // [178] slot 646
    {RZStringForm::Heap, 103, 111, "Please make sure that The Sims 2 and any of its tools are not running before starting this application.", 0, false}, // [179] slot 654
    {RZStringForm::Heap, 111, 111, "Veuillez vous assurer que Les Sims 2 ou l'un de ses outils ne fonctionne pas avant de lancer cette application.", 0, false}, // [180] slot 655
    {RZStringForm::Heap, 125, 127, "Bitte vergewissere dich, dass weder Die Sims 2 und noch eines der dazugeh\xf6" "rigen Tools l\xe4" "uft, bevor du die Anwendung startest.", 0, false}, // [181] slot 656
    {RZStringForm::Heap, 101, 111, "Assicurati che The Sims 2 e i suoi strumenti non siano in esecuzione prima di avviare l'applicazione.", 0, false}, // [182] slot 657
    {RZStringForm::Heap, 115, 127, "Aseg\xfa" "rate de que ni Los Sims 2 ni ninguna de sus herramientas se est\xe9" "n ejecutando antes de iniciar esta aplicaci\xf3" "n.", 0, false}, // [183] slot 658
    {RZStringForm::Heap, 100, 111, "Zorg ervoor dat De Sims 2 of gerelateerde programma's niet draaien voordat je dit programma opstart.", 0, false}, // [184] slot 659
    {RZStringForm::Heap, 115, 127, "S\xf8" "rg venligst for, at hverken The Sims 2 eller nogen tilknyttede v\xe6" "rkt\xf8" "jer k\xf8" "rer, f\xf8" "r du starter denne applikation.", 0, false}, // [185] slot 660
    {RZStringForm::Heap, 94, 95, "Se till att varken The Sims 2 eller n\xe5" "got av dess verktyg k\xf6" "rs innan du startar detta program.", 0, false}, // [186] slot 661
    {RZStringForm::Heap, 106, 111, "Sjekk at The Sims 2 og verkt\xf8" "y som har med The Sims 2 \xe5" " gj\xf8" "re ikke kj\xf8" "rer n\xe5" "r du starter dette programmet.", 0, false}, // [187] slot 662
    {RZStringForm::Heap, 123, 127, "Ennen pelin k\xe4" "ynnist\xe4" "mist\xe4" " sinun tulisi varmistaa, ett\xe4" " The Sims 2 tai mik\xe4" "\xe4" "n muu The Sims 2 -sovellus ei ole jo k\xe4" "ynniss\xe4" ".", 0, false}, // [188] slot 663
    {RZStringForm::Heap, 124, 127, "Antes de iniciar esta aplica\xe7" "\xe3" "o, certifique-se de que The Sims 2 e qualquer das suas ferramentas n\xe3" "o est\xe3" "o a ser executados.", 0, false}, // [189] slot 664
    {RZStringForm::Heap, 51, 63, "\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?", 0, false}, // [190] slot 665
    {RZStringForm::Heap, 116, 127, "Przed uruchomieniem tej aplikacji upewnij si\?, \?e nie jest ju\? uruchomiona gra The Sims 2, ani \?adne z jej narz\?dzi.", 0, false}, // [191] slot 666
    {RZStringForm::Heap, 32, 47, "\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?2\?\?\?\?\?\?\?\?\?\?", 0, false}, // [192] slot 667
    {RZStringForm::Heap, 32, 47, "\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?2\?\?\?\?\?\?\?\?\?\?\?\?\?", 0, false}, // [193] slot 668
    {RZStringForm::Heap, 118, 127, "\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\? 2 \?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?\?", 0, false}, // [194] slot 669
    {RZStringForm::Heap, 43, 47, "\? \?\?\?\?\? \?\?\?\? \?\? \?\? 2\? \?\? 2\? \?\? \?\?\? \?\?\? \?\?\?.", 0, false}, // [195] slot 670
    {RZStringForm::Heap, 85, 95, "P\?ed spu\x9a" "t\?n\xed" "m t\xe9" "to aplikace je nutn\xe9" " ukon\?it hru The Sims 2 a v\x9a" "echny jej\xed" " n\xe1" "stroje.", 0, false}, // [196] slot 671
    {RZStringForm::Heap, 111, 111, "K\xe9" "rlek, az alkalmaz\xe1" "s elind\xed" "t\xe1" "sa el\?tt gy\?z\?dj meg r\xf3" "la, hogy sem a The Sims 2, sem seg\xe9" "dprogramjai nem futnak.", 0, false}, // [197] slot 672
    {RZStringForm::Heap, 142, 143, "Por favor, tenha certeza de que o The Sims 2 e qualquer uma de suas ferramentas n\xe3" "o estejam sendo executados antes de iniciar esse aplicativo.", 0, false}, // [198] slot 673
    {RZStringForm::None, 0, 0, nullptr, 0, false}, // [199] slot 682
    {RZStringForm::None, 0, 0, nullptr, 0, false}, // [200] slot 734
    {RZStringForm::None, 0, 0, nullptr, 0, false}, // [201] slot 735
    {RZStringForm::Construct, 10, 0, "The Sims 2", 0, false}, // [202] slot 756
    {RZStringForm::Construct, 70, 0, "Direct3D returned an error: %s!\x0a" "\x0a" "The application will now terminate...", 0, false}, // [203] slot 757
    {RZStringForm::Construct, 132, 0, "Failed to initialize Direct3D!\x0a" "\x0a" "Please make sure DirectX 9 has been installed on this system.\x0a" "\x0a" "The application will now terminate...", 0, false}, // [204] slot 758
    {RZStringForm::Construct, 244, 0, "Failed to enumerate any DirectX 9 compatible graphics adapters in this system!\x0a" "\x0a" "Please make sure you have a DirectX 9 compatible graphics adapter and have installed the latest manufacturer provided driver.\x0a" "\x0a" "The application will now terminate...", 0, false}, // [205] slot 759
    {RZStringForm::Construct, 264, 0, "The graphics adapter in this system experienced an internal error!\x0a" "\x0a" "Please contact your graphics adapter manufacturer for technical support.\x0a" "\x0a" "The application will now terminate...The application experienced a rendering error!\x0a" "\x0a" "The application will now terminate...", 0, false}, // [206] slot 760
    {RZStringForm::Construct, 85, 0, "The application experienced a rendering error!\x0a" "\x0a" "The application will now terminate...", 0, false}, // [207] slot 761
    {RZStringForm::Construct, 39, 0, "<\?xml version=\"1.0\" encoding=\"UTF-8\" \?>", 0, false}, // [208] slot 763
    {RZStringForm::Construct, 3, 0, "://", 0, false}, // [209] slot 767
    {RZStringForm::Construct, 1, 0, "/", 0, false}, // [210] slot 768
    {RZStringForm::Heap, 19, 31, "EP8_glowWitchGoodFX", 0, false}, // [211] slot 786
    {RZStringForm::Heap, 19, 31, "EP8_glowWitchEvilFX", 0, false}, // [212] slot 787
    {RZStringForm::Heap, 21, 31, "EP8_glowRingWitchGood", 0, false}, // [213] slot 788
    {RZStringForm::Heap, 21, 31, "EP8_glowRingWitchEvil", 0, false}, // [214] slot 789
    {RZStringForm::Heap, 22, 31, "EP8_glowWitchGoodHands", 0, false}, // [215] slot 790
    {RZStringForm::Heap, 22, 31, "EP8_glowWitchEvilHands", 0, false}, // [216] slot 791
    {RZStringForm::Heap, 18, 31, "Sims2 Packager 1.0", 0, false}, // [217] slot 792
    {RZStringForm::Construct, 9, 0, "european_", 0, false}, // [218] slot 800
};

// Slot order: slot, the image's initialiser, storage, count, stride, array, first element, alias.
constexpr RZStringStatic kEntries[] = {
    {108, 0x138479, 0x10cb728, 1, 0x20, false, 0, 0x0}, // atexit 0xd90773
    {139, 0x138950, 0xffa3dc, 1, 0x20, false, 1, 0x0}, // atexit 0xd90f40
    {140, 0x1388f0, 0x10d8dd8, 1, 0x20, false, 2, 0x0}, // atexit 0xd90ee0
    {141, 0x138b10, 0xffa7a8, 1, 0x20, false, 3, 0x0}, // atexit 0xd91120
    {142, 0x138990, 0xffa7c8, 1, 0x20, false, 4, 0x0}, // atexit 0xd90fa0
    {143, 0x138a90, 0x10d8e2c, 1, 0x20, false, 5, 0x0}, // atexit 0xd910c0
    {144, 0x138bb0, 0x10d8e4c, 1, 0x20, false, 6, 0x0}, // atexit 0xd911e0
    {145, 0x138b40, 0x10d8e6c, 1, 0x20, false, 7, 0x0}, // atexit 0xd91180
    {146, 0x1389a0, 0x10d8e8c, 1, 0x20, false, 8, 0x0}, // atexit 0xd91000
    {147, 0x138a10, 0x10d8eac, 1, 0x20, false, 9, 0x0}, // atexit 0xd91060
    {149, 0x138d00, 0x10d8f68, 1, 0x20, false, 10, 0x0}, // atexit 0xd91360
    {150, 0x138c80, 0x10d8f88, 1, 0x20, false, 11, 0x0}, // atexit 0xd91300
    {151, 0x139220, 0xffb1dc, 1, 0x20, false, 12, 0x0}, // atexit 0xd91840
    {152, 0x138c40, 0xffb1fc, 1, 0x20, false, 13, 0x0}, // atexit 0xd912a0
    {153, 0x138d70, 0xffb21c, 1, 0x20, false, 14, 0x0}, // atexit 0xd913c0
    {154, 0x138f70, 0x10d8fa8, 1, 0x20, false, 15, 0x0}, // atexit 0xd91600
    {155, 0x139000, 0x10d8fc8, 1, 0x20, false, 16, 0x0}, // atexit 0xd91660
    {156, 0x1390d0, 0x10d8fe8, 1, 0x20, false, 17, 0x0}, // atexit 0xd91720
    {157, 0x139070, 0x10d9008, 1, 0x20, false, 18, 0x0}, // atexit 0xd916c0
    {158, 0x1391b0, 0x10d9028, 1, 0x20, false, 19, 0x0}, // atexit 0xd917e0
    {159, 0x139140, 0x10d9048, 1, 0x20, false, 20, 0x0}, // atexit 0xd91780
    {160, 0x138f40, 0xffb23c, 1, 0x20, false, 21, 0x0}, // atexit 0xd915a0
    {161, 0x138ee0, 0x10d9068, 1, 0x20, false, 22, 0x0}, // atexit 0xd91540
    {162, 0x138e80, 0x10d9088, 1, 0x20, false, 23, 0x0}, // atexit 0xd914e0
    {163, 0x138e20, 0x10d90a8, 1, 0x20, false, 24, 0x0}, // atexit 0xd91480
    {164, 0x138db0, 0x10d90c8, 1, 0x20, false, 25, 0x0}, // atexit 0xd91420
    {187, 0x139520, 0xffef00, 1, 0x20, false, 26, 0x0}, // atexit 0xd91d50
    {188, 0x1394c0, 0x10d9548, 1, 0x20, false, 27, 0x0}, // atexit 0xd91cf0
    {196, 0x1397b0, 0x100a110, 1, 0x20, false, 28, 0x0}, // atexit 0xd92080
    {198, 0x13a720, 0x10d9ed8, 1, 0x20, false, 29, 0x0}, // atexit 0xd92300
    {199, 0x13a6a0, 0x10d9ef8, 1, 0x20, false, 30, 0x0}, // atexit 0xd922a0
    {200, 0x13a620, 0x10d9f18, 1, 0x20, false, 31, 0x0}, // atexit 0xd92240
    {201, 0x13a5e0, 0x100a594, 1, 0x20, false, 32, 0x0}, // atexit 0xd92180
    {202, 0x13a5f0, 0x100a5b4, 1, 0x20, false, 33, 0x0}, // atexit 0xd921e0
    {203, 0x1397d0, 0x100a6e8, 13, 0x20, true, 34, 0x0}, // atexit 0xd92140
    {204, 0x139c20, 0x100a888, 30, 0x20, true, 47, 0x0}, // atexit 0xd92160
    {209, 0x13a8a0, 0x10da1f8, 1, 0x20, false, 77, 0x0}, // atexit 0xd926c0
    {210, 0x13a830, 0x10da218, 1, 0x20, false, 78, 0x0}, // atexit 0xd92660
    {211, 0x13a800, 0x100d414, 1, 0x20, false, 79, 0x0}, // atexit 0xd92600
    {221, 0x13aa20, 0x1012914, 1, 0x20, false, 80, 0x0}, // atexit 0xd92b10
    {222, 0x13aae0, 0x1013230, 1, 0x20, false, 81, 0x0}, // atexit 0xd92c80
    {226, 0x13ab20, 0x10134e4, 1, 0x20, false, 82, 0x0}, // atexit 0xd92d40
    {228, 0x13ab90, 0x1014578, 23, 0x20, true, 83, 0x0}, // atexit 0xd92e10
    {229, 0x13b200, 0x1014858, 8, 0x20, true, 106, 0x0}, // atexit 0xd92e30
    {232, 0x13b490, 0x10149f0, 1, 0x20, false, 114, 0x0}, // atexit 0xd92f70
    {344, 0x13ccfc, 0x10235f4, 1, 0x20, false, 115, 0x0}, // atexit 0xd947a5
    {347, 0x13cda2, 0x10dca58, 1, 0x20, false, 116, 0x0}, // atexit 0xd947f5
    {348, 0x13cd1e, 0x10dca78, 1, 0x20, false, 117, 0x0}, // atexit 0xd947b9
    {349, 0x13cd4a, 0x10dca98, 1, 0x20, false, 118, 0x0}, // atexit 0xd947cd
    {350, 0x13cd76, 0x10dcab8, 1, 0x20, false, 119, 0x0}, // atexit 0xd947e1
    {351, 0x13ce52, 0x10dcb0c, 1, 0x20, false, 120, 0x0}, // atexit 0xd94845
    {352, 0x13cdce, 0x10dcb2c, 1, 0x20, false, 121, 0x0}, // atexit 0xd94809
    {353, 0x13cdfa, 0x10dcb4c, 1, 0x20, false, 122, 0x0}, // atexit 0xd9481d
    {354, 0x13ce26, 0x10dcb6c, 1, 0x20, false, 123, 0x0}, // atexit 0xd94831
    {355, 0x13cf02, 0x10dcbc0, 1, 0x20, false, 124, 0x0}, // atexit 0xd94895
    {356, 0x13ce7e, 0x10dcbe0, 1, 0x20, false, 125, 0x0}, // atexit 0xd94859
    {357, 0x13ceaa, 0x10dcc00, 1, 0x20, false, 126, 0x0}, // atexit 0xd9486d
    {358, 0x13ced6, 0x10dcc20, 1, 0x20, false, 127, 0x0}, // atexit 0xd94881
    {359, 0x13cfb2, 0x10dcc74, 1, 0x20, false, 128, 0x0}, // atexit 0xd948e5
    {360, 0x13cf2e, 0x10dcc94, 1, 0x20, false, 129, 0x0}, // atexit 0xd948a9
    {361, 0x13cf5a, 0x10dccb4, 1, 0x20, false, 130, 0x0}, // atexit 0xd948bd
    {362, 0x13cf86, 0x10dccd4, 1, 0x20, false, 131, 0x0}, // atexit 0xd948d1
    {365, 0x13d011, 0x10dcdd0, 4, 0x20, true, 132, 0x0}, // atexit 0xd948f9
    {369, 0x13d103, 0x10dd194, 1, 0x20, false, 136, 0x0}, // atexit 0xd9497d
    {370, 0x13d0d7, 0x10dd1b4, 1, 0x20, false, 137, 0x0}, // atexit 0xd94969
    {371, 0x13d15b, 0x10dd1e4, 1, 0x20, false, 138, 0x0}, // atexit 0xd949a5
    {372, 0x13d12f, 0x10dd204, 1, 0x20, false, 139, 0x0}, // atexit 0xd94991
    {373, 0x13d1b3, 0x10dd234, 1, 0x20, false, 140, 0x0}, // atexit 0xd949cd
    {374, 0x13d187, 0x10dd254, 1, 0x20, false, 141, 0x0}, // atexit 0xd949b9
    {465, 0x13db74, 0x10de738, 1, 0x20, false, 142, 0x0}, // atexit 0xd94d21
    {466, 0x13daf0, 0x10de758, 1, 0x20, false, 143, 0x0}, // atexit 0xd94ce5
    {467, 0x13db48, 0x10de778, 1, 0x20, false, 144, 0x0}, // atexit 0xd94d0d
    {468, 0x13db1c, 0x10de798, 1, 0x20, false, 145, 0x0}, // atexit 0xd94cf9
    {469, 0x13dc24, 0x10de7f4, 1, 0x20, false, 146, 0x0}, // atexit 0xd94d71
    {470, 0x13dba0, 0x10de814, 1, 0x20, false, 147, 0x0}, // atexit 0xd94d35
    {471, 0x13dbf8, 0x10de834, 1, 0x20, false, 148, 0x0}, // atexit 0xd94d5d
    {472, 0x13dbcc, 0x10de854, 1, 0x20, false, 149, 0x0}, // atexit 0xd94d49
    {473, 0x13dcd4, 0x10de874, 1, 0x20, false, 150, 0x0}, // atexit 0xd94dc1
    {474, 0x13dc50, 0x10de894, 1, 0x20, false, 151, 0x0}, // atexit 0xd94d85
    {475, 0x13dca8, 0x10de8b4, 1, 0x20, false, 152, 0x0}, // atexit 0xd94dad
    {476, 0x13dc7c, 0x10de8d4, 1, 0x20, false, 153, 0x0}, // atexit 0xd94d99
    {486, 0x13de65, 0x10deefc, 1, 0x20, false, 154, 0x0}, // atexit 0xd94e07
    {487, 0x13de39, 0x10def1c, 1, 0x20, false, 155, 0x0}, // atexit 0xd94df3
    {488, 0x13debd, 0x10def3c, 1, 0x20, false, 156, 0x0}, // atexit 0xd94e2f
    {489, 0x13de91, 0x10def5c, 1, 0x20, false, 157, 0x0}, // atexit 0xd94e1b
    {491, 0x13df30, 0x10df288, 1, 0x20, false, 158, 0x0}, // atexit 0xd94e89
    {492, 0x13df88, 0x10df2a8, 1, 0x20, false, 159, 0x0}, // atexit 0xd94eb1
    {493, 0x13df5c, 0x10df2c8, 1, 0x20, false, 160, 0x0}, // atexit 0xd94e9d
    {494, 0x13df04, 0x10df2e8, 1, 0x20, false, 161, 0x0}, // atexit 0xd94e75
    {498, 0x13e000, 0x104ee68, 1, 0x20, false, 162, 0x0}, // atexit 0xd95030
    {623, 0x13ef90, 0x10f8da0, 9, 0x24, true, 163, 0x0}, // atexit 0xd963d0
    {624, 0x13f3a0, 0x1071460, 4, 0x24, true, 172, 0x0}, // atexit 0xd963f0
    {643, 0x13f685, 0x10fae18, 1, 0x20, false, 176, 0x10fae58}, // atexit 0xd969ed
    {644, 0x13f6bc, 0x10fae38, 1, 0x20, false, 177, 0x10fae5c}, // atexit 0xd96a01
    {646, 0x13f6f3, 0x10faed0, 1, 0x20, false, 178, 0x0}, // atexit 0xd96a53
    {654, 0x13f930, 0x10fb08c, 1, 0x20, false, 179, 0x0}, // atexit 0xd96df0
    {655, 0x13f9f0, 0x10fb0ac, 1, 0x20, false, 180, 0x0}, // atexit 0xd96eb0
    {656, 0x13fa50, 0x10fb0cc, 1, 0x20, false, 181, 0x0}, // atexit 0xd96f10
    {657, 0x13fb10, 0x10fb0ec, 1, 0x20, false, 182, 0x0}, // atexit 0xd96fd0
    {658, 0x13fdf0, 0x10fb10c, 1, 0x20, false, 183, 0x0}, // atexit 0xd97270
    {659, 0x13f8d0, 0x10fb12c, 1, 0x20, false, 184, 0x0}, // atexit 0xd96d90
    {660, 0x13f870, 0x10fb14c, 1, 0x20, false, 185, 0x0}, // atexit 0xd96d30
    {661, 0x13fe50, 0x10fb16c, 1, 0x20, false, 186, 0x0}, // atexit 0xd972d0
    {662, 0x13fc70, 0x10fb18c, 1, 0x20, false, 187, 0x0}, // atexit 0xd970f0
    {663, 0x13f990, 0x10fb1ac, 1, 0x20, false, 188, 0x0}, // atexit 0xd96e50
    {664, 0x13fd30, 0x10fb1cc, 1, 0x20, false, 189, 0x0}, // atexit 0xd971b0
    {665, 0x13fb70, 0x10fb1ec, 1, 0x20, false, 190, 0x0}, // atexit 0xd97030
    {666, 0x13fcd0, 0x10fb20c, 1, 0x20, false, 191, 0x0}, // atexit 0xd97150
    {667, 0x13fd90, 0x10fb22c, 1, 0x20, false, 192, 0x0}, // atexit 0xd97210
    {668, 0x13ff10, 0x10fb24c, 1, 0x20, false, 193, 0x0}, // atexit 0xd97390
    {669, 0x13feb0, 0x10fb26c, 1, 0x20, false, 194, 0x0}, // atexit 0xd97330
    {670, 0x13fbf0, 0x10fb28c, 1, 0x20, false, 195, 0x0}, // atexit 0xd97090
    {671, 0x13f810, 0x10fb2ac, 1, 0x20, false, 196, 0x0}, // atexit 0xd96cd0
    {672, 0x13fab0, 0x10fb2cc, 1, 0x20, false, 197, 0x0}, // atexit 0xd96f70
    {673, 0x13f7b0, 0x10fb2ec, 1, 0x20, false, 198, 0x0}, // atexit 0xd96c70
    {682, 0x140040, 0x107bb78, 1, 0x20, false, 199, 0x0}, // atexit 0xd975c0
    {734, 0x14052c, 0x10805e0, 1, 0x20, false, 200, 0x0}, // atexit 0xd98800
    {735, 0x140538, 0x1080600, 1, 0x20, false, 201, 0x0}, // atexit 0xd98814
    {756, 0x14074e, 0x10fda54, 1, 0x20, false, 202, 0x0}, // atexit 0xd98915
    {757, 0x1407a6, 0x10fda74, 1, 0x20, false, 203, 0x0}, // atexit 0xd9893d
    {758, 0x14077a, 0x10fda94, 1, 0x20, false, 204, 0x0}, // atexit 0xd98929
    {759, 0x1407fe, 0x10fdab4, 1, 0x20, false, 205, 0x0}, // atexit 0xd98965
    {760, 0x1407d2, 0x10fdad4, 1, 0x20, false, 206, 0x0}, // atexit 0xd98951
    {761, 0x14082a, 0x10fdaf4, 1, 0x20, false, 207, 0x0}, // atexit 0xd98979
    {763, 0x140893, 0x10fe4a0, 1, 0x20, false, 208, 0x0}, // atexit 0xd989d3
    {767, 0x140956, 0x11031a0, 1, 0x20, false, 209, 0x0}, // atexit 0xd98a28
    {768, 0x140982, 0x11031c0, 1, 0x20, false, 210, 0x0}, // atexit 0xd98a3c
    {786, 0x140d40, 0x11078d4, 1, 0x20, false, 211, 0x0}, // atexit 0xd991a0
    {787, 0x140bf0, 0x11078f4, 1, 0x20, false, 212, 0x0}, // atexit 0xd99080
    {788, 0x140cd0, 0x1107914, 1, 0x20, false, 213, 0x0}, // atexit 0xd99140
    {789, 0x140b80, 0x1107934, 1, 0x20, false, 214, 0x0}, // atexit 0xd99020
    {790, 0x140c60, 0x1107954, 1, 0x20, false, 215, 0x0}, // atexit 0xd990e0
    {791, 0x140b10, 0x1107974, 1, 0x20, false, 216, 0x0}, // atexit 0xd98fc0
    {792, 0x140db0, 0x11079a0, 1, 0x20, false, 217, 0x0}, // atexit 0xd99200
    {800, 0x140f00, 0x1107e24, 1, 0x20, false, 218, 0x0}, // atexit 0xd99400
};

cRZString *Element(const RZStringStatic &e, uint32_t i)
{
    return reinterpret_cast<cRZString *>(GameAddress<uint8_t>(e.storage) + i * e.stride);
}

void Build(cRZString *s, const RZStringElement &el)
{
    switch (el.form) {
    case RZStringForm::None:
        break;
    case RZStringForm::Characters:
        // The inline buffer is the first member of the std::string; size and capacity stay as .data has them.
        std::memcpy(reinterpret_cast<uint8_t *>(&s->mString), el.text, el.size);
        break;
    case RZStringForm::Construct:
        s->Construct(el.text);
        break;
    case RZStringForm::Heap: {
        uint8_t *string = reinterpret_cast<uint8_t *>(&s->mString);
        char *buffer = static_cast<char *>(::operator new(el.capacity + 1));
        std::memcpy(buffer, el.text, el.size + 1);
        std::memcpy(string, &buffer, sizeof(buffer));            // _Bx._Ptr
        std::memcpy(string + 0x10, &el.size, sizeof(el.size));    // _Mysize
        std::memcpy(string + 0x14, &el.capacity, sizeof(el.capacity)); // _Myres
        s->mVTable = GameAddress(cRZString::kVTable);
        s->mRefCount = 0;
        break;
    }
    }
    if (el.hasExtra) std::memcpy(reinterpret_cast<uint8_t *>(s) + sizeof(cRZString), &el.extra, sizeof(el.extra));
}

void Destroy(const RZStringStatic &e)
{
    for (uint32_t i = e.count; i != 0;) Element(e, --i)->Destruct();
}

template <size_t I> void __cdecl RZStringThunk() { Destroy(kEntries[I]); }

using Thunk = void(__cdecl *)();
template <size_t... I> constexpr std::array<Thunk, sizeof...(I)> RZStringThunks(std::index_sequence<I...>) { return {&RZStringThunk<I>...}; }
constexpr auto kThunks = RZStringThunks(std::make_index_sequence<std::size(kEntries)>());

constexpr auto kOwned = [] {
    std::array<winmainstart::OwnedInitialiser, std::size(kEntries)> out{};
    for (size_t i = 0; i < std::size(kEntries); ++i) out[i] = {kEntries[i].slot, kEntries[i].imageRva, &InitRZString, &kEntries[i]};
    return out;
}();

constexpr bool Consistent()
{
    uint32_t next = 0;
    for (size_t i = 0; i < std::size(kEntries); ++i) {
        if (kEntries[i].first != next || (i && kEntries[i - 1].slot >= kEntries[i].slot)) return false;
        if (kEntries[i].count != 1 && !kEntries[i].array) return false;
        next += kEntries[i].count;
    }
    return next == std::size(kElements);
}
static_assert(Consistent(), "cRZString statics: elements in entry order, entries in slot order");

} // namespace

void __cdecl InitRZString(const void *rzStringStatic)
{
    const auto &e = *static_cast<const RZStringStatic *>(rzStringStatic);
    for (uint32_t i = 0; i < e.count; ++i) Build(Element(e, i), kElements[e.first + i]);
    if (e.alias) *GameAddress<void *>(e.alias) = Element(e, 0);
    BoundAtExit()(kThunks[&e - kEntries]);
}

const RZStringStatic *RZStringStatics(size_t *count)
{
    *count = std::size(kEntries);
    return kEntries;
}

const RZStringElement *RZStringElements(size_t *count)
{
    *count = std::size(kElements);
    return kElements;
}

const winmainstart::OwnedInitialiser *OwnedRZStrings(size_t *count)
{
    *count = kOwned.size();
    return kOwned.data();
}

} // namespace staticinit
