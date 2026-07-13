# MRBNN UE Integration

This plugin adds the Unreal-side runtime layer for Extra-Creativity/MRBNN baked volumetric rendering data.

## What is included

- `UMRBNNBakedVolumeData`: points to an MRBNN working directory, for example `third_party_refs/MRBNN/data/cloud-03`, and validates the required `.bin` files.
- `UMRBNNVolumeComponent`: calls an optional native bridge, owns the output render target, and can build render descriptions for the UE-native compute path.
- `UMRBNNProjectSettings`: exposes global data paths, bake destination, optional native bridge preview defaults, and debug logging in Project Settings > Plugins > MRBNN. Paths can use `$(PluginDir)`, `$(ProjectDir)`, or `$(EngineDir)`.
- `AMRBNNVolumeActor`: a ready-to-drop volume actor. It bakes the bundled MRBNN density field and spatial baked feature files into transient `UVolumeTexture` objects, drives the default SceneViewExtension compute composite path, and synchronizes the scene directional light into per-actor relight parameters.
- `FMRBNNComputeRenderer` and `FMRBNNSceneViewExtension`: dispatch `/Plugin/MRBNN/Private/MRBNNComputeRender.usf` as an RDG GlobalShader pass and composite the generated cloud texture over SceneColor from a post-process callback.
- `MRBNNBridge`: a small C API wrapper around the original `RenderInterfaceWithTCNN` so Unreal does not compile CUDA code directly. It sets `MRBNN_RELA_PATH_ROOT` before creating the original renderer so relative volume paths in `config.json` resolve from the configured repository root.

The bundled example data lives in:

```text
Engine/Plugins/Experimental/MRBNN/Data/cloud-03
Engine/Plugins/Experimental/MRBNN/Data/volumes/cloud_03.bin
```

## Build the bridge DLL

To configure the local build environment in PowerShell, run:

```powershell
Engine/Plugins/Experimental/MRBNN/Scripts/Setup-MRBNNBuildEnv.ps1
```

If CUDA toolkit is missing on the machine, the setup script can install it through Chocolatey and persist the user-level CUDA environment variables:

```powershell
Engine/Plugins/Experimental/MRBNN/Scripts/Setup-MRBNNBuildEnv.ps1 -InstallCudaIfMissing -PersistUserEnvironment
```

After that, build the native bridge with:

```powershell
Engine/Plugins/Experimental/MRBNN/Scripts/Build-MRBNNBridge.ps1
```

The script configures CMake, builds `MRBNNBridge`, and deploys `MRBNNBridge.dll` plus `ExternalTCNN.dll` under:

```text
Engine/Plugins/Experimental/MRBNN/Binaries/ThirdParty/MRBNNBridge/Win64/
```

By default it now resolves the local GPU to a concrete CUDA architecture number, for example `120` on this machine, and passes that to both `CMAKE_CUDA_ARCHITECTURES` and `TCNN_CUDA_ARCHITECTURES`. You can still override that with `-CudaArchitectures`.
On Windows, use `-BuildDir` to keep CMake's binary tree short when tiny-cuda-nn/CUTLASS object paths exceed MSVC limits:

```powershell
Engine/Plugins/Experimental/MRBNN/Scripts/Build-MRBNNBridge.ps1 -BuildDir C:\MRBNNBridgeBuild -RunSmokeTest
```

The deployed CUDA runtime DLLs must be compatible with the installed display driver. If `nvidia-smi` reports CUDA 13.1, use a 13.1 toolkit/runtime for the smoke test; newer NVRTC DLLs can generate PTX that the driver rejects with `CUDA_ERROR_UNSUPPORTED_PTX_VERSION`.

To validate the native path before opening Unreal, run:

```powershell
Engine/Plugins/Experimental/MRBNN/Scripts/Build-MRBNNBridge.ps1 -RunSmokeTest
```

That builds `MRBNNBridgeSmokeTest`, renders a small frame from `data/cloud-03`, and writes `MRBNNBridgeSmokeTest.ppm` next to the deployed DLLs.
The smoke test now averages multiple frame indices before writing the image, then also converts it to `MRBNNBridgeSmokeTestPreview.png` for the example actor fallback preview. The default smoke-test preview is `1024x1024` with 256 averaged samples; use `-SmokeTestSize` and `-SmokeTestSamples` to trade quality for speed.
It also writes `MRBNNBridgeSmokeTest.ppm.rgba32f` before spatial denoise and `MRBNNBridgeSmokeTest.ppm.denoised.rgba32f` after spatial denoise. These raw float buffers are intended for numerical comparison while migrating the CUDA path into UE-native RDG/RHI shaders.
If `<work_dir>/skybox` exists, the smoke test also loads that baking directory and enables MRBNN's skybox-baking path during the render.

Manual equivalent:

```powershell
cmake -S Engine/Plugins/Experimental/MRBNN/Source/ThirdParty/MRBNNBridge `
  -B Engine/Plugins/Experimental/MRBNN/Intermediate/MRBNNBridge `
  -DMRBNN_ROOT=D:/_Gohot-UE/third_party_refs/MRBNN `
  -DCMAKE_BUILD_TYPE=Release

cmake --build Engine/Plugins/Experimental/MRBNN/Intermediate/MRBNNBridge --config Release --target MRBNNBridge -j 16
```

Copy the resulting `MRBNNBridge.dll` and `ExternalTCNN.dll` next to each other under:

```text
Engine/Plugins/Experimental/MRBNN/Binaries/ThirdParty/MRBNNBridge/Win64/
```

The Unreal module is intentionally not linked directly against CUDA. If the DLL is missing, the plugin stays loadable and reports a runtime error from `InitializeRenderer`.
Use `Is MRBNNBridge Available` to check the expected runtime DLL location from Blueprint.
When `MRBNNBridge.dll` and `ExternalTCNN.dll` are present under `Binaries/ThirdParty/MRBNNBridge/Win64`, the module now declares them as runtime dependencies so packaged builds can stage the native bridge alongside the plugin.

## Use in Unreal

1. Enable the `MRBNN Volumetric Renderer` plugin.
2. Open Project Settings > Plugins > MRBNN. The defaults point at `$(PluginDir)/Data/cloud-03`, so the bundled example works without absolute paths.
3. Open `/MRBNN/Examples/MRBNNVolumeExample`, or drop an `MRBNNVolumeActor` into any level.
4. The actor shows the UE-native compute volume in the editor through a SceneViewExtension post-process pass. No material raymarch actor path is created by default.
5. To use your own data, create or select a `MRBNNBakedVolumeData` asset in Project Settings, or assign it directly to the actor's `MRBNNVolume` component.
6. Select an `MRBNNVolumeActor` and run `Bake Current Data To Plugin Data` to package the selected baked files under the plugin's `Data` directory. The copy preserves the MRBNN `config.json` volume path layout.
7. Use `Apply Realtime Preview Settings` or `Apply Mobile Preview Settings` on the actor/component to reduce output size, sample count, compute steps, feature level, and shadow cost for realtime iteration.

The default display path is now a UE GlobalShader compute renderer, not a flat card, point proxy, or material-only raymarch. At construction time the actor reads the MRBNN `volume.path` entry from `config.json`, crops the effective density bounds, downsamples it into a transient 3D texture, and prepares it for the RDG compute pass. It also reads the paper-side spatial feature grids from `base.bin`, `ms0.bin`, and `ms1.bin`, using the same dense-grid level resolution and half-float packing rules as the original MRBNN `Encoding` loader. Those spatial features are compressed into a second RGBA volume texture: base feature energy, low-order multi-scatter energy, anisotropy proxy, and confidence.

`FMRBNNSceneViewExtension` subscribes to the Tonemap post-process pass, asks the actor for a per-view camera/render description, dispatches `MainCS` into an RDG cloud texture, then dispatches `MainCompositeCS` to blend that cloud over the current SceneColor. The single plugin shader file performs bounded volume traversal in the plugin-built 3D textures, a small shadow traversal along the scene directional light, a controllable Henyey-Greenstein-style phase response, baked-feature proxy lighting, and the final scene composite. The old cube-mesh material raymarch path has been removed from the runtime actor.

This is closer to the paper than a density-only cloud because it uses the paper's baked spatial radiance features and now runs through UE's render graph instead of a material preview. It is still an approximation: the full paper path samples `base`, `ms`, `view`, `light`, `hg`, `albedo`, and transmittance features and evaluates the trained MLP/TCNN decoder. The optional native bridge remains the closest implementation of that full neural decoder and now emits raw float buffers for parity checks; the default compute path is the game-friendly UE shader path that can run without CUDA.

To regenerate the bundled example map and validate it in CI or from PowerShell:

```powershell
D:\_Gohot-UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe D:\_Gohot-UE\Projects\MRBNNExample\MRBNNExample.uproject -run=PythonScript -script=D:/_Gohot-UE/Engine/Plugins/Experimental/MRBNN/Scripts/Setup-MRBNNPluginExample.py -unattended -nop4 -nosplash -NullRHI
D:\_Gohot-UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe D:\_Gohot-UE\Projects\MRBNNExample\MRBNNExample.uproject -run=PythonScript -script=D:/_Gohot-UE/Projects/MRBNNExample/Scripts/ValidateMRBNNExample.py -unattended -nop4 -nosplash -NullRHI
```

Project Settings > Plugins > MRBNN is intentionally global: default data roots, bake destination, native bridge render size/sample defaults, skybox defaults, and verbose debug logging live there.

Per-actor controls live on `MRBNNVolumeActor`:

- `MRBNN|Volume` and `MRBNN|Compute Volume`: bounds, density crop, texture resolution, compute steps, density shaping, opacity, and debug displays.
- `MRBNN|Direct Light` and `MRBNN|Relight`: direct light scale, shadow steps, shadow density, HG phase, ambient/direct balance, and scene directional light selection.
- `MRBNN|Paper Feature Proxy`: baked feature lighting enable, feature level, baked feature contribution, multi-scatter contribution, feature albedo blend, and baked feature tint.
- `Apply Realtime Preview Settings` and `Apply Mobile Preview Settings`: per-actor presets for desktop or mobile-friendly sampling.
- `Apply Paper Preview Settings`: a high-quality preset for the SceneViewExtension compute path, with baked feature lighting and higher sampling.

`Auto Initialize` attempts to create the native renderer once on BeginPlay. If the bridge DLL is missing, the component records `Last Error` and will stay quiet on subsequent ticks unless `Retry Failed Auto Initialize` is enabled or `InitializeRenderer` / `RenderOnce` is called manually.
`Use Player Camera` maps the active player camera into the owner actor's local space and divides by `World Units Per MRBNN Unit`; this lets moving a UE camera around the actor drive MRBNN's original orbit-style camera position.
`Apply Output To Materials` creates dynamic material instances on the listed primitive components and sets `MRBNNTexture` (or your chosen parameter name) to the output render target after each render. If the list is empty, the component can bind every primitive component on its owner actor.
`MRBNNVolumeComponent` can tick in editor viewports when `Allow Automatic Render In Editor` is enabled, but the example keeps automatic editor rendering off by default to avoid surprise CUDA work while editing.
