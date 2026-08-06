import math
import os
import time

import unreal


EXPECTED_PAWN = "/Script/RoverReplica.RoverCharacter"
TRAINING_ENEMY_TAG = "RoverP0TrainingEnemy"
START_TIMEOUT_SECONDS = 30.0
STOP_TIMEOUT_SECONDS = 15.0
SUCCESS_MARKER = "ROVER_FEEDBACK_PIE_OK"
FAILURE_MARKER = "ROVER_FEEDBACK_PIE_FAIL"
CAMERA_ONLY = os.environ.get("ROVER_VALIDATE_CAMERA_ONLY") == "1"


level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
unreal_editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
state = {
    "phase": "starting",
    "deadline": time.monotonic() + START_TIMEOUT_SECONDS,
    "result": None,
    "world": None,
    "controller": None,
    "pawn": None,
    "movement": None,
    "locomotion": None,
    "mesh": None,
    "camera": None,
    "phase_world_time": 0.0,
    "phase_start_location": None,
    "phase_start_camera": None,
    "phase_start_root": None,
    "jump_start_world_time": 0.0,
    "was_falling": False,
    "max_jump_height": 0.0,
    "max_camera_vertical_lag": 0.0,
    "max_root_horizontal_drift": 0.0,
    "camera_release_world_time": 0.0,
    "camera_max_yaw_drift": 0.0,
    "delayed_landing_input_world_time": 0.0,
    "delayed_landing_anim_exit_delay": None,
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


def yaw_delta(actual, expected):
    return (actual - expected + 180.0) % 360.0 - 180.0


def enum_name(value):
    name = getattr(value, "name", None)
    if name:
        return str(name).lower()

    text = str(value).split(".")[-1]
    return text.split(":", 1)[0].strip("<> ").lower()


def camera_location():
    return state["camera"].get_world_location()


def root_location():
    return state["mesh"].get_socket_location("root")


def set_actor_yaw(yaw):
    state["pawn"].set_actor_rotation(
        unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw),
        False,
    )


def set_control_yaw(yaw):
    current = state["controller"].get_control_rotation()
    state["controller"].set_control_rotation(
        unreal.Rotator(roll=current.roll, pitch=current.pitch, yaw=yaw)
    )


def set_move(input_x, input_y, world_direction):
    state["locomotion"].set_move_input(
        unreal.Vector2D(input_x, input_y),
        world_direction,
    )


def clear_move():
    set_move(0.0, 0.0, unreal.Vector(0.0, 0.0, 0.0))


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
    controller = unreal.GameplayStatics.get_player_controller(world, 0)
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    if controller is None or pawn is None:
        return
    if object_path(pawn.get_class()) != EXPECTED_PAWN:
        fail(f"unexpected pawn={object_path(pawn.get_class())}")
        return

    movement = pawn.get_component_by_class(unreal.CharacterMovementComponent)
    locomotion = pawn.get_locomotion_component()
    mesh = pawn.get_component_by_class(unreal.SkeletalMeshComponent)
    camera = pawn.get_follow_camera()
    if not require(movement is not None, "missing character movement component"):
        return
    if not require(locomotion is not None, "missing locomotion component"):
        return
    if not require(mesh is not None, "missing skeletal mesh component"):
        return
    if not require(camera is not None, "missing follow camera"):
        return
    if not movement.is_moving_on_ground():
        return

    state.update(
        {
            "world": world,
            "controller": controller,
            "pawn": pawn,
            "movement": movement,
            "locomotion": locomotion,
            "mesh": mesh,
            "camera": camera,
            "test_origin": unreal.Vector(
                pawn.get_actor_location().x,
                pawn.get_actor_location().y,
                pawn.get_actor_location().z,
            ),
        }
    )

    jump_velocity = movement.get_editor_property("jump_z_velocity")
    gravity_scale = movement.get_editor_property("gravity_scale")
    air_control = movement.get_editor_property("air_control")
    if not require(620.0 <= jump_velocity <= 640.0, f"jump velocity={jump_velocity:.1f}"):
        return
    if not require(1.35 <= gravity_scale <= 1.55, f"gravity scale={gravity_scale:.2f}"):
        return
    if not require(0.35 <= air_control <= 0.5, f"air control={air_control:.2f}"):
        return

    clear_move()
    movement.stop_movement_immediately()
    set_actor_yaw(90.0)
    set_control_yaw(0.0)
    advance("idle_camera_free", 6.0)


def run_phase():
    phase = state["phase"]
    pawn = state["pawn"]
    movement = state["movement"]
    locomotion = state["locomotion"]

    if phase == "idle_camera_free":
        if phase_elapsed() < 1.0:
            return
        control_yaw = state["controller"].get_control_rotation().yaw
        if not require(abs(yaw_delta(control_yaw, 0.0)) <= 0.5, f"idle camera drifted yaw={control_yaw:.2f}"):
            return
        state["checks"].append("idle_camera=free")

        movement.stop_movement_immediately()
        set_actor_yaw(0.0)
        set_control_yaw(0.0)
        start = pawn.get_actor_location()
        state["phase_start_location"] = start
        set_move(-1.0, 0.0, unreal.Vector(0.0, -1.0, 0.0))
        advance("left_start", 2.0)
        return

    if phase == "left_start":
        pawn.add_movement_input(unreal.Vector(0.0, -1.0, 0.0), 1.0)
        control_yaw = state["controller"].get_control_rotation().yaw
        if not require(abs(yaw_delta(control_yaw, 0.0)) <= 0.5, f"held-left camera yaw={control_yaw:.2f}"):
            return
        if phase_elapsed() < 0.35:
            return
        distance = horizontal_distance(pawn.get_actor_location(), state["phase_start_location"])
        if not require(not locomotion.is_ground_turn_pending(), "full left input requested turn in place"):
            return
        if not require(not locomotion.is_ground_turn_active(), "full left input entered turn in place"):
            return
        if not require(distance >= 20.0, f"left start moved only {distance:.1f} cm"):
            return
        state["checks"].append(f"left_start={distance:.0f}")
        clear_move()
        movement.stop_movement_immediately()
        release_time = world_time()
        state["camera_release_world_time"] = release_time
        state["camera_max_yaw_drift"] = 0.0
        advance("left_stop_camera_free", 3.0)
        return

    if phase == "left_stop_camera_free":
        now = world_time()
        release_elapsed = now - state["camera_release_world_time"]
        control_yaw = state["controller"].get_control_rotation().yaw
        yaw_drift = abs(yaw_delta(control_yaw, 0.0))
        state["camera_max_yaw_drift"] = max(
            state["camera_max_yaw_drift"], yaw_drift
        )
        if not require(yaw_drift <= 0.5, f"stopped camera drifted yaw={control_yaw:.2f}"):
            return

        # Wait well past the former 0.3s recenter delay before accepting the result.
        if release_elapsed < 1.0:
            return
        state["checks"].append(
            f"stop_camera=free/{state['camera_max_yaw_drift']:.2f}deg"
        )
        if CAMERA_ONLY:
            finish(True, "checks=" + ",".join(state["checks"]))
            return

        clear_move()
        movement.stop_movement_immediately()
        pawn.set_actor_location(state["test_origin"], False, True)
        set_actor_yaw(0.0)
        set_control_yaw(0.0)
        start = pawn.get_actor_location()
        state["phase_start_location"] = start
        state["phase_start_camera"] = camera_location()
        state["phase_start_root"] = root_location()
        state["jump_start_world_time"] = world_time()
        state["was_falling"] = False
        state["max_jump_height"] = 0.0
        state["max_camera_vertical_lag"] = 0.0
        state["max_root_horizontal_drift"] = 0.0
        set_move(0.0, 1.0, unreal.Vector(1.0, 0.0, 0.0))
        movement.set_editor_property("velocity", unreal.Vector(400.0, 0.0, 0.0))
        if not require(locomotion.try_jump(), "ground jump was rejected"):
            return
        advance("forward_jump", 3.0)
        return

    if phase == "forward_jump":
        pawn.add_movement_input(unreal.Vector(1.0, 0.0, 0.0), 1.0)
        location = pawn.get_actor_location()
        camera = camera_location()
        root = root_location()
        start = state["phase_start_location"]
        camera_start = state["phase_start_camera"]
        root_start = state["phase_start_root"]
        pawn_rise = location.z - start.z
        camera_rise = camera.z - camera_start.z
        state["max_jump_height"] = max(state["max_jump_height"], pawn_rise)
        state["max_camera_vertical_lag"] = max(
            state["max_camera_vertical_lag"], pawn_rise - camera_rise
        )
        root_relative_x = (root.x - location.x) - (root_start.x - start.x)
        root_relative_y = (root.y - location.y) - (root_start.y - start.y)
        state["max_root_horizontal_drift"] = max(
            state["max_root_horizontal_drift"],
            math.hypot(root_relative_x, root_relative_y),
        )

        if movement.is_falling():
            state["was_falling"] = True
            return
        if not state["was_falling"]:
            return

        flight_time = world_time() - state["jump_start_world_time"]
        jump_distance = horizontal_distance(location, start)
        if not require(300.0 <= jump_distance <= 380.0, f"jump distance={jump_distance:.1f} cm"):
            return
        if not require(105.0 <= state["max_jump_height"] <= 150.0, f"jump height={state['max_jump_height']:.1f} cm"):
            return
        if not require(0.72 <= flight_time <= 0.98, f"flight time={flight_time:.2f} s"):
            return
        if not require(state["max_camera_vertical_lag"] <= 50.0, f"camera vertical lag={state['max_camera_vertical_lag']:.1f} cm"):
            return
        if not require(state["max_root_horizontal_drift"] <= 50.0, f"root drift={state['max_root_horizontal_drift']:.1f} cm"):
            return
        if not require(enum_name(locomotion.get_landing_type()) == "none", f"moving landing type={locomotion.get_landing_type()}"):
            return
        state["checks"].append(
            f"jump={jump_distance:.0f}x{state['max_jump_height']:.0f}/{flight_time:.2f}s"
        )
        state["checks"].append(f"camera_lag={state['max_camera_vertical_lag']:.0f}")
        state["checks"].append(f"root_drift={state['max_root_horizontal_drift']:.0f}")
        advance("landing_exit", 2.0)
        return

    if phase == "landing_exit":
        set_move(0.0, 1.0, unreal.Vector(1.0, 0.0, 0.0))
        pawn.add_movement_input(unreal.Vector(1.0, 0.0, 0.0), 1.0)
        if phase_elapsed() < 0.25:
            return
        anim_instance = state["mesh"].get_anim_instance()
        if not require(anim_instance is not None, "missing animation instance after landing"):
            return
        current_state = str(
            unreal.RoverEditorTestLibrary.get_current_animation_state_name(
                anim_instance, 0
            )
        )
        if not require(current_state == "Grounded", f"animation remained in {current_state}"):
            return
        speed = horizontal_speed(pawn.get_velocity())
        if speed < 100.0 and phase_elapsed() < 1.0:
            return
        if not require(speed >= 100.0, f"post-land speed={speed:.1f}"):
            return
        state["checks"].append("landing=grounded")

        clear_move()
        movement.stop_movement_immediately()
        state["was_falling"] = False
        if not require(locomotion.try_jump(), "delayed-input landing jump was rejected"):
            return
        advance("delayed_landing_airborne", 3.0)
        return

    if phase == "delayed_landing_airborne":
        clear_move()
        if movement.is_falling():
            state["was_falling"] = True
            return
        if not state["was_falling"]:
            return
        if not require(
            enum_name(locomotion.get_landing_type()) == "light",
            f"stationary landing type={locomotion.get_landing_type()}",
        ):
            return
        advance("delayed_landing_wait_input", 0.5)
        return

    if phase == "delayed_landing_wait_input":
        clear_move()
        if phase_elapsed() < 0.05:
            return
        if not require(
            enum_name(locomotion.get_landing_type()) == "light",
            f"light landing expired before delayed input type={locomotion.get_landing_type()}",
        ):
            return

        set_move(0.0, 1.0, unreal.Vector(1.0, 0.0, 0.0))
        pawn.add_movement_input(unreal.Vector(1.0, 0.0, 0.0), 1.0)
        if not require(
            enum_name(locomotion.get_landing_type()) == "none",
            f"delayed input did not interrupt light landing type={locomotion.get_landing_type()}",
        ):
            return
        state["delayed_landing_input_world_time"] = world_time()
        advance("delayed_landing_resume", 0.3)
        return

    if phase == "delayed_landing_resume":
        set_move(0.0, 1.0, unreal.Vector(1.0, 0.0, 0.0))
        pawn.add_movement_input(unreal.Vector(1.0, 0.0, 0.0), 1.0)
        anim_instance = state["mesh"].get_anim_instance()
        if not require(anim_instance is not None, "missing animation instance during delayed landing exit"):
            return
        current_state = str(
            unreal.RoverEditorTestLibrary.get_current_animation_state_name(
                anim_instance, 0
            )
        )
        if current_state != "Grounded":
            return

        if state["delayed_landing_anim_exit_delay"] is None:
            exit_delay = world_time() - state["delayed_landing_input_world_time"]
            if not require(exit_delay <= 0.08, f"delayed landing animation exit={exit_delay:.3f}s"):
                return
            state["delayed_landing_anim_exit_delay"] = exit_delay
        speed = horizontal_speed(pawn.get_velocity())
        if speed < 80.0 and phase_elapsed() < 0.15:
            return
        if not require(speed >= 80.0, f"delayed landing resume speed={speed:.1f}"):
            return
        state["checks"].append(
            f"delayed_landing={state['delayed_landing_anim_exit_delay']:.3f}s/{speed:.0f}cmps"
        )
        finish(True, " ".join(state["checks"]))


def phase_status():
    pawn = state["pawn"]
    movement = state["movement"]
    if pawn is None or movement is None:
        return "pawn_not_ready"
    return (
        f"location={pawn.get_actor_location()} velocity={pawn.get_velocity()} "
        f"falling={movement.is_falling()}"
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
