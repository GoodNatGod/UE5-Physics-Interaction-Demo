import time

import unreal


MAP_PATH = "/Game/PhysicsWorldDemo/Maps/L_PhysicsWorldDemo_Lumen"
DEMO_REGION_TAG = "PhysicsWorldDemoDualLakeRegion"
EXPECTED_GRID_SIZE = 2400.0
EXPECTED_GRID_RESOLUTION = 512
START_TIMEOUT_SECONDS = 60.0
PHASE_TIMEOUT_SECONDS = 10.0
STOP_TIMEOUT_SECONDS = 15.0
CHARACTER_CENTER_DEPTH = 145.0
CHARACTER_FIELD_DEPTH = 240.0
OVERLAP_SETTLE_SECONDS = 0.15
SUCCESS_MARKER = "PHYSICS_WORLD_DUAL_LAKE_PIE_OK"
FAILURE_MARKER = "PHYSICS_WORLD_DUAL_LAKE_PIE_FAIL"
WATER_ADVANCED_COLLIDER_TAG = "RigidMesh_ShallowWaterCollider"


level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
unreal_editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
state = {
    "phase": "starting",
    "deadline": time.monotonic() + START_TIMEOUT_SECONDS,
    "result": None,
    "last": "PIE has not started",
    "world": None,
    "regions": None,
    "subsystem": None,
    "pawn": None,
    "events": None,
    "surface_locations": None,
    "event_index": 0,
    "before_forwarded": None,
    "before_source": None,
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


def source_count(region, source_name):
    getters = {
        "movement": region.get_movement_impulse_count,
        "jump": region.get_jump_impulse_count,
        "attack": region.get_attack_impulse_count,
        "landing": region.get_landing_impulse_count,
        "explosion": region.get_explosion_impulse_count,
    }
    return getters[source_name]()


def find_surface_location(region):
    center = region.get_resolved_domain_center()
    size = region.get_resolved_domain_world_size()
    surface_z = region.get_water_surface_z()
    candidates = [unreal.Vector(center.x, center.y, surface_z)]
    for y_index in range(1, 10):
        for x_index in range(1, 10):
            candidates.append(
                unreal.Vector(
                    center.x + size.x * (x_index / 10.0 - 0.5),
                    center.y + size.y * (y_index / 10.0 - 0.5),
                    surface_z,
                )
            )
    return next(
        (
            candidate
            for candidate in candidates
            if region.is_advanced_impact_location_on_target_water_body(candidate)
        ),
        None,
    )


def make_field(region, source_name):
    source_types = {
        "movement": unreal.WorldLightweightInteractionSource.MOVEMENT,
        "jump": unreal.WorldLightweightInteractionSource.JUMP,
        "attack": unreal.WorldLightweightInteractionSource.ATTACK,
        "landing": unreal.WorldLightweightInteractionSource.LANDING,
        "explosion": unreal.WorldLightweightInteractionSource.EXPLOSION,
    }
    velocities = {
        "movement": unreal.Vector(600.0, 0.0, 0.0),
        "jump": unreal.Vector(300.0, 0.0, 1100.0),
        "attack": unreal.Vector(1200.0, 300.0, -180.0),
        "landing": unreal.Vector(0.0, 0.0, -1100.0),
        "explosion": unreal.Vector(0.0, 0.0, 0.0),
    }
    directions = {
        "movement": unreal.Vector(1.0, 0.0, 0.0),
        "jump": unreal.Vector(0.0, 0.0, 1.0),
        "attack": unreal.Vector(1.0, 0.25, -0.15),
        "landing": unreal.Vector(0.0, 0.0, -1.0),
        "explosion": unreal.Vector(0.0, 0.0, 1.0),
    }
    strengths = {
        "movement": 1.0,
        "jump": 2.0,
        "attack": 2.0,
        "landing": 2.5,
        "explosion": 4.0,
    }
    radii = {
        "movement": 160.0,
        "jump": 200.0,
        "attack": 220.0,
        "landing": 240.0,
        "explosion": 320.0,
    }
    location = state["surface_locations"][state["regions"].index(region)]
    if source_name in ("movement", "jump", "landing"):
        location = unreal.Vector(
            location.x,
            location.y,
            location.z - CHARACTER_FIELD_DEPTH,
        )
    field = unreal.WorldLightweightInteractionField()
    field.set_editor_property("source_actor", state["pawn"])
    field.set_editor_property("source_type", source_types[source_name])
    field.set_editor_property(
        "shape_type", unreal.WorldLightweightInteractionShape.SPHERE
    )
    field.set_editor_property("start", location)
    field.set_editor_property("end", location)
    field.set_editor_property("direction", directions[source_name])
    field.set_editor_property("source_velocity", velocities[source_name])
    field.set_editor_property("radius", radii[source_name])
    field.set_editor_property("strength", strengths[source_name])
    field.set_editor_property(
        "upward_lift", 900.0 if source_name == "explosion" else 0.0
    )
    field.set_editor_property("duration", 0.1)
    field.set_editor_property("falloff_exponent", 1.0)
    return field


def begin_validation(world):
    actors = unreal.GameplayStatics.get_all_actors_with_tag(world, DEMO_REGION_TAG)
    regions = sorted(
        [
            actor
            for actor in actors
            if actor and actor.get_class().get_name() == "WorldWaterRippleRegion"
        ],
        key=lambda actor: actor.get_actor_label(),
    )
    if len(regions) != 2:
        fail(f"Expected two tagged Lake ripple Regions; found={len(regions)}")
        return
    if not all(region.is_simulation_ready() for region in regions):
        state["last"] = "waiting for UBasicShallowWaterSubsystem initialization"
        return
    if not all(region.is_using_water_advanced_shallow_water() for region in regions):
        fail("A dual-Lake Region is not using WaterAdvanced shallow water")
        return

    targets = [region.get_editor_property("target_water_body") for region in regions]
    if any(
        not target or target.get_class().get_name() != "WaterBodyLake"
        for target in targets
    ):
        fail(f"A Region is not bound to WaterBodyLake: targets={targets}")
        return
    if targets[0] == targets[1]:
        fail("Both runtime Regions target the same WaterBodyLake")
        return
    for region in regions:
        if (
            region.get_current_state_render_target()
            or region.get_previous_state_render_target()
            or region.get_render_target_resolution() != 0
        ):
            fail(
                "WaterAdvanced Region unexpectedly created the legacy custom RT solver: "
                f"region={region.get_actor_label()}"
            )
            return
        if abs(region.get_advanced_grid_size() - EXPECTED_GRID_SIZE) > 1.0:
            fail(
                f"Unexpected WaterAdvanced grid size on {region.get_actor_label()}: "
                f"{region.get_advanced_grid_size()}"
            )
            return
        if region.get_advanced_grid_resolution() != EXPECTED_GRID_RESOLUTION:
            fail(
                f"Unexpected WaterAdvanced grid resolution on {region.get_actor_label()}: "
                f"{region.get_advanced_grid_resolution()}"
            )
            return

    surface_locations = [find_surface_location(region) for region in regions]
    if any(location is None for location in surface_locations):
        collision_details = " | ".join(
            f"{region.get_actor_label()}: {region.get_advanced_target_collision_debug_string()}"
            for region in regions
        )
        fail(
            "WaterAdvanced impact trace cannot hit one or both WaterBodyLake actors; "
            f"details={collision_details}"
        )
        return

    subsystem = unreal.RoverEditorTestLibrary.get_world_interaction_subsystem(world)
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    if not subsystem or not pawn:
        state["last"] = "waiting for player and WorldInteractionSubsystem"
        return
    mesh_component = pawn.get_component_by_class(unreal.SkeletalMeshComponent)
    if not mesh_component:
        fail("Rover pawn has no SkeletalMeshComponent for WaterAdvanced collision")
        return
    skeletal_mesh = mesh_component.get_editor_property("skeletal_mesh_asset")
    physics_asset = (
        skeletal_mesh.get_editor_property("physics_asset")
        if skeletal_mesh
        else None
    )
    if not physics_asset:
        fail("Rover Skeletal Mesh has no Physics Asset for WaterAdvanced collision")
        return
    component_tags = {
        str(tag)
        for tag in mesh_component.get_editor_property("component_tags")
    }
    if WATER_ADVANCED_COLLIDER_TAG not in component_tags:
        state["last"] = "waiting for WaterAdvanced to tag the Rover mesh collider"
        return
    physics_body_count = unreal.RoverEditorTestLibrary.get_physics_asset_body_count(
        physics_asset.get_path_name()
    )
    if physics_body_count <= 0:
        fail("Rover WaterAdvanced Physics Asset contains no collision bodies")
        return
    locomotion_component = pawn.get_component_by_class(
        unreal.RoverLocomotionComponent
    )
    if locomotion_component:
        locomotion_component.set_component_tick_enabled(False)
    for region in regions:
        region.reset_debug_stats()
    subsystem.reset_debug_stats()
    events = [
        (region_index, source_name)
        for region_index in range(2)
        for source_name in ("movement", "jump", "attack", "landing", "explosion")
    ]
    state.update(
        {
            "world": world,
            "regions": regions,
            "subsystem": subsystem,
            "pawn": pawn,
            "events": events,
            "surface_locations": surface_locations,
            "physics_body_count": physics_body_count,
        }
    )
    prepare_current_event()


def prepare_current_event():
    region_index, source_name = state["events"][state["event_index"]]
    if source_name == "movement":
        surface = state["surface_locations"][region_index]
        character_location = unreal.Vector(
            surface.x,
            surface.y,
            surface.z - CHARACTER_CENTER_DEPTH,
        )
        state["pawn"].set_actor_location(character_location, False, True)
        state["phase"] = "waiting_for_water_overlap"
        state["overlap_ready_time"] = time.monotonic() + OVERLAP_SETTLE_SECONDS
        state["deadline"] = time.monotonic() + PHASE_TIMEOUT_SECONDS
        state["last"] = (
            f"waiting for Lake {region_index + 1} pawn overlap "
            f"at {character_location}"
        )
        return
    publish_current_event()


def publish_current_event():
    event_index = state["event_index"]
    region_index, source_name = state["events"][event_index]
    regions = state["regions"]
    state["before_forwarded"] = [
        region.get_forwarded_advanced_impact_count() for region in regions
    ]
    state["before_source"] = [
        source_count(region, source_name) for region in regions
    ]
    if not state["subsystem"].publish_lightweight_interaction_field(
        make_field(regions[region_index], source_name)
    ):
        fail(
            f"WorldInteractionSubsystem rejected Lake {region_index + 1} "
            f"{source_name} field"
        )
        return
    state["phase"] = "checking_event"
    state["deadline"] = time.monotonic() + PHASE_TIMEOUT_SECONDS
    state["last"] = f"lake={region_index + 1} source={source_name}"


def validate_current_event():
    region_index, source_name = state["events"][state["event_index"]]
    regions = state["regions"]
    forwarded = [region.get_forwarded_advanced_impact_count() for region in regions]
    source_counts = [source_count(region, source_name) for region in regions]
    expected_forwarded = list(state["before_forwarded"])
    expected_source = list(state["before_source"])
    expected_forwarded[region_index] += 1
    expected_source[region_index] += 1
    state["last"] = (
        f"lake={region_index + 1} source={source_name} "
        f"forwarded={forwarded} expected={expected_forwarded} "
        f"source_counts={source_counts} expected_source={expected_source}"
    )
    if forwarded != expected_forwarded or source_counts != expected_source:
        return

    state["event_index"] += 1
    if state["event_index"] >= len(state["events"]):
        total_forwarded = [
            region.get_forwarded_advanced_impact_count() for region in regions
        ]
        total_handled = [
            region.get_handled_lightweight_field_count() for region in regions
        ]
        if (
            any(count < 5 for count in total_forwarded)
            or total_handled != total_forwarded
        ):
            fail(
                f"Unexpected final WaterAdvanced counts: forwarded={total_forwarded} "
                f"handled={total_handled}"
            )
            return
        finish(
            True,
            " ".join(
                (
                    f"map={MAP_PATH}",
                    "subsystem=UBasicShallowWaterSubsystem",
                    "niagara=Grid2D_SW_WaterBody",
                    "regions=2",
                    "targets=2xWaterBodyLake",
                    f"grid={EXPECTED_GRID_SIZE:.0f}/{EXPECTED_GRID_RESOLUTION}",
                    "legacy_region_rts=0",
                    f"rover_collider={state['physics_body_count']}bodies/tagged",
                    "movement=2",
                    "jump=2",
                    "attack=2",
                    "landing=2",
                    "explosion=2",
                    "cross_region_impacts=0",
                    f"total_forwarded={total_forwarded}",
                )
            ),
        )
        return
    state["phase"] = "preparing_next"


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
        elif state["phase"] == "checking_event":
            validate_current_event()
        elif state["phase"] == "preparing_next":
            prepare_current_event()
        elif (
            state["phase"] == "waiting_for_water_overlap"
            and now >= state["overlap_ready_time"]
        ):
            publish_current_event()

        if state["result"] is None and now >= state["deadline"]:
            fail(f"validation timeout; last={state['last']}")
    except Exception as exc:
        fail(f"exception={exc!r}; phase={state['phase']} last={state['last']}")


editor_world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
if not editor_world:
    raise RuntimeError(f"Failed to load dual-Lake validation map: {MAP_PATH}")

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
tick_handle = unreal.register_slate_post_tick_callback(on_tick)
if not unreal.RoverEditorTestLibrary.request_play_in_new_window():
    unreal.unregister_slate_post_tick_callback(tick_handle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    raise RuntimeError("Unable to request a new-window PIE session")
