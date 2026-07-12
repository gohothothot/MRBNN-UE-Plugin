# MRBNN UE Plugin

Unreal Engine runtime plugin for previewing Extra-Creativity MRBNN volumetric data as a realtime raymarched volume shader.

## What is included

- `MRBNNVolumeActor`: ready-to-drop actor for the bundled volume example.
- `Content/Examples/MRBNNVolumeExample.umap`: plugin-contained example map.
- `Content/Materials/M_MRBNN_VolumeRaymarch.uasset`: final raymarched volume material.
- `Data/cloud-03` and `Data/volumes/cloud_03.bin`: bundled example data.
- `Project Settings > Plugins > MRBNN`: default paths, bake destination, raymarch quality, relight, realtime/mobile presets.
- `Scripts/Setup-MRBNNBuildEnv.ps1`: local CUDA/Visual Studio/CMake environment setup.
- `Scripts/Build-MRBNNBridge.ps1`: optional native CUDA bridge build and smoke test.

The default visible path does not require the native CUDA bridge. It reads the bundled density volume, builds a transient `UVolumeTexture`, and raymarches it in UE.

The realtime shader is an approximation of the MRBNN data, not the full neural decoder from the paper. It uses the baked density field for a game-friendly volume preview, then applies UE-side direct lighting from the scene `DirectionalLight` with color, intensity, phase, and short shadow marching controls.

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

You can also place an `MRBNNVolumeActor` in any level. With the default project settings it uses plugin-relative data under `Data/`.

## Optional native bridge

The CUDA bridge is optional and is not needed for the realtime volume shader preview.

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

## Rebuild plugin assets

When the material-generation script changes, run the Python setup script from an Unreal project that has this plugin enabled. In the original development workspace this is:

```powershell
D:\_Gohot-UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe D:\_Gohot-UE\Projects\MRBNNExample\MRBNNExample.uproject -run=PythonScript -script=D:/_Gohot-UE/Projects/MRBNNExample/Scripts/SetupMRBNNExample.py -unattended -nop4 -nosplash -NullRHI
```

## More docs

See `Docs/MRBNN_UE_Integration.md` for the full integration notes.
