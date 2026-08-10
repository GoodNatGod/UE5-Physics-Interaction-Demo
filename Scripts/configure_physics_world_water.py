import unreal


ROOT = "/Game/PhysicsWorldDemo/Water"
CONFIG_DIRECTORY = f"{ROOT}/Config"
CONFIG_PATH = f"{CONFIG_DIRECTORY}/DA_WorldWaterRippleConfig"
MATERIAL_DIRECTORY = f"{ROOT}/Materials"
BASE_MATERIAL_PATH = f"{MATERIAL_DIRECTORY}/M_RoverWater"
WATER_MATERIAL_PATH = f"{MATERIAL_DIRECTORY}/MI_RoverWater_Lake"
SOLVE_MATERIAL_PATH = f"{MATERIAL_DIRECTORY}/M_RoverRippleSolve"
IMPULSE_MATERIAL_PATH = f"{MATERIAL_DIRECTORY}/M_RoverRippleImpulse"
SIMULATION_DIRECTORY = f"{ROOT}/Simulation"
STATE_A_PATH = f"{SIMULATION_DIRECTORY}/RT_RoverRippleStateA"
STATE_B_PATH = f"{SIMULATION_DIRECTORY}/RT_RoverRippleStateB"
NIAGARA_DIRECTORY = f"{ROOT}/Niagara"
SPLASH_PATH = f"{NIAGARA_DIRECTORY}/NS_RoverWaterSplash"
MAP_DIRECTORY = f"{ROOT}/Maps"
MAP_PATH = f"{MAP_DIRECTORY}/L_WaterP0"
SOURCE_MAP_PATH = "/Game/ModularLostRuinKit/Maps/ExampleMap_Lumen"
REGION_TAG = "PhysicsWorldWaterRippleRegion"
WATER_PLANE_TAG = "PhysicsWorldInteractiveWaterPlane"
WATER_PLANE_BLUEPRINT_PATH = (
    "/Game/ModularLostRuinKit/Blueprint/Effects/BP_Waterplane"
)


def ensure_directory(path):
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def asset_name(path):
    return path.rsplit("/", 1)[-1]


def create_asset(path, asset_class, factory):
    existing = unreal.load_asset(path)
    if existing:
        if not isinstance(existing, asset_class):
            raise RuntimeError(
                f"Unexpected asset class at {path}: {existing.get_class().get_name()}"
            )
        return existing, False
    created = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name(path), path.rsplit("/", 1)[0], asset_class, factory
    )
    if not isinstance(created, asset_class):
        raise RuntimeError(f"Failed to create {path}")
    return created, True


def expression(material, expression_class, x, y, **properties):
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, expression_class, x, y
    )
    if not node:
        raise RuntimeError(
            f"Failed to create {expression_class.__name__} in {material.get_path_name()}"
        )
    for property_name, value in properties.items():
        node.set_editor_property(property_name, value)
    return node


def connect(source, source_output, destination, destination_input):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
        source, source_output, destination, destination_input
    ):
        raise RuntimeError(
            f"Failed material connection {source.get_name()}:{source_output} -> "
            f"{destination.get_name()}:{destination_input}"
        )


def connect_property(source, source_output, material_property):
    if not unreal.MaterialEditingLibrary.connect_material_property(
        source, source_output, material_property
    ):
        raise RuntimeError(
            f"Failed material property connection {source.get_name()}:{source_output} "
            f"-> {material_property}"
        )


def scalar_parameter(material, name, value, x, y):
    return expression(
        material,
        unreal.MaterialExpressionScalarParameter,
        x,
        y,
        parameter_name=unreal.Name(name),
        default_value=value,
    )


def vector_parameter(material, name, value, x, y):
    return expression(
        material,
        unreal.MaterialExpressionVectorParameter,
        x,
        y,
        parameter_name=unreal.Name(name),
        default_value=value,
    )


def constant(material, value, x, y):
    return expression(
        material, unreal.MaterialExpressionConstant, x, y, r=float(value)
    )


def constant2(material, x_value, y_value, x, y):
    return expression(
        material,
        unreal.MaterialExpressionConstant2Vector,
        x,
        y,
        r=float(x_value),
        g=float(y_value),
    )


def op(material, expression_class, a, b, x, y):
    node = expression(material, expression_class, x, y)
    connect(a, "", node, "A")
    connect(b, "", node, "B")
    return node


def mask(material, source, channels, x, y):
    node = expression(
        material,
        unreal.MaterialExpressionComponentMask,
        x,
        y,
        r="r" in channels,
        g="g" in channels,
        b="b" in channels,
        a="a" in channels,
    )
    source_class_name = source.get_class().get_name()
    if source_class_name.startswith("MaterialExpressionTextureSample"):
        source_output = "RGBA"
    elif source_class_name.startswith("MaterialExpressionVectorParameter"):
        source_output = "RGBA"
    else:
        source_output = ""
    connect(source, source_output, node, "None")
    return node


def append(material, a, b, x, y):
    node = expression(material, unreal.MaterialExpressionAppendVector, x, y)
    connect(a, "", node, "A")
    connect(b, "", node, "B")
    return node


def sample_state(material, texture, uv, x, y):
    node = expression(
        material,
        unreal.MaterialExpressionTextureSampleParameter2D,
        x,
        y,
        parameter_name=unreal.Name("RippleStateTexture"),
        texture=texture,
        sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR,
    )
    connect(uv, "", node, "UVs")
    return node


def ensure_render_target(path):
    render_target, _ = create_asset(
        path,
        unreal.TextureRenderTarget2D,
        unreal.TextureRenderTargetFactoryNew(),
    )
    render_target.set_editor_property("size_x", 512)
    render_target.set_editor_property("size_y", 512)
    render_target.set_editor_property(
        "render_target_format", unreal.TextureRenderTargetFormat.RTF_RG16F
    )
    render_target.set_editor_property(
        "clear_color", unreal.LinearColor(0.0, 0.0, 0.0, 0.0)
    )
    render_target.set_editor_property("auto_generate_mips", False)
    unreal.EditorAssetLibrary.save_loaded_asset(render_target, only_if_is_dirty=False)
    return render_target


def configure_solve_material(state_texture):
    material, _ = create_asset(
        SOLVE_MATERIAL_PATH,
        unreal.Material,
        unreal.MaterialFactoryNew(),
    )
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property(
        "shading_model", unreal.MaterialShadingModel.MSM_UNLIT
    )

    uv = expression(material, unreal.MaterialExpressionTextureCoordinate, -1800, 0)
    resolution = scalar_parameter(material, "RippleResolution", 512.0, -1800, 240)
    one = constant(material, 1.0, -1800, 360)
    texel = op(material, unreal.MaterialExpressionDivide, one, resolution, -1580, 300)
    zero = constant(material, 0.0, -1580, 440)
    texel_x = append(material, texel, zero, -1360, 260)
    texel_y = append(material, zero, texel, -1360, 400)
    uv_left = op(material, unreal.MaterialExpressionSubtract, uv, texel_x, -1120, -220)
    uv_right = op(material, unreal.MaterialExpressionAdd, uv, texel_x, -1120, -80)
    uv_down = op(material, unreal.MaterialExpressionSubtract, uv, texel_y, -1120, 60)
    uv_up = op(material, unreal.MaterialExpressionAdd, uv, texel_y, -1120, 200)

    center = sample_state(material, state_texture, uv, -880, 0)
    left = sample_state(material, state_texture, uv_left, -880, -260)
    right = sample_state(material, state_texture, uv_right, -880, -140)
    down = sample_state(material, state_texture, uv_down, -880, 120)
    up = sample_state(material, state_texture, uv_up, -880, 240)
    current = mask(material, center, "r", -620, -20)
    previous = mask(material, center, "g", -620, 80)
    left_h = mask(material, left, "r", -620, -260)
    right_h = mask(material, right, "r", -620, -160)
    down_h = mask(material, down, "r", -620, 180)
    up_h = mask(material, up, "r", -620, 280)

    two = constant(material, 2.0, -400, -20)
    current_twice = op(
        material, unreal.MaterialExpressionMultiply, current, two, -180, -20
    )
    base_next = op(
        material,
        unreal.MaterialExpressionSubtract,
        current_twice,
        previous,
        20,
        -20,
    )
    horizontal_sum = op(
        material, unreal.MaterialExpressionAdd, left_h, right_h, -380, -210
    )
    vertical_sum = op(
        material, unreal.MaterialExpressionAdd, down_h, up_h, -380, 230
    )
    horizontal_lap = op(
        material,
        unreal.MaterialExpressionSubtract,
        horizontal_sum,
        current_twice,
        -150,
        -190,
    )
    vertical_lap = op(
        material,
        unreal.MaterialExpressionSubtract,
        vertical_sum,
        current_twice,
        -150,
        210,
    )

    simulation = vector_parameter(
        material,
        "RippleSimulationParameters",
        unreal.LinearColor(0.41, 0.41, 0.98, 0.08),
        -380,
        420,
    )
    coefficient_x = mask(material, simulation, "r", -150, 390)
    coefficient_y = mask(material, simulation, "g", -150, 460)
    damping = mask(material, simulation, "b", -150, 530)
    lap_x = op(
        material,
        unreal.MaterialExpressionMultiply,
        horizontal_lap,
        coefficient_x,
        80,
        -180,
    )
    lap_y = op(
        material,
        unreal.MaterialExpressionMultiply,
        vertical_lap,
        coefficient_y,
        80,
        180,
    )
    lap = op(material, unreal.MaterialExpressionAdd, lap_x, lap_y, 300, 0)
    propagated = op(
        material, unreal.MaterialExpressionAdd, base_next, lap, 500, -20
    )
    propagated = op(
        material,
        unreal.MaterialExpressionMultiply,
        propagated,
        damping,
        700,
        -20,
    )

    domain_origin_parameter = vector_parameter(
        material,
        "RippleDomainOrigin",
        unreal.LinearColor(-1000.0, -1000.0, 0.0, 0.0),
        -1800,
        620,
    )
    domain_size_parameter = vector_parameter(
        material,
        "RippleDomainSize",
        unreal.LinearColor(2000.0, 2000.0, 0.0, 0.0),
        -1800,
        720,
    )
    domain_origin = mask(material, domain_origin_parameter, "rg", -1580, 600)
    domain_size = mask(material, domain_size_parameter, "rg", -1580, 700)
    uv_scaled = op(
        material, unreal.MaterialExpressionMultiply, uv, domain_size, -1360, 650
    )
    world_xy = op(
        material,
        unreal.MaterialExpressionAdd,
        domain_origin,
        uv_scaled,
        -1140,
        650,
    )

    impulse_sum = None
    for index in range(8):
        row_y = 620 + index * 125
        impulse = vector_parameter(
            material,
            f"RippleImpulse{index}",
            unreal.LinearColor(0.0, 0.0, 1.0, 0.0),
            -880,
            row_y,
        )
        impulse_xy = mask(material, impulse, "rg", -660, row_y)
        impulse_radius = mask(material, impulse, "b", -660, row_y + 45)
        impulse_strength = scalar_parameter(
            material,
            f"RippleImpulseStrength{index}",
            0.0,
            -660,
            row_y + 85,
        )
        distance = expression(material, unreal.MaterialExpressionDistance, -440, row_y)
        connect(world_xy, "", distance, "A")
        connect(impulse_xy, "", distance, "B")
        normalized = op(
            material,
            unreal.MaterialExpressionDivide,
            distance,
            impulse_radius,
            -230,
            row_y,
        )
        saturate = expression(
            material, unreal.MaterialExpressionSaturate, -30, row_y
        )
        connect(normalized, "", saturate, "None")
        falloff = expression(material, unreal.MaterialExpressionOneMinus, 160, row_y)
        connect(saturate, "", falloff, "None")
        falloff_squared = op(
            material,
            unreal.MaterialExpressionMultiply,
            falloff,
            falloff,
            350,
            row_y,
        )
        weighted = op(
            material,
            unreal.MaterialExpressionMultiply,
            falloff_squared,
            impulse_strength,
            550,
            row_y,
        )
        if impulse_sum is None:
            impulse_sum = weighted
        else:
            impulse_sum = op(
                material,
                unreal.MaterialExpressionAdd,
                impulse_sum,
                weighted,
                760 + index * 30,
                row_y,
            )

    scalar_parameter(material, "RippleImpulseCount", 0.0, -1800, 820)
    scalar_parameter(material, "RippleFixedDeltaTime", 1.0 / 60.0, -1800, 900)
    next_height = op(
        material,
        unreal.MaterialExpressionAdd,
        propagated,
        impulse_sum,
        1120,
        80,
    )
    state_rg = append(material, next_height, current, 1320, 60)
    state_rgb = append(material, state_rg, zero, 1500, 60)
    connect_property(state_rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    return material


def configure_impulse_material():
    material, _ = create_asset(
        IMPULSE_MATERIAL_PATH,
        unreal.Material,
        unreal.MaterialFactoryNew(),
    )
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    material.set_editor_property(
        "shading_model", unreal.MaterialShadingModel.MSM_UNLIT
    )
    uv = expression(material, unreal.MaterialExpressionTextureCoordinate, -700, 0)
    center = constant2(material, 0.5, 0.5, -700, 130)
    distance = expression(material, unreal.MaterialExpressionDistance, -470, 20)
    connect(uv, "", distance, "A")
    connect(center, "", distance, "B")
    radius = scalar_parameter(material, "ImpulseRadiusUV", 0.08, -470, 150)
    normalized = op(
        material, unreal.MaterialExpressionDivide, distance, radius, -250, 20
    )
    saturate = expression(material, unreal.MaterialExpressionSaturate, -50, 20)
    connect(normalized, "", saturate, "None")
    falloff = expression(material, unreal.MaterialExpressionOneMinus, 150, 20)
    connect(saturate, "", falloff, "None")
    strength = scalar_parameter(material, "ImpulseStrength", 1.0, 150, 150)
    output = op(
        material, unreal.MaterialExpressionMultiply, falloff, strength, 360, 20
    )
    connect_property(output, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    return material


def configure_water_material(state_texture):
    material, _ = create_asset(
        BASE_MATERIAL_PATH,
        unreal.Material,
        unreal.MaterialFactoryNew(),
    )
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property(
        "shading_model", unreal.MaterialShadingModel.MSM_SINGLE_LAYER_WATER
    )
    material.set_editor_property("two_sided", True)

    shallow = vector_parameter(
        material,
        "ShallowWaterColor",
        unreal.LinearColor(0.16, 0.42, 0.46, 1.0),
        -1500,
        -420,
    )
    deep = vector_parameter(
        material,
        "DeepWaterColor",
        unreal.LinearColor(0.015, 0.09, 0.14, 1.0),
        -1500,
        -300,
    )
    scene_depth = expression(
        material, unreal.MaterialExpressionSceneDepthWithoutWater, -1500, -120
    )
    pixel_depth = expression(material, unreal.MaterialExpressionPixelDepth, -1500, 0)
    water_depth = op(
        material,
        unreal.MaterialExpressionSubtract,
        scene_depth,
        pixel_depth,
        -1280,
        -80,
    )
    depth_distance = scalar_parameter(
        material, "DepthColorDistance", 420.0, -1280, 60
    )
    depth_normalized = op(
        material,
        unreal.MaterialExpressionDivide,
        water_depth,
        depth_distance,
        -1060,
        -80,
    )
    depth_alpha = expression(material, unreal.MaterialExpressionSaturate, -860, -80)
    connect(depth_normalized, "", depth_alpha, "None")
    depth_color = expression(
        material, unreal.MaterialExpressionLinearInterpolate, -640, -280
    )
    connect(shallow, "", depth_color, "A")
    connect(deep, "", depth_color, "B")
    connect(depth_alpha, "", depth_color, "Alpha")

    foam_width = scalar_parameter(material, "FoamWidth", 28.0, -1060, 100)
    foam_depth = op(
        material,
        unreal.MaterialExpressionDivide,
        water_depth,
        foam_width,
        -860,
        100,
    )
    foam_depth_saturate = expression(
        material, unreal.MaterialExpressionSaturate, -660, 100
    )
    connect(foam_depth, "", foam_depth_saturate, "None")
    foam_mask = expression(material, unreal.MaterialExpressionOneMinus, -470, 100)
    connect(foam_depth_saturate, "", foam_mask, "None")
    foam_intensity = scalar_parameter(
        material, "FoamIntensity", 0.42, -470, 210
    )
    foam = op(
        material,
        unreal.MaterialExpressionMultiply,
        foam_mask,
        foam_intensity,
        -260,
        100,
    )
    foam_color = vector_parameter(
        material,
        "FoamColor",
        unreal.LinearColor(0.88, 0.96, 1.0, 1.0),
        -260,
        220,
    )
    foam_tint = op(
        material, unreal.MaterialExpressionMultiply, foam, foam_color, -40, 100
    )
    surface_color = op(
        material,
        unreal.MaterialExpressionAdd,
        depth_color,
        foam_tint,
        180,
        -220,
    )
    connect_property(surface_color, "", unreal.MaterialProperty.MP_BASE_COLOR)

    roughness = scalar_parameter(
        material, "SurfaceRoughness", 0.08, 180, -80
    )
    opacity = scalar_parameter(material, "SurfaceOpacity", 0.72, 180, 20)
    connect_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    connect_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)

    world_position = expression(
        material, unreal.MaterialExpressionWorldPosition, -1500, 420
    )
    world_xy = mask(material, world_position, "rg", -1280, 420)
    domain_origin_parameter = vector_parameter(
        material,
        "RippleDomainOrigin",
        unreal.LinearColor(-1000.0, -1000.0, 0.0, 0.0),
        -1500,
        560,
    )
    domain_size_parameter = vector_parameter(
        material,
        "RippleDomainSize",
        unreal.LinearColor(2000.0, 2000.0, 0.0, 0.0),
        -1500,
        660,
    )
    domain_origin = mask(material, domain_origin_parameter, "rg", -1280, 540)
    domain_size = mask(material, domain_size_parameter, "rg", -1280, 640)
    local_xy = op(
        material,
        unreal.MaterialExpressionSubtract,
        world_xy,
        domain_origin,
        -1060,
        460,
    )
    ripple_uv = op(
        material,
        unreal.MaterialExpressionDivide,
        local_xy,
        domain_size,
        -850,
        460,
    )
    resolution = scalar_parameter(material, "RippleResolution", 512.0, -1060, 720)
    one = constant(material, 1.0, -1060, 820)
    texel = op(material, unreal.MaterialExpressionDivide, one, resolution, -850, 760)
    zero = constant(material, 0.0, -850, 860)
    texel_x = append(material, texel, zero, -640, 720)
    texel_y = append(material, zero, texel, -640, 820)
    uv_left = op(
        material, unreal.MaterialExpressionSubtract, ripple_uv, texel_x, -420, 560
    )
    uv_right = op(
        material, unreal.MaterialExpressionAdd, ripple_uv, texel_x, -420, 660
    )
    uv_down = op(
        material, unreal.MaterialExpressionSubtract, ripple_uv, texel_y, -420, 760
    )
    uv_up = op(
        material, unreal.MaterialExpressionAdd, ripple_uv, texel_y, -420, 860
    )
    left = mask(
        material,
        sample_state(material, state_texture, uv_left, -180, 550),
        "r",
        40,
        550,
    )
    right = mask(
        material,
        sample_state(material, state_texture, uv_right, -180, 650),
        "r",
        40,
        650,
    )
    down = mask(
        material,
        sample_state(material, state_texture, uv_down, -180, 750),
        "r",
        40,
        750,
    )
    up = mask(
        material,
        sample_state(material, state_texture, uv_up, -180, 850),
        "r",
        40,
        850,
    )
    dx = op(
        material, unreal.MaterialExpressionSubtract, left, right, 260, 600
    )
    dy = op(material, unreal.MaterialExpressionSubtract, down, up, 260, 800)
    normal_strength = scalar_parameter(
        material, "RippleNormalStrength", 10.0, 260, 920
    )
    dx = op(
        material,
        unreal.MaterialExpressionMultiply,
        dx,
        normal_strength,
        480,
        600,
    )
    dy = op(
        material,
        unreal.MaterialExpressionMultiply,
        dy,
        normal_strength,
        480,
        800,
    )
    normal_xy = append(material, dx, dy, 700, 680)
    normal_z = constant(material, 1.0, 700, 820)
    normal_xyz = append(material, normal_xy, normal_z, 900, 700)
    normal = expression(material, unreal.MaterialExpressionNormalize, 1100, 700)
    connect(normal_xyz, "", normal, "VectorInput")
    connect_property(normal, "", unreal.MaterialProperty.MP_NORMAL)

    frequency = scalar_parameter(
        material, "WpoSpatialFrequency", 0.012, -1060, 1040
    )
    speed = scalar_parameter(material, "WpoSpeed", 0.55, -1060, 1140)
    amplitude = scalar_parameter(material, "WpoAmplitude", 1.5, -1060, 1240)
    time = expression(material, unreal.MaterialExpressionTime, -850, 1140)
    moving_time = op(
        material, unreal.MaterialExpressionMultiply, time, speed, -640, 1140
    )
    world_x = mask(material, world_position, "r", -850, 1000)
    world_y = mask(material, world_position, "g", -850, 1060)
    wave_x_phase = op(
        material,
        unreal.MaterialExpressionMultiply,
        world_x,
        frequency,
        -640,
        1000,
    )
    wave_y_phase = op(
        material,
        unreal.MaterialExpressionMultiply,
        world_y,
        frequency,
        -640,
        1060,
    )
    wave_x_phase = op(
        material,
        unreal.MaterialExpressionAdd,
        wave_x_phase,
        moving_time,
        -420,
        1010,
    )
    wave_y_phase = op(
        material,
        unreal.MaterialExpressionSubtract,
        wave_y_phase,
        moving_time,
        -420,
        1110,
    )
    wave_x = expression(material, unreal.MaterialExpressionSine, -200, 1010)
    wave_y = expression(material, unreal.MaterialExpressionSine, -200, 1110)
    connect(wave_x_phase, "", wave_x, "None")
    connect(wave_y_phase, "", wave_y, "None")
    wave = op(material, unreal.MaterialExpressionAdd, wave_x, wave_y, 20, 1060)
    wave = op(
        material,
        unreal.MaterialExpressionMultiply,
        wave,
        amplitude,
        240,
        1060,
    )
    wpo_xy = constant2(material, 0.0, 0.0, 240, 1160)
    wpo = append(material, wpo_xy, wave, 460, 1080)
    connect_property(wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

    scattering = vector_parameter(
        material,
        "WaterScatteringCoefficients",
        unreal.LinearColor(0.0030, 0.0060, 0.0075, 0.0),
        700,
        -380,
    )
    absorption = vector_parameter(
        material,
        "WaterAbsorptionCoefficients",
        unreal.LinearColor(0.0015, 0.0008, 0.00045, 0.0),
        700,
        -280,
    )
    phase_g = scalar_parameter(material, "WaterPhaseG", 0.15, 700, -180)
    behind = vector_parameter(
        material,
        "ColorScaleBehindWater",
        unreal.LinearColor(0.82, 0.96, 1.0, 1.0),
        700,
        -80,
    )
    slw_output = expression(
        material,
        unreal.MaterialExpressionSingleLayerWaterMaterialOutput,
        1040,
        -300,
    )
    connect(scattering, "", slw_output, "ScatteringCoefficients")
    connect(absorption, "", slw_output, "AbsorptionCoefficients")
    connect(phase_g, "", slw_output, "PhaseG")
    connect(behind, "", slw_output, "ColorScaleBehindWater")

    unreal.MaterialEditingLibrary.set_base_material_usage(
        material, unreal.MaterialUsage.MATUSAGE_WATER, True
    )
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    if not unreal.MaterialEditingLibrary.has_material_usage(
        material, unreal.MaterialUsage.MATUSAGE_WATER
    ):
        raise RuntimeError("M_RoverWater was not marked Used with Water")
    return material


def configure_water_material_instance(parent):
    instance, _ = create_asset(
        WATER_MATERIAL_PATH,
        unreal.MaterialInstanceConstant,
        unreal.MaterialInstanceConstantFactoryNew(),
    )
    unreal.MaterialEditingLibrary.set_material_instance_parent(instance, parent)
    scalar_values = {
        "DepthColorDistance": 420.0,
        "SurfaceRoughness": 0.08,
        "SurfaceOpacity": 0.72,
        "RippleNormalStrength": 10.0,
        "WpoAmplitude": 1.5,
        "WpoSpatialFrequency": 0.012,
        "WpoSpeed": 0.55,
        "FoamWidth": 28.0,
        "FoamIntensity": 0.42,
    }
    vector_values = {
        "ShallowWaterColor": unreal.LinearColor(0.16, 0.42, 0.46, 1.0),
        "DeepWaterColor": unreal.LinearColor(0.015, 0.09, 0.14, 1.0),
        "WaterScatteringCoefficients": unreal.LinearColor(
            0.0030, 0.0060, 0.0075, 0.0
        ),
        "WaterAbsorptionCoefficients": unreal.LinearColor(
            0.0015, 0.0008, 0.00045, 0.0
        ),
        "ColorScaleBehindWater": unreal.LinearColor(0.82, 0.96, 1.0, 1.0),
    }
    for name, value in scalar_values.items():
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
            instance, name, value
        )
    for name, value in vector_values.items():
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
            instance, name, value
        )
    unreal.EditorAssetLibrary.save_loaded_asset(instance, only_if_is_dirty=False)
    return instance


def configure_niagara():
    if not unreal.RoverEditorTestLibrary.configure_physics_world_water_assets():
        raise RuntimeError("C++ water Niagara generation failed")
    splash = unreal.load_asset(SPLASH_PATH)
    if not isinstance(splash, unreal.NiagaraSystem):
        raise RuntimeError(f"Missing generated Niagara system: {SPLASH_PATH}")
    unreal.EditorAssetLibrary.save_loaded_asset(splash, only_if_is_dirty=False)
    return splash


def configure_data_asset(solve_material, water_material, splash):
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.WorldWaterRippleConfig)
    config, created = create_asset(
        CONFIG_PATH, unreal.WorldWaterRippleConfig, factory
    )
    settings = config.get_editor_property("settings")
    if created:
        settings.set_editor_property("world_size", unreal.Vector2D(2000.0, 2000.0))
        settings.set_editor_property("render_target_resolution", 512)
        settings.set_editor_property("fixed_step_seconds", 1.0 / 60.0)
        settings.set_editor_property("max_substeps_per_frame", 2)
        settings.set_editor_property("wave_speed", 150.0)
        settings.set_editor_property("damping_per_second", 1.25)
        settings.set_editor_property("edge_damping_width", 0.08)
        settings.set_editor_property("max_queued_impulses", 32)
        settings.set_editor_property("max_impulses_per_step", 8)
    settings.set_editor_property("simulation_material", solve_material)
    settings.set_editor_property("water_surface_material", water_material)
    settings.set_editor_property("water_entry_splash_effect", splash)
    config.set_editor_property("settings", settings)
    unreal.EditorAssetLibrary.save_loaded_asset(config, only_if_is_dirty=False)
    return config


def ensure_map(config, water_material):
    level_editor_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    current_world = editor_subsystem.get_editor_world()
    if not current_world or current_world.get_name() != asset_name(MAP_PATH):
        if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
            if not level_editor_subsystem.load_level(MAP_PATH):
                raise RuntimeError(f"Failed to load water map: {MAP_PATH}")
        else:
            if not unreal.EditorAssetLibrary.does_asset_exist(SOURCE_MAP_PATH):
                raise RuntimeError(f"Missing source map: {SOURCE_MAP_PATH}")
            if not level_editor_subsystem.new_level_from_template(
                MAP_PATH, SOURCE_MAP_PATH
            ):
                raise RuntimeError(
                    f"Failed to create {MAP_PATH} from {SOURCE_MAP_PATH}"
                )
    world = editor_subsystem.get_editor_world()
    if not world:
        raise RuntimeError(f"Failed to load water map: {MAP_PATH}")
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = list(actor_subsystem.get_all_level_actors())
    player_starts = [actor for actor in actors if isinstance(actor, unreal.PlayerStart)]
    origin = (
        player_starts[0].get_actor_location()
        if player_starts
        else unreal.Vector(0.0, 0.0, 100.0)
    )
    water_location = origin + unreal.Vector(650.0, 0.0, -90.0)

    zones = [actor for actor in actors if actor.get_class().get_name() == "WaterZone"]
    lakes = [
        actor for actor in actors if actor.get_class().get_name() == "WaterBodyLake"
    ]
    regions = [
        actor
        for actor in actors
        if actor.get_class().get_name() == "WorldWaterRippleRegion"
    ]
    if len(regions) > 1:
        raise RuntimeError(
            "Generated water map has duplicate ripple regions: "
            f"regions={len(regions)}"
        )

    # These actors were generated by the previous WaterBodyLake implementation.
    # Removing them keeps this P0 map free of Landscape water-brush edits.
    for obsolete_actor in zones + lakes:
        if not actor_subsystem.destroy_actor(obsolete_actor):
            raise RuntimeError(
                f"Failed to remove obsolete actor={obsolete_actor.get_path_name()}"
            )

    water_plane_class = unreal.EditorAssetLibrary.load_blueprint_class(
        WATER_PLANE_BLUEPRINT_PATH
    )
    if not water_plane_class:
        raise RuntimeError(
            f"Missing water plane Blueprint={WATER_PLANE_BLUEPRINT_PATH}"
        )
    interactive_planes = [
        actor
        for actor in actors
        if actor.get_class() == water_plane_class
        and WATER_PLANE_TAG in {str(tag) for tag in actor.get_editor_property("tags")}
    ]
    if len(interactive_planes) > 1:
        raise RuntimeError(
            f"Generated water map has duplicate interactive planes={len(interactive_planes)}"
        )
    plane = interactive_planes[0] if interactive_planes else actor_subsystem.spawn_actor_from_class(
        water_plane_class, water_location, unreal.Rotator()
    )
    if not interactive_planes and plane:
        plane.set_actor_scale3d(unreal.Vector(20.0, 20.0, 1.0))

    assembly_location = plane.get_actor_location() if plane else water_location
    region = regions[0] if regions else actor_subsystem.spawn_actor_from_class(
        unreal.WorldWaterRippleRegion, assembly_location, unreal.Rotator()
    )
    if not plane or not region:
        raise RuntimeError("Failed to spawn BP_Waterplane or ripple region")

    plane.set_actor_label("Physics World Interactive Water Plane")
    region.set_actor_label("Physics World Water Ripple Region")
    region.set_editor_property("water_ripple_config", config)
    region.set_editor_property("target_water_surface_actor", plane)
    region.set_editor_property("fit_domain_to_target_surface_bounds", True)
    plane_tags = list(plane.get_editor_property("tags"))
    plane_tag = unreal.Name(WATER_PLANE_TAG)
    if plane_tag not in plane_tags:
        plane_tags.append(plane_tag)
        plane.set_editor_property("tags", plane_tags)
    tags = list(region.get_editor_property("tags"))
    tag = unreal.Name(REGION_TAG)
    if tag not in tags:
        tags.append(tag)
        region.set_editor_property("tags", tags)

    if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH):
        raise RuntimeError(f"Failed to save water map: {MAP_PATH}")
    return plane, region


def main():
    for directory in (
        ROOT,
        CONFIG_DIRECTORY,
        MATERIAL_DIRECTORY,
        SIMULATION_DIRECTORY,
        NIAGARA_DIRECTORY,
        MAP_DIRECTORY,
    ):
        ensure_directory(directory)

    state_a = ensure_render_target(STATE_A_PATH)
    ensure_render_target(STATE_B_PATH)
    solve_material = configure_solve_material(state_a)
    configure_impulse_material()
    base_material = configure_water_material(state_a)
    water_material = configure_water_material_instance(base_material)
    splash = configure_niagara()
    config = configure_data_asset(solve_material, water_material, splash)
    plane, region = ensure_map(config, water_material)
    if not unreal.EditorAssetLibrary.save_directory(
        ROOT, only_if_is_dirty=True, recursive=True
    ):
        raise RuntimeError(f"Failed to save generated assets under {ROOT}")

    unreal.log(
        "PHYSICS_WORLD_WATER_CONFIG_OK "
        f"config={config.get_path_name()} "
        f"material={water_material.get_path_name()} "
        f"splash={splash.get_path_name()} "
        f"plane={plane.get_path_name()} "
        f"region={region.get_path_name()}"
    )


main()
