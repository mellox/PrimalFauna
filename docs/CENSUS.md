# CENSUS — live vanilla creature data (feeds doc 01 anchors)

Data collected in-game via `PFA.DumpAllCreatures` / `PFA.DumpCreature`. Append per session with
provenance. Stat anchors in doc 01 resolve against THESE numbers, never memory.

## 2026-07-23 — smoke session (Grass Fields spawn, build w11-2, 113-mod profile incl. SF+)

| Class | Count seen | CDO max health |
|---|---|---|
| `Char_Stinger_Child_C` | 1 | 10.0 |
| `Char_SpaceGiraffe_C` | 2 | 200.0 |
| `Char_NonFlyingBird_C` | 2 | 10.0 |

Action-config sample (`PFA.DumpCreature`, pfa.Diag=1): `Char_NonFlyingBird_C` carries
`BPCA_NonFlyingBird_Lure_C` + `UFGCreatureActionForwardJump` (`mForwardVelocity=500`) — confirms
vanilla creatures configure native action subclasses exactly as doc 07 §5 assumes, and the
reflection dump reads them.

## 2026-07-23 — Phase-1 gate re-run: THE BT CENSUS (build w15-3, full profile)

Only **3 BehaviorTree assets exist under /Game/FactoryGame** (even with 113 mods + SF+ loaded):
- `/Game/FactoryGame/Prototype/Exploration/Creatures/BT_CreatureBehavior.BT_CreatureBehavior` ← **the shared hostile tree** (hogs/stingers; SpaceGiraffe verified running it live); borrow chose + assigned this one.
- `/Game/FactoryGame/Character/Creature/Wildlife/SpaceRabbit/BT_SpaceRabbit.BT_SpaceRabbit`
- `/Game/FactoryGame/Character/Creature/Wildlife/NonFlyingBird/BT_NonFlyingBird.BT_NonFlyingBird`

Gate finding: the borrow WORKS (asset valid, CDO assignment + readback OK) but a creature's
controller must START the tree — vanilla `BP_CreatureController_C` does it in its BP graph;
`APFACreatureController` must do it in C++ (W1.6). Also: fresh nest Sicklejaws read
`Health=0.0/200.0` (current-health init gap; vanilla reads 200/200) — W1.6 fixup.

**Still needed (Phase-1 gate or later):** hog family (`Char_Hog_C`, alpha variant), stinger
adults/elites, spitter — the doc 01 anchor species (Sicklejaw ≈ Alpha Hog HP, etc.). Also
`GetBehaviorTreeAsset()` paths per class (W1.4 needs the hog/stinger BT paths — none captured yet;
the smoke session's creatures were all passive fauna). **Caveat:** captured under the full mod
profile (SF+ present) — re-verify any surprising number in a clean profile before hard-coding it.
