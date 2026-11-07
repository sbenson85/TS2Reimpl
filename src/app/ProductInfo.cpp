// The product helpers 0xcb4690..0xcb4cc0 (lane MB). See ProductInfo.h.
#include "app/ProductInfo.h"

#include "game/import.h"

#define NOMINMAX
#include <windows.h>

#include <io.h>

#include <cstdlib>
#include <cstring>

namespace product {
namespace {

constexpr uint32_t kIatGetModuleFileNameA = 0xd9a16c;
constexpr uint32_t kModulePathSize = 0x200;    // the original's buffer for the executable's path
constexpr uint32_t kFolderSize = 0x1e;         // its buffer for the product's folder name (strcpy_s's size)
constexpr uint32_t kListBufferSize = 0x400;    // its copy of the product list
constexpr uint32_t kNameSize = 0x104;          // each listed name's buffer, and each directory's
constexpr uint32_t kKeyBufferSize = 0x104;     // the key buffers of 0xcb4950 / 0xcb4cc0
constexpr uint32_t kNewestKeyLength = 0x103;   // the length 0xcb4950 passes in (not read)
constexpr int32_t kNewerThreshold = 0x7e0;     // a best score above this is a newer product
constexpr int32_t kStuffPackScore = 1000;
constexpr int32_t kNewerScore = 2000;
constexpr int32_t kLastOlderExpansion = 2;     // the first three expansions are scored by index alone
constexpr size_t kFolderOffset = 5;            // "Sims2" before the product's folder name
constexpr size_t kExtensionLength = 4;         // ".exe"

constexpr char kProductKey[] = "Software\\Electronic Arts\\The Sims 2 Ultimate Collection 25\\"; // 0xeb72e0
constexpr char kProductList[] = "Sims2EP1.exe,Sims2EP2.exe,Sims2EP3.exe,Sims2SP1.exe,Sims2SP2.exe,Sims2EP4.exe,"
                                "Sims2EP5.exe,Sims2SP4.exe,Sims2SP5.exe,Sims2EP6.exe,Sims2SP6.exe,Sims2EP7.exe,"
                                "Sims2SP7.exe,Sims2SP8.exe,Sims2EP8.exe,Sims2EP9.exe"; // 0xeb7328
constexpr char kBaseExecutable[] = "Sims2.exe"; // 0xe9856c
constexpr char kBaseFolder[] = "Base";          // 0xeb731c

// *length = strlen(text), then the text into the buffer (strncpy_s with that length + 1 as the size): only when both
// the buffer and the length pointer are given.
void CopyOut(char *buffer, uint32_t *length, const char *text)
{
    if (!buffer || !length) return;
    const size_t n = std::strlen(text);
    *length = static_cast<uint32_t>(n);
    strncpy_s(buffer, n + 1, text, n);
}

// Cuts the path at its last '\' or '/' unless that is the first character (or there is none).
void StripLastComponent(char *path)
{
    if (!path[0]) return;
    size_t last = 0;
    for (size_t i = 0; path[i]; ++i) {
        if (path[i] == '\\' || path[i] == '/') last = i;
    }
    if (last != 0) path[last] = 0;
}

} // namespace

bool __cdecl GetProductPaths(const char *executable, char *key, uint32_t *keyLength, char *directory, uint32_t *directoryLength,
                     char *modulePath, uint32_t *modulePathLength, uint32_t *directoryExists)
{
    CopyOut(key, keyLength, kProductKey);

    char path[kModulePathSize];
    ImportFunction<decltype(&GetModuleFileNameA), kIatGetModuleFileNameA>()(nullptr, path, kModulePathSize);
    CopyOut(modulePath, modulePathLength, path);
    for (int i = 0; i < 3; ++i) StripLastComponent(path);
    std::strcat(path, "\\");

    char folder[kFolderSize];
    strcpy_s(folder, kFolderSize, executable + kFolderOffset);
    const size_t length = std::strlen(folder);
    // ORIGINAL-BUG: the original clears folder[length - 4] without checking the length: a name with fewer than four
    // characters after "Sims2" clears a byte below its buffer (a saved register). No caller passes one; ours leaves it.
    if (length >= kExtensionLength) folder[length - kExtensionLength] = 0;
    std::strcat(path, folder[0] ? folder : kBaseFolder);

    CopyOut(directory, directoryLength, path);
    if (directoryExists) *directoryExists = directory ? (_access(path, 0) == 0) : 0;
    return true;
}

bool __cdecl ListProductExecutables(const char *, char ***names, int32_t *count, int32_t minimum)
{
    char list[kListBufferSize];
    std::strcpy(list, kProductList);
    *count = 0;
    if (list[0]) {
        for (const char *p = list; p; ++*count) {
            p = std::strchr(p, ',');
            if (p) ++p;
        }
    }
    const int32_t slots = minimum > *count ? minimum : *count;
    *names = slots ? static_cast<char **>(std::malloc(static_cast<size_t>(slots) * sizeof(char *))) : nullptr;
    if (*count == 0) return false;
    for (int32_t i = 0; i < slots; ++i) (*names)[i] = static_cast<char *>(std::calloc(kNameSize, 1));
    char *name = list;
    for (int32_t i = 0; name; ++i) {
        char *next = std::strchr(name, ',');
        if (next) *next++ = 0;
        std::strcpy((*names)[i], name);
        name = next;
    }
    return true;
}

bool __cdecl InstalledProductDirectories(const char *key, char ***directories, int32_t *count)
{
    *count = 0;
    char **names;
    if (!ListProductExecutables(key, &names, count, 0)) {
        *directories = nullptr;
        return false;
    }
    *directories = static_cast<char **>(std::malloc(static_cast<size_t>(*count) * sizeof(char *)));
    for (int32_t i = 0; i < *count; ++i) {
        uint32_t exists = 0;
        char *directory = static_cast<char *>(std::malloc(kNameSize));
        (*directories)[i] = directory;
        bool keep = false;
        if (names[i][0]) {
            uint32_t length = kNameSize;
            GetProductPaths(names[i], nullptr, nullptr, directory, &length, nullptr, nullptr, &exists);
            keep = exists != 0;
        }
        if (!keep) {
            std::free(directory);
            (*directories)[i] = nullptr;
        }
        std::free(names[i]);
    }
    std::free(names);
    return true;
}

bool __cdecl NewestProductExecutable(char *out)
{
    char key[kKeyBufferSize] = {};
    uint32_t keyLength = kNewestKeyLength;
    // (The original tries a second executable name when this fails; it never fails.)
    GetProductPaths(kBaseExecutable, key, &keyLength, nullptr, nullptr, nullptr, nullptr, nullptr);
    char **names;
    int32_t count = 0;
    if (!ListProductExecutables(key, &names, &count, 0)) {
        std::strcpy(out, kBaseExecutable);
        return true;
    }
    for (int32_t i = count - 1; i >= 0; --i) {
        if (names[i] && names[i][0]) {
            std::strcpy(out, names[i]);
            break;
        }
    }
    if (names) {
        for (int32_t i = 0; i < count; ++i) std::free(names[i]);
        std::free(names);
    }
    return true;
}

bool __cdecl FindNewerProduct(char *outExecutable, int32_t *outScore)
{
    char key[kKeyBufferSize] = {};
    uint32_t keyLength = kKeyBufferSize;
    GetProductPaths(kBaseExecutable, key, &keyLength, nullptr, nullptr, nullptr, nullptr, nullptr);
    char **names = nullptr;
    int32_t count = 0;
    if (!ListProductExecutables(key, &names, &count, 1)) return false;

    int32_t best = 0, bestScore = 0;
    for (int32_t i = 0; i < count; ++i) {
        const char *name = names[i];
        int32_t score = 0;
        if (name && name[0]) {
            if (name[kFolderOffset] == 'S') score = i + (name[kFolderOffset + 1] == 'C' ? kNewerScore : kStuffPackScore);
            else score = i > kLastOlderExpansion ? i + kNewerScore : i;
        }
        if (score > bestScore) {
            bestScore = score;
            best = i;
        }
    }
    if (outScore) *outScore = bestScore;
    if (outExecutable) std::strcpy(outExecutable, names[best]);
    for (int32_t i = 0; i < count; ++i) {
        if (names[i]) std::free(names[i]);
    }
    std::free(names);
    return bestScore > kNewerThreshold;
}

} // namespace product
