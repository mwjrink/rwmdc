#pragma once

#include <lib/grim/mem/arena.h>
#include <lib/grim/rune/types.h>

#ifdef VERIFY_OUTPUT
#include <lib/grim/rune/reader.h>
#endif

// TODO version in header. ?in DEBUG not RELEASE?
void write_assets(rop(rw ScratchArena) scratch, rop(rw Arena) arena, char* dst, AssetRepo repo) {
    u64 write_time = time_start();

    DEBUG_LOG(SCOPE_RUNE_WRITER, "Writing %s", dst);
    FILE* out_file = fopen(dst, "wc");
    if (out_file == NULL) {
        CRITICAL_LOG(SCOPE_RUNE_WRITER, "Errno: %i", errno);
        exit(1);
    }

    // These should be written in an order where things that are loaded are next to each-other

    // out_file
    OutFileHeader outfile_header = {0};
    outfile_header.version       = 0;
    // Do I need this?
    // model_count instead?
    outfile_header.num_models    = repo.model_count;
    {
        i64 written = fwrite(&outfile_header, sizeof(outfile_header), 1, out_file);
        if (written != 1) {
            CRITICAL_LOG(SCOPE_RUNE_WRITER, "Failed to write header: %li", written);
            CRITICAL_LOG(SCOPE_RUNE_WRITER, "Errno: %i", errno);
            exit(1);
        }
    }

    // TODO this is annoying, total_written was nicer but fwrite returns array num... cringe
    u64 models_offset = ftell(out_file);

    // Goal is to be able to load things from an arbitrary location in the file
    // This means we need to trivially be able to grab the offset to a model,
    // then get all the contents
    // First, write all the model headers in a row/array
    // then, write all the clusters in a row/array
    // then, write all the vertices & indices in a row/array
    for (u32 midx = 0; midx < repo.model_count; midx++) {
        Model model = repo.models[midx];

        u64 written = fwrite(&model.header, sizeof(model.header), 1, out_file);
        if (written != 1) {
            CRITICAL_LOG(SCOPE_RUNE_WRITER, "Failed to write model.header");
            exit(1);
        }
    }

    u64 clusters_offset = ftell(out_file);

    for (u32 midx = 0; midx < repo.model_count; midx++) {
        Model model   = repo.models[midx];
        u64   written = fwrite(model.clusters, sizeof(Cluster), model.header.cls_count, out_file);
        if (written != model.header.cls_count) {
            CRITICAL_LOG(SCOPE_RUNE_WRITER, "Failed to write clusters");
            exit(1);
        }
    }

    u64 indices_offset = ftell(out_file);
    for (u32 midx = 0; midx < repo.model_count; midx++) {
        Model model   = repo.models[midx];
        u64   written = fwrite(model.indices, sizeof(u8), model.header.idx_count, out_file);
        if (written != model.header.idx_count) {
            CRITICAL_LOG(SCOPE_RUNE_WRITER, "Failed to write indices");
            exit(1);
        }
    }

    u64 vertices_offset = ftell(out_file);
    for (u32 midx = 0; midx < repo.model_count; midx++) {
        Model model   = repo.models[midx];
        u64   written = fwrite(model.vertices, sizeof(Vertex), model.header.vtx_count, out_file);
        if (written != model.header.vtx_count) {
            CRITICAL_LOG(SCOPE_RUNE_WRITER, "Failed to write vertices");
            exit(1);
        }
    }

    u64 textures_offset = ftell(out_file);
    {
        // TODO do this
    }

    u64 materials_offset = ftell(out_file);
    {
        u64 written = fwrite(repo.materials, sizeof(Material), repo.material_count, out_file);
        if (written != repo.material_count) {
            CRITICAL_LOG(SCOPE_RUNE_WRITER, "Failed to write materials");
            exit(1);
        }
    }

    fseek(out_file, 0, SEEK_SET);

    // rewrite the header with correct offsets
    outfile_header.materials_offset = materials_offset;
    outfile_header.textures_offset  = textures_offset;
    outfile_header.models_offset    = models_offset;
    outfile_header.vertices_offset  = vertices_offset;
    outfile_header.indices_offset   = indices_offset;
    outfile_header.clusters_offset  = clusters_offset;
    {
        i64 written = fwrite(&outfile_header, sizeof(OutFileHeader), 1, out_file);
        if (written != 1) {
            CRITICAL_LOG(SCOPE_RUNE_WRITER, "Failed to write header: %li", written);
            CRITICAL_LOG(SCOPE_RUNE_WRITER, "Errno: %i", errno);
            exit(1);
        }
    }
    VERBOSE_LOG(SCOPE_RUNE_WRITER, "BEFORE FH");

    VERBOSE_LOG(SCOPE_RUNE_WRITER, "models_offset %lu", outfile_header.models_offset);
    VERBOSE_LOG(SCOPE_RUNE_WRITER, "textures_offset %lu", outfile_header.textures_offset);
    VERBOSE_LOG(SCOPE_RUNE_WRITER, "materials_offset %lu", outfile_header.materials_offset);
    VERBOSE_LOG(SCOPE_RUNE_WRITER, "vertices_offset %lu", outfile_header.vertices_offset);
    VERBOSE_LOG(SCOPE_RUNE_WRITER, "indices_offset %lu", outfile_header.indices_offset);
    VERBOSE_LOG(SCOPE_RUNE_WRITER, "clusters_offset %lu", outfile_header.clusters_offset);

    // TODO do I need to flush and close? or close flushes?
    fflush(out_file);
    fclose(out_file);

    INFO_LOG(SCOPE_RUNE_WRITER, "Finished writing.");

    DEBUG_LOG(SCOPE_RUNE_WRITER, "Write time: ");
    time_end(SCOPE_RUNE_WRITER, write_time);

#ifdef VERIFY_OUTPUT
    u64 verif_start = time_start();
    INFO_LOG(SCOPE_RUNE_WRITER, "Verifying output.");
    {
        FileAssetRepo asset_repo = load_file(dst);
        Arena         repo_arena = arena_create();

        // TODO check all models, textures, materials
        arena_ckpt(&repo_arena);
        for (u32 midx = 0; midx < repo.model_count; midx++) {
            Model model = load_get_model(&repo_arena, &asset_repo, 0);

            // Header
            {
                i32 result = memcmp(&repo.models[midx].header, &model.header, sizeof(model.header));
                if (result != 0) {
                    CRITICAL_LOG(SCOPE_RUNE_WRITER, "Model.header at idx %u is not equal to src.", midx);
                }
            }

            // Vertices
            {
                i32 result =
                    memcmp(repo.models[midx].vertices, model.vertices, sizeof(Vertex) * model.header.vtx_count);

                DEBUG_LOG(
                    SCOPE_RUNE_WRITER, "v count: %u %u", model.header.vtx_count, repo.models[midx].header.vtx_count);
                for (int i = 0; i < model.header.vtx_count; i++) {
                    Vertex vert = model.vertices[i];
                    DEBUG_LOG(
                        SCOPE_RUNE_WRITER, "vert[%u]: %f %f %f", i, vert.position.x, vert.position.y, vert.position.z);
                }

                if (result != 0) {
                    CRITICAL_LOG(SCOPE_RUNE_WRITER, "Model.vertices at idx %u is not equal to src.", midx);
                }
            }

            // Indices
            {
                i32 result = memcmp(repo.models[midx].indices, model.indices, sizeof(u8) * model.header.idx_count);
                DEBUG_LOG(SCOPE_RUNE_WRITER, "count: %u %u", midx, model.header.idx_count);

                for (int i = 0; i < model.header.idx_count; i += 3) {
                    DEBUG_LOG(SCOPE_RUNE_WRITER,
                              "tri: %u %u %u",
                              model.indices[i],
                              model.indices[i + 1],
                              model.indices[i + 2]);
                }

                if (result != 0) {
                    CRITICAL_LOG(SCOPE_RUNE_WRITER, "Model.indices at idx %u is not equal to src.", midx);
                }
            }

            // Clusters
            {
                i32 result =
                    memcmp(repo.models[midx].clusters, model.clusters, sizeof(Cluster) * model.header.cls_count);
                DEBUG_LOG(
                    SCOPE_RUNE_WRITER, "STUFFER: %u %u", model.clusters[0].tri_count * 3, model.clusters[0].idx_offset);
                if (result != 0) {
                    CRITICAL_LOG(SCOPE_RUNE_WRITER, "Model.clusters at idx %u is not equal to src.", midx);
                }
            }

            arena_rollback(&repo_arena);
        }

        for (u32 midx = 0; midx < repo.model_count; midx++) {
            Material material = load_get_material(&repo_arena, &asset_repo, midx);

            i32 result = memcmp(&repo.materials[midx], &material, sizeof(Material));
            if (result != 0) {
                CRITICAL_LOG(SCOPE_RUNE_WRITER, "Model at idx %u is not equal to src.", midx);
            }

            arena_rollback(&repo_arena);
        }

        load_close_repo(asset_repo);
    }
    INFO_LOG(SCOPE_RUNE_WRITER, "Verification successful.");

    DEBUG_LOG(SCOPE_RUNE_WRITER, "Verification time: ");
    time_end(SCOPE_RUNE_WRITER, verif_start);
#endif
}
