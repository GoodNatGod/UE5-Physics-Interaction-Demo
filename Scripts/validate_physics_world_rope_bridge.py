import math
import time

import unreal


BRIDGE_TAG = "PhysicsWorldRopeBridge"
BRIDGE_BLUEPRINT_CLASS = (
    "/Game/PhysicsWorldDemo/Blueprints/BP_RopeBridge.BP_RopeBridge_C"
)
SECONDARY_ANCHOR_TAG = "RopeBridgeSecondaryAnchor"
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
SETTLE_MIN_SECONDS = 1.0
SETTLE_QUIET_SECONDS = 0.10
SETTLED_LINEAR_SPEED = 18.0
SETTLED_ANGULAR_SPEED = 120.0
IMPULSE_SAMPLE_SECONDS = 0.45
IMPULSE_DECAY_SECONDS = 2.5
STATIONARY_SAMPLE_SECONDS = 0.65
POST_LANDING_SAMPLE_SECONDS = 1.0
RECOVERY_SECONDS = 6.0
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
    "center_plank": None,
    "expected_planks": 0,
    "expected_constraints": 0,
    "phase_start_time": 0.0,
    "quiet_since": None,
    "original_pawn_location": None,
    "baseline_center_location": None,
    "minimum_center_z": float("inf"),
    "impulse_peak_linear": 0.0,
    "impulse_peak_angular": 0.0,
    "impulse_decay_linear": 0.0,
    "impulse_decay_angular": 0.0,
    "standing_peak_linear": 0.0,
    "standing_peak_angular": 0.0,
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
    "maximum_endpoint_error": 0.0,
    "maximum_joint_error": 0.0,
    "maximum_anchor_frame_error": 0.0,
    "support_count": 0,
    "primary_anchor_count": 0,
    "secondary_anchor_count": 0,
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


def get_constraint_profile(constraint):
    instance = constraint.get_editor_property("constraint_instance")
    return instance.get_editor_property("profile_instance")


def validate_constraint_profile(constraint, settings, secondary):
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
        raise RuntimeError(f"primary {constraint.get_name()} swing1 is not limited")
    if not enum_matches(swing2, "ACM_FREE") or not enum_matches(
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

    return {
        "support_count": len(supports),
        "primary_anchor_count": primary_count,
        "secondary_anchor_count": secondary_count,
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
    expected_constraints = expected_planks + 3
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
    movement_config = (
        locomotion.get_editor_property("movement_config") if locomotion else None
    )
    if movement is None or not isinstance(movement_config, unreal.RoverMovementConfig):
        fail("Rover movement/config is unavailable")
        return
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
    state.update(
        {
            "world": world,
            "bridge": bridge,
            "pawn": pawn,
            "movement": movement,
            "locomotion": locomotion,
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
            "original_pawn_location": pawn.get_actor_location(),
            "plank_mass_kg": expected_mass,
            "character_mass_kg": actual_character_mass,
            "standing_force_scale": actual_standing_force,
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
        state["last"] = (
            f"unloaded settle linear={sample[0]:.2f}cm/s "
            f"angular={sample[1]:.2f}deg/s quiet={quiet_for:.2f}s"
        )
        return

    state["baseline_center_location"] = state["center_plank"].get_world_location()
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
        state["last"] = (
            f"Rover has not acquired a bridge movement base; "
            f"linear={sample[0]:.1f}cm/s"
        )
        return
    state["movement"].stop_movement_immediately()
    state["bridge"].reset_character_response_debug()
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
    state["bridge"].reset_character_response_debug()
    set_phase("walking")
    state["last"] = "injecting real CharacterMovement walk velocity"


def drive_character(speed, peak_prefix):
    sample = sample_stability()
    if sample is None:
        return None
    if not state["bridge"].is_character_supported_by_bridge(state["pawn"]):
        fail(f"Rover left the bridge before {peak_prefix} response was sampled")
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
    impulse = drive_character(WALK_TEST_SPEED, "walk")
    if impulse is None:
        return
    if impulse <= 0.0:
        state["last"] = "waiting for speed-scaled walking impulse"
        return
    state["walk_impulse"] = impulse
    place_character_over_center(12.0, "support_after_walk")


def sample_running():
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
        state["last"] = (
            f"jump takeoff={state['jump_takeoff_impulse']:.1f} "
            f"supported={supported} landing={landing:.1f}"
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
        state["last"] = (
            f"recovery linear={sample[0]:.2f}cm/s angular={sample[1]:.2f}deg/s"
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

    center_drop = (
        state["baseline_center_location"].z - state["minimum_center_z"]
    )
    finish(
        True,
        " ".join(
            (
                f"planks={state['expected_planks']}",
                f"constraints={state['expected_constraints']}",
                f"supports={state['support_count']}",
                f"anchor_profiles={state['primary_anchor_count']}/"
                f"{state['secondary_anchor_count']}",
                f"plank_mass={state['plank_mass_kg']:.2f}kg",
                f"character_mass={state['character_mass_kg']:.1f}kg",
                f"standing_force_scale={state['standing_force_scale']:.2f}",
                f"standing_peak={state['standing_peak_linear']:.1f}cm/s",
                f"walk_impulse={state['walk_impulse']:.1f}",
                f"run_impulse={state['run_impulse']:.1f}",
                f"jump_impulse={state['jump_takeoff_impulse']:.1f}",
                f"landing_impulse={state['landing_impulse']:.1f}",
                f"landing_peak={state['landing_peak_linear']:.1f}cm/s",
                f"center_drop={center_drop:.1f}cm",
                f"recovery={state['recovery_linear']:.1f}cm/s",
                f"recovery_angular={state['recovery_angular']:.1f}deg/s",
                f"anchor_error={state['maximum_endpoint_error']:.2f}cm",
                f"anchor_frame_error="
                f"{state['maximum_anchor_frame_error']:.3f}cm",
                f"joint_error={state['maximum_joint_error']:.2f}cm",
            )
        ),
    )


def on_tick(_delta_seconds):
    try:
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
        elif phase == "walking":
            sample_walking()
        elif phase == "support_after_walk":
            wait_for_support("running")
        elif phase == "running":
            sample_running()
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
