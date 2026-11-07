#pragma once
// Maps the (unpacked) original executable into the test process so its functions can be called
// directly. Sections are copied and relocated; every import is replaced by a trap that raises
// kImportTrapCode naming the import, so a test fails cleanly if the original code reaches outside
// itself. No CRT or static initialisation of the original image is run.
#include <cstdint>
#include <string>
#include <vector>

constexpr unsigned long kImportTrapCode = 0xE0545332; // 'TS2' user exception

class OriginalImage {
public:
    // Resolves one import to a real function (the host exe, host/); `function` is a name, or an ordinal as a pointer below 0x10000. Returns null to fall back
    // to the default: passthrough, sandbox, or a trap.
    using ImportResolver = void *(*)(const char *dll, const char *function);

    // Always maps at kGameBase (src/game/game.h): our code is linked against that address, and capture/replay
    // need the same base in both processes. Fails if the range is taken.
    bool Load(const std::wstring &path, std::string &error, ImportResolver resolver = nullptr);

    // Pointer to an RVA in the mapped image.
    template <typename T = void> T *At(uint32_t rva) const { return reinterpret_cast<T *>(mBase + rva); }

    uintptr_t Base() const { return reinterpret_cast<uintptr_t>(mBase); }
    uint32_t Size() const { return mSize; }

    // Sets up the image's implicit TLS (its __declspec(thread) data) for the calling thread. Done for
    // the main thread at startup; a test that runs original code on another thread calls it there.
    bool AttachTlsToThisThread(std::string &error) const;
    // Re-adds the image's TLS slot if the loader has reallocated the thread's TLS array since (it
    // does when a DLL with TLS loads). Called before each test.
    bool EnsureTls(std::string &error) const;
    // Restores the thread's own TLS array (before the thread or process exits).
    void DetachTlsFromThisThread() const;

    // Name ("DLL!function") of the import whose trap fired most recently.
    static const std::string &LastTrappedImport();

private:
    uint8_t *mBase = nullptr;
    uint32_t mSize = 0;
    uint8_t *mThunks = nullptr;
};
