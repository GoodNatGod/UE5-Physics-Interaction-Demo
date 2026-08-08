import re
import time

import unreal


EXPECTED_PAWN = "/Script/RoverReplica.RoverCharacter"
EXPECTED_ANIM_CLASS = "/Game/Rover/Animations/ABP_Rover.ABP_Rover_C"
LEFT_FOOT_BONE = "Bip001LFoot"
RIGHT_FOOT_BONE = "Bip001RFoot"
command_line = unreal.SystemLibrary.get_command_line()
target_match = re.search(r"(?:^|\s)-RoverAttackIdleTarget=(\d+)(?:\s|$)", command_line)
TARGET_COMBO_INDEX = int(target_match.group(1)) if target_match else 1
if TARGET_COMBO_INDEX not in (1, 2, 3, 4):
    raise RuntimeError(f"Invalid RoverAttackIdleTarget={TARGET_COMBO_INDEX}; expected 1..4")
START_TIMEOUT_SECONDS = 30.0
ATTACK_TIMEOUT_SECONDS = 20.0
LOCK_OBSERVATION_SECONDS = 1.0
STOP_TIMEOUT_SECONDS = 15.0
SUCCESS_MARKER = "ROVER_ATTACK_IDLE_STANCE_PIE_OK"
FAILURE_MARKER = "ROVER_ATTACK_IDLE_STANCE_PIE_FAIL"


level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
unreal_editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
state = {
    "phase": "starting",
    "deadline": time.monotonic() + START_TIMEOUT_SECONDS,
    "result": None,
    "last": "PIE has not started",
    "pawn": None,
    "combat": None,
    "locomotion": None,
    "mesh": None,
    "anim_instance": None,
    "saw_attacking": False,
    "saw_final_attack": False,
    "queued_combo_indices": set(),
    "locked_stance": False,
    "lock_observation_end": 0.0,
    "left_projection": 0.0,
    "right_projection": 0.0,
    "recovery_left_projection": 0.0,
    "recovery_right_projection": 0.0,
    "recovery_stance": False,
    "projection_tolerance": 0.0,
    "prelock_left_projection": None,
    "prelock_right_projection": None,
    "capture_left_projection": None,
    "capture_right_projection": None,
    "capture_stance": False,
    "capture_alpha": 0.0,
    "saw_stance_capture": False,
}
tick_handle = None


def object_path(value):
    return value.get_path_name() if value else "None"


def attack_type_contains(combat, expected_name, previous=False):
    attack_type = (
        combat.get_previous_attack_type()
        if previous
        else combat.get_current_attack_type()
    )
    return expected_name in str(attack_type).upper()


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

    locomotion = state["locomotion"]
    if locomotion is not None:
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


def begin_validation(world):
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    if pawn is None:
        state["last"] = "waiting for player pawn"
        return
    if object_path(pawn.get_class()) != EXPECTED_PAWN:
        finish(False, f"unexpected pawn={object_path(pawn.get_class())}")
        return

    movement = pawn.get_component_by_class(unreal.CharacterMovementComponent)
    if movement is None or not movement.is_moving_on_ground():
        state["last"] = "waiting for player to reach the ground"
        return

    combat = pawn.get_combat_component()
    locomotion = pawn.get_locomotion_component()
    mesh = pawn.get_component_by_class(unreal.SkeletalMeshComponent)
    anim_instance = mesh.get_anim_instance() if mesh else None
    if any(value is None for value in (combat, locomotion, mesh, anim_instance)):
        finish(False, "player is missing a combat, locomotion, mesh, or AnimInstance dependency")
        return
    if object_path(anim_instance.get_class()) != EXPECTED_ANIM_CLASS:
        finish(False, f"unexpected AnimInstance={object_path(anim_instance.get_class())}")
        return
    if mesh.get_bone_index(LEFT_FOOT_BONE) < 0 or mesh.get_bone_index(RIGHT_FOOT_BONE) < 0:
        finish(False, "Rover mesh is missing a required foot bone")
        return

    if TARGET_COMBO_INDEX == 4:
        combat_config = combat.get_editor_property("combat_config")
        settings = combat_config.get_editor_property("settings")
        attack_chain = list(settings.get_editor_property("light_attack_chain"))
        if len(attack_chain) < 4:
            finish(False, f"combat config has only {len(attack_chain)} attacks")
            return
        attack04 = attack_chain[3]
        blend_out = float(attack04.get_editor_property("montage_blend_out_time"))
        blend_trigger = float(
            attack04.get_editor_property("montage_blend_out_trigger_time")
        )
        if blend_out < 0.2 or abs(blend_trigger - blend_out) > 0.01:
            finish(
                False,
                f"Attack04 full-body blend-out is invalid: "
                f"blend={blend_out:.3f}s trigger={blend_trigger:.3f}s",
            )
            return

    locomotion.set_move_input(
        unreal.Vector2D(0.0, 0.0), unreal.Vector(0.0, 0.0, 0.0)
    )
    combat.set_light_attack_held(False)
    if not combat.request_attack():
        finish(False, "grounded Attack01 request was rejected")
        return

    state.update(
        {
            "phase": "attacking",
            "deadline": time.monotonic() + ATTACK_TIMEOUT_SECONDS,
            "pawn": pawn,
            "combat": combat,
            "locomotion": locomotion,
            "mesh": mesh,
            "anim_instance": anim_instance,
            "projection_tolerance": anim_instance.get_idle_foot_stance_projection_tolerance(),
            "last": f"waiting for Attack0{TARGET_COMBO_INDEX} to end naturally",
        }
    )


def measure_foot_projections():
    pawn = state["pawn"]
    mesh = state["mesh"]
    origin = pawn.get_actor_location()
    forward = pawn.get_actor_forward_vector()
    left_offset = mesh.get_socket_location(LEFT_FOOT_BONE) - origin
    right_offset = mesh.get_socket_location(RIGHT_FOOT_BONE) - origin
    return (
        left_offset.x * forward.x + left_offset.y * forward.y + left_offset.z * forward.z,
        right_offset.x * forward.x + right_offset.y * forward.y + right_offset.z * forward.z,
    )


def observe_stance_capture():
    anim_instance = state["anim_instance"]
    left_projection, right_projection = measure_foot_projections()
    if not anim_instance.is_foot_stance_locked():
        state["prelock_left_projection"] = left_projection
        state["prelock_right_projection"] = right_projection
        return
    if state["saw_stance_capture"]:
        return

    state["capture_left_projection"] = (
        state["prelock_left_projection"]
        if state["prelock_left_projection"] is not None
        else left_projection
    )
    state["capture_right_projection"] = (
        state["prelock_right_projection"]
        if state["prelock_right_projection"] is not None
        else right_projection
    )
    state["capture_stance"] = anim_instance.is_using_stand2()
    state["capture_alpha"] = anim_instance.get_idle_stance_alpha()
    state["saw_stance_capture"] = True


def validate_attack_end():
    combat = state["combat"]
    anim_instance = state["anim_instance"]
    if combat.is_attacking():
        state["saw_attacking"] = True
        if not attack_type_contains(combat, "LIGHT_ATTACK"):
            finish(False, f"active attack type={combat.get_current_attack_type()}")
            return
        combo_index = combat.get_current_combo_index()
        if combo_index == TARGET_COMBO_INDEX:
            state["saw_final_attack"] = True
            observe_stance_capture()
            if "RECOVERY" in str(combat.get_combat_phase()).upper():
                recovery_left, recovery_right = measure_foot_projections()
                state["recovery_left_projection"] = recovery_left
                state["recovery_right_projection"] = recovery_right
                state["recovery_stance"] = anim_instance.is_using_stand2()
        if (
            combo_index == 3
            and combo_index < TARGET_COMBO_INDEX
            and combo_index not in state["queued_combo_indices"]
            and not combat.is_combo_window_open()
            and not combat.is_resonance_window_open()
            and not combat.is_resonance_trigger_window_open()
            and any(
                name in str(combat.get_third_attack_weapon_throw_phase()).upper()
                for name in ("OUTBOUND", "SPINNING")
            )
            and "ACTIVE" in str(combat.get_combat_phase()).upper()
        ):
            request_id = combat.get_attack_request_id()
            if not combat.request_attack():
                finish(False, "Attack03 rejected the pre-window Attack04 buffer")
                return
            if (
                combat.get_attack_request_id() != request_id
                or combat.get_current_combo_index() != 3
                or not combat.is_attack_input_buffered()
            ):
                finish(False, "Attack03 pre-window input was not buffered")
                return
            state["queued_combo_indices"].add(combo_index)
            return
        if (
            combo_index < TARGET_COMBO_INDEX
            and combat.is_combo_window_open()
            and combo_index not in state["queued_combo_indices"]
        ):
            if not combat.request_attack():
                finish(False, f"Attack0{combo_index} ComboWindow rejected the next click")
                return
            state["queued_combo_indices"].add(combo_index)
        return
    if not attack_type_contains(combat, "NONE") or not attack_type_contains(
        combat, "LIGHT_ATTACK", previous=True
    ):
        finish(
            False,
            f"natural attack end types current={combat.get_current_attack_type()} "
            f"previous={combat.get_previous_attack_type()}",
        )
        return
    if not state["saw_attacking"]:
        finish(False, "attack chain never entered the attacking state")
        return
    if not state["saw_final_attack"]:
        finish(False, f"attack chain ended before Attack0{TARGET_COMBO_INDEX}")
        return
    observe_stance_capture()
    if not state["saw_stance_capture"]:
        state["phase"] = "waiting_for_lock"
        state["last"] = f"Attack0{TARGET_COMBO_INDEX} ended; waiting for pre-blend stance capture"
        return
    validate_captured_stance()


def validate_captured_stance():
    anim_instance = state["anim_instance"]
    if not anim_instance.is_foot_stance_locked():
        finish(False, "post-attack foot stance unlocked before validation")
        return

    left_projection = state["capture_left_projection"]
    right_projection = state["capture_right_projection"]
    use_stand2 = anim_instance.is_using_stand2()
    expected_alpha = 1.0 if use_stand2 else 0.0
    if abs(state["capture_alpha"] - expected_alpha) > 0.01:
        finish(
            False,
            f"idle stance source was not ready before Montage blend-out: "
            f"alpha={state['capture_alpha']:.3f} expected={expected_alpha:.1f}",
        )
        return
    if abs(right_projection - left_projection) > state["projection_tolerance"]:
        expected_stand2 = right_projection > left_projection
        if use_stand2 != expected_stand2:
            finish(
                False,
                f"stance mismatch left={left_projection:.2f} right={right_projection:.2f} "
                f"stand2={use_stand2} expected={expected_stand2}",
            )
            return

    state["locked_stance"] = use_stand2
    state["left_projection"] = left_projection
    state["right_projection"] = right_projection
    state["lock_observation_end"] = time.monotonic() + LOCK_OBSERVATION_SECONDS
    state["phase"] = "observing_lock"
    state["last"] = "post-attack stance locked; observing stationary hold"


def wait_for_stance_capture():
    observe_stance_capture()
    if state["saw_stance_capture"]:
        validate_captured_stance()


def validate_stationary_lock():
    anim_instance = state["anim_instance"]
    if not anim_instance.is_foot_stance_locked():
        finish(False, "foot stance unlocked while the player remained stationary")
        return
    if anim_instance.is_using_stand2() != state["locked_stance"]:
        finish(False, "idle stance changed while the post-attack lock was active")
        return
    expected_alpha = 1.0 if state["locked_stance"] else 0.0
    if abs(anim_instance.get_idle_stance_alpha() - expected_alpha) > 0.01:
        finish(False, "idle stance source changed while the post-attack lock was active")
        return
    if time.monotonic() < state["lock_observation_end"]:
        return

    move_direction = state["pawn"].get_actor_forward_vector()
    state["locomotion"].set_move_input(
        unreal.Vector2D(0.0, 1.0), move_direction
    )
    state["pawn"].add_movement_input(move_direction, 1.0, False)
    state["phase"] = "unlocking"
    state["last"] = "movement input injected; waiting for stance unlock"


def validate_movement_unlock():
    anim_instance = state["anim_instance"]
    move_direction = state["pawn"].get_actor_forward_vector()
    state["locomotion"].set_move_input(
        unreal.Vector2D(0.0, 1.0), move_direction
    )
    state["pawn"].add_movement_input(move_direction, 1.0, False)
    if anim_instance.is_foot_stance_locked():
        return
    if anim_instance.is_using_stand2():
        finish(False, "movement unlocked the foot stance but did not restore Stand1")
        return

    finish(
        True,
        f"left={state['left_projection']:.2f}cm right={state['right_projection']:.2f}cm "
        f"locked_stand={'Stand2' if state['locked_stance'] else 'Stand1'} "
        f"preblend_alpha={state['capture_alpha']:.2f} "
        f"recovery_left={state['recovery_left_projection']:.2f}cm "
        f"recovery_right={state['recovery_right_projection']:.2f}cm "
        f"recovery_stand={'Stand2' if state['recovery_stance'] else 'Stand1'} "
        "stationary_lock=held movement_unlock=Stand1",
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
        if state["phase"] == "starting" and level_editor.is_in_play_in_editor() and world:
            begin_validation(world)
        elif state["phase"] == "attacking":
            validate_attack_end()
        elif state["phase"] == "waiting_for_lock":
            wait_for_stance_capture()
        elif state["phase"] == "observing_lock":
            validate_stationary_lock()
        elif state["phase"] == "unlocking":
            validate_movement_unlock()

        if state["result"] is None and now >= state["deadline"]:
            finish(False, f"validation timeout; last={state['last']}")
    except Exception as exc:
        finish(False, f"exception={exc!r}; last={state['last']}")


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
tick_handle = unreal.register_slate_post_tick_callback(on_tick)
if not unreal.RoverEditorTestLibrary.request_play_in_new_window():
    unreal.unregister_slate_post_tick_callback(tick_handle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    raise RuntimeError("Unable to request a new-window PIE session")
