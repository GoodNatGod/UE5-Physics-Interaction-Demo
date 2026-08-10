import unreal


for asset_name in ("Land_Light", "Land_Heavy", "Land_Roll"):
    asset_path = f"/Game/Rover/Animations/P0/{asset_name}"
    sequence = unreal.load_asset(asset_path)
    if not isinstance(sequence, unreal.AnimSequence):
        raise RuntimeError(f"Missing landing animation: {asset_path}")

    result = unreal.RoverAnimationEditorLibrary.validate_move_stop_root_motion(sequence)
    if isinstance(result, tuple):
        root_motion_valid, report = bool(result[0]), str(result[1])
    else:
        root_motion_valid, report = bool(result), ""

    unreal.log(
        "ROVER_LANDING_ASSET "
        f"name={asset_name} length={sequence.get_play_length():.6f} "
        f"root_motion={sequence.get_editor_property('enable_root_motion')} "
        f"force_root_lock={sequence.get_editor_property('force_root_lock')} "
        f"root_motion_valid={root_motion_valid} metrics=({report})"
    )
