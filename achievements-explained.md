# How Achievements Work, and Where They Are Calculated

**Audience:** anyone integrating the Scorbit SDK against achievements, or picking up the
unmerged achievements module.

**Scope and provenance.** This document has two kinds of statements in it, and the
difference matters:

| Marker | Meaning |
|---|---|
| **[verified]** | Read directly out of this repository at the commit below. Cited with `file:line`. |
| **[reference]** | Asserted by `achievements-platform-reference.pdf` (2026-07-27) about the **server**, the Creator Portal, or the API, and not independently checked. Treat as authoritative-but-unchecked. |
| **[verified: api]** | Read from the `api` repo at `/Users/dbarnett/git/Scorbit/api` (branch `feature/SB-4071-seed-charge-disclosure` @ `55b1131b3`). Cited with `file:line`. |

The server-side rule engine — `Rule.get_current_value`, `Rule.is_satisfied`, the
`AchievementV2` field list, and the scorbitron serializer — has been read directly and is
marked **[verified: api]**. Claims still marked **[reference]** are ones I have not opened the
source for, chiefly the Creator Portal UI, Celery task wiring, and notification templates.

Code references are against **`origin/fix/SB-4063-achievement-group-id-json-key`** at
`0751653`, which is the achievements branch tip (one commit on top of
`origin/feature/v2_achievements_module` at `c42f788`).

---

## 1. The short version

An achievement is a badge attached to a **user profile**, earned by playing pinball.
Definitions and unlock state are **stored centrally**; evaluation is **distributed** —
some achievements are decided on the machine during play, the rest are decided on the
server after the session uploads.

The single most important sentence, from the reference:

> Achievements and their associated logic are **stored** in the Scorbit platform. In most
> cases, they are **processed** by the game or Scorbitron hardware.

Two consequences that drive everything else:

1. **There are two engines, not one.** Which one runs a given achievement is derived from
   its rule types and scope — there is no flag on the record that selects the engine.
2. **The server is always the authority.** The on-machine engine is predictive. It exists
   so the machine can light up a "you're close" indicator and pre-load artwork. Never
   show "unlocked" off the back of a local match — wait for the server's
   `AchievementUnlocked` event.

## 2. Where the code actually is

**[verified]** There is no achievement code on `main`. At `main` (v2.0.4) a
case-insensitive search for `achiev` across the tracked tree returns nothing.

The module lives on a branch:

```
origin/feature/v2_achievements_module              c42f788  2026-02-09
origin/fix/SB-4063-achievement-group-id-json-key   0751653  2026-07-27  (+1 commit)
```

Neither is merged. The feature branch is **6 commits ahead of `main` and 250 behind** it.

Files, branch only:

| Path | Lines | Role |
|---|---:|---|
| `include/scorbit_sdk/achievements.h` | 231 | C++ types: `Achievement`, `AchievementRule`, `AchievementProgress`, callbacks |
| `include/scorbit_sdk/achievements_c.h` | 473 | The C API — 18 functions |
| `source/achievement_manager.h` | 231 | `AchievementManager` — caches + local matching |
| `source/achievement_manager.cpp` | 402 | **The on-device evaluator** |
| `source/achievements_c.cpp` | 459 | C shim over the C++ types |

Because the reference PDF was written against this branch, its `file:line` citations do
not resolve against `main`. For example it cites `source/identifiers.h:38-41` for the
achievement endpoints; on `main` those lines hold diagnostics URLs.

## 3. The two engines

**[reference]** for the routing table and the server column; **[verified]** for
everything attributed to the SDK.

|  | In-session engine | Post-game engine |
|---|---|---|
| Runs on | The machine — SDK inside game code or Scorbitron | API server, Celery worker |
| Implementation | `source/achievement_manager.cpp` | `core/tasks/achievement.py::evaluate_post_game_achievements` |
| When | The moment game code asks, during play | After the session completes and uploads |
| Latency | Immediate | Seconds to minutes, deliberately unbounded |
| Input | Cached definitions + cached user progress + live modes/score | Session record + `Progress.metrics` + existing unlocks |
| Handles | `MODE`, `MODE_START`, `MODE_STACK`, `SCORE` | `ACHIEVEMENT` chains, `PROGRESS` counters, all `global` scope |
| Authority | **Predictive only** | **Authoritative** |

Routing is implicit — derived from the rules, not declared:

| Rule types present | Evaluated where |
|---|---|
| Only `MODE` / `MODE_START` / `MODE_STACK` / `SCORE` | In-session; the game posts the unlock |
| Any `ACHIEVEMENT` rule | Post-game, server |
| Any `PROGRESS` rule | Post-game, server |
| Scope `global` | Post-game, server, regardless of rule types |
| `GAME_CODE` | Neither engine — game code decides and calls unlock explicitly |

The two engines are complementary by construction rather than overlapping: the SDK
**skips** `ACHIEVEMENT` and `PROGRESS` rules during local matching, and the server
resolves the SDK-owned types to `0` so it cannot accidentally grant something it has no
way to measure.

> **[verified: api]** That server-side `0` is a fail-safe that depends on the comparison
> operator. `get_current_value` returns a hard `0` for `GAME_CODE`, `TIMER`, `EVENT`,
> `ATTEMPT`, `MODE_STACK` and `MODE_START` (`api/core/models/rule.py:103-104`), and
> `is_satisfied` compares that against the target (`:106-117`). With the default `>` and a
> target of 1 or more the rule is never satisfied. But `< 5` gives `0 < 5` and `= 0` gives
> `0 == 0` — both **satisfied trivially**, so authoring either on an SDK-owned rule type
> defeats the fail-safe entirely. Nothing in the model, serializer, or Portal rejects it.
> Punch-list K7.

## 4. Anatomy of an achievement

**[reference]** throughout this section, except the SDK struct shape which is
**[verified]** at `include/scorbit_sdk/achievements.h:164-183`.

**Key** — immutable, unique, max 100 chars, lowercase/digits/hyphens:
`{scope}-{context}-{identifier}`, e.g. `game-cv-boom-balloon`,
`event-tpf2025-tournament-winner`. Game code references achievements by key forever.

**Scope** — what it attaches to: `game` (a title, not a machine instance — the common
case), `venue`, `event`, `global`. Note the deliberate v1→v2 change: a `game`-scoped
achievement applies to *Theatre of Magic*, not to one particular ToM cabinet.

**Permanence** — `permanent` (stays on the profile once earned; the common case) or
`trophy` (held by exactly one player at a time, Foursquare-mayor style; winning it takes
it from the previous holder, who is notified). Because achievements are *unlocked*,
"locking" is the reverse — revoking one.

**Visibility** — three independent ideas that get conflated:

- `visible: false` — an *ingredient*. Never appears in player-facing lists. Used for
  intermediate steps in nesting chains.
- `obscure: true` — listed, but name/description/image replaced by a placeholder until
  earned. This is the mystery mechanic.
- `status` — `draft` / `published` / `archived`. An authoring workflow state, not a
  player-facing one. Only `published` reaches players or gets evaluated.

**Session scope** — `is_single_session` decides whether progress resets.
**[verified]** the SDK maps this at `net.cpp:2209-2211`:
`true` → `AchievementInputTime::Limited`, `false` → `Unlimited`.

**Groups and levels** — a tier ladder (100 spins / 1,000 / 10,000). Members share a
`group_id`, ordered by `level`. Levels use sparse integers (default gap 1000) so a tier
can be inserted without renumbering; the API also returns a dense `display_position` for
UI ordering. Groups carry **no evaluation semantics** — they are for display only.

**Nesting** — any achievement can require others first, via an `ACHIEVEMENT` rule
pointing at a `subachievement`. Combined with `visible: false` this produces long
invisible chains that surface only at the final step. The canonical example is *Star Trek
Multiballer*: ten invisible links, each requiring the previous plus one more multiball.

**Counters** — an achievement is either boolean (one completion) or a counter (`count`
completions, driving a progress bar).

## 5. The rule engine

**[verified: api]** An achievement holds one or more **rules**, and they are **always ANDed**.
There is no OR, in either engine, and no schema field that could hold one:

- Server: `all(rule.is_satisfied(user) for rule in rules)` —
  `api/core/managers/achievement_v2.py:599`.
- SDK: `evaluateRulesForMode` returns `false` on the first failing rule —
  `achievement_manager.cpp:161-219`.
- `Rule` has exactly nine fields (`api/core/models/rule.py`) — `id`, `achievement`,
  `subachievement`, `type`, `comparison`, `reference`, `target`, `completed`, `completed_at`.
  No combinator. `AchievementV2` has none either. Adding OR means a migration plus changes to
  both engines.

Multiple rules **of the same type** AND perfectly well, so "complete mode A *and* mode B" is
two `MODE` rules. It is only the disjunction that has no direct expression.

### 5.1.1 How to express "A or B"

> **Correction.** An earlier revision of this document repeated the reference's advice that
> alternatives are "modelled as two achievements in a group, or as a chain of
> sub-achievements." The second half of that is **wrong**, and the first half is weaker than
> it sounds.

| Approach | Does it give one badge for "A or B"? |
|---|---|
| **Two achievements in a group** | **No.** Groups are display-only — they "carry no evaluation semantics" (§4.6). You get *two distinct badges* shown together, and the player earns whichever they qualified for. Fine for tier ladders; not a disjunction. |
| **A chain of sub-achievements** | **No.** `ACHIEVEMENT` rules are ordinary rules and AND like everything else. A chain gives you AND *with ordering*, never OR. |
| **A shared `PROGRESS` metric** | **Yes — this is the one that works.** |

The working mechanism is the metric layer, not the rule layer. `Rule.get_current_value` for a
`PROGRESS` rule reads `Progress.metrics.get(rule.reference, 0)`
(`api/core/models/rule.py:83-88`), and metric keys are **arbitrary caller-supplied strings** —
`Progress.objects.update_metrics(user, game, metrics_delta)` merges any dict (`:640-653`) and
`increment_metric(user, game, metric_key)` bumps any key (`:655-665`), both driven by the REST
endpoint at `api/api/v2/views/achievements.py:935`.

So if two different gameplay paths both write the same metric key, a single rule —
`PROGRESS`, `reference: "beat_the_boss"`, `> 0` — is satisfied by **either**. The disjunction
is expressed by whatever decides to write that key.

**Caveat:** `PROGRESS` rules are server-evaluated post-game, and the SDK skips them during
local matching (§6.4). So a shared-metric OR cannot drive an in-session unlock today — it
resolves after the session uploads. For an in-session disjunction there is currently no
mechanism at all.

The ten rule types:

| Type | Meaning | Evaluated by | Status |
|---|---|---|---|
| `MODE` | A specific mode was completed | SDK, in-session | Implemented |
| `MODE_START` | A specific mode was started | SDK, in-session | Implemented |
| `MODE_STACK` | Multiple modes active simultaneously | SDK, in-session | Implemented |
| `SCORE` | Score reached a threshold | SDK, in-session | Implemented |
| `PROGRESS` | Counter metric on the user/game Progress record | Server, post-game | Implemented |
| `ACHIEVEMENT` | Another achievement must be earned first | Server, post-game | Implemented |
| `GAME_CODE` | Custom logic private to the game | Game code | By design — never auto-unlocks |
| `TIMER` | Elapsed time from a mode's start | — | **Not implemented** |
| `EVENT` | A specific event the Scorbitron matches | — | **Not implemented** |
| `ATTEMPT` | Whether this was the first try during the game | — | **Not implemented** |

Rule fields: `type`, `comparison`, `target`, `reference` (mode name or metric key),
`subachievement`, plus `completed` / `completed_at`.

**Comparison operators are per-rule, not per-achievement** — this is a genuine v2
improvement. `>` / `<` / `=`, default `>`. Because the operator lives on `Rule` rather
than on `Achievement`, a multi-rule achievement can mix them: "score `>` 1,000,000,000
**and** duration `<` 60,000 ms" is two rules.

> **[verified] The SDK does not honour `comparison`.** It is parsed
> (`net.cpp:2222`, defaulting to `">"`) and exposed through the C API
> (`achievements_c.cpp:414`), but it appears nowhere in `achievement_manager.cpp`. The
> local matcher hardcodes `>=`. See the punch-list, item P1 — this is the most
> consequential gap between this document's model and the shipped code.

### Worked examples

**[reference]** *Bride of Pinbot Billionaires Club* — global, permanent, score over a
billion:

```
scope: global · is_trophy: false
rules: [ { type: SCORE, comparison: ">", target: 1000000000, reference: "score" } ]
```

*Theatre of Magic Grand Finale* — game-scoped, mode completed on any ToM machine:

```
scope: game · game: Theatre of Magic
rules: [ { type: MODE, comparison: ">", target: 0, reference: "Grand Finale" } ]
```

*Keith Elwin Lover* — played three specific titles. Modelled as a **chain**, because
rules AND within one game context and this spans three games:

```
global-played-jurassic-park   (invisible)
global-played-iron-maiden     (invisible)
global-played-archer          (invisible)
global-keith-elwin-lover      rules: three ACHIEVEMENT rules, "> 0" each
```

That last example is the key modelling idiom: **cross-game conditions become chains**,
because a single achievement's rules all evaluate within one game's context.

## 6. How the SDK calculates — the actual algorithm

**[verified]** All of §6. `source/achievement_manager.cpp`.

### 6.1 State it holds

Three independently-locked caches, all `mutable std::mutex`
(`achievement_manager.h:217-226`):

| Cache | Shape | Notes |
|---|---|---|
| Definitions | `std::vector<Achievement>` + `std::map<std::string,size_t>` key→index | Rebuilt wholesale by `setAchievements` (:28-39); not persisted across boots |
| Per-user progress | `std::map<int64_t, std::map<std::string, AchievementProgress>>` | Keyed userId → key → progress |
| DMD frames | `LRUCache<std::string, DmdFrame>`, capacity 32 | `MAX_DMD_FRAMES_CACHED`, `achievement_manager.h:36` |

### 6.2 The SDK never evaluates on its own

This is the most important integration fact. `sb_set_score`, `sb_add_mode`,
`sb_remove_mode` and `sb_commit` contain no achievement logic — they only mutate game
state. Evaluation happens **only** when game code calls a `check_*` function. The cadence
of achievement checking is entirely the integrator's choice.

### 6.3 Pre-filter: already unlocked?

`isAlreadyUnlocked` (:145-159) runs first for every candidate. It returns true — meaning
*skip* — only when the achievement is unlocked **and** `inputTime == Unlimited`. So
permanent achievements are skipped once earned; session-limited ones are re-checked.

### 6.4 Mode path

`checkModeAchievements` (:221-226) → `checkModeAchievementsWithScore` (:228-248) →
`evaluateRulesForMode` (:161-219) per candidate.

`evaluateRulesForMode`:

1. **If `ach.rules` is empty**, fall back to the derived flat fields — compare
   `ach.trigger`, `ach.modeName`, and `ach.modeType` against the incoming
   `modeName`/`modeType` string (:164-181).
2. **Otherwise**, walk `ach.rules`:
   - `ACHIEVEMENT` and `PROGRESS` → `continue`. **Skipped, not failed** (:188-190). This
     is what makes the two engines non-overlapping.
   - Set `hasEvaluableRule = true` for everything else (:192).
   - `MODE` / `MODE_START` / `MODE_STACK` → require `rule.reference == modeName`, then
     require the mode type to match the rule type (`MODE`→`"complete"`,
     `MODE_START`→`"start"`, `MODE_STACK`→`"stack"`) (:194-208).
   - `SCORE` → `if (score < rule.target) return false` (:209-213).
   - `GAME_CODE`, `TIMER`, `EVENT`, `ATTEMPT` → fall through, skipped (:215).
3. Return `hasEvaluableRule` (:218). **An achievement whose rules are all skipped never
   matches**, because at least one evaluable rule is required.

Note step 2's `SCORE` handling combined with `checkModeAchievements` passing `score = 0`
(:225): a mode+score achievement can never match through the plain
`checkModeAchievements` entry point. Use `checkModeAchievementsWithScore`. See punch-list
P3.

### 6.5 Score path

`checkScoreAchievements` (:250-302). Same shape, with two differences: it requires
`hasScoreRule` as well as `hasEvaluableRule` before matching (:296), and it does **not**
check mode rules — the comment at :293 says they "can't be evaluated in score-only
context". They are skipped for checking but still set `hasEvaluableRule`. See punch-list
P3.

### 6.6 Counter path

`incrementProgress` (:304-344):

1. Look up the definition; warn and return false if unknown (:306-310).
2. Lazily create the progress entry (:318-322).
3. If already unlocked and not a trophy, return false — no further progress (:325-327).
4. `prog.progress += increment` (:329).
5. Unlock when `prog.progress >= ach.count` (:333-338).
6. Fire the triggered callback (:341), then return whether this call newly unlocked.

> **Step 5 compares against the wrong number, and usually against `1`.** There is no
> achievement-level counter threshold in v2 at all: `AchievementV2` has no `count` field
> (`api/core/models/achievement.py:415-777`) and the scorbitron serializer does not emit one
> (`api/api/v2/serializers/achievement.py:225-247`). v2 puts the threshold on the
> **`PROGRESS` rule's `target`** — confirmed by the server's own tests at
> `api/core/tests/achievement_v2_tests.py:473-580`. The SDK instead fills `ach.count` from
> `ball_count` (`net.cpp:2198`), a nullable "which ball" qualifier that nothing evaluates
> server-side; when it is unset the parser default of `1` applies, so **a counter achievement
> unlocks on its first increment**. Treat `incrementProgress`'s `newlyUnlocked` return as
> unreliable and wait for the server's `AchievementUnlocked` event. See punch-list P5.

> **The callback at step 6 runs while `m_progressMutex` is still held.** The lock is taken
> at `:314` and its scope does not end until `:344`. `m_progressMutex` is a plain
> `std::mutex` (`achievement_manager.h:221`), so a callback that calls back into the
> manager — `getProgress`, `getUserProgress`, `updateProgress`, `incrementProgress`, or
> `sb_get_cached_progress` from the C API — **deadlocks the calling thread**. On a machine
> that is a dead table, not a caught exception. Until this is fixed, treat the triggered
> callback as: copy what you need out of its four arguments, hand off, return. See
> punch-list P4.

### 6.7 Where definitions come from

`Net::fetchAchievements` parses the v2 response at `net.cpp:2193-2254`. The nested `rules`
array is parsed at :2218-2228. Then flat legacy fields are **derived from `rules[0]`
only** (:2231-2251) — `trigger`, `modeType`, `modeName`, `targetScore`.

**[reference]** confirms this derivation is lossy for multi-rule achievements: inspect the
`rules` array, not the flat fields.

Two field mappings worth knowing, both **[verified]**:

- `ach.count` is read from the API's **`ball_count`** (:2198). **[reference]** flags this
  as a semantics mismatch — the API emits no `count` field, and `ball_count` was intended
  as a "complete this before ball N" qualifier, which is a different concept.
- `ach.achievementId` is read from the API's **`level`** (:2206), in a field documented as
  "achievement ID within group". Functional but mislabelled.

> **Rule targets cap at 2,147,483,647 — platform-wide, not just on-device.**
> `AchievementRule::target` is a plain `int` (`achievements.h:153`, mirrored at
> `achievements_c.h:112`). The server's `Rule.target` is a Django `PositiveIntegerField`
> (`api/core/models/rule.py:65`), documented as safe for 0–2,147,483,647 across supported
> backends — **the same ceiling**. So there is no truncation risk on data the server sends;
> the two sides agree. What it does mean is that **no score target above ~2.1 billion is
> expressible anywhere in the system**, which is below what modern machines reach. That is a
> platform limitation for the API team, not an SDK bug. See punch-list P2.

## 7. How the server calculates

**[verified: api]** for the rule engine below; **[reference]** for the Celery trigger chain,
which I have not opened.

`Rule.get_current_value(user)` — `api/core/models/rule.py:81-104` — resolves a rule to a
number:

- `PROGRESS`, `MODE`, `SCORE` → `Progress.metrics[rule.reference]` for that user and
  game, or `0` if no Progress record exists.
- `ACHIEVEMENT` → `1` if the `subachievement` is achieved, else `0`. Raises
  `ImproperlyConfigured` if no subachievement is set.
- `GAME_CODE`, `TIMER`, `EVENT`, `ATTEMPT`, `MODE_STACK`, `MODE_START` → `0`, because
  these are not server-measurable.

`Rule.is_satisfied(user)` then applies the comparison. The post-game trigger path:

```
session completes
  → session_completed signal
  → guard: session.successfully_completed must be true
  → transaction.on_commit(...)
  → evaluate_post_game_achievements.delay(session.id)   [Celery, queue="default"]
      → for each claimed user on the session:
          → published achievements for this game having ACHIEVEMENT rules → check_completion
          → published achievements with scope=global                      → check_completion
             → UserAchievementV2.objects.check_and_unlock(user, achievement, session)
                  → skip if already achieved
                  → skip if the achievement has no rules
                  → all(rule.is_satisfied(user)) → unlock + publish + notify
```

Two guards worth remembering operationally: **an achievement with no rules can never be
auto-unlocked**, and an already-achieved achievement short-circuits immediately.

## 8. End-to-end lifecycles

**[reference]**, except where noted.

**Machine boot.** SDK authenticates and establishes its session → fetches all published
definitions for the machine's game (`GET v2/achievements/scorbitron/`) → caches them
including the nested `rules` → compares the DMD frame ZIP version against its local copy
and downloads only if changed.

**Player claims a slot.** Player taps NFC or claims in the app → SDK fetches that player's
progress (`GET v2/achievements/scorbitron/progress/`) → progress merges with cached
definitions to yield earned / unearned / in-progress → the API subscribes the session to
`achievements:session:{session_uuid}` on Centrifugo.

**In-session unlock.**

1. Game code reports state: `sb_add_mode` / `sb_remove_mode` / `sb_set_score`, then
   `sb_commit`.
2. Game code **asks** what might qualify: `sb_check_mode_achievements`,
   `sb_check_score_achievements`, or the combined mode+score variant. **[verified]** —
   the SDK does not evaluate automatically.
3. For a match, game code calls
   `sb_unlock_achievement(handle, user_id, key, count, cb)`.
4. Server validates, records, publishes to the session channel, queues notifications.
5. SDK receives `AchievementUnlocked` and dispatches it to the game's event callback.
6. Game displays the cached DMD frame — immediately, or deferred to end of ball.

**Post-game.** Session ends and the log uploads → `session_completed` fires post-game
evaluation → newly satisfied chains and global achievements unlock → profile updates, and
the player and their followers are notified.

**Trophy transfer.** A player meets a trophy condition → the client calls
`POST v2/achievements/lock/` to move the trophy → previous holder loses it
(`AchievementLocked` published, holder notified) → new holder gains it
(`AchievementUnlocked` published). Note this is currently **client-initiated**; the
specification's examples imply the server should recompute holders after each session.

## 9. Transport

**[reference]** REST carries durable state; Centrifugo carries live notification.

| Data | Transport | Why |
|---|---|---|
| Achievement definitions | REST | Bulk, cacheable, changes rarely |
| User progress snapshot | REST | Fetched at player claim |
| Unlock / lock requests | REST | Must be reliable and idempotent |
| DMD frames | REST (zip) | Large binary, cached on device |
| Unlock / lock / progress events | Centrifugo | Low latency, fire-and-forget acceptable |

The four endpoints the SDK actually calls:

| Method | Path | Purpose |
|---|---|---|
| `GET` | `v2/achievements/scorbitron/` | All published achievements for the device's game, nested rules and frame URLs, one response |
| `GET` | `v2/achievements/scorbitron/progress/` | The claimed user's progress |
| `POST` | `v2/achievements/unlock/` | Unlock or update counts, keyed by achievement key |
| `POST` | `v2/achievements/lock/` | Revoke a trophy from its holder |

Real-time channel: `achievements:session:{session_uuid}`, one per game session. Event
types are `achievement_unlocked`, `achievement_locked`, `achievement_progress`. The SDK
converts each into a typed event at `EventPriority::High` and delivers it through the
event manager to the game's callback.

Client guidance from the reference, worth heeding:

- Callbacks must return fast. Queue artwork loading and animation; never block the event
  loop.
- After a reconnect you may receive events for unlocks that happened while disconnected —
  **deduplicate on `key` + `user_id`**.
- Both a progress event at 100% and an unlock event will arrive for the same achievement.
  Show one celebration, not two.
- `session_uuid` on the unlock request is what makes an unlock visible in real time.
  Without it the unlock is recorded but no Centrifugo event is published.

## 10. The SDK surface — read this before integrating

**[verified].**

| Language | Achievement support |
|---|---|
| **C** | Complete — 18 functions in `include/scorbit_sdk/achievements_c.h` |
| **C++** | **Types only.** `achievements.h` defines structs, enums and callback aliases. The public `GameState` class in `game_state.h` has **no achievement methods** — confirmed by search. |
| **Python** | **None.** No achievement bindings exist in `wrappers/python/`. |

**Integrate against the C API.** This is a live discrepancy with the SDK documentation,
which shows C++ and Python examples throughout — including a `gameState.fetchAchievements(...)`
call in the `achievements.h` header comment itself (:52) that does not compile.

The 18 functions, grouped:

- **Network** — `sb_fetch_achievements`, `sb_fetch_achievement_progress`,
  `sb_unlock_achievement`, `sb_lock_achievement`
- **Cache access** — `sb_has_achievements`, `sb_get_cached_achievements_count`,
  `sb_get_cached_achievement_at`, `sb_get_cached_achievement`, `sb_get_cached_progress`,
  `sb_achievement_get_rules_count`, `sb_achievement_get_rule_at`
- **Local matching** — `sb_check_mode_achievements`, `sb_check_score_achievements`,
  `sb_increment_achievement_progress`, `sb_set_achievement_triggered_callback`
- **DMD frames** — `sb_download_achievement_frames`, `sb_has_dmd_frame`,
  `sb_get_dmd_frame`

Events arrive through the general event callback set on `Config` **before** the
`GameState` is created, then decoded with `sb_event_achievement_unlocked` / `_locked` /
`_progress` from `event_helpers_c.h`.

## 11. What is specified but not built

**[reference]** §14 of the reference documents fourteen items in full so the design intent
is not lost. The ones most likely to affect an integrator:

- **`TIMER` / duration** — the rule type and `duration` field exist and are editable in
  the Portal. Nothing evaluates either. Timing is inherently in-session, so the SDK is the
  natural owner: it would need a mode-start timestamp map and a new matching entry point.
  This blocks the PRD's flagship example, "maintain multiball for 60 seconds".
- **`EVENT` and `ATTEMPT`** — types defined, nothing evaluates them.
- **Ball count** — stored, editable, migrated from v1, but no rule type references it and
  no engine evaluates it. Meanwhile the SDK maps `ball_count` onto the counter threshold,
  which is a different concept.
- **Venue / event scope enforcement** — delivery is filtered by venue, but the unlock path
  does not verify the player was actually there. Venue scoping is a delivery filter, not a
  guarantee.
- **Relative targets** — "highest score among players" is not expressible; `target` is a
  scalar compared with `>` / `<` / `=`.
- **Automatic trophy recomputation** — the transfer *mechanism* exists; the server
  deciding on its own that a transfer should happen does not.
- **Additional real-time channels** — only the per-session channel exists. Per-user,
  per-machine and per-game were specified.
- **Animated DMD frames** — static frames only. Badge artwork (`icon`) already supports
  GIF and Lottie; DMD frames do not.

## 12. Gotchas for integrators

Consolidating the traps, **[verified]** unless marked:

1. **Nothing happens unless you ask.** No `check_*` call, no local matching.
2. **A local match is not an unlock.** Post the unlock, then wait for the
   `AchievementUnlocked` event before celebrating.
3. **Use `checkModeAchievementsWithScore` for anything with a score rule.** The plain
   mode variant passes `score = 0` and will never match a mode+score achievement.
4. **Don't trust `comparison` to be applied on-device.** The local matcher ignores it and
   behaves as `>=`. A `<` rule matches backwards locally. (P1)
5. **Never re-enter the manager from the triggered callback.** It runs with
   `m_progressMutex` held, and that mutex is not recursive — calling `getProgress`,
   `updateProgress`, `incrementProgress` or `sb_get_cached_progress` from inside it
   deadlocks the game thread. Copy the four arguments, hand off, return. (P4)
6. **Counter achievements unlock on the first increment.** The threshold the SDK compares
   against (`ach.count`, from `ball_count`) is not where v2 stores it — that's the `PROGRESS`
   rule's `target`. Ignore `incrementProgress`'s return value and wait for the server. (P5)
7. **No score target above 2,147,483,647 is expressible** anywhere in the platform — SDK and
   server share that ceiling. Not a truncation risk, just a limit to design around. (P2)
8. **Don't trust the flat convenience fields** (`trigger`, `modeName`, `modeType`,
   `targetScore`) for multi-rule achievements — they are derived from `rules[0]` only.
   Read `rules`.
9. **An achievement with a `GAME_CODE` rule will never auto-unlock** and must be unlocked
   explicitly by the game. **[reference]**
10. **Session-limited progress does not reset on-device.** `clearUserProgress` and
    `clearAllProgress` exist but have no callers anywhere on the branch, so
    `is_single_session` is unenforced in the SDK.
11. **`group_id` was inert until 2026-07-27.** The SDK read JSON key `groupid`; the v2 API
    emits `group_id`, so `Achievement::groupId` was always `0`. Fixed on
    `fix/SB-4063-achievement-group-id-json-key` (`0751653`).
12. **Deduplicate events on `key` + `user_id`** after a reconnect. **[reference]**
13. **Pass `session_uuid` on unlock** or the unlock is silent — recorded, but no real-time
    event. **[reference]**

For known defects and the work needed to close them, see
`achievements-sdk-punchlist.md`.
