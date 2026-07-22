# MRBNN Bake Console Design Notes

## Surface Intent

MRBNNBakeConsole is a native Dear ImGui production tool for cloud bake setup,
preview, and UE sync handoff. The surface should read as a dense technical
workbench: quiet dark base, clear tabbed workflow, large path fields, compact
numeric controls, and explicit status feedback.

## Tokens

- Background: near-black charcoal viewport with slightly lifted panels.
- Primary accent: desaturated blue for selected tabs and active controls.
- Action accent: warm amber for one-click bake/sync actions.
- Success/status accent: muted teal for generated artifact paths.
- Radius: small ImGui rounding only; keep controls rectangular and stable.
- Spacing: use ImGui item spacing and separators to create scan groups rather
  than decorative cards.

## Workflow Layout

- Top of the main panel always exposes language, CUDA status, and scene.
- Tabs divide work into Setup, Render, Cloud Bake/Sync, and Actions.
- Long paths get full-width rows and never share a line with labels.
- The Cloud Bake/Sync tab owns generator presets, reproducibility controls,
  bake resolution, output locations, and sync manifest behavior.
- The Preview and Log windows remain separate so render feedback does not
  collapse the control surface.

## Bilingual Behavior

- English and Simplified Chinese are selectable at runtime.
- Labels switch together; generated manifest keys remain stable English.
- The app attempts to load a Windows CJK font so Chinese text renders without
  tofu boxes.

## Sync Contract

- `mrbnn_bake_sync_manifest.json` is the single UE handoff file.
- It is written next to the Cloud Info output and may also be copied to the
  active work directory for editor-side discovery.
- Generator controls must be recorded in the manifest so a UE import can tell
  whether the raw density was source-only or sculpted.
