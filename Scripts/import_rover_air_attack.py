import os
from pathlib import Path

import unreal


ROVER_MESH_PATH = "/Game/Rover/Character/SK_Rover_Male"
ANIMATION_ROOT = "/Game/Rover/Combat/Animations"
MONTAGE_PATH = "/Game/Rover/Combat/Montages/AM_Rover_AirAttack"
COMBAT_CONFIG_PATH = "/Game/Rover/Combat/DA_RoverCombatConfig"
PHASES = ("Start", "Loop", "End")


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
    unreal.log(f"ROVER_AIR_ATTACK_EDITOR_RESULT {report}")


def make_import_options(skeleton):
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
    return options


def main():
    rover_mesh = unreal.load_asset(ROVER_MESH_PATH)
    if not isinstance(rover_mesh, unreal.SkeletalMesh):
        raise RuntimeError(f"Missing Rover mesh: {ROVER_MESH_PATH}")
    skeleton = rover_mesh.get_editor_property("skeleton")
    if not isinstance(skeleton, unreal.Skeleton):
        raise RuntimeError("The Rover mesh has no Skeleton")

    tasks = []
    for phase in PHASES:
        source_value = os.environ.get(f"ROVER_AIR_ATTACK_{phase.upper()}_FBX")
        if not source_value:
            raise RuntimeError(f"ROVER_AIR_ATTACK_{phase.upper()}_FBX is required")
        source_path = Path(source_value).resolve()
        if not source_path.is_file():
            raise RuntimeError(f"AirAttack {phase} FBX was not found: {source_path}")
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", str(source_path))
        task.set_editor_property("destination_path", ANIMATION_ROOT)
        task.set_editor_property("destination_name", f"AirAttack_{phase}")
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("replace_existing_settings", True)
        task.set_editor_property("save", True)
        task.set_editor_property("options", make_import_options(skeleton))
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    sequences = []
    for phase in PHASES:
        asset_path = f"{ANIMATION_ROOT}/AirAttack_{phase}"
        sequence = unreal.load_asset(asset_path)
        if not isinstance(sequence, unreal.AnimSequence):
            raise RuntimeError(f"AirAttack import failed: {asset_path}")
        if sequence.get_editor_property("skeleton") != skeleton:
            raise RuntimeError(f"AirAttack {phase} uses the wrong skeleton")
        sequences.append(sequence)

    result = unreal.RoverAnimationEditorLibrary.create_rover_air_attack_asset(
        sequences[0], sequences[1], sequences[2]
    )
    require_editor_success(result, "AirAttack Montage generation")

    montage = unreal.load_asset(MONTAGE_PATH)
    config = unreal.load_asset(COMBAT_CONFIG_PATH)
    if not isinstance(montage, unreal.AnimMontage):
        raise RuntimeError(f"Missing generated AirAttack Montage: {MONTAGE_PATH}")
    if not isinstance(config, unreal.RoverCombatConfig):
        raise RuntimeError(f"Missing combat config: {COMBAT_CONFIG_PATH}")
    settings = config.get_editor_property("settings")
    definition = settings.get_editor_property("air_attack_definition")
    configured_montage = definition.get_editor_property("montage")
    if configured_montage.get_path_name() != montage.get_path_name():
        raise RuntimeError("AirAttackDefinition does not reference the generated Montage")
    for phase, sequence in zip(PHASES, sequences):
        if sequence.get_editor_property("enable_root_motion"):
            raise RuntimeError(f"AirAttack {phase} Root Motion must remain disabled")
        if not sequence.get_editor_property("force_root_lock"):
            raise RuntimeError(f"AirAttack {phase} Force Root Lock must be enabled")

    if not unreal.EditorAssetLibrary.save_directory(
        "/Game/Rover/Combat", only_if_is_dirty=False, recursive=True
    ):
        raise RuntimeError("Failed to save AirAttack combat assets")

    unreal.log(
        "ROVER_AIR_ATTACK_IMPORT_OK "
        f"montage={montage.get_path_name()} "
        f"lengths={','.join(f'{sequence.get_play_length():.3f}' for sequence in sequences)} "
        f"ascent_height={settings.get_editor_property('air_attack_ascent_height'):.1f} "
        f"apex_frame={settings.get_editor_property('air_attack_apex_frame')} "
        f"descent={settings.get_editor_property('air_attack_descent_speed'):.1f} "
        f"horizontal_scale={settings.get_editor_property('air_attack_horizontal_velocity_scale'):.2f} "
        f"damage={definition.get_editor_property('damage'):.1f}"
    )


main()
