# feature/data-sections

Based on `main` (v0.10.0).

## Why

Some titles link non-code data into sections flagged executable. Lara Croft
and the Guardian of Light keeps Xenos shader data in its `.shad` section.
Decoding that as PowerPC produced thousands of invalid-instruction warnings
and 235 empty stub functions from bogus branch targets.

Separately, a project that builds the SDK in-tree (`REXSDK_DIR`) failed its
macOS post-build step, because `REXGLUE_ROOT` is only set in the SDK's own
directory scope.

## What

- `analysis.data_sections` codegen option: section names listed under
  `[entrypoint.analysis]` (or a module's analysis table) lose their
  executable flag before analysis.

  ```toml
  data_sections = [".shad"]
  ```

  Unknown names log a warning. The list is part of the codegen fingerprint,
  so changing it regenerates output.
- `rexglue_helpers.cmake` finds `MoltenVK_icd.json` relative to itself
  (`CMAKE_CURRENT_FUNCTION_LIST_DIR`), not through `REXGLUE_ROOT`.

## Status

Both are general fixes, suitable for upstream.
