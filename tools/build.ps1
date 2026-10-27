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

Usage:  pwsh -File tools\build.ps1  [-SkipSync] [-SkipImportCheck] [-Marker <expected>] [-BuildEditor]

  * -BuildEditor ALSO compiles the FactoryEditor target (every mod's
    UnrealEditor-<Mod>.dll) before PackagePlugin. PackagePlugin runs with
    -nocompileeditor, and its cook LOADS whatever UnrealEditor-PrimalFauna.dll
    already sits in the Mods tree: the w59-1..w59-3 cook logs all print the
    w58-8 marker while shipping w59 code (_team\PFA-population-w59-3\
    build-1-Log.txt:4699). Pass it whenever anything the editor or cook executes
    or serializes changed: StartupModule, CDO/constructor defaults, any
    UCLASS/USTRUCT/UPROPERTY. It builds the SHARED FactoryEditor target, so it
    recompiles every enabled mod's editor module from its Mods-tree copy; another
    mod's broken source fails this build too.

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
    [string]$Marker,
    # Compile the FactoryEditor target first (see the usage note above).
    [switch]$BuildEditor
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

# A diagnostic must not call AFGCreatureSpawner::IsReadyToSpawn: shipping
# disassembly shows that nominal query can populate slots, trace nearby bases,
# and advance native timing. Anchor on the exact function so the check cannot
# pass vacuously if the diagnostic is renamed or removed.
$nestSpawnerCpp = Join-Path $Repo 'Source\PrimalFauna\Private\PFANestSpawner.cpp'
$nestSource = Get-Content -LiteralPath $nestSpawnerCpp -Raw
$diagAnchor = 'void APFANestSpawner::LogSpawnDecisionDiagnostics'
$diagStart = $nestSource.IndexOf($diagAnchor)
if ($diagStart -lt 0) {
    Fail 'LogSpawnDecisionDiagnostics body not found; mutation gate is vacuous.'
}
$diagBody = $nestSource.Substring($diagStart)
$mutatingNeedle = 'IsReady' + 'ToSpawn('
if ($diagBody.Contains($mutatingNeedle)) {
    Fail 'Diagnostics must not call mutating AFGCreatureSpawner::IsReadyToSpawn.'
}
Write-Host 'PFABUILD nest diagnostics mutation gate OK'

# --- gate 2a2: pin the w58-3 nest force-drive fix -----------------------------
# Three separate one-line reverts would each restore the load-time spawn-request
# flood (17 nests, ~765 native requests in one frame, zero returns, measured
# 2026-08-18). None is reachable by the self-tests: they report NO-COVERAGE on a
# world with no nests, and the flood needs a loaded save with many nests plus a
# spawn multiplier to reproduce at all.
#
# Every needle is built by CONCATENATION so this script's own text -- including
# these comments and the failure messages -- can neither satisfy nor trip a check
# if the script is ever itself scanned.
#
# Bodies are BOUNDED at the next member definition. The gate above deliberately
# reads to end-of-file; these do not, because a later function legitimately calls
# the very symbol they forbid.
function Get-PfaMemberBody {
    param([string]$Source, [string]$Anchor, [string]$NextDefinitionMarker)
    $start = $Source.IndexOf($Anchor)
    if ($start -lt 0) { return $null }
    $after = $start + $Anchor.Length
    $end = $Source.IndexOf($NextDefinitionMarker, $after)
    if ($end -lt 0) { $end = $Source.Length }
    return $Source.Substring($start, $end - $start)
}

$memberMarker = "`nvoid APFANestSpawner" + '::'
$ensureAnchor = 'void APFANestSpawner::' + 'EnsureRuntimeConfigurationAfterAdoption'
$ensureBody = Get-PfaMemberBody -Source $nestSource -Anchor $ensureAnchor -NextDefinitionMarker $memberMarker
if (-not $ensureBody) { Fail 'EnsureRuntimeConfigurationAfterAdoption body not found; w58-3 force-drive gate is vacuous.' }
$spawnNeedle = 'SpawnCreatures' + '();'
if ($ensureBody.Contains($spawnNeedle)) {
    Fail ('w58-3: EnsureRuntimeConfigurationAfterAdoption must NOT call the native spawn path. ' +
          'That unconditional call had no player-distance test and force-drove every restored nest at world load.')
}

# Bounded by the NEXT member definition, which is LogSpawnDecisionDiagnostics.
# Without that bound the body would run to end-of-file and every positive check
# below could be satisfied by a different function's text.
$driveAnchor = 'FPFANestDriveResult APFANestSpawner::' + 'TryDriveSpawn'
$driveBody = Get-PfaMemberBody -Source $nestSource -Anchor $driveAnchor -NextDefinitionMarker $memberMarker
if (-not $driveBody) { Fail 'TryDriveSpawn body not found; w58-3 distance/budget gates are vacuous.' }
if (-not $driveBody.Contains($spawnNeedle)) {
    Fail 'w58-3: TryDriveSpawn no longer drives the native spawn path -- nests would never activate.'
}
$rangeNeedle = 'EPFANestDriveDecision::' + 'SkipPlayerOutOfRange'
if (-not $driveBody.Contains($rangeNeedle)) {
    Fail 'w58-3: TryDriveSpawn lost its player-distance gate -- the load-time spawn-request flood returns.'
}
$spawnDistanceNeedle = 'GetSpawnDistance' + '()'
if (-not $driveBody.Contains($spawnDistanceNeedle)) {
    Fail ("w58-3: TryDriveSpawn must gate on the spawner's OWN " + $spawnDistanceNeedle + ', not an invented constant.')
}
$budgetNeedle = 'RemainingSlotBudget' + ' <= 0'
if (-not $driveBody.Contains($budgetNeedle)) {
    Fail 'w58-3: TryDriveSpawn lost its per-pass request budget check.'
}

# The re-drive is what makes a gated nest recoverable instead of dead-until-reload,
# so the spawn half must NOT be behind the one-shot configuration guard.
$subsystemCpp = Join-Path $Repo 'Source\PrimalFauna\Private\PFASubsystem.cpp'
$subsystemSource = Get-Content -LiteralPath $subsystemCpp -Raw
$reconcileAnchor = 'void APFASubsystem::' + 'ReconcileNests'
$reconcileBody = Get-PfaMemberBody -Source $subsystemSource -Anchor $reconcileAnchor `
                                   -NextDefinitionMarker ("`nbool APFASubsystem" + '::')
if (-not $reconcileBody) { Fail 'ReconcileNests body not found; w58-3 re-drive gate is vacuous.' }
$redriveNeedle = 'TryDriveSpawn' + '('
if (-not $reconcileBody.Contains($redriveNeedle)) {
    Fail 'w58-3: ReconcileNests no longer re-drives nests each pass -- a stalled nest would stay dead until reload.'
}
# ANCHORED ON THE INITIALISATION SITE, not on a bare token. A bare-token pin here
# was measured (2026-08-18) to pass against a mutant that hard-coded the running
# budget to 100000, because the SAME token still appeared in the census log call
# a few lines later -- the sibling-occurrence hole. Pin the assignment itself.
$budgetConstNeedle = 'DriveSlotBudgetLeft = ' + 'NestDriveSlotBudgetPerPass;'
if (-not $reconcileBody.Contains($budgetConstNeedle)) {
    Fail ('w58-3: the per-pass slot budget in ReconcileNests is no longer initialised from ' +
          'NestDriveSlotBudget' + 'PerPass -- one pass could re-create the request flood.')
}
# Both adopt branches must drive. An asymmetry here is this workspace's most
# repeated defect class: a rule applied to one side of a relationship only.
$driveCallCount = ([regex]::Matches($reconcileBody, [regex]::Escape($redriveNeedle))).Count
if ($driveCallCount -lt 2) {
    Fail ("w58-3: only $driveCallCount of the 2 adopt branches in ReconcileNests drives the spawner " +
          '(id-adopt and adopt-by-name must behave identically).')
}
Write-Host 'PFABUILD w58-3 nest drive gate OK (force-drive removed; distance + budget + re-drive pinned on both adopt branches)'

# --- gate 2b: pin the w58-1 corpse fix. A one-line revert of the un-latch, or a
# legend that starts printing field-and-value pairs, must fail the BUILD -- the
# self-tests cannot catch either (they report NO-COVERAGE on a corpse-free world).
# Needles are concatenated so this script's own text can neither satisfy nor trip
# the check if it is ever scanned.
$sicklejawCpp = Join-Path $Repo 'Source\PrimalFauna\Private\Species\PFASicklejaw.cpp'
$sicklejawSource = Get-Content -LiteralPath $sicklejawCpp -Raw
$diedAnchor = 'void APFASicklejaw::' + 'Died(AActor* DiedActor)'
$diedStart = $sicklejawSource.IndexOf($diedAnchor)
if ($diedStart -lt 0) { Fail 'APFASicklejaw::Died body not found; corpse-fix gate is vacuous.' }
$diedBody = $sicklejawSource.Substring($diedStart)
$superDiedIdx = $diedBody.IndexOf('Super::' + 'Died(DiedActor)')
$unlatchIdx   = $diedBody.IndexOf('mIsRagdolled' + ' = false;')
if ($superDiedIdx -lt 0) { Fail 'APFASicklejaw::Died no longer calls the native death path.' }
if ($unlatchIdx -lt 0) {
    Fail 'w58-1 ragdoll un-latch missing from APFASicklejaw::Died (237k SyncRagdoll warnings return).'
}
if ($unlatchIdx -lt $superDiedIdx) {
    Fail 'w58-1 ragdoll un-latch must run AFTER the native death path, not before it.'
}
$censusCpp = Join-Path $Repo 'Source\PrimalFauna\Private\PFACorpseCensus.cpp'
$censusSource = Get-Content -LiteralPath $censusCpp -Raw
$legendAnchor = 'CorpseCensus ' + 'legend:'
$legendStart = $censusSource.IndexOf($legendAnchor)
if ($legendStart -lt 0) { Fail 'Corpse census legend not found; legend gate is vacuous.' }
$legendEnd = $censusSource.IndexOf(');', $legendStart)
$legendText = $censusSource.Substring($legendStart, $legendEnd - $legendStart)
if ($legendText.Contains([char]61)) {
    Fail 'Corpse census legend contains an equals sign -- it would collide with the greps the log exists for.'
}
Write-Host 'PFABUILD corpse fix gate OK (un-latch pinned after Super::Died; census legend clean)'

# --- gate 2c: pin the w58-5 dead-target retention fix -------------------------
# THIS GATE LOADS EVERY FILE THE w58-5 PACKET TOUCHED. A green mutation suite
# says nothing about a file it never reads: on 2026-08-11 a one-line revert of a
# shipped fix passed 15/15 because the lint never opened that file. Extending the
# lint to LOAD and PIN a new file is part of the fix, not a follow-up.
#
# Every pin is ANCHORED ON ITS ENCLOSING FUNCTION, never on a bare trailing
# fragment: a bare fragment is satisfiable by a SIBLING occurrence, which is how
# two w58-3 mutants "passed" against the wrong site until they were re-anchored.
#
# Every needle is built by CONCATENATION so this script's own text -- comments
# and failure messages included -- can neither satisfy nor trip a check.
$perceptionCpp = Join-Path $Repo 'Source\PrimalFauna\Private\PFACreatureControllerPerception.cpp'
$perceptionSource = Get-Content -LiteralPath $perceptionCpp -Raw
$controllerMemberMarker = "`nvoid APFACreatureController" + '::'

# --- A, half 1: the drop itself. Anchored on TickDeadTargetDrop's own body.
$dropAnchor = 'void APFACreatureController::' + 'TickDeadTargetDrop'
$dropBody = Get-PfaMemberBody -Source $perceptionSource -Anchor $dropAnchor `
                              -NextDefinitionMarker $controllerMemberMarker
if (-not $dropBody) { Fail 'w58-5: TickDeadTargetDrop body not found; the dead-target drop gate is vacuous.' }
$notFightableNeedle = 'IsConfirmed' + 'NotFightable('
if (-not $dropBody.Contains($notFightableNeedle)) {
    Fail 'w58-5: TickDeadTargetDrop no longer grades its target liveness -- a corpse would be held forever again.'
}
# w58-6: the bare needle above is now satisfiable by the H1 remaining-live-entry
# loop, which grades OTHER entries. Pin the CURRENT-target grade on its own site
# (the only one that passes the graded value as `Liveness`), so removing the test
# this whole path exists for cannot be masked by a sibling occurrence.
$currentTargetGradeNeedle = 'IsConfirmed' + 'NotFightable(Liveness)'
if (-not $dropBody.Contains($currentTargetGradeNeedle)) {
    Fail ('w58-5: TickDeadTargetDrop no longer grades the CURRENT target -- the remaining-live-entry ' +
          'loop grades other entries and cannot stand in for it.')
}
$diedResetNeedle = 'ResetTargetingState(TEXT("' + 'target died"))'
if (-not $dropBody.Contains($diedResetNeedle)) {
    Fail 'w58-5: TickDeadTargetDrop no longer clears targeting when the target grades dead.'
}
# w58-7 (r2 review F1 / R-F1): the selective branch must BIND the next target
# EXPLICITLY, and must keep the change-guarded RE-SEAT that ForceAggro was forced
# to add -- w57-5/w57-8 measured that TriggerStateUpdate does not bind
# mCurrentTarget. Deleting either call returns this path to relying on native
# re-selection, the mechanism this repository has already measured as absent.
# This is the pin for the review's new mutant (M-d).
$bindNeedle = 'SetCurrent' + 'Target(NextTarget);'
$bindCount = ([regex]::Matches($dropBody, [regex]::Escape($bindNeedle))).Count
if ($bindCount -lt 2) {
    Fail ("w58-7 F1: the dead-target release binds the next target $bindCount time(s) -- it must set it " +
          'and then re-seat it under a change guard, or the creature is left in the fight state with no ' +
          'target bound, which is the symptom this whole packet exists to remove.')
}
$readbackNeedle = 'bound' + 'Readback=%s'
if (-not $dropBody.Contains($readbackNeedle)) {
    Fail ('w58-7 F1: the dead-target release no longer prints the bind readback -- the postcondition ' +
          'this fix turns on would go back to being asserted instead of measured.')
}
# w58-8 (r3 review F-9): two holes the round-3 gate left open in this same body.
$deadZeroNeedle = 'ModifyAggroFor' + 'Target(CurrentTarget,'
if (-not $dropBody.Contains($deadZeroNeedle)) {
    Fail ('w58-8 F-2: the dead-target release no longer zeroes the dead entry''s own aggro -- the ' +
          'corpse would stay a native re-selection candidate while the release line still reports success.')
}
$reseatGuardNeedle = 'GetCurrentTarget() != ' + 'NextTarget'
if (-not $dropBody.Contains($reseatGuardNeedle)) {
    Fail ('w58-7 F1: the second bind is no longer change-guarded -- an unconditional double-set ' +
          'satisfies the occurrence count while proving nothing about the postcondition.')
}
$roamKickNeedle = 'ForceRoamAfter' + 'Disengage('
if (-not $dropBody.Contains($roamKickNeedle)) {
    Fail ('w58-5: TickDeadTargetDrop does not kick the state machine after the reset. ' +
          'w48-1 measured that a reset alone leaves the native machine parked in CS_Alert.')
}
# w58-6 (review H1): the reset above is a TOTAL wipe, so it may only run when
# nothing LIVE is left on the ledger. Without this the creature that kills one
# target forgets every other one it was fighting.
$liveKeepNeedle = 'LiveAggroEntries' + 'Remaining > 0'
if (-not $dropBody.Contains($liveKeepNeedle)) {
    Fail ('w58-5 H1: TickDeadTargetDrop resets targeting without checking for remaining LIVE aggro ' +
          'entries -- a creature that kills one target would forget every other live one.')
}
$liveKeepIdx  = $dropBody.IndexOf($liveKeepNeedle)
$diedResetIdx = $dropBody.IndexOf($diedResetNeedle)
$guardReturnIdx = $dropBody.IndexOf('return;', $liveKeepIdx)
if ($guardReturnIdx -lt 0 -or $guardReturnIdx -gt $diedResetIdx) {
    Fail ('w58-6 H1: the remaining-live-entry branch in TickDeadTargetDrop does not return before ' +
          'the total wipe -- both branches would run and the fight would be forgotten anyway.')
}
# w58-6 (review H2): the kill-switch is the A/B control for this whole fix. It
# must gate the BEHAVIOUR only -- if it returns before the hold is stamped, the
# control arm reports heldSecs of minus one and can never report a stall.
$killSwitchNeedle = 'CVarPFADropDead' + 'Targets.GetValueOnGameThread() == 0'
$holdStampNeedle  = 'mEngagementObservation.HeldTarget' + 'SinceSeconds = NowSeconds;'
# w58-7 (r2 review, generalisable countermeasure): an ordering pin expressed as a
# comparison of two IndexOf results must be preceded by a Contains pin on BOTH
# needles -- a needle that is GONE returns minus one and silently turns the
# assertion into a tautology. This one is the kill-switch side; the review's R-F4
# below is the hold-stamp side.
if (-not $dropBody.Contains($killSwitchNeedle)) {
    Fail ('w58-5 H2: TickDeadTargetDrop no longer reads its kill-switch -- the A/B control that says ' +
          'whether this path is doing the work would be gone, and the ordering check below would ' +
          'pass vacuously against a minus-one index.')
}
if (-not $dropBody.Contains($holdStampNeedle)) {
    Fail ('w58-6 H2: TickDeadTargetDrop no longer stamps the engagement hold -- the ordering check ' +
          'below would pass vacuously against a minus-one index, and the A/B control arm would ' +
          'have no instrument at all.')
}
if ($dropBody.IndexOf($killSwitchNeedle) -lt $dropBody.IndexOf($holdStampNeedle)) {
    Fail ('w58-5 H2: the drop kill-switch returns before the engagement hold is stamped -- ' +
          'the A/B control arm would report heldSecs of minus one and could never report a stall.')
}

# --- A, half 2: THE PACK-WIDE VETO. Fixing only the self-drop leaves a corpse
# vetoing every other pack member's disengage, which is the half that produced
# the reported symptom. Anchored inside TickAggroRangeDecay so a sibling
# occurrence in the drop function above cannot satisfy it.
$decayAnchor = 'void APFACreatureController::' + 'TickAggroRangeDecay'
$decayBody = Get-PfaMemberBody -Source $perceptionSource -Anchor $decayAnchor `
                               -NextDefinitionMarker $controllerMemberMarker
if (-not $decayBody) { Fail 'w58-5: TickAggroRangeDecay body not found; the veto-liveness gate is vacuous.' }
if (-not $decayBody.Contains($notFightableNeedle)) {
    Fail ('w58-5: TickAggroRangeDecay lost its per-entry liveness test -- a dead aggro entry would set ' +
          'bAnyAggroRemains again and veto the disengage for every other pack member.')
}
$deadTallyNeedle = '++Dead' + 'AggroEntries;'
if (-not $decayBody.Contains($deadTallyNeedle)) {
    Fail 'w58-5: TickAggroRangeDecay no longer tallies dead aggro entries -- the stale-target line goes blind again.'
}
# The liveness test must come BEFORE the veto sites, or a dead entry reaches
# NoteAggroRemains through the not-draining branch and vetoes anyway.
# The lambda is DECLARED as an assignment (no parenthesis after the name), so the
# first occurrence of the name followed by an open parenthesis is the first CALL.
$livenessIdx = $decayBody.IndexOf($notFightableNeedle)
$firstVetoIdx = $decayBody.IndexOf('NoteAggro' + 'Remains(')
if ($firstVetoIdx -lt 0) { Fail 'w58-5: no NoteAggroRemains call found in TickAggroRangeDecay; the veto-order gate is vacuous.' }
if ($livenessIdx -gt $firstVetoIdx) {
    Fail 'w58-5: the liveness test runs AFTER a veto site in TickAggroRangeDecay -- a dead entry can still veto.'
}

# --- the drop must actually be CALLED, and before the decay beat.
$tickAnchor = 'void APFACreatureController::' + 'Tick(float DeltaTime)'
$tickBody = Get-PfaMemberBody -Source $perceptionSource -Anchor $tickAnchor `
                              -NextDefinitionMarker $controllerMemberMarker
if (-not $tickBody) { Fail 'w58-5: APFACreatureController::Tick body not found; the drop-call gate is vacuous.' }
$dropCallNeedle = 'TickDead' + 'TargetDrop(DeltaTime);'
$decayCallNeedle = 'TickAggro' + 'RangeDecay(DeltaTime);'
if (-not $tickBody.Contains($dropCallNeedle)) {
    Fail 'w58-5: APFACreatureController::Tick no longer calls the dead-target drop -- the whole fix is dead code.'
}
# w58-7 (r2 review, generalisable countermeasure): the second needle of this
# ordering comparison, pinned by Contains so the comparison can never degrade
# into a tautology against a minus-one index.
if (-not $tickBody.Contains($decayCallNeedle)) {
    Fail 'w58-5: APFACreatureController::Tick no longer calls the range decay -- the ordering check below would be vacuous.'
}
if ($tickBody.IndexOf($dropCallNeedle) -gt $tickBody.IndexOf($decayCallNeedle)) {
    Fail 'w58-5: the dead-target drop must run BEFORE the range decay in Tick.'
}
# w58-8 (r3 review F-9c): the dead-OBSERVER guard was never pinned anywhere. It
# moved out of a small function into Tick in w58-7, where anyone inserting code
# above it silently un-guards a corpse -- so it now needs both a presence pin and
# an ordering pin, and the ordering comparison is preceded by Contains pins on
# BOTH needles ($dropCallNeedle is pinned above).
$observerGuardNeedle = 'SelfHealth->' + 'IsDead()'
if (-not $tickBody.Contains($observerGuardNeedle)) {
    Fail ('w58-7 F7: APFACreatureController::Tick no longer refuses to run on a DEAD creature -- a ' +
          'corpse that keeps its controller would resume decay, roam telemetry and the give-up disengage.')
}
$guardIdx = $tickBody.IndexOf($observerGuardNeedle)
if ($guardIdx -lt 0 -or $guardIdx -gt $tickBody.IndexOf($dropCallNeedle)) {
    Fail ('w58-7 F7: the dead-observer guard no longer precedes the drop call in Tick -- work would ' +
          'run on a corpse before the guard could stop it.')
}

# --- B: the liveness predicate must never grade an unmeasurable actor as dead.
# That is the OPPOSITE failure this change could introduce -- creatures dropping
# live targets they simply could not ask about.
$engagementCpp = Join-Path $Repo 'Source\PrimalFauna\Private\PFAEngagementCensus.cpp'
if (-not (Test-Path $engagementCpp)) { Fail 'w58-5: PFAEngagementCensus.cpp is missing.' }
$engagementSource = Get-Content -LiteralPath $engagementCpp -Raw
$unmeasurableToken = 'NoHealth' + 'Component'
$fightableAnchor = 'bool IsConfirmed' + 'NotFightable(ETargetLiveness Liveness)'
$fightableBody = Get-PfaMemberBody -Source $engagementSource -Anchor $fightableAnchor `
                                   -NextDefinitionMarker ("`n`tfloat Get" + 'StallSeconds')
if (-not $fightableBody) { Fail 'w58-5: IsConfirmedNotFightable body not found; the over-eager-drop gate is vacuous.' }
if ($fightableBody.Contains($unmeasurableToken)) {
    Fail ('w58-5: IsConfirmedNotFightable treats an unmeasurable actor as not fightable. ' +
          'Cannot-measure must never read as dead, or creatures drop live targets.')
}

# --- B: the assertion must be able to FAIL during ordinary play, not only when
# somebody types the self-test command. A test that cannot fire is exactly how
# this bug survived a previous fix.
$runPassAnchor = 'void RunPass(UWorld* World, const TCHAR* Trigger, bool bLogEveryCreature)'
$runPassBody = Get-PfaMemberBody -Source $engagementSource -Anchor $runPassAnchor `
                                 -NextDefinitionMarker ("`n`tbool Evaluate" + 'EngagementLiveness')
if (-not $runPassBody) { Fail 'w58-5: engagement RunPass body not found; the always-on FAIL gate is vacuous.' }
# w58-6 (review H3): TWO names, pinned separately. Merging the stall count back
# into the FAIL line makes a FAIL mean "either a real retention defect or a
# creature that is simply chasing you", i.e. nothing.
$failLineNeedle  = 'EngagementLiveness: ' + 'FAIL deadTargetsHeld='
$stallLineNeedle = 'EngagementStall: ' + 'OBSERVED stalled='
if (-not $runPassBody.Contains($failLineNeedle)) {
    Fail 'w58-5 H3: the dead-target assertion lost its always-on FAIL line.'
}
if (-not $runPassBody.Contains($stallLineNeedle)) {
    Fail ('w58-5 H3: the stall count is no longer reported under its own name -- merging it back ' +
          'into the FAIL line trains the reader to ignore a real defect.')
}
$targetAliveNeedle = 'target' + 'Alive='
if (-not $engagementSource.Contains($targetAliveNeedle)) {
    Fail 'w58-5: the engagement line lost its target-liveness field, which is the field this whole packet exists to add.'
}
# Same legend discipline as the corpse census: prose only, no equals sign, so a
# field grep can never match the legend that explains it.
$engLegendAnchor = 'Engagement ' + 'legend:'
$engLegendStart = $engagementSource.IndexOf($engLegendAnchor)
if ($engLegendStart -lt 0) { Fail 'w58-5: engagement legend not found; legend gate is vacuous.' }
$engLegendEnd = $engagementSource.IndexOf(');', $engLegendStart)
$engLegendText = $engagementSource.Substring($engLegendStart, $engLegendEnd - $engLegendStart)
if ($engLegendText.Contains([char]61)) {
    Fail 'w58-5: the engagement legend contains an equals sign -- it would collide with the greps the log exists for.'
}

# --- B: the recorders must be OUTSIDE the diagnostics gate. Recording that only
# happens while someone is watching is the w44-1 defect, and it would make the
# self-test silently vacuous in every ordinary session.
$meleeCpp = Join-Path $Repo 'Source\PrimalFauna\Private\Actions\PFACreatureActionMelee.cpp'
$meleeSource = Get-Content -LiteralPath $meleeCpp -Raw
$diagGateNeedle = 'PFADiag::' + 'IsEnabled()'
$canPerformAnchor = 'bool UPFACreatureActionMelee::' + 'CanPerformAction_Implementation'
$canPerformBody = Get-PfaMemberBody -Source $meleeSource -Anchor $canPerformAnchor `
                                    -NextDefinitionMarker ("`nvoid UPFACreatureActionMelee" + '::')
if (-not $canPerformBody) { Fail 'w58-5: CanPerformAction_Implementation body not found; the recorder gate is vacuous.' }
$sampleNeedle = 'NoteMeleeCan' + 'PerformSample('
if (-not $canPerformBody.Contains($sampleNeedle)) {
    Fail 'w58-5: CanPerformAction no longer records its sample -- the engagement line loses range, cooldown and verdict.'
}
if ($canPerformBody.IndexOf($sampleNeedle) -gt $canPerformBody.IndexOf($diagGateNeedle)) {
    Fail 'w58-5: the CanPerformAction recorder sits inside the diagnostics gate -- it would only record while someone is watching.'
}
$performAnchor = 'void UPFACreatureActionMelee::' + 'PerformAction_Implementation'
$performBody = Get-PfaMemberBody -Source $meleeSource -Anchor $performAnchor `
                                 -NextDefinitionMarker ("`nvoid UPFACreatureActionMelee::" + 'OnBiteImpact')
if (-not $performBody) { Fail 'w58-5: PerformAction_Implementation body not found; the perform-count gate is vacuous.' }
$performNeedle = 'NoteMeleePerform' + 'ActionStarted('
if (-not $performBody.Contains($performNeedle)) {
    Fail 'w58-5: PerformAction no longer records an action start -- the stall assertion could never pass or fail honestly.'
}
if ($performBody.IndexOf($performNeedle) -gt $performBody.IndexOf($diagGateNeedle)) {
    Fail 'w58-5: the PerformAction recorder sits inside the diagnostics gate -- the stall test would fire falsely with diagnostics off.'
}

# --- w58-6: TWO FILES THE w58-5 GATE NEVER LOADED. The cold review named both:
# a green mutation suite says nothing about a file it never opens, and the M4/M6
# fixes land in PFACreatureController.cpp while the liveness CONTRACT itself is
# declared in PFAEngagementCensus.h. Both pins are anchored on their enclosing
# function (or, in the header, on the declaration site), never on a bare
# fragment a sibling could satisfy.
$controllerCpp = Join-Path $Repo 'Source\PrimalFauna\Private\PFACreatureController.cpp'
if (-not (Test-Path $controllerCpp)) { Fail 'w58-6: PFACreatureController.cpp is missing.' }
$controllerSource = Get-Content -LiteralPath $controllerCpp -Raw

# M4: PackAssist must grade the TARGET it shares, not only the packmates.
# Bounded by the NEXT definition, which is `bool APFACreatureController::...`.
$packAnchor = 'void APFACreatureController::' + 'OnCreatureStateChanged_Implementation'
$packBody = Get-PfaMemberBody -Source $controllerSource -Anchor $packAnchor `
                              -NextDefinitionMarker ("`nbool APFACreatureController" + '::')
if (-not $packBody) { Fail 'w58-6: OnCreatureStateChanged_Implementation body not found; the PackAssist target gate is vacuous.' }
if (-not $packBody.Contains($notFightableNeedle)) {
    Fail ('w58-6 M4: PackAssist shares a target it never grades for liveness -- one state transition ' +
          'would ForceAggro the whole pack onto a corpse.')
}
$packSkipNeedle = 'outcome=target-graded-' + 'not-fightable'
if (-not $packBody.Contains($packSkipNeedle)) {
    Fail 'w58-6 M4: the PackAssist liveness skip no longer names its outcome -- the log could not show which branch ran.'
}

# M6: a real unbinding restarts the engagement hold clock, or heldSecs spans a
# disengage and a minute of roaming and gets quoted forward as a measured hold.
$resetAnchor = 'void APFACreatureController::' + 'ResetTargetingState'
$resetBody = Get-PfaMemberBody -Source $controllerSource -Anchor $resetAnchor `
                               -NextDefinitionMarker ("`nvoid APFACreatureController" + '::')
if (-not $resetBody) { Fail 'w58-6: ResetTargetingState body not found; the hold-restart gate is vacuous.' }
$holdRestartNeedle = 'mEngagementObservation.HeldTarget' + 'SinceSeconds = -1.0;'
if (-not $resetBody.Contains($holdRestartNeedle)) {
    Fail ('w58-6 M6: ResetTargetingState no longer restarts the engagement hold clock -- a re-aggro ' +
          'after a genuine disengage would report a hold spanning the whole gap.')
}

# The header carries the CONTRACT the two .cpp gates above both depend on.
$engagementHeader = Join-Path $Repo 'Source\PrimalFauna\Public\PFAEngagementCensus.h'
if (-not (Test-Path $engagementHeader)) { Fail 'w58-6: PFAEngagementCensus.h is missing.' }
$engagementHeaderSource = Get-Content -LiteralPath $engagementHeader -Raw
$gradeDeclNeedle = 'ETargetLiveness Grade' + 'TargetLiveness(const AActor* Target);'
if (-not $engagementHeaderSource.Contains($gradeDeclNeedle)) {
    Fail ('w58-6: PFAEngagementCensus.h no longer declares the shared liveness predicate -- a predicate ' +
          'defined per call site is a predicate that will diverge.')
}
$unknownGradeNeedle = 'NoHealth' + 'Component,'
if (-not $engagementHeaderSource.Contains($unknownGradeNeedle)) {
    Fail ('w58-6: the liveness enum lost its cannot-measure grade -- collapsing it into dead or alive ' +
          'is how creatures start dropping live targets they simply could not ask about.')
}
$notFightableDeclNeedle = 'bool IsConfirmed' + 'NotFightable(ETargetLiveness Liveness);'
if (-not $engagementHeaderSource.Contains($notFightableDeclNeedle)) {
    Fail 'w58-6: PFAEngagementCensus.h no longer declares the shared not-fightable test.'
}

# --- w58-7 (r2 review F6): THE FOURTH AGGRO-CLASSIFICATION SITE, in a file this
# gate never loaded. A green mutation suite says nothing about a file it never
# opens. This is the pin for the review's new mutant (M-c).
$roamCpp = Join-Path $Repo 'Source\PrimalFauna\Private\PFACreatureControllerRoam.cpp'
if (-not (Test-Path $roamCpp)) { Fail 'w58-7: PFACreatureControllerRoam.cpp is missing.' }
$roamSource = Get-Content -LiteralPath $roamCpp -Raw
$countAnchor = 'int32 CountLive' + 'AggroEntries(const TArray<FFGCreatureAggroData>& AggroTargets)'
$countBody = Get-PfaMemberBody -Source $roamSource -Anchor $countAnchor `
                               -NextDefinitionMarker ("`nvoid APFACreatureController" + '::')
if (-not $countBody) { Fail 'w58-7: CountLiveAggroEntries body not found; the roam-telemetry liveness gate is vacuous.' }
if (-not $countBody.Contains($notFightableNeedle)) {
    Fail ('w58-7 F6: CountLiveAggroEntries is named Live and grades no liveness -- a corpse holding full ' +
          'aggro would stamp every roam leg as a pursuit for the whole corpse lifetime, and the standing ' +
          'analysis instruction for this telemetry discards pursuit legs.')
}

# --- the census must actually RIDE the 10s heartbeat.
$refreshAnchor = 'void APFASubsystem::' + 'RefreshTick'
# Bounded by the NEXT definition, which is `bool APFASubsystem::Ensure...` -- NOT
# by the next `void`, which is 200 lines further on and would let a sibling
# function's text satisfy this pin.
$refreshBody = Get-PfaMemberBody -Source $subsystemSource -Anchor $refreshAnchor `
                                 -NextDefinitionMarker ("`nbool APFASubsystem" + '::')
if (-not $refreshBody) { Fail 'w58-5: APFASubsystem::RefreshTick body not found; the heartbeat gate is vacuous.' }
$censusCallNeedle = 'PFAEngagement::' + 'RunPass('
if (-not $refreshBody.Contains($censusCallNeedle)) {
    Fail 'w58-5: the engagement census is no longer driven from the 10s heartbeat -- it would only run on demand.'
}
Write-Host 'PFABUILD w58-5 dead-target gate OK (drop + pack-veto liveness + ungated recorders + always-on FAIL + clean legend)'

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

    # WwisePatches' generated pre-build .bat launches Windows PowerShell from
    # cmd.exe and calls Set-ExecutionPolicy before applyPatches.ps1. On this
    # machine that command can fail to autoload Microsoft.PowerShell.Security
    # in the custom-build-step context. Seed the child process policy instead;
    # the exact generated batch was verified to finish with this process-scoped
    # value even when its redundant Set-ExecutionPolicy call logs an error.
    $env:PSExecutionPolicyPreference = 'Bypass'
    Write-Host 'PFABUILD child PowerShell execution policy=Bypass for Wwise pre-build step'

    # --- gate 2b: access-transformer grants in the shared UHT headers ---------
    # A UHT run with any AT mod's .uplugin set aside regenerates the shared
    # FactoryGame headers without that mod's grants; on 2026-10-17 that left
    # PrimalFauna's AFGCreature/AFGCreatureController friends out of the Steam
    # headers and the next build died with C2248 in untouched files. Pre gates
    # refuse such a compile; post gates name any grant a compile stripped.
    # WU set-aside (gate 2) is the committed workaround for the UHT 'Unused
    # friend' error; it strips WU's 8 grants from every target -- kept by user
    # decision 2026-10-24 (WU not installed in the game).
    . 'C:\Claude\Projects\_team\tools\at_grants_gate.ps1'

    if ($BuildEditor) {
        $atEditorSnap = New-AtGrantsSnapshotPath -Mod 'PrimalFauna-editor'
        if ((Invoke-AtGrantsGate -Phase Pre -Mod PrimalFauna -Targets UnrealEditor -Snapshot $atEditorSnap `
                -AllowUndiscoverable WeaponUpgrades) -ne 0) {
            Fail 'AT grant pre-gate refused the FactoryEditor build (AT- lines above)'
        }
        Write-Host 'PFABUILD running FactoryEditor Win64 Development (editor DLLs the cook loads)...'
        & (Join-Path (Split-Path $RunUAT -Parent) 'Build.bat') FactoryEditor Win64 Development `
            -project="$UProject" -installed -waitmutex 2>&1 |
            Tee-Object -Variable editorOut |
            Select-String -Pattern 'Result: |error C\d+|error LNK|Total execution time' |
            ForEach-Object { $_.Line }
        $editorCode = $LASTEXITCODE
        $atEditorPost = Invoke-AtGrantsGate -Phase Post -Mod PrimalFauna -Targets UnrealEditor -Snapshot $atEditorSnap
        if ($editorCode -ne 0) {
            Write-Host 'PFABUILD raw FactoryEditor failure tail (last 80 lines):' -ForegroundColor Yellow
            @($editorOut) | Select-Object -Last 80 | ForEach-Object { Write-Host $_ }
            Fail "FactoryEditor build failed (exit $editorCode)"
        }
        $editorDll = Join-Path $ModsPlugin 'Binaries\Win64\UnrealEditor-PrimalFauna.dll'
        if (-not (Select-String -Path $editorDll -Pattern $Marker -Encoding unicode -Quiet)) {
            Fail "marker $Marker NOT found in $editorDll after the FactoryEditor build"
        }
        Write-Host "PFABUILD EDITOR_MARKER_${Marker}=True in UnrealEditor-PrimalFauna.dll"
        if ($atEditorPost -ne 0) { Fail 'AT grant post-gate: the FactoryEditor build left AT grants missing (AT- lines above)' }
    }

    if (-not $BuildEditor) {
        $staleEditorDll = Join-Path $ModsPlugin 'Binaries\Win64\UnrealEditor-PrimalFauna.dll'
        $editorHasMarker = (Test-Path $staleEditorDll) -and
            (Select-String -Path $staleEditorDll -Pattern $Marker -Encoding unicode -Quiet)
        if (-not $editorHasMarker) {
            Write-Warning ("PFABUILD UnrealEditor-PrimalFauna.dll does not contain marker $Marker -- the cook will load " +
                           "older editor code. Re-run with -BuildEditor if StartupModule, defaults or reflected types changed.")
        }
    }

    # PackagePlugin without -server compiles FactoryGameSteam only.
    $atSnap = New-AtGrantsSnapshotPath -Mod PrimalFauna
    if ((Invoke-AtGrantsGate -Phase Pre -Mod PrimalFauna -Targets FactoryGameSteam -Snapshot $atSnap `
            -AllowUndiscoverable WeaponUpgrades) -ne 0) {
        Fail 'AT grant pre-gate refused PackagePlugin (AT- lines above)'
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
    $atPost = Invoke-AtGrantsGate -Phase Post -Mod PrimalFauna -Targets FactoryGameSteam -Snapshot $atSnap

    if (-not ($uatOut -match 'BUILD SUCCESSFUL')) {
        Write-Host 'PFABUILD raw UAT failure tail (last 200 lines):' -ForegroundColor Yellow
        @($uatOut) | Select-Object -Last 200 | ForEach-Object { Write-Host $_ }
        Fail 'PackagePlugin did not report BUILD SUCCESSFUL'
    }
    # A post-gate failure does not stop the deploy checks below (the DLL is
    # already deployed); it turns the final exit into a failure instead.
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

# --- gate 4a2: the deployed DLL must have been LINKED BY THIS BUILD ----------
# A source file restored from a backup can carry an mtime OLDER than its own
# .obj. UBT then judges the object up to date and SKIPS the relink; the build
# reports BUILD SUCCESSFUL, the marker still matches (the marker text was not
# what changed), every content gate passes, and a DLL compiled from stale source
# deploys. Measured 2026-08-18 during the w58-3 packet: a mutated DLL reached
# the live game directory this way.
#
# mtime cannot express this -- the incident IS an mtime going backwards -- so
# key the gate on source CONTENT instead. Rule: if the source text changed since
# the last recorded build, the DLL must have been written during this build.
$srcFiles = Get-ChildItem (Join-Path $Repo 'Source') -Recurse -File -Include *.cpp,*.h |
            Sort-Object FullName
$srcConcat = ($srcFiles | ForEach-Object {
    $_.FullName.Substring($Repo.Length) + ':' + (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
}) -join "`n"
$srcHash = (Get-FileHash -InputStream ([System.IO.MemoryStream]::new(
    [System.Text.Encoding]::UTF8.GetBytes($srcConcat))) -Algorithm SHA256).Hash
$srcStampFile = Join-Path $Repo 'tools\.build-source-hash'
$prevSrcHash = if (Test-Path $srcStampFile) { (Get-Content -LiteralPath $srcStampFile -Raw).Trim() } else { '' }
$dllWritten = (Get-Item $DeployDll).LastWriteTime
if ($srcHash -ne $prevSrcHash -and $dllWritten -lt $BuildStart) {
    Fail ("STALE RELINK: source text changed since the last recorded build, but the deployed DLL was " +
          "written $dllWritten, BEFORE this build started $BuildStart. UBT skipped the relink -- the " +
          "game would load code compiled from DIFFERENT source than the tree you just built. " +
          "Touch every changed source file to now (Get-ChildItem ... | ForEach-Object { `$_.LastWriteTime = Get-Date }) and rebuild.")
}
Set-Content -LiteralPath $srcStampFile -Value $srcHash -NoNewline
Write-Host ("PFABUILD DLL freshness OK (source hash {0}, DLL written {1})" -f $srcHash.Substring(0,8), $dllWritten)

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

# The loot descriptor loads these by string path after content mount. A compile
# cannot prove the cooker retained them, so require the exact custom presentation
# assets in this build's cook instead of accepting the old Hog-visual fallback.
$CookRemainsDir = Join-Path $SmlRoot 'Mods\PrimalFauna\Saved\Cooked\Windows\FactoryGame\Mods\PrimalFauna\Content\SicklejawRemains'
$RequiredRemainsCook = @(
    'SM_PFA_SicklejawRemains.uasset',
    'T_PFA_SicklejawRemains_Icon_Small.uasset',
    'T_PFA_SicklejawRemains_Icon_Large.uasset',
    'T_PFA_SicklejawRemains_BaseColor.uasset',
    'T_PFA_SicklejawRemains_Normal.uasset',
    'T_PFA_SicklejawRemains_ReflectionMap.uasset',
    'T_PFA_SicklejawRemains_Cut_BaseColor.uasset',
    'MI_PFA_SicklejawRemains_Hide.uasset',
    'MI_PFA_SicklejawRemains_Cut.uasset'
)
$MissingRemainsCook = @($RequiredRemainsCook | Where-Object {
    -not (Test-Path (Join-Path $CookRemainsDir $_))
})
if ($MissingRemainsCook.Count -gt 0) {
    Fail ("Sicklejaw-remains assets missing from cook: " + ($MissingRemainsCook -join ', ') +
          " -- run tools\import_sicklejaw_remains_ue.py through tools\ue_headless.ps1, back-copy Content, then rebuild")
}
Write-Host ("PFABUILD remains content OK: {0} required assets present in the cook" -f $RequiredRemainsCook.Count)

# --- gate 4d: CONTENT PARITY, repo vs build tree ---------------------------
# The hole this closes: gate 1 syncs Source, Config and the .uplugin repo->Mods,
# and Content is in NEITHER list. Content is written straight into the build tree
# by the Unreal import scripts, and the repo copy is a manual back-copy with
# nothing enforcing it. That is exactly how the repo drifted 6 clips stale with
# no material at all, undetected, while every build reported success.
#
# THIS IS A DETECTOR, NOT A MUTATOR -- read-only by construction (Get-ChildItem +
# Get-FileHash, nothing else). Content must NEVER be added to the gate-1 robocopy
# /MIR list: that copies repo->build tree, so a stale repo would DELETE freshly
# imported assets, turning a reporting gap into data loss. If these two ever
# disagree, a human decides which side is right.
#
# WARNING, NOT Fail -- PERMANENTLY, BY DESIGN. Do NOT "promote it to Fail once it
# has proven quiet" (this comment used to say exactly that, and following it would
# have broken builds). A divergence here is the NORMAL state of a CORRECT workflow,
# not drift: import_sicklejaw_ue.py:65 writes to
# ...\SatisfactoryModLoader\Mods\PrimalFauna\Content\Sicklejaw -- the BUILD TREE
# ONLY -- and the repo copy is a manual back-copy made afterwards. The workflow is
# import -> build -> back-copy -> commit, so at the moment build.ps1 runs after any
# asset import the repo is LEGITIMATELY stale and this gate correctly reports
# "in build tree, NOT in repo". Fail would break every build that follows an import
# for doing precisely the right thing.
#
# "Proven quiet" is also a sampling trap. The trees are 54/54 as of 2026-07-26, so
# the gate will stay silent across the next few builds -- but those are builds with
# NO import, which is exactly the population that never exercises the promotion.
# Quiet on that sample is evidence about the wrong thing.
#
# If Fail is ever genuinely wanted, the prerequisite is to make the IMPORTER
# back-copy into the repo itself. That would make the repo authoritative and only
# then is Fail safe -- but it turns the importer into a repo mutator, so it is its
# own packet, not a footnote here.
#
# Off the critical path in both senses: it runs after the try/finally above has
# already released the build lock, so it cannot strand one, and the whole block
# is wrapped in try/catch because $ErrorActionPreference='Stop' would otherwise
# let an unreadable file abort the script and report a FAILED build that in fact
# succeeded.
try {
    $RepoContent = Join-Path $Repo 'Content'
    $ModsContent = Join-Path $ModsPlugin 'Content'
    if ((Test-Path $RepoContent) -and (Test-Path $ModsContent)) {
        $hashTree = {
            param($root)
            $map = @{}
            $rootFull = (Resolve-Path -LiteralPath $root).Path.TrimEnd('\')
            foreach ($f in (Get-ChildItem -LiteralPath $rootFull -Recurse -File)) {
                $rel = $f.FullName.Substring($rootFull.Length).TrimStart('\')
                $map[$rel] = (Get-FileHash -LiteralPath $f.FullName -Algorithm MD5).Hash
            }
            return $map
        }
        $repoMap = & $hashTree $RepoContent
        $modsMap = & $hashTree $ModsContent

        $diffs = @()
        $matched = 0
        foreach ($rel in ($repoMap.Keys | Sort-Object)) {
            if (-not $modsMap.ContainsKey($rel))       { $diffs += "in repo, NOT in build tree : $rel" }
            elseif ($modsMap[$rel] -ne $repoMap[$rel]) { $diffs += "DIFFERENT content          : $rel" }
            else                                       { $matched++ }
        }
        foreach ($rel in ($modsMap.Keys | Sort-Object)) {
            # The drift that actually bit us: imported into the build tree, never
            # back-copied to the repo, so the repo cannot rebuild what shipped.
            if (-not $repoMap.ContainsKey($rel)) { $diffs += "in build tree, NOT in repo : $rel" }
        }

        # N/N counts files that are byte-identical on BOTH sides, over the size of
        # the union -- so a divergence can never read as a full score, and the
        # matched count can never go negative (an early draft printed 0/3 for a
        # tree with one good file by subtracting diffs from the repo count).
        $union = ($repoMap.Keys + $modsMap.Keys | Sort-Object -Unique).Count
        if ($diffs.Count -eq 0) {
            Write-Host ("PFABUILD content parity: {0}/{1}" -f $matched, $union)
        }
        else {
            Write-Host ("PFABUILD !! content parity: {0}/{1} -- {2} divergence(s) between the repo and the build tree:" -f
                        $matched, $union, $diffs.Count) -ForegroundColor Yellow
            $diffs | Select-Object -First 12 | ForEach-Object { Write-Host "           $_" -ForegroundColor DarkYellow }
            if ($diffs.Count -gt 12) { Write-Host ("           ... and {0} more" -f ($diffs.Count - 12)) -ForegroundColor DarkYellow }
            Write-Host "           Content is imported into the build tree, so the build tree is USUALLY right --" -ForegroundColor Yellow
            Write-Host "           back-copy to $RepoContent and commit. Do NOT sync the other way." -ForegroundColor Yellow
        }
    }
    else {
        Write-Host 'PFABUILD NOTE: a Content dir is missing -- content parity NOT checked' -ForegroundColor Yellow
    }
}
catch {
    Write-Warning "PFABUILD content parity check could not run (build is unaffected): $_"
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
    $repoRoot = Split-Path $PSScriptRoot -Parent
    $unreviewed = @(git -C $repoRoot log --oneline "$lastReviewed..HEAD" 2>$null)
    # A deployed build can contain dirty Source/Content/Config/tool inputs even
    # when HEAD still equals the reviewed SHA. That exact state made w53-1
    # falsely print "up to date" before its required review. Docs/art alone do
    # not affect the packaged result, so scope this to build inputs.
    $dirtyBuildInputs = @(git -C $repoRoot status --porcelain --untracked-files=all -- Source Content Config PrimalFauna.uplugin tools 2>$null)
    if ($unreviewed.Count -gt 0 -or $dirtyBuildInputs.Count -gt 0) {
        Write-Host ''
        Write-Host "PFABUILD !! COLD REVIEW OWED: $($unreviewed.Count) commit(s) and $($dirtyBuildInputs.Count) dirty build input(s) beyond the last reviewed SHA." -ForegroundColor Yellow
        $unreviewed | Select-Object -First 6 | ForEach-Object { Write-Host "           $_" -ForegroundColor DarkYellow }
        $dirtyBuildInputs | Select-Object -First 6 | ForEach-Object { Write-Host "           dirty: $_" -ForegroundColor DarkYellow }
        Write-Host "           Run a fresh reviewer BEFORE the user tests. Then record the sha in:" -ForegroundColor Yellow
        Write-Host "           $((Resolve-Path $reviewMark -EA SilentlyContinue) ?? $reviewMark)" -ForegroundColor DarkYellow
        Write-Host ''
    } else {
        Write-Host 'PFABUILD cold-review gate: up to date' -ForegroundColor DarkGray
    }
} else {
    Write-Host "PFABUILD !! COLD REVIEW GATE NOT INITIALISED -- create _team\PFA-LAST-REVIEWED.txt with the last reviewed sha." -ForegroundColor Yellow
}

if ($atPost -ne 0) {
    Write-Host 'PFABUILD FAIL: AT grant post-gate -- PackagePlugin left AT grants missing in the shared UHT headers (AT- lines above); the build itself succeeded' -ForegroundColor Red
    exit 1
}
Write-Host 'PFABUILD OK' -ForegroundColor Green
exit 0
