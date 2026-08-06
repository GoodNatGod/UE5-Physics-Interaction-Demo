import unreal


MESH_PATH = "/Game/Rover/Character/SK_Rover_Male"
ANIM_BLUEPRINT_PATH = "/Game/Rover/Animations/ABP_Rover"


def main():
    mesh = unreal.load_asset(MESH_PATH)
    if not isinstance(mesh, unreal.SkeletalMesh):
        raise RuntimeError(f"Missing Rover mesh: {MESH_PATH}")

    anim_blueprint = unreal.load_asset(ANIM_BLUEPRINT_PATH)
    if not isinstance(anim_blueprint, unreal.AnimBlueprint):
        raise RuntimeError(f"Missing AnimBlueprint: {ANIM_BLUEPRINT_PATH}")

    report = unreal.RoverAnimationEditorLibrary.get_rover_anim_blueprint_validation_report(
        anim_blueprint, mesh
    )
    if not report.startswith("parent=URoverAnimInstance"):
        raise RuntimeError(f"AnimBlueprint validation failed: {report}")

    unreal.log(
        f"ROVER_ANIM_BLUEPRINT_VALIDATION_OK path={ANIM_BLUEPRINT_PATH} {report}"
    )


main()
