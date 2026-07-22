# MRBNN Plugin Data

This directory is the default data root used by `UMRBNNProjectSettings`.

- `cloud-03` is the bundled example scene.
- `volumes/cloud_03.bin` is referenced by `cloud-03/config.json`.
- Additional baked MRBNN scenes can be packaged here from Unreal with `AMRBNNVolumeActor::BakeCurrentDataToPluginData`, or from the standalone `MRBNNBakeConsole` Actions tab.
- `mrbnn_ue_manifest.json` is the legacy package manifest written inside a packaged scene directory. It records the copied scene and source/destination roots for the scene package.
- `mrbnn_bake_sync_manifest.json` is the newer bake-console handoff manifest. It is written by the Cloud Bake / Sync workflow next to the Cloud Info RGBA32F output, may be copied into a scene work directory, and is consumed by the editor `Sync MRBNN` toolbar command.
- Cloud Info sidecars such as `*_cloud_info.rgba32f` and `*_cloud_info_manifest.json` are optional bake artifacts. They describe local CUDA density/shape preprocessing for future UE import and shader-quality work; the bundled runtime preview can still load the base `cloud-03` scene without them.
