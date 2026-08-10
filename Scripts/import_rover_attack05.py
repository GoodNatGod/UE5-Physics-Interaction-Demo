import os
from pathlib import Path

import unreal


ROVER_MESH_PATH = "/Game/Rover/Character/SK_Rover_Male"
ATTACK05_PATH = "/Game/Rover/Combat/Animations/Attack05"
ATTACK05_MONTAGE_PATH = "/Game/Rover/Combat/Montages/AM_Rover_Attack05"
COMBAT_CONFIG_PATH = "/Game/Rover/Combat/DA_RoverCombatConfig"


def require_editor_success(result, operation):
    if result is None:
        unreal.log_warning(
            f"{operation} returned no UE Python value; validating generated assets directly"
        )
        return
    succeeded = bool(result[0]) if isinstance(result, tuple) else bool(result)
    report = str(result[-1]) if isinstance(result, tuple) else str(result)
    if not succeeded:
        raise RuntimeError(f"{operation} failed: {report}")


def main():
    source_value = os.environ.get("ROVER_ATTACK05_FBX")
    if not source_value:
        raise RuntimeError("ROVER_ATTACK05_FBX is required")
    source_path = Path(source_value).resolve()
    if not source_path.is_file():
        raise RuntimeError(f"Attack05 FBX was not found: {source_path}")

    rover_mesh = unreal.load_asset(ROVER_MESH_PATH)
    if not isinstance(rover_mesh, unreal.SkeletalMesh):
        raise RuntimeError(f"Missing Rover mesh: {ROVER_MESH_PATH}")
    skeleton = rover_mesh.get_editor_property("skeleton")
    if not isinstance(skeleton, unreal.Skeleton):
        raise RuntimeError("The Rover mesh has no Skeleton")

    options = unreal.FbxImportUI()
    options.set_editor_property("import_as_skeletal", True)
    options.set_editor_property("import_mesh", False)
    options.set_editor_property("import_animations", True)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_textures", False)
    options.set_editor_property(
        "mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION
    )
    options.set_editor_property("skeleton", skeleton)
    animation_data = options.get_editor_property("anim_sequence_import_data")
    animation_data.set_editor_property("import_meshes_in_bone_hierarchy", True)
    animation_data.set_editor_property("import_bone_tracks", True)

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(source_path))
    task.set_editor_property("destination_path", "/Game/Rover/Combat/Animations")
    task.set_editor_property("destination_name", "Attack05")
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("replace_existing_settings", True)
    task.set_editor_property("save", True)
    task.set_editor_property("options", options)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    sequence = unreal.load_asset(ATTACK05_PATH)
    if not isinstance(sequence, unreal.AnimSequence):
        raise RuntimeError(f"Attack05 import failed: {ATTACK05_PATH}")
    if sequence.get_editor_property("skeleton") != skeleton:
        raise RuntimeError("Attack05 was not imported on the Rover skeleton")

    result = unreal.RoverAnimationEditorLibrary.create_rover_heavy_attack_asset(
        sequence
    )
    require_editor_success(result, "Attack05 Montage generation")

    montage = unreal.load_asset(ATTACK05_MONTAGE_PATH)
    config = unreal.load_asset(COMBAT_CONFIG_PATH)
    if not isinstance(montage, unreal.AnimMontage):
        raise RuntimeError(f"Missing generated heavy Montage: {ATTACK05_MONTAGE_PATH}")
    if not isinstance(config, unreal.RoverCombatConfig):
        raise RuntimeError(f"Missing combat config: {COMBAT_CONFIG_PATH}")
    settings = config.get_editor_property("settings")
    heavy = settings.get_editor_property("heavy_attack_definition")
    configured_montage = heavy.get_editor_property("montage")
    if configured_montage.get_path_name() != montage.get_path_name():
        raise RuntimeError(
            "HeavyAttackDefinition does not reference the generated Attack05 Montage"
        )
    if sequence.get_editor_property("enable_root_motion"):
        raise RuntimeError("Attack05 Root Motion must remain disabled")
    if not sequence.get_editor_property("force_root_lock"):
        raise RuntimeError("Attack05 Force Root Lock must be enabled")

    validation = unreal.RoverAnimationEditorLibrary.validate_rover_attack_montage(
        sequence, montage
    )
    require_editor_success(validation, "Attack05 Montage validation")
    if not unreal.EditorAssetLibrary.save_directory(
        "/Game/Rover/Combat", only_if_is_dirty=False, recursive=True
    ):
        raise RuntimeError("Failed to save Attack05 combat assets")

    unreal.log(
        "ROVER_ATTACK05_IMPORT_OK "
        f"source={source_path} length={sequence.get_play_length():.3f}s "
        f"hold={settings.get_editor_property('heavy_attack_hold_threshold'):.3f}s "
        f"retreat={settings.get_editor_property('heavy_attack_retreat_distance'):.1f}cm/"
        f"{settings.get_editor_property('heavy_attack_retreat_duration'):.3f}s "
        f"play_rate={heavy.get_editor_property('anim_play_rate'):.2f}"
    )


main()
