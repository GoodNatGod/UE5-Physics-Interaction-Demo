import math
import time

import unreal


BRIDGE_TAG = "PhysicsWorldRopeBridge"
BRIDGE_BLUEPRINT_CLASS = (
    "/Game/PhysicsWorldDemo/Blueprints/BP_RopeBridge.BP_RopeBridge_C"
)
SECONDARY_ANCHOR_TAG = "RopeBridgeSecondaryAnchor"
INTERNAL_NEGATIVE_Y_TAG = "RopeBridgeInternalNegativeY"
INTERNAL_POSITIVE_Y_TAG = "RopeBridgeInternalPositiveY"
SUPPORT_PROPERTIES = (
    "left_support",
    "left_support_secondary",
    "right_support",
    "right_support_secondary",
)
ANCHOR_SPECS = (
    ("LeftAnchor_NegativeY", "left_support", -1.0, -1.0, False),
    ("LeftAnchor_PositiveY", "left_support_secondary", -1.0, 1.0, True),
    ("RightAnchor_NegativeY", "right_support", 1.0, -1.0, False),
    ("RightAnchor_PositiveY", "right_support_secondary", 1.0, 1.0, True),
)
MIN_PLANK_COUNT = 12
MASS_TOLERANCE_KG = 0.15
MAX_ANCHOR_COMPONENT_ERROR_CM = 0.25
MAX_ANCHOR_FRAME_ERROR_CM = 3.0
MAX_INTERNAL_CONSTRAINT_LATERAL_ERROR_CM = 0.25
SETTLE_MIN_SECONDS = 1.0
SETTLE_QUIET_SECONDS = 0.10
SETTLED_LINEAR_SPEED = 30.0
SETTLED_ANGULAR_SPEED = 120.0
IMPULSE_SAMPLE_SECONDS = 0.45
IMPULSE_DECAY_SECONDS = 2.5
STATIONARY_SAMPLE_SECONDS = 0.65
ATTACK_IDLE_CONTROL_SECONDS = 8.0
ATTACK_SETTLED_LINEAR_SPEED = 100.0
ATTACK_SETTLED_ANGULAR_SPEED = 35.0
ATTACK_REPEAT_MIN_WAIT_SECONDS = 1.0
ATTACK_REPEAT_QUIET_SECONDS = 0.20
ATTACK_RESTORE_GRACE_SECONDS = 1.0
FIRST_LOAD_HITCH_MIN_DELTA_SECONDS = 0.050
FIRST_LOAD_HITCH_MIN_DELTA_GAP_SECONDS = 0.015
FIRST_LOAD_HITCH_MIN_DELTA_RATIO = 1.50
SUPPORT_CONFIRM_SECONDS = 0.15
POST_LANDING_SAMPLE_SECONDS = 1.0
RECOVERY_SECONDS = 10.0
MAX_REST_ANGULAR_ERROR_DEGREES = 2.0
WALK_TEST_SPEED = 140.0
RUN_TEST_SPEED = 500.0
MAX_ENDPOINT_ERROR_CM = 12.0
MAX_ADJACENT_DISTANCE_ERROR_CM = 10.0
MAX_CENTER_DROP_CM = 75.0
START_TIMEOUT_SECONDS = 30.0
PHASE_TIMEOUT_SECONDS = 20.0
STOP_TIMEOUT_SECONDS = 15.0
SUCCESS_MARKER = "PHYSICS_WORLD_ROPE_BRIDGE_PIE_OK"
FAILURE_MARKER = "PHYSICS_WORLD_ROPE_BRIDGE_PIE_FAIL"


level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
unreal_editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
state = {
    "phase": "starting",
    "deadline": time.monotonic() + START_TIMEOUT_SECONDS,
    "result": None,
    "last": "PIE has not started",
    "world": None,
    "bridge": None,
    "pawn": None,
    "movement": None,
    "locomotion": None,
    "combat": None,
    "interaction_subsystem": None,
    "center_plank": None,
    "expected_planks": 0,
    "expected_constraints": 0,
    "phase_start_time": 0.0,
    "quiet_since": None,
    "support_since": None,
    "original_pawn_location": None,
    "baseline_center_location": None,
    "minimum_center_z": float("inf"),
    "impulse_peak_linear": 0.0,
    "impulse_peak_angular": 0.0,
    "impulse_decay_linear": 0.0,
    "impulse_decay_angular": 0.0,
    "standing_peak_linear": 0.0,
    "standing_peak_angular": 0.0,
    "attack_request_id": 0,
    "attack_seen_active": False,
    "attack_peak_movement_impulse": 0.0,
    "attack_peak_linear": 0.0,
    "attack_peak_angular": 0.0,
    "attack_baseline_linear": 0.0,
    "attack_baseline_angular": 0.0,
    "attack_idle_peak_linear": 0.0,
    "attack_idle_peak_angular": 0.0,
    "attack_push_scale_observed": False,
    "attack_push_force_factor": 0.0,
    "attack_start_pawn_location": None,
    "attack_max_pawn_displacement": 0.0,
    "attack_max_horizontal_speed": 0.0,
    "attack_max_vertical_speed": 0.0,
    "attack_peak_plank_index": -1,
    "attack_peak_nearest_plank_index": -1,
    "attack_peak_phase": "unknown",
    "attack_peak_weapon_trace": False,
    "attack_peak_elapsed": 0.0,
    "attack_peak_initial_push_force": 0.0,
    "attack_peak_push_force": 0.0,
    "attack_peak_standing_force_scale": 0.0,
    "attack_restore_wait_started": None,
    "attack_samples": [],
    "attack_repeat_quiet_since": None,
    "current_delta_seconds": 0.0,
    "attack_delta_total_seconds": 0.0,
    "attack_delta_frame_count": 0,
    "attack_max_delta_seconds": 0.0,
    "attack_request_call_seconds": 0.0,
    "attack_start_interaction_count": 0,
    "attack_world_interaction_count": 0,
    "attack_advance_seen": False,
    "attack_anim_root_motion_seen": False,
    "attack_reference_plank": None,
    "attack_start_relative_location": None,
    "attack_max_relative_displacement": 0.0,
    "attack_first_load_hitch": False,
    "attack_delta_ratio": 0.0,
    "walk_peak_linear": 0.0,
    "walk_peak_angular": 0.0,
    "walk_impulse": 0.0,
    "run_peak_linear": 0.0,
    "run_peak_angular": 0.0,
    "run_impulse": 0.0,
    "jump_takeoff_impulse": 0.0,
    "jump_peak_linear": 0.0,
    "jump_peak_angular": 0.0,
    "landing_impulse": 0.0,
    "landing_peak_linear": 0.0,
    "landing_peak_angular": 0.0,
    "recovery_linear": 0.0,
    "recovery_angular": 0.0,
    "recovery_rest_angular_error": 0.0,
    "initial_rest_angular_error": 0.0,
    "maximum_endpoint_error": 0.0,
    "maximum_joint_error": 0.0,
    "maximum_anchor_frame_error": 0.0,
    "support_count": 0,
    "primary_anchor_count": 0,
    "secondary_anchor_count": 0,
    "internal_constraint_pair_count": 0,
    "maximum_internal_lateral_error": 0.0,
    "plank_mass_kg": 0.0,
    "character_mass_kg": 0.0,
    "standing_force_scale": 0.0,
}
tick_handle = None


def nearly_equal(left, right, tolerance):
    return abs(left - right) <= tolerance


def vector_distance(left, right):
    return math.sqrt(
        (left.x - right.x) ** 2
        + (left.y - right.y) ** 2
        + (left.z - right.z) ** 2
    )


def enum_matches(value, expected_suffix):
    return expected_suffix.upper() in str(value).upper()


def has_component_tag(component, tag):
    return unreal.Name(tag) in list(component.get_editor_property("component_tags"))


def actor_local_to_world(actor, location):
    scale = actor.get_actor_scale3d()
    return (
        actor.get_actor_location()
        + actor.get_actor_forward_vector() * location.x * scale.x
        + actor.get_actor_right_vector() * location.y * scale.y
        + actor.get_actor_up_vector() * location.z * scale.z
    )


def actor_world_to_local_lateral(actor, location):
    relative = location - actor.get_actor_location()
    right = actor.get_actor_right_vector()
    scale_y = float(actor.get_actor_scale3d().y)
    if abs(scale_y) <= 1.0e-6:
        raise RuntimeError("bridge actor has a zero Y scale")
    return (
        relative.x * right.x
        + relative.y * right.y
        + relative.z * right.z
    ) / scale_y


def get_constraint_profile(constraint):
    instance = constraint.get_editor_property("constraint_instance")
    return instance.get_editor_property("profile_instance")


def validate_constraint_profile(constraint, settings, secondary, internal=False):
    profile = get_constraint_profile(constraint)
    linear = profile.get_editor_property("linear_limit")
    cone = profile.get_editor_property("cone_limit")
    twist = profile.get_editor_property("twist_limit")

    swing1 = cone.get_editor_property("swing1_motion")
    swing2 = cone.get_editor_property("swing2_motion")
    twist_motion = twist.get_editor_property("twist_motion")
    if secondary:
        for axis in ("x_motion", "y_motion"):
            if not enum_matches(linear.get_editor_property(axis), "LCM_LOCKED"):
                raise RuntimeError(f"{constraint.get_name()} {axis} is not locked")
        if not enum_matches(
            linear.get_editor_property("z_motion"), "LCM_FREE"
        ):
            raise RuntimeError(
                f"secondary {constraint.get_name()} z_motion is not free"
            )
        for name, motion in (
            ("swing1", swing1),
            ("swing2", swing2),
            ("twist", twist_motion),
        ):
            if not enum_matches(motion, "ACM_FREE"):
                raise RuntimeError(
                    f"secondary {constraint.get_name()} {name} is not free"
                )
        if constraint.is_projection_enabled():
            raise RuntimeError(
                f"secondary {constraint.get_name()} still has projection enabled"
            )
        return

    for axis in ("x_motion", "y_motion", "z_motion"):
        if not enum_matches(linear.get_editor_property(axis), "LCM_LOCKED"):
            raise RuntimeError(f"{constraint.get_name()} {axis} is not locked")
    if not enum_matches(swing1, "ACM_LIMITED"):
        kind = "internal" if internal else "primary"
        raise RuntimeError(f"{kind} {constraint.get_name()} swing1 is not limited")
    if internal:
        if not enum_matches(swing2, "ACM_FREE"):
            raise RuntimeError(
                f"internal {constraint.get_name()} swing2={swing2} "
                "expected=ACM_FREE for a dual-side seam"
            )
        if not enum_matches(twist_motion, "ACM_FREE"):
            raise RuntimeError(
                f"internal {constraint.get_name()} twist={twist_motion} "
                "expected=ACM_FREE for a dual-side seam"
            )
    elif not enum_matches(swing2, "ACM_FREE") or not enum_matches(
        twist_motion, "ACM_FREE"
    ):
        raise RuntimeError(
            f"primary {constraint.get_name()} swing2/twist are not free"
        )
    if not nearly_equal(
        float(cone.get_editor_property("swing1_limit_degrees")),
        float(settings.get_editor_property("swing1_limit_degrees")),
        0.01,
    ):
        raise RuntimeError(f"primary {constraint.get_name()} swing1 limit is invalid")
    if not constraint.is_projection_enabled():
        raise RuntimeError(f"primary {constraint.get_name()} projection is disabled")
    if not nearly_equal(
        float(profile.get_editor_property("projection_linear_alpha")),
        float(settings.get_editor_property("projection_linear_alpha")),
        0.001,
    ) or not nearly_equal(
        float(profile.get_editor_property("projection_angular_alpha")),
        float(settings.get_editor_property("projection_angular_alpha")),
        0.001,
    ):
        raise RuntimeError(f"primary {constraint.get_name()} projection profile differs")


def validate_four_anchor_structure(bridge, settings, expected_constraints):
    valid_support_count = int(bridge.get_valid_support_count())
    if valid_support_count != 4:
        raise RuntimeError(f"valid support count={valid_support_count} expected=4")

    supports = {}
    for property_name in SUPPORT_PROPERTIES:
        support = bridge.get_editor_property(property_name)
        if not isinstance(support, unreal.StaticMeshComponent):
            raise RuntimeError(f"bridge support {property_name} is missing")
        if support.is_simulating_physics():
            raise RuntimeError(f"bridge support {property_name} simulates physics")
        supports[property_name] = support
    if len({support.get_path_name() for support in supports.values()}) != 4:
        raise RuntimeError("bridge does not expose four distinct support components")

    constraints = []
    for index in range(expected_constraints):
        constraint = bridge.get_constraint_component(index)
        if not isinstance(constraint, unreal.PhysicsConstraintComponent):
            raise RuntimeError(f"constraint {index} is missing")
        constraints.append(constraint)

    secondary_constraints = [
        constraint
        for constraint in constraints
        if has_component_tag(constraint, SECONDARY_ANCHOR_TAG)
    ]
    if len(secondary_constraints) != 2:
        raise RuntimeError(
            f"secondary anchor count={len(secondary_constraints)} expected=2"
        )

    plank_count = int(settings.get_editor_property("plank_count"))
    step = float(settings.get_editor_property("plank_depth")) + float(
        settings.get_editor_property("plank_gap")
    )
    half_span = step * float(plank_count - 1) * 0.5 + float(
        settings.get_editor_property("anchor_extension")
    )
    half_width = float(settings.get_editor_property("plank_width")) * 0.5
    lateral_offset = max(
        0.0,
        half_width - float(settings.get_editor_property("anchor_lateral_inset")),
    )
    support_height = float(settings.get_editor_property("support_height"))
    deck_center_z = support_height + float(
        settings.get_editor_property("plank_height")
    ) * 0.5

    frame_errors = []
    anchor_constraint_paths = set()
    primary_count = 0
    secondary_count = 0
    for anchor_index, (
        token,
        support_property,
        x_sign,
        y_sign,
        secondary,
    ) in enumerate(ANCHOR_SPECS):
        matching = [
            constraint for constraint in constraints if token in constraint.get_name()
        ]
        if len(matching) != 1:
            raise RuntimeError(
                f"anchor {token} matched {len(matching)} constraints; expected one"
            )
        constraint = matching[0]
        anchor_constraint_paths.add(constraint.get_path_name())
        tagged_secondary = has_component_tag(constraint, SECONDARY_ANCHOR_TAG)
        if tagged_secondary != secondary:
            raise RuntimeError(f"anchor {token} has the wrong primary/secondary tag")
        validate_constraint_profile(constraint, settings, secondary)
        if secondary:
            secondary_count += 1
        else:
            primary_count += 1

        anchor_local = unreal.Vector(
            x_sign * half_span,
            y_sign * lateral_offset,
            deck_center_z,
        )
        expected_anchor = actor_local_to_world(bridge, anchor_local)
        component_error = vector_distance(
            constraint.get_world_location(), expected_anchor
        )
        if component_error > MAX_ANCHOR_COMPONENT_ERROR_CM:
            raise RuntimeError(
                f"anchor {token} component error={component_error:.3f}cm"
            )

        support_local = unreal.Vector(
            x_sign * half_span,
            y_sign * lateral_offset,
            support_height * 0.5,
        )
        support_error = vector_distance(
            supports[support_property].get_world_location(),
            actor_local_to_world(bridge, support_local),
        )
        if support_error > MAX_ANCHOR_COMPONENT_ERROR_CM:
            raise RuntimeError(
                f"support {support_property} frame error={support_error:.3f}cm"
            )

        frame_error = float(bridge.get_endpoint_position_error(anchor_index))
        if not math.isfinite(frame_error) or frame_error > MAX_ANCHOR_FRAME_ERROR_CM:
            raise RuntimeError(
                f"anchor {token} reference-frame error={frame_error:.3f}cm"
            )
        frame_errors.append(frame_error)

    internal_constraints = [
        constraint
        for constraint in constraints
        if constraint.get_path_name() not in anchor_constraint_paths
    ]
    expected_internal_constraints = (plank_count - 1) * 2
    if len(internal_constraints) != expected_internal_constraints:
        raise RuntimeError(
            f"internal constraint count={len(internal_constraints)} "
            f"expected={expected_internal_constraints}"
        )
    if lateral_offset <= MAX_INTERNAL_CONSTRAINT_LATERAL_ERROR_CM:
        raise RuntimeError(
            f"internal constraint lateral offset={lateral_offset:.3f}cm "
            "cannot form two distinct sides"
        )

    maximum_internal_lateral_error = 0.0
    for plank_index in range(plank_count - 1):
        pair_token = f"_{plank_index:02d}_{plank_index + 1:02d}"
        pair_constraints = [
            constraint
            for constraint in internal_constraints
            if pair_token in constraint.get_name()
        ]
        if len(pair_constraints) != 2:
            raise RuntimeError(
                f"internal pair {plank_index:02d}-{plank_index + 1:02d} "
                f"matched {len(pair_constraints)} constraints; expected two"
            )

        lateral_components = []
        for constraint in pair_constraints:
            local_y = actor_world_to_local_lateral(
                bridge, constraint.get_world_location()
            )
            if not math.isfinite(local_y):
                raise RuntimeError(
                    f"internal {constraint.get_name()} has a non-finite lateral position"
                )
            lateral_components.append((local_y, constraint))

        lateral_components.sort(key=lambda entry: entry[0])
        negative_constraint = lateral_components[0][1]
        positive_constraint = lateral_components[1][1]
        if not has_component_tag(
            negative_constraint, INTERNAL_NEGATIVE_Y_TAG
        ) or has_component_tag(negative_constraint, INTERNAL_POSITIVE_Y_TAG):
            raise RuntimeError(
                f"internal pair {plank_index:02d}-{plank_index + 1:02d} "
                "negative-Y constraint has invalid side tags"
            )
        if not has_component_tag(
            positive_constraint, INTERNAL_POSITIVE_Y_TAG
        ) or has_component_tag(positive_constraint, INTERNAL_NEGATIVE_Y_TAG):
            raise RuntimeError(
                f"internal pair {plank_index:02d}-{plank_index + 1:02d} "
                "positive-Y constraint has invalid side tags"
            )
        validate_constraint_profile(
            negative_constraint,
            settings,
            secondary=False,
            internal=True,
        )
        validate_constraint_profile(
            positive_constraint,
            settings,
            secondary=True,
            internal=True,
        )

        expected_sides = (-lateral_offset, lateral_offset)
        for (actual_y, constraint), expected_y in zip(
            lateral_components, expected_sides
        ):
            lateral_error = abs(actual_y - expected_y)
            maximum_internal_lateral_error = max(
                maximum_internal_lateral_error, lateral_error
            )
            if lateral_error > MAX_INTERNAL_CONSTRAINT_LATERAL_ERROR_CM:
                raise RuntimeError(
                    f"internal {constraint.get_name()} lateral={actual_y:.3f}cm "
                    f"expected={expected_y:.3f}cm error={lateral_error:.3f}cm"
                )

    return {
        "support_count": len(supports),
        "primary_anchor_count": primary_count,
        "secondary_anchor_count": secondary_count,
        "internal_constraint_pair_count": plank_count - 1,
        "maximum_internal_lateral_error": maximum_internal_lateral_error,
        "maximum_anchor_frame_error": max(frame_errors, default=0.0),
    }


def game_time():
    return unreal.GameplayStatics.get_time_seconds(state["world"])


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
    state["result"] = (ok, detail)
    if level_editor.is_in_play_in_editor():
        state["phase"] = "stopping"
        state["deadline"] = time.monotonic() + STOP_TIMEOUT_SECONDS
        level_editor.editor_request_end_play()
    else:
        shutdown()


def fail(detail):
    finish(False, detail)


def set_phase(name, duration=PHASE_TIMEOUT_SECONDS):
    state["phase"] = name
    state["phase_start_time"] = game_time()
    state["deadline"] = time.monotonic() + duration
    state["support_since"] = None


def elapsed_in_phase():
    return game_time() - state["phase_start_time"]


def sample_stability():
    bridge = state["bridge"]
    linear = float(bridge.get_maximum_plank_linear_speed())
    angular = float(bridge.get_maximum_plank_angular_speed_degrees())
    endpoint_error = float(bridge.get_maximum_endpoint_position_error())
    joint_error = float(bridge.get_maximum_adjacent_plank_distance_error())
    if not all(
        math.isfinite(value)
        for value in (linear, angular, endpoint_error, joint_error)
    ):
        fail("bridge physics produced a non-finite value")
        return None

    state["maximum_endpoint_error"] = max(
        state["maximum_endpoint_error"], endpoint_error
    )
    state["maximum_joint_error"] = max(
        state["maximum_joint_error"], joint_error
    )
    if endpoint_error > MAX_ENDPOINT_ERROR_CM:
        fail(f"anchor drift={endpoint_error:.2f}cm")
        return None
    if joint_error > MAX_ADJACENT_DISTANCE_ERROR_CM:
        fail(f"adjacent plank distance error={joint_error:.2f}cm")
        return None
    return linear, angular


def update_center_drop():
    baseline = state["baseline_center_location"]
    center = state["center_plank"].get_world_location()
    state["minimum_center_z"] = min(state["minimum_center_z"], center.z)
    drop = baseline.z - state["minimum_center_z"]
    if drop > MAX_CENTER_DROP_CM:
        fail(f"bridge center collapsed by {drop:.1f}cm")
        return None
    return drop


def begin_validation(world):
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    bridges = unreal.GameplayStatics.get_all_actors_with_tag(world, BRIDGE_TAG)
    if pawn is None or not bridges:
        state["last"] = (
            f"waiting for pawn/bridge pawn={pawn is not None} bridges={len(bridges)}"
        )
        return
    if len(bridges) != 1:
        fail(f"expected one tagged validation bridge, found {len(bridges)}")
        return

    bridge = bridges[0]
    expected_class = unreal.load_object(None, BRIDGE_BLUEPRINT_CLASS)
    if not isinstance(bridge, unreal.WorldRopeBridge):
        fail(f"tagged actor is not WorldRopeBridge: {bridge.get_class().get_name()}")
        return
    if expected_class is None or bridge.get_class() != expected_class:
        fail(f"tagged actor class={bridge.get_class().get_name()} expected=BP_RopeBridge_C")
        return

    settings = bridge.get_resolved_bridge_settings()
    expected_planks = int(settings.get_editor_property("plank_count"))
    expected_constraints = (expected_planks - 1) * 2 + 4
    if expected_planks < MIN_PLANK_COUNT:
        fail(
            f"configured plank count={expected_planks}; "
            f"expected at least {MIN_PLANK_COUNT}"
        )
        return
    if bridge.get_generated_plank_count() != expected_planks:
        fail(
            f"plank count={bridge.get_generated_plank_count()} "
            f"expected={expected_planks}"
        )
        return
    if bridge.get_generated_constraint_count() != expected_constraints:
        fail(
            f"constraint count={bridge.get_generated_constraint_count()} "
            f"expected={expected_constraints}"
        )
        return
    if not bridge.are_all_planks_simulating_physics():
        fail("one or more bridge planks are not simulating physics")
        return
    if not bridge.has_valid_constraint_configuration():
        fail("bridge constraint limits/locks/projection are invalid")
        return

    try:
        anchor_structure = validate_four_anchor_structure(
            bridge, settings, expected_constraints
        )
    except RuntimeError as error:
        fail(str(error))
        return

    expected_mass = float(settings.get_editor_property("plank_mass_kg"))
    for index in range(expected_planks):
        plank = bridge.get_plank_component(index)
        if not isinstance(plank, unreal.StaticMeshComponent):
            fail(f"plank {index} is missing")
            return
        actual_mass = float(plank.get_mass())
        if not nearly_equal(actual_mass, expected_mass, MASS_TOLERANCE_KG):
            fail(
                f"plank {index} mass={actual_mass:.2f}kg expected={expected_mass:.2f}kg"
            )
            return

    movement = pawn.get_component_by_class(unreal.CharacterMovementComponent)
    locomotion = pawn.get_locomotion_component()
    combat = pawn.get_combat_component()
    interaction_subsystem = unreal.RoverEditorTestLibrary.get_world_interaction_subsystem(
        world
    )
    movement_config = (
        locomotion.get_editor_property("movement_config") if locomotion else None
    )
    if (
        movement is None
        or combat is None
        or interaction_subsystem is None
        or not isinstance(movement_config, unreal.RoverMovementConfig)
    ):
        fail("Rover movement/combat/config is unavailable")
        return

    skeletal_collision = []
    for component in pawn.get_components_by_class(unreal.SkeletalMeshComponent):
        skeletal_collision.append(
            f"{component.get_name()}:collision={component.get_collision_enabled()}:"
            f"physics={component.is_simulating_physics()}:"
            f"overlap={bool(component.get_editor_property('generate_overlap_events'))}"
        )
    unreal.log(
        "PHYSICS_WORLD_ROPE_BRIDGE_CHARACTER_COLLISION "
        + ",".join(skeletal_collision)
    )
    movement_settings = movement_config.get_editor_property("settings")
    expected_character_mass = float(
        movement_settings.get_editor_property("physics_interaction_character_mass_kg")
    )
    expected_standing_force = float(
        movement_settings.get_editor_property(
            "physics_interaction_standing_downward_force_scale"
        )
    )
    actual_character_mass = float(movement.get_editor_property("mass"))
    actual_standing_force = float(
        movement.get_editor_property("standing_downward_force_scale")
    )
    original_initial_push_force = float(
        movement.get_editor_property("initial_push_force_factor")
    )
    original_push_force = float(movement.get_editor_property("push_force_factor"))
    attack_physics_push_scale = float(
        movement_settings.get_editor_property(
            "attack_physics_push_scale_on_simulated_base"
        )
    )
    attack_standing_force_scale = float(
        movement_settings.get_editor_property(
            "attack_standing_downward_force_scale_on_simulated_base"
        )
    )
    if not nearly_equal(actual_character_mass, expected_character_mass, 0.01):
        fail(
            f"character mass={actual_character_mass:.2f}kg "
            f"expected={expected_character_mass:.2f}kg"
        )
        return
    if not nearly_equal(actual_standing_force, expected_standing_force, 0.01):
        fail(
            f"standing force scale={actual_standing_force:.2f} "
            f"expected={expected_standing_force:.2f}"
        )
        return
    if not movement.get_editor_property("scale_push_force_to_velocity"):
        fail("CharacterMovement push force is not velocity-scaled")
        return
    if attack_physics_push_scale < 0.0 or attack_physics_push_scale > 1.0:
        fail(f"invalid attack physics push scale={attack_physics_push_scale:.3f}")
        return
    if attack_standing_force_scale < 0.0 or attack_standing_force_scale > 1.0:
        fail(f"invalid attack standing load scale={attack_standing_force_scale:.3f}")
        return

    movement_impulse = float(
        settings.get_editor_property("movement_impulse_at_reference_speed")
    )
    jump_scale = float(settings.get_editor_property("jump_takeoff_impulse_scale"))
    landing_scale = float(settings.get_editor_property("landing_impulse_scale"))
    if movement_impulse <= 0.0 or jump_scale <= 0.0 or landing_scale <= jump_scale:
        fail(
            "character-load settings must enable movement/jump and make landing "
            "stronger than takeoff"
        )
        return

    center = bridge.get_plank_component(expected_planks // 2)
    capsule = pawn.get_component_by_class(unreal.CapsuleComponent)
    capsule_half_height = (
        float(capsule.get_scaled_capsule_half_height()) if capsule else 90.0
    )
    safe_departure_location = (
        bridge.get_actor_location()
        + bridge.get_actor_right_vector()
        * (float(settings.get_editor_property("plank_width")) * 0.5 + 600.0)
        + bridge.get_actor_up_vector()
        * (
            float(settings.get_editor_property("support_height"))
            + capsule_half_height
            + 50.0
        )
    )
    movement.stop_movement_immediately()
    movement.set_movement_mode(unreal.MovementMode.MOVE_FLYING)
    if not pawn.set_actor_location(safe_departure_location, False, True):
        fail("unable to move Rover off the bridge for unloaded settling")
        return
    if vector_distance(pawn.get_actor_location(), safe_departure_location) > 2.0:
        fail("Rover did not reach the unloaded settling location")
        return
    state.update(
        {
            "world": world,
            "bridge": bridge,
            "pawn": pawn,
            "movement": movement,
            "locomotion": locomotion,
            "combat": combat,
            "interaction_subsystem": interaction_subsystem,
            "center_plank": center,
            "expected_planks": expected_planks,
            "expected_constraints": expected_constraints,
            "maximum_anchor_frame_error": anchor_structure[
                "maximum_anchor_frame_error"
            ],
            "support_count": anchor_structure["support_count"],
            "primary_anchor_count": anchor_structure["primary_anchor_count"],
            "secondary_anchor_count": anchor_structure[
                "secondary_anchor_count"
            ],
            "internal_constraint_pair_count": anchor_structure[
                "internal_constraint_pair_count"
            ],
            "maximum_internal_lateral_error": anchor_structure[
                "maximum_internal_lateral_error"
            ],
            "original_pawn_location": safe_departure_location,
            "plank_mass_kg": expected_mass,
            "character_mass_kg": actual_character_mass,
            "standing_force_scale": actual_standing_force,
            "original_initial_push_force": original_initial_push_force,
            "original_push_force": original_push_force,
            "attack_physics_push_scale": attack_physics_push_scale,
            "attack_standing_force_scale": attack_standing_force_scale,
        }
    )
    set_phase("settling")
    state["last"] = "waiting for a continuously quiet unloaded bridge"


def settle_bridge():
    sample = sample_stability()
    if sample is None:
        return
    now = game_time()
    quiet = (
        sample[0] <= SETTLED_LINEAR_SPEED
        and sample[1] <= SETTLED_ANGULAR_SPEED
        and elapsed_in_phase() >= SETTLE_MIN_SECONDS
    )
    if quiet:
        if state["quiet_since"] is None:
            state["quiet_since"] = now
    else:
        state["quiet_since"] = None
    quiet_for = 0.0 if state["quiet_since"] is None else now - state["quiet_since"]
    if quiet_for < SETTLE_QUIET_SECONDS:
        rest_error = float(
            state["bridge"].get_maximum_plank_rest_angular_error_degrees()
        )
        supported = state["bridge"].is_character_supported_by_bridge(state["pawn"])
        recovery_armed = state["bridge"].is_unloaded_recovery_armed()
        state["last"] = (
            f"unloaded settle linear={sample[0]:.2f}cm/s "
            f"angular={sample[1]:.2f}deg/s rest={rest_error:.2f}deg "
            f"supported={supported} armed={recovery_armed} "
            f"quiet={quiet_for:.2f}s"
        )
        return

    state["baseline_center_location"] = state["center_plank"].get_world_location()
    state["initial_rest_angular_error"] = float(
        state["bridge"].get_maximum_plank_rest_angular_error_degrees()
    )
    state["minimum_center_z"] = state["baseline_center_location"].z
    if not state["bridge"].apply_impulse_to_center_plank(
        unreal.Vector(0.0, 90.0, -240.0)
    ):
        fail("unable to apply the controlled center-plank impulse")
        return
    set_phase("impulse_response")
    state["last"] = "sampling controlled bridge impulse"


def sample_impulse_response():
    sample = sample_stability()
    if sample is None:
        return
    state["impulse_peak_linear"] = max(state["impulse_peak_linear"], sample[0])
    state["impulse_peak_angular"] = max(state["impulse_peak_angular"], sample[1])
    if elapsed_in_phase() < IMPULSE_SAMPLE_SECONDS:
        state["last"] = (
            f"impulse linear={state['impulse_peak_linear']:.2f}cm/s "
            f"angular={state['impulse_peak_angular']:.2f}deg/s"
        )
        return
    if state["impulse_peak_linear"] < 3.0 or state["impulse_peak_angular"] < 2.0:
        fail(
            f"bridge did not respond in translation/rotation "
            f"linear={state['impulse_peak_linear']:.2f} "
            f"angular={state['impulse_peak_angular']:.2f}"
        )
        return
    set_phase("impulse_decay")
    state["last"] = "waiting for controlled impulse damping"


def validate_impulse_decay():
    sample = sample_stability()
    if sample is None:
        return
    if elapsed_in_phase() < IMPULSE_DECAY_SECONDS:
        state["last"] = (
            f"impulse decay linear={sample[0]:.2f}cm/s "
            f"angular={sample[1]:.2f}deg/s"
        )
        return
    state["impulse_decay_linear"] = sample[0]
    state["impulse_decay_angular"] = sample[1]
    linear_limit = min(18.0, state["impulse_peak_linear"] * 0.70)
    angular_limit = min(80.0, state["impulse_peak_angular"] * 0.90)
    if sample[0] > linear_limit or sample[1] > angular_limit:
        fail(
            f"controlled response did not decay: peak="
            f"{state['impulse_peak_linear']:.1f}/{state['impulse_peak_angular']:.1f} "
            f"remaining={sample[0]:.1f}/{sample[1]:.1f}"
        )
        return
    place_character_over_center(20.0, "waiting_for_support")


def place_character_over_center(drop_height, next_phase):
    pawn = state["pawn"]
    movement = state["movement"]
    center = state["center_plank"].get_world_location()
    capsule = pawn.get_component_by_class(unreal.CapsuleComponent)
    capsule_half_height = (
        float(capsule.get_scaled_capsule_half_height()) if capsule else 90.0
    )
    location = center + unreal.Vector(
        0.0, 0.0, capsule_half_height + drop_height
    )
    movement.stop_movement_immediately()
    if not pawn.set_actor_location(location, False, True):
        fail("unable to place Rover over the bridge")
        return
    movement.set_movement_mode(unreal.MovementMode.MOVE_FALLING)
    set_phase(next_phase)
    state["last"] = "waiting for Rover movement base to become a bridge plank"


def wait_for_support(next_phase):
    sample = sample_stability()
    if sample is None:
        return
    update_center_drop()
    if state["result"] is not None:
        return
    if not state["bridge"].is_character_supported_by_bridge(state["pawn"]):
        state["support_since"] = None
        state["last"] = (
            f"Rover has not acquired a bridge movement base; "
            f"linear={sample[0]:.1f}cm/s"
        )
        return
    state["movement"].stop_movement_immediately()
    if state["support_since"] is None:
        state["support_since"] = game_time()
    support_duration = game_time() - state["support_since"]
    if support_duration < SUPPORT_CONFIRM_SECONDS:
        state["last"] = (
            f"confirming continuous bridge support {support_duration:.2f}/"
            f"{SUPPORT_CONFIRM_SECONDS:.2f}s"
        )
        return
    state["bridge"].reset_character_response_debug()
    if next_phase in ("walking", "running"):
        speed = WALK_TEST_SPEED if next_phase == "walking" else RUN_TEST_SPEED
        direction = state["bridge"].get_actor_forward_vector()
        state["pawn"].add_movement_input(direction, 1.0, False)
        state["movement"].set_editor_property("velocity", direction * speed)
    set_phase(next_phase)
    state["last"] = f"Rover support confirmed; entering {next_phase}"


def sample_stationary():
    sample = sample_stability()
    if sample is None:
        return
    if not state["bridge"].is_character_supported_by_bridge(state["pawn"]):
        fail("Rover lost bridge support during stationary sample")
        return
    drop = update_center_drop()
    if drop is None:
        return
    state["standing_peak_linear"] = max(state["standing_peak_linear"], sample[0])
    state["standing_peak_angular"] = max(state["standing_peak_angular"], sample[1])
    if state["bridge"].get_last_movement_impulse_magnitude() > 0.01:
        fail("stationary Rover incorrectly generated a movement impulse")
        return
    if elapsed_in_phase() < STATIONARY_SAMPLE_SECONDS:
        state["last"] = (
            f"stationary linear={sample[0]:.1f}cm/s "
            f"angular={sample[1]:.1f}deg/s drop={drop:.1f}cm"
        )
        return
    set_phase("attack_idle_control")
    state["last"] = "sampling an equal-duration idle control before Attack01"


def sample_attack_idle_control():
    sample = sample_stability()
    if sample is None:
        return
    if not state["bridge"].is_character_supported_by_bridge(state["pawn"]):
        fail("Rover lost bridge support during the pre-attack idle control")
        return
    state["attack_idle_peak_linear"] = max(
        state["attack_idle_peak_linear"], sample[0]
    )
    state["attack_idle_peak_angular"] = max(
        state["attack_idle_peak_angular"], sample[1]
    )
    if elapsed_in_phase() < ATTACK_IDLE_CONTROL_SECONDS:
        state["last"] = (
            f"pre-attack idle control={sample[0]:.1f}cm/s/"
            f"{sample[1]:.1f}deg/s"
        )
        return
    set_phase("begin_attack")
    state["last"] = "idle control complete; waiting for a quiet bridge before Attack01"


def reset_attack_sample_metrics():
    state.update(
        {
            "attack_request_id": 0,
            "attack_seen_active": False,
            "attack_peak_movement_impulse": 0.0,
            "attack_peak_linear": 0.0,
            "attack_peak_angular": 0.0,
            "attack_push_scale_observed": False,
            "attack_push_force_factor": 0.0,
            "attack_start_pawn_location": None,
            "attack_max_pawn_displacement": 0.0,
            "attack_max_horizontal_speed": 0.0,
            "attack_max_vertical_speed": 0.0,
            "attack_peak_plank_index": -1,
            "attack_peak_nearest_plank_index": -1,
            "attack_peak_phase": "unknown",
            "attack_peak_weapon_trace": False,
            "attack_peak_elapsed": 0.0,
            "attack_peak_initial_push_force": 0.0,
            "attack_peak_push_force": 0.0,
            "attack_peak_standing_force_scale": 0.0,
            "attack_restore_wait_started": None,
            "attack_delta_total_seconds": 0.0,
            "attack_delta_frame_count": 0,
            "attack_max_delta_seconds": 0.0,
            "attack_request_call_seconds": 0.0,
            "attack_start_interaction_count": 0,
            "attack_world_interaction_count": 0,
            "attack_advance_seen": False,
            "attack_anim_root_motion_seen": False,
            "attack_reference_plank": None,
            "attack_start_relative_location": None,
            "attack_max_relative_displacement": 0.0,
        }
    )


def complete_attack_sample():
    label = "first" if not state["attack_samples"] else "repeat"
    frame_count = state["attack_delta_frame_count"]
    average_delta = (
        state["attack_delta_total_seconds"] / frame_count
        if frame_count > 0
        else 0.0
    )
    sample = {
        "label": label,
        "request_id": state["attack_request_id"],
        "baseline_linear": state["attack_baseline_linear"],
        "baseline_angular": state["attack_baseline_angular"],
        "peak_linear": state["attack_peak_linear"],
        "peak_angular": state["attack_peak_angular"],
        "peak_movement_impulse": state["attack_peak_movement_impulse"],
        "peak_plank_index": state["attack_peak_plank_index"],
        "peak_nearest_plank_index": state["attack_peak_nearest_plank_index"],
        "peak_phase": state["attack_peak_phase"],
        "peak_weapon_trace": state["attack_peak_weapon_trace"],
        "peak_elapsed": state["attack_peak_elapsed"],
        "peak_initial_push_force": state["attack_peak_initial_push_force"],
        "peak_push_force": state["attack_peak_push_force"],
        "peak_standing_force_scale": state["attack_peak_standing_force_scale"],
        "max_pawn_displacement": state["attack_max_pawn_displacement"],
        "max_horizontal_speed": state["attack_max_horizontal_speed"],
        "max_vertical_speed": state["attack_max_vertical_speed"],
        "average_delta_seconds": average_delta,
        "max_delta_seconds": state["attack_max_delta_seconds"],
        "request_call_seconds": state["attack_request_call_seconds"],
        "frame_count": frame_count,
    }
    state["attack_samples"].append(sample)
    unreal.log(
        "PHYSICS_WORLD_ROPE_BRIDGE_ATTACK_SAMPLE "
        f"label={label} request={sample['request_id']} "
        f"peak={sample['peak_linear']:.1f}cm/s/"
        f"{sample['peak_angular']:.1f}deg/s "
        f"plank={sample['peak_plank_index']} "
        f"nearest={sample['peak_nearest_plank_index']} "
        f"phase={sample['peak_phase']} trace={sample['peak_weapon_trace']} "
        f"movement_push={sample['peak_movement_impulse']:.2f} "
        f"character_push={sample['peak_initial_push_force']:.1f}/"
        f"{sample['peak_push_force']:.1f}/"
        f"standing={sample['peak_standing_force_scale']:.2f} "
        f"delta_avg={sample['average_delta_seconds'] * 1000.0:.2f}ms "
        f"delta_max={sample['max_delta_seconds'] * 1000.0:.2f}ms "
        f"request_call={sample['request_call_seconds'] * 1000.0:.2f}ms "
        f"frames={sample['frame_count']}"
    )
    return sample


def classify_first_load_hitch():
    if len(state["attack_samples"]) != 2:
        return
    first, repeat = state["attack_samples"]
    first_delta = first["max_delta_seconds"]
    repeat_delta = repeat["max_delta_seconds"]
    ratio = first_delta / repeat_delta if repeat_delta > 1.0e-6 else 0.0
    state["attack_delta_ratio"] = ratio
    state["attack_first_load_hitch"] = (
        first_delta >= FIRST_LOAD_HITCH_MIN_DELTA_SECONDS
        and first_delta - repeat_delta >= FIRST_LOAD_HITCH_MIN_DELTA_GAP_SECONDS
        and ratio >= FIRST_LOAD_HITCH_MIN_DELTA_RATIO
    )
    message = (
        "PHYSICS_WORLD_ROPE_BRIDGE_ATTACK_LOAD_COMPARISON "
        f"first_delta={first_delta * 1000.0:.2f}ms "
        f"repeat_delta={repeat_delta * 1000.0:.2f}ms ratio={ratio:.2f} "
        f"first_request={first['request_call_seconds'] * 1000.0:.2f}ms "
        f"repeat_request={repeat['request_call_seconds'] * 1000.0:.2f}ms "
        f"first_load_hitch={state['attack_first_load_hitch']}"
    )
    (unreal.log_warning if state["attack_first_load_hitch"] else unreal.log)(message)


def begin_bridge_attack():
    sample = sample_stability()
    if sample is None:
        return
    if not state["bridge"].is_character_supported_by_bridge(state["pawn"]):
        state["last"] = "waiting for stable bridge support before Attack01"
        return
    if (
        sample[0] > ATTACK_SETTLED_LINEAR_SPEED
        or sample[1] > ATTACK_SETTLED_ANGULAR_SPEED
    ):
        state["last"] = (
            f"waiting for a quiet bridge before Attack01 "
            f"linear={sample[0]:.1f}cm/s angular={sample[1]:.1f}deg/s"
        )
        return

    if len(state["attack_samples"]) >= 2:
        fail("bridge attack probe attempted more than two Attack01 samples")
        return

    reset_attack_sample_metrics()
    state["movement"].stop_movement_immediately()
    state["bridge"].reset_character_response_debug()
    state["attack_baseline_linear"] = sample[0]
    state["attack_baseline_angular"] = sample[1]
    combat = state["combat"]
    combat.set_light_attack_held(False)
    if combat.is_attacking():
        fail("Rover was already attacking before the bridge attack probe")
        return
    request_started = time.perf_counter()
    request_accepted = combat.request_light_attack()
    state["attack_request_call_seconds"] = time.perf_counter() - request_started
    if not request_accepted:
        fail("grounded bridge Attack01 request was rejected")
        return

    request_id = int(combat.get_attack_request_id())
    if request_id <= 0 or int(combat.get_current_combo_index()) != 1:
        fail(
            f"bridge Attack01 request={request_id} "
            f"combo={combat.get_current_combo_index()}"
        )
        return
    state["attack_request_id"] = request_id
    state["attack_start_interaction_count"] = int(
        state["interaction_subsystem"].get_processed_request_count()
    )
    state["attack_seen_active"] = bool(combat.is_attacking())
    state["attack_start_pawn_location"] = state["pawn"].get_actor_location()
    reference_plank = min(
        (
            state["bridge"].get_plank_component(index)
            for index in range(state["expected_planks"])
        ),
        key=lambda plank: vector_distance(
            plank.get_world_location(), state["attack_start_pawn_location"]
        ),
    )
    state["attack_reference_plank"] = reference_plank
    state["attack_start_relative_location"] = (
        state["attack_start_pawn_location"] - reference_plank.get_world_location()
    )
    set_phase("attacking_on_bridge")
    state["last"] = f"sampling bridge movement impulse during Attack01 request={request_id}"


def sample_bridge_attack():
    sample = sample_stability()
    if sample is None:
        return
    if state["locomotion"].is_combat_attack_advance_active():
        state["attack_advance_seen"] = True
        fail("Attack01 created a combat advance Root Motion Source on the bridge")
        return
    state["attack_anim_root_motion_seen"] = (
        state["attack_anim_root_motion_seen"]
        or state["locomotion"].has_active_animation_root_motion()
    )
    delta_seconds = state["current_delta_seconds"]
    if math.isfinite(delta_seconds) and delta_seconds > 0.0:
        state["attack_delta_total_seconds"] += delta_seconds
        state["attack_delta_frame_count"] += 1
        state["attack_max_delta_seconds"] = max(
            state["attack_max_delta_seconds"], delta_seconds
        )
    actual_initial_push = float(
        state["movement"].get_editor_property("initial_push_force_factor")
    )
    actual_push = float(
        state["movement"].get_editor_property("push_force_factor")
    )
    actual_standing_force = float(
        state["movement"].get_editor_property("standing_downward_force_scale")
    )
    if sample[0] > state["attack_peak_linear"]:
        state["attack_peak_linear"] = sample[0]
        fastest_index = -1
        fastest_speed = -1.0
        nearest_index = -1
        nearest_distance = float("inf")
        pawn_location_for_peak = state["pawn"].get_actor_location()
        for plank_index in range(state["expected_planks"]):
            plank = state["bridge"].get_plank_component(plank_index)
            if plank is None:
                continue
            plank_velocity = plank.get_physics_linear_velocity()
            plank_speed = math.sqrt(
                plank_velocity.x ** 2
                + plank_velocity.y ** 2
                + plank_velocity.z ** 2
            )
            if plank_speed > fastest_speed:
                fastest_speed = plank_speed
                fastest_index = plank_index
            plank_distance = vector_distance(
                plank.get_world_location(), pawn_location_for_peak
            )
            if plank_distance < nearest_distance:
                nearest_distance = plank_distance
                nearest_index = plank_index
        state["attack_peak_plank_index"] = fastest_index
        state["attack_peak_nearest_plank_index"] = nearest_index
        state["attack_peak_phase"] = str(state["combat"].get_combat_phase())
        state["attack_peak_weapon_trace"] = bool(
            state["combat"].is_weapon_trace_active()
        )
        state["attack_peak_elapsed"] = elapsed_in_phase()
        state["attack_peak_initial_push_force"] = actual_initial_push
        state["attack_peak_push_force"] = actual_push
        state["attack_peak_standing_force_scale"] = actual_standing_force
    state["attack_peak_angular"] = max(state["attack_peak_angular"], sample[1])
    pawn_location = state["pawn"].get_actor_location()
    reference_plank = state["attack_reference_plank"]
    if reference_plank is not None:
        current_relative_location = pawn_location - reference_plank.get_world_location()
        state["attack_max_relative_displacement"] = max(
            state["attack_max_relative_displacement"],
            vector_distance(
                current_relative_location,
                state["attack_start_relative_location"],
            ),
        )
    pawn_velocity = state["movement"].get_editor_property("velocity")
    state["attack_max_pawn_displacement"] = max(
        state["attack_max_pawn_displacement"],
        vector_distance(pawn_location, state["attack_start_pawn_location"]),
    )
    state["attack_max_horizontal_speed"] = max(
        state["attack_max_horizontal_speed"],
        math.hypot(pawn_velocity.x, pawn_velocity.y),
    )
    state["attack_max_vertical_speed"] = max(
        state["attack_max_vertical_speed"], abs(pawn_velocity.z)
    )
    if not state["bridge"].is_character_supported_by_bridge(state["pawn"]):
        fail("Rover lost bridge support during Attack01")
        return
    if update_center_drop() is None:
        return

    movement_impulse = float(
        state["bridge"].get_last_movement_impulse_magnitude()
    )
    if not math.isfinite(movement_impulse):
        fail("bridge Attack01 movement impulse is non-finite")
        return
    state["attack_peak_movement_impulse"] = max(
        state["attack_peak_movement_impulse"], movement_impulse
    )
    state["attack_world_interaction_count"] = max(
        0,
        int(state["interaction_subsystem"].get_processed_request_count())
        - state["attack_start_interaction_count"],
    )
    if movement_impulse > 0.01:
        fail(
            f"Attack01 generated a bridge movement impulse={movement_impulse:.2f}; "
            "root-motion combat movement must be suppressed"
        )
        return

    combat = state["combat"]
    if combat.is_attacking():
        state["attack_seen_active"] = True
        if int(combat.get_attack_request_id()) != state["attack_request_id"]:
            fail("bridge Attack01 changed RequestId without another attack input")
            return
        expected_initial_push = (
            state["original_initial_push_force"]
            * state["attack_physics_push_scale"]
        )
        expected_push = (
            state["original_push_force"]
            * state["attack_physics_push_scale"]
        )
        expected_standing_force = (
            state["standing_force_scale"]
            * state["attack_standing_force_scale"]
        )
        if not nearly_equal(actual_initial_push, expected_initial_push, 0.01) or not nearly_equal(
            actual_push, expected_push, 0.01
        ) or not nearly_equal(actual_standing_force, expected_standing_force, 0.01):
            fail(
                f"Attack01 physics push suppression ended before the Montage: "
                f"phase={combat.get_combat_phase()} "
                f"initial={actual_initial_push:.2f}/{expected_initial_push:.2f} "
                f"push={actual_push:.2f}/{expected_push:.2f} "
                f"standing={actual_standing_force:.2f}/{expected_standing_force:.2f}"
            )
            return
        state["attack_push_scale_observed"] = True
        state["attack_push_force_factor"] = actual_push
        state["last"] = (
            f"Attack01 active movement_impulse={movement_impulse:.2f} "
            f"bridge={sample[0]:.1f}cm/s/{sample[1]:.1f}deg/s"
        )
        return

    if not state["attack_seen_active"]:
        fail("bridge Attack01 never entered the active attack state")
        return
    if not state["attack_push_scale_observed"]:
        fail("Attack01 never applied the simulated-base physics push scale")
        return
    if state["locomotion"].is_combat_attack_advance_active():
        fail("bridge Attack01 left an attack advance Root Motion Source active")
        return
    restored_initial_push = float(
        state["movement"].get_editor_property("initial_push_force_factor")
    )
    restored_push = float(state["movement"].get_editor_property("push_force_factor"))
    restored_standing_force = float(
        state["movement"].get_editor_property("standing_downward_force_scale")
    )
    bRestoreComplete = (
        nearly_equal(restored_initial_push, state["original_initial_push_force"], 0.01)
        and nearly_equal(restored_push, state["original_push_force"], 0.01)
        and nearly_equal(restored_standing_force, state["standing_force_scale"], 0.01)
    )
    if not bRestoreComplete:
        now = game_time()
        if state["attack_restore_wait_started"] is None:
            state["attack_restore_wait_started"] = now
        restore_wait = now - state["attack_restore_wait_started"]
        if restore_wait > ATTACK_RESTORE_GRACE_SECONDS:
            fail(
                f"Attack01 physics restore did not complete: "
                f"initial={restored_initial_push:.2f}/"
                f"{state['original_initial_push_force']:.2f} "
                f"push={restored_push:.2f}/{state['original_push_force']:.2f} "
                f"standing={restored_standing_force:.2f}/"
                f"{state['standing_force_scale']:.2f}"
            )
            return
        state["last"] = (
            f"waiting for smooth attack physics restore {restore_wait:.2f}s "
            f"push={restored_push:.1f} standing={restored_standing_force:.2f}"
        )
        return
    complete_attack_sample()
    if len(state["attack_samples"]) == 1:
        state["attack_repeat_quiet_since"] = None
        set_phase("waiting_for_repeat_attack")
        state["last"] = (
            "first Attack01 complete; waiting for combo reset and a quiet bridge"
        )
        return

    classify_first_load_hitch()
    place_character_over_center(12.0, "support_before_walk")


def wait_for_repeat_attack():
    sample = sample_stability()
    if sample is None:
        return
    if not state["bridge"].is_character_supported_by_bridge(state["pawn"]):
        fail("Rover lost bridge support while waiting for the repeat Attack01")
        return
    if update_center_drop() is None:
        return

    combat = state["combat"]
    if combat.is_attacking():
        fail("first Attack01 remained active after its completion sample")
        return
    combo_index = int(combat.get_current_combo_index())
    reset_remaining = float(combat.get_combo_reset_remaining())
    waited_long_enough = elapsed_in_phase() >= ATTACK_REPEAT_MIN_WAIT_SECONDS
    combo_reset = combo_index == -1 and reset_remaining <= 0.0
    quiet = (
        sample[0] <= ATTACK_SETTLED_LINEAR_SPEED
        and sample[1] <= ATTACK_SETTLED_ANGULAR_SPEED
    )
    if not waited_long_enough or not combo_reset or not quiet:
        state["attack_repeat_quiet_since"] = None
        state["last"] = (
            f"repeat Attack01 wait={elapsed_in_phase():.2f}s "
            f"combo={combo_index}/{reset_remaining:.2f}s "
            f"bridge={sample[0]:.1f}cm/s/{sample[1]:.1f}deg/s"
        )
        return

    now = game_time()
    if state["attack_repeat_quiet_since"] is None:
        state["attack_repeat_quiet_since"] = now
    quiet_for = now - state["attack_repeat_quiet_since"]
    if quiet_for < ATTACK_REPEAT_QUIET_SECONDS:
        state["last"] = (
            f"confirming quiet bridge before repeat Attack01 "
            f"{quiet_for:.2f}/{ATTACK_REPEAT_QUIET_SECONDS:.2f}s"
        )
        return

    begin_bridge_attack()


def drive_character(speed, peak_prefix):
    sample = sample_stability()
    if sample is None:
        return None
    if not state["bridge"].is_character_supported_by_bridge(state["pawn"]):
        pawn_location = state["pawn"].get_actor_location()
        center_location = state["center_plank"].get_world_location()
        velocity = state["movement"].get_editor_property("velocity")
        movement_mode = state["movement"].get_editor_property("movement_mode")
        fail(
            f"Rover left the bridge before {peak_prefix} response was sampled: "
            f"center_distance={vector_distance(pawn_location, center_location):.1f}cm "
            f"velocity={velocity.x:.1f},{velocity.y:.1f},{velocity.z:.1f} "
            f"mode={movement_mode} "
            f"attack_advance={state['locomotion'].is_combat_attack_advance_active()}"
        )
        return None
    direction = state["bridge"].get_actor_forward_vector()
    state["pawn"].add_movement_input(direction, 1.0, False)
    state["movement"].set_editor_property("velocity", direction * speed)
    state[f"{peak_prefix}_peak_linear"] = max(
        state[f"{peak_prefix}_peak_linear"], sample[0]
    )
    state[f"{peak_prefix}_peak_angular"] = max(
        state[f"{peak_prefix}_peak_angular"], sample[1]
    )
    update_center_drop()
    if state["result"] is not None:
        return None
    return float(state["bridge"].get_last_movement_impulse_magnitude())


def sample_walking():
    existing_impulse = float(
        state["bridge"].get_last_movement_impulse_magnitude()
    )
    if existing_impulse > 0.0:
        state["walk_impulse"] = existing_impulse
        place_character_over_center(12.0, "support_after_walk")
        return
    impulse = drive_character(WALK_TEST_SPEED, "walk")
    if impulse is None:
        return
    if impulse <= 0.0:
        state["last"] = "waiting for speed-scaled walking impulse"
        return
    state["walk_impulse"] = impulse
    place_character_over_center(12.0, "support_after_walk")


def sample_running():
    existing_impulse = float(
        state["bridge"].get_last_movement_impulse_magnitude()
    )
    if existing_impulse > 0.0:
        state["run_impulse"] = existing_impulse
        if state["run_impulse"] <= state["walk_impulse"] * 1.5:
            fail(
                f"running impulse={state['run_impulse']:.1f} did not exceed "
                f"walking impulse={state['walk_impulse']:.1f}"
            )
            return
        place_character_over_center(12.0, "support_before_jump")
        return
    impulse = drive_character(RUN_TEST_SPEED, "run")
    if impulse is None:
        return
    if impulse <= 0.0:
        state["last"] = "waiting for speed-scaled running impulse"
        return
    state["run_impulse"] = impulse
    if state["run_impulse"] <= state["walk_impulse"] * 1.5:
        fail(
            f"running impulse={state['run_impulse']:.1f} did not exceed "
            f"walking impulse={state['walk_impulse']:.1f}"
        )
        return
    place_character_over_center(12.0, "support_before_jump")


def begin_jump():
    sample = sample_stability()
    if sample is None:
        return
    if not state["bridge"].is_character_supported_by_bridge(state["pawn"]):
        state["last"] = "waiting for stable bridge support before jump"
        return
    state["movement"].stop_movement_immediately()
    state["bridge"].reset_character_response_debug()
    if not state["locomotion"].try_jump():
        fail("Rover locomotion rejected a supported bridge jump")
        return
    set_phase("jump_airborne")
    state["last"] = "waiting for bridge takeoff and landing impulses"


def sample_jump_airborne():
    sample = sample_stability()
    if sample is None:
        return
    state["jump_peak_linear"] = max(state["jump_peak_linear"], sample[0])
    state["jump_peak_angular"] = max(state["jump_peak_angular"], sample[1])
    takeoff = float(state["bridge"].get_last_jump_takeoff_impulse_magnitude())
    if takeoff > 0.0:
        state["jump_takeoff_impulse"] = takeoff
    supported = state["bridge"].is_character_supported_by_bridge(state["pawn"])
    landing = float(state["bridge"].get_last_landing_impulse_magnitude())
    if not supported or landing <= 0.0:
        velocity = state["movement"].get_editor_property("velocity")
        movement_mode = state["movement"].get_editor_property("movement_mode")
        pawn_height = (
            state["pawn"].get_actor_location().z
            - state["center_plank"].get_world_location().z
        )
        state["last"] = (
            f"jump takeoff={state['jump_takeoff_impulse']:.1f} "
            f"supported={supported} landing={landing:.1f} "
            f"velocity_z={velocity.z:.1f} height={pawn_height:.1f}cm "
            f"mode={movement_mode}"
        )
        return
    if state["jump_takeoff_impulse"] <= 0.0:
        fail("bridge did not receive a jump takeoff impulse")
        return
    state["landing_impulse"] = landing
    if landing <= state["jump_takeoff_impulse"] or landing <= state["run_impulse"]:
        fail(
            f"landing impulse={landing:.1f} must exceed takeoff="
            f"{state['jump_takeoff_impulse']:.1f} and run={state['run_impulse']:.1f}"
        )
        return
    set_phase("post_landing")
    state["last"] = "sampling the physical response after a real jump landing"


def sample_post_landing():
    sample = sample_stability()
    if sample is None:
        return
    state["landing_peak_linear"] = max(state["landing_peak_linear"], sample[0])
    state["landing_peak_angular"] = max(state["landing_peak_angular"], sample[1])
    drop = update_center_drop()
    if drop is None:
        return
    if elapsed_in_phase() < POST_LANDING_SAMPLE_SECONDS:
        state["last"] = (
            f"landing peak={state['landing_peak_linear']:.1f}cm/s "
            f"angular={state['landing_peak_angular']:.1f}deg/s drop={drop:.1f}cm"
        )
        return
    if state["landing_peak_linear"] < 4.0 or state["landing_peak_angular"] < 2.0:
        fail("real jump landing produced no measurable bridge response")
        return

    state["movement"].stop_movement_immediately()
    original = state["original_pawn_location"]
    if not state["pawn"].set_actor_location(original, False, True):
        fail("unable to move Rover off the bridge")
        return
    if vector_distance(state["pawn"].get_actor_location(), original) > 2.0:
        fail("Rover off-bridge teleport did not reach its requested location")
        return
    state["movement"].set_movement_mode(unreal.MovementMode.MOVE_FALLING)
    set_phase("confirm_departure")
    state["last"] = "confirming Rover is no longer supported by the bridge"


def confirm_departure():
    sample = sample_stability()
    if sample is None:
        return
    if state["bridge"].is_character_supported_by_bridge(state["pawn"]):
        state["last"] = "Rover still reports a bridge movement base"
        return
    if elapsed_in_phase() < 0.25:
        return
    set_phase("recovery")
    state["last"] = "waiting for unloaded bridge linear/angular recovery"


def validate_recovery():
    sample = sample_stability()
    if sample is None:
        return
    if elapsed_in_phase() < RECOVERY_SECONDS:
        rest_error = float(
            state["bridge"].get_maximum_plank_rest_angular_error_degrees()
        )
        recovery_armed = state["bridge"].is_unloaded_recovery_armed()
        state["last"] = (
            f"recovery linear={sample[0]:.2f}cm/s angular={sample[1]:.2f}deg/s "
            f"rest={rest_error:.2f}deg armed={recovery_armed}"
        )
        return
    state["recovery_linear"] = sample[0]
    state["recovery_angular"] = sample[1]
    linear_limit = min(25.0, state["landing_peak_linear"] * 0.60)
    angular_limit = min(80.0, state["landing_peak_angular"] * 0.80)
    if sample[0] > linear_limit or sample[1] > angular_limit:
        fail(
            f"bridge remained too active after Rover left: peak="
            f"{state['landing_peak_linear']:.1f}/{state['landing_peak_angular']:.1f} "
            f"remaining={sample[0]:.1f}/{sample[1]:.1f}"
        )
        return

    rest_angular_error = float(
        state["bridge"].get_maximum_plank_rest_angular_error_degrees()
    )
    if not math.isfinite(rest_angular_error):
        fail("bridge recovery rest-pose angular error is non-finite")
        return
    state["recovery_rest_angular_error"] = rest_angular_error
    if rest_angular_error > MAX_REST_ANGULAR_ERROR_DEGREES:
        plank_errors = [
            float(state["bridge"].get_plank_rest_angular_error_degrees(index))
            for index in range(state["expected_planks"])
        ]
        worst_plank = max(range(len(plank_errors)), key=plank_errors.__getitem__)
        worst_entries = sorted(
            enumerate(plank_errors), key=lambda item: item[1], reverse=True
        )[:5]
        worst_rotation = state["bridge"].get_plank_component(
            worst_plank
        ).get_world_rotation()
        worst_target_rotation = state["bridge"].get_plank_natural_rest_rotation(
            worst_plank
        )
        fail(
            f"bridge did not recover its rest pose: "
            f"angular_error={rest_angular_error:.2f}deg "
            f"limit={MAX_REST_ANGULAR_ERROR_DEGREES:.2f}deg "
            f"initial={state['initial_rest_angular_error']:.2f}deg "
            f"armed={state['bridge'].is_unloaded_recovery_armed()} "
            f"worst_plank={worst_plank} "
            f"top_errors={','.join(f'{index}:{error:.2f}' for index, error in worst_entries)} "
            f"rotation={worst_rotation.pitch:.2f},"
            f"{worst_rotation.yaw:.2f},{worst_rotation.roll:.2f} "
            f"target={worst_target_rotation.pitch:.2f},"
            f"{worst_target_rotation.yaw:.2f},{worst_target_rotation.roll:.2f}"
        )
        return

    center_drop = (
        state["baseline_center_location"].z - state["minimum_center_z"]
    )
    if len(state["attack_samples"]) != 2:
        fail(
            f"attack comparison samples={len(state['attack_samples'])} expected=2"
        )
        return
    first_attack, repeat_attack = state["attack_samples"]
    finish(
        True,
        " ".join(
            (
                f"planks={state['expected_planks']}",
                f"constraints={state['expected_constraints']}",
                f"supports={state['support_count']}",
                f"anchor_profiles={state['primary_anchor_count']}/"
                f"{state['secondary_anchor_count']}",
                f"internal_pairs={state['internal_constraint_pair_count']}",
                f"internal_lateral_error="
                f"{state['maximum_internal_lateral_error']:.3f}cm",
                f"plank_mass={state['plank_mass_kg']:.2f}kg",
                f"character_mass={state['character_mass_kg']:.1f}kg",
                f"standing_force_scale={state['standing_force_scale']:.2f}",
                f"standing_peak={state['standing_peak_linear']:.1f}cm/s",
                f"standing_angular={state['standing_peak_angular']:.1f}deg/s",
                f"attack_movement_impulse="
                f"{state['attack_peak_movement_impulse']:.2f}",
                f"attack_baseline={state['attack_baseline_linear']:.1f}cm/s/"
                f"{state['attack_baseline_angular']:.1f}deg/s",
                f"attack_idle={state['attack_idle_peak_linear']:.1f}cm/s/"
                f"{state['attack_idle_peak_angular']:.1f}deg/s",
                f"attack_peak={state['attack_peak_linear']:.1f}cm/s",
                f"attack_angular={state['attack_peak_angular']:.1f}deg/s",
                f"attack_push_force={state['attack_push_force_factor']:.1f}",
                f"attack_world_interactions={state['attack_world_interaction_count']}",
                f"attack_advance_seen={state['attack_advance_seen']}",
                f"attack_anim_root_motion_seen={state['attack_anim_root_motion_seen']}",
                f"attack_relative_move={state['attack_max_relative_displacement']:.1f}cm",
                f"attack_pawn_move={state['attack_max_pawn_displacement']:.1f}cm",
                f"attack_pawn_speed={state['attack_max_horizontal_speed']:.1f}/"
                f"{state['attack_max_vertical_speed']:.1f}cm/s",
                f"attack_peak_detail={state['attack_peak_plank_index']}/"
                f"{state['attack_peak_nearest_plank_index']}/"
                f"{state['attack_peak_phase']}/"
                f"trace={state['attack_peak_weapon_trace']}/"
                f"push={state['attack_peak_initial_push_force']:.1f}/"
                f"{state['attack_peak_push_force']:.1f}/"
                f"t={state['attack_peak_elapsed']:.2f}s",
                f"attack_first={first_attack['peak_linear']:.1f}cm/s/"
                f"{first_attack['peak_angular']:.1f}deg/s@"
                f"{first_attack['peak_plank_index']}/"
                f"{first_attack['peak_phase']}/"
                f"push={first_attack['peak_movement_impulse']:.2f}/"
                f"delta={first_attack['max_delta_seconds'] * 1000.0:.2f}ms",
                f"attack_repeat={repeat_attack['peak_linear']:.1f}cm/s/"
                f"{repeat_attack['peak_angular']:.1f}deg/s@"
                f"{repeat_attack['peak_plank_index']}/"
                f"{repeat_attack['peak_phase']}/"
                f"push={repeat_attack['peak_movement_impulse']:.2f}/"
                f"delta={repeat_attack['max_delta_seconds'] * 1000.0:.2f}ms",
                f"attack_delta_ratio={state['attack_delta_ratio']:.2f}",
                f"attack_first_load_hitch={state['attack_first_load_hitch']}",
                f"walk_impulse={state['walk_impulse']:.1f}",
                f"run_impulse={state['run_impulse']:.1f}",
                f"jump_impulse={state['jump_takeoff_impulse']:.1f}",
                f"landing_impulse={state['landing_impulse']:.1f}",
                f"landing_peak={state['landing_peak_linear']:.1f}cm/s",
                f"center_drop={center_drop:.1f}cm",
                f"recovery={state['recovery_linear']:.1f}cm/s",
                f"recovery_angular={state['recovery_angular']:.1f}deg/s",
                f"recovery_armed={state['bridge'].is_unloaded_recovery_armed()}",
                f"rest_angular_error="
                f"{state['recovery_rest_angular_error']:.2f}deg",
                f"anchor_error={state['maximum_endpoint_error']:.2f}cm",
                f"anchor_frame_error="
                f"{state['maximum_anchor_frame_error']:.3f}cm",
                f"joint_error={state['maximum_joint_error']:.2f}cm",
            )
        ),
    )


def on_tick(_delta_seconds):
    try:
        delta_seconds = float(_delta_seconds)
        state["current_delta_seconds"] = (
            delta_seconds
            if math.isfinite(delta_seconds) and delta_seconds >= 0.0
            else 0.0
        )
        now = time.monotonic()
        if state["phase"] == "stopping":
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
        phase = state["phase"]
        if phase == "starting" and level_editor.is_in_play_in_editor() and world:
            begin_validation(world)
        elif phase == "settling":
            settle_bridge()
        elif phase == "impulse_response":
            sample_impulse_response()
        elif phase == "impulse_decay":
            validate_impulse_decay()
        elif phase == "waiting_for_support":
            wait_for_support("stationary")
        elif phase == "stationary":
            sample_stationary()
        elif phase == "attack_idle_control":
            sample_attack_idle_control()
        elif phase == "walking":
            sample_walking()
        elif phase == "support_after_walk":
            wait_for_support("running")
        elif phase == "running":
            sample_running()
        elif phase == "support_before_attack":
            wait_for_support("begin_attack")
        elif phase == "begin_attack":
            begin_bridge_attack()
        elif phase == "attacking_on_bridge":
            sample_bridge_attack()
        elif phase == "waiting_for_repeat_attack":
            wait_for_repeat_attack()
        elif phase == "support_before_walk":
            wait_for_support("walking")
        elif phase == "support_before_jump":
            wait_for_support("begin_jump")
        elif phase == "begin_jump":
            begin_jump()
        elif phase == "jump_airborne":
            sample_jump_airborne()
        elif phase == "post_landing":
            sample_post_landing()
        elif phase == "confirm_departure":
            confirm_departure()
        elif phase == "recovery":
            validate_recovery()

        if state["result"] is None and now >= state["deadline"]:
            fail(f"validation timeout; phase={state['phase']} last={state['last']}")
    except Exception as exc:
        fail(f"exception={exc!r}; phase={state['phase']} last={state['last']}")


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
tick_handle = unreal.register_slate_post_tick_callback(on_tick)
if not unreal.RoverEditorTestLibrary.request_play_in_new_window():
    unreal.unregister_slate_post_tick_callback(tick_handle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    raise RuntimeError("Unable to request a new-window PIE session")
