// FreeType's build options for our build (FT_CONFIG_OPTIONS_H, CMakeLists.txt "FreeType"): FreeType's defaults, less
// what the game's fonts don't need and what would make the engine depend on the environment.
#pragma once
#include <freetype/config/ftoption.h>

#undef FT_CONFIG_OPTION_ENVIRONMENT_PROPERTIES // no FREETYPE_PROPERTIES: the same glyphs on every machine
#undef FT_CONFIG_OPTION_USE_LZW
#undef FT_CONFIG_OPTION_USE_ZLIB // no WOFF, no compressed PCF
#undef FT_CONFIG_OPTION_MAC_FONTS
#undef FT_CONFIG_OPTION_GUESSING_EMBEDDED_RFORK
#undef FT_CONFIG_OPTION_INCREMENTAL
#undef FT_CONFIG_OPTION_SVG
#undef TT_CONFIG_OPTION_COLOR_LAYERS
#undef TT_CONFIG_OPTION_GX_VAR_SUPPORT
#undef TT_CONFIG_OPTION_BDF
#undef TT_SUPPORT_COLRV1
