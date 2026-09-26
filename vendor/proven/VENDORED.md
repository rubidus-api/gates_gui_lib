# Vendored: proven_c_lib

- Upstream: `../proven_c_lib` (sibling local checkout; reference homepage: https://github.com/rubidus-api/proven_c_lib)
- Version: `proven_c_lib-v0.1.1` (git `22f964e`, tag `v0.1.1`)
- Vendored: 2026-09-22, full copy of `include/`, `src/`, `platform/` (was v26.07.23d / `c0e4d09` / 2026-07-25)
- License: MIT (see `LICENSE` here). `THIRD_PARTY_NOTICES.md` covers upstream's vendored `nob.h`.

## Rules

- Do not edit vendored sources inside gates_gui_lib. For defects, halt and report upstream (`../proven_c_lib/docs/REPORT.md` procedure), then re-vendor after the upstream fix.
- Re-vendor as a whole (all three directories from one upstream commit); never mix files from different upstream versions.
- Record version changes here and in `docs/resources/sources-and-licenses.md` (R001).

## Why

RFC-0001 §5: gates_gui_lib builds on proven_c_lib for allocator policy, result/error style, owned/borrowed strings, containers (array/ring/map), formatting/diagnostics, and cooperative task support. `gates_` wrapper types stay cheap to convert to/from proven types.

## What is compiled

Decided per phase in the Makefile (`PROVEN_SRC`). Phase 0 expected set: `memory`, `arena`, `panic`, `u8str`, `array` + `platform/proven_sys_mem`. Keep the compiled set minimal and warning-clean under `-std=c23 -Wall -Wextra`; do not compile modules no gates code includes.
