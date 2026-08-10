import unreal


MOVEMENT_CONFIG_PATH = "/Game/Rover/Config/DA_RoverMovementConfig"


movement_config = unreal.load_asset(MOVEMENT_CONFIG_PATH)
if not isinstance(movement_config, unreal.RoverMovementConfig):
    raise RuntimeError(f"Missing Rover movement config: {MOVEMENT_CONFIG_PATH}")

settings = movement_config.get_editor_property("settings")
previous_speed = float(
    settings.get_editor_property("camera_auto_follow_interp_speed")
)
settings.set_editor_property("camera_auto_follow_interp_speed", 0.0)
movement_config.set_editor_property("settings", settings)

if not unreal.EditorAssetLibrary.save_loaded_asset(
    movement_config, only_if_is_dirty=False
):
    raise RuntimeError(f"Failed to save {MOVEMENT_CONFIG_PATH}")

saved_config = unreal.load_asset(MOVEMENT_CONFIG_PATH)
saved_speed = float(
    saved_config.get_editor_property("settings").get_editor_property(
        "camera_auto_follow_interp_speed"
    )
)
if abs(saved_speed) > 1.0e-6:
    raise RuntimeError(
        f"Camera auto follow remained enabled after save: speed={saved_speed:.3f}"
    )

unreal.log(
    "ROVER_CAMERA_AUTO_FOLLOW_DISABLED "
    f"previous_speed={previous_speed:.3f} saved_speed={saved_speed:.3f}"
)
