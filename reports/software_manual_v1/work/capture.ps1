param([string]$Action='capture',[string]$Name='01_main',[int]$X=0,[int]$Y=0,[string]$Keys='')
$ErrorActionPreference='Stop'
if($Action -eq 'finalshots') {
 & $PSCommandPath -Action click -X 840 -Y 506 -Name 24_configuration_saved
 & $PSCommandPath -Action click -X 204 -Y 206 -Name 08_system
 & $PSCommandPath -Action click -X 126 -Y 206 -Name 07_ack_saved
 & $PSCommandPath -Action click -X 1390 -Y 275 -Name 25_unsaved
 & $PSCommandPath -Action click -X 1439 -Y 34 -Name 26_exit_confirmation
 exit
}
if($Action -eq 'configsteps') {
 & $PSCommandPath -Action click -X 294 -Y 172 -Name 08_system
 & $PSCommandPath -Action click -X 70 -Y 206 -Name 06_tcp
 & $PSCommandPath -Action click -X 126 -Y 206 -Name 07_ack
 & $PSCommandPath -Action click -X 1390 -Y 275 -Name 22_configuration_draft
 & $PSCommandPath -Action click -X 1410 -Y 892 -Name 23_apply_confirmation
 exit
}
if($Action -eq 'files') {
 & $PSCommandPath -Action click -X 85 -Y 234 -Name 10_calibration_center
 & $PSCommandPath -Action click -X 90 -Y 873 -Name 20_choose_points
 & $PSCommandPath -Keys 'D:\CodeSpace\projects\QtVisionApp\reports\software_manual_v1\theoretical_points_5x5.csv{ENTER}' -Name 21_points_loaded
 & $PSCommandPath -Action click -X 294 -Y 172 -Name 06_tcp
 & $PSCommandPath -Action click -X 126 -Y 206 -Name 07_ack
 & $PSCommandPath -Action click -X 450 -Y 279 -Keys '^a1500{TAB}' -Name 22_configuration_draft
 & $PSCommandPath -Action click -X 1410 -Y 892 -Name 23_apply_confirmation
 exit
}
if($Action -eq 'tour2') {
 & $PSCommandPath -Action click -X 1399 -Y 70 -Name 19_return_detail
 & $PSCommandPath -Action click -X 218 -Y 172 -Name 10_calibration_center
 & $PSCommandPath -Action click -X 182 -Y 234 -Name 11_calibration_corners
 exit
}
if($Action -eq 'protocol') {
 $client=[Net.Sockets.TcpClient]::new('127.0.0.1',19090)
 $client.ReceiveTimeout=4000
 $stream=$client.GetStream()
 $reader=[IO.StreamReader]::new($stream)
 $writer=[IO.StreamWriter]::new($stream,[Text.UTF8Encoding]::new($false))
 $writer.AutoFlush=$true
 function Send($m) { $writer.WriteLine(($m | ConvertTo-Json -Compress)) }
 function Heartbeat { Send @{version=1;type='heartbeat';sequence=1}; $reader.ReadLine() | Write-Output }
 function Locate($id,$mode) {
  Heartbeat
  Send @{version=1;type='prepareLocate';taskId=$id;mode=$mode}
  do { $line=$reader.ReadLine(); Write-Output $line; $m=$line | ConvertFrom-Json } until($m.type -eq 'locateResult' -or $m.type -eq 'prepareRejected')
  if($m.type -eq 'locateResult') { Send @{version=1;type='resultAck';taskId=$id} }
 }
 & $PSCommandPath -Action click -X 75 -Y 172 -Name 01_main
 Locate '1001' 'EDGE'
 & $PSCommandPath -Name 13_capture_error
 Heartbeat
 & $PSCommandPath -Action click -X 1300 -Y 130 -Name 14_recovered
 Locate '1002' 'EDGE'
 & $PSCommandPath -Name 15_edge_ok
 Locate '1003' 'MARK_POINT'
 & $PSCommandPath -Name 16_mark_ng
 Heartbeat
 & $PSCommandPath -Action click -X 369 -Y 172 -Name 17_diagnostics
 $client.Dispose()
 exit
}
if($Action -eq 'tour') {
 & $PSCommandPath -Action click -X 122 -Y 206 -Name 03_light
 & $PSCommandPath -Action click -X 169 -Y 206 -Name 04_algorithm
 & $PSCommandPath -Action click -X 1375 -Y 266 -Name 05_algorithm_validation
 & $PSCommandPath -Action click -X 294 -Y 172 -Name 06_tcp
 & $PSCommandPath -Action click -X 126 -Y 206 -Name 07_ack
 & $PSCommandPath -Action click -X 204 -Y 206 -Name 08_system
 & $PSCommandPath -Action click -X 369 -Y 172 -Name 09_logs
 & $PSCommandPath -Action click -X 218 -Y 172 -Name 10_calibration_center
 exit
}
Add-Type -AssemblyName System.Drawing,System.Windows.Forms
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class ManualWin {
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
 [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out RECT r);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int n);
 [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h,int x,int y,int w,int h2,bool r);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint x,uint y,uint d,UIntPtr e);
}
'@
[ManualWin]::SetProcessDPIAware() | Out-Null
$base=Split-Path $PSScriptRoot
if($Action -eq 'launch') {
 $p=Start-Process -FilePath (Join-Path (Get-Location) 'x64/Release/VisionApp.exe') -ArgumentList '--config reports/software_manual_v1/work/manual.simulation.json --backend simulation' -PassThru
 $p.Id | Set-Content (Join-Path $PSScriptRoot 'app.pid')
 Start-Sleep -Seconds 2
}
$appId=[int](Get-Content (Join-Path $PSScriptRoot 'app.pid'))
$p=Get-Process -Id $appId
$handle=$p.MainWindowHandle
if($handle -ne [IntPtr]::Zero) {
 [ManualWin]::ShowWindow($handle,9) | Out-Null
 [ManualWin]::MoveWindow($handle,20,20,1450,920,$true) | Out-Null
}
[ManualWin]::SetForegroundWindow($handle) | Out-Null
Start-Sleep -Milliseconds 200
if($Action -eq 'click') {
 [ManualWin]::SetCursorPos($X,$Y) | Out-Null
 [ManualWin]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
 [ManualWin]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
}
if($Keys) { Start-Sleep -Milliseconds 150; [System.Windows.Forms.SendKeys]::SendWait($Keys) }
Start-Sleep -Milliseconds 600
$p.Refresh()
if($p.HasExited) { Write-Output 'Manual simulation application exited normally.'; exit }
if($p.MainWindowHandle -ne [IntPtr]::Zero) { $handle=$p.MainWindowHandle }
$r=New-Object ManualWin+RECT
[ManualWin]::GetWindowRect($handle,[ref]$r) | Out-Null
$bmp=New-Object System.Drawing.Bitmap ($r.R-$r.L),($r.B-$r.T)
$g=[System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L,$r.T,0,0,$bmp.Size)
$out=Join-Path $base "screenshots/$Name.png"
$bmp.Save($out,[System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Output "$out rectangle=$($r.L),$($r.T),$($r.R),$($r.B)"
