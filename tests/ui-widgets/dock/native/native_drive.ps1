# Real-desktop drive of the native floating backend (slice 5.2).
# Launches native_harness_gpu_test.exe --interactive on the RTX 4080, then drives it with real OS input
# (SetCursorPos + mouse_event) and checks the results with EnumWindows, GetWindowRect, DWM attributes
# and PrintWindow captures: tab out of the main window to empty desktop space, move the floating window
# by its title bar, resize it, drag the tab back, tab between two floating windows, close with the
# window's X, minimize and restore the main window, drag a window to the second monitor (when there is
# one), close the main window. Every mouse press is preceded by a check that the point belongs to the
# harness (a press never lands on another application). The harness ends itself after -Seconds, and the
# script kills it in a finally block: nothing is left running.
# Usage: powershell -File native_drive.ps1 -Exe <path to native_harness_gpu_test.exe> -OutDir <dir>
param([string]$Exe, [string]$OutDir, [int]$Seconds = 150)
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
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int attr, out int value, int size);
  public static List<IntPtr> Windows(uint pid) {
    var list = new List<IntPtr>();
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid) { var sb = new StringBuilder(64); GetClassNameW(h, sb, 64); if (sb.ToString() == "R1GUI.Window") list.Add(h); } return true; }, IntPtr.Zero);
    return list;
  }
}
"@
[void][W]::SetProcessDPIAware()
New-Item -ItemType Directory -Force $OutDir | Out-Null
$log = Join-Path $OutDir 'drive.log'
Set-Content $log "native floating backend drive, $(Get-Date -Format s)"
$script:fail = 0
function Say($m) { Add-Content $log $m; Write-Host $m }
function Check($ok, $what) { if ($ok) { Say "  PASS  $what" } else { Say "  FAIL  $what"; $script:fail++ } }

$status = Join-Path $OutDir 'status.txt'
Remove-Item $status -ErrorAction SilentlyContinue
$env:R1UI_GPU = 'RTX 4080'
$env:PATH = 'C:\VulkanSDK\1.4.363.0\Bin;' + $env:PATH
$proc = Start-Process -FilePath $Exe -ArgumentList '--interactive', '--seconds', $Seconds, '--status', $status -PassThru -RedirectStandardOutput (Join-Path $OutDir 'harness.out') -RedirectStandardError (Join-Path $OutDir 'harness.err')
$null = $proc.Handle
$procId = [uint32]$proc.Id

function Status() {
  for ($i = 0; $i -lt 40; $i++) {
    try { $t = Get-Content $status -Raw -ErrorAction Stop; if ($t) { return $t } } catch {}
    Start-Sleep -Milliseconds 50
  }
  throw 'no status file'
}
function Lines($prefix) { (Status) -split "`r?`n" | Where-Object { $_ -like "$prefix*" } }
function Tab($n) { $m = [regex]::Match((Lines 'tabs')[0], " $n=(-?\d+),(-?\d+)"); if (-not $m.Success) { return $null }; return @([int]$m.Groups[1].Value, [int]$m.Groups[2].Value) }
function FloatRects() { @(Lines 'float' | ForEach-Object { $m = [regex]::Match($_, 'hwnd=(\S+) rect=(-?\d+),(-?\d+),(\d+),(\d+) content=\S+ dpi=(\S+) visible=(\d) monitor=(\S*)'); [pscustomobject]@{ hwnd = $m.Groups[1].Value; x = [int]$m.Groups[2].Value; y = [int]$m.Groups[3].Value; w = [int]$m.Groups[4].Value; h = [int]$m.Groups[5].Value; dpi = $m.Groups[6].Value; visible = $m.Groups[7].Value; monitor = $m.Groups[8].Value } }) }
function MainRect() { $m = [regex]::Match((Lines 'main')[0], 'hwnd=(\S+) rect=(-?\d+),(-?\d+),(\d+),(\d+)'); [pscustomobject]@{ x = [int]$m.Groups[2].Value; y = [int]$m.Groups[3].Value; w = [int]$m.Groups[4].Value; h = [int]$m.Groups[5].Value } }
function HasArea($pattern) { return (@(Lines 'area' | Where-Object { $_ -match $pattern }).Count -ge 1) }
function ModelFollows() {
  $c = [regex]::Match((@(Lines 'float'))[0], 'content=(\S+)').Groups[1].Value
  foreach ($a in (Lines 'area')) { if ($a -match 'main=0' -and $a -match ('rect=' + [regex]::Escape($c) + '$')) { return $true } }
  return $false
}
function Settle($ms = 400) { Start-Sleep -Milliseconds $ms }
function Ours($x, $y) {
  $p = New-Object POINT; $p.X = $x; $p.Y = $y
  $h = [W]::WindowFromPoint($p); if ($h -eq [IntPtr]::Zero) { return $false }
  $root = [W]::GetAncestor($h, 2)
  $pid2 = 0; [void][W]::GetWindowThreadProcessId($root, [ref]$pid2)
  return ($pid2 -eq $procId)
}
function Move($x, $y) { [void][W]::SetCursorPos([int]$x, [int]$y); Start-Sleep -Milliseconds 15 }
function Press($x, $y) {
  Move $x $y; Start-Sleep -Milliseconds 60
  if (-not (Ours $x $y)) { throw "refusing to press at $x,$y: that point is not one of the harness windows" }
  [W]::mouse_event(0x2, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 40
}
function Release() { [W]::mouse_event(0x4, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 80 }
function DragTo($x0, $y0, $x1, $y1, [int]$steps = 16) {
  Press $x0 $y0
  for ($i = 1; $i -le $steps; $i++) { Move ($x0 + ($x1 - $x0) * $i / $steps) ($y0 + ($y1 - $y0) * $i / $steps); Start-Sleep -Milliseconds 18 }
  Start-Sleep -Milliseconds 120
  Release
  Settle 500
}
function Describe($h) {
  $hh = [IntPtr]([Convert]::ToInt64($h, 16)); $r = New-Object RECT; [void][W]::GetWindowRect($hh, [ref]$r)
  $ex = [int64][W]::GetWindowLongPtr($hh, -20); $style = [int64][W]::GetWindowLongPtr($hh, -16)
  $owner = [W]::GetWindow($hh, 4); $corner = 0; [void][W]::DwmGetWindowAttribute($hh, 33, [ref]$corner, 4)
  [pscustomobject]@{ hwnd = $hh; rect = "$($r.Left),$($r.Top) $($r.Right - $r.Left)x$($r.Bottom - $r.Top)"; toolWindow = (($ex -band 0x80) -ne 0); appWindow = (($ex -band 0x40000) -ne 0); owner = $owner; corner = $corner; visible = [W]::IsWindowVisible($hh); style = ('0x{0:X}' -f $style) }
}
function Capture($hwnd, $name) {
  $r = New-Object RECT; [void][W]::GetWindowRect($hwnd, [ref]$r); $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
  $bmp = New-Object System.Drawing.Bitmap $w, $h; $g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc()
  $ok = [W]::PrintWindow($hwnd, $dc, 2); $g.ReleaseHdc($dc)
  $bmp.Save((Join-Path $OutDir $name), [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose()
  Say "  saved $name ($w x $h) printwindow=$ok"
}

try {
  Settle 1500
  $mainHwnd = [IntPtr]([Convert]::ToInt64(([regex]::Match((Lines 'main')[0], 'hwnd=(\S+)').Groups[1].Value), 16))
  $monitors = [W]::GetSystemMetrics(80)
  $sw = [W]::GetSystemMetrics(0); $sh = [W]::GetSystemMetrics(1)
  Say "monitors (SM_CMONITORS): $monitors"
  Say "harness monitors: $((Get-Content (Join-Path $OutDir 'harness.out') | Where-Object { $_ -like '*monitor*' }) -join ' | ')"
  $mr = MainRect; Say "main window rect: $($mr.x),$($mr.y) $($mr.w)x$($mr.h)"
  Say "1. baseline"
  $ours = [W]::Windows($procId); Check ($ours.Count -eq 1) "one visible top-level window of the harness ($($ours.Count))"
  Check ((@(Lines 'float')).Count -eq 0) 'no floating window yet'
  Capture $mainHwnd '01_main.png'

  Say "2. drag a tab out of the main window to empty desktop space"
  $t2 = Tab 2; Say "  tab 2 at $($t2 -join ',')"
  $drop = @([int]($sw * 0.55), [int]($sh * 0.30))
  DragTo $t2[0] $t2[1] $drop[0] $drop[1]
  $f = FloatRects
  Check ($f.Count -eq 1) "a floating window exists in the backend ($($f.Count))"
  $ours = [W]::Windows($procId); Check ($ours.Count -eq 2) "two visible top-level windows now ($($ours.Count))"
  if ($f.Count -ge 1) {
    $d = Describe $f[0].hwnd
    Say "  floating window: rect $($d.rect) style $($d.style) toolWindow=$($d.toolWindow) appWindow=$($d.appWindow) owner=$($d.owner) corner=$($d.corner) visible=$($d.visible)"
    Check $d.toolWindow 'tool-window style (no taskbar button)'
    Check (-not $d.appWindow) 'no WS_EX_APPWINDOW'
    Check ($d.owner -eq $mainHwnd) 'owned by the main window'
    Check ($d.corner -eq 2) 'DWM corner preference is DWMWCP_ROUND (2)'
    Check ([Math]::Abs($f[0].x - ($drop[0] - 80)) -lt 500 -and $f[0].y -gt 100) 'placed near where the ghost was dropped'
    Capture ([IntPtr]([Convert]::ToInt64($f[0].hwnd, 16))) '02_floating.png'
  }
  Check (HasArea 'main=0 panels=2,') 'the model has Panel 2 in a floating area'

  Say "3. move the floating window by its title bar (real OS move loop)"
  $f = FloatRects; $before = $f[0]
  DragTo ($before.x + 100) ($before.y + 14) ($before.x + 100 + 160) ($before.y + 14 + 90) 20
  $f = FloatRects; $after = $f[0]
  Say "  rect before $($before.x),$($before.y) after $($after.x),$($after.y)"
  Check ([Math]::Abs(($after.x - $before.x) - 160) -le 3 -and [Math]::Abs(($after.y - $before.y) - 90) -le 3) 'the window followed the pointer (title bar drag)'
  Check (ModelFollows) 'the model follows (the area rectangle equals the window content rectangle)'

  Say "4. resize by the bottom-right corner"
  $f = FloatRects; $before = $f[0]
  DragTo ($before.x + $before.w - 3) ($before.y + $before.h - 3) ($before.x + $before.w - 3 + 120) ($before.y + $before.h - 3 + 70) 14
  $f = FloatRects; $after = $f[0]
  Say "  size before $($before.w)x$($before.h) after $($after.w)x$($after.h)"
  Check ($after.w -ge $before.w + 100 -and $after.h -ge $before.h + 55) 'the window grew with the pointer'
  Capture ([IntPtr]([Convert]::ToInt64($after.hwnd, 16))) '03_resized.png'

  Say "5. drag the tab back into a main-window region"
  $t2 = Tab 2; $t4 = Tab 4; Say "  tab 2 at $($t2 -join ','), target tab 4 at $($t4 -join ',')"
  DragTo $t2[0] $t2[1] ($t4[0] + 70) $t4[1] 24
  Check ((FloatRects).Count -eq 0) 'the floating window is gone from the backend'
  Check (([W]::Windows($procId)).Count -eq 1) 'only the main window is left on the desktop'
  Check (HasArea 'main=1 panels=.*2,') 'Panel 2 is docked in the main area again'

  Say "6. tab from one floating window to another"
  $t2 = Tab 2; DragTo $t2[0] $t2[1] ([int]($sw * 0.45)) ([int]($sh * 0.28))
  $t5 = Tab 5; DragTo $t5[0] $t5[1] ([int]($sw * 0.68)) ([int]($sh * 0.50))
  $f = FloatRects; Check ($f.Count -eq 2) "two floating windows ($($f.Count))"
  Check (([W]::Windows($procId)).Count -eq 3) 'three top-level windows'
  $t2 = Tab 2; $t5 = Tab 5; Say "  tab 2 at $($t2 -join ','), tab 5 at $($t5 -join ',')"
  DragTo $t5[0] $t5[1] ($t2[0] + 60) $t2[1] 24
  $f = FloatRects
  Check ($f.Count -eq 1) "the emptied window was destroyed ($($f.Count) left)"
  Check (HasArea 'main=0 panels=(2,5|5,2),') 'Panels 2 and 5 share one floating window'
  Capture ([IntPtr]([Convert]::ToInt64($f[0].hwnd, 16))) '04_two_tabs.png'

  Say "7. close the floating window with its X button"
  $f = FloatRects; $w = $f[0]
  Move ($w.x + $w.w - 20) ($w.y + 17); Start-Sleep -Milliseconds 120
  Press ($w.x + $w.w - 20) ($w.y + 17); Release; Settle 600
  Check ((FloatRects).Count -eq 0) 'the window closed'
  Check (([W]::Windows($procId)).Count -eq 1) 'only the main window remains'
  Check (-not (HasArea 'main=0 panels=.*[25],')) 'its panels were closed into the closed-panel memory'

  Say "8. minimize and restore the main window with a floating window open"
  $t3 = Tab 3; DragTo $t3[0] $t3[1] ([int]($sw * 0.50)) ([int]($sh * 0.35))
  $f = FloatRects; Check ($f.Count -eq 1) 'a floating window is open'
  [void][W]::ShowWindow($mainHwnd, 6); Settle 700
  Check ([W]::IsIconic($mainHwnd)) 'the main window is minimized'
  Check (-not [W]::IsWindowVisible([IntPtr]([Convert]::ToInt64($f[0].hwnd, 16)))) 'the floating window went with it'
  Check (([W]::Windows($procId)).Count -eq 0) 'no visible window of the harness while minimized'
  [void][W]::ShowWindow($mainHwnd, 9); Settle 900
  Check (-not [W]::IsIconic($mainHwnd) -and [W]::IsWindowVisible([IntPtr]([Convert]::ToInt64($f[0].hwnd, 16)))) 'both came back'

  Say "9. monitors and DPI"
  $f = FloatRects; $w = $f[0]
  Say "  floating window now: monitor=$($w.monitor) dpi=$($w.dpi) rect $($w.x),$($w.y) $($w.w)x$($w.h)"
  if ($monitors -gt 1) {
    # Drag the title bar down to the second monitor (below the primary on this machine's layout).
    $other = [System.Windows.Forms.Screen]::AllScreens | Where-Object { -not $_.Primary } | Select-Object -First 1
    $target = @(($other.WorkingArea.X + [int]($other.WorkingArea.Width * 0.5)), ($other.WorkingArea.Y + [int]($other.WorkingArea.Height * 0.4)))
    DragTo ($w.x + 100) ($w.y + 14) $target[0] $target[1] 30
    $f = FloatRects; $w2 = $f[0]
    Say "  after the drag: monitor=$($w2.monitor) dpi=$($w2.dpi) rect $($w2.x),$($w2.y) $($w2.w)x$($w2.h)"
    Check ($w2.monitor -ne $w.monitor) 'the window is on another monitor (monitorAt follows)'
    Check ($w2.w -eq $w.w -or $w2.dpi -ne $w.dpi) 'logical size kept across monitors'
    Capture ([IntPtr]([Convert]::ToInt64($w2.hwnd, 16))) '05_second_monitor.png'
  }

  Say "10. close the main window"
  [void][W]::PostMessage($mainHwnd, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
  if (-not $proc.WaitForExit(8000)) { Say '  harness did not exit on WM_CLOSE'; $script:fail++ } else { Say "  harness exited with code $($proc.ExitCode)" }
  Check ($proc.HasExited) 'no harness process left'
}
catch { Say "ERROR: $($_.Exception.Message)"; $script:fail++ }
finally {
  if (-not $proc.HasExited) { Say 'killing the harness'; $proc.Kill() }
  Say "harness output:"
  Get-Content (Join-Path $OutDir 'harness.out') | ForEach-Object { Say "  $_" }
  $err = Get-Content (Join-Path $OutDir 'harness.err') -ErrorAction SilentlyContinue | Where-Object { $_ -notmatch 'r1ui.render' }
  if ($err) { Say "harness stderr:"; $err | ForEach-Object { Say "  $_" } }
  Say "FAILURES: $script:fail"
}
exit $script:fail
