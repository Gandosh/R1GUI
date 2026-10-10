# Real-desktop drive of the custom menu features of the preview's Editor screen (slices 5.13 to 5.19).
# Launches r1gui-preview.exe on the RTX 4080 with a throwaway data folder and drives it with real OS
# input (SetCursorPos, mouse_event, keybd_event), reading coordinates from the status file the app writes
# when R1GUI_PREVIEW_STATUS is set (DriveStatus.cpp) and verifying with the status text, files on disk and
# PrintWindow captures:
#   1  the sample Tools Pie and Quick Tools panel of the first start, the Custom Menus menu;
#   2  the pie in the viewport: hold the right mouse button, flick toward slots, a quick click, Escape;
#   3  Custom Menus > Create: choose Pie, drag three actions from the list onto slots, name it, Create,
#      use it in the viewport; the same for a dockable menu with Move and Rotate; close its panel and open
#      it again from the menu;
#   4  save a menu as .r1mn, delete it, load it again;
#   5  the hotkey editor (a native window): select an action, click a key on the keyboard, press the key;
#   6  save a custom workspace, change things, load it; idle CPU; GPU check;
#   7  restart over the same data: the menus, the panels and the pie are back.
# Every mouse press and key is preceded by a check that the point or the foreground window belongs to the
# preview process (nothing is ever sent to another application). The script kills the preview in a finally
# block: nothing is left running.
# Usage: powershell -File menus_drive.ps1 -Exe <path to r1gui-preview.exe> -OutDir <dir>
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
function CMenus() {
  @(Lines 'cmenu') | ForEach-Object {
    $m = [regex]::Match($_, '^cmenu (\S+) (\w+)\|(.*?)\|(.*)$')
    [pscustomobject]@{ id = $m.Groups[1].Value; kind = $m.Groups[2].Value; name = $m.Groups[3].Value; entries = $m.Groups[4].Value.TrimEnd(',') }
  }
}
function MenuByName($name) { @(CMenus) | Where-Object { $_.name -eq $name } | Select-Object -First 1 }
function Creator() {
  $l = @(Lines 'text creator')
  if ($l.Count -eq 0) { return $null }
  $m = [regex]::Match($l[0], 'chooser=(\d) kind=(\S+) filled=(\d+) editing=(\d) issue=(.*?) status=(.*)$')
  [pscustomobject]@{ chooser = [int]$m.Groups[1].Value; kind = $m.Groups[2].Value; filled = [int]$m.Groups[3].Value; editing = [int]$m.Groups[4].Value; issue = $m.Groups[5].Value; status = $m.Groups[6].Value }
}
function Text2() { $m = [regex]::Match(@(Lines 'text layout')[0], 'layout=(.*?) status=(.*) floating=(\d+)$'); [pscustomobject]@{ layout = $m.Groups[1].Value; status = $m.Groups[2].Value; floating = [int]$m.Groups[3].Value } }
function ViewportPie() { [regex]::Match(@(Lines 'text viewportpie')[0], 'viewportpie=(\S*)').Groups[1].Value }
function RightDown($x, $y) {
  MoveTo $x $y; Start-Sleep -Milliseconds 60
  if (-not (Ours $x $y)) { BringForward; MoveTo $x $y; Start-Sleep -Milliseconds 60 }
  if (-not (Ours $x $y)) { throw "refusing to press the right button at ${x},${y}: not a point of the preview" }
  [W]::mouse_event(0x8, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 40
}
function RightUp() { [W]::mouse_event(0x10, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 120 }
# Holds the right button at (x, y), waits for the pie to be drawn, moves by (dx, dy) in steps, optionally
# captures the window, and releases.
function Flick([double]$x, [double]$y, [double]$dx, [double]$dy, [string]$capture = '', [bool]$escape = $false) {
  if (-not (ForegroundIsOurs)) { BringForward }
  RightDown $x $y
  Start-Sleep -Milliseconds 260
  $steps = 8
  for ($i = 1; $i -le $steps; $i++) { MoveTo ($x + $dx * $i / $steps) ($y + $dy * $i / $steps); Start-Sleep -Milliseconds 25 }
  Start-Sleep -Milliseconds 200
  if ($capture) { CaptureMain $capture }
  if ($escape) { KeyTap 'Escape'; Start-Sleep -Milliseconds 100 }
  RightUp
  Settle 400
}
function Chain($title, [string[]]$keys) {
  $c = WidgetCenter 'menu' $title
  if ($null -eq $c) { throw "no menu $title" }
  Click $c[0] $c[1]
  Settle 250
  foreach ($k in $keys) {
    if ($k -match '^Down(\d+)$') { $n = [int]$Matches[1]; for ($i = 0; $i -lt $n; $i++) { KeyTap 'Down'; Settle 180 } } else { KeyTap $k; Settle 350 }
  }
  Settle 400
}
$KeyCodes['End'] = 0x23; $KeyCodes['Home'] = 0x24; $KeyCodes['Right'] = 0x27; $KeyCodes['Left'] = 0x25; $KeyCodes['Backspace'] = 0x08; $KeyCodes['Delete'] = 0x2E
function ClearAndType([string]$text) { KeyTap 'A' @('Ctrl'); if ($text) { TypeText $text } else { KeyTap 'Backspace' } }
function WaitWidget($name, [int]$ms = 5000) { WaitFor { $null -ne (WidgetRect 'widget' $name) } $ms }
function ClickWidget($name) { $c = WidgetCenter 'widget' $name; if ($null -eq $c) { throw "no widget $name" }; Click $c[0] $c[1] }
function SearchAction([string]$text) {
  ClickWidget 'creator-search'
  ClearAndType $text
  Settle 500
}
# Drags the first row of the creator's action list (after a search) onto a point.
function DragRowTo($x, $y) {
  if (-not (WaitWidget 'creator-row0' 2500)) { Say '  (no action row after the search)'; return }
  $r = WidgetRect 'widget' 'creator-row0'
  DragTo ($r[0] + 60) ($r[1] + [int]($r[3] / 2)) $x $y 14
}
function Slot($i) { WidgetCenter 'widget' "creator-slot$i" }
function CloseTabOf($panelId) { $t = TabPos $panelId; if ($null -eq $t) { throw "no tab $panelId" }; Click $t[0] $t[1]; KeyTap 'W' @('Ctrl'); Settle 400 }


# Searches for an action and drags its row onto the target until the draft holds `expectFilled` actions
# (a drag that the desktop dropped is repeated, at most three times).
function PlaceAction([string]$search, $target, [int]$expectFilled) {
  for ($try = 0; $try -lt 3; $try++) {
    SearchAction $search
    DragRowTo $target[0] $target[1]
    if (WaitFor { (Creator).filled -ge $expectFilled } 2500) { return $true }
  }
  return $false
}


try {
  $screens = [System.Windows.Forms.Screen]::AllScreens
  Say "monitors: $($screens.Count)"
  Say "=== run 1: first start ==="
  Launch 'run1'
  $m = MainRect; Say "main window rect: $($m -join ',')"

  Say "1. the first start: sample menus, the Custom Menus menu, the Quick Tools panel"
  Check (HasMenu 'Custom Menus') 'the menu bar has a Custom Menus menu'
  Check ((@(CMenus)).Count -eq 2) 'two sample custom menus exist'
  $pie0 = MenuByName 'Tools Pie'; $quick = MenuByName 'Quick Tools'
  Check ($null -ne $pie0 -and $pie0.kind -eq 'pie' -and $pie0.entries -like 'tool.move,*') 'the Tools Pie starts with Move at the top'
  Check ($null -ne $quick -and $quick.kind -eq 'panel') 'the Quick Tools dockable menu exists'
  Check (WaitFor { $null -ne (WidgetRect 'widget' "cmpanel-$($quick.id)") } 3000) 'the Quick Tools panel is open in the dock'
  Check ((ViewportPie) -eq $pie0.id) 'the Tools Pie is the viewport pie'
  CaptureMain '01_first_start.png'

  Say "2. the pie in the viewport: hold the right button and flick"
  $vp = WidgetRect 'widget' 'viewport'; Say "  viewport $($vp -join ',')"
  $px = $vp[0] + [int]($vp[2] / 2) + 120; $py = $vp[1] + [int]($vp[3] / 2) + 60
  Check ((Text1).tool -eq 'tool.select') 'the Select tool is active'
  Flick $px $py 0 -90 '02_pie_up.png'
  Check (WaitFor { (Text1).tool -eq 'tool.move' } 2500) 'flicking up (Move) ran the Move tool'
  Flick $px $py 90 0
  Check (WaitFor { (Text1).tool -eq 'tool.scale' } 2500) 'flicking right (Scale) ran the Scale tool'
  KeyTap 'V'
  $before = (Text1).tool
  RightDown $px $py; RightUp
  Settle 300
  Check ((Text1).tool -eq $before) 'a quick right click runs nothing'
  Flick $px $py 0 -90 '' $true
  Check ((Text1).tool -eq $before) 'Escape during the hold cancels the pie: nothing ran'

  Say "3. Custom Menus > Create Custom Menu: a pie menu"
  Chain 'Custom Menus' @('Down4', 'Enter')
  Check (WaitFor { @(Floats).Count -eq 1 -and $null -ne (WidgetRect 'widget' 'creator-pie-card') } 6000) 'the creator opened in a native window and shows the type chooser'
  $f = @(Floats)[0]
  $d = Inspect $f.hwnd
  Check ($d.toolWindow -and $d.owner -eq $script:mainHwnd) 'the creator is a native tool window owned by the main window'
  CaptureWindow ([IntPtr]([Convert]::ToInt64($f.hwnd, 16))) '03_creator_chooser.png'
  ClickWidget 'creator-pie-card'
  Check (WaitWidget 'creator-pie') 'choosing Pie shows the pie preview'
  Check ((Creator).kind -eq 'pie' -and (Creator).chooser -eq 0) 'the draft is a pie'
  CaptureWindow ([IntPtr]([Convert]::ToInt64((@(Floats))[0].hwnd, 16))) '04_creator_pie_empty.png'
  Check (PlaceAction 'move' (Slot 0) 1) 'dragging Move from the list onto the top slot filled it'
  Check (PlaceAction 'rotate' (Slot 2) 2) 'dragging Rotate onto the right slot filled it'
  Check (PlaceAction 'scale' (Slot 4) 3) 'dragging Scale onto the bottom slot filled it'
  Say "  creator: $((Creator).status)"
  ClickWidget 'creator-name'; ClearAndType 'Drive Pie'
  Check (WaitFor { (Creator).issue -eq '' } 2000) 'with a name and three actions the draft is valid'
  CaptureWindow ([IntPtr]([Convert]::ToInt64((@(Floats))[0].hwnd, 16))) '05_creator_pie_filled.png'
  ClickWidget 'creator-create'
  Check (WaitFor { $null -ne (MenuByName 'Drive Pie') } 4000) 'Create made the pie menu'
  Check (WaitFor { @(Floats).Count -eq 0 } 4000) 'the creator window closed'
  $drivePie = MenuByName 'Drive Pie'
  Check ($drivePie.entries -eq 'tool.move,,tool.rotate,,tool.scale') "the pie holds Move, Rotate and Scale on the dragged slots ($($drivePie.entries))"
  Check ((ViewportPie) -eq $drivePie.id) 'the new pie is the viewport pie'
  KeyTap 'V'
  Flick $px $py 90 0
  Check (WaitFor { (Text1).tool -eq 'tool.rotate' } 2500) 'flicking right in the viewport ran Rotate from the new pie'
  CaptureMain '06_after_create_pie.png'

  Say "4. a dockable menu with Move and Rotate"
  Chain 'Custom Menus' @('Down5', 'Enter')
  Check (WaitFor { @(Floats).Count -eq 1 -and $null -ne (WidgetRect 'widget' 'creator-panel-card') } 6000) 'the creator opened again on the type chooser'
  ClickWidget 'creator-panel-card'
  Check (WaitWidget 'creator-panelview') 'choosing Dockable panel shows the panel preview'
  Check (PlaceAction 'move' (WidgetCenter 'widget' 'creator-cell0') 1) 'Move was dropped into the panel'
  Check (PlaceAction 'rotate' (WidgetCenter 'widget' 'creator-cell1') 2) 'Rotate was dropped after it'
  ClickWidget 'creator-name'; ClearAndType 'Drive Panel'
  CaptureWindow ([IntPtr]([Convert]::ToInt64((@(Floats))[0].hwnd, 16))) '07_creator_panel_filled.png'
  ClickWidget 'creator-create'
  Check (WaitFor { $null -ne (MenuByName 'Drive Panel') } 4000) 'Create made the dockable menu'
  $panelMenu = MenuByName 'Drive Panel'
  Check (WaitFor { $null -ne (WidgetRect 'widget' "cmpanel-$($panelMenu.id)") } 4000) 'its panel opened in the dock'
  $pid1 = 1000 + [int]($panelMenu.id -replace 'menu\.', '')
  Check (WaitFor { @(Floats).Count -eq 0 } 3000) 'the creator window closed'
  $b1 = WidgetCenter 'widget' "cmpanel-$($panelMenu.id)-button1"
  if ($null -ne $b1) { Click $b1[0] $b1[1]; Check (WaitFor { (Text1).tool -eq 'tool.rotate' } 2000) 'its Rotate button ran the Rotate tool' } else { Check $false 'the panel has a second button' }
  KeyTap 'V'
  CaptureMain '08_panel_created.png'

  Say "5. close the panel and open it again from Custom Menus"
  CloseTabOf $pid1
  Check (WaitFor { $null -eq (WidgetRect 'widget' "cmpanel-$($panelMenu.id)") } 3000) 'the panel closed'
  Chain 'Custom Menus' @('Down2', 'Enter')
  Check (WaitFor { $null -ne (WidgetRect 'widget' "cmpanel-$($panelMenu.id)") } 4000) 'Custom Menus > Drive Panel opened it again'

  Say "6. save the pie as a .r1mn file, delete it, load it again"
  Chain 'Custom Menus' @('Down4', 'Right', 'Down1', 'Enter')
  Check (WaitFor { (Text1).overlays -ge 1 } 4000) 'Save As... opened the file dialog'
  Settle 600
  CaptureMain '09_save_dialog.png'
  KeyTap 'A' @('Ctrl'); TypeText 'drivepie'; KeyTap 'Enter'; Settle 500
  Check (WaitFor { Test-Path (Join-Path $data 'menus\drivepie.r1mn') } 3000) 'drivepie.r1mn was written under the data folder'
  Chain 'Custom Menus' @('Down4', 'Right', 'Down2', 'Enter')
  Check (WaitFor { (Text1).overlays -ge 1 } 4000) 'Delete asks first'
  KeyTap 'Tab'; KeyTap 'Enter'; Settle 500
  Check (WaitFor { $null -eq (MenuByName 'Drive Pie') } 3000) 'the pie was deleted'
  Chain 'Custom Menus' @('End', 'Enter')
  Check (WaitFor { (Text1).overlays -ge 1 } 4000) 'Load Custom Menu... opened the file dialog'
  CaptureMain '10_load_dialog.png'
  KeyTap 'A' @('Ctrl'); TypeText 'drivepie'; KeyTap 'Enter'; Settle 500
  Check (WaitFor { $null -ne (MenuByName 'Drive Pie') } 3000) 'the pie came back from the file'
  Check (((MenuByName 'Drive Pie').entries) -eq 'tool.move,,tool.rotate,,tool.scale') 'with its three actions'

  Say "7. the hotkey editor: pick an action, click a key"
  KeyTap 'K' @('Ctrl'); KeyTap 'S' @('Ctrl')
  Check (WaitFor { $null -ne (WidgetRect 'widget' 'hk-editor') } 6000) 'Ctrl+K, Ctrl+S opened the hotkey editor'
  Check (WaitFor { @(Floats).Count -eq 1 } 3000) 'the hotkey editor is a native window of its own'
  $hk = @(Floats)[0]
  ClickWidget 'hk-search'; ClearAndType 'move'; Settle 500
  ClickWidget 'hk-row0'; Settle 400
  Check (WaitFor { @(Lines 'text hotkeys')[0] -match 'selected=tool.move' } 3000) 'the Move action is selected in the list'
  CaptureWindow ([IntPtr]([Convert]::ToInt64($hk.hwnd, 16))) '11_hotkey_editor_selected.png'
  ClickWidget 'hk-cap-M'; Settle 600
  CaptureWindow ([IntPtr]([Convert]::ToInt64($hk.hwnd, 16))) '12_hotkey_editor_assigned.png'
  Check (WaitFor { (Test-Path (Join-Path $data 'keybindings.json')) -and ((Get-Content (Join-Path $data 'keybindings.json') -Raw) -match 'tool\.move') } 4000) 'clicking the M key assigned it: keybindings.json holds the binding'
  [void][W]::PostMessage([IntPtr]([Convert]::ToInt64($hk.hwnd, 16)), 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
  Check (WaitFor { @(Floats).Count -eq 0 } 4000) 'the hotkey editor window closed'
  $vp = WidgetRect 'widget' 'viewport'
  Click ($vp[0] + 30) ($vp[1] + $vp[3] - 30)
  KeyTap 'V'; Check (WaitFor { (Text1).tool -eq 'tool.select' } 2000) 'V selects the Select tool'
  KeyTap 'M'; Check (WaitFor { (Text1).tool -eq 'tool.move' } 2000) 'the new key M runs the Move tool'
  KeyTap 'V'

  Say "8. a custom workspace"
  $layoutBefore = AreaPanels 1
  Chain 'Layout' @('End', 'Up', 'Enter')
  Check (WaitFor { (Text1).overlays -ge 1 } 4000) 'Save custom workspace... opened the file dialog'
  Settle 400
  CaptureMain '13_workspace_save_dialog.png'
  KeyTap 'A' @('Ctrl'); TypeText 'drivews'; KeyTap 'Enter'; Settle 600
  Check (WaitFor { Test-Path (Join-Path $data 'workspaces\drivews.r1ws') } 3000) 'drivews.r1ws was written'
  Chain 'Layout' @('Down2', 'Enter')
  Check (WaitFor { (AreaPanels 1) -ne $layoutBefore } 3000) 'switching to Modeling changed the arrangement'
  Chain 'Custom Menus' @('Down4', 'Right', 'Down2', 'Enter')
  KeyTap 'Tab'; KeyTap 'Enter'; Settle 500
  Check (WaitFor { $null -eq (MenuByName 'Drive Pie') } 3000) 'a menu was deleted'
  Chain 'Layout' @('End', 'Enter')
  Check (WaitFor { (Text1).overlays -ge 1 } 4000) 'Load custom workspace... opened the file dialog'
  Settle 400
  CaptureMain '14_workspace_load_dialog.png'
  KeyTap 'A' @('Ctrl'); TypeText 'drivews'; KeyTap 'Enter'; Settle 800
  Check (WaitFor { $null -ne (MenuByName 'Drive Pie') } 4000) 'the workspace brought the deleted menu back'
  Check (WaitFor { (AreaPanels 1) -eq $layoutBefore } 4000) 'and the arrangement of the layout'
  CaptureMain '15_workspace_loaded.png'

  Say "9. idle CPU, GPU, restart"
  Settle 2500
  $cpu = CpuPercent 6
  Say ("  idle CPU of the preview process over 6 s: {0:N2} % of one core" -f $cpu)
  Check ($cpu -lt 3.0) 'idle CPU is near zero (under 3 % of one core)'
  $smi = @(& nvidia-smi --query-compute-apps=pid,gpu_uuid --format=csv,noheader 2>$null | Where-Object { $_ -match "^$($script:procId)," })
  Say "  nvidia-smi entries of the preview: $($smi -join ' | ')"
  Check ($smi.Count -ge 1 -and -not (($smi -join ' ') -match '7b76b9f0')) 'the preview runs on the RTX 4080 and not on the RTX 3090'
  $names = (@(CMenus) | ForEach-Object { $_.name }) -join '|'
  $viewportPie = ViewportPie
  CloseApp 'run1'
  Check (-not (Get-Process -Id $script:procId -ErrorAction SilentlyContinue)) 'no preview process left after run 1'

  Say "=== run 2: restart over the same data ==="
  Launch 'run2'
  Check (HasMenu 'Custom Menus') 'the Custom Menus menu is there'
  Check (((@(CMenus) | ForEach-Object { $_.name }) -join '|') -eq $names) "the same custom menus came back ($names)"
  Check ((ViewportPie) -eq $viewportPie) 'the viewport pie came back'
  $dp = MenuByName 'Drive Panel'
  Check (WaitFor { $null -ne (WidgetRect 'widget' "cmpanel-$($dp.id)") } 3000) 'the Drive Panel panel is open again'
  $vp = WidgetRect 'widget' 'viewport'
  Click ($vp[0] + 30) ($vp[1] + $vp[3] - 30)
  KeyTap 'M'; Check (WaitFor { (Text1).tool -eq 'tool.move' } 2000) 'the key M (a hotkey assignment) survived the restart'
  CaptureMain '16_restarted.png'
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


