import unreal


ANIMATION_ROOT = "/Game/Rover/Animations/P0"

MOVE_STOP_ROOT_MOTION_ASSETS = {
	"Land_Roll",
    "Stop_Walk_L",
    "Stop_Walk_R",
    "Stop_Run_L",
    "Stop_Run_R",
    "Stop_Sprint_L",
    "Stop_Sprint_R",
}

ENABLE_ROOT_MOTION_PROPERTIES = (
    "enable_root_motion",
    "b_enable_root_motion",
    "bEnableRootMotion",
)
FORCE_ROOT_LOCK_PROPERTIES = (
    "force_root_lock",
    "b_force_root_lock",
    "bForceRootLock",
)
ROOT_MOTION_LOCK_PROPERTIES = (
    "root_motion_root_lock",
    "RootMotionRootLock",
)


def _animation_library():
    for class_name in ("AnimationBlueprintLibrary", "AnimationLibrary"):
        library = getattr(unreal, class_name, None)
        if library is not None:
            return library
    return None


def _read_editor_property(asset, property_names):
    errors = []
    for property_name in property_names:
        try:
            return property_name, asset.get_editor_property(property_name)
        except Exception as exc:
            errors.append(f"{property_name}: {exc}")
    raise RuntimeError("; ".join(errors))


def _set_boolean_property(
    asset,
    label,
    property_names,
    expected_value,
    setter_names,
    getter_names,
):
    errors = []
    for property_name in property_names:
        try:
            asset.set_editor_property(property_name, expected_value)
            actual_value = asset.get_editor_property(property_name)
            if bool(actual_value) != expected_value:
                raise RuntimeError(f"read back {actual_value!r}")
            return
        except Exception as exc:
            errors.append(f"property {property_name}: {exc}")

    library = _animation_library()
    if library is not None:
        for setter_name in setter_names:
            setter = getattr(library, setter_name, None)
            if not callable(setter):
                continue
            try:
                setter(asset, expected_value)
                for getter_name in getter_names:
                    getter = getattr(library, getter_name, None)
                    if callable(getter):
                        if bool(getter(asset)) != expected_value:
                            raise RuntimeError(f"{getter_name} returned the wrong value")
                        return

                _, actual_value = _read_editor_property(asset, property_names)
                if bool(actual_value) != expected_value:
                    raise RuntimeError(f"read back {actual_value!r}")
                return
            except Exception as exc:
                errors.append(f"API {setter_name}: {exc}")

    raise RuntimeError(
        f"Unable to set {label}={expected_value} on {asset.get_path_name()}: "
        + "; ".join(errors)
    )


def _ref_pose_values(current_value=None):
    values = []
    for enum_type in (getattr(unreal, "RootMotionRootLock", None), type(current_value)):
        if enum_type is None:
            continue
        for member_name in ("REF_POSE", "REFPOSE", "REF_POSE_ROOT_LOCK"):
            value = getattr(enum_type, member_name, None)
            if value is not None and value not in values:
                values.append(value)

    # Some older reflection layers accept the underlying RefPose enum value.
    if 0 not in values:
        values.append(0)
    return values


def _is_ref_pose(value):
    enum_type = getattr(unreal, "RootMotionRootLock", None)
    ref_pose = getattr(enum_type, "REF_POSE", None) if enum_type is not None else None
    if ref_pose is not None and value == ref_pose:
        return True

    normalized = str(value).upper().replace(" ", "_")
    return "REF_POSE" in normalized or normalized in ("REFPOSE", "0")


def _anim_first_frame_values(current_value=None):
    values = []
    for enum_type in (getattr(unreal, "RootMotionRootLock", None), type(current_value)):
        if enum_type is None:
            continue
        for member_name in ("ANIM_FIRST_FRAME", "ANIMFIRSTFRAME"):
            value = getattr(enum_type, member_name, None)
            if value is not None and value not in values:
                values.append(value)

    # ERootMotionRootLock::AnimFirstFrame has underlying value 1.
    if 1 not in values:
        values.append(1)
    return values


def _is_anim_first_frame(value):
    enum_type = getattr(unreal, "RootMotionRootLock", None)
    expected = (
        getattr(enum_type, "ANIM_FIRST_FRAME", None)
        if enum_type is not None
        else None
    )
    if expected is not None and value == expected:
        return True

    normalized = str(value).upper().replace(" ", "_")
    return "ANIM_FIRST_FRAME" in normalized or normalized in ("ANIMFIRSTFRAME", "1")


def _set_ref_pose_root_lock(asset):
    errors = []
    for property_name in ROOT_MOTION_LOCK_PROPERTIES:
        try:
            current_value = asset.get_editor_property(property_name)
        except Exception as exc:
            errors.append(f"property {property_name}: {exc}")
            continue

        for ref_pose in _ref_pose_values(current_value):
            try:
                asset.set_editor_property(property_name, ref_pose)
                actual_value = asset.get_editor_property(property_name)
                if not _is_ref_pose(actual_value):
                    raise RuntimeError(f"read back {actual_value!r}")
                return
            except Exception as exc:
                errors.append(f"property {property_name} with {ref_pose!r}: {exc}")

    library = _animation_library()
    if library is not None:
        setter = getattr(library, "set_root_motion_lock_type", None)
        getter = getattr(library, "get_root_motion_lock_type", None)
        if callable(setter):
            for ref_pose in _ref_pose_values():
                try:
                    setter(asset, ref_pose)
                    if callable(getter):
                        actual_value = getter(asset)
                    else:
                        _, actual_value = _read_editor_property(
                            asset, ROOT_MOTION_LOCK_PROPERTIES
                        )
                    if not _is_ref_pose(actual_value):
                        raise RuntimeError(f"read back {actual_value!r}")
                    return
                except Exception as exc:
                    errors.append(f"API set_root_motion_lock_type: {exc}")

    raise RuntimeError(
        f"Unable to set root_motion_root_lock=RefPose on {asset.get_path_name()}: "
        + "; ".join(errors)
    )


def _set_anim_first_frame_root_lock(asset):
    errors = []
    for property_name in ROOT_MOTION_LOCK_PROPERTIES:
        try:
            current_value = asset.get_editor_property(property_name)
        except Exception as exc:
            errors.append(f"property {property_name}: {exc}")
            continue

        for lock_value in _anim_first_frame_values(current_value):
            try:
                asset.set_editor_property(property_name, lock_value)
                actual_value = asset.get_editor_property(property_name)
                if not _is_anim_first_frame(actual_value):
                    raise RuntimeError(f"read back {actual_value!r}")
                return
            except Exception as exc:
                errors.append(f"property {property_name} with {lock_value!r}: {exc}")

    library = _animation_library()
    if library is not None:
        setter = getattr(library, "set_root_motion_lock_type", None)
        getter = getattr(library, "get_root_motion_lock_type", None)
        if callable(setter):
            for lock_value in _anim_first_frame_values():
                try:
                    setter(asset, lock_value)
                    if callable(getter):
                        actual_value = getter(asset)
                    else:
                        _, actual_value = _read_editor_property(
                            asset, ROOT_MOTION_LOCK_PROPERTIES
                        )
                    if not _is_anim_first_frame(actual_value):
                        raise RuntimeError(f"read back {actual_value!r}")
                    return
                except Exception as exc:
                    errors.append(f"API set_root_motion_lock_type: {exc}")

    raise RuntimeError(
        "Unable to set root_motion_root_lock=AnimFirstFrame on "
        f"{asset.get_path_name()}: " + "; ".join(errors)
    )


def configure_animation_sequence(animation_sequence):
    modify = getattr(animation_sequence, "modify", None)
    if callable(modify):
        modify()
    enable_root_motion = animation_sequence.get_name() in MOVE_STOP_ROOT_MOTION_ASSETS
    _set_boolean_property(
        animation_sequence,
        "enable_root_motion",
        ENABLE_ROOT_MOTION_PROPERTIES,
        enable_root_motion,
        ("set_root_motion_enabled", "set_is_root_motion_enabled"),
        ("is_root_motion_enabled", "get_is_root_motion_enabled"),
    )
    if enable_root_motion:
        _set_anim_first_frame_root_lock(animation_sequence)
    else:
        _set_ref_pose_root_lock(animation_sequence)
    _set_boolean_property(
        animation_sequence,
        "force_root_lock",
        FORCE_ROOT_LOCK_PROPERTIES,
        True,
        ("set_is_root_motion_lock_forced", "set_root_motion_lock_forced"),
        ("is_root_motion_lock_forced", "get_is_root_motion_lock_forced"),
    )


def configure_animation_assets(animation_root=ANIMATION_ROOT):
    asset_paths = sorted(
        unreal.EditorAssetLibrary.list_assets(
            animation_root, recursive=True, include_folder=False
        )
    )
    animation_sequences = []
    for asset_path in asset_paths:
        asset = unreal.load_asset(asset_path)
        if isinstance(asset, unreal.AnimSequence):
            animation_sequences.append(asset)

    if not animation_sequences:
        raise RuntimeError(f"No Animation Sequences were found under {animation_root}.")

    for animation_sequence in animation_sequences:
        configure_animation_sequence(animation_sequence)

    failed_saves = []
    for animation_sequence in animation_sequences:
        if not unreal.EditorAssetLibrary.save_loaded_asset(
            animation_sequence, only_if_is_dirty=False
        ):
            failed_saves.append(animation_sequence.get_path_name())
    if failed_saves:
        raise RuntimeError(
            "Failed to save configured Animation Sequences: " + ", ".join(failed_saves)
        )

    unreal.log(
        "ROVER_ANIMATION_ASSET_CONFIG_OK "
        f"count={len(animation_sequences)} move_stop_root_motion=true "
        "move_stop_root_lock=AnimFirstFrame other_root_motion=false "
        "force_root_lock=true"
    )
    return animation_sequences


def main():
    configure_animation_assets()


if __name__ == "__main__":
    main()
