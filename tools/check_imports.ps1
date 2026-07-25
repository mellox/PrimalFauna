# Post-package guard against the SHIPPING-EXPORT TRAP (found 2026-07-23, Phase-1 gate):
# a game method can be declared in the SML headers and link against the stubs, yet be
# ABSENT from the shipping DLL's export table -> the mod module fails to load at boot
# with GetLastError=127 (ERROR_PROC_NOT_FOUND). Compile and cook cannot catch it.
#
# This script dumps the mod DLL's imports and verifies every symbol imported from a
# FactoryGameSteam-*.dll module actually exists in that module's export table.
# Run after every package (candidate for promotion into build_mod.ps1).
#
# w31-1: it searches THREE binary directories, not one. Until then only
# FactoryGame\Binaries was searched, so the game module's 173 imports were
# checked and the other 623 -- Engine, CoreUObject, Core, AIModule, SML -- were
# skipped behind a warning while the summary still read "0 missing". See the
# $binDirs comment below.
#
# Usage: .\check_imports.ps1 [-ModName PrimalFauna]
param(
    [string]$ModName = "PrimalFauna",
    [string]$GameDir = "C:\Program Files (x86)\Steam\steamapps\common\Satisfactory"
)

$ErrorActionPreference = "Stop"

# Locate dumpbin (VS Build Tools).
$dumpbin = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe" -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
if (-not $dumpbin) { Write-Error "dumpbin.exe not found under Visual Studio tools."; exit 2 }

$modDll = "$GameDir\FactoryGame\Mods\$ModName\Binaries\Win64\FactoryGameSteam-$ModName-Win64-Shipping.dll"
if (-not (Test-Path $modDll)) { Write-Error "Mod DLL not found: $modDll"; exit 2 }

# Where a FactoryGameSteam-* dependency can live. The game module sits under
# FactoryGame\Binaries; every ENGINE module (Engine, AnimGraphRuntime, Core,
# CoreUObject, AIModule, ...) sits under Engine\Binaries.
#
# Only the first directory was searched until w31-1, so the engine modules were
# "dependent DLL not found ... (skipped)" -- a warning that scrolled past while
# the summary line said 0 missing. That was 373 unchecked Engine imports on a
# mod whose one known load-killer is an unexported shipping symbol. It went
# unnoticed because nothing had reached for a rarely-used engine symbol before;
# a hand-built FAnimInstanceProxy reaches for a great many.
#
# SML is a mod like ours and lives under its own Mods folder, so it was skipped
# for the same reason. Its 40 imports are the ones most likely to break on an
# SML version bump, which makes them the last thing that should go unchecked.
$binDirs = @(
    "$GameDir\FactoryGame\Binaries\Win64",
    "$GameDir\Engine\Binaries\Win64",
    "$GameDir\FactoryGame\Mods\SML\Binaries\Win64"
)

# Parse the mod DLL's import sections: group imported symbol names by source DLL.
$importsRaw = & $dumpbin /nologo /imports $modDll
$bySourceDll = @{}; $currentDll = $null
foreach ($line in $importsRaw) {
    if ($line -match '^\s+(\S+\.dll)\s*$') { $currentDll = $Matches[1]; if (-not $bySourceDll.ContainsKey($currentDll)) { $bySourceDll[$currentDll] = New-Object System.Collections.Generic.List[string] } }
    elseif ($currentDll -and $line -match '^\s+[0-9A-Fa-f]+\s+(\?\S+|\w[\w@]+)\s*$') { $bySourceDll[$currentDll].Add($Matches[1]) }
}

$totalMissing = 0
$skipped = 0
foreach ($srcDll in $bySourceDll.Keys | Where-Object { $_ -like 'FactoryGameSteam-*' } | Sort-Object) {
    $srcPath = $binDirs | ForEach-Object { Join-Path $_ $srcDll } | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $srcPath) {
        # Fallback for ENGINE PLUGIN modules (Niagara, ChaosCloth, AnimationCore,
        # ...), which live under Engine\Plugins\<Category>\<Plugin>\Binaries\Win64
        # rather than in any fixed directory. Recursive and therefore slow, so it
        # only runs on a miss -- the three fixed directories above cover every
        # module the mod imports today.
        $srcPath = Get-ChildItem "$GameDir\Engine\Plugins" -Recurse -Filter $srcDll -File -EA SilentlyContinue |
                   Select-Object -First 1 -ExpandProperty FullName
    }
    if (-not $srcPath) { Write-Warning "dependent DLL not found next to game: $srcDll (NOT CHECKED)"; $skipped++; continue }
    $exports = New-Object System.Collections.Generic.HashSet[string]
    foreach ($line in (& $dumpbin /nologo /exports $srcPath)) {
        if ($line -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)') { [void]$exports.Add($Matches[1]) }
    }
    $missing = $bySourceDll[$srcDll] | Where-Object { -not $exports.Contains($_) }
    "{0}: {1} imports checked, {2} missing" -f $srcDll, $bySourceDll[$srcDll].Count, @($missing).Count
    foreach ($m in $missing) { "  MISSING EXPORT: $m"; $totalMissing++ }
}

if ($totalMissing -gt 0) {
    Write-Error "$totalMissing unresolved import(s) - the module WILL fail to load (GetLastError=127). Replace the offending call(s); see PFACreatureBase.h's ClearNativeOrphanTimer for the pattern."
    exit 1
}
if ($skipped -gt 0) {
    # A SKIP IS NOT A PASS, and it must not exit 0 (cold review D4). Saying so
    # only in the summary text would have re-created, in the exit code, exactly
    # the gap w31-1 fixed in the search path: build.ps1 and any future caller
    # gate on $LASTEXITCODE, and would have read "unchecked" as "clean".
    Write-Error "$skipped dependent DLL(s) could NOT be located, so their imports were NOT checked. An unchecked module can still 127-kill the mod at load. Add its directory to `$binDirs."
    exit 3
}
"ALL IMPORTS RESOLVED - module should load."
exit 0
