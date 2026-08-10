import math

import unreal


ROOT = "/Game/PhysicsWorldDemo/Water"
CONFIG_PATH = f"{ROOT}/Config/DA_WorldWaterRippleConfig"
BASE_MATERIAL_PATH = f"{ROOT}/Materials/M_RoverWater"
WATER_MATERIAL_PATH = f"{ROOT}/Materials/MI_RoverWater_Lake"
SOLVE_MATERIAL_PATH = f"{ROOT}/Materials/M_RoverRippleSolve"
IMPULSE_MATERIAL_PATH = f"{ROOT}/Materials/M_RoverRippleImpulse"
STATE_A_PATH = f"{ROOT}/Simulation/RT_RoverRippleStateA"
STATE_B_PATH = f"{ROOT}/Simulation/RT_RoverRippleStateB"
SPLASH_PATH = f"{ROOT}/Niagara/NS_RoverWaterSplash"
MAP_PATH = f"{ROOT}/Maps/L_WaterP0"
REGION_TAG = "PhysicsWorldWaterRippleRegion"
WATER_PLANE_TAG = "PhysicsWorldInteractiveWaterPlane"
EXPECTED_RESOLUTION = 512
EXPECTED_WORLD_SIZE_CM = 2000.0
EXPECTED_FIXED_STEP_SECONDS = 1.0 / 60.0
SUCCESS_MARKER = "PHYSICS_WORLD_WATER_ASSETS_OK"


def object_path(value):
    return value.get_path_name() if value else "None"


def load_required(path, expected_class_name):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError(f"Missing required asset: {path}")
    class_name = asset.get_class().get_name()
    if class_name != expected_class_name:
        raise RuntimeError(
            f"Unexpected class for {path}: {class_name}; expected {expected_class_name}"
        )
    return asset


def validate_settings(config):
    settings = config.get_editor_property("settings")
    if not settings.get_editor_property("enabled"):
        raise RuntimeError("Water ripple config is disabled")

    resolution = int(settings.get_editor_property("render_target_resolution"))
    fixed_step = float(settings.get_editor_property("fixed_step_seconds"))
    max_substeps = int(settings.get_editor_property("max_substeps_per_frame"))
    wave_speed = float(settings.get_editor_property("wave_speed"))
    damping = float(settings.get_editor_property("damping_per_second"))
    edge_width = float(settings.get_editor_property("edge_damping_width"))
    max_queued = int(settings.get_editor_property("max_queued_impulses"))
    max_per_step = int(settings.get_editor_property("max_impulses_per_step"))

    if resolution != EXPECTED_RESOLUTION:
        raise RuntimeError(
            f"Unexpected ripple resolution={resolution}; expected={EXPECTED_RESOLUTION}"
        )
    world_size = settings.get_editor_property("world_size")
    if (
        abs(float(world_size.x) - EXPECTED_WORLD_SIZE_CM) > 0.01
        or abs(float(world_size.y) - EXPECTED_WORLD_SIZE_CM) > 0.01
    ):
        raise RuntimeError(
            f"Unexpected ripple world size={world_size.x:.2f}x{world_size.y:.2f}cm; "
            f"expected={EXPECTED_WORLD_SIZE_CM:.2f}cm"
        )
    if not math.isclose(fixed_step, EXPECTED_FIXED_STEP_SECONDS, abs_tol=1.0e-5):
        raise RuntimeError(
            f"Ripple solver is not fixed at 60Hz: step={fixed_step:.8f}s"
        )
    if max_substeps < 1 or max_substeps > 8:
        raise RuntimeError(f"Invalid max_substeps={max_substeps}")
    if wave_speed <= 0.0 or damping < 0.0:
        raise RuntimeError(
            f"Invalid wave solver settings speed={wave_speed:.3f} damping={damping:.3f}"
        )
    texel_x = float(world_size.x) / resolution
    texel_y = float(world_size.y) / resolution
    coefficient_sum = (wave_speed * fixed_step / texel_x) ** 2 + (
        wave_speed * fixed_step / texel_y
    ) ** 2
    if coefficient_sum > 0.99:
        raise RuntimeError(
            "Configured wave solver exceeds the explicit stability limit: "
            f"lambda_squared_sum={coefficient_sum:.4f}"
        )
    if edge_width <= 0.0 or edge_width >= 0.5:
        raise RuntimeError(f"Invalid normalized edge damping width={edge_width:.3f}")
    if max_per_step < 1 or max_queued < max_per_step:
        raise RuntimeError(
            f"Invalid impulse budget queued={max_queued} per_step={max_per_step}"
        )

    expected_materials = {
        "simulation_material": SOLVE_MATERIAL_PATH,
        "water_surface_material": WATER_MATERIAL_PATH,
    }
    for property_name, expected_path in expected_materials.items():
        actual = settings.get_editor_property(property_name)
        if object_path(actual) != f"{expected_path}.{expected_path.rsplit('/', 1)[-1]}":
            raise RuntimeError(
                f"Unexpected {property_name}={object_path(actual)}; expected={expected_path}"
            )

    return settings


def validate_render_target(path):
    render_target = load_required(path, "TextureRenderTarget2D")
    size_x = int(render_target.get_editor_property("size_x"))
    size_y = int(render_target.get_editor_property("size_y"))
    render_format = render_target.get_editor_property("render_target_format")
    if size_x != EXPECTED_RESOLUTION or size_y != EXPECTED_RESOLUTION:
        raise RuntimeError(
            f"Unexpected render target size at {path}: {size_x}x{size_y}"
        )
    if "RG16F" not in str(render_format).upper():
        raise RuntimeError(
            f"Render target must use RG16F history packing at {path}: {render_format}"
        )
    return render_target


def validate_map(config):
    world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
    if not world:
        raise RuntimeError(f"Unable to load water validation map: {MAP_PATH}")

    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = list(actor_subsystem.get_all_level_actors())
    water_zones = [actor for actor in actors if actor.get_class().get_name() == "WaterZone"]
    lakes = [actor for actor in actors if actor.get_class().get_name() == "WaterBodyLake"]
    water_planes = [
        actor
        for actor in actors
        if WATER_PLANE_TAG in {str(tag) for tag in actor.get_editor_property("tags")}
    ]
    regions = [
        actor
        for actor in actors
        if actor.get_class().get_name() == "WorldWaterRippleRegion"
    ]
    if water_zones or lakes:
        raise RuntimeError(
            "Plane water map must not contain Landscape Water actors: "
            f"zones={len(water_zones)} lakes={len(lakes)}"
        )
    if len(water_planes) != 1:
        raise RuntimeError(
            f"Expected one tagged BP_Waterplane; found={len(water_planes)}"
        )
    if len(regions) != 1:
        raise RuntimeError(f"Expected one WorldWaterRippleRegion; found={len(regions)}")

    region = regions[0]
    tags = {str(tag) for tag in region.get_editor_property("tags")}
    if REGION_TAG not in tags:
        raise RuntimeError(f"Water ripple region is missing tag={REGION_TAG}")
    configured = region.get_editor_property("water_ripple_config")
    if configured != config:
        raise RuntimeError(
            f"Water ripple region has unexpected config={object_path(configured)}"
        )

    target_surface = region.get_editor_property("target_water_surface_actor")
    if target_surface != water_planes[0]:
        raise RuntimeError(
            f"Ripple region target={object_path(target_surface)}; "
            f"expected={object_path(water_planes[0])}"
        )
    if not region.get_editor_property("fit_domain_to_target_surface_bounds"):
        raise RuntimeError("Ripple region is not fitting its target plane bounds")
    mesh_components = water_planes[0].get_components_by_class(
        unreal.StaticMeshComponent
    )
    if len(mesh_components) != 1:
        raise RuntimeError(
            f"BP_Waterplane must expose one StaticMeshComponent; found={len(mesh_components)}"
        )
    static_mesh = mesh_components[0].get_editor_property("static_mesh")
    if object_path(static_mesh) != "/Engine/BasicShapes/Plane.Plane":
        raise RuntimeError(
            f"Unexpected BP_Waterplane mesh={object_path(static_mesh)}"
        )

    return len(actors)


def main():
    config = load_required(CONFIG_PATH, "WorldWaterRippleConfig")
    validate_settings(config)
    base_material = load_required(BASE_MATERIAL_PATH, "Material")
    water_material = load_required(WATER_MATERIAL_PATH, "MaterialInstanceConstant")
    solve_material = load_required(SOLVE_MATERIAL_PATH, "Material")
    load_required(IMPULSE_MATERIAL_PATH, "Material")
    load_required(SPLASH_PATH, "NiagaraSystem")
    validate_render_target(STATE_A_PATH)
    validate_render_target(STATE_B_PATH)

    if water_material.get_editor_property("parent") != base_material:
        raise RuntimeError("MI_RoverWater_Lake does not use M_RoverWater as its parent")
    if not unreal.MaterialEditingLibrary.has_material_usage(
        base_material, unreal.MaterialUsage.MATUSAGE_WATER
    ):
        raise RuntimeError("M_RoverWater is not marked Used with Water")
    if solve_material.get_editor_property("material_domain") != unreal.MaterialDomain.MD_SURFACE:
        raise RuntimeError("Ripple solve material must use the Surface domain")

    actor_count = validate_map(config)
    unreal.log(
        f"{SUCCESS_MARKER} map={MAP_PATH} actors={actor_count} "
        f"resolution={EXPECTED_RESOLUTION} format=RG16F "
        f"fixed_step={EXPECTED_FIXED_STEP_SECONDS:.6f}s"
    )


main()
