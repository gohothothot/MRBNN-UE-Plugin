# MRBNN UE Plugin

Unreal Engine runtime plugin for previewing Extra-Creativity MRBNN volumetric data through a UE-native GlobalShader/RDG compute render path.

## What is included

- `MRBNNVolumeActor`: ready-to-drop actor for the bundled volume example.
- `Content/Examples/MRBNNVolumeExample.umap`: plugin-contained example map.
- `Shaders/Private/MRBNNComputeRender.usf`: compute volume renderer plus SceneColor composite shader.
- `MRBNNComputeRenderer` and `MRBNNSceneViewExtension`: RDG GlobalShader dispatch and post-process integration.
- `Content/Materials/M_MRBNN_VolumeRaymarch.uasset`: fallback/debug material, kept for comparison and editor fallback only.
- `Data/cloud-03` and `Data/volumes/cloud_03.bin`: bundled example data.
- `Project Settings > Plugins > MRBNN`: global data paths, bake destination, native bridge preview defaults, and debug logging.
- `Scripts/Setup-MRBNNBuildEnv.ps1`: local CUDA/Visual Studio/CMake environment setup.
- `Scripts/Build-MRBNNBridge.ps1`: optional native CUDA bridge build and smoke test.
- `Scripts/Setup-MRBNNPluginExample.py`: self-contained editor script that rebuilds the plugin material and example map.

The default visible path does not require the native CUDA bridge. It reads the bundled density volume, builds transient density and baked-feature `UVolumeTexture` objects, dispatches a UE GlobalShader compute pass, and composites the result into the scene through `FSceneViewExtensionBase` after tonemapping.

The UE-native compute renderer is an approximation of the MRBNN paper path, not the full TCNN decoder. It uses the density field plus spatial baked proxy features derived from `base.bin`, `ms0.bin`, and `ms1.bin`; per-actor controls blend those features into ambient multi-scattering, phase response, tint, and direct-light shadowing. The optional CUDA bridge remains the reference path for full neural-decoder parity work.

## Install

Clone or copy this folder into either:

```text
<UnrealEngineRoot>/Engine/Plugins/Experimental/MRBNN
```

or:

```text
<YourProject>/Plugins/MRBNN
```

Enable `MRBNN Volumetric Renderer`, restart the editor if prompted, and open:

```text
/MRBNN/Examples/MRBNNVolumeExample
```

You can also place an `MRBNNVolumeActor` in any level. With the default project settings it uses plugin-relative data under `Data/`. Volume shape, compute sampling quality, direct light response, baked-feature strength, and mobile/realtime presets are adjusted per actor.

## Optional native bridge

The CUDA bridge is optional and is not needed for the default UE compute preview.

To configure the local build environment:

```powershell
Scripts/Setup-MRBNNBuildEnv.ps1
```

To let the script install CUDA through Chocolatey when it is missing:

```powershell
Scripts/Setup-MRBNNBuildEnv.ps1 -InstallCudaIfMissing -PersistUserEnvironment
```

Build and deploy the bridge DLLs:

```powershell
Scripts/Build-MRBNNBridge.ps1
```

Run the smoke test:

```powershell
Scripts/Build-MRBNNBridge.ps1 -RunSmokeTest
```

On Windows, pass a short build directory if CMake or MSVC hits path-length limits while building tiny-cuda-nn/CUTLASS:

```powershell
Scripts/Build-MRBNNBridge.ps1 -BuildDir C:\MRBNNBridgeBuild -RunSmokeTest
```

Use a CUDA toolkit/runtime that is supported by the installed NVIDIA driver. If `nvidia-smi` reports CUDA 13.1, deploying CUDA 13.3 `nvrtc*.dll` can make tiny-cuda-nn's runtime JIT fail with `CUDA_ERROR_UNSUPPORTED_PTX_VERSION`.

The smoke test writes a preview image plus raw float RGBA buffers next to the deployed DLLs. Use `MRBNNBridgeSmokeTest.ppm.rgba32f` and `MRBNNBridgeSmokeTest.ppm.denoised.rgba32f` as numeric references for later UE-native shader work.

For the closest available UE-native preview inside Unreal, place or select an `MRBNNVolumeActor` and run `Apply Paper Preview Settings`. The actor uses the SceneViewExtension compute composite path by default. Use the CUDA/TCNN bridge smoke outputs as numeric and visual references while migrating the remaining neural decoder pieces into UE shaders.

## Rebuild plugin assets

When the material-generation script changes, run the Python setup script from an Unreal project that has this plugin enabled. In the original development workspace this is:

```powershell
D:\_Gohot-UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe D:\_Gohot-UE\Projects\MRBNNExample\MRBNNExample.uproject -run=PythonScript -script=D:/_Gohot-UE/Engine/Plugins/Experimental/MRBNN/Scripts/Setup-MRBNNPluginExample.py -unattended -nop4 -nosplash -NullRHI
```

## More docs

See `Docs/MRBNN_UE_Integration.md` for the full integration notes.
