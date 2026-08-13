# SDLC Playbook — scorbit_sdk

> **Source:** Derived from the Scorbit SDLC (`SDLC_Document-draft` v1.0 / `SDLC_Presentation.pdf`
> v1.1) as adopted in [scorbit-io/api `SDLC.md`](https://github.com/scorbit-io/api/blob/main/SDLC.md),
> adapted for a versioned, customer-distributed SDK.
>
> **Applies to:** the `scorbit_sdk` C/C++ library and its Python wrappers.
>
> **Key difference from the web/SaaS playbook:** the SDK is **not continuously deployed**. It is
> cut as a versioned artifact, published to GitHub Releases and PyPI, and then *pulled* by
> third-party manufacturers and by on-device self-update. Once a version is published it cannot be
> recalled from devices already running it. Release discipline therefore replaces rollback as the
> primary safety mechanism.
>
> The key words "MUST", "SHOULD", and "MAY" are interpreted per RFC 2119.

---

## 1. Work Hierarchy

Identical to the company-wide hierarchy. Levels and owners do not change per stack.

| Level | Name | Description | Typical Size | Owner |
|-------|------|-------------|--------------|-------|
| 1 | **Initiative** | Strategic theme or company-level goal. | Quarters to a year | PM + Leadership |
| 2 | **Epic** | Significant capability delivering meaningful value. May be designated an MVP. | 2–6 weeks | PM + Tech Lead |
| 3 | **Story** | Single, testable unit of functionality. Completable within one cycle. | 1–3 days | Engineer + PM |
| 4 | **Task / Sub-task** | Specific technical action required to complete a story. | Hours | Engineer |

**SDK-specific note:** most SDK work is developer-facing rather than end-user-facing. A story's
"user" is frequently a *manufacturer integrator* or the *scorbitd/firmware team*, not a player.
Write the user story from that consumer's perspective — "As an integrator, I want a C API for LAN
IP so that I can report device network state without linking C++."

---

## 2. Planning Cadence

The SDK follows the same quarterly / 2-week-cycle / daily rhythm as the rest of engineering.
See the shared SDLC for the full cadence. SDK-specific additions:

### 2.1. Quarterly

- **ABI/API roadmap review is REQUIRED.** Any epic that changes `include/scorbit_sdk/*_c.h` MUST be
  identified at quarterly planning, because it forces a major version bump and a coordinated
  rollout with every consumer (scorbitd, vpinball, manufacturer integrations).
- Toolchain and platform-support changes (new ABI tag, dropped architecture, compiler bump) MUST be
  planned a quarter ahead — they invalidate every prebuilt package customers hold.

### 2.2. Per Cycle

Standard story readiness criteria apply. A story MUST additionally answer, before entering a cycle:

1. Does it change the public API surface? If yes — C++ **and** C **and** Python binding scope is
   included in the estimate.
2. Does it require a firmware, `api`, or `scorbitd` change to land first? Dependencies MUST be
   explicit.
3. Does it affect packaging, install layout, or the self-update path? If yes, it needs a field-
   impact note.

### 2.3. Daily

Standard. Blockers escalate to the Tech Lead same-day; no blocker sits more than 24 hours.

---

## 3. Story Standard

The three story types (Feature, Bug, Spike) and their required fields are unchanged from the shared
SDLC. SDK stories carry two extra required fields:

- **API surface:** `none` · `C++ only` · `C++ and C` · `packaging/build only`
- **Consumer impact:** which downstream repos or manufacturer integrations must change

A story whose API surface is `C++ and C` is not Done until the Python `ctypes` bindings and all
four examples are updated (AGENT_CONVENTIONS.md §3).

---

## 4. Acceptance Criteria Rules

Standard rules apply: each criterion MUST be independently verifiable with a clear pass/fail
outcome, and ACs MUST be written before estimation — "No AC = not ready."

SDK-flavoured examples:

| Context | Bad (vague) | Good (testable) |
|---------|------------|-----------------|
| C API | "The C API exposes LAN IP." | "`sb_config_set_lan_ip(config, ip)` accepts a NUL-terminated string; the value appears in the scorbitron object PATCH body; passing `NULL` omits the field rather than sending an empty string." |
| Reliability | "Auth retries are better." | "After 3 consecutive 401s, retry intervals follow 1s/2s/4s/8s capped at 60s, verified by a `test_detail` case with a mocked clock." |
| Packaging | "The deb installs correctly." | "`dpkg -i scorbit_sdk-<ver>-arm64_u18.deb` creates `/etc/ld.so.conf.d/scorbit-sdk.conf`, leaves `/opt/scorbit/lib` at mode 0777, and `ldconfig -p` lists `libscorbit_sdk.so.2`." |
| Reproducibility | "The build is deterministic." | "Two `make amd64` runs from the same commit produce `.deb` files with identical SHA-256." |

---

## 5. Definition of Done

### 5.1. Story-Level DoD

A story is done when ALL of the following are true:

- Code is written, reviewed, and merged to `main` via a pull request
- At least one other engineer has reviewed and approved the PR
- All acceptance criteria are met and confirmed by the author
- The change builds clean with `-Werror` on at least one Linux target, and `ctest` passes
- **Build and `ctest` output is pasted in the PR** — this repo has no PR-gating CI
  (AGENT_CONVENTIONS.md §7.6), so evidence is manual
- New behaviour has unit tests, explicitly added to the relevant `tests/*/CMakeLists.txt`
- New public API has C++ + C surfaces, Python bindings, example updates, and Doxygen comments
- Changed C/C++ files are `clang-format`-clean; changed CMake files are `gersemi`-clean
- Any packaging, install-path, or README-visible change updates `README.md` in the same PR
- The Linear issue is in the correct state

### 5.2. Cycle-Level DoD

Unchanged from the shared SDLC: carryover documented and re-estimated, Cycle Review demo held,
Retrospective held with at least one logged improvement action.

### 5.3. Release-Level DoD

| Check | Required |
|-------|----------|
| All stories in scope meet Story DoD | REQUIRED |
| `VERSION` bumped via `make release` (not by hand) | REQUIRED |
| Semver correctness reviewed: ABI break → major; new API → minor; fix only → patch | REQUIRED |
| CA bundle refreshed (`scripts/update-cacert.sh`, run automatically by `make release`) | REQUIRED |
| `make all` succeeds — all three Linux arches + both Python wheels | REQUIRED |
| Reproducibility spot-check: a repeat build of one arch yields identical artifact hashes | REQUIRED |
| Full `ctest` run green on at least one arch | REQUIRED |
| Install smoke test: `.deb` installs and a linked example runs on a real arm64 or armhf device | REQUIRED |
| Self-update path exercised at least once per **minor** release | REQUIRED |
| Python wheel smoke test: `pip install` + `import scorbit` against the matching native package | REQUIRED |
| `README.md` package names, ABI tags, and install instructions match the artifacts produced | REQUIRED |
| Release notes drafted on the GitHub release, with any ABI/API break called out first | REQUIRED |
| Downstream consumers notified when the API surface changed (scorbitd, vpinball, integrators) | REQUIRED |

**There is no rollback.** Once a release is published and a device self-updates, the only remedy is
a forward fix in a new version. Treat the publish step as irreversible.

---

## 6. Release Pipeline

### 6.1. Versioning

Bare semver in the root `VERSION` file — no `v` prefix. Both release workflows assert the git tag
equals `VERSION` exactly and fail the release if it does not.

| Change | Bump |
|--------|------|
| Any change to `include/scorbit_sdk/*_c.h` that is not purely additive | **major** |
| New public API, new event type, new config option | **minor** |
| Bug fix, performance work, packaging fix, dependency bump | **patch** |

### 6.2. Cutting a Release

```bash
# 1. On a clean tree, edit VERSION only
$EDITOR VERSION

# 2. Create the release branch + commits (refuses if the tree is otherwise dirty)
make release
#    -> branch chore/release_<version>
#    -> commit "chore: update certs"            (only if cacert.pem changed)
#    -> commit "chore(release): bump version to <version>"

# 3. Open a PR, get review, merge to main

# 4. Tag the merge commit on main and push
git tag <version> && git push origin <version>
```

### 6.3. What CI Does From There

| Stage | Workflow | Trigger | Result |
|-------|----------|---------|--------|
| Build | `Release Ubuntu` (`ubuntu.yml`) | tag matching `[0-9]*.[0-9]*.[0-9]*` | Verifies tag == `VERSION`, verifies `SCORBIT_SDK_ENCRYPT_SECRET` is present, sets `SOURCE_DATE_EPOCH`/`TZ`/`LC_ALL`/`LANG`, runs `make all`, uploads a **draft** GitHub release with all 15 expected artifacts (`fail_on_unmatched_files: true`) |
| Publish | *manual* | Tech Lead | Review the draft release, write release notes, publish |
| PyPI | `Release PyPI` (`release-pypi.yml`) | GitHub release `published` | Re-verifies tag == `VERSION`, downloads `*.whl` from the release, publishes to PyPI via trusted publishing |

The draft-release gate is deliberate: it is the last point at which a bad build can be discarded
without anything reaching customers. Do not automate past it.

`Release PyPI` also accepts `workflow_dispatch` with an existing tag, for republishing wheels
without re-cutting a release.

### 6.4. Artifact Set (15 files per release)

- `scorbit_sdk-<ver>-{amd64,arm64}_u18.{deb,tar.gz}` and `scorbit_sdk-<ver>-armhf_u12.{deb,tar.gz}`
- `scorbit_sdk-dev-<ver>-…` — the same six, headers + CMake package files
- `encrypt_tool-*.tar.gz`
- `scorbit-<ver>-py3-none-any.whl`, `scorbit_py2-<ver>-py2-none-any.whl`

Adding or renaming an artifact requires updating the `files:` list in `ubuntu.yml`, or the release
job fails on unmatched files.

### 6.5. Consumer Coordination

The SDK is a dependency of `scorbitd`, `vpinball`, and third-party manufacturer firmware. A release
that changes the API surface MUST:

1. Note the affected consumers in the Linear epic before the cycle in which it ships
2. Land consumer-side changes behind the new version, not simultaneously with it
3. State the required SDK version in the consumer repo's dependency pin

Firmware releases are managed by the firmware team and operate outside this SDLC. Where a firmware
release depends on an SDK version, the PM coordinates the ordering; SDK releases MUST NOT be
blocked on the firmware cycle.

---

## 7. Hotfix & Incident Response

### 7.1. Incident Severity

| Severity | Condition | Response |
|----------|-----------|----------|
| **P1 — Critical** | Crash or hang in deployed machines, data loss, security issue, self-update bricking devices, complete loss of connectivity to the platform | Acknowledge within 1 hour (business hours). Post in `#incidents` immediately. Tech Lead decides fix-forward scope. PM owns integrator communication. Blameless post-mortem within 48 hours. |
| **P2 — Degraded** | A feature is broken but machines remain functional and connected | Acknowledge same business day. Triage within 24 hours. Normal cycle process applies. |

### 7.2. Hotfix Rule

A hotfix MAY only be declared for a confirmed P1. Declaration authority is the Tech Lead or senior
leadership.

**Qualifies as P1:** crash in the field · data loss · security issue · bricked self-update ·
complete platform disconnection

**Does NOT qualify:** slow operation · log noise · a broken example · anything that can wait for the
normal cycle

**Hotfix process:**

1. Tech Lead declares the hotfix and the target version (always a **patch** bump)
2. Branch `hotfix/SB-XXXX-slug` off `main`
3. Fix implemented; reviewed by a single reviewer (relaxed from the normal peer-review requirement)
4. `make release` on the hotfix branch → merge → tag → `Release Ubuntu` → publish
5. **Install smoke test on real hardware is NOT relaxed** — a hotfix that bricks self-update is
   worse than the original incident
6. Post-mortem within 48 hours

No exceptions to Conventional Commits or the `FIXES: SB-XXXX` requirement, hotfix or not.

### 7.3. Field Remediation

Because published versions cannot be recalled, a P1 caused by a released version requires all of:

- A patch release with the fix, published to GitHub Releases and PyPI
- Confirmation that self-update reaches affected devices, or an explicit manual-upgrade advisory
- Notice to any manufacturer holding the bad version

---

## 8. Linear State Map

Linear is the single source of truth for product work, and the only issue tracker for this repo.

| Linear State | Meaning | Who Moves It |
|-------------|---------|--------------|
| **Backlog** | Created but not yet refined or estimated | PM or Tech Lead on creation |
| **To-Do** | Refined, estimated, ACs written; eligible for cycle planning | PM + Tech Lead after refining |
| **In Progress** | Actively being worked on; branch created | Engineer on start |
| **In Review** | PR open (non-draft) and awaiting code review | Engineer on PR creation |
| **UAT** | Merged; awaiting acceptance testing | Engineer when PR merged, if `sdlc:uat` labelled |
| **Done** | Story-level DoD met | Auto via `FIXES: SB-XXXX`, or PM after UAT |
| **Cancelled** | Deprioritized or no longer relevant; reason documented | PM |

**Default merge behaviour:** `FIXES: SB-XXXX` in a merged PR auto-transitions the issue from
In Review to Done.

**SDK-specific:** a story that is merged to `main` but **not yet in a published release** is not
delivered to anyone. For stories where an integrator is waiting on the released artifact, label the
issue `sdlc:uat` before merge and hold it in UAT until the release carrying it is published.

Do not move an issue to In Review while its PR is still a draft. Do not mark Done manually.

---

## 9. Branch Hygiene

The SDK accumulates long-lived branches faster than the web stack because feature work often waits
on firmware or manufacturer coordination. Standing rules:

- A feature branch that has not been rebased on `main` in **30 days** MUST be either rebased, or
  converted to a draft PR with an explicit blocker noted in the Linear issue.
- **Stacked branches MUST be declared.** A PR whose base is another unmerged branch cannot land on
  `main` and MUST say so in its description. (Current example: PR #177
  `fix/SB-4063-achievement-group-id-json-key` is stacked on the unmerged
  `feature/v2_achievements_module`.)
- Merged branches SHOULD be deleted at merge time.
- Branches with no commits in **6 months** SHOULD be deleted or archived as a tag. The repo
  currently carries roughly a dozen such branches, the oldest dating to 2024.
- Release branches (`chore/release_*`) MUST be deleted after the tag is pushed.

Branch review is a standing Cycle Review agenda item.

---

## 10. Known Process Gaps

Tracked here so they are addressed deliberately rather than rediscovered each cycle. Each SHOULD
become a Linear issue.

| Gap | Impact | Suggested Fix |
|-----|--------|---------------|
| Five of seven workflows are `disabled_manually` (`Style`, `MacOS`, `Windows`, `Install`, `Documentation`) | No automated verification of any pull request | Fix and re-enable — see next row |
| Those workflows reference `cmake -Stest` (directory is `tests/`) and a `check-format` target that no build file defines | They would fail immediately if re-enabled | Repoint to `tests/`, add a real `check-format` target driving `clang-format --dry-run -Werror` + `gersemi --check` |
| No branch-name or commit-message enforcement | `improve/`, `release/`, underscore, and ticket-less branch names have all accumulated | Install the `.sdlc/git-hooks/` set used by `api`, or a lightweight pre-push hook |
| `tests/test_python` is not registered with CTest | The Python wrapper has zero automated coverage | Add a CTest entry, or a CI job running `python -m unittest` against a built `.so` |
| `source/test_net.cpp` is commented out of the build | The network layer — the SDK's largest failure surface — is untested | Restore it behind a mocked `net_base` seam |
| No release checklist artifact | §5.3 lives only in this document | Add a GitHub release PR template mirroring §5.3 |
| No `CHANGELOG.md` | Release notes are ad-hoc per GitHub release | Generate from Conventional Commits at release time |
| No `CONTRIBUTING.md`, `CODEOWNERS`, or PR template | External contributors have no stated process; reviewers are assigned ad-hoc | Add all three |
| Governance not mirrored from eng_docs | These files can drift from the canonical cross-stack rules | Run `/sdlc:init` + `/sdlc:sync-mirror` once the eng_docs `scorbit_sdk/` governance directory exists |

---

*Cross-stack process (work hierarchy, planning cadence, story standard, Linear lifecycle) is
canonical in [scorbit-io/eng_docs](https://github.com/scorbit-io/eng_docs/tree/main/governance).
This file documents the SDK-specific release pipeline, DoD, and hotfix rules that differ from the
continuously-deployed web/SaaS model.*
