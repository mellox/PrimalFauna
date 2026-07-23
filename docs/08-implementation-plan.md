# 08 — Implementation Plan (team execution)

Written to be executed by an **orchestrating main agent handing packets to lower-cost
implementation agents** (the Satismagictory model). Nothing here overrides workspace law: build
lock, robocopy repo→Mods sync before every build, cold review after every build, differential
review on ⚠ packets, diagnostics everywhere.

## 1. Orchestration model

- **Main agent = orchestrator + integrator.** Owns packet sequencing, builds (under
  `_team/BUILD-LOCK.txt`), cold-review dispatch, in-game checklist handoff to the user, and
  `_team/primalfauna-state.md` updates after every packet.
- **Implementation funnels — ONE packet in flight.** Implementers (`satisfactory-modder`, or
  `ruflo-core:coder` for pure C++) write in the canonical repo only; never build, never deploy.
- **Research/design may fan out read-only** (farmout pattern) but defaults inline per budget rules.
- **Cold review = fresh `ruflo-core:reviewer` per build**, BEFORE user tests. ⚠ packets get the
  differential/parity prompt (old-path invariants → provided/assumed/unverifiable → in-game list).
- **Asset track runs PARALLEL to the systems track** (it's not code), executed by the shared
  **`mod-artist` agent** (model table below) — and respects GPU law: no generative/texture tools
  during builds; UE headless imports sequenced like builds.
- **Models (explicit per role — the budget rule made concrete):**

  | Role / packets | Model | Why |
  |---|---|---|
  | Orchestrator (sequencing, builds, integration, census sessions with user, architecture deviations) | **top-tier session model** (Opus-class+) | Judgment-heavy; owns every merge |
  | ⚠ novel-engineering implementation: **W1.4** (borrowed-BT + actions), **W4.1** (worm movement/director) | **top-tier** | Assumption-resolving, no prior art anywhere; rework costs more than the tier delta |
  | **All art creation & art decisions:** W2.1, W4.4, the art halves of W3.1/W3.2 (creature asset runs) and W4.2 (telegraph/eruption VFX), icons, any post-ship art tweak | **`mod-artist` agent** (`C:\Claude\Projects\.claude\agents\mod-artist.md`, runs `opus`; orchestrator may override to the session top tier for hero pieces — Sandwyrm, eruption) | Art judgment demonstrably fails on cheap tiers (Satismagictory's spheres-and-circles); the shared agent carries the pipeline + anti-primitive guardrails |
  | Standard implementation: W0.1, W1.1, W1.3, W1.5, W2.2–W2.6, W3.1–W3.4 (systems halves), W4.2 (wiring/replication), W4.3, W4.5, W5.1–W5.2 | `sonnet` | Contract-driven C++/bpy against these docs |
  | Mechanical passes: CENSUS.md transcription, region-table data entry, anim import batches, ASSET-LEDGER | `haiku` (`effort: low`) | Verify/extract work, cheap to check |
  | Cold review, ordinary packets | `sonnet` (fresh `ruflo-core:reviewer`) | Independent eyes ≫ tier here |
  | Cold review, ⚠ packets (W1.4, W1.5, W2.5, W3.4) + anything touching doc 06 contract items | **top-tier** differential/parity review | The regression class that slips past local review |
  | Local LLM (LM Studio) | log/dump summarization during test sessions ONLY | Never implementation (correctness-critical), never during builds (VRAM) |

  State agent counts before fan-outs >10; one implementation packet in flight regardless of tier.
- **Mandatory reading for every implementer:** satisfactory-modding skill (esp. `sml-gotchas.md`,
  `new-mod-scaffold.md`), `_team/playbooks/satisfactory-mod.md`, doc 07, and the packet's domain doc.

### Packet handoff contract

```
Inputs:     doc sections + memory names listed in §5 for the family (load ONLY these)
Scope:      files/classes owned by the packet (no drive-by edits)
Invariants: doc 06 contract items (C1-C6/S1-S4) the packet may touch
Output:     code in repo + handoff note + [PFA] diagnostics on every new path
Done when:  compiles in orchestrator build + cold review SHIP + in-game checklist items written
```

## 2. Packet families

**W0 — Skeleton**
- **W0.1** Plugin scaffold per `new-mod-scaffold.md`: `.uplugin` (SML ^3.12.0), module + build
  marker, `UPFAConfig` skeleton (traps A–D), root modules, `APFASubsystem` skeleton with RefreshTick
  heartbeat, `LogPrimalFauna`. **Gotcha:** brand-new plugin needs the FactoryEditor module compiled
  once via Build.bat before PackagePlugin cooks [[sf-new-plugin-needs-editor-build]]. Gate: mod
  loads, marker in log, config page renders + saves to `Configs/PrimalFauna.cfg`.

**W1 — Systems slice (placeholder assets; parallel with W2 asset track)**
- **W1.1** Dev commands + census (`PFACheats`): `PFA.DumpCreature` (class/BT path/actions/health/
  aggro), `PFA.DumpCandidates`, `PFA.SpawnNest`, `PFA.KillAll`; anchor dump (doc 07 §9). Also
  exercise SML's built-in `ForceSpawnCreatures` cheat as the spawner test accelerator.
- **W1.2** Census run (user session): dump hog/stinger/spitter — vanilla BT asset paths, action
  configs, anchor stats, remains→protein recipe shape. Output: `docs/CENSUS.md` (data, not prose).
- **W1.3** `APFACreatureBase` + `APFACreatureController` (full doc 02 §2 policy + Verbose verdicts) +
  `APFASicklejaw` with **scripted placeholder rig** (Blender capsule + 2-bone chain export — proves
  import path without waiting on art) + health/speeds/perception on CDO.
- **W1.4** ⚠ Borrowed-BT + actions: CDO `mBehaviorTree` ← vanilla tree (census path), melee action
  via `CreateDefaultSubobject`, montage-less. **The riskiest systems packet** — instrument
  action-scoring/target lines from the first build. Fallback ready: `BT_PFAGeneric` editor recipe.
- **W1.5** ⚠ `APFANestSpawner` + minimal injection (ONE hardcoded region near spawn), stable names,
  write-once spawn data, adopt-on-load idempotence.
- Phase-1 gate (in-game): placeholder Sicklejaw spawns from our nest, detects, chases, bites,
  takes damage, dies, drops a test item, respawns per days; save/load keeps kill-state; no
  duplicate nests after reload.

**W2 — The Sicklejaw (real creature; ships as 0.9 beta candidate)**
- **W2.1** Asset track per doc 05 §§1–4: generate→clean→rig-check (want `biped`)→rig→preset anims→
  Blender-author gaps→texture→**textured-render review gate with user** (purchase decision point if
  rung 4 needed).
- **W2.2** Import + wire (doc 05 §6): mesh/anims/montages/AnimBP-or-code, emissive aggro material.
- **W2.3** Full config tree (doc 04) + live-apply reconciliation + `mIsPassiveCreature` mode 0.
- **W2.4** HostileScaling verification matrix (doc 06 §4) — including the 2-cycle compounding test.
- **W2.5** ⚠ Territory authoring: `PFA.DumpCandidates` sessions with user → bake real Sicklejaw
  regions → dynamic-spawner persistence verdict (doc 03 §4; fallback flip if needed).
- **W2.6** Pack assist (doc 02 §4) + polish (loot final, sounds via Wwise reuse).

**W3 — The roster**
- **W3.1** Crestback: quadruped asset run + charge/roar actions + Territorial mode showcase.
- **W3.2** Duskstalker: asset run + leap + night gating (`mCanSpawnDuringNight`) + stalk flavor
  (experimental-flagged).
- **W3.3** Region expansion to the doc 01 biome sets + anti-grief checks at scale.
- **W3.4** Radar/scanner surface (doc 07 §8; ⚠ only vanilla-CDO-write packet, experimental-gated).

**W4 — The Wyrm Beneath**
- **W4.1** `UPFAWormMovement` + director state machine + procedural spine on placeholder segments +
  `PFA.WormForceEvent`. Persistent-creature exemptions verified (`SetIsPersistent`, orphan timer).
- **W4.2** Telegraph chain: rumble → dust trail (Niagara ribbon + decal) → eruption flipbook VFX +
  audio; MP-replicated visuals.
- **W4.3** Emergence radial damage (causer/instigator per C3) + surface window + kill/respawn/
  trophy + config gates.
- **W4.4** Worm final assets (doc 05 §7) + review gate (StockLancer fallback decision if needed).
- **W4.5** Full-region balance: disturbance scoring tuning session in a real desert factory save.

**W5 — Apex (release)**
- **W5.1** Balance playthrough vs HostileScaling at tier 3/6/9 (anchors tuning only).
- **W5.2** MP matrix: host+client on everything (targeting, nests, worm telegraphs, config
  authority) + dedicated-server package smoke.
- **W5.3** Release prep: verbose gating audit, `.uplugin` metadata, README/GitHub-About/uplugin
  Description triple-check [[nodeshuffle-release-checklist]], `package_release.ps1` multi-target,
  ASSET-LEDGER license audit (doc 05 §5). Publish only when the user asks.

## 3. Phases

| Phase | Name | Packets | Proves / ships |
|---|---|---|---|
| 0 | Skeleton | W0.1 | Plugin loads, config UI works |
| 1 | First Blood | W1.1–W1.5 | **The whole creature loop works in-game on placeholder art** — spawn/AI/combat/death/loot/save/respawn + both ⚠ assumptions resolved |
| 2 | The Sicklejaw | W2.1–W2.6 | One REAL creature, full config, HostileScaling matrix green, real territories. First public-lookable build |
| 3 | The Pack & the Plains | W3.1–W3.4 | Three species, full regions, radar polish |
| 4 | The Wyrm Beneath | W4.1–W4.5 | The flagship |
| 5 | Apex | W5.1–W5.3 | Balance, MP, release candidate |

Phase-1-before-art rationale: The Wilds (the only prior attempt at this mod, July 2025) died
between design doc and first code. Phase 1 exists to make the riskiest engineering REAL before any
content investment — if the borrowed-BT and spawner-persistence bets both fail, we know within the
first phase at placeholder cost, with authored-BT and transient-registry fallbacks specced.

## 4. In-game test checklists (owner: user; orchestrator writes per-build)

Standing items every phase: fresh save · existing save · mod-added-mid-save · mod-removed-load ·
SP + MP client · HostileScaling present/absent/disabled (doc 06 §4 matrix from Phase 2) ·
`[PFA]` log review (the log alone must explain every decision path) · NodeShuffle-active
coexistence smoke · vanilla AGS creature-hostility setting flipped.

**[assumption] items with dedicated lines (from the differential reviews):**
1. Borrowed vanilla BT drives our actions (docs 07 §4) — Phase 1.
2. Actions run montage-less (07 §5) — Phase 1.
3. Dynamic `APFANestSpawner` persists with kill-state; no dupes; creatures re-couple (03 §4) — Phase 1/2.
4. Native `mCreaturesCanAttackEachother` doesn't veto our infighting (02 §7.1) — Phase 1.
5. Vanilla creatures retaliate against ours (02 §7.2) — Phase 1.
6. Per-instance `mIsPassiveCreature` cleanly de-aggros + HostileScaling skips (02 §7.3) — Phase 2.
7. `Super::CanBeHostileTowards` honors per-player passive mode for our species (02 §7.4) — Phase 2.
8. Spawner near-base suppression behaves (`mCachedIsNearBase`) (03 §1) — Phase 2.
9. Worm persistent-creature exemptions hold (03 §7a) — Phase 4.
10. Radar tower lists our fauna via spawners (07 §8) — Phase 3.

## 5. Packet-family inputs (load ONLY these + doc 07)

| Family | Docs | Memories / references |
|---|---|---|
| W0 | 04, 07 | new-mod-scaffold.md, sml-gotchas §3/§7, [[sf-new-plugin-needs-editor-build]], playbook |
| W1 | 01 §1, 02, 03, 07 | sml-gotchas §5/§6, [[sml-tcallscope-substitute-args]] (contingency only), CENSUS.md |
| W2 asset | 01, 05 | [[feedback-render-textured-for-review]], [[blender-ue-fbx-export-gotchas]], [[sf-skeletal-mesh-import-crash]], [[blender-ue-batch-memory-hygiene]], [[tripo-two-credit-pools]] |
| W2 systems | 02, 03, 04, 06 | sml-gotchas §3, HostileScaling source (read-only reference) |
| W3 | 01, 02, 03 | world-modification.md §5-6 (spawned-actor lessons), content-and-assets.md (scanner CDO rules) |
| W4 | 01 §4, 02 §6, 03 §7, 05 §7 | [[laserrifle-procedural-hold]], [[sf-mod-wwise-sound]], VectorayGen/VDB guide sections |
| W5 | 00 §3, 06 | [[nodeshuffle-release-checklist]], packaging-and-publishing.md, [[driving-satisfactory-via-log]] |

## 6. Risk register

| # | Risk | Mitigation |
|---|---|---|
| R1 | **Nobody has ever shipped a custom SF creature** — unknown unknowns in binary-side behavior | Native-machinery-first architecture (real spawners/actions/BT); Phase 1 resolves all flagged assumptions on placeholder art; census-driven (copy what vanilla data does, not what docs say — there are no docs) |
| R2 | Borrowed vanilla BT incompatible with our actions | Authored `BT_PFAGeneric` fallback (one editor recipe, reused by all species) — W1.4 exit criterion either way |
| R3 | Dynamic spawner persistence broken (MobSpawners' manta/crab bugs are evidence this CAN bite) | Doc 03 §4 fallback: transient spawners + own SaveGame registry; registry designed for the swap |
| R4 | Native creature-vs-creature gate vetoes infighting | Doc 02 §7.1 fallbacks (global toggle / direct aggro), instrumented from first build |
| R5 | AI-pipeline animation quality below bar | Doc 05 §3 ladder; biped-classification trick; purchase fallbacks pre-licensed, pre-priced, user-approved at review gates |
| R6 | Worm procedural motion reads badly | StockLancer pack fallback ($60, license-clean); director/VFX layers unchanged either way |
| R7 | HostileScaling compounding on our spawners | Contract S1–S4 + mandatory 2-cycle compounding test (doc 06 §4) |
| R8 | Injected nests stall on navmesh (spawn never fires) | Spawners are navigation invokers; ground-Z baked at authoring; `ForceSpawnCreatures` + `WaitingForSpawnLocation` logging localize stalls |
| R9 | Scope creep (The Wilds syndrome — design forever, ship never) | Phase gates are shippable builds; parking lots in 00 §4 / 01 §6; v1.0 definition fixed below |
| R10 | Token budget | Packet model tiers (§1); asset track parallelism uses user-time (reviews), not agent fan-outs |

## 7. Definition of v1.0

Doc 00 §3 verbatim: 3 land species in curated multi-biome territories + Sandwyrm events in the
Dune Desert; HostileScaling matrix green both ways; loot in the protein economy; full config;
verbose logs gated; MP matrix green; release checklist done. Publishing only when the user asks.
