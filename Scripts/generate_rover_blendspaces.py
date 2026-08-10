import unreal


MESH_PATH = "/Game/Rover/Character/SK_Rover_Male"
ANIMATION_ROOT = "/Game/Rover/Animations/P0"
BLEND_SPACE_ROOT = "/Game/Rover/Animations/BlendSpaces"

DIRECTION_SAMPLES = (
    ("B", -180.0),
    ("LB", -135.0),
    ("LF", -45.0),
    ("F", 0.0),
    ("RF", 45.0),
    ("RB", 135.0),
    ("B", 180.0),
)


def require_asset(path, asset_type):
    asset = unreal.load_asset(path)
    if not isinstance(asset, asset_type):
        raise RuntimeError(f"Required {asset_type.__name__} is missing: {path}")
    return asset


def make_direction_parameter():
    parameter = unreal.BlendParameter()
    parameter.set_editor_properties(
        {
            "display_name": "Direction",
            "min": -180.0,
            "max": 180.0,
            "grid_num": 8,
            "snap_to_grid": False,
            "wrap_input": True,
        }
    )
    return parameter


def make_sample(animation, direction):
    sample = unreal.BlendSample()
    sample.set_editor_properties(
        {
            "animation": animation,
            "sample_value": unreal.Vector(direction, 0.0, 0.0),
            "rate_scale": 1.0,
            "mirror": False,
            "use_single_frame_for_blending": False,
        }
    )
    return sample


def create_or_update_blend_space(asset_name, animation_prefix, skeleton, mesh):
    asset_path = f"{BLEND_SPACE_ROOT}/{asset_name}"
    blend_space = unreal.load_asset(asset_path)

    if blend_space is None:
        factory = unreal.BlendSpaceFactoryNew()
        factory.set_editor_properties(
            {
                "target_skeleton": skeleton,
                "preview_skeletal_mesh": mesh,
            }
        )
        blend_space = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name,
            BLEND_SPACE_ROOT,
            unreal.BlendSpace1D,
            factory,
        )

    if not isinstance(blend_space, unreal.BlendSpace1D):
        raise RuntimeError(f"Existing asset is not a BlendSpace1D: {asset_path}")
    if blend_space.get_editor_property("skeleton") != skeleton:
        raise RuntimeError(f"BlendSpace skeleton mismatch: {asset_path}")

    parameters = list(blend_space.get_editor_property("blend_parameters"))
    parameters[0] = make_direction_parameter()
    blend_space.set_editor_property("blend_parameters", parameters)

    samples = []
    for suffix, direction in DIRECTION_SAMPLES:
        animation = require_asset(
            f"{ANIMATION_ROOT}/{animation_prefix}_{suffix}", unreal.AnimSequence
        )
        if animation.get_editor_property("skeleton") != skeleton:
            raise RuntimeError(f"Animation skeleton mismatch: {animation.get_path_name()}")
        samples.append(make_sample(animation, direction))

    blend_space.set_editor_properties(
        {
            "sample_data": samples,
            "loop": True,
            "interpolate_using_grid": False,
            "target_weight_interpolation_speed_per_sec": 5.0,
        }
    )
    if not unreal.RoverAnimationEditorLibrary.resample_blend_space(blend_space):
        raise RuntimeError(f"Failed to resample BlendSpace: {asset_path}")
    if not unreal.EditorAssetLibrary.save_loaded_asset(blend_space, False):
        raise RuntimeError(f"Failed to save BlendSpace: {asset_path}")

    unreal.log(f"ROVER_BLENDSPACE_SAVED path={asset_path} samples={len(samples)}")


def main():
    mesh = require_asset(MESH_PATH, unreal.SkeletalMesh)
    skeleton = mesh.get_editor_property("skeleton")
    if not isinstance(skeleton, unreal.Skeleton):
        raise RuntimeError("Rover mesh does not have a valid skeleton.")

    create_or_update_blend_space("BS_Rover_Walk", "Walk", skeleton, mesh)
    create_or_update_blend_space("BS_Rover_Run", "Run", skeleton, mesh)


main()
