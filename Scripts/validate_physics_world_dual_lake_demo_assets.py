import unreal


MAP_PATH = "/Game/PhysicsWorldDemo/Maps/L_PhysicsWorldDemo_Lumen"
CONFIG_PATH = (
    "/Game/PhysicsWorldDemo/Water/Config/"
    "DA_WorldWaterRippleConfig.DA_WorldWaterRippleConfig"
)
DEMO_REGION_TAG = "PhysicsWorldDemoDualLakeRegion"
LAKE_LABELS = ("WaterBodyLake", "WaterBodyLake2")
ROVER_MESH_PATH = "/Game/Rover/Character/SK_Rover_Male.SK_Rover_Male"
ROVER_PHYSICS_ASSET_PATH = (
    "/Game/Rover/Character/PHYS_Rover_Male.PHYS_Rover_Male"
)


world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
if not world:
    raise RuntimeError(f"Unable to load demo map={MAP_PATH}")
config = unreal.load_asset(CONFIG_PATH)
if not config:
    raise RuntimeError(f"Missing water config={CONFIG_PATH}")
settings = config.get_editor_property("settings")
required_character_sources = (
    "accept_movement",
    "accept_jump",
    "accept_landing",
)
disabled_character_sources = [
    name
    for name in required_character_sources
    if not settings.get_editor_property(name)
]
if disabled_character_sources:
    raise RuntimeError(
        "Water character interaction sources are disabled: "
        f"{disabled_character_sources}"
    )

rover_mesh = unreal.load_asset(ROVER_MESH_PATH)
rover_physics_asset = unreal.load_asset(ROVER_PHYSICS_ASSET_PATH)
if not rover_mesh or not rover_physics_asset:
    raise RuntimeError("Missing Rover WaterAdvanced Physics Asset assembly")
if rover_mesh.get_editor_property("physics_asset") != rover_physics_asset:
    raise RuntimeError("Rover Skeletal Mesh does not use PHYS_Rover_Male")
rover_physics_body_count = (
    unreal.RoverEditorTestLibrary.get_physics_asset_body_count(
        ROVER_PHYSICS_ASSET_PATH
    )
)
if rover_physics_body_count <= 0:
    raise RuntimeError("Rover WaterAdvanced Physics Asset has no collision bodies")

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = list(actor_subsystem.get_all_level_actors())
zones = [actor for actor in actors if actor.get_class().get_name() == "WaterZone"]
lakes = [
    actor for actor in actors if actor.get_class().get_name() == "WaterBodyLake"
]
regions = [
    actor
    for actor in actors
    if actor.get_class().get_name() == "WorldWaterRippleRegion"
    and DEMO_REGION_TAG in {str(tag) for tag in actor.get_editor_property("tags")}
]
if len(zones) != 1 or len(lakes) != 2 or len(regions) != 2:
    raise RuntimeError(
        "Expected one WaterZone, two WaterBodyLake actors, and two dual-Lake Regions; "
        f"zones={len(zones)} lakes={len(lakes)} regions={len(regions)}"
    )
lakes_by_label = {lake.get_actor_label(): lake for lake in lakes}
if set(lakes_by_label) != set(LAKE_LABELS):
    raise RuntimeError(f"Unexpected Lake labels={sorted(lakes_by_label)}")

targets = []
for region in regions:
    target = region.get_editor_property("target_water_body")
    targets.append(target)
    if target not in lakes:
        raise RuntimeError(
            f"Region target is not one of the two existing Lakes: {target}"
        )
    if region.get_editor_property("target_water_surface_actor") is not None:
        raise RuntimeError("Dual-Lake Region still targets a StaticMesh water plane")
    if region.get_editor_property("water_ripple_config") != config:
        raise RuntimeError("Dual-Lake Region uses an unexpected ripple config")
    if not region.get_editor_property("fit_domain_to_target_surface_bounds"):
        raise RuntimeError("Dual-Lake Region does not fit its target Lake bounds")
    if not region.get_editor_property("use_water_advanced_shallow_water"):
        raise RuntimeError("Dual-Lake Region is not using WaterAdvanced shallow water")
    delta = region.get_actor_location() - target.get_actor_location()
    if delta.length() > 0.1:
        raise RuntimeError(
            f"Region is not aligned with target Lake: region={region.get_path_name()}"
        )
    spline = target.get_component_by_class(unreal.SplineComponent)
    if not spline or spline.get_number_of_spline_points() < 3:
        raise RuntimeError(
            f"Recovered Lake spline is incomplete: lake={target.get_path_name()}"
        )
if len(set(targets)) != 2:
    raise RuntimeError("Both Regions target the same Lake")

unreal.log(
    "PHYSICS_WORLD_DUAL_LAKE_ASSETS_OK "
    f"map={MAP_PATH} zone={zones[0].get_path_name()} "
    "character_sources=movement/jump/landing "
    f"rover_physics_bodies={rover_physics_body_count} "
    + " ".join(
        f"{label}={lakes_by_label[label].get_actor_location()}"
        for label in LAKE_LABELS
    )
)
