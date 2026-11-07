// Quiet mode for automated tests while someone is using the computer: when the environment
// variable TS2REIMPL_QUIET is set (tools/game.ps1 -Quiet), the game window is kept off-screen and at
// the bottom of the z-order, focus goes back to whatever window had it, and the game's audio
// session is muted. Screenshots (PrintWindow) and posted clicks don't need the window on screen.
#include "hook/log.h"
#include "hook/quiet.h"

#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include <cstdlib>

namespace {

HWND gUserWindow; // the foreground window when the game started

struct FindState {
    DWORD pid;
    HWND found;
};

BOOL CALLBACK FindGameWindow(HWND hwnd, LPARAM param)
{
    auto *state = reinterpret_cast<FindState *>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != state->pid || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
    state->found = hwnd;
    return FALSE;
}

// Moves the window beyond the right edge of the virtual screen, keeping its size.
void MoveOffScreen(HWND hwnd)
{
    RECT r;
    if (!GetWindowRect(hwnd, &r)) return;
    int right = GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    if (r.left >= right) return;
    SetWindowPos(hwnd, HWND_BOTTOM, right + 64, GetSystemMetrics(SM_YVIRTUALSCREEN), 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    Log("quiet: moved the game window off-screen");
}

// Mutes this process's default audio session on the default output device.
bool MuteSession()
{
    IMMDeviceEnumerator *enumerator = nullptr;
    IMMDevice *device = nullptr;
    IAudioSessionManager *manager = nullptr;
    ISimpleAudioVolume *volume = nullptr;
    bool ok = SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                         reinterpret_cast<void **>(&enumerator))) &&
              SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) &&
              SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(&manager))) &&
              SUCCEEDED(manager->GetSimpleAudioVolume(nullptr, FALSE, &volume)) && SUCCEEDED(volume->SetMute(TRUE, nullptr));
    if (volume) volume->Release();
    if (manager) manager->Release();
    if (device) device->Release();
    if (enumerator) enumerator->Release();
    return ok;
}

DWORD WINAPI QuietWorker(LPVOID)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool muted = false;
    for (int tick = 0;; ++tick) {
        FindState state{GetCurrentProcessId(), nullptr};
        EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&state));
        if (state.found) {
            MoveOffScreen(state.found);
            // Give focus back if the game took it (allowed: we're the foreground process then).
            if (GetForegroundWindow() == state.found && gUserWindow && IsWindow(gUserWindow)) {
                SetForegroundWindow(gUserWindow);
                Log("quiet: gave focus back");
            }
        }
        // Re-mute now and then: the session may be created or recreated after startup.
        if (tick % 20 == 0) {
            bool now = MuteSession();
            if (now != muted) Log("quiet: audio session %s", now ? "muted" : "could not be muted");
            muted = now;
        }
        Sleep(tick < 600 ? 50 : 500);
    }
}

} // namespace

void StartQuietModeIfRequested()
{
    char value[8];
    size_t len = 0;
    if (getenv_s(&len, value, "TS2REIMPL_QUIET") != 0 || len == 0) return;
    gUserWindow = GetForegroundWindow();
    Log("quiet mode on");
    if (HANDLE t = CreateThread(nullptr, 0, QuietWorker, nullptr, 0, nullptr)) CloseHandle(t);
}
