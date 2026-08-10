import time

import unreal


EXPECTED_GAME_MODE = "/Script/RoverReplica.RoverGameMode"
EXPECTED_PAWN = "/Script/RoverReplica.RoverCharacter"
TRAINING_ENEMY_TAG = "RoverP0TrainingEnemy"
EXPECTED_MESH = "/Game/Rover/Character/SK_Rover_Male.SK_Rover_Male"
EXPECTED_ANIM_CLASS = "/Game/Rover/Animations/ABP_Rover.ABP_Rover_C"
JUMP_ACTION_PATH = "/Game/Input/Actions/IA_Jump.IA_Jump"
MIN_EFFECTIVE_MESH_HEIGHT = 140.0
MAX_EFFECTIVE_MESH_HEIGHT = 200.0
JUMP_SAMPLE_SECONDS = 0.25
START_TIMEOUT_SECONDS = 30.0
STOP_TIMEOUT_SECONDS = 15.0
SUCCESS_MARKER = "ROVER_PIE_VALIDATION_OK"
FAILURE_MARKER = "ROVER_PIE_VALIDATION_FAIL"


level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
unreal_editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
state = {
    "phase": "starting",
    "deadline": time.monotonic() + START_TIMEOUT_SECONDS,
    "result": None,
    "last": "PIE has not started",
    "pawn": None,
    "movement": None,
    "locomotion": None,
    "world": None,
    "jump_start_location": None,
    "jump_start_time": None,
    "jump_start_world_time": None,
    "mesh_height": None,
}
tick_handle = None


def object_path(value):
    return value.get_path_name() if value else "None"


def horizontal_speed(vector):
    return (vector.x * vector.x + vector.y * vector.y) ** 0.5


def shutdown():
    global tick_handle

    if state["phase"] == "done":
        return

    state["phase"] = "done"
    ok, detail = state["result"]
    marker = SUCCESS_MARKER if ok else FAILURE_MARKER
    (unreal.log if ok else unreal.log_error)(f"{marker} {detail}")

    handle, tick_handle = tick_handle, None
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)


def finish(ok, detail):
    if state["result"] is not None:
        return

    if state["locomotion"] is not None:
        state["locomotion"].set_move_input(
            unreal.Vector2D(0.0, 0.0),
            unreal.Vector(0.0, 0.0, 0.0),
        )
        state["locomotion"].stop_jump()

    state["result"] = (ok, detail)
    if level_editor.is_in_play_in_editor():
        state["phase"] = "stopping"
        state["deadline"] = time.monotonic() + STOP_TIMEOUT_SECONDS
        level_editor.editor_request_end_play()
    else:
        shutdown()


def disable_training_enemy_collision(world):
    for actor in unreal.GameplayStatics.get_all_actors_with_tag(
        world, TRAINING_ENEMY_TAG
    ):
        if actor:
            actor.set_actor_enable_collision(False)


def validate_play_world(world):
    disable_training_enemy_collision(world)
    game_mode = unreal.GameplayStatics.get_game_mode(world)
    game_mode_path = object_path(game_mode.get_class()) if game_mode else "None"
    if game_mode_path != EXPECTED_GAME_MODE:
        finish(False, f"unexpected game_mode={game_mode_path}")
        return

    controller = unreal.GameplayStatics.get_player_controller(world, 0)
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    if controller is None or pawn is None:
        state["last"] = (
            f"waiting for possession controller={object_path(controller)} "
            f"pawn={object_path(pawn)}"
        )
        return

    pawn_class_path = object_path(pawn.get_class())
    if pawn_class_path != EXPECTED_PAWN:
        finish(False, f"unexpected pawn={pawn_class_path}")
        return

    mappings_check = getattr(pawn, "has_active_input_mappings", None)
    if mappings_check is None:
        finish(False, "Python method has_active_input_mappings is unavailable")
        return
    if not mappings_check():
        state["last"] = f"pawn={pawn_class_path} active_input_mappings=false"
        return

    mesh_component = pawn.get_component_by_class(unreal.SkeletalMeshComponent)
    if mesh_component is None:
        finish(False, "Rover pawn has no SkeletalMeshComponent")
        return

    skeletal_mesh = mesh_component.get_editor_property("skeletal_mesh_asset")
    mesh_path = object_path(skeletal_mesh)
    if mesh_path != EXPECTED_MESH:
        finish(False, f"unexpected mesh={mesh_path}")
        return

    mesh_scale = mesh_component.get_editor_property("relative_scale3d")
    imported_bounds = skeletal_mesh.get_imported_bounds()
    effective_mesh_height = imported_bounds.box_extent.z * 2.0 * abs(mesh_scale.z)
    if not MIN_EFFECTIVE_MESH_HEIGHT <= effective_mesh_height <= MAX_EFFECTIVE_MESH_HEIGHT:
        finish(False, f"unexpected effective mesh height={effective_mesh_height:.3f} cm")
        return

    anim_instance = mesh_component.get_anim_instance()
    if anim_instance is None:
        state["last"] = f"pawn={pawn_class_path} waiting for animation instance"
        return

    anim_class_path = object_path(anim_instance.get_class())
    if anim_class_path != EXPECTED_ANIM_CLASS:
        finish(False, f"unexpected anim_class={anim_class_path}")
        return

    jump_action = unreal.load_asset(JUMP_ACTION_PATH)
    if not isinstance(jump_action, unreal.InputAction):
        finish(False, f"missing jump_action={JUMP_ACTION_PATH}")
        return
    if jump_action.get_editor_property("triggers"):
        finish(False, "IA_Jump has explicit triggers; held jump would complete on an input edge")
        return

    movement = pawn.get_component_by_class(unreal.CharacterMovementComponent)
    locomotion = pawn.get_locomotion_component()
    if movement is None or locomotion is None:
        finish(False, "Rover pawn is missing movement or locomotion component")
        return
    if not movement.is_moving_on_ground():
        state["last"] = "waiting for Rover pawn to reach the ground"
        return

    pawn.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0), False)
    locomotion.set_move_input(
        unreal.Vector2D(0.0, 1.0),
        unreal.Vector(1.0, 0.0, 0.0),
    )
    movement.set_editor_property("velocity", unreal.Vector(400.0, 0.0, 0.0))

    state["pawn"] = pawn
    state["movement"] = movement
    state["locomotion"] = locomotion
    state["world"] = world
    jump_start_location = pawn.get_actor_location()
    state["jump_start_location"] = (
        jump_start_location.x,
        jump_start_location.y,
        jump_start_location.z,
    )
    state["jump_start_time"] = time.monotonic()
    state["jump_start_world_time"] = unreal.GameplayStatics.get_time_seconds(world)
    state["mesh_height"] = effective_mesh_height
    if not locomotion.try_jump():
        state["last"] = "waiting for locomotion to accept the grounded jump"
        return

    state["phase"] = "jumping"
    state["deadline"] = time.monotonic() + 2.0
    state["last"] = "ground jump requested"


def validate_jump():
    pawn = state["pawn"]
    movement = state["movement"]
    world = state["world"]
    pawn.add_movement_input(unreal.Vector(1.0, 0.0, 0.0), 1.0)

    elapsed = time.monotonic() - state["jump_start_time"]
    world_elapsed = (
        unreal.GameplayStatics.get_time_seconds(world) - state["jump_start_world_time"]
    )
    location = pawn.get_actor_location()
    start_x, start_y, start_z = state["jump_start_location"]
    delta_x = location.x - start_x
    delta_z = location.z - start_z
    velocity = pawn.get_velocity()
    state["last"] = (
        f"jump elapsed={elapsed:.3f} dx={delta_x:.2f} dz={delta_z:.2f} "
        f"speed2d={horizontal_speed(velocity):.2f} vz={velocity.z:.2f} "
        f"mode={movement.get_editor_property('movement_mode')} "
        f"falling={movement.is_falling()} world_elapsed={world_elapsed:.3f} "
        f"world_dt={unreal.GameplayStatics.get_world_delta_seconds(world):.4f} "
        f"paused={unreal.GameplayStatics.is_game_paused(world)} "
        f"local={pawn.is_locally_controlled()} pawn_tick={pawn.is_actor_tick_enabled()} "
        f"movement_active={movement.is_active()} "
        f"movement_tick={movement.is_component_tick_enabled()}"
    )

    if world_elapsed < JUMP_SAMPLE_SECONDS:
        return
    if not movement.is_falling():
        finish(False, f"ground jump never entered falling movement; {state['last']}")
        return
    if delta_z < 40.0:
        finish(False, f"ground jump produced insufficient vertical displacement; {state['last']}")
        return
    if delta_x < 50.0:
        finish(False, f"forward jump produced insufficient horizontal displacement; {state['last']}")
        return

    finish(
        True,
        " ".join(
            (
                f"game_mode={EXPECTED_GAME_MODE}",
                f"pawn={EXPECTED_PAWN}",
                "active_input_mappings=true",
                f"mesh_height={state['mesh_height']:.1f}",
                "jump_triggers=none",
                f"jump_dx={delta_x:.1f}",
                f"jump_dz={delta_z:.1f}",
            )
        ),
    )


def on_tick(_delta_seconds):
    try:
        now = time.monotonic()

        if state["phase"] == "stopping":
            if not level_editor.is_in_play_in_editor() and unreal_editor.get_game_world() is None:
                shutdown()
            elif now >= state["deadline"]:
                state["result"] = (False, "PIE did not stop before timeout")
                shutdown()
            return

        world = unreal_editor.get_game_world()
        if state["phase"] == "starting" and level_editor.is_in_play_in_editor() and world is not None:
            validate_play_world(world)
        elif state["phase"] == "jumping":
            validate_jump()

        if state["result"] is None and now >= state["deadline"]:
            finish(False, f"validation timeout; last={state['last']}")
    except Exception as exc:
        finish(False, f"exception={exc!r}")


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
tick_handle = unreal.register_slate_post_tick_callback(on_tick)
if not unreal.RoverEditorTestLibrary.request_play_in_new_window():
    unreal.unregister_slate_post_tick_callback(tick_handle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    raise RuntimeError("Unable to request a new-window PIE session")
