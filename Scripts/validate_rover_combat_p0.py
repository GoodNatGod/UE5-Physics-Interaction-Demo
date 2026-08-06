import math
import time

import unreal


EXPECTED_PAWN = "/Script/RoverReplica.RoverCharacter"
EXPECTED_ENEMY = "/Script/RoverReplica.RoverEnemyCharacter"
EXPECTED_ANIM_CLASS = "/Game/Rover/Animations/ABP_Rover.ABP_Rover_C"
ATTACK_MONTAGE_PATHS = (
    "/Game/Rover/Combat/Montages/AM_Rover_Attack01.AM_Rover_Attack01",
    "/Game/Rover/Combat/Montages/AM_Rover_Attack02.AM_Rover_Attack02",
    "/Game/Rover/Combat/Montages/AM_Rover_Attack03.AM_Rover_Attack03",
)
WEAPON_MESH_PATH = "/Game/Rover/Weapons/R2Sword001/SK_R2Sword001.SK_R2Sword001"
WEAPON_SKELETON_PATH = "/Game/Rover/Weapons/R2Sword001/SKEL_R2Sword001.SKEL_R2Sword001"
WEAPON_MATERIAL_PATH = "/Game/Rover/Weapons/R2Sword001/M_R2Sword001.M_R2Sword001"
WEAPON_BASE_COLOR_PATH = (
    "/Game/Rover/Weapons/R2Sword001/Textures/"
    "T_R2Sword001Md20001_D.T_R2Sword001Md20001_D"
)
WEAPON_NORMAL_PATH = (
    "/Game/Rover/Weapons/R2Sword001/Textures/"
    "T_R2Sword001Md20001_N.T_R2Sword001Md20001_N"
)
EXPECTED_WEAPON_ATTACHMENTS = {
    1: "Bip001LHand",
    2: "RoverWeapon",
    3: "RoverWeapon",
}
MINIMUM_STAGE_ADVANCE = {1: 35.0, 2: 42.0, 3: 55.0}
START_TIMEOUT_SECONDS = 30.0
ATTACK_TIMEOUT_SECONDS = 20.0
STOP_TIMEOUT_SECONDS = 15.0
MAX_MONTAGE_START_DELAY = 0.25
EXPECTED_INITIAL_HEALTH = 300.0
SUCCESS_MARKER = "ROVER_COMBAT_P0_PIE_OK"
FAILURE_MARKER = "ROVER_COMBAT_P0_PIE_FAIL"


level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
unreal_editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
state = {
    "phase": "starting",
    "deadline": time.monotonic() + START_TIMEOUT_SECONDS,
    "result": None,
    "last": "PIE has not started",
    "world": None,
    "pawn": None,
    "enemy": None,
    "combat": None,
    "enemy_combat": None,
    "health": None,
    "locomotion": None,
    "weapon": None,
    "anim_instance": None,
    "attack_montages": (),
    "initial_location": None,
    "segment_start_location": None,
    "last_request_id": 0,
    "last_combo_index": 0,
    "request_world_time": 0.0,
    "request_ids": [],
    "combo_indices": [],
    "montage_delays": {},
    "saw_montages": set(),
    "saw_advances": set(),
    "stage_displacements": {},
    "damaged_requests": set(),
    "expected_stage_health": {},
    "attack_damage": (),
    "saw_weapon": False,
    "weapon_visibility_armed": False,
    "saw_weapon_attachments": set(),
    "saw_trace": set(),
    "saw_hit_reaction": False,
    "saw_recovery_release": False,
    "saw_recovery_movement_cancel": False,
    "jump_cancel_request_id": 0,
    "combo_success_detail": "",
    "queued_combo_indices": set(),
}
tick_handle = None


def object_path(value):
    return value.get_path_name() if value else "None"


def nearly_equal(left, right, tolerance=0.01):
    return abs(left - right) <= tolerance


def planar_distance(start, end):
    return math.hypot(end.x - start.x, end.y - start.y)


def vector_length(value):
    return math.sqrt(value.x * value.x + value.y * value.y + value.z * value.z)


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

    combat = state["combat"]
    if combat is not None:
        combat.set_light_attack_held(False)
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


def require_asset(path, expected_type):
    asset = unreal.load_asset(path)
    if not isinstance(asset, expected_type):
        finish(False, f"missing or invalid asset={path}")
        return None
    return asset


def validate_weapon_assets(pawn, mesh, weapon):
    weapon_mesh = require_asset(WEAPON_MESH_PATH, unreal.SkeletalMesh)
    weapon_skeleton = require_asset(WEAPON_SKELETON_PATH, unreal.Skeleton)
    weapon_material = require_asset(WEAPON_MATERIAL_PATH, unreal.Material)
    base_color = require_asset(WEAPON_BASE_COLOR_PATH, unreal.Texture2D)
    normal = require_asset(WEAPON_NORMAL_PATH, unreal.Texture2D)
    if state["result"] is not None:
        return False

    component_weapon_mesh = weapon.get_editor_property("skeletal_mesh_asset")
    if object_path(component_weapon_mesh) != WEAPON_MESH_PATH:
        finish(False, f"unexpected equipped weapon={object_path(component_weapon_mesh)}")
        return False
    if object_path(weapon_mesh.get_editor_property("skeleton")) != WEAPON_SKELETON_PATH:
        finish(False, "weapon does not use its independent Skeleton")
        return False
    if weapon_skeleton is not weapon_mesh.get_editor_property("skeleton"):
        finish(False, "weapon Skeleton reference is inconsistent")
        return False
    if object_path(weapon.get_material(0)) != WEAPON_MATERIAL_PATH:
        finish(False, f"unexpected weapon material={object_path(weapon.get_material(0))}")
        return False
    if base_color.get_editor_property("srgb") is not True:
        finish(False, "weapon base color texture is not sRGB")
        return False
    if normal.get_editor_property("srgb") is not False:
        finish(False, "weapon normal texture is incorrectly marked sRGB")
        return False
    if not weapon.does_socket_exist("WeaponTraceBase") or not weapon.does_socket_exist(
        "WeaponTraceTip"
    ):
        finish(False, "weapon Trace sockets are unavailable on the runtime component")
        return False

    attach_socket = str(weapon.get_attach_socket_name())
    if attach_socket != "RoverWeapon":
        finish(False, f"weapon attach socket={attach_socket}, expected RoverWeapon")
        return False
    if weapon.get_attach_parent() != mesh:
        finish(False, "weapon is not attached to the character skeletal mesh")
        return False
    socket_offset = vector_length(weapon.get_editor_property("relative_location"))
    if socket_offset > 0.5:
        finish(False, f"weapon origin is {socket_offset:.2f}cm from RoverWeapon socket")
        return False

    relative_scale = weapon.get_editor_property("relative_scale3d")
    if not all(
        nearly_equal(axis, 0.09, 0.01)
        for axis in (relative_scale.x, relative_scale.y, relative_scale.z)
    ):
        finish(False, f"weapon relative scale={relative_scale}, expected uniform 0.09")
        return False

    runtime_weapon_length = pawn.get_combat_weapon_world_length()
    if runtime_weapon_length < 80.0 or runtime_weapon_length > 115.0:
        finish(False, f"weapon runtime mesh length={runtime_weapon_length:.1f}cm")
        return False
    trace_base = weapon.get_socket_location("WeaponTraceBase")
    trace_tip = weapon.get_socket_location("WeaponTraceTip")
    runtime_blade_length = vector_length(trace_tip - trace_base)
    if runtime_blade_length < 30.0 or runtime_blade_length > 90.0:
        finish(False, f"weapon runtime blade length={runtime_blade_length:.1f}cm")
        return False
    if pawn.is_combat_weapon_visible():
        finish(False, "weapon is visible before the attack")
        return False
    return True


def begin_attack_validation(world):
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

    enemies = unreal.GameplayStatics.get_all_actors_with_tag(
        world, "RoverP0TrainingEnemy"
    )
    enemy = next(
        (
            actor
            for actor in enemies
            if actor and object_path(actor.get_class()) == EXPECTED_ENEMY
        ),
        None,
    )
    if enemy is None:
        finish(False, "map has no tagged Rover P0 training enemy")
        return

    combat = pawn.get_combat_component()
    enemy_combat = enemy.get_combat_component()
    health = enemy.get_health_component()
    locomotion = pawn.get_locomotion_component()
    weapon = pawn.get_combat_weapon()
    mesh = pawn.get_component_by_class(unreal.SkeletalMeshComponent)
    if any(
        value is None
        for value in (combat, enemy_combat, health, locomotion, weapon, mesh)
    ):
        finish(False, "player or enemy is missing a P0 combat component")
        return

    anim_instance = mesh.get_anim_instance()
    if anim_instance is None or object_path(anim_instance.get_class()) != EXPECTED_ANIM_CLASS:
        finish(False, f"unexpected anim instance={object_path(anim_instance)}")
        return

    attack_montages = tuple(
        require_asset(path, unreal.AnimMontage) for path in ATTACK_MONTAGE_PATHS
    )
    if state["result"] is not None or not validate_weapon_assets(pawn, mesh, weapon):
        return

    health.reset_health()
    if not nearly_equal(health.get_max_health(), EXPECTED_INITIAL_HEALTH):
        finish(False, f"enemy max health={health.get_max_health():.1f}, expected 300")
        return
    if not nearly_equal(health.get_current_health(), EXPECTED_INITIAL_HEALTH):
        finish(False, f"enemy initial health={health.get_current_health():.1f}")
        return

    combat_config = combat.get_editor_property("combat_config")
    if not isinstance(combat_config, unreal.RoverCombatConfig):
        finish(False, "combat component has no RoverCombatConfig")
        return
    attack_chain = list(
        combat_config.get_editor_property("settings").get_editor_property(
            "light_attack_chain"
        )
    )
    if len(attack_chain) != 3:
        finish(False, f"combat config attack count={len(attack_chain)}, expected 3")
        return
    attack_damage = tuple(
        float(definition.get_editor_property("damage"))
        for definition in attack_chain
    )
    expected_stage_health = {}
    remaining_health = EXPECTED_INITIAL_HEALTH
    for index, damage in enumerate(attack_damage, 1):
        remaining_health = max(0.0, remaining_health - damage)
        expected_stage_health[index] = remaining_health

    pawn.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0), False)
    player_location = pawn.get_actor_location()
    enemy.set_actor_location(
        player_location + unreal.Vector(500.0, 200.0, 0.0), False, True
    )
    enemy.set_actor_rotation(
        unreal.Rotator(roll=0.0, pitch=0.0, yaw=180.0), False
    )

    combat.set_light_attack_held(False)
    if not combat.request_light_attack():
        finish(False, "grounded Attack01 request was rejected")
        return
    request_id = combat.get_attack_request_id()
    if request_id <= 0 or combat.get_current_combo_index() != 1:
        finish(False, "accepted Attack01 has invalid RequestId or combo index")
        return
    if locomotion.get_combat_movement_restriction_request_id() != request_id:
        finish(False, "Attack01 did not acquire its movement restriction")
        return

    state.update(
        {
            "phase": "attacking",
            "deadline": time.monotonic() + ATTACK_TIMEOUT_SECONDS,
            "world": world,
            "pawn": pawn,
            "enemy": enemy,
            "combat": combat,
            "enemy_combat": enemy_combat,
            "health": health,
            "locomotion": locomotion,
            "weapon": weapon,
            "anim_instance": anim_instance,
            "attack_montages": attack_montages,
            "initial_location": player_location,
            "segment_start_location": player_location,
            "last_request_id": request_id,
            "last_combo_index": 1,
            "request_world_time": unreal.GameplayStatics.get_time_seconds(world),
            "request_ids": [request_id],
            "combo_indices": [1],
            "expected_stage_health": expected_stage_health,
            "attack_damage": attack_damage,
            "last": f"Attack01 request={request_id} accepted",
        }
    )


def observe_new_segment(request_id, combo_index):
    previous_index = state["last_combo_index"]
    current_location = state["pawn"].get_actor_location()
    state["stage_displacements"][previous_index] = planar_distance(
        state["segment_start_location"], current_location
    )
    if request_id in state["request_ids"]:
        finish(False, f"attack RequestId was reused={request_id}")
        return
    if combo_index != previous_index + 1:
        finish(False, f"combo order jumped {previous_index}->{combo_index}")
        return

    state["request_ids"].append(request_id)
    state["combo_indices"].append(combo_index)
    state["last_request_id"] = request_id
    state["last_combo_index"] = combo_index
    state["segment_start_location"] = current_location
    state["request_world_time"] = unreal.GameplayStatics.get_time_seconds(state["world"])
    state["enemy"].set_actor_location(
        current_location + unreal.Vector(500.0, 200.0, 0.0), False, True
    )
def validate_attack_tick():
    world = state["world"]
    pawn = state["pawn"]
    combat = state["combat"]
    enemy_combat = state["enemy_combat"]
    health = state["health"]
    locomotion = state["locomotion"]
    weapon = state["weapon"]
    anim_instance = state["anim_instance"]

    request_id = combat.get_attack_request_id()
    combo_index = combat.get_current_combo_index()
    if request_id > 0 and request_id != state["last_request_id"]:
        observe_new_segment(request_id, combo_index)
        if state["result"] is not None:
            return

    phase_name = str(combat.get_combat_phase()).upper()
    if (
        combo_index in (1, 2)
        and combat.is_combo_window_open()
        and combo_index not in state["queued_combo_indices"]
        and request_id in state["damaged_requests"]
    ):
        request_before_input = request_id
        if not combat.request_light_attack():
            finish(False, f"Attack0{combo_index} ComboWindow rejected the next click")
            return
        if combat.get_attack_request_id() == request_before_input:
            finish(False, f"Attack0{combo_index} ComboWindow did not transition immediately")
            return
        state["queued_combo_indices"].add(combo_index)
        return

    if combo_index in (1, 2, 3):
        montage = state["attack_montages"][combo_index - 1]
        if anim_instance.montage_is_playing(montage):
            if combo_index not in state["saw_montages"]:
                delay = (
                    unreal.GameplayStatics.get_time_seconds(world)
                    - state["request_world_time"]
                )
                state["montage_delays"][combo_index] = delay
                state["saw_montages"].add(combo_index)
                if delay > MAX_MONTAGE_START_DELAY:
                    finish(False, f"Attack0{combo_index} Montage delay={delay:.3f}s")
                    return

    weapon_visible = pawn.is_combat_weapon_visible()
    if weapon_visible:
        state["saw_weapon"] = True
        state["weapon_visibility_armed"] = True
    elif state["weapon_visibility_armed"] and combat.is_attacking():
        finish(False, f"weapon flickered off during combo stage={combo_index}")
        return

    if locomotion.is_combat_attack_advance_active() and combo_index in (1, 2, 3):
        state["saw_advances"].add(combo_index)

    trace_active = combat.is_weapon_trace_active()
    if trace_active and combo_index in (1, 2, 3):
        state["saw_trace"].add(combo_index)
        expected_attachment = EXPECTED_WEAPON_ATTACHMENTS[combo_index]
        actual_attachment = str(weapon.get_attach_socket_name())
        if actual_attachment != expected_attachment:
            finish(
                False,
                f"Attack0{combo_index} weapon attachment={actual_attachment}, "
                f"expected={expected_attachment}",
            )
            return
        state["saw_weapon_attachments"].add(combo_index)
        if request_id not in state["damaged_requests"]:
            trace_base = weapon.get_socket_location("WeaponTraceBase")
            trace_tip = weapon.get_socket_location("WeaponTraceTip")
            blade_center = (trace_base + trace_tip) * 0.5
            state["enemy"].set_actor_location(blade_center, False, True)

    current_health = health.get_current_health()
    expected_stage_health = state["expected_stage_health"]
    if request_id > 0 and combo_index in expected_stage_health:
        expected_health = expected_stage_health[combo_index]
        previous_expected = (
            EXPECTED_INITIAL_HEALTH
            if combo_index == 1
            else expected_stage_health[combo_index - 1]
        )
        if current_health < previous_expected - 0.01:
            if not nearly_equal(current_health, expected_health):
                finish(
                    False,
                    f"Attack0{combo_index} damage mismatch health={current_health:.1f} "
                    f"expected={expected_health:.1f}",
                )
                return
            state["damaged_requests"].add(request_id)

    state["saw_hit_reaction"] = (
        state["saw_hit_reaction"] or enemy_combat.is_in_hit_reaction()
    )

    restriction_id = locomotion.get_combat_movement_restriction_request_id()
    if combat.is_attacking() and restriction_id == 0:
        state["saw_recovery_release"] = True
    elif combat.is_attacking() and request_id > 0 and restriction_id != request_id:
        finish(
            False,
            f"movement restriction={restriction_id} does not own active request={request_id}",
        )
        return

    if combo_index == 3:
        move_direction = unreal.Vector(1.0, 0.0, 0.0)
        locomotion.set_move_input(unreal.Vector2D(0.0, 1.0), move_direction)

    if (
        combo_index == 3
        and "RECOVERY" in phase_name
        and not state["saw_recovery_movement_cancel"]
    ):
        move_direction = unreal.Vector(1.0, 0.0, 0.0)
        if not combat.request_recovery_movement_interrupt():
            finish(False, "final Recovery rejected movement interrupt")
            return
        if combat.is_attacking() or combat.get_current_combo_index() != -1:
            finish(False, "movement interrupt did not terminate and reset the attack")
            return
        pawn.add_movement_input(move_direction, 1.0, False)
        state["saw_recovery_movement_cancel"] = True
        return

    state["last"] = (
        f"combo={combo_index} request={request_id} attacking={combat.is_attacking()} "
        f"weapon={weapon_visible} trace={trace_active} enemy_hp={current_health:.1f} "
        f"restriction={restriction_id}"
    )

    if combat.is_attacking():
        return

    final_location = pawn.get_actor_location()
    state["stage_displacements"][3] = planar_distance(
        state["segment_start_location"], final_location
    )
    if state["request_ids"] != sorted(set(state["request_ids"])):
        finish(False, f"invalid request sequence={state['request_ids']}")
        return
    if state["combo_indices"] != [1, 2, 3]:
        finish(False, f"combo sequence={state['combo_indices']}, expected [1, 2, 3]")
        return
    if state["saw_montages"] != {1, 2, 3}:
        finish(False, f"montages observed={sorted(state['saw_montages'])}")
        return
    if state["saw_advances"] != {1, 2, 3}:
        finish(False, f"attack advances observed={sorted(state['saw_advances'])}")
        return
    if state["saw_trace"] != {1, 2, 3}:
        finish(False, f"Trace stages observed={sorted(state['saw_trace'])}")
        return
    if state["saw_weapon_attachments"] != {1, 2, 3}:
        finish(
            False,
            "weapon attachment stages="
            f"{sorted(state['saw_weapon_attachments'])}",
        )
        return
    expected_final_health = state["expected_stage_health"][3]
    if len(state["damaged_requests"]) != 3 or not nearly_equal(
        current_health, expected_final_health
    ):
        finish(
            False,
            f"damage requests={len(state['damaged_requests'])} final health={current_health:.1f}",
        )
        return
    for stage, minimum_distance in MINIMUM_STAGE_ADVANCE.items():
        actual_distance = state["stage_displacements"].get(stage, 0.0)
        if actual_distance < minimum_distance:
            finish(
                False,
                f"Attack0{stage} capsule advance={actual_distance:.1f}cm "
                f"minimum={minimum_distance:.1f}cm",
            )
            return
    if not state["saw_weapon"] or not state["saw_hit_reaction"]:
        finish(False, "combo did not show weapon or trigger hit reaction")
        return
    if not state["saw_recovery_release"]:
        finish(False, "final Recovery did not release movement before attack cleanup")
        return
    if not state["saw_recovery_movement_cancel"]:
        finish(False, "final Recovery movement did not cancel the attack")
        return
    if pawn.is_combat_weapon_visible():
        finish(False, "weapon remained visible after the combo")
        return
    if combat.is_weapon_trace_active():
        finish(False, "weapon Trace remained active after the combo")
        return
    if restriction_id != 0:
        finish(False, f"movement restriction leaked request={restriction_id}")
        return
    if locomotion.is_combat_attack_advance_active():
        finish(False, "attack Root Motion Source remained active after the combo")
        return

    total_advance = planar_distance(state["initial_location"], final_location)
    state["combo_success_detail"] = " ".join(
        (
            f"requests={state['request_ids']}",
            "combo=01->02->03",
            f"montage_delays={state['montage_delays']}",
            f"stage_advance={state['stage_displacements']}",
            f"total_advance={total_advance:.1f}cm",
            "weapon=Attack01Left/Attack02Right/Attack03Right",
            f"damage={'+'.join(f'{value:g}' for value in state['attack_damage'])}",
            f"enemy_hp=300->{expected_final_health:g}",
            "recovery_cancel=movement+full_body_locomotion_blendout",
            "cleanup=complete",
        )
    )

    locomotion.set_move_input(
        unreal.Vector2D(0.0, 0.0), unreal.Vector(0.0, 0.0, 0.0)
    )
    state["enemy"].set_actor_location(
        pawn.get_actor_location() + unreal.Vector(1000.0, 1000.0, 0.0), False, True
    )
    if not combat.request_light_attack():
        finish(False, "jump-cancel Attack01 request was rejected")
        return
    jump_request_id = combat.get_attack_request_id()
    if jump_request_id <= 0 or combat.get_current_combo_index() != 1:
        finish(False, "jump-cancel Attack01 has invalid request state")
        return
    if combat.request_recovery_movement_interrupt():
        finish(False, "Startup incorrectly accepted a Recovery-only jump interrupt")
        return
    if not combat.is_attacking() or combat.get_attack_request_id() != jump_request_id:
        finish(False, "rejected Startup interrupt changed the active attack")
        return
    state["jump_cancel_request_id"] = jump_request_id
    state["phase"] = "jump_cancel_attacking"
    state["deadline"] = time.monotonic() + ATTACK_TIMEOUT_SECONDS


def validate_jump_cancel_tick():
    combat = state["combat"]
    locomotion = state["locomotion"]
    pawn = state["pawn"]
    movement = pawn.get_component_by_class(unreal.CharacterMovementComponent)
    request_id = state["jump_cancel_request_id"]

    if state["phase"] == "jump_cancel_attacking":
        if not combat.is_attacking() or combat.get_attack_request_id() != request_id:
            finish(False, "jump-cancel attack ended before Recovery")
            return
        phase_name = str(combat.get_combat_phase()).upper()
        if "RECOVERY" not in phase_name:
            state["last"] = f"waiting for jump-cancel Recovery request={request_id}"
            return
        if not combat.request_recovery_movement_interrupt():
            finish(False, "Recovery rejected jump interrupt")
            return
        if combat.is_attacking() or combat.get_current_combo_index() != -1:
            finish(False, "jump interrupt did not terminate and reset the attack")
            return
        if not locomotion.try_jump():
            finish(False, "jump was rejected after Recovery attack cancellation")
            return
        state["phase"] = "jump_cancel_observing"
        state["last"] = "Recovery attack cancelled; waiting for Airborne"
        return

    if combat.is_attacking() or combat.get_attack_request_id() != 0:
        finish(False, "attack restarted while observing jump cancellation")
        return
    if locomotion.get_combat_movement_restriction_request_id() != 0:
        finish(False, "jump cancellation leaked the movement restriction")
        return
    if pawn.is_combat_weapon_visible() or combat.is_weapon_trace_active():
        finish(False, "jump cancellation leaked weapon combat state")
        return
    if movement is not None and movement.is_falling():
        finish(
            True,
            f"{state['combo_success_detail']} "
            "jump_cancel=startup_blocked+recovery_cancelled+airborne",
        )
        return
    state["last"] = "jump cancellation completed; waiting for falling movement mode"


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
            begin_attack_validation(world)
        elif state["phase"] == "attacking":
            validate_attack_tick()
        elif state["phase"] in ("jump_cancel_attacking", "jump_cancel_observing"):
            validate_jump_cancel_tick()

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
