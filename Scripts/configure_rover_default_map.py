import unreal


DEFAULT_MAP_PATH = "/Game/PhysicsWorldDemo/Maps/L_PhysicsWorldDemo_Lumen"
ROVER_GAME_MODE_CLASS = "/Script/RoverReplica.RoverGameMode"


def main():
    rover_game_mode = unreal.load_class(None, ROVER_GAME_MODE_CLASS)
    if not rover_game_mode:
        raise RuntimeError(f"Missing class: {ROVER_GAME_MODE_CLASS}")

    world = unreal.EditorLoadingAndSavingUtils.load_map(DEFAULT_MAP_PATH)
    if not world:
        raise RuntimeError(f"Could not load map: {DEFAULT_MAP_PATH}")

    world_settings = world.get_world_settings()
    if not world_settings:
        raise RuntimeError(f"Map has no WorldSettings: {DEFAULT_MAP_PATH}")

    world_settings.set_editor_property("default_game_mode", rover_game_mode)
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, DEFAULT_MAP_PATH):
        raise RuntimeError(f"Could not save map: {DEFAULT_MAP_PATH}")

    configured_class = world_settings.get_editor_property("default_game_mode")
    if configured_class != rover_game_mode:
        raise RuntimeError(
            "Default map GameMode did not persist: "
            f"{configured_class.get_path_name() if configured_class else 'None'}"
        )

    unreal.log(
        "ROVER_DEFAULT_MAP_CONFIGURED "
        f"map={DEFAULT_MAP_PATH} game_mode={configured_class.get_path_name()}"
    )


main()
