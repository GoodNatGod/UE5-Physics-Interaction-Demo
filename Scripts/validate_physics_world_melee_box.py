import time

import unreal


BOX_TAG = "PhysicsWorldP0Box"
EXPECTED_PAWN = "/Script/RoverReplica.RoverCharacter"
EXPECTED_SURFACE = unreal.PhysicalSurface.SURFACE_TYPE2
EXPECTED_GEOMETRY_COLLECTION = (
    "/Game/PhysicsWorldDemo/GeometryCollections/"
    "GC_Demo_WoodenCrate_Fractured.GC_Demo_WoodenCrate_Fractured"
)
EXPECTED_CAMERA_YAW = 90.0
MAX_CAMERA_YAW_ERROR = 0.5
MINIMUM_DEBRIS_EXPANSION = 20.0
START_TIMEOUT_SECONDS = 30.0
ATTACK_TIMEOUT_SECONDS = 20.0
DEBRIS_TIMEOUT_SECONDS = 2.0
STOP_TIMEOUT_SECONDS = 15.0
SUCCESS_MARKER = "PHYSICS_WORLD_MELEE_BOX_PIE_OK"
FAILURE_MARKER = "PHYSICS_WORLD_MELEE_BOX_PIE_FAIL"


level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
unreal_editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
state = {
    "phase": "starting",
    "deadline": time.monotonic() + START_TIMEOUT_SECONDS,
    "result": None,
    "last": "PIE has not started",
    "world": None,
    "controller": None,
    "pawn": None,
    "box": None,
    "combat": None,
    "weapon": None,
    "subsystem": None,
    "last_request_id": 0,
    "last_combo_index": 0,
    "request_ids": [],
    "damaged_stages": set(),
    "saw_trace_stages": set(),
    "max_camera_yaw_error": 0.0,
    "initial_health": 0.0,
    "attack_damage": 0.0,
    "box_placed_for_sweep": False,
    "max_box_target_error": 0.0,
    "saw_left_hand_attachment": False,
}
tick_handle = None


def object_path(value):
    return value.get_path_name() if value else "None"


def nearly_equal(left, right, tolerance=0.01):
    return abs(left - right) <= tolerance


def yaw_delta(left, right):
    return (left - right + 180.0) % 360.0 - 180.0


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
    state["result"] = (ok, detail)
    if level_editor.is_in_play_in_editor():
        state["phase"] = "stopping"
        state["deadline"] = time.monotonic() + STOP_TIMEOUT_SECONDS
        level_editor.editor_request_end_play()
    else:
        shutdown()


def begin_validation(world):
    controller = unreal.GameplayStatics.get_player_controller(world, 0)
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    if controller is None or pawn is None:
        state["last"] = "waiting for player controller and pawn"
        return
    if object_path(pawn.get_class()) != EXPECTED_PAWN:
        finish(False, f"unexpected pawn={object_path(pawn.get_class())}")
        return

    movement = pawn.get_component_by_class(unreal.CharacterMovementComponent)
    if movement is None or not movement.is_moving_on_ground():
        state["last"] = "waiting for player to reach the ground"
        return

    boxes = unreal.GameplayStatics.get_all_actors_with_tag(world, BOX_TAG)
    candidates = [
        actor
        for actor in boxes
        if actor is not None
        and not actor.is_destroyed()
        and nearly_equal(actor.get_current_health(), actor.get_max_health())
    ]
    if not candidates:
        finish(False, f"found no intact tagged destructible box among {len(boxes)}")
        return
    pawn_location = pawn.get_actor_location()
    box = min(
        candidates,
        key=lambda actor: (
            (actor.get_actor_location().x - pawn_location.x) ** 2
            + (actor.get_actor_location().y - pawn_location.y) ** 2
            + (actor.get_actor_location().z - pawn_location.z) ** 2
        ),
    )
    combat = pawn.get_combat_component()
    weapon = pawn.get_combat_weapon()
    subsystem = unreal.RoverEditorTestLibrary.get_world_interaction_subsystem(world)
    if any(value is None for value in (combat, weapon, subsystem)):
        finish(False, "melee box test is missing combat, weapon, or interaction subsystem")
        return
    if box.is_destroyed() or not nearly_equal(
        box.get_current_health(), box.get_max_health()
    ):
        finish(False, f"box initial health={box.get_current_health():.1f}")
        return
    if not box.has_geometry_collection_asset():
        finish(False, "box has no Geometry Collection asset")
        return
    collection = box.get_geometry_collection().get_editor_property("rest_collection")
    if object_path(collection) != EXPECTED_GEOMETRY_COLLECTION:
        finish(False, f"unexpected Geometry Collection={object_path(collection)}")
        return

    combat_config = combat.get_editor_property("combat_config")
    if combat_config is None:
        finish(False, "combat component has no CombatConfig")
        return
    attack_chain = combat_config.get_editor_property("settings").get_editor_property(
        "light_attack_chain"
    )
    if not attack_chain:
        finish(False, "CombatConfig has no light attack definitions")
        return
    attack_damage = attack_chain[0].get_editor_property("damage")
    attack_trace = (
        attack_chain[0].get_editor_property("trace_radius"),
        attack_chain[0].get_editor_property("trace_sample_count"),
        attack_chain[0].get_editor_property("trace_substep_distance"),
        attack_chain[0].get_editor_property("max_trace_substeps"),
    )
    if (
        attack_trace[0] <= 0.0
        or attack_trace[1] < 2
        or attack_trace[2] <= 0.0
        or attack_trace[3] < 1
    ):
        finish(False, f"invalid Attack01 trace settings={attack_trace}")
        return
    initial_health = box.get_max_health()
    if attack_damage + 0.01 < initial_health:
        finish(
            False,
            f"Attack01 damage={attack_damage:.1f} cannot one-hit "
            f"crate health={initial_health:.1f}",
        )
        return

    interaction_config = box.get_editor_property("interaction_config")
    if not isinstance(interaction_config, unreal.WorldInteractionConfig):
        finish(False, "box has no WorldInteractionConfig")
        return
    interaction_settings = interaction_config.get_editor_property("settings")
    expected_gravity_z = float(
        interaction_settings.get_editor_property("world_gravity_z")
    )
    applied_gravity_z = float(subsystem.get_applied_world_gravity_z())
    if not interaction_settings.get_editor_property("override_world_gravity"):
        finish(False, "shared world gravity override is disabled")
        return
    if not nearly_equal(applied_gravity_z, expected_gravity_z, 0.1):
        finish(
            False,
            f"world gravity={applied_gravity_z:.1f}, expected={expected_gravity_z:.1f}",
        )
        return
    if not box.is_intact_physics_simulating():
        finish(False, "intact box is not simulating physics")
        return
    configured_mass = float(box.get_configured_box_mass_kg())
    actual_mass = float(box.get_intact_physics_mass_kg())
    if not nearly_equal(actual_mass, configured_mass, 0.1):
        finish(
            False,
            f"intact box mass={actual_mass:.2f}kg, configured={configured_mass:.2f}kg",
        )
        return

    for enemy in unreal.GameplayStatics.get_all_actors_with_tag(
        world, "RoverP0TrainingEnemy"
    ):
        enemy.set_actor_location(
            pawn.get_actor_location() + unreal.Vector(800.0, 800.0, 0.0),
            False,
            True,
        )

    box.set_actor_location(
        pawn.get_actor_location() + unreal.Vector(600.0, 300.0, 0.0),
        False,
        True,
    )
    subsystem.reset_debug_stats()
    combat.set_light_attack_held(False)
    pawn.set_actor_rotation(
        unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0), False
    )
    control_rotation = controller.get_control_rotation()
    controller.set_control_rotation(
        unreal.Rotator(
            roll=control_rotation.roll,
            pitch=control_rotation.pitch,
            yaw=EXPECTED_CAMERA_YAW,
        )
    )
    if not combat.request_light_attack():
        finish(False, "grounded Attack01 request was rejected")
        return

    request_id = combat.get_attack_request_id()
    if request_id <= 0 or combat.get_current_combo_index() != 1:
        finish(False, "Attack01 has invalid RequestId or combo index")
        return

    state.update(
        {
            "phase": "attacking",
            "deadline": time.monotonic() + ATTACK_TIMEOUT_SECONDS,
            "world": world,
            "controller": controller,
            "pawn": pawn,
            "box": box,
            "combat": combat,
            "weapon": weapon,
            "subsystem": subsystem,
            "last_request_id": request_id,
            "last_combo_index": 1,
            "request_ids": [request_id],
            "initial_health": initial_health,
            "attack_damage": attack_damage,
            "attack_trace": attack_trace,
            "box_mass_kg": actual_mass,
            "world_gravity_z": applied_gravity_z,
            "box_placed_for_sweep": False,
            "max_box_target_error": 0.0,
            "last": f"Attack01 request={request_id} accepted",
        }
    )


def validate_attack_tick():
    combat = state["combat"]
    box = state["box"]
    request_id = combat.get_attack_request_id()
    combo_index = combat.get_current_combo_index()

    if request_id not in (0, state["last_request_id"]):
        finish(False, f"unexpected follow-up attack request={request_id}")
        return

    if combat.is_attacking():
        camera_yaw = state["controller"].get_control_rotation().yaw
        camera_yaw_error = abs(yaw_delta(camera_yaw, EXPECTED_CAMERA_YAW))
        state["max_camera_yaw_error"] = max(
            state["max_camera_yaw_error"], camera_yaw_error
        )
        if camera_yaw_error > MAX_CAMERA_YAW_ERROR:
            finish(
                False,
                f"attack camera yaw changed to {camera_yaw:.2f}; "
                f"expected={EXPECTED_CAMERA_YAW:.2f}",
            )
            return

    trace_active = combat.is_weapon_trace_active()
    if trace_active and combo_index == 1:
        state["saw_trace_stages"].add(1)
        attachment = str(state["weapon"].get_attach_socket_name())
        if attachment != "Bip001LHand":
            finish(
                False,
                f"Attack01 weapon attachment={attachment}, expected Bip001LHand",
            )
            return
        state["saw_left_hand_attachment"] = True
        if not box.is_destroyed() and 1 not in state["damaged_stages"]:
            trace_base = state["weapon"].get_socket_location("WeaponTraceBase")
            trace_tip = state["weapon"].get_socket_location("WeaponTraceTip")
            trace_target = trace_base + (trace_tip - trace_base) * 0.85
            intact_mesh = box.get_intact_mesh()
            intact_mesh.set_simulate_physics(False)
            box.set_actor_location(trace_target, False, True)
            intact_mesh.set_simulate_physics(True)
            intact_mesh.set_enable_gravity(False)
            intact_mesh.set_physics_linear_velocity(unreal.Vector(), False)
            intact_mesh.set_physics_angular_velocity_in_degrees(
                unreal.Vector(), False
            )
            intact_mesh.put_rigid_body_to_sleep()
            placement_error = (box.get_actor_location() - trace_target).length()
            state["max_box_target_error"] = max(
                state["max_box_target_error"], placement_error
            )
            state["box_placed_for_sweep"] = True

    current_health = box.get_current_health()
    if (
        current_health < state["initial_health"] - 0.01
        and 1 not in state["damaged_stages"]
    ):
        if not nearly_equal(current_health, 0.0):
            finish(
                False,
                f"Attack01 box health={current_health:.1f} expected=0.0",
            )
            return
        state["damaged_stages"].add(1)
        result = state["subsystem"].get_last_interaction_result()
        if not result.get_editor_property("accepted"):
            finish(False, "Attack01 interaction was not accepted")
            return
        if result.get_editor_property("surface_type") != EXPECTED_SURFACE:
            finish(False, "Attack01 did not resolve Wood surface")
            return
        if not box.has_applied_break_impulse():
            finish(False, "Attack01 break impulse was deferred after the hit")
            return
        if not box.has_applied_break_strain():
            finish(False, "Attack01 did not apply External Strain to the root cluster")
            return

    state["last"] = (
        f"combo={combo_index} request={request_id} trace={trace_active} "
        f"box_hp={current_health:.1f} requests="
        f"{state['subsystem'].get_processed_request_count()}"
    )
    if combat.is_attacking():
        return

    if state["request_ids"] != [state["last_request_id"]]:
        finish(False, f"invalid Attack01 requests={state['request_ids']}")
        return
    if state["damaged_stages"] != {1}:
        finish(
            False,
            f"damage stages={sorted(state['damaged_stages'])} "
            f"trace_stages={sorted(state['saw_trace_stages'])} "
            f"box_placed={state['box_placed_for_sweep']} "
            f"max_placement_error={state['max_box_target_error']:.2f}cm",
        )
        return
    if state["saw_trace_stages"] != {1}:
        finish(False, f"Trace stages={sorted(state['saw_trace_stages'])}")
        return
    if not state["saw_left_hand_attachment"]:
        finish(False, "Attack01 Trace never ran from the left-hand weapon")
        return
    if not box.is_destroyed() or not nearly_equal(current_health, 0.0):
        finish(False, f"Attack01 did not destroy box; health={current_health:.1f}")
        return
    if not box.is_geometry_collection_active():
        finish(False, "Attack01 destroyed box without activating Geometry Collection")
        return
    if box.get_break_transform_transfer_error() > 1.0:
        finish(
            False,
            "Geometry Collection transfer error="
            f"{box.get_break_transform_transfer_error():.2f}cm",
        )
        return
    if state["subsystem"].get_processed_request_count() != 1:
        finish(
            False,
            "Attack01 interaction request count="
            f"{state['subsystem'].get_processed_request_count()} expected=1",
        )
        return
    if state["subsystem"].get_dispatched_receiver_count() != 1:
        finish(
            False,
            "Attack01 receiver dispatch count="
            f"{state['subsystem'].get_dispatched_receiver_count()} expected=1",
        )
        return

    state["phase"] = "checking_debris"
    state["deadline"] = time.monotonic() + DEBRIS_TIMEOUT_SECONDS
    state["last"] = "waiting for deferred Chaos debris impulse"


def validate_debris_tick():
    box = state["box"]
    if not box.has_applied_break_impulse():
        state["last"] = "break impulse has not reached the Chaos proxy"
        return
    expansion = box.get_debris_expansion_distance()
    state["last"] = f"debris expansion={expansion:.1f}cm"
    if expansion < MINIMUM_DEBRIS_EXPANSION:
        return
    finish(
        True,
        " ".join(
            (
                f"attack_damage={state['attack_damage']:.1f}",
                f"trace={state['attack_trace']}",
                f"crate_health={state['initial_health']:.1f}->0",
                "requests=1",
                "receivers=1",
                "camera_yaw_locked=true",
                "weapon_hand=left",
                f"box_mass={state['box_mass_kg']:.1f}kg",
                f"gravity_z={state['world_gravity_z']:.1f}",
                "break_transform=continuous",
                f"camera_max_error={state['max_camera_yaw_error']:.2f}deg",
                "surface=Wood",
                "geometry_collection=active",
                "external_strain=applied",
                "break_impulse=immediate",
                f"debris_expansion={expansion:.1f}cm",
            )
        ),
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
            validate_attack_tick()
        elif state["phase"] == "checking_debris":
            validate_debris_tick()

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
