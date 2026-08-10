import unreal


MAP_PATH = "/Game/PhysicsWorldDemo/Maps/L_PhysicsWorldDemo_Lumen"
CONFIG_PATH = (
    "/Game/PhysicsWorldDemo/Water/Config/"
    "DA_WorldWaterRippleConfig.DA_WorldWaterRippleConfig"
)
REGION_TAG = "PhysicsWorldWaterRippleRegion"
DEMO_REGION_TAG = "PhysicsWorldDemoDualLakeRegion"
LEGACY_PLANE_REGION_TAG = "PhysicsWorldDemoWaterPlaneRegion"
LAKE_LABELS = ("WaterBodyLake", "WaterBodyLake2")
ROVER_MESH_PATH = "/Game/Rover/Character/SK_Rover_Male.SK_Rover_Male"
ROVER_PHYSICS_ASSET_PATH = (
    "/Game/Rover/Character/PHYS_Rover_Male.PHYS_Rover_Male"
)


world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
if not world:
    raise RuntimeError(f"Unable to load demo map={MAP_PATH}")
config = unreal.load_asset(CONFIG_PATH)
if not config or config.get_class().get_name() != "WorldWaterRippleConfig":
    raise RuntimeError(f"Missing water ripple config={CONFIG_PATH}")
if not unreal.RoverEditorTestLibrary.configure_rover_water_advanced_physics_asset():
    raise RuntimeError("Unable to configure Rover WaterAdvanced Physics Asset")
for asset_path in (ROVER_PHYSICS_ASSET_PATH, ROVER_MESH_PATH):
    asset = unreal.load_asset(asset_path)
    if not asset or not unreal.EditorAssetLibrary.save_loaded_asset(
        asset, only_if_is_dirty=False
    ):
        raise RuntimeError(f"Unable to save Rover collider asset={asset_path}")

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = list(actor_subsystem.get_all_level_actors())
zones = [actor for actor in actors if actor.get_class().get_name() == "WaterZone"]
if len(zones) != 1:
    raise RuntimeError(f"Expected one existing WaterZone; found={len(zones)}")

lakes_by_label = {}
for label in LAKE_LABELS:
    matches = [
        actor
        for actor in actors
        if actor.get_class().get_name() == "WaterBodyLake"
        and actor.get_actor_label() == label
    ]
    if len(matches) != 1:
        raise RuntimeError(
            f"Expected one existing WaterBodyLake label={label}; found={len(matches)}"
        )
    lakes_by_label[label] = matches[0]
all_lakes = [
    actor for actor in actors if actor.get_class().get_name() == "WaterBodyLake"
]
if len(all_lakes) != len(LAKE_LABELS):
    raise RuntimeError(
        "The main map WaterBodyLake count changed; refusing automatic assembly: "
        f"found={len(all_lakes)}"
    )

all_regions = [
    actor
    for actor in actors
    if actor.get_class().get_name() == "WorldWaterRippleRegion"
]
regions = [
    actor
    for actor in all_regions
    if DEMO_REGION_TAG in {str(tag) for tag in actor.get_editor_property("tags")}
]
legacy_regions = [
    actor
    for actor in all_regions
    if LEGACY_PLANE_REGION_TAG
    in {str(tag) for tag in actor.get_editor_property("tags")}
    and actor not in regions
]
if len(regions) + len(legacy_regions) > len(LAKE_LABELS):
    raise RuntimeError(
        "Too many demo/legacy water Regions to bind safely: "
        f"dual={len(regions)} legacy={len(legacy_regions)}"
    )
regions.extend(legacy_regions)
while len(regions) < len(LAKE_LABELS):
    lake = lakes_by_label[LAKE_LABELS[len(regions)]]
    region = actor_subsystem.spawn_actor_from_class(
        unreal.WorldWaterRippleRegion,
        lake.get_actor_location(),
        unreal.Rotator(),
    )
    if not region:
        raise RuntimeError("Unable to spawn the second Lake ripple Region")
    regions.append(region)

for index, label in enumerate(LAKE_LABELS):
    lake = lakes_by_label[label]
    region = regions[index]
    region.set_actor_location(lake.get_actor_location(), False, False)
    region.set_actor_label(f"Physics World Lake Ripple Region - {label}")
    region.set_editor_property("water_ripple_config", config)
    region.set_editor_property("target_water_body", lake)
    region.set_editor_property("target_water_surface_actor", None)
    region.set_editor_property("fit_domain_to_target_surface_bounds", True)
    region.set_editor_property("use_water_advanced_shallow_water", True)
    tags = [
        tag
        for tag in region.get_editor_property("tags")
        if str(tag) != LEGACY_PLANE_REGION_TAG
    ]
    for tag_text in (REGION_TAG, DEMO_REGION_TAG, f"{DEMO_REGION_TAG}_{index + 1}"):
        tag = unreal.Name(tag_text)
        if tag not in tags:
            tags.append(tag)
    region.set_editor_property("tags", tags)

if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH):
    raise RuntimeError(f"Unable to save demo map={MAP_PATH}")

unreal.log(
    "PHYSICS_WORLD_DUAL_LAKE_CONFIG_OK "
    f"map={MAP_PATH} zone={zones[0].get_path_name()} "
    + " ".join(
        f"lake{index + 1}={lakes_by_label[label].get_path_name()} "
        f"region{index + 1}={regions[index].get_path_name()}"
        for index, label in enumerate(LAKE_LABELS)
    )
)
