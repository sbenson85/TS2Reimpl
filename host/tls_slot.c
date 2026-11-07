/* ts2tls.dll: an implicit TLS slot for the game image that host/main.cpp maps by hand.

   The Windows loader allocates a module's implicit TLS (its __declspec(thread) data) on every thread,
   but only for modules it loads itself. The host imports this DLL, so the loader gives it a TLS index
   and copies its template into each thread's block, including threads the game or other DLLs create
   later. The host copies the game's TLS data into our template before the game starts any thread
   and points the game's TLS index variable at our index, so the game finds its per-thread data at
   the same offsets as natively.

   The game's TLS block is 8 bytes and its code only uses offset 4 (the CRT's _Init_thread_epoch).
   In this DLL the CRT's _tls_start byte sits at offset 0 of the block, and ts2tls_game (aligned to
   4) follows at offset 4; the host checks that at startup and copies the game's bytes 4..7 there.
   (A hand-written TLS directory without the CRT was ignored by the loader.) */
#include <windows.h>

#define TS2TLS_SIZE 60

__declspec(thread) __declspec(align(4)) unsigned char ts2tls_game[TS2TLS_SIZE] = "TS2TLS-SLOT-MARKER";
extern ULONG _tls_index; /* the CRT's: written by the loader */

__declspec(dllexport) ULONG Ts2TlsIndex(void) { return _tls_index; }
/* This thread's copy of ts2tls_game, and the template it was copied from. */
__declspec(dllexport) unsigned char *Ts2TlsGameData(void) { return ts2tls_game; }
__declspec(dllexport) ULONG Ts2TlsSize(void) { return TS2TLS_SIZE; }
