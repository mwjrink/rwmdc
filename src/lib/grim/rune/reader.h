#pragma once

#include <lib/grim/mem/arena.h>
#include <lib/grim/rune/types.h>

void load_close_repo(FileAssetRepo repo) {
    fclose(repo.file_handle);
}

FileAssetRepo load_file(const char* path) {
    DEBUG_LOG(SCOPE_RUNE_READER, "Reading %s", path);
    FILE*         file   = fopen(path, "r");
    OutFileHeader header = {0};
    u64           read   = fread(&header, sizeof(OutFileHeader), 1, file);
    if (read != 1) {
        CRITICAL_LOG(SCOPE_RUNE_READER, "Failed to read file header.");
        exit(1);
    }

    VERBOSE_LOG(SCOPE_RUNE_READER, "AFTER FH");

    VERBOSE_LOG(SCOPE_RUNE_READER, "models_offset %lu", header.models_offset);
    VERBOSE_LOG(SCOPE_RUNE_READER, "textures_offset %lu", header.textures_offset);
    VERBOSE_LOG(SCOPE_RUNE_READER, "materials_offset %lu", header.materials_offset);
    VERBOSE_LOG(SCOPE_RUNE_READER, "vertices_offset %lu", header.vertices_offset);
    VERBOSE_LOG(SCOPE_RUNE_READER, "indices_offset %lu", header.indices_offset);
    VERBOSE_LOG(SCOPE_RUNE_READER, "clusters_offset %lu", header.clusters_offset);

    return (FileAssetRepo){
        .file_handle = file,
        .header      = header,
    };
}

// TODO function to load model & materials & textures necessary for that model

// TODO create a repo that stores models, materials, textures in case they overlap?
// - will they though?

Model load_get_model(rop(rw Arena) arena, rop(rw FileAssetRepo) asset_repo, u64 idx) {
    Model model = {0};
    fseek(asset_repo->file_handle, asset_repo->header.models_offset + sizeof(model.header) * idx, SEEK_SET);
    u64 read = fread(&model.header, sizeof(model.header), 1, asset_repo->file_handle);
    if (read != 1) {
        CRITICAL_LOG(SCOPE_RUNE_READER, "Failed to read file header.");
        exit(1);
    }

    VERBOSE_LOG(SCOPE_RUNE_READER, "AFTER");

    VERBOSE_LOG(SCOPE_RUNE_READER, "materials_count %u", model.header.materials_count);
    VERBOSE_LOG(SCOPE_RUNE_READER, "textures_count  %u", model.header.textures_count);

    VERBOSE_LOG(SCOPE_RUNE_READER, "cluster_offset  %lu", model.header.cls_offset);
    VERBOSE_LOG(SCOPE_RUNE_READER, "cluster_count   %u", model.header.cls_count);

    VERBOSE_LOG(SCOPE_RUNE_READER, "vertex_offset   %lu", model.header.vtx_offset);
    VERBOSE_LOG(SCOPE_RUNE_READER, "vertex_count    %u", model.header.vtx_count);

    VERBOSE_LOG(SCOPE_RUNE_READER, "index_offset    %lu", model.header.idx_offset);
    VERBOSE_LOG(SCOPE_RUNE_READER, "index_count     %u", model.header.idx_count);

    VERBOSE_LOG(SCOPE_RUNE_READER, "mat_idx %u", model.header.mat_idx);

    {
        VERBOSE_LOG(SCOPE_RUNE_READER, "vertex_count: %u", model.header.vtx_count);
        model.vertices = arena_alloc_aligned(arena, Vertex, model.header.vtx_count);
        fseek(asset_repo->file_handle,
              asset_repo->header.vertices_offset + sizeof(Vertex) * model.header.vtx_offset,
              SEEK_SET);
        u64 read = fread(model.vertices, sizeof(Vertex), model.header.vtx_count, asset_repo->file_handle);
        if (read != model.header.vtx_count) {
            CRITICAL_LOG(SCOPE_RUNE_READER, "Failed to read vertices.");
            exit(1);
        }
    }

    {
        VERBOSE_LOG(SCOPE_RUNE_READER, "index_count: %u", model.header.idx_count);
        model.indices = arena_alloc_aligned(arena, u32, model.header.idx_count);
        fseek(asset_repo->file_handle,
              asset_repo->header.indices_offset + sizeof(u8) * model.header.idx_offset,
              SEEK_SET);
        u64 read = fread(model.indices, sizeof(u8), model.header.idx_count, asset_repo->file_handle);
        if (read != model.header.idx_count) {
            CRITICAL_LOG(SCOPE_RUNE_READER, "Failed to read indices.");
            exit(1);
        }
    }

    {
        VERBOSE_LOG(SCOPE_RUNE_READER, "cluster_count: %u", model.header.cls_count);
        model.clusters = arena_alloc_aligned(arena, Cluster, model.header.cls_count);
        fseek(asset_repo->file_handle,
              asset_repo->header.clusters_offset + sizeof(Cluster) * model.header.cls_offset,
              SEEK_SET);
        u64 read = fread(model.clusters, sizeof(Cluster), model.header.cls_count, asset_repo->file_handle);
        if (read != model.header.cls_count) {
            CRITICAL_LOG(SCOPE_RUNE_READER, "Failed to read clusters.");
            exit(1);
        }
    }

    return model;
}

Material load_get_material(rop(rw Arena) arena, rop(rw FileAssetRepo) asset_repo, u64 idx) {
    Material material = {0};

    fseek(asset_repo->file_handle, asset_repo->header.materials_offset + sizeof(Material) * idx, SEEK_SET);
    u64 read = fread(&material, sizeof(Material), 1, asset_repo->file_handle);
    if (read != 1) {
        CRITICAL_LOG(SCOPE_RUNE_READER, "Failed to read material.");
        exit(1);
    }

    return material;
}
