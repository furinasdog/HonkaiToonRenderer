"""Offline UE editor material authoring; importing this module does not change assets."""
from pathlib import Path
import unreal as u


def plugin_path(result):
    if isinstance(result, tuple):
        assert result and result[0] is not False, result
        result = result[-1]
    assert isinstance(result, str) and result, "HonkaiToonRenderer must be enabled"
    return result


BASE = Path(plugin_path(u.PluginBlueprintLibrary.get_plugin_base_dir("HonkaiToonRenderer")))
ROOT = plugin_path(u.PluginBlueprintLibrary.get_plugin_mounted_asset_path("HonkaiToonRenderer")).rstrip("/") + "/Materials"
SHADERS = BASE / "Shaders" / "Material"
tools = u.AssetToolsHelpers.get_asset_tools()
lib = u.MaterialEditingLibrary
linear_white = None


def shader_code(name):
    return (SHADERS / "HonkaiColor.hlsl").read_text(encoding="utf-8") + "\n" + (SHADERS / name).read_text(encoding="utf-8")


def initialize_assets():
    global linear_white
    path = ROOT + "/T_LinearWhite"
    linear_white = u.load_asset(path) if u.EditorAssetLibrary.does_asset_exist(path) else u.EditorAssetLibrary.duplicate_asset("/Engine/EngineResources/WhiteSquareTexture", path)
    assert linear_white, path
    linear_white.set_editor_property("srgb", False)
    assert u.EditorAssetLibrary.save_loaded_asset(linear_white), path


def connect(source, output, target, input_name):
    assert lib.connect_material_expressions(source, output, target, input_name), (source, output, target, input_name)


def output(source, output_name, prop):
    assert lib.connect_material_property(source, output_name, prop), (source, output_name, prop)


def material(name, translucent=False, outline=False, two_sided=False, face_outline=False):
    path = ROOT + "/" + name
    if u.EditorAssetLibrary.does_asset_exist(path):
        m = u.load_asset(path)
        lib.delete_all_material_expressions(m)
    else:
        m = tools.create_asset(name, ROOT, u.Material, u.MaterialFactoryNew())
    lit = not (translucent or outline)
    m.set_editor_property("shading_model", u.MaterialShadingModel.MSM_DEFAULT_LIT if lit else u.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", two_sided or outline)
    m.set_editor_property("blend_mode", u.BlendMode.BLEND_TRANSLUCENT if translucent else u.BlendMode.BLEND_MASKED)
    m.set_editor_property("opacity_mask_clip_value", 0.1)
    lib.set_base_material_usage(m, u.MaterialUsage.MATUSAGE_SKELETAL_MESH)
    lib.set_base_material_usage(m, u.MaterialUsage.MATUSAGE_MORPH_TARGETS)

    def node(cls, **props):
        n = lib.create_material_expression(m, cls)
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    tint = node(u.MaterialExpressionVectorParameter, parameter_name="Tint", default_value=u.LinearColor(1, 1, 1, 1))
    texture = node(u.MaterialExpressionTextureSampleParameter2D, parameter_name="BaseTexture", texture=u.load_asset("/Engine/EngineResources/WhiteSquareTexture"))
    opacity = node(u.MaterialExpressionScalarParameter, parameter_name="Opacity", default_value=1.0)
    alpha = node(u.MaterialExpressionMultiply)
    connect(texture, "A", alpha, "A")
    connect(opacity, "", alpha, "B")
    # Per-instance part visibility uses only vertex alpha; white preserves authored opacity.
    visibility = node(u.MaterialExpressionMultiply)
    connect(alpha, "", visibility, "A")
    connect(node(u.MaterialExpressionVertexColor), "A", visibility, "B")
    alpha = visibility
    output(alpha, "", u.MaterialProperty.MP_OPACITY if translucent else u.MaterialProperty.MP_OPACITY_MASK)
    if outline:
        face_mask = node(u.MaterialExpressionConstant, r=1)
        if face_outline:
            face_map = node(u.MaterialExpressionTextureSampleParameter2D, parameter_name="FaceMap",
                            texture=linear_white, sampler_type=u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR,
                            sampler_source=u.SamplerSourceMode.SSM_CLAMP_WORLD_GROUP_SETTINGS,
                            mip_value_mode=u.TextureMipValueMode.TMVM_MIP_LEVEL, const_mip_value=0)
            face_mask = node(u.MaterialExpressionComponentMask, r=False, g=False, b=True, a=False)
            connect(face_map, "RGB", face_mask, "")
        surface_tint = node(u.MaterialExpressionVectorParameter, parameter_name="SurfaceTint", default_value=u.LinearColor(1, 1, 1, 1))
        ink = node(u.MaterialExpressionCustom, output_type=u.CustomMaterialOutputType.CMOT_FLOAT3,
                   code="float3 inkColor = lerp(Base * SurfaceTint * float3(0.18, 0.065, 0.045), Ink, 0.25); return lerp(lerp(Base * SurfaceTint, inkColor, 0.55), inkColor, FaceMask);" if face_outline else "float3 authored = saturate(Ink); if (Region > 1.5) return authored * 0.75; if (Region > 0.5) return pow(authored, 1.3) * float3(0.20, 0.13, 0.11); return authored * 0.18;")
        ink_inputs = []
        for input_name in ("Ink", "Base", "SurfaceTint", "FaceMask", "Region"):
            i = u.CustomInput()
            i.set_editor_property("input_name", input_name)
            ink_inputs.append(i)
        ink.set_editor_property("inputs", ink_inputs)
        connect(tint, "RGB", ink, "Ink")
        connect(texture, "RGB", ink, "Base")
        connect(surface_tint, "RGB", ink, "SurfaceTint")
        connect(face_mask, "", ink, "FaceMask")
        region = node(u.MaterialExpressionScalarParameter, parameter_name="OutlineRegion", default_value=0)
        connect(region, "", ink, "Region")
        output(ink, "", u.MaterialProperty.MP_EMISSIVE_COLOR)
        normal = node(u.MaterialExpressionVertexNormalWS)
        width = node(u.MaterialExpressionScalarParameter, parameter_name="OutlineWidth", default_value=0.12)
        nose_width = node(u.MaterialExpressionLinearInterpolate, const_a=0.45, const_b=1)
        connect(face_mask, "", nose_width, "Alpha")
        scaled_width = node(u.MaterialExpressionMultiply)
        connect(width, "", scaled_width, "A")
        connect(nose_width, "", scaled_width, "B")
        offset = node(u.MaterialExpressionCustom, output_type=u.CustomMaterialOutputType.CMOT_FLOAT3,
                      code=(SHADERS / "HonkaiOutline.hlsl").read_text(encoding="utf-8"))
        offset_inputs = []
        for name in ("Normal", "Width", "Position", "Camera"):
            entry = u.CustomInput()
            entry.set_editor_property("input_name", name)
            offset_inputs.append(entry)
        offset.set_editor_property("inputs", offset_inputs)
        connect(normal, "", offset, "Normal")
        connect(scaled_width if face_outline else width, "", offset, "Width")
        connect(node(u.MaterialExpressionWorldPosition), "", offset, "Position")
        connect(node(u.MaterialExpressionCameraPositionWS), "", offset, "Camera")
        output(offset, "", u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
        facing = node(u.MaterialExpressionTwoSidedSign)
        mask = node(u.MaterialExpressionCustom, output_type=u.CustomMaterialOutputType.CMOT_FLOAT1, code="return A * step(S, 0.0);")
        inputs = []
        for input_name in ("A", "S"):
            i = u.CustomInput()
            i.set_editor_property("input_name", input_name)
            inputs.append(i)
        mask.set_editor_property("inputs", inputs)
        connect(alpha, "", mask, "A")
        connect(facing, "", mask, "S")
        output(mask, "", u.MaterialProperty.MP_OPACITY_MASK)
    else:
        base = node(u.MaterialExpressionMultiply)
        connect(texture, "RGB", base, "A")
        connect(tint, "RGB", base, "B")
        strength = node(u.MaterialExpressionScalarParameter, parameter_name="ToonStrength", default_value=1)
        sphere_uv = node(u.MaterialExpressionCustom, output_type=u.CustomMaterialOutputType.CMOT_FLOAT2,
                         code="return mul(N, (float3x3)ResolvedView.TranslatedWorldToView).xy * float2(0.5, -0.5) + 0.5;")
        normal_input = u.CustomInput()
        normal_input.set_editor_property("input_name", "N")
        sphere_uv.set_editor_property("inputs", [normal_input])
        connect(node(u.MaterialExpressionPixelNormalWS), "", sphere_uv, "N")
        sphere = node(u.MaterialExpressionTextureSampleParameter2D, parameter_name="SphereTexture",
                      texture=u.load_asset("/Engine/EngineResources/WhiteSquareTexture"),
                      sampler_source=u.SamplerSourceMode.SSM_CLAMP_WORLD_GROUP_SETTINGS)
        connect(sphere_uv, "", sphere, "UVs")
        mode = node(u.MaterialExpressionScalarParameter, parameter_name="SphereMode", default_value=0)
        composite = node(u.MaterialExpressionCustom, output_type=u.CustomMaterialOutputType.CMOT_FLOAT3,
                         code="float3 result = Mode < 0.5 ? Base : (Mode < 1.5 ? Base * Sphere : Base + Sphere); return lerp(Base, result, Strength);")
        sphere_inputs = []
        for input_name in ("Base", "Sphere", "Mode", "Strength"):
            i = u.CustomInput()
            i.set_editor_property("input_name", input_name)
            sphere_inputs.append(i)
        composite.set_editor_property("inputs", sphere_inputs)
        connect(base, "", composite, "Base")
        connect(sphere, "RGB", composite, "Sphere")
        connect(mode, "", composite, "Mode")
        sphere_strength = node(u.MaterialExpressionScalarParameter, parameter_name="SphereStrength", default_value=1)
        connect(sphere_strength, "", composite, "Strength")
        base = composite
        if lit:
            # AO is disabled in this viewer; its GBuffer byte carries the material's ramp row.
            ramp_row = node(u.MaterialExpressionScalarParameter, parameter_name="RampRow", default_value=0)
            output(ramp_row, "", u.MaterialProperty.MP_AMBIENT_OCCLUSION)
            toon_atlas = node(u.MaterialExpressionTextureObjectParameter, parameter_name="ToonAtlas",
                             texture=u.load_asset("/Engine/EngineResources/WhiteSquareTexture"))
            face_enabled = node(u.MaterialExpressionScalarParameter, parameter_name="FaceEnabled", default_value=0)
            face_map = node(u.MaterialExpressionTextureSampleParameter2D, parameter_name="FaceMap",
                            texture=linear_white,
                            sampler_type=u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR,
                            sampler_source=u.SamplerSourceMode.SSM_CLAMP_WORLD_GROUP_SETTINGS)
            light = node(u.MaterialExpressionVectorParameter, parameter_name="LightDirection", default_value=u.LinearColor(1, 0, 0, 0))
            local_light = node(u.MaterialExpressionTransform,
                               transform_source_type=u.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD,
                               transform_type=u.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
            connect(light, "RGB", local_light, "")
            face = node(u.MaterialExpressionCustom, output_type=u.CustomMaterialOutputType.CMOT_FLOAT3,
                        code=shader_code("HonkaiFace.hlsl"))
            face_inputs = []
            for input_name in ("Base", "Map", "Light", "ToonAtlas", "RampRow", "SpecularColor", "Shininess", "WorldNormal", "WorldLight", "ViewDirection"):
                i = u.CustomInput()
                i.set_editor_property("input_name", input_name)
                face_inputs.append(i)
            face.set_editor_property("inputs", face_inputs)
            connect(base, "", face, "Base")
            connect(face_map, "RGBA", face, "Map")
            connect(local_light, "", face, "Light")
            connect(toon_atlas, "", face, "ToonAtlas")
            connect(ramp_row, "", face, "RampRow")
            connect(node(u.MaterialExpressionVectorParameter, parameter_name="SpecularColor", default_value=u.LinearColor(0, 0, 0, 1)), "RGB", face, "SpecularColor")
            connect(node(u.MaterialExpressionScalarParameter, parameter_name="Shininess", default_value=32), "", face, "Shininess")
            connect(node(u.MaterialExpressionPixelNormalWS), "", face, "WorldNormal")
            world_light = node(u.MaterialExpressionVectorParameter, parameter_name="WorldLightDirection", default_value=u.LinearColor(1, 0, 0, 0))
            connect(world_light, "RGB", face, "WorldLight")
            connect(node(u.MaterialExpressionCameraVectorWS), "", face, "ViewDirection")
            body_enabled = node(u.MaterialExpressionOneMinus)
            connect(face_enabled, "", body_enabled, "")
            body_strength = node(u.MaterialExpressionMultiply)
            connect(strength, "", body_strength, "A")
            connect(body_enabled, "", body_strength, "B")
            diffuse = node(u.MaterialExpressionMultiply)
            connect(base, "", diffuse, "A")
            connect(body_strength, "", diffuse, "B")
            output(diffuse, "", u.MaterialProperty.MP_BASE_COLOR)
            floor = node(u.MaterialExpressionLinearInterpolate, const_a=1.0, const_b=0.08)
            connect(strength, "", floor, "Alpha")
            ambient = node(u.MaterialExpressionMultiply)
            connect(base, "", ambient, "A")
            connect(floor, "", ambient, "B")
            color = node(u.MaterialExpressionLinearInterpolate)
            connect(ambient, "", color, "A")
            connect(face, "", color, "B")
            connect(face_enabled, "", color, "Alpha")
            output(color, "", u.MaterialProperty.MP_EMISSIVE_COLOR)
            output(node(u.MaterialExpressionConstant, r=1), "", u.MaterialProperty.MP_ROUGHNESS)
            output(node(u.MaterialExpressionConstant, r=0), "", u.MaterialProperty.MP_SPECULAR)
        else:
            # shortcut: translucent PMX overlays have no GBuffer; retain authored color and alpha.
            output(base, "", u.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.layout_material_expressions(m)
    errors = lib.recompile_material(m)
    assert not errors, errors
    u.log("MMD_MATERIAL_CHECK " + name + " shading=" + str(m.get_editor_property("shading_model")))
    assert u.EditorAssetLibrary.save_loaded_asset(m), path
    return m


def cel_lighting():
    path = ROOT + "/M_CelLighting"
    m = u.load_asset(path) if u.EditorAssetLibrary.does_asset_exist(path) else tools.create_asset("M_CelLighting", ROOT, u.Material, u.MaterialFactoryNew())
    lib.delete_all_material_expressions(m)
    m.set_editor_property("material_domain", u.MaterialDomain.MD_POST_PROCESS)
    m.set_editor_property("blendable_location", u.BlendableLocation.BL_SCENE_COLOR_BEFORE_DOF)
    shader = lib.create_material_expression(m, u.MaterialExpressionCustom)
    shader.set_editor_property("output_type", u.CustomMaterialOutputType.CMOT_FLOAT3)
    shader.set_editor_property("code", shader_code("HonkaiToon.hlsl"))
    textures = {"Scene": u.SceneTextureId.PPI_POST_PROCESS_INPUT0,
                "Diffuse": u.SceneTextureId.PPI_DIFFUSE_COLOR,
                "Normal": u.SceneTextureId.PPI_WORLD_NORMAL,
                "Stencil": u.SceneTextureId.PPI_CUSTOM_STENCIL,
                "Depth": u.SceneTextureId.PPI_SCENE_DEPTH,
                "MaterialAO": u.SceneTextureId.PPI_MATERIAL_AO,
                "CustomDepth": u.SceneTextureId.PPI_CUSTOM_DEPTH}
    inputs = []
    for name in textures:
        i = u.CustomInput()
        i.set_editor_property("input_name", name)
        inputs.append(i)
    for name in ("LightDirection", "ToonAtlas", "SpecularAtlas", "ViewDirection"):
        i = u.CustomInput()
        i.set_editor_property("input_name", name)
        inputs.append(i)
    shader.set_editor_property("inputs", inputs)
    for name, texture_id in textures.items():
        n = lib.create_material_expression(m, u.MaterialExpressionSceneTexture)
        n.set_editor_property("scene_texture_id", texture_id)
        connect(n, "Color", shader, name)
    light = lib.create_material_expression(m, u.MaterialExpressionVectorParameter)
    light.set_editor_property("parameter_name", "LightDirection")
    light.set_editor_property("default_value", u.LinearColor(0.82, -0.38, 0.42, 0))
    connect(light, "RGB", shader, "LightDirection")
    atlas = lib.create_material_expression(m, u.MaterialExpressionTextureObjectParameter)
    atlas.set_editor_property("parameter_name", "ToonAtlas")
    atlas.set_editor_property("texture", u.load_asset("/Engine/EngineResources/WhiteSquareTexture"))
    connect(atlas, "", shader, "ToonAtlas")
    specular = lib.create_material_expression(m, u.MaterialExpressionTextureObjectParameter)
    specular.set_editor_property("parameter_name", "SpecularAtlas")
    specular.set_editor_property("texture", linear_white)
    specular.set_editor_property("sampler_type", u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    connect(specular, "", shader, "SpecularAtlas")
    connect(lib.create_material_expression(m, u.MaterialExpressionCameraVectorWS), "", shader, "ViewDirection")
    output(shader, "", u.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.layout_material_expressions(m)
    assert not lib.recompile_material(m)
    assert u.EditorAssetLibrary.save_loaded_asset(m), path
    u.log("MMD_MATERIAL_CHECK M_CelLighting before_dof=true")


def model_materials():
    initialize_assets()
    return [
        material("M_Toon"),
        material("M_ToonTwoSided", two_sided=True),
        material("M_ToonTranslucent", translucent=True),
        material("M_ToonTranslucentTwoSided", translucent=True, two_sided=True),
        material("M_Outline", outline=True),
        material("M_FaceOutline", outline=True, face_outline=True),
    ]



def main():
    u.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([ROOT], True)
    model_materials()
    cel_lighting()
    import runpy
    runpy.run_path(str(BASE / "Scripts" / "verify_materials.py"), run_name="__main__")
    u.log("HONKAI_TOON_MATERIALS_COMPLETE count=8")


if __name__ == "__main__":
    main()
