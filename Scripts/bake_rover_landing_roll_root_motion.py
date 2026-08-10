import unreal


ASSET_PATH = "/Game/Rover/Animations/P0/Land_Roll"


def unpack_result(result):
    if isinstance(result, tuple):
        return bool(result[0]), str(result[1]) if len(result) > 1 else ""
    return result is not None, "" if result is None else str(result)


sequence = unreal.load_asset(ASSET_PATH)
if not isinstance(sequence, unreal.AnimSequence):
    raise RuntimeError(f"Missing landing roll animation: {ASSET_PATH}")

succeeded, report = unpack_result(
    unreal.RoverAnimationEditorLibrary.bake_move_stop_root_motion(sequence)
)
if not succeeded:
    raise RuntimeError(f"Failed to bake Land_Roll root motion: {report}")
if not unreal.EditorAssetLibrary.save_loaded_asset(sequence, only_if_is_dirty=False):
    raise RuntimeError("Failed to save baked Land_Roll animation")

unreal.log(f"ROVER_LANDING_ROLL_ROOT_BAKE_OK {report}")
