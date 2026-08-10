import os
from pathlib import Path

import unreal


ROVER_MESH_PATH = "/Game/Rover/Character/SK_Rover_Male"
COMBAT_ANIMATION_ROOT = "/Game/Rover/Combat/Animations"
WEAPON_ROOT = "/Game/Rover/Weapons/R2Sword001"
WEAPON_MESH_PATH = f"{WEAPON_ROOT}/SK_R2Sword001"
WEAPON_SKELETON_PATH = f"{WEAPON_ROOT}/SKEL_R2Sword001"
WEAPON_TEXTURE_ROOT = f"{WEAPON_ROOT}/Textures"
WEAPON_BASE_COLOR_PATH = (
    f"{WEAPON_TEXTURE_ROOT}/T_R2Sword001Md20001_D"
)
WEAPON_NORMAL_PATH = f"{WEAPON_TEXTURE_ROOT}/T_R2Sword001Md20001_N"
WEAPON_MATERIAL_PATH = f"{WEAPON_ROOT}/M_R2Sword001"
DEFAULT_MAP_PATH = "/Game/PhysicsWorldDemo/Maps/L_PhysicsWorldDemo_Lumen"
ENEMY_CLASS_PATH = "/Script/RoverReplica.RoverEnemyCharacter"
TRAINING_ENEMY_TAG = "RoverP0TrainingEnemy"

COMBAT_ANIMATIONS = {
    "Attack01.fbx": "Attack01",
    "Attack02.fbx": "Attack02",
    "Attack03.fbx": "Attack03",
    "Behit_S_L.fbx": "Behit_S_L",
    "Behit_S_R.fbx": "Behit_S_R",
}


def require_path(environment_name: str, directory: bool) -> Path:
    value = os.environ.get(environment_name)
    if not value:
        raise RuntimeError(f"Environment variable {environment_name} is required.")
    path = Path(value).resolve()
    valid = path.is_dir() if directory else path.is_file()
    if not valid:
        kind = "directory" if directory else "file"
        raise RuntimeError(f"{environment_name} does not point to a {kind}: {path}")
    return path


def make_task(
    filename: Path,
    destination_path: str,
    destination_name: str,
    options: unreal.FbxImportUI,
) -> unreal.AssetImportTask:
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(filename))
    task.set_editor_property("destination_path", destination_path)
    task.set_editor_property("destination_name", destination_name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("replace_existing_settings", True)
    task.set_editor_property("save", True)
    task.set_editor_property("options", options)
    return task


def animation_options(skeleton: unreal.Skeleton) -> unreal.FbxImportUI:
    options = unreal.FbxImportUI()
    options.set_editor_property("import_as_skeletal", True)
    options.set_editor_property("import_mesh", False)
    options.set_editor_property("import_animations", True)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)
    options.set_editor_property("skeleton", skeleton)
    animation_import_data = options.get_editor_property("anim_sequence_import_data")
    animation_import_data.set_editor_property("import_meshes_in_bone_hierarchy", True)
    animation_import_data.set_editor_property("import_bone_tracks", True)
    return options


def weapon_options() -> unreal.FbxImportUI:
    options = unreal.FbxImportUI()
    options.set_editor_property("import_as_skeletal", True)
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_animations", False)
    options.set_editor_property("import_materials", True)
    options.set_editor_property("import_textures", True)
    options.set_editor_property("create_physics_asset", False)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    return options


def import_texture(source_path: Path, destination_name: str) -> unreal.Texture2D:
    asset_path = f"{WEAPON_TEXTURE_ROOT}/{destination_name}"
    existing = unreal.load_asset(asset_path)
    if isinstance(existing, unreal.Texture2D):
        return existing

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(source_path))
    task.set_editor_property("destination_path", WEAPON_TEXTURE_ROOT)
    task.set_editor_property("destination_name", destination_name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.load_asset(asset_path)
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError(f"Texture import failed: {source_path}")
    return texture


def configure_weapon_material(
    weapon_mesh: unreal.SkeletalMesh, weapon_fbx: Path
) -> None:
    texture_root = weapon_fbx.parent / "TEX"
    base_color_source = texture_root / "T_R2Sword001Md20001_D.png"
    normal_source = texture_root / "T_R2Sword001Md20001_N.png"
    if not base_color_source.is_file() or not normal_source.is_file():
        raise RuntimeError(f"Weapon TEX directory is incomplete: {texture_root}")

    unreal.EditorAssetLibrary.make_directory(WEAPON_TEXTURE_ROOT)
    base_color = import_texture(base_color_source, "T_R2Sword001Md20001_D")
    normal = import_texture(normal_source, "T_R2Sword001Md20001_N")
    base_color.set_editor_property("srgb", True)
    normal.set_editor_property("srgb", False)
    normal.set_editor_property(
        "compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP
    )

    material = unreal.load_asset(WEAPON_MATERIAL_PATH)
    if not isinstance(material, unreal.Material):
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "M_R2Sword001",
            WEAPON_ROOT,
            unreal.Material,
            unreal.MaterialFactoryNew(),
        )
    if not isinstance(material, unreal.Material):
        raise RuntimeError(f"Could not create weapon material: {WEAPON_MATERIAL_PATH}")

    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    base_expression = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureSample, -320, -80
    )
    normal_expression = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureSample, -320, 100
    )
    base_expression.set_editor_property("texture", base_color)
    normal_expression.set_editor_property("texture", normal)
    normal_expression.set_editor_property(
        "sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
    )
    unreal.MaterialEditingLibrary.connect_material_property(
        base_expression, "RGB", unreal.MaterialProperty.MP_BASE_COLOR
    )
    unreal.MaterialEditingLibrary.connect_material_property(
        normal_expression, "RGB", unreal.MaterialProperty.MP_NORMAL
    )
    skeletal_usage = unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH
    unreal.MaterialEditingLibrary.set_base_material_usage(
        material, skeletal_usage, True
    )
    if not unreal.MaterialEditingLibrary.has_material_usage(
        material, skeletal_usage
    ):
        raise RuntimeError(
            f"Could not enable Skeletal Mesh usage: {WEAPON_MATERIAL_PATH}"
        )
    unreal.MaterialEditingLibrary.recompile_material(material)

    skeletal_materials = list(weapon_mesh.get_editor_property("materials"))
    if not skeletal_materials:
        raise RuntimeError("Imported weapon mesh has no material slot.")
    for skeletal_material in skeletal_materials:
        skeletal_material.set_editor_property("material_interface", material)
    weapon_mesh.set_editor_property("materials", skeletal_materials)
    unreal.EditorAssetLibrary.save_loaded_asset(base_color, False)
    unreal.EditorAssetLibrary.save_loaded_asset(normal, False)
    unreal.EditorAssetLibrary.save_loaded_asset(material, False)
    unreal.EditorAssetLibrary.save_loaded_asset(weapon_mesh, False)
    unreal.log(
        f"ROVER_COMBAT_P0_WEAPON_MATERIAL mesh={weapon_mesh.get_path_name()} "
        f"material={material.get_path_name()} base={base_color.get_path_name()} "
        f"normal={normal.get_path_name()}"
    )


def import_combat_animations(
    source_root: Path, skeleton: unreal.Skeleton
) -> dict[str, unreal.AnimSequence]:
    tasks = []
    for filename, asset_name in COMBAT_ANIMATIONS.items():
        source_path = source_root / filename
        if not source_path.is_file():
            raise RuntimeError(f"Missing required combat FBX: {source_path}")
        tasks.append(
            make_task(
                source_path,
                COMBAT_ANIMATION_ROOT,
                asset_name,
                animation_options(skeleton),
            )
        )

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    imported = {}
    for asset_name in COMBAT_ANIMATIONS.values():
        asset = unreal.load_asset(f"{COMBAT_ANIMATION_ROOT}/{asset_name}")
        if not isinstance(asset, unreal.AnimSequence):
            raise RuntimeError(f"Combat animation import failed: {asset_name}")
        imported[asset_name] = asset
    return imported


def import_weapon(weapon_fbx: Path) -> unreal.SkeletalMesh:
    existing = unreal.load_asset(WEAPON_MESH_PATH)
    if isinstance(existing, unreal.SkeletalMesh):
        unreal.log(f"Using existing skeletal weapon: {WEAPON_MESH_PATH}")
        return existing

    weapon_mesh = None
    imported_paths = unreal.EditorAssetLibrary.list_assets(
        WEAPON_ROOT, recursive=False, include_folder=False
    )
    for asset_path in imported_paths:
        asset = unreal.load_asset(asset_path)
        asset_name = asset.get_name() if asset else ""
        if isinstance(asset, unreal.SkeletalMesh) and "Scabbard" not in asset_name:
            weapon_mesh = asset
            break

    if not isinstance(weapon_mesh, unreal.SkeletalMesh):
        task = make_task(weapon_fbx, WEAPON_ROOT, "SK_R2Sword001", weapon_options())
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        for asset_path in task.get_editor_property("imported_object_paths"):
            asset = unreal.load_asset(asset_path)
            asset_name = asset.get_name() if asset else ""
            if isinstance(asset, unreal.SkeletalMesh) and "Scabbard" not in asset_name:
                weapon_mesh = asset
                break

    if not isinstance(weapon_mesh, unreal.SkeletalMesh):
        raise RuntimeError("Weapon import did not produce a sword Skeletal Mesh.")

    weapon_skeleton = weapon_mesh.get_editor_property("skeleton")
    if not isinstance(weapon_skeleton, unreal.Skeleton):
        raise RuntimeError("Imported weapon has no independent Skeleton.")
    current_skeleton_path = weapon_skeleton.get_path_name().split(".", 1)[0]
    if current_skeleton_path != WEAPON_SKELETON_PATH:
        if unreal.EditorAssetLibrary.does_asset_exist(WEAPON_SKELETON_PATH):
            raise RuntimeError(
                f"Cannot rename weapon Skeleton because target exists: {WEAPON_SKELETON_PATH}"
            )
        if not unreal.EditorAssetLibrary.rename_asset(
            current_skeleton_path, WEAPON_SKELETON_PATH
        ):
            raise RuntimeError(
                f"Failed to rename weapon Skeleton {current_skeleton_path} "
                f"to {WEAPON_SKELETON_PATH}."
            )

    current_mesh_path = weapon_mesh.get_path_name().split(".", 1)[0]
    if current_mesh_path != WEAPON_MESH_PATH:
        if unreal.EditorAssetLibrary.does_asset_exist(WEAPON_MESH_PATH):
            raise RuntimeError(
                f"Cannot rename weapon mesh because target exists: {WEAPON_MESH_PATH}"
            )
        if not unreal.EditorAssetLibrary.rename_asset(
            current_mesh_path, WEAPON_MESH_PATH
        ):
            raise RuntimeError(
                f"Failed to rename weapon mesh {current_mesh_path} "
                f"to {WEAPON_MESH_PATH}."
            )

    normalized_mesh = unreal.load_asset(WEAPON_MESH_PATH)
    if not isinstance(normalized_mesh, unreal.SkeletalMesh):
        raise RuntimeError(f"Could not load normalized weapon mesh: {WEAPON_MESH_PATH}")
    return normalized_mesh


def unpack_editor_result(result) -> tuple[bool, str]:
    if isinstance(result, tuple):
        return bool(result[0]), str(result[-1])
    return bool(result), str(result)


def build_combat_assets(
    rover_mesh: unreal.SkeletalMesh,
    animations: dict[str, unreal.AnimSequence],
    weapon_mesh: unreal.SkeletalMesh,
) -> None:
    succeeded, report = unpack_editor_result(
        unreal.RoverAnimationEditorLibrary.create_rover_combat_p0_assets(
            rover_mesh,
            animations["Attack01"],
            animations["Attack02"],
            animations["Attack03"],
            animations["Behit_S_L"],
            animations["Behit_S_R"],
            weapon_mesh,
        )
    )
    if not succeeded:
        raise RuntimeError(f"P0 combat asset generation failed: {report}")
    unreal.log(f"ROVER_COMBAT_P0_ASSETS_READY {report}")


def configure_training_enemy() -> None:
    world = unreal.EditorLoadingAndSavingUtils.load_map(DEFAULT_MAP_PATH)
    if not world:
        raise RuntimeError(f"Could not load map: {DEFAULT_MAP_PATH}")

    enemy_class = unreal.load_class(None, ENEMY_CLASS_PATH)
    if not enemy_class:
        raise RuntimeError(f"Missing class: {ENEMY_CLASS_PATH}")

    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = actor_subsystem.get_all_level_actors()
    training_enemies = [
        actor
        for actor in actors
        if actor and actor.actor_has_tag(TRAINING_ENEMY_TAG)
    ]
    player_starts = [actor for actor in actors if isinstance(actor, unreal.PlayerStart)]
    if not player_starts:
        raise RuntimeError("The default map has no PlayerStart.")

    player_start = player_starts[0]
    start_location = player_start.get_actor_location()
    start_rotation = player_start.get_actor_rotation()
    forward = start_rotation.get_forward_vector()
    enemy_location = start_location + forward * 220.0 + unreal.Vector(0.0, 0.0, 5.0)
    enemy_rotation = unreal.Rotator(
        roll=0.0, pitch=0.0, yaw=start_rotation.yaw + 180.0
    )

    if training_enemies:
        enemy = training_enemies[0]
        enemy.set_actor_location(enemy_location, False, False)
        enemy.set_actor_rotation(enemy_rotation, False)
        if len(training_enemies) > 1:
            unreal.log_warning(
                f"Found {len(training_enemies)} training enemies; reused the first one."
            )
    else:
        enemy = actor_subsystem.spawn_actor_from_class(
            enemy_class, enemy_location, enemy_rotation
        )
        if not enemy:
            raise RuntimeError("Failed to spawn the P0 training enemy.")
    enemy.set_actor_label("Rover P0 Training Enemy")

    if not unreal.EditorLoadingAndSavingUtils.save_map(world, DEFAULT_MAP_PATH):
        raise RuntimeError(f"Could not save map: {DEFAULT_MAP_PATH}")
    unreal.log(
        f"ROVER_COMBAT_P0_ENEMY_READY actor={enemy.get_path_name()} "
        f"location={enemy_location}"
    )


def main() -> None:
    character_source_root = require_path("ROVER_COMBAT_CHARACTER_FBX_ROOT", True)
    weapon_fbx = require_path("ROVER_COMBAT_WEAPON_FBX", False)
    rover_mesh = unreal.load_asset(ROVER_MESH_PATH)
    if not isinstance(rover_mesh, unreal.SkeletalMesh):
        raise RuntimeError(f"Missing Rover mesh: {ROVER_MESH_PATH}")
    rover_skeleton = rover_mesh.get_editor_property("skeleton")
    if not isinstance(rover_skeleton, unreal.Skeleton):
        raise RuntimeError("The Rover mesh has no Skeleton.")

    unreal.EditorAssetLibrary.make_directory(COMBAT_ANIMATION_ROOT)
    unreal.EditorAssetLibrary.make_directory(WEAPON_ROOT)
    animations = import_combat_animations(character_source_root, rover_skeleton)
    weapon_mesh = import_weapon(weapon_fbx)
    configure_weapon_material(weapon_mesh, weapon_fbx)
    build_combat_assets(rover_mesh, animations, weapon_mesh)
    configure_training_enemy()

    if not unreal.EditorAssetLibrary.save_directory(
        "/Game/Rover", only_if_is_dirty=True, recursive=True
    ):
        raise RuntimeError("Failed to save generated Rover combat assets.")
    unreal.log("ROVER_COMBAT_P0_IMPORT_COMPLETE")


main()
