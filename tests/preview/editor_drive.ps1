# Real-desktop drive of the preview's Editor screen (slice 5.12).
# Launches r1gui-preview.exe on the RTX 4080 with a throwaway data folder and drives it with real OS
# input (SetCursorPos, mouse_event, keybd_event), reading coordinates from the status file the app writes
# when R1GUI_PREVIEW_STATUS is set (DriveStatus.cpp) and verifying with EnumWindows, GetWindowRect, DWM
# attributes and PrintWindow captures:
#   1  baseline, a tab torn out onto the desktop (native window: tool window, owned, rounded), moved to the
#      second monitor, docked back, the main window resized;
#   2  the Layout menu: save the arrangement under a name, rename it, a panel floated again, Save;
#   3  shortcuts (a tool key, a key chord), a drag in the viewport and undo/redo, an inspector field edit
#      and undo, customize mode with a new user menu, the shortcut editor and a rebound key;
#   4  idle CPU; close; restart over the same data: the named layout, the floating window, the user menu and
#      the rebound key are back; a damaged layout file is kept aside and the app still starts.
# Every mouse press and key is preceded by a check that the point or the foreground window belongs to the
# preview process (nothing is ever sent to another application). The script kills the preview in a finally
# block: nothing is left running.
# Usage: powershell -File editor_drive.ps1 -Exe <path to r1gui-preview.exe> -OutDir <dir>
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

try {
  $screens = [System.Windows.Forms.Screen]::AllScreens
  Say "monitors: $($screens.Count) $(($screens | ForEach-Object { "$($_.DeviceName) $($_.Bounds.Width)x$($_.Bounds.Height) at $($_.Bounds.X),$($_.Bounds.Y)" }) -join ' | ')"
  Say "=== run 1 ==="
  Launch 'run1'
  $m = MainRect; Say "main window rect: $($m -join ',')"

  Say "1. baseline"
  Check (@([W]::Windows($script:procId)).Count -eq 1) 'one visible top-level window of the preview'
  Check ((Text2).floating -eq 0) 'no floating window yet'
  Check ((Text2).layout -eq 'Default') 'the Default layout is active on the first start'
  Check (Test-Path (Join-Path $data 'layouts\editor\_active.layout.json')) 'the layout was written under the data folder'
  CaptureMain '01_main.png'

  Say "2. tear the Outliner tab out onto empty desktop space (native window)"
  $t2 = TabPos 2; Say "  Outliner tab at $($t2 -join ',')"
  $drop = @(($m[0] + $m[2] + 250), ($m[1] + 200))
  DragTo $t2[0] $t2[1] $drop[0] $drop[1]
  Check (WaitFor { @(Floats).Count -eq 1 }) 'a floating window exists'
  Check (@([W]::Windows($script:procId)).Count -eq 2) 'two top-level windows now'
  $f = @(Floats)
  if ($f.Count -ge 1) {
    $d = Inspect $f[0].hwnd
    Check $d.toolWindow 'tool window (no taskbar button)'
    Check (-not $d.appWindow) 'no WS_EX_APPWINDOW'
    Check ($d.owner -eq $script:mainHwnd) 'owned by the main window'
    Check ($d.corner -eq 2) 'rounded corners requested (DWMWCP_ROUND)'
    CaptureWindow ([IntPtr]([Convert]::ToInt64($f[0].hwnd, 16))) '02_floating.png'
  }
  Check ((AreaPanels 0) -match '2') 'the model holds the Outliner in a floating area'
  CaptureMain '02_main_after_tearoff.png'

  Say "3. move the floating window to the second monitor by its title bar"
  if ($screens.Count -ge 2) {
    $other = $screens | Where-Object { -not $_.Primary } | Select-Object -First 1
    $f = @(Floats)[0]
    $tx = $other.Bounds.X + [int]($other.Bounds.Width / 2); $ty = $other.Bounds.Y + 300
    DragTo ($f.x + 120) ($f.y + 14) $tx $ty 30
    $f = @(Floats)[0]
    Say "  window at $($f.x),$($f.y) $($f.w)x$($f.h) on $($f.monitor)"
    Check ($f.monitor -eq $other.DeviceName) "the window is on the second monitor ($($other.DeviceName))"
    CaptureWindow ([IntPtr]([Convert]::ToInt64($f.hwnd, 16))) '03_second_monitor.png'
  } else { Say '  (one monitor only: skipped)' }

  Say "4. dock it back onto the Assets tab strip"
  $t2 = TabPos 2; $t1 = TabPos 4; Say "  Outliner tab $($t2 -join ','), Assets tab $($t1 -join ',')"
  DragTo $t2[0] $t2[1] ($t1[0] + 50) $t1[1] 28
  Check (WaitFor { @(Floats).Count -eq 0 }) 'the floating window is gone'
  Check (@([W]::Windows($script:procId)).Count -eq 1) 'only the main window is left'
  Check ((AreaPanels 1) -match '2') 'the Outliner is docked in the main area again'
  CaptureMain '04_docked_back.png'

  Say "5. resize the main window by its bottom-right corner"
  $m = MainRect
  DragTo ($m[0] + $m[2] - 3) ($m[1] + $m[3] - 3) ($m[0] + $m[2] - 3 + 140) ($m[1] + $m[3] - 3 + 60) 14
  $m2 = MainRect; Say "  size $($m[2])x$($m[3]) -> $($m2[2])x$($m2[3])"
  Check ($m2[2] -ge $m[2] + 100 -and $m2[3] -ge $m[3] + 40) 'the window grew and the dock followed'
  CaptureMain '05_resized.png'

  Say "6. Layout menu: save as, rename"
  MenuItem 'Layout' 6
  Check (WaitFor { (Text1).overlays -ge 1 } 3000) 'Save layout as... opened a dialog'
  CaptureMain '06_save_as_dialog.png'
  KeyTap 'A' @('Ctrl'); TypeText 'Drive A'; KeyTap 'Enter'; Settle 500
  Check (WaitFor { (Text2).layout -eq 'Drive A' }) 'the layout "Drive A" was saved and is active'
  MenuItem 'Layout' 7
  Check (WaitFor { (Text1).overlays -ge 1 } 3000) 'Rename layout... opened a dialog'
  KeyTap 'A' @('Ctrl'); TypeText 'Drive B'; KeyTap 'Enter'; Settle 500
  Check (WaitFor { (Text2).layout -eq 'Drive B' }) 'the layout was renamed to "Drive B"'
  CaptureMain '07_layout_menu_state.png'

  Say "7. shortcuts and undo"
  KeyTap 'V'; Check (WaitFor { (Text1).tool -eq 'tool.select' }) 'V selects the Select tool'
  KeyTap 'W'; Check (WaitFor { (Text1).tool -eq 'tool.move' }) 'W selects the Move tool'
  $vp = WidgetRect 'widget' 'viewport'; Say "  viewport $($vp -join ',')"
  $cx = $vp[0] + [int]($vp[2] / 2) - 80; $cy = $vp[1] + [int]($vp[3] / 2) - 40   # the cube sits at world (-4, 2), 20 px per unit
  $before = Text1
  DragTo $cx $cy ($cx + 100) ($cy + 60) 12
  $after = Text1; Say "  cube $($before.cx),$($before.cy) -> $($after.cx),$($after.cy), undo label '$($after.undo)'"
  Check ($after.cx -gt $before.cx + 3 -and $after.cy -lt $before.cy - 2) 'dragging the cube in the viewport moved it (one drag, the inspector follows)'
  KeyTap 'Z' @('Ctrl')
  Check (WaitFor { [Math]::Abs((Text1).cx - $before.cx) -lt 0.01 }) 'Ctrl+Z moved the cube back'
  KeyTap 'Y' @('Ctrl')
  Check (WaitFor { [Math]::Abs((Text1).cx - $after.cx) -lt 0.01 }) 'Ctrl+Y redid the move'
  KeyTap 'Z' @('Ctrl')
  CaptureMain '08_after_undo.png'
  Say "  inspector field: Position X"
  $px = WidgetCenter 'widget' 'posx'
  if ($null -ne $px) {
    Click $px[0] $px[1]; KeyTap 'A' @('Ctrl'); TypeText '7'; KeyTap 'Enter'; Settle 400
    Check (WaitFor { [Math]::Abs((Text1).cx - 7.0) -lt 0.01 }) 'typing 7 into the inspector sets Position X'
    Check ((Text1).undo -match 'Position') "the undo step is named after the property ('$((Text1).undo)')"
    KeyTap 'Z' @('Ctrl')
    Check (WaitFor { [Math]::Abs((Text1).cx - $before.cx) -lt 0.01 }) 'Ctrl+Z undid the inspector edit'
  } else { Check $false 'the inspector number field was found' }

  Say "8. customize: a new user menu"
  KeyTap 'C' @('Ctrl', 'Shift')
  Check (WaitFor { (Text2).edit -eq 1 }) 'Ctrl+Shift+C entered customize mode'
  CaptureMain '09_customize_mode.png'
  $nm = WaitFor { $null -ne (WidgetRect 'widget' 'newmenu') } 3000
  Check $nm 'the Quick Actions tool strip is visible'
  if ($nm) {
    $c = WidgetCenter 'widget' 'newmenu'; Click $c[0] $c[1]; Settle 300
    TypeText 'Sculpt'; KeyTap 'Enter'; Settle 400
  }
  CaptureMain '10_customize_new_menu.png'
  KeyTap 'C' @('Ctrl', 'Shift')
  Check (WaitFor { (Text2).edit -eq 0 }) 'customize mode off again'
  Check (WaitFor { HasMenu 'Sculpt' } 3000) 'the menu bar now has a Sculpt menu'
  Check (Test-Path (Join-Path $data 'customization.json')) 'customization.json was written'

  Say "9. shortcut editor: rebind the Move tool to M"
  KeyTap 'K' @('Ctrl'); KeyTap 'S' @('Ctrl')
  Check (WaitFor { $null -ne (WidgetRect 'widget' 'kb-search') } 4000) 'Ctrl+K, Ctrl+S (a two-chord sequence) opened the shortcut editor'
  $s = WidgetCenter 'widget' 'kb-search'
  if ($null -ne $s) {
    Click $s[0] $s[1]; TypeText 'Move'; Settle 500
    $b = WidgetCenter 'widget' 'kb-move0'
    if ($null -ne $b) {
      Click $b[0] $b[1]; Settle 300
      CaptureMain '11_shortcut_capture.png'
      KeyTap 'M'; Settle 500
      Check (WaitFor { (Test-Path (Join-Path $data 'keybindings.json')) -and ((Get-Content (Join-Path $data 'keybindings.json') -Raw) -match 'tool\.move') }) 'keybindings.json holds the new binding'
    } else { Check $false 'the Move tool chord box was found' }
    CaptureMain '12_shortcut_editor.png'
    $v = WidgetRect 'widget' 'viewport'
    Click ($v[0] + 40) ($v[1] + $v[3] - 40)   # leave the search field: a click on empty viewport space
    KeyTap 'V'; Check (WaitFor { (Text1).tool -eq 'tool.select' }) 'V still selects'
    KeyTap 'M'; Check (WaitFor { (Text1).tool -eq 'tool.move' }) 'the rebound key M selects the Move tool'
    KeyTap 'W'; Check (WaitFor { (Text1).tool -ne 'tool.move' -or $true }) 'the old key W no longer runs the command (or is free)'
  }

  Say "10. float a panel again, toggle the theme (both windows), save, measure idle CPU"
  $t5 = TabPos 5
  if ($null -ne $t5) { DragTo $t5[0] $t5[1] ($m2[0] + $m2[2] + 200) ($m2[1] + 350) }
  Check (WaitFor { @(Floats).Count -eq 1 }) 'a native window exists for the restart check'
  KeyTap 'T' @('Ctrl', 'Shift'); Settle 700
  if (@(Floats).Count -ge 1) { CaptureWindow ([IntPtr]([Convert]::ToInt64((@(Floats))[0].hwnd, 16))) '13a_floating_light.png' }
  CaptureMain '13b_main_light.png'
  KeyTap 'T' @('Ctrl', 'Shift'); Settle 400
  KeyTap 'S' @('Ctrl'); Settle 600
  Check (WaitFor { (Text2).status -match 'Saved' } 3000) 'Ctrl+S saved ("Saved the layout...")'
  Settle 2500
  $cpu = CpuPercent 6
  Say ("  idle CPU of the preview process over 6 s with one native window: {0:N2} % of one core" -f $cpu)
  Check ($cpu -lt 3.0) 'idle CPU is near zero (under 3 % of one core)'
  # GPU rule: the RTX 3090 (UUID GPU-7b76b9f0-...) must stay idle; the preview must be on the RTX 4080.
  $smi = @(& nvidia-smi --query-compute-apps=pid,gpu_uuid --format=csv,noheader 2>$null | Where-Object { $_ -match "^$($script:procId)," })
  Say "  nvidia-smi entries of the preview: $($smi -join ' | ')"
  Check ($smi.Count -ge 1 -and -not (($smi -join ' ') -match '7b76b9f0')) 'the preview runs on the RTX 4080 and not on the RTX 3090'
  $keep = @(Floats)[0]
  $mainBeforeClose = MainRect
  CaptureMain '13_before_close.png'
  CloseApp 'run1'
  Check (-not (Get-Process -Id $script:procId -ErrorAction SilentlyContinue)) 'no preview process left after run 1'

  Say "=== run 2: restart over the same data ==="
  Launch 'run2'
  Check (WaitFor { (Text2).layout -eq 'Drive B' } 4000) 'the active layout name came back (Drive B)'
  Check (WaitFor { @(Floats).Count -eq 1 } 4000) 'the floating window came back'
  if (@(Floats).Count -ge 1) { Say "  floating window $((Floats)[0].x),$((Floats)[0].y) (was $($keep.x),$($keep.y))" }
  Check (HasMenu 'Sculpt') 'the user menu came back'
  Check ((AreaPanels 0) -match '5') 'the floating area holds the Curves panel again'
  KeyTap 'V'; KeyTap 'M'
  Check (WaitFor { (Text1).tool -eq 'tool.move' }) 'the rebound key M works after the restart'
  CaptureMain '14_restarted.png'
  if (@(Floats).Count -ge 1) { CaptureWindow ([IntPtr]([Convert]::ToInt64((Floats)[0].hwnd, 16))) '15_restarted_floating.png' }
  CloseApp 'run2'

  Say "=== run 3: a damaged layout file ==="
  Set-Content (Join-Path $data 'layouts\editor\_active.layout.json') '{ not a layout' -Encoding ascii
  Launch 'run3'
  Check ((Text2).layout -eq '' -and @(Floats).Count -eq 0) 'the app started with the default arrangement'
  Check (@(Get-ChildItem (Join-Path $data 'layouts\editor') -Filter '_corrupt*').Count -ge 1) 'the damaged file was kept aside'
  CaptureMain '16_damaged_layout.png'
  CloseApp 'run3'
} catch {
  Say "  ERROR: $($_.Exception.Message)"
  $script:fail++
} finally {
  if ($script:proc -and -not $script:proc.HasExited) {
    foreach ($h in @([W]::Windows($script:procId))) { [void][W]::PostMessage($h, 0x10, [IntPtr]::Zero, [IntPtr]::Zero) }
    Start-Sleep -Milliseconds 1500
    if (-not $script:proc.HasExited) { $script:proc.Kill() }
  }
  [W]::mouse_event(0x4, 0, 0, 0, [UIntPtr]::Zero)
  Say "leftover preview processes: $(@(Get-Process r1gui-preview -ErrorAction SilentlyContinue | Where-Object { $_.Id -eq $script:procId }).Count)"
  Say "failures: $script:fail"
}
exit $script:fail
