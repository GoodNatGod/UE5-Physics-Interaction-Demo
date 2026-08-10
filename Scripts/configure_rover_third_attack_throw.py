import unreal


COMBAT_CONFIG_PATH = "/Game/Rover/Combat/DA_RoverCombatConfig"


THROW_SETTINGS = {
    "enable_third_attack_weapon_throw": True,
    "third_attack_throw_start_delay": 0.08,
    "third_attack_throw_outbound_duration": 0.35,
    "third_attack_throw_spin_duration": 0.55,
    "third_attack_throw_return_duration": 0.25,
    "third_attack_throw_target_forward_offset": 300.0,
    "third_attack_throw_target_lateral_offset": 0.0,
    "third_attack_throw_target_height_offset": 80.0,
    "third_attack_throw_anchor_rotation_offset": unreal.Rotator(
        roll=0.0, pitch=0.0, yaw=0.0
    ),
    "third_attack_throw_spin_axis": unreal.Vector(0.0, 1.0, 0.0),
    "third_attack_throw_spin_degrees_per_second": 1080.0,
    "third_attack_throw_collision_outbound": True,
    "third_attack_throw_collision_spinning": True,
    "third_attack_throw_collision_returning": True,
    "third_attack_throw_trace_radius": 18.0,
    "third_attack_throw_trace_sample_count": 9,
    "third_attack_throw_trace_substep_distance": 8.0,
    "third_attack_throw_max_trace_substeps": 12,
}


def main():
    config = unreal.load_asset(COMBAT_CONFIG_PATH)
    if not isinstance(config, unreal.RoverCombatConfig):
        raise RuntimeError(f"Missing Rover combat config: {COMBAT_CONFIG_PATH}")

    settings = config.get_editor_property("settings")
    attack_chain = list(settings.get_editor_property("light_attack_chain"))
    if len(attack_chain) < 3:
        raise RuntimeError(f"Expected at least three light attacks, got {len(attack_chain)}")

    attack_chain[2].set_editor_property("weapon_hand", unreal.RoverWeaponHand.RIGHT)
    for property_name, value in THROW_SETTINGS.items():
        settings.set_editor_property(property_name, value)
    settings.set_editor_property("light_attack_chain", attack_chain)
    config.set_editor_property("settings", settings)

    if not unreal.EditorAssetLibrary.save_loaded_asset(config, only_if_is_dirty=False):
        raise RuntimeError(f"Failed to save {COMBAT_CONFIG_PATH}")

    saved_settings = config.get_editor_property("settings")
    if not bool(saved_settings.get_editor_property("enable_third_attack_weapon_throw")):
        raise RuntimeError("Third-attack weapon throw was not saved as enabled")
    saved_chain = list(saved_settings.get_editor_property("light_attack_chain"))
    if "RIGHT" not in str(saved_chain[2].get_editor_property("weapon_hand")).upper():
        raise RuntimeError("Attack03 is not configured to release from the right hand")

    unreal.log(
        "ROVER_ATTACK03_THROW_CONFIGURED "
        "path=RightHand->ForwardFixedAnchor->LeftHand "
        "timing=0.08+0.35+0.55+0.25 "
        "offset=300,0,80 spin=1080deg/s trace=r18/samples9/step8/max12"
    )


main()
