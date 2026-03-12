# Physics-First System Audit (2026-03-05)
Audit completed due to flight instabilities. All systems now passing tests and manual flight testing, acceptable outcome.

---

Severity legend:
- `S0`: Physics-invalid / safety-critical
- `S1`: High-impact correctness bug
- `S2`: Medium correctness/reliability risk
- `S3`: Tooling/ergonomics gap

## Underlying Systems

### 1) Flight Dynamics Core (`FlightDynamicsSystem`, `AeroModel`, `RigidBodyIntegrator`)
- `S1` (Resolved): Default-config drift between runtime and simulation defaults caused fixed-step inconsistency and physics divergence.
  - Repro: `lovec portSource/Tests --test FlightDynamics` previously failed fixed-step tick consistency.
  - Impact: Different behavior across frame rates and test/runtime mismatch.
  - Fix: Introduced shared default source in `portSource/Source/Sim/FlightModelDefaults.lua`; both `FlightDynamicsSystem` and `GameDefaults` now consume it.
  - Acceptance: `FlightDynamics` matrix test at 15/30/60/120 passes.

- `S2` (Mitigated): Extreme-parameter instability risk.
  - Repro: High-thrust/low-drag configs can amplify numerical error.
  - Impact: Potential NaN/runaway without guardrails.
  - Validation: Added stress test in `portSource/Tests/FlightDynamics.lua` verifying finite state and speed guard behavior under extreme but valid tuning.
  - Acceptance: Test passes.

### 2) Atmosphere / Propulsion / Trim
- `S2` (Validated): ISA density trend and thrust lapse behavior.
  - Repro: `portSource/Tests/AtmosphereAndPropulsion.lua`.
  - Impact: If incorrect, climb/cruise realism is invalid.
  - Status: Pass (density drops with altitude; thrust drops with speed and altitude).

### 3) Terrain SDF / Collision / Contact
- `S2` (Validated): Contact/crash thresholds on low-speed vs high-speed impact and slope normal response.
  - Repro: `portSource/Tests/FlightGroundContact.lua`.
  - Impact: Incorrect crash/contact behavior breaks gameplay realism and recovery logic.
  - Status: Pass (safe touchdown no crash, high-speed impact crash event, slope depenetration along normal).

- `S2` (Open Risk): Contact uses positional depenetration and velocity projection, not full CCD.
  - Impact: Very high-speed edge cases can still tunnel under atypical dt/config combinations.
  - Mitigation: Existing dt clamp, fixed substeps, and speed clamps reduce practical occurrence.
  - Acceptance target: No tunneling under bounded config ranges used in game and tests.

### 4) Terrain Generation/Streaming Determinism
- `S2` (Validated): SDF determinism and streaming continuity.
  - Repro: `portSource/Tests/TerrainSdfDeterminism.lua`, `portSource/Tests/TerrainChunkStreaming.lua`, `portSource/Tests/TerrainCollision.lua`.
  - Status: Pass.

### 5) Physics Networking (`STATE3` + interpolation/extrapolation)
- `S2` (Validated): Dynamic state decode and interpolation continuity.
  - Repro: `portSource/Tests/PacketCodecAndNetworking.lua`.
  - Added check: quaternion normalization and angular extrapolation coherence.
  - Status: Pass.

## Surface-Level Systems

### 1) Test Harness / Launch Reliability
- `S2` (Resolved): `portSource/Tests/main.lua` path resolution depended on process working directory.
  - Repro: Launching from repo root failed with missing test file paths.
  - Fix: Runner now uses `love.filesystem.load` instead of raw `dofile` path assumptions.
  - Acceptance: `lovec portSource/Tests --all` and `cd portSource/Tests && lovec . --all` both pass.

- `S3` (Resolved): No standardized launcher for exit-code capture.
  - Fix: Added:
    - `Scripts/run_love_tests.cmd`
    - `Scripts/run_love_tests.py`
  - Acceptance: wrappers print `[launcher] exit_code=<n>` and return same process code.

### 2) Input/Controls/View
- `S2` (Validated): Strict modifier behavior and rebind/reset contract.
  - Repro: `portSource/Tests/ControlsStrictModifiers.lua`, `portSource/Tests/ControlsRebind.lua`.
  - Status: Pass.

### 3) Restart Snapshot Compatibility
- `S2` (Validated): Migration and tuned-parameter persistence on same-version pass-through.
  - Repro: `portSource/Tests/RestartSnapshotCompat.lua`.
  - Status: Pass.

### 4) Render/HUD Contract Surface
- `S2` (Validated): CPU classifier and PBR/lighting contract checks.
  - Repro: `portSource/Tests/CpuRenderClassifier.lua`, `portSource/Tests/PbrLightingContracts.lua`.
  - Status: Pass.

## Acceptance Matrix (Current)
- Full suite: Pass
- Physics fixed-step invariance matrix (15/30/60/120): Pass
- Config parity (`flight.defaultConfig` vs runtime defaults): Pass
- Ground-contact/crash behavior checks: Pass
- Control remap + strict modifiers: Pass
- Restart snapshot compatibility + tuned pass-through: Pass
- Networking interpolation/extrapolation angular coherence: Pass
