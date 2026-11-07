#pragma once
// Generated from the binary (scratch script, not committed): the director table's indices. RVAs are
// 1.18.0.187. See COMDirectors.h.
namespace comdir {

enum DirectorIndex : unsigned {
    kDir_MadCow = 0,
    kDir_Draw2D = 1,
    kDir_Graphics = 2,
    kDir_CompGeom = 3,
    kDir_Event = 4,
    kDir_Expat = 5,
    kDir_TSWeb = 6,
    kDir_TSPersist = 7,
    kDir_PathPlan = 8,
    kDir_TSSG = 9,
    kDir_TSSim = 10,
    kDir_TSUI = 11,
    kDir_TSWorld = 12,
    kDir_Script = 13,
    kDir_UIScript = 14,
    kDir_GX = 15,
    kDir_Draw = 16,
    kDir_Font = 17,
    kDir_Win = 18,
    kDir_Win32 = 19,
    kDir_Numerics = 20,
    kDir_ResMan = 21,
    kDir_ResourceW32 = 22,
    kDir_XmlUtils = 23,
    kDir_SceneDBAnim = 24,
    kDir_SceneGraph = 25,
    kDir_CommandParser = 26,
    kDir_Freshness = 27,
    kDir_SndEa = 28,
    kDir_SpeexCodec = 29,
    kDir_Sound = 30,
    kDir_HitMan = 31,
    kDir_HitMp3 = 32,
    kDir_TSEffects = 33,
    kDir_TSMetaData = 34,
    kDir_TSAppFW = 35,
    kDir_RZMessage = 36,
    kDir_Lua5 = 37,
    kDir_TSCommon = 38,
    kDir_TSCAS = 39,
    kDir_TSUICommon = 40,
};
constexpr unsigned kDirectorCount = 41;

#define COMDIR_FOR_EACH_DIRECTOR(X) \
    X(0) \
    X(1) \
    X(2) \
    X(3) \
    X(4) \
    X(5) \
    X(6) \
    X(7) \
    X(8) \
    X(9) \
    X(10) \
    X(11) \
    X(12) \
    X(13) \
    X(14) \
    X(15) \
    X(16) \
    X(17) \
    X(18) \
    X(19) \
    X(20) \
    X(21) \
    X(22) \
    X(23) \
    X(24) \
    X(25) \
    X(26) \
    X(27) \
    X(28) \
    X(29) \
    X(30) \
    X(31) \
    X(32) \
    X(33) \
    X(34) \
    X(35) \
    X(36) \
    X(37) \
    X(38) \
    X(39) \
    X(40)

} // namespace comdir
