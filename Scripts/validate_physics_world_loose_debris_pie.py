import time

import unreal


CONFIG_PATH = (
    "/Game/PhysicsWorldDemo/LooseDebris/Config/"
    "DA_WorldLooseDebrisConfig.DA_WorldLooseDebrisConfig"
)
DATA_CHANNEL_PATH = (
    "/Game/PhysicsWorldDemo/LooseDebris/DataChannels/"
    "NDC_LooseDebrisInteraction.NDC_LooseDebrisInteraction"
)
REGION_TAG = "PhysicsWorldLooseDebrisRegion"
START_TIMEOUT_SECONDS = 30.0
PHASE_TIMEOUT_SECONDS = 8.0
STOP_TIMEOUT_SECONDS = 15.0
SUCCESS_MARKER = "PHYSICS_WORLD_LOOSE_DEBRIS_PIE_OK"
FAILURE_MARKER = "PHYSICS_WORLD_LOOSE_DEBRIS_PIE_FAIL"


level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
unreal_editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
state = {
    "phase": "starting",
    "deadline": time.monotonic() + START_TIMEOUT_SECONDS,
    "result": None,
    "last": "PIE has not started",
    "world": None,
    "pawn": None,
    "locomotion": None,
    "combat": None,
    "subsystem": None,
    "region": None,
    "settings": None,
    "phase_started": 0.0,
    "movement_fields": 0,
    "attack_fields": 0,
    "attack_wake": 0,
    "force_coverage": 0,
    "ambient_rotation": "unset",
    "jump_fields": 0,
    "landing_fields": 0,
    "explosion_fields": 0,
    "burst_count": 0,
    "ndc_writes": 0,
    "dropped_fields": 0,
    "attack_request_attempts": 0,
    "emission_tail": 0.0,
    "interaction_lifetime": 0.0,
}
tick_handle = None


def enum_name(value):
    return str(value).upper()


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
    locomotion = state["locomotion"]
    if locomotion:
        locomotion.set_move_input(
            unreal.Vector2D(0.0, 0.0), unreal.Vector(0.0, 0.0, 0.0)
        )
        locomotion.stop_jump()
    combat = state["combat"]
    if combat:
        combat.set_light_attack_held(False)
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


def disable_demo_targets(world):
    for tag in ("RoverP0TrainingEnemy", "PhysicsWorldP0Box"):
        for actor in unreal.GameplayStatics.get_all_actors_with_tag(world, tag):
            if actor:
                actor.set_actor_enable_collision(False)


def begin_validation(world):
    config = unreal.load_asset(CONFIG_PATH)
    if not isinstance(config, unreal.WorldLooseDebrisConfig):
        fail(f"missing config={CONFIG_PATH}")
        return
    schema_version = int(config.get_editor_property("asset_schema_version"))
    if schema_version < 1:
        fail(f"loose-debris config migration is stale schema={schema_version}")
        return
    data_channel = unreal.load_asset(DATA_CHANNEL_PATH)
    if not data_channel or data_channel.get_class().get_name() != "NiagaraDataChannelAsset":
        fail(f"missing data_channel={DATA_CHANNEL_PATH}")
        return
    settings = config.get_editor_property("settings")
    state["settings"] = settings
    if not settings.get_editor_property("enabled"):
        fail("loose-debris config is disabled")
        return
    if not settings.get_editor_property("write_niagara_data_channel"):
        fail("Niagara Data Channel writes are disabled")
        return
    emission_tail = float(settings.get_editor_property("interaction_emission_tail"))
    interaction_lifetime = float(
        settings.get_editor_property("interaction_particle_lifetime")
    )
    if emission_tail <= 0.0 or interaction_lifetime < 14.9:
        fail(
            f"invalid settling lifecycle emission_tail={emission_tail:.2f}s "
            f"lifetime={interaction_lifetime:.2f}s"
        )
        return
    configured_channel = settings.get_editor_property("interaction_data_channel")
    if not configured_channel or configured_channel.get_path_name() != DATA_CHANNEL_PATH:
        fail(f"unexpected configured channel={configured_channel}")
        return
    for property_name in (
        "ambient_effect",
        "movement_effect",
        "attack_effect",
        "jump_effect",
        "landing_effect",
        "explosion_effect",
    ):
        if not settings.get_editor_property(property_name):
            fail(f"config has no {property_name}")
            return
    for property_name in (
        "movement_interaction_spawn_rate",
        "attack_interaction_spawn_rate",
        "jump_interaction_spawn_rate",
        "landing_interaction_spawn_rate",
        "explosion_interaction_spawn_rate",
    ):
        if float(settings.get_editor_property(property_name)) <= 0.0:
            fail(f"config has invalid {property_name}")
            return

    ambient_rotational_drag = float(
        settings.get_editor_property("ambient_rotational_drag")
    )
    ambient_leaf_rotation = float(
        settings.get_editor_property("ambient_leaf_rotation_strength")
    )
    ambient_paper_rotation = float(
        settings.get_editor_property("ambient_paper_rotation_strength")
    )
    ambient_restitution = float(settings.get_editor_property("ambient_restitution"))
    if (
        ambient_rotational_drag <= 0.0
        or ambient_leaf_rotation < 0.0
        or ambient_paper_rotation < 0.0
        or ambient_restitution < 0.0
    ):
        fail(
            "invalid ambient settling rotation settings "
            f"leaf={ambient_leaf_rotation:.2f} paper={ambient_paper_rotation:.2f} "
            f"drag={ambient_rotational_drag:.2f} restitution={ambient_restitution:.2f}"
        )
        return
    state["ambient_rotation"] = (
        f"{ambient_leaf_rotation:.2f}/{ambient_paper_rotation:.2f}"
        f"@{ambient_rotational_drag:.2f}"
    )

    for property_name in (
        "attack_wake_force_scale",
        "attack_wake_radius_scale",
        "attack_wake_forward_offset_scale",
        "attack_wake_height",
        "attack_wake_falloff_exponent",
        "attack_wake_duration",
        "minimum_ground_release_offset",
        "max_force_origin_offset_ratio",
    ):
        if float(settings.get_editor_property(property_name)) <= 0.0:
            fail(f"config has invalid {property_name}")
            return

    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    if pawn is None:
        state["last"] = "waiting for player pawn"
        return
    movement = pawn.get_component_by_class(unreal.CharacterMovementComponent)
    if movement is None or not movement.is_moving_on_ground():
        state["last"] = "waiting for grounded player"
        return
    mesh = pawn.get_component_by_class(unreal.SkeletalMeshComponent)
    anim_instance = mesh.get_anim_instance() if mesh else None
    if anim_instance is None or not isinstance(anim_instance, unreal.RoverAnimInstance):
        state["last"] = "waiting for Rover animation instance"
        return
    locomotion = pawn.get_locomotion_component()
    combat = pawn.get_combat_component()
    subsystem = unreal.RoverEditorTestLibrary.get_world_interaction_subsystem(world)
    if not locomotion or not combat or not subsystem:
        fail("player or world is missing the interaction components")
        return

    regions = unreal.GameplayStatics.get_all_actors_with_tag(world, REGION_TAG)
    region = next(
        (actor for actor in regions if isinstance(actor, unreal.WorldLooseDebrisRegion)),
        None,
    )
    if region is None:
        fail(f"map has no tagged {REGION_TAG} actor")
        return
    if not region.is_ambient_effect_active():
        ambient_component = region.get_component_by_class(unreal.NiagaraComponent)
        ambient_asset = (
            ambient_component.get_editor_property("asset")
            if ambient_component
            else None
        )
        state["last"] = (
            "waiting for ambient Niagara activation "
            f"component={ambient_component is not None} "
            f"asset={ambient_asset.get_path_name() if ambient_asset else 'none'}"
        )
        return

    disable_demo_targets(world)
    subsystem.reset_debug_stats()
    state.update(
        {
            "world": world,
            "pawn": pawn,
            "locomotion": locomotion,
            "combat": combat,
            "anim_instance": anim_instance,
            "subsystem": subsystem,
            "region": region,
            "initial_bursts": region.get_spawned_burst_count(),
            "initial_handled": region.get_handled_field_count(),
            "emission_tail": emission_tail,
            "interaction_lifetime": interaction_lifetime,
        }
    )
    start_phase("stationary", 3.0)


def validate_stationary():
    subsystem = state["subsystem"]
    if elapsed_in_phase() < 0.35:
        return
    if subsystem.get_published_lightweight_field_count() != 0:
        fail(
            "stationary player published a movement field; count="
            f"{subsystem.get_published_lightweight_field_count()}"
        )
        return
    if subsystem.get_processed_request_count() != 0:
        fail("stationary lightweight phase processed a Chaos interaction request")
        return
    locomotion = state["locomotion"]
    locomotion.set_move_input(
        unreal.Vector2D(0.0, 1.0), unreal.Vector(1.0, 0.0, 0.0)
    )
    start_phase("movement")


def validate_movement():
    state["pawn"].add_movement_input(unreal.Vector(1.0, 0.0, 0.0), 1.0)
    subsystem = state["subsystem"]
    field = subsystem.get_last_lightweight_field()
    published_count = subsystem.get_published_lightweight_field_count()
    state["last"] = (
        f"waiting for movement field published="
        f"{published_count} source={field.source_type} "
        f"handled={state['region'].get_handled_field_count()} "
        f"interaction_systems={state['region'].get_active_burst_system_count()}"
    )
    if published_count < 1 or "MOVEMENT" not in enum_name(field.source_type):
        return
    if state["region"].get_handled_field_count() <= state["initial_handled"]:
        return
    state["movement_fields"] = published_count
    state["locomotion"].set_move_input(
        unreal.Vector2D(0.0, 0.0), unreal.Vector(0.0, 0.0, 0.0)
    )
    subsystem.reset_debug_stats()
    start_phase("settling_after_movement", 4.0)


def validate_settling_after_movement():
    speed = state["pawn"].get_velocity().length()
    state["last"] = f"waiting for movement to settle speed={speed:.1f}"
    if speed > 5.0:
        return
    if not state["combat"].request_attack():
        state["last"] = "waiting for Attack01 request acceptance"
        return
    state["attack_request_attempts"] += 1
    state["attack_request_id"] = state["combat"].get_attack_request_id()
    start_phase("attack")


def validate_attack():
    subsystem = state["subsystem"]
    field = subsystem.get_last_lightweight_field()
    montage = state["anim_instance"].get_current_active_montage()
    if (
        "ATTACK" not in enum_name(field.source_type)
        and not state["combat"].is_attacking()
        and state["attack_request_attempts"] < 3
    ):
        if state["combat"].request_attack():
            state["attack_request_attempts"] += 1
            state["attack_request_id"] = state["combat"].get_attack_request_id()
        return
    state["last"] = (
        f"waiting for empty-swing attack field trace="
        f"{state['combat'].is_weapon_trace_active()} source={field.source_type} "
        f"request={state['combat'].get_attack_request_id()} "
        f"initial_request={state.get('attack_request_id', 0)} "
        f"phase={state['combat'].get_combat_phase()} "
        f"attacking={state['combat'].is_attacking()} "
        f"montage={montage} "
        f"move_stop_pending={state['locomotion'].is_move_stop_pending()} "
        f"move_stop_active={state['locomotion'].is_move_stop_active()}"
    )
    if "ATTACK" not in enum_name(field.source_type):
        return
    state["attack_fields"] = subsystem.get_published_lightweight_field_count()
    if not state["region"].is_attack_wake_active():
        fail("attack field did not activate the directional debris wake")
        return
    wake_target = state["region"].get_last_attack_wake_target()
    ground_location = state["region"].get_last_interaction_spawn_location()
    wake_delta = wake_target - ground_location
    direction = field.direction
    direction_size_2d = (direction.x * direction.x + direction.y * direction.y) ** 0.5
    horizontal_wake_size = (wake_delta.x * wake_delta.x + wake_delta.y * wake_delta.y) ** 0.5
    if wake_delta.z <= 0.0 or horizontal_wake_size <= 1.0:
        fail(
            f"attack wake target is not ahead and above ground delta={wake_delta}"
        )
        return
    if direction_size_2d > 0.01:
        wake_dot_direction = (
            wake_delta.x * direction.x + wake_delta.y * direction.y
        ) / direction_size_2d
        if wake_dot_direction <= 0.0:
            fail(
                "attack wake target does not follow the published attack direction "
                f"dot={wake_dot_direction:.2f}"
            )
            return
    state["attack_wake"] = 1
    if subsystem.get_processed_request_count() != 0:
        fail("empty weapon swing leaked into the Chaos request path")
        return
    state["ndc_writes"] += subsystem.get_niagara_data_channel_write_count()
    subsystem.reset_debug_stats()
    start_phase("waiting_for_attack_finish", 5.0)


def validate_waiting_for_attack_finish():
    if state["combat"].is_attacking():
        state["last"] = "waiting for Attack01 to finish before jump"
        return
    movement = state["pawn"].get_component_by_class(unreal.CharacterMovementComponent)
    if movement is None or not movement.is_moving_on_ground():
        state["last"] = "waiting for grounded player after Attack01"
        return
    if not state["locomotion"].try_jump():
        state["last"] = "waiting for grounded jump acceptance"
        return
    start_phase("jump", 2.0)


def validate_jump():
    subsystem = state["subsystem"]
    field = subsystem.get_last_lightweight_field()
    state["last"] = f"waiting for jump field source={field.source_type}"
    if "JUMP" not in enum_name(field.source_type):
        return
    state["jump_fields"] = subsystem.get_published_lightweight_field_count()
    state["ndc_writes"] += subsystem.get_niagara_data_channel_write_count()
    if subsystem.get_processed_request_count() != 0:
        fail("jump field leaked into the Chaos request path")
        return
    subsystem.reset_debug_stats()
    start_phase("landing", 6.0)


def validate_landing():
    subsystem = state["subsystem"]
    field = subsystem.get_last_lightweight_field()
    state["last"] = f"waiting for landing field source={field.source_type}"
    if "LANDING" not in enum_name(field.source_type):
        return
    state["landing_fields"] = subsystem.get_published_lightweight_field_count()
    state["ndc_writes"] += subsystem.get_niagara_data_channel_write_count()
    if subsystem.get_processed_request_count() != 0:
        fail("landing field leaked into the Chaos request path")
        return

    subsystem.reset_debug_stats()
    field = unreal.WorldLightweightInteractionField()
    field.set_editor_property(
        "source_type", unreal.WorldLightweightInteractionSource.EXPLOSION
    )
    field.set_editor_property(
        "shape_type", unreal.WorldLightweightInteractionShape.SPHERE
    )
    location = state["pawn"].get_actor_location()
    field.set_editor_property("start", location)
    field.set_editor_property("end", location)
    field.set_editor_property("direction", unreal.Vector(1.0, 0.0, 0.0))
    field.set_editor_property("radius", 260.0)
    # Mirrors the default Explosion lift/strength ratio (0.45 / 0.35), which
    # used to place the underground force source outside its own radius.
    field.set_editor_property("strength", 350.0)
    field.set_editor_property("upward_lift", 450.0)
    field.set_editor_property("duration", 0.25)
    field.set_editor_property("falloff_exponent", 1.0)
    if not subsystem.publish_lightweight_interaction_field(field):
        fail("valid lightweight explosion field was rejected")
        return
    force_origin = state["region"].get_last_interaction_force_origin()
    ground_location = state["region"].get_last_interaction_spawn_location()
    force_radius = state["region"].get_last_interaction_force_radius()
    force_delta = force_origin - ground_location
    force_offset = (
        force_delta.x * force_delta.x
        + force_delta.y * force_delta.y
        + force_delta.z * force_delta.z
    ) ** 0.5
    max_offset_ratio = float(
        state["settings"].get_editor_property("max_force_origin_offset_ratio")
    )
    if (
        force_radius <= 0.0
        or force_offset >= force_radius
        or force_offset > force_radius * max_offset_ratio + 1.0
    ):
        fail(
            "grounded debris is outside the interaction force coverage "
            f"offset={force_offset:.1f} radius={force_radius:.1f} "
            f"max_ratio={max_offset_ratio:.2f}"
        )
        return
    state["force_coverage"] = 1
    state["explosion_fields"] = subsystem.get_published_lightweight_field_count()
    state["ndc_writes"] += subsystem.get_niagara_data_channel_write_count()
    start_phase("limit_check", 2.0)


def validate_limit_check():
    if elapsed_in_phase() < 0.05:
        return
    subsystem = state["subsystem"]
    if state["region"].get_spawned_burst_count() != state["initial_bursts"]:
        fail("interaction fields spawned new particle systems instead of disturbing ambient debris")
        return
    if state["region"].get_active_burst_system_count() != 0:
        fail("an interaction-only Niagara system is active")
        return
    if subsystem.get_processed_request_count() != 0:
        fail("lightweight explosion field processed a Chaos interaction request")
        return
    if subsystem.get_niagara_data_channel_write_count() < 1:
        fail("lightweight explosion field did not write the Niagara Data Channel")
        return

    subsystem.reset_debug_stats()
    field = unreal.WorldLightweightInteractionField()
    field.set_editor_property("source_id", 987654)
    field.set_editor_property(
        "source_type", unreal.WorldLightweightInteractionSource.MOVEMENT
    )
    field.set_editor_property(
        "shape_type", unreal.WorldLightweightInteractionShape.CAPSULE
    )
    location = state["pawn"].get_actor_location()
    field.set_editor_property("start", location)
    field.set_editor_property("end", location + unreal.Vector(10.0, 0.0, 0.0))
    field.set_editor_property("direction", unreal.Vector(1.0, 0.0, 0.0))
    field.set_editor_property("source_velocity", unreal.Vector(300.0, 0.0, 0.0))
    field.set_editor_property("radius", 140.0)
    field.set_editor_property("strength", 240.0)
    field.set_editor_property("upward_lift", 45.0)
    field.set_editor_property("duration", 0.08)
    field.set_editor_property("falloff_exponent", 1.5)
    accepted_first = subsystem.publish_lightweight_interaction_field(field)
    accepted_second = subsystem.publish_lightweight_interaction_field(field)
    if not accepted_first or accepted_second:
        fail(
            f"per-source movement budget failed first={accepted_first} "
            f"second={accepted_second}"
        )
        return
    state["dropped_fields"] = subsystem.get_dropped_lightweight_field_count()
    if state["dropped_fields"] != 1:
        fail(f"unexpected dropped-field count={state['dropped_fields']}")
        return
    if subsystem.get_processed_request_count() != 0:
        fail("budget test leaked into the Chaos request path")
        return
    state["ndc_writes"] += subsystem.get_niagara_data_channel_write_count()
    state["burst_count"] = state["region"].get_spawned_burst_count()
    start_phase("settle_check", state["emission_tail"] + 2.0)


def validate_settle_check():
    emitting_count = state["region"].get_emitting_interaction_system_count()
    ground_projected = state["region"].was_last_interaction_ground_projected()
    spawn_location = state["region"].get_last_interaction_spawn_location()
    state["last"] = (
        f"waiting for interaction emission to stop emitting={emitting_count} "
        f"tail={state['emission_tail']:.2f}s projected={ground_projected} "
        f"spawn_z={spawn_location.z:.1f}"
    )
    if elapsed_in_phase() < state["emission_tail"] + 0.2:
        return
    if emitting_count != 0:
        fail("interaction Niagara systems kept emitting after the configured tail")
        return
    if not ground_projected:
        fail("the interaction Niagara spawn point was not projected onto a world surface")
        return

    finish(
        True,
        " ".join(
            (
                "stationary_fields=0",
                f"movement_fields={state['movement_fields']}",
                f"attack_fields={state['attack_fields']}",
                f"attack_wake={state['attack_wake']}",
                f"force_coverage={state['force_coverage']}",
                f"ambient_rotation={state['ambient_rotation']}",
                f"attack_request_attempts={state['attack_request_attempts']}",
                f"jump_fields={state['jump_fields']}",
                f"landing_fields={state['landing_fields']}",
                f"explosion_fields={state['explosion_fields']}",
                f"interaction_systems={state['burst_count']}",
                f"ndc_writes={state['ndc_writes']}",
                f"budget_drops={state['dropped_fields']}",
                "ground_projected=1",
                f"spawn_z={spawn_location.z:.1f}",
                f"lifecycle={state['emission_tail']:.1f}s->"
                f"{state['interaction_lifetime']:.1f}s",
                "chaos_requests=0",
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
        elif state["phase"] == "settling_after_movement":
            validate_settling_after_movement()
        elif state["phase"] == "attack":
            validate_attack()
        elif state["phase"] == "waiting_for_attack_finish":
            validate_waiting_for_attack_finish()
        elif state["phase"] == "jump":
            validate_jump()
        elif state["phase"] == "landing":
            validate_landing()
        elif state["phase"] == "limit_check":
            validate_limit_check()
        elif state["phase"] == "settle_check":
            validate_settle_check()

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
