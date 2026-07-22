# MRBNN Sky RT Architecture

This note records the planned sky-filling MRBNN cloud mode requested for replacing or complementing Unreal's Volumetric Cloud component. The current implementation remains a boxed `MRBNNVolumeActor` compute path; a useful sky component should land as a separate render source rather than adding more branches to the actor.

## Current Split

- Runtime module: `Source/MRBNN`
- Editor module: `Source/MRBNNEditor`
- CUDA/ImGui bridge: `Source/ThirdParty/MRBNNBridge`
- Boxed volume shader: `Shaders/Private/MRBNNComputeRender.usf`
- Scene composite: `FMRBNNSceneViewExtension`

The existing SceneView path already renders MRBNN cloud layers into RDG textures, composites multiple cloud actors, can copy debug RTs, and blends over SceneColor. That compositor should be reused by sky mode.

## Coupling To Remove First

`AMRBNNVolumeActor` currently owns data import, transient texture building, scene-light collection, render-desc assembly, preview presets, and render-extension lifecycle. A sky component should not inherit those responsibilities. The next cleanup should extract:

- `MRBNNPathResolver`: shared token expansion for `$(PluginDir)`, `$(ProjectDir)`, and `$(EngineDir)`.
- `MRBNNVolumeDataReader`: MRBNN `config.json`, raw density, bounds, and feature-grid metadata reads.
- `MRBNNVolumeTextureBuilder`: transient density/feature `UVolumeTexture` construction.
- `MRBNNSceneLightingCollector`: skylight, fog, atmosphere, directional, point, spot, and rect-light summaries.
- `MRBNNRenderSource`: a small interface consumed by `FMRBNNSceneViewExtension`.

## Proposed Sky Component

Add these files after the current volume renderer stabilizes:

```text
Source/MRBNN/Public/MRBNNSkyCloudComponent.h
Source/MRBNN/Private/MRBNNSkyCloudComponent.cpp
Source/MRBNN/Public/MRBNNSkyCloudTypes.h
Source/MRBNN/Private/MRBNNSkyCloudRender.cpp
Shaders/Private/MRBNNSkyCloudRender.usf
```

Primary controls should match the mental model of UE's volumetric cloud component:

- Layer: bottom altitude, layer height, planet radius or flat-layer fallback, max trace distance.
- Density: coverage, density multiplier, density offset, shape scale, detail scale, detail strength, erosion, anvil bias, lower/upper height fades.
- Lighting: phase, phase strength, multi-scattering octaves/contribution, silver lining, powder, sky ambient, ground occlusion.
- Wind: flow direction, flow speed, shear, temporal offset.
- Quality: primary steps, shadow steps, half-resolution mode, jitter, future reprojection blend.
- Output: one transient or user-provided float RGBA render target plus existing RT debug display.

## Render Path

1. `UMRBNNSkyCloudComponent` builds an `FMRBNNSkyRenderDesc` per view.
2. `FMRBNNSceneViewExtension` gathers registered cloud render sources, not only `AMRBNNVolumeActor`.
3. `MRBNNSkyCloudRender.usf` traces a sky-layer domain and writes a cloud RT.
4. Boxed MRBNN volumes render through the existing `MRBNNComputeRender.usf` path.
5. Sky clouds composite first as the far layer; boxed clouds composite after that using the shared cloud-over kernel.
6. The combined cloud RT can be copied to the existing debug preview target.
7. The final composite blends the combined cloud over SceneColor at the existing post-process hook.

## Density Model

The first sky pass should be procedural and not depend on MRBNN baked working directories:

- Compute height fraction inside the layer.
- Remap coverage into low-frequency shape noise.
- Multiply by a height-density profile.
- Subtract high-frequency erosion/detail near edges.
- Apply Beer-Lambert transmittance, the existing phase controls, multi-scatter proxies, and silver-lining boost.

Later work can replace or seed procedural density with baked Cloud Info volumes from the CUDA bake console. That import should use the sync manifest but remain optional.

## Boundaries

Sky mode may depend on the shared renderer, scene-light collector, SceneView compositor, and debug RT workflow. It should not depend on `UMRBNNBakedVolumeData`, MRBNN repository paths, the ThirdParty bridge, the ImGui console, or `AMRBNNVolumeActor`.

## Known Limitations

The current boxed renderer is an analytic approximation. It does not sample UE shadow maps, SkyAtmosphere LUTs, volumetric-fog voxel grids, IES profiles, light functions, rect source textures, reflection captures, or per-pixel renderer light lists. A sky RT component should treat those as future integration points rather than hiding them behind the current actor settings.

## Implementation Order

1. Extract path resolution.
2. Extract volume data/texture building.
3. Extract scene-light collection.
4. Add `MRBNNRenderSource` registration and update `FMRBNNSceneViewExtension`.
5. Add `UMRBNNSkyCloudComponent` and `MRBNNSkyCloudRender.usf`.
6. Add an example map only after the sky RT produces a nonblank render target in editor QA.
