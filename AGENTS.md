# Agent Operating Protocol - Scorbit SDK

This document is the **primary operating protocol** for any AI agent working in this codebase. Agents MUST follow these instructions exactly. The key words "MUST", "MUST NOT", "SHOULD", "SHOULD NOT", and "MAY" are to be interpreted as described in RFC 2119.

---

## 1. Project Overview

The Scorbit SDK is a **C++17 library** that enables pinball machines and other gaming devices to communicate with the Scorbit platform. It provides:

- Real-time score tracking and game session management
- Player profile and authentication integration
- Achievement event notifications via WebSocket (Centrifugo)
- Both **C++ and C APIs** for maximum compatibility

---

## 2. Issue Tracking & Workflow (Beads)

This project uses **bd** (beads) for issue tracking. Run `bd onboard` to get started.

### Quick Reference

```bash
bd ready              # Find available work
bd show <id>          # View issue details
bd update <id> --status in_progress  # Claim work
bd close <id>         # Complete work
bd sync               # Sync with git
```

### Standard Work Cycle

1. **Find work:** `bd ready`
2. **Review details:** `bd show <id>`
3. **Claim it:** `bd update <id> --status in_progress`
4. **Complete Pre-Implementation Checklist** (see Section 3)
5. **Implement** (adhering to Red Lines in Section 4)
6. **Land the Plane** (see Section 6)

---

## 3. Pre-Implementation Checklist

Before writing ANY code, agents MUST review and confirm the following rules.

### 3.1. Where Does This Code Belong?

| If you're writing... | It MUST go in... | NOT in... |
|---------------------|------------------|-----------|
| Public C++ API | `include/scorbit_sdk/*.h` | `source/` |
| Public C API | `include/scorbit_sdk/*_c.h` | `source/` |
| C wrapper implementation | `source/*_c.cpp` | `include/` |
| Private implementation | `source/*.cpp` and `source/*.h` | `include/` |
| Constants/identifiers | `source/identifiers.h` | Scattered in files |
| Event class definitions | `source/event_classes.h` | Other files |
| Examples | `examples/cpp_example/` or `examples/c_example/` | `source/` |

### 3.2. Ask Yourself Before Every Change

- [ ] **Am I adding a public API?** → It needs BOTH C++ and C versions
- [ ] **Am I adding an event type?** → Follow the 7-step event addition process (see Section 5.2)
- [ ] **Am I adding a JSON key?** → Define it as a constant in `source/identifiers.h`
- [ ] **Am I modifying public headers?** → This affects the API surface - be careful
- [ ] **Did I update the examples?** → New features MUST have example code

---

## 4. Red Lines / Hard Constraints

The following patterns are **FORBIDDEN**. Agents MUST NOT introduce them under any circumstances.

### 4.1. Absolute Prohibitions

| Prohibition | Reason |
|-------------|--------|
| **C++ features beyond C++17** | Must maintain compiler compatibility |
| **Public API without C wrapper** | SDK supports both C++ and C consumers |
| **Hardcoded strings for JSON keys** | Use constants from `identifiers.h` |
| **Blocking network calls in callbacks** | Can cause deadlocks |
| **Memory leaks in C API** | C callers expect proper lifetime management |
| **Breaking API changes without version bump** | Semver compliance required |
| **Platform-specific code without guards** | Must compile on Linux, macOS, Windows |

### 4.2. Required Patterns

| Requirement | Standard |
|-------------|----------|
| **Formatting** | `clang-format` with project `.clang-format` |
| **Column limit** | 100 characters |
| **Member variables** | `m_` prefix (e.g., `m_sessionUuid`) |
| **Constants** | `UPPER_SNAKE_CASE` |
| **Classes/Enums** | `PascalCase` |
| **Functions** | `camelCase` |
| **File names** | `snake_case.cpp`, `snake_case.h` |
| **Namespace closing** | Comment with namespace name |
| **License header** | MIT license header on all source files |

---

## 5. Architecture & Patterns

### 5.1. Dual API Pattern

Every public feature MUST have both C++ and C APIs:

```
┌─────────────────────────────────────────────────────────────┐
│                     Public Headers                          │
│  include/scorbit_sdk/                                       │
│  ├── feature.h        (C++ API - classes, RAII)            │
│  └── feature_c.h      (C API - functions, handles)         │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                     Implementation                          │
│  source/                                                    │
│  ├── feature.cpp      (C++ implementation)                 │
│  └── feature_c.cpp    (C wrapper calling C++ code)         │
└─────────────────────────────────────────────────────────────┘
```

### 5.2. Adding New Event Types (7-Step Process)

When adding a new event type, follow these steps IN ORDER:

1. **Add C enum value** → `include/scorbit_sdk/event_types_c.h`
2. **Add C++ enum value** → `include/scorbit_sdk/event_types.h`
3. **Create event class** → `source/event_classes.h`
4. **Add C helper declaration** → `include/scorbit_sdk/event_helpers_c.h`
5. **Implement C helper** → `source/event_helpers_c.cpp`
6. **Add C++ wrapper method** → `include/scorbit_sdk/event.h`
7. **Update examples** → Both `cpp_example/main.cpp` AND `c_example/main.c`

### 5.3. Network Layer (net.cpp)

The `Net` class handles all server communication:

- **Authentication** via JWT tokens
- **REST API calls** via libcpr
- **WebSocket** via Centrifugo client
- **Channel subscriptions** for real-time events

Key patterns:
- Use `task_t` lambdas for async operations
- Define endpoints in `identifiers.h`
- Handle errors gracefully with logging

### 5.4. Event System

```
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ Event Source │───▶│ EventManager │───▶│   Callback   │
│ (Net/Channel)│    │   (Queue)    │    │ (User Code)  │
└──────────────┘    └──────────────┘    └──────────────┘
```

- Events are pushed to `EventManager` queue
- User callback receives events via `Config::setEventCallback()`
- Event data lifetime is valid only during callback

---

## 6. Landing the Plane (Session Completion)

When ending a work session, agents MUST complete ALL steps below.

### 6.1. Quality Gates (MANDATORY)

```bash
# 1. Format code
clang-format -i source/*.cpp source/*.h include/scorbit_sdk/*.h

# 2. Build (must succeed)
cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j8

# 3. Run tests (if available)
cd build && ctest --output-on-failure
```

### 6.2. Convention Compliance Checklist

Before committing, agents MUST verify:

- [ ] **Dual API:** New public features have both C++ and C APIs
- [ ] **Examples updated:** New features demonstrated in examples
- [ ] **Constants used:** No hardcoded JSON keys or URLs
- [ ] **Formatting:** Code passes `clang-format` check
- [ ] **Naming conventions:** Variables, classes follow standards
- [ ] **License headers:** Present on all new files
- [ ] **No platform-specific code:** Or properly guarded with `#ifdef`

### 6.3. Commit & Push Workflow

```bash
# 1. Check status
git status

# 2. Stage changes (specific files, not -A)
git add include/scorbit_sdk/new_feature.h source/new_feature.cpp

# 3. Sync beads (if using)
bd sync

# 4. Commit with conventional message
git commit -m "feat(component): add new feature description"

# 5. Push to remote
git push

# 6. Verify push succeeded
git status  # MUST show "up to date with origin"
```

### 6.4. Commit Message Format

```
<type>(<scope>): <description>

Types:
- feat:     New feature
- fix:      Bug fix
- docs:     Documentation
- test:     Tests
- chore:    Maintenance
- refactor: Code restructuring

Examples:
- feat(achievements): add real-time achievement events
- fix(net): handle connection timeout gracefully
- docs(readme): update build instructions
```

### 6.5. Critical Rules

- Work is NOT complete until `git push` succeeds
- NEVER stop before pushing - that leaves work stranded locally
- If build fails, fix it before committing
- New public APIs MUST include examples

---

## 7. Build System

### 7.1. CMake Configuration

```bash
# Debug build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j8

# Release build
cmake .. -DCMAKE_BUILD_TYPE=Release -DSCORBIT_SDK_PRODUCTION=ON -DSCORBIT_SDK_ABI=glibc
make -j8

# Platform-specific scripts
./scripts/macos-build.sh
./scripts/u20-build.sh
```

### 7.2. Key CMake Options

| Option | Description |
|--------|-------------|
| `CMAKE_BUILD_TYPE` | Debug or Release |
| `SCORBIT_SDK_PRODUCTION` | Enable for release builds |
| `SCORBIT_SDK_SHARED` | Build shared library (default: ON) |
| `SCORBIT_SDK_ABI` | ABI tag (glibc, musl, etc.) |

---

## 8. Dependencies

The SDK fetches these via CMake FetchContent:

| Dependency | Purpose |
|------------|---------|
| `nlohmann/json` | JSON parsing |
| `fmtlib/fmt` | String formatting |
| `libcpr/cpr` | HTTP client |
| `centrifugo-cpp` | WebSocket messaging |
| `cryptoauthlib` | Hardware security |

**Do NOT add new dependencies without discussion.**

---

## 9. Quick Commands Reference

```bash
# Build
mkdir build && cd build && cmake .. && make -j8

# Format
clang-format -i source/*.cpp source/*.h

# Test
cd build && ctest

# Beads workflow
bd ready                        # Find available work
bd update <id> --status in_progress  # Claim work
bd close <id>                   # Complete work
bd sync                         # Sync with git
```

---

## 10. Key Files Reference

| File | Purpose |
|------|---------|
| `include/scorbit_sdk/scorbit_sdk.h` | Main C++ include |
| `include/scorbit_sdk/scorbit_sdk_c.h` | Main C include |
| `include/scorbit_sdk/game_state.h` | Core GameState C++ API |
| `include/scorbit_sdk/event.h` | Event handling C++ API |
| `source/net.cpp` | Network implementation |
| `source/identifiers.h` | Constants (URLs, JSON keys) |
| `source/event_classes.h` | Event class definitions |
| `examples/cpp_example/main.cpp` | C++ usage example |
| `examples/c_example/main.c` | C usage example |

---

*This protocol integrates with `AGENT_CONVENTIONS.md` for detailed coding standards.*
