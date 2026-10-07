# Workflow: Slop Cleanup Session 2.0

**Session Name**: `slop-cleanup-2.0`  
**Created**: 2026-10-07  
**Owner**: @nicknad (or assign)  
**Baseline**: commit 362f1be  
**Target Completion**: Before 1.0 release

---

## 🎯 Objectives

Address confirmed slop items identified in Review 2.0 (2026-10-07) before the 1.0 release.

---

## 📋 Work Items (Priority Order)

### Phase 1: HIGH Priority (Do Now - Before 1.0)

#### Task 1: REV2-R5 - Consolidate `TakeLast` in C++

**Objective**: Remove duplicate `TakeLast` function from `wait.cpp` and `wsl_container.cpp`

**Steps**:
1. **Locate**: Find `TakeLast` in both files
   ```bash
   grep -n "std::vector<LogLine> TakeLast" cpp/src/wait.cpp cpp/src/wsl_container.cpp
   ```

2. **Centralize**: Move to `cpp/src/internal/util.hpp`
   - Add declaration in header
   - Add implementation in `cpp/src/internal/util.cpp`
   - Use same namespace (`wslc::internal`)

3. **Update Callers**:
   - Replace local definitions with `#include "internal/util.hpp"`
   - Update function calls to use `internal::TakeLast`

4. **Verify**: Build and test
   ```bash
   # Build
   cd cpp/build
   cmake --build . --config Release
   
   # Run tests
   ctest -C Release -V
   ```

**Estimate**: 1-2 hours  
**Files Modified**: 
- `cpp/src/internal/util.hpp` (+ declaration)
- `cpp/src/internal/util.cpp` (+ implementation)
- `cpp/src/wait.cpp` (- duplicate, + include)
- `cpp/src/wsl_container.cpp` (- duplicate, + include)

---

#### Task 2: REV2-CPP-FMT - Add Endpoint Formatting Helpers

**Objective**: Eliminate duplicate connection string/endpoint formatting across 15 modules

**Steps**:
1. **Add Helpers** to `cpp/include/wslc/wsl_module_container.hpp`:
   ```cpp
   protected:
       /// <summary>Renders an endpoint as host:port for modules without credentials.</summary>
       static std::string FormatEndpoint(WslEndpoint endpoint);
       
       /// <summary>Renders a connection string for modules with credentials.</summary>
       static std::string FormatConnectionString(
           WslEndpoint endpoint,
           const std::string& username,
           const std::string& password,
           const std::string& database);
   ```

2. **Implement** in `cpp/src/wsl_module_container.cpp` (or inline in header if simple):
   ```cpp
   std::string WslModuleContainer::FormatEndpoint(WslEndpoint endpoint) {
       return endpoint.Host + ":" + std::to_string(endpoint.Port);
   }
   
   std::string WslModuleContainer::FormatConnectionString(
       WslEndpoint endpoint,
       const std::string& username,
       const std::string& password,
       const std::string& database) {
       return "Host=" + endpoint.Host + 
              ";Port=" + std::to_string(endpoint.Port) +
              ";Username=" + username +
              ";Password=" + password +
              ";Database=" + database;
   }
   ```

3. **Update Modules**: For each of the 15 modules:
   - Replace manual string construction with helper calls
   - Modules using `GetEndpoint()` → use `FormatEndpoint()`
   - Modules using `GetConnectionString()` → use `FormatConnectionString()`

4. **Verify**: Build and test all modules

**Estimate**: 2-3 hours  
**Files Modified**: 
- `cpp/include/wslc/wsl_module_container.hpp` (+ helpers)
- `cpp/src/wsl_module_container.cpp` (+ implementation)
- 15 module files in `cpp/modules/*/` (update to use helpers)

**Modules to Update**:
- ClickHouse, Elasticsearch, Keycloak, MailPit, MariaDB, MongoDB
- NATS, PostgreSQL, Qdrant, RabbitMQ, Redis, RustFs
- Valkey, Vault, WireMock

---

### Phase 2: MEDIUM Priority (Verify)

#### Task 3: REV2-R6-CHECK - Verify `IsOwnerAlive(int)` Usage

**Objective**: Check if `IsOwnerAlive(int)` in C++ reaper logic is unused

**Steps**:
1. **Search** for all callers:
   ```bash
   grep -r "IsOwnerAlive" cpp/src --include="*.cpp" --include="*.hpp"
   ```

2. **Analyze**: Check if `int` overload has any callers

3. **Action**:
   - If unused: Remove from `cpp/src/internal/reaper_logic.hpp`
   - If used: Document why it's needed (C# removed theirs in commit 79e2bea)

**Estimate**: 1 hour  
**Files Modified**: 
- `cpp/src/internal/reaper_logic.hpp` (potentially -1 function)

---

## ✅ Acceptance Criteria

### For Each Task
- [ ] Code compiles without warnings
- [ ] All existing tests pass
- [ ] No regression in functionality
- [ ] Code follows project conventions (clang-format clean)

### For Phase 1
- [ ] `TakeLast` centralized in `internal/util.hpp`
- [ ] All module endpoints use helper methods
- [ ] Build and tests pass

---

## 🚀 Getting Started

### Prerequisites
```bash
# Clone and setup
cd C:\Users\nadol\repos\WslContainer
git checkout development  # or main

# C++ build dependencies
cd cpp
# Ensure CMake, Visual Studio 2022, WSL 2.9.3+ with container support
```

### Recommended Workflow
```bash
# 1. Create feature branch
git checkout -b chore/slop-cleanup-2.0

# 2. Work on Task 1 (TakeLast consolidation)
# ... make changes ...

# 3. Commit incrementally
git add -A
git commit -m "chore: consolidate TakeLast in C++ internal/util"

# 4. Work on Task 2 (Endpoint formatting)
# ... make changes ...

git add -A
git commit -m "chore: add endpoint formatting helpers to WslModuleContainer"

# 5. Verify
git push origin chore/slop-cleanup-2.0
```

---

## 📊 Tracking

Use the todo tool to track progress:
```
todo write [
  {"id": "REV2-R5", "content": "Consolidate TakeLast in C++", "status": "pending", "priority": "high"},
  {"id": "REV2-CPP-FMT", "content": "Add endpoint formatting helpers", "status": "pending", "priority": "high"},
  {"id": "REV2-R6-CHECK", "content": "Verify IsOwnerAlive(int) usage", "status": "pending", "priority": "medium"}
]
```

---

## 🔄 Next Session Handover

To start this workflow in a new session:

1. **Load the workflow**:
   ```
   Read docs/WORKFLOW-SLOP-CLEANUP.md
   ```

2. **Review objectives**: Understand Phase 1 tasks (REV2-R5, REV2-CPP-FMT)

3. **Setup environment**:
   - Check out `chore/slop-cleanup-2.0` branch (or create it)
   - Ensure C++ build dependencies are available

4. **Start with Task 1**: Consolidate `TakeLast`

5. **Track progress** using todo tool

---

## 📚 Reference

- **Review Document**: `docs/REVIEW-2026-10-07.md`
- **Review Ledger**: `docs/review-ledger.md` (updated with Review 2.0 section)
- **Project Conventions**: `AGENTS.md`
- **ADRs**: `docs/adr/` (especially ADR-0006, ADR-0007)

---

## 💡 Tips

1. **Follow C# Patterns**: C# already solved these issues (commit 79e2bea) — use the same approach in C++
2. **Test Incrementally**: Build and test after each change
3. **Use clang-format**: Run formatter after changes
   ```bash
   cd cpp
   cmake -DCMAKE_BUILD_TYPE=Release -S . -B build
   cmake --build build --target format
   ```
4. **Check CI**: Verify changes don't break CI workflows

---

*Workflow created: 2026-10-07 | Session: slop-cleanup-2.0*