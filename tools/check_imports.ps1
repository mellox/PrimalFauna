# Post-package guard against the SHIPPING-EXPORT TRAP (found 2026-07-23, Phase-1 gate):
# a game method can be declared in the SML headers and link against the stubs, yet be
# ABSENT from the shipping DLL's export table -> the mod module fails to load at boot
# with GetLastError=127 (ERROR_PROC_NOT_FOUND). Compile and cook cannot catch it.
#
# This script dumps the mod DLL's imports and verifies every symbol imported from a
# FactoryGameSteam-*.dll module actually exists in that module's export table.
# Run after every package (candidate for promotion into build_mod.ps1).
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
$gameBinDir = "$GameDir\FactoryGame\Binaries\Win64"

# Parse the mod DLL's import sections: group imported symbol names by source DLL.
$importsRaw = & $dumpbin /nologo /imports $modDll
$bySourceDll = @{}; $currentDll = $null
foreach ($line in $importsRaw) {
    if ($line -match '^\s+(\S+\.dll)\s*$') { $currentDll = $Matches[1]; if (-not $bySourceDll.ContainsKey($currentDll)) { $bySourceDll[$currentDll] = New-Object System.Collections.Generic.List[string] } }
    elseif ($currentDll -and $line -match '^\s+[0-9A-Fa-f]+\s+(\?\S+|\w[\w@]+)\s*$') { $bySourceDll[$currentDll].Add($Matches[1]) }
}

$totalMissing = 0
foreach ($srcDll in $bySourceDll.Keys | Where-Object { $_ -like 'FactoryGameSteam-*' }) {
    $srcPath = Join-Path $gameBinDir $srcDll
    if (-not (Test-Path $srcPath)) { Write-Warning "dependent DLL not found next to game: $srcDll (skipped)"; continue }
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
"ALL IMPORTS RESOLVED - module should load."
exit 0
