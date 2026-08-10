import unreal


ASSET_PATH = "/Game/Rover/Animations/P0/Run_Turnback"

sequence = unreal.load_asset(ASSET_PATH)
if not isinstance(sequence, unreal.AnimSequence):
    raise RuntimeError(f"Missing Run_Turnback AnimSequence: {ASSET_PATH}")

import_data = sequence.get_editor_property("asset_import_data")
source_filename = import_data.get_first_filename() if import_data else ""
unreal.log(
    "ROVER_TURNBACK_ASSET "
    f"length={sequence.get_play_length():.6f} "
    f"root_motion={sequence.get_editor_property('enable_root_motion')} "
    f"force_root_lock={sequence.get_editor_property('force_root_lock')} "
    f"root_lock={sequence.get_editor_property('root_motion_root_lock')} "
    f"source={source_filename}"
)

result = unreal.RoverAnimationEditorLibrary.validate_move_stop_root_motion(sequence)
if isinstance(result, tuple):
    succeeded, report = bool(result[0]), str(result[1])
else:
    succeeded, report = bool(result), ""
unreal.log(f"ROVER_TURNBACK_RAW_MOTION accepted_as_stop={succeeded} report={report}")

root_methods = sorted(name for name in dir(sequence) if "root" in name.lower())
unreal.log(f"ROVER_TURNBACK_ROOT_API methods={','.join(root_methods)}")
