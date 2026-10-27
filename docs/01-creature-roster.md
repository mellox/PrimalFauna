# 01 — Creature Roster

Four species for v1.0. Stats are specified as **anchors relative to vanilla creatures**, never
absolute guesses: packet W1.2 (doc 08) dumps the real CDO values (`UFGHealthComponent::mMaxHealth`,
attack damage fields) from live vanilla creatures and derives our numbers from those anchors. This
keeps us correct across game patches and keeps HostileScaling's CDO-based math meaningful (doc 06).

Art direction for all species: **dinosaur silhouette, alien-planet skin** — Coffee Stain's clean
stylized look, muted base tones + the planet's signature bioluminescent accents (like the glowing
flora/stinger markings). No feathers, no Earth-documentary realism. Full prompt briefs in doc 05.

## 1. Sicklejaw — the vertical-slice species (raptor-class pack hunter)

- **Fantasy:** fast, coordinated pack predator; the "wolves" of this planet. First contact should
  feel like the hog field moment: memorable, survivable, teaches respect for territory.
- **Silhouette:** ~player-height bipedal theropod (deinonychus-like), long tail, oversized curved
  jaw ("sickle"), bioluminescent throat/flank stripes that brighten when aggroed (emissive material
  parameter — cheap telegraph, doc 05).
- **Behavior loop:** roam near nest → detect player (sight/noise) → circle/approach → lunge melee.
  Pack of 2–4 per nest; pack members share aggro (when one aggros, nearby packmates join — doc 02 §4).
- **Attacks:** (a) bite melee, damage anchored ≈ Alpha Hog charge; (b) lunge/pounce gap-closer with
  brief windup telegraph (glow surge + screech). V1 slice ships with (a) only; (b) is a fast-follow
  in the same phase if the borrowed-behavior path (doc 07 §4) makes it cheap.
- **Stats anchors:** HP ≈ Alpha Hog. Move speed ≈ 1.3× player sprint (catchable-by-vehicle, not by
  running). Sight range ≈ Stinger. These are anchor RATIOS; W1.2 fixes numbers.
- **Territories:** grasslands / Rocky Desert borders / forest edges — mid-game traffic corridors,
  NOT the starter plains' spawn zones (doc 03 curates exact regions; starter areas get at most one
  distant, avoidable nest so new saves aren't griefed).
- **Loot:** `Desc_SicklejawRemains` → existing Alien Protein recipe pattern (§5).
- **Audio:** reuse base-game Wwise events (stinger screech family for aggro, hog flesh impacts) —
  per [[sf-mod-wwise-sound]], `PostAkEventAtLocation`, no custom banks in v1.

## 2. Crestback — heavy territorial charger (trike-class)

- **Fantasy:** armored quadruped tank that owns clearings; ignores you until you enter its circle
  or hit it, then charges like a freight train. The "environmental hazard you route around" —
  factory builders will curse and love it.
- **Silhouette:** triceratops-scale quadruped, bony head crest continuing down the spine,
  bioluminescent crest edges (brightness = aggro state).
- **Behavior:** grazing idle → territorial warning display (paw ground, flare crest, bellow) when
  player inside territory radius → charge attack (heavy damage + big knockback) with long straight-line
  commitment and slow turn — dodgeable sideways; may collide with terrain for a stun window.
- **Attacks:** charge (anchored ≈ 2× Alpha Hog charge damage, large knockback); close-range
  headbutt for point-blank harassment.
- **Stats anchors:** HP ≈ 2.5–3× Alpha Hog (a mid-game rifle magazine dump shouldn't drop one).
  Slow walk, very fast charge.
- **Aggression mode:** **Territorial** (doc 02 §3) — the showcase species for that mode; default
  does NOT chase far beyond its territory.
- **Territories:** open fields (Grass Fields borders, Dune Desert fringe, Bamboo Fields clearings).
- **Loot:** `Desc_CrestbackRemains` (protein), plus 2–4 crest-plate trophy items (§5 — sink fodder).

## 3. Duskstalker — ambush predator (dilophosaur-class)

- **Fantasy:** solitary stalker of dark places and night hours; punishes complacent walking routes.
  The "you never see the first one" species — but its territories are fixed, so the *second* time
  you know where not to walk at night.
- **Silhouette:** lean mid-size theropod, low-slung, twin head fins, near-black skin with faint
  bioluminescent freckles that extinguish while stalking (emissive → 0 = "hunting you" tell for
  observant players).
- **Behavior:** lair-bound by day (idle in caves/dense foliage regions) → active hunting at night
  within territory → slow crouched approach from behind/flank → pounce. Breaks off if player turns
  a flashlight/spotlight on it within N meters (config-gated flavor rule, doc 02 §5 — implement
  only if the perception API exposes a cheap seam; otherwise cut, it's polish).
- **Attacks:** pounce (high burst, applies brief slow), then rapid bites; flees to darkness at
  <30% HP and re-stalks (one repeat max, then fully disengages — anti-annoyance rule).
- **Stats anchors:** HP ≈ Stinger family; pounce burst ≈ Elite Gas Stinger hit.
- **Day/night:** requires a time-of-day gate on activity. `AFGCreatureSpawner` has native day/night
  spawn awareness to verify in W1.2 (the census packet confirms which fields); otherwise our
  subsystem gates the controller's activity by `AFGTimeSubsystem` time. **[assumption → in-game test]**
- **Territories:** caves mouths, Red Jungle / Spore Flower zones, night activity everywhere in its
  regions.
- **Loot:** `Desc_DuskstalkerRemains` (protein) + occasional `Desc_DuskstalkerFang` trophy.

## 4. Sandwyrm — the flagship (one roaming individual; proposed)

- **Status (2026-10-14):** detailed modeling via Claude is active; the model is not ready and
  gameplay is not implemented. This section supersedes the earlier per-region, 25–40 m design.
- **Fantasy and population:** Dune-inspired segmented sand predator; **one saved roaming
  individual across desert biomes**, not one per region, one per player, or a fresh worm per event.
  Regional scoring can select its next target without creating additional individuals.
- **Provisional art targets:** **180 m body length, approximately 30 m shaft diameter,
  38 m external mouth-lobe span (36–40 m range), 26 m clear bore**. The wider head supersedes
  the earlier 30 m outer-collar cap; current proportions live in `art/sandworm/design/art-specification.md`.
  These are not measured miner-fit acceptance. Tooth intrusion, mouth poses and curved throat
  clearance must be measured against real Mk1/2/3 miners. Local editor miner geometry proved to
  be placeholders and is rejected for sizing (doc 10 §1). The collar is not the open passage.
- **Required motion and attacks (doc 03 §7):** underground and partially exposed travel; visible
  emergence; eruption beneath a miner to swallow it whole; an arching attack descending onto a
  miner from above; player and creature swallowing; articulated spine and mouth; sand eruption
  and travel effects. Disturbance scoring and rumble/dust telegraphs are proposed selection and
  readability mechanisms. Timing, collision, attackable windows and scoring require testing.
- **Death and loot:** killable through standard health/damage; tooth shards or spice are the
  requested drop candidates, with concept art and usable item assets. Their distribution,
  quantities and economy use, health anchors and post-kill respawn policy remain design decisions.
  Miner consumption requires an explicit contents/refund policy and native cleanup verification;
  modest radial building damage alone does not satisfy swallowing a whole miner.
- **Lifetime:** saved identity/health/death/drop state is separate from a materialized actor.
  Streaming may require safe retirement and re-materialization of that same individual. Do not
  assume permanent actor residency or reuse a small creature's center-distance thresholds for a
  180 m body; review the full body extent, attacks and player visibility (doc 03 §7a).
- **Constraint (locked):** despite custom movement, the Sandwyrm **must remain an
  `AFGCreature`-family actor with a standard `UFGHealthComponent` and CDO-authored stats** so
  HostileScaling scales it exactly like everything else (doc 06 §2). Movement/mesh/attack are
  custom; identity/health/damage pipeline are vanilla-standard.

## 5. Loot & economy integration

- Ground-species default: a `Desc_<Species>Remains` item (FGItemDescriptor, `Cat_Other`-style category,
  conveyor mesh = simple retint of our creature-part mesh or a vanilla remains mesh via
  ReuseGameFiles) + a recipe `Recipe_Protein<Species>` mirroring the vanilla
  hog/stinger-remains → Alien Protein constructor recipes (1 remains → 1 protein; read the vanilla
  recipe's exact shape in W1.2 and mirror it).
- Rationale: plugs into DNA capsules/sink/consumables with ZERO new economy design, and keeps
  "kill new creature" exactly as rewarding as vanilla hunting. Trophies (crest plates, fang)
  are proposed AWESOME-sink flavor items (sink points via the world module's
  `mResourceSinkItemPointsTable`).
- Sandwyrm exception: tooth-shard/spice drops in §4 supersede the old mandatory worm-core and
  heavy-protein proposal. Reuse the item/pickup pipeline, but do not invent a final recipe or
  sink value before the drop design is settled.
- Registration: loot recipes ride a single hidden-cheap schematic (or direct recipe registration in
  the world module) — decided in W1.2 per the content-registration model (schematic is the loading
  anchor; see skill `content-and-assets.md`).

## 6. Roster expansion parking lot (post-1.0, NOT in scope)

Swamp lurker (water ambush), canyon glider (flying — needs the flying-movement research spike),
broodmother + juvenile swarm (spawner-in-a-creature), "Primal Alpha" rare variants (bigger CDO-set
stat classes — NEVER runtime multipliers, doc 06 §2 explains why).
