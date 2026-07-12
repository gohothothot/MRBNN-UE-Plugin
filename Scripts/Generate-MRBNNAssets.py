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

float3 absCamLocal = abs(camLocal);
float cameraInside =
    absCamLocal.x < MRBNNHalfExtent.x &&
    absCamLocal.y < MRBNNHalfExtent.y &&
    absCamLocal.z < MRBNNHalfExtent.z ? 1.0 : 0.0;
float surfaceT = dot(pixelLocal - camLocal, rayLocal);
float entryFaceTolerance = max(max(MRBNNHalfExtent.x, max(MRBNNHalfExtent.y, MRBNNHalfExtent.z)) * 0.015, 2.0);
if (cameraInside < 0.5 && surfaceT > tEnter + entryFaceTolerance)
{
    return float4(0.0, 0.0, 0.0, 0.0);
}

float stepCount = clamp(MRBNNRaySteps, 4.0, 96.0);
float dt = (tExit - tEnter) / stepCount;
float t = tEnter + dt * 0.5;
float3 lightDir = normalize(MRBNNLightDirectionLocal.xyz + float3(0.0001, 0.0001, 0.0001));
float cosTheta = clamp(dot(-rayLocal, lightDir), -1.0, 1.0);
float phaseG = clamp(MRBNNPhaseG, -0.85, 0.85);
float phaseDenom = max(1.0 + phaseG * phaseG - 2.0 * phaseG * cosTheta, 0.05);
float hgPhase = (1.0 - phaseG * phaseG) / max(pow(phaseDenom, 1.5), 0.05);
float phase = lerp(1.0, max(hgPhase * 0.28, 0.05), saturate(MRBNNPhaseStrength));
int directShadowSteps = (int)clamp(round(MRBNNDirectShadowSteps), 0.0, 8.0);
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
    float4 bakedFeature = MRBNNFeatureTexture.SampleLevel(MRBNNFeatureTextureSampler, uvw, 0.0);
    float featureMask = saturate(MRBNNUseBakedFeatures) * bakedFeature.a;
    float baseFeature = bakedFeature.r;
    float multiScatterFeature = bakedFeature.g;
    float anisotropyFeature = bakedFeature.b;
    float3 edgeDistance = min(uvw, 1.0 - uvw);
    float edgeT = saturate(min(edgeDistance.x, min(edgeDistance.y, edgeDistance.z)) / 0.035);
    density *= edgeT * edgeT * (3.0 - 2.0 * edgeT);
    float transmittance = 1.0;
    for (int lightStepIndex = 0; lightStepIndex < 8; ++lightStepIndex)
    {
        if (lightStepIndex >= directShadowSteps)
        {
            break;
        }
        float lightDensity = MRBNNDensityTexture.SampleLevel(
            MRBNNDensityTextureSampler,
            saturate(uvw + lightDir * MRBNNLightStep * (float(lightStepIndex) + 1.0)),
            0.0).r;
        transmittance *= exp(-lightDensity * MRBNNDirectShadowDensity);
    }
    float legacyShadow = saturate(1.0 - (1.0 - transmittance) * MRBNNShadowStrength);
    float bakedFeatureWeight = featureMask * clamp(MRBNNBakedFeatureContribution, 0.0, 2.0);
    float multiScatterWeight = featureMask * clamp(MRBNNMultiScatterContribution, 0.0, 2.0);
    float directFeature = lerp(1.0, saturate(0.78 + baseFeature * 0.5), bakedFeatureWeight);
    float phaseFeature = lerp(1.0, 0.86 + anisotropyFeature * 0.55, bakedFeatureWeight);
    float3 directLight = MRBNNDirectLightColor.rgb * max(MRBNNDirectLightIntensity, 0.0) * MRBNNDirectional * legacyShadow * phase * directFeature * phaseFeature;
    float3 ambientLight = float3(MRBNNAmbient, MRBNNAmbient, MRBNNAmbient);
    ambientLight += MRBNNBakedFeatureTint.rgb * multiScatterFeature * multiScatterWeight;
    float3 lighting = ambientLight + directLight;
    float referenceLength = max(MRBNNHalfExtent.x + MRBNNHalfExtent.y + MRBNNHalfExtent.z, 1.0);
    float alpha = saturate(1.0 - exp(-density * MRBNNOpacity * 40.0 * dt / referenceLength));
    float tintBlend = featureMask * saturate(MRBNNFeatureAlbedoBlend) * saturate(baseFeature + multiScatterFeature * 0.5);
    float3 cloudTint = lerp(MRBNNCloudColor.rgb, MRBNNBakedFeatureTint.rgb, tintBlend);
    float3 color = cloudTint * MRBNNBrightness * lighting * lerp(0.88, 1.14, density);

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
    set_editor_property_if_possible(material, "two_sided", True)
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

    feature_tex = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionTextureObjectParameter, -1100, 40)
    feature_tex.set_editor_property("parameter_name", "MRBNNFeatureTexture")
    if default_volume_texture:
        feature_tex.set_editor_property("texture", default_volume_texture)
    set_editor_property_if_possible(feature_tex, "sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)

    w2l0 = create_parameter(material, unreal.MaterialExpressionVectorParameter, "MRBNNWorldToLocal0", unreal.LinearColor(1.0, 0.0, 0.0, 0.0), -1100, 140)
    w2l1 = create_parameter(material, unreal.MaterialExpressionVectorParameter, "MRBNNWorldToLocal1", unreal.LinearColor(0.0, 1.0, 0.0, 0.0), -1100, 240)
    w2l2 = create_parameter(material, unreal.MaterialExpressionVectorParameter, "MRBNNWorldToLocal2", unreal.LinearColor(0.0, 0.0, 1.0, 0.0), -1100, 340)
    actor_world_position = create_parameter(material, unreal.MaterialExpressionVectorParameter, "MRBNNActorWorldPosition", unreal.LinearColor(0.0, 0.0, 0.0, 0.0), -1100, 440)
    half_extent = create_parameter(material, unreal.MaterialExpressionVectorParameter, "MRBNNHalfExtent", unreal.LinearColor(120.0, 220.0, 160.0, 1.0), -1100, 540)
    light_dir = create_parameter(material, unreal.MaterialExpressionVectorParameter, "MRBNNLightDirectionLocal", unreal.LinearColor(0.35, -0.35, 0.86, 0.0), -1100, 640)
    cloud_color = create_parameter(material, unreal.MaterialExpressionVectorParameter, "MRBNNCloudColor", unreal.LinearColor(0.86, 0.9, 0.92, 1.0), -1100, 740)
    direct_light_color = create_parameter(material, unreal.MaterialExpressionVectorParameter, "MRBNNDirectLightColor", unreal.LinearColor(1.0, 0.96, 0.88, 1.0), -1100, 840)
    baked_feature_tint = create_parameter(material, unreal.MaterialExpressionVectorParameter, "MRBNNBakedFeatureTint", unreal.LinearColor(1.0, 0.965, 0.88, 1.0), -1100, 940)

    steps = create_parameter(material, unreal.MaterialExpressionScalarParameter, "MRBNNRaySteps", 40.0, -660, -220)
    opacity = create_parameter(material, unreal.MaterialExpressionScalarParameter, "MRBNNOpacity", 0.055, -660, -120)
    ambient = create_parameter(material, unreal.MaterialExpressionScalarParameter, "MRBNNAmbient", 0.55, -660, -20)
    directional = create_parameter(material, unreal.MaterialExpressionScalarParameter, "MRBNNDirectional", 0.45, -660, 80)
    shadow = create_parameter(material, unreal.MaterialExpressionScalarParameter, "MRBNNShadowStrength", 0.55, -660, 180)
    light_step = create_parameter(material, unreal.MaterialExpressionScalarParameter, "MRBNNLightStep", 0.075, -660, 280)
    brightness = create_parameter(material, unreal.MaterialExpressionScalarParameter, "MRBNNBrightness", 1.8, -660, 380)
    direct_light_intensity = create_parameter(material, unreal.MaterialExpressionScalarParameter, "MRBNNDirectLightIntensity", 1.0, -660, 480)
    direct_shadow_steps = create_parameter(material, unreal.MaterialExpressionScalarParameter, "MRBNNDirectShadowSteps", 4.0, -660, 580)
    direct_shadow_density = create_parameter(material, unreal.MaterialExpressionScalarParameter, "MRBNNDirectShadowDensity", 1.35, -660, 680)
    phase_g = create_parameter(material, unreal.MaterialExpressionScalarParameter, "MRBNNPhaseG", 0.35, -660, 780)
    phase_strength = create_parameter(material, unreal.MaterialExpressionScalarParameter, "MRBNNPhaseStrength", 0.75, -660, 880)
    use_baked_features = create_parameter(material, unreal.MaterialExpressionScalarParameter, "MRBNNUseBakedFeatures", 1.0, -660, 980)
    baked_feature_contribution = create_parameter(material, unreal.MaterialExpressionScalarParameter, "MRBNNBakedFeatureContribution", 0.65, -660, 1080)
    multi_scatter_contribution = create_parameter(material, unreal.MaterialExpressionScalarParameter, "MRBNNMultiScatterContribution", 0.75, -660, 1180)
    feature_albedo_blend = create_parameter(material, unreal.MaterialExpressionScalarParameter, "MRBNNFeatureAlbedoBlend", 0.25, -660, 1280)

    custom = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionCustom, -120, 80)
    custom.set_editor_property("description", "MRBNN Volume Raymarch")
    custom.set_editor_property("code", RAYMARCH_CODE)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property("inputs", [
        make_custom_input("PixelWS"),
        make_custom_input("CameraWS"),
        make_custom_input("MRBNNDensityTexture"),
        make_custom_input("MRBNNFeatureTexture"),
        make_custom_input("MRBNNWorldToLocal0"),
        make_custom_input("MRBNNWorldToLocal1"),
        make_custom_input("MRBNNWorldToLocal2"),
        make_custom_input("MRBNNActorWorldPosition"),
        make_custom_input("MRBNNHalfExtent"),
        make_custom_input("MRBNNLightDirectionLocal"),
        make_custom_input("MRBNNCloudColor"),
        make_custom_input("MRBNNDirectLightColor"),
        make_custom_input("MRBNNBakedFeatureTint"),
        make_custom_input("MRBNNRaySteps"),
        make_custom_input("MRBNNOpacity"),
        make_custom_input("MRBNNAmbient"),
        make_custom_input("MRBNNDirectional"),
        make_custom_input("MRBNNShadowStrength"),
        make_custom_input("MRBNNLightStep"),
        make_custom_input("MRBNNBrightness"),
        make_custom_input("MRBNNDirectLightIntensity"),
        make_custom_input("MRBNNDirectShadowSteps"),
        make_custom_input("MRBNNDirectShadowDensity"),
        make_custom_input("MRBNNPhaseG"),
        make_custom_input("MRBNNPhaseStrength"),
        make_custom_input("MRBNNUseBakedFeatures"),
        make_custom_input("MRBNNBakedFeatureContribution"),
        make_custom_input("MRBNNMultiScatterContribution"),
        make_custom_input("MRBNNFeatureAlbedoBlend"),
    ])

    for source, target in [
        (pixel_ws, "PixelWS"),
        (camera_ws, "CameraWS"),
        (density_tex, "MRBNNDensityTexture"),
        (feature_tex, "MRBNNFeatureTexture"),
        (w2l0, "MRBNNWorldToLocal0"),
        (w2l1, "MRBNNWorldToLocal1"),
        (w2l2, "MRBNNWorldToLocal2"),
        (actor_world_position, "MRBNNActorWorldPosition"),
        (half_extent, "MRBNNHalfExtent"),
        (light_dir, "MRBNNLightDirectionLocal"),
        (cloud_color, "MRBNNCloudColor"),
        (direct_light_color, "MRBNNDirectLightColor"),
        (baked_feature_tint, "MRBNNBakedFeatureTint"),
        (steps, "MRBNNRaySteps"),
        (opacity, "MRBNNOpacity"),
        (ambient, "MRBNNAmbient"),
        (directional, "MRBNNDirectional"),
        (shadow, "MRBNNShadowStrength"),
        (light_step, "MRBNNLightStep"),
        (brightness, "MRBNNBrightness"),
        (direct_light_intensity, "MRBNNDirectLightIntensity"),
        (direct_shadow_steps, "MRBNNDirectShadowSteps"),
        (direct_shadow_density, "MRBNNDirectShadowDensity"),
        (phase_g, "MRBNNPhaseG"),
        (phase_strength, "MRBNNPhaseStrength"),
        (use_baked_features, "MRBNNUseBakedFeatures"),
        (baked_feature_contribution, "MRBNNBakedFeatureContribution"),
        (multi_scatter_contribution, "MRBNNMultiScatterContribution"),
        (feature_albedo_blend, "MRBNNFeatureAlbedoBlend"),
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
