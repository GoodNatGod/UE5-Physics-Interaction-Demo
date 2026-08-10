import os
import sys
from pathlib import Path

import unreal


SCRIPT_DIRECTORY = Path(__file__).resolve().parent
if str(SCRIPT_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIRECTORY))

from configure_rover_animation_assets import (
    MOVE_STOP_ROOT_MOTION_ASSETS,
    configure_animation_assets,
)


DESTINATION_ROOT = "/Game/Rover"
SKELETAL_MESH_PATH = f"{DESTINATION_ROOT}/Character/SK_Rover_Male"

P0_ANIMATIONS = (
    "Stand1.fbx",
    "Stand2.fbx",
    "Stand1_Action01.fbx",
    "Stand1_Turn_L90D.fbx",
    "Stand1_Turn_R90D.fbx",
    "Walk_F.fbx",
    "Walk_B.fbx",
    "Walk_LF.fbx",
    "Walk_LB.fbx",
    "Walk_RF.fbx",
    "Walk_RB.fbx",
    "Run_F.fbx",
    "Run_B.fbx",
    "Run_LF.fbx",
    "Run_LB.fbx",
    "Run_RF.fbx",
    "Run_RB.fbx",
    "Run_Turnback.fbx",
    "Stop_Walk_L.fbx",
    "Stop_Walk_R.fbx",
    "Stop_Run_L.fbx",
    "Stop_Run_R.fbx",
    "Stop_Sprint_L.fbx",
    "Stop_Sprint_R.fbx",
    "Sprint_F.fbx",
    "Sprint_Impulse_F.fbx",
    "Jump_Walk_LF.fbx",
    "Jump_Walk_RF.fbx",
    "Jump_Run_LF.fbx",
    "Jump_Run_RF.fbx",
    "Jump_Loop.fbx",
    "Jump_Second_F.fbx",
    "Jump_Second_B.fbx",
    "Fall_Loop.fbx",
    "Fall_Loop_Fast.fbx",
    "Land_Light.fbx",
    "Land_Heavy.fbx",
    "Land_Roll.fbx",
)


def require_file(environment_name: str) -> Path:
    value = os.environ.get(environment_name)
    if not value:
        raise RuntimeError(f"Environment variable {environment_name} is required.")

    path = Path(value).resolve()
    if not path.is_file():
        raise RuntimeError(f"{environment_name} does not point to a file: {path}")
    return path


def require_directory(environment_name: str) -> Path:
    value = os.environ.get(environment_name)
    if not value:
        raise RuntimeError(f"Environment variable {environment_name} is required.")

    path = Path(value).resolve()
    if not path.is_dir():
        raise RuntimeError(f"{environment_name} does not point to a directory: {path}")
    return path


def make_tpose_options() -> unreal.FbxImportUI:
    options = unreal.FbxImportUI()
    options.set_editor_property("import_as_skeletal", True)
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_animations", False)
    options.set_editor_property("import_materials", True)
    options.set_editor_property("import_textures", True)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    return options


def make_animation_options(skeleton: unreal.Skeleton) -> unreal.FbxImportUI:
    options = unreal.FbxImportUI()
    options.set_editor_property("import_as_skeletal", True)
    options.set_editor_property("import_mesh", False)
    options.set_editor_property("import_animations", True)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)
    options.set_editor_property("skeleton", skeleton)
    return options


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


def import_skeletal_mesh(tpose_path: Path) -> unreal.SkeletalMesh:
    existing_mesh = unreal.load_asset(SKELETAL_MESH_PATH)
    if isinstance(existing_mesh, unreal.SkeletalMesh):
        unreal.log(f"Using existing skeletal mesh: {SKELETAL_MESH_PATH}")
        return existing_mesh

    task = make_task(
        tpose_path,
        f"{DESTINATION_ROOT}/Character",
        "SK_Rover_Male",
        make_tpose_options(),
    )
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    for imported_object in task.get_objects():
        if isinstance(imported_object, unreal.SkeletalMesh):
            unreal.log(f"Imported skeletal mesh: {imported_object.get_path_name()}")
            return imported_object

    imported_mesh = unreal.load_asset(SKELETAL_MESH_PATH)
    if not isinstance(imported_mesh, unreal.SkeletalMesh):
        raise RuntimeError(f"T-pose import did not create {SKELETAL_MESH_PATH}.")
    return imported_mesh


def import_animations(source_root: Path, skeleton: unreal.Skeleton) -> None:
    tasks = []
    missing = []

    for filename in P0_ANIMATIONS:
        source_file = source_root / filename
        if not source_file.is_file():
            missing.append(filename)
            continue

        tasks.append(
            make_task(
                source_file,
                f"{DESTINATION_ROOT}/Animations/P0",
                source_file.stem,
                make_animation_options(skeleton),
            )
        )

    if missing:
        raise RuntimeError("Missing required P0 FBX files: " + ", ".join(missing))

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    failed = [task.filename for task in tasks if not task.get_objects()]
    if failed:
        raise RuntimeError("Animation imports produced no assets: " + ", ".join(failed))

    unreal.log(f"Imported {len(tasks)} P0 animation sequences.")


def _unpack_editor_result(result):
    if isinstance(result, tuple):
        return bool(result[0]), str(result[1]) if len(result) > 1 else ""
    return result is not None, "" if result is None else str(result)


def bake_move_stop_root_motion() -> None:
    for asset_name in sorted(MOVE_STOP_ROOT_MOTION_ASSETS):
        animation = unreal.load_asset(f"{DESTINATION_ROOT}/Animations/P0/{asset_name}")
        if not isinstance(animation, unreal.AnimSequence):
            raise RuntimeError(f"Missing move-stop AnimSequence: {asset_name}")

        succeeded, report = _unpack_editor_result(
            unreal.RoverAnimationEditorLibrary.bake_move_stop_root_motion(animation)
        )
        if not succeeded:
            raise RuntimeError(
                f"Failed to bake move-stop root motion for {asset_name}: {report}"
            )
        unreal.log(f"ROVER_P0_MOVE_STOP_BAKE name={asset_name} {report}")


def main() -> None:
    source_root = require_directory("ROVER_FBX_SOURCE")
    tpose_path = require_file("ROVER_TPOSE_FBX")

    unreal.EditorAssetLibrary.make_directory(f"{DESTINATION_ROOT}/Character")
    unreal.EditorAssetLibrary.make_directory(f"{DESTINATION_ROOT}/Animations/P0")

    skeletal_mesh = import_skeletal_mesh(tpose_path)
    skeleton = skeletal_mesh.get_editor_property("skeleton")
    if not isinstance(skeleton, unreal.Skeleton):
        raise RuntimeError("The imported Rover skeletal mesh has no skeleton.")

    import_animations(source_root, skeleton)
    configure_animation_assets(f"{DESTINATION_ROOT}/Animations/P0")
    bake_move_stop_root_motion()
    if not unreal.EditorAssetLibrary.save_directory(
        DESTINATION_ROOT,
        only_if_is_dirty=True,
        recursive=True,
    ):
        raise RuntimeError(f"Failed to save imported Rover assets under {DESTINATION_ROOT}.")
    unreal.log("Rover P0 asset import completed.")


main()
