// The target of every image symbol an image-free program aliases (tools/noimage.py --write-aliases): ts2core objects
// the program loads still name game-image symbols in sections /OPT:REF drops, and MSVC wants each of them resolved.
// If live code reached one, the linker would keep this function, and the gate after the link (noimage.py --check)
// fails the build and deletes the exe; it is never meant to run.
#include <intrin.h>

extern "C" __declspec(noinline) void ts2_noimage_trap()
{
    __fastfail(7); // FAST_FAIL_FATAL_APP_EXIT
}
