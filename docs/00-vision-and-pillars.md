# 00 — Vision & Pillars

**Mod:** PrimalFauna *(working title — verify no ficsit.app collision before scaffolding; alternates: Xenofauna, ApexFauna, PrimalThreat. Renaming is free until the plugin folder is created; after that it's a breaking ModRef change.)*

**One-liner:** New hostile megafauna for Satisfactory — dinosaur-class predators with real AI,
territories you learn to respect, and (as the flagship finale) a Dune-style sand worm beneath the
Dune Desert. FICSIT set up shop on a planet whose apex predators were merely *asleep*.

This is a **plan-only repository** (like Satismagictory): these docs are the contract for an
orchestrating agent handing packets to lower-cost implementation agents. No source exists yet.

## 1. Locked decisions (user-approved 2026-07-11)

| # | Decision | Choice |
|---|---|---|
| D1 | Asset source | **AI pipeline first** (Tripo/Hunyuan3D → Blender → Tripo rig → StableProjectorz/ArmorPaint), marketplace packs as the per-creature fallback. See doc 05. |
| D2 | V1 scope | **One creature vertical slice first** (the Sicklejaw raptor), proving spawn → AI → combat → save/MP → HostileScaling pickup → config, before any roster expansion. Sand worm is a late flagship phase. |
| D3 | Availability | **Active from game start** in their biomes; config controls density/aggression. No tier gating in v1 (HostileScaling already escalates stats with progression). |
| D4 | Spawning | **Biome nests/territories** — persistent, seeded spawner injection per curated region; vanilla-like respawn. The worm gets its own event-driven system in its phase. |
| D5 | HostileScaling | New creatures **must be scaled by HostileScaling exactly like vanilla creatures**, with **zero dependency in either direction**. Achieved structurally (doc 06) — not by cross-mod code. |
| D6 | Independence | PrimalFauna is its own mod/repo/vault. It must behave correctly with HostileScaling absent, present, enabled, or disabled. |

## 2. Pillars (tie-breakers for every design argument)

1. **They are creatures, not scripts.** Every creature is a real `AFGCreature`-family citizen:
   standard health component, standard damage pipeline, standard spawner machinery wherever
   possible. This is what makes save/load, multiplayer, the resource scanner, other mods
   (HostileScaling!), and future game patches work *for free*. Custom paths (worm movement) are
   quarantined behind the smallest possible surface.
2. **Territory over ambush.** Danger should be *legible*: players learn "that valley belongs to
   Crestbacks" the way they learned hog fields. Nests are persistent and discoverable, not random
   jump-scares. (D4.)
3. **Hostile by default, configurable by choice.** Ships aggressive. Every species has config
   controls (enable / aggression / density) so players tune their own nightmare. UI advice locked in
   doc 04: C++ ModConfiguration tree, HostileScaling-style.
4. **The ecosystem fights back — and each other.** Creature-vs-creature hostility is architectural
   from day one (doc 02's target policy), shipped conservatively (predators fight vanilla
   *hostiles*; never tamed pets; passive fauna protected by default).
5. **Satisfactory's alien-planet look, dinosaur silhouettes.** Not Earth Jurassic-park skins:
   scaly megafauna with the planet's bioluminescent accents, matching Coffee Stain's clean stylized
   art direction (doc 05 §art direction).
6. **Vertical slice before roster.** Nothing gets a second creature until the first one survives
   the full gauntlet: fresh save, existing save, MP client, HostileScaling on/off, mod-removed
   save-load. (D2.)
7. **Diagnostics everywhere.** Every decision path logs under `LogPrimalFauna` (`[PF]` prefix);
   one launch must localize any failure. Per-entity spam at Verbose, session summaries at Display —
   NodeShuffle conventions.

## 3. V1.0 definition (ship gate)

- 3 land species (Sicklejaw pack hunter, Crestback charger, Duskstalker ambusher) live in curated
  territories across at least 4 biomes; respawn/persistence correct; scanner/compass behavior
  decided and consistent.
- Sandwyrm flagship in the Dune Desert (+ Rocky Desert option): patrol, telegraphed emergence
  attacks, config-gated building damage. Event-driven, not nest-driven.
- All species scaled by HostileScaling when present (verified in-game both ways).
- Loot integrated with the vanilla protein/remains economy (doc 01 §5).
- Full config tree; verbose logs gated; MP matrix green; release checklist per playbook.

## 4. Non-goals (v1) — the parking lot

- **No taming/breeding**, no saddles. (Future-proof: nothing in doc 02's target policy prevents a
  tamed state later — `IsTamed()` is already respected everywhere.)
- **No new biomes / map edits.** Territories are curated placements in the existing world.
- **No MAM research tree** in v1 (avoids the LaserRifle MAM-vs-recipe drift class of bugs;
  loot sinks into the existing protein chain instead). Revisit post-1.0 (bestiary tree).
- **No vanilla-creature replacement or removal.** We only add. Vanilla spawners are untouched
  (that's HostileScaling's lane).
- **No egg-stealing/raiding economy** in v1 (candidate post-1.0 hook: nest loot).
- **No arachnophobia-mode variants** (none of the designs are spider-like; noted for QA only).

## 5. Reading order

| Doc | Contents |
|---|---|
| 01 | Creature roster: species designs, stats, behaviors, loot |
| 02 | Hostility & factions: target policy, creature-vs-creature, provocation |
| 03 | Spawning & territories: region data, spawner injection, persistence, density |
| 04 | Config & UI: the full config tree, defaults, UI advice |
| 05 | Asset pipeline: per-creature recipe (gen → rig → anim → texture → import), worm procedural rig |
| 06 | Integration: the HostileScaling zero-dependency contract, MP, save, coexistence tests |
| 07 | Technical architecture: classes, subsystems, hooks, diagnostics |
| 08 | Implementation plan: packets, phases, test checklists, risks |

Workspace law applies throughout: build lock, cold review after every build, differential/parity
review for path-replacing changes, read `_team/playbooks/satisfactory-mod.md` before building.
