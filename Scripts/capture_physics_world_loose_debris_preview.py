import os
import time
import math

import unreal


SCREENSHOT_PATH = os.environ.get(
    "ROVER_LOOSE_DEBRIS_SCREENSHOT",
    os.path.abspath(
        os.path.join(
            unreal.Paths.project_saved_dir(),
            "Screenshots",
            "WindowsEditor",
            "LooseDebrisPreview.png",
        )
    ),
)
SUCCESS_MARKER = "PHYSICS_WORLD_LOOSE_DEBRIS_PREVIEW_OK"
FAILURE_MARKER = "PHYSICS_WORLD_LOOSE_DEBRIS_PREVIEW_FAIL"
VIEW_PITCH = float(os.environ.get("ROVER_LOOSE_DEBRIS_VIEW_PITCH", "0.0"))


level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
unreal_editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
state = {
    "phase": "starting",
    "deadline": time.monotonic() + 30.0,
    "result": None,
    "world": None,
    "pawn": None,
    "subsystem": None,
    "activation_time": 0.0,
    "screenshot_request_time": 0.0,
    "warmup_done": False,
    "region": None,
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


def make_field(source_type, shape_type, start, end, direction, radius, strength, lift):
    field = unreal.WorldLightweightInteractionField()
    field.set_editor_property("source_type", source_type)
    field.set_editor_property("shape_type", shape_type)
    field.set_editor_property("start", start)
    field.set_editor_property("end", end)
    field.set_editor_property("direction", direction)
    field.set_editor_property("source_velocity", direction * strength)
    field.set_editor_property("radius", radius)
    field.set_editor_property("strength", strength)
    field.set_editor_property("upward_lift", lift)
    field.set_editor_property("duration", 0.25)
    field.set_editor_property("falloff_exponent", 1.2)
    return field


def normalized(vector):
    length = math.sqrt(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z)
    return vector / max(length, 0.0001)


def activate_preview(world):
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    if not pawn:
        return
    subsystem = unreal.RoverEditorTestLibrary.get_world_interaction_subsystem(world)
    if not subsystem:
        finish(False, "world interaction subsystem is unavailable")
        return
    regions = unreal.GameplayStatics.get_all_actors_with_tag(
        world, "PhysicsWorldLooseDebrisRegion"
    )
    region = next(
        (actor for actor in regions if isinstance(actor, unreal.WorldLooseDebrisRegion)),
        None,
    )
    if not region or not region.is_ambient_effect_active():
        return

    if os.environ.get("ROVER_LOOSE_DEBRIS_DISABLE_AMBIENT") == "1":
        ambient_component = region.get_editor_property("ambient_debris")
        if ambient_component:
            ambient_component.deactivate()

    controller = unreal.GameplayStatics.get_player_controller(world, 0)
    if controller:
        view_rotation = pawn.get_actor_rotation()
        view_rotation.pitch = VIEW_PITCH
        controller.set_control_rotation(view_rotation)
    unreal.SystemLibrary.execute_console_command(world, "pw.LooseDebris.DrawFields 1")

    state.update(
        {
            "phase": "warming",
            "deadline": time.monotonic() + 10.0,
            "world": world,
            "pawn": pawn,
            "subsystem": subsystem,
            "region": region,
            "activation_time": unreal.GameplayStatics.get_time_seconds(world),
        }
    )
    if os.environ.get("ROVER_LOOSE_DEBRIS_BASELINE") == "1":
        state["phase"] = "capturing"
        return
    # Prime all four burst systems in editor PIE before capturing the visible pass.
    publish_preview_fields()
    if state["result"] is None:
        state["phase"] = "warming"
        state["activation_time"] = unreal.GameplayStatics.get_time_seconds(world)
        state["warmup_done"] = True


def publish_preview_fields():
    world = state["world"]
    pawn = state["pawn"]
    subsystem = state["subsystem"]
    if state["warmup_done"]:
        unreal.SystemLibrary.execute_console_command(world, "pw.LooseDebris.DrawFields 0")

    location = pawn.get_actor_location()
    forward = pawn.get_actor_forward_vector()
    right = pawn.get_actor_right_vector()
    feet = location - unreal.Vector(0.0, 0.0, 80.0)
    fields = (
        make_field(
            unreal.WorldLightweightInteractionSource.MOVEMENT,
            unreal.WorldLightweightInteractionShape.CAPSULE,
            feet - forward * 80.0,
            feet + forward * 140.0,
            forward,
            150.0,
            360.0,
            55.0,
        ),
        make_field(
            unreal.WorldLightweightInteractionSource.ATTACK,
            unreal.WorldLightweightInteractionShape.CAPSULE,
            location + forward * 90.0 - right * 90.0,
            location + forward * 210.0 + right * 90.0,
            normalized(forward + right * 0.6),
            105.0,
            900.0,
            320.0,
        ),
        make_field(
            unreal.WorldLightweightInteractionSource.JUMP,
            unreal.WorldLightweightInteractionShape.SPHERE,
            feet - right * 220.0,
            feet - right * 220.0,
            unreal.Vector(0.0, 0.0, 1.0),
            170.0,
            300.0,
            220.0,
        ),
        make_field(
            unreal.WorldLightweightInteractionSource.LANDING,
            unreal.WorldLightweightInteractionShape.SPHERE,
            feet + forward * 210.0 - right * 160.0,
            feet + forward * 210.0 - right * 160.0,
            unreal.Vector(0.0, 0.0, 1.0),
            240.0,
            900.0,
            380.0,
        ),
        make_field(
            unreal.WorldLightweightInteractionSource.EXPLOSION,
            unreal.WorldLightweightInteractionShape.SPHERE,
            feet + forward * 360.0,
            feet + forward * 360.0,
            forward,
            260.0,
            1200.0,
            520.0,
        ),
    )
    if not all(subsystem.publish_lightweight_interaction_field(field) for field in fields):
        finish(False, "one or more preview fields were rejected")
        return

    state.update(
        {
            "phase": "capturing",
            "deadline": time.monotonic() + 10.0,
            "activation_time": unreal.GameplayStatics.get_time_seconds(world),
        }
    )


def capture_preview():
    world = state["world"]
    world_time = unreal.GameplayStatics.get_time_seconds(world)
    if state["screenshot_request_time"] <= 0.0:
        # Capture during the visible push window. The old 4-second delay only
        # proved that particles settled; it hid short movement/attack/jump events.
        if world_time - state["activation_time"] < 0.55:
            return
        region = state["region"]
        if not region or not region.is_ambient_effect_active():
            finish(False, "ambient debris system is not active at capture time")
            return
        if region.get_active_burst_system_count() != 0:
            finish(False, "interaction incorrectly spawned a separate Niagara system")
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
            f"path={SCREENSHOT_PATH} fields="
            f"{state['subsystem'].get_published_lightweight_field_count()} "
            f"interaction_systems={state['region'].get_spawned_burst_count()} "
            f"baseline={int(os.environ.get('ROVER_LOOSE_DEBRIS_BASELINE') == '1')}",
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
            if (
                unreal.GameplayStatics.get_time_seconds(state["world"])
                - state["activation_time"]
                >= 5.2
            ):
                publish_preview_fields()
        elif state["phase"] == "capturing":
            capture_preview()
        if state["result"] is None and now >= state["deadline"]:
            finish(False, f"preview timed out in phase={state['phase']}")
    except Exception as exc:
        finish(False, f"exception={exc!r}")


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
tick_handle = unreal.register_slate_post_tick_callback(on_tick)
if not unreal.RoverEditorTestLibrary.request_play_in_new_window():
    unreal.unregister_slate_post_tick_callback(tick_handle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    raise RuntimeError("Unable to request a new-window PIE session")
