import time

import unreal


SUCCESS_MARKER = "ROVER_AIR_ATTACK_PIE_OK"
FAILURE_MARKER = "ROVER_AIR_ATTACK_PIE_FAIL"
CONFIG_PATH = "/Game/Rover/Combat/DA_RoverCombatConfig"
MONTAGE_PATH = "/Game/Rover/Combat/Montages/AM_Rover_AirAttack"
SEQUENCE_PATHS = tuple(
    f"/Game/Rover/Combat/Animations/AirAttack_{phase}"
    for phase in ("Start", "Loop", "End")
)
START_TIMEOUT = 30.0
SCENARIO_TIMEOUT = 20.0
STOP_TIMEOUT = 15.0
TEST_HEIGHT = 3000.0


level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
unreal_editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
tick_handle = None
state = {
    "mode": "starting",
    "scenario": "wait_for_airborne",
    "deadline": time.monotonic() + START_TIMEOUT,
    "result": None,
    "last": "PIE has not started",
    "world": None,
    "pawn": None,
    "movement": None,
    "locomotion": None,
    "combat": None,
    "anim_instance": None,
    "montage": None,
    "ascent_height": 0.0,
    "apex_frame": 0,
    "descent_speed": 0.0,
    "request_id": 0,
    "saw_montage": False,
    "saw_weapon": False,
    "saw_trace": False,
    "saw_loop": False,
    "saw_end": False,
    "saw_ascent": False,
    "saw_descent_after_ascent": False,
    "attack_start_z": 0.0,
    "maximum_z": 0.0,
    "maximum_vertical_speed": 0.0,
    "minimum_vertical_speed": 0.0,
    "last_snapshot": None,
}


def object_path(value):
    return value.get_path_name() if value else "None"


def attack_type_contains(combat, expected, previous=False):
    value = (
        combat.get_previous_attack_type()
        if previous
        else combat.get_current_attack_type()
    )
    return expected in str(value).upper()


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


def validate_assets():
    config = unreal.load_asset(CONFIG_PATH)
    montage = unreal.load_asset(MONTAGE_PATH)
    sequences = tuple(unreal.load_asset(path) for path in SEQUENCE_PATHS)
    if not isinstance(config, unreal.RoverCombatConfig):
        raise RuntimeError(f"Missing combat config: {CONFIG_PATH}")
    if not isinstance(montage, unreal.AnimMontage):
        raise RuntimeError(f"Missing AirAttack Montage: {MONTAGE_PATH}")
    if not all(isinstance(sequence, unreal.AnimSequence) for sequence in sequences):
        raise RuntimeError(f"Missing AirAttack sequences: {SEQUENCE_PATHS}")

    settings = config.get_editor_property("settings")
    definition = settings.get_editor_property("air_attack_definition")
    configured_montage = definition.get_editor_property("montage")
    ascent_height = float(settings.get_editor_property("air_attack_ascent_height"))
    apex_frame = int(settings.get_editor_property("air_attack_apex_frame"))
    descent_speed = float(settings.get_editor_property("air_attack_descent_speed"))
    horizontal_scale = float(
        settings.get_editor_property("air_attack_horizontal_velocity_scale")
    )
    maximum_duration = float(
        settings.get_editor_property("air_attack_maximum_duration")
    )
    if object_path(configured_montage) != object_path(montage):
        raise RuntimeError(
            f"AirAttack definition montage={object_path(configured_montage)} "
            f"expected={object_path(montage)}"
        )
    if (
        ascent_height <= 0.0
        or apex_frame <= 0
        or descent_speed <= 0.0
        or not 0.0 <= horizontal_scale <= 1.0
    ):
        raise RuntimeError(
            f"Invalid AirAttack movement ascent={ascent_height:.1f} "
            f"apex_frame={apex_frame} descent={descent_speed:.1f} "
            f"horizontal_scale={horizontal_scale:.2f}"
        )
    if maximum_duration <= 0.0:
        raise RuntimeError(f"Invalid AirAttack watchdog={maximum_duration:.2f}s")
    for path, sequence in zip(SEQUENCE_PATHS, sequences):
        if sequence.get_editor_property("enable_root_motion"):
            raise RuntimeError(f"Root Motion is enabled: {path}")
        if not sequence.get_editor_property("force_root_lock"):
            raise RuntimeError(f"Force Root Lock is disabled: {path}")

    state["montage"] = montage
    state["ascent_height"] = ascent_height
    state["apex_frame"] = apex_frame
    state["descent_speed"] = descent_speed
    unreal.log(
        "ROVER_AIR_ATTACK_CONFIG "
        f"ascent={ascent_height:.1f}cm apex_frame={apex_frame} "
        f"descent={descent_speed:.1f} horizontal_scale={horizontal_scale:.2f} "
        f"watchdog={maximum_duration:.2f}s damage="
        f"{float(definition.get_editor_property('damage')):.1f}"
    )


def begin_runtime(world):
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    if pawn is None:
        state["last"] = "waiting for player pawn"
        return
    movement = pawn.get_component_by_class(unreal.CharacterMovementComponent)
    combat = pawn.get_combat_component()
    locomotion = pawn.get_locomotion_component()
    mesh = pawn.get_component_by_class(unreal.SkeletalMeshComponent)
    anim_instance = mesh.get_anim_instance() if mesh else None
    if any(value is None for value in (movement, combat, locomotion, anim_instance)):
        finish(False, "player is missing AirAttack runtime objects")
        return
    if not movement.is_moving_on_ground() or combat.is_attacking():
        state["last"] = "waiting for a clean grounded player"
        return

    locomotion.set_move_input(
        unreal.Vector2D(0.0, 0.0), unreal.Vector(0.0, 0.0, 0.0)
    )
    state.update(
        {
            "world": world,
            "pawn": pawn,
            "movement": movement,
            "locomotion": locomotion,
            "combat": combat,
            "anim_instance": anim_instance,
        }
    )
    if not locomotion.try_jump():
        state["last"] = "waiting for locomotion to accept the AirAttack setup jump"
        return
    state["mode"] = "running"
    state["deadline"] = time.monotonic() + SCENARIO_TIMEOUT


def start_air_attack():
    pawn = state["pawn"]
    movement = state["movement"]
    combat = state["combat"]
    if not movement.is_falling() or pawn.get_velocity().z >= -100.0:
        return
    location = pawn.get_actor_location()
    pawn.set_actor_location(location + unreal.Vector(0.0, 0.0, TEST_HEIGHT), False, True)
    if not combat.begin_attack_input():
        finish(False, "airborne attack input was rejected")
        return
    combat.end_attack_input()
    request_id = combat.get_attack_request_id()
    if (
        request_id <= 0
        or not attack_type_contains(combat, "AIR_ATTACK")
        or combat.get_current_combo_index() != -1
        or combat.is_attack_input_decision_pending()
    ):
        finish(False, "airborne input did not route directly to AirAttack")
        return
    if state["locomotion"].get_combat_movement_restriction_request_id() != request_id:
        finish(False, "AirAttack did not acquire its movement restriction")
        return
    state["request_id"] = request_id
    state["attack_start_z"] = pawn.get_actor_location().z
    state["maximum_z"] = state["attack_start_z"]
    state["scenario"] = "air_attack"


def validate_air_attack():
    combat = state["combat"]
    movement = state["movement"]
    pawn = state["pawn"]
    anim_instance = state["anim_instance"]
    montage = state["montage"]
    velocity = pawn.get_velocity()
    location_z = pawn.get_actor_location().z
    state["maximum_z"] = max(state["maximum_z"], location_z)
    state["maximum_vertical_speed"] = max(
        state["maximum_vertical_speed"], velocity.z
    )
    state["minimum_vertical_speed"] = min(state["minimum_vertical_speed"], velocity.z)
    if velocity.z > 1.0 and location_z > state["attack_start_z"] + 1.0:
        state["saw_ascent"] = True
    if state["saw_ascent"] and velocity.z <= -state["descent_speed"] * 0.80:
        state["saw_descent_after_ascent"] = True
    if anim_instance.montage_is_playing(montage):
        state["saw_montage"] = True
        section = str(anim_instance.montage_get_current_section(montage))
        state["saw_loop"] = state["saw_loop"] or section == "Loop"
        state["saw_end"] = state["saw_end"] or section == "End"
    if pawn.is_combat_weapon_visible():
        state["saw_weapon"] = True
    if combat.is_weapon_trace_active():
        state["saw_trace"] = True

    if not combat.is_attacking():
        if not state["saw_montage"]:
            finish(False, "AirAttack Montage was never observed")
            return
        if not state["saw_loop"] or not state["saw_end"]:
            finish(
                False,
                f"AirAttack section flow loop={state['saw_loop']} end={state['saw_end']}",
            )
            return
        if not state["saw_weapon"] or not state["saw_trace"]:
            finish(False, "AirAttack did not expose its weapon/Trace window")
            return
        measured_ascent = state["maximum_z"] - state["attack_start_z"]
        if not state["saw_ascent"] or measured_ascent < state["ascent_height"] * 0.50:
            finish(
                False,
                f"AirAttack ascent={measured_ascent:.1f}cm "
                f"expected>={state['ascent_height'] * 0.50:.1f}cm "
                f"max_vz={state['maximum_vertical_speed']:.1f}",
            )
            return
        if not state["saw_descent_after_ascent"]:
            finish(False, "AirAttack never entered its plunge after the ascent")
            return
        if state["minimum_vertical_speed"] > -state["descent_speed"] * 0.80:
            finish(
                False,
                f"AirAttack descent={state['minimum_vertical_speed']:.1f} "
                f"expected<=-{state['descent_speed'] * 0.80:.1f}",
            )
            return
        if movement.is_falling() or not movement.is_moving_on_ground():
            finish(False, "AirAttack completed before reaching the ground")
            return
        if not attack_type_contains(combat, "AIR_ATTACK", previous=True):
            finish(False, "AirAttack completion did not retain PreviousAttackType")
            return
        if pawn.is_combat_weapon_visible() or combat.is_weapon_trace_active():
            finish(False, "AirAttack completion left weapon presentation active")
            return
        if state["locomotion"].get_combat_movement_restriction_request_id() != 0:
            finish(False, "AirAttack completion leaked its movement restriction")
            return
        finish(
            True,
            f"input=airborne->AirAttack sections=Start/Loop/End "
            f"ascent={measured_ascent:.1f}cm apex_frame={state['apex_frame']} "
            f"descent={state['minimum_vertical_speed']:.1f}cm/s trace=active "
            "landing=completed restriction=released",
        )


def validate_runtime_tick():
    combat = state["combat"]
    snapshot = (
        state["scenario"],
        combat.get_attack_request_id(),
        str(combat.get_current_attack_type()),
        str(combat.get_combat_phase()),
        state["movement"].is_falling(),
        combat.has_air_attack_landed(),
    )
    if snapshot != state["last_snapshot"]:
        state["last_snapshot"] = snapshot
        unreal.log(f"ROVER_AIR_ATTACK_SNAPSHOT {snapshot}")
    state["last"] = str(snapshot)
    if state["scenario"] == "wait_for_airborne":
        start_air_attack()
    else:
        validate_air_attack()


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
            begin_runtime(world)
        elif state["mode"] == "running":
            validate_runtime_tick()
        if state["result"] is None and now >= state["deadline"]:
            finish(False, f"validation timeout; last={state['last']}")
    except Exception as error:
        finish(False, f"exception={error!r}; last={state['last']}")


validate_assets()
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
tick_handle = unreal.register_slate_post_tick_callback(on_tick)
if not unreal.RoverEditorTestLibrary.request_play_in_new_window():
    unreal.unregister_slate_post_tick_callback(tick_handle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    raise RuntimeError("Unable to request a new-window PIE session")
