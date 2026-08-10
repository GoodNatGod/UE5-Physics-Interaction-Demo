import os

import unreal


FOLIAGE_MATERIAL_PATHS = (
    "/Game/ModularLostRuinKit/Materials/Nature/Foliage/MI_Tree_Leafs",
    "/Game/ModularLostRuinKit/Materials/Nature/Foliage/MI_HangingLeaves",
)
WOODEN_BOX_PATH = "/Game/ModularLostRuinKit/Models/Props/SM_WoodenBox1"
SOURCE_MAP_PATH = "/Game/ModularLostRuinKit/Maps/ExampleMap_Lumen"
TEXTURE_EXPORT_PATHS = (
    "/Game/ModularLostRuinKit/Textures/Nature/Foliage/T_Fol_Leafs_BC",
    "/Game/ModularLostRuinKit/Textures/Nature/Foliage/T_Fol_HangingLeaves_BC",
)


def describe_material(path: str) -> None:
    material = unreal.load_asset(path)
    if not isinstance(material, unreal.MaterialInterface):
        raise RuntimeError(f"Missing material interface: {path}")

    base_material = material.get_base_material()
    parent = material.get_editor_property("parent") if isinstance(
        material, unreal.MaterialInstance
    ) else None
    texture_values = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value
    texture_names = unreal.MaterialEditingLibrary.get_texture_parameter_names(material)
    textures = []
    for name in texture_names:
        value = texture_values(material, name)
        textures.append(f"{name}={value.get_path_name() if value else 'None'}")

    unreal.log(
        "LOST_RUIN_FOLIAGE "
        f"asset={material.get_path_name()} "
        f"parent={parent.get_path_name() if parent else 'None'} "
        f"base={base_material.get_path_name() if base_material else 'None'} "
        f"blend={base_material.get_editor_property('blend_mode') if base_material else 'None'} "
        f"two_sided={base_material.get_editor_property('two_sided') if base_material else 'None'} "
        f"niagara_sprites={unreal.MaterialEditingLibrary.has_material_usage(base_material, unreal.MaterialUsage.MATUSAGE_NIAGARA_SPRITES) if base_material else False} "
        f"textures={'|'.join(textures) if textures else 'None'}"
    )


def export_textures() -> None:
    export_directory = os.path.join(
        unreal.Paths.project_saved_dir(), "Diagnostics", "ModularLostRuinKit"
    )
    os.makedirs(export_directory, exist_ok=True)
    for path in TEXTURE_EXPORT_PATHS:
        texture = unreal.load_asset(path)
        if not isinstance(texture, unreal.Texture2D):
            raise RuntimeError(f"Missing texture: {path}")
        filename = os.path.join(export_directory, f"{texture.get_name()}.png")
        task = unreal.AssetExportTask()
        task.set_editor_property("object", texture)
        task.set_editor_property("filename", filename)
        task.set_editor_property("automated", True)
        task.set_editor_property("prompt", False)
        task.set_editor_property("replace_identical", True)
        task.set_editor_property("exporter", unreal.TextureExporterPNG())
        if not unreal.Exporter.run_asset_export_task(task):
            raise RuntimeError(f"Failed to export texture: {path}")
        unreal.log(f"LOST_RUIN_TEXTURE_EXPORT asset={path} file={filename}")


def describe_mesh() -> None:
    mesh = unreal.load_asset(WOODEN_BOX_PATH)
    if not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError(f"Missing static mesh: {WOODEN_BOX_PATH}")

    bounds = mesh.get_bounds()
    materials = []
    for index, slot in enumerate(mesh.get_editor_property("static_materials")):
        interface = slot.get_editor_property("material_interface")
        materials.append(
            f"{index}:{slot.get_editor_property('material_slot_name')}="
            f"{interface.get_path_name() if interface else 'None'}"
        )
    unreal.log(
        "LOST_RUIN_WOODEN_BOX "
        f"asset={mesh.get_path_name()} "
        f"origin={bounds.origin} extent={bounds.box_extent} radius={bounds.sphere_radius:.2f} "
        f"materials={'|'.join(materials) if materials else 'None'}"
    )


def describe_map() -> None:
    world = unreal.EditorLoadingAndSavingUtils.load_map(SOURCE_MAP_PATH)
    if not world:
        raise RuntimeError(f"Missing source map: {SOURCE_MAP_PATH}")

    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = [actor for actor in actor_subsystem.get_all_level_actors() if actor]
    starts = [actor for actor in actors if isinstance(actor, unreal.PlayerStart)]
    world_settings = world.get_world_settings()
    game_mode = world_settings.get_editor_property("default_game_mode") if world_settings else None
    unreal.log(
        "LOST_RUIN_MAP "
        f"asset={SOURCE_MAP_PATH} actors={len(actors)} player_starts={len(starts)} "
        f"game_mode={game_mode.get_path_name() if game_mode else 'None'}"
    )
    for start in starts:
        unreal.log(
            "LOST_RUIN_PLAYER_START "
            f"actor={start.get_path_name()} location={start.get_actor_location()} "
            f"rotation={start.get_actor_rotation()}"
        )
    wooden_box_mesh = unreal.load_asset(WOODEN_BOX_PATH)
    for actor in actors:
        component = actor.get_component_by_class(unreal.StaticMeshComponent)
        if component and component.get_editor_property("static_mesh") == wooden_box_mesh:
            unreal.log(
                "LOST_RUIN_MAP_WOODEN_BOX "
                f"actor={actor.get_path_name()} label={actor.get_actor_label()} "
                f"location={actor.get_actor_location()} rotation={actor.get_actor_rotation()} "
                f"scale={actor.get_actor_scale3d()}"
            )
        if actor.get_class().get_name() == "BP_LooseDebrisRegion_C":
            unreal.log(
                "LOST_RUIN_MAP_DEBRIS_REGION "
                f"actor={actor.get_path_name()} location={actor.get_actor_location()} "
                f"scale={actor.get_actor_scale3d()}"
            )


def main() -> None:
    for path in FOLIAGE_MATERIAL_PATHS:
        describe_material(path)
    export_textures()
    describe_mesh()
    describe_map()
    unreal.log("LOST_RUIN_ASSET_INSPECTION_OK")


main()
