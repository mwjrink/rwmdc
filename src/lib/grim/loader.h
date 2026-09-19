#pragma once

#define _LARGEFILE64_SOURCE
#include <fcntl.h>

#include <lib/grim/assert.h>
#include <lib/grim/bp.h>
#include <lib/grim/gfx/graphics.h>
#include <lib/grim/math.h>
#include <lib/grim/mem/arena.h>
#include <lib/grim/mem/str.h>

// FIX TEMP
#include <lib/grim/gfx/internal_graphics.h>

#include <lib/cgltf.h>

typedef struct GlbHeader {
    u32 magic;
    u32 version;
    u32 length;
} GlbHeader;

typedef struct GlbChunkHeader {
    u32 length;
    u32 type;
} GlbChunkHeader;

typedef struct Scene {
    ModelUbo* model_ubos;
    u64       model_ubos_count;

    Renderable* renderables;
    u64         renderables_count;
} Scene;

static void* cgltf_arena_alloc(void* user, cgltf_size size) {
    // rop(rw Arena) arena = user;
    // return arena_alloc(arena, size);

    rop(rw ScratchArena) scratch = user;
    return scratch_alloc(scratch, size);
}

static void cgltf_arena_free(void* user, void* ptr) {
    // no op
}

// TODO don't use render_state here...
// TODO have a scene arena that we can reset when unloading the scene/switching scene?
Scene load_scene(rop(rw Arena) arena, rop(rw ScratchArena) scratch, Str path, rop(rw RenderState) render_state) {
    const char glb_ext[] = "glb";
    if (strncmp(glb_ext, path.data + path.len - sizeof(glb_ext), sizeof(glb_ext)) != 0) {
        CRITICAL_LOG(SCOPE_LOAD, "Unsupported scene type to load.");
        exit(1);
    }

    // i32 scene_fd = open(path.data, O_RDONLY | O_LARGEFILE);
    i32 scene_fd = open(path.data, O_RDONLY);
    if (scene_fd == -1) {
        CRITICAL_LOG(SCOPE_LOAD, "Failed to open scene file.");
        exit(1);
    }

    GlbHeader header             = {0};
    i64       read_header_result = read(scene_fd, &header, sizeof(header));
    if (read_header_result != sizeof(header)) {
        CRITICAL_LOG(SCOPE_LOAD, "Failed to read scene file header.");
        exit(1);
    }

    if (header.magic != 0x46546C67) {
        CRITICAL_LOG(SCOPE_LOAD, "Magic number check failed when loading scene file.");
        exit(1);
    }

    if (header.version != 2) {
        CRITICAL_LOG(SCOPE_LOAD, "Version check failed when loading scene file.");
        exit(1);
    }

    u64 scene_file_size = header.length;
    VERBOSE_LOG(SCOPE_LOAD, "File size: %ld bytes\n", scene_file_size);

    void* scene_mmap = mmap(NULL, scene_file_size, PROT_READ, MAP_PRIVATE | MAP_FILE | MAP_POPULATE, scene_fd, 0);
    if (scene_mmap == MAP_FAILED) {
        CRITICAL_LOG(SCOPE_LOAD, "Failed to mmap file: %i.", errno);
        exit(1);
    }
    madvise(scene_mmap, PAGE_SIZE, MADV_WILLNEED);
    madvise(scene_mmap, PAGE_SIZE, MADV_SEQUENTIAL);

    u64 start = time_start();

    cgltf_options options = {0};
    // options.type          = cgltf_file_type_glb;
    // options.json_token_count = json_token_count;
    options.memory        = (cgltf_memory_options){
        .user_data  = scratch,
        .alloc_func = cgltf_arena_alloc,
        .free_func  = cgltf_arena_free,
    };

    cgltf_data*  data   = NULL;
    // cgltf_result  result  = cgltf_parse(&options, json.data, json.len, &data);
    cgltf_result result = cgltf_parse(&options, scene_mmap, scene_file_size, &data);

    // TODO I don't love the whole allocDynList thing here... maybe just pointers?
    // Or make this an Array type that just contains the data* len and cap?
    // Currently, these don't grow but that might be a feature later.
    AllocDynList model_ubos  = {0};
    AllocDynList renderables = {0};
    AllocDynList materials   = {0};
    AllocDynList textures    = {0};
    if (result == cgltf_result_success) {
        // DEBUG_LOG(SCOPE_LOAD, "Meshes: %lu, nodes: %lu, acc: %lu", data->meshes_count, data->nodes_count,
        // data->accessors_count); model_ubos  = alloc_list_create_with_cap(arena, sizeof(ModelUbo),
        // (u32)data->meshes_count); renderables = alloc_list_create_with_cap(arena, sizeof(Renderable),
        // (u32)data->nodes_count);

        textures = alloc_list_create_with_cap(arena, sizeof(Image), (u32)data->textures_count);
        DEBUG_LOG(SCOPE_LOAD, "Textures count: %lu", data->textures_count);
        for (u32 tidx = 0; tidx < data->textures_count; tidx++) {
            cgltf_texture texture  = data->textures[tidx];
            rop(ro void) image_mem = data->bin + texture.image->buffer_view->offset;
            Image image =
                image_load_texture_mem(arena, render_state->r_ctx->ctx, image_mem, texture.image->buffer_view->size);

            alloc_list_push(arena, &textures, &image);
        }

        materials = alloc_list_create_with_cap(arena, sizeof(Material), (u32)data->materials_count);
        DEBUG_LOG(SCOPE_LOAD, "Materials count: %lu", data->materials_count);
        for (u32 midx = 0; midx < data->materials_count; midx++) {
            cgltf_material               material = data->materials[midx];
            cgltf_pbr_metallic_roughness pbr_mr   = material.pbr_metallic_roughness;

            u32 metallic_roughness_texture_idx = u32_MAX;
            u32 base_color_texture_idx         = u32_MAX;
            u32 normal_texture_idx             = u32_MAX;
            DEBUG_LOG(SCOPE_LOAD, "textures %p %lu", data->textures, data->textures_count);
            if (pbr_mr.metallic_roughness_texture.texture != NULL) {
                DEBUG_LOG(SCOPE_LOAD, "metallic_roughness_texture_idx %p", pbr_mr.metallic_roughness_texture.texture);
                metallic_roughness_texture_idx =
                    (u32)cgltf_texture_index(data, pbr_mr.metallic_roughness_texture.texture);
            }

            if (pbr_mr.base_color_texture.texture != NULL) {
                DEBUG_LOG(SCOPE_LOAD, "base_color_texture_idx %p", pbr_mr.base_color_texture.texture);
                base_color_texture_idx = (u32)cgltf_texture_index(data, pbr_mr.base_color_texture.texture);
            }

            // if (material.normal_texture.texture != NULL) {
            //     DEBUG_LOG(SCOPE_LOAD, "normal_texture_idx %p", material.normal_texture.texture);
            //     normal_texture_idx = (u32)cgltf_texture_index(data, material.normal_texture.texture);
            // }

            Material parsed_mat = (Material){
                .metallic  = pbr_mr.metallic_factor,
                .roughness = pbr_mr.roughness_factor,

                .metallic_roughness_texture_idx = metallic_roughness_texture_idx,

                .base_color             = (Vec4){.r = 1.0f, .a = 1.0f},
                .base_color_texture_idx = base_color_texture_idx,

                .normal_texture_idx = normal_texture_idx,
            };
            alloc_list_push(arena, &materials, &parsed_mat);
        }

        // FIX refactor this to loop over meshes, not nodes.
        // model_ubos = alloc_list_create_with_cap(arena, sizeof(ModelUbo), (u32)data->nodes_count);
        // renderables = alloc_list_create_with_cap(arena, sizeof(Renderable), (u32)data->meshes_count);
        // for (u32 nidx = 0; nidx < data->meshes_count; nidx++) {
        //     //
        // }

        // FIX this is a hack because multiple primitives
        model_ubos  = alloc_list_create_with_cap(arena, sizeof(ModelUbo), 4096);
        renderables = alloc_list_create_with_cap(arena, sizeof(Renderable), 4096);
        for (u32 nidx = 0; nidx < data->nodes_count; nidx++) {
            cgltf_node node = data->nodes[nidx];

            if (node.mesh != NULL) {
                if (nidx == 0) {
                    DEBUG_LOG(SCOPE_LOAD, "Mesh name: %s", node.mesh->name);
                }
                for (u32 pidx = 0; pidx < node.mesh->primitives_count; pidx++) {
                    cgltf_accessor* pos_acc = NULL;
                    cgltf_accessor* uv_acc  = NULL;
                    cgltf_accessor* idx_acc = node.mesh->primitives[pidx].indices;
                    u32             mat_idx = (u32)cgltf_material_index(data, node.mesh->primitives[pidx].material);
                    for (u32 aidx = 0; aidx < node.mesh->primitives[pidx].attributes_count; aidx++) {
                        // node.mesh->primitives[pidx].attributes[aidx].index;
                        // node.mesh->primitives[pidx].attributes[aidx].data;

                        switch (node.mesh->primitives[pidx].attributes[aidx].type) {
                            case cgltf_attribute_type_position: {
                                pos_acc = node.mesh->primitives[pidx].attributes[aidx].data;
                            } break;
                            case cgltf_attribute_type_texcoord: {
                                uv_acc = node.mesh->primitives[pidx].attributes[aidx].data;
                            } break;
                                // cgltf_attribute_type_normal,
                                // cgltf_attribute_type_tangent,
                            default: {
                            } break;
                        }
                    }
                    // DEBUG_LOG(SCOPE_LOAD, "PRIM: %u %p %p %p", pidx, pos_acc, uv_acc, idx_acc);

                    // if (nidx == 0) {
                    //     DEBUG_LOG(SCOPE_LOAD, "pos count: %lu", pos_acc->count);
                    //     DEBUG_LOG(SCOPE_LOAD, "uv count: %lu", uv_acc->count);
                    //     DEBUG_LOG(SCOPE_LOAD, "idx count: %lu", idx_acc->count);
                    //     //     "attributes": {
                    //     //       "POSITION": 0,
                    //     //       "NORMAL": 1,
                    //     //       "TEXCOORD_0": 2,
                    //     //       "TEXCOORD_1": 3
                    //     //     },
                    //     //     "indices": 4,
                    //     //     "material": 0
                    // }

                    u64       vertices_size = sizeof(Vertex) * pos_acc->count;
                    Vertex*   vertices      = scratch_alloc(scratch, vertices_size);
                    AllocBuff verts         = {
                        .data = vertices,
                        .len  = vertices_size,
                    };

                    assert(SCOPE_LOAD, pos_acc->count == uv_acc->count);
                    assert(SCOPE_LOAD, sizeof(Vec3) * pos_acc->count == pos_acc->buffer_view->size);
                    assert(SCOPE_LOAD, sizeof(Vec2) * uv_acc->count == uv_acc->buffer_view->size);
                    assert(SCOPE_LOAD, pos_acc->stride == sizeof(Vec3));
                    assert(SCOPE_LOAD, uv_acc->stride == sizeof(Vec2));
                    rwp(ro void) _pos_ptr = data->bin + pos_acc->buffer_view->offset + pos_acc->offset;
                    rwp(ro void) _uv_ptr  = data->bin + uv_acc->buffer_view->offset + uv_acc->offset;
                    for (u32 vidx = 0; vidx < pos_acc->count; vidx++) {
                        const Vec3* pos_ptr = _pos_ptr;
                        const Vec2* uv_ptr  = _uv_ptr;

                        vertices[vidx] = (Vertex){
                            .pos = *pos_ptr,
                            .nrm = vec3_unit_x(),
                            .uv  = *uv_ptr,
                        };

                        _pos_ptr += pos_acc->stride;
                        _uv_ptr += uv_acc->stride;

                        // if (nidx == 0) {
                        //     // dump_vec3(vertices[vidx].position);
                        //     // dump_vec2(vertices[vidx].tex_coord);
                        // }
                    };

                    Mat3 model_to_world = mat3_identity();
                    Vec3 model_offset   = {0};
                    if (node.has_translation) {
                        memcpy(model_offset.el, node.translation, sizeof(node.translation));
                    }

                    if (node.has_rotation) {
                        Rotor3 rotation = rotor3_from_quaternion(node.rotation);
                        Mat3   rot_mat  = rotor3_to_mat3(rotation);
                        model_to_world  = rot_mat;
                    }

                    if (node.has_scale) {
                        Mat3 scale_mat = mat3_identity();
                        scale_mat.c0_r0 *= node.scale[0];
                        scale_mat.c1_r1 *= node.scale[1];
                        scale_mat.c2_r2 *= node.scale[2];

                        mat3_mul_ip(&model_to_world, scale_mat);
                    }

                    if (node.has_matrix) {
                        Mat4 dst_mat = {0};
                        memcpy(dst_mat.el, node.matrix, sizeof(node.matrix));
                        DEBUG_LOG(SCOPE_LOAD, "We have matrix!");
                        model_to_world = mat4_to_mat3(dst_mat);
                        model_offset   = dst_mat.col3.xyz;
                    }

                    ModelUbo ubo = {
                        .model_to_world = model_to_world,
                        .model_offset   = model_offset,
                    };

                    // TODO pos_acc->min & max give us AABB
                    switch (idx_acc->component_type) {
                        case cgltf_component_type_r_16u: {
                            assert(SCOPE_LOAD, idx_acc->stride == sizeof(u16));

                            u64 indices_size = sizeof(u16) * idx_acc->count;
                            assert(SCOPE_LOAD, indices_size == idx_acc->buffer_view->size);
                            assert(SCOPE_LOAD, idx_acc->offset == 0);
                            AllocBuff idxs = {
                                .data = (void*)(data->bin + idx_acc->buffer_view->offset + idx_acc->offset),
                                .len  = idx_acc->count,
                            };

                            Renderable renderable     = load_model(render_state, verts, idxs, mat_idx);
                            renderable.instance_count = 1;
                            alloc_list_push(arena, &renderables, &renderable);
                        } break;
                        case cgltf_component_type_r_32u: {
                            assert(SCOPE_LOAD, idx_acc->stride == sizeof(u32));

                            u64 indices_size = sizeof(u32) * idx_acc->count;
                            assert(SCOPE_LOAD, indices_size == idx_acc->buffer_view->size);
                            assert(SCOPE_LOAD, idx_acc->offset == 0);
                            AllocBuff idxs = {
                                .data = (void*)(data->bin + idx_acc->buffer_view->offset + idx_acc->offset),
                                .len  = idx_acc->count,
                            };

                            Renderable renderable     = load_model_idx_u32(render_state, verts, idxs, mat_idx);
                            renderable.instance_count = 1;
                            alloc_list_push(arena, &renderables, &renderable);
                        } break;
                        default: {
                        } break;
                    }

                    void* model_ubo = alloc_list_push(arena, &model_ubos, &ubo);
                    (void)model_ubo;

                    scratch_pop_to(scratch, vertices);
                }
            }

            // For now, ignore parent transform
        }

        upload_materials(render_state, materials.data, materials.len);
        upload_textures(arena, render_state, textures.data, textures.len);

        // {
        //     i32 out_fd = open("assets/BistroExterior2.gltf", O_WRONLY | O_CREAT);
        //     write(out_fd, data->json, data->json_size);
        //
        //     close(out_fd);
        // }

        // TODO pretty sure we don't need to call this because we scratch reset everything
        // asan seems to agree
        cgltf_free(data);
    } else {
        CRITICAL_LOG(SCOPE_LOAD, "Failed to parse gltf.");
        exit(1);
    }

    time_end(start);

    scratch_reset(scratch);

    // TODO munmap somewhere...
    munmap(scene_mmap, scene_file_size);
    close(scene_fd);

    return (Scene){
        .model_ubos        = model_ubos.data,
        .model_ubos_count  = model_ubos.len,
        .renderables       = renderables.data,
        .renderables_count = renderables.len,
    };
}
