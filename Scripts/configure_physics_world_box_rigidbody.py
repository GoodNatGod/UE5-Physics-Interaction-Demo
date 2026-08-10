import unreal


CONFIG_PATH = "/Game/PhysicsWorldDemo/Config/DA_WorldInteractionConfig"
MOVEMENT_CONFIG_PATH = "/Game/Rover/Config/DA_RoverMovementConfig"
WOOD_PHYSICAL_MATERIAL_PATH = (
    "/Game/PhysicsWorldDemo/Materials/PhysicalMaterials/PM_Wood"
)
DEFAULT_MAP_PATH = "/Game/PhysicsWorldDemo/Maps/L_PhysicsWorldDemo_Lumen"
BOX_TAG = "PhysicsWorldP0Box"
CRATE_MATERIAL_PATHS = (
    "/Game/PhysicsWorldDemo/Materials/M_PW_WoodenBox_GC",
    "/Game/PhysicsWorldDemo/Materials/M_Demo_WoodInterior",
)


def ensure_geometry_collection_material_usage():
    usage = unreal.MaterialUsage.MATUSAGE_GEOMETRY_COLLECTIONS
    for asset_path in CRATE_MATERIAL_PATHS:
        material = unreal.load_asset(asset_path)
        if not isinstance(material, unreal.Material):
            raise RuntimeError(f"Missing crate material: {asset_path}")
        unreal.MaterialEditingLibrary.set_base_material_usage(material, usage, True)
        if not unreal.MaterialEditingLibrary.has_material_usage(material, usage):
            raise RuntimeError(
                f"Failed to enable Geometry Collections usage: {asset_path}"
            )
        if not unreal.EditorAssetLibrary.save_loaded_asset(
            material, only_if_is_dirty=False
        ):
            raise RuntimeError(f"Failed to save {asset_path}")


def ensure_heavy_wood_physical_material():
    material = unreal.load_asset(WOOD_PHYSICAL_MATERIAL_PATH)
    if not isinstance(material, unreal.PhysicalMaterial):
        raise RuntimeError(
            f"Missing wood physical material: {WOOD_PHYSICAL_MATERIAL_PATH}"
        )
    material.set_editor_property("friction", 0.85)
    material.set_editor_property("restitution", 0.05)
    if not unreal.EditorAssetLibrary.save_loaded_asset(
        material, only_if_is_dirty=False
    ):
        raise RuntimeError(f"Failed to save {WOOD_PHYSICAL_MATERIAL_PATH}")


def ensure_character_physics_interaction_settings():
    movement_config = unreal.load_asset(MOVEMENT_CONFIG_PATH)
    if not isinstance(movement_config, unreal.RoverMovementConfig):
        raise RuntimeError(f"Missing Rover movement config: {MOVEMENT_CONFIG_PATH}")

    settings = movement_config.get_editor_property("settings")
    settings.set_editor_property("enable_physics_interaction", True)
    settings.set_editor_property("physics_interaction_push_force_scaled_to_mass", False)
    settings.set_editor_property("physics_interaction_touch_force_scaled_to_mass", False)
    settings.set_editor_property("physics_interaction_scale_push_force_to_velocity", True)
    settings.set_editor_property("physics_interaction_character_mass_kg", 100.0)
    settings.set_editor_property(
        "physics_interaction_standing_downward_force_scale", 1.0
    )
    settings.set_editor_property("physics_interaction_initial_push_force_factor", 100.0)
    settings.set_editor_property("physics_interaction_push_force_factor", 20000.0)
    settings.set_editor_property("physics_interaction_touch_force_factor", 0.1)
    settings.set_editor_property("physics_interaction_min_touch_force", -1.0)
    settings.set_editor_property("physics_interaction_max_touch_force", 50.0)
    settings.set_editor_property("physics_interaction_repulsion_force", 0.5)
    movement_config.set_editor_property("settings", settings)
    if not unreal.EditorAssetLibrary.save_loaded_asset(
        movement_config, only_if_is_dirty=False
    ):
        raise RuntimeError(f"Failed to save {MOVEMENT_CONFIG_PATH}")


def main():
    ensure_geometry_collection_material_usage()
    ensure_heavy_wood_physical_material()
    ensure_character_physics_interaction_settings()
    config = unreal.load_asset(CONFIG_PATH)
    if not isinstance(config, unreal.WorldInteractionConfig):
        raise RuntimeError(f"Missing Physics World config: {CONFIG_PATH}")

    settings = config.get_editor_property("settings")
    settings.set_editor_property("override_world_gravity", True)
    gravity_z = float(settings.get_editor_property("world_gravity_z"))
    if gravity_z >= 0.0:
        gravity_z = -980.0
        settings.set_editor_property("world_gravity_z", gravity_z)
    default_mass_kg = float(
        settings.get_editor_property("destructible_box_default_mass_kg")
    )
    default_mass_kg = 80.0
    settings.set_editor_property("destructible_box_default_mass_kg", default_mass_kg)
    settings.set_editor_property("destructible_intact_linear_damping", 0.8)
    settings.set_editor_property("destructible_intact_angular_damping", 2.0)
    settings.set_editor_property("destructible_debris_linear_damping", 2.5)
    settings.set_editor_property("destructible_debris_angular_damping", 4.0)
    settings.set_editor_property("destructible_directional_break_velocity", 60.0)
    settings.set_editor_property("destructible_break_impulse_scale", 0.03)
    settings.set_editor_property("destructible_minimum_break_impulse", 120.0)
    settings.set_editor_property("destructible_break_impulse_ignores_mass", False)
    config.set_editor_property("settings", settings)
    if not unreal.EditorAssetLibrary.save_loaded_asset(
        config, only_if_is_dirty=False
    ):
        raise RuntimeError(f"Failed to save {CONFIG_PATH}")

    world = unreal.EditorLoadingAndSavingUtils.load_map(DEFAULT_MAP_PATH)
    if world is None:
        raise RuntimeError(f"Failed to load map: {DEFAULT_MAP_PATH}")
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    boxes = [
        actor
        for actor in actor_subsystem.get_all_level_actors()
        if actor is not None and actor.actor_has_tag(BOX_TAG)
    ]
    if not boxes:
        raise RuntimeError(f"Map has no actors tagged {BOX_TAG}")

    masses = []
    for box in boxes:
        intact_mesh = box.get_intact_mesh()
        if not box.ensure_intact_mesh_actor_root():
            raise RuntimeError(f"Failed to migrate {box.get_name()} actor root")
        if not box.is_intact_mesh_actor_root():
            raise RuntimeError(
                f"{box.get_name()} does not use {intact_mesh.get_name()} "
                "as its actor root"
            )
        box.set_editor_property("enable_intact_physics", True)
        mass_kg = max(0.0, float(box.get_editor_property("box_mass_kg")))
        box.set_editor_property("box_mass_kg", mass_kg)
        masses.append(mass_kg if mass_kg > 0.0 else default_mass_kg)

    if not unreal.EditorLoadingAndSavingUtils.save_map(world, DEFAULT_MAP_PATH):
        raise RuntimeError(f"Failed to save map: {DEFAULT_MAP_PATH}")
    unreal.log(
        "PHYSICS_WORLD_BOX_RIGIDBODY_CONFIGURED "
        f"boxes={len(boxes)} masses_kg={masses} "
        f"default_mass_kg={default_mass_kg:.1f} gravity_z={gravity_z:.1f}"
    )


main()
