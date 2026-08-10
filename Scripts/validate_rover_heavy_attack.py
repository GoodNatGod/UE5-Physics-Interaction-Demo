import time

import unreal


SUCCESS_MARKER = "ROVER_HEAVY_ATTACK_PIE_OK"
FAILURE_MARKER = "ROVER_HEAVY_ATTACK_PIE_FAIL"
CONFIG_PATH = "/Game/Rover/Combat/DA_RoverCombatConfig"
ATTACK01_PATH = "/Game/Rover/Combat/Montages/AM_Rover_Attack01"
ATTACK02_PATH = "/Game/Rover/Combat/Montages/AM_Rover_Attack02"
ATTACK05_PATH = "/Game/Rover/Combat/Animations/Attack05"
HEAVY_MONTAGE_PATH = "/Game/Rover/Combat/Montages/AM_Rover_Attack05"
START_TIMEOUT = 30.0
SCENARIO_TIMEOUT = 30.0
STOP_TIMEOUT = 15.0


level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
unreal_editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
tick_handle = None
state = {
    "mode": "starting",
    "scenario": "tap_hold",
    "deadline": time.monotonic() + START_TIMEOUT,
    "result": None,
    "last": "PIE has not started",
    "world": None,
    "pawn": None,
    "combat": None,
    "locomotion": None,
    "enemy": None,
    "health": None,
    "weapon": None,
    "anim_instance": None,
    "attack01": None,
    "attack02": None,
    "heavy_montage": None,
    "hold_threshold": 0.0,
    "retreat_distance": 0.0,
    "heavy_damage": 0.0,
    "initial_enemy_health": 0.0,
    "tap_press_time": 0.0,
    "tap_release_delay": 0.0,
    "tap_request_id": 0,
    "chain_request_id": 0,
    "combat_hold_press_time": 0.0,
    "saw_tap_montage": False,
    "saw_chain_montage": False,
    "heavy_trigger_elapsed": 0.0,
    "heavy_request_id": 0,
    "heavy_start_location": None,
    "heavy_forward": None,
    "heavy_min_forward_delta": 0.0,
    "saw_heavy_montage": False,
    "saw_heavy_trace": False,
    "saw_heavy_weapon": False,
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


def planar_dot(left, right):
    return left.x * right.x + left.y * right.y


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
    combat = state["combat"]
    if combat is not None:
        combat.set_light_attack_held(False)
    state["result"] = (ok, detail)
    if level_editor.is_in_play_in_editor():
        state["mode"] = "stopping"
        state["deadline"] = time.monotonic() + STOP_TIMEOUT
        level_editor.editor_request_end_play()
    else:
        shutdown()


def validate_assets():
    config = unreal.load_asset(CONFIG_PATH)
    attack01 = unreal.load_asset(ATTACK01_PATH)
    attack02 = unreal.load_asset(ATTACK02_PATH)
    sequence = unreal.load_asset(ATTACK05_PATH)
    heavy_montage = unreal.load_asset(HEAVY_MONTAGE_PATH)
    if not isinstance(config, unreal.RoverCombatConfig):
        raise RuntimeError(f"Missing combat config: {CONFIG_PATH}")
    if not isinstance(attack01, unreal.AnimMontage):
        raise RuntimeError(f"Missing Attack01 Montage: {ATTACK01_PATH}")
    if not isinstance(attack02, unreal.AnimMontage):
        raise RuntimeError(f"Missing Attack02 Montage: {ATTACK02_PATH}")
    if not isinstance(sequence, unreal.AnimSequence):
        raise RuntimeError(f"Missing Attack05 sequence: {ATTACK05_PATH}")
    if not isinstance(heavy_montage, unreal.AnimMontage):
        raise RuntimeError(f"Missing heavy Montage: {HEAVY_MONTAGE_PATH}")

    settings = config.get_editor_property("settings")
    hold_threshold = float(
        settings.get_editor_property("heavy_attack_hold_threshold")
    )
    retreat_distance = float(
        settings.get_editor_property("heavy_attack_retreat_distance")
    )
    retreat_duration = float(
        settings.get_editor_property("heavy_attack_retreat_duration")
    )
    heavy = settings.get_editor_property("heavy_attack_definition")
    heavy_damage = float(heavy.get_editor_property("damage"))
    configured_montage = heavy.get_editor_property("montage")
    if not 0.05 <= hold_threshold <= 0.30:
        raise RuntimeError(f"Heavy hold threshold is invalid: {hold_threshold:.3f}s")
    if retreat_distance <= 0.0 or retreat_duration <= 0.0:
        raise RuntimeError(
            f"Heavy retreat is invalid: {retreat_distance:.1f}cm/{retreat_duration:.3f}s"
        )
    if heavy_damage <= 0.0:
        raise RuntimeError(f"Heavy damage must be positive, got={heavy_damage:.1f}")
    if object_path(configured_montage) != object_path(heavy_montage):
        raise RuntimeError(
            f"Heavy definition montage={object_path(configured_montage)} "
            f"expected={object_path(heavy_montage)}"
        )
    if sequence.get_editor_property("enable_root_motion"):
        raise RuntimeError("Attack05 Root Motion is enabled")
    if not sequence.get_editor_property("force_root_lock"):
        raise RuntimeError("Attack05 Force Root Lock is disabled")

    state.update(
        {
            "attack01": attack01,
            "attack02": attack02,
            "heavy_montage": heavy_montage,
            "hold_threshold": hold_threshold,
            "retreat_distance": retreat_distance,
            "heavy_damage": heavy_damage,
        }
    )
    unreal.log(
        "ROVER_HEAVY_ATTACK_CONFIG "
        f"hold={hold_threshold:.3f}s retreat={retreat_distance:.1f}cm/"
        f"{retreat_duration:.3f}s play_rate="
        f"{float(heavy.get_editor_property('anim_play_rate')):.2f}"
    )


def begin_runtime(world):
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
    weapon = pawn.get_combat_weapon()
    mesh = pawn.get_component_by_class(unreal.SkeletalMeshComponent)
    anim_instance = mesh.get_anim_instance() if mesh else None
    enemies = unreal.GameplayStatics.get_all_actors_with_tag(
        world, "RoverP0TrainingEnemy"
    )
    enemy = next((actor for actor in enemies if actor), None)
    health = enemy.get_health_component() if enemy else None
    if any(
        value is None
        for value in (combat, locomotion, weapon, anim_instance, enemy, health)
    ):
        finish(False, "player is missing heavy-attack runtime objects")
        return
    if combat.is_attacking() or combat.get_current_combo_index() != -1:
        finish(False, "combat did not start from a clean idle state")
        return

    pawn.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0), False)
    locomotion.set_move_input(
        unreal.Vector2D(0.0, 0.0), unreal.Vector(0.0, 0.0, 0.0)
    )
    health.reset_health()
    initial_enemy_health = float(health.get_current_health())
    enemy.set_actor_location(
        pawn.get_actor_location() + unreal.Vector(600.0, 600.0, 0.0),
        False,
        True,
    )
    state.update(
        {
            "world": world,
            "pawn": pawn,
            "combat": combat,
            "locomotion": locomotion,
            "enemy": enemy,
            "health": health,
            "weapon": weapon,
            "anim_instance": anim_instance,
            "initial_enemy_health": initial_enemy_health,
        }
    )
    if not combat.begin_attack_input():
        finish(False, "idle tap press was rejected")
        return
    if not combat.is_attack_input_decision_pending() or combat.get_attack_request_id() != 0:
        finish(False, "idle tap did not enter a request-free decision window")
        return
    state["tap_press_time"] = unreal.GameplayStatics.get_time_seconds(world)
    state["mode"] = "running"
    state["deadline"] = time.monotonic() + SCENARIO_TIMEOUT


def validate_tap_hold(combat):
    now = unreal.GameplayStatics.get_time_seconds(state["world"])
    elapsed = now - state["tap_press_time"]
    release_at = min(0.05, state["hold_threshold"] * 0.45)
    if elapsed < release_at:
        if combat.get_attack_request_id() != 0 or combat.is_attacking():
            finish(False, f"tap started an attack before release at {elapsed:.3f}s")
        return

    if elapsed >= state["hold_threshold"]:
        finish(False, f"tap release missed hold threshold at {elapsed:.3f}s")
        return
    if not combat.end_attack_input():
        finish(False, "tap release did not request Attack01")
        return
    request_id = combat.get_attack_request_id()
    if (
        request_id <= 0
        or combat.get_current_combo_index() != 1
        or not attack_type_contains(combat, "LIGHT_ATTACK")
    ):
        finish(False, "tap release produced an invalid light-attack state")
        return
    state["tap_release_delay"] = elapsed
    state["tap_request_id"] = request_id
    state["scenario"] = "tap_chain"


def validate_tap_chain(combat):
    anim_instance = state["anim_instance"]
    if anim_instance.montage_is_playing(state["attack01"]):
        state["saw_tap_montage"] = True
    if not state["saw_tap_montage"]:
        return
    if combat.get_current_combo_index() == 1 and combat.is_combo_window_open():
        previous_id = combat.get_attack_request_id()
        if not combat.begin_attack_input():
            finish(False, "ComboWindow press was rejected")
            return
        next_id = combat.get_attack_request_id()
        if (
            next_id <= previous_id
            or combat.get_current_combo_index() != 2
            or not combat.is_attack_input_decision_pending()
        ):
            finish(
                False,
                "ComboWindow press did not route Attack02 while tracking the hold",
            )
            return
        state["chain_request_id"] = next_id
        state["combat_hold_press_time"] = unreal.GameplayStatics.get_time_seconds(
            state["world"]
        )
        state["scenario"] = "combat_hold"
        return


def validate_combat_hold(combat):
    anim_instance = state["anim_instance"]
    if anim_instance.montage_is_playing(state["attack02"]):
        state["saw_chain_montage"] = True
    if not state["saw_chain_montage"]:
        return
    elapsed = (
        unreal.GameplayStatics.get_time_seconds(state["world"])
        - state["combat_hold_press_time"]
    )
    request_id = combat.get_attack_request_id()
    if request_id == state["chain_request_id"]:
        if elapsed > state["hold_threshold"] + 0.10:
            finish(False, f"combat hold produced no heavy request after {elapsed:.3f}s")
        return
    if request_id <= state["chain_request_id"]:
        finish(False, f"combat hold produced invalid request={request_id}")
        return
    state["heavy_trigger_elapsed"] = elapsed
    state["heavy_request_id"] = request_id
    if elapsed < state["hold_threshold"] - 0.03:
        finish(False, f"heavy triggered too early at {elapsed:.3f}s")
        return
    if (
        not attack_type_contains(combat, "HEAVY_ATTACK")
        or combat.get_current_combo_index() != 0
        or combat.is_attack_input_decision_pending()
    ):
        finish(False, "hold threshold produced an invalid heavy-attack state")
        return
    combat.end_attack_input()
    state["heavy_start_location"] = state["pawn"].get_actor_location()
    state["heavy_forward"] = state["pawn"].get_actor_forward_vector()
    state["scenario"] = "heavy_attack"


def validate_heavy_attack(combat):
    location_delta = state["pawn"].get_actor_location() - state["heavy_start_location"]
    forward_delta = planar_dot(location_delta, state["heavy_forward"])
    state["heavy_min_forward_delta"] = min(
        state["heavy_min_forward_delta"], forward_delta
    )
    if state["anim_instance"].montage_is_playing(state["heavy_montage"]):
        state["saw_heavy_montage"] = True
    if state["pawn"].is_combat_weapon_visible():
        state["saw_heavy_weapon"] = True
    if combat.is_weapon_trace_active():
        state["saw_heavy_trace"] = True
        trace_base = state["weapon"].get_socket_location("WeaponTraceBase")
        trace_tip = state["weapon"].get_socket_location("WeaponTraceTip")
        state["enemy"].set_actor_location(
            (trace_base + trace_tip) * 0.5, False, True
        )

    if combat.is_attacking():
        return
    if not state["saw_heavy_montage"]:
        finish(False, "Attack05 Montage was never observed")
        return
    if not state["saw_heavy_weapon"] or not state["saw_heavy_trace"]:
        finish(False, "heavy attack did not expose its weapon/Trace window")
        return
    expected_health = max(
        0.0, state["initial_enemy_health"] - state["heavy_damage"]
    )
    actual_health = float(state["health"].get_current_health())
    if abs(actual_health - expected_health) > 0.01:
        finish(
            False,
            f"heavy damage health={actual_health:.1f} expected={expected_health:.1f}",
        )
        return
    minimum_retreat = state["retreat_distance"] * 0.55
    actual_retreat = -state["heavy_min_forward_delta"]
    if actual_retreat < minimum_retreat:
        finish(
            False,
            f"heavy retreat={actual_retreat:.1f}cm expected>={minimum_retreat:.1f}cm",
        )
        return
    if not attack_type_contains(combat, "HEAVY_ATTACK", previous=True):
        finish(False, "natural heavy completion did not retain PreviousAttackType")
        return
    if state["pawn"].is_combat_weapon_visible() or combat.is_weapon_trace_active():
        finish(False, "heavy completion left weapon presentation active")
        return
    if state["locomotion"].get_combat_movement_restriction_request_id() != 0:
        finish(False, "heavy completion leaked its movement restriction")
        return
    finish(
        True,
        f"tap_release={state['tap_release_delay']:.3f}s->Attack01 "
        "combo_input=immediate->Attack02 "
        f"combat_hold={state['heavy_trigger_elapsed']:.3f}s->HeavyAttack "
        f"retreat={actual_retreat:.1f}cm damage={state['heavy_damage']:.1f} "
        "montage=Attack05 trace=hit_once",
    )


def validate_runtime_tick():
    combat = state["combat"]
    snapshot = (
        state["scenario"],
        combat.get_attack_request_id(),
        combat.get_current_combo_index(),
        str(combat.get_current_attack_type()),
        str(combat.get_combat_phase()),
        combat.is_attack_input_decision_pending(),
    )
    if snapshot != state["last_snapshot"]:
        state["last_snapshot"] = snapshot
        unreal.log(f"ROVER_HEAVY_ATTACK_SNAPSHOT {snapshot}")
    state["last"] = str(snapshot)

    if state["scenario"] == "tap_hold":
        validate_tap_hold(combat)
    elif state["scenario"] == "tap_chain":
        validate_tap_chain(combat)
    elif state["scenario"] == "combat_hold":
        validate_combat_hold(combat)
    else:
        validate_heavy_attack(combat)


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
