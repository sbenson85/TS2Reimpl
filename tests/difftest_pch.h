// The difftest runner's precompiled header (CMakeLists.txt, target_precompile_headers): force-included first in every
// test and harness source. Toolset headers only, and not windows.h: a test that never includes it must not see its
// macros (small, LoadCursor, IN/OUT...), and some include winsock2.h before it. Never project headers either: a change to
// one would rebuild every test. Sources that define a configuration macro these headers read (_CRT_SECURE_NO_WARNINGS,
// WIN32_LEAN_AND_MEAN, _USE_MATH_DEFINES) are built without this header (CMakeLists.txt, ts2_pch_skip_configuring).
#pragma once
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <random>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
