import unreal


MESH_PATH = "/Game/Rover/Character/SK_Rover_Male.SK_Rover_Male"
PHYSICS_ASSET_PATH = (
    "/Game/Rover/Character/PHYS_Rover_Male.PHYS_Rover_Male"
)


if not unreal.RoverEditorTestLibrary.configure_rover_water_advanced_physics_asset():
    raise RuntimeError("Unable to configure Rover WaterAdvanced Physics Asset")

mesh = unreal.load_asset(MESH_PATH)
physics_asset = unreal.load_asset(PHYSICS_ASSET_PATH)
if not mesh or not physics_asset:
    raise RuntimeError("Generated Rover Physics Asset could not be loaded")
if mesh.get_editor_property("physics_asset") != physics_asset:
    raise RuntimeError("Rover Skeletal Mesh does not reference the generated Physics Asset")

body_count = unreal.RoverEditorTestLibrary.get_physics_asset_body_count(
    PHYSICS_ASSET_PATH
)
if body_count <= 0:
    raise RuntimeError("Generated Rover Physics Asset has no collision bodies")

for asset in (physics_asset, mesh):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Unable to save asset={asset.get_path_name()}")

unreal.log(
    "ROVER_WATER_ADVANCED_COLLIDER_CONFIG_OK "
    f"mesh={mesh.get_path_name()} physics_asset={physics_asset.get_path_name()} "
    f"bodies={body_count}"
)
