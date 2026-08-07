import unreal


MAP_PATH = "/Game/ThirdPerson/Lvl_ThirdPerson"
BRIDGE_TAG = "PhysicsWorldRopeBridge"
SECONDARY_ANCHOR_TAG = "RopeBridgeSecondaryAnchor"
SUPPORT_PROPERTIES = (
    "left_support",
    "left_support_secondary",
    "right_support",
    "right_support_secondary",
)


def profile_summary(constraint):
    instance = constraint.get_editor_property("constraint_instance")
    profile = instance.get_editor_property("profile_instance")
    cone = profile.get_editor_property("cone_limit")
    twist = profile.get_editor_property("twist_limit")

    def short_name(value):
        return str(value).rsplit(".", 1)[-1].lower()

    return "/".join(
        (
            short_name(cone.get_editor_property("swing1_motion")),
            short_name(cone.get_editor_property("swing2_motion")),
            short_name(twist.get_editor_property("twist_motion")),
        )
    )


def main():
    world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
    if not world:
        raise RuntimeError(f"Failed to load map: {MAP_PATH}")

    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    bridges = [
        actor
        for actor in actor_subsystem.get_all_level_actors()
        if actor and actor.actor_has_tag(BRIDGE_TAG)
    ]
    if len(bridges) != 1:
        raise RuntimeError(
            f"Expected one actor tagged {BRIDGE_TAG}; found {len(bridges)}"
        )

    bridge = bridges[0]
    settings = bridge.get_resolved_bridge_settings()
    constraint_count = bridge.get_generated_constraint_count()
    constraints = [
        bridge.get_constraint_component(index) for index in range(constraint_count)
    ]
    anchor_constraints = [
        constraint
        for constraint in constraints
        if constraint and "Anchor" in constraint.get_name()
    ]
    secondary_constraints = [
        constraint
        for constraint in constraints
        if constraint
        and unreal.Name(SECONDARY_ANCHOR_TAG)
        in list(constraint.get_editor_property("component_tags"))
    ]
    primary_anchor_constraints = [
        constraint
        for constraint in anchor_constraints
        if constraint not in secondary_constraints
    ]
    secondary_anchor_constraints = [
        constraint
        for constraint in anchor_constraints
        if constraint in secondary_constraints
    ]
    supports = [
        bridge.get_editor_property(property_name)
        for property_name in SUPPORT_PROPERTIES
    ]
    frame_errors = [
        float(bridge.get_endpoint_position_error(index)) for index in range(4)
    ]
    secondary_projection_off = sum(
        not constraint.is_projection_enabled()
        for constraint in secondary_anchor_constraints
    )
    primary_profile = (
        profile_summary(primary_anchor_constraints[0])
        if primary_anchor_constraints
        else "missing"
    )
    secondary_profile = (
        profile_summary(secondary_anchor_constraints[0])
        if secondary_anchor_constraints
        else "missing"
    )
    plank_count = int(settings.get_editor_property("plank_count"))
    plank_depth = float(settings.get_editor_property("plank_depth"))
    plank_gap = float(settings.get_editor_property("plank_gap"))
    visible_length = float(plank_count - 1) * (plank_depth + plank_gap) + plank_depth
    player_starts = [
        actor
        for actor in actor_subsystem.get_all_level_actors()
        if isinstance(actor, unreal.PlayerStart)
    ]
    bridge_location = bridge.get_actor_location()
    start_distance = (
        (player_starts[0].get_actor_location() - bridge_location).length()
        if player_starts
        else -1.0
    )
    unreal.log(
        "PHYSICS_WORLD_ROPE_BRIDGE_INSPECT_OK "
        f"actor={bridge.get_path_name()} "
        f"location={bridge_location.x:.1f},{bridge_location.y:.1f},{bridge_location.z:.1f} "
        f"player_start_distance={start_distance:.1f}cm "
        f"override={str(bool(bridge.get_editor_property('override_shared_settings'))).lower()} "
        f"planks={plank_count} "
        f"visible_length={visible_length:.1f}cm "
        f"width={float(settings.get_editor_property('plank_width')):.1f} "
        f"depth={plank_depth:.1f} "
        f"anchor_inset="
        f"{float(settings.get_editor_property('anchor_lateral_inset')):.1f} "
        f"minimum_anchor_separation="
        f"{float(settings.get_editor_property('minimum_anchor_separation')):.1f} "
        f"sag={float(settings.get_editor_property('bridge_sag')):.1f} "
        f"supports={bridge.get_valid_support_count()}/"
        f"{sum(isinstance(support, unreal.StaticMeshComponent) for support in supports)} "
        f"support_radius={float(settings.get_editor_property('support_radius')):.1f} "
        f"support_height={float(settings.get_editor_property('support_height')):.1f} "
        f"mass={float(settings.get_editor_property('plank_mass_kg')):.1f}kg "
        f"damping={float(settings.get_editor_property('linear_damping')):.1f}/"
        f"{float(settings.get_editor_property('angular_damping')):.1f} "
        f"movement_impulse="
        f"{float(settings.get_editor_property('movement_impulse_at_reference_speed')):.1f}/"
        f"{float(settings.get_editor_property('movement_impulse_interval')):.2f}s "
        f"jump_load="
        f"{float(settings.get_editor_property('minimum_jump_takeoff_speed')):.1f}/"
        f"{float(settings.get_editor_property('jump_takeoff_impulse_scale')):.4f}/"
        f"{float(settings.get_editor_property('maximum_jump_takeoff_impulse')):.1f} "
        f"landing_load="
        f"{float(settings.get_editor_property('minimum_landing_speed')):.1f}/"
        f"{float(settings.get_editor_property('landing_impulse_scale')):.4f}/"
        f"{float(settings.get_editor_property('maximum_landing_impulse')):.1f} "
        f"recovery="
        f"{float(settings.get_editor_property('unloaded_recovery_delay')):.2f}s/"
        f"{float(settings.get_editor_property('unloaded_recovery_angular_stiffness')):.1f}/"
        f"{float(settings.get_editor_property('unloaded_recovery_angular_damping')):.1f} "
        f"swing={float(settings.get_editor_property('swing1_limit_degrees')):.1f}/"
        f"{float(settings.get_editor_property('swing2_limit_degrees')):.1f}/"
        f"{float(settings.get_editor_property('twist_limit_degrees')):.1f} "
        f"projection={float(settings.get_editor_property('projection_linear_alpha')):.2f}/"
        f"{float(settings.get_editor_property('projection_angular_alpha')):.2f} "
        f"solver={int(settings.get_editor_property('position_solver_iterations'))}/"
        f"{int(settings.get_editor_property('velocity_solver_iterations'))} "
        f"anchor_profiles={len(primary_anchor_constraints)}/"
        f"{len(secondary_anchor_constraints)} "
        f"primary_profile={primary_profile} "
        f"secondary_profile={secondary_profile} "
        f"secondary_projection_off={secondary_projection_off}/"
        f"{len(secondary_anchor_constraints)} "
        f"anchor_frame_error={max(frame_errors, default=0.0):.3f}cm "
        f"rest_angular_error="
        f"{float(bridge.get_maximum_plank_rest_angular_error_degrees()):.2f}deg "
        f"generated_planks={bridge.get_generated_plank_count()} "
        f"constraints={constraint_count}"
    )


main()
