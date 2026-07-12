# MRBNN UE Integration

This plugin adds the Unreal-side runtime layer for Extra-Creativity/MRBNN baked volumetric rendering data.

## What is included

- `UMRBNNBakedVolumeData`: points to an MRBNN working directory, for example `third_party_refs/MRBNN/data/cloud-03`, and validates the required `.bin` files.
- `UMRBNNVolumeComponent`: calls an optional native bridge and publishes the rendered frame as a transient `UTexture2D` and `UTextureRenderTarget2D` (`PF_FloatRGBA`).
- `UMRBNNProjectSettings`: exposes default data paths, realtime quality, volume texture baking, raymarch shader quality, and relight controls in Project Settings > Plugins > MRBNN. Paths can use `$(PluginDir)`, `$(ProjectDir)`, or `$(EngineDir)`.
- `AMRBNNVolumeActor`: a ready-to-drop volume actor. It bakes the bundled MRBNN density field into a transient `UVolumeTexture`, renders it with the plugin raymarch material, and synchronizes the scene directional light into the relight parameters.
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

To validate the native path before opening Unreal, run:

```powershell
Engine/Plugins/Experimental/MRBNN/Scripts/Build-MRBNNBridge.ps1 -RunSmokeTest
```

That builds `MRBNNBridgeSmokeTest`, renders a small frame from `data/cloud-03`, and writes `MRBNNBridgeSmokeTest.ppm` next to the deployed DLLs.
The smoke test now averages multiple frame indices before writing the image, then also converts it to `MRBNNBridgeSmokeTestPreview.png` for the example actor fallback preview. The default smoke-test preview is `1024x1024` with 256 averaged samples; use `-SmokeTestSize` and `-SmokeTestSamples` to trade quality for speed.
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
4. The actor shows the raymarched volume shader immediately in the editor. To exercise the optional native MRBNN bridge output path, select the actor and run `Render Preview Once`; automatic bridge rendering is disabled by default for realtime safety.
5. To use your own data, create or select a `MRBNNBakedVolumeData` asset in Project Settings, or assign it directly to the actor's `MRBNNVolume` component.
6. Select an `MRBNNVolumeActor` and run `Bake Current Data To Plugin Data` to package the selected baked files under the plugin's `Data` directory. The copy preserves the MRBNN `config.json` volume path layout.
7. Use `Apply Realtime Preview Settings` or `Apply Mobile Preview Settings` on the actor/component to reduce output size, sample count, and skybox cost for realtime iteration.

The default display path is now a realtime volume shader, not a flat card or point proxy. At construction time the actor reads the MRBNN `volume.path` entry from `config.json`, crops the effective density bounds, downsamples it into a transient 3D texture, and assigns that texture to `/MRBNN/Materials/M_MRBNN_VolumeRaymarch`. The material raymarches a cube mesh, uses `SampleLevel` on a `Texture3D`, applies a soft edge fade, and does a simple directional-light relight from the level's `DirectionalLight`.

To regenerate the bundled material/maps and validate the example map in CI or from PowerShell:

```powershell
D:\_Gohot-UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe D:\_Gohot-UE\Projects\MRBNNExample\MRBNNExample.uproject -run=PythonScript -script=D:/_Gohot-UE/Projects/MRBNNExample/Scripts/SetupMRBNNExample.py -unattended -nop4 -nosplash -NullRHI
D:\_Gohot-UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe D:\_Gohot-UE\Projects\MRBNNExample\MRBNNExample.uproject -run=PythonScript -script=D:/_Gohot-UE/Projects/MRBNNExample/Scripts/ValidateMRBNNExample.py -unattended -nop4 -nosplash -NullRHI
```

Useful Project Settings > Plugins > MRBNN controls:

- `Volume Raymarch Texture Resolution`: 80 by default for realtime desktop; 48 is the mobile preset.
- `Volume Raymarch Step Count`: 40 by default; mobile preset uses 22.
- `Volume Raymarch Fit To Density Bounds`: crops sparse source data before downsampling, so cloud data fills the actor bounds instead of becoming a thin slice.
- `Volume Raymarch Bounds Threshold` and `Volume Raymarch Bounds Padding`: control that crop.
- `Volume Raymarch Input Threshold`, `Normalize Density`, `Density Power`, and `Opacity`: shape the softness and thickness of the volume.
- `Volume Ambient Relight`, `Directional Relight`, `Shadow Strength`, `Light Step`, and `Cloud Color`: control the simple relight model.

`Auto Initialize` attempts to create the native renderer once on BeginPlay. If the bridge DLL is missing, the component records `Last Error` and will stay quiet on subsequent ticks unless `Retry Failed Auto Initialize` is enabled or `InitializeRenderer` / `RenderOnce` is called manually.
`Use Player Camera` maps the active player camera into the owner actor's local space and divides by `World Units Per MRBNN Unit`; this lets moving a UE camera around the actor drive MRBNN's original orbit-style camera position.
`Apply Output To Materials` creates dynamic material instances on the listed primitive components and sets `MRBNNTexture` (or your chosen parameter name) to the output render target after each render. If the list is empty, the component can bind every primitive component on its owner actor.
`MRBNNVolumeComponent` can tick in editor viewports when `Allow Automatic Render In Editor` is enabled, but the example keeps automatic editor rendering off by default to avoid surprise CUDA work while editing.
