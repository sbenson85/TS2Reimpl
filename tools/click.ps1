# Clicks or presses a key in the game copy's window. By default the input is posted to the window as
# messages: it works while the window is covered or unfocused and never touches the user's cursor
# or keyboard. -RealInput uses real input instead (foregrounds the game and moves the cursor).
#   click.ps1 -X 1004 -Y 660            left-click at client coordinates (full-resolution pixels;
#                                       screenshots are downscaled, so multiply their coordinates)
#   click.ps1 -Key 0x1B                 press a virtual-key code (0x1B = Escape, 0x0D = Enter)
#   click.ps1 -Text jh                  post one WM_CHAR per character (posted mode only): typed text for the
#                                       focused text field, without key-down / key-up translation
#   -Instance N                         the game in local/game-N (tools/instances.ps1)
param([int]$X = -1, [int]$Y = -1, [int]$Key = 0, [string]$Text = '', [switch]$RealInput, [int]$Instance = 0)
$ErrorActionPreference = 'Stop'
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class TS2Input {
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, UIntPtr e);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
}
'@ -ErrorAction SilentlyContinue
. "$PSScriptRoot\common.ps1"
$game = Get-InstanceProcess $Instance | Where-Object ProcessName -in 'Sims2EP9', 'ts2host' | Select-Object -First 1
if (-not $game) { throw 'Game copy is not running' }
$h = $game.MainWindowHandle
if (-not $RealInput) {
    if ($Key) {
        $scan = [TS2Input]::MapVirtualKey([uint32]$Key, 0)
        [TS2Input]::PostMessage($h, 0x100, [IntPtr]$Key, [IntPtr](1 -bor ($scan -shl 16))) | Out-Null           # WM_KEYDOWN
        Start-Sleep -Milliseconds 50
        [TS2Input]::PostMessage($h, 0x101, [IntPtr]$Key, [IntPtr](0xC0000001 -bor ($scan -shl 16))) | Out-Null  # WM_KEYUP
        "posted key 0x{0:X2}" -f $Key
    }
    if ($Text) {
        foreach ($c in $Text.ToCharArray()) {
            [TS2Input]::PostMessage($h, 0x102, [IntPtr][int]$c, [IntPtr]1) | Out-Null                             # WM_CHAR
            Start-Sleep -Milliseconds 50
        }
        "posted text '$Text'"
    }
    if ($X -ge 0) {
        $l = [IntPtr](($Y -shl 16) -bor ($X -band 0xffff))
        [TS2Input]::PostMessage($h, 0x200, [IntPtr]0, $l) | Out-Null   # WM_MOUSEMOVE
        Start-Sleep -Milliseconds 100
        [TS2Input]::PostMessage($h, 0x201, [IntPtr]1, $l) | Out-Null   # WM_LBUTTONDOWN
        Start-Sleep -Milliseconds 80
        [TS2Input]::PostMessage($h, 0x202, [IntPtr]0, $l) | Out-Null   # WM_LBUTTONUP
        "posted click at client ($X,$Y)"
    }
    return
}
[TS2Input]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 200
if ($Key) {
    [TS2Input]::keybd_event([byte]$Key, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 50
    [TS2Input]::keybd_event([byte]$Key, 0, 2, [UIntPtr]::Zero)
    "pressed key 0x{0:X2}" -f $Key
}
if ($X -ge 0) {
    $p = New-Object TS2Input+POINT; $p.X = $X; $p.Y = $Y
    [TS2Input]::ClientToScreen($h, [ref]$p) | Out-Null
    [TS2Input]::SetCursorPos($p.X, $p.Y) | Out-Null
    Start-Sleep -Milliseconds 100
    [TS2Input]::mouse_event(0x2, 0, 0, 0, [UIntPtr]::Zero)   # left down
    Start-Sleep -Milliseconds 60
    [TS2Input]::mouse_event(0x4, 0, 0, 0, [UIntPtr]::Zero)   # left up
    "clicked client ($X,$Y) = screen ($($p.X),$($p.Y))"
}
