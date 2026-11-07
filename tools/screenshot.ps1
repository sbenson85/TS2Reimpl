# Captures the game window (our copy in local/game only) to a PNG, downscaled to -Width.
# Works while the window is covered, since it uses PrintWindow rather than screen capture.
param([string]$Out = "$PSScriptRoot\..\local\screenshots\$(Get-Date -Format 'yyyyMMdd-HHmmss').png", [int]$Width = 1280, [int]$Instance = 0) # -Instance N: local/game-N
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class TS2Win {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
}
'@ -ErrorAction SilentlyContinue
$game = Get-InstanceProcess $Instance | Where-Object ProcessName -in 'Sims2EP9', 'ts2host' | Select-Object -First 1
if (-not $game) { throw 'Game copy is not running' }
$r = New-Object TS2Win+RECT
[TS2Win]::GetClientRect($game.MainWindowHandle, [ref]$r) | Out-Null
$bmp = New-Object Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
$g = [Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc()
[TS2Win]::PrintWindow($game.MainWindowHandle, $dc, 3) | Out-Null   # PW_CLIENTONLY | PW_RENDERFULLCONTENT
$g.ReleaseHdc($dc)
$h = [int]($bmp.Height * $Width / $bmp.Width)
New-Item -ItemType Directory -Force (Split-Path $Out) | Out-Null
(New-Object Drawing.Bitmap $bmp, $Width, $h).Save($Out)
Write-Output "$Out (responding=$($game.Responding), $([int]($game.WS / 1MB)) MB)"
