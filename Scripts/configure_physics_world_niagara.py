import unreal


ROOT = "/Game/PhysicsWorldDemo"
CONFIG_PATH = f"{ROOT}/Config/DA_WorldInteractionConfig"
SYSTEM_PATHS = {
    "fireball_effect": f"{ROOT}/Niagara/NS_PW_Fireball",
    "explosion_effect": f"{ROOT}/Niagara/NS_PW_Explosion",
    "impact_effect": f"{ROOT}/Niagara/NS_PW_SurfaceImpact",
    "chaos_break_effect": f"{ROOT}/Niagara/NS_PW_ChaosBreak",
}


def load_system(setting_name):
    asset_path = SYSTEM_PATHS[setting_name]
    system = unreal.load_asset(asset_path)
    if not isinstance(system, unreal.NiagaraSystem):
        raise RuntimeError(f"Missing generated Niagara system: {asset_path}")
    return system


def main():
    if not unreal.RoverEditorTestLibrary.configure_physics_world_niagara_assets():
        raise RuntimeError("C++ Niagara asset configuration failed")

    systems = {name: load_system(name) for name in SYSTEM_PATHS}
    config = unreal.load_asset(CONFIG_PATH)
    if not isinstance(config, unreal.WorldInteractionConfig):
        raise RuntimeError(f"Missing World Interaction config: {CONFIG_PATH}")

    settings = config.get_editor_property("settings")
    settings.set_editor_property("fireball_effect", systems["fireball_effect"])
    settings.set_editor_property("fireball_effect_scale", 1.0)
    settings.set_editor_property("explosion_effect", systems["explosion_effect"])
    settings.set_editor_property("explosion_effect_scale", 1.0)
    settings.set_editor_property("chaos_break_effect", systems["chaos_break_effect"])
    settings.set_editor_property("chaos_break_effect_scale", 1.0)
    settings.set_editor_property("chaos_break_effect_min_speed", 40.0)
    settings.set_editor_property("max_chaos_break_effect_bursts_per_actor", 8)

    responses = list(settings.get_editor_property("surface_responses"))
    if not responses:
        raise RuntimeError("World Interaction config has no surface responses")
    for response in responses:
        response.set_editor_property("impact_effect", systems["impact_effect"])
        response.set_editor_property("impact_effect_scale", 1.0)
    settings.set_editor_property("surface_responses", responses)
    config.set_editor_property("settings", settings)

    for system in systems.values():
        unreal.EditorAssetLibrary.save_loaded_asset(system, only_if_is_dirty=False)
    unreal.EditorAssetLibrary.save_loaded_asset(config, only_if_is_dirty=False)

    diagnostics = {
        name: unreal.RoverEditorTestLibrary.dump_niagara_system(path)
        for name, path in SYSTEM_PATHS.items()
    }
    if "SingleLoopingParticle" not in diagnostics["fireball_effect"]:
        raise RuntimeError("Generated fireball is missing its core emitter")
    if "Fountain" not in diagnostics["fireball_effect"]:
        raise RuntimeError("Generated fireball is missing its trail emitter")
    if "OmnidirectionalBurst" not in diagnostics["explosion_effect"]:
        raise RuntimeError("Generated explosion is missing its spark emitter")
    if "DirectionalBurst" not in diagnostics["chaos_break_effect"]:
        raise RuntimeError("Generated Chaos break effect is missing its debris emitter")

    unreal.log(
        "PHYSICS_WORLD_NIAGARA_CONFIG_OK "
        + " ".join(
            f"{name}={system.get_path_name()}" for name, system in systems.items()
        )
    )


main()
