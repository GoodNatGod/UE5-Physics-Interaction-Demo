import math
import time

import unreal


EXPECTED_PAWN = "/Script/RoverReplica.RoverCharacter"
TRAINING_ENEMY_TAG = "RoverP0TrainingEnemy"
START_TIMEOUT_SECONDS = 30.0
STOP_TIMEOUT_SECONDS = 15.0
SUCCESS_MARKER = "ROVER_MOVE_STOP_PIE_OK"
FAILURE_MARKER = "ROVER_MOVE_STOP_PIE_FAIL"

RESET_TIMEOUT_SECONDS = 2.0
PREPARE_TIMEOUT_SECONDS = 7.0
NO_TRIGGER_OBSERVE_SECONDS = 0.4
WAIT_ACTIVE_TIMEOUT_SECONDS = 1.5
WAIT_COMPLETE_TIMEOUT_SECONDS = 1.4
RESUME_TIMEOUT_SECONDS = 0.75
RESUME_GUARD_SECONDS = 0.5
IDLE_READY_TIMEOUT_SECONDS = 0.45
IDLE_GUARD_SECONDS = 0.3
MOVE_STOP_RESUME_NORMALIZED_TIME = 0.52
MOVE_STOP_ACTIVE_TIME_TOLERANCE_SECONDS = 0.18
MAX_MOVE_STOP_ACTIVE_SECONDS = 1.25
MAX_RESUME_READY_SECONDS = 0.45
MAX_WALK_RESUME_INPUT_TO_READY_SECONDS = 0.45
TAIL_LOOKBACK_SECONDS = 0.25

MAX_CAPSULE_LATERAL_CM = 15.0
MAX_BIP001_DRIFT_CM = 20.0
MAX_BIP001_END_OFFSET_CM = 12.0
MAX_CAPSULE_BACKSTEP_CM = 3.0
MAX_CAPSULE_TAIL_REBOUND_CM = 3.0
MAX_VISUAL_BACKSTEP_CM = 8.0
MAX_VISUAL_TAIL_REBOUND_CM = 8.0
MAX_TAIL_PLANAR_STEP_CM = 12.0
MIN_STOP_SAMPLES = 12
MIN_TAIL_SAMPLES = 4

REVERSE_CASES = (
    {
        "name": "walk_reverse",
        "gait": "walk",
        "sprint": False,
        "magnitude": 0.3,
        "min_speed": 25.0,
        "expected_ground_turn": None,
    },
    {
        "name": "run_reverse",
        "gait": "run",
        "sprint": False,
        "magnitude": 1.0,
        "min_speed": 300.0,
        "expected_ground_turn": None,
    },
    {
        "name": "sprint_reverse_run_turnback",
        "gait": "sprint",
        "sprint": True,
        "magnitude": 1.0,
        "min_speed": 500.0,
        "expected_ground_turn": "run_turnback",
    },
)

MOVE_STOP_CASES = (
    {
        "name": "walk_L",
        "gait": "walk",
        "sprint": False,
        "magnitude": 0.2,
        "min_entry_speed": 8.0,
        "trigger": "release",
        "resume_direction": "forward",
        "resume_during_stop": True,
        "expected_left_variant": True,
        "asset_duration": 1.5,
        "resume_normalized_time": 0.20,
        "ground_asset": "BS_Rover_Walk",
        "travel_bounds": (8.0, 60.0),
        "resume_speed": 15.0,
        "resume_distance": 3.0,
        "guard_speed": 12.0,
        "guard_progress": 8.0,
    },
    {
        "name": "walk_R",
        "gait": "walk",
        "sprint": False,
        "magnitude": 0.2,
        "min_entry_speed": 8.0,
        "trigger": "release",
        "resume_direction": "forward",
        "resume_during_stop": True,
        "expected_left_variant": False,
        "asset_duration": 1.5,
        "resume_normalized_time": 0.20,
        "ground_asset": "BS_Rover_Walk",
        "travel_bounds": (8.0, 60.0),
        "resume_speed": 15.0,
        "resume_distance": 3.0,
        "guard_speed": 12.0,
        "guard_progress": 8.0,
    },
    {
        "name": "walk_release_idle_L",
        "gait": "walk",
        "sprint": False,
        "magnitude": 0.2,
        "min_entry_speed": 8.0,
        "trigger": "release",
        "resume_direction": None,
        "resume_during_stop": False,
        "expected_left_variant": True,
        "asset_duration": 1.5,
        "resume_normalized_time": 0.92,
        "active_time_bounds": (1.25, 1.60),
        "wait_complete_timeout": 1.8,
        "ground_asset": "Stand1",
        "travel_bounds": (20.0, 80.0),
    },
    {
        "name": "walk_release_idle_R",
        "gait": "walk",
        "sprint": False,
        "magnitude": 0.2,
        "min_entry_speed": 8.0,
        "trigger": "release",
        "resume_direction": None,
        "resume_during_stop": False,
        "expected_left_variant": False,
        "asset_duration": 1.5,
        "resume_normalized_time": 0.92,
        "active_time_bounds": (1.25, 1.60),
        "wait_complete_timeout": 1.8,
        "ground_asset": "Stand1",
        "travel_bounds": (20.0, 80.0),
    },
    {
        "name": "run_release_resume_L",
        "gait": "run",
        "sprint": False,
        "magnitude": 1.0,
        "min_entry_speed": 300.0,
        "trigger": "release",
        "resume_direction": "forward",
        "resume_during_stop": True,
        "expected_left_variant": True,
        "asset_duration": 1.333333,
        "resume_normalized_time": MOVE_STOP_RESUME_NORMALIZED_TIME,
        "ground_asset": "BS_Rover_Run",
        "travel_bounds": (40.0, 100.0),
        "resume_speed": 150.0,
        "resume_distance": 20.0,
        "guard_speed": 180.0,
        "guard_progress": 55.0,
    },
    {
        "name": "run_release_resume_R",
        "gait": "run",
        "sprint": False,
        "magnitude": 1.0,
        "min_entry_speed": 300.0,
        "trigger": "release",
        "resume_direction": "forward",
        "resume_during_stop": True,
        "expected_left_variant": False,
        "asset_duration": 1.333333,
        "resume_normalized_time": MOVE_STOP_RESUME_NORMALIZED_TIME,
        "ground_asset": "BS_Rover_Run",
        "travel_bounds": (40.0, 100.0),
        "resume_speed": 150.0,
        "resume_distance": 20.0,
        "guard_speed": 180.0,
        "guard_progress": 55.0,
    },
    {
        "name": "run_release_idle_L",
        "gait": "run",
        "sprint": False,
        "magnitude": 1.0,
        "min_entry_speed": 300.0,
        "trigger": "release",
        "resume_direction": None,
        "resume_during_stop": False,
        "expected_left_variant": True,
        "asset_duration": 1.333333,
        "resume_normalized_time": 0.92,
        "active_time_bounds": (1.10, 1.35),
        "wait_complete_timeout": 1.7,
        "ground_asset": "Stand1",
        "travel_bounds": (50.0, 110.0),
    },
    {
        "name": "run_release_idle_R",
        "gait": "run",
        "sprint": False,
        "magnitude": 1.0,
        "min_entry_speed": 300.0,
        "trigger": "release",
        "resume_direction": None,
        "resume_during_stop": False,
        "expected_left_variant": False,
        "asset_duration": 1.333333,
        "resume_normalized_time": 0.92,
        "active_time_bounds": (1.10, 1.35),
        "wait_complete_timeout": 1.7,
        "ground_asset": "Stand1",
        "travel_bounds": (50.0, 110.0),
    },
    {
        "name": "sprint_release_resume_L",
        "gait": "sprint",
        "sprint": True,
        "magnitude": 1.0,
        "min_entry_speed": 500.0,
        "trigger": "release",
        "resume_direction": "forward",
        "resume_during_stop": True,
        "expected_left_variant": True,
        "asset_duration": 1.433333,
        "resume_normalized_time": MOVE_STOP_RESUME_NORMALIZED_TIME,
        "ground_asset": "Sprint_F",
        "travel_bounds": (135.0, 235.0),
        "resume_speed": 250.0,
        "resume_distance": 35.0,
        "guard_speed": 250.0,
        "guard_progress": 80.0,
    },
    {
        "name": "sprint_release_resume_R",
        "gait": "sprint",
        "sprint": True,
        "magnitude": 1.0,
        "min_entry_speed": 500.0,
        "trigger": "release",
        "resume_direction": "forward",
        "resume_during_stop": True,
        "expected_left_variant": False,
        "asset_duration": 1.433333,
        "resume_normalized_time": MOVE_STOP_RESUME_NORMALIZED_TIME,
        "ground_asset": "Sprint_F",
        "travel_bounds": (135.0, 235.0),
        "resume_speed": 250.0,
        "resume_distance": 35.0,
        "guard_speed": 250.0,
        "guard_progress": 80.0,
    },
    {
        "name": "sprint_release_idle_L",
        "gait": "sprint",
        "sprint": True,
        "magnitude": 1.0,
        "min_entry_speed": 500.0,
        "trigger": "release",
        "resume_direction": None,
        "resume_during_stop": False,
        "expected_left_variant": True,
        "asset_duration": 1.433333,
        "resume_normalized_time": 0.92,
        "active_time_bounds": (1.18, 1.45),
        "wait_complete_timeout": 1.8,
        "ground_asset": "Stand1",
        "travel_bounds": (135.0, 260.0),
    },
    {
        "name": "sprint_release_idle_R",
        "gait": "sprint",
        "sprint": True,
        "magnitude": 1.0,
        "min_entry_speed": 500.0,
        "trigger": "release",
        "resume_direction": None,
        "resume_during_stop": False,
        "expected_left_variant": False,
        "asset_duration": 1.433333,
        "resume_normalized_time": 0.92,
        "active_time_bounds": (1.18, 1.45),
        "wait_complete_timeout": 1.8,
        "ground_asset": "Stand1",
        "travel_bounds": (135.0, 260.0),
    },
)

if len(MOVE_STOP_CASES) != 12 or any(
    case["trigger"] != "release" for case in MOVE_STOP_CASES
):
    raise RuntimeError("MoveStop PIE matrix must contain exactly 12 release-only cases")


level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
unreal_editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
state = {
    "phase": "starting",
    "deadline": time.monotonic() + START_TIMEOUT_SECONDS,
    "phase_started": time.monotonic(),
    "phase_game_started": 0.0,
    "result": None,
    "checks": [],
    "next_action": None,
    "current_case": None,
    "current_stop": None,
}
tick_handle = None


def object_path(value):
    return value.get_path_name() if value else "None"


def enum_name(value):
    text = str(value).lower()
    for candidate in ("idle", "walk", "run", "sprint"):
        if candidate in text:
            return candidate
    return text


def ground_turn_name(value):
    normalized = "".join(character for character in str(value).lower() if character.isalnum())
    if "runturnback" in normalized:
        return "run_turnback"
    if "turninplace" in normalized:
        return "turn_in_place"
    if "none" in normalized:
        return "none"
    return normalized


def horizontal_length(value):
    return math.sqrt(value.x**2 + value.y**2)


def horizontal_distance(a, b):
    return math.sqrt((a.x - b.x) ** 2 + (a.y - b.y) ** 2)


def copy_vector(value):
    return unreal.Vector(value.x, value.y, value.z)


def game_time():
    world = state.get("world")
    return unreal.GameplayStatics.get_time_seconds(world) if world is not None else 0.0


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


def set_move(raw_x, raw_y, world_x, world_y):
    state["locomotion"].set_move_input(
        unreal.Vector2D(raw_x, raw_y),
        unreal.Vector(world_x, world_y, 0.0),
    )


def drive_move(raw_x, raw_y, world_x, world_y, sprint_held):
    locomotion = state["locomotion"]
    locomotion.set_sprint_held(sprint_held)
    set_move(raw_x, raw_y, world_x, world_y)
    if not locomotion.can_accept_movement_input():
        return

    processed = locomotion.get_move_input()
    scale = math.sqrt(processed.x**2 + processed.y**2)
    if scale > 0.0:
        state["pawn"].add_movement_input(
            unreal.Vector(world_x, world_y, 0.0), scale
        )


def drive_forward(magnitude, sprint_held):
    drive_move(0.0, magnitude, 1.0, 0.0, sprint_held)


def drive_reverse(case):
    drive_move(0.0, -case["magnitude"], -1.0, 0.0, case["sprint"])


def apply_move_stop_release():
    clear_move()


def drive_move_stop_input(case, resume_requested):
    stop = state.get("current_stop")
    if stop is not None and stop.get("active_release_applied", False):
        state["locomotion"].set_sprint_held(False)
        clear_move()
        return
    if resume_requested:
        drive_forward(case["magnitude"], case["sprint"])
    else:
        clear_move()


def clear_move():
    set_move(0.0, 0.0, 0.0, 0.0)


def set_actor_yaw(yaw):
    state["pawn"].set_actor_rotation(
        unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw), False
    )


def advance(phase, timeout_seconds=2.0):
    state["phase"] = phase
    state["phase_started"] = time.monotonic()
    state["phase_game_started"] = game_time()
    state["deadline"] = state["phase_started"] + timeout_seconds


def phase_game_elapsed():
    return game_time() - state["phase_game_started"]


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

    locomotion = state.get("locomotion")
    movement = state.get("movement")
    if movement is not None:
        movement.stop_movement_immediately()
    if locomotion is not None:
        locomotion.set_sprint_held(False)
        locomotion.set_move_input(
            unreal.Vector2D(0.0, 0.0), unreal.Vector(0.0, 0.0, 0.0)
        )

    state["result"] = (ok, detail)
    if level_editor.is_in_play_in_editor():
        state["phase"] = "stopping"
        state["deadline"] = time.monotonic() + STOP_TIMEOUT_SECONDS
        level_editor.editor_request_end_play()
    else:
        shutdown()


def expected_left_variant():
    pawn = state["pawn"]
    mesh = state["mesh"]
    velocity = pawn.get_velocity()
    speed = horizontal_length(velocity)
    if speed <= 1.0:
        return None

    travel_x = velocity.x / speed
    travel_y = velocity.y / speed
    origin = pawn.get_actor_location()
    left_foot = mesh.get_socket_location("Bip001LFoot")
    right_foot = mesh.get_socket_location("Bip001RFoot")
    left_projection = (
        (left_foot.x - origin.x) * travel_x
        + (left_foot.y - origin.y) * travel_y
    )
    right_projection = (
        (right_foot.x - origin.x) * travel_x
        + (right_foot.y - origin.y) * travel_y
    )
    if abs(left_projection - right_projection) <= 0.5:
        return None

    # _L starts right-foot-leading and uses the left foot as the main brake.
    return left_projection < right_projection


def movement_is_ready(expected_gait, minimum_speed):
    locomotion = state["locomotion"]
    pawn = state["pawn"]
    velocity = pawn.get_velocity()
    speed = horizontal_length(velocity)
    if speed <= 1.0:
        return False

    velocity_alignment = velocity.x / speed
    facing_alignment = pawn.get_actor_forward_vector().x
    return (
        phase_game_elapsed() >= 0.4
        and enum_name(locomotion.get_gait()) == expected_gait
        and speed >= minimum_speed
        and velocity_alignment >= 0.95
        and facing_alignment >= 0.90
        and "Grounded" in current_anim_state()
        and not locomotion.is_move_stop_pending()
        and not locomotion.is_move_stop_active()
        and not locomotion.is_ground_turn_pending()
        and not locomotion.is_ground_turn_active()
    )


def check_reverse_routing(case):
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
    anim_state = current_anim_state()
    if not require("MoveStop" not in anim_state, f"{label} entered state={anim_state}"):
        return False

    expected_ground_turn = case["expected_ground_turn"]
    if expected_ground_turn is None:
        if not require(
            locomotion.get_ground_turn_request_id() == state["baseline_ground_turn_id"],
            f"{label} changed GroundTurn request id",
        ):
            return False
        if not require(
            not locomotion.is_ground_turn_pending()
            and not locomotion.is_ground_turn_active(),
            f"{label} created GroundTurn pending/active",
        ):
            return False
        if not require(
            "RunTurnback" not in anim_state and "TurnInPlace" not in anim_state,
            f"{label} entered GroundTurn state={anim_state}",
        ):
            return False
        return True

    if not require(
        expected_ground_turn == "run_turnback",
        f"{label} has unsupported expected GroundTurn={expected_ground_turn}",
    ):
        return False
    if not require(
        state["routed_ground_turn_id"] != state["baseline_ground_turn_id"]
        and locomotion.get_ground_turn_request_id() == state["routed_ground_turn_id"],
        f"{label} did not retain exactly one GroundTurn request",
    ):
        return False

    ground_turn_in_progress = (
        locomotion.is_ground_turn_pending() or locomotion.is_ground_turn_active()
    )
    if ground_turn_in_progress and not require(
        ground_turn_name(locomotion.get_ground_turn_type()) == "run_turnback",
        f"{label} routed to GroundTurn={locomotion.get_ground_turn_type()}",
    ):
        return False
    if locomotion.is_ground_turn_active():
        state["turnback_active_seen"] = True
        if not require(
            "RunTurnback" in anim_state,
            f"{label} active GroundTurn used state={anim_state}",
        ):
            return False
    if not require("TurnInPlace" not in anim_state, f"{label} entered state={anim_state}"):
        return False
    if not ground_turn_in_progress and not require(
        state["turnback_active_seen"],
        f"{label} GroundTurn ended before RunTurnback became active",
    ):
        return False
    return True


def schedule_reset(next_action, index=None):
    state["next_action"] = (next_action, index)
    state["current_case"] = None
    state["current_stop"] = None
    state["movement"].stop_movement_immediately()
    state["locomotion"].set_sprint_held(False)
    clear_move()
    state["pawn"].set_actor_location(state["test_origin"], False, True)
    set_actor_yaw(0.0)
    advance("reset", RESET_TIMEOUT_SECONDS)


def run_reset_phase():
    locomotion = state["locomotion"]
    state["movement"].stop_movement_immediately()
    locomotion.set_sprint_held(False)
    clear_move()
    if locomotion.is_move_stop_pending() or locomotion.is_move_stop_active():
        return
    if locomotion.is_ground_turn_pending() or locomotion.is_ground_turn_active():
        return
    set_actor_yaw(0.0)
    if phase_game_elapsed() < 0.15 or "Grounded" not in current_anim_state():
        return

    action, index = state["next_action"]
    if action == "reverse":
        start_reverse_case(index)
    elif action == "move_stop":
        start_move_stop(index)
    else:
        fail(f"unknown reset action={action}")


def start_reverse_case(index):
    case = REVERSE_CASES[index]
    state["reverse_index"] = index
    state["current_case"] = case
    advance(f"{case['name']}_prepare", PREPARE_TIMEOUT_SECONDS)


def run_reverse_prepare_phase():
    case = state["current_case"]
    drive_forward(case["magnitude"], case["sprint"])
    if not movement_is_ready(case["gait"], case["min_speed"]):
        return

    locomotion = state["locomotion"]
    state["baseline_move_stop_id"] = locomotion.get_move_stop_request_id()
    state["baseline_ground_turn_id"] = locomotion.get_ground_turn_request_id()
    state["turnback_active_seen"] = False
    drive_reverse(case)
    state["routed_ground_turn_id"] = locomotion.get_ground_turn_request_id()
    if case["expected_ground_turn"] == "run_turnback" and not require(
        state["routed_ground_turn_id"] != state["baseline_ground_turn_id"],
        f"{case['name']} did not request GroundTurn RunTurnback",
    ):
        return
    if not check_reverse_routing(case):
        return
    advance(f"{case['name']}_observe", 1.0)


def run_reverse_observe_phase():
    case = state["current_case"]
    state["locomotion"].set_sprint_held(case["sprint"])
    drive_reverse(case)
    if not check_reverse_routing(case):
        return
    if phase_game_elapsed() < NO_TRIGGER_OBSERVE_SECONDS:
        return

    if case["expected_ground_turn"] == "run_turnback" and not require(
        state["turnback_active_seen"],
        f"{case['name']} never entered RunTurnback",
    ):
        return
    result = case["expected_ground_turn"] or "none"
    state["checks"].append(f"{case['name']}={result}")
    next_index = state["reverse_index"] + 1
    if next_index < len(REVERSE_CASES):
        schedule_reset("reverse", next_index)
    else:
        schedule_reset("move_stop", 0)


def start_move_stop(index):
    case = MOVE_STOP_CASES[index]
    state["stop_index"] = index
    state["current_case"] = case
    state["current_stop"] = None
    advance(f"{case['name']}_prepare", PREPARE_TIMEOUT_SECONDS)


def begin_move_stop():
    case = state["current_case"]
    pawn = state["pawn"]
    locomotion = state["locomotion"]
    velocity = pawn.get_velocity()
    speed = horizontal_length(velocity)
    if not require(
        speed >= case["min_entry_speed"],
        f"{case['name']} entry speed={speed:.1f}",
    ):
        return False

    selected_from_pose = expected_left_variant()
    if not require(
        selected_from_pose == case["expected_left_variant"],
        f"{case['name']} foot phase changed before request",
    ):
        return False

    travel_x = velocity.x / speed
    travel_y = velocity.y / speed
    actor_start = copy_vector(pawn.get_actor_location())
    bip_relative_start = copy_vector(
        state["mesh"].get_socket_location("Bip001") - actor_start
    )
    move_stop_id_before = locomotion.get_move_stop_request_id()
    ground_turn_id = locomotion.get_ground_turn_request_id()
    state["current_stop"] = {
        "name": case["name"],
        "travel": (travel_x, travel_y),
        "actor_start": actor_start,
        "bip_relative_start": bip_relative_start,
        "selected_left_variant": selected_from_pose,
        "expected_asset": f"Stop_{case['gait'].title()}_"
        f"{'L' if selected_from_pose else 'R'}",
        "minimum_input": 0.08 if case["magnitude"] < 0.5 else 0.8,
        "samples": [],
        "active_seen": False,
        "active_started": None,
        "completion_time": None,
        "completion_location": None,
        "ground_turn_id": ground_turn_id,
        "asset_verified": False,
        "resume_requested": False,
        "resume_requested_time": None,
        "active_release_applied": False,
    }

    apply_move_stop_release()
    request_id = locomotion.get_move_stop_request_id()
    stop = state["current_stop"]
    stop["request_id"] = request_id
    if not require(
        request_id != move_stop_id_before,
        f"{case['name']} {case['trigger']} did not request MoveStop",
    ):
        return False
    if not require(
        locomotion.is_move_stop_pending() or locomotion.is_move_stop_active(),
        f"{case['name']} {case['trigger']} request was not pending or active",
    ):
        return False
    if not require(
        enum_name(locomotion.get_move_stop_gait()) == case["gait"],
        f"MoveStop gait={locomotion.get_move_stop_gait()}",
    ):
        return False
    if not require(
        locomotion.does_move_stop_use_left_variant() == selected_from_pose,
        f"{case['name']} selected the opposite foot variant",
    ):
        return False
    if not require(
        locomotion.get_ground_turn_request_id() == ground_turn_id
        and not locomotion.is_ground_turn_pending()
        and not locomotion.is_ground_turn_active(),
        f"{case['name']} {case['trigger']} also requested GroundTurn",
    ):
        return False
    if not require(
        not locomotion.can_accept_movement_input(),
        "MoveStop request did not lock movement input",
    ):
        return False
    move_input = locomotion.get_move_input()
    if not require(
        abs(move_input.x) <= 0.01 and abs(move_input.y) <= 0.01,
        f"{case['name']} release retained nonzero input="
        f"({move_input.x:.2f},{move_input.y:.2f})",
    ):
        return False
    if not require(
        not locomotion.should_move_stop_resume_movement(),
        f"{case['name']} release was already marked to resume",
    ):
        return False

    sample_stop()
    return True


def run_move_stop_prepare_phase():
    case = state["current_case"]
    drive_forward(case["magnitude"], case["sprint"])
    if not movement_is_ready(case["gait"], case["min_entry_speed"]):
        return
    if expected_left_variant() != case["expected_left_variant"]:
        return
    if not begin_move_stop():
        return
    advance(f"{case['name']}_wait_active", WAIT_ACTIVE_TIMEOUT_SECONDS)


def verify_stop_input(require_active_or_pending=True):
    stop = state["current_stop"]
    case = state["current_case"]
    locomotion = state["locomotion"]
    stop_in_progress = (
        locomotion.is_move_stop_pending() or locomotion.is_move_stop_active()
    )
    if not require(
        locomotion.get_move_stop_request_id() == stop["request_id"],
        f"{stop['name']} created multiple MoveStop requests",
    ):
        return False
    if not require(
        locomotion.get_ground_turn_request_id() == stop["ground_turn_id"]
        and not locomotion.is_ground_turn_pending()
        and not locomotion.is_ground_turn_active(),
        f"{stop['name']} also requested GroundTurn",
    ):
        return False
    if not require(
        "RunTurnback" not in current_anim_state(),
        f"{stop['name']} entered RunTurnback",
    ):
        return False
    move_input = locomotion.get_move_input()
    if not stop["resume_requested"] or stop["active_release_applied"]:
        if not require(
            abs(move_input.x) <= 0.01 and abs(move_input.y) <= 0.01,
            f"{stop['name']} release gained input="
            f"({move_input.x:.2f},{move_input.y:.2f})",
        ):
            return False
        if stop_in_progress and not require(
            not locomotion.should_move_stop_resume_movement(),
            f"{stop['name']} release was marked to resume before input",
        ):
            return False
    else:
        if not require(
            move_input.y >= stop["minimum_input"],
            f"{stop['name']} lost forward input="
            f"({move_input.x:.2f},{move_input.y:.2f})",
        ):
            return False
        if stop_in_progress and not require(
            locomotion.should_move_stop_resume_movement(),
            f"{stop['name']} retained input was not marked to resume",
        ):
            return False
    if require_active_or_pending and not require(
        locomotion.is_move_stop_pending() or locomotion.is_move_stop_active(),
        f"{stop['name']} canceled before completion",
    ):
        return False
    if stop_in_progress and not require(
        not locomotion.can_accept_movement_input(),
        f"{stop['name']} allowed movement during Root Motion",
    ):
        return False
    return True


def apply_active_release():
    case = state["current_case"]
    stop = state["current_stop"]
    locomotion = state["locomotion"]
    order = case.get("active_release_order")
    if order is None or stop["active_release_applied"]:
        return True

    if order == "move_then_sprint":
        clear_move()
        locomotion.set_sprint_held(False)
    elif order == "sprint_then_move":
        locomotion.set_sprint_held(False)
        clear_move()
    else:
        fail(f"{stop['name']} unknown active release order={order}")
        return False

    stop["active_release_applied"] = True
    stop["resume_requested"] = False
    stop["active_release_time"] = game_time()
    if not require(
        locomotion.get_move_stop_request_id() == stop["request_id"],
        f"{stop['name']} rebuilt MoveStop request during {order}",
    ):
        return False
    if not require(
        locomotion.is_move_stop_active() and not locomotion.is_move_stop_pending(),
        f"{stop['name']} lost the active MoveStop during {order}",
    ):
        return False
    if not require(
        enum_name(locomotion.get_move_stop_gait()) == "sprint",
        f"{stop['name']} changed MoveStop gait during {order}",
    ):
        return False
    if not require(
        not locomotion.should_move_stop_resume_movement(),
        f"{stop['name']} remained marked to resume after {order}",
    ):
        return False
    if not verify_stop_input():
        return False

    unreal.log(
        f"ROVER_MOVE_STOP_ACTIVE_RELEASE case={stop['name']} order={order} "
        f"request={stop['request_id']}"
    )
    return True


def sample_stop():
    stop = state.get("current_stop")
    if stop is None:
        return None

    pawn_location = state["pawn"].get_actor_location()
    bip_location = state["mesh"].get_socket_location("Bip001")
    bip_relative = bip_location - pawn_location
    actor_delta = pawn_location - stop["actor_start"]
    bip_delta = bip_relative - stop["bip_relative_start"]
    travel_x, travel_y = stop["travel"]
    lateral_x, lateral_y = -travel_y, travel_x
    sample = {
        "time": game_time(),
        "actor_forward": actor_delta.x * travel_x + actor_delta.y * travel_y,
        "actor_lateral": actor_delta.x * lateral_x + actor_delta.y * lateral_y,
        "bip_forward": bip_delta.x * travel_x + bip_delta.y * travel_y,
        "bip_lateral": bip_delta.x * lateral_x + bip_delta.y * lateral_y,
        "bip_drift": horizontal_distance(bip_relative, stop["bip_relative_start"]),
        "active": state["locomotion"].is_move_stop_active(),
        "pending": state["locomotion"].is_move_stop_pending(),
    }
    sample["visual_forward"] = sample["actor_forward"] + sample["bip_forward"]
    sample["visual_lateral"] = sample["actor_lateral"] + sample["bip_lateral"]
    stop["samples"].append(sample)
    stop["active_seen"] = stop["active_seen"] or sample["active"]
    return sample


def backward_step(samples, key):
    return max(
        (previous[key] - current[key] for previous, current in zip(samples, samples[1:])),
        default=0.0,
    )


def max_planar_step(samples, forward_key, lateral_key):
    return max(
        (
            math.sqrt(
                (current[forward_key] - previous[forward_key]) ** 2
                + (current[lateral_key] - previous[lateral_key]) ** 2
            )
            for previous, current in zip(samples, samples[1:])
        ),
        default=0.0,
    )


def validate_completed_stop():
    stop = state["current_stop"]
    case = state["current_case"]
    samples = stop["samples"]
    if not require(stop["active_seen"], f"{stop['name']} never became active"):
        return False
    if not require(stop["asset_verified"], f"{stop['name']} asset was not verified"):
        return False
    if not require(
        len(samples) >= MIN_STOP_SAMPLES,
        f"{stop['name']} only collected {len(samples)} stop samples",
    ):
        return False

    last = samples[-1]
    completion_time = stop["completion_time"]
    tail = [
        sample
        for sample in samples
        if sample["time"] >= completion_time - TAIL_LOOKBACK_SECONDS
    ]
    if not require(
        len(tail) >= MIN_TAIL_SAMPLES,
        f"{stop['name']} only collected {len(tail)} tail samples",
    ):
        return False

    capsule_travel = last["actor_forward"]
    capsule_lateral = max(abs(sample["actor_lateral"]) for sample in samples)
    capsule_backstep = max(0.0, backward_step(samples, "actor_forward"))
    capsule_peak = max(sample["actor_forward"] for sample in tail)
    capsule_rebound = max(0.0, capsule_peak - capsule_travel)
    bip_drift = max(sample["bip_drift"] for sample in samples)
    bip_end_offset = last["bip_drift"]
    visual_backstep = max(0.0, backward_step(samples, "visual_forward"))
    visual_rebound = max(
        0.0, max(sample["visual_forward"] for sample in tail) - last["visual_forward"]
    )
    bip_tail_step = max_planar_step(tail, "bip_forward", "bip_lateral")
    visual_tail_step = max_planar_step(tail, "visual_forward", "visual_lateral")

    variant = "L" if stop["selected_left_variant"] else "R"
    unreal.log(
        "ROVER_MOVE_STOP_METRIC "
        f"case={stop['name']} gait={case['gait']} variant={variant} "
        f"asset={stop['expected_asset']} "
        f"active_duration={stop['active_duration']:.3f} "
        f"normalized_target={case['resume_normalized_time']:.2f} "
        f"samples={len(samples)} tail_samples={len(tail)} "
        f"capsule_travel={capsule_travel:.2f} "
        f"capsule_lateral={capsule_lateral:.2f} "
        f"capsule_backstep={capsule_backstep:.2f} "
        f"capsule_tail_rebound={capsule_rebound:.2f} "
        f"bip001_drift={bip_drift:.2f} "
        f"bip001_end_offset={bip_end_offset:.2f} "
        f"visual_backstep={visual_backstep:.2f} "
        f"visual_tail_rebound={visual_rebound:.2f} "
        f"bip001_tail_step={bip_tail_step:.2f} "
        f"visual_tail_step={visual_tail_step:.2f}"
    )

    minimum_travel, maximum_travel = case["travel_bounds"]
    checks = (
        (
            minimum_travel <= capsule_travel <= maximum_travel,
            f"{stop['name']} capsule travel={capsule_travel:.1f} cm "
            f"expected={minimum_travel:.0f}-{maximum_travel:.0f} cm",
        ),
        (
            capsule_lateral <= MAX_CAPSULE_LATERAL_CM,
            f"{stop['name']} capsule lateral drift={capsule_lateral:.1f} cm",
        ),
        (
            capsule_backstep <= MAX_CAPSULE_BACKSTEP_CM,
            f"{stop['name']} capsule backstep={capsule_backstep:.1f} cm",
        ),
        (
            capsule_rebound <= MAX_CAPSULE_TAIL_REBOUND_CM,
            f"{stop['name']} capsule tail rebound={capsule_rebound:.1f} cm",
        ),
        (
            bip_drift <= MAX_BIP001_DRIFT_CM,
            f"{stop['name']} Bip001 relative drift={bip_drift:.1f} cm",
        ),
        (
            bip_end_offset <= MAX_BIP001_END_OFFSET_CM,
            f"{stop['name']} Bip001 end offset={bip_end_offset:.1f} cm",
        ),
        (
            visual_backstep <= MAX_VISUAL_BACKSTEP_CM,
            f"{stop['name']} visual backstep={visual_backstep:.1f} cm",
        ),
        (
            visual_rebound <= MAX_VISUAL_TAIL_REBOUND_CM,
            f"{stop['name']} visual tail rebound={visual_rebound:.1f} cm",
        ),
        (
            bip_tail_step <= MAX_TAIL_PLANAR_STEP_CM,
            f"{stop['name']} Bip001 planar tail step={bip_tail_step:.1f} cm",
        ),
        (
            visual_tail_step <= MAX_TAIL_PLANAR_STEP_CM,
            f"{stop['name']} visual planar tail step={visual_tail_step:.1f} cm",
        ),
    )
    for condition, detail in checks:
        if not require(condition, detail):
            return False

    state["checks"].append(
        f"{stop['name']}={variant}:{capsule_travel:.0f}cm/"
        f"rebound{visual_rebound:.1f}"
    )
    return True


def run_stop_wait_active_phase():
    case = state["current_case"]
    stop = state["current_stop"]
    drive_move_stop_input(case, stop["resume_requested"])
    if not verify_stop_input():
        return
    sample_stop()
    if not state["locomotion"].is_move_stop_active():
        return

    anim_state = current_anim_state()
    if not require("MoveStop" in anim_state, f"state={anim_state}"):
        return
    asset = current_anim_asset()
    if not require(
        asset == stop["expected_asset"],
        f"{stop['name']} expected asset={stop['expected_asset']} actual={asset}",
    ):
        return
    stop["asset_verified"] = True
    stop["active_started"] = game_time()
    if case.get("active_release_order") is not None:
        if not apply_active_release():
            return
    elif case["resume_during_stop"]:
        stop["resume_requested"] = True
        stop["resume_requested_time"] = game_time()
        drive_move_stop_input(case, True)
        if not verify_stop_input():
            return
    unreal.log(
        f"ROVER_MOVE_STOP_ACTIVE case={stop['name']} asset={asset} "
        f"request={stop['request_id']} trigger={case['trigger']} "
        f"resume={case['resume_direction']}"
    )
    advance(
        f"{stop['name']}_wait_complete",
        case.get("wait_complete_timeout", WAIT_COMPLETE_TIMEOUT_SECONDS),
    )


def run_stop_wait_complete_phase():
    case = state["current_case"]
    stop = state["current_stop"]
    drive_move_stop_input(case, stop["resume_requested"])
    if not verify_stop_input(require_active_or_pending=False):
        return
    sample = sample_stop()
    locomotion = state["locomotion"]
    if locomotion.is_move_stop_active():
        if not require("MoveStop" in current_anim_state(), f"state={current_anim_state()}"):
            return
        asset = current_anim_asset()
        if not require(
            asset == stop["expected_asset"],
            f"{stop['name']} changed asset during stop actual={asset}",
        ):
            return
        return
    if locomotion.is_move_stop_pending():
        fail(f"{stop['name']} returned to pending after becoming active")
        return

    stop["completion_time"] = sample["time"]
    stop["completion_location"] = copy_vector(state["pawn"].get_actor_location())
    active_duration = stop["completion_time"] - stop["active_started"]
    expected_active_duration = (
        case["asset_duration"] * case["resume_normalized_time"]
    )
    if "active_time_bounds" in case:
        minimum_active_duration, maximum_active_duration = case["active_time_bounds"]
    else:
        minimum_active_duration = max(
            0.1,
            expected_active_duration - MOVE_STOP_ACTIVE_TIME_TOLERANCE_SECONDS,
        )
        maximum_active_duration = min(
            MAX_MOVE_STOP_ACTIVE_SECONDS,
            expected_active_duration + MOVE_STOP_ACTIVE_TIME_TOLERANCE_SECONDS,
        )
    if not require(
        minimum_active_duration <= active_duration <= maximum_active_duration,
        f"{stop['name']} active for {active_duration:.2f}s; expected "
        f"{minimum_active_duration:.2f}-{maximum_active_duration:.2f}s "
        f"around normalized {case['resume_normalized_time']:.2f}",
    ):
        return
    if not require(
        locomotion.can_accept_movement_input(),
        f"{stop['name']} completion did not unlock movement",
    ):
        return
    stop["active_duration"] = active_duration
    if not validate_completed_stop():
        return
    if case["resume_during_stop"]:
        advance(f"{stop['name']}_resume", RESUME_TIMEOUT_SECONDS)
    else:
        advance(f"{stop['name']}_idle", IDLE_READY_TIMEOUT_SECONDS)


def check_stop_ids_after_completion(label):
    stop = state["current_stop"]
    locomotion = state["locomotion"]
    if not require(
        locomotion.get_move_stop_request_id() == stop["request_id"],
        f"{label} retriggered MoveStop while resume input stayed held",
    ):
        return False
    if not require(
        not locomotion.is_move_stop_pending()
        and not locomotion.is_move_stop_active(),
        f"{label} re-entered MoveStop",
    ):
        return False
    if not require(
        locomotion.get_ground_turn_request_id() == stop["ground_turn_id"]
        and not locomotion.is_ground_turn_pending()
        and not locomotion.is_ground_turn_active(),
        f"{label} requested GroundTurn after completion",
    ):
        return False
    return True


def run_stop_idle_phase():
    case = state["current_case"]
    stop = state["current_stop"]
    clear_move()
    if not check_stop_ids_after_completion(stop["name"]):
        return

    speed = horizontal_length(state["pawn"].get_velocity())
    if (
        speed <= 2.0
        and enum_name(state["locomotion"].get_gait()) == "idle"
        and "Grounded" in current_anim_state()
        and current_anim_asset() == case["ground_asset"]
        and state["locomotion"].can_accept_movement_input()
    ):
        stop["idle_ready_time"] = phase_game_elapsed()
        advance(f"{stop['name']}_idle_guard", IDLE_GUARD_SECONDS + 0.2)


def run_stop_idle_guard_phase():
    case = state["current_case"]
    stop = state["current_stop"]
    clear_move()
    if not check_stop_ids_after_completion(stop["name"]):
        return
    if not require(
        horizontal_length(state["pawn"].get_velocity()) <= 2.0
        and enum_name(state["locomotion"].get_gait()) == "idle"
        and "Grounded" in current_anim_state()
        and current_anim_asset() == case["ground_asset"]
        and state["locomotion"].can_accept_movement_input(),
        f"{stop['name']} did not remain idle state={current_anim_state()} "
        f"asset={current_anim_asset()} gait={state['locomotion'].get_gait()}",
    ):
        return
    if phase_game_elapsed() < IDLE_GUARD_SECONDS:
        return

    state["checks"].append(
        f"{stop['name']}_idle={stop['active_duration']:.2f}s>"
        f"{stop['idle_ready_time']:.2f}s/{case['ground_asset']}"
    )
    next_index = state["stop_index"] + 1
    if next_index < len(MOVE_STOP_CASES):
        schedule_reset("move_stop", next_index)
    else:
        finish(True, " ".join(state["checks"]))


def run_stop_resume_phase():
    case = state["current_case"]
    stop = state["current_stop"]
    drive_move_stop_input(case, True)
    if not check_stop_ids_after_completion(stop["name"]):
        return

    pawn = state["pawn"]
    velocity = pawn.get_velocity()
    speed = horizontal_length(velocity)
    completion = stop["completion_location"]
    location = pawn.get_actor_location()
    resume_distance = location.x - completion.x
    resume_elapsed = phase_game_elapsed()
    input_to_ready_elapsed = (
        game_time() - stop["resume_requested_time"]
        if stop["resume_requested_time"] is not None
        else None
    )
    max_ready_seconds = (
        MAX_WALK_RESUME_INPUT_TO_READY_SECONDS
        if case["gait"] == "walk"
        else MAX_RESUME_READY_SECONDS
    )
    if resume_elapsed > max_ready_seconds:
        fail(
            f"{stop['name']} did not resume {case['gait']} within "
            f"{max_ready_seconds:.2f}s of move-stop completion; "
            f"speed={speed:.1f} distance={resume_distance:.1f} "
            f"state={current_anim_state()} "
            f"asset={current_anim_asset()}"
        )
        return
    if (
        speed >= case["resume_speed"]
        and velocity.x / speed >= 0.85
        and pawn.get_actor_forward_vector().x >= 0.80
        and resume_distance >= case["resume_distance"]
        and enum_name(state["locomotion"].get_gait()) == case["gait"]
        and "Grounded" in current_anim_state()
        and current_anim_asset() == case["ground_asset"]
        and state["locomotion"].can_accept_movement_input()
    ):
        stop["resume_speed"] = speed
        stop["resume_distance"] = resume_distance
        stop["resume_time"] = resume_elapsed
        stop["resume_input_to_ready_time"] = input_to_ready_elapsed
        stop["resume_guard_start_distance"] = resume_distance
        stop["resume_guard_min_speed"] = speed
        advance(f"{stop['name']}_resume_guard", 1.0)


def run_stop_resume_guard_phase():
    case = state["current_case"]
    stop = state["current_stop"]
    drive_move_stop_input(case, True)
    if not check_stop_ids_after_completion(stop["name"]):
        return
    anim_state = current_anim_state()
    if not require(
        "MoveStop" not in anim_state and "RunTurnback" not in anim_state,
        f"{stop['name']} resume guard state={anim_state}",
    ):
        return
    if not require(
        enum_name(state["locomotion"].get_gait()) == case["gait"],
        f"{stop['name']} resume gait={state['locomotion'].get_gait()}",
    ):
        return
    if not require(
        state["locomotion"].can_accept_movement_input(),
        f"{stop['name']} locked movement again during resume guard",
    ):
        return
    if not require(
        current_anim_asset() == case["ground_asset"],
        f"{stop['name']} resume asset={current_anim_asset()} "
        f"expected={case['ground_asset']}",
    ):
        return

    pawn = state["pawn"]
    velocity = pawn.get_velocity()
    speed = horizontal_length(velocity)
    if not require(
        speed >= case["guard_speed"]
        and velocity.x / speed >= 0.80
        and pawn.get_actor_forward_vector().x >= 0.75,
        f"{stop['name']} did not sustain forward "
        f"{case['gait']} "
        f"speed={speed:.1f} velocity_x={velocity.x:.1f} "
        f"forward_x={pawn.get_actor_forward_vector().x:.2f}",
    ):
        return
    resume_distance = pawn.get_actor_location().x - stop["completion_location"].x
    stop["resume_guard_min_speed"] = min(stop["resume_guard_min_speed"], speed)
    if phase_game_elapsed() < RESUME_GUARD_SECONDS:
        return

    guard_progress = resume_distance - stop["resume_guard_start_distance"]
    if not require(
        guard_progress >= case["guard_progress"],
        f"{stop['name']} stalled during resume guard progress={guard_progress:.1f} cm",
    ):
        return

    ready_time = (
        stop["resume_input_to_ready_time"]
        if stop["resume_input_to_ready_time"] is not None
        else stop["resume_time"]
    )
    state["checks"].append(
        f"{stop['name']}_resume={stop['active_duration']:.2f}s>"
        f"{ready_time:.2f}s/{stop['resume_speed']:.0f}cmps/"
        f"{stop['resume_distance']:.0f}cm+{guard_progress:.0f}cm"
    )
    next_index = state["stop_index"] + 1
    if next_index < len(MOVE_STOP_CASES):
        schedule_reset("move_stop", next_index)
    else:
        finish(True, " ".join(state["checks"]))


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
    mesh = pawn.get_component_by_class(unreal.SkeletalMeshComponent)
    locomotion = pawn.get_locomotion_component()
    if not require(movement is not None, "missing movement component"):
        return
    if not require(mesh is not None, "missing skeletal mesh component"):
        return
    if not require(locomotion is not None, "missing locomotion component"):
        return
    if not require(mesh.get_anim_instance() is not None, "missing animation instance"):
        return
    for bone in ("Bip001", "Bip001LFoot", "Bip001RFoot"):
        if not require(mesh.does_socket_exist(bone), f"missing bone/socket={bone}"):
            return
    if not movement.is_moving_on_ground():
        return

    state.update(
        {
            "world": world,
            "pawn": pawn,
            "movement": movement,
            "mesh": mesh,
            "locomotion": locomotion,
            "test_origin": copy_vector(pawn.get_actor_location()),
        }
    )
    schedule_reset("reverse", 0)


def run_phase():
    phase = state["phase"]
    if phase == "reset":
        run_reset_phase()
    elif phase.endswith("_prepare"):
        if state["current_stop"] is not None or any(
            phase.startswith(f"{case['name']}_") for case in MOVE_STOP_CASES
        ):
            run_move_stop_prepare_phase()
        else:
            run_reverse_prepare_phase()
    elif phase.endswith("_observe"):
        run_reverse_observe_phase()
    elif phase.endswith("_wait_active"):
        run_stop_wait_active_phase()
    elif phase.endswith("_wait_complete"):
        run_stop_wait_complete_phase()
    elif phase.endswith("_idle_guard"):
        run_stop_idle_guard_phase()
    elif phase.endswith("_idle"):
        run_stop_idle_phase()
    elif phase.endswith("_resume_guard"):
        run_stop_resume_guard_phase()
    elif phase.endswith("_resume"):
        run_stop_resume_phase()
    else:
        fail(f"unknown phase={phase}")


def phase_status():
    locomotion = state.get("locomotion")
    if locomotion is None:
        return "pawn_not_ready"
    pawn = state["pawn"]
    forward = pawn.get_actor_forward_vector()
    return (
        f"state={current_anim_state()} asset={current_anim_asset()} "
        f"gait={locomotion.get_gait()} speed={horizontal_length(pawn.get_velocity()):.1f} "
        f"move_stop={locomotion.is_move_stop_pending()}/{locomotion.is_move_stop_active()} "
        f"ground_turn={locomotion.is_ground_turn_pending()}/{locomotion.is_ground_turn_active()} "
        f"facing=({forward.x:.2f},{forward.y:.2f})"
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
