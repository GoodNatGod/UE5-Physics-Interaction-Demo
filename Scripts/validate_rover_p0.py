import unreal


MESH_PATH = "/Game/Rover/Character/SK_Rover_Male"
ANIMATION_ROOT = "/Game/Rover/Animations/P0"
EXPECTED_ANIMATIONS = (
    "Stand1",
    "Stand2",
    "Stand1_Action01",
    "Stand1_Turn_L90D",
    "Stand1_Turn_R90D",
    "Walk_F",
    "Walk_B",
    "Walk_LF",
    "Walk_LB",
    "Walk_RF",
    "Walk_RB",
    "Run_F",
    "Run_B",
    "Run_LF",
    "Run_LB",
    "Run_RF",
    "Run_RB",
    "Run_Turnback",
    "Stop_Walk_L",
    "Stop_Walk_R",
    "Stop_Run_L",
    "Stop_Run_R",
    "Stop_Sprint_L",
    "Stop_Sprint_R",
    "Sprint_F",
    "Sprint_Impulse_F",
    "Jump_Walk_LF",
    "Jump_Walk_RF",
    "Jump_Run_LF",
    "Jump_Run_RF",
    "Jump_Loop",
    "Jump_Second_F",
    "Jump_Second_B",
    "Fall_Loop",
    "Fall_Loop_Fast",
    "Land_Light",
    "Land_Heavy",
    "Land_Roll",
)

MOVE_STOP_ANIMATIONS = {
    "Stop_Walk_L",
    "Stop_Walk_R",
    "Stop_Run_L",
    "Stop_Run_R",
    "Stop_Sprint_L",
    "Stop_Sprint_R",
}

ROOT_MOTION_ANIMATIONS = MOVE_STOP_ANIMATIONS | {"Land_Roll"}


def _unpack_editor_result(result):
    if isinstance(result, tuple):
        return bool(result[0]), str(result[1]) if len(result) > 1 else ""
    return result is not None, "" if result is None else str(result)


def main() -> None:
    mesh = unreal.load_asset(MESH_PATH)
    if not isinstance(mesh, unreal.SkeletalMesh):
        raise RuntimeError(f"Missing Rover skeletal mesh: {MESH_PATH}")

    skeleton = mesh.get_editor_property("skeleton")
    if not isinstance(skeleton, unreal.Skeleton):
        raise RuntimeError("Rover skeletal mesh does not have a valid skeleton.")

    invalid_assets = []
    total_duration = 0.0
    for asset_name in EXPECTED_ANIMATIONS:
        asset_path = f"{ANIMATION_ROOT}/{asset_name}"
        animation = unreal.load_asset(asset_path)
        if not isinstance(animation, unreal.AnimSequence):
            invalid_assets.append(f"{asset_name}: missing or not AnimSequence")
            continue
        if animation.get_editor_property("skeleton") != skeleton:
            invalid_assets.append(f"{asset_name}: skeleton mismatch")
            continue
        expects_root_motion = asset_name in ROOT_MOTION_ANIMATIONS
        root_motion_enabled = bool(
            animation.get_editor_property("enable_root_motion")
        )
        if root_motion_enabled != expects_root_motion:
            invalid_assets.append(
                f"{asset_name}: root motion is {root_motion_enabled}, "
                f"expected {expects_root_motion}"
            )
        if not bool(animation.get_editor_property("force_root_lock")):
            invalid_assets.append(f"{asset_name}: force root lock is disabled")
        root_lock = animation.get_editor_property("root_motion_root_lock")
        expected_root_lock = (
            unreal.RootMotionRootLock.ANIM_FIRST_FRAME
            if expects_root_motion
            else unreal.RootMotionRootLock.REF_POSE
        )
        if root_lock != expected_root_lock:
            invalid_assets.append(
                f"{asset_name}: root lock is {root_lock}, "
                f"expected {expected_root_lock}"
            )
        if expects_root_motion:
            succeeded, report = _unpack_editor_result(
                unreal.RoverAnimationEditorLibrary.validate_move_stop_root_motion(
                    animation
                )
            )
            if not succeeded:
                invalid_assets.append(
                    f"{asset_name}: root-motion validation failed: {report}"
                )
        total_duration += animation.get_play_length()

    if invalid_assets:
        raise RuntimeError("P0 validation failed: " + "; ".join(invalid_assets))

    unreal.log(
        f"ROVER_P0_VALIDATION_OK mesh={mesh.get_name()} "
        f"skeleton={skeleton.get_name()} animations={len(EXPECTED_ANIMATIONS)} "
        f"duration={total_duration:.2f}s landing_roll_and_move_stop_root_motion=true "
        "other_root_motion=false force_root_lock=true"
    )


main()
