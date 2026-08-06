import math
import time

import unreal


EXPECTED_PAWN = "/Script/RoverReplica.RoverCharacter"
TRAINING_ENEMY_TAG = "RoverP0TrainingEnemy"
START_TIMEOUT_SECONDS = 30.0
STOP_TIMEOUT_SECONDS = 15.0
SUCCESS_MARKER = "ROVER_LANDING_ROLL_PIE_OK"
FAILURE_MARKER = "ROVER_LANDING_ROLL_PIE_FAIL"


level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
unreal_editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
state = {
    "phase": "starting",
    "deadline": time.monotonic() + START_TIMEOUT_SECONDS,
    "result": None,
    "world": None,
    "pawn": None,
    "movement": None,
    "locomotion": None,
    "mesh": None,
    "phase_world_time": 0.0,
    "was_falling": False,
    "landing_location": None,
    "previous_location": None,
    "landing_yaw": 0.0,
    "roll_state_entered": False,
    "roll_state_entry_time": None,
    "input_unlock_time": None,
    "max_frame_step": 0.0,
    "max_vertical_offset": 0.0,
    "checks": [],
}
tick_handle = None


def object_path(value):
    return value.get_path_name() if value else "None"


def world_time():
    return unreal.GameplayStatics.get_time_seconds(state["world"])


def phase_elapsed():
    return world_time() - state["phase_world_time"]


def horizontal_distance(a, b):
    return math.hypot(a.x - b.x, a.y - b.y)


def horizontal_speed(vector):
    return math.hypot(vector.x, vector.y)


def enum_name(value):
    name = getattr(value, "name", None)
    if name:
        return str(name).lower()
    text = str(value).split(".")[-1]
    return text.split(":", 1)[0].strip("<> ").lower()


def animation_state_name():
    anim_instance = state["mesh"].get_anim_instance()
    if anim_instance is None:
        return ""
    return str(
        unreal.RoverEditorTestLibrary.get_current_animation_state_name(
            anim_instance, 0
        )
    )


def set_move(input_x, input_y, world_direction):
    state["locomotion"].set_move_input(
        unreal.Vector2D(input_x, input_y), world_direction
    )


def clear_move():
    set_move(0.0, 0.0, unreal.Vector(0.0, 0.0, 0.0))


def set_forward_move():
    set_move(0.0, 1.0, unreal.Vector(1.0, 0.0, 0.0))


def add_forward_movement():
    state["pawn"].add_movement_input(unreal.Vector(1.0, 0.0, 0.0), 1.0)


def advance(phase, timeout_seconds=3.0):
    state["phase"] = phase
    state["phase_world_time"] = world_time()
    state["deadline"] = time.monotonic() + timeout_seconds


def fail(detail):
    finish(False, f"phase={state['phase']} {detail}")


def require(condition, detail):
    if not condition:
        fail(detail)
        return False
    return True


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
        clear_move()
        state["locomotion"].set_crouch_held(False)
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


def initialize(world):
    disable_training_enemy_collision(world)
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    if pawn is None:
        return
    if object_path(pawn.get_class()) != EXPECTED_PAWN:
        fail(f"unexpected pawn={object_path(pawn.get_class())}")
        return

    movement = pawn.get_component_by_class(unreal.CharacterMovementComponent)
    locomotion = pawn.get_locomotion_component()
    mesh = pawn.get_component_by_class(unreal.SkeletalMeshComponent)
    if not require(movement is not None, "missing character movement component"):
        return
    if not require(locomotion is not None, "missing locomotion component"):
        return
    if not require(mesh is not None, "missing skeletal mesh component"):
        return
    if not movement.is_moving_on_ground():
        return

    state.update(
        {
            "world": world,
            "pawn": pawn,
            "movement": movement,
            "locomotion": locomotion,
            "mesh": mesh,
        }
    )

    clear_move()
    locomotion.set_crouch_held(False)
    movement.stop_movement_immediately()
    pawn.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0), False)
    if not require(locomotion.try_jump(), "roll setup jump was rejected"):
        return
    advance("airborne", 3.0)


def run_phase():
    phase = state["phase"]
    pawn = state["pawn"]
    movement = state["movement"]
    locomotion = state["locomotion"]

    if phase == "airborne":
        clear_move()
        if movement.is_falling():
            state["was_falling"] = True
            if pawn.get_velocity().z < -100.0:
                locomotion.set_crouch_held(True)
            return
        if not state["was_falling"]:
            return
        if not require(
            enum_name(locomotion.get_landing_type()) == "roll",
            f"landing type={locomotion.get_landing_type()}",
        ):
            return

        locomotion.set_crouch_held(False)
        location = pawn.get_actor_location()
        state["landing_location"] = unreal.Vector(location.x, location.y, location.z)
        state["previous_location"] = unreal.Vector(location.x, location.y, location.z)
        state["landing_yaw"] = pawn.get_actor_rotation().yaw
        set_forward_move()
        advance("rolling", 2.6)
        return

    if phase == "rolling":
        elapsed = phase_elapsed()
        set_forward_move()

        current_state = animation_state_name()
        if current_state == "LandRoll":
            if not state["roll_state_entered"]:
                state["roll_state_entered"] = True
                state["roll_state_entry_time"] = elapsed
        elif not state["roll_state_entered"] and elapsed >= 0.2:
            fail(f"animation never entered LandRoll; current={current_state}")
            return

        accepts_input = bool(locomotion.can_accept_movement_input())
        if accepts_input:
            if state["input_unlock_time"] is None:
                state["input_unlock_time"] = elapsed
            add_forward_movement()
        elif elapsed > 0.82:
            fail(f"movement input remained locked for {elapsed:.2f}s")
            return

        location = pawn.get_actor_location()
        previous = state["previous_location"]
        step = horizontal_distance(location, previous)
        state["max_frame_step"] = max(state["max_frame_step"], step)
        state["max_vertical_offset"] = max(
            state["max_vertical_offset"],
            abs(location.z - state["landing_location"].z),
        )
        state["previous_location"] = unreal.Vector(location.x, location.y, location.z)

        landing_type = enum_name(locomotion.get_landing_type())
        if elapsed < 0.48 and landing_type != "roll":
            fail(f"roll ignored its input lock at {elapsed:.2f}s; type={landing_type}")
            return
        if landing_type == "roll":
            return

        if not require(state["roll_state_entered"], "LandRoll state was never observed"):
            return
        if not require(0.5 <= elapsed <= 0.82, f"held-input roll exit={elapsed:.2f}s"):
            return
        unlock_time = state["input_unlock_time"]
        if not require(unlock_time is not None, "movement input never unlocked"):
            return
        if not require(0.5 <= unlock_time <= 0.82, f"input unlock={unlock_time:.2f}s"):
            return

        start = state["landing_location"]
        yaw_radians = math.radians(state["landing_yaw"])
        forward_x = math.cos(yaw_radians)
        forward_y = math.sin(yaw_radians)
        right_x = -forward_y
        right_y = forward_x
        delta_x = location.x - start.x
        delta_y = location.y - start.y
        forward_distance = delta_x * forward_x + delta_y * forward_y
        lateral_distance = abs(delta_x * right_x + delta_y * right_y)
        total_distance = math.hypot(delta_x, delta_y)
        if not require(
            20.0 <= total_distance <= 300.0,
            f"roll displacement={total_distance:.1f}cm forward={forward_distance:.1f}cm",
        ):
            return
        if not require(forward_distance >= 15.0, f"roll moved backward {forward_distance:.1f}cm"):
            return
        if not require(lateral_distance <= 75.0, f"roll lateral drift={lateral_distance:.1f}cm"):
            return
        if not require(state["max_frame_step"] <= 80.0, f"roll teleport step={state['max_frame_step']:.1f}cm"):
            return
        if not require(state["max_vertical_offset"] <= 20.0, f"roll vertical offset={state['max_vertical_offset']:.1f}cm"):
            return

        state["checks"].append(
            f"held_roll_exit={elapsed:.2f}s/{total_distance:.0f}cm/{lateral_distance:.0f}cm_side"
        )
        state["checks"].append(
            f"input_unlock={unlock_time:.2f}s max_step={state['max_frame_step']:.0f}cm"
        )
        advance("recovery", 0.7)
        return

    if phase == "recovery":
        set_forward_move()
        add_forward_movement()
        current_state = animation_state_name()
        speed = horizontal_speed(pawn.get_velocity())
        if current_state != "Grounded" and phase_elapsed() < 0.35:
            return
        if not require(current_state == "Grounded", f"roll recovered to {current_state}"):
            return
        if speed < 200.0 and phase_elapsed() < 0.5:
            return
        if not require(speed >= 200.0, f"post-roll speed={speed:.1f}cm/s"):
            return
        state["checks"].append(f"recovery={speed:.0f}cmps")
        finish(True, " ".join(state["checks"]))


def phase_status():
    pawn = state["pawn"]
    movement = state["movement"]
    if pawn is None or movement is None:
        return "pawn_not_ready"
    return (
        f"location={pawn.get_actor_location()} velocity={pawn.get_velocity()} "
        f"falling={movement.is_falling()} anim={animation_state_name()}"
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
        if state["phase"] == "starting":
            if level_editor.is_in_play_in_editor() and world is not None:
                initialize(world)
        else:
            run_phase()

        if state["result"] is None and now >= state["deadline"]:
            fail(f"phase timed out; {phase_status()}")
    except Exception as exc:
        fail(f"exception={exc!r}")


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
tick_handle = unreal.register_slate_post_tick_callback(on_tick)
if not unreal.RoverEditorTestLibrary.request_play_in_new_window():
    unreal.unregister_slate_post_tick_callback(tick_handle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    raise RuntimeError("Unable to request a new-window PIE session")
