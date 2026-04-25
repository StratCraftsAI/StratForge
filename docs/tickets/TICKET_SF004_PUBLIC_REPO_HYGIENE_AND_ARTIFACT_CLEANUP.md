# TICKET_SF004: Public Repository Hygiene and Artifact Cleanup

**Status**: Implemented
**Priority**: High
**Category**: Open Source / Repo Hygiene

---

## Motivation

Public repositories should not expose confusing duplicate artifact trees or ambiguous generated outputs. Extra files increase clone size, create uncertainty about the source of truth, and make review harder.

StratForge had a duplicated golden-output tree under `tools/golden_extract/output/output/` that appeared to be a generated copy rather than canonical reference data.

---

## Goals

- Remove duplicate generated artifacts from the public worktree
- Keep the canonical golden output location clear
- Prevent obvious repository confusion before public release expansion

---

## Implemented Scope

- Removed duplicated `tools/golden_extract/output/output/` tree from the working copy
- Added explicit ticket documentation so the cleanup is part of repository history
- Preserved canonical output data under `tools/golden_extract/output/`

---

## Success Criteria

| Criterion | Status |
|----------|--------|
| No duplicate `output/output` tree in public worktree | Done |
| Canonical golden data path remains intact | Done |

---

## Follow-Up

- If the extraction tooling still regenerates nested output trees, patch the tool next rather than re-cleaning by hand
