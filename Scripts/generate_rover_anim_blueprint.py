import unreal


MESH_PATH = "/Game/Rover/Character/SK_Rover_Male"
ANIM_BLUEPRINT_PATH = "/Game/Rover/Animations/ABP_Rover"


def main():
    mesh = unreal.load_asset(MESH_PATH)
    if not isinstance(mesh, unreal.SkeletalMesh):
        raise RuntimeError(f"Missing Rover mesh: {MESH_PATH}")

    if unreal.EditorAssetLibrary.does_asset_exist(ANIM_BLUEPRINT_PATH):
        if not unreal.EditorAssetLibrary.delete_asset(ANIM_BLUEPRINT_PATH):
            raise RuntimeError(
                f"Failed to delete existing AnimBlueprint: {ANIM_BLUEPRINT_PATH}"
            )

    anim_blueprint = unreal.RoverAnimationEditorLibrary.create_rover_anim_blueprint(
        mesh, ANIM_BLUEPRINT_PATH
    )
    if not isinstance(anim_blueprint, unreal.AnimBlueprint):
        raise RuntimeError("Rover AnimBlueprint builder returned no asset.")

    report = unreal.RoverAnimationEditorLibrary.get_rover_anim_blueprint_validation_report(
        anim_blueprint, mesh
    )
    if not report.startswith("parent=URoverAnimInstance"):
        raise RuntimeError(f"Generated AnimBlueprint failed validation: {report}")
    if not unreal.EditorAssetLibrary.save_loaded_asset(anim_blueprint, False):
        raise RuntimeError(f"Failed to save AnimBlueprint: {ANIM_BLUEPRINT_PATH}")

    unreal.log(
        f"ROVER_ANIM_BLUEPRINT_SAVED path={ANIM_BLUEPRINT_PATH} {report}"
    )


main()
