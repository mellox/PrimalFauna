# 04 — Config & UI

The user delegated the config-surface decision ("maybe in the ui config, I'll take your advice").
**Decision: a C++ `UModConfiguration` tree (the HostileScaling pattern), rendered on the mod's page
in the in-game Mods menu.** Rationale and the considered alternative in §4.

## 1. The config tree (v1)

All properties `bRequiresWorldReload = false` (live-apply via the subsystem's 10 s refresh tick,
HostileScaling-style). Build the tree **exactly** per the satisfactory-modding skill's
`sml-gotchas.md` §3 — all four traps (BP property classes via `LoadClass`, outer-to-section,
world-reload flags, `DefaultValue` mirrors) apply verbatim.

### Section: General
| Key | Type | Default | Meaning |
|---|---|---|---|
| `Enabled` | Bool | true | Master switch. Off = no nests injected, existing PrimalFauna creatures despawned on next refresh, worm events suppressed. Must be safe to toggle mid-save both ways. |
| `NestDensity` | Float (0.25–3.0, spinbox) | 1.0 | Scales HOW MANY territory nests materialize (doc 03 §5). Deliberately does NOT touch per-spawner creature counts — that is HostileScaling's lane (doc 06 §3). |
| `EnableExperimentalFeatures` | Bool | false | Workspace convention flag (see skill SKILL.md). V1 candidate occupant: Duskstalker flashlight-flee rule. If empty at release, description must say so. |
| `EnableDiagnostics` | Bool | false | Verbose `[PFA]` logging gate (sml-gotchas §3 config-gated-diagnostics pattern: hooks installed unconditionally, bodies gated on a file-static bool pushed from the refresh tick). |

### Sections: per land species (Sicklejaw / Crestback / Duskstalker)
| Key | Type | Default | Meaning |
|---|---|---|---|
| `Enabled` | Bool | true | Species on/off. Off = its nests deactivate + live members despawn on refresh. |
| `Aggression` | Integer 0–2 | Sicklejaw 2, Crestback 1, Duskstalker 2 | 0 = Passive (exists, never initiates; `IsPassiveCreature()` true → HostileScaling correctly skips it, doc 06 §2), 1 = Territorial (initiates only inside territory / when provoked), 2 = Hostile (initiates on detection). Tooltip documents the values — SML has no native enum config property; Integer+tooltip beats a String parse. |
| `DamageMultiplier` | Float (0.25–2.0) | 1.0 | OUR balance knob, applied when *defining* attack damage at the source (doc 07 §6) — composes multiplicatively and independently with HostileScaling's damage hook (theirs multiplies in the health-component entry points; ours sets the outgoing attack's base). |

### Section: Factions (doc 02)
| Key | Type | Default | Meaning |
|---|---|---|---|
| `HuntVanillaHostiles` | Bool | true | Predators may fight vanilla hostile creatures (hogs, stingers, spitters). The creature-vs-creature spectacle switch. |
| `HuntPassiveFauna` | Bool | false | Allow targeting passive wildlife (non-tamed). Default off — protects doggos/ambient fauna. Tamed creatures are NEVER targets; hardcoded, not config. |
| `PackAssist` | Bool | true | Same-species pack members share aggro (doc 02 §4). |

### Section: Sandwyrm (added in its phase; section present-but-minimal before that)
| Key | Type | Default | Meaning |
|---|---|---|---|
| `Enabled` | Bool | true | Worm events on/off. |
| `EventFrequency` | Float (0.25–3.0) | 1.0 | Scales disturbance-score accumulation → time between hunts (doc 03 §7). |
| `DamageBuildings` | Bool | true | Emergence damages factory buildings in the blast radius (modest, config off = worm never harms structures — mirrors HostileScaling's `ScaleDamageToBuildings` philosophy). |
| `RespawnDays` | Integer | 7 | In-game days of regional respite after a kill. |

## 2. Multiplayer semantics (must be logged + tested, not assumed)

All gameplay decisions execute **server-side** (subsystem + controllers run with `HasAuthority()`),
reading the **server's/host's** config — a client's local .cfg has no gameplay effect. That is the
same contract HostileScaling ships with, so users already understand it. Log the active config
snapshot at world load (`[PFA] Config: Enabled=… NestDensity=…`) so MP misconfiguration is
diagnosable from one log. In-game test item: client edits config → nothing changes; host edits →
applies within one refresh tick.

## 3. Config ↔ world-reload interactions

- Everything applies live: enable/disable species, density, aggression — the refresh tick
  reconciles (materialize/deactivate nests idempotently, doc 03 §6; retarget live creatures'
  aggression next decision cycle).
- Schema stability: changing property classes/keys resets stored values once (SML behavior) — warn
  in release notes when it happens; keep keys stable from v1.

## 4. Why ModConfiguration (and the alternative considered)

**Chosen: `UModConfiguration` (C++).**
- Proven on this machine end-to-end (HostileScaling): the three silent config traps are already
  solved in the scaffold; zero editor-asset authoring (matters — our headless pipeline can't build
  UI assets, and editor-manual steps are budgeted for creatures, not settings plumbing).
- One consistent mental model with HostileScaling for the same user base ("mod page in Mods menu").

**Alternative (documented for post-1.0): SML Session Settings** (`USMLSessionSetting` +
`UGameInstanceModule.SessionSettings`) — per-SAVE settings shown in the new-game/session UI,
replicated, admin-gated; strictly more "correct" for gameplay toggles in public MP. Costs:
authored as `UFGUserSetting`-style **editor assets** (breaks headless authoring), a second settings
surface to document, and no precedent in our shipped mods. Graduation path: if MP users ask for
per-save control, move `Enabled` + `Factions` keys to session settings in a minor release and keep
ModConfig for the rest. Not v1.

**Rejected: per-player settings** — hostility is world-state; per-player toggles would desync the
shared world's danger level.

## 5. UI copy standards

Every property gets a Tooltip that states: what it does, its default, when it applies
("takes effect within ~10 s, no reload"). The mod Description (uplugin) must carry the three-line
summary of D5: "Works with HostileScaling automatically; no dependency either way."
