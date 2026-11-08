"""GPU regression for face/body color, run in an isolated UE Python commandlet with rendering."""
from pathlib import Path
import math
import unreal as u


def verify(shader_dir=None):
    shader_dir = Path(shader_dir or Path(__file__).parents[1] / "Shaders" / "Material")
    shared = shader_dir / "HonkaiColor.hlsl"
    prefix = shared.read_text(encoding="utf-8") + "\n" if shared.exists() else ""
    face_code = prefix + (shader_dir / "HonkaiFace.hlsl").read_text(encoding="utf-8")
    body_source = (shader_dir / "HonkaiToon.hlsl").read_text(encoding="utf-8")
    # Execute the production color block with a fixed light band, excluding geometric rim/contact effects.
    color_block = body_source.split("// Material AO is reserved", 1)[1].split("// A narrow depth edge", 1)[0]
    color_block = color_block[color_block.index("float3 shadowColor"):]
    result = body_source[body_source.index("HonkaiColor color;"):] if "HonkaiColor color;" in body_source else body_source[body_source.index("return (base *"):]
    result = result.replace("Texture2DSampleLevel(SpecularAtlas, SpecularAtlasSampler, float2(0.5, row), 0)", "float4(SpecularColor, Shininess)")
    result = result.replace("Normal.xyz", "WorldNormal").replace("LightDirection", "WorldLight")
    body_code = prefix + """
float3 base = Base;
float sourceSaturation = 1 - min(base.r, min(base.g, base.b)) / max(max(base.r, max(base.g, base.b)), 0.001);
float band = Band;
float row = (round(RampRow * 255.0) + 0.5) / 256.0;
""" + color_block + "\nfloat rim = 0;\n" + result.replace(" * View.PreExposure", "")
    world = u.EditorLoadingAndSavingUtils.new_blank_map(False)
    lib = u.MaterialEditingLibrary
    white = u.load_asset("/Engine/EngineResources/WhiteSquareTexture")
    assert world and white
    targets, instances = [], []
    atlas_code = "if (UV.x > .35 && UV.x < .65) return float3(.15,.8,.25); return UV.y < 1.0 / 256.0 ? (UV.x < .5 ? float3(.4,.5,.7) : float3(.9,.8,.7)) : (UV.x < .5 ? float3(.7,.3,.2) : float3(.5,.8,1));"
    face_color_code = face_code.replace("float lit = smoothstep(angle - width, angle + width, threshold);", "float lit = Band;")
    for label, code in (("Body", body_code), ("Face", face_code), ("Atlas", atlas_code), ("FaceColor", face_color_code), ("Region", (shader_dir / "HonkaiFaceRegion.hlsl").read_text(encoding="utf-8"))):
        material = u.AssetToolsHelpers.get_asset_tools().create_asset("M_" + label, "/Game/__HonkaiColorParityTest", u.Material, u.MaterialFactoryNew())
        assert material
        material.set_editor_property("shading_model", u.MaterialShadingModel.MSM_UNLIT)
        custom = lib.create_material_expression(material, u.MaterialExpressionCustom)
        custom.set_editor_property("output_type", u.CustomMaterialOutputType.CMOT_FLOAT3)
        custom.set_editor_property("code", code)
        parameters = {
            "RestNormal": (u.MaterialExpressionVectorParameter, u.LinearColor(0, 1, 0, 0)),
            "NoseMask": (u.MaterialExpressionScalarParameter, 1.),
            "Enabled": (u.MaterialExpressionScalarParameter, 1.),
            "Base": (u.MaterialExpressionVectorParameter, u.LinearColor(.7, .6, .55, 1)),
            "Map": (u.MaterialExpressionVectorParameter, u.LinearColor(1, 1, 1, 1)),
            "Light": (u.MaterialExpressionVectorParameter, u.LinearColor(1, 0, 0, 0)),
            "Band": (u.MaterialExpressionScalarParameter, 1.),
            "RampRow": (u.MaterialExpressionScalarParameter, 0.),
            "SpecularColor": (u.MaterialExpressionVectorParameter, u.LinearColor(0, 0, 0, 1)),
            "Shininess": (u.MaterialExpressionScalarParameter, 32.),
            "WorldNormal": (u.MaterialExpressionVectorParameter, u.LinearColor(0, 0, 1, 0)),
            "WorldLight": (u.MaterialExpressionVectorParameter, u.LinearColor(0, 0, 1, 0)),
            "ViewDirection": (u.MaterialExpressionVectorParameter, u.LinearColor(0, 0, 1, 0)),
            "ToonRampEnabled": (u.MaterialExpressionScalarParameter, 1.),
            "ToonAtlas": (u.MaterialExpressionTextureObjectParameter, white),
            "ToonRamp": (u.MaterialExpressionTextureObjectParameter, white),
            "UV": (u.MaterialExpressionTextureCoordinate, None),
        }
        inputs = []
        for name in parameters:
            entry = u.CustomInput()
            entry.set_editor_property("input_name", name)
            inputs.append(entry)
        custom.set_editor_property("inputs", inputs)
        for name, (kind, value) in parameters.items():
            node = lib.create_material_expression(material, kind)
            if value is not None:
                node.set_editor_property("parameter_name", name)
                node.set_editor_property("texture" if kind == u.MaterialExpressionTextureObjectParameter else "default_value", value)
            pin = "RGBA" if name == "Map" else "RGB" if kind == u.MaterialExpressionVectorParameter else ""
            assert lib.connect_material_expressions(node, pin, custom, name)
        assert lib.connect_material_property(custom, "", u.MaterialProperty.MP_EMISSIVE_COLOR)
        assert not lib.recompile_material(material), label
        instances.append(u.MaterialLibrary.create_dynamic_material_instance(world, material))
        size = 256 if label == "Atlas" else 16
        targets.append(u.RenderingLibrary.create_render_target2d(world, size, size, u.TextureRenderTargetFormat.RTF_RGBA16F))
    u.RenderingLibrary.draw_material_to_render_target(world, targets[2], instances[2])

    def render(index, base, band, nose=1.):
        instance = instances[index]
        instance.set_vector_parameter_value("Base", u.LinearColor(*base, 1))
        instance.set_vector_parameter_value("Map", u.LinearColor(band, band, 1, nose))
        instance.set_scalar_parameter_value("Band", band)
        u.RenderingLibrary.draw_material_to_render_target(world, targets[index], instance)
        color = u.RenderingLibrary.read_render_target_raw_pixel(world, targets[index], 8, 8, False)
        return [color.r, color.g, color.b]

    for normal, nose_mask, enabled, expected in (
            ((0, 1, 0), 1, 1, 1), ((0, .8, -.6), 1, 1, 1),
            ((0, -.8, -.6), 1, 1, 0), ((0, -.8, -.6), 0, 1, 1),
            ((0, 1, 0), 1, 0, 0), ((0, -1, 0), 1, 1, 1)):
        instances[4].set_vector_parameter_value("RestNormal", u.LinearColor(*normal, 0))
        instances[4].set_scalar_parameter_value("NoseMask", nose_mask)
        instances[4].set_scalar_parameter_value("Enabled", enabled)
        u.RenderingLibrary.draw_material_to_render_target(world, targets[4], instances[4])
        value = u.RenderingLibrary.read_render_target_raw_pixel(world, targets[4], 8, 8, False).r
        assert abs(value - expected) < .001, (normal, nose_mask, enabled, value, expected)
    u.log("HONKAI_FACE_REGION_RESULT jaw=lit frontal_downward=SDF nose=preserved disabled=lit")

    worst = 0.
    rendered = []
    colors = ((.7, .6, .55), (.9, .82, .78), (.35, .22, .18), (.85, .85, .85), (.2, .4, .8), (.8, .05, .1))
    ramps = [(white, 0.)] + ([(targets[2], 0.), (targets[2], 1. / 255.)] if shared.exists() else [])
    for atlas, row in ramps:
        for instance in instances[:2]:
            instance.set_texture_parameter_value("ToonAtlas", atlas)
            instance.set_scalar_parameter_value("RampRow", row)
        for band in (0., 1.):
            for base in colors:
                body, face = render(0, base, band), render(1, base, band)
                assert all(math.isfinite(c) for c in body + face) and max(body) > .02 and max(face) > .02, (body, face)
                error = max(abs(a - b) for a, b in zip(body, face))
                worst = max(worst, error)
                rendered.append((body, face))
                u.log(f"HONKAI_COLOR_PARITY row={row} base={base} band={band} body={body} face={face} error={error}")
    for index in (0, 1):
        assert max(abs(a - b) for a, b in zip(rendered[0][index], rendered[4][index])) > .2, "Material fell back instead of shading the test colors"
        assert max(abs(a - b) for a, b in zip(rendered[0][index], rendered[6][index])) > .01, "Light band did not reach the shader"
        if shared.exists():
            assert max(abs(a - b) for a, b in zip(rendered[12][index], rendered[24][index])) > .1, "Atlas row did not reach the shader"
    clear = render(1, colors[0], 1.)
    nose = render(1, colors[0], 1., 0.)
    assert nose[1] < clear[1] * .8 and nose[2] < clear[2] * .8, (clear, nose)
    # A deliberately green middle cannot be reproduced by interpolating the red/blue endpoints.
    for index in (0, 3):
        instances[index].set_texture_parameter_value("ToonAtlas", targets[2])
        instances[index].set_scalar_parameter_value("RampRow", 0.)
    body_mid, face_mid = render(0, (.5, .5, .5), .5), render(3, (.5, .5, .5), .5)
    assert body_mid[1] > body_mid[0] * 2 and body_mid[1] > body_mid[2] * 2, body_mid
    assert max(abs(a - b) for a, b in zip(body_mid, face_mid)) <= .002, (body_mid, face_mid)
    for index in (0, 3):
        instances[index].set_vector_parameter_value("SpecularColor", u.LinearColor(.2, .6, .9, 1))
    highlights = []
    for direction in ((0, 0, 1), (.6, 0, .8), (0, 0, -1)):
        for index in (0, 3):
            instances[index].set_vector_parameter_value("ViewDirection", u.LinearColor(*direction, 0))
        body, face = render(0, colors[0], 1.), render(3, colors[0], 1.)
        assert max(abs(a - b) for a, b in zip(body, face)) <= .002, (body, face)
        highlights.append(body)
    assert highlights[0][2] > highlights[1][2] + .1 and highlights[1][2] > highlights[2][2] + .01, highlights
    u.log(f"HONKAI_COLOR_PARITY_RESULT cases={len(rendered)} worst={worst} nose=preserved")
    assert worst <= .002, f"Enhanced face changes color for the same paint, ramp and light band: {worst}"


if __name__ == "__main__":
    verify()
