import os

import unreal


MAP_PATH = "/Game/PhysicsWorldDemo/Maps/L_PhysicsWorldDemo_Lumen"
CONFIG_PATH = (
    "/Game/PhysicsWorldDemo/Water/Config/"
    "DA_WorldWaterRippleConfig.DA_WorldWaterRippleConfig"
)
DEMO_REGION_TAG = "PhysicsWorldDemoWaterPlaneRegion"
WATER_PLANE_LABEL = os.environ.get("ROVER_WATER_PLANE_LABEL", "BP_Waterplane2")


world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
if not world:
    raise RuntimeError(f"Unable to load demo map={MAP_PATH}")
config = unreal.load_asset(CONFIG_PATH)
if not config:
    raise RuntimeError(f"Missing water config={CONFIG_PATH}")

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = list(actor_subsystem.get_all_level_actors())
water_landscape_actors = [
    actor
    for actor in actors
    if actor.get_class().get_name() in ("WaterBodyLake", "WaterZone")
]
if water_landscape_actors:
    raise RuntimeError(
        "Demo map unexpectedly contains Landscape Water actors: "
        + ",".join(actor.get_path_name() for actor in water_landscape_actors)
    )

planes = [actor for actor in actors if actor.get_actor_label() == WATER_PLANE_LABEL]
if len(planes) != 1 or planes[0].get_class().get_name() != "BP_Waterplane_C":
    raise RuntimeError(
        f"Expected one BP_Waterplane label={WATER_PLANE_LABEL}; found={len(planes)}"
    )
plane = planes[0]
regions = [
    actor
    for actor in actors
    if actor.get_class().get_name() == "WorldWaterRippleRegion"
    and DEMO_REGION_TAG in {str(tag) for tag in actor.get_editor_property("tags")}
]
if len(regions) != 1:
    raise RuntimeError(f"Expected one demo plane ripple region; found={len(regions)}")
region = regions[0]
if region.get_editor_property("target_water_surface_actor") != plane:
    raise RuntimeError("Demo ripple region is not bound to the requested BP_Waterplane")
if region.get_editor_property("water_ripple_config") != config:
    raise RuntimeError("Demo ripple region does not use DA_WorldWaterRippleConfig")
if not region.get_editor_property("fit_domain_to_target_surface_bounds"):
    raise RuntimeError("Demo ripple region does not fit the BP_Waterplane bounds")

mesh_components = plane.get_components_by_class(unreal.StaticMeshComponent)
if len(mesh_components) != 1:
    raise RuntimeError(
        f"Target BP_Waterplane StaticMeshComponent count={len(mesh_components)}"
    )
mesh = mesh_components[0].get_editor_property("static_mesh")
if not mesh or mesh.get_path_name() != "/Engine/BasicShapes/Plane.Plane":
    raise RuntimeError(f"Target water mesh is not Engine Plane: {mesh}")

unreal.log(
    "PHYSICS_WORLD_WATER_PLANE_DEMO_ASSETS_OK "
    f"map={MAP_PATH} plane={plane.get_path_name()} "
    f"location={plane.get_actor_location()} scale={plane.get_actor_scale3d()} "
    f"region={region.get_path_name()} landscape_water_actors=0"
)
