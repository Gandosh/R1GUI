# Real-desktop stress drive of native floating windows in the preview's Editor screen.
# Launches r1gui-preview.exe on the RTX 4080 with a throwaway data folder and repeats, for -Iterations rounds:
# click a panel tab, press Ctrl+Alt+F (the float-tab chord: a native window is created inside the key handler),
# then one of four endings chosen by the round number: dock the tab back by dragging it to the main window's tab
# strip (the window is destroyed), close the tab inside the floating window with Ctrl+W, drag the window to the
# other monitor and dock it back, or minimize and restore the main window while the window exists; every fourth
# round also opens Custom Menus > Create Custom Menu (a native window) and closes it with WM_CLOSE.
# Stall detector: after every step the main window must answer a WM_NULL within 3 s (SendMessageTimeout with
# SMTO_ABORTIFHUNG) and must not be IsHungAppWindow; on a stall the script waits 5 s more, writes the stacks of
# every thread of the preview and a minidump with the stack tool (-StackTool, see tools in the slice evidence),
# kills the preview, counts the stall and starts a fresh one.
# Every mouse press and key is preceded by a check that the point or the foreground window belongs to the preview
# process (nothing is ever sent to another application). The preview is killed in a finally block.
# Helpers as in editor_drive.ps1.
# Usage: powershell -File stress_drive.ps1 -Exe <r1gui-preview.exe> -OutDir <dir> [-Iterations 30] [-StackTool <stackpid.exe>]
param([string]$Exe, [string]$OutDir, [int]$Iterations = 30, [string]$StackTool = '', [int]$Seed = 1)
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
  [DllImport("user32.dll")] public static extern bool IsHungAppWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll", SetLastError=true)] public static extern IntPtr SendMessageTimeout(IntPtr h, uint msg, IntPtr w, IntPtr l, uint flags, uint timeout, out IntPtr result);
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
function Text2() { $m = [regex]::Match(@(Lines 'text layout')[0], 'layout=(.*?) status=(.*) floating=(\d+)$'); [pscustomobject]@{ layout = $m.Groups[1].Value; status = $m.Groups[2].Value; floating = [int]$m.Groups[3].Value } }
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


# ---- stall detector ----------------------------------------------------------------------------------------
$script:stalls = 0
function Responsive([int]$ms = 3000) {
  $result = [IntPtr]::Zero
  $r = [W]::SendMessageTimeout($script:mainHwnd, 0, [IntPtr]::Zero, [IntPtr]::Zero, 2, [uint32]$ms, [ref]$result)
  return ($r -ne [IntPtr]::Zero) -and (-not [W]::IsHungAppWindow($script:mainHwnd))
}
function Stall($where) {
  # The first probe failed; a slow frame is not a stall, so give it 5 s more.
  if (WaitFor { Responsive 1000 } 5000) { Say "  (slow, recovered: $where)"; return $false }
  $script:stalls++
  Say "  STALL #$($script:stalls) at: $where (pid $($script:procId))"
  if ($StackTool) {
    $dump = Join-Path $OutDir ("stall-{0}.dmp" -f $script:stalls)
    $stacks = & $StackTool $script:procId $dump 2>&1
    Set-Content (Join-Path $OutDir ("stall-{0}.stacks.txt" -f $script:stalls)) $stacks
    Say ($stacks -join "`n")
  }
  try { $script:proc.Kill() } catch {}
  [void]$script:proc.WaitForExit(5000)
  return $true
}
# Returns $true when the preview is still responsive; otherwise it was killed and restarted.
function Probe($where) {
  if (Responsive) { return $true }
  if (-not (Stall $where)) { return $true }
  Launch ("restart{0}" -f $script:stalls)
  return $false
}
function MinimizeRestore() {
  [void][W]::ShowWindow($script:mainHwnd, 6); Start-Sleep -Milliseconds 500
  [void][W]::ShowWindow($script:mainHwnd, 9); Start-Sleep -Milliseconds 500
  [void][W]::SetForegroundWindow($script:mainHwnd)
}

$rng = New-Object System.Random $Seed
try {
  $screens = [System.Windows.Forms.Screen]::AllScreens
  Say "monitors: $($screens.Count)"
  Launch 'run'
  for ($it = 1; $it -le $Iterations; $it++) {
    Say "round $it"
    try {
      EnsureForeground
      $n = @(2, 3, 4, 5)[$rng.Next(4)]
      $dstTab = if ($n -eq 4) { 2 } else { 4 }
      $dst = TabPos $dstTab
      $t = TabPos $n
      if ($null -eq $t -or $null -eq $dst) { Say '  (tab not found, relaunching)'; try { $script:proc.Kill() } catch {}; Launch 'relaunch'; continue }
      $before = @(Floats).Count
      Click $t[0] $t[1]
      KeyTap 'F' @('Ctrl', 'Alt')
      if (-not (WaitFor { @(Floats).Count -eq ($before + 1) } 4000)) { if (-not (Probe "round $it after Ctrl+Alt+F")) { continue }; Say '  (the chord did not float the tab)' }
      if (-not (Probe "round $it after float")) { continue }
      Start-Sleep -Milliseconds 400
      switch ($it % 4) {
        0 {
          $t = TabPos $n
          if ($null -ne $t) { DragTo $t[0] $t[1] ($dst[0] + 50) $dst[1] 20 }
        }
        1 {
          $t = TabPos $n
          if ($null -ne $t) { KeyTap 'W' @('Ctrl') }
          Settle 500
        }
        2 {
          $f = @(Floats) | Select-Object -Last 1
          $other = $screens | Where-Object { -not $_.Primary } | Select-Object -First 1
          if ($null -ne $f -and $null -ne $other) {
            DragTo ($f.x + 120) ($f.y + 14) ($other.Bounds.X + 600) ($other.Bounds.Y + 300) 24
            if (-not (Probe "round $it after move to the other monitor")) { continue }
            $t = TabPos $n
            if ($null -ne $t) { DragTo $t[0] $t[1] ($dst[0] + 50) $dst[1] 24 }
          }
        }
        3 {
          MinimizeRestore
          if (-not (Probe "round $it after minimize/restore")) { continue }
          $t = TabPos $n
          if ($null -ne $t) { DragTo $t[0] $t[1] ($dst[0] + 50) $dst[1] 20 }
        }
      }
      if (-not (Probe "round $it after the window ending")) { continue }
      if ($it % 4 -eq 0) {
        EnsureForeground
        $c = WidgetCenter 'menu' 'Custom Menus'
        if ($null -ne $c) {
          Click $c[0] $c[1]; Settle 250
          for ($i = 0; $i -lt 4; $i++) { KeyTap 'Down' }
          KeyTap 'Enter'; Settle 400
          if (WaitFor { @(Floats).Count -ge 1 -and $null -ne (WidgetRect 'widget' 'creator-pie-card') } 5000) {
            if (-not (Probe "round $it creator open")) { continue }
            $f = @(Floats) | Select-Object -Last 1
            [void][W]::PostMessage([IntPtr]([Convert]::ToInt64($f.hwnd, 16)), 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
            [void](WaitFor { @(Floats).Count -eq 0 } 4000)
          }
          if (-not (Probe "round $it after the creator closed")) { continue }
        }
      }
      $left = @(Floats).Count
      if ($left -ne 0) { Say "  ($left floating windows left, docking them)"; }
    } catch {
      Say "  round $it error: $($_.Exception.Message)"
      if (-not (Probe "round $it error")) { continue }
    }
  }
  Say "done: $Iterations rounds, $($script:stalls) stalls"
} finally {
  try { if ($script:proc -and -not $script:proc.HasExited) { $script:proc.Kill() } } catch {}
}
if ($script:stalls -gt 0) { exit 1 }
exit 0
