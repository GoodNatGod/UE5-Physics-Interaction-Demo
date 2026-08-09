import math

import unreal


MAP_PATH = "/Game/ThirdPerson/Lvl_ThirdPerson"
MOVEMENT_CONFIG_PATH = "/Game/Rover/Config/DA_RoverMovementConfig"
BRIDGE_TAG = "PhysicsWorldRopeBridge"

BRIDGE_TUNING = {
    "swing2_limit_degrees": 0.8,
    "direct_hit_impulse_scale": 0.05,
    "maximum_direct_hit_impulse": 50.0,
    "attack_response_linear_damping_multiplier": 4.0,
    "attack_response_angular_damping_multiplier": 6.0,
    "attack_response_damping_grace_time": 1.0,
    "enable_unloaded_angular_recovery": True,
    "unloaded_recovery_delay": 0.5,
    "unloaded_recovery_blend_in_time": 1.0,
    "unloaded_recovery_angular_stiffness": 1200.0,
    "unloaded_recovery_angular_damping": 75.0,
    "unloaded_recovery_angular_force_limit": 0.0,
    "unloaded_recovery_rest_tolerance_degrees": 2.0,
    "unloaded_recovery_stop_angular_speed_degrees": 5.0,
    "suppress_root_motion_movement_impulses": True,
    "root_motion_movement_impulse_suppression_grace_time": 0.60,
    "minimum_landing_speed": 180.0,
}
MOVEMENT_TUNING = {
    "gravity_scale": 1.40,
    "attack_advance_scale_on_simulated_base": 0.55,
    "attack_advance_duration_scale_on_simulated_base": 2.50,
    "attack_advance_ease_on_simulated_base": 1.0,
    "attack_physics_push_scale_on_simulated_base": 0.0,
    "attack_standing_downward_force_scale_on_simulated_base": 1.0,
}


def find_bridge():
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    bridges = [
        actor
        for actor in actor_subsystem.get_all_level_actors()
        if actor and actor.actor_has_tag(BRIDGE_TAG)
    ]
    if len(bridges) != 1:
        paths = ", ".join(actor.get_path_name() for actor in bridges) or "none"
        raise RuntimeError(
            f"Expected exactly one actor tagged {BRIDGE_TAG}; "
            f"found {len(bridges)}: {paths}"
        )

    bridge = bridges[0]
    if not isinstance(bridge, unreal.WorldRopeBridge):
        raise RuntimeError(
            f"Actor tagged {BRIDGE_TAG} is not a WorldRopeBridge: "
            f"{bridge.get_path_name()}"
        )
    if not bool(bridge.get_editor_property("override_shared_settings")):
        raise RuntimeError(
            f"Bridge instance must already use Override Shared Settings: "
            f"{bridge.get_path_name()}"
        )
    return bridge


def read_values(container, tuning):
    return {
        property_name: container.get_editor_property(property_name)
        for property_name in tuning
    }


def apply_values(container, tuning):
    for property_name, value in tuning.items():
        container.set_editor_property(property_name, value)


def verify_values(container, tuning, context):
    for property_name, expected in tuning.items():
        actual = container.get_editor_property(property_name)
        if isinstance(expected, bool):
            matches = bool(actual) is expected
        else:
            matches = math.isclose(
                float(actual),
                float(expected),
                rel_tol=0.0,
                abs_tol=1.0e-4,
            )
        if not matches:
            raise RuntimeError(
                f"Failed to set {context}.{property_name}: "
                f"expected={expected!r} actual={actual!r}"
            )


def compact_values(values):
    return ",".join(f"{name}={value}" for name, value in values.items())


def main():
    world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
    if not world:
        raise RuntimeError(f"Failed to load map: {MAP_PATH}")

    bridge = find_bridge()
    movement_config = unreal.load_asset(MOVEMENT_CONFIG_PATH)
    if not isinstance(movement_config, unreal.RoverMovementConfig):
        raise RuntimeError(f"Missing Rover movement config: {MOVEMENT_CONFIG_PATH}")

    # Read the instance override directly so every unlisted hand-tuned value remains intact.
    bridge_settings = bridge.get_editor_property("override_settings")
    movement_settings = movement_config.get_editor_property("settings")
    bridge_before = read_values(bridge_settings, BRIDGE_TUNING)
    movement_before = read_values(movement_settings, MOVEMENT_TUNING)

    preserved_bridge_summary = {
        "planks": int(bridge_settings.get_editor_property("plank_count")),
        "width": float(bridge_settings.get_editor_property("plank_width")),
        "sag": float(bridge_settings.get_editor_property("bridge_sag")),
        "swing1": float(
            bridge_settings.get_editor_property("swing1_limit_degrees")
        ),
        "movement_impulse": float(
            bridge_settings.get_editor_property("movement_impulse_at_reference_speed")
        ),
        "movement_interval": float(
            bridge_settings.get_editor_property("movement_impulse_interval")
        ),
    }

    apply_values(bridge_settings, BRIDGE_TUNING)
    bridge.set_editor_property("override_settings", bridge_settings)
    apply_values(movement_settings, MOVEMENT_TUNING)
    movement_config.set_editor_property("settings", movement_settings)

    verify_values(
        bridge.get_editor_property("override_settings"),
        BRIDGE_TUNING,
        "bridge.override_settings",
    )
    verify_values(
        movement_config.get_editor_property("settings"),
        MOVEMENT_TUNING,
        "movement_config.settings",
    )

    bridge.rebuild_bridge()
    if not unreal.EditorAssetLibrary.save_loaded_asset(
        movement_config, only_if_is_dirty=False
    ):
        raise RuntimeError(f"Failed to save asset: {MOVEMENT_CONFIG_PATH}")
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH):
        raise RuntimeError(f"Failed to save map: {MAP_PATH}")

    unreal.log(
        "ROPE_BRIDGE_STABILITY_TUNING_OK "
        f"actor={bridge.get_path_name()} "
        f"map={MAP_PATH} movement_asset={MOVEMENT_CONFIG_PATH} "
        f"bridge_before=[{compact_values(bridge_before)}] "
        f"bridge_after=[{compact_values(BRIDGE_TUNING)}] "
        f"movement_before=[{compact_values(movement_before)}] "
        f"movement_after=[{compact_values(MOVEMENT_TUNING)}] "
        f"preserved_bridge_values=[{compact_values(preserved_bridge_summary)}]"
    )


main()
