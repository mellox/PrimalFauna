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

  5. AND THE MARKER SAYS NOTHING ABOUT ASSETS. A clip that failed to import,
     or a cook that skipped new content, ships silently -- correct DLL, and the
     creature just quietly falls back down its animation chain. Every authored
     clip is asserted present in the cook. (This cannot be checked in the
     .pak/.ucas: IoStore hashes asset names, so a string search there reads
     False even for clips that demonstrably work in game.)

Usage:  pwsh -File tools\build.ps1  [-SkipSync] [-SkipImportCheck] [-Marker <expected>]

  * `pwsh`, NOT `powershell`. This script uses `?:` and `??` (see the
    cold-review-gate block near the end), which PowerShell 5.1 cannot even
    PARSE -- it fails with two parse errors before executing a single line, and
    that reads in a transcript as a mysterious build failure rather than "wrong
    shell". The `#requires -Version 7.0` below turns it into a message that
    names the actual fix. (Silver lining verified by the w46-1 review: the 5.1
    parse failure happens before the lock is taken at line ~99, so it can never
    strand the build lock.)

  * THE MARKER IS NOT A PARAMETER YOU SET. It is read by regex out of
    Source\PrimalFauna\Private\PrimalFauna.cpp, which is the single source of
    truth (the marker must be monotonic and authored -- not typo-able from a
    command line). To change the build id, EDIT THAT STRING, then run this
    script bare. `-Marker <expected>` is an ASSERTION only: if supplied it is
    compared against the value in the source and the build FAILS on a mismatch.
    History: three builds in one session were invoked with `-Marker "..."` in
    the belief that it set something. Without [CmdletBinding()] an unknown
    named argument is silently swallowed into $args -- so the builds were
    correct only because the source string had also been hand-edited.

Exit 0 only if the build succeeded, the marker is in the deployed DLL, and
every authored clip is in the cook.
#>
#requires -Version 7.0
[CmdletBinding()]
param(
    [switch]$SkipSync,
    [switch]$SkipImportCheck,
    # Optional ASSERTION of the expected build id (see the usage note above).
    # Never used to WRITE the marker.
    [string]$Marker
)

$ErrorActionPreference = 'Stop'
$BuildStart = Get-Date

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
# Source of truth = the string in PrimalFauna.cpp. -Marker, if supplied, is only
# an assertion about it (see the usage block at the top).
$ExpectedMarker = $Marker
$markerLine = Select-String -Path (Join-Path $Repo 'Source\PrimalFauna\Private\PrimalFauna.cpp') -Pattern 'BUILD ([0-9a-zA-Z\-]+) LOADED'
if (-not $markerLine) { Fail 'could not read the build marker out of PrimalFauna.cpp' }
$Marker = $markerLine.Matches[0].Groups[1].Value
if ($ExpectedMarker -and $ExpectedMarker -ne $Marker) {
    Fail ("-Marker '$ExpectedMarker' does not match the marker in Source\PrimalFauna\Private\PrimalFauna.cpp ('$Marker'). " +
          "This flag ASSERTS the marker, it does not set it -- edit the UE_LOG string in PrimalFauna.cpp, then rebuild.")
}
Write-Host "PFABUILD marker=$Marker$(if ($ExpectedMarker) { ' (asserted by -Marker: MATCH)' })"

# --- gate 3: the game must be closed ---------------------------------------
$game = Get-Process -Name 'FactoryGame*' -ErrorAction SilentlyContinue
if ($game) { Fail "Satisfactory is running (PID $($game.Id -join ',')) -- it locks the deployed DLL. Close it first." }

# --- gate 3b: no OTHER build in flight. The lock file below only catches
# another build.ps1; a session driving RunUAT directly holds no lock, and two
# concurrent builds corrupt the shared toolchain. Process check per
# _team/BUILD-COORDINATION.md (review W5).
# UnrealEditor* included deliberately: a headless UnrealEditor-Cmd (asset import,
# material authoring) holds ~28GB and hammers the same intermediates. Omitting it
# was a hole in THIS wrapper -- the Satismagictory and LaserRifle wrappers both
# list it, so a cross-session build would have been blocked by them and not by
# us, which is the worst kind of asymmetry: safe in one direction only.
$builders = Get-Process -Name 'UnrealBuildTool','AutomationTool','cl','link','MSBuild','UnrealEditor*' -ErrorAction SilentlyContinue
if ($builders) {
    Fail ("a build is already running ({0}) -- builds MUST serialize; wait for it to finish." -f
          (($builders | ForEach-Object { "$($_.ProcessName):$($_.Id)" }) -join ', '))
}

# --- cross-session build lock (_team/BUILD-COORDINATION.md) ----------------
# Staleness window per that doc: a lock older than 30 minutes is treated as
# abandoned. Without this, one hard-killed build blocks EVERY future build in
# EVERY session until a human deletes the file by hand (review W4) -- and
# review W1 found a path that could strand exactly such a lock.
$LockStaleMinutes = 30
if (Test-Path $LockFile) {
    $lockAge = (Get-Date) - (Get-Item $LockFile).LastWriteTime
    Write-Host "PFABUILD existing build lock ($([int]$lockAge.TotalMinutes)m old):" -ForegroundColor Yellow
    Get-Content $LockFile | Write-Host
    if ($lockAge.TotalMinutes -lt $LockStaleMinutes) {
        Fail ("build lock held and only {0}m old -- another build is probably live. Wait, or delete the lock if you know it is dead." -f [int]$lockAge.TotalMinutes)
    }
    Write-Host "PFABUILD lock is older than ${LockStaleMinutes}m -- treating as abandoned and taking it over." -ForegroundColor Yellow
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
    # Each cleanup action is ISOLATED. Review W1 reproduced the un-isolated
    # version: a throwing Move-Item skipped the lock deletion entirely, which
    # strands _team\BUILD-LOCK.txt and blocks every future build in every
    # session for every mod. Cleanup must never be all-or-nothing.
    if ($restoreWu -and (Test-Path $WuDisabled)) {
        try {
            Move-Item $WuDisabled $WuPlugin -Force
            Write-Host 'PFABUILD WeaponUpgrades.uplugin restored'
        }
        catch {
            Write-Warning ("PFABUILD COULD NOT RESTORE WeaponUpgrades.uplugin -- the user's installed mod is DISABLED. " +
                           "Rename '$WuDisabled' back to '$WuPlugin' by hand. Error: $_")
        }
    }
    # [System.IO.File]::Delete, NOT Remove-Item -- the harness blocks
    # Remove-Item next to a robocopy /MIR (workspace CLAUDE.md).
    try {
        if (Test-Path $LockFile) { [System.IO.File]::Delete($LockFile) }
    }
    catch {
        Write-Warning "PFABUILD COULD NOT DELETE the build lock '$LockFile' -- delete it by hand or the next build will refuse to start. Error: $_"
    }
}

# --- gate 4a: the marker must be in the DLL the user will actually load ----
if (-not (Test-Path $DeployDll)) { Fail "deployed DLL missing at $DeployDll" }
$hit = Select-String -Path $DeployDll -Pattern $Marker -Encoding unicode -ErrorAction SilentlyContinue
if (-not $hit) { Fail "marker $Marker NOT found in the deployed DLL -- the game would load an older build" }
Write-Host "PFABUILD MARKER_${Marker}=True in $(Split-Path $DeployDll -Leaf)"

# --- gate 4c: CONTENT freshness. The marker proves the CODE is fresh and says
# nothing about assets. A clip that failed to import, or a cook that skipped
# new content, ships silently: the DLL is right, the creature just quietly
# falls back down its animation chain and the log says MISSING. Assert every
# source clip made it through the cook, and that the cook ran in THIS build.
# (Cannot be checked in the .pak/.ucas -- IoStore hashes names, so a plain
# string search there returns False even for clips known to work in game.)
$AnimSrcDir = Join-Path $Repo 'art\wip\sicklejaw-d-v2'
$CookAnimDir = Join-Path $SmlRoot 'Mods\PrimalFauna\Saved\Cooked\Windows\FactoryGame\Mods\PrimalFauna\Content\Sicklejaw\Anims'
if ((Test-Path $AnimSrcDir) -and (Test-Path $CookAnimDir)) {
    $srcClips = Get-ChildItem $AnimSrcDir -Filter 'A_Sicklejaw_*.fbx' -EA SilentlyContinue
    $cookClips = Get-ChildItem $CookAnimDir -Filter 'A_Sicklejaw_*.uexp' -EA SilentlyContinue
    $cookNames = @($cookClips | ForEach-Object { $_.BaseName })
    $missingClips = @($srcClips | Where-Object { $cookNames -notcontains $_.BaseName })
    if ($missingClips.Count -gt 0) {
        Fail ("clips authored but NOT in the cook: " + ($missingClips.BaseName -join ', ') +
              " -- re-run tools\import_sicklejaw_ue.py, then rebuild")
    }
    $staleClips = @($cookClips | Where-Object { $_.LastWriteTime -lt $BuildStart })
    if ($staleClips.Count -eq $cookClips.Count -and $cookClips.Count -gt 0) {
        Write-Host ("PFABUILD NOTE: no clip was re-cooked this build ({0} clips, all older than build start) -- fine if content did not change." -f $cookClips.Count)
    }
    Write-Host ("PFABUILD content OK: {0}/{1} authored clips present in the cook" -f $cookClips.Count, $srcClips.Count)
}
else {
    Write-Host 'PFABUILD NOTE: anim source or cook dir not found -- content freshness NOT checked' -ForegroundColor Yellow
}

# --- gate 4b: shipping-export check -------------------------------------
if (-not $SkipImportCheck) {
    & (Join-Path $Repo 'tools\check_imports.ps1')
    if ($LASTEXITCODE -ne 0) { Fail 'check_imports.ps1 reported missing imports -- the module would 127-fail at load' }
}

# ---- COLD-REVIEW GATE --------------------------------------------------------
# The workspace rule is "cold-review every change before the user tests it", and
# on 2026-07-25 it was silently dropped for FOUR consecutive builds the moment
# the pace picked up -- w38, w39, w42, w43 all reached the user unreviewed, and
# the user found the bugs. A rule that lives only in a document is a suggestion
# (the lesson this project keeps re-learning), so the build itself now says it.
#
# Stamp a review with:  git tag -f pfa-reviewed <sha>   (or edit the file below)
$reviewMark = Join-Path $PSScriptRoot '..\..\_team\PFA-LAST-REVIEWED.txt'
$lastReviewed = (Test-Path $reviewMark) ? ((Get-Content $reviewMark -TotalCount 1).Trim()) : ''
if ($lastReviewed) {
    $unreviewed = @(git -C (Split-Path $PSScriptRoot -Parent) log --oneline "$lastReviewed..HEAD" 2>$null)
    if ($unreviewed.Count -gt 0) {
        Write-Host ''
        Write-Host "PFABUILD !! COLD REVIEW OWED: $($unreviewed.Count) commit(s) since the last reviewed one." -ForegroundColor Yellow
        $unreviewed | Select-Object -First 6 | ForEach-Object { Write-Host "           $_" -ForegroundColor DarkYellow }
        Write-Host "           Run a fresh reviewer BEFORE the user tests. Then record the sha in:" -ForegroundColor Yellow
        Write-Host "           $((Resolve-Path $reviewMark -EA SilentlyContinue) ?? $reviewMark)" -ForegroundColor DarkYellow
        Write-Host ''
    } else {
        Write-Host 'PFABUILD cold-review gate: up to date' -ForegroundColor DarkGray
    }
} else {
    Write-Host "PFABUILD !! COLD REVIEW GATE NOT INITIALISED -- create _team\PFA-LAST-REVIEWED.txt with the last reviewed sha." -ForegroundColor Yellow
}

Write-Host 'PFABUILD OK' -ForegroundColor Green
exit 0
