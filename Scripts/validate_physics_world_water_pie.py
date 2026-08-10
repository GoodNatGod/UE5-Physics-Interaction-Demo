import time

import unreal


MAP_PATH = "/Game/PhysicsWorldDemo/Water/Maps/L_WaterP0"
CONFIG_PATH = (
    "/Game/PhysicsWorldDemo/Water/Config/"
    "DA_WorldWaterRippleConfig.DA_WorldWaterRippleConfig"
)
REGION_TAG = "PhysicsWorldWaterRippleRegion"
EXPECTED_RESOLUTION = 512
EXPECTED_FIXED_STEP_SECONDS = 1.0 / 60.0
START_TIMEOUT_SECONDS = 30.0
PHASE_TIMEOUT_SECONDS = 8.0
STOP_TIMEOUT_SECONDS = 15.0
SUCCESS_MARKER = "PHYSICS_WORLD_WATER_PIE_OK"
FAILURE_MARKER = "PHYSICS_WORLD_WATER_PIE_FAIL"


level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
unreal_editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
state = {
    "phase": "starting",
    "deadline": time.monotonic() + START_TIMEOUT_SECONDS,
    "result": None,
    "last": "PIE has not started",
    "world": None,
    "region": None,
    "subsystem": None,
    "pawn": None,
    "surface_location": None,
    "phase_started": 0.0,
    "stationary_steps": 0,
}
tick_handle = None


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


def start_phase(name, timeout=PHASE_TIMEOUT_SECONDS):
    state["phase"] = name
    state["phase_started"] = unreal.GameplayStatics.get_time_seconds(state["world"])
    state["deadline"] = time.monotonic() + timeout
    state["last"] = f"phase={name}"


def elapsed_in_phase():
    return (
        unreal.GameplayStatics.get_time_seconds(state["world"])
        - state["phase_started"]
    )


def make_field(source_type, location, velocity, strength, radius):
    field = unreal.WorldLightweightInteractionField()
    field.set_editor_property("source_actor", state["pawn"])
    field.set_editor_property("source_type", source_type)
    field.set_editor_property(
        "shape_type", unreal.WorldLightweightInteractionShape.SPHERE
    )
    field.set_editor_property("start", location)
    field.set_editor_property("end", location)
    speed = velocity.length()
    direction = (
        velocity / speed if speed > 0.01 else unreal.Vector(0.0, 0.0, 1.0)
    )
    field.set_editor_property("direction", direction)
    field.set_editor_property("source_velocity", velocity)
    field.set_editor_property("radius", radius)
    field.set_editor_property("strength", strength)
    field.set_editor_property("upward_lift", 0.0)
    field.set_editor_property("duration", 0.1)
    field.set_editor_property("falloff_exponent", 1.0)
    return field


def begin_validation(world):
    regions = unreal.GameplayStatics.get_all_actors_with_tag(world, REGION_TAG)
    region = next(
        (
            actor
            for actor in regions
            if actor and actor.get_class().get_name() == "WorldWaterRippleRegion"
        ),
        None,
    )
    if region is None:
        fail(f"Map has no tagged {REGION_TAG} actor")
        return
    if not region.is_simulation_ready():
        state["last"] = "waiting for water simulation readiness"
        return

    target_surface = region.get_editor_property("target_water_surface_actor")
    if (
        not target_surface
        or target_surface.get_class().get_name() != "BP_Waterplane_C"
    ):
        fail(
            "Ripple region is not bound to BP_Waterplane: "
            f"target={target_surface}"
        )
        return
    domain_size = region.get_resolved_domain_world_size()
    if abs(domain_size.x - 2000.0) > 1.0 or abs(domain_size.y - 2000.0) > 1.0:
        fail(f"Ripple domain did not fit target plane bounds: size={domain_size}")
        return

    config = unreal.load_asset(CONFIG_PATH)
    if not config or config.get_class().get_name() != "WorldWaterRippleConfig":
        fail(f"Missing water config={CONFIG_PATH}")
        return
    settings = config.get_editor_property("settings")
    fixed_step = float(settings.get_editor_property("fixed_step_seconds"))
    if abs(fixed_step - EXPECTED_FIXED_STEP_SECONDS) > 1.0e-5:
        fail(f"Water simulation is not fixed at 60Hz: step={fixed_step:.8f}s")
        return

    current_rt = region.get_current_state_render_target()
    previous_rt = region.get_previous_state_render_target()
    if not current_rt or not previous_rt or current_rt == previous_rt:
        fail("Water simulation does not own two distinct runtime render targets")
        return
    if region.get_render_target_resolution() != EXPECTED_RESOLUTION:
        fail(
            "Unexpected runtime render target resolution="
            f"{region.get_render_target_resolution()}"
        )
        return
    for render_target in (current_rt, previous_rt):
        render_format = render_target.get_editor_property("render_target_format")
        if "RG16F" not in str(render_format).upper():
            fail(f"Runtime render target is not RG16F: {render_format}")
            return

    subsystem = unreal.RoverEditorTestLibrary.get_world_interaction_subsystem(world)
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    if not subsystem or not pawn:
        state["last"] = "waiting for player and WorldInteractionSubsystem"
        return

    region.reset_debug_stats()
    subsystem.reset_debug_stats()
    region_location = region.get_resolved_domain_center()
    surface_z = float(region.get_water_surface_z())
    state.update(
        {
            "world": world,
            "region": region,
            "subsystem": subsystem,
            "pawn": pawn,
            "surface_location": unreal.Vector(
                region_location.x, region_location.y, surface_z
            ),
        }
    )
    start_phase("stationary", 4.0)


def validate_stationary():
    if elapsed_in_phase() < 0.5:
        return
    region = state["region"]
    steps = region.get_simulation_step_count()
    swaps = region.get_buffer_swap_count()
    if steps < 1 or swaps != steps:
        fail(f"Fixed-step solver did not swap once per step: steps={steps} swaps={swaps}")
        return
    zero_counters = {
        "pending": region.get_pending_impulse_count(),
        "accepted": region.get_accepted_impulse_count(),
        "dropped": region.get_dropped_impulse_count(),
        "handled": region.get_handled_lightweight_field_count(),
        "movement": region.get_movement_impulse_count(),
        "landing": region.get_landing_impulse_count(),
        "entry": region.get_water_entry_impulse_count(),
    }
    nonzero = {name: value for name, value in zero_counters.items() if value != 0}
    if nonzero:
        fail(f"Stationary water emitted impulses: {nonzero}")
        return
    state["stationary_steps"] = steps

    movement = make_field(
        unreal.WorldLightweightInteractionSource.MOVEMENT,
        state["surface_location"],
        unreal.Vector(450.0, 0.0, 0.0),
        1.0,
        45.0,
    )
    if not state["subsystem"].publish_lightweight_interaction_field(movement):
        fail("WorldInteractionSubsystem rejected the Movement water event")
        return
    start_phase("movement")


def validate_movement():
    region = state["region"]
    state["last"] = (
        f"waiting for Movement impulse handled={region.get_handled_lightweight_field_count()} "
        f"movement={region.get_movement_impulse_count()} "
        f"accepted={region.get_accepted_impulse_count()}"
    )
    if (
        region.get_handled_lightweight_field_count() < 1
        or region.get_movement_impulse_count() < 1
        or region.get_accepted_impulse_count() < 1
    ):
        return

    landing = make_field(
        unreal.WorldLightweightInteractionSource.LANDING,
        state["surface_location"],
        unreal.Vector(0.0, 0.0, -900.0),
        2.0,
        100.0,
    )
    if not state["subsystem"].publish_lightweight_interaction_field(landing):
        fail("WorldInteractionSubsystem rejected the Landing water event")
        return
    start_phase("landing")


def validate_landing():
    region = state["region"]
    state["last"] = (
        f"waiting for Landing impulse handled={region.get_handled_lightweight_field_count()} "
        f"landing={region.get_landing_impulse_count()} "
        f"accepted={region.get_accepted_impulse_count()} "
        f"pending={region.get_pending_impulse_count()}"
    )
    if (
        region.get_handled_lightweight_field_count() < 2
        or region.get_landing_impulse_count() < 1
        or region.get_accepted_impulse_count() < 2
    ):
        return
    if elapsed_in_phase() < 0.2 or region.get_pending_impulse_count() != 0:
        return

    steps = region.get_simulation_step_count()
    swaps = region.get_buffer_swap_count()
    if steps <= state["stationary_steps"] or swaps != steps:
        fail(f"Solver stopped or swap count diverged: steps={steps} swaps={swaps}")
        return
    if region.get_dropped_impulse_count() != 0:
        fail(f"Expected events were dropped: count={region.get_dropped_impulse_count()}")
        return
    if region.get_movement_impulse_count() != 1:
        fail(
            f"Movement event produced duplicate impulses={region.get_movement_impulse_count()}"
        )
        return
    if region.get_landing_impulse_count() != 1:
        fail(
            f"Landing event produced duplicate impulses={region.get_landing_impulse_count()}"
        )
        return
    unexpected = {
        "attack": region.get_attack_impulse_count(),
        "explosion": region.get_explosion_impulse_count(),
        "entry": region.get_water_entry_impulse_count(),
    }
    if any(value != 0 for value in unexpected.values()):
        fail(f"Unexpected water impulse sources were counted: {unexpected}")
        return
    if region.get_last_impulse_strength() <= 0.0:
        fail("Last accepted water impulse has no strength")
        return
    if state["subsystem"].get_published_lightweight_field_count() != 2:
        fail(
            "Unexpected published field count="
            f"{state['subsystem'].get_published_lightweight_field_count()}"
        )
        return

    if not region.queue_ripple_impulse(
        state["surface_location"],
        140.0,
        2.5,
        unreal.WorldWaterRippleImpulseSource.WATER_ENTRY,
        state["pawn"],
    ):
        fail("Ripple region rejected a direct WaterEntry impulse")
        return
    start_phase("water_entry")


def validate_water_entry():
    region = state["region"]
    entry_count = region.get_water_entry_impulse_count()
    splash_count = region.get_spawned_water_entry_splash_count()
    state["last"] = (
        f"waiting for WaterEntry entry={entry_count} splash={splash_count} "
        f"accepted={region.get_accepted_impulse_count()} "
        f"pending={region.get_pending_impulse_count()}"
    )
    if entry_count < 1 or splash_count < 1:
        return
    if elapsed_in_phase() < 0.2 or region.get_pending_impulse_count() != 0:
        return

    if entry_count != 1 or splash_count != 1:
        fail(
            "WaterEntry did not stay one-shot: "
            f"entry={entry_count} splash={splash_count}"
        )
        return
    if region.get_landing_impulse_count() != 1:
        fail(
            "WaterEntry was incorrectly counted as Landing: "
            f"landing={region.get_landing_impulse_count()}"
        )
        return
    if region.get_accepted_impulse_count() != 3:
        fail(
            "Unexpected accepted impulse count after WaterEntry="
            f"{region.get_accepted_impulse_count()}"
        )
        return
    if state["subsystem"].get_published_lightweight_field_count() != 2:
        fail("Direct WaterEntry unexpectedly published through the world subsystem")
        return

    steps = region.get_simulation_step_count()
    swaps = region.get_buffer_swap_count()
    if swaps != steps:
        fail(f"Buffer swap count diverged after WaterEntry: steps={steps} swaps={swaps}")
        return

    finish(
        True,
        " ".join(
            (
                f"map={MAP_PATH}",
                f"fixed_steps={steps}",
                f"buffer_swaps={swaps}",
                "stationary_impulses=0",
                f"movement_impulses={region.get_movement_impulse_count()}",
                f"landing_impulses={region.get_landing_impulse_count()}",
                f"water_entry_impulses={entry_count}",
                f"water_entry_splashes={splash_count}",
                f"accepted={region.get_accepted_impulse_count()}",
                "dropped=0",
                "runtime_rt=RG16F/512",
                "target_surface=BP_Waterplane",
                "domain="
                f"{region.get_resolved_domain_world_size().x:.0f}x"
                f"{region.get_resolved_domain_world_size().y:.0f}",
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
        elif state["phase"] == "stationary":
            validate_stationary()
        elif state["phase"] == "movement":
            validate_movement()
        elif state["phase"] == "landing":
            validate_landing()
        elif state["phase"] == "water_entry":
            validate_water_entry()

        if state["result"] is None and now >= state["deadline"]:
            fail(f"validation timeout; last={state['last']}")
    except Exception as exc:
        fail(f"exception={exc!r}; phase={state['phase']} last={state['last']}")


editor_world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
if not editor_world:
    raise RuntimeError(f"Failed to load water validation map: {MAP_PATH}")

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
tick_handle = unreal.register_slate_post_tick_callback(on_tick)
if not unreal.RoverEditorTestLibrary.request_play_in_new_window():
    unreal.unregister_slate_post_tick_callback(tick_handle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    raise RuntimeError("Unable to request a new-window PIE session")
