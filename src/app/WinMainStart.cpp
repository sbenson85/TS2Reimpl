#include "app/WinMainStart.h"

namespace winmainstart {

namespace {

constexpr int kModuleTypeExe = 1;
constexpr unsigned kFastFailFatalAppExit = 7; // FAST_FAIL_FATAL_APP_EXIT
constexpr int kInitialiserFailed = 255;

enum NativeStartupState { kNotStarted = 0, kInitialising = 1, kInitialised = 2 };

} // namespace

int CommonMain(const ImageCrt &crt)
{
    if (!crt.initializeCrt(kModuleTypeExe)) {
        crt.fastFail(kFastFailFatalAppExit);
        __debugbreak(); // __scrt_fastfail does not return
    }

    // Set when the CRT's start-up had already run (a second entry into the module): the exits of a
    // managed module then leave the CRT's exit-time work to whoever ran it first.
    volatile bool alreadyInitialised = false;
    unsigned long exceptionCode = 0;
    __try {
        bool nested = crt.acquireStartupLock();
        int state = *crt.nativeStartupState;
        if (state == kInitialising) {
            crt.fastFail(kFastFailFatalAppExit);
            __debugbreak();
        }
        if (state == kNotStarted) {
            *crt.nativeStartupState = kInitialising;
            // ORIGINAL: a failing XI entry returns at once, with the start-up lock still held.
            if (crt.runCheckedInitialisers(crt.xiFirst, crt.xiLast) != 0) return kInitialiserFailed;
            crt.runInitialisers(crt.xcFirst, crt.xcLast);
            *crt.nativeStartupState = kInitialised;
        } else {
            alreadyInitialised = true;
        }
        crt.releaseStartupLock(nested);

        // Thread-local variables with dynamic initialisation: the compiler's callbacks, only when the
        // slot holds one and lives in a read-only section of the module (a writable slot is not trusted).
        TlsCallback *tlsInit = crt.dynamicTlsInit;
        if (*tlsInit != nullptr && crt.isNonwritableInImage(tlsInit)) (*tlsInit)(nullptr, DLL_THREAD_ATTACH, nullptr);
        TlsCallback *tlsDtor = crt.dynamicTlsDtor;
        if (*tlsDtor != nullptr && crt.isNonwritableInImage(tlsDtor)) crt.registerTlsDtor(*tlsDtor);

        uint16_t show = crt.showWindowMode();
        char *commandLine = crt.narrowCommandLine();
        int result = crt.winMain(crt.instance, nullptr, commandLine, show);

        if (!crt.isManagedApp()) {
            crt.exit(result); // runs the atexit list and ends the process
            __debugbreak();
        }
        if (!alreadyInitialised) crt.cexit();
        crt.uninitializeCrt(true, false);
        return result;
    } __except (exceptionCode = GetExceptionCode(), crt.exceptionFilter(exceptionCode, GetExceptionInformation())) {
        // The filter chose to handle it: the exception code becomes the exit code.
        if (!crt.isManagedApp()) {
            crt.exitNow(static_cast<int>(exceptionCode));
            __debugbreak();
        }
        if (!alreadyInitialised) crt.exitQuick();
        return static_cast<int>(exceptionCode);
    }
}

XcRunCounts RunInitialisersOwned(Initialiser *first, Initialiser *last, uintptr_t imageBase,
                                 const OwnedInitialiser *owned, size_t ownedCount)
{
    XcRunCounts counts;
    const size_t slots = static_cast<size_t>(last - first);
    for (size_t i = 0; i < ownedCount; ++i) {
        bool repeated = false;
        for (size_t j = 0; j < i; ++j) repeated |= owned[j].slot == owned[i].slot;
        if (owned[i].slot >= slots || repeated) ++counts.unused;
    }
    for (size_t slot = 0; slot < slots; ++slot) {
        Initialiser image = first[slot];
        if (image == nullptr) continue;
        const OwnedInitialiser *ours = nullptr;
        for (size_t i = 0; i < ownedCount && ours == nullptr; ++i) {
            if (owned[i].slot == slot) ours = &owned[i];
        }
        if (ours != nullptr && reinterpret_cast<uintptr_t>(image) == imageBase + ours->imageRva) {
            ours->run(ours->arg);
            ++counts.owned;
            continue;
        }
        if (ours != nullptr) ++counts.mismatched;
        image();
        ++counts.image;
    }
    return counts;
}

int RunImageStart(const ImageCrt &crt)
{
    crt.initSecurityCookie();
    return CommonMain(crt);
}

} // namespace winmainstart
