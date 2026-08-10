import time

import unreal


SUCCESS_MARKER = "ROVER_HEAVY_RESONANCE_PIE_OK"
FAILURE_MARKER = "ROVER_HEAVY_RESONANCE_PIE_FAIL"
CONFIG_PATH = "/Game/Rover/Combat/DA_RoverCombatConfig"
RESONANCE_MONTAGE_PATH = "/Game/Rover/Combat/Montages/AM_Rover_Attack_EX01"
ATTACK01_MONTAGE_PATH = "/Game/Rover/Combat/Montages/AM_Rover_Attack01"
START_TIMEOUT = 30.0
SCENARIO_TIMEOUT = 50.0
STOP_TIMEOUT = 15.0


level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
unreal_editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
tick_handle = None
state = {
    "mode": "starting",
    "scenario": "starting",
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
    "resonance_montage": None,
    "attack01_montage": None,
    "resonance_damage": 0.0,
    "dash_distance": 0.0,
    "hold_threshold": 0.0,
    "last_chain_request": 0,
    "saw_attack3_early_window": False,
    "throw_phases": set(),
    "saw_throw_detached": False,
    "saw_throw_returned": False,
    "route": "",
    "route_request": 0,
    "route_health": 0.0,
    "route_start": None,
    "route_forward": None,
    "route_max_forward": 0.0,
    "route_saw_montage": False,
    "route_saw_trace": False,
    "route_saw_weapon": False,
    "attack3_hold_dash": 0.0,
    "attack3_hold_request": 0,
    "attack3_hold_press_time": 0.0,
    "attack3_hold_trigger_elapsed": 0.0,
    "loop_request": 0,
    "loop_saw_montage": False,
    "heavy_dash": 0.0,
    "heavy_request": 0,
    "last_snapshot": None,
}


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
    montage = unreal.load_asset(RESONANCE_MONTAGE_PATH)
    attack01_montage = unreal.load_asset(ATTACK01_MONTAGE_PATH)
    if not isinstance(config, unreal.RoverCombatConfig):
        raise RuntimeError(f"Missing combat config: {CONFIG_PATH}")
    if not isinstance(montage, unreal.AnimMontage):
        raise RuntimeError(f"Missing Heavy Resonance Montage: {RESONANCE_MONTAGE_PATH}")
    if not isinstance(attack01_montage, unreal.AnimMontage):
        raise RuntimeError(f"Missing Attack01 Montage: {ATTACK01_MONTAGE_PATH}")
    settings = config.get_editor_property("settings")
    definition = settings.get_editor_property("heavy_resonance_definition")
    configured_montage = definition.get_editor_property("montage")
    if configured_montage.get_path_name() != montage.get_path_name():
        raise RuntimeError("HeavyResonanceDefinition references the wrong Montage")
    damage = float(definition.get_editor_property("damage"))
    dash_distance = float(settings.get_editor_property("resonance_dash_distance"))
    dash_duration = float(settings.get_editor_property("resonance_dash_duration"))
    trigger_duration = float(
        settings.get_editor_property("resonance_trigger_window_duration")
    )
    hold_threshold = float(settings.get_editor_property("heavy_attack_hold_threshold"))
    blend_out = float(definition.get_editor_property("montage_blend_out_time"))
    blend_trigger = float(
        definition.get_editor_property("montage_blend_out_trigger_time")
    )
    montage_blend_out = float(montage.get_default_blend_out_time())
    montage_blend_trigger = float(
        montage.get_editor_property("blend_out_trigger_time")
    )
    if damage <= 0.0 or dash_distance <= 0.0 or dash_duration <= 0.0:
        raise RuntimeError("Heavy Resonance damage or dash settings are invalid")
    if trigger_duration <= 0.0:
        raise RuntimeError("Heavy Resonance follow-up window is disabled")
    if (
        blend_out < 0.2
        or abs(blend_trigger - blend_out) > 0.01
        or abs(montage_blend_out - blend_out) > 0.01
        or abs(montage_blend_trigger - blend_trigger) > 0.01
    ):
        raise RuntimeError(
            "Heavy Resonance full-body blend-out is invalid: "
            f"definition={blend_out:.3f}/{blend_trigger:.3f}s "
            f"montage={montage_blend_out:.3f}/{montage_blend_trigger:.3f}s"
        )
    state.update(
        {
            "resonance_montage": montage,
            "attack01_montage": attack01_montage,
            "resonance_damage": damage,
            "dash_distance": dash_distance,
            "hold_threshold": hold_threshold,
            "resonance_blend_out": blend_out,
        }
    )
    unreal.log(
        "ROVER_HEAVY_RESONANCE_CONFIG "
        f"half={settings.get_editor_property('resonance_half_window_normalized'):.2f} "
        f"followup={trigger_duration:.3f}s dash={dash_distance:.1f}cm/"
        f"{dash_duration:.3f}s damage={damage:.1f} "
        f"play_rate={definition.get_editor_property('anim_play_rate'):.2f} "
        f"blend_out={blend_out:.3f}s"
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
        finish(False, "player is missing Heavy Resonance runtime objects")
        return
    if combat.is_attacking() or combat.get_current_combo_index() != -1:
        finish(False, "combat did not start from a clean idle state")
        return

    pawn.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0), False)
    locomotion.set_move_input(
        unreal.Vector2D(0.0, 0.0), unreal.Vector(0.0, 0.0, 0.0)
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
        }
    )
    if not combat.request_attack():
        finish(False, "Attack01 request was rejected")
        return
    state["scenario"] = "chain_to_attack3_hold"
    state["mode"] = "running"
    state["deadline"] = time.monotonic() + SCENARIO_TIMEOUT


def enter_resonance_runtime(source, previous_id):
    combat = state["combat"]
    request_id = combat.get_attack_request_id()
    expected_previous = (
        "LIGHT_ATTACK" if source.startswith("Attack03") else "HEAVY_ATTACK"
    )
    if (
        request_id <= previous_id
        or not attack_type_contains(combat, "HEAVY_RESONANCE")
        or not attack_type_contains(combat, expected_previous, previous=True)
        or combat.get_current_combo_index() != 0
        or combat.is_attack_input_decision_pending()
    ):
        finish(False, f"{source} input produced an invalid Resonance state")
        return False
    combat.end_attack_input()
    state.update(
        {
            "route": source,
            "route_request": request_id,
            "route_health": float(state["health"].get_current_health()),
            "route_start": state["pawn"].get_actor_location(),
            "route_forward": state["pawn"].get_actor_forward_vector(),
            "route_max_forward": 0.0,
            "route_saw_montage": False,
            "route_saw_trace": False,
            "route_saw_weapon": False,
            "scenario": "resonance_runtime",
        }
    )
    return True


def start_resonance_from_input(source):
    combat = state["combat"]
    previous_id = combat.get_attack_request_id()
    if not combat.begin_attack_input():
        finish(False, f"{source} Resonance input was rejected")
        return False
    return enter_resonance_runtime(source, previous_id)


def validate_chain_to_attack3():
    combat = state["combat"]
    combo_index = combat.get_current_combo_index()
    request_id = combat.get_attack_request_id()
    if combo_index in (1, 2) and combat.is_combo_window_open():
        if state["last_chain_request"] == request_id:
            return
        state["last_chain_request"] = request_id
        if not combat.request_attack():
            finish(False, f"Attack0{combo_index} did not advance")
        return
    if combo_index != 3 or not attack_type_contains(combat, "LIGHT_ATTACK"):
        return
    throw_active = combat.is_third_attack_weapon_throw_active()
    throw_phase = str(combat.get_third_attack_weapon_throw_phase()).upper()
    if throw_active:
        for phase_name in ("WAITING", "OUTBOUND", "SPINNING", "RETURNING"):
            if phase_name in throw_phase:
                state["throw_phases"].add(phase_name)
                break
        if any(name in throw_phase for name in ("OUTBOUND", "SPINNING", "RETURNING")):
            state["saw_throw_detached"] = True
    elif state["saw_throw_detached"]:
        attachment = str(state["weapon"].get_attach_socket_name())
        if attachment != "Bip001LHand" or state["weapon"].get_attach_parent() is None:
            finish(False, "Attack03 throw ended without returning to the left hand")
            return
        state["saw_throw_returned"] = True
    if combat.is_combo_window_open() and not combat.is_resonance_window_open():
        state["saw_attack3_early_window"] = True
    if (
        state["scenario"] == "chain_to_attack3_hold"
        and any(name in throw_phase for name in ("OUTBOUND", "SPINNING"))
        and "ACTIVE" in str(combat.get_combat_phase()).upper()
        and not combat.is_combo_window_open()
        and not combat.is_resonance_window_open()
        and not combat.is_resonance_trigger_window_open()
    ):
        state["attack3_hold_request"] = request_id
        state["attack3_hold_press_time"] = unreal.GameplayStatics.get_time_seconds(
            state["world"]
        )
        if not combat.begin_attack_input():
            finish(False, "Attack03 hold input was rejected")
            return
        if not combat.is_attack_input_decision_pending():
            finish(False, "Attack03 hold did not start a hold decision")
            return
        if attack_type_contains(combat, "HEAVY_ATTACK"):
            finish(False, "Attack03 hold immediately routed to Attack05")
            return
        state["scenario"] = "attack3_hold"
        return
    if combat.is_resonance_window_open() and state["saw_throw_returned"]:
        if not state["saw_attack3_early_window"]:
            finish(False, "Attack03 never exposed an early Attack04 window")
            return
        if state["throw_phases"] & {"OUTBOUND", "SPINNING", "RETURNING"} != {
            "OUTBOUND",
            "SPINNING",
            "RETURNING",
        }:
            finish(False, f"Attack03 throw phases={sorted(state['throw_phases'])}")
            return
        start_resonance_from_input("Attack03Late")


def validate_attack3_hold():
    combat = state["combat"]
    elapsed = (
        unreal.GameplayStatics.get_time_seconds(state["world"])
        - state["attack3_hold_press_time"]
    )
    if attack_type_contains(combat, "HEAVY_ATTACK"):
        finish(False, f"Attack03 hold routed to Attack05 after {elapsed:.3f}s")
        return
    if not attack_type_contains(combat, "HEAVY_RESONANCE"):
        if elapsed > state["hold_threshold"] + 0.20:
            finish(False, f"Attack03 hold produced no Resonance after {elapsed:.3f}s")
        return
    state["attack3_hold_trigger_elapsed"] = elapsed
    enter_resonance_runtime("Attack03Hold", state["attack3_hold_request"])


def validate_resonance_runtime():
    combat = state["combat"]
    pawn = state["pawn"]
    delta = pawn.get_actor_location() - state["route_start"]
    forward_delta = planar_dot(delta, state["route_forward"])
    state["route_max_forward"] = max(state["route_max_forward"], forward_delta)
    if state["anim_instance"].montage_is_playing(state["resonance_montage"]):
        state["route_saw_montage"] = True
    if pawn.is_combat_weapon_visible():
        state["route_saw_weapon"] = True
    if combat.is_weapon_trace_active():
        state["route_saw_trace"] = True
        trace_base = state["weapon"].get_socket_location("WeaponTraceBase")
        trace_tip = state["weapon"].get_socket_location("WeaponTraceTip")
        state["enemy"].set_actor_location((trace_base + trace_tip) * 0.5, False, True)

    if (
        state["route"] == "Attack03Hold"
        and attack_type_contains(combat, "HEAVY_RESONANCE")
        and "RECOVERY" in str(combat.get_combat_phase()).upper()
    ):
        if not state["route_saw_montage"]:
            finish(False, "Attack03 hold route never played EX01")
            return
        if not state["route_saw_weapon"] or not state["route_saw_trace"]:
            finish(False, "Attack03 hold route missed weapon/Trace presentation")
            return
        expected_health = max(
            0.0, state["route_health"] - state["resonance_damage"]
        )
        actual_health = float(state["health"].get_current_health())
        if abs(actual_health - expected_health) > 0.01:
            finish(
                False,
                f"Attack03 hold damage health={actual_health:.1f} expected={expected_health:.1f}",
            )
            return
        minimum_dash = state["dash_distance"] * 0.45
        if state["route_max_forward"] < minimum_dash:
            finish(
                False,
                f"Attack03 hold dash={state['route_max_forward']:.1f}cm expected>={minimum_dash:.1f}cm",
            )
            return
        previous_id = combat.get_attack_request_id()
        if not combat.begin_attack_input():
            finish(False, "HeavyResonance Recovery rejected Attack01 input")
            return
        loop_request = combat.get_attack_request_id()
        if (
            loop_request <= previous_id
            or not attack_type_contains(combat, "LIGHT_ATTACK")
            or combat.get_current_combo_index() != 1
            or not attack_type_contains(combat, "HEAVY_RESONANCE", previous=True)
        ):
            finish(False, "HeavyResonance Recovery did not transition to Attack01")
            return
        combat.end_attack_input()
        state["attack3_hold_dash"] = state["route_max_forward"]
        state["loop_request"] = loop_request
        state["loop_saw_montage"] = False
        state["scenario"] = "resonance_loop_attack1"
        return

    if combat.is_attacking():
        if not attack_type_contains(combat, "HEAVY_RESONANCE"):
            finish(False, f"{state['route']} Resonance changed attack type mid-Montage")
        return
    if not state["route_saw_montage"]:
        finish(False, f"{state['route']} route never played EX01")
        return
    if not state["route_saw_weapon"] or not state["route_saw_trace"]:
        finish(False, f"{state['route']} route missed weapon/Trace presentation")
        return
    expected_health = max(
        0.0, state["route_health"] - state["resonance_damage"]
    )
    actual_health = float(state["health"].get_current_health())
    if abs(actual_health - expected_health) > 0.01:
        finish(
            False,
            f"{state['route']} damage health={actual_health:.1f} expected={expected_health:.1f}",
        )
        return
    minimum_dash = state["dash_distance"] * 0.45
    if state["route_max_forward"] < minimum_dash:
        finish(
            False,
            f"{state['route']} dash={state['route_max_forward']:.1f}cm expected>={minimum_dash:.1f}cm",
        )
        return
    if not attack_type_contains(combat, "HEAVY_RESONANCE", previous=True):
        finish(False, f"{state['route']} completion lost PreviousAttackType")
        return
    if state["locomotion"].get_combat_movement_restriction_request_id() != 0:
        finish(False, f"{state['route']} completion leaked movement restriction")
        return
    if not state["anim_instance"].is_foot_stance_locked():
        finish(False, f"{state['route']} completion did not lock the post-attack stance")
        return
    expected_stance_alpha = (
        1.0 if state["anim_instance"].is_using_stand2() else 0.0
    )
    actual_stance_alpha = float(state["anim_instance"].get_idle_stance_alpha())
    if abs(actual_stance_alpha - expected_stance_alpha) > 0.01:
        finish(
            False,
            f"{state['route']} completion stance mismatch: "
            f"alpha={actual_stance_alpha:.3f} expected={expected_stance_alpha:.1f}",
        )
        return

    state["heavy_dash"] = state["route_max_forward"]
    finish(
        True,
        f"Attack03 hold={state['attack3_hold_trigger_elapsed']:.3f}s->HeavyResonance "
        "Attack05=blocked throw=cancelled "
        f"hold_dash={state['attack3_hold_dash']:.1f}cm "
        "HeavyResonance->Attack01 "
        "HeavyAttack Recovery=HeavyResonance "
        f"heavy_dash={state['heavy_dash']:.1f}cm damage={state['resonance_damage']:.1f} "
        f"blend_out={state['resonance_blend_out']:.3f}s "
        "montage=Attack_EX01 stance=locked trace=hit_once/route",
    )


def validate_resonance_loop_attack1():
    combat = state["combat"]
    if (
        combat.get_attack_request_id() != state["loop_request"]
        or not attack_type_contains(combat, "LIGHT_ATTACK")
        or combat.get_current_combo_index() != 1
    ):
        finish(False, "HeavyResonance loop did not remain on Attack01")
        return
    if state["anim_instance"].montage_is_playing(state["attack01_montage"]):
        state["loop_saw_montage"] = True
    if not state["loop_saw_montage"]:
        return

    far_location = state["pawn"].get_actor_location() + unreal.Vector(
        0.0, 3000.0, 0.0
    )
    state["enemy"].set_actor_location(far_location, False, True)
    if not combat.request_heavy_attack():
        finish(False, "direct HeavyAttack setup after Attack01 was rejected")
        return
    state["heavy_request"] = combat.get_attack_request_id()
    state["scenario"] = "heavy_recovery"


def validate_heavy_recovery():
    combat = state["combat"]
    if combat.get_attack_request_id() != state["heavy_request"]:
        finish(False, "HeavyAttack request changed before its Recovery")
        return
    if (
        attack_type_contains(combat, "HEAVY_ATTACK")
        and "RECOVERY" in str(combat.get_combat_phase()).upper()
        and combat.is_resonance_trigger_window_open()
    ):
        start_resonance_from_input("HeavyAttack")


def validate_runtime_tick():
    combat = state["combat"]
    snapshot = (
        state["scenario"],
        combat.get_attack_request_id(),
        combat.get_current_combo_index(),
        str(combat.get_current_attack_type()),
        str(combat.get_combat_phase()),
        combat.is_resonance_window_open(),
        combat.is_resonance_trigger_window_open(),
    )
    if snapshot != state["last_snapshot"]:
        state["last_snapshot"] = snapshot
        unreal.log(f"ROVER_HEAVY_RESONANCE_SNAPSHOT {snapshot}")
    state["last"] = str(snapshot)
    if state["scenario"] == "chain_to_attack3_hold":
        validate_chain_to_attack3()
    elif state["scenario"] == "attack3_hold":
        validate_attack3_hold()
    elif state["scenario"] == "resonance_loop_attack1":
        validate_resonance_loop_attack1()
    elif state["scenario"] == "heavy_recovery":
        validate_heavy_recovery()
    else:
        validate_resonance_runtime()


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
