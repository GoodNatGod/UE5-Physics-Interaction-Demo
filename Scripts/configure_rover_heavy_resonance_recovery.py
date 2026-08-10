import os

import unreal


CONFIG_PATH = "/Game/Rover/Combat/DA_RoverCombatConfig"
MONTAGE_PATH = "/Game/Rover/Combat/Montages/AM_Rover_Attack_EX01"
BLEND_OUT_TIME = float(os.environ.get("ROVER_RESONANCE_BLEND_OUT_TIME", "0.25"))


def main():
    if BLEND_OUT_TIME < 0.05:
        raise RuntimeError("Heavy Resonance blend-out must be at least 0.05 seconds")

    config = unreal.load_asset(CONFIG_PATH)
    montage = unreal.load_asset(MONTAGE_PATH)
    if not isinstance(config, unreal.RoverCombatConfig):
        raise RuntimeError(f"Missing Rover combat config: {CONFIG_PATH}")
    if not isinstance(montage, unreal.AnimMontage):
        raise RuntimeError(f"Missing Heavy Resonance Montage: {MONTAGE_PATH}")

    settings = config.get_editor_property("settings")
    definition = settings.get_editor_property("heavy_resonance_definition")
    previous_definition_blend = float(
        definition.get_editor_property("montage_blend_out_time")
    )
    previous_play_rate = float(definition.get_editor_property("anim_play_rate"))
    definition.set_editor_property("montage_blend_out_time", BLEND_OUT_TIME)
    definition.set_editor_property(
        "montage_blend_out_trigger_time", BLEND_OUT_TIME
    )
    settings.set_editor_property("heavy_resonance_definition", definition)
    config.set_editor_property("settings", settings)

    blend_out = montage.get_editor_property("blend_out")
    blend_out.set_editor_property("blend_time", BLEND_OUT_TIME)
    montage.set_editor_property("blend_out", blend_out)
    montage.set_editor_property("blend_out_trigger_time", BLEND_OUT_TIME)

    if not unreal.EditorAssetLibrary.save_loaded_asset(
        config, only_if_is_dirty=False
    ):
        raise RuntimeError("Failed to save Heavy Resonance combat config")
    if not unreal.EditorAssetLibrary.save_loaded_asset(
        montage, only_if_is_dirty=False
    ):
        raise RuntimeError("Failed to save Heavy Resonance Montage")

    actual_blend_out = float(montage.get_default_blend_out_time())
    actual_trigger = float(montage.get_editor_property("blend_out_trigger_time"))
    if (
        abs(actual_blend_out - BLEND_OUT_TIME) > 0.001
        or abs(actual_trigger - BLEND_OUT_TIME) > 0.001
    ):
        raise RuntimeError(
            "Heavy Resonance Montage blend-out update did not persist: "
            f"blend={actual_blend_out:.3f}s trigger={actual_trigger:.3f}s"
        )

    unreal.log(
        "ROVER_HEAVY_RESONANCE_RECOVERY_OK "
        f"blend_out={previous_definition_blend:.3f}->{BLEND_OUT_TIME:.3f}s "
        f"play_rate_preserved={previous_play_rate:.2f} "
        "timeline=preserved"
    )


main()
