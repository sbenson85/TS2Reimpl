// The device call log (TS2HOST_DEVLOG; docs/ui-testing.md): test control like host/control.cpp, a diagnostic that
// records what the game asks of Direct3D and changes none of it. The image's Direct3DCreate9 import resolves to
// LoggedDirect3DCreate9, which wraps the real IDirect3D9 (host/shared/d3dlog.h); the device's calls are grouped per
// framework tick (ControlFrame) by the recorder (host/shared/devlog.h) and written to ts2devlog.txt next to the host
// log. tools/devlog.py summarises and compares these files.
//
// TS2HOST_DEVLOG=1 (or "on"): a digest line per frame. TS2HOST_DEVLOG=<frames> ("900,4000-4002"): the digests, plus the
// full call lists of those frames. The control command "devlog <n> [label]" adds full lists from the current frame on
// (for frames anchored by aftersimframes); "devlog mark <label>" notes the frame.
#include "control.h"

#include "shared/d3dlog.h"
#include "shared/devlog.h"

#include "hook/log.h"

#include <share.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

bool gEnabled = false;
std::wstring gPath;
devlog::FrameSet gFullFrames;
std::string gHeader;
FILE *gFile = nullptr;
devlog::Recorder *gRecorder = nullptr;

using CreateFn = IDirect3D9 *(WINAPI *)(UINT);
CreateFn gRealCreate = nullptr;

void Write(std::string_view text)
{
    if (!gFile) return;
    std::fwrite(text.data(), 1, text.size(), gFile);
    std::fflush(gFile);
}

devlog::Recorder &TheRecorder()
{
    if (!gRecorder) {
        gFile = _wfsopen(gPath.c_str(), L"wb", _SH_DENYNO); // readable while the game runs
        Write(gHeader);
        gRecorder = new devlog::Recorder([] { return ControlFrame(); }, &Write); // lives for the process
        gRecorder->SetFullFrames(gFullFrames);
    }
    return *gRecorder;
}

IDirect3D9 *WINAPI LoggedDirect3DCreate9(UINT sdkVersion)
{
    IDirect3D9 *real = gRealCreate(sdkVersion);
    devlog::Recorder &recorder = TheRecorder();
    char text[48];
    std::snprintf(text, sizeof(text), "Direct3DCreate9 %u%s", sdkVersion, real ? "" : " =null");
    recorder.Record(devlog::Kind::Stream, text);
    if (!real) return nullptr;
    Log("devlog: Direct3DCreate9 wrapped; logging to ts2devlog.txt");
    return d3dlog::Wrap(real, recorder);
}

} // namespace

void ConfigureDeviceLog(const std::wstring &directory)
{
    char value[256];
    const DWORD n = GetEnvironmentVariableA("TS2HOST_DEVLOG", value, sizeof(value));
    if (!n || n >= sizeof(value)) return;
    gEnabled = true;
    gPath = directory + L"ts2devlog.txt";
    std::string spec = value;
    if (spec != "1" && spec != "on" && !gFullFrames.Parse(spec)) {
        Log("devlog: TS2HOST_DEVLOG=\"%s\" is not a frame list (e.g. 900,4000-4002); digests only", value);
        spec = "1";
    }
    char clock[32] = "off";
    GetEnvironmentVariableA("TS2HOST_FRAMECLOCK", clock, sizeof(clock));
    gHeader = "devlog 2 full=" + (spec == "1" || spec == "on" ? std::string("none") : spec) + " frameclock=" + clock + "\n";
}

void *DeviceLogImport(const char *dll, const char *function)
{
    if (!gEnabled || _stricmp(dll, "d3d9.dll") != 0 || reinterpret_cast<uintptr_t>(function) < 0x10000 ||
        std::strcmp(function, "Direct3DCreate9") != 0)
        return nullptr;
    HMODULE d3d9 = LoadLibraryExW(L"d3d9.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    gRealCreate = d3d9 ? reinterpret_cast<CreateFn>(GetProcAddress(d3d9, "Direct3DCreate9")) : nullptr;
    if (!gRealCreate) {
        Log("devlog: no Direct3DCreate9 in d3d9.dll; not logging");
        return nullptr;
    }
    Log("devlog: on (%s)", gHeader.substr(0, gHeader.size() - 1).c_str());
    TheRecorder(); // the file and its header exist from here, device or not
    return reinterpret_cast<void *>(&LoggedDirect3DCreate9);
}

void DeviceLogNote(const char *line)
{
    if (gEnabled && gRecorder) gRecorder->Note(line);
}

void DeviceLogCommand(const char *arguments)
{
    if (!gEnabled) {
        Log("control: devlog is off (TS2HOST_DEVLOG not set)");
        return;
    }
    devlog::Recorder &recorder = TheRecorder();
    if (std::strncmp(arguments, "mark", 4) == 0 && (arguments[4] == ' ' || arguments[4] == 0)) {
        const char *label = arguments[4] ? arguments + 5 : "";
        recorder.Mark(label);
        Log("control: devlog mark \"%s\" at frame %u", label, ControlFrame());
        return;
    }
    char *end = nullptr;
    const unsigned long count = std::strtoul(arguments, &end, 10);
    if (end == arguments || count == 0) {
        Log("control: devlog: expected \"devlog <frames> [label]\" or \"devlog mark <label>\", got \"%s\"", arguments);
        return;
    }
    while (*end == ' ') ++end;
    recorder.RequestFull(static_cast<uint32_t>(count), end);
    Log("control: devlog: %lu frames in full from frame %u (\"%s\")", count, ControlFrame(), end);
}
