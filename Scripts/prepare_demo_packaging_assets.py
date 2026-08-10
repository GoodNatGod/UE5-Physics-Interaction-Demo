import unreal


WEAPON_MATERIAL_PATH = "/Game/Rover/Weapons/R2Sword001/M_R2Sword001"


def ensure_weapon_skeletal_mesh_usage() -> None:
    material = unreal.load_asset(WEAPON_MATERIAL_PATH)
    if not isinstance(material, unreal.Material):
        raise RuntimeError(f"Missing weapon material: {WEAPON_MATERIAL_PATH}")

    usage = unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH
    unreal.MaterialEditingLibrary.set_base_material_usage(material, usage, True)
    unreal.MaterialEditingLibrary.recompile_material(material)
    if not unreal.MaterialEditingLibrary.has_material_usage(material, usage):
        raise RuntimeError(
            f"Failed to enable Skeletal Mesh usage: {WEAPON_MATERIAL_PATH}"
        )
    if not unreal.EditorAssetLibrary.save_loaded_asset(
        material, only_if_is_dirty=False
    ):
        raise RuntimeError(f"Failed to save weapon material: {WEAPON_MATERIAL_PATH}")

    unreal.log(
        "ROVER_DEMO_PACKAGING_ASSETS_OK "
        f"weapon_material={material.get_path_name()} skeletal_mesh_usage=true"
    )


ensure_weapon_skeletal_mesh_usage()
