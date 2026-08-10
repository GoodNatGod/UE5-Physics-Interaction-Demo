import unreal


ASSET_ROOT = "/Game/Rover/Animations/P0"
STOP_NAMES = (
    "Stop_Walk_L",
    "Stop_Walk_R",
    "Stop_Run_L",
    "Stop_Run_R",
    "Stop_Sprint_L",
    "Stop_Sprint_R",
)


for name in STOP_NAMES:
    sequence = unreal.load_asset(f"{ASSET_ROOT}/{name}")
    if sequence is None:
        unreal.log_error(f"ROVER_STOP_ASSET missing={name}")
        continue

    import_data = sequence.get_editor_property("asset_import_data")
    source_filename = import_data.get_first_filename() if import_data else ""
    unreal.log(
        "ROVER_STOP_ASSET "
        f"name={name} "
        f"length={sequence.get_play_length():.6f} "
        f"root_motion={sequence.get_editor_property('enable_root_motion')} "
        f"force_root_lock={sequence.get_editor_property('force_root_lock')} "
        f"source={source_filename}"
    )
