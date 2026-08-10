import os
import time

import unreal


MAP_PATH = "/Game/PhysicsWorldDemo/Water/Maps/L_WaterP0"
REGION_TAG = "PhysicsWorldWaterRippleRegion"
SCREENSHOT_PATH = os.environ.get(
    "ROVER_WATER_SCREENSHOT",
    os.path.abspath(
        os.path.join(
            unreal.Paths.project_saved_dir(),
            "Screenshots",
            "WindowsEditor",
            "WaterP0Preview.png",
        )
    ),
)
SUCCESS_MARKER = "PHYSICS_WORLD_WATER_PREVIEW_OK"
FAILURE_MARKER = "PHYSICS_WORLD_WATER_PREVIEW_FAIL"


level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
unreal_editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
state = {
    "phase": "starting",
    "deadline": time.monotonic() + 30.0,
    "result": None,
    "world": None,
    "region": None,
    "phase_started": 0.0,
    "screenshot_request_time": 0.0,
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
        state["deadline"] = time.monotonic() + 15.0
        level_editor.editor_request_end_play()
    else:
        shutdown()


def activate_preview(world):
    regions = unreal.GameplayStatics.get_all_actors_with_tag(world, REGION_TAG)
    region = next(
        (
            actor
            for actor in regions
            if actor and actor.get_class().get_name() == "WorldWaterRippleRegion"
        ),
        None,
    )
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    controller = unreal.GameplayStatics.get_player_controller(world, 0)
    if not region or not pawn or not controller or not region.is_simulation_ready():
        return
    target_surface = region.get_editor_property("target_water_surface_actor")
    if (
        not target_surface
        or target_surface.get_class().get_name() != "BP_Waterplane_C"
    ):
        finish(False, f"region target is not BP_Waterplane: {target_surface}")
        return

    center = region.get_resolved_domain_center()
    surface_z = float(region.get_water_surface_z())
    pawn.set_actor_location(
        unreal.Vector(center.x - 720.0, center.y, surface_z + 160.0),
        False,
        False,
    )
    pawn.set_actor_rotation(unreal.Rotator(0.0, 0.0, 0.0), False)
    controller.set_control_rotation(unreal.Rotator(-22.0, 0.0, 0.0))
    unreal.SystemLibrary.execute_console_command(world, "showflag.HUD 0")

    region.clear_ripple_simulation()
    region.reset_debug_stats()
    state.update(
        {
            "phase": "warming",
            "deadline": time.monotonic() + 12.0,
            "world": world,
            "region": region,
            "phase_started": unreal.GameplayStatics.get_time_seconds(world),
        }
    )


def queue_preview_impulses():
    world = state["world"]
    region = state["region"]
    center = region.get_resolved_domain_center()
    surface_z = float(region.get_water_surface_z())
    points = (
        (
            unreal.Vector(center.x - 180.0, center.y - 170.0, surface_z),
            70.0,
            1.25,
            unreal.WorldWaterRippleImpulseSource.MOVEMENT,
        ),
        (
            unreal.Vector(center.x + 80.0, center.y - 70.0, surface_z),
            115.0,
            2.25,
            unreal.WorldWaterRippleImpulseSource.ATTACK,
        ),
        (
            unreal.Vector(center.x + 260.0, center.y + 120.0, surface_z),
            190.0,
            3.5,
            unreal.WorldWaterRippleImpulseSource.EXPLOSION,
        ),
        (
            unreal.Vector(center.x - 60.0, center.y + 230.0, surface_z),
            145.0,
            2.75,
            unreal.WorldWaterRippleImpulseSource.WATER_ENTRY,
        ),
    )
    if not all(
        region.queue_ripple_impulse(location, radius, strength, source, None)
        for location, radius, strength, source in points
    ):
        finish(False, "one or more preview ripple impulses were rejected")
        return
    state.update(
        {
            "phase": "capturing",
            "deadline": time.monotonic() + 10.0,
            "phase_started": unreal.GameplayStatics.get_time_seconds(world),
        }
    )


def capture_preview():
    world = state["world"]
    region = state["region"]
    elapsed = unreal.GameplayStatics.get_time_seconds(world) - state["phase_started"]
    if state["screenshot_request_time"] <= 0.0:
        if elapsed < 0.45:
            return
        if region.get_accepted_impulse_count() < 4:
            finish(
                False,
                f"preview accepted={region.get_accepted_impulse_count()} expected_at_least=4",
            )
            return
        if region.get_water_entry_impulse_count() < 1:
            finish(False, "preview did not include a WaterEntry impulse")
            return
        os.makedirs(os.path.dirname(SCREENSHOT_PATH), exist_ok=True)
        if os.path.exists(SCREENSHOT_PATH):
            os.remove(SCREENSHOT_PATH)
        unreal.SystemLibrary.execute_console_command(
            world, f'HighResShot 1 filename="{SCREENSHOT_PATH}"'
        )
        state["screenshot_request_time"] = time.monotonic()
        return
    if os.path.exists(SCREENSHOT_PATH) and os.path.getsize(SCREENSHOT_PATH) > 0:
        finish(
            True,
            f"path={SCREENSHOT_PATH} accepted={region.get_accepted_impulse_count()} "
            f"steps={region.get_simulation_step_count()} "
            f"water_entry_splashes={region.get_spawned_water_entry_splash_count()}",
        )
    elif time.monotonic() - state["screenshot_request_time"] > 6.0:
        finish(False, f"screenshot was not written: {SCREENSHOT_PATH}")


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
            activate_preview(world)
        elif state["phase"] == "warming":
            elapsed = unreal.GameplayStatics.get_time_seconds(world) - state["phase_started"]
            if elapsed >= 1.0:
                queue_preview_impulses()
        elif state["phase"] == "capturing":
            capture_preview()
        if state["result"] is None and now >= state["deadline"]:
            finish(False, f"preview timed out in phase={state['phase']}")
    except Exception as exc:
        finish(False, f"exception={exc!r}; phase={state['phase']}")


editor_world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
if not editor_world:
    raise RuntimeError(f"Failed to load water preview map: {MAP_PATH}")

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
tick_handle = unreal.register_slate_post_tick_callback(on_tick)
if not unreal.RoverEditorTestLibrary.request_play_in_new_window():
    unreal.unregister_slate_post_tick_callback(tick_handle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    raise RuntimeError("Unable to request a new-window PIE session")
