import pathlib

import unreal


PLUGIN_MAP_PATH = "/MRBNN/Examples/MRBNNVolumeExample"
PLUGIN_ROOT = pathlib.Path(__file__).resolve().parents[1]


def level_editor():
    return unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def actor_editor():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def plugin_map_file_exists(map_path):
    if not map_path.startswith("/MRBNN/"):
        return False
    relative_path = map_path.removeprefix("/MRBNN/")
    return (PLUGIN_ROOT / "Content" / f"{relative_path}.umap").exists()


def ensure_map(map_path):
    package_dir = map_path.rsplit("/", 1)[0]
    unreal.EditorAssetLibrary.make_directory(package_dir)

    asset_name = map_path.rsplit("/", 1)[-1]
    if plugin_map_file_exists(map_path) or unreal.EditorAssetLibrary.does_asset_exist(map_path) or unreal.EditorAssetLibrary.does_asset_exist(f"{map_path}.{asset_name}"):
        level_editor().load_level(map_path)
        return

    if not level_editor().new_level(map_path):
        level_editor().load_level(map_path)
        return

    level_editor().load_level(map_path)


def delete_existing(label_prefixes):
    editor = actor_editor()
    for actor in editor.get_all_level_actors():
        label = actor.get_actor_label()
        actor_class = actor.get_class().get_name()
        if actor_class in label_prefixes or any(label.startswith(prefix) for prefix in label_prefixes):
            editor.destroy_actor(actor)


def spawn_actor(actor_class_path, location, rotation, label):
    actor_class = unreal.load_class(None, actor_class_path)
    if not actor_class:
        raise RuntimeError(f"Unable to load class {actor_class_path}")

    actor = actor_editor().spawn_actor_from_class(actor_class, location, rotation)
    actor.set_actor_label(label)
    return actor


def configure_light_actor(light):
    light_component = light.get_component_by_class(unreal.DirectionalLightComponent)
    if light_component:
        light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        light_component.set_editor_property("intensity", 5.0)
        light_component.set_editor_property("light_color", unreal.Color(255, 244, 224, 255))


def configure_sky_actor(sky):
    sky_component = sky.get_component_by_class(unreal.SkyLightComponent)
    if sky_component:
        sky_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        sky_component.set_editor_property("real_time_capture", False)
        sky_component.set_editor_property("intensity", 0.35)


def setup_current_level():
    delete_existing({"MRBNNExampleActor", "MRBNNVolumeActor", "MRBNNVolume", "PlayerStart", "DirectionalLight", "SkyLight"})

    spawn_actor(
        "/Script/Engine.PlayerStart",
        unreal.Vector(-460.0, 0.0, 160.0),
        unreal.Rotator(0.0, 0.0, 0.0),
        "PlayerStart",
    )
    sun = spawn_actor(
        "/Script/Engine.DirectionalLight",
        unreal.Vector(-220.0, -180.0, 500.0),
        unreal.Rotator(-38.0, 42.0, 0.0),
        "DirectionalLight",
    )
    sky = spawn_actor(
        "/Script/Engine.SkyLight",
        unreal.Vector(0.0, 0.0, 260.0),
        unreal.Rotator(0.0, 0.0, 0.0),
        "SkyLight",
    )

    configure_light_actor(sun)
    configure_sky_actor(sky)

    volume = spawn_actor(
        "/Script/MRBNN.MRBNNVolumeActor",
        unreal.Vector(0.0, 0.0, 160.0),
        unreal.Rotator(0.0, 0.0, 0.0),
        "MRBNNVolume",
    )
    volume.set_editor_property("directional_light_actor", sun)
    volume.set_editor_property("auto_rebuild_on_parameter_change", True)
    volume.set_editor_property("use_compute_global_shader", True)
    volume.set_editor_property("use_scene_view_extension_render_pass", True)
    volume.configure_from_project_settings()
    volume.apply_paper_preview_settings()
    volume.set_editor_property("volume_extent", unreal.Vector(240.0, 260.0, 95.0))
    volume.set_editor_property("raymarch_input_threshold", 0.8)
    volume.set_editor_property("raymarch_normalize_density", 88.0)
    volume.set_editor_property("raymarch_density_power", 0.62)
    volume.set_editor_property("raymarch_opacity", 0.048)
    volume.set_editor_property("ambient_relight", 0.18)
    volume.set_editor_property("directional_relight", 1.45)
    volume.set_editor_property("raymarch_baked_feature_contribution", 0.45)
    volume.set_editor_property("raymarch_multi_scatter_contribution", 0.65)
    volume.set_editor_property("raymarch_feature_albedo_blend", 0.22)
    volume.set_editor_property("use_compute_global_shader", True)
    volume.set_editor_property("use_scene_view_extension_render_pass", True)
    volume.set_editor_property("use_mrbnn_cloud_axis_mapping", True)

    try:
        actor_editor().set_selected_level_actors([volume])
    except Exception:
        pass

    try:
        level_editor().set_level_viewport_camera_info(
            unreal.Vector(-460.0, 0.0, 160.0),
            unreal.Rotator(0.0, 0.0, 0.0),
        )
    except Exception:
        pass


def main():
    ensure_map(PLUGIN_MAP_PATH)
    setup_current_level()
    level_editor().save_current_level()
    unreal.EditorAssetLibrary.save_directory("/MRBNN", only_if_is_dirty=False, recursive=True)
    unreal.log("MRBNN plugin example map is ready.")


main()
