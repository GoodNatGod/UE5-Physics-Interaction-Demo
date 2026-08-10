import unreal


ROVER_CHARACTER_CLASS = "/Script/RoverReplica.RoverCharacter"
ROVER_GAME_MODE_CLASS = "/Script/RoverReplica.RoverGameMode"
ROVER_MESH_PATH = "/Game/Rover/Character/SK_Rover_Male.SK_Rover_Male"
ROVER_ANIM_CLASS_PATH = "/Game/Rover/Animations/ABP_Rover.ABP_Rover_C"
JUMP_ACTION_PATH = "/Game/Input/Actions/IA_Jump.IA_Jump"
MIN_EFFECTIVE_MESH_HEIGHT = 140.0
MAX_EFFECTIVE_MESH_HEIGHT = 200.0


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def object_path(value):
    return value.get_path_name() if value else "None"


def main():
    rover_class = unreal.load_class(None, ROVER_CHARACTER_CLASS)
    require(rover_class, f"Missing class: {ROVER_CHARACTER_CLASS}")

    rover_default = unreal.get_default_object(rover_class)
    mesh_component = rover_default.get_editor_property("mesh")
    require(mesh_component, "RoverCharacter has no skeletal mesh component")

    skeletal_mesh = mesh_component.get_editor_property("skeletal_mesh_asset")
    require(
        object_path(skeletal_mesh) == ROVER_MESH_PATH,
        f"Unexpected Rover mesh: {object_path(skeletal_mesh)}",
    )

    mesh_scale = mesh_component.get_editor_property("relative_scale3d")
    imported_bounds = skeletal_mesh.get_imported_bounds()
    effective_mesh_height = imported_bounds.box_extent.z * 2.0 * abs(mesh_scale.z)
    require(
        MIN_EFFECTIVE_MESH_HEIGHT <= effective_mesh_height <= MAX_EFFECTIVE_MESH_HEIGHT,
        f"Unexpected effective Rover mesh height: {effective_mesh_height:.3f} cm",
    )

    anim_class = mesh_component.get_editor_property("anim_class")
    require(
        object_path(anim_class) == ROVER_ANIM_CLASS_PATH,
        f"Unexpected Rover AnimBP class: {object_path(anim_class)}",
    )

    jump_action = unreal.load_asset(JUMP_ACTION_PATH)
    require(isinstance(jump_action, unreal.InputAction), f"Missing Jump Input Action: {JUMP_ACTION_PATH}")
    require(
        not jump_action.get_editor_property("triggers"),
        "IA_Jump must use the default held-action trigger semantics.",
    )

    game_mode_class = unreal.load_class(None, ROVER_GAME_MODE_CLASS)
    require(game_mode_class, f"Missing class: {ROVER_GAME_MODE_CLASS}")
    game_mode_default = unreal.get_default_object(game_mode_class)
    default_pawn_class = game_mode_default.get_editor_property("default_pawn_class")
    require(
        object_path(default_pawn_class) == ROVER_CHARACTER_CLASS,
        f"Unexpected default pawn class: {object_path(default_pawn_class)}",
    )

    unreal.log(
        "ROVER_RUNTIME_VALIDATION_OK "
        f"pawn={object_path(default_pawn_class)} "
        f"mesh={object_path(skeletal_mesh)} "
        f"mesh_height={effective_mesh_height:.1f} "
        "jump_triggers=none "
        f"anim={object_path(anim_class)}"
    )


main()
