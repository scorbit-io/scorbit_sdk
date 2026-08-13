# Project Governance & Conventions (RFC 2119) — scorbit_sdk (C/C++/Python)

> **Cross-stack rules** (branch naming, Conventional Commits, PR standards, Linear
> lifecycle, governance distribution) follow canonical upstream:
> [scorbit-io/eng_docs `_shared/AGENT_CONVENTIONS.md`](https://github.com/scorbit-io/eng_docs/blob/main/governance/_shared/AGENT_CONVENTIONS.md).
> This file contains scorbit_sdk-specific rules only.

The key words "MUST", "MUST NOT", "SHOULD", "SHOULD NOT", and "MAY" are interpreted per RFC 2119.

---

## 0. Development Environment (CRITICAL)

The SDK is a **C++20** library with a **C ABI** and **pure-Python (ctypes)** wrappers. It is
cross-compiled for Linux (armhf/arm64/amd64) inside Docker, and built natively on macOS for
development.

### 0.1. Running Commands

| Task | Command | NEVER Use |
|------|---------|-----------|
| Build all release artifacts | `make all` | ad-hoc `cmake` invocations for release output |
| Build one Linux arch | `make armhf` / `make arm64` / `make amd64` | host `cmake` (wrong ABI/glibc) |
| Build macOS (dev only) | `make macos` | — |
| Build Python 3 wheel | `make python` | `pip wheel wrappers/python` (version comes from repo `VERSION`) |
| Build Python 2.7 wheel | `make python27` | — |
| Cut a release branch | `make release` | hand-editing `VERSION` on `main` |
| Remove build artifacts | `make clean` | `rm -rf build` (drops the CPM cache) |
| Local dev build | `cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -G Ninja && cmake --build build` | in-source builds (CMake hard-fails) |
| Run tests | `ctest --test-dir build --output-on-failure` | running test binaries directly |
| Format C/C++ | `clang-format -i <files>` | reformatting unrelated files |
| Format CMake | `./scripts/format-cmakelists.sh` | `cmake-format` (project uses gersemi) |

Linux builds run inside `dilshodm/gcc-builder:$(cat DOCKER_RELEASE)`; Python wheels run inside
`dilshodm/python-builder:$(cat DOCKER_RELEASE)`. `SCORBIT_PYTHON_NO_DOCKER=1 make python` builds
the wheel on the host Python when Docker is unavailable.

### 0.2. Absolute Prohibitions

- **NEVER** perform an in-source build — CMake fails with `In-source builds not allowed`.
- **NEVER** bump `VERSION` outside the `make release` flow (see §6).
- **NEVER** commit build output. `encrypt_tool/encrypt_tool`, `build/`, and `build-*/` are
  artifacts; `encrypt_tool/encrypt_tool` is **not** currently covered by `.gitignore` — do not
  `git add` it.
- **NEVER** commit or log `SCORBIT_SDK_ENCRYPT_SECRET`, provider private keys, or machine tokens.
- **NEVER** change the C ABI (`include/scorbit_sdk/*_c.h`) without a major/minor version bump —
  consumers link against `libscorbit_sdk.so.<major>`.

### 0.3. Quick Reference

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -G Ninja   # configure dev build
cmake --build build -j                                   # build
ctest --test-dir build --output-on-failure                # run unit tests
cmake -B build-all -S all -G Ninja                        # configure the IDE-wide superproject
clang-format -i source/net.cpp source/net.h               # format changed C/C++ files
./scripts/format-cmakelists.sh                            # format CMakeLists via gersemi
make arm64                                                # cross-build arm64 release artifacts
```

### 0.4. Key CMake Options

| Option | Values | Meaning |
|--------|--------|---------|
| `SCORBIT_SDK_PRODUCTION` | `ON`/`OFF` (default `OFF`) | Production build for distribution. Requires `SCORBIT_SDK_ABI`. |
| `SCORBIT_SDK_SHARED` | `ON`/`OFF` (default `ON`) | Shared vs. static library. Component packaging only applies when `ON`. |
| `SCORBIT_SDK_ABI` | `u12`, `u12dev`, `u18`, `u20`, `macos`, `unknown` | ABI tag baked into the platform ID and artifact names. `u12`/`u12dev` additionally link `-static-libstdc++`. |
| `SCORBIT_LOGGER` | `callback` (default), `spdlog` | Logger backend. `callback` compiles `source/log_c.cpp`; `spdlog` compiles `source/log_c_stub.cpp` and defines `SCORBIT_LOGGER_SPDLOG`. |
| `ENABLE_TEST_COVERAGE` | `ON`/`OFF` | gcov instrumentation for the test targets. |
| `TEST_INSTALLED_VERSION` | `ON`/`OFF` | Test against an installed `find_package(scorbit_sdk)` instead of the source tree. `test_detail` is skipped in this mode. |

Install prefix is fixed to **`/opt/scorbit`** (`SCORBIT_SDK_INSTALL_PREFIX`); `CMAKE_INSTALL_LIBDIR`
is forced to `lib`. Do not override these — the DEB maintainer scripts, `ld.so.conf.d` entry, and
the self-update path all assume that layout.

---

## 1. Project Structure & Architecture

```
scorbit_sdk/
├── include/scorbit_sdk/   # PUBLIC API surface (installed). *_c.h = C API, *.h = C++ API
├── source/                # Private implementation (never installed)
│   ├── utils/             # Leaf helpers: mac_address, lan_ip, jwt_parser, archiver, decrypt, ...
│   ├── identifiers.h      # ALL URL / JSON-key / channel constants
│   └── event_classes.h    # Event class hierarchy
├── libs/                  # Vendored in-repo sub-libraries: logger, tpm, nfc, utils
├── cmake/                 # CPM, packaging, arch detection, encryption, patches
├── scripts/               # Build/release shell scripts
├── tests/                 # test_detail (internals), test_interface (C++ API), test_interface_c (C API), test_python
├── examples/              # c_example, cpp_example, python_example, python27_example
├── wrappers/              # python (py3), python27 — pure-Python ctypes bindings
├── encrypt_tool/          # Standalone key-encryption tool, packaged separately
├── assets/                # certs/cacert.pem, deb/ maintainer scripts, scripts/ helpers
├── all/                   # Superproject that adds every subdirectory (IDE convenience only)
└── documentation/         # Doxygen configuration
```

### 1.1. Header Visibility

| Location | Purpose | Installed? |
|----------|---------|-----------|
| `include/scorbit_sdk/*.h` | Public C++ API | YES — `scorbit_sdk-dev` package |
| `include/scorbit_sdk/*_c.h` | Public C API | YES — `scorbit_sdk-dev` package |
| `source/**/*.h`, `source/**/*.hpp` | Private implementation | NO |
| `libs/*/include/**` | Sub-library headers | NO (linked `PRIVATE`, symbols hidden) |

- Adding a file to `include/scorbit_sdk/` changes the shipped API surface. It MUST be listed in
  the root `CMakeLists.txt` `target_sources()` block and reviewed as an API change.
- The library builds with `CXX_VISIBILITY_PRESET hidden` and `-Wl,--exclude-libs,ALL` (GNU) /
  `-Wl,-dead_strip` (Darwin). Anything intended for export MUST use the generated export macros.

### 1.2. Where Does This Code Belong?

| If you are writing… | It MUST go in… | NOT in… |
|---------------------|----------------|---------|
| Public C++ API | `include/scorbit_sdk/<feature>.h` | `source/` |
| Public C API | `include/scorbit_sdk/<feature>_c.h` | `source/` |
| C wrapper implementation | `source/<feature>_c.cpp` | `include/` |
| Private implementation | `source/<feature>.{h,cpp}` | `include/` |
| Leaf utility with no SDK deps | `source/utils/` | `source/` root |
| URL, JSON key, channel name, literal value | `source/identifiers.h` | inline string literals |
| Event class definition | `source/event_classes.h` | other files |
| Reusable, independently testable subsystem | `libs/<name>/` | `source/` |

---

## 2. C++ Coding Standards

### 2.1. Language Standard

- **C++20** MUST be used (`CMAKE_CXX_STANDARD 20`, `CXX_STANDARD_REQUIRED ON`).
- Public **C** headers MUST remain valid C99 and MUST be wrapped in `extern "C"` guards.
- Code MUST compile clean with `-Wall -Wpedantic -Wextra -Werror` (enabled by the test targets on
  GCC/Clang). Warnings are build failures.

### 2.2. Style & Formatting

- **Formatter:** `clang-format` using the repo `.clang-format` (Qt/WebKit-derived).
- **Column limit:** 100 characters.
- **Pinned version:** `clang-format==14.0.6` (as pinned in `.github/workflows/style.yml`). Newer
  versions reflow code differently — do not reformat the tree with an unpinned binary.
- `SortIncludes` is **disabled**. Include order is maintained by hand (see §2.5).
- **CMake files:** formatted with `gersemi` per `.gersemirc` (indent 4, line length 100). Run
  `./scripts/format-cmakelists.sh`.
- Agents MUST format only the files they changed. Bulk reformatting is FORBIDDEN in a feature PR.

### 2.3. Naming Conventions

| Element | Convention | Example |
|---------|------------|---------|
| Classes / structs | PascalCase | `GameState`, `EventManager`, `Worker` |
| Enum types and enumerators | PascalCase | `AuthStatus::Authenticated`, `Worker::Timer::Heartbeat` |
| Functions / methods | camelCase | `setScore()`, `getPlayerInfo()` |
| Local variables / parameters | camelCase | `sessionData`, `threadNiceValue` |
| Member variables | `m_` + camelCase | `m_sessionUuid`, `m_isAuthenticated` |
| Compile-time constants | UPPER_SNAKE_CASE | `URL_SCORBITRON_TOKEN`, `JKEY_PLAYER_ID` |
| Namespaces | lowercase | `scorbit`, `scorbit::detail` |
| File names | snake_case | `game_state.cpp`, `player_profiles_manager.h` |
| C types | `sb_` prefix, `_t` suffix | `sb_game_handle_t`, `sb_event_type_t` |
| C functions | `sb_` prefix, snake_case | `sb_event_type()`, `sb_config_set_lan_ip()` |
| C enum values | `SB_` prefix, UPPER_SNAKE | `SB_EVT_ACHIEVEMENT_UNLOCKED` |

### 2.4. Namespaces

```cpp
namespace scorbit {
namespace detail {
// internal implementation
} // namespace detail
} // namespace scorbit
```

- Public API MUST live in `scorbit`.
- Internal implementation MUST live in `scorbit::detail`.
- Closing braces MUST carry a `// namespace <name>` comment.
- `NamespaceIndentation: None` — namespace bodies are not indented.

### 2.5. Include Order

Groups separated by a blank line, in this order (clang-format will not reorder them for you):

```cpp
#include "my_class.h"            // 1. corresponding header (.cpp only)

#include <scorbit_sdk/game_state.h>   // 2. project headers
#include "internal_helper.h"

#include <boost/asio/strand.hpp>      // 3. third-party
#include <fmt/format.h>

#include <string>                     // 4. standard library
#include <vector>
```

### 2.6. Error Handling & Logging

- Exceptions MUST NOT cross the C ABI boundary. Every `source/*_c.cpp` entry point MUST catch and
  translate to an `sb_error_t`.
- Prefer error codes and callbacks over exceptions at API boundaries.
- Internal diagnostics MUST use the logger macros from `libs/logger`: `DBG()`, `INF()`, `WRN()`,
  `ERR()`. Do not use `printf`, `std::cout`, or `fmt::print` for runtime logging.
- Network and hardware (TPM/NFC) failures MUST degrade gracefully — the SDK runs unattended inside
  a pinball machine and MUST NOT abort the host process.
- Blocking network or hardware calls MUST NOT be made from inside a user callback.

### 2.7. Concurrency

- Async work MUST be scheduled through `scorbit::detail::Worker` (Boost.Asio `io_context` +
  strand). Do not spawn raw `std::thread` in SDK code.
- Recurring work MUST use a named entry in `Worker::Timer`. New timers MUST be added **before**
  the `Count` sentinel.
- Shared mutable state accessed from more than one strand/thread MUST be documented and guarded.

---

## 3. Dual API Pattern (C++ and C) — MANDATORY

Every public feature MUST ship both a C++ and a C surface, in the same PR.

```
include/scorbit_sdk/feature.h     C++ API — classes, RAII, std types
include/scorbit_sdk/feature_c.h   C  API — opaque handles, POD, extern "C"
source/feature.cpp                C++ implementation
source/feature_c.cpp              C wrapper delegating to the C++ implementation
```

| C++ type | C type |
|----------|--------|
| `scorbit::GameState` | `sb_game_handle_t` |
| `scorbit::EventType` | `sb_event_type_t` |
| `scorbit::Error` | `sb_error_t` |
| `scorbit::AuthStatus` | `sb_auth_status_t` |

Rules:
- C++ enum values MUST be defined **in terms of** their C counterparts so the two cannot drift:
  `enum class EventType { NewEvent = SB_EVT_NEW_EVENT };`
- The C API MUST NOT leak ownership ambiguity: any pointer handed to the caller MUST have a
  documented lifetime, and any allocation MUST have a matching `sb_*_free`/destroy call.
- The Python wrappers (`wrappers/python`, `wrappers/python27`) bind the **C** API via `ctypes`.
  A new C entry point is not usable from Python until `_bindings.py` declares its `argtypes` and
  `restype`. Adding a C function without the binding is incomplete work.

### 3.1. Adding a New Event Type (7 steps, in order)

1. Add the C enum value → `include/scorbit_sdk/event_types_c.h`
2. Add the C++ enum value (aliased to the C value) → `include/scorbit_sdk/event_types.h`
3. Create the event class → `source/event_classes.h`
4. Declare the C helper → `include/scorbit_sdk/event_helpers_c.h`
5. Implement the C helper → `source/event_helpers_c.cpp`
6. Add the C++ accessor → `include/scorbit_sdk/event.h`
7. Update **all four** examples and add a `tests/test_detail/source/test_event_*.cpp` case

---

## 4. Network, Provisioning & Identifiers

### 4.1. Identifier Constants

All URLs, JSON keys, JSON literal values, and Centrifugo channel names MUST be `constexpr auto`
constants in `source/identifiers.h`. Inline string literals for these are FORBIDDEN.

| Prefix | Meaning | Example |
|--------|---------|---------|
| `URL_*` | REST endpoint path, `/api` prefix included | `URL_SCORBITRON_TOKEN` |
| `ARG_*` | `fmt` named-argument key used in a URL template | `ARG_SCORBITRON_UUID` |
| `JKEY_*` | JSON object key | `JKEY_PLAYER_ID` |
| `JVAL_*` | JSON literal value | `JVAL_CHN_TYPE_START_GAME` |
| `CF_CHN_*` | Centrifugo channel name/prefix | `CF_CHN_MACHINE` |

URL templates use `fmt::arg()` named substitution — positional substitution is FORBIDDEN.

### 4.2. API Versioning

New endpoints MUST target `api/v2/`. The `/api` prefix is embedded in the endpoint constant itself
(see commit `f9219e0`); do not re-add it at the call site.

### 4.3. Transport

- REST via `libcpr`; WebSocket/real-time via `centrifugo-cpp`.
- The CA bundle is **embedded** from `assets/certs/cacert.pem` via CMakeRC for reproducibility.
  It is refreshed only by `./scripts/update-cacert.sh` during `make release` — never by hand.
- Auth retries MUST use exponential backoff; TPM/ATCA transient errors MUST use randomized backoff.

### 4.4. Secrets

Provider private keys are encrypted at build time with `SCORBIT_SDK_ENCRYPT_SECRET` (see
`cmake/encrypt.cmake` and `encrypt_tool/`). The secret is supplied by the CI environment. It MUST
NOT appear in source, logs, test fixtures, or CMake cache files committed to the repo.

---

## 5. Testing & QA

### 5.1. Framework

- **Catch2 v3** (`v3.8.1`) with **trompeloeil v49** for mocking. Tests are registered with CTest
  via `catch_discover_tests()`.
- Google Test is NOT used. Any documentation claiming otherwise is stale.

| Target | Scope |
|--------|-------|
| `tests/test_detail` | Internal implementation — compiles SDK sources directly with `SCORBIT_SDK_STATIC_DEFINE=1` |
| `tests/test_interface` | Public C++ API against the built library |
| `tests/test_interface_c` | Public C API (skipped on MSVC) |
| `tests/test_python` | Python wrapper (`unittest`) — **not currently wired into CTest** |

### 5.2. Rules

- New behaviour in `source/` MUST have a `tests/test_detail` case. New public API MUST have a
  `test_interface` (and `test_interface_c`) case.
- `tests/test_detail/CMakeLists.txt` lists sources **explicitly**. A new test file is not compiled
  until it is added there — verify your test actually ran, do not assume.
- Test files MUST be named `test_<unit>.cpp` and mirror the unit under test.
- Tests MUST NOT require network access, real hardware, or a provisioned TPM. Mock at the
  `key_resolver` / `net_base` seam.
- Run `ctest --test-dir build --output-on-failure` before claiming a change works. A passing
  compile is not evidence.

### 5.3. Known Test Gaps

These are documented debt, not permission to add more:
- `source/test_net.cpp` is commented out of `tests/test_detail/CMakeLists.txt` (lines ~107–109).
- `tests/test_python/` is not registered with CTest and does not run in any pipeline.

---

## 6. Versioning, Packaging & Release

### 6.1. Version Source of Truth

`VERSION` at the repo root is the single source of truth. CMake reads it, the Python wheels read
it, and both release workflows assert the git tag matches it exactly.

### 6.2. Cutting a Release

```bash
# edit VERSION only; working tree otherwise clean
make release      # creates chore/release_<version>, refreshes certs, commits the bump
```

`make release` refuses to run if `VERSION` is unmodified or if anything else is dirty. It creates:
1. `chore: update certs` (only when `assets/certs/cacert.pem` changed)
2. `chore(release): bump version to <version>`

Then: open a PR → merge to `main` → tag `main` with the bare version (`2.0.4`, no `v` prefix) →
push the tag.

### 6.3. Artifact Matrix

| Arch | ABI tag | Produced by |
|------|---------|-------------|
| `armhf` | `u12` | `make armhf` |
| `arm64` | `u18` | `make arm64` |
| `amd64` | `u18` | `make amd64` |
| macOS arm64 | `macos` | `make macos` (development only, not released) |

Each Linux build emits four artifacts: `scorbit_sdk-<ver>-<arch>_<abi>.{deb,tar.gz}` (runtime) and
`scorbit_sdk-dev-<ver>-<arch>_<abi>.{deb,tar.gz}` (headers + CMake package files). Plus
`encrypt_tool-*.tar.gz`, `scorbit-<ver>-py3-none-any.whl`, and `scorbit_py2-<ver>-py2-none-any.whl`.

> **Doc drift:** `README.md` §"ABI Tag" documents `u12`, `u12dev`, and `u20`, but the release
> pipeline ships `u12` and `u18`. Fix the README, not the pipeline, unless the ABI targets are
> deliberately changing.

### 6.4. Reproducible Builds

Release builds are byte-reproducible and MUST stay that way:
- `SOURCE_DATE_EPOCH` is derived from the last commit; `TZ=UTC`, `LC_ALL=C`, `LANG=C`, `umask 022`.
- `-ffile-prefix-map` / `-fdebug-prefix-map` strip absolute paths.
- `.deb` files are unpacked and rebuilt with `--root-owner-group`, a normalized `Installed-Size:`,
  and uniform gzip compression (`scripts/build-core.sh`).
- The CA bundle is a versioned source input, not a build-time download.

Changes that introduce timestamps, absolute paths, hostnames, or network fetches into build output
are FORBIDDEN.

### 6.5. Install Layout

Linux packages install under `/opt/scorbit`. The runtime DEB's maintainer scripts add
`/etc/ld.so.conf.d/scorbit-sdk.conf`, run `ldconfig`, set `/opt/scorbit/lib` to mode `0777` so a
non-root process can replace `.so` files during self-update, and install the RPI-RP2 `/etc/fstab`
block via `/opt/scorbit/bin/add-rpi-rp2-fstab.sh`. Changing any of this affects field devices and
requires explicit sign-off.

---

## 7. Git / PR — scorbit_sdk-specific rules

> **Cross-stack Git/PR rules** live in canonical upstream:
> [scorbit-io/eng_docs `_shared/AGENT_CONVENTIONS.md` §1–§3](https://github.com/scorbit-io/eng_docs/blob/main/governance/_shared/AGENT_CONVENTIONS.md).

### 7.1. Branch Naming

New branches MUST match:

```
^(feature|fix|chore|hotfix|refactor|perf)/(SB-\d+-)?[a-z0-9._-]+$
```

- A Linear ticket SHOULD be referenced: `feature/SB-3394-set-lan-ip`.
- `improve/…` and `release/…` are **legacy** and MUST NOT be used for new branches — use `chore/`.
- This repo has **no** pre-push hook enforcing the pattern (unlike `api`). Compliance is a review
  responsibility.

### 7.2. Commit Messages

Conventional Commits, `<type>(<scope>): <subject>`, imperative mood, no trailing period.

Allowed types: `feat`, `fix`, `chore`, `refactor`, `perf`, `docs`, `test`, `build`, `ci`.
`improve` appears in history but is **deprecated** — map it to `feat`, `perf`, or `refactor`.

Scopes in active use: `net`, `build`, `tpm`, `nfc`, `packaging`, `release`, `diag`, `python`,
`api`, `provision`, `worker`, `fmt`, `tests`, `logger`, `cf`.

Breaking ABI/API changes MUST use `!` (`feat!: API v2`) and MUST be called out in the PR body.

### 7.3. PR Requirements

A PR is not reviewable until all of the following hold:
- It builds clean on at least one Linux arch (`make amd64` or an equivalent local Ninja build) with
  `-Werror` in force.
- `ctest --output-on-failure` passes.
- Changed C/C++ files are `clang-format`-clean; changed CMake files are `gersemi`-clean.
- New public API has **both** C++ and C surfaces, Python `ctypes` bindings, an example update, and
  tests.
- The PR body carries `FIXES: SB-XXXX` (or `Part of SB-XXXX`) on its own line when a Linear issue
  exists.
- **Because PR-gating CI is currently disabled (see §7.6), the author MUST paste the build and
  `ctest` output into the PR description as evidence.**

### 7.4. Files That MUST NOT Be Modified Without Explicit Request

- `VERSION` — owned by the `make release` flow
- `DOCKER_RELEASE` — pins the builder image tag
- `assets/certs/cacert.pem` — refreshed only by `scripts/update-cacert.sh`
- `assets/deb/`, `assets/scripts/` — maintainer scripts that run on customer devices
- `.github/workflows/` — CI/CD pipelines
- `cmake/` — packaging and dependency modules
- `.clang-format`, `.gersemirc` — reformatting the tree is a separate, standalone PR

### 7.5. Responding to PR Review Comments

Every review comment — human or bot (Copilot review is enabled on this repo) — MUST be answered
before the PR is considered done. Use the GitHub GraphQL API via `gh api graphql`; the REST reply
endpoints return 404.

```bash
gh api graphql -f query='
{
  repository(owner: "scorbit-io", name: "scorbit_sdk") {
    pullRequest(number: <PR_NUMBER>) {
      reviewThreads(first: 20) {
        nodes { id isResolved comments(first: 1) { nodes { body databaseId } } }
      }
    }
  }
}'
```

```bash
gh api graphql -f query='
mutation {
  reply: addPullRequestReviewThreadReply(input: {
    pullRequestReviewThreadId: "PRRT_...",
    body: "Fixed in <commit_sha>."
  }) { comment { id } }
  resolve: resolveReviewThread(input: { threadId: "PRRT_..." }) { thread { isResolved } }
}'
```

Rules: always state the action taken and the commit SHA; always resolve after replying; batch
threads with aliases; **NEVER use backticks in the `body` field** — they break GraphQL parsing.

### 7.6. CI Status — READ THIS

As of this document's authoring, **five of seven GitHub Actions workflows are
`disabled_manually`**: `Style`, `MacOS`, `Windows`, `Install`, and `Documentation`. Only the
tag-triggered `Release Ubuntu` and `Release PyPI` workflows are active.

Those five workflows are also **broken as written** — they run `cmake -Stest -Bbuild` against a
`test/` directory that does not exist (the directory is `tests/`), and `style.yml` builds a
`check-format` target that no build file defines. Re-enabling them without fixing those two
problems will produce red builds, not green ones.

**Consequence:** nothing verifies a pull request automatically. Local verification and pasted
evidence (§7.3) are the only gate. Do not describe a change as "CI green" — there is no PR CI.

---

## 8. Issue Tracking — scorbit_sdk-specific rules

> **Cross-stack Linear lifecycle** lives in canonical upstream:
> [scorbit-io/eng_docs `_shared/AGENT_CONVENTIONS.md` §5](https://github.com/scorbit-io/eng_docs/blob/main/governance/_shared/AGENT_CONVENTIONS.md).
> See also `SDLC.md` §8 for the state map.

### 8.1. Linear

**Linear is the only issue tracker for this repo.** Issues are `SB-XXXX` on team **Scorbit**. The
native HTTP MCP server at `https://mcp.linear.app/mcp` is canonical. Priority values: `0`=None,
`1`=Urgent, `2`=High, `3`=Normal, `4`=Low.

### 8.2. Rules

- Every non-trivial change SHOULD have a Linear issue; reference it in the branch name and in the
  PR body as `FIXES: SB-XXXX` (or `Part of SB-XXXX`).
- NEVER mark a Linear issue "Done" manually — `FIXES: SB-XXXX` plus merge handles it.
- NEVER move an issue to "In Review" while the PR is still a draft.

---

## 9. Documentation & Licensing

### 9.1. Doxygen

Public headers MUST carry Doxygen comments (`/** … */`) for every exported type, function, and
enumerator. `documentation/` builds the published reference; the `Documentation` workflow that
published it is currently disabled.

### 9.2. License Header

Every source file MUST begin with the MIT header:

```cpp
/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
 *
 * MIT License
 * ...
 */
```

Two standing defects, both pre-existing on `main`:
- **`scrobit.io` typo** (missing second `o`) appears in the header of ~146 files. New files MUST
  use `scorbit.io`. A tree-wide fix belongs in its own `chore:` PR.
- Missing headers entirely: `source/dflags.h`, `source/identifiers.h`,
  `include/scorbit_sdk/event.h`.

### 9.3. README

`README.md` is customer-facing — it documents package names, ABI tags, and install paths that
integrators depend on. Any change to packaging, install prefix, or ABI targets MUST update it in
the same PR.

---

## 10. Dependencies

Fetched via **CPM** (`cmake/CPM.cmake`), not `FetchContent`. Each has a dedicated
`cmake/lib_<name>.cmake` module.

| Dependency | Purpose |
|------------|---------|
| `fmtlib/fmt` | String formatting (also the URL templating engine) |
| `libcpr/cpr` | HTTP client |
| `nlohmann/json` | JSON parsing |
| `centrifugo-cpp` | WebSocket real-time messaging |
| `Boost` | `filesystem`, `thread`, `url` (compiled); `asio`, `uuid`, `dll`, `signals2`, `flyweight` (header-only) |
| `OpenSSL::Crypto` | Cryptography (system `find_package`) |
| `LibArchive` | Archive extraction for self-update |
| `concurrentqueue` | Lock-free event queue |
| `CMakeRC` | Embedding the CA bundle |
| `Catch2` + `trompeloeil` | Testing (test targets only) |
| `libs/logger`, `libs/tpm`, `libs/nfc`, `libs/utils` | In-repo sub-libraries |

New dependencies MUST be discussed before adding — every one lands on constrained embedded targets
and must cross-compile for armhf/arm64/amd64 under the pinned builder image.

---

*Cross-stack rules (branch naming, Conventional Commits, PR standards, Linear lifecycle,
governance distribution) are in
[scorbit-io/eng_docs `_shared/AGENT_CONVENTIONS.md`](https://github.com/scorbit-io/eng_docs/blob/main/governance/_shared/AGENT_CONVENTIONS.md).
This file supersedes the stale `AGENT_CONVENTIONS.md` on the unmerged
`feature/achievements-module` branch, which documents C++17, Google Test, FetchContent, and a
`scripts/u20-build.sh` that no longer exists.*
