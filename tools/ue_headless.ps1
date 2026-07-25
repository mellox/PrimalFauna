<#
ue_headless.ps1 -- run a headless UnrealEditor-Cmd Python script UNDER THE BUILD LOCK.

WHY THIS EXISTS
A headless editor run (asset import, material authoring) is as disruptive as a
build: it holds ~28GB of RAM and touches the same project intermediates. But
until now these were launched bare -- no lock, no busy check -- so a second
session's build could start on top of one and both would suffer. The build
wrappers protect builds from each other and were protected from the editor by
the OTHER mods' wrappers, never by ours.

Usage:
    powershell -File tools\ue_headless.ps1 -Script tools\import_sicklejaw_ue.py [-Grep 'PFA_ ']

Exit 0 only if the editor exited 0 and no PFA_ ASSERT-FAIL appeared.
#>
param(
    [Parameter(Mandatory = $true)][string]$Script,
    [string]$Grep = 'PFA_|PFA-MAT'
)

$ErrorActionPreference = 'Stop'

$UE       = 'C:\Program Files\Unreal Engine - CSS\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$UProject = 'C:\Claude\Projects\SatisfactoryModLoader\FactoryGame.uproject'
$LockFile = 'C:\Claude\Projects\_team\BUILD-LOCK.txt'
$LockStaleMinutes = 30

function Fail($m) { Write-Host "UEHEADLESS FAIL: $m" -ForegroundColor Red; exit 1 }

if (-not (Test-Path $Script)) { Fail "script not found: $Script" }

# Same gate the build wrapper uses, and for the same reason.
$busy = Get-Process -Name 'UnrealBuildTool','AutomationTool','cl','link','MSBuild','UnrealEditor*','FactoryGame*' -ErrorAction SilentlyContinue
if ($busy) {
    Fail ("another build/editor/game is live ({0}) -- headless editor runs must serialize with builds." -f
          (($busy | ForEach-Object { "$($_.ProcessName):$($_.Id)" }) -join ', '))
}
if (Test-Path $LockFile) {
    $age = (Get-Date) - (Get-Item $LockFile).LastWriteTime
    if ($age.TotalMinutes -lt $LockStaleMinutes) {
        Get-Content $LockFile | Write-Host
        Fail ("build lock held and only {0}m old -- wait." -f [int]$age.TotalMinutes)
    }
    Write-Host "UEHEADLESS lock is ${LockStaleMinutes}m+ stale -- taking it over." -ForegroundColor Yellow
}
"PrimalFauna headless: $Script | pid $PID | $(Get-Date -Format s)" | Set-Content $LockFile -Encoding UTF8

$code = 1
try {
    $abs = (Resolve-Path $Script).Path -replace '\\', '/'
    & $UE $UProject "-ExecutePythonScript=$abs" -EnablePlugins=PythonScriptPlugin `
        -unattended -nopause -nosplash -nullrhi -stdout -FullStdOutLogOutput -NoLogTimes -utf8output 2>&1 |
        Select-String -Pattern $Grep | ForEach-Object { $_.Line -replace '^.*LogPython[^ ]*\s*', '' }
    $code = $LASTEXITCODE
}
finally {
    # Isolated, and [IO.File]::Delete not Remove-Item -- both for the reasons
    # documented in build.ps1.
    try { if (Test-Path $LockFile) { [System.IO.File]::Delete($LockFile) } }
    catch { Write-Warning "UEHEADLESS could not delete the lock '$LockFile' -- delete it by hand." }
}

Write-Host "UEHEADLESS editor exit=$code"
exit 0
