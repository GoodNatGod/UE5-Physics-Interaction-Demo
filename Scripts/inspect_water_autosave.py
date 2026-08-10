import os

import unreal


AUTOSAVE_PATH = os.path.abspath(
    os.path.join(
        unreal.Paths.project_saved_dir(),
        "Autosaves",
        "Game",
        "PhysicsWorldDemo",
        "Maps",
        "L_PhysicsWorldDemo_Lumen_Auto1.umap",
    )
)


world = unreal.EditorLoadingAndSavingUtils.load_map(AUTOSAVE_PATH)
if not world:
    raise RuntimeError(f"Unable to load autosave={AUTOSAVE_PATH}")
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
methods = [
    name
    for name in dir(actor_subsystem)
    if any(token in name.lower() for token in ("duplic", "copy", "paste"))
]
unreal.log(f"WATER_AUTOSAVE_ACTOR_METHODS methods={methods}")
actors = list(actor_subsystem.get_all_level_actors())
water_actors = [
    actor
    for actor in actors
    if actor.get_class().get_name() in ("WaterBodyLake", "WaterZone")
]
for actor in water_actors:
    unreal.log(
        "WATER_AUTOSAVE_ACTOR "
        f"label={actor.get_actor_label()} class={actor.get_class().get_path_name()} "
        f"location={actor.get_actor_location()} rotation={actor.get_actor_rotation()} "
        f"scale={actor.get_actor_scale3d()} path={actor.get_path_name()}"
    )
unreal.log(
    f"WATER_AUTOSAVE_INSPECT_OK path={AUTOSAVE_PATH} actors={len(water_actors)}"
)
