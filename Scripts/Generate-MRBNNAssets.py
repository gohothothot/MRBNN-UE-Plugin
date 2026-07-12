import unreal


MATERIAL_PATH = "/MRBNN/Materials/M_MRBNN_VolumeRaymarch"


RAYMARCH_CODE = r"""
float3 camRelative = CameraWS - MRBNNActorWorldPosition.xyz;
float3 pixelRelative = PixelWS - MRBNNActorWorldPosition.xyz;
float3 camLocal = float3(
    dot(camRelative, MRBNNWorldToLocal0.xyz),
    dot(camRelative, MRBNNWorldToLocal1.xyz),
    dot(camRelative, MRBNNWorldToLocal2.xyz));
float3 pixelLocal = float3(
    dot(pixelRelative, MRBNNWorldToLocal0.xyz),
    dot(pixelRelative, MRBNNWorldToLocal1.xyz),
    dot(pixelRelative, MRBNNWorldToLocal2.xyz));
float3 rayLocal = normalize(pixelLocal - camLocal);
float3 safeRay = rayLocal;
safeRay.x = abs(safeRay.x) < 0.0001 ? (safeRay.x < 0.0 ? -0.0001 : 0.0001) : safeRay.x;
safeRay.y = abs(safeRay.y) < 0.0001 ? (safeRay.y < 0.0 ? -0.0001 : 0.0001) : safeRay.y;
safeRay.z = abs(safeRay.z) < 0.0001 ? (safeRay.z < 0.0 ? -0.0001 : 0.0001) : safeRay.z;

float3 boxMin = -MRBNNHalfExtent.xyz;
float3 boxMax = MRBNNHalfExtent.xyz;
float3 t0 = (boxMin - camLocal) / safeRay;
float3 t1 = (boxMax - camLocal) / safeRay;
float3 tNear3 = min(t0, t1);
float3 tFar3 = max(t0, t1);
float tEnter = max(max(tNear3.x, tNear3.y), max(tNear3.z, 0.0));
float tExit = min(tFar3.x, min(tFar3.y, tFar3.z));
if (tExit <= tEnter)
{
    return float4(0.0, 0.0, 0.0, 0.0);
}

float stepCount = clamp(MRBNNRaySteps, 4.0, 96.0);
float dt = (tExit - tEnter) / stepCount;
float t = tEnter + dt * 0.5;
float3 lightDir = normalize(MRBNNLightDirectionLocal.xyz + float3(0.0001, 0.0001, 0.0001));
float viewPhase = pow(saturate(dot(-rayLocal, lightDir) * 0.5 + 0.5), 1.5);
float phase = 0.55 + 0.45 * viewPhase;
float4 accum = float4(0.0, 0.0, 0.0, 0.0);

for (int i = 0; i < 96; ++i)
{
    if (i >= stepCount || accum.a > 0.985)
    {
        break;
    }

    float3 localPos = camLocal + rayLocal * t;
    float3 uvw = saturate(localPos / (MRBNNHalfExtent.xyz * 2.0) + 0.5);
    float density = MRBNNDensityTexture.SampleLevel(MRBNNDensityTextureSampler, uvw, 0.0).r;
    float3 edgeDistance = min(uvw, 1.0 - uvw);
    float edgeT = saturate(min(edgeDistance.x, min(edgeDistance.y, edgeDistance.z)) / 0.035);
    density *= edgeT * edgeT * (3.0 - 2.0 * edgeT);
    float lightDensity = MRBNNDensityTexture.SampleLevel(MRBNNDensityTextureSampler, saturate(uvw + lightDir * MRBNNLightStep), 0.0).r;
    float shadow = saturate(1.0 - lightDensity * MRBNNShadowStrength);
    float lighting = MRBNNAmbient + MRBNNDirectional * shadow * phase;
    float referenceLength = max(MRBNNHalfExtent.x + MRBNNHalfExtent.y + MRBNNHalfExtent.z, 1.0);
    float alpha = saturate(1.0 - exp(-density * MRBNNOpacity * 40.0 * dt / referenceLength));
    float3 color = MRBNNCloudColor.rgb * MRBNNBrightness * lighting * lerp(0.88, 1.14, density);

    accum.rgb += (1.0 - accum.a) * color * alpha;
    accum.a += (1.0 - accum.a) * alpha;
    t += dt;
}

return saturate(accum);
"""


def ensure_directory(path):
    unreal.EditorAssetLibrary.make_directory(path)


def set_editor_property_if_possible(obj, name, value):
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception as exc:
        unreal.log_warning(f"MRBNN asset setup skipped property {name}: {exc}")
        return False


def make_custom_input(name):
    custom_input = unreal.CustomInput()
    custom_input.set_editor_property("input_name", name)
    return custom_input


def create_parameter(material, cls, name, default_value, x, y):
    expr = unreal.MaterialEditingLibrary.create_material_expression(material, cls, x, y)
    expr.set_editor_property("parameter_name", name)
    if isinstance(default_value, unreal.LinearColor):
        expr.set_editor_property("default_value", default_value)
    else:
        expr.set_editor_property("default_value", float(default_value))
    return expr


def create_mask(material, source, channels, x, y):
    mask = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionComponentMask, x, y)
    mask.set_editor_property("r", "r" in channels)
    mask.set_editor_property("g", "g" in channels)
    mask.set_editor_property("b", "b" in channels)
    mask.set_editor_property("a", "a" in channels)
    unreal.MaterialEditingLibrary.connect_material_expressions(source, "", mask, "")
    return mask


def rebuild_volume_material():
    ensure_directory("/MRBNN/Materials")
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = unreal.load_asset(MATERIAL_PATH)
    if not material:
        material = asset_tools.create_asset("M_MRBNN_VolumeRaymarch", "/MRBNN/Materials", unreal.Material, unreal.MaterialFactoryNew())
    if not material:
        raise RuntimeError("Unable to create MRBNN raymarch material")

    material.modify()
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    set_editor_property_if_possible(material, "blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    set_editor_property_if_possible(material, "shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    set_editor_property_if_possible(material, "two_sided", False)
    set_editor_property_if_possible(material, "used_with_static_mesh", True)
    set_editor_property_if_possible(material, "used_with_instanced_static_meshes", True)

    pixel_ws = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1100, -260)
    camera_ws = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionCameraPositionWS, -1100, -160)

    density_tex = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionTextureObjectParameter, -1100, -40)
    density_tex.set_editor_property("parameter_name", "MRBNNDensityTexture")
    default_volume_texture = unreal.load_asset("/Engine/EngineResources/DefaultVolumeTexture")
    if default_volume_texture:
        density_tex.set_editor_property("texture", default_volume_texture)
    set_editor_property_if_possible(density_tex, "sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)

    w2l0 = create_parameter(material, unreal.MaterialExpressionVectorParameter, "MRBNNWorldToLocal0", unreal.LinearColor(1.0, 0.0, 0.0, 0.0), -1100, 80)
    w2l1 = create_parameter(material, unreal.MaterialExpressionVectorParameter, "MRBNNWorldToLocal1", unreal.LinearColor(0.0, 1.0, 0.0, 0.0), -1100, 180)
    w2l2 = create_parameter(material, unreal.MaterialExpressionVectorParameter, "MRBNNWorldToLocal2", unreal.LinearColor(0.0, 0.0, 1.0, 0.0), -1100, 280)
    actor_world_position = create_parameter(material, unreal.MaterialExpressionVectorParameter, "MRBNNActorWorldPosition", unreal.LinearColor(0.0, 0.0, 0.0, 0.0), -1100, 380)
    half_extent = create_parameter(material, unreal.MaterialExpressionVectorParameter, "MRBNNHalfExtent", unreal.LinearColor(120.0, 220.0, 160.0, 1.0), -1100, 480)
    light_dir = create_parameter(material, unreal.MaterialExpressionVectorParameter, "MRBNNLightDirectionLocal", unreal.LinearColor(0.35, -0.35, 0.86, 0.0), -1100, 580)
    cloud_color = create_parameter(material, unreal.MaterialExpressionVectorParameter, "MRBNNCloudColor", unreal.LinearColor(0.86, 0.9, 0.92, 1.0), -1100, 680)

    steps = create_parameter(material, unreal.MaterialExpressionScalarParameter, "MRBNNRaySteps", 40.0, -660, -220)
    opacity = create_parameter(material, unreal.MaterialExpressionScalarParameter, "MRBNNOpacity", 0.055, -660, -120)
    ambient = create_parameter(material, unreal.MaterialExpressionScalarParameter, "MRBNNAmbient", 0.55, -660, -20)
    directional = create_parameter(material, unreal.MaterialExpressionScalarParameter, "MRBNNDirectional", 0.45, -660, 80)
    shadow = create_parameter(material, unreal.MaterialExpressionScalarParameter, "MRBNNShadowStrength", 0.55, -660, 180)
    light_step = create_parameter(material, unreal.MaterialExpressionScalarParameter, "MRBNNLightStep", 0.075, -660, 280)
    brightness = create_parameter(material, unreal.MaterialExpressionScalarParameter, "MRBNNBrightness", 1.8, -660, 380)

    custom = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionCustom, -120, 80)
    custom.set_editor_property("description", "MRBNN Volume Raymarch")
    custom.set_editor_property("code", RAYMARCH_CODE)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property("inputs", [
        make_custom_input("PixelWS"),
        make_custom_input("CameraWS"),
        make_custom_input("MRBNNDensityTexture"),
        make_custom_input("MRBNNWorldToLocal0"),
        make_custom_input("MRBNNWorldToLocal1"),
        make_custom_input("MRBNNWorldToLocal2"),
        make_custom_input("MRBNNActorWorldPosition"),
        make_custom_input("MRBNNHalfExtent"),
        make_custom_input("MRBNNLightDirectionLocal"),
        make_custom_input("MRBNNCloudColor"),
        make_custom_input("MRBNNRaySteps"),
        make_custom_input("MRBNNOpacity"),
        make_custom_input("MRBNNAmbient"),
        make_custom_input("MRBNNDirectional"),
        make_custom_input("MRBNNShadowStrength"),
        make_custom_input("MRBNNLightStep"),
        make_custom_input("MRBNNBrightness"),
    ])

    for source, target in [
        (pixel_ws, "PixelWS"),
        (camera_ws, "CameraWS"),
        (density_tex, "MRBNNDensityTexture"),
        (w2l0, "MRBNNWorldToLocal0"),
        (w2l1, "MRBNNWorldToLocal1"),
        (w2l2, "MRBNNWorldToLocal2"),
        (actor_world_position, "MRBNNActorWorldPosition"),
        (half_extent, "MRBNNHalfExtent"),
        (light_dir, "MRBNNLightDirectionLocal"),
        (cloud_color, "MRBNNCloudColor"),
        (steps, "MRBNNRaySteps"),
        (opacity, "MRBNNOpacity"),
        (ambient, "MRBNNAmbient"),
        (directional, "MRBNNDirectional"),
        (shadow, "MRBNNShadowStrength"),
        (light_step, "MRBNNLightStep"),
        (brightness, "MRBNNBrightness"),
    ]:
        unreal.MaterialEditingLibrary.connect_material_expressions(source, "", custom, target)

    rgb = create_mask(material, custom, "rgb", 180, 30)
    alpha = create_mask(material, custom, "a", 180, 180)
    unreal.MaterialEditingLibrary.connect_material_property(rgb, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)

    unreal.MaterialEditingLibrary.layout_material_expressions(material)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)
    unreal.EditorAssetLibrary.save_directory("/MRBNN/Materials", only_if_is_dirty=False, recursive=True)
    unreal.log(f"MRBNN raymarch material ready: {MATERIAL_PATH}")


def main():
    rebuild_volume_material()


if __name__ == "__main__":
    main()
