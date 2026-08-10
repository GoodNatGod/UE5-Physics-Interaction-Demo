import os
from pathlib import Path

import unreal


ROVER_MESH_PATH = "/Game/Rover/Character/SK_Rover_Male"
ATTACK_EX01_PATH = "/Game/Rover/Combat/Animations/Attack_EX01"
RESONANCE_MONTAGE_PATH = "/Game/Rover/Combat/Montages/AM_Rover_Attack_EX01"
COMBAT_CONFIG_PATH = "/Game/Rover/Combat/DA_RoverCombatConfig"
RESONANCE_BLEND_OUT_TIME = 0.25


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
    unreal.log(f"ROVER_RESONANCE_EDITOR_RESULT {report}")


def main():
    source_value = os.environ.get("ROVER_ATTACK_EX01_FBX")
    if not source_value:
        raise RuntimeError("ROVER_ATTACK_EX01_FBX is required")
    source_path = Path(source_value).resolve()
    if not source_path.is_file():
        raise RuntimeError(f"Attack EX01 FBX was not found: {source_path}")

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
    task.set_editor_property("destination_name", "Attack_EX01")
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("replace_existing_settings", True)
    task.set_editor_property("save", True)
    task.set_editor_property("options", options)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    sequence = unreal.load_asset(ATTACK_EX01_PATH)
    if not isinstance(sequence, unreal.AnimSequence):
        raise RuntimeError(f"Attack EX01 import failed: {ATTACK_EX01_PATH}")
    if sequence.get_editor_property("skeleton") != skeleton:
        raise RuntimeError("Attack EX01 was not imported on the Rover skeleton")

    config = unreal.load_asset(COMBAT_CONFIG_PATH)
    if not isinstance(config, unreal.RoverCombatConfig):
        raise RuntimeError(f"Missing combat config: {COMBAT_CONFIG_PATH}")
    settings = config.get_editor_property("settings")
    definition = settings.get_editor_property("heavy_resonance_definition")
    definition.set_editor_property(
        "montage_blend_out_time", RESONANCE_BLEND_OUT_TIME
    )
    definition.set_editor_property(
        "montage_blend_out_trigger_time", RESONANCE_BLEND_OUT_TIME
    )
    settings.set_editor_property("heavy_resonance_definition", definition)
    config.set_editor_property("settings", settings)

    result = unreal.RoverAnimationEditorLibrary.create_rover_heavy_resonance_asset(
        sequence
    )
    require_editor_success(result, "Attack EX01 Montage generation")

    montage = unreal.load_asset(RESONANCE_MONTAGE_PATH)
    if not isinstance(montage, unreal.AnimMontage):
        raise RuntimeError(f"Missing generated Resonance Montage: {RESONANCE_MONTAGE_PATH}")
    settings = config.get_editor_property("settings")
    definition = settings.get_editor_property("heavy_resonance_definition")
    configured_montage = definition.get_editor_property("montage")
    if configured_montage.get_path_name() != montage.get_path_name():
        raise RuntimeError(
            "HeavyResonanceDefinition does not reference the generated EX01 Montage"
        )
    if sequence.get_editor_property("enable_root_motion"):
        raise RuntimeError("Attack EX01 Root Motion must remain disabled")
    if not sequence.get_editor_property("force_root_lock"):
        raise RuntimeError("Attack EX01 Force Root Lock must be enabled")

    validation = unreal.RoverAnimationEditorLibrary.validate_rover_attack_montage(
        sequence, montage
    )
    require_editor_success(validation, "Attack EX01 Montage validation")
    if not unreal.EditorAssetLibrary.save_directory(
        "/Game/Rover/Combat", only_if_is_dirty=False, recursive=True
    ):
        raise RuntimeError("Failed to save Heavy Resonance combat assets")

    unreal.log(
        "ROVER_ATTACK_EX01_IMPORT_OK "
        f"source={source_path} length={sequence.get_play_length():.3f}s "
        f"half_window={settings.get_editor_property('resonance_half_window_normalized'):.2f} "
        f"followup={settings.get_editor_property('resonance_trigger_window_duration'):.3f}s "
        f"dash={settings.get_editor_property('resonance_dash_distance'):.1f}cm/"
        f"{settings.get_editor_property('resonance_dash_duration'):.3f}s "
        f"damage={definition.get_editor_property('damage'):.1f} "
        f"play_rate={definition.get_editor_property('anim_play_rate'):.2f} "
        f"blend_out={definition.get_editor_property('montage_blend_out_time'):.3f}s"
    )


main()
