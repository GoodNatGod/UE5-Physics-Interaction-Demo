import time

import unreal


BOX_TAG = "PhysicsWorldP0Box"
MINIMUM_LIFT_HEIGHT_CM = 500.0
MIN_CLEARANCE_AFTER_FALL_CM = 250.0
FALL_SAMPLE_SECONDS = 0.20
FALL_CLEARANCE_GUARD_SECONDS = 0.50
MASS_TOLERANCE_KG = 0.10
GRAVITY_TOLERANCE_CM_S2 = 0.10
MAX_BREAK_TRANSFER_ERROR_CM = 1.0
MAX_BREAK_ROTATION_ERROR_DEGREES = 0.1
MAX_BREAK_SCALE_ERROR = 0.001
MINIMUM_DEBRIS_EXPANSION_CM = 20.0
START_TIMEOUT_SECONDS = 30.0
RESET_TIMEOUT_SECONDS = 3.0
FALL_TIMEOUT_SECONDS = 3.0
DEBRIS_TIMEOUT_SECONDS = 3.0
STOP_TIMEOUT_SECONDS = 15.0
SUCCESS_MARKER = "PHYSICS_WORLD_BOX_PHYSICS_PIE_OK"
FAILURE_MARKER = "PHYSICS_WORLD_BOX_PHYSICS_PIE_FAIL"


level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
unreal_editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
state = {
    "phase": "starting",
    "deadline": time.monotonic() + START_TIMEOUT_SECONDS,
    "result": None,
    "last": "PIE has not started",
    "world": None,
    "pawn": None,
    "skill": None,
    "box": None,
    "intact_mesh": None,
    "geometry_collection": None,
    "subsystem": None,
    "baseline_z": 0.0,
    "lift_height": 0.0,
    "lifted_z": 0.0,
    "fall_start_time": 0.0,
    "fall_start_z": 0.0,
    "fall_start_velocity_z": 0.0,
    "fall_elapsed": 0.0,
    "fall_displacement_z": 0.0,
    "fall_velocity_z": 0.0,
    "expected_displacement_z": 0.0,
    "expected_velocity_z": 0.0,
    "configured_mass_kg": 0.0,
    "probe_mass_kg": 0.0,
    "gravity_z": 0.0,
    "debris_expansion": 0.0,
}
tick_handle = None


def nearly_equal(left, right, tolerance):
    return abs(left - right) <= tolerance


def distance_squared(left, right):
    return (
        (left.x - right.x) ** 2
        + (left.y - right.y) ** 2
        + (left.z - right.z) ** 2
    )


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


def choose_intact_box(world, pawn):
    boxes = unreal.GameplayStatics.get_all_actors_with_tag(world, BOX_TAG)
    candidates = [
        actor
        for actor in boxes
        if actor is not None
        and not actor.is_destroyed()
        and nearly_equal(actor.get_current_health(), actor.get_max_health(), 0.01)
    ]
    if not candidates:
        return None, len(boxes)

    reference = pawn.get_actor_location()
    candidates.sort(
        key=lambda actor: (
            distance_squared(actor.get_actor_location(), reference),
            actor.get_path_name(),
        )
    )
    return candidates[0], len(boxes)


def begin_validation(world):
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    if pawn is None:
        state["last"] = "waiting for player pawn"
        return

    box, tagged_count = choose_intact_box(world, pawn)
    if box is None:
        fail(f"found no intact tagged destructible box among {tagged_count}")
        return

    skill = pawn.get_world_skill_component()
    subsystem = unreal.RoverEditorTestLibrary.get_world_interaction_subsystem(world)
    intact_mesh = box.get_intact_mesh()
    geometry_collection = box.get_geometry_collection()
    if any(
        value is None
        for value in (skill, subsystem, intact_mesh, geometry_collection)
    ):
        fail("box physics test is missing skill, subsystem, or box components")
        return
    if not box.has_geometry_collection_asset():
        fail("destructible box has no Geometry Collection asset")
        return

    interaction_config = box.get_editor_property("interaction_config")
    if not isinstance(interaction_config, unreal.WorldInteractionConfig):
        fail("destructible box has no WorldInteractionConfig")
        return
    settings = interaction_config.get_editor_property("settings")
    if not settings.get_editor_property("override_world_gravity"):
        fail("shared world gravity override is disabled")
        return
    configured_gravity_z = float(settings.get_editor_property("world_gravity_z"))
    applied_gravity_z = float(subsystem.get_applied_world_gravity_z())
    if not nearly_equal(
        applied_gravity_z,
        configured_gravity_z,
        GRAVITY_TOLERANCE_CM_S2,
    ):
        fail(
            f"world gravity={applied_gravity_z:.2f}, "
            f"configured={configured_gravity_z:.2f}"
        )
        return
    if not box.get_editor_property("enable_intact_physics"):
        fail("box has intact physics disabled")
        return
    if not box.is_intact_physics_simulating() or not intact_mesh.is_gravity_enabled():
        fail("intact box is not simulating with gravity enabled")
        return

    configured_mass = float(box.get_configured_box_mass_kg())
    actual_mass = float(box.get_intact_physics_mass_kg())
    gc_mass = float(box.get_geometry_collection_mass_kg())
    if configured_mass <= 0.0 or not nearly_equal(
        actual_mass, configured_mass, MASS_TOLERANCE_KG
    ):
        fail(
            f"intact mass={actual_mass:.3f}kg, configured={configured_mass:.3f}kg"
        )
        return
    if not nearly_equal(gc_mass, configured_mass, MASS_TOLERANCE_KG):
        fail(
            f"Geometry Collection mass={gc_mass:.3f}kg, "
            f"configured={configured_mass:.3f}kg"
        )
        return

    # Exercise the runtime setter on the PIE actor only, then restore the authored value.
    probe_mass = max(configured_mass + 1.0, configured_mass * 1.5)
    box.set_box_mass_kg(probe_mass)
    measured_probe_mass = float(box.get_intact_physics_mass_kg())
    measured_probe_gc_mass = float(box.get_geometry_collection_mass_kg())
    box.set_box_mass_kg(configured_mass)
    restored_mass = float(box.get_intact_physics_mass_kg())
    restored_gc_mass = float(box.get_geometry_collection_mass_kg())
    if not nearly_equal(measured_probe_mass, probe_mass, MASS_TOLERANCE_KG):
        fail(
            f"runtime mass probe={measured_probe_mass:.3f}kg, "
            f"requested={probe_mass:.3f}kg"
        )
        return
    if not nearly_equal(measured_probe_gc_mass, probe_mass, MASS_TOLERANCE_KG):
        fail(
            f"Geometry Collection runtime mass probe={measured_probe_gc_mass:.3f}kg, "
            f"requested={probe_mass:.3f}kg"
        )
        return
    if not nearly_equal(restored_mass, configured_mass, MASS_TOLERANCE_KG):
        fail(
            f"restored mass={restored_mass:.3f}kg, "
            f"configured={configured_mass:.3f}kg"
        )
        return
    if not nearly_equal(restored_gc_mass, configured_mass, MASS_TOLERANCE_KG):
        fail(
            f"restored Geometry Collection mass={restored_gc_mass:.3f}kg, "
            f"configured={configured_mass:.3f}kg"
        )
        return

    baseline_location = intact_mesh.get_world_location()
    guarded_fall_distance = (
        0.5
        * abs(applied_gravity_z)
        * FALL_CLEARANCE_GUARD_SECONDS
        * FALL_CLEARANCE_GUARD_SECONDS
    )
    lift_height = max(
        MINIMUM_LIFT_HEIGHT_CM,
        MIN_CLEARANCE_AFTER_FALL_CM + guarded_fall_distance + 100.0,
    )
    lifted_location = baseline_location + unreal.Vector(0.0, 0.0, lift_height)
    if not box.set_actor_location(lifted_location, False, True):
        fail("unable to teleport the intact box to the free-fall test height")
        return
    intact_mesh.set_physics_linear_velocity(unreal.Vector(0.0, 0.0, 0.0))
    intact_mesh.set_physics_angular_velocity_in_degrees(
        unreal.Vector(0.0, 0.0, 0.0)
    )
    intact_mesh.wake_all_rigid_bodies()

    state.update(
        {
            "phase": "resetting_fall",
            "deadline": time.monotonic() + RESET_TIMEOUT_SECONDS,
            "last": "waiting one physics frame after the lift teleport",
            "world": world,
            "pawn": pawn,
            "skill": skill,
            "box": box,
            "intact_mesh": intact_mesh,
            "geometry_collection": geometry_collection,
            "subsystem": subsystem,
            "baseline_z": baseline_location.z,
            "lift_height": lift_height,
            "lifted_z": lifted_location.z,
            "configured_mass_kg": configured_mass,
            "probe_mass_kg": probe_mass,
            "gravity_z": applied_gravity_z,
        }
    )


def arm_free_fall():
    intact_mesh = state["intact_mesh"]
    current_location = intact_mesh.get_world_location()
    if abs(current_location.z - state["lifted_z"]) > 25.0:
        fail(
            "box moved unexpectedly while arming free fall; "
            f"z={current_location.z:.2f} target={state['lifted_z']:.2f}"
        )
        return

    # Clear the single gravity step that can occur between teleport and this callback.
    intact_mesh.set_physics_linear_velocity(unreal.Vector(0.0, 0.0, 0.0))
    intact_mesh.set_physics_angular_velocity_in_degrees(
        unreal.Vector(0.0, 0.0, 0.0)
    )
    intact_mesh.wake_all_rigid_bodies()
    start_velocity = intact_mesh.get_physics_linear_velocity()
    start_time = unreal.GameplayStatics.get_time_seconds(state["world"])
    state.update(
        {
            "phase": "free_fall",
            "deadline": time.monotonic() + FALL_TIMEOUT_SECONDS,
            "last": "sampling free fall before any ground contact",
            "fall_start_time": start_time,
            "fall_start_z": current_location.z,
            "fall_start_velocity_z": start_velocity.z,
        }
    )


def validate_free_fall():
    world = state["world"]
    elapsed = unreal.GameplayStatics.get_time_seconds(world) - state["fall_start_time"]
    if elapsed < FALL_SAMPLE_SECONDS:
        state["last"] = f"free-fall sample elapsed={elapsed:.3f}s"
        return

    intact_mesh = state["intact_mesh"]
    current_location = intact_mesh.get_world_location()
    current_velocity = intact_mesh.get_physics_linear_velocity()
    gravity_z = state["gravity_z"]
    initial_velocity_z = state["fall_start_velocity_z"]
    expected_velocity_z = initial_velocity_z + gravity_z * elapsed
    expected_displacement_z = (
        initial_velocity_z * elapsed + 0.5 * gravity_z * elapsed * elapsed
    )
    displacement_z = current_location.z - state["fall_start_z"]
    velocity_tolerance = max(50.0, abs(gravity_z * elapsed) * 0.30)
    displacement_tolerance = max(8.0, abs(expected_displacement_z) * 0.40)

    if current_location.z - state["baseline_z"] < MIN_CLEARANCE_AFTER_FALL_CM:
        fail(
            "free-fall sample reached possible ground contact; "
            f"clearance={current_location.z - state['baseline_z']:.1f}cm"
        )
        return
    if not nearly_equal(
        current_velocity.z, expected_velocity_z, velocity_tolerance
    ):
        fail(
            f"free-fall velocity={current_velocity.z:.1f}cm/s, "
            f"expected={expected_velocity_z:.1f}cm/s, dt={elapsed:.3f}s"
        )
        return
    if not nearly_equal(
        displacement_z, expected_displacement_z, displacement_tolerance
    ):
        fail(
            f"free-fall displacement={displacement_z:.1f}cm, "
            f"expected={expected_displacement_z:.1f}cm, dt={elapsed:.3f}s"
        )
        return

    state.update(
        {
            "fall_elapsed": elapsed,
            "fall_displacement_z": displacement_z,
            "fall_velocity_z": current_velocity.z,
            "expected_displacement_z": expected_displacement_z,
            "expected_velocity_z": expected_velocity_z,
        }
    )
    trigger_break()


def trigger_break():
    box = state["box"]
    skill = state["skill"]
    subsystem = state["subsystem"]
    intact_mesh = state["intact_mesh"]

    subsystem.reset_debug_stats()
    if not skill.request_fireball():
        fail("RoverWorldSkillComponent rejected the break-test fireball")
        return
    projectile = skill.get_last_spawned_fireball()
    if projectile is None or projectile.has_detonated():
        fail("break-test fireball did not spawn as a live projectile")
        return
    if not unreal.RoverEditorTestLibrary.trigger_p0_fireball_impact(projectile, box):
        fail("unable to trigger the break-test fireball against the airborne box")
        return

    result = subsystem.get_last_interaction_result()
    if not result.get_editor_property("accepted"):
        fail("airborne break interaction was not accepted")
        return
    if result.get_editor_property("receiver_count") < 1:
        fail("airborne break interaction reached no destructible receiver")
        return
    if not box.is_destroyed():
        fail("airborne box survived the lethal break interaction")
        return
    # Read the component directly: the actor convenience getter intentionally returns
    # false after destruction and cannot prove that the old body was shut down.
    if intact_mesh.is_simulating_physics():
        fail("intact rigid body kept simulating after Geometry Collection takeover")
        return
    if intact_mesh.get_collision_enabled() != unreal.CollisionEnabled.NO_COLLISION:
        fail("intact rigid body kept collision after Geometry Collection takeover")
        return
    if intact_mesh.is_visible():
        fail("intact mesh remained visible after Geometry Collection takeover")
        return
    if box.get_break_transform_transfer_error() > MAX_BREAK_TRANSFER_ERROR_CM:
        fail(
            "Geometry Collection transfer error="
            f"{box.get_break_transform_transfer_error():.2f}cm"
        )
        return
    if box.get_break_rotation_transfer_error_degrees() > MAX_BREAK_ROTATION_ERROR_DEGREES:
        fail(
            "Geometry Collection rotation transfer error="
            f"{box.get_break_rotation_transfer_error_degrees():.3f}deg"
        )
        return
    if box.get_break_scale_transfer_error() > MAX_BREAK_SCALE_ERROR:
        fail(
            "Geometry Collection scale transfer error="
            f"{box.get_break_scale_transfer_error():.5f}"
        )
        return

    state.update(
        {
            "phase": "checking_geometry_collection",
            "deadline": time.monotonic() + DEBRIS_TIMEOUT_SECONDS,
            "last": "waiting for the Geometry Collection root cluster to break",
        }
    )


def validate_geometry_collection():
    box = state["box"]
    geometry_collection = state["geometry_collection"]
    if not box.has_applied_break_impulse() or not box.has_applied_break_strain():
        state["last"] = "waiting for Chaos strain and break impulse"
        return
    if not box.is_geometry_collection_active():
        state["last"] = "waiting for Geometry Collection visibility and collision"
        return
    if not geometry_collection.is_simulating_physics():
        fail("Geometry Collection takeover is not simulating physics")
        return
    if not box.is_geometry_collection_gravity_enabled():
        fail("Geometry Collection takeover has gravity disabled")
        return
    if (
        geometry_collection.get_collision_enabled()
        != unreal.CollisionEnabled.QUERY_AND_PHYSICS
    ):
        fail("Geometry Collection takeover lacks query-and-physics collision")
        return
    if not geometry_collection.is_root_broken():
        state["last"] = "waiting for Geometry Collection root break"
        return

    expansion = box.get_debris_expansion_distance()
    state["debris_expansion"] = max(state["debris_expansion"], expansion)
    state["last"] = f"debris expansion={state['debris_expansion']:.1f}cm"
    if state["debris_expansion"] < MINIMUM_DEBRIS_EXPANSION_CM:
        return

    finish(
        True,
        " ".join(
            (
                f"mass={state['configured_mass_kg']:.2f}kg",
                f"gc_mass={state['configured_mass_kg']:.2f}kg",
                f"mass_probe={state['probe_mass_kg']:.2f}kg",
                f"gravity_z={state['gravity_z']:.1f}cm/s2",
                f"lift={state['lift_height']:.1f}cm",
                f"fall_dt={state['fall_elapsed']:.3f}s",
                f"fall_dz={state['fall_displacement_z']:.1f}cm",
                f"expected_dz={state['expected_displacement_z']:.1f}cm",
                f"fall_vz={state['fall_velocity_z']:.1f}cm/s",
                f"expected_vz={state['expected_velocity_z']:.1f}cm/s",
                "intact_body=disabled",
                "break_transform=continuous",
                "geometry_collection=simulating",
                "root_cluster=broken",
                f"debris_expansion={state['debris_expansion']:.1f}cm",
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
        if state["phase"] == "starting" and level_editor.is_in_play_in_editor() and world:
            begin_validation(world)
        elif state["phase"] == "resetting_fall":
            arm_free_fall()
        elif state["phase"] == "free_fall":
            validate_free_fall()
        elif state["phase"] == "checking_geometry_collection":
            validate_geometry_collection()

        if state["result"] is None and now >= state["deadline"]:
            fail(f"validation timeout; last={state['last']}")
    except Exception as exc:
        fail(f"exception={exc!r}; last={state['last']}")


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
tick_handle = unreal.register_slate_post_tick_callback(on_tick)
if not unreal.RoverEditorTestLibrary.request_play_in_new_window():
    unreal.unregister_slate_post_tick_callback(tick_handle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    raise RuntimeError("Unable to request a new-window PIE session")
