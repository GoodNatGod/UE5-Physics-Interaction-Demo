import time

import unreal


SUCCESS_MARKER = "ROVER_ATTACK_COMBO_PIE_OK"
FAILURE_MARKER = "ROVER_ATTACK_COMBO_PIE_FAIL"
START_TIMEOUT = 30.0
SCENARIO_TIMEOUT = 30.0
STOP_TIMEOUT = 15.0
ATTACK_SEQUENCES = (
    "/Game/Rover/Combat/Animations/Attack01",
    "/Game/Rover/Combat/Animations/Attack02",
    "/Game/Rover/Combat/Animations/Attack03",
    "/Game/Rover/Combat/Animations/Attack04",
)
ATTACK_MONTAGES = (
    "/Game/Rover/Combat/Montages/AM_Rover_Attack01",
    "/Game/Rover/Combat/Montages/AM_Rover_Attack02",
    "/Game/Rover/Combat/Montages/AM_Rover_Attack03",
    "/Game/Rover/Combat/Montages/AM_Rover_Attack04",
)
COMBAT_CONFIG = "/Game/Rover/Combat/DA_RoverCombatConfig"


level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
unreal_editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
tick_handle = None
state = {
    "mode": "starting",
    "deadline": time.monotonic() + START_TIMEOUT,
    "result": None,
    "last": "PIE has not started",
    "scenario": "combo",
    "world": None,
    "pawn": None,
    "combat": None,
    "locomotion": None,
    "anim_instance": None,
    "montages": (),
    "request_ids": [],
    "combo_indices": [],
    "last_request_id": 0,
    "clicked_window": False,
    "buffered_attack02": False,
    "attack02_buffer_probe_count": 0,
    "buffer_consumed": False,
    "buffered_attack03": False,
    "transitioned_attack04": False,
    "runtime_windows": set(),
    "input_buffer_duration": 0.0,
    "combo_reset_duration": 0.0,
    "combo_window_starts": (),
    "continuation_request_id": 0,
    "reset_started_world_time": 0.0,
    "reset_elapsed": 0.0,
    "dodge_request_id": 0,
    "dodge_buffer_started_world_time": 0.0,
    "dodge_buffer_elapsed": 0.0,
    "last_snapshot": None,
}


def phase_name(combat):
    return str(combat.get_combat_phase()).upper()


def shutdown():
    global tick_handle
    if state["mode"] == "done":
        return
    state["mode"] = "done"
    ok, detail = state["result"]
    (unreal.log if ok else unreal.log_error)(
        f"{SUCCESS_MARKER if ok else FAILURE_MARKER} {detail}"
    )
    handle, tick_handle = tick_handle, None
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)


def finish(ok, detail):
    if state["result"] is not None:
        return
    state["result"] = (ok, detail)
    if level_editor.is_in_play_in_editor():
        state["mode"] = "stopping"
        state["deadline"] = time.monotonic() + STOP_TIMEOUT
        level_editor.editor_request_end_play()
    else:
        shutdown()


def validate_asset_settings():
    config = unreal.load_asset(COMBAT_CONFIG)
    if config is None:
        raise RuntimeError(f"Missing combat config: {COMBAT_CONFIG}")
    settings = config.get_editor_property("settings")
    input_buffer = settings.get_editor_property("attack_input_buffer_duration")
    combo_reset = settings.get_editor_property("combo_reset_duration")
    if input_buffer <= 0.0:
        raise RuntimeError(f"Attack input buffer must be positive, got={input_buffer:.3f}s")
    if combo_reset <= 0.0:
        raise RuntimeError(f"Combo reset must be positive, got={combo_reset:.3f}s")

    definitions = settings.get_editor_property("light_attack_chain")
    if len(definitions) != 4:
        raise RuntimeError(f"Expected four light attacks, got={len(definitions)}")
    combo_window_starts = []
    for index, definition in enumerate(definitions, 1):
        start_normalized = definition.get_editor_property(
            "combo_window_start_normalized"
        )
        if not 0.0 < start_normalized < 1.0:
            raise RuntimeError(
                f"Attack0{index} ComboWindow start={start_normalized:.3f} "
                "must be inside (0, 1)"
            )
        combo_window_starts.append(float(start_normalized))

    state["input_buffer_duration"] = float(input_buffer)
    state["combo_reset_duration"] = float(combo_reset)
    state["combo_window_starts"] = tuple(combo_window_starts)
    unreal.log(
        "ROVER_ATTACK_COMBO_CONFIG "
        f"input_buffer={input_buffer:.3f}s combo_reset={combo_reset:.3f}s "
        f"window_starts={tuple(round(value, 3) for value in combo_window_starts)}"
    )

    for attack_index, (sequence_path, montage_path) in enumerate(
        zip(ATTACK_SEQUENCES, ATTACK_MONTAGES), 1
    ):
        sequence = unreal.load_asset(sequence_path)
        montage = unreal.load_asset(montage_path)
        if not isinstance(sequence, unreal.AnimSequence):
            raise RuntimeError(f"Missing attack sequence: {sequence_path}")
        if not isinstance(montage, unreal.AnimMontage):
            raise RuntimeError(f"Missing attack Montage: {montage_path}")
        root_lock = str(sequence.get_editor_property("root_motion_root_lock")).upper()
        if sequence.get_editor_property("enable_root_motion"):
            raise RuntimeError(f"Root Motion is enabled: {sequence_path}")
        if not sequence.get_editor_property("force_root_lock"):
            raise RuntimeError(f"Force Root Lock is disabled: {sequence_path}")
        if "REF_POSE" not in root_lock:
            raise RuntimeError(
                f"Unexpected Root Motion Root Lock: {sequence_path}={root_lock}"
            )
        result = unreal.RoverAnimationEditorLibrary.validate_rover_attack_montage(
            sequence, montage
        )
        unreal.log(
            f"ROVER_ATTACK_ASSET_RESULT index={attack_index} raw={result!r}"
        )
        if result is None:
            unreal.log_warning(
                f"Attack0{attack_index} editor validation returned no Python value; "
                "runtime Montage and Notify behavior will be validated in PIE"
            )
            continue
        succeeded = bool(result[0]) if isinstance(result, tuple) else bool(result)
        report = str(result[-1]) if isinstance(result, tuple) else str(result)
        if not succeeded:
            raise RuntimeError(
                f"Attack0{attack_index} Montage validation failed: {report}"
            )
        unreal.log(f"ROVER_ATTACK_ASSET_OK index={attack_index} {report}")


def request_attack(expected_combo_index):
    combat = state["combat"]
    if not combat.request_attack():
        finish(False, f"Attack0{expected_combo_index} request was rejected")
        return 0
    request_id = combat.get_attack_request_id()
    if request_id <= 0 or combat.get_current_combo_index() != expected_combo_index:
        finish(
            False,
            f"Attack0{expected_combo_index} request has invalid state "
            f"request={request_id} combo={combat.get_current_combo_index()}",
        )
        return 0
    state["deadline"] = time.monotonic() + SCENARIO_TIMEOUT
    return request_id


def begin_runtime_validation(world):
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    if pawn is None:
        state["last"] = "waiting for player pawn"
        return
    movement = pawn.get_component_by_class(unreal.CharacterMovementComponent)
    if movement is None or not movement.is_moving_on_ground():
        state["last"] = "waiting for grounded player"
        return
    combat = pawn.get_combat_component()
    locomotion = pawn.get_locomotion_component()
    mesh = pawn.get_component_by_class(unreal.SkeletalMeshComponent)
    anim_instance = mesh.get_anim_instance() if mesh else None
    montages = tuple(unreal.load_asset(path) for path in ATTACK_MONTAGES)
    if any(
        value is None
        for value in (combat, locomotion, anim_instance, *montages)
    ):
        finish(False, "player is missing combo runtime objects")
        return
    if combat.get_current_combo_index() != -1:
        finish(False, f"initial combo index={combat.get_current_combo_index()} expected=-1")
        return

    state.update(
        {
            "world": world,
            "pawn": pawn,
            "combat": combat,
            "locomotion": locomotion,
            "anim_instance": anim_instance,
            "montages": montages,
        }
    )
    request_id = request_attack(1)
    if request_id <= 0:
        return
    state["request_ids"] = [request_id]
    state["combo_indices"] = [1]
    state["last_request_id"] = request_id
    state["mode"] = "running"


def observe_new_request(request_id, combo_index):
    if request_id <= 0 or request_id == state["last_request_id"]:
        return
    if request_id in state["request_ids"]:
        finish(False, f"combo reused RequestId={request_id}")
        return
    state["request_ids"].append(request_id)
    state["combo_indices"].append(combo_index)
    state["last_request_id"] = request_id


def validate_combo(combat, request_id, combo_index, phase):
    if combat.is_combo_window_open():
        state["runtime_windows"].add(combo_index)

    if combo_index == 1 and combat.is_combo_window_open() and not state["clicked_window"]:
        old_montage = state["montages"][0]
        next_montage = state["montages"][1]
        if not combat.request_attack():
            finish(False, "Attack01 ComboWindow rejected Attack02")
            return
        next_request_id = combat.get_attack_request_id()
        if next_request_id == request_id or combat.get_current_combo_index() != 2:
            finish(False, "ComboWindow input did not transition immediately to Attack02")
            return
        if state["anim_instance"].montage_is_playing(old_montage):
            finish(False, "Attack01 Montage still played after immediate transition")
            return
        if not state["anim_instance"].montage_is_playing(next_montage):
            finish(False, "Attack02 Montage was not playing in the transition call")
            return
        state["clicked_window"] = True
        return

    if combo_index == 2:
        if (
            combat.is_attacking()
            and not combat.is_combo_window_open()
            and not combat.is_attack_input_buffered()
        ):
            if not combat.request_attack():
                finish(False, "Attack02 rejected pre-window Attack03 buffer")
                return
            if combat.get_attack_request_id() != request_id:
                finish(False, "pre-window input transitioned before ComboWindow opened")
                return
            if not combat.is_attack_input_buffered():
                finish(False, "pre-window input was not stored in the configured buffer")
                return
            state["buffered_attack02"] = True
            state["attack02_buffer_probe_count"] += 1
            return

    if state["buffered_attack02"] and combo_index == 3:
        if not state["buffer_consumed"]:
            if combat.is_attack_input_buffered():
                finish(False, "Attack02 buffered input was not consumed by ComboWindow")
                return
            state["buffer_consumed"] = True

        throw_phase = str(combat.get_third_attack_weapon_throw_phase()).upper()
        if (
            combat.is_attacking()
            and not combat.is_combo_window_open()
            and not combat.is_resonance_window_open()
            and not combat.is_resonance_trigger_window_open()
            and not state["buffered_attack03"]
            and any(name in throw_phase for name in ("OUTBOUND", "SPINNING"))
            and "ACTIVE" in phase
        ):
            if not combat.request_attack():
                finish(False, "Attack03 rejected pre-window Attack04 buffer")
                return
            if combat.get_attack_request_id() != request_id:
                finish(False, "Attack03 pre-window input transitioned before ComboWindow")
                return
            if combat.get_current_combo_index() != 3 or not combat.is_attack_input_buffered():
                finish(False, "Attack03 pre-window input was not stored in the buffer")
                return
            state["buffered_attack03"] = True
            return

    if state["buffered_attack03"] and combo_index == 4 and not state["transitioned_attack04"]:
        old_montage = state["montages"][2]
        next_montage = state["montages"][3]
        if combat.is_attack_input_buffered():
            finish(False, "Attack03 buffered input was not consumed by ComboWindow")
            return
        if state["anim_instance"].montage_is_playing(old_montage):
            finish(False, "Attack03 Montage still played after buffered Attack04 transition")
            return
        if not state["anim_instance"].montage_is_playing(next_montage):
            finish(False, "Attack04 Montage was not playing after buffered transition")
            return
        state["transitioned_attack04"] = True
        return

    if combat.is_attacking():
        return
    if state["combo_indices"] != [1, 2, 3, 4]:
        finish(False, f"combo order={state['combo_indices']} expected=[1, 2, 3, 4]")
        return
    if not all(
        (
            state["clicked_window"],
            state["buffer_consumed"],
            state["buffered_attack03"],
            state["transitioned_attack04"],
        )
    ):
        finish(False, "combo missed immediate, buffered, or Attack04 transition")
        return
    if not {1, 4}.issubset(state["runtime_windows"]):
        finish(False, f"runtime ComboWindow states={sorted(state['runtime_windows'])}")
        return
    if combo_index != 4 or combat.get_combo_reset_remaining() <= 0.0:
        finish(False, "Attack04 Montage end did not start the combo reset timer")
        return

    continuation_id = request_attack(1)
    if continuation_id <= 0:
        return
    state["continuation_request_id"] = continuation_id
    state["scenario"] = "continuation"


def validate_continuation(combat, request_id, combo_index):
    if combat.is_attacking():
        if request_id != state["continuation_request_id"] or combo_index != 1:
            finish(False, "configured reset window did not wrap Attack04 to Attack01")
        return

    if combo_index != 1 or combat.get_combo_reset_remaining() <= 0.0:
        finish(False, "continued Attack01 did not preserve its reset window after Montage end")
        return
    state["reset_started_world_time"] = unreal.GameplayStatics.get_time_seconds(
        state["world"]
    )
    state["scenario"] = "reset_wait"


def validate_reset_wait(combat, combo_index):
    if combo_index != -1:
        return
    elapsed = (
        unreal.GameplayStatics.get_time_seconds(state["world"])
        - state["reset_started_world_time"]
    )
    state["reset_elapsed"] = elapsed
    expected = state["combo_reset_duration"]
    lower_bound = max(0.0, expected - 0.12)
    upper_bound = expected + 0.20
    if not lower_bound <= elapsed <= upper_bound:
        finish(
            False,
            f"combo reset elapsed={elapsed:.3f}s expected around {expected:.3f}s",
        )
        return
    if combat.get_combo_reset_remaining() > 0.0:
        finish(False, "combo index reset while reset timer remained active")
        return
    dodge_request_id = request_attack(1)
    if dodge_request_id <= 0:
        return
    state["dodge_request_id"] = dodge_request_id
    state["scenario"] = "dodge"


def validate_dodge(combat, request_id, combo_index, phase):
    if request_id != state["dodge_request_id"] or combo_index != 1:
        finish(False, "dodge scenario lost its Attack01 request")
        return
    if "ACTIVE" not in phase or combat.is_combo_window_open():
        return
    if not state["anim_instance"].montage_is_playing(state["montages"][0]):
        return

    if state["dodge_buffer_started_world_time"] <= 0.0:
        if not combat.request_attack():
            finish(False, "Attack01 rejected the input-buffer expiry probe")
            return
        if combat.get_attack_request_id() != request_id or not combat.is_attack_input_buffered():
            finish(False, "pre-window expiry probe did not enter the input buffer")
            return
        state["dodge_buffer_started_world_time"] = unreal.GameplayStatics.get_time_seconds(
            state["world"]
        )
        return

    if combat.is_attack_input_buffered():
        return

    state["dodge_buffer_elapsed"] = (
        unreal.GameplayStatics.get_time_seconds(state["world"])
        - state["dodge_buffer_started_world_time"]
    )
    expected_buffer = state["input_buffer_duration"]
    if not max(0.0, expected_buffer - 0.08) <= state[
        "dodge_buffer_elapsed"
    ] <= expected_buffer + 0.15:
        finish(
            False,
            f"input buffer expired after {state['dodge_buffer_elapsed']:.3f}s "
            f"instead of configured {expected_buffer:.3f}s",
        )
        return

    if not combat.request_dodge_interrupt():
        finish(False, "Dodge interrupt request was rejected during Attack Active")
        return
    if combat.is_attacking() or combat.get_attack_request_id() != 0:
        finish(False, "Dodge did not cancel the active attack immediately")
        return
    if combat.get_current_combo_index() != -1 or combat.get_combo_reset_remaining() > 0.0:
        finish(False, "Dodge did not reset the combo index and timer")
        return
    if combat.is_attack_input_buffered() or combat.is_combo_window_open():
        finish(False, "Dodge left combo input state active")
        return
    if state["anim_instance"].montage_is_playing(state["montages"][0]):
        finish(False, "Dodge did not stop the Attack Montage immediately")
        return
    if state["pawn"].is_combat_weapon_visible() or combat.is_weapon_trace_active():
        finish(False, "Dodge left the weapon or Trace active")
        return
    if state["locomotion"].get_combat_movement_restriction_request_id() != 0:
        finish(False, "Dodge leaked the combat movement restriction")
        return

    finish(
        True,
        "window_transition=immediate "
        f"input_buffer=consumed/expired@{state['dodge_buffer_elapsed']:.3f}s "
        f"combo_reset={state['reset_elapsed']:.3f}s/index=-1 "
        "continuation=Attack04->Attack01 dodge_interrupt=immediate/reset",
    )


def validate_runtime_tick():
    combat = state["combat"]
    request_id = combat.get_attack_request_id()
    combo_index = combat.get_current_combo_index()
    phase = phase_name(combat)
    observe_new_request(request_id, combo_index)
    if state["result"] is not None:
        return

    playing = tuple(
        index + 1
        for index, montage in enumerate(state["montages"])
        if state["anim_instance"].montage_is_playing(montage)
    )
    snapshot = (
        state["scenario"],
        request_id,
        combo_index,
        phase,
        combat.is_combo_window_open(),
        combat.is_attack_input_buffered(),
        playing,
    )
    if snapshot != state["last_snapshot"]:
        state["last_snapshot"] = snapshot
        unreal.log(f"ROVER_ATTACK_COMBO_SNAPSHOT {snapshot}")
    state["last"] = str(snapshot)

    if state["scenario"] == "combo":
        validate_combo(combat, request_id, combo_index, phase)
    elif state["scenario"] == "continuation":
        validate_continuation(combat, request_id, combo_index)
    elif state["scenario"] == "reset_wait":
        validate_reset_wait(combat, combo_index)
    else:
        validate_dodge(combat, request_id, combo_index, phase)


def on_tick(_delta_seconds):
    try:
        now = time.monotonic()
        if state["mode"] == "stopping":
            if (
                not level_editor.is_in_play_in_editor()
                and unreal_editor.get_game_world() is None
            ):
                shutdown()
            elif now >= state["deadline"]:
                state["result"] = (False, "PIE did not stop before timeout")
                shutdown()
            return

        world = unreal_editor.get_game_world()
        if state["mode"] == "starting" and level_editor.is_in_play_in_editor() and world:
            begin_runtime_validation(world)
        elif state["mode"] == "running":
            validate_runtime_tick()

        if state["result"] is None and now >= state["deadline"]:
            finish(False, f"validation timeout; last={state['last']}")
    except Exception as error:
        finish(False, f"exception={error!r}; last={state['last']}")


validate_asset_settings()
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
tick_handle = unreal.register_slate_post_tick_callback(on_tick)
if not unreal.RoverEditorTestLibrary.request_play_in_new_window():
    unreal.unregister_slate_post_tick_callback(tick_handle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    raise RuntimeError("Unable to request a new-window PIE session")
