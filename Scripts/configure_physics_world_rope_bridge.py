import os

import unreal


ROOT = "/Game/PhysicsWorldDemo"
CONFIG_PATH = f"{ROOT}/Config/DA_WorldInteractionConfig"
MOVEMENT_CONFIG_PATH = "/Game/Rover/Config/DA_RoverMovementConfig"
BLUEPRINT_DIRECTORY = f"{ROOT}/Blueprints"
BLUEPRINT_NAME = "BP_RopeBridge"
BLUEPRINT_PATH = f"{BLUEPRINT_DIRECTORY}/{BLUEPRINT_NAME}"
DEFAULT_MAP_PATH = "/Game/ThirdPerson/Lvl_ThirdPerson"
BRIDGE_TAG = "PhysicsWorldRopeBridge"
LARGE_PRESET_ENVIRONMENT = "ROVER_ROPE_BRIDGE_APPLY_LARGE_PRESET"
LONG_TUNING_ENVIRONMENT = "ROVER_ROPE_BRIDGE_APPLY_LONG_TUNING"
PLANK_COUNT_ENVIRONMENT = "ROVER_ROPE_BRIDGE_PLANK_COUNT"


def ensure_directory(path: str) -> None:
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def load_shared_assets():
    config = unreal.load_asset(CONFIG_PATH)
    if not isinstance(config, unreal.WorldInteractionConfig):
        raise RuntimeError(f"Missing World Interaction config: {CONFIG_PATH}")

    movement_config = unreal.load_asset(MOVEMENT_CONFIG_PATH)
    if not isinstance(movement_config, unreal.RoverMovementConfig):
        raise RuntimeError(f"Missing movement config: {MOVEMENT_CONFIG_PATH}")
    return config


def apply_large_bridge_preset(config):
    if os.environ.get(LARGE_PRESET_ENVIRONMENT) != "1":
        return False

    settings = config.get_editor_property("settings")
    bridge = settings.get_editor_property("rope_bridge")
    bridge.set_editor_property("plank_count", 15)
    bridge.set_editor_property("plank_width", 110.0)
    bridge.set_editor_property("plank_depth", 25.0)
    bridge.set_editor_property("plank_height", 6.0)
    bridge.set_editor_property("plank_gap", 3.0)
    bridge.set_editor_property("bridge_sag", 45.0)
    bridge.set_editor_property("anchor_extension", 60.0)
    bridge.set_editor_property("support_radius", 45.0)
    bridge.set_editor_property("support_height", 80.0)
    bridge.set_editor_property("linear_damping", 1.0)
    bridge.set_editor_property("angular_damping", 3.0)
    bridge.set_editor_property("projection_linear_alpha", 0.1)
    bridge.set_editor_property("position_solver_iterations", 12)
    bridge.set_editor_property("velocity_solver_iterations", 4)
    settings.set_editor_property("rope_bridge", bridge)
    config.set_editor_property("settings", settings)
    if not unreal.EditorAssetLibrary.save_loaded_asset(
        config, only_if_is_dirty=False
    ):
        raise RuntimeError(f"Failed to save large bridge preset to {CONFIG_PATH}")
    return True


def ensure_bridge_blueprint():
    blueprint = unreal.load_asset(BLUEPRINT_PATH)
    if blueprint and not isinstance(blueprint, unreal.Blueprint):
        raise RuntimeError(f"Unexpected asset at {BLUEPRINT_PATH}: {blueprint}")

    if not blueprint:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", unreal.WorldRopeBridge)
        blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            BLUEPRINT_NAME,
            BLUEPRINT_DIRECTORY,
            unreal.Blueprint,
            factory,
        )
    if not isinstance(blueprint, unreal.Blueprint):
        raise RuntimeError(f"Failed to create {BLUEPRINT_PATH}")

    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    compile_status = blueprint.get_editor_property("status")
    successful_statuses = (
        unreal.BlueprintStatus.BS_UP_TO_DATE,
        unreal.BlueprintStatus.BS_UP_TO_DATE_WITH_WARNINGS,
    )
    if compile_status not in successful_statuses:
        raise RuntimeError(
            f"Blueprint compile failed for {BLUEPRINT_PATH}: {compile_status}"
        )
    if not unreal.EditorAssetLibrary.save_loaded_asset(
        blueprint, only_if_is_dirty=False
    ):
        raise RuntimeError(f"Failed to save {BLUEPRINT_PATH}")
    generated_class = unreal.load_object(
        None, f"{BLUEPRINT_PATH}.{BLUEPRINT_NAME}_C"
    )
    if not generated_class:
        raise RuntimeError(f"Blueprint generated class is missing: {BLUEPRINT_PATH}")
    default_object = unreal.get_default_object(generated_class)
    if not isinstance(default_object, unreal.WorldRopeBridge):
        raise RuntimeError(
            f"{BLUEPRINT_PATH} does not inherit from WorldRopeBridge"
        )
    return blueprint, generated_class


def read_requested_plank_count():
    raw_value = os.environ.get(PLANK_COUNT_ENVIRONMENT)
    if raw_value is None:
        return None
    try:
        plank_count = int(raw_value)
    except ValueError as error:
        raise RuntimeError(
            f"Invalid {PLANK_COUNT_ENVIRONMENT} value: {raw_value!r}"
        ) from error
    if plank_count < 12:
        raise RuntimeError(f"Plank count must be at least 12; got {plank_count}")
    return plank_count


def apply_instance_tuning(bridge, requested_plank_count, apply_long_tuning):
    if requested_plank_count is None and not apply_long_tuning:
        return False

    instance_settings = bridge.get_resolved_bridge_settings()
    if apply_long_tuning:
        # Four-anchor wide bridge: 59 * (25cm + 3cm) + 25cm = 16.77m.
        instance_settings.set_editor_property("plank_count", 60)
        instance_settings.set_editor_property("plank_width", 200.0)
        instance_settings.set_editor_property("plank_depth", 25.0)
        instance_settings.set_editor_property("plank_height", 6.0)
        instance_settings.set_editor_property("plank_gap", 3.0)
        instance_settings.set_editor_property("bridge_sag", 120.0)
        instance_settings.set_editor_property("anchor_extension", 60.0)
        instance_settings.set_editor_property("anchor_lateral_inset", 15.0)
        instance_settings.set_editor_property("minimum_anchor_separation", 10.0)
        instance_settings.set_editor_property("support_radius", 30.0)
        instance_settings.set_editor_property("support_height", 220.0)
        instance_settings.set_editor_property("plank_mass_kg", 15.0)
        instance_settings.set_editor_property("linear_damping", 1.5)
        instance_settings.set_editor_property("angular_damping", 4.0)
        instance_settings.set_editor_property("swing1_limit_degrees", 10.0)
        instance_settings.set_editor_property("swing2_limit_degrees", 2.0)
        instance_settings.set_editor_property("twist_limit_degrees", 0.0)
        instance_settings.set_editor_property("projection_linear_alpha", 0.15)
        instance_settings.set_editor_property("projection_angular_alpha", 0.0)
        instance_settings.set_editor_property("projection_linear_tolerance", 2.0)
        instance_settings.set_editor_property("projection_angular_tolerance", 10.0)
        instance_settings.set_editor_property("enable_mass_conditioning", True)
        instance_settings.set_editor_property("use_continuous_collision_detection", True)
        instance_settings.set_editor_property("position_solver_iterations", 32)
        instance_settings.set_editor_property("velocity_solver_iterations", 8)
        instance_settings.set_editor_property("max_angular_velocity_degrees", 240.0)
        instance_settings.set_editor_property("max_depenetration_velocity", 200.0)
    if requested_plank_count is not None:
        instance_settings.set_editor_property("plank_count", requested_plank_count)

    bridge.set_editor_property("override_settings", instance_settings)
    bridge.set_editor_property("override_shared_settings", True)
    return True


def ensure_bridge_actor(
    config, generated_class, requested_plank_count, apply_long_tuning
):
    world = unreal.EditorLoadingAndSavingUtils.load_map(DEFAULT_MAP_PATH)
    if not world:
        raise RuntimeError(f"Failed to load map: {DEFAULT_MAP_PATH}")

    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = actor_subsystem.get_all_level_actors()
    bridges = [
        actor for actor in actors if actor and actor.actor_has_tag(BRIDGE_TAG)
    ]
    if len(bridges) > 1:
        bridge_paths = ", ".join(actor.get_path_name() for actor in bridges)
        raise RuntimeError(
            f"Expected at most one actor tagged {BRIDGE_TAG}; found "
            f"{len(bridges)}: {bridge_paths}"
        )
    player_starts = [
        actor for actor in actors if isinstance(actor, unreal.PlayerStart)
    ]
    if not player_starts:
        raise RuntimeError("The default map has no PlayerStart")

    bridge = bridges[0] if bridges else None
    if bridge and bridge.get_class() != generated_class:
        raise RuntimeError(
            f"Actor tagged {BRIDGE_TAG} must be an exact {BLUEPRINT_NAME} instance; "
            f"found {bridge.get_path_name()} ({bridge.get_class().get_path_name()})"
        )

    if not bridge:
        player_start = player_starts[0]
        start_location = player_start.get_actor_location()
        start_rotation = player_start.get_actor_rotation()
        # [PLACEHOLDER] Keep the first demo placement clear of the P0 crate row.
        bridge_location = (
            start_location
            + start_rotation.get_forward_vector() * 900.0
            + start_rotation.get_right_vector() * 450.0
            - unreal.Vector(0.0, 0.0, 90.0)
        )
        bridge_rotation = unreal.Rotator(0.0, start_rotation.yaw, 0.0)
        bridge = actor_subsystem.spawn_actor_from_class(
            generated_class, bridge_location, bridge_rotation
        )
        if not bridge:
            raise RuntimeError("Failed to place BP_RopeBridge")
        bridge.set_editor_property("interaction_config", config)
        tags = list(bridge.get_editor_property("tags"))
        bridge_tag = unreal.Name(BRIDGE_TAG)
        if bridge_tag not in tags:
            tags.append(bridge_tag)
            bridge.set_editor_property("tags", tags)
    bridge.set_actor_label("Physics World Rope Bridge")
    applied_instance_tuning = apply_instance_tuning(
        bridge, requested_plank_count, apply_long_tuning
    )
    bridge.rebuild_bridge()

    bridge_settings = bridge.get_resolved_bridge_settings()
    expected_plank_count = int(bridge_settings.get_editor_property("plank_count"))
    if expected_plank_count < 12:
        raise RuntimeError(
            f"Resolved plank count must be at least 12; got {expected_plank_count}"
        )
    expected_constraint_count = expected_plank_count + 3
    if bridge.get_generated_plank_count() != expected_plank_count:
        raise RuntimeError(
            f"Unexpected construction plank count: "
            f"{bridge.get_generated_plank_count()} "
            f"expected={expected_plank_count}"
        )
    if bridge.get_generated_constraint_count() != expected_constraint_count:
        raise RuntimeError(
            f"Unexpected construction constraint count: "
            f"{bridge.get_generated_constraint_count()} "
            f"expected={expected_constraint_count}"
        )
    if not bridge.has_valid_constraint_configuration():
        raise RuntimeError("BP_RopeBridge construction constraints are invalid")

    if not unreal.EditorLoadingAndSavingUtils.save_map(world, DEFAULT_MAP_PATH):
        raise RuntimeError(f"Failed to save map: {DEFAULT_MAP_PATH}")
    return bridge, applied_instance_tuning


def main() -> None:
    ensure_directory(ROOT)
    ensure_directory(BLUEPRINT_DIRECTORY)
    config = load_shared_assets()
    applied_large_preset = apply_large_bridge_preset(config)
    requested_plank_count = read_requested_plank_count()
    apply_long_tuning = os.environ.get(LONG_TUNING_ENVIRONMENT) == "1"
    blueprint, generated_class = ensure_bridge_blueprint()
    bridge, applied_instance_tuning = ensure_bridge_actor(
        config,
        generated_class,
        requested_plank_count,
        apply_long_tuning,
    )
    bridge_settings = bridge.get_resolved_bridge_settings()
    unreal.log(
        "PHYSICS_WORLD_ROPE_BRIDGE_CONFIG_OK "
        f"blueprint={blueprint.get_path_name()} "
        f"actor={bridge.get_path_name()} "
        f"planks={bridge.get_generated_plank_count()} "
        f"constraints={bridge.get_generated_constraint_count()} "
        f"requested_planks={requested_plank_count} "
        f"width={float(bridge_settings.get_editor_property('plank_width')):.1f} "
        f"anchor_inset="
        f"{float(bridge_settings.get_editor_property('anchor_lateral_inset')):.1f} "
        f"sag={float(bridge_settings.get_editor_property('bridge_sag')):.1f} "
        f"support_height="
        f"{float(bridge_settings.get_editor_property('support_height')):.1f} "
        f"instance_tuning={str(applied_instance_tuning).lower()} "
        f"large_preset={str(applied_large_preset).lower()}"
    )


main()
