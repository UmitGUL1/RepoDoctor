# Contributing to RepoDoctor

Thank you for your interest in contributing to **RepoDoctor** (*Scan. Diagnose. Improve.*) — an open-source, cross-platform repository health analyzer.

---

## 1. Development Prerequisites

- **C++20 Compiler**: GCC 12+, Clang 15+, or MSVC 2022+
- **CMake**: 3.20 or newer (Ninja recommended)
- **Dependencies**:
  - `nlohmann/json` 3.11+ (bundled in `third_party/nlohmann` as a fallback)
  - `libgit2` 1.5+ (optional; automatic filesystem fallback when disabled)
  - `GoogleTest` 1.12+ (for running unit & integration tests)
  - `Qt 6.4+` (`Core`, `Gui`, `Qml`, `Quick`, `QuickControls2`, `Widgets`) for building the desktop GUI

---

## 2. Building and Testing Locally

```bash
# Configure and build CLI, Qt6 GUI, Tests, and Benchmarks
cmake -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux --parallel

# Run GoogleTest suite
ctest --test-dir build/linux --output-on-failure

# Run AddressSanitizer + UndefinedBehaviorSanitizer checks
cmake -B build/sanitizers -G Ninja -DCMAKE_BUILD_TYPE=Debug -DREPODOCTOR_ENABLE_SANITIZERS=ON -DREPODOCTOR_BUILD_GUI=OFF
cmake --build build/sanitizers --parallel
ctest --test-dir build/sanitizers --output-on-failure
```

---

## 3. Coding Standards & Static Analysis

- Follow `.clang-format` (Google-based, 4-space indentation, 100-column limit) and `.clang-tidy`.
- Keep `repodoctor_core` strictly independent of Qt headers and types.
- Every diagnostic rule must implement `IRule` with a stable ID (`RD-<CAT>-<NNN>`), clear remediation guidance, and false-positive considerations.
- See [docs/rule-development.md](docs/rule-development.md) for a step-by-step guide on authoring new rules.

---

## 4. Pull Request Checklist

1. Zero compiler warnings under `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion`.
2. Unit tests added in `tests/repodoctor_tests.cpp` covering both true positives and false-positive edge cases.
3. No real credentials or live secrets committed in test fixtures (use runtime-constructed synthetic tokens).
4. `repodoctor-cli scan .` passes cleanly against the repository itself.
