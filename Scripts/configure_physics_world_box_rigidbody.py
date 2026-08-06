import unreal


CONFIG_PATH = "/Game/PhysicsWorldDemo/Config/DA_WorldInteractionConfig"
DEFAULT_MAP_PATH = "/Game/ThirdPerson/Lvl_ThirdPerson"
BOX_TAG = "PhysicsWorldP0Box"
CRATE_MATERIAL_PATHS = (
    "/Game/PhysicsWorldDemo/Materials/M_Demo_WoodCrate",
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


def main():
    ensure_geometry_collection_material_usage()
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
    if default_mass_kg <= 0.0:
        default_mass_kg = 35.0
        settings.set_editor_property(
            "destructible_box_default_mass_kg", default_mass_kg
        )
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
