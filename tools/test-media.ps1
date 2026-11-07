<#
Runs the media example page in linweb, headless-style (it presses Enter, waits, takes a screenshot) and counts the checks.

    powershell -File tools\test-media.ps1 [-Page D:\Virtualbox\test\server\media.html] [-Exe build\Release\linweb.exe]

Prints "PASS n FAIL m"; the example page has 44 checks. Needs the test media next to the page (music\ and video\).
#>
param(
    [string]$Page = 'D:\Virtualbox\test\server\media.html',
    [string]$Exe = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\Release\linweb.exe'),
    [int]$Seconds = 32
)

$work = Join-Path $env:TEMP 'linweb-test-media'
New-Item -ItemType Directory -Force $work | Out-Null
$log = Join-Path $work 'out.txt'
Get-Process linweb -ErrorAction SilentlyContinue | Stop-Process -Force
& $Exe $Page --size=1100x560 --keys=Enter@2 --screenshot-live "--screenshot=$work\shot.bmp" "--screenshot-delay=$Seconds" *> $log
$lines = Select-String -Path $log -Pattern '^TEST' | ForEach-Object { $_.Line }
$pass = ($lines | Where-Object { $_ -like 'TEST PASS*' }).Count
$fail = ($lines | Where-Object { $_ -like 'TEST FAIL*' })
$lines | Where-Object { $_ -like 'TEST FAIL*' }
Write-Output "PASS $pass FAIL $($fail.Count)"
if ($fail.Count -gt 0 -or $pass -lt 44) { exit 1 }
