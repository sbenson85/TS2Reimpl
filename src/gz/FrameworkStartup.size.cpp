#include "gz/FrameworkStartup.h"

#include "game/game.h"
#include "rz/RZFrameworkHelpers.h"
#include "game/vtable.h"
#include "gz/FrameworkShape.h"
#include "gz/GZCOM.h"
#include "gz/cIGZApp.h"
#include "rz/StringKeyParser.h"

#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

#pragma warning(disable : 4996) // strncpy, as the game calls it

namespace fw_startup {
namespace {

template <typename R, typename... Args> R CallSlot(void *obj, int slot, Args... args)
{
    return VirtualSlot<R(__thiscall *)(void *, Args...)>(obj, slot)(obj, args...);
}

// A cRZString local, built and torn down like the original's stack objects.
class Str {
public:
    Str() { S().Construct(); }
    ~Str() { S().Destruct(); }
    Str(const Str &) = delete;
    Str &operator=(const Str &) = delete;

    cRZString &S() { return *reinterpret_cast<cRZString *>(mRaw); }
    std::string &Text() { return S().mString; }
    cIGZString *Interface() { return S().AsInterface(); }
    void Lower() { reinterpret_cast<cRZStringHelpers *>(mRaw)->LowerCase(); } // 0x1ad167

private:
    alignas(cRZString) unsigned char mRaw[sizeof(cRZString)];
};

// 0x14205a: assign through (data, size), and not at all to itself.
void AssignString(std::string &dst, const std::string &src)
{
    if (&dst != &src) dst.assign(src.c_str(), src.size());
}

gz::cGZFramework *Framework()
{
    return static_cast<gz::cGZFramework *>(ts2_rva_10c9378);
}

cIGZApp *Application()
{
    return *GameAddress<cIGZApp *>(gz::cGZFramework::kGlobalApp);
}

int32_t &ExitCode()
{
    return *GameAddress<int32_t>(gz::cGZFramework::kGlobalExitCode);
}

// The directory separators and the drive colon (the string at 0xda26d8), and the extension dot (0xd9f880).
constexpr char kSeparators[] = "\\/:";
constexpr char kDot[] = ".";

// What the parser compares: the file's name without directory and extension, lowercased.
// `source` is copied into `out` first (0x14205a), then the part up to the last separator and from the
// last dot are cut, then it is lowercased. The cuts compare the position against the size as unsigned
// values, so "not found" (npos) skips them.
void ReduceToName(Str &out, const std::string &source)
{
    AssignString(out.Text(), source);
    size_t at = out.Text().find_last_of(kSeparators, std::string::npos);
    if (at < out.Text().size()) out.Text().erase(0, at + 1);
    at = out.Text().find_last_of(kDot, std::string::npos);
    if (at < out.Text().size()) out.Text().erase(at);
    out.Lower();
}

} // namespace

// ---- 0x1420af / 0x14155c ----

void CopyArguments(cRZStringVector &dst, const cRZStringVector &src)
{
    if (&dst == &src) return;
    uint32_t count = static_cast<uint32_t>(src.Count());
    uint32_t capacity = static_cast<uint32_t>(dst.mEnd - dst.mFirst);
    cRZString *newLast;
    if (count > capacity) {
        // 0x143a4d: clear and reserve geometrically (the growth is worked out before anything is freed).
        if (count > cRZStringVector::kMaxElements) std::_Xlength_error("vector too long");
        uint32_t newCapacity = dst.CalculateGrowth(count);
        dst.Tidy();
        cRZString *block = std::allocator<cRZString>().allocate(newCapacity);
        dst.mFirst = block;
        dst.mLast = block;
        dst.mEnd = block + newCapacity;
        newLast = cRZStringVector::UninitializedCopy(src.mFirst, src.mFirst + count, dst.mFirst, nullptr);
    } else {
        uint32_t size = static_cast<uint32_t>(dst.Count());
        if (count > size) {
            const cRZString *from = src.mFirst;
            for (cRZString *p = dst.mFirst; p != dst.mLast; ++p, ++from) AssignString(p->mString, from->mString);
            newLast = cRZStringVector::UninitializedCopy(src.mFirst + size, src.mFirst + count, dst.mLast, nullptr);
        } else {
            cRZStringVector::CopyForward(src.mFirst, src.mFirst + count, dst.mFirst);
            newLast = dst.mFirst + count;
            cRZStringVector::DestroyRange(newLast, dst.mLast, nullptr);
        }
    }
    dst.mLast = newLast;
}

// ---- 0x1420d2 ----

cRZCmdLine *__fastcall AssignCmdLine(cRZCmdLine *self, void *, const cRZCmdLine *other)
{
    self->mRefCount = other->mRefCount;
    CopyArguments(self->mArgs, other->mArgs);
    AssignString(self->mText.mString, other->mText.mString);
    AssignString(self->mEmpty.mString, other->mEmpty.mString);
    return self;
}

// ---- 0x1aeb57 ----

void __cdecl ParseCommandLine(cIGZCmdLine *cmd, cIGZString *text)
{
    int32_t argc = cmd->ArgC();
    bool hasArguments = argc > 0;
    Str path;
    Str program;
    if (argc > 0) program.Text().append(cmd->ArgV(0));

    char buffer[0x200];
    std::memset(buffer, 0, sizeof(buffer));
    const char *data;
    uint32_t length;
    if (text) {
        length = text->Strlen();
        data = text->Data();
    } else {
        GetModulePath(path.Interface());
        data = path.Text().c_str();
        length = static_cast<uint32_t>(path.Text().size());
    }
    // ORIGINAL-BUG: the path is copied into a 512-byte stack buffer without checking its length, so a
    // text of 512 or more characters overruns the stack (a module path is at most 260). A copy that
    // fills the buffer is also not terminated, which the strlen below then runs past.
    std::strncpy(buffer, data, length);

    if (argc > 0) {
        Str pathName;
        Str programName;
        if (path.Text().empty()) path.Text().assign(buffer, std::strlen(buffer));
        ReduceToName(pathName, path.Text());
        ReduceToName(programName, program.Text());
        if (pathName.Text() == programName.Text()) {
            cmd->EraseArgument(0);
            cmd->InsertArgument(*path.Interface(), 0);
        } else {
            hasArguments = false;
        }
    }
    if (!hasArguments) {
        path.Text().assign(buffer, std::strlen(buffer));
        if (!path.Text().empty()) cmd->InsertArgument(*path.Interface(), 0);
    }
}

// ---- 0x144514 ----

// On g_Framework as gz::cGZFramework (lane EX): its members called directly where the original calls them directly
// (SetCommandLine 0x143660, CommitCommandLine 0x1427a7, SetInstallFlag, the hook runners), its slots through the object's
// vtable (the live object's are the game's, which lead to the hooks' adapters). g_Framework is read again at every step.
bool __cdecl GZFrameWork_Startup(cRZCmdLine *commandLine, uint32_t installFlag)
{
    Framework()->mState = 0;
    Framework()->SetCommandLine(*commandLine);
    gz::cGZFramework::CommitCommandLine(reinterpret_cast<cIGZCmdLine *>(&Framework()->mCmdLine));
    Framework()->SetInstallFlag(installFlag);

    void *com = Framework()->GetCOMObject();
    if (CallSlot<uint8_t>(com, 9)) CallSlot<uint32_t>(com, 11, 1u); // RealInit, SetServiceRunning(true)
    void *service = nullptr;
    if (CallSlot<uint8_t>(com, 0, 0x287fb697u, &service)) {
        Framework()->AddSystemService(static_cast<cIGZSystemService *>(service));
        CallSlot<uint32_t>(service, 2); // Release
        service = nullptr;
    }

    if (!Application()->AddApplicationServicesHere()) {
        ExitCode() = -1;
        return false;
    }
    Application()->AddCOMDirectorsHere();
    Application()->AddDynamicLibrariesHere();
    Framework()->mState = 1;
    if (!Application()->PreFrameworkInit()) {
        CallSlot<uint8_t>(com, 10); // RealShutdown
        ExitCode() = -1;
        return false;
    }
    Framework()->RunPreFrameWorkInitHooks();
    Framework()->mState = 2;
    Framework()->mState = 3;
    Framework()->InitHighPriorityServices();
    Framework()->RunPreAppInitHooks();
    Framework()->mState = 4;
    if (!Framework()->InitAppBandServices()) {
        ExitCode() = -1;
        return false;
    }
    Framework()->ToggleTick(1);
    Framework()->mState = 5;
    Framework()->InitLowPriorityServices();
    Framework()->mState = 6;
    Application()->PostFrameworkInit();
    Framework()->RunPostAppInitHooks();
    return ExitCode() == 0;
}

} // namespace fw_startup
