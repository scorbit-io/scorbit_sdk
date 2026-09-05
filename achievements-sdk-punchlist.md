# Achievements SDK — Punch-list

**Reviewed:** `origin/fix/SB-4063-achievement-group-id-json-key` @ `0751653`
(= `origin/feature/v2_achievements_module` @ `c42f788` + the SB-4063 fix).
**Review date:** 2026-07-30. **Reviewer:** code read of the branch, cross-checked against
`achievements-platform-reference.pdf` (2026-07-27).

**How to read this.** Items **P1–P4** are new findings from this review, each verified in
branch source and **not** listed in the reference's own defect register (§16). Items
**K1–K7** map one-to-one onto that register's seven defects, restated with my verification
status so you know which are still live — keep the numbering aligned if you edit. **K8** is
new here, but filed in the K series because the reference already scopes its category
("dead JSON keys", Epic 0) even though the specific audit is not in the register.
**M1–M2** are merge-path items.

Line numbers are against `0751653`. The SB-4063 fix touched only `source/identifiers.h`,
so `achievement_manager.cpp` and `net.cpp` line numbers hold on both branch tips.

**Server side now verified.** An earlier revision of this document said server behaviour
could not be checked. That was wrong — the `api` repo is at
`/Users/dbarnett/git/Scorbit/api` (branch `feature/SB-4071-seed-charge-disclosure` @
`55b1131b3`), just not in the SDK working directory. Server claims below are read from that
source and cited. Verifying it **overturned two of this document's own findings** — see the
correction notes on P2 and K5.

---

## Summary

| ID | Severity | Item | Status |
|---|---|---|---|
| P1 | **High** | Local matcher ignores `rule.comparison`; hardcodes `>=` | New — server behaviour now confirmed |
| P5 | **High** | Counter unlock threshold has no server-side counterpart | **New, found on re-verification** |
| P3 | **Medium** | `check*` entry points mis-handle mixed mode+score rules | New |
| P4 | **Medium** | Triggered callback invoked while holding `m_progressMutex` | New |
| P2 | ~~High~~ **Low** | `rule.target` is 32-bit | **Fixed** — widened to `int64_t`; server side still 32-bit |
| K1 | — | `groupid` vs `group_id` | **Fixed** on this branch |
| K2 | **High** | Zero SDK achievement test coverage (server has 3,016 lines) | Confirmed live |
| K3 | **Medium** | Achievements are C-only | Confirmed live |
| K4 | **Medium** | Session-limited progress never resets on-device | Confirmed live |
| K5 | Low | `ball_count` mapped onto `count` | Confirmed live — **superseded by P5** |
| K6 | Low | `level` stored in `achievementId` | Confirmed live |
| K7 | **Medium** | Comparison operators can defeat the server fail-safe | **Now verified in server source** |
| K8 | Low | 15 of 40 `JKEY_ACH_*` constants dead | Confirmed live |
| M1 | **High** | Branch is 250 commits behind `main` | Merge blocker |
| M2 | Medium | Reference doc cites lines that don't resolve on `main` | Doc hygiene |

---

## P1 — Local matcher ignores `rule.comparison`

**Severity: High.** Silent wrong answers, worst on `<` rules.

**Evidence.** `comparison` is parsed with a `">"` default at `source/net.cpp:2222` and
re-exported through the C API at `source/achievements_c.cpp:414`. It appears **nowhere** in
`source/achievement_manager.cpp` — verified by exhaustive grep across
`achievement_manager.cpp`, `achievements_c.cpp`, `net.cpp`, `achievements.h`,
`achievements_c.h`. The only hits are the parse, the C re-export, and two field
declarations.

The matcher hardcodes greater-than-or-equal in both score paths:

```cpp
// achievement_manager.cpp:211-213 (mode path) and :288-291 (score path)
if (score < rule.target) {
    return false;              // ⇒ passes when score >= target
}
```

**Server behaviour now verified**, so the mismatch below is source-to-source rather than
source-to-document. `Rule.is_satisfied` (`api/core/models/rule.py:106-117`) applies a strict
comparison — `current > self.target` for `GT` (`:111`), `<` for `LT` (`:113`), `==` for `EQ`
(`:115`) — with `GT` as the model default (`:63`). And `get_current_value` (`:81-104`) resolves
`MODE` and `SCORE` through `Progress.metrics` (`:83-88`), so a `SCORE` rule genuinely **is**
re-evaluated server-side with strict `>`. The off-by-one row below is reachable, not
theoretical.

**Impact.** Three distinct wrong behaviours:

| Authored rule | Server (verified) | SDK local match |
|---|---|---|
| `score > 1000000` | satisfied above 1,000,000 | satisfied **at** 1,000,000 — off by one |
| `duration < 60000` | satisfied below 60,000 | satisfied **at or above** 60,000 — **backwards** |
| `count = 5` | satisfied at exactly 5 | satisfied at 5 **or more** |

The `<` case is the serious one. Any "complete it in under N" achievement matches locally
under exactly the conditions where it should not. The server refuses the unlock, so no bad
badge is granted — but the machine will light the indicator and the game may fire artwork
and sound for an achievement the player did not earn.

This also blocks `TIMER` (reference §14.2), whose whole point is a `<` duration
comparison. Implementing `TIMER` in the SDK without fixing this ships an evaluator that
gets the flagship example — "maintain multiball for 60 seconds" — exactly wrong.

**Fix.** Add a comparison helper and route every threshold test through it:

```cpp
namespace {
bool satisfies(int64_t value, const std::string &comparison, int64_t target)
{
    if (comparison == "<") return value < target;
    if (comparison == "=") return value == target;
    return value > target;   // ">" and the documented default
}
}
```

Then replace `if (score < rule.target) return false;` with
`if (!satisfies(score, rule.comparison, rule.target)) return false;` at both sites.

Note this changes `>` from `>=` to a true `>`, matching the server. That is a **behaviour
change for existing content** — any score achievement currently firing locally at exactly
`target` will stop. Worth a deliberate decision plus a note to content authors rather than
a silent flip.

**Effort.** ~20 lines plus tests. The tests are the real work — see K2.

---

## P5 — The counter unlock threshold has no server-side counterpart

**Severity: High.** Every counter achievement unlocks locally at the wrong threshold —
usually on the first increment. Found while verifying K5 against the API; it supersedes K5.

**Evidence — the server has no achievement-level counter threshold.**

- `AchievementV2` (`api/core/models/achievement.py:415-777`) has **no `count` field**. Its
  complete field list is `name`, `key`, `description`, `is_active`, `is_badge`,
  `is_single_session`, `is_trophy`, `game`, `venue`, `event`, `scope`, `visible`, `obscure`,
  `group_id`, `level`, `frame`, `frame_image`, `frame_version`, `status`, `draft`, `creator`,
  `users`, `notify_when_achieved`, `ball_count`, `duration`, `created_at`, `updated_at`.
- `ScorbitronAchievementSerializer` (`api/api/v2/serializers/achievement.py:225-247`) emits
  `ball_count`, and **no `count`**.
- The `count` fields that do exist belong to **v1**: `Achievement.count` (`:118`) and
  `UserAchievement.count` (`:339`).

**So v2 expresses a counter threshold as a `PROGRESS` rule's `target`,** not as a field on
the achievement. Confirmed by the server's own tests, which build counters as
`Rule(type=Rule.PROGRESS, comparison=..., target=N)` — `core/tests/achievement_v2_tests.py:473`,
`:490`, `:514`, `:542`, `:573`. The *user's* running value is
`UserAchievementV2.current_value` (a `FloatField`, `:795`); the threshold lives only on the
rule.

**What the SDK does instead.** `net.cpp:2198` populates `ach.count` from `ball_count`, and
`incrementProgress` unlocks on `prog.progress >= ach.count`
(`achievement_manager.cpp:333`). So the threshold is read from a field that means "which
ball" — and `ball_count` is nullable, so when it is unset the parser's default of `1` applies
and **the achievement unlocks on the very first increment.**

`ball_count` is never evaluated by any rule logic server-side either — every reference is
Portal authoring (`core/pages/creator.py:442`, `:861`, `:1196`, `:1324`) or a serializer
field list. It is stored, editable, migrated, and inert, exactly as reference §14.5 says.

**Impact.** A "shoot 100 ramps" achievement authored as `PROGRESS target=100` unlocks
locally after one ramp. The server refuses it, so no badge is granted — but this is the same
false-celebration class as P1 and P3, and it is the default path for **every** counter
achievement rather than an edge case. It also makes `sb_increment_achievement_progress`
misleading: its return value means "newly unlocked" and is essentially always `true` on the
first call.

**Fix.** Take the threshold from the achievement's `PROGRESS` rule instead of `ach.count`:
locate the `PROGRESS` rule for the metric being incremented and compare against its `target`
using the P1 comparison helper. Two things this collides with, both needing a decision:

1. **The SDK currently skips `PROGRESS` rules entirely** during local matching
   (`achievement_manager.cpp:188-190`), because the routing table assigns them to the server.
   Counter achievements are the case where the SDK *does* hold the value locally
   (`prog.progress`), so the skip is too broad. Narrow it: skip `PROGRESS` when matching
   modes and scores, honour it in `incrementProgress`.
2. **`rule.reference` is the metric key.** `incrementProgress` takes an achievement key, not
   a metric key, so the mapping from "which counter am I bumping" to "which rule governs it"
   needs defining. The reference's Epic 1 metric vocabulary
   (`mode:<name>:count`, `target:<key>:count`, `achievement:<key>:trigger_count`) is the
   intended namespace.

Until that lands, the honest short-term fix is to **stop pretending**: have
`incrementProgress` track progress and fire the progress callback but never claim
`newlyUnlocked`, leaving the unlock decision to the server. That is strictly better than
unlocking on increment one.

**Supersedes K5.** K5 described this as "`ball_count` mapped onto `count`, needs a semantics
decision." That was too mild, and my follow-up hypothesis — that `JKEY_ACH_COUNT` being
defined meant the API emits `count` and K5 was a one-constant swap — is **disproved above**.
There is no `count` to read.

---

## P3 — Mixed mode+score achievements are mis-evaluated at both entry points

**Severity: Medium.** One false-negative path, one false-positive path.

### P3a — `checkModeAchievements` can never match a mode+score achievement

`checkModeAchievements` delegates with a hardcoded score of zero:

```cpp
// achievement_manager.cpp:221-226
std::vector<std::string> AchievementManager::checkModeAchievements(
        const std::string &modeName, const std::string &modeType, int64_t userId) const
{
    return checkModeAchievementsWithScore(modeName, modeType, userId, 0);
}
```

`evaluateRulesForMode` then tests any `SCORE` rule against that zero (`:209-213`), which
fails for every positive target. So an achievement with both a `MODE` and a `SCORE` rule
matches only through `checkModeAchievementsWithScore`.

This is arguably intended — there are two entry points for a reason — but nothing in the
headers says so. `achievement_manager.h:140-142` documents `checkModeAchievements` with no
mention that score rules will fail, and the C API mirrors that. An integrator calling the
obvious function gets silence.

**Fix.** Cheapest correct option: document it on both the C++ and C declarations, and have
`checkModeAchievements` log at debug level when it rejects a candidate solely on a `SCORE`
rule. Better option: drop the zero-score overload and make the score parameter explicit at
the single entry point.

### P3b — `checkScoreAchievements` ignores mode rules but counts them as evaluable

```cpp
// achievement_manager.cpp:280-298
for (const auto &rule : ach.rules) {
    if (rule.type == "ACHIEVEMENT" || rule.type == "PROGRESS") {
        continue;
    }
    hasEvaluableRule = true;          // ← set for MODE rules too

    if (rule.type == "SCORE") {
        hasScoreRule = true;
        if (score < rule.target) { allSatisfied = false; break; }
    }
    // Mode/other rules can't be evaluated in score-only context — skip
}

if (hasEvaluableRule && hasScoreRule && allSatisfied) {
    matched.push_back(ach.key);
}
```

A `MODE` rule sets `hasEvaluableRule` and is then never checked. So "complete Grand Finale
**and** score over a billion" is reported as matched by `checkScoreAchievements` the moment
the score crosses the threshold — whether or not the mode was ever completed.

The AND semantics that both engines are documented to share (reference §5.1) are broken
here: an unevaluable-in-this-context rule is treated as satisfied rather than as a reason
to withhold the match.

**Impact.** Bounded by the server, which refuses the unlock. But it is a false "you're
close" on the machine, and it is the same failure class as P1 — the game may fire
presentation for something unearned.

**Fix.** Track skipped-but-required rules and withhold the match. Concretely: add a
`hasUnevaluableRule` flag set for rule types this context cannot judge, and require
`!hasUnevaluableRule` alongside the existing conditions. That makes both `check*` functions
conservative in the same direction — they report only what they can fully verify — which is
the right bias for a predictive engine.

Consider instead a single `checkAchievements(modes, score, userId)` taking the full live
state, so no rule is ever unevaluable for context reasons. That is the larger refactor but
it removes the whole class of bug.

---

## P4 — Triggered callback invoked while holding `m_progressMutex`

**Severity: Medium.** Deadlock on a re-entrant callback.

**Evidence.** `incrementProgress` takes the progress lock at `achievement_manager.cpp:314`
and calls the user callback at `:341`, still inside the lock's scope (which ends at `:344`):

```cpp
std::lock_guard lock(m_progressMutex);          // :314
...
notifyTriggered(key, userId, newlyUnlocked, prog.progress);   // :341 — lock still held
return newlyUnlocked;
}                                                              // :344 — lock released
```

`m_progressMutex` is a plain `std::mutex` (`achievement_manager.h:221`), which is not
recursive. So a callback that calls back into the manager — `getProgress`, `getUserProgress`,
`updateProgress`, or `incrementProgress` again — deadlocks the calling thread immediately.

Reaching back into the manager from that callback is a natural thing to write: the callback
signature (`achievement_manager.h:48-49`) hands you `key`, `userId`, `isUnlock` and
`progress`, so wanting the full `Achievement` or the progress record to render a
notification is the obvious next step. `sb_get_cached_progress` is right there in the C API.

**Impact.** Hangs the game thread. On a pinball machine that is a dead table, not a caught
exception.

**Fix.** Copy what the callback needs, release the lock, then notify:

```cpp
bool newlyUnlocked = false;
int progressSnapshot = 0;
{
    std::lock_guard lock(m_progressMutex);
    ... // existing mutation
    progressSnapshot = prog.progress;
}                                    // lock released here
notifyTriggered(key, userId, newlyUnlocked, progressSnapshot);
return newlyUnlocked;
```

Also worth auditing the lock ordering while in here. `incrementProgress` acquires
definitions-then-progress (via `getAchievement` at `:306`, which releases before
returning), and `checkModeAchievementsWithScore` / `checkScoreAchievements` acquire
`m_achievementsMutex` then `m_progressMutex` (`:234-235`, `:255-256`). Consistent today, so
there is no deadlock between them — but it is undocumented, and the next person to add a
method has nothing telling them the order. Add a comment on the member declarations.

---

## P2 — `rule.target` is 32-bit — **FIXED on the SDK side** (was downgraded to Low; original premise was wrong)

> **Resolution.** `target` is now `int64_t` in `AchievementRule` and `sb_achievement_rule_t`,
> parsed as `int64_t` in both `parseAchievements()` and the debug-seed path, and compared as
> `int64_t` by `satisfies()` — so the value is never narrowed anywhere in the SDK. The decision
> was deliberate despite the C ABI break below: a `"SCORE"` rule's target *is* a pinball score,
> and the type should say so rather than encode today's server limit. Regression tests cover a
> ten-billion target through both the parser and the matcher.
>
> The remaining ceiling is entirely server-side: `Rule.target` is still a 32-bit
> `PositiveIntegerField`, so nothing above 2,147,483,647 can be authored yet. That is the API
> team's item; the SDK will no longer need touching when it lands.


> **Correction.** This item was filed as High severity on the premise that "the server accepts
> targets the SDK cannot represent," and asserted that Django's `PositiveIntegerField` is
> 32-bit *unsigned* (max 4,294,967,295). **Both were wrong.** Django documents
> `PositiveIntegerField` as safe for **0 to 2,147,483,647** across all supported backends,
> and `core/migrations/0001_initial.py` is the only migration touching `rule.target` — it is
> never widened. So the server ceiling is `INT32_MAX`, **identical** to the SDK's `int`.
>
> The server therefore *cannot* store a target the SDK cannot hold, and no truncation of
> server-sent data is possible. There is no SDK defect here.
>
> What remains is a **platform-wide limitation**, not an SDK one: no score target above
> 2,147,483,647 is expressible anywhere in the system — below what modern machines reach.
> That belongs to the API team, and it is the reason the reference's own example tops out at
> exactly one billion. Kept in this list only so the ceiling is written down somewhere.

**Severity: Low.** No data loss in practice; a shared platform ceiling.

**Original evidence (still accurate as far as the SDK types go).**

- `AchievementRule::target` is `int` — `include/scorbit_sdk/achievements.h:153`.
- The C mirror is also `int` — `include/scorbit_sdk/achievements_c.h:112`.
- Parsed into that `int` at `source/net.cpp:2223`.
- `ach.targetScore` is `int64_t` (`achievements.h:179`) but is assigned **from the
  already-truncated `int`** at `source/net.cpp:2247`, so the wider type buys nothing.
- Comparisons take `int64_t score` against `int rule.target` — `achievement_manager.cpp:162`,
  `:230`, `:251` — so the target is sign-extended after truncation, not before.

**Impact.** `INT32_MAX` is 2,147,483,647. Pinball scores routinely exceed that; on modern
Sterns a good game passes two billion regularly, and billion-point thresholds are exactly
the kind of milestone achievements are written for. A target of 3,000,000,000 does not
saturate — `nlohmann::json`'s `value<int>()` narrows it, so it lands as a small or negative
number and the achievement matches almost immediately.

The reference's own worked example, *Bride of Pinbot Billionaires Club* at 1,000,000,000,
fits with ~1.1 billion of headroom. So the field passes every plausible hand-test and fails
on the first ten-billion-point target someone authors.

**Verified server-side:** `Rule.target = models.PositiveIntegerField()` —
`api/core/models/rule.py:65`. Ceiling 2,147,483,647, matching the SDK exactly.

**Fix as applied.** `target` widened to `int64_t` in `achievements.h` and `achievements_c.h`,
with the JSON fallbacks changed to `int64_t {0}` so `nlohmann` deduces the wide type instead of
`get<int>()`. This *is* an **ABI break** on the C API — the struct's layout changes — so C
consumers must be rebuilt against the new header. `ach.targetScore` no longer narrows on its way
in, which resolves the second half of this item.

---

---

## Known defects from the reference, with verification status

### K1 — `groupid` vs `group_id` — **FIXED**

Was: `JKEY_ACH_GROUP_ID` was defined as the v1-era `"groupid"`; the v2 serializer emits
`"group_id"`; `nlohmann::json::value()` returns the default rather than throwing, so
`Achievement::groupId` was silently always `0` and on-device grouping was inert.

Fixed by `0751653` on `origin/fix/SB-4063-achievement-group-id-json-key` — one constant in
`source/identifiers.h`, no call-site or ABI change. **Verified fixed.** Tracked as SB-4063 /
PR scorbit_sdk#177. This branch is the only place the fix exists; it is not on `main` or on
the feature branch.

### K2 — Zero achievement test coverage — **live**

**Verified.** `tests/test_detail/source/` holds 17 test files
(`test_game_state.cpp`, `test_modes.cpp`, `test_net.cpp`, `test_lru_cache.cpp`,
`test_worker.cpp`, …). None reference achievements; a case-insensitive grep for `achiev`
across `tests/` returns nothing on the branch.

This is the highest-leverage item on the list. P1, P3 and P5 are all exactly the kind of
defect a table-driven test over `(rules, state) → expected match` catches on the first run,
and all three survived to a review-by-reading instead. Any fix without tests is one refactor
away from regressing.

**There is a ready-made oracle.** The server has **3,016 lines** of achievement tests —
`api/api/v2/tests/achievement_tests.py` (2,420) and `api/core/tests/achievement_v2_tests.py`
(596) — against the SDK's zero. Since both engines are specified to share AND semantics and
the same comparison operators, the server's rule-evaluation cases can be transcribed directly
into SDK test vectors. Where the two engines are *supposed* to differ (the skip rules), that
difference becomes an explicit assertion rather than an assumption. Start by porting the
`PROGRESS`/comparison cases at `core/tests/achievement_v2_tests.py:473-580` — those are the
ones that expose P5.

Minimum useful suite:

- **Rule parsing** — `net.cpp:2193-2254` against captured v2 fixtures: all ten rule types,
  multi-rule achievements, missing/extra keys, and the derived-flat-field logic. This is
  also the regression test K1 never had.
- **Comparison matrix** — every rule type × `>` / `<` / `=` × below/at/above target. This is
  the P1 test.
- **Boundary values** — targets at and beyond `INT32_MAX`. This is the P2 test. **Done:**
  `test_achievement_json.cpp` and `test_achievement_manager.cpp` both carry a ten-billion case.
- **Context isolation** — mode-only, score-only and mode+score achievements through each of
  the three `check*` entry points. This is the P3 test.
- **Skip semantics** — that `ACHIEVEMENT` / `PROGRESS` rules are skipped rather than failed,
  and that an all-skipped achievement never matches.
- **Counter path** — `incrementProgress` across the threshold, the trophy exception at
  `:325-327`, and a re-entrant callback (which currently deadlocks — this is the P4 test).
- **`isAlreadyUnlocked`** — `Limited` re-checks, `Unlimited` skips.

### K3 — Achievements are C-only — **live**

**Verified.** `include/scorbit_sdk/game_state.h` on the branch contains no `achiev` match at
all, so the public C++ `GameState` has no achievement methods. `wrappers/python/` has no
achievement bindings.

The documentation problem is worse than the gap itself: `achievements.h:50-84` is a
`@code{.cpp}` block showing `gameState.fetchAchievements(...)`,
`gameState.checkModeAchievements(...)`, `gameState.incrementProgress(...)` — none of which
exist. That header comment ships to integrators and does not compile. **[reference]** notes
`sdk-017`–`sdk-025` document all three languages.

Either build the C++ facade or fix the header comment to show the C API. The current state
actively misleads.

### K4 — Session-limited progress never resets on-device — **live**

**Verified.** `clearUserProgress` and `clearAllProgress` are defined
(`achievement_manager.cpp:131`, `:137`; declared `achievement_manager.h:124`, `:129`) and
have **no callers anywhere on the branch** — a repo-wide grep across `source/` and
`include/` returns only the definitions and declarations.

So `is_single_session` / `AchievementInputTime::Limited` is parsed
(`net.cpp:2209-2211`), stored, and honoured by `isAlreadyUnlocked` in the sense that
`Limited` achievements are re-checked — but the accumulated counter never resets. A
`Limited` counter achievement therefore behaves as lifetime on-device: progress from
previous sessions carries forward and it unlocks early.

**Fix.** Call `clearUserProgress` when a player leaves a session and `clearAllProgress` on
game end / session teardown. The right hook is in the session lifecycle rather than in
`AchievementManager` itself; worth checking where `PlayerProfilesManager` clears its own
state and matching that.

### K5 — `ball_count` mapped onto `count` — **live**

**Verified.** `net.cpp:2198` reads `ach.count = j.value(JKEY_ACH_BALL_COUNT, 1)`.

**[reference]** the API emits no `count` field, and `ball_count` was specified as a
"complete this before ball N" qualifier (§14.5) — a different concept from a counter
threshold. So the counter threshold is currently populated from a field that means
something else, with a default of 1.

> **Superseded by P5, and my hypothesis here was wrong.** An earlier revision argued that
> because `JKEY_ACH_COUNT {"count"}` is defined in `source/identifiers.h` but never read,
> the API probably *does* emit `count` and this item would collapse to a one-constant swap.
> **Checked, and no.** `AchievementV2` has no `count` field and
> `ScorbitronAchievementSerializer` does not emit one — see P5 for the citations. The
> constant is dead because there is nothing to read. The reference document was right and I
> was wrong.
>
> The real problem is larger than a field name: v2 has **no achievement-level counter
> threshold at all** — it lives on the `PROGRESS` rule's `target`. Work this as **P5**.

Note the parser's default of `1` means boolean achievements currently work by accident:
absent `ball_count`, `count = 1`, and `incrementProgress` unlocks on the first increment. Any
fix must preserve boolean behaviour while correcting counters.

### K6 — `level` stored in `achievementId` — **live**

**Verified.** `net.cpp:2206` reads `ach.achievementId = j.value(JKEY_ACH_LEVEL, 0)` into a
field documented as "Achievement ID within group" (`achievements.h:181`). Functional but
mislabelled. Rename the member, or fix the comment — with K1 freshly fixed next door, the
grouping fields deserve one consistent pass.

### K7 — Comparison operators can defeat the server fail-safe — **VERIFIED**, severity raised

Previously listed as unverifiable. Now confirmed in server source, and it is worse than
"documented concern" — it is a live authoring hazard with no guard.

`get_current_value` returns a hard `0` for the SDK-owned types —
`GAME_CODE`, `TIMER`, `EVENT`, `ATTEMPT`, `MODE_STACK`, `MODE_START` — at
`api/core/models/rule.py:103-104`. `is_satisfied` then compares that `0` against the target
(`:106-117`). So:

| Authored rule on an SDK-owned type | `get_current_value` | Satisfied server-side? |
|---|---|---|
| `> 0` (the default) | `0` | No — fail-safe works |
| `< 5` | `0` | **Yes, trivially** — `0 < 5` |
| `= 0` | `0` | **Yes, trivially** — `0 == 0` |

Both `LT` and `EQ` are freely selectable in the Creator Portal for these rule types, and
nothing in `Rule.clean()`, the serializer, or the Portal form rejects the combination. An
author can therefore create an achievement that the server grants to **anyone whose session
is evaluated**, with no gameplay condition met at all — the opposite of the fail-safe's
intent.

**Fix (API/Portal side, not SDK).** Validate on `Rule`: reject `LT`, and reject `EQ` with
`target = 0`, when `type` is one of the six SDK-owned types. Add it as a model-level
`clean()` plus a serializer validator so both the Portal and the API reject it.

**Sequencing matters here.** This must land **with or before P1**. Today the SDK ignores
`comparison`, which accidentally masks the hazard on-device. Once P1 makes the SDK honour it,
an `= 0` rule matches *both* engines trivially — so fixing P1 without this validation makes
the exposure worse, not better.

### K8 — 15 of 40 `JKEY_ACH_*` constants are dead — **live**, low severity

**Verified** by auditing every `JKEY_ACH_*` constant in `source/identifiers.h` against reads
in `source/net.cpp`. Fifteen are never read:

| Constant | Wire key | Why it's dead |
|---|---|---|
| `JKEY_ACH_COUNT` | `"count"` | **See K5** — the correct key, unwired; `ball_count` is read instead |
| `JKEY_ACH_ACHIEVEMENT_ID` | `"achievementid"` | v1 unsuffixed residue. Same spelling pattern as the K1 bug, but harmless because nothing reads it |
| `JKEY_ACH_INPUT_TIME` | `"input_time"` | Superseded by `is_single_session` |
| `JKEY_ACH_TRIGGER` | `"trigger"` | v1 flat field; v2 derives from `rules` |
| `JKEY_ACH_MODE_NAME` | `"mode_name"` | ditto |
| `JKEY_ACH_MODE_TYPE` | `"mode_type"` | ditto |
| `JKEY_ACH_TARGET_SCORE` | `"target_score"` | ditto |
| `JKEY_ACH_PROGRESS` | `"progress"` | v1 progress shape; v2 reads `current_value` |
| `JKEY_ACH_UNLOCKED` | `"unlocked"` | v1; v2 reads `achieved` |
| `JKEY_ACH_UNLOCKED_AT` | `"unlocked_at"` | v1; v2 reads `achieved_time` |
| `JKEY_ACH_IMAGE` | `"image"` | superseded by `icon` |
| `JKEY_ACH_SCOPE` | `"scope"` | never consumed — see note below |
| `JKEY_ACH_IS_BADGE` | `"is_badge"` | never consumed |
| `JKEY_ACH_USER_ACHIEVEMENT` | `"user_achievement"` | never consumed |

Most of these are expected v1→v2 residue and are pure cleanup — **[reference]** already
scopes "dead JSON keys" into Epic 0. Two are worth more than cleanup:

- **`JKEY_ACH_COUNT`** — the live mis-wiring in K5.
- **`JKEY_ACH_SCOPE`** — because the SDK never reads `scope`, it cannot distinguish a
  `global` achievement from a `game`-scoped one locally. Harmless today, since `global` is
  server-evaluated by definition and the scorbitron endpoint already filters delivery. But
  it means the SDK cannot self-filter, and it forecloses any future local check that needs
  to know scope.

**Why this matters beyond tidiness:** dead constants that shadow live ones are exactly how
K1 happened. A constant named `JKEY_ACH_GROUP_ID` sat next to a wrong string literal for
months because nothing failed loudly. Deleting the dead ones removes the ambiguity — and
should happen in the same pass as the K5/K6 field-semantics work, with the parsing tests
from K2 in place first so the deletions are provably inert.

---

## Merge-path items

### M1 — Branch is 250 commits behind `main`

`git rev-list --count origin/feature/v2_achievements_module..main` = **250**. The branch
forked at `2422987` and its last feature work is 2026-02-09. `main` has since moved to
v2.0.4.

The intervening range is not cosmetic. `main`'s recent history includes an exponential-backoff
rework of auth retries (`859c717`), fingerprinting changes (`571ab70`), and LAN-IP work
(`3415a25`) — and the achievements code sits in `net.cpp`, which is one of the most heavily
touched files in the repo (3,082 lines on `main`). The achievements additions land around
`net.cpp:2180-2300` on the branch; expect real conflicts there and in `identifiers.h`.

**Recommendation.** Rebase onto `main` **before** fixing P1–P4, not after. Fixing first means
doing the conflict resolution twice and re-reviewing fixes that moved. Land the rebase as its
own reviewable step with no behaviour change, then stack the fixes on top.

**Which branch is the base — settled.** Use
`fix/SB-4063-achievement-group-id-json-key`. **Verified:** `git rev-list --count fix..feature`
is **0** and `git merge-base --is-ancestor feature fix` succeeds, so the fix branch is a
strict descendant — it contains every commit and every file from the feature branch, and the
complete content diff between them is the single `identifiers.h` line. There is nothing to
cherry-pick and nothing left behind; rebasing `fix` onto `main` carries the whole module plus
the K1 fix.

### M2 — Reference document cites lines that don't resolve on `main`

`achievements-platform-reference.pdf` cites SDK paths and line numbers against the branch:
`source/achievement_manager.cpp`, `include/scorbit_sdk/achievements*.h`, `source/net.cpp:859`,
`net.cpp:1990`, `source/identifiers.h:38-41`, `identifiers.h:148`. On `main`, the first three
paths do not exist and `identifiers.h:38-41` holds diagnostics URLs.

Anyone reading the PDF against a `main` checkout will conclude the document is wrong. Add a
one-line scope note to the PDF naming the branch and commit it was written against. Cheap,
and it stops the next person repeating this whole investigation — which is what prompted it.

---

## Suggested sequencing

Revised after server verification. The two changes from the original ordering: **P2 dropped
from third to last** (its premise was wrong), and **K7 promoted to run alongside P1** (fixing
P1 without it widens the hole).

1. **M1** — rebase onto `main`, no behaviour change, own PR. Base is
   `fix/SB-4063-achievement-group-id-json-key`.
2. **K2 (partial)** — stand up the harness and port the server's rule-evaluation cases as
   characterisation tests *first*, so they fail against P1/P3/P5 and pass after.
3. **P5** — counter thresholds. Highest-value functional fix, and the one needing a design
   decision (metric-key mapping), so start the conversation early. Ship the conservative
   interim behaviour — never claim `newlyUnlocked` locally — if the full fix will not land
   soon.
4. **P1 + K7 together** — honour `comparison` in the SDK **and** add the Portal/API
   validation rejecting `LT` / `EQ 0` on SDK-owned rule types. Landing P1 alone makes the
   fail-safe hole reachable from both engines. Flag the `>=` → `>` change to content authors.
5. **P4** — release the lock before notifying; document the lock ordering.
6. **P3** — make both `check*` paths conservative, or collapse to one full-state entry point.
7. **K4** — wire up the session-scoped progress reset.
8. **K3** — decide C++/Python facade vs. correcting the header comment. Fix the
   non-compiling example in `achievements.h` either way; five-minute change, actively
   misleading integrators today.
9. **K6 / K8** — field-naming and dead-key pass. Delete the 15 dead constants once K2's
   parsing tests can prove the deletions are inert. `JKEY_ACH_COUNT` goes with them.
10. **P2** — **done.** The SDK side was widened ahead of the API rather than waiting, so only
    the server's `Rule.target` field remains 32-bit. The `int64_t targetScore` narrowing is gone
    with it.

Items 5 and 6 are unambiguous corrections in `achievement_manager.cpp` and could share a PR.
Items 3 and 4 each carry a decision — a design question and a cross-repo coordination — so
they want their own review.

**Cross-repo note.** Items 4 (K7 half) and any P5 API-side work land in the `api` repo, not
here. P5's SDK half can proceed independently as long as the interim behaviour is the
conservative one.
