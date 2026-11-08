"""Read-only graph and package checks. Run inside an Unreal Python commandlet."""
from pathlib import Path
import runpy

library = runpy.run_path(str(Path(__file__).with_name("build_materials.py")), run_name="honkai_material_library")
u, lib, root = library["u"], library["lib"], library["ROOT"]
model_names = ("M_Toon", "M_ToonTwoSided", "M_ToonTranslucent", "M_ToonTranslucentTwoSided", "M_Outline", "M_FaceOutline")


def walk(material, expression):
    pending, seen = [expression], set()
    while pending:
        current = pending.pop()
        if not current or current in seen:
            continue
        seen.add(current)
        yield current
        pending.extend(child for child in lib.get_inputs_for_material_expression(material, current) if child)


for name in model_names:
    material = u.load_asset(root + "/" + name)
    assert material, name
    assert material.get_editor_property("used_with_skeletal_mesh"), name
    assert material.get_editor_property("used_with_morph_targets"), name
    translucent = material.get_editor_property("blend_mode") == u.BlendMode.BLEND_TRANSLUCENT
    assert translucent or material.get_editor_property("blend_mode") == u.BlendMode.BLEND_MASKED, name
    prop = u.MaterialProperty.MP_OPACITY if translucent else u.MaterialProperty.MP_OPACITY_MASK
    alpha_root = lib.get_material_property_input_node(material, prop)
    assert alpha_root, name
    alpha_edges = []
    for expression in walk(material, alpha_root):
        for child in lib.get_inputs_for_material_expression(material, expression):
            if isinstance(child, u.MaterialExpressionVertexColor):
                pin = lib.get_input_node_output_name_for_material_expression(expression, child)
                if isinstance(pin, tuple):
                    assert pin[0]
                    pin = pin[-1]
                assert pin == "A", (name, pin)
                alpha_edges.append(child)
    assert len(alpha_edges) == 1, name
    for color_prop in (u.MaterialProperty.MP_BASE_COLOR, u.MaterialProperty.MP_EMISSIVE_COLOR, u.MaterialProperty.MP_WORLD_POSITION_OFFSET):
        assert not any(isinstance(node, u.MaterialExpressionVertexColor)
                       for node in walk(material, lib.get_material_property_input_node(material, color_prop))), name
    if name in ("M_Toon", "M_ToonTwoSided"):
        assert material.get_editor_property("shading_model") == u.MaterialShadingModel.MSM_DEFAULT_LIT, name
        nodes = list(walk(material, lib.get_material_property_input_node(material, u.MaterialProperty.MP_EMISSIVE_COLOR)))
        face = next((node for node in nodes if isinstance(node, u.MaterialExpressionCustom) and
                     node.get_editor_property("code") == library["shader_code"]("HonkaiFace.hlsl")), None)
        assert face, name
        region = next((entry for entry in nodes if isinstance(entry, u.MaterialExpressionCustom) and
                       entry.get_editor_property("code") == (library["SHADERS"] / "HonkaiFaceRegion.hlsl").read_text(encoding="utf-8")), None)
        assert region and region in set(walk(material, lib.get_material_property_input_node(material, u.MaterialProperty.MP_BASE_COLOR))), name
        # Both lighting paths must use one selector; fractional diffuse/emissive mixing corrupts Scene/Diffuse.
        region_nodes = list(walk(material, region))
        assert any(isinstance(entry, u.MaterialExpressionPreSkinnedNormal) for entry in region_nodes), name
        assert any(isinstance(entry, u.MaterialExpressionVertexInterpolator) for entry in region_nodes), name

        assert {str(i.get_editor_property("input_name")) for i in face.get_editor_property("inputs")} == {
            "Base", "Map", "Light", "ToonAtlas", "RampRow", "SpecularColor", "Shininess", "WorldNormal", "WorldLight", "ViewDirection"}, name
        ramp_row = lib.get_material_property_input_node(material, u.MaterialProperty.MP_AMBIENT_OCCLUSION)
        assert ramp_row in lib.get_inputs_for_material_expression(material, face), name
    if name in ("M_Outline", "M_FaceOutline"):
        assert material.get_editor_property("two_sided"), name
        offset = lib.get_material_property_input_node(material, u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
        assert isinstance(offset, u.MaterialExpressionCustom), name
        assert offset.get_editor_property("code") == (library["SHADERS"] / "HonkaiOutline.hlsl").read_text(encoding="utf-8"), name
    u.log("HONKAI_TOON_GRAPH_OK " + name)

cel = u.load_asset(root + "/M_CelLighting")
assert cel and cel.get_editor_property("material_domain") == u.MaterialDomain.MD_POST_PROCESS
assert cel.get_editor_property("blendable_location") == u.BlendableLocation.BL_SCENE_COLOR_BEFORE_DOF
shader = lib.get_material_property_input_node(cel, u.MaterialProperty.MP_EMISSIVE_COLOR)
assert isinstance(shader, u.MaterialExpressionCustom)
assert shader.get_editor_property("code") == library["shader_code"]("HonkaiToon.hlsl")
white = u.load_asset(root + "/T_LinearWhite")
assert white and not white.get_editor_property("srgb")
options = u.AssetRegistryDependencyOptions(include_hard_package_references=True, include_soft_package_references=True,
    include_searchable_names=False, include_soft_management_references=False, include_hard_management_references=False)
registry = u.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous([root], force_rescan=True)
for name in (*model_names, "M_CelLighting", "T_LinearWhite"):
    package = root + "/" + name
    assets = registry.get_assets_by_package_name(package, include_only_on_disk_assets=True)
    assert assets and any(asset.is_valid() and str(asset.asset_name) == name for asset in assets), package
    dependencies = registry.get_dependencies(package, options) or []
    if name in ("M_Toon", "M_ToonTwoSided", "M_FaceOutline"):
        assert root + "/T_LinearWhite" in (str(item) for item in dependencies), (package, dependencies)
    for dependency in dependencies:
        path = str(dependency)
        assert path.startswith(("/HonkaiToonRenderer/", "/Engine/", "/Script/")), (package, path)
    u.log("HONKAI_TOON_DEPENDENCIES " + name + " " + repr([str(item) for item in dependencies]))
u.log("HONKAI_TOON_VERIFIED count=8 host_dependencies=0")
