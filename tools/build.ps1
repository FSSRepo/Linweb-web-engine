<#
Builds the engine in the normal build folder (build/) and prints only what matters.

    powershell -File tools\build.ps1            builds the linweb target
    powershell -File tools\build.ps1 -Target linweb-media-test -BuildDir build-media

Several agents can work in this repository at once and the compilers lock the files they use, so a build takes a lock
first: the folder ..\.build-lock is created (atomic), the build runs, the folder is removed. Another build waits.
#>
param(
    [string]$Target = 'linweb',
    [string]$BuildDir = 'build',
    [int]$WaitSeconds = 900
)

$root = Split-Path -Parent $PSScriptRoot
$lock = Join-Path (Split-Path -Parent $root) '.build-lock'
$deadline = (Get-Date).AddSeconds($WaitSeconds)
while ($true) {
    try { New-Item -ItemType Directory -Path $lock -ErrorAction Stop | Out-Null; break } catch {
        if ((Get-Date) -gt $deadline) { Write-Output "BUILD LOCK BUSY for $WaitSeconds s: $lock"; exit 2 }
        Start-Sleep -Seconds 5
    }
}
try {
    Set-Location $root
    $out = cmake --build $BuildDir --config Release --target $Target 2>&1
    $problems = $out | Select-String -Pattern ' error |fatal error|: error C|undefined reference|LNK[0-9]+' | ForEach-Object { $_.Line.Trim() }
    if ($problems) { $problems | Select-Object -First 40; Write-Output "BUILD FAILED ($Target)"; exit 1 }
    Write-Output "BUILD OK ($Target)"
} finally {
    Remove-Item -Recurse -Force $lock -ErrorAction SilentlyContinue
}
