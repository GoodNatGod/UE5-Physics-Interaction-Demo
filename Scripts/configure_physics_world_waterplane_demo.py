raise RuntimeError(
    "This destructive plane-replacement script is retired. "
    "Use ConfigurePhysicsWorldDualLakeDemo.ps1; existing WaterZone and "
    "WaterBodyLake actors must be preserved."
)

import os

import unreal


MAP_PATH = "/Game/PhysicsWorldDemo/Maps/L_PhysicsWorldDemo_Lumen"
CONFIG_PATH = (
    "/Game/PhysicsWorldDemo/Water/Config/"
    "DA_WorldWaterRippleConfig.DA_WorldWaterRippleConfig"
)
WATER_PLANE_BLUEPRINT_PATH = (
    "/Game/ModularLostRuinKit/Blueprint/Effects/BP_Waterplane"
)
REGION_TAG = "PhysicsWorldWaterRippleRegion"
DEMO_REGION_TAG = "PhysicsWorldDemoWaterPlaneRegion"
WATER_PLANE_LABEL = os.environ.get("ROVER_WATER_PLANE_LABEL", "BP_Waterplane2")


world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
if not world:
    raise RuntimeError(f"Unable to load demo map={MAP_PATH}")

config = unreal.load_asset(CONFIG_PATH)
if not config or config.get_class().get_name() != "WorldWaterRippleConfig":
    raise RuntimeError(f"Missing water ripple config={CONFIG_PATH}")
water_plane_class = unreal.EditorAssetLibrary.load_blueprint_class(
    WATER_PLANE_BLUEPRINT_PATH
)
if not water_plane_class:
    raise RuntimeError(f"Missing water plane Blueprint={WATER_PLANE_BLUEPRINT_PATH}")

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = list(actor_subsystem.get_all_level_actors())
obsolete_landscape_water_actors = [
    actor
    for actor in actors
    if actor.get_class().get_name() in ("WaterBodyLake", "WaterZone")
]
for obsolete_actor in obsolete_landscape_water_actors:
    if not actor_subsystem.destroy_actor(obsolete_actor):
        raise RuntimeError(
            f"Unable to remove obsolete Landscape Water actor={obsolete_actor.get_path_name()}"
        )
matching_planes = [
    actor
    for actor in actors
    if actor.get_class() == water_plane_class
    and actor.get_actor_label() == WATER_PLANE_LABEL
]
if len(matching_planes) != 1:
    available_labels = sorted(
        actor.get_actor_label()
        for actor in actors
        if actor.get_class() == water_plane_class
    )
    raise RuntimeError(
        f"Expected one BP_Waterplane label={WATER_PLANE_LABEL}; "
        f"found={len(matching_planes)} available={available_labels}"
    )
water_plane = matching_planes[0]

demo_regions = [
    actor
    for actor in actors
    if actor.get_class().get_name() == "WorldWaterRippleRegion"
    and DEMO_REGION_TAG in {str(tag) for tag in actor.get_editor_property("tags")}
]
if len(demo_regions) > 1:
    raise RuntimeError(f"Duplicate demo water-plane regions={len(demo_regions)}")
region = demo_regions[0] if demo_regions else actor_subsystem.spawn_actor_from_class(
    unreal.WorldWaterRippleRegion,
    water_plane.get_actor_location(),
    unreal.Rotator(),
)
if not region:
    raise RuntimeError("Unable to spawn WorldWaterRippleRegion in demo map")

region.set_actor_location(water_plane.get_actor_location(), False, False)
region.set_actor_label("Physics World BP Waterplane Interaction Region")
region.set_editor_property("water_ripple_config", config)
region.set_editor_property("target_water_surface_actor", water_plane)
region.set_editor_property("fit_domain_to_target_surface_bounds", True)
tags = list(region.get_editor_property("tags"))
for tag_text in (REGION_TAG, DEMO_REGION_TAG):
    tag = unreal.Name(tag_text)
    if tag not in tags:
        tags.append(tag)
region.set_editor_property("tags", tags)

if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH):
    raise RuntimeError(f"Unable to save demo map={MAP_PATH}")

unreal.log(
    "PHYSICS_WORLD_WATER_PLANE_DEMO_OK "
    f"map={MAP_PATH} plane={water_plane.get_path_name()} "
    f"plane_location={water_plane.get_actor_location()} "
    f"plane_scale={water_plane.get_actor_scale3d()} "
    f"region={region.get_path_name()} "
    f"removed_landscape_water_actors={len(obsolete_landscape_water_actors)}"
)
