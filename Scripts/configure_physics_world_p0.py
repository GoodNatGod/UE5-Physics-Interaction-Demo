import unreal


ROOT = "/Game/PhysicsWorldDemo"
CONFIG_PATH = f"{ROOT}/Config/DA_WorldInteractionConfig"
DECAL_PATH = f"{ROOT}/Materials/M_P0_BurnDecal"
CRATE_MESH_PATH = "/Game/ModularLostRuinKit/Models/Props/SM_WoodenBox1"
CRATE_BASE_COLOR_PATH = "/Game/ModularLostRuinKit/Textures/Props/T_WoodenBox_BC"
CRATE_NORMAL_PATH = "/Game/ModularLostRuinKit/Textures/Props/T_WoodenBox_N"
CRATE_EXTERIOR_MATERIAL_PATH = f"{ROOT}/Materials/M_PW_WoodenBox_GC"
CRATE_INTERIOR_MATERIAL_PATH = f"{ROOT}/Materials/M_Demo_WoodInterior"
GC_PATH = f"{ROOT}/GeometryCollections/GC_PW_WoodenBox1_Fractured"
COMBAT_CONFIG_PATH = "/Game/Rover/Combat/DA_RoverCombatConfig"
MOVEMENT_CONFIG_PATH = "/Game/Rover/Config/DA_RoverMovementConfig"
SOURCE_MAP_PATH = "/Game/ModularLostRuinKit/Maps/ExampleMap_Lumen"
DEFAULT_MAP_PATH = f"{ROOT}/Maps/L_PhysicsWorldDemo_Lumen"
BOX_TAG = "PhysicsWorldP0Box"
CONVERTED_BOX_TAG = "PhysicsWorldLostRuinWoodenBox"
MINIMUM_DEMO_BOX_COUNT = 5

PHYSICAL_MATERIALS = {
    "PM_Stone": unreal.PhysicalSurface.SURFACE_TYPE1,
    "PM_Wood": unreal.PhysicalSurface.SURFACE_TYPE2,
    "PM_Metal": unreal.PhysicalSurface.SURFACE_TYPE3,
    "PM_Grass": unreal.PhysicalSurface.SURFACE_TYPE4,
    "PM_Water": unreal.PhysicalSurface.SURFACE_TYPE5,
    "PM_Cloth": unreal.PhysicalSurface.SURFACE_TYPE6,
}

NIAGARA_TEMPLATES = {
    "NS_P0_Fireball": "/Niagara/DefaultAssets/Templates/Systems/FountainLightweight",
    "NS_P0_Explosion": "/Niagara/DefaultAssets/Templates/Systems/SimpleExplosion",
    "NS_P0_SurfaceImpact": "/Niagara/DefaultAssets/Templates/Systems/DirectionalBurst",
}


def ensure_directory(path: str) -> None:
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def ensure_duplicated_asset(source_path: str, destination_path: str):
    existing = unreal.load_asset(destination_path)
    if existing:
        return existing
    source = unreal.load_asset(source_path)
    if not source:
        unreal.log_warning(f"P0 Niagara template unavailable: {source_path}")
        return None
    if not unreal.EditorAssetLibrary.duplicate_asset(source_path, destination_path):
        unreal.log_warning(
            f"Unable to duplicate Niagara template {source_path} -> {destination_path}"
        )
        return None
    return unreal.load_asset(destination_path)


def ensure_physical_material(asset_name: str, surface_type):
    asset_path = f"{ROOT}/Materials/PhysicalMaterials/{asset_name}"
    material = unreal.load_asset(asset_path)
    if not material:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name,
            f"{ROOT}/Materials/PhysicalMaterials",
            unreal.PhysicalMaterial,
            unreal.PhysicalMaterialFactoryNew(),
        )
    if not isinstance(material, unreal.PhysicalMaterial):
        raise RuntimeError(f"Unexpected asset at {asset_path}: {material}")
    material.set_editor_property("surface_type", surface_type)
    if asset_name == "PM_Wood":
        # [PLACEHOLDER] Heavy wood should grip the ground and lose almost all bounce.
        material.set_editor_property("friction", 0.85)
        material.set_editor_property("restitution", 0.05)
    if material.get_editor_property("surface_type") != surface_type:
        raise RuntimeError(f"Failed to set surface type on {asset_path}")
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    return material


def ensure_burn_decal_material():
    material = unreal.load_asset(DECAL_PATH)
    if material:
        if not isinstance(material, unreal.Material):
            raise RuntimeError(f"Unexpected asset at {DECAL_PATH}: {material}")
        return material

    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_P0_BurnDecal",
        f"{ROOT}/Materials",
        unreal.Material,
        unreal.MaterialFactoryNew(),
    )
    if not isinstance(material, unreal.Material):
        raise RuntimeError("Failed to create the P0 burn decal material")

    # P0 placeholder: a dark translucent decal with all tuning exposed by the config.
    material.set_editor_property(
        "material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL
    )
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)

    color = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionConstant3Vector, -360, -60
    )
    color.set_editor_property("constant", unreal.LinearColor(0.015, 0.004, 0.001, 1.0))
    opacity = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionConstant, -360, 80
    )
    opacity.set_editor_property("r", 0.72)
    roughness = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionConstant, -360, 220
    )
    roughness.set_editor_property("r", 0.92)
    unreal.MaterialEditingLibrary.connect_material_property(
        color, "", unreal.MaterialProperty.MP_BASE_COLOR
    )
    unreal.MaterialEditingLibrary.connect_material_property(
        opacity, "", unreal.MaterialProperty.MP_OPACITY
    )
    unreal.MaterialEditingLibrary.connect_material_property(
        roughness, "", unreal.MaterialProperty.MP_ROUGHNESS
    )
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    return material


def ensure_demo_crate_material(asset_path: str, color: unreal.LinearColor, roughness: float):
    material = unreal.load_asset(asset_path)
    if material:
        if not isinstance(material, unreal.Material):
            raise RuntimeError(f"Unexpected asset at {asset_path}: {material}")
    else:
        asset_name = asset_path.rsplit("/", 1)[-1]
        package_path = asset_path.rsplit("/", 1)[0]
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name,
            package_path,
            unreal.Material,
            unreal.MaterialFactoryNew(),
        )
        if not isinstance(material, unreal.Material):
            raise RuntimeError(f"Failed to create demo crate material: {asset_path}")

        base_color = unreal.MaterialEditingLibrary.create_material_expression(
            material, unreal.MaterialExpressionConstant3Vector, -260, -40
        )
        base_color.set_editor_property("constant", color)
        roughness_node = unreal.MaterialEditingLibrary.create_material_expression(
            material, unreal.MaterialExpressionConstant, -260, 100
        )
        roughness_node.set_editor_property("r", roughness)
        unreal.MaterialEditingLibrary.connect_material_property(
            base_color, "", unreal.MaterialProperty.MP_BASE_COLOR
        )
        unreal.MaterialEditingLibrary.connect_material_property(
            roughness_node, "", unreal.MaterialProperty.MP_ROUGHNESS
        )
        unreal.MaterialEditingLibrary.recompile_material(material)

    usage = unreal.MaterialUsage.MATUSAGE_GEOMETRY_COLLECTIONS
    unreal.MaterialEditingLibrary.set_base_material_usage(material, usage, True)
    if not unreal.MaterialEditingLibrary.has_material_usage(material, usage):
        raise RuntimeError(f"Failed to enable Geometry Collections usage: {asset_path}")
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    return material


def ensure_wooden_box_gc_material():
    material = unreal.load_asset(CRATE_EXTERIOR_MATERIAL_PATH)
    if material and not isinstance(material, unreal.Material):
        raise RuntimeError(
            f"Unexpected asset at {CRATE_EXTERIOR_MATERIAL_PATH}: {material}"
        )
    if not material:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "M_PW_WoodenBox_GC",
            f"{ROOT}/Materials",
            unreal.Material,
            unreal.MaterialFactoryNew(),
        )
    if not isinstance(material, unreal.Material):
        raise RuntimeError("Failed to create the wooden-box Geometry Collection material")

    base_color_texture = unreal.load_asset(CRATE_BASE_COLOR_PATH)
    normal_texture = unreal.load_asset(CRATE_NORMAL_PATH)
    if not isinstance(base_color_texture, unreal.Texture2D) or not isinstance(
        normal_texture, unreal.Texture2D
    ):
        raise RuntimeError("The ModularLostRuinKit wooden-box textures are missing")

    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    base_color = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureSample, -360, -80
    )
    base_color.set_editor_property("texture", base_color_texture)
    base_color.set_editor_property(
        "sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
    )
    normal = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureSample, -360, 80
    )
    normal.set_editor_property("texture", normal_texture)
    normal.set_editor_property(
        "sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
    )
    roughness_node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionConstant, -360, 220
    )
    roughness_node.set_editor_property("r", 0.78)
    unreal.MaterialEditingLibrary.connect_material_property(
        base_color, "RGB", unreal.MaterialProperty.MP_BASE_COLOR
    )
    unreal.MaterialEditingLibrary.connect_material_property(
        normal, "RGB", unreal.MaterialProperty.MP_NORMAL
    )
    unreal.MaterialEditingLibrary.connect_material_property(
        roughness_node, "", unreal.MaterialProperty.MP_ROUGHNESS
    )
    unreal.MaterialEditingLibrary.set_base_material_usage(
        material, unreal.MaterialUsage.MATUSAGE_GEOMETRY_COLLECTIONS, True
    )
    unreal.MaterialEditingLibrary.recompile_material(material)
    if not unreal.MaterialEditingLibrary.has_material_usage(
        material, unreal.MaterialUsage.MATUSAGE_GEOMETRY_COLLECTIONS
    ):
        raise RuntimeError("Wooden-box material is missing Geometry Collections usage")
    if not unreal.EditorAssetLibrary.save_loaded_asset(
        material, only_if_is_dirty=False
    ):
        raise RuntimeError(f"Failed to save {CRATE_EXTERIOR_MATERIAL_PATH}")
    return material


def ensure_demo_crate_mesh():
    mesh = unreal.load_asset(CRATE_MESH_PATH)
    if not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError(f"Missing ModularLostRuinKit wooden box: {CRATE_MESH_PATH}")
    return mesh


def ensure_interaction_config(decal_material, niagara_assets):
    config = unreal.load_asset(CONFIG_PATH)
    if not config:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.WorldInteractionConfig)
        config = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "DA_WorldInteractionConfig",
            f"{ROOT}/Config",
            unreal.WorldInteractionConfig,
            factory,
        )
    if not isinstance(config, unreal.WorldInteractionConfig):
        raise RuntimeError(f"Unexpected asset at {CONFIG_PATH}: {config}")

    settings = config.get_editor_property("settings")
    settings.set_editor_property("override_world_gravity", True)
    settings.set_editor_property("world_gravity_z", -980.0)
    # [PLACEHOLDER] Keep melee destruction values explicit in the generated DataAsset.
    settings.set_editor_property("destructible_box_max_health", 10.0)
    settings.set_editor_property("destructible_box_default_mass_kg", 80.0)
    settings.set_editor_property("destructible_intact_linear_damping", 0.8)
    settings.set_editor_property("destructible_intact_angular_damping", 2.0)
    settings.set_editor_property("destructible_debris_linear_damping", 2.5)
    settings.set_editor_property("destructible_debris_angular_damping", 4.0)
    settings.set_editor_property("destructible_break_strain", 500000.0)
    settings.set_editor_property("destructible_strain_propagation_depth", 1)
    settings.set_editor_property("destructible_strain_propagation_factor", 1.0)
    settings.set_editor_property("destructible_directional_break_velocity", 60.0)
    settings.set_editor_property("destructible_break_impulse_scale", 0.03)
    settings.set_editor_property("destructible_minimum_break_radius", 180.0)
    settings.set_editor_property("destructible_minimum_break_impulse", 120.0)
    settings.set_editor_property("destructible_break_impulse_ignores_mass", False)
    settings.set_editor_property("destructible_debris_lifetime", 2.0)
    fireball_effect = unreal.load_asset(
        f"{ROOT}/Niagara/NS_PW_Fireball"
    ) or niagara_assets.get("NS_P0_Fireball")
    explosion_effect = unreal.load_asset(
        f"{ROOT}/Niagara/NS_PW_Explosion"
    ) or niagara_assets.get("NS_P0_Explosion")
    impact_effect = unreal.load_asset(
        f"{ROOT}/Niagara/NS_PW_SurfaceImpact"
    ) or niagara_assets.get("NS_P0_SurfaceImpact")
    chaos_break_effect = unreal.load_asset(f"{ROOT}/Niagara/NS_PW_ChaosBreak")
    if fireball_effect:
        settings.set_editor_property("fireball_effect", fireball_effect)
    if explosion_effect:
        settings.set_editor_property("explosion_effect", explosion_effect)
    if chaos_break_effect:
        settings.set_editor_property("chaos_break_effect", chaos_break_effect)

    responses = []
    for surface_type in [
        unreal.PhysicalSurface.SURFACE_TYPE_DEFAULT,
        unreal.PhysicalSurface.SURFACE_TYPE1,
        unreal.PhysicalSurface.SURFACE_TYPE2,
        unreal.PhysicalSurface.SURFACE_TYPE3,
        unreal.PhysicalSurface.SURFACE_TYPE4,
        unreal.PhysicalSurface.SURFACE_TYPE5,
        unreal.PhysicalSurface.SURFACE_TYPE6,
    ]:
        response = unreal.WorldSurfaceResponse()
        response.set_editor_property("surface_type", surface_type)
        response.set_editor_property("burn_decal_material", decal_material)
        response.set_editor_property(
            "burn_decal_size", unreal.Vector(8.0, 70.0, 70.0)
        )
        response.set_editor_property(
            "allow_burn_decal",
            surface_type != unreal.PhysicalSurface.SURFACE_TYPE5,
        )
        if impact_effect:
            response.set_editor_property("impact_effect", impact_effect)
            response.set_editor_property("impact_effect_scale", 1.0)
        responses.append(response)
    settings.set_editor_property("surface_responses", responses)
    config.set_editor_property("settings", settings)
    if not unreal.EditorAssetLibrary.save_loaded_asset(
        config, only_if_is_dirty=False
    ):
        raise RuntimeError(f"Failed to save {CONFIG_PATH}")
    return config


def ensure_geometry_collection():
    if not unreal.RoverEditorTestLibrary.create_demo_wooden_crate_fractured_geometry_collection(
        GC_PATH,
        CRATE_MESH_PATH,
        CRATE_EXTERIOR_MATERIAL_PATH,
        CRATE_INTERIOR_MATERIAL_PATH,
    ):
        raise RuntimeError("Failed to create the fractured demo wooden crate")
    collection = unreal.load_asset(GC_PATH)
    if not collection or collection.get_class().get_name() != "GeometryCollection":
        raise RuntimeError(f"Invalid Geometry Collection asset: {collection}")
    stats = unreal.RoverEditorTestLibrary.get_geometry_collection_structure_stats(GC_PATH)
    if not stats.get_editor_property("is_expected_demo_fracture"):
        raise RuntimeError(
            "Unexpected demo crate fracture structure: "
            f"leaves={stats.get_editor_property('rigid_leaf_count')} "
            f"clusters={stats.get_editor_property('cluster_count')} "
            f"internal_faces={stats.get_editor_property('internal_face_count')}"
        )
    if not stats.get_editor_property("has_simulation_data"):
        raise RuntimeError(
            "Demo crate has invalid Chaos simulation data: "
            f"simulatable={stats.get_editor_property('simulatable_particle_count')} "
            f"implicits={stats.get_editor_property('implicit_count')}"
        )
    unreal.log(
        "PHYSICS_WORLD_DEMO_CRATE_FRACTURE_OK "
        f"version={stats.get_editor_property('asset_version')} "
        f"leaves={stats.get_editor_property('rigid_leaf_count')} "
        f"clusters={stats.get_editor_property('cluster_count')} "
        f"internal_faces={stats.get_editor_property('internal_face_count')} "
        f"convex_hulls={stats.get_editor_property('convex_hull_count')} "
        f"convex_leaves={stats.get_editor_property('rigid_leaf_with_convex_count')} "
        f"simulatable={stats.get_editor_property('simulatable_particle_count')} "
        f"implicits={stats.get_editor_property('implicit_count')}"
    )
    unreal.EditorAssetLibrary.save_loaded_asset(collection, only_if_is_dirty=False)
    return collection


def ensure_melee_trace_settings():
    combat_config = unreal.load_asset(COMBAT_CONFIG_PATH)
    if not isinstance(combat_config, unreal.RoverCombatConfig):
        raise RuntimeError(f"Unexpected combat config: {combat_config}")

    settings = combat_config.get_editor_property("settings")
    attack_chain = list(settings.get_editor_property("light_attack_chain"))
    trace_settings = (
        (10.0, 7, 10.0, 8),
        (11.0, 7, 10.0, 8),
        (12.0, 7, 10.0, 8),
    )
    if len(attack_chain) < len(trace_settings):
        raise RuntimeError(
            f"Combat config has only {len(attack_chain)} light attack definitions"
        )

    for definition, values in zip(attack_chain, trace_settings):
        radius, sample_count, substep_distance, max_substeps = values
        definition.set_editor_property("trace_radius", radius)
        definition.set_editor_property("trace_sample_count", sample_count)
        definition.set_editor_property("trace_substep_distance", substep_distance)
        definition.set_editor_property("max_trace_substeps", max_substeps)

    settings.set_editor_property("light_attack_chain", attack_chain)
    combat_config.set_editor_property("settings", settings)
    unreal.EditorAssetLibrary.save_loaded_asset(
        combat_config, only_if_is_dirty=False
    )
    unreal.log(
        "ROVER_MELEE_TRACE_CONFIG_OK "
        + " ".join(
            f"A{index + 1}=r{values[0]:.0f}/samples{values[1]}/"
            f"step{values[2]:.0f}/max{values[3]}"
            for index, values in enumerate(trace_settings)
        )
    )
    return combat_config


def ensure_character_physics_interaction_settings():
    movement_config = unreal.load_asset(MOVEMENT_CONFIG_PATH)
    if not isinstance(movement_config, unreal.RoverMovementConfig):
        raise RuntimeError(f"Unexpected movement config: {movement_config}")
    return movement_config


def ensure_demo_map():
    ensure_directory(f"{ROOT}/Maps")
    if not unreal.EditorAssetLibrary.does_asset_exist(DEFAULT_MAP_PATH):
        source_world = unreal.load_asset(SOURCE_MAP_PATH)
        if not source_world:
            raise RuntimeError(f"Missing ModularLostRuinKit map: {SOURCE_MAP_PATH}")
        if not unreal.EditorAssetLibrary.duplicate_asset(
            SOURCE_MAP_PATH, DEFAULT_MAP_PATH
        ):
            raise RuntimeError(
                f"Failed to duplicate {SOURCE_MAP_PATH} -> {DEFAULT_MAP_PATH}"
            )

    world = unreal.EditorLoadingAndSavingUtils.load_map(DEFAULT_MAP_PATH)
    if not world:
        raise RuntimeError(f"Failed to load map: {DEFAULT_MAP_PATH}")
    rover_game_mode = unreal.load_class(None, "/Script/RoverReplica.RoverGameMode")
    if not rover_game_mode:
        raise RuntimeError("RoverGameMode class is unavailable")
    world_settings = world.get_world_settings()
    if not world_settings:
        raise RuntimeError(f"Map has no WorldSettings: {DEFAULT_MAP_PATH}")
    world_settings.set_editor_property("default_game_mode", rover_game_mode)
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, DEFAULT_MAP_PATH):
        raise RuntimeError(f"Failed to save map: {DEFAULT_MAP_PATH}")
    return world


def configure_destructible_box(box, config, mesh, geometry_collection, label):
    box.set_editor_property("interaction_config", config)
    box.set_editor_property("intact_mesh_asset", mesh)
    box.set_editor_property(
        "fractured_geometry_collection_asset", geometry_collection
    )
    box.set_editor_property("enable_intact_physics", True)
    tags = list(box.get_editor_property("tags"))
    for value in (BOX_TAG, CONVERTED_BOX_TAG):
        tag = unreal.Name(value)
        if tag not in tags:
            tags.append(tag)
    box.set_editor_property("tags", tags)
    box.set_actor_label(label)


def ensure_demo_boxes(config, mesh, geometry_collection) -> None:
    world = unreal.EditorLoadingAndSavingUtils.load_map(DEFAULT_MAP_PATH)
    if not world:
        raise RuntimeError(f"Failed to load map: {DEFAULT_MAP_PATH}")
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = [actor for actor in actor_subsystem.get_all_level_actors() if actor]
    player_starts = [actor for actor in actors if isinstance(actor, unreal.PlayerStart)]
    if not player_starts:
        raise RuntimeError("The default map has no PlayerStart")

    box_class = unreal.load_class(None, "/Script/RoverReplica.WorldDestructibleBox")
    if not box_class:
        raise RuntimeError("WorldDestructibleBox class is unavailable")

    converted_count = 0
    source_actors = []
    for actor in actors:
        if actor.get_class() == box_class:
            continue
        component = actor.get_component_by_class(unreal.StaticMeshComponent)
        if component and component.get_editor_property("static_mesh") == mesh:
            source_actors.append(actor)

    for index, source_actor in enumerate(source_actors, start=1):
        source_location = source_actor.get_actor_location()
        source_rotation = source_actor.get_actor_rotation()
        source_scale = source_actor.get_actor_scale3d()
        source_label = source_actor.get_actor_label()
        box = actor_subsystem.spawn_actor_from_class(
            box_class, source_location, source_rotation
        )
        if not box:
            raise RuntimeError(f"Failed to replace wooden box: {source_label}")
        box.set_actor_scale3d(source_scale)
        configure_destructible_box(
            box,
            config,
            mesh,
            geometry_collection,
            f"Physics World Destructible {source_label or index}",
        )
        if not actor_subsystem.destroy_actor(source_actor):
            raise RuntimeError(f"Failed to remove source wooden box: {source_label}")
        converted_count += 1

    boxes = [
        actor
        for actor in actor_subsystem.get_all_level_actors()
        if actor and actor.get_class() == box_class and actor.actor_has_tag(BOX_TAG)
    ]
    player_start = player_starts[0]
    start_location = player_start.get_actor_location()
    start_rotation = player_start.get_actor_rotation()
    nearby_boxes = sorted(
        boxes,
        key=lambda actor: (
            actor.get_actor_location().x - start_location.x
        ) ** 2
        + (actor.get_actor_location().y - start_location.y) ** 2,
    )
    reference_z = nearby_boxes[0].get_actor_location().z if nearby_boxes else (
        start_location.z - 140.0
    )
    extra_offsets = (
        (450.0, 240.0),
        (580.0, -220.0),
        (700.0, 40.0),
        (820.0, 300.0),
        (900.0, -320.0),
    )
    created_count = 0
    for forward_offset, right_offset in extra_offsets:
        if len(boxes) >= MINIMUM_DEMO_BOX_COUNT:
            break
        box_location = (
            start_location
            + start_rotation.get_forward_vector() * forward_offset
            + start_rotation.get_right_vector() * right_offset
        )
        box_location.z = reference_z
        box = actor_subsystem.spawn_actor_from_class(
            box_class,
            box_location,
            unreal.Rotator(0.0, start_rotation.yaw, 0.0),
        )
        if not box:
            raise RuntimeError("Failed to place a showcase destructible box")
        configure_destructible_box(
            box,
            config,
            mesh,
            geometry_collection,
            f"Physics World Destructible Wooden Box {len(boxes) + 1:02d}",
        )
        boxes.append(box)
        created_count += 1

    for index, box in enumerate(boxes, start=1):
        configure_destructible_box(
            box,
            config,
            mesh,
            geometry_collection,
            box.get_actor_label()
            or f"Physics World Destructible Wooden Box {index:02d}",
        )

    if len(boxes) < MINIMUM_DEMO_BOX_COUNT:
        raise RuntimeError(
            f"Expected at least {MINIMUM_DEMO_BOX_COUNT} destructible boxes; "
            f"found {len(boxes)}"
        )
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, DEFAULT_MAP_PATH):
        raise RuntimeError(f"Failed to save map: {DEFAULT_MAP_PATH}")
    unreal.log(
        "PHYSICS_WORLD_P0_BOXES_READY "
        f"map={DEFAULT_MAP_PATH} boxes={len(boxes)} "
        f"converted={converted_count} created={created_count}"
    )


def main() -> None:
    for directory in [
        ROOT,
        f"{ROOT}/Config",
        f"{ROOT}/Materials",
        f"{ROOT}/Materials/PhysicalMaterials",
        f"{ROOT}/Meshes",
        f"{ROOT}/Niagara",
        f"{ROOT}/GeometryCollections",
        f"{ROOT}/Maps",
    ]:
        ensure_directory(directory)

    ensure_demo_map()

    niagara_assets = {
        name: ensure_duplicated_asset(source, f"{ROOT}/Niagara/{name}")
        for name, source in NIAGARA_TEMPLATES.items()
    }
    physical_materials = {
        name: ensure_physical_material(name, surface_type)
        for name, surface_type in PHYSICAL_MATERIALS.items()
    }
    decal_material = ensure_burn_decal_material()
    ensure_wooden_box_gc_material()
    ensure_demo_crate_material(
        CRATE_INTERIOR_MATERIAL_PATH,
        unreal.LinearColor(0.42, 0.19, 0.06, 1.0),
        0.92,
    )
    crate_mesh = ensure_demo_crate_mesh()
    config = ensure_interaction_config(decal_material, niagara_assets)
    ensure_melee_trace_settings()
    ensure_character_physics_interaction_settings()
    geometry_collection = ensure_geometry_collection()
    ensure_demo_boxes(config, crate_mesh, geometry_collection)

    if not unreal.EditorAssetLibrary.save_directory(
        ROOT, only_if_is_dirty=True, recursive=True
    ):
        raise RuntimeError(f"Failed to save generated assets under {ROOT}")
    unreal.log(
        "PHYSICS_WORLD_P0_CONFIG_OK "
        f"physical_materials={len(physical_materials)} "
        f"niagara={sum(1 for asset in niagara_assets.values() if asset)} "
        f"config={config.get_path_name()} gc={geometry_collection.get_path_name()}"
    )


main()
