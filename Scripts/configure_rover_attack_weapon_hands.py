import unreal


COMBAT_CONFIG_PATH = "/Game/Rover/Combat/DA_RoverCombatConfig"

ATTACK_PRESERVED_PROPERTIES = (
    "montage",
    "anim_play_rate",
    "montage_blend_in_time",
    "montage_blend_out_time",
    "montage_blend_out_trigger_time",
    "combo_window_start_normalized",
    "damage",
    "poise_damage",
    "environment_impulse_strength",
    "trace_radius",
    "trace_sample_count",
    "trace_substep_distance",
    "max_trace_substeps",
    "advance_distance",
    "advance_duration",
)

SETTINGS_PRESERVED_PROPERTIES = (
    "attack_input_buffer_duration",
    "allow_directional_light_attack",
    "enable_third_attack_weapon_throw",
    "third_attack_throw_start_delay",
    "third_attack_throw_outbound_duration",
    "third_attack_throw_spin_duration",
    "third_attack_throw_return_duration",
    "third_attack_throw_target_forward_offset",
    "third_attack_throw_target_lateral_offset",
    "third_attack_throw_target_height_offset",
    "third_attack_throw_anchor_rotation_offset",
    "third_attack_throw_spin_axis",
    "third_attack_throw_spin_degrees_per_second",
    "third_attack_throw_collision_outbound",
    "third_attack_throw_collision_spinning",
    "third_attack_throw_collision_returning",
    "third_attack_throw_trace_radius",
    "third_attack_throw_trace_sample_count",
    "third_attack_throw_trace_substep_distance",
    "third_attack_throw_max_trace_substeps",
    "combo_reset_duration",
    "attack_pending_timeout",
    "attack_active_timeout",
    "hit_reaction_timeout",
    "light_hit_left_montage",
    "light_hit_right_montage",
    "weapon_mesh",
    "character_weapon_socket",
    "weapon_trace_base_socket",
    "weapon_trace_tip_socket",
    "scabbard_bone",
    "weapon_relative_location",
    "weapon_relative_rotation",
    "weapon_relative_scale",
    "left_hand_weapon_socket",
    "left_hand_weapon_relative_location",
    "left_hand_weapon_relative_rotation",
    "left_hand_weapon_relative_scale",
)


def freeze(value):
    if hasattr(value, "x") and hasattr(value, "y") and hasattr(value, "z"):
        return (float(value.x), float(value.y), float(value.z))
    if hasattr(value, "pitch") and hasattr(value, "yaw") and hasattr(value, "roll"):
        return (float(value.pitch), float(value.yaw), float(value.roll))
    if hasattr(value, "get_path_name"):
        return value.get_path_name()
    return str(value)


def preserved_snapshot(settings, attack_chain):
    return (
        tuple(
            (name, freeze(settings.get_editor_property(name)))
            for name in SETTINGS_PRESERVED_PROPERTIES
        ),
        tuple(
            tuple(
                (name, freeze(definition.get_editor_property(name)))
                for name in ATTACK_PRESERVED_PROPERTIES
            )
            for definition in attack_chain
        ),
    )


def main():
    config = unreal.load_asset(COMBAT_CONFIG_PATH)
    if not isinstance(config, unreal.RoverCombatConfig):
        raise RuntimeError(f"Missing Rover combat config: {COMBAT_CONFIG_PATH}")

    settings = config.get_editor_property("settings")
    attack_chain = list(settings.get_editor_property("light_attack_chain"))
    if len(attack_chain) < 3:
        raise RuntimeError(f"Expected at least three light attacks, got {len(attack_chain)}")

    before = preserved_snapshot(settings, attack_chain)
    expected_hands = (
        unreal.RoverWeaponHand.LEFT,
        unreal.RoverWeaponHand.RIGHT,
        unreal.RoverWeaponHand.RIGHT,
    )
    for definition, weapon_hand in zip(attack_chain, expected_hands):
        definition.set_editor_property("weapon_hand", weapon_hand)

    settings.set_editor_property("left_hand_weapon_socket", "Bip001LHand")
    settings.set_editor_property("light_attack_chain", attack_chain)
    config.set_editor_property("settings", settings)

    updated_settings = config.get_editor_property("settings")
    updated_chain = list(updated_settings.get_editor_property("light_attack_chain"))
    if preserved_snapshot(updated_settings, updated_chain) != before:
        raise RuntimeError("Weapon-hand migration changed an existing combat tuning value")
    actual_hands = tuple(
        str(definition.get_editor_property("weapon_hand")).upper()
        for definition in updated_chain
    )
    if not (
        "LEFT" in actual_hands[0]
        and "RIGHT" in actual_hands[1]
        and "RIGHT" in actual_hands[2]
    ):
        raise RuntimeError(f"Unexpected attack weapon hands: {actual_hands}")

    if not unreal.EditorAssetLibrary.save_loaded_asset(
        config, only_if_is_dirty=False
    ):
        raise RuntimeError(f"Failed to save {COMBAT_CONFIG_PATH}")

    damages = tuple(
        definition.get_editor_property("damage") for definition in updated_chain
    )
    combo_reset = updated_settings.get_editor_property("combo_reset_duration")
    unreal.log(
        "ROVER_ATTACK_WEAPON_HANDS_CONFIGURED "
        f"hands=Left,Right,Right left_socket=Bip001LHand "
        f"damage={damages} combo_reset={combo_reset:.3f} preserved=true"
    )


main()
