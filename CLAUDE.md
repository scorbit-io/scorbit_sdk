# Claude Code Project Instructions — scorbit_sdk (C/C++/Python)

> **Shared protocol:** the cross-stack workflow boilerplate (issue tracking, branch
> workflow, session completion, bypass policy) follows canonical upstream:
> [scorbit-io/eng_docs `_shared/CLAUDE.md`](https://github.com/scorbit-io/eng_docs/blob/main/governance/_shared/CLAUDE.md).
> This file contains scorbit_sdk-specific overrides and additions only.

## 1. Governance & Conventions

Coding standards are in **AGENT_CONVENTIONS.md**. Delivery process is in **SDLC.md**.
Read AGENT_CONVENTIONS.md §0 (environment) and §3 (dual API pattern) before touching any code.

These three governance files (`CLAUDE.md`, `AGENT_CONVENTIONS.md`, `SDLC.md`) are tracked in this
repo. They are modeled on the `api` repo's governance set; unlike `api`, this repo does **not** yet
have `/sdlc:init` hooks or an eng_docs mirror check installed.

## 2. What This Repo Is

A **C++20 shared library** with a stable **C ABI** and pure-Python (`ctypes`) wrappers, shipped to
pinball manufacturers as `.deb`/`.tar.gz` packages for armhf/arm64/amd64 and as PyPI wheels. It
runs unattended on embedded devices in the field, talks to `api/v2` over REST and Centrifugo, and
can self-update its own `.so`.

Practical consequences for every change:
- **The C ABI is a contract.** Breaking it strands deployed machines. See AGENT_CONVENTIONS.md §3.
- **Builds must stay byte-reproducible.** No timestamps, absolute paths, or network fetches in
  build output. See AGENT_CONVENTIONS.md §6.4.
- **Failures must degrade, not crash.** The SDK is embedded in a host process it does not own.

## 3. Development Environment

Release builds run in Docker (`dilshodm/gcc-builder:$(cat DOCKER_RELEASE)`); day-to-day work uses a
local Ninja build. See **AGENT_CONVENTIONS.md §0** for the full table.

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -G Ninja
cmake --build build -j
ctest --test-dir build --output-on-failure
```

**Claude Code-specific rules:**
- **Never build in-source.** CMake hard-fails; the correct pattern is always `-B build -S .`.
- **Long builds:** run `cmake --build`, `make <arch>`, and `ctest` with `run_in_background: true`.
  A cold CPM cache makes the first configure genuinely slow.
- **Sequential Bash calls:** commands that may exit non-zero (`gh pr checks`, `gh run view`,
  `ctest`) run sequentially, not in parallel.
- **GitHub GraphQL:** never use backticks in the `body` field — they break GraphQL string parsing.
  Never chain multiple `gh api graphql` calls with `&&`.

## 4. Quality Gates (MANDATORY if code changed)

```bash
clang-format -i <changed .h/.cpp/.c files>     # pinned to clang-format 14.0.6
./scripts/format-cmakelists.sh                  # if any CMakeLists.txt changed
cmake --build build -j                          # MUST be clean; -Werror is in force
ctest --test-dir build --output-on-failure      # MUST pass
```

There is **no coverage gate** here (unlike `api`'s 100% requirement), and — critically — **there is
no PR-gating CI** (see §7). Local evidence is the only gate that exists. Paste the build and
`ctest` output into the PR.

## 5. Pre-Implementation Checklist

Before writing ANY code, confirm (see AGENT_CONVENTIONS.md §1–§3):
- [ ] Public API? → needs **both** `<feature>.h` (C++) and `<feature>_c.h` (C), in the same PR
- [ ] New C entry point? → `wrappers/python/src/scorbit/_bindings.py` needs `argtypes`/`restype`
- [ ] New URL, JSON key, or channel name? → `constexpr auto` in `source/identifiers.h`, never inline
- [ ] New endpoint? → `api/v2/`, with the `/api` prefix already inside the constant
- [ ] New event type? → the 7-step process in AGENT_CONVENTIONS.md §3.1, all four examples updated
- [ ] New async/recurring work? → goes through `Worker`; new timers before the `Count` sentinel
- [ ] New test file? → explicitly added to `tests/test_detail/CMakeLists.txt`, or it silently never runs
- [ ] New source file? → MIT header spelled **`scorbit.io`** (the tree has a `scrobit.io` typo — do not copy it)
- [ ] Touching `VERSION`, `DOCKER_RELEASE`, `assets/deb/`, `cmake/`, or `.github/`? → needs explicit request

## 6. PR Review Comments

Respond to every PR review comment — human or Copilot — before considering work done. Use the
GraphQL mutation pattern in AGENT_CONVENTIONS.md §7.5. Always reference the fixing commit SHA and
resolve the thread.

## 7. Known Repo State — Do Not Rediscover This

Verified against `main` at the time of writing. Treat as facts to work around, not as work to
silently take on.

| Area | State |
|------|-------|
| PR CI | **Absent.** `Style`, `MacOS`, `Windows`, `Install`, `Documentation` are all `disabled_manually`. Only tag-triggered `Release Ubuntu` / `Release PyPI` are active. |
| Those workflows | **Broken as written.** They configure `cmake -Stest` (dir is `tests/`) and build a `check-format` target that does not exist. Re-enabling as-is yields red builds. |
| `tests/test_python` | Not registered with CTest — never runs anywhere. |
| `source/test_net.cpp` | Commented out of `tests/test_detail/CMakeLists.txt`. |
| License headers | ~146 files carry the `scrobit.io` typo; `source/dflags.h`, `source/identifiers.h`, `include/scorbit_sdk/event.h` have no header at all. |
| README ABI table | Documents `u12`/`u12dev`/`u20`; the pipeline actually ships `u12` and `u18`. |
| `encrypt_tool/encrypt_tool` | A build artifact sitting untracked in the tree, not covered by `.gitignore`. Never `git add` it. |
| `main` history | `origin/main` has been force-pushed at least once — PR #96 reports merged on GitHub, but its merge commit `a551c40` is absent from `origin/main`. Verify a merge actually landed before assuming it did. |
| Stale governance | `feature/achievements-module` carries an older `AGENTS.md` + `AGENT_CONVENTIONS.md` claiming C++17, Google Test, FetchContent, and `scripts/u20-build.sh`. All four are wrong for `main`. This file set supersedes them. |

## 8. Long-Lived Branches

Several branches carry substantial unmerged work. Check before starting anything adjacent:

| Branch | State |
|--------|-------|
| `feature/v2_achievements_module` | 6 commits incl. `feat!: API v2` — achievements REST + Centrifugo + local matching. Unmerged since Feb 2026. |
| `fix/SB-4063-achievement-group-id-json-key` | PR #177, open. **Stacked on the unmerged achievements branch** — it cannot merge to `main` on its own. |
| `feat/wifi_diagnostics` | 7 commits — standalone diagnostics library, WPA Supplicant D-Bus listener. Unmerged since May 2026. |
| `feature/SB-3394-set-lan-ip` | PR #171, open since May 2026. Overlaps with the already-merged `feat(net): include local LAN IP in scorbitron object`. Verify it is not redundant. |

Roughly a dozen other remote branches have had no commits since 2024–2025 and are candidates for
deletion (see SDLC.md §9).

## 9. Issue Tracking

**Linear** (`SB-XXXX`, team Scorbit) is the only issue tracker for this repo. Reference the issue
in the branch name and as `FIXES: SB-XXXX` in the PR body. Never mark a Linear issue Done
manually — `FIXES: SB-XXXX` plus merge does it. See AGENT_CONVENTIONS.md §8.

### SDLC Configuration

- **Stack:** scorbit_sdk (C/C++/Python)
- **Governance source:** [scorbit-io/eng_docs](https://github.com/scorbit-io/eng_docs/tree/main/governance) (canonical upstream)
- **Governance files:** `CLAUDE.md`, `AGENT_CONVENTIONS.md`, `SDLC.md` (tracked in repo)
- **Hooks:** none installed — this repo has no `.sdlc/git-hooks/`, no branch-name pre-push gate,
  and no `SessionStart` mirror check. Conventions are review-enforced.
- **Tracker:** Linear, team Scorbit (`SB-`)
- **Branch pattern (review-enforced):** `^(feature|fix|chore|hotfix|refactor|perf)/(SB-\d+-)?[a-z0-9._-]+$`
- **PR close keyword:** `FIXES:` · **Ref keyword:** `Part of`
- **Merge strategy:** merge commit (repo history uses `Merge pull request #NNN from …`)
- **Release tag format:** bare semver, no `v` prefix (`2.0.4`)
