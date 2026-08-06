import unreal


ASSET_PATHS = (
    "/Game/PhysicsWorldDemo/Niagara/NS_P0_Fireball",
    "/Game/PhysicsWorldDemo/Niagara/NS_P0_Explosion",
    "/Game/PhysicsWorldDemo/Niagara/NS_P0_SurfaceImpact",
    "/ChaosNiagara/ChaosDestructionListenerActor",
)


def describe_object(value):
    if value is None:
        return "None"
    if hasattr(value, "get_path_name"):
        return value.get_path_name()
    return repr(value)


def main():
    for asset_path in ASSET_PATHS:
        asset = unreal.load_asset(asset_path)
        unreal.log(
            "NIAGARA_INSPECT_ASSET "
            f"path={asset_path} object={describe_object(asset)} "
            f"class={asset.get_class().get_name() if asset else 'None'}"
        )
        if not asset:
            continue

        if isinstance(asset, unreal.NiagaraSystem):
            unreal.RoverEditorTestLibrary.dump_niagara_system(asset_path)

        methods = [
            name
            for name in dir(asset)
            if any(token in name.lower() for token in ("emitter", "parameter", "system"))
        ]
        unreal.log(f"NIAGARA_INSPECT_METHODS path={asset_path} names={methods}")

        for property_name in (
            "exposed_parameters",
            "emitter_handles",
            "system_spawn_script",
            "system_update_script",
            "template_specification",
        ):
            try:
                value = asset.get_editor_property(property_name)
            except Exception as exc:
                unreal.log(
                    "NIAGARA_INSPECT_PROPERTY_UNAVAILABLE "
                    f"path={asset_path} property={property_name} error={exc!r}"
                )
                continue
            unreal.log(
                "NIAGARA_INSPECT_PROPERTY "
                f"path={asset_path} property={property_name} value={describe_object(value)} "
                f"type={type(value).__name__}"
            )
            nested_methods = [
                name
                for name in dir(value)
                if any(token in name.lower() for token in ("parameter", "variable", "emitter"))
            ]
            unreal.log(
                "NIAGARA_INSPECT_NESTED_METHODS "
                f"path={asset_path} property={property_name} names={nested_methods}"
            )


main()
