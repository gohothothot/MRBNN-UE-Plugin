# MRBNN Plugin Data

This directory is the default data root used by `UMRBNNProjectSettings`.

- `cloud-03` is the bundled example scene.
- `volumes/cloud_03.bin` is referenced by `cloud-03/config.json`.
- Additional baked MRBNN scenes can be packaged here from Unreal with `AMRBNNVolumeActor::BakeCurrentDataToPluginData`.
