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

## 4. Sandwyrm — the flagship (event creature, own phase)

- **Fantasy:** the Dune homage. The Dune Desert stops being the boring flat biome: heavy machinery
  and sustained noise there now has a cost. You learn to read the sand.
- **Silhouette:** segmented armored worm, 25–40 m emerged length, tri-petal jaw, dust-glow between
  segments. (Procedural bone-chain rig — doc 05 §6; this is deliberately NOT a navmesh creature.)
- **Behavior (event-driven, doc 03 §7):** invisible "patrol" point moves under the sand within the
  worm region → telegraph chain when a target is selected: distant rumble (audio) → moving dust
  trail + ground decal approaching (5–10 s of readable dread) → emergence eruption at the target
  point (radial damage + physics launch + huge dust VFX) → brief surface lunge window (attackable!)
  → re-submerge, cooldown. Targets are scored by *disturbance*: players sprinting/vehicles driving/
  buildings operating in the region (doc 02 §6).
- **It is killable** during surface windows (HP anchored ≈ 8–10× Alpha Hog; HostileScaling applies).
  Killing it grants a long regional respite (multi-day respawn) + a unique trophy + heavy protein
  yield. Not killing it is fine: emergence damage vs buildings is config-gated (default ON at modest
  damage; "worm ignores buildings" toggle for pacifist factories, doc 04).
- **Constraint (locked):** despite custom movement, the Sandwyrm **must remain an
  `AFGCreature`-family actor with a standard `UFGHealthComponent` and CDO-authored stats** so
  HostileScaling scales it exactly like everything else (doc 06 §2). Movement/mesh/attack are
  custom; identity/health/damage pipeline are vanilla-standard.

## 5. Loot & economy integration

- Every species drops a `Desc_<Species>Remains` item (FGItemDescriptor, `Cat_Other`-style category,
  conveyor mesh = simple retint of our creature-part mesh or a vanilla remains mesh via
  ReuseGameFiles) + a recipe `Recipe_Protein<Species>` mirroring the vanilla
  hog/stinger-remains → Alien Protein constructor recipes (1 remains → 1 protein; read the vanilla
  recipe's exact shape in W1.2 and mirror it).
- Rationale: plugs into DNA capsules/sink/consumables with ZERO new economy design, and keeps
  "kill new creature" exactly as rewarding as vanilla hunting. Trophies (crest plates, fang, worm
  core) are pure AWESOME-sink flavor items (sink points via the world module's
  `mResourceSinkItemPointsTable`).
- Registration: loot recipes ride a single hidden-cheap schematic (or direct recipe registration in
  the world module) — decided in W1.2 per the content-registration model (schematic is the loading
  anchor; see skill `content-and-assets.md`).

## 6. Roster expansion parking lot (post-1.0, NOT in scope)

Swamp lurker (water ambush), canyon glider (flying — needs the flying-movement research spike),
broodmother + juvenile swarm (spawner-in-a-creature), "Primal Alpha" rare variants (bigger CDO-set
stat classes — NEVER runtime multipliers, doc 06 §2 explains why).
