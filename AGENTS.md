# Agent Operating Protocol — scorbit_sdk

This repo's agent instructions live in three tracked files. Read them in this order:

| File | Contents |
|------|----------|
| **[CLAUDE.md](CLAUDE.md)** | Start here. Environment, quality gates, pre-implementation checklist, known repo state, long-lived branches. |
| **[AGENT_CONVENTIONS.md](AGENT_CONVENTIONS.md)** | RFC 2119 coding standards: build system, C++/C style, the mandatory dual C++/C API pattern, identifiers, testing, packaging, Git/PR rules. |
| **[SDLC.md](SDLC.md)** | Delivery process: planning cadence, story standard, Definition of Done, release pipeline, hotfix rules, branch hygiene. |

## The five rules most often broken here

1. **Never build in-source.** `cmake -B build -S .` — CMake hard-fails otherwise.
2. **Public API means both C++ and C**, plus Python `ctypes` bindings and example updates, in the
   same PR. The C ABI is a contract with deployed machines.
3. **A new test file is not compiled** until it is explicitly listed in the relevant
   `tests/*/CMakeLists.txt`. Verify your test actually ran.
4. **There is no PR-gating CI** — five of seven workflows are disabled and broken. Build, run
   `ctest`, and paste the output into the PR. Never claim "CI green."
5. **No URLs or JSON keys as inline literals.** They go in `source/identifiers.h`.

> The older `AGENTS.md` and `AGENT_CONVENTIONS.md` on the unmerged `feature/achievements-module`
> branch are **stale** — they document C++17, Google Test, FetchContent, and a
> `scripts/u20-build.sh` that no longer exists. The files above supersede them.
