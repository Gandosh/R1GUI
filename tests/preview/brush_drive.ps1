# Real-desktop drive of the brush library of the preview's Editor screen (slices 5.20 to 5.23).
# Launches r1gui-preview.exe on the RTX 4080 with a throwaway data folder and drives it with real OS input
# (SetCursorPos, mouse_event, keybd_event), reading the library's state and coordinates from the status file
# the app writes when R1GUI_PREVIEW_STATUS is set (DriveStatus.cpp) and verifying with PrintWindow captures:
#   first start (Standard is active), B opens the library over the viewport, S narrows to the ten brushes
#   starting with S, N (the second letter) picks Snake Hook and the status line follows, arrows and Enter,
#   a click on a tile, the star, Backspace widens, Escape picks nothing, B again closes, Tab switches to the
#   search mode, B inside a number field does not open it, a letter assigned from the tile menu picks its
#   brush at once, B in a floating native window (the library opens there and goes with the window), the time
#   from the B key going down to the first changed pixel, idle CPU open and closed, the GPU (nothing on the
#   RTX 3090), and a restart over the same data (the last brush, the favourite and the letter are back).
# Every mouse press and key is preceded by a check that the point or the foreground window belongs to the
# preview process (nothing is ever sent to another application). The script kills the preview in a finally
# block: nothing is left running.
# Usage: powershell -File brush_drive.ps1 -Exe <path to r1gui-preview.exe> -OutDir <dir>
param([string]$Exe, [string]$OutDir, [int]$Seconds = 600)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Collections.Generic;
public struct RECT { public int Left, Top, Right, Bottom; }
public struct POINT { public int X, Y; }
public static class W {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int hh, uint flags);
  [DllImport("user32.dll")] public static extern uint MapVirtualKeyW(uint code, uint mapType);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
  [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT p);
  [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint flags);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);
  [DllImport("user32.dll", EntryPoint="GetWindowLongPtrW")] public static extern IntPtr GetWindowLongPtr(IntPtr h, int idx);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int attr, out int value, int size);
  public static List<IntPtr> Windows(uint pid) {
    var list = new List<IntPtr>();
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid && IsWindowVisible(h) && !IsIconic(h)) { var sb = new StringBuilder(64); GetClassNameW(h, sb, 64); if (sb.ToString() == "R1GUI.Window") list.Add(h); } return true; }, IntPtr.Zero);
    return list;
  }
}
"@
[void][W]::SetProcessDPIAware()
New-Item -ItemType Directory -Force $OutDir | Out-Null
$log = Join-Path $OutDir 'drive.log'
Set-Content $log "Editor screen drive, $(Get-Date -Format s)"
$script:fail = 0
function Say($m) { Add-Content $log $m; Write-Host $m }
function Check($ok, $what) { if ($ok) { Say "  PASS  $what" } else { Say "  FAIL  $what"; $script:fail++ } }

$data = Join-Path $OutDir 'data'
Remove-Item $data -Recurse -Force -ErrorAction SilentlyContinue
$status = Join-Path $OutDir 'status.txt'
$env:R1UI_GPU = 'RTX 4080'
$env:R1GUI_PREVIEW_DATA = $data
$env:R1GUI_PREVIEW_STATUS = $status
$env:PATH = 'C:\VulkanSDK\1.4.363.0\Bin;' + $env:PATH
$script:proc = $null
$script:procId = [uint32]0
$script:mainHwnd = [IntPtr]::Zero

function Launch($tag) {
  Remove-Item $status -ErrorAction SilentlyContinue
  $script:proc = Start-Process -FilePath $Exe -PassThru -RedirectStandardError (Join-Path $OutDir "preview-$tag.err")
  $null = $script:proc.Handle
  $script:procId = [uint32]$script:proc.Id
  $deadline = (Get-Date).AddSeconds(30)
  while (-not (Test-Path $status) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 100 }
  if (-not (Test-Path $status)) { throw 'the preview wrote no status file' }
  Start-Sleep -Milliseconds 800
  $script:mainHwnd = [IntPtr]([Convert]::ToInt64(([regex]::Match(@(Lines 'main')[0], 'hwnd=(\S+)').Groups[1].Value), 16))
  # The preview's own window stays above other applications for the duration of the drive (its owned floating
  # windows follow it), so another application that takes the foreground cannot cover the point being pressed.
  [void][W]::SetWindowPos($script:mainHwnd, [IntPtr](-1), 0, 0, 0, 0, 0x13)
  [void][W]::SetForegroundWindow($script:mainHwnd)
}
function ReadStatus() {
  for ($i = 0; $i -lt 60; $i++) {
    try { $t = [System.IO.File]::ReadAllText($status); if ($t) { return $t } } catch {}
    Start-Sleep -Milliseconds 50
  }
  throw 'no status'
}
function Lines($prefix) { @((ReadStatus) -split "`r?`n" | Where-Object { $_ -like "$prefix*" }) }
function Rect4($m) { @([int]$m.Groups[1].Value, [int]$m.Groups[2].Value, [int]$m.Groups[3].Value, [int]$m.Groups[4].Value) }
function MainRect() { Rect4 ([regex]::Match(@(Lines 'main')[0], ' rect=(-?\d+),(-?\d+),(\d+),(\d+)')) }
function Floats() { @(Lines 'float' | ForEach-Object { $m = [regex]::Match($_, 'hwnd=(\S+) rect=(-?\d+),(-?\d+),(\d+),(\d+) visible=(\d) monitor=(\S*)'); [pscustomobject]@{ hwnd = $m.Groups[1].Value; x = [int]$m.Groups[2].Value; y = [int]$m.Groups[3].Value; w = [int]$m.Groups[4].Value; h = [int]$m.Groups[5].Value; monitor = $m.Groups[7].Value } }) }
function TabPos($n) { $m = [regex]::Match(@(Lines 'tabs')[0], " $n=(-?\d+),(-?\d+)"); if (-not $m.Success) { return $null }; @([int]$m.Groups[1].Value, [int]$m.Groups[2].Value) }
function WidgetRect($kind, $name) { $m = [regex]::Match(((Lines $kind) -join "`n"), "(?m)^$kind $([regex]::Escape($name))=(-?\d+),(-?\d+),(\d+),(\d+)"); if (-not $m.Success) { return $null }; Rect4 $m }
function WidgetCenter($kind, $name) { $r = WidgetRect $kind $name; if ($null -eq $r) { return $null }; @(($r[0] + [int]($r[2] / 2)), ($r[1] + [int]($r[3] / 2))) }
function Text1() { $m = [regex]::Match(@(Lines 'text tool')[0], 'tool=(\S+) cube=(\S+),(\S+) undo=(.*) overlays=(\d+)$'); [pscustomobject]@{ tool = $m.Groups[1].Value; cx = [double]$m.Groups[2].Value; cy = [double]$m.Groups[3].Value; undo = $m.Groups[4].Value; overlays = [int]$m.Groups[5].Value } }
function Text2() { $m = [regex]::Match(@(Lines 'text layout')[0], 'layout=(.*?) status=(.*) edit=(\d) floating=(\d+)$'); [pscustomobject]@{ layout = $m.Groups[1].Value; status = $m.Groups[2].Value; edit = [int]$m.Groups[3].Value; floating = [int]$m.Groups[4].Value } }
function HasMenu($title) { $null -ne (WidgetRect 'menu' $title) }
function AreaPanels($main) { ((Lines 'area') | Where-Object { $_ -match "main=$main " } | ForEach-Object { [regex]::Match($_, 'panels=(\S*)').Groups[1].Value }) -join ';' }
function WaitFor([scriptblock]$cond, [int]$ms = 6000) {
  $deadline = (Get-Date).AddMilliseconds($ms)
  while ((Get-Date) -lt $deadline) { try { if (& $cond) { return $true } } catch {}; Start-Sleep -Milliseconds 100 }
  return $false
}
function Settle($ms = 400) { Start-Sleep -Milliseconds $ms }

function Ours($x, $y) {
  $p = New-Object POINT; $p.X = $x; $p.Y = $y
  $h = [W]::WindowFromPoint($p); if ($h -eq [IntPtr]::Zero) { return $false }
  $root = [W]::GetAncestor($h, 2)
  $pid2 = 0; [void][W]::GetWindowThreadProcessId($root, [ref]$pid2)
  return ($pid2 -eq $script:procId)
}
function ForegroundIsOurs() { $f = [W]::GetForegroundWindow(); $pid2 = 0; [void][W]::GetWindowThreadProcessId($f, [ref]$pid2); return ($pid2 -eq $script:procId) }
function BringForward() {
  # A tap of Alt lets the foreground change; the main window is raised last so the floating windows (owned by
  # it) stay above it. Retried: another application may take the foreground again meanwhile.
  for ($i = 0; $i -lt 4; $i++) {
    [W]::keybd_event(0xA4, 0, 0, [UIntPtr]::Zero); [W]::keybd_event(0xA4, 0, 2, [UIntPtr]::Zero)
    [void][W]::SetForegroundWindow($script:mainHwnd)
    Start-Sleep -Milliseconds 250
    if (ForegroundIsOurs) { break }
  }
}
function EnsureForeground() {
  if (-not (ForegroundIsOurs)) { BringForward }
  if (-not (ForegroundIsOurs)) { throw 'refusing to send keys: the foreground window is not the preview' }
}
function MoveTo($x, $y) { [void][W]::SetCursorPos([int]$x, [int]$y); Start-Sleep -Milliseconds 15 }
function PressButton($x, $y) {
  MoveTo $x $y; Start-Sleep -Milliseconds 60
  if (-not (Ours $x $y)) { BringForward; MoveTo $x $y; Start-Sleep -Milliseconds 60 }
  if (-not (Ours $x $y)) {
    $pt = New-Object POINT; $pt.X = [int]$x; $pt.Y = [int]$y
    $who = [W]::GetAncestor([W]::WindowFromPoint($pt), 2); $wpid = 0; [void][W]::GetWindowThreadProcessId($who, [ref]$wpid)
    $name = (Get-Process -Id $wpid -ErrorAction SilentlyContinue).ProcessName
    $cls = New-Object System.Text.StringBuilder 64; [void][W]::GetClassNameW($who, $cls, 64)
    $rr = New-Object RECT; [void][W]::GetWindowRect($who, [ref]$rr)
    $topmost = (([int64][W]::GetWindowLongPtr($who, -20)) -band 8) -ne 0
    $cloak = 0; [void][W]::DwmGetWindowAttribute($script:mainHwnd, 14, [ref]$cloak, 4)
    $fg = [W]::GetForegroundWindow(); $fgp = 0; [void][W]::GetWindowThreadProcessId($fg, [ref]$fgp)
    $mrect = New-Object RECT; [void][W]::GetWindowRect($script:mainHwnd, [ref]$mrect)
    Say "  diagnostics: main iconic=$([W]::IsIconic($script:mainHwnd)) visible=$([W]::IsWindowVisible($script:mainHwnd)) cloaked=$cloak rect=$($mrect.Left),$($mrect.Top) foreground pid=$fgp (preview pid $($script:procId)) topmost=$((([int64][W]::GetWindowLongPtr($script:mainHwnd, -20)) -band 8) -ne 0)"
    throw "refusing to press at ${x},${y} (that point belongs to process '$name', window class '$cls' at $($rr.Left),$($rr.Top) $($rr.Right - $rr.Left)x$($rr.Bottom - $rr.Top) topmost=$topmost, not to the preview)"
  }
  [W]::mouse_event(0x2, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 40
}
function ReleaseButton() { [W]::mouse_event(0x4, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 80 }
function Click($x, $y) { PressButton $x $y; ReleaseButton; Settle 250 }
function DragTo($x0, $y0, $x1, $y1, [int]$steps = 16) {
  PressButton $x0 $y0
  for ($i = 1; $i -le $steps; $i++) { MoveTo ($x0 + ($x1 - $x0) * $i / $steps) ($y0 + ($y1 - $y0) * $i / $steps); Start-Sleep -Milliseconds 18 }
  Start-Sleep -Milliseconds 120
  ReleaseButton
  Settle 500
}
$KeyCodes = @{ Ctrl = 0xA2; Shift = 0xA0; Alt = 0xA4; Enter = 0x0D; Escape = 0x1B; Down = 0x28; Up = 0x26; Tab = 0x09; Space = 0x20 }
function KeyEvent([int]$vk, [bool]$up) {
  $scan = [byte][W]::MapVirtualKeyW([uint32]$vk, 0)
  $flags = 0; if ($up) { $flags = 2 }
  [W]::keybd_event([byte]$vk, $scan, $flags, [UIntPtr]::Zero)
}
function KeyTap($key, [string[]]$mods = @()) {
  EnsureForeground
  $vk = if ($key -is [string] -and $key.Length -eq 1) { [int][char]$key.ToUpper() } elseif ($KeyCodes.ContainsKey([string]$key)) { [int]$KeyCodes[[string]$key] } else { [int]$key }
  foreach ($m in $mods) { KeyEvent ([int]$KeyCodes[$m]) $false; Start-Sleep -Milliseconds 25 }
  KeyEvent $vk $false; Start-Sleep -Milliseconds 45
  KeyEvent $vk $true; Start-Sleep -Milliseconds 25
  foreach ($m in $mods) { KeyEvent ([int]$KeyCodes[$m]) $true }
  Start-Sleep -Milliseconds 110
}
function TypeText([string]$text) {
  foreach ($c in $text.ToCharArray()) {
    if ($c -eq ' ') { KeyTap 'Space' } elseif ([char]::IsUpper($c)) { KeyTap ([string]$c) @('Shift') } else { KeyTap ([string]$c) }
  }
}
function MenuItem($title, [int]$downs) {
  $c = WidgetCenter 'menu' $title
  if ($null -eq $c) { throw "no menu $title" }
  Click $c[0] $c[1]
  Settle 250
  for ($i = 0; $i -lt $downs; $i++) { KeyTap 'Down' }
  KeyTap 'Enter'
  Settle 400
}
function CaptureWindow($hwnd, $name) {
  $r = New-Object RECT; [void][W]::GetWindowRect($hwnd, [ref]$r); $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
  $bmp = New-Object System.Drawing.Bitmap $w, $h; $g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc()
  $ok = [W]::PrintWindow($hwnd, $dc, 2); $g.ReleaseHdc($dc)
  $bmp.Save((Join-Path $OutDir $name), [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose()
  Say "  saved $name ($w x $h) printwindow=$ok"
}
function CaptureMain($name) { CaptureWindow $script:mainHwnd $name }
function Inspect($h) {
  $hh = [IntPtr]([Convert]::ToInt64($h, 16)); $r = New-Object RECT; [void][W]::GetWindowRect($hh, [ref]$r)
  $ex = [int64][W]::GetWindowLongPtr($hh, -20)
  $owner = [W]::GetWindow($hh, 4); $corner = 0; [void][W]::DwmGetWindowAttribute($hh, 33, [ref]$corner, 4)
  [pscustomobject]@{ hwnd = $hh; toolWindow = (($ex -band 0x80) -ne 0); appWindow = (($ex -band 0x40000) -ne 0); owner = $owner; corner = $corner }
}
function CpuPercent([int]$seconds) {
  $p = Get-Process -Id $script:procId
  $c0 = $p.TotalProcessorTime.TotalSeconds; $t0 = Get-Date
  Start-Sleep -Seconds $seconds
  $p.Refresh(); $c1 = $p.TotalProcessorTime.TotalSeconds
  return 100.0 * ($c1 - $c0) / ((Get-Date) - $t0).TotalSeconds
}
function CloseApp([string]$tag) {
  $all = @([W]::Windows($script:procId))
  [void][W]::PostMessage($script:mainHwnd, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
  $exited = $script:proc.WaitForExit(15000)
  Check $exited "the preview exited after the main window was closed ($tag)"
  if ($exited) { Check ($script:proc.ExitCode -eq 0) "exit code 0 ($tag)" }
}



# ---- helpers of this drive ----------------------------------------------------------------------------
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class G {
  [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr h);
  [DllImport("user32.dll")] static extern int ReleaseDC(IntPtr h, IntPtr dc);
  [DllImport("gdi32.dll")] static extern uint GetPixel(IntPtr dc, int x, int y);
  public static uint Pixel(int x, int y) { IntPtr dc = GetDC(IntPtr.Zero); uint c = GetPixel(dc, x, y); ReleaseDC(IntPtr.Zero, dc); return c; }
}
"@
$KeyCodes['End'] = 0x23; $KeyCodes['Home'] = 0x24; $KeyCodes['Right'] = 0x27; $KeyCodes['Left'] = 0x25; $KeyCodes['Backspace'] = 0x08; $KeyCodes['Delete'] = 0x2E; $KeyCodes['F2'] = 0x71
function Brush() {
  $l = @(Lines 'text brush ')
  if ($l.Count -eq 0) { return $null }
  $m = [regex]::Match($l[0], 'active=(.*?) activeid=(\S*) open=(\d) window=(\S+) typed=(.*?) mode=(\S+) matches=(\d+) tiles=(\d+) highlight=(\S+) assigning=(\d) menu=(\d) favourites=(\d+) pick=(\d) recents=(\S*)$')
  [pscustomobject]@{ active = $m.Groups[1].Value; activeId = $m.Groups[2].Value; open = [int]$m.Groups[3].Value; window = $m.Groups[4].Value; typed = $m.Groups[5].Value; mode = $m.Groups[6].Value
                     matches = [int]$m.Groups[7].Value; tiles = [int]$m.Groups[8].Value; highlight = $m.Groups[9].Value; assigning = [int]$m.Groups[10].Value; menu = [int]$m.Groups[11].Value
                     favourites = [int]$m.Groups[12].Value; pick = [int]$m.Groups[13].Value; recents = $m.Groups[14].Value }
}
function Text2() { $m = [regex]::Match(@(Lines 'text layout')[0], 'layout=(.*?) status=(.*) floating=(\d+)$'); [pscustomobject]@{ layout = $m.Groups[1].Value; status = $m.Groups[2].Value; floating = [int]$m.Groups[3].Value } }
function BrushHint() { $l = @(Lines 'text brushhint='); if ($l.Count -eq 0) { return '' }; $l[0].Substring('text brushhint='.Length) }
function OpenLibrary() {
  KeyTap 'B'
  if (-not (WaitFor { (Brush).open -eq 1 } 3000)) { throw 'B did not open the brush library' }
  # The coordinates are valid once the first frame has laid the popup out (tiles are listed then).
  if (-not (WaitFor { @(VisibleTiles).Count -gt 0 } 5000)) { throw 'the opened library listed no tile' }
  Settle 400
}
function WaitClosed() { WaitFor { (Brush).open -eq 0 } 3000 }
function TileCenter($name) { WidgetCenter 'widget' $name }
function VisibleTiles() { @((ReadStatus) -split "`r?`n" | Where-Object { $_ -match '^widget brushtile-(?!recent-)(\S+?)=' } | ForEach-Object { [regex]::Match($_, '^widget brushtile-(\S+?)=').Groups[1].Value }) }
function RightClickAt($x, $y) {
  MoveTo $x $y; Start-Sleep -Milliseconds 60
  if (-not (Ours $x $y)) { BringForward; MoveTo $x $y; Start-Sleep -Milliseconds 60 }
  if (-not (Ours $x $y)) { throw "refusing to right-click at ${x},${y}: not a point of the preview" }
  [W]::mouse_event(0x8, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 50
  [W]::mouse_event(0x10, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 250
}
# Milliseconds from the B key going down to the first changed pixel of the screen at (x, y): the key's way
# through the OS, the event loop, the frame and the compositor.
function MeasureOpen([int]$x, [int]$y) {
  EnsureForeground
  $before = [G]::Pixel($x, $y)
  $scan = [byte][W]::MapVirtualKeyW(0x42, 0)
  $sw = [System.Diagnostics.Stopwatch]::StartNew()
  [W]::keybd_event(0x42, $scan, 0, [UIntPtr]::Zero)
  $ms = -1.0
  while ($sw.ElapsedMilliseconds -lt 600) { if ([G]::Pixel($x, $y) -ne $before) { $ms = $sw.Elapsed.TotalMilliseconds; break } }
  [W]::keybd_event(0x42, $scan, 2, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 200
  return $ms
}

try {
  Say "=== run 1: first start ==="
  Launch 'run1'
  $m = MainRect; Say "main window rect: $($m -join ',')"
  $vp = WidgetRect 'widget' 'viewport'; Say "viewport $($vp -join ',')"
  $cx = $vp[0] + [int]($vp[2] / 2); $cy = $vp[1] + [int]($vp[3] / 2)

  Say "1. first start"
  $b = Brush
  Check ($null -ne $b -and $b.active -eq 'Standard' -and $b.open -eq 0 -and $b.favourites -eq 0 -and $b.pick -eq 1) "the first start: Standard is the active brush, the library is closed, pick on unique is on ($($b.active))"
  MoveTo $cx $cy; Settle 300
  CaptureMain '00_start.png'

  Say "2. B opens the library over the viewport"
  OpenLibrary
  $b = Brush
  Check ($b.window -eq 'main' -and $b.tiles -ge 38 -and $b.typed -eq '') "B opened the library in the main window with $($b.tiles) tiles"
  $hint = BrushHint; Say "  hint: $hint"
  CaptureMain '01_open.png'

  Say "3. S then N: narrowing, then the second letter picks Snake Hook"
  KeyTap 'S'
  Check (WaitFor { (Brush).typed -eq 's' } 2000) 'S narrowed the list (typed s)'
  $b = Brush; Check ($b.matches -eq 10 -and $b.open -eq 1) "ten brushes start with S and the library stays open ($($b.matches))"
  CaptureMain '02_typed_s.png'
  KeyTap 'N'
  Check (WaitClosed) 'the second letter closed the library'
  $b = Brush; Check ($b.active -eq 'Snake Hook' -and $b.activeId -eq 'snake-hook') "the active brush is Snake Hook ($($b.active))"
  Check ((Text2).status -eq 'Brush: Snake Hook') "the status line says Brush: Snake Hook ($((Text2).status))"

  Say "4. arrows and Enter"
  OpenLibrary
  KeyTap 'Right'; KeyTap 'Down'; Settle 200
  $b = Brush; $expect = $b.highlight; Say "  highlighted: $expect"
  CaptureMain '03_arrows.png'
  KeyTap 'Enter'
  Check (WaitClosed) 'Enter closed the library'
  Check ((Brush).activeId -eq $expect -and $expect -ne '-') "Enter picked the highlighted brush ($expect)"

  Say "5. a click on a tile"
  OpenLibrary
  $tiles = VisibleTiles; Say "  visible tiles: $($tiles.Count)"
  $target = $tiles | Where-Object { $_ -ne (Brush).activeId } | Select-Object -Skip 3 -First 1
  $c = TileCenter "brushtile-$target"
  Check ($null -ne $c) "the tile $target is on screen"
  Say "  clicking tile $target at $($c -join ',')"
  CaptureMain '03b_before_click.png'
  Click $c[0] $c[1]
  Settle 300
  Say "  after the click: $(((Lines 'text brush ')[0]))"
  Check (WaitClosed) 'the click closed the library'
  Check ((Brush).activeId -eq $target) "the click picked $target ($((Brush).activeId))"

  Say "6. the star"
  OpenLibrary
  $tiles = VisibleTiles
  $starTarget = $tiles | Where-Object { $_ -ne (Brush).activeId } | Select-Object -Skip 5 -First 1
  $c = TileCenter "brushstar-$starTarget"
  Click $c[0] $c[1]
  Settle 300
  $b = Brush; Check ($b.favourites -eq 1 -and $b.open -eq 1) "a click on the star of $starTarget made a favourite and kept the library open"
  CaptureMain '04_star.png'
  KeyTap 'Escape'
  Check (WaitClosed) 'Escape closed the library'

  Say "7. Backspace widens, Escape closes without picking"
  $before = (Brush).activeId
  OpenLibrary
  KeyTap 'S'; KeyTap 'M'
  Check (WaitFor { (Brush).typed -eq 'sm' } 2000) 'S, M typed'
  $b = Brush; Check ($b.matches -eq 2 -and $b.open -eq 1) "two brushes start with SM and none is picked at once ($($b.matches))"
  CaptureMain '05_sm.png'
  KeyTap 'Backspace'
  Check (WaitFor { (Brush).typed -eq 's' } 2000) 'Backspace removed the M'
  Check ((Brush).matches -eq 10) 'the list is wide again (10)'
  KeyTap 'Escape'
  Check (WaitClosed) 'Escape closed the library'
  Check ((Brush).activeId -eq $before) 'Escape picked nothing'

  Say "8. B again closes it; Tab switches to the search"
  OpenLibrary
  KeyTap 'B'
  Check (WaitClosed) 'B again closed the library'
  OpenLibrary
  KeyTap 'Tab'
  Check (WaitFor { (Brush).mode -eq 'search' } 2000) 'Tab switched to the search mode'
  TypeText 'ish'
  Check (WaitFor { (Brush).typed -eq 'ish' } 2000) 'ish typed'
  Check ((Brush).matches -eq 2 -and (Brush).open -eq 1) "two names contain ish ($((Brush).matches))"
  CaptureMain '06_search.png'
  KeyTap 'Escape'
  Check (WaitClosed) 'Escape closed the search'

  Say "9. B inside a text field types B"
  $pos = WidgetCenter 'widget' 'posx'
  if ($null -eq $pos) { Say '  (the inspector shows no position field: skipped)'; $script:fail++ } else {
    Click $pos[0] $pos[1]
    KeyTap 'B'
    Settle 400
    Check ((Brush).open -eq 0) 'B typed in a number field did not open the brush library'
    KeyTap 'Escape'; Settle 300
    Click $cx $cy   # a click in the viewport takes the focus out of the field
  }

  Say "10. assign a letter from the tile menu"
  OpenLibrary
  $tiles = VisibleTiles
  $letterTarget = $tiles | Where-Object { $_ -eq 'blob' } | Select-Object -First 1
  if (-not $letterTarget) { $letterTarget = $tiles | Select-Object -Skip 2 -First 1 }
  $c = TileCenter "brushtile-$letterTarget"
  RightClickAt $c[0] $c[1]
  Check (WaitFor { (Brush).menu -eq 1 } 2000) 'a right click opened the tile menu'
  CaptureMain '07_menu.png'
  KeyTap 'Down'; KeyTap 'Down'; KeyTap 'Enter'
  Check (WaitFor { (Brush).assigning -eq 1 } 2000) 'Assign letter opened the popover'
  KeyTap 'X'
  Settle 300
  CaptureMain '08_assign.png'
  Say "  hint: $(BrushHint)"
  KeyTap 'Enter'
  Check (WaitFor { (Brush).assigning -eq 0 } 2000) 'Enter assigned the letter'
  KeyTap 'Escape'
  Check (WaitClosed) 'closed'
  OpenLibrary
  KeyTap 'X'
  Check (WaitClosed) 'the new letter X picked its brush at once'
  Check ((Brush).activeId -eq $letterTarget) "X picked $letterTarget ($((Brush).activeId))"

  Say "11. B in a floating window"
  $tab = TabPos 2
  Click $tab[0] $tab[1]
  KeyTap 'F' @('Ctrl', 'Alt')
  Check (WaitFor { @(Floats).Count -ge 1 } 4000) 'the outliner floated into a native window'
  Settle 600
  $f = @(Floats)[0]
  Click ($f.x + [int]($f.w / 2)) ($f.y + [int]($f.h * 0.75))
  Settle 300
  KeyTap 'B'
  Check (WaitFor { (Brush).open -eq 1 } 3000) 'B opened the library'
  Settle 400
  $b = Brush
  Check ($b.window -eq 'float') "the library opened in the floating window ($($b.window))"
  CaptureWindow ([IntPtr]([Convert]::ToInt64($f.hwnd, 16))) '09_float.png'
  KeyTap 'I'
  Check (WaitClosed) 'one letter picked Inflate in the floating window'
  Check ((Brush).active -eq 'Inflate') "the active brush is Inflate ($((Brush).active))"
  KeyTap 'B'
  Check (WaitFor { (Brush).open -eq 1 } 3000) 'B opened it again in the floating window'
  [void][W]::PostMessage([IntPtr]([Convert]::ToInt64($f.hwnd, 16)), 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
  Check (WaitFor { @(Floats).Count -eq 0 -and (Brush).open -eq 0 } 4000) 'closing the window closed the library with it'
  Check (-not $script:proc.HasExited) 'the preview is still running'
  BringForward
  MoveTo $cx $cy
  OpenLibrary
  CaptureMain '10_after_float.png'
  KeyTap 'Escape'
  Check (WaitClosed) 'closed'

  Say "12. key to first changed pixel (includes the display refresh)"
  $times = @()
  for ($i = 0; $i -lt 8; $i++) {
    MoveTo $cx $cy; Settle 350
    $ms = MeasureOpen $cx $cy
    $times += $ms
    KeyTap 'Escape'; Start-Sleep -Milliseconds 300
  }
  $sorted = @($times | Where-Object { $_ -ge 0 } | Sort-Object)
  if ($sorted.Count -gt 0) { Say ("  key to pixel ms: min {0:N1} median {1:N1} max {2:N1} (n={3}, undetected={4})" -f $sorted[0], $sorted[[int]($sorted.Count / 2)], $sorted[-1], $sorted.Count, (8 - $sorted.Count)) }
  Check ($sorted.Count -ge 6) 'the popup appeared in at least six of eight measured openings'
  if ($sorted.Count -gt 0) { Check ($sorted[[int]($sorted.Count / 2)] -lt 100) "the median key to pixel time is under 100 ms ($([int]$sorted[[int]($sorted.Count / 2)]) ms)" }

  Say "13. idle CPU and GPU"
  # The decisive number is the count of frames the app presents while nothing happens; the CPU time is
  # printed too but is only informative (other applications on a shared machine disturb it).
  function FramesNow() { [int][regex]::Match((ReadStatus), '(?m)^text frames=(\d+)').Groups[1].Value }
  OpenLibrary
  Settle 1500
  $f0 = FramesNow; $cpuOpen = CpuPercent 5; $f1 = FramesNow
  Say ("  library open and idle for 5 s: {0} frames presented, CPU {1:N2} % of one core" -f ($f1 - $f0), $cpuOpen)
  Check (($f1 - $f0) -le 3) 'an open idle library presents no frames (at most 3 in 5 s)'
  KeyTap 'Escape'; WaitClosed | Out-Null
  Settle 1000
  $f0 = FramesNow; $cpuClosed = CpuPercent 5; $f1 = FramesNow
  Say ("  library closed and idle for 5 s: {0} frames presented, CPU {1:N2} % of one core" -f ($f1 - $f0), $cpuClosed)
  Check (($f1 - $f0) -le 3) 'a closed idle library presents no frames (at most 3 in 5 s)'
  $smi = & nvidia-smi --query-compute-apps=pid,gpu_uuid --format=csv,noheader 2>$null
  $mine = @($smi | Where-Object { $_ -match "^$($script:procId)," })
  Say "  GPU processes of ours: $($mine -join ' | ')"
  Check (@($mine | Where-Object { $_ -match 'GPU-7b76b9f0' }).Count -eq 0) 'nothing of the preview is on the RTX 3090 (GPU-7b76b9f0...)'
  CloseApp 'run1'

  Say "=== run 2: restart over the same data ==="
  Launch 'run2'
  $b = Brush
  Check ($b.active -eq 'Inflate' -or $b.activeId -eq $letterTarget) "the last pick is the active brush again ($($b.active))"
  Check ($b.favourites -eq 1) 'the favourite survived'
  Check (Test-Path (Join-Path $data 'brushes.json')) 'brushes.json exists'
  MoveTo $cx $cy; Settle 300
  OpenLibrary
  CaptureMain '11_restarted.png'
  KeyTap 'X'
  Check (WaitClosed) 'the letter X still picks its brush after the restart'
  Check ((Brush).activeId -eq $letterTarget) "X picked $letterTarget"
  CloseApp 'run2'
}
catch {
  Say "  ERROR: $($_.Exception.Message)"
  $script:fail++
} finally {
  if ($script:proc -and -not $script:proc.HasExited) {
    foreach ($h in @([W]::Windows($script:procId))) { [void][W]::PostMessage($h, 0x10, [IntPtr]::Zero, [IntPtr]::Zero) }
    Start-Sleep -Milliseconds 1500
    if (-not $script:proc.HasExited) { $script:proc.Kill() }
  }
  [W]::mouse_event(0x4, 0, 0, 0, [UIntPtr]::Zero)
  [W]::mouse_event(0x10, 0, 0, 0, [UIntPtr]::Zero)
  Say "leftover preview processes: $(@(Get-Process r1gui-preview -ErrorAction SilentlyContinue | Where-Object { $_.Id -eq $script:procId }).Count)"
  Say "failures: $script:fail"
}
exit $script:fail
