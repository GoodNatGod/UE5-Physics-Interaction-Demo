import math
import time

import unreal


EXPECTED_PAWN = "/Script/RoverReplica.RoverCharacter"
TRAINING_ENEMY_TAG = "RoverP0TrainingEnemy"
RUN_TURNBACK_ASSET_PATH = "/Game/Rover/Animations/P0/Run_Turnback"
START_TIMEOUT_SECONDS = 30.0
STOP_TIMEOUT_SECONDS = 15.0
SUCCESS_MARKER = "ROVER_GROUND_TURN_PIE_OK"
FAILURE_MARKER = "ROVER_GROUND_TURN_PIE_FAIL"

RESET_TIMEOUT_SECONDS = 3.0
PREPARE_TIMEOUT_SECONDS = 7.0
NO_SPECIAL_OBSERVE_SECONDS = 0.4
RUN_TURNBACK_ACTIVE_TIMEOUT_SECONDS = 2.0
RUN_TURNBACK_COMPLETE_TIMEOUT_SECONDS = 4.0
RUN_TURNBACK_RECOVERY_TIMEOUT_SECONDS = 1.5
RUN_TURNBACK_MIN_INERTIA_CM = 3.0
RUN_TURNBACK_MAX_INERTIA_CM = 80.0
RUN_TURNBACK_MAX_LATERAL_CM = 25.0
RUN_TURNBACK_MAX_YAW_ERROR_DEGREES = 8.0
RUN_TURNBACK_MAX_YAW_REGRESSION_DEGREES = 20.0
RUN_TURNBACK_MAX_COMPLETION_STEP_DEGREES = 15.0
RUN_TURNBACK_MAX_ACCELERATION_TIME_SECONDS = 0.5
RUN_TURNBACK_MAX_PRE_RESUME_SPEED_CM_S = 5.0
RUN_TURNBACK_RESUME_WINDOW_TOLERANCE = 0.08

NO_SPECIAL_CASES = (
    {
        "name": "walk_180",
        "gait": "walk",
        "magnitude": 0.3,
        "sprint": False,
        "input_yaw": 180.0,
        "min_speed": 25.0,
        "allow_turn_in_place": True,
        "stop_before_trigger": False,
    },
    {
        "name": "run_180",
        "gait": "run",
        "magnitude": 1.0,
        "sprint": False,
        "input_yaw": 180.0,
        "min_speed": 300.0,
        "allow_turn_in_place": False,
        "stop_before_trigger": False,
    },
    {
        "name": "sprint_135",
        "gait": "sprint",
        "magnitude": 1.0,
        "sprint": True,
        "input_yaw": 135.0,
        "min_speed": 500.0,
        "allow_turn_in_place": False,
        "stop_before_trigger": False,
    },
    {
        "name": "sprint_180_below_speed",
        "gait": "sprint",
        "magnitude": 1.0,
        "sprint": True,
        "input_yaw": 180.0,
        "min_speed": 500.0,
        "allow_turn_in_place": False,
        "stop_before_trigger": True,
    },
)

level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
unreal_editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
state = {
    "phase": "starting",
    "deadline": time.monotonic() + START_TIMEOUT_SECONDS,
    "phase_game_started": 0.0,
    "result": None,
    "pawn": None,
    "mesh": None,
    "movement": None,
    "locomotion": None,
    "test_origin": None,
    "next_test": None,
    "current_case": None,
    "case_index": -1,
    "baseline_move_stop_id": 0,
    "baseline_ground_turn_id": 0,
    "turnback": None,
    "cancel_turnback": None,
    "run_turnback_resume_normalized_time": 0.70,
    "run_turnback_recovery_initial_speed": 180.0,
    "checks": [],
}
tick_handle = None


def object_path(value):
    return value.get_path_name() if value else "None"


def enum_name(value):
    name = getattr(value, "name", None)
    if name:
        text = str(name)
    else:
        text = str(value).split(".")[-1]
        text = text.split(":", 1)[0].strip("<> ")
    return text.replace("_", "").lower()


def horizontal_speed(vector):
    return math.sqrt(vector.x * vector.x + vector.y * vector.y)


def copy_vector(vector):
    return unreal.Vector(vector.x, vector.y, vector.z)


def direction_from_yaw(yaw_degrees):
    radians = math.radians(yaw_degrees)
    return unreal.Vector(math.cos(radians), math.sin(radians), 0.0)


def yaw_delta(actual, expected):
    return (actual - expected + 180.0) % 360.0 - 180.0


def horizontal_motion_from(origin, location, forward_x, forward_y):
    delta_x = location.x - origin.x
    delta_y = location.y - origin.y
    forward = delta_x * forward_x + delta_y * forward_y
    lateral = -delta_x * forward_y + delta_y * forward_x
    distance = math.sqrt(delta_x * delta_x + delta_y * delta_y)
    return forward, lateral, distance


def movement_rotation_policy():
    movement = state["movement"]
    return (
        bool(movement.get_editor_property("orient_rotation_to_movement")),
        bool(movement.get_editor_property("use_controller_desired_rotation")),
    )


def current_anim_state():
    return str(
        unreal.RoverEditorTestLibrary.get_current_animation_state_name(
            state["mesh"].get_anim_instance(), 0
        )
    )


def current_anim_asset():
    return str(
        unreal.RoverEditorTestLibrary.get_current_relevant_animation_asset_name(
            state["mesh"].get_anim_instance(), 0
        )
    )


def set_actor_yaw(yaw):
    state["pawn"].set_actor_rotation(
        unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw), False
    )


def set_move(yaw_degrees, magnitude=1.0):
    state["locomotion"].set_move_input(
        unreal.Vector2D(0.0, magnitude),
        direction_from_yaw(yaw_degrees),
    )


def drive_move(yaw_degrees, magnitude=1.0, sprint_held=False):
    locomotion = state["locomotion"]
    locomotion.set_sprint_held(sprint_held)
    set_move(yaw_degrees, magnitude)
    if not locomotion.can_accept_movement_input():
        return

    processed = locomotion.get_move_input()
    scale = math.sqrt(processed.x**2 + processed.y**2)
    if scale > 0.0:
        state["pawn"].add_movement_input(direction_from_yaw(yaw_degrees), scale)


def clear_move():
    state["locomotion"].set_move_input(
        unreal.Vector2D(0.0, 0.0),
        unreal.Vector(0.0, 0.0, 0.0),
    )


def advance(phase, timeout_seconds=2.0):
    state["phase"] = phase
    state["phase_game_started"] = game_time()
    state["deadline"] = time.monotonic() + timeout_seconds


def game_time():
    world = unreal_editor.get_game_world()
    return unreal.GameplayStatics.get_time_seconds(world) if world is not None else 0.0


def phase_game_elapsed():
    return game_time() - state["phase_game_started"]


def fail(detail):
    finish(False, f"phase={state['phase']} {detail}")


def require(condition, detail):
    if not condition:
        fail(detail)
        return False
    return True


def reset_character():
    locomotion = state["locomotion"]
    state["movement"].stop_movement_immediately()
    locomotion.set_sprint_held(False)
    locomotion.set_crouch_held(False)
    clear_move()
    if state["test_origin"] is not None:
        state["pawn"].set_actor_location(state["test_origin"], False, True)
    set_actor_yaw(0.0)


def movement_is_ready(expected_gait, minimum_speed):
    locomotion = state["locomotion"]
    pawn = state["pawn"]
    velocity = pawn.get_velocity()
    speed = horizontal_speed(velocity)
    if speed <= 1.0:
        return False

    return (
        phase_game_elapsed() >= 0.4
        and enum_name(locomotion.get_gait()) == expected_gait
        and speed >= minimum_speed
        and velocity.x / speed >= 0.95
        and pawn.get_actor_forward_vector().x >= 0.90
        and current_anim_state() == "Grounded"
        and not locomotion.is_move_stop_pending()
        and not locomotion.is_move_stop_active()
        and not locomotion.is_ground_turn_pending()
        and not locomotion.is_ground_turn_active()
    )


def check_no_special_request(case):
    locomotion = state["locomotion"]
    label = case["name"]
    if not require(
        locomotion.get_move_stop_request_id() == state["baseline_move_stop_id"],
        f"{label} changed MoveStop request id",
    ):
        return False
    if not require(
        not locomotion.is_move_stop_pending()
        and not locomotion.is_move_stop_active(),
        f"{label} created MoveStop pending/active",
    ):
        return False
    ground_turn_type = enum_name(locomotion.get_ground_turn_type())
    if case["allow_turn_in_place"]:
        if not require(
            ground_turn_type != "runturnback",
            f"{label} created RunTurnback",
        ):
            return False
    else:
        if not require(
            locomotion.get_ground_turn_request_id()
            == state["baseline_ground_turn_id"],
            f"{label} changed GroundTurn request id",
        ):
            return False
        if not require(
            not locomotion.is_ground_turn_pending()
            and not locomotion.is_ground_turn_active()
            and ground_turn_type == "none",
            f"{label} created GroundTurn type={locomotion.get_ground_turn_type()}",
        ):
            return False
    if not require(
        current_anim_state() not in ("MoveStop", "RunTurnback"),
        f"{label} entered state={current_anim_state()}",
    ):
        return False
    if not require(
        current_anim_asset() != "Run_Turnback",
        f"{label} used Run_Turnback animation asset",
    ):
        return False
    return True


def schedule_test_reset(next_test, index=None):
    state["next_test"] = (next_test, index)
    state["current_case"] = None
    reset_character()
    advance("movement_reset", RESET_TIMEOUT_SECONDS)


def run_movement_reset_phase():
    locomotion = state["locomotion"]
    reset_character()
    if (
        locomotion.is_move_stop_pending()
        or locomotion.is_move_stop_active()
        or locomotion.is_ground_turn_pending()
        or locomotion.is_ground_turn_active()
    ):
        return
    if phase_game_elapsed() < 0.2 or current_anim_state() != "Grounded":
        return

    next_test, index = state["next_test"]
    if next_test == "no_special":
        state["case_index"] = index
        state["current_case"] = NO_SPECIAL_CASES[index]
        advance("no_special_prepare", PREPARE_TIMEOUT_SECONDS)
    elif next_test == "run_turnback":
        state["turnback"] = None
        advance("run_turnback_prepare", PREPARE_TIMEOUT_SECONDS)
    elif next_test == "cancel_turnback":
        state["cancel_turnback"] = None
        advance("cancel_turnback_prepare", PREPARE_TIMEOUT_SECONDS)
    else:
        fail(f"unknown next test={next_test}")


def run_no_special_prepare_phase():
    case = state["current_case"]
    if not movement_is_ready(case["gait"], case["min_speed"]):
        drive_move(0.0, case["magnitude"], case["sprint"])
        return

    locomotion = state["locomotion"]
    if case["stop_before_trigger"]:
        state["movement"].stop_movement_immediately()
        if not require(
            enum_name(locomotion.get_gait()) == "sprint"
            and horizontal_speed(state["pawn"].get_velocity()) <= 1.0,
            f"{case['name']} did not preserve Sprint gait at low speed",
        ):
            return
    state["baseline_move_stop_id"] = locomotion.get_move_stop_request_id()
    state["baseline_ground_turn_id"] = locomotion.get_ground_turn_request_id()
    drive_move(case["input_yaw"], case["magnitude"], case["sprint"])
    if not check_no_special_request(case):
        return
    advance("no_special_observe", 1.0)


def run_no_special_observe_phase():
    case = state["current_case"]
    drive_move(case["input_yaw"], case["magnitude"], case["sprint"])
    if not check_no_special_request(case):
        return
    if phase_game_elapsed() < NO_SPECIAL_OBSERVE_SECONDS:
        return

    state["checks"].append(f"{case['name']}=no_RunTurnback")
    next_index = state["case_index"] + 1
    if next_index < len(NO_SPECIAL_CASES):
        schedule_test_reset("no_special", next_index)
    else:
        schedule_test_reset("run_turnback")


def check_run_turnback_request():
    locomotion = state["locomotion"]
    turnback = state["turnback"]
    if not require(
        locomotion.get_ground_turn_request_id() == turnback["request_id"],
        "RunTurnback request id changed while handling the turn",
    ):
        return False
    if not require(
        locomotion.get_move_stop_request_id() == turnback["move_stop_id"],
        "RunTurnback changed MoveStop request id",
    ):
        return False
    if not require(
        not locomotion.is_move_stop_pending()
        and not locomotion.is_move_stop_active(),
        "RunTurnback created MoveStop pending/active",
    ):
        return False
    if not require(
        enum_name(locomotion.get_ground_turn_type()) == "runturnback",
        f"RunTurnback has type={locomotion.get_ground_turn_type()}",
    ):
        return False
    return True


def observe_run_turnback_animation():
    turnback = state["turnback"]
    if current_anim_state() != "RunTurnback":
        return True

    turnback["state_seen"] = True
    asset = current_anim_asset()
    if asset.lower() in ("", "none"):
        return True

    if not require(asset == "Run_Turnback", f"RunTurnback used asset={asset}"):
        return False
    turnback["asset_seen"] = True
    return True


def check_run_turnback_rotation_policy_active():
    orient_to_movement, use_controller_rotation = movement_rotation_policy()
    if not require(
        not orient_to_movement,
        "RunTurnback left orient_rotation_to_movement enabled while active",
    ):
        return False
    if not require(
        not use_controller_rotation,
        "RunTurnback left use_controller_desired_rotation enabled while active",
    ):
        return False
    return True


def check_run_turnback_rotation_policy_restored():
    turnback = state["turnback"]
    actual = movement_rotation_policy()
    expected = turnback["entry_rotation_policy"]
    return require(
        actual == expected,
        "RunTurnback did not restore CharacterMovement rotation policy "
        f"actual={actual} expected={expected}",
    )


def sample_run_turnback_motion(active):
    turnback = state["turnback"]
    pawn = state["pawn"]
    location = pawn.get_actor_location()
    yaw = pawn.get_actor_rotation().yaw
    turnback["unwrapped_yaw"] += yaw_delta(yaw, turnback["last_yaw"])
    turnback["last_yaw"] = yaw
    # An exact 180-degree yaw can be represented as either +180 or -180 by
    # Unreal. For an immediate turnback only the accumulated magnitude matters.
    progress = abs(turnback["unwrapped_yaw"])
    forward, lateral, distance = horizontal_motion_from(
        turnback["start_location"],
        location,
        turnback["forward_x"],
        turnback["forward_y"],
    )
    velocity = pawn.get_velocity()
    anim_instance = state["mesh"].get_anim_instance()
    sample = {
        "time": game_time(),
        "yaw": yaw,
        "progress": progress,
        "forward": forward,
        "lateral": lateral,
        "distance": distance,
        "normalized_time": float(
            anim_instance.get_editor_property("run_turnback_normalized_time")
        ),
        "resume_window_open": bool(
            state["locomotion"].is_run_turnback_resume_window_open()
        ),
        "resume_movement_requested": bool(
            state["locomotion"].should_ground_turn_resume_movement()
        ),
        "reverse_speed": max(
            0.0,
            -(velocity.x * turnback["forward_x"]
              + velocity.y * turnback["forward_y"]),
        ),
    }
    turnback["samples"].append(sample)
    if active:
        turnback["active_samples"].append(sample)
    return sample


def validate_run_turnback_motion():
    turnback = state["turnback"]
    active_samples = turnback["active_samples"]
    final_sample = sample_run_turnback_motion(False)
    if not require(
        len(active_samples) >= 2,
        f"RunTurnback produced only {len(active_samples)} active motion samples",
    ):
        return False

    resume_threshold = turnback["resume_normalized_time"]
    pre_resume_samples = [
        sample
        for sample in active_samples
        if sample["normalized_time"]
        < resume_threshold - RUN_TURNBACK_RESUME_WINDOW_TOLERANCE
    ]
    if not require(
        bool(pre_resume_samples),
        "RunTurnback produced no samples before its configured resume window",
    ):
        return False
    maximum_pre_resume_speed = max(
        sample["reverse_speed"] for sample in pre_resume_samples
    )
    if not require(
        maximum_pre_resume_speed <= RUN_TURNBACK_MAX_PRE_RESUME_SPEED_CM_S,
        "RunTurnback moved in the new direction before the configured window "
        f"speed={maximum_pre_resume_speed:.1f}cm/s "
        f"threshold={resume_threshold:.2f}",
    ):
        return False
    exit_normalized_time = max(
        sample["normalized_time"] for sample in active_samples
    )
    if not require(
        resume_threshold - RUN_TURNBACK_RESUME_WINDOW_TOLERANCE
        <= exit_normalized_time
        <= resume_threshold + RUN_TURNBACK_RESUME_WINDOW_TOLERANCE,
        "RunTurnback exited at the wrong animation progress "
        f"last_active={exit_normalized_time:.3f} expected={resume_threshold:.3f}",
    ):
        return False

    active_progress = [sample["progress"] for sample in active_samples]
    first_active_progress = active_progress[0]
    if not require(
        160.0 <= first_active_progress <= 200.0,
        f"RunTurnback did not reverse immediately; first active yaw={first_active_progress:.1f}",
    ):
        return False

    maximum_active_regression = max(
        0.0,
        max(first_active_progress - progress for progress in active_progress),
    )
    if not require(
        maximum_active_regression <= RUN_TURNBACK_MAX_YAW_REGRESSION_DEGREES,
        f"RunTurnback active yaw regressed by {maximum_active_regression:.1f} degrees",
    ):
        return False

    progress_sequence = active_progress + [final_sample["progress"]]
    high_water = 0.0
    maximum_regression = 0.0
    for progress in progress_sequence:
        maximum_regression = max(maximum_regression, high_water - progress)
        high_water = max(high_water, progress)
    if not require(
        maximum_regression <= RUN_TURNBACK_MAX_YAW_REGRESSION_DEGREES,
        f"RunTurnback yaw regressed by {maximum_regression:.1f} degrees",
    ):
        return False

    completion_step = abs(final_sample["progress"] - active_progress[-1])
    if not require(
        completion_step <= RUN_TURNBACK_MAX_COMPLETION_STEP_DEGREES,
        f"RunTurnback snapped {completion_step:.1f} degrees on completion",
    ):
        return False
    if not require(
        160.0 <= final_sample["progress"] <= 200.0,
        f"RunTurnback accumulated yaw={final_sample['progress']:.1f}, expected about 180",
    ):
        return False
    final_yaw_error = abs(yaw_delta(final_sample["yaw"], turnback["target_yaw"]))
    if not require(
        final_yaw_error <= RUN_TURNBACK_MAX_YAW_ERROR_DEGREES,
        f"RunTurnback ended {final_yaw_error:.1f} degrees from its 180-degree target",
    ):
        return False

    maximum_forward = max(sample["forward"] for sample in turnback["samples"])
    maximum_lateral = max(abs(sample["lateral"]) for sample in turnback["samples"])
    if not require(
        RUN_TURNBACK_MIN_INERTIA_CM
        <= maximum_forward
        <= RUN_TURNBACK_MAX_INERTIA_CM,
        f"RunTurnback forward inertia={maximum_forward:.1f}cm, expected 3-80cm",
    ):
        return False
    minimum_exit_speed = max(
        1.0,
        state["run_turnback_recovery_initial_speed"] * 0.75,
    )
    if not require(
        final_sample["reverse_speed"] >= minimum_exit_speed,
        "RunTurnback did not release movement with its configured recovery speed "
        f"speed={final_sample['reverse_speed']:.1f} minimum={minimum_exit_speed:.1f}",
    ):
        return False
    if not require(
        maximum_lateral <= RUN_TURNBACK_MAX_LATERAL_CM,
        f"RunTurnback lateral drift reached {maximum_lateral:.1f}cm",
    ):
        return False

    turnback["final_progress"] = final_sample["progress"]
    turnback["forward_inertia"] = maximum_forward
    turnback["maximum_lateral"] = maximum_lateral
    turnback["maximum_pre_resume_speed"] = maximum_pre_resume_speed
    turnback["exit_normalized_time"] = exit_normalized_time
    turnback["resume_started_at"] = final_sample["time"]
    return True


def run_run_turnback_prepare_phase():
    # Keep the final forward-driving callback separate from the reverse-input
    # callback so two opposing AddMovementInput calls cannot share a frame.
    drive_move(0.0, 1.0, True)
    if not movement_is_ready("sprint", 500.0):
        return
    advance("run_turnback_trigger", 1.0)


def run_run_turnback_trigger_phase():
    pawn = state["pawn"]
    velocity = pawn.get_velocity()
    entry_speed = horizontal_speed(velocity)
    if not require(entry_speed >= 500.0, f"Sprint speed decayed before trigger: {entry_speed:.1f}"):
        return

    locomotion = state["locomotion"]
    move_stop_id = locomotion.get_move_stop_request_id()
    ground_turn_id = locomotion.get_ground_turn_request_id()
    start_location = copy_vector(pawn.get_actor_location())
    start_yaw = pawn.get_actor_rotation().yaw
    forward_x = velocity.x / entry_speed
    forward_y = velocity.y / entry_speed
    entry_rotation_policy = movement_rotation_policy()
    drive_move(180.0, 1.0, True)
    request_id = locomotion.get_ground_turn_request_id()
    if not require(request_id != ground_turn_id, "Sprint 180 did not advance GroundTurn request id"):
        return
    if not require(locomotion.is_ground_turn_pending(), "Sprint 180 did not create a pending GroundTurn"):
        return
    if not require(
        enum_name(locomotion.get_ground_turn_type()) == "runturnback",
        f"Sprint 180 requested type={locomotion.get_ground_turn_type()}",
    ):
        return

    state["turnback"] = {
        "request_id": request_id,
        "move_stop_id": move_stop_id,
        "entry_speed": entry_speed,
        "start_location": start_location,
        "start_yaw": start_yaw,
        "last_yaw": start_yaw,
        "unwrapped_yaw": 0.0,
        "turn_sign": 1.0 if locomotion.does_ground_turn_right() else -1.0,
        "target_yaw": start_yaw
        + (180.0 if locomotion.does_ground_turn_right() else -180.0),
        "forward_x": forward_x,
        "forward_y": forward_y,
        "entry_rotation_policy": entry_rotation_policy,
        "resume_normalized_time": state[
            "run_turnback_resume_normalized_time"
        ],
        "samples": [],
        "active_samples": [],
        "active_seen": False,
        "state_seen": False,
        "asset_seen": False,
    }
    if not check_run_turnback_request():
        return
    advance("run_turnback_wait_active", RUN_TURNBACK_ACTIVE_TIMEOUT_SECONDS)


def run_run_turnback_wait_active_phase():
    locomotion = state["locomotion"]
    drive_move(180.0, 1.0, True)
    if not check_run_turnback_request():
        return
    if locomotion.is_ground_turn_active():
        state["turnback"]["active_seen"] = True
        if not check_run_turnback_rotation_policy_active():
            return
        if not observe_run_turnback_animation():
            return
        sample_run_turnback_motion(True)
        advance(
            "run_turnback_wait_complete", RUN_TURNBACK_COMPLETE_TIMEOUT_SECONDS
        )
    elif not locomotion.is_ground_turn_pending():
        fail("Sprint 180 request cleared before RunTurnback became active")


def run_run_turnback_wait_complete_phase():
    locomotion = state["locomotion"]
    drive_move(180.0, 1.0, True)
    if locomotion.is_ground_turn_pending() or locomotion.is_ground_turn_active():
        if not check_run_turnback_request():
            return
        if locomotion.is_ground_turn_active():
            if not check_run_turnback_rotation_policy_active():
                return
            if not observe_run_turnback_animation():
                return
            sample_run_turnback_motion(True)
        return

    turnback = state["turnback"]
    if not require(turnback["active_seen"], "RunTurnback never became active"):
        return
    if not require(turnback["state_seen"], "AnimBP never entered RunTurnback"):
        return
    if not require(
        turnback["asset_seen"],
        "AnimBP never exposed the Run_Turnback animation asset",
    ):
        return
    if not check_run_turnback_rotation_policy_restored():
        return
    anim_instance = state["mesh"].get_anim_instance()
    if not require(
        bool(anim_instance.get_editor_property("should_move"))
        and bool(anim_instance.get_editor_property("is_sprinting")),
        "RunTurnback transition targeted Idle instead of the resumed sprint pose",
    ):
        return
    if not validate_run_turnback_motion():
        return
    advance("run_turnback_recovery", RUN_TURNBACK_RECOVERY_TIMEOUT_SECONDS)


def run_run_turnback_recovery_phase():
    locomotion = state["locomotion"]
    pawn = state["pawn"]
    turnback = state["turnback"]
    drive_move(180.0, 1.0, True)

    if not require(
        locomotion.get_move_stop_request_id() == turnback["move_stop_id"],
        "RunTurnback recovery changed MoveStop request id",
    ):
        return
    if not require(
        locomotion.get_ground_turn_request_id() == turnback["request_id"],
        "RunTurnback recovery created another GroundTurn request",
    ):
        return
    if not require(
        not locomotion.is_move_stop_pending()
        and not locomotion.is_move_stop_active()
        and not locomotion.is_ground_turn_pending()
        and not locomotion.is_ground_turn_active(),
        "RunTurnback recovery left a movement transition active",
    ):
        return

    velocity = pawn.get_velocity()
    speed = horizontal_speed(velocity)
    minimum_recovery_speed = max(350.0, turnback["entry_speed"] * 0.65)
    recovered = (
        locomotion.can_accept_movement_input()
        and enum_name(locomotion.get_gait()) == "sprint"
        and speed >= minimum_recovery_speed
        and velocity.x / max(speed, 1.0) <= -0.90
        and abs(yaw_delta(pawn.get_actor_rotation().yaw, 180.0)) <= 6.0
        and current_anim_state() == "Grounded"
    )
    if not recovered:
        return

    acceleration_time = game_time() - turnback["resume_started_at"]
    if not require(
        acceleration_time <= RUN_TURNBACK_MAX_ACCELERATION_TIME_SECONDS,
        "RunTurnback accelerated too slowly after its configured exit point "
        f"time={acceleration_time:.3f}s limit={RUN_TURNBACK_MAX_ACCELERATION_TIME_SECONDS:.3f}s",
    ):
        return
    recovery_sample = sample_run_turnback_motion(False)
    if not require(
        recovery_sample["forward"] <= turnback["forward_inertia"] - 20.0,
        "RunTurnback did not accelerate away from its old travel direction "
        f"peak={turnback['forward_inertia']:.1f}cm "
        f"recovery={recovery_sample['forward']:.1f}cm",
    ):
        return
    turnback["acceleration_time"] = acceleration_time

    state["checks"].append(
        "sprint_180=Run_Turnback/"
        f"yaw_{turnback['final_progress']:.0f}/"
        f"inertia_{turnback['forward_inertia']:.1f}cm/"
        f"gate_{turnback['exit_normalized_time']:.2f}/"
        f"pre_new_{turnback['maximum_pre_resume_speed']:.1f}cmps/"
        f"accel_{turnback['acceleration_time']:.2f}s/"
        f"lateral_{turnback['maximum_lateral']:.1f}cm/"
        f"recovered_{speed:.0f}"
    )
    schedule_test_reset("cancel_turnback")


def run_cancel_turnback_prepare_phase():
    drive_move(0.0, 1.0, True)
    if not movement_is_ready("sprint", 500.0):
        return
    advance("cancel_turnback_trigger", 1.0)


def run_cancel_turnback_trigger_phase():
    if not require(
        horizontal_speed(state["pawn"].get_velocity()) >= 500.0,
        "Sprint speed decayed before cancel trigger",
    ):
        return

    locomotion = state["locomotion"]
    move_stop_id = locomotion.get_move_stop_request_id()
    ground_turn_id = locomotion.get_ground_turn_request_id()
    locomotion.set_sprint_held(True)
    set_move(180.0, 1.0)
    request_id = locomotion.get_ground_turn_request_id()
    if not require(request_id != ground_turn_id, "cancel setup did not create RunTurnback"):
        return
    if not require(
        locomotion.is_ground_turn_pending()
        and enum_name(locomotion.get_ground_turn_type()) == "runturnback",
        "cancel setup did not leave RunTurnback pending",
    ):
        return

    # Both calls stay in one Slate callback, before AnimBP can acknowledge the request.
    locomotion.set_sprint_held(False)
    state["cancel_turnback"] = {
        "request_id": request_id,
        "move_stop_id": move_stop_id,
    }
    if not require(
        not locomotion.is_ground_turn_pending()
        and not locomotion.is_ground_turn_active()
        and enum_name(locomotion.get_ground_turn_type()) == "none",
        "releasing Sprint did not cancel pending RunTurnback",
    ):
        return
    if not require(
        locomotion.get_move_stop_request_id() == move_stop_id,
        "canceling RunTurnback changed MoveStop request id",
    ):
        return
    if not require(
        locomotion.can_accept_movement_input(),
        "canceling pending RunTurnback did not release movement input",
    ):
        return
    advance("cancel_turnback_observe", 1.0)


def run_cancel_turnback_observe_phase():
    locomotion = state["locomotion"]
    canceled = state["cancel_turnback"]
    drive_move(180.0, 1.0, False)
    if not require(
        locomotion.get_ground_turn_request_id() == canceled["request_id"],
        "canceled RunTurnback was replaced by another GroundTurn request",
    ):
        return
    if not require(
        locomotion.get_move_stop_request_id() == canceled["move_stop_id"],
        "canceled RunTurnback created a MoveStop request",
    ):
        return
    if not require(
        not locomotion.is_ground_turn_pending()
        and not locomotion.is_ground_turn_active()
        and not locomotion.is_move_stop_pending()
        and not locomotion.is_move_stop_active()
        and enum_name(locomotion.get_ground_turn_type()) == "none"
        and locomotion.can_accept_movement_input(),
        "canceled RunTurnback relocked movement input",
    ):
        return
    if not require(
        current_anim_state() != "RunTurnback"
        and current_anim_asset() != "Run_Turnback",
        "AnimBP used RunTurnback after Sprint was released",
    ):
        return
    if phase_game_elapsed() < NO_SPECIAL_OBSERVE_SECONDS:
        return

    state["checks"].append("sprint_release=pending_canceled")
    finish(True, " ".join(state["checks"]))


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

    if state["mesh"] is not None:
        state["mesh"].set_component_tick_enabled(True)
    if state["locomotion"] is not None and state["movement"] is not None:
        reset_character()

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
    turnback_asset = unreal.load_asset(RUN_TURNBACK_ASSET_PATH)
    if not require(
        isinstance(turnback_asset, unreal.AnimSequence),
        f"missing RunTurnback AnimSequence={RUN_TURNBACK_ASSET_PATH}",
    ):
        return
    if not require(
        not bool(turnback_asset.get_editor_property("enable_root_motion")),
        "Run_Turnback must keep enable_root_motion disabled",
    ):
        return

    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    if pawn is None:
        return
    if object_path(pawn.get_class()) != EXPECTED_PAWN:
        fail(f"unexpected pawn={object_path(pawn.get_class())}")
        return

    locomotion = pawn.get_locomotion_component()
    mesh = pawn.get_component_by_class(unreal.SkeletalMeshComponent)
    movement = pawn.get_component_by_class(unreal.CharacterMovementComponent)
    if not require(locomotion is not None, "missing locomotion component"):
        return
    if not require(mesh is not None, "missing skeletal mesh component"):
        return
    if not require(movement is not None, "missing character movement component"):
        return
    if not require(mesh.get_anim_instance() is not None, "missing animation instance"):
        return
    if not movement.is_moving_on_ground():
        return

    movement_config = locomotion.get_editor_property("movement_config")
    if not require(movement_config is not None, "missing movement config"):
        return
    movement_settings = movement_config.get_editor_property("settings")
    resume_normalized_time = float(
        movement_settings.get_editor_property(
            "run_turnback_resume_normalized_time"
        )
    )
    recovery_initial_speed = float(
        movement_settings.get_editor_property(
            "run_turnback_recovery_initial_speed"
        )
    )
    if not require(
        0.1 <= resume_normalized_time <= 1.0,
        f"invalid RunTurnback resume normalized time={resume_normalized_time:.3f}",
    ):
        return

    state["pawn"] = pawn
    state["mesh"] = mesh
    state["movement"] = movement
    state["locomotion"] = locomotion
    state["run_turnback_resume_normalized_time"] = resume_normalized_time
    state["run_turnback_recovery_initial_speed"] = recovery_initial_speed
    state["test_origin"] = copy_vector(pawn.get_actor_location())
    reset_character()
    advance("exact_180", 2.0)


def run_phase():
    phase = state["phase"]
    locomotion = state["locomotion"]
    movement = state["movement"]

    if phase == "movement_reset":
        run_movement_reset_phase()
        return

    if phase == "no_special_prepare":
        run_no_special_prepare_phase()
        return

    if phase == "no_special_observe":
        run_no_special_observe_phase()
        return

    if phase == "run_turnback_prepare":
        run_run_turnback_prepare_phase()
        return

    if phase == "run_turnback_trigger":
        run_run_turnback_trigger_phase()
        return

    if phase == "run_turnback_wait_active":
        run_run_turnback_wait_active_phase()
        return

    if phase == "run_turnback_wait_complete":
        run_run_turnback_wait_complete_phase()
        return

    if phase == "run_turnback_recovery":
        run_run_turnback_recovery_phase()
        return

    if phase == "cancel_turnback_prepare":
        run_cancel_turnback_prepare_phase()
        return

    if phase == "cancel_turnback_trigger":
        run_cancel_turnback_trigger_phase()
        return

    if phase == "cancel_turnback_observe":
        run_cancel_turnback_observe_phase()
        return

    if phase == "exact_180":
        before = locomotion.get_ground_turn_request_id()
        set_move(180.0, 0.3)
        if not require(locomotion.is_ground_turn_pending(), "exact 180 did not request a turn"):
            return
        if not require(locomotion.does_ground_turn_right(), "exact 180 did not deterministically choose right"):
            return
        if not require(locomotion.get_ground_turn_request_id() != before, "exact 180 did not advance request id"):
            return
        clear_move()
        state["checks"].append("exact180=right")
        advance("pending_watchdog_request", 2.0)
        return

    if phase == "pending_watchdog_request":
        state["mesh"].set_component_tick_enabled(False)
        set_move(100.0, 0.3)
        if not require(locomotion.is_ground_turn_pending(), "watchdog setup did not create a pending turn"):
            return
        advance("pending_watchdog_wait", 2.0)
        return

    if phase == "pending_watchdog_wait":
        if not locomotion.is_ground_turn_pending():
            state["mesh"].set_component_tick_enabled(True)
            clear_move()
            if not require(locomotion.can_accept_movement_input(), "pending watchdog did not release movement input"):
                return
            state["checks"].append("pending_watchdog=unlocked")
            advance("post_watchdog_settle", 2.0)
        return

    if phase == "post_watchdog_settle":
        clear_move()
        if current_anim_state() != "Grounded" or phase_game_elapsed() < 0.2:
            return
        advance("seam_request", 2.0)
        return

    if phase == "seam_request":
        reset_character()
        set_move(-179.5, 0.3)
        request_id = locomotion.get_ground_turn_request_id()
        if not require(locomotion.is_ground_turn_pending(), "-179.5 did not request turn in place"):
            return
        if not require(not locomotion.does_ground_turn_right(), "-179.5 did not choose left"):
            return
        set_move(179.5, 0.3)
        if not require(locomotion.is_ground_turn_pending(), "rear seam crossing canceled the pending turn"):
            return
        if not require(locomotion.get_ground_turn_request_id() == request_id, "rear seam crossing replaced the request"):
            return
        if not require(not locomotion.does_ground_turn_right(), "rear seam crossing changed the latched direction"):
            return
        advance("seam_wait_active", 2.0)
        return

    if phase == "seam_wait_active":
        if locomotion.is_ground_turn_active():
            advance("seam_wait_complete", 3.0)
        elif not locomotion.is_ground_turn_pending():
            fail("rear-seam request cleared before the animation state entered")
        return

    if phase == "seam_wait_complete":
        if not locomotion.is_ground_turn_pending() and not locomotion.is_ground_turn_active():
            actual_yaw = state["pawn"].get_actor_rotation().yaw
            if not require(abs(yaw_delta(actual_yaw, -90.0)) <= 5.0, f"seam turn completed at yaw={actual_yaw:.2f}"):
                return
            state["checks"].append("rear_seam=left")
            reset_character()
            set_move(100.0, 0.3)
            if not require(locomotion.is_ground_turn_pending(), "active watchdog setup did not request turn in place"):
                return
            advance("active_watchdog_wait_active", 2.0)
        return

    if phase == "active_watchdog_wait_active":
        if locomotion.is_ground_turn_active():
            state["mesh"].set_component_tick_enabled(False)
            advance("active_watchdog_wait_complete", 4.0)
        elif not locomotion.is_ground_turn_pending():
            fail("active-watchdog request cleared before the animation state entered")
        return

    if phase == "active_watchdog_wait_complete":
        if not locomotion.is_ground_turn_pending() and not locomotion.is_ground_turn_active():
            state["mesh"].set_component_tick_enabled(True)
            clear_move()
            if not require(locomotion.can_accept_movement_input(), "active watchdog did not release movement input"):
                return
            actual_yaw = state["pawn"].get_actor_rotation().yaw
            if not require(abs(yaw_delta(actual_yaw, 90.0)) <= 5.0, f"active watchdog completed at yaw={actual_yaw:.2f}"):
                return
            state["checks"].append("active_watchdog=completed")
            advance("active_watchdog_settle", 3.0)
        return

    if phase == "active_watchdog_settle":
        clear_move()
        if phase_game_elapsed() < 0.2:
            return
        if not require(
            not locomotion.is_ground_turn_pending()
            and not locomotion.is_ground_turn_active()
            and locomotion.can_accept_movement_input(),
            "ground turn relocked while the AnimBP recovered from the watchdog",
        ):
            return
        if current_anim_state() != "Grounded":
            return
        state["checks"].append("active_watchdog_anim=grounded")
        schedule_test_reset("no_special", 0)


def phase_status():
    locomotion = state["locomotion"]
    if locomotion is None or state["pawn"] is None:
        return "pawn_not_ready"
    return " ".join(
        (
            f"state={current_anim_state()}",
            f"asset={current_anim_asset()}",
            f"gait={locomotion.get_gait()}",
            f"speed={horizontal_speed(state['pawn'].get_velocity()):.2f}",
            f"ground_turn={locomotion.is_ground_turn_pending()}/{locomotion.is_ground_turn_active()}/{enum_name(locomotion.get_ground_turn_type())}",
            f"move_stop={locomotion.is_move_stop_pending()}/{locomotion.is_move_stop_active()}",
        )
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
