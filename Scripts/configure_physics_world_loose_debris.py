import unreal


ROOT = "/Game/PhysicsWorldDemo/LooseDebris"
CONFIG_DIRECTORY = f"{ROOT}/Config"
CONFIG_PATH = f"{CONFIG_DIRECTORY}/DA_WorldLooseDebrisConfig"
BLUEPRINT_DIRECTORY = f"{ROOT}/Blueprints"
BLUEPRINT_NAME = "BP_LooseDebrisRegion"
BLUEPRINT_PATH = f"{BLUEPRINT_DIRECTORY}/{BLUEPRINT_NAME}"
DATA_CHANNEL_PATH = f"{ROOT}/DataChannels/NDC_LooseDebrisInteraction"
SYSTEM_DIRECTORY = f"{ROOT}/Niagara/Systems"
SYSTEM_PATHS = {
    "ambient_effect": f"{SYSTEM_DIRECTORY}/NS_LooseDebris_Ambient",
    "movement_effect": f"{SYSTEM_DIRECTORY}/NS_LooseDebris_Movement",
    "attack_effect": f"{SYSTEM_DIRECTORY}/NS_LooseDebris_Attack",
    "landing_effect": f"{SYSTEM_DIRECTORY}/NS_LooseDebris_Landing",
    "explosion_effect": f"{SYSTEM_DIRECTORY}/NS_LooseDebris_Explosion",
}
MATERIAL_DIRECTORY = f"{ROOT}/Materials"
DEFAULT_MAP_PATH = "/Game/ThirdPerson/Lvl_ThirdPerson"
REGION_TAG = "PhysicsWorldLooseDebrisRegion"
CURRENT_CONFIG_SCHEMA_VERSION = 1


def ensure_directory(path: str) -> None:
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def create_constant(material, value, x, y):
    expression = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionConstant, x, y
    )
    expression.set_editor_property("r", value)
    return expression


def create_constant2(material, x_value, y_value, x, y):
    expression = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionConstant2Vector, x, y
    )
    expression.set_editor_property("r", x_value)
    expression.set_editor_property("g", y_value)
    return expression


def ensure_debris_material(asset_name: str, leaf_mask: bool):
    asset_path = f"{MATERIAL_DIRECTORY}/{asset_name}"
    material = unreal.load_asset(asset_path)
    if material and not isinstance(material, unreal.Material):
        raise RuntimeError(f"Unexpected asset at {asset_path}: {material}")
    if not material:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name,
            MATERIAL_DIRECTORY,
            unreal.Material,
            unreal.MaterialFactoryNew(),
        )
    if not isinstance(material, unreal.Material):
        raise RuntimeError(f"Failed to create {asset_path}")
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("opacity_mask_clip_value", 0.34)

    particle_color = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionParticleColor, -700, -100
    )
    unreal.MaterialEditingLibrary.connect_material_property(
        particle_color, "RGB", unreal.MaterialProperty.MP_BASE_COLOR
    )
    roughness = create_constant(material, 0.88 if leaf_mask else 0.82, -300, 120)
    unreal.MaterialEditingLibrary.connect_material_property(
        roughness, "", unreal.MaterialProperty.MP_ROUGHNESS
    )

    if leaf_mask:
        texcoord = unreal.MaterialEditingLibrary.create_material_expression(
            material, unreal.MaterialExpressionTextureCoordinate, -900, 140
        )
        center = create_constant2(material, 0.5, 0.5, -900, 260)
        subtract = unreal.MaterialEditingLibrary.create_material_expression(
            material, unreal.MaterialExpressionSubtract, -700, 200
        )
        unreal.MaterialEditingLibrary.connect_material_expressions(
            texcoord, "", subtract, "A"
        )
        unreal.MaterialEditingLibrary.connect_material_expressions(
            center, "", subtract, "B"
        )
        absolute = unreal.MaterialEditingLibrary.create_material_expression(
            material, unreal.MaterialExpressionAbs, -540, 200
        )
        unreal.MaterialEditingLibrary.connect_material_expressions(
            subtract, "", absolute, ""
        )
        scale = create_constant2(material, 1.05, 1.65, -540, 320)
        multiply = unreal.MaterialEditingLibrary.create_material_expression(
            material, unreal.MaterialExpressionMultiply, -370, 200
        )
        unreal.MaterialEditingLibrary.connect_material_expressions(
            absolute, "", multiply, "A"
        )
        unreal.MaterialEditingLibrary.connect_material_expressions(
            scale, "", multiply, "B"
        )
        mask_r = unreal.MaterialEditingLibrary.create_material_expression(
            material, unreal.MaterialExpressionComponentMask, -200, 170
        )
        mask_r.set_editor_property("r", True)
        mask_g = unreal.MaterialEditingLibrary.create_material_expression(
            material, unreal.MaterialExpressionComponentMask, -200, 260
        )
        mask_g.set_editor_property("g", True)
        unreal.MaterialEditingLibrary.connect_material_expressions(
            multiply, "", mask_r, ""
        )
        unreal.MaterialEditingLibrary.connect_material_expressions(
            multiply, "", mask_g, ""
        )
        add = unreal.MaterialEditingLibrary.create_material_expression(
            material, unreal.MaterialExpressionAdd, -40, 210
        )
        unreal.MaterialEditingLibrary.connect_material_expressions(mask_r, "", add, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(mask_g, "", add, "B")
        one_minus = unreal.MaterialEditingLibrary.create_material_expression(
            material, unreal.MaterialExpressionOneMinus, 120, 210
        )
        unreal.MaterialEditingLibrary.connect_material_expressions(
            add, "", one_minus, ""
        )
        unreal.MaterialEditingLibrary.connect_material_property(
            one_minus, "", unreal.MaterialProperty.MP_OPACITY_MASK
        )
    else:
        opacity = create_constant(material, 1.0, -300, 240)
        unreal.MaterialEditingLibrary.connect_material_property(
            opacity, "", unreal.MaterialProperty.MP_OPACITY_MASK
        )

    unreal.MaterialEditingLibrary.set_base_material_usage(
        material, unreal.MaterialUsage.MATUSAGE_NIAGARA_SPRITES, True
    )
    unreal.MaterialEditingLibrary.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(
        material, only_if_is_dirty=False
    ):
        raise RuntimeError(f"Failed to save {asset_path}")
    return material


def ensure_generated_niagara_assets():
    if not unreal.RoverEditorTestLibrary.configure_physics_world_loose_debris_assets():
        raise RuntimeError("Failed to generate loose-debris Niagara assets")

    data_channel = unreal.load_asset(DATA_CHANNEL_PATH)
    if not data_channel or data_channel.get_class().get_name() != "NiagaraDataChannelAsset":
        raise RuntimeError(f"Invalid Niagara Data Channel: {DATA_CHANNEL_PATH}")

    systems = {}
    for property_name, asset_path in SYSTEM_PATHS.items():
        system = unreal.load_asset(asset_path)
        if not isinstance(system, unreal.NiagaraSystem):
            raise RuntimeError(f"Invalid Niagara system: {asset_path}")
        systems[property_name] = system
    return data_channel, systems


def ensure_config(data_channel, systems):
    config = unreal.load_asset(CONFIG_PATH)
    if config and not isinstance(config, unreal.WorldLooseDebrisConfig):
        raise RuntimeError(f"Unexpected asset at {CONFIG_PATH}: {config}")
    if not config:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.WorldLooseDebrisConfig)
        config = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "DA_WorldLooseDebrisConfig",
            CONFIG_DIRECTORY,
            unreal.WorldLooseDebrisConfig,
            factory,
        )
    if not isinstance(config, unreal.WorldLooseDebrisConfig):
        raise RuntimeError(f"Failed to create {CONFIG_PATH}")

    settings = config.get_editor_property("settings")

    # Version 1 removes the legacy continuous aerodynamic spin from ambient
    # debris. Apply this once, then preserve all later designer-authored values.
    schema_version = int(config.get_editor_property("asset_schema_version"))
    if schema_version < 1:
        settings.set_editor_property("ambient_rotational_drag", 8.0)
        settings.set_editor_property("ambient_leaf_rotation_strength", 0.0)
        settings.set_editor_property("ambient_paper_rotation_strength", 0.0)
        settings.set_editor_property("ambient_resting_calming_rate", 12.0)
        settings.set_editor_property("ambient_bouncing_calming_rate", 12.0)
        settings.set_editor_property("ambient_restitution", 0.0)
        config.set_editor_property(
            "asset_schema_version", CURRENT_CONFIG_SCHEMA_VERSION
        )

    # Migrate only the original non-responsive defaults. Once any value is
    # changed in the DataAsset, subsequent generator runs preserve it.
    if (
        int(settings.get_editor_property("ambient_particle_budget")) == 300
        and abs(float(settings.get_editor_property("movement_radius")) - 140.0) < 0.01
        and abs(float(settings.get_editor_property("attack_interaction_radius")) - 90.0) < 0.01
        and abs(float(settings.get_editor_property("interaction_force_scale")) - 1.0) < 0.01
        and abs(float(settings.get_editor_property("interaction_force_radius_scale")) - 1.0) < 0.01
        and abs(float(settings.get_editor_property("minimum_interaction_force_duration")) - 0.08) < 0.01
    ):
        settings.set_editor_property("ambient_particle_budget", 450)
        settings.set_editor_property("movement_radius", 180.0)
        settings.set_editor_property("attack_interaction_radius", 150.0)
        settings.set_editor_property("interaction_force_scale", 10.0)
        settings.set_editor_property("interaction_force_radius_scale", 2.0)
        settings.set_editor_property("minimum_interaction_force_duration", 0.18)

    settings.set_editor_property("interaction_data_channel", data_channel)
    for property_name, system in systems.items():
        settings.set_editor_property(property_name, system)
    # Jump reuses the landing graph but receives its own runtime values on a
    # separate Niagara component.
    settings.set_editor_property("jump_effect", systems["landing_effect"])
    config.set_editor_property("settings", settings)
    if not unreal.EditorAssetLibrary.save_loaded_asset(
        config, only_if_is_dirty=False
    ):
        raise RuntimeError(f"Failed to save {CONFIG_PATH}")
    return config


def ensure_region_blueprint():
    blueprint = unreal.load_asset(BLUEPRINT_PATH)
    if blueprint and not isinstance(blueprint, unreal.Blueprint):
        raise RuntimeError(f"Unexpected asset at {BLUEPRINT_PATH}: {blueprint}")
    if not blueprint:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", unreal.WorldLooseDebrisRegion)
        blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            BLUEPRINT_NAME,
            BLUEPRINT_DIRECTORY,
            unreal.Blueprint,
            factory,
        )
    if not isinstance(blueprint, unreal.Blueprint):
        raise RuntimeError(f"Failed to create {BLUEPRINT_PATH}")

    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    valid_statuses = (
        unreal.BlueprintStatus.BS_UP_TO_DATE,
        unreal.BlueprintStatus.BS_UP_TO_DATE_WITH_WARNINGS,
    )
    status = blueprint.get_editor_property("status")
    if status not in valid_statuses:
        raise RuntimeError(f"Blueprint compile failed for {BLUEPRINT_PATH}: {status}")
    if not unreal.EditorAssetLibrary.save_loaded_asset(
        blueprint, only_if_is_dirty=False
    ):
        raise RuntimeError(f"Failed to save {BLUEPRINT_PATH}")

    generated_class = unreal.load_object(
        None, f"{BLUEPRINT_PATH}.{BLUEPRINT_NAME}_C"
    )
    if not generated_class:
        raise RuntimeError(f"Missing generated class for {BLUEPRINT_PATH}")
    default_object = unreal.get_default_object(generated_class)
    if not isinstance(default_object, unreal.WorldLooseDebrisRegion):
        raise RuntimeError(
            f"{BLUEPRINT_PATH} does not inherit from WorldLooseDebrisRegion"
        )
    return blueprint, generated_class


def ensure_region_actor(config, generated_class):
    world = unreal.EditorLoadingAndSavingUtils.load_map(DEFAULT_MAP_PATH)
    if not world:
        raise RuntimeError(f"Failed to load map: {DEFAULT_MAP_PATH}")

    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = actor_subsystem.get_all_level_actors()
    regions = [
        actor for actor in actors if actor and actor.actor_has_tag(REGION_TAG)
    ]
    if len(regions) > 1:
        paths = ", ".join(actor.get_path_name() for actor in regions)
        raise RuntimeError(
            f"Expected one {REGION_TAG} actor at most; found {len(regions)}: {paths}"
        )
    player_starts = [
        actor for actor in actors if isinstance(actor, unreal.PlayerStart)
    ]
    if not player_starts:
        raise RuntimeError("The default map has no PlayerStart")

    region = regions[0] if regions else None
    if region and region.get_class() != generated_class:
        raise RuntimeError(
            f"Actor tagged {REGION_TAG} must be an exact {BLUEPRINT_NAME} instance; "
            f"found {region.get_path_name()}"
        )
    if not region:
        start_location = player_starts[0].get_actor_location()
        region_location = start_location - unreal.Vector(0.0, 0.0, 80.0)
        region = actor_subsystem.spawn_actor_from_class(
            generated_class, region_location, unreal.Rotator()
        )
        if not region:
            raise RuntimeError(f"Failed to place {BLUEPRINT_NAME}")
        tags = list(region.get_editor_property("tags"))
        region_tag = unreal.Name(REGION_TAG)
        if region_tag not in tags:
            tags.append(region_tag)
            region.set_editor_property("tags", tags)

    region.set_editor_property("loose_debris_config", config)
    region.set_actor_label("Physics World Loose Debris Region")
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, DEFAULT_MAP_PATH):
        raise RuntimeError(f"Failed to save map: {DEFAULT_MAP_PATH}")
    return region


def main() -> None:
    for directory in [
        ROOT,
        CONFIG_DIRECTORY,
        BLUEPRINT_DIRECTORY,
        f"{ROOT}/DataChannels",
        f"{ROOT}/EffectTypes",
        MATERIAL_DIRECTORY,
        f"{ROOT}/Niagara",
        SYSTEM_DIRECTORY,
    ]:
        ensure_directory(directory)

    ensure_debris_material("M_LooseDebris_Leaf", True)
    ensure_debris_material("M_LooseDebris_Paper", False)
    data_channel, systems = ensure_generated_niagara_assets()
    config = ensure_config(data_channel, systems)
    blueprint, generated_class = ensure_region_blueprint()
    region = ensure_region_actor(config, generated_class)

    if not unreal.EditorAssetLibrary.save_directory(
        ROOT, only_if_is_dirty=True, recursive=True
    ):
        raise RuntimeError(f"Failed to save generated assets under {ROOT}")
    unreal.log(
        "PHYSICS_WORLD_LOOSE_DEBRIS_CONFIG_OK "
        f"config={config.get_path_name()} "
        f"channel={data_channel.get_path_name()} "
        f"blueprint={blueprint.get_path_name()} "
        f"actor={region.get_path_name()} "
        f"systems={len(systems)} "
        f"schema={config.get_editor_property('asset_schema_version')} "
        f"force_scale={config.get_editor_property('settings').get_editor_property('interaction_force_scale'):.2f} "
        f"radius_scale={config.get_editor_property('settings').get_editor_property('interaction_force_radius_scale'):.2f} "
        f"ambient_rotation="
        f"{config.get_editor_property('settings').get_editor_property('ambient_leaf_rotation_strength'):.2f}/"
        f"{config.get_editor_property('settings').get_editor_property('ambient_paper_rotation_strength'):.2f} "
        f"ambient_rotational_drag="
        f"{config.get_editor_property('settings').get_editor_property('ambient_rotational_drag'):.2f} "
        f"ambient_restitution="
        f"{config.get_editor_property('settings').get_editor_property('ambient_restitution'):.2f}"
    )


main()
