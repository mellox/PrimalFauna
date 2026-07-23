# BT-FALLBACK-RECIPE — authoring `BT_PFAGeneric`

**When you need this:** the borrowed-BT gamble (docs/07 §4, W1.4) failed. Two distinct
failure signatures, both read from the log:

1. **`[PFA][TEST] BorrowedBTAssigned: FAIL ...`** — discovery found no usable vanilla
   `UBehaviorTree` (zero discovered, or none matched the name heuristic).
   `APFASicklejaw` CDO `mBehaviorTree` is null; the creature stands idle.
2. **`BorrowedBTAssigned: PASS` but the creature still stands idle** while aggroed and in
   bite range — i.e. a BT *was* assigned but it does **not** drive our actions. Confirm
   with `pfa.Diag 1`: if you see **no** `[PFA] Sicklejaw action=Melee ScoreAction ...`
   lines while a Sicklejaw is next to the player, the borrowed BT is not invoking the
   action-scoring loop (the docs/07 §4 "generic executor" assumption is false for that
   tree). This is the case `BT_PFAGeneric` exists for.

> The distinction matters: signature 1 is "no BT"; signature 2 is "BT assigned but actions
> never selected." The `ScoreAction`/`CanPerformAction`/`PerformAction`/`ProcessHit` staged
> logs in `UPFACreatureActionMelee` are what let you tell them apart in one test.

This is the **only** unavoidable editor step in the whole mod if the borrow fails (docs/07
§4 point 3). It is authored once and reused by every species.

---

## Step 0 — read the census the borrower already logged (do this first)

W1.4's borrower logs **every** vanilla behavior tree it found, at Display:

```
[PFA] BT census: N BehaviorTree asset(s) under /Game/FactoryGame
[PFA] BT census: /Game/FactoryGame/.../BT_SomeCreature.BT_SomeCreature
[PFA] BT census: ...
```

Grep `FactoryGame.log` for `[PFA] BT census:`. That list is the map you never had at design
time. **Before authoring anything, try Path A** (a hardcode is far cheaper than a hand-built
tree).

---

## Path A — hardcode a discovered vanilla path (try this before Path B)

If the census shows a plausible creature tree the name heuristic missed (e.g. a hostile-mob
tree not named Hog/Stinger/Spitter/Creature/Enemy/Wildlife):

1. Open that BT in the editor (Content Browser → paste the census path). Confirm it *reads*
   as a generic combat executor: a state-driven root that runs a "score & perform the
   creature's actions" task in a Fight/Combat branch, rather than hard-referencing a
   specific vanilla creature's bespoke nodes.
2. Hardcode it. In `Source/PrimalFauna/Private/PFABehaviorBorrower.cpp`, `PickCandidate`
   is the single place to bias the choice — add the exact asset name as a new **top** tier,
   e.g. `{ TEXT("BT_TheGoodOne"), TEXT("hand-picked from census, W1.x") }`. Keep discovery
   (so the census keeps logging) but force the pick. Rebuild; re-run `PFA.SelfTest`.

This keeps the whole system data-driven and avoids a hand-authored asset. Only fall to Path
B if **no** discovered tree is a generic executor.

---

## Path B — author `BT_PFAGeneric` from scratch

### Prerequisites
- Satisfactory editor project open (the modding starter, `SatisfactoryModLoader`).
- A blackboard is required by `UBehaviorTree`. Reuse a vanilla creature blackboard if one is
  visible in the census-linked trees (open a census BT → note its `BlackboardData` asset);
  otherwise create a minimal `BB_PFAGeneric` with at least a `SelfActor` (Object) and a
  `TargetActor` (Object) key. **Open a vanilla creature BT first and copy its blackboard key
  set** — the SF creature controller populates specific keys, and guessing them is the main
  risk of a from-scratch tree.

### The structure (best-effort — verify against a census BT)
The exact node that runs the creature's `mCreatureActionsSetup` is an **SF-custom BT task**
whose name is not knowable statically (it lives in the closed FactoryGame binary). Get it by
opening any census-listed creature BT in the editor and reading its Fight/Combat branch — the
task that "executes the current creature action" is what you replicate. With that caveat, the
minimal tree is:

```
BT_PFAGeneric  (Blackboard = BB_PFAGeneric or the reused vanilla blackboard)
└─ Root
   └─ Selector
      ├─ Sequence  [decorator: blackboard TargetActor Is Set]      ← FIGHT branch
      │   └─ <SF "run creature action" task>   (copied from a census BT's fight branch)
      └─ <Roam/Idle task>                                          ← fallback branch
          (a Wait, or the vanilla roam task if the census BT exposes one)
```

The load-bearing node is the fight-branch action task. Everything else (roam, alert,
investigate) is optional for the Phase-1 combat proof — a Selector with just the fight
branch + a `Wait` fallback is enough to prove the bite fires. Expand later.

Our action side is already done: `UPFACreatureActionMelee` is enabled for `CS_Fight`
(`RequiredStates`), forces a positive `ScoreAction`, and gates on range in
`CanPerformAction`. So the BT only has to (a) enter a fight state when there is a target and
(b) invoke the creature action executor. It does not need to know anything about the bite.

### Assigning `BT_PFAGeneric` to our creatures
Do **not** re-plumb the borrower for this. Instead, hardcode the asset path as a top tier in
`PickCandidate` (as in Path A step 2) — put `BT_PFAGeneric` under `/Game/PrimalFauna/AI/` and
add `{ TEXT("BT_PFAGeneric"), TEXT("authored fallback tree, W1.x") }` as the first tier.
Because our own content is under `/Game` (the primary search root already covers it, and the
`/Game` widen covers it regardless), the existing discovery finds and assigns it with no new
code path. `BorrowedBTAssigned` then PASSes on our own tree.

> Alternative (heavier, not recommended for the fallback): a thin BP subclass of
> `APFASicklejaw` with `mBehaviorTree` set in the editor. Rejected because it splits the CDO
> source of truth (docs/07 §3) and would have to be repeated per species; the hardcode-a-path
> route keeps one assignment mechanism for both borrowed and authored trees.

---

## After either path — re-verify
1. `PFA.SelfTest` → `[PFA][TEST] BorrowedBTAssigned: PASS ... mBehaviorTree=<path>`.
2. In-game, `pfa.Diag 1`, spawn a Sicklejaw at the player (`PFA.SpawnTest` / a nest):
   expect the staged lines `ScoreAction → CanPerformAction=true → PerformAction START`.
   If those appear, the tree drives our actions (the borrow assumption now holds for this
   tree). Whether the montage-less bite then deals damage (`ProcessHit` line) is the
   separate assumption-2 item — see the W1.4 handoff's montage-less fallback levers.
