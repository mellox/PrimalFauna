<#
build.ps1 -- the ONE way to build PrimalFauna. Committed wrapper, per
_team/playbooks/satisfactory-mod.md ("keep this in a committed repo wrapper --
don't re-create a scratchpad wrapper each session").

It exists because four separate things must be true for a build to mean
anything, and each one has silently broken a build on this machine before:

  1. TWO SOURCE TREES. This repo is the canonical source; UBT compiles
     SatisfactoryModLoader\Mods\PrimalFauna, which is a COPY, not a link.
     Building without syncing compiles yesterday's code and reports SUCCESS
     ([[nodeshuffle-two-source-trees]]). Verified stale by an hour on
     2026-07-25 -- this is not hypothetical.
  2. WEAPONUPGRADES BLOCKS THE SHARED TARGET. PackagePlugin compiles ALL
     discovered mods; WeaponUpgrades' AccessTransformer trips a UHT
     "unused friend" false-flag -> C2248 across the whole target
     ([[weaponupgrades-uht-unused-friend]]). Disabled here, restored in
     `finally` so a failed build never leaves the user's mod broken.
  3. THE GAME LOCKS THE DEPLOYED DLL. A running Satisfactory turns the copy
     step into a confusing partial failure.
  4. "BUILD SUCCESSFUL" IS NOT PROOF THE USER GETS YOUR CODE. The marker is
     verified INSIDE the deployed DLL (UTF-16), and check_imports.ps1 runs
     after, because a clean compile + cook can still 127-kill at module load
     if a new call hit a non-exported shipping symbol
     ([[sf-shipping-export-trap]]).

Usage:  powershell -File tools\build.ps1  [-SkipSync] [-SkipImportCheck]
Exit 0 only if the build succeeded AND the marker is in the deployed DLL.
#>
param(
    [switch]$SkipSync,
    [switch]$SkipImportCheck
)

$ErrorActionPreference = 'Stop'

$Repo      = 'C:\Claude\Projects\PrimalFauna'
$SmlRoot   = 'C:\Claude\Projects\SatisfactoryModLoader'
$ModsPlugin= Join-Path $SmlRoot 'Mods\PrimalFauna'
$UProject  = Join-Path $SmlRoot 'FactoryGame.uproject'
$RunUAT    = 'C:\Program Files\Unreal Engine - CSS\Engine\Build\BatchFiles\RunUAT.bat'
$GameDir   = 'C:\Program Files (x86)\Steam\steamapps\common\Satisfactory'
$DeployDll = Join-Path $GameDir 'FactoryGame\Mods\PrimalFauna\Binaries\Win64\FactoryGameSteam-PrimalFauna-Win64-Shipping.dll'
$LockFile  = 'C:\Claude\Projects\_team\BUILD-LOCK.txt'
$WuPlugin  = Join-Path $SmlRoot 'Mods\WeaponUpgrades\WeaponUpgrades.uplugin'
$WuDisabled= "$WuPlugin.disabled_for_build"

function Fail($msg) { Write-Host "PFABUILD FAIL: $msg" -ForegroundColor Red; exit 1 }

# --- the marker we must find in the deployed DLL afterwards -----------------
$markerLine = Select-String -Path (Join-Path $Repo 'Source\PrimalFauna\Private\PrimalFauna.cpp') -Pattern 'BUILD ([0-9a-zA-Z\-]+) LOADED'
if (-not $markerLine) { Fail 'could not read the build marker out of PrimalFauna.cpp' }
$Marker = $markerLine.Matches[0].Groups[1].Value
Write-Host "PFABUILD marker=$Marker"

# --- gate 3: the game must be closed ---------------------------------------
$game = Get-Process -Name 'FactoryGame*' -ErrorAction SilentlyContinue
if ($game) { Fail "Satisfactory is running (PID $($game.Id -join ',')) -- it locks the deployed DLL. Close it first." }

# --- cross-session build lock (_team/BUILD-COORDINATION.md) ----------------
if (Test-Path $LockFile) {
    Write-Host "PFABUILD another build holds the lock:" -ForegroundColor Yellow
    Get-Content $LockFile | Write-Host
    Fail 'build lock held -- wait for the other build, or delete the lock if it is stale.'
}
"PrimalFauna $Marker | pid $PID | $(Get-Date -Format s)" | Set-Content $LockFile -Encoding UTF8

$restoreWu = $false
try {
    # --- gate 1: repo -> Mods sync. /MIR so a DELETED source file is really
    # deleted in the build tree; a leftover .cpp there compiles happily and
    # duplicate-symbols or resurrects removed code.
    if (-not $SkipSync) {
        foreach ($sub in 'Source', 'Config') {
            $src = Join-Path $Repo $sub
            if (-not (Test-Path $src)) { continue }
            robocopy $src (Join-Path $ModsPlugin $sub) /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
            if ($LASTEXITCODE -ge 8) { Fail "robocopy $sub failed (exit $LASTEXITCODE)" }
        }
        Copy-Item (Join-Path $Repo 'PrimalFauna.uplugin') (Join-Path $ModsPlugin 'PrimalFauna.uplugin') -Force
        Write-Host 'PFABUILD synced Source + Config + .uplugin -> Mods tree'
    }

    # --- gate 2: keep WeaponUpgrades out of the shared target ---------------
    if (Test-Path $WuPlugin) {
        Move-Item $WuPlugin $WuDisabled -Force
        $restoreWu = $true
        Write-Host 'PFABUILD WeaponUpgrades.uplugin disabled for this build'
    }

    Write-Host 'PFABUILD running PackagePlugin...'
    & $RunUAT -ScriptsForProject="$UProject" PackagePlugin `
        -project="$UProject" `
        -clientconfig=Shipping -serverconfig=Shipping -utf8output -DLCName=PrimalFauna -build -platform=Win64 `
        -nocompileeditor -installed `
        -CopyToGameDirectory_Windows="$GameDir" 2>&1 |
        Tee-Object -Variable uatOut |
        Select-String -Pattern 'BUILD SUCCESSFUL|BUILD FAILED|ExitCode=|error C\d+|error LNK|Error:' |
        ForEach-Object { $_.Line }

    if (-not ($uatOut -match 'BUILD SUCCESSFUL')) { Fail 'PackagePlugin did not report BUILD SUCCESSFUL' }
}
finally {
    if ($restoreWu -and (Test-Path $WuDisabled)) {
        Move-Item $WuDisabled $WuPlugin -Force
        Write-Host 'PFABUILD WeaponUpgrades.uplugin restored'
    }
    # [System.IO.File]::Delete, NOT Remove-Item -- the harness blocks
    # Remove-Item next to a robocopy /MIR (workspace CLAUDE.md).
    if (Test-Path $LockFile) { [System.IO.File]::Delete($LockFile) }
}

# --- gate 4a: the marker must be in the DLL the user will actually load ----
if (-not (Test-Path $DeployDll)) { Fail "deployed DLL missing at $DeployDll" }
$hit = Select-String -Path $DeployDll -Pattern $Marker -Encoding unicode -ErrorAction SilentlyContinue
if (-not $hit) { Fail "marker $Marker NOT found in the deployed DLL -- the game would load an older build" }
Write-Host "PFABUILD MARKER_${Marker}=True in $(Split-Path $DeployDll -Leaf)"

# --- gate 4b: shipping-export check -------------------------------------
if (-not $SkipImportCheck) {
    & (Join-Path $Repo 'tools\check_imports.ps1')
    if ($LASTEXITCODE -ne 0) { Fail 'check_imports.ps1 reported missing imports -- the module would 127-fail at load' }
}

Write-Host 'PFABUILD OK' -ForegroundColor Green
exit 0
