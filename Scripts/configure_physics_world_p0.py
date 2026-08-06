import unreal


ROOT = "/Game/PhysicsWorldDemo"
CONFIG_PATH = f"{ROOT}/Config/DA_WorldInteractionConfig"
DECAL_PATH = f"{ROOT}/Materials/M_P0_BurnDecal"
CRATE_MESH_PATH = f"{ROOT}/Meshes/SM_Demo_WoodenCrate"
CRATE_EXTERIOR_MATERIAL_PATH = f"{ROOT}/Materials/M_Demo_WoodCrate"
CRATE_INTERIOR_MATERIAL_PATH = f"{ROOT}/Materials/M_Demo_WoodInterior"
GC_PATH = f"{ROOT}/GeometryCollections/GC_Demo_WoodenCrate_Fractured"
COMBAT_CONFIG_PATH = "/Game/Rover/Combat/DA_RoverCombatConfig"
DEFAULT_MAP_PATH = "/Game/ThirdPerson/Lvl_ThirdPerson"
BOX_TAG = "PhysicsWorldP0Box"

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


def ensure_demo_crate_mesh(exterior_material):
    mesh = unreal.load_asset(CRATE_MESH_PATH)
    if not mesh:
        source_path = "/Engine/BasicShapes/Cube"
        if not unreal.EditorAssetLibrary.duplicate_asset(source_path, CRATE_MESH_PATH):
            raise RuntimeError(
                f"Failed to duplicate {source_path} -> {CRATE_MESH_PATH}"
            )
        mesh = unreal.load_asset(CRATE_MESH_PATH)
    if not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError(f"Unexpected asset at {CRATE_MESH_PATH}: {mesh}")
    mesh.set_material(0, exterior_material)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
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
    settings.set_editor_property("destructible_box_max_health", 25.0)
    settings.set_editor_property("destructible_box_default_mass_kg", 35.0)
    settings.set_editor_property("destructible_break_strain", 500000.0)
    settings.set_editor_property("destructible_strain_propagation_depth", 1)
    settings.set_editor_property("destructible_strain_propagation_factor", 1.0)
    settings.set_editor_property("destructible_directional_break_velocity", 450.0)
    settings.set_editor_property("destructible_minimum_break_radius", 180.0)
    settings.set_editor_property("destructible_minimum_break_impulse", 1200.0)
    settings.set_editor_property("destructible_break_impulse_ignores_mass", True)
    settings.set_editor_property("destructible_debris_lifetime", 2.0)
    fireball_effect = niagara_assets.get("NS_P0_Fireball")
    explosion_effect = niagara_assets.get("NS_P0_Explosion")
    impact_effect = niagara_assets.get("NS_P0_SurfaceImpact")
    if fireball_effect:
        settings.set_editor_property("fireball_effect", fireball_effect)
    if explosion_effect:
        settings.set_editor_property("explosion_effect", explosion_effect)

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
        responses.append(response)
    settings.set_editor_property("surface_responses", responses)
    config.set_editor_property("settings", settings)
    unreal.EditorAssetLibrary.save_loaded_asset(config, only_if_is_dirty=False)
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


def ensure_demo_box() -> None:
    world = unreal.EditorLoadingAndSavingUtils.load_map(DEFAULT_MAP_PATH)
    if not world:
        raise RuntimeError(f"Failed to load map: {DEFAULT_MAP_PATH}")
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = actor_subsystem.get_all_level_actors()
    boxes = [actor for actor in actors if actor and actor.actor_has_tag(BOX_TAG)]
    player_starts = [actor for actor in actors if isinstance(actor, unreal.PlayerStart)]
    if not player_starts:
        raise RuntimeError("The default map has no PlayerStart")

    player_start = player_starts[0]
    start_location = player_start.get_actor_location()
    start_rotation = player_start.get_actor_rotation()
    box_location = (
        start_location
        + start_rotation.get_forward_vector() * 450.0
        + start_rotation.get_right_vector() * 180.0
        + unreal.Vector(0.0, 0.0, -40.0)
    )
    box_class = unreal.load_class(None, "/Script/RoverReplica.WorldDestructibleBox")
    if not box_class:
        raise RuntimeError("WorldDestructibleBox class is unavailable")

    if boxes:
        box = boxes[0]
        box.set_actor_location(box_location, False, False)
    else:
        box = actor_subsystem.spawn_actor_from_class(
            box_class, box_location, unreal.Rotator()
        )
        if not box:
            raise RuntimeError("Failed to place the P0 destructible box")
    tags = list(box.get_editor_property("tags"))
    tag_name = unreal.Name(BOX_TAG)
    if tag_name not in tags:
        tags.append(tag_name)
        box.set_editor_property("tags", tags)
    box.set_actor_label("Physics World P0 Destructible Box")
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, DEFAULT_MAP_PATH):
        raise RuntimeError(f"Failed to save map: {DEFAULT_MAP_PATH}")
    unreal.log(f"PHYSICS_WORLD_P0_BOX_READY actor={box.get_path_name()}")


def main() -> None:
    for directory in [
        ROOT,
        f"{ROOT}/Config",
        f"{ROOT}/Materials",
        f"{ROOT}/Materials/PhysicalMaterials",
        f"{ROOT}/Meshes",
        f"{ROOT}/Niagara",
        f"{ROOT}/GeometryCollections",
    ]:
        ensure_directory(directory)

    niagara_assets = {
        name: ensure_duplicated_asset(source, f"{ROOT}/Niagara/{name}")
        for name, source in NIAGARA_TEMPLATES.items()
    }
    physical_materials = {
        name: ensure_physical_material(name, surface_type)
        for name, surface_type in PHYSICAL_MATERIALS.items()
    }
    decal_material = ensure_burn_decal_material()
    crate_exterior_material = ensure_demo_crate_material(
        CRATE_EXTERIOR_MATERIAL_PATH,
        unreal.LinearColor(0.16, 0.055, 0.018, 1.0),
        0.78,
    )
    ensure_demo_crate_material(
        CRATE_INTERIOR_MATERIAL_PATH,
        unreal.LinearColor(0.42, 0.19, 0.06, 1.0),
        0.92,
    )
    ensure_demo_crate_mesh(crate_exterior_material)
    config = ensure_interaction_config(decal_material, niagara_assets)
    ensure_melee_trace_settings()
    geometry_collection = ensure_geometry_collection()
    ensure_demo_box()

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
