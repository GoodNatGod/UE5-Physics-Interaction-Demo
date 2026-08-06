import unreal


MESH_PATH = "/Game/Rover/Character/SK_Rover_Male"
BLEND_SPACE_ROOT = "/Game/Rover/Animations/BlendSpaces"
EXPECTED = {
    "BS_Rover_Walk": {
        -180.0: "Walk_B",
        -135.0: "Walk_LB",
        -45.0: "Walk_LF",
        0.0: "Walk_F",
        45.0: "Walk_RF",
        135.0: "Walk_RB",
        180.0: "Walk_B",
    },
    "BS_Rover_Run": {
        -180.0: "Run_B",
        -135.0: "Run_LB",
        -45.0: "Run_LF",
        0.0: "Run_F",
        45.0: "Run_RF",
        135.0: "Run_RB",
        180.0: "Run_B",
    },
}


def validate_blend_space(asset_name, skeleton):
    asset_path = f"{BLEND_SPACE_ROOT}/{asset_name}"
    blend_space = unreal.load_asset(asset_path)
    if not isinstance(blend_space, unreal.BlendSpace1D):
        raise RuntimeError(f"Missing BlendSpace1D: {asset_path}")
    if blend_space.get_editor_property("skeleton") != skeleton:
        raise RuntimeError(f"Skeleton mismatch: {asset_path}")

    direction = list(blend_space.get_editor_property("blend_parameters"))[0]
    if direction.get_editor_property("display_name") != "Direction":
        raise RuntimeError(f"Direction axis has the wrong name: {asset_path}")
    if direction.get_editor_property("min") != -180.0 or direction.get_editor_property("max") != 180.0:
        raise RuntimeError(f"Direction axis has the wrong range: {asset_path}")
    if not direction.get_editor_property("wrap_input"):
        raise RuntimeError(f"Direction axis must wrap: {asset_path}")

    actual = {}
    for sample in blend_space.get_editor_property("sample_data"):
        animation = sample.get_editor_property("animation")
        position = sample.get_editor_property("sample_value")
        if not isinstance(animation, unreal.AnimSequence):
            raise RuntimeError(f"BlendSpace contains an empty sample: {asset_path}")
        actual[position.x] = animation.get_name()

    if actual != EXPECTED[asset_name]:
        raise RuntimeError(f"Unexpected samples in {asset_path}: {actual}")

    segment_count = unreal.RoverAnimationEditorLibrary.get_blend_space_runtime_segment_count(
        blend_space
    )
    if segment_count <= 0:
        raise RuntimeError(
            f"BlendSpace runtime data is not sampled: {asset_path} "
            f"segments={segment_count}"
        )

    unreal.log(
        f"ROVER_BLENDSPACE_VALIDATION_OK path={asset_path} "
        f"samples={len(actual)} segments={segment_count}"
    )


def main():
    mesh = unreal.load_asset(MESH_PATH)
    if not isinstance(mesh, unreal.SkeletalMesh):
        raise RuntimeError(f"Missing Rover mesh: {MESH_PATH}")
    skeleton = mesh.get_editor_property("skeleton")
    if not isinstance(skeleton, unreal.Skeleton):
        raise RuntimeError("Rover mesh does not have a valid skeleton.")

    for asset_name in EXPECTED:
        validate_blend_space(asset_name, skeleton)


main()
