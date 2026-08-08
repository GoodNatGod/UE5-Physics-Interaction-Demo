import os
from pathlib import Path

import unreal


ROVER_MESH_PATH = "/Game/Rover/Character/SK_Rover_Male"
ATTACK04_PATH = "/Game/Rover/Combat/Animations/Attack04"
ATTACK04_MONTAGE_PATH = "/Game/Rover/Combat/Montages/AM_Rover_Attack04"
COMBAT_CONFIG_PATH = "/Game/Rover/Combat/DA_RoverCombatConfig"
ATTACK04_BLEND_OUT_TIME = 0.25


def unpack_editor_result(result):
    if isinstance(result, tuple):
        return bool(result[0]), str(result[-1])
    return bool(result), str(result)


def configure_attack04_blend_out():
    config = unreal.load_asset(COMBAT_CONFIG_PATH)
    if not isinstance(config, unreal.RoverCombatConfig):
        raise RuntimeError(f"Missing Rover combat config: {COMBAT_CONFIG_PATH}")

    settings = config.get_editor_property("settings")
    attack_chain = list(settings.get_editor_property("light_attack_chain"))
    if len(attack_chain) < 4:
        raise RuntimeError(
            f"Attack04 generation produced only {len(attack_chain)} attack definitions"
        )
    attack04 = attack_chain[3]
    attack04.set_editor_property("montage_blend_out_time", ATTACK04_BLEND_OUT_TIME)
    attack04.set_editor_property(
        "montage_blend_out_trigger_time", ATTACK04_BLEND_OUT_TIME
    )
    settings.set_editor_property("light_attack_chain", attack_chain)
    config.set_editor_property("settings", settings)
    if not unreal.EditorAssetLibrary.save_loaded_asset(config, only_if_is_dirty=False):
        raise RuntimeError("Failed to save Attack04 full-body blend-out tuning")


def main():
    source_value = os.environ.get("ROVER_ATTACK04_FBX")
    if not source_value:
        raise RuntimeError("ROVER_ATTACK04_FBX is required")
    source_path = Path(source_value).resolve()
    if not source_path.is_file():
        raise RuntimeError(f"Attack04 FBX was not found: {source_path}")

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
    task.set_editor_property("destination_name", "Attack04")
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("replace_existing_settings", True)
    task.set_editor_property("save", True)
    task.set_editor_property("options", options)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    sequence = unreal.load_asset(ATTACK04_PATH)
    if not isinstance(sequence, unreal.AnimSequence):
        raise RuntimeError(f"Attack04 import failed: {ATTACK04_PATH}")
    if sequence.get_editor_property("skeleton") != skeleton:
        raise RuntimeError("Attack04 was not imported on the Rover skeleton")

    succeeded, initial_report = unpack_editor_result(
        unreal.RoverAnimationEditorLibrary.create_rover_light_attack_asset(
            sequence, 4
        )
    )
    if not succeeded:
        raise RuntimeError(f"Attack04 Montage generation failed: {initial_report}")

    configure_attack04_blend_out()
    succeeded, report = unpack_editor_result(
        unreal.RoverAnimationEditorLibrary.create_rover_light_attack_asset(
            sequence, 4
        )
    )
    if not succeeded:
        raise RuntimeError(f"Attack04 Montage blend-out update failed: {report}")

    montage = unreal.load_asset(ATTACK04_MONTAGE_PATH)
    validation_succeeded, validation_report = unpack_editor_result(
        unreal.RoverAnimationEditorLibrary.validate_rover_attack_montage(
            sequence, montage
        )
    )
    if not validation_succeeded:
        raise RuntimeError(f"Attack04 validation failed: {validation_report}")
    if not unreal.EditorAssetLibrary.save_directory(
        "/Game/Rover/Combat", only_if_is_dirty=False, recursive=True
    ):
        raise RuntimeError("Failed to save Attack04 combat assets")

    unreal.log(
        "ROVER_ATTACK04_IMPORT_OK "
        f"source={source_path} length={sequence.get_play_length():.3f}s "
        f"blend_out={ATTACK04_BLEND_OUT_TIME:.3f}s "
        f"generation={report} validation={validation_report}"
    )


main()
