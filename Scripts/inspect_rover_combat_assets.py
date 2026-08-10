import unreal


ROVER_MESH_PATH = "/Game/Rover/Character/SK_Rover_Male"
WEAPON_MESH_PATH = "/Game/Rover/Weapons/R2Sword001/SK_R2Sword001"
ANIMATION_ROOT = "/Game/Rover/Combat/Animations"


def describe_bounds(label: str, asset) -> None:
    try:
        bounds = asset.get_bounds()
        unreal.log(
            "ROVER_COMBAT_BOUNDS "
            f"asset={label} origin={bounds.origin} extent={bounds.box_extent} "
            f"radius={bounds.sphere_radius:.3f}"
        )
    except Exception as error:
        unreal.log_warning(f"ROVER_COMBAT_BOUNDS asset={label} unavailable={error}")


rover_mesh = unreal.load_asset(ROVER_MESH_PATH)
weapon_mesh = unreal.load_asset(WEAPON_MESH_PATH)
if not isinstance(rover_mesh, unreal.SkeletalMesh):
    raise RuntimeError(f"Missing Rover mesh: {ROVER_MESH_PATH}")
if not isinstance(weapon_mesh, unreal.SkeletalMesh):
    raise RuntimeError(f"Missing weapon mesh: {WEAPON_MESH_PATH}")

describe_bounds("rover", rover_mesh)
describe_bounds("weapon", weapon_mesh)

for name in ("Attack01", "Attack02", "Attack03"):
    sequence = unreal.load_asset(f"{ANIMATION_ROOT}/{name}")
    if not isinstance(sequence, unreal.AnimSequence):
        unreal.log_warning(f"ROVER_COMBAT_ANIMATION name={name} missing=true")
        continue
    unreal.log(
        "ROVER_COMBAT_ANIMATION "
        f"name={name} length={sequence.get_play_length():.6f} "
        f"root_motion={sequence.get_editor_property('enable_root_motion')} "
        f"force_root_lock={sequence.get_editor_property('force_root_lock')} "
        f"root_lock={sequence.get_editor_property('root_motion_root_lock')}"
    )
    result = unreal.RoverAnimationEditorLibrary.validate_move_stop_root_motion(sequence)
    if isinstance(result, tuple):
        accepted, report = bool(result[0]), str(result[-1])
    else:
        accepted, report = bool(result), ""
    unreal.log(
        f"ROVER_COMBAT_RAW_MOTION name={name} accepted_as_stop={accepted} report={report}"
    )
