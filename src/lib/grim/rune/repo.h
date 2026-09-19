#pragma once

#include <lib/grim/mem/arena.h>

#include <lib/grim/rune/types.h>

#include <assimp/cimport.h>     // Plain-C interface
#include <assimp/postprocess.h> // Post processing flags
#include <assimp/scene.h>       // Output data structure

static ScratchArena* _global_scratch;

void* _global_scratch_alloc(u64 sz) {
    return scratch_alloc_align(_global_scratch, sizeof(void*), sz);
}

void _global_scratch_free() {
}

AssetRepo create_repo(rop(rw ScratchArena) scratch, rop(rw Arena) arena, const char* src) {
    DEBUG_LOG(SCOPE_RUNE_REPO, "Loading %s", src);
    const struct aiScene* scene = aiImportFile(src,
                                               aiProcess_CalcTangentSpace |             //
                                                   aiProcess_Triangulate |              //
                                                   aiProcess_JoinIdenticalVertices |    //
                                                   aiProcess_SortByPType |              //
                                                   aiProcess_RemoveComponent |          //
                                                   aiProcess_GenSmoothNormals |         //
                                                   aiProcess_PreTransformVertices |     //
                                                   aiProcess_ValidateDataStructure |    //
                                                   aiProcess_RemoveRedundantMaterials | //
                                                   aiProcess_FindDegenerates |          //
                                                   aiProcess_FindInvalidData |          //
                                                   aiProcess_FindInstances |            //
                                                   aiProcess_OptimizeMeshes |           //
                                                   aiProcess_OptimizeGraph |            //
                                                   aiProcess_EmbedTextures |            //
                                                   aiProcess_GenBoundingBoxes |         //
                                                   aiProcess_ValidateDataStructure      //
    );
    // If the import failed, report it
    if (0 == scene) {
        CRITICAL_LOG(SCOPE_RUNE_REPO, "Failed to parse provided scene file: %s", aiGetErrorString());
        exit(1);
    }

    // ktxTexture_WriteToStream(This, dststr);

    // FIX this is temp, texture_count should be total materials for a number of objects/meshes in the file
    DEBUG_LOG(SCOPE_RUNE_REPO, "Num textures: %u", scene->mNumTextures);
    u32      texture_count = scene->mNumTextures;
    Texture* textures      = arena_alloc_aligned(arena, Texture, texture_count);
    for (u32 idx = 0; idx < scene->mNumTextures; idx++) {
        // scene->mTextures[idx];
        DEBUG_LOG(SCOPE_RUNE_REPO, "Texture filename: %s", scene->mTextures[idx]->mFilename.data);
    }

    // FIX this is temp, material_count should be total materials for a number of objects/meshes in the file
    DEBUG_LOG(SCOPE_RUNE_REPO, "Num materials: %u", scene->mNumMaterials);
    u32       material_count = scene->mNumMaterials;
    Material* materials      = arena_alloc_aligned(arena, Material, material_count);
    for (u32 idx = 0; idx < scene->mNumMaterials; idx++) {
        struct aiMaterial* aimat = scene->mMaterials[idx];

        aiGetMaterialFloat(aimat, AI_MATKEY_METALLIC_FACTOR, &materials[idx].metallic);
        aiGetMaterialFloat(aimat, AI_MATKEY_ROUGHNESS_FACTOR, &materials[idx].roughness);

        struct aiString* mr_tex_path = 0;
        aiGetMaterialTexture(aimat, AI_MATKEY_METALLIC_TEXTURE, mr_tex_path, NULL, NULL, NULL, NULL, NULL, NULL);
        if (mr_tex_path != NULL) {
            DEBUG_LOG(SCOPE_RUNE_REPO, "Texture path: %s", mr_tex_path->data);
        }

        // &materials[idx].metallic_roughness_texture_idx,

        // materials[idx].metallic_roughness_texture_idx;
        // materials[idx].base_color_texture_idx;
        // materials[idx].normal_texture_idx;
    }

    DEBUG_LOG(SCOPE_RUNE_REPO, "Num meshes: %u", scene->mNumMeshes);
    u32 model_count    = scene->mNumMeshes;
    u64 total_clusters = 0;
    u64 total_vertices = 0;
    u64 total_indices  = 0;

    Model* models = arena_alloc_aligned(arena, Model, model_count);
    for (u32 idx = 0; idx < scene->mNumMeshes; idx++) {
        struct aiMesh* mesh     = scene->mMeshes[idx];
        Vertex*        vertices = arena_alloc_aligned(arena, Vertex, mesh->mNumVertices);

        // u32* vremap = scratch_alloc_aligned(scratch, u32, mesh->mNumVertices);
        // meshopt_spatialSortRemap(vremap, (f32*)mesh->mVertices, mesh->mNumVertices, sizeof(struct aiVector3D));
        //
        // Vec3* vertices_sorted = scratch_alloc_aligned(scratch, Vec3, mesh->mNumVertices);
        // meshopt_remapVertexBuffer(vertices_sorted, (f32*)mesh->mVertices, mesh->mNumVertices, sizeof(f32) * 3,
        // vremap);
        //
        // meshopt_spatialSortTriangles
        // meshopt_optimizeVertexCache

        DEBUG_LOG(SCOPE_RUNE_REPO, "Num Vertices: %u", mesh->mNumVertices);
        for (u32 vidx = 0; vidx < mesh->mNumVertices; vidx++) {
            Vec2 uv        = {0};
            // if (mesh->mTextureCoords[vidx] != NULL) {
            //     uv = (Vec2){
            //         .x = mesh->mTextureCoords[vidx][0].x,
            //         .y = mesh->mTextureCoords[vidx][0].y,
            //     };
            // }
            vertices[vidx] = (Vertex){
                // .pos =
                .pos =
                    (Vec3){
                        .x = mesh->mVertices[vidx].x,
                        .y = mesh->mVertices[vidx].y,
                        .z = mesh->mVertices[vidx].z,

                        // .x = vertices_sorted[vidx].x,
                        // .y = vertices_sorted[vidx].y,
                        // .z = vertices_sorted[vidx].z,
                    },
                .nrm =
                    (Vec3){
                        .x = mesh->mNormals[vidx].x,
                        .y = mesh->mNormals[vidx].y,
                        .z = mesh->mNormals[vidx].z,
                    },
                .uv = uv,

                // .tan =
                //     (Vec3){
                //         .x = mesh->mTangents[vidx].x,
                //         .y = mesh->mTangents[vidx].y,
                //         .z = mesh->mTangents[vidx].z,
                //     },
                // .bit =
                //     (Vec3){
                //         .x = mesh->mBitangents[vidx].x,
                //         .y = mesh->mBitangents[vidx].y,
                //         .z = mesh->mBitangents[vidx].z,
                //     },
            };
        }

        u32 idx_count = mesh->mNumFaces * 3;
        DEBUG_LOG(SCOPE_RUNE_REPO, "idx_count: %u", idx_count);
        u32* idxs = arena_alloc_aligned(arena, u32, idx_count);
        for (u32 fidx = 0; fidx < mesh->mNumFaces; fidx++) {
            assert(mesh->mFaces[fidx].mNumIndices == 3);
            memcpy(idxs + fidx * 3, mesh->mFaces[fidx].mIndices, sizeof(u32) * 3);

            // DEBUG_LOG(SCOPE_RUNE_REPO,"%u", mesh->mFaces[fidx].mIndices[0]);
            // DEBUG_LOG(SCOPE_RUNE_REPO,"%u", mesh->mFaces[fidx].mIndices[1]);
            // DEBUG_LOG(SCOPE_RUNE_REPO,"%u", mesh->mFaces[fidx].mIndices[2]);
        }

#ifdef VERIFY
        for (u32 iidx = 0; iidx < idx_count; iidx++) {
            u32 fidx  = iidx / 3;
            u32 fiidx = iidx - fidx * 3;
            // DEBUG_LOG(SCOPE_RUNE_REPO,"%u == %u", idxs[iidx], mesh->mFaces[fidx].mIndices[fiidx]);
            // DEBUG_LOG(SCOPE_RUNE_REPO,"%u / %u mod %u", iidx, fidx, fiidx);
            assert(idxs[iidx] == mesh->mFaces[fidx].mIndices[fiidx]);
        }
#endif

        _global_scratch = scratch;
        meshopt_setAllocator(_global_scratch_alloc, _global_scratch_free);

        u64                     cluster_count     = 0;
        struct meshopt_Meshlet* meshlets          = NULL;
        u32*                    meshlet_vertices  = NULL;
        u8*                     meshlet_triangles = NULL;

        // Clusterize
        {
            const u64 max_vertices  = 64;
            // const u64 max_vertices  = 126;
            const u64 min_triangles = 32;
            const u64 max_triangles = 126;
            const f32 fill_weight   = 0.5f;

            // TODO use a simple cube as input obj and figure out wtf these arrays contain
            size_t max_meshlets = meshopt_buildMeshletsBound(idx_count, max_vertices, min_triangles);
            meshlets            = arena_alloc_aligned(arena, struct meshopt_Meshlet, max_meshlets);
            meshlet_vertices    = arena_alloc_aligned(arena, u32, idx_count);
            meshlet_triangles   = arena_alloc_aligned(arena, u8, align_up_pow2(idx_count, 3));

            cluster_count = meshopt_buildMeshletsSpatial(meshlets,
                                                         meshlet_vertices,
                                                         meshlet_triangles,
                                                         idxs,
                                                         idx_count,
                                                         vertices[0].pos.el,
                                                         mesh->mNumVertices,
                                                         sizeof(Vertex),
                                                         max_vertices,
                                                         min_triangles,
                                                         max_triangles,
                                                         fill_weight);

            DEBUG_LOG(SCOPE_RUNE_REPO, "Constructed %lu clusters", cluster_count);
            scratch_reset(scratch);
        }

        // Optimize the clusters
        {
            u32 max_diff = 0;
            for (u32 cidx = 0; cidx < cluster_count; cidx++) {
                struct meshopt_Meshlet m = meshlets[cidx];
                meshopt_optimizeMeshlet(&meshlet_vertices[m.vertex_offset],
                                        &meshlet_triangles[m.triangle_offset],
                                        m.triangle_count,
                                        m.vertex_count);

                // DEBUG_LOG(SCOPE_RUNE_REPO,"MESHLET SHIT");
                //
                // DEBUG_LOG(SCOPE_RUNE_REPO,"Meshlet: %u", cidx);
                //
                // // DEBUG_LOG(SCOPE_RUNE_REPO,"meshlet_vertices");
                // // for (u32 idx = 0; idx < meshlet->vertex_count; idx++) {
                // //     DEBUG_LOG(SCOPE_RUNE_REPO,"v %u", meshlet_vertices[meshlet->vertex_offset + idx]);
                // // }
                //
                u32 min_idx = u32_MAX;
                u32 max_idx = 0;
                // DEBUG_LOG(SCOPE_RUNE_REPO,"meshlet_triangles");
                for (u32 idx = 0; idx < m.triangle_count * 3; idx++) {
                    // DEBUG_LOG(SCOPE_RUNE_REPO,"t %u", meshlet_triangles[meshlet->triangle_offset + idx]);
                    u32 vert_idx = meshlet_vertices[m.vertex_offset + meshlet_triangles[m.triangle_offset + idx]];
                    // DEBUG_LOG(SCOPE_RUNE_REPO,"t %u", vert_idx);

                    min_idx = min(vert_idx, min_idx);
                    max_idx = max(vert_idx, max_idx);
                }

                max_diff = max(max_diff, max_idx - min_idx);

                // DEBUG_LOG(SCOPE_RUNE_REPO,"min: %u max: %u diff: %u", min_idx, max_idx, max_idx - min_idx);
            }

            DEBUG_LOG(SCOPE_RUNE_REPO, "max diff: %u", max_diff);
        }

        Cluster* clusters           = arena_alloc_aligned(arena, Cluster, cluster_count);
        // u8*      clustered_indices  = arena_alloc_aligned(arena, u32, idx_count);
        u32*     clustered_vertices = arena_alloc_aligned(arena, u32, idx_count);
        // u32      idx_idx            = 0;
        u32      vtx_idx            = 0;
        u32      tri_count          = 0;
        for (u32 cidx = 0; cidx < cluster_count; cidx++) {
            struct meshopt_Meshlet* meshlet = &meshlets[cidx];

            assert(meshlet->vertex_count < u8_MAX);
            assert(meshlet->triangle_count < u8_MAX);

            clusters[cidx].vtx_count = (u8)meshlet->vertex_count;
            clusters[cidx].tri_count = (u8)meshlet->triangle_count;
            // clusters[cidx].idx_offset = idx_idx;
            // clusters[cidx].vtx_offset = vtx_idx;

            clusters[cidx].idx_offset = meshlet->triangle_offset;
            clusters[cidx].vtx_offset = meshlet->vertex_offset;

            struct meshopt_Bounds bounds = meshopt_computeMeshletBounds(meshlet_vertices + meshlet->vertex_offset,
                                                                        meshlet_triangles + meshlet->triangle_offset,
                                                                        meshlet->triangle_count,
                                                                        vertices->pos.el,
                                                                        mesh->mNumVertices,
                                                                        sizeof(Vertex));

            DEBUG_LOG(SCOPE_RUNE_REPO, "What %p %p", models, clusters);

            DEBUG_LOG(SCOPE_RUNE_REPO, "debuggering %p %u %p %u", models, idx, models[idx].clusters, cidx);

            // models[idx].clusters[cidx].bounds.center.x = bounds.center[0];
            // models[idx].clusters[cidx].bounds.center.y = bounds.center[1];
            // models[idx].clusters[cidx].bounds.center.z = bounds.center[2];
            clusters[cidx].bounds.radius = bounds.radius;

            clusters[cidx].bounds.cone_axis_s8[0] = bounds.cone_axis_s8[0];
            clusters[cidx].bounds.cone_axis_s8[1] = bounds.cone_axis_s8[1];
            clusters[cidx].bounds.cone_axis_s8[2] = bounds.cone_axis_s8[2];
            clusters[cidx].bounds.cone_cutoff_s8  = bounds.cone_cutoff_s8;

            tri_count += meshlet->triangle_count;

            // TODO pretty sure this is entirely useless and I can just copy in triangle_offset
            // for (u32 idx = 0; idx < meshlet->triangle_count * 3; idx++) {
            //     clustered_indices[idx_idx] = meshlet_triangles[meshlet->triangle_offset + idx];
            //     idx_idx++;
            // }

            for (u32 idx = 0; idx < meshlet->vertex_count; idx++) {
                clustered_vertices[vtx_idx] =
                    meshlet_vertices[meshlet->vertex_offset + meshlet_triangles[meshlet->triangle_offset + idx]];
                vtx_idx++;
            }
        }

        // assert(idx_idx == idx_count);
        // assert(vtx_idx == vtx_count);

        // Partition clusters together
        // {
        //
        //     const u64 partition_size = 4;
        //
        //     u32* cluster_partitions = arena_alloc_aligned(&arena, u32, cluster_count);
        //     u64  partition_count    = meshopt_partitionClusters(cluster_partitions,
        //                                                         ,
        //                                                         idx_count,
        //                                                         meshlet_vertices,
        //                                                         cluster_count,
        //                                                         vertices[0].pos.el,
        //                                                         mesh->mNumVertices,
        //                                                         sizeof(Vertex),
        //                                                         partition_size);
        // }

        Vertex* final_vertices = arena_alloc_aligned(arena, Vertex, vtx_idx);
        for (u32 idx = 0; idx < vtx_idx; idx++) {
            final_vertices[idx] = vertices[clustered_vertices[idx]];
        }

        models[idx].header.cls_offset = total_clusters;
        models[idx].header.vtx_offset = total_vertices;
        models[idx].header.idx_offset = total_indices;

        models[idx].header.materials_count = 1;
        models[idx].header.textures_count  = 0; // TODO figure this out

        models[idx].vertices         = final_vertices;
        models[idx].header.vtx_count = vtx_idx;

        models[idx].indices          = meshlet_triangles;
        // models[idx].indices            = clustered_indices;
        models[idx].header.idx_count = tri_count * 3; // TODO is this correct?

        models[idx].clusters         = clusters;
        models[idx].header.cls_count = (u32)cluster_count;

        // models[idx].clusters             = arena_alloc_aligned(arena, Cluster, cluster_count);
        // models[idx].header.cluster_count = (u32)cluster_count;

        // models[idx].header.cluster_count = 0;
        // models[idx].clusters             = NULL;

        // DEBUG
        {
            Model model = models[idx];
            VERBOSE_LOG(SCOPE_RUNE_REPO, "BEFORE");

            VERBOSE_LOG(SCOPE_RUNE_REPO, "materials_count %u", model.header.materials_count);
            VERBOSE_LOG(SCOPE_RUNE_REPO, "textures_count  %u", model.header.textures_count);

            VERBOSE_LOG(SCOPE_RUNE_REPO, "cluster_offset  %lu", model.header.cls_offset);
            VERBOSE_LOG(SCOPE_RUNE_REPO, "cluster_count   %u", model.header.cls_count);

            VERBOSE_LOG(SCOPE_RUNE_REPO, "vertex_offset   %lu", model.header.vtx_offset);
            VERBOSE_LOG(SCOPE_RUNE_REPO, "vertex_count    %u", model.header.vtx_count);

            VERBOSE_LOG(SCOPE_RUNE_REPO, "index_offset    %lu", model.header.idx_offset);
            VERBOSE_LOG(SCOPE_RUNE_REPO, "index_count     %u", model.header.idx_count);

            VERBOSE_LOG(SCOPE_RUNE_REPO, "mat_idx %u", model.header.mat_idx);
        }

        // models[idx].aabb_min = mesh->mAABB.mMin;
        // models[idx].aabb_max = mesh->mAABB.mMax;

        memcpy(&models[idx].header.aabb_min, &mesh->mAABB.mMin, sizeof(Vec3));
        memcpy(&models[idx].header.aabb_max, &mesh->mAABB.mMax, sizeof(Vec3));

        models[idx].header.mat_idx = mesh->mMaterialIndex;

        total_clusters += cluster_count;
        total_vertices += mesh->mNumVertices;
        total_indices += idx_count;

        // for (u32 cidx = 0; cidx < cluster_count; cidx++) {
        //     struct meshopt_Meshlet m = meshlets[cidx];
        //
        //     // meshlets;
        //     // meshlet_vertices;
        //     // meshlet_triangles;
        //
        //     // -
        //
        //     models[idx].clusters[cidx].vertex_offset   = m.vertex_offset;
        //     models[idx].clusters[cidx].triangle_offset = m.triangle_offset;
        //
        //     // TODO pretty sure this is unecessary?
        //     models[idx].clusters[cidx].vertex_count   = m.vertex_count;
        //     models[idx].clusters[cidx].triangle_count = m.triangle_count;
        //
        //     // -
        //
        //     struct meshopt_Bounds bounds = meshopt_computeMeshletBounds(meshlet_vertices + m.vertex_offset,
        //                                                                 meshlet_triangles + m.triangle_offset,
        //                                                                 m.triangle_count,
        //                                                                 vertices->pos.el,
        //                                                                 mesh->mNumVertices,
        //                                                                 sizeof(Vertex));
        //     // models[idx].clusters[cidx].bounds.center = bounds.center;
        //
        //     // FIX only because they are identical right now
        //     memcpy(&models[idx].clusters[cidx].bounds.center, &bounds, sizeof(struct meshopt_Bounds));
        // }
    }

    AssetRepo repo = {0};

    repo.models      = models;
    repo.model_count = model_count;

    repo.materials      = materials;
    repo.material_count = material_count;

    repo.textures      = textures;
    repo.texture_count = texture_count;

    // We're done. Release all resources associated with this import
    aiReleaseImport(scene);

    return repo;
}
