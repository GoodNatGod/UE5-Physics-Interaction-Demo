import time

import unreal


BOX_TAG = "PhysicsWorldP0Box"
EXPECTED_CONFIG = (
    "/Game/PhysicsWorldDemo/Config/"
    "DA_WorldInteractionConfig.DA_WorldInteractionConfig"
)
EXPECTED_SURFACE = unreal.PhysicalSurface.SURFACE_TYPE2
MINIMUM_DEBRIS_EXPANSION = 20.0
EXPECTED_DEBRIS_LIFETIME = 2.0
START_TIMEOUT_SECONDS = 30.0
CLEANUP_GRACE_SECONDS = 2.0
STOP_TIMEOUT_SECONDS = 15.0
SUCCESS_MARKER = "PHYSICS_WORLD_P0_VALIDATION_OK"
FAILURE_MARKER = "PHYSICS_WORLD_P0_VALIDATION_FAIL"


level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
unreal_editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
state = {
    "phase": "starting",
    "deadline": time.monotonic() + START_TIMEOUT_SECONDS,
    "result": None,
    "last": "PIE has not started",
    "world": None,
    "projectile": None,
    "box": None,
    "subsystem": None,
    "impact_time": None,
    "cleanup_after": None,
    "debris_cleanup_after": None,
    "receiver_count": 0,
    "decal_count": 0,
    "debris_expansion": 0.0,
}
tick_handle = None


def object_path(value):
    return value.get_path_name() if value else "None"


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


def validate_and_trigger(world):
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    if pawn is None:
        state["last"] = "waiting for player pawn"
        return

    skill = pawn.get_world_skill_component()
    if skill is None:
        fail("Rover pawn has no RoverWorldSkillComponent")
        return

    config = skill.get_editor_property("interaction_config")
    if object_path(config) != EXPECTED_CONFIG:
        fail(f"unexpected interaction config={object_path(config)}")
        return

    settings = skill.get_settings()
    if settings.get_editor_property("fireball_effect") is None:
        fail("interaction config has no fireball Niagara effect")
        return
    if settings.get_editor_property("explosion_effect") is None:
        fail("interaction config has no explosion Niagara effect")
        return

    boxes = unreal.GameplayStatics.get_all_actors_with_tag(world, BOX_TAG)
    candidates = [
        actor
        for actor in boxes
        if actor is not None
        and not actor.is_destroyed()
        and abs(actor.get_current_health() - actor.get_max_health()) <= 0.01
    ]
    if not candidates:
        fail(f"found no intact tagged destructible box among {len(boxes)}")
        return
    pawn_location = pawn.get_actor_location()
    candidates.sort(
        key=lambda actor: (
            (actor.get_actor_location().x - pawn_location.x) ** 2
            + (actor.get_actor_location().y - pawn_location.y) ** 2
            + (actor.get_actor_location().z - pawn_location.z) ** 2,
            actor.get_path_name(),
        )
    )
    box = candidates[0]
    if not box.has_geometry_collection_asset():
        fail("destructible box has no Geometry Collection asset")
        return

    subsystem = unreal.RoverEditorTestLibrary.get_world_interaction_subsystem(world)
    if subsystem is None:
        fail("UWorldInteractionSubsystem was not created for PIE")
        return
    subsystem.reset_debug_stats()

    if not skill.request_fireball():
        fail("RoverWorldSkillComponent rejected the initial fireball request")
        return
    projectile = skill.get_last_spawned_fireball()
    if projectile is None or projectile.has_detonated():
        fail("fireball request did not spawn a live projectile")
        return

    if not unreal.RoverEditorTestLibrary.trigger_p0_fireball_impact(projectile, box):
        fail("unable to trigger a real fireball impact against the P0 box")
        return

    result = subsystem.get_last_interaction_result()
    if not result.get_editor_property("accepted"):
        fail("world interaction request was not accepted")
        return
    if result.get_editor_property("surface_type") != EXPECTED_SURFACE:
        fail(
            "surface resolver did not return Wood; "
            f"surface={result.get_editor_property('surface_type')}"
        )
        return
    receiver_count = result.get_editor_property("receiver_count")
    if receiver_count < 1 or subsystem.get_dispatched_receiver_count() < 1:
        fail("explosion request did not reach the destructible receiver")
        return
    if subsystem.get_processed_request_count() != 1:
        fail(
            "unexpected processed request count="
            f"{subsystem.get_processed_request_count()}"
        )
        return
    if not result.get_editor_property("spawned_surface_feedback"):
        fail("explosion request spawned no surface feedback")
        return
    if not box.is_destroyed() or box.get_current_health() > 0.0:
        fail(
            "explosion did not destroy the box; "
            f"health={box.get_current_health():.2f}"
        )
        return
    if not box.is_geometry_collection_active():
        fail("box was destroyed without activating its Geometry Collection")
        return
    if not box.has_applied_break_impulse():
        fail("box break impulse was deferred after the explosion")
        return
    if not box.has_applied_break_strain():
        fail("explosion did not apply External Strain to the root cluster")
        return

    decal_count = subsystem.get_active_feedback_decal_count()
    max_decals = settings.get_editor_property("max_active_feedback_decals")
    if decal_count < 1 or decal_count > max_decals:
        fail(f"unexpected active decal count={decal_count} max={max_decals}")
        return

    debris_lifetime = settings.get_editor_property("destructible_debris_lifetime")
    decal_lifetime = (
        settings.get_editor_property("burn_decal_fade_start_delay")
        + settings.get_editor_property("burn_decal_fade_duration")
    )
    if debris_lifetime <= 0.0 or decal_lifetime <= 0.0:
        fail("P0 cleanup lifetimes must be positive in the interaction config")
        return
    if abs(debris_lifetime - EXPECTED_DEBRIS_LIFETIME) > 0.01:
        fail(
            f"debris lifetime={debris_lifetime:.2f}s "
            f"expected={EXPECTED_DEBRIS_LIFETIME:.2f}s"
        )
        return
    cleanup_after = max(debris_lifetime, decal_lifetime) + 0.5

    state.update(
        {
            "phase": "checking_cleanup",
            "deadline": time.monotonic() + cleanup_after + CLEANUP_GRACE_SECONDS,
            "last": "waiting for configured projectile/debris/decal cleanup",
            "world": world,
            "projectile": projectile,
            "box": box,
            "subsystem": subsystem,
            "impact_time": unreal.GameplayStatics.get_time_seconds(world),
            "cleanup_after": cleanup_after,
            "debris_cleanup_after": debris_lifetime + 0.5,
            "receiver_count": receiver_count,
            "decal_count": decal_count,
        }
    )


def validate_cleanup():
    world = state["world"]
    elapsed = unreal.GameplayStatics.get_time_seconds(world) - state["impact_time"]
    if elapsed < 0.1:
        return

    if unreal.SystemLibrary.is_valid(state["projectile"]):
        fail(f"projectile remains valid after {elapsed:.3f}s")
        return
    box_is_valid = unreal.SystemLibrary.is_valid(state["box"])
    if box_is_valid and elapsed >= state["debris_cleanup_after"]:
        fail(
            "destructible box outlived its configured debris lifetime; "
            f"elapsed={elapsed:.2f}s"
        )
        return
    if box_is_valid:
        if elapsed >= 0.1 and not state["box"].has_applied_break_impulse():
            state["last"] = "waiting for deferred Chaos debris impulse"
            if elapsed >= 1.0:
                fail("Geometry Collection never received its deferred break impulse")
            return
        state["debris_expansion"] = max(
            state["debris_expansion"],
            state["box"].get_debris_expansion_distance(),
        )
        if elapsed >= 1.0 and state["debris_expansion"] < MINIMUM_DEBRIS_EXPANSION:
            fail(
                "Geometry Collection pieces did not visibly separate; "
                f"expansion={state['debris_expansion']:.1f}cm"
            )
            return
    if elapsed < state["cleanup_after"]:
        state["last"] = (
            f"waiting for lifecycle cleanup elapsed={elapsed:.2f}s "
            f"debris_expansion={state['debris_expansion']:.1f}cm"
        )
        return
    if unreal.SystemLibrary.is_valid(state["box"]):
        fail("destructible box outlived its configured debris lifetime")
        return
    if state["subsystem"].get_active_feedback_decal_count() != 0:
        fail(
            "feedback decals outlived their configured fade; active="
            f"{state['subsystem'].get_active_feedback_decal_count()}"
        )
        return
    if state["debris_expansion"] < MINIMUM_DEBRIS_EXPANSION:
        fail(
            "debris expansion was never observed before cleanup; "
            f"expansion={state['debris_expansion']:.1f}cm"
        )
        return

    finish(
        True,
        " ".join(
            (
                f"config={EXPECTED_CONFIG}",
                "skill_component=true",
                "projectile_spawned=true",
                "surface=Wood",
                f"receivers={state['receiver_count']}",
                "geometry_collection=active",
                "external_strain=applied",
                f"debris_expansion={state['debris_expansion']:.1f}cm",
                f"decals={state['decal_count']}",
                "lifecycle_cleanup=true",
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
            validate_and_trigger(world)
        elif state["phase"] == "checking_cleanup":
            validate_cleanup()

        if state["result"] is None and now >= state["deadline"]:
            fail(f"validation timeout; last={state['last']}")
    except Exception as exc:
        fail(f"exception={exc!r}")


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
tick_handle = unreal.register_slate_post_tick_callback(on_tick)
if not unreal.RoverEditorTestLibrary.request_play_in_new_window():
    unreal.unregister_slate_post_tick_callback(tick_handle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    raise RuntimeError("Unable to request a new-window PIE session")
