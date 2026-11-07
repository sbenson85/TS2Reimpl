#pragma once
#include <cstdint>

// The product helpers (lane MB, F4e): five __cdecl free functions at 0xcb4690..0xcb4dfd, in the game's own code after
// cTSAppCommonImpl. Despite the names callers gave them ("registry lookup"), none of them reads the registry: the product
// registry key is a constant, the installation directory is the executable's, and the product list is a fixed text.
// Callers: cTSAppFW (Init, RegisteredProductName), cTSAppCommonImpl (FindProductDirectories, SetupUserDataDirectories),
// cTSCommonSystem (the product key readers, ReadAppDword), and each other. Every one returns in al (the rest of eax is
// left over); every caller tests al.
namespace product {

// 0xcb4690: the product's registry key ("Software\Electronic Arts\The Sims 2 Ultimate Collection 25\") and the
// directory of `executable`'s product. That directory is the running executable's (GetModuleFileNameA) three levels up,
// then '\' and the product's folder: `executable` from its sixth character, its last four ("EP9" from "Sims2EP9.exe"),
// or "Base" when that leaves nothing ("Sims2.exe"). Each output is written only when both its buffer and its length
// pointer are given: *length = strlen, then the text copied with strncpy_s(buffer, strlen + 1, text, strlen) (the
// length's incoming value, the buffer's size, is not read). `modulePath` gets the executable's full path. `directoryExists`
// (when given) is whether the directory exists (_access), 0 when no directory buffer was given. Always true.
bool __cdecl GetProductPaths(const char *executable, char *key, uint32_t *keyLength, char *directory, uint32_t *directoryLength,
                     char *modulePath, uint32_t *modulePathLength, uint32_t *directoryExists);

// 0xcb4aa0: the product executables, in release order ("Sims2EP1.exe" ... "Sims2EP9.exe", 16 names). *count = how many;
// *names = a malloc'ed array of max(count, minimum) calloc'ed 0x104-byte buffers (null for none), the first *count
// filled. `key` is not read. False (and *names as said) when the list is empty, which it never is.
bool __cdecl ListProductExecutables(const char *key, char ***names, int32_t *count, int32_t minimum);

// 0xcb4bb0: each product's installation directory (GetProductPaths of its executable): a malloc'ed array of *count
// malloc'ed 0x104-byte directories, null for a product whose directory doesn't exist. False (*directories null) when the
// list is empty. The caller frees both.
bool __cdecl InstalledProductDirectories(const char *key, char ***directories, int32_t *count);

// 0xcb4950: the newest product's executable name (the list's last non-empty entry, "Sims2EP9.exe") copied to `out`;
// "Sims2.exe" when the list is empty. True.
bool __cdecl NewestProductExecutable(char *out);

// 0xcb4cc0: whether a product newer than this one is installed. Each listed product is scored (its index; +1000 for a
// stuff pack "Sims2SP..", +2000 for a "Sims2SC.." name or an expansion after the third); the best (first of equals)
// is copied to `outExecutable` and its score to `outScore` (each when given). Newer: a score above 2016. With the fixed
// list the answer is always false ("Sims2EP9.exe", 2015).
bool __cdecl FindNewerProduct(char *outExecutable, int32_t *outScore);

} // namespace product
