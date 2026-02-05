# Scorbit SDK - Project Governance & Conventions (RFC 2119)

This document establishes the coding standards and conventions for the Scorbit SDK. All AI agents and developers MUST adhere to these rules to maintain architectural integrity.

## 0. Development Environment

### 0.1. Build System

The SDK uses **CMake** (minimum version 3.28) with **C++17** standard.

| Task | Command |
|------|---------|
| Configure (Debug) | `mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug` |
| Configure (Release) | `mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release` |
| Build | `cd build && make -j8` |
| macOS Build | `./scripts/macos-build.sh` |
| Ubuntu Build | `./scripts/u20-build.sh` |

### 0.2. Code Formatting

- **Formatter**: Code MUST be formatted with `clang-format` using the project's `.clang-format` configuration
- **Style**: Qt/WebKit-based style with 100 character column limit
- **CMake Files**: Use `gersemi` for CMakeLists.txt formatting (see `.gersemirc`)

Run formatting:
```bash
clang-format -i source/*.cpp source/*.h include/scorbit_sdk/*.h
```

## 1. Project Structure

```
scorbit_sdk/
├── include/scorbit_sdk/   # Public headers (API surface)
│   ├── *_c.h              # C API headers
│   └── *.h                # C++ API headers
├── source/                # Implementation files (private)
│   ├── *_c.cpp            # C wrapper implementations
│   └── *.cpp/*.h          # C++ implementations
├── examples/              # Example applications
│   ├── cpp_example/       # C++ usage example
│   └── c_example/         # C usage example
├── tests/                 # Unit tests
├── cmake/                 # CMake modules
├── scripts/               # Build scripts
└── documentation/         # Doxygen documentation
```

### 1.1. Header Organization

| Location | Purpose | Visibility |
|----------|---------|------------|
| `include/scorbit_sdk/*.h` | Public API headers | Installed, user-facing |
| `source/*.h` | Private implementation headers | Internal only |

## 2. C++ Coding Standards

### 2.1. Language Standard

- **C++ Standard**: C++17 MUST be used (`CMAKE_CXX_STANDARD 17`)
- **Compiler Support**: GCC 7+, Clang 5+, MSVC 2017+

### 2.2. Naming Conventions

| Element | Convention | Example |
|---------|------------|---------|
| Classes | PascalCase | `GameState`, `EventManager` |
| Member variables | m_ prefix + camelCase | `m_sessionUuid`, `m_isAuthenticated` |
| Local variables | camelCase | `sessionData`, `playerCount` |
| Constants | UPPER_SNAKE_CASE | `URL_API`, `JKEY_PLAYER_ID` |
| Functions/Methods | camelCase | `setScore()`, `getPlayerInfo()` |
| Namespaces | lowercase | `scorbit`, `scorbit::detail` |
| Enums | PascalCase values | `AuthStatus::Authenticated` |
| File names | snake_case | `game_state.cpp`, `event_manager.h` |

### 2.3. Namespace Organization

```cpp
namespace scorbit {           // Public API
namespace detail {            // Internal implementation (not for external use)
} // namespace detail
} // namespace scorbit
```

- Public API MUST be in `scorbit` namespace
- Internal implementation SHOULD be in `scorbit::detail` namespace
- Closing braces MUST have namespace comment

### 2.4. Class Structure

```cpp
class ClassName
{
public:
    // Constructors/Destructors
    ClassName();
    ~ClassName();

    // Public methods
    void publicMethod();

private:
    // Private methods
    void privateHelper();

private:
    // Member variables (m_ prefix)
    std::string m_memberVar;
};
```

### 2.5. Include Order

```cpp
// 1. Corresponding header (for .cpp files)
#include "my_class.h"

// 2. Project headers
#include <scorbit_sdk/game_state.h>
#include "internal_helper.h"

// 3. Third-party headers
#include <nlohmann/json.hpp>
#include <fmt/format.h>

// 4. Standard library
#include <string>
#include <vector>
```

### 2.6. Error Handling

- Use exceptions sparingly; prefer error codes/callbacks for API boundaries
- Internal errors SHOULD use the logging macros: `INF()`, `WRN()`, `ERR()`, `DBG()`
- Network errors MUST be handled gracefully without crashing

## 3. Dual API Pattern (C++ and C)

The SDK provides both C++ and C APIs. This pattern MUST be followed for all public features.

### 3.1. C++ API (Primary)

Located in `include/scorbit_sdk/*.h` (without `_c` suffix):

```cpp
// include/scorbit_sdk/event.h
namespace scorbit {
class Event {
public:
    EventType type() const;
    bool getAchievementUnlocked(std::string &key, std::string &name, ...) const;
};
} // namespace scorbit
```

### 3.2. C API (Wrapper)

Located in `include/scorbit_sdk/*_c.h`:

```c
// include/scorbit_sdk/event_helpers_c.h
#ifdef __cplusplus
extern "C" {
#endif

sb_event_type_t sb_event_type(const sb_event_t *event);
bool sb_event_achievement_unlocked(const sb_event_t *event, const char **key, ...);

#ifdef __cplusplus
}
#endif
```

### 3.3. C API Implementation

Located in `source/*_c.cpp`:

```cpp
// source/event_helpers_c.cpp
#include <scorbit_sdk/event_helpers_c.h>
#include "event_classes.h"

sb_event_type_t sb_event_type(const sb_event_t *event)
{
    const auto eventBase = static_cast<const detail::EventBase *>(event);
    return static_cast<sb_event_type_t>(eventBase->type());
}
```

### 3.4. Type Naming Convention

| C++ Type | C Type |
|----------|--------|
| `scorbit::EventType` | `sb_event_type_t` |
| `scorbit::GameState` | `sb_game_handle_t` |
| `scorbit::Error` | `sb_error_t` |
| `scorbit::AuthStatus` | `sb_auth_status_t` |

C types MUST use `sb_` prefix and `_t` suffix.

## 4. Event System

### 4.1. Adding New Event Types

1. **Add C enum value** in `include/scorbit_sdk/event_types_c.h`:
   ```c
   typedef enum {
       SB_EVT_NEW_EVENT = 100,
   } sb_event_type_t;
   ```

2. **Add C++ enum value** in `include/scorbit_sdk/event_types.h`:
   ```cpp
   enum class EventType {
       NewEvent = SB_EVT_NEW_EVENT,
   };
   ```

3. **Create event class** in `source/event_classes.h`:
   ```cpp
   class NewEventClass : public EventBase {
   public:
       NewEventClass(/* params */) : EventBase(EventType::NewEvent), /* init */ {}
       // Getters...
   private:
       // Data members...
   };
   ```

4. **Add C helper function** declaration in `include/scorbit_sdk/event_helpers_c.h`

5. **Implement C helper** in `source/event_helpers_c.cpp`

6. **Add C++ wrapper method** in `include/scorbit_sdk/event.h`

7. **Update examples** in `examples/cpp_example/main.cpp` and `examples/c_example/main.c`

### 4.2. Event Callback Pattern

Events are delivered via callback registered during configuration:

```cpp
// C++ callback signature
void eventsCallback(const scorbit::Event &event);

// C callback signature
void eventsCallback(const sb_event_t *event, void *user_data);
```

## 5. Network & Centrifugo Integration

### 5.1. Channel Naming

Channel names are defined as constants in `source/identifiers.h`:

```cpp
constexpr auto CF_CHN_MACHINE {"machine"};
constexpr auto CF_CHN_ACHIEVEMENTS_SESSION {"achievements:session:"};
```

### 5.2. JSON Keys

All JSON keys MUST be defined as constants:

```cpp
constexpr auto JKEY_PLAYER_ID {"id"};
constexpr auto JVAL_CHN_TYPE_START_GAME {"start_game"};
```

Naming convention:
- `JKEY_*` - JSON object keys
- `JVAL_*` - JSON literal values

### 5.3. URL Endpoints

```cpp
constexpr auto URL_SCORBITRON_TOKEN {"v2/scorbitrons/{scorbitron_uuid}/token/"};
```

Use `fmt::arg()` for parameter substitution with named arguments.

## 6. Testing

### 6.1. Test Framework

Tests use Google Test (gtest) framework.

### 6.2. Running Tests

```bash
cd build
ctest --output-on-failure
```

## 7. Git & Pull Request Conventions

### 7.1. Branch Naming

- `feature/<description>` - New features
- `fix/<description>` - Bug fixes
- `chore/<description>` - Maintenance tasks

### 7.2. Commit Messages

Follow conventional commits format:

```
feat(component): add new feature
fix(component): fix bug description
chore(component): maintenance task
docs(component): documentation update
test(component): add/update tests
```

### 7.3. PR Requirements

- PRs MUST compile without errors on all supported platforms
- PRs MUST include example updates for new public API features
- PRs SHOULD update both C++ and C APIs together

### 7.4. Files That MUST NOT Be Modified Without Explicit Request

- `.github/workflows/` - CI/CD pipelines
- `cmake/` - CMake modules (unless build system changes required)
- `VERSION` - Version file (managed by release process)

## 8. Dependencies

The SDK uses these key dependencies (fetched via CMake FetchContent):

| Dependency | Purpose |
|------------|---------|
| `nlohmann/json` | JSON parsing |
| `fmtlib/fmt` | String formatting |
| `libcpr/cpr` | HTTP client |
| `centrifugo-cpp` | WebSocket real-time messaging |
| `spdlog` (via logger) | Logging |

New dependencies MUST be discussed before adding.

## 9. Documentation

### 9.1. Code Comments

- Use `//` for single-line comments
- Use `/* */` for multi-line comments
- Doxygen-style comments (`/** */`) for public API documentation

### 9.2. License Header

All source files MUST include the MIT license header:

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

---
*Generated for Scorbit SDK v1.99.51+*
