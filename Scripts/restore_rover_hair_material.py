"""Revert the Rover hair look back to the material that shipped with the FBX import.

The custom hair pass (M_Rover_Hair / MI_Rover_Hair plus the long-hair AnimDynamics
chains) was dropped because it read worse than the original skinned hair cards.
This script reassigns the affected skeletal mesh slots to the imported material
instance and removes the custom material assets.
"""

import unreal


MESH_PATH = "/Game/Rover/Character/SK_Rover_Male"
ORIGINAL_MATERIAL_PATH = "/Game/Rover/Character/MI_R2T1PlayerMaleMd10011Hair"
CUSTOM_MATERIAL_INSTANCE_PATH = "/Game/Rover/Character/Materials/MI_Rover_Hair"
CUSTOM_MATERIAL_PATH = "/Game/Rover/Character/Materials/M_Rover_Hair"
CUSTOM_MATERIAL_ROOT = "/Game/Rover/Character/Materials"
MASK_TEXTURE_PATH = "/Game/Rover/Character/T_R2T1PlayerMaleMd10011Hair_HM"


def restore_mesh_slots(mesh: unreal.SkeletalMesh, original) -> int:
    """Point every slot that uses the custom hair material back at the import."""
    materials = list(mesh.get_editor_property("materials"))
    if not materials:
        raise RuntimeError(f"Rover mesh has no material slots: {MESH_PATH}")

    restored = 0
    for index, slot in enumerate(materials):
        assigned = slot.get_editor_property("material_interface")
        assigned_path = assigned.get_path_name() if assigned else "None"
        if assigned_path.startswith(CUSTOM_MATERIAL_INSTANCE_PATH) or assigned_path.startswith(
            CUSTOM_MATERIAL_PATH
        ):
            slot.set_editor_property("material_interface", original)
            restored += 1
            unreal.log(
                "ROVER_HAIR_SLOT_RESTORED "
                f"index={index} slot={slot.get_editor_property('material_slot_name')} "
                f"from={assigned_path} to={original.get_path_name()}"
            )
        else:
            unreal.log(
                "ROVER_HAIR_SLOT_UNCHANGED "
                f"index={index} slot={slot.get_editor_property('material_slot_name')} "
                f"material={assigned_path}"
            )

    if restored:
        mesh.set_editor_property("materials", materials)
        unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
    return restored


def registry_referencer_packages(asset_path: str):
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    referencers = registry.get_referencers(
        asset_path, unreal.AssetRegistryDependencyOptions(include_hard_package_references=True)
    )
    return [str(package) for package in (referencers or []) if str(package) != asset_path]


def package_still_references(package_path: str, asset_path: str) -> bool:
    """Confirm a referencer really still points at the asset.

    The asset registry caches dependencies from the package as it was loaded, so a
    mesh we just resaved is still listed as a referencer. Re-read the mesh material
    slots to tell a genuine reference apart from stale registry data.
    """
    for asset in unreal.EditorAssetLibrary.list_assets(package_path, recursive=False) or [
        package_path
    ]:
        loaded = unreal.load_asset(asset)
        if isinstance(loaded, (unreal.SkeletalMesh, unreal.StaticMesh)):
            for slot in loaded.get_editor_property("materials"):
                assigned = slot.get_editor_property("material_interface")
                if assigned and assigned.get_path_name().startswith(asset_path):
                    return True
            continue
        # Anything other than a mesh slot assignment is not something this script
        # rewired, so treat the registry entry as authoritative.
        return True
    return False


def live_referencers(asset_path: str):
    stale = []
    live = []
    for package in registry_referencer_packages(asset_path):
        if package_still_references(package, asset_path):
            live.append(package)
        else:
            stale.append(package)
    if stale:
        unreal.log(
            f"ROVER_HAIR_STALE_REFERENCERS asset={asset_path} packages={','.join(stale)}"
        )
    return live


def delete_custom_asset(asset_path: str) -> str:
    if not unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        return "absent"

    remaining = live_referencers(asset_path)
    if remaining:
        raise RuntimeError(
            f"Refusing to delete {asset_path}; still referenced by {', '.join(remaining)}."
        )
    if not unreal.EditorAssetLibrary.delete_asset(asset_path):
        raise RuntimeError(f"Failed to delete {asset_path}.")
    return "deleted"


def main() -> None:
    mesh = unreal.load_asset(MESH_PATH)
    original = unreal.load_asset(ORIGINAL_MATERIAL_PATH)
    if not isinstance(mesh, unreal.SkeletalMesh):
        raise RuntimeError(f"Missing Rover mesh: {MESH_PATH}")
    if not isinstance(original, unreal.MaterialInterface):
        raise RuntimeError(f"Missing the imported Rover hair material: {ORIGINAL_MATERIAL_PATH}")

    restored = restore_mesh_slots(mesh, original)

    # The instance holds the only reference to the base material, so it goes first.
    instance_state = delete_custom_asset(CUSTOM_MATERIAL_INSTANCE_PATH)
    material_state = delete_custom_asset(CUSTOM_MATERIAL_PATH)
    if unreal.EditorAssetLibrary.does_directory_exist(CUSTOM_MATERIAL_ROOT):
        if not unreal.EditorAssetLibrary.list_assets(CUSTOM_MATERIAL_ROOT, recursive=True):
            unreal.EditorAssetLibrary.delete_directory(CUSTOM_MATERIAL_ROOT)

    # The mask texture was imported only for the custom material. Report it instead of
    # deleting it, so re-importing does not require the external FBX source tree again.
    mask_referencers = (
        live_referencers(MASK_TEXTURE_PATH)
        if unreal.EditorAssetLibrary.does_asset_exist(MASK_TEXTURE_PATH)
        else None
    )

    unreal.log(
        "ROVER_HAIR_MATERIAL_RESTORED "
        f"mesh={mesh.get_path_name()} restored_slots={restored} "
        f"material_instance={instance_state} material={material_state} "
        f"mask_texture_referencers={','.join(mask_referencers) if mask_referencers else 'none'}"
    )


main()
