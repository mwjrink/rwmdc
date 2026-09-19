#pragma once

#include <lib/grim/gfx/graphics.h>
#include <lib/grim/math.h>
#include <lib/grim/mem/arena.h>

#include <models.h>

// DESIGN
// - Stepping forward, we pick a location on the ground in the direction we're going and move foot to there
// - Look in cardioid pattern? this makes it so the feet don't cross each-other?
// - The hard part here is we need to move the foot to that location, ie find a path there from the current position
//     - Is this a spline we follow or is it more natural to move the angles instead?
//     - Also need to be able to step over things
//         - constraint on other foot to be at a y when at a z?
//     - Do we also put forces in here?
//     - Essentialy this just becomes a physics solver then, right?
// - Enhancement, inertia & force/stretch
//     - It is hard to extend some joints while others are extended, ie walk on only balls of feet
//     - Inertia for hands swaying as walking etc

// typedef Mat4 Tensor;

typedef enum AnimTask {
    Posture,
    MoveTo,
    MaintainLocation,
    CenterOfMass,
} AnimTask;

typedef struct AnimConstraint {
    // One example is that the location obtained by going from root to bone is xyz
    // Rotation remains between ra and rb
} AnimConstraint;

typedef struct Joint {
    u32    parent; // should this be an i32, offset from this one and keep them contiguous?
    f32    length; // length constraint from parent
    Rotor3 base;

    // If I want dynamic flexibility, I need a flex_weight for each axis
    // Ex if you have a flex weight of 45 then the joint can move an extra 45 in each dir?
    //     - for splits, outwards, this makes sense. Inwards... not so much
    f32 min_xy;
    f32 max_xy;

    f32 min_xz;
    f32 max_xz;

    f32 min_yz;
    f32 max_yz;
} Joint;
STATIC_ASSERT(sizeof(Joint) == 48);

// How do I get a foot specifically?
// typedef struct HumanoidArmature {
//     Joint*  joints;
//     Rotor3* state;
//     Rotor3* goal;
//     u32     joints_count;
//     u32     _padding; // generation number? for updating gpu data?
// } HumanoidArmature;
// STATIC_ASSERT(sizeof(HumanoidArmature) == 32);

typedef struct Armature {
    Joint*  joints;
    Rotor3* state;
    Rotor3* goal;
    u32     joints_count;
    u32     _padding; // generation number? for updating gpu data?
} Armature;
STATIC_ASSERT(sizeof(Armature) == 32);

typedef struct AnimContext {
    // FIX this is temporary
    struct {
        u32 root       : 4;
        u32 upper_back : 4;
        u32 lower_back : 4;
        u32 _padding   : 4;
    } core;
    struct {
        struct {
            u32 clavicle : 4;
            u32 shoulder : 4;
            u32 elbow    : 4;
            u32 wrist    : 4;
        } arm;
        struct {
            u32 thigh : 4;
            u32 knee  : 4;
            u32 ankle : 4;
            u32 toes  : 4;
        } leg;
    } left;
    struct {
        struct {
            u32 clavicle : 4;
            u32 shoulder : 4;
            u32 elbow    : 4;
            u32 wrist    : 4;
        } arm;
        struct {
            u32 thigh : 4;
            u32 knee  : 4;
            u32 ankle : 4;
            u32 toes  : 4;
        } leg;
    } right;
} AnimContext;
STATIC_ASSERT(sizeof(AnimContext) == 20);

typedef struct AnimState {
    Armature  armature;
    ModelUbo* model_ubos; // can this just be a slice into the rebar/mapped mem?

    f32 time;
    struct {
        u32 anim_left_right : 1;
        u32 ready           : 1;
        u32 _padding        : 30;
    };

    // TODO eventually when multiple characters?
    // Or is one AnimState per Char/Entity?
    // Armature* armatures;
    // u32       armatures_count;
} AnimState;
STATIC_ASSERT(sizeof(AnimState) == 48);

AnimContext anim_ctx_create() {
    return (AnimContext){};
}

// TODO separate functions or an enum to create human vs other
AnimState anim_create_state(rop(rw Arena) arena,
                            rop(rw AnimContext) anim_ctx,
                            rop(rw RenderState) render_state,
                            rop(rw AllocDynList) model_ubos,
                            rop(rw AllocDynList) renderables) {
    Armature  armature         = {0};
    ModelUbo* model_ubos_start = model_ubos->data;
    model_ubos_start += model_ubos->len;

    armature.joints_count = 20;
    armature.joints       = arena_alloc_aligned(arena, Joint, armature.joints_count);
    armature.state        = arena_alloc_aligned(arena, Rotor3, armature.joints_count);
    armature.goal         = arena_alloc_aligned(arena, Rotor3, armature.joints_count);

    u32 j  = 0;
    f32 hu = 0.244f; // head unit is 24.4 cm

    // TODO Do I need a root bone?
    // Root bone
    armature.joints[j].parent = u32_MAX;
    armature.joints[j].length = 0.0f;
    armature.joints[j].base   = rotor3_from_euler_angles(0.0f, rad(-90.0f), 0.0f);
    anim_ctx->core.root       = j;
    j++;

    // Upper back
    {
        armature.joints[j].parent = 0;
        armature.joints[j].length = 1.2f;
        armature.joints[j].base   = rotor3_from_euler_angles(0.0f, 0.0f, 0.0f);

        armature.joints[j].min_xy = rad(-25.0f);
        armature.joints[j].max_xy = rad(25.0f);

        armature.joints[j].min_xz = rad(0.0f);
        armature.joints[j].max_xz = rad(0.0f);

        armature.joints[j].min_yz = rad(-10.0f);
        armature.joints[j].max_yz = rad(10.0f);

        anim_ctx->core.upper_back = j;

        j++;
    }

    // Right Clavicle
    {
        armature.joints[j].parent = 1;
        armature.joints[j].length = 0.85f;
        armature.joints[j].base   = rotor3_from_euler_angles(rad(90.0f), 0.0f, rad(90.0f));

        armature.joints[j].min_xy = rad(0.0f);
        armature.joints[j].max_xy = rad(0.0f);

        armature.joints[j].min_xz = rad(-5.0f);
        armature.joints[j].max_xz = rad(10.0f);

        armature.joints[j].min_yz = rad(0.0f);
        armature.joints[j].max_yz = rad(0.0f);

        anim_ctx->right.arm.clavicle = j;

        j++;
    }

    // Right Shoulder
    {
        armature.joints[j].parent = 2;
        armature.joints[j].length = 1.4f;
        armature.joints[j].base   = rotor3_from_euler_angles(0.0f, 0.0f, 0.0f);

        armature.joints[j].min_xy = rad(-90.0f);
        armature.joints[j].max_xy = rad(90.0f);

        armature.joints[j].min_xz = rad(-90.0f);
        armature.joints[j].max_xz = rad(135.0f);

        armature.joints[j].min_yz = rad(-90.0f);
        armature.joints[j].max_yz = rad(90.0f);

        anim_ctx->right.arm.shoulder = j;

        j++;
    }

    // Right Elbow
    {
        armature.joints[j].parent = 3;
        armature.joints[j].length = 0.9f;
        armature.joints[j].base   = rotor3_from_euler_angles(0.0f, 0.0f, rad(90.0f));

        armature.joints[j].min_xy = rad(-45.0f);
        armature.joints[j].max_xy = rad(120.0f);

        armature.joints[j].min_xz = rad(0.0f);
        armature.joints[j].max_xz = rad(140.0f);

        armature.joints[j].min_yz = rad(0.0f);
        armature.joints[j].max_yz = rad(0.0f);

        anim_ctx->right.arm.elbow = j;

        j++;
    }

    // Left Clavicle
    {
        armature.joints[j].parent = 1;
        armature.joints[j].length = 0.85f;
        // I could make the length positive and flip the angle & min/maxes?
        armature.joints[j].base   = rotor3_from_euler_angles(rad(-90.0f), 0.0f, rad(-90.0f));

        armature.joints[j].min_xy = rad(0.0f);
        armature.joints[j].max_xy = rad(0.0f);

        armature.joints[j].min_xz = rad(-5.0f);
        armature.joints[j].max_xz = rad(10.0f);

        armature.joints[j].min_yz = rad(0.0f);
        armature.joints[j].max_yz = rad(0.0f);

        anim_ctx->left.arm.clavicle = j;

        j++;
    }

    // Left Shoulder
    {
        armature.joints[j].parent = 5;
        armature.joints[j].length = 1.4f; // negative length?
        armature.joints[j].base   = rotor3_from_euler_angles(0.0f, 0.0f, 0.0f);

        armature.joints[j].min_xy = rad(90.0f);
        armature.joints[j].max_xy = rad(-90.0f);

        armature.joints[j].min_xz = rad(90.0f);
        armature.joints[j].max_xz = rad(-135.0f);

        armature.joints[j].min_yz = rad(90.0f);
        armature.joints[j].max_yz = rad(-90.0f);

        anim_ctx->left.arm.shoulder = j;

        j++;
    }

    // Left Elbow
    {
        armature.joints[j].parent = 6;
        armature.joints[j].length = 0.9f;
        armature.joints[j].base   = rotor3_from_euler_angles(0.0f, 0.0f, rad(90.0f));

        armature.joints[j].min_xy = rad(45.0f);
        armature.joints[j].max_xy = rad(-120.0f);

        armature.joints[j].min_xz = rad(0.0f);
        armature.joints[j].max_xz = rad(-140.0f);

        armature.joints[j].min_yz = rad(0.0f);
        armature.joints[j].max_yz = rad(0.0f);

        anim_ctx->left.arm.elbow = j;

        j++;
    }

    // Lower back
    {
        armature.joints[j].parent = 0;
        armature.joints[j].length = 1.2f;
        armature.joints[j].base   = rotor3_from_euler_angles(0.0f, rad(180.0f), rad(-180.0f));

        armature.joints[j].min_xy = rad(-25.0f);
        armature.joints[j].max_xy = rad(25.0f);

        armature.joints[j].min_xz = rad(0.0f);
        armature.joints[j].max_xz = rad(0.0f);

        armature.joints[j].min_yz = rad(-10.0f);
        armature.joints[j].max_yz = rad(10.0f);

        anim_ctx->core.lower_back = j;

        j++;
    }

    // Right hip
    {
        armature.joints[j].parent = 8;
        armature.joints[j].length = 0.6f;
        armature.joints[j].base   = rotor3_from_euler_angles(rad(-90.0f), 0.0f, rad(-90.0f));

        armature.joints[j].min_xy = rad(0.0f);
        armature.joints[j].max_xy = rad(0.0f);

        armature.joints[j].min_xz = rad(0.0f);
        armature.joints[j].max_xz = rad(0.0f);

        armature.joints[j].min_yz = rad(0.0f);
        armature.joints[j].max_yz = rad(0.0f);

        j++;
    }

    // Right Thigh:
    {
        armature.joints[j].parent = 9;
        armature.joints[j].length = 1.9f;
        armature.joints[j].base   = rotor3_from_euler_angles(0.0f, rad(90.0f), rad(-90.0f));

        armature.joints[j].min_xy = rad(-45.0f);
        armature.joints[j].max_xy = rad(0.0f);

        // 90.0f if you can do the splits
        armature.joints[j].min_xz = rad(-45.0f);
        armature.joints[j].max_xz = rad(60.0f);

        armature.joints[j].min_yz = rad(-135.0f);
        armature.joints[j].max_yz = rad(5.0f);

        anim_ctx->right.leg.thigh = j;

        j++;
    }

    // Right Knee:
    {
        armature.joints[j].parent = 10;
        armature.joints[j].length = 1.8f;
        armature.joints[j].base   = rotor3_from_euler_angles(0.0f, 0.0f, rad(180.0f));

        armature.joints[j].min_xy = rad(-10.0f);
        armature.joints[j].max_xy = rad(10.0f);

        armature.joints[j].min_xz = rad(0.0f);
        armature.joints[j].max_xz = rad(0.0f);

        armature.joints[j].min_yz = rad(0.0f);
        armature.joints[j].max_yz = rad(-110.0f);

        anim_ctx->right.leg.knee = j;

        j++;
    }

    // Right Ankle:
    {
        armature.joints[j].parent = 11;
        armature.joints[j].length = 1.15f;
        armature.joints[j].base   = rotor3_from_euler_angles(0.0f, rad(90.0f), rad(180.0f));

        armature.joints[j].min_xy = rad(-45.0f);
        armature.joints[j].max_xy = rad(45.0f);

        armature.joints[j].min_xz = rad(-35.0f);
        armature.joints[j].max_xz = rad(15.0f);

        armature.joints[j].min_yz = rad(-30.0f);
        armature.joints[j].max_yz = rad(75.0f);

        anim_ctx->right.leg.ankle = j;

        j++;
    }

    // Left hip
    {
        armature.joints[j].parent = 8;
        armature.joints[j].length = 0.6f;
        armature.joints[j].base   = rotor3_from_euler_angles(rad(90.0f), 0.0f, rad(90.0f));

        armature.joints[j].min_xy = rad(0.0f);
        armature.joints[j].max_xy = rad(0.0f);

        armature.joints[j].min_xz = rad(0.0f);
        armature.joints[j].max_xz = rad(0.0f);

        armature.joints[j].min_yz = rad(0.0f);
        armature.joints[j].max_yz = rad(0.0f);

        j++;
    }

    // Left Hip/Thigh:
    {
        armature.joints[j].parent = 13;
        armature.joints[j].length = 1.9f;
        armature.joints[j].base   = rotor3_from_euler_angles(0.0f, rad(90.0f), rad(90.0f));

        armature.joints[j].min_xy = rad(-45.0f);
        armature.joints[j].max_xy = rad(0.0f);

        // 90.0f if you can do the splits
        armature.joints[j].min_xz = rad(-45.0f);
        armature.joints[j].max_xz = rad(60.0f);

        armature.joints[j].min_yz = rad(-135.0f);
        armature.joints[j].max_yz = rad(5.0f);

        anim_ctx->left.leg.thigh = j;

        j++;
    }

    // Left Knee:
    {
        armature.joints[j].parent = 14;
        armature.joints[j].length = 1.8f;
        armature.joints[j].base   = rotor3_from_euler_angles(0.0f, 0.0f, rad(180.0f));

        armature.joints[j].min_xy = rad(-10.0f);
        armature.joints[j].max_xy = rad(10.0f);

        armature.joints[j].min_xz = rad(0.0f);
        armature.joints[j].max_xz = rad(0.0f);

        armature.joints[j].min_yz = rad(0.0f);
        armature.joints[j].max_yz = rad(-110.0f);

        anim_ctx->left.leg.knee = j;

        j++;
    }

    // Left Ankle:
    {
        armature.joints[j].parent = 15;
        armature.joints[j].length = 1.15f;
        armature.joints[j].base   = rotor3_from_euler_angles(0.0f, rad(90.0f), rad(180.0f));

        armature.joints[j].min_xy = rad(-45.0f);
        armature.joints[j].max_xy = rad(45.0f);

        armature.joints[j].min_xz = rad(-35.0f);
        armature.joints[j].max_xz = rad(15.0f);

        armature.joints[j].min_yz = rad(-30.0f);
        armature.joints[j].max_yz = rad(75.0f);

        anim_ctx->left.leg.ankle = j;

        j++;
    }

    armature.joints_count = j;

    AllocBuff cube_verts = {
        .data = &joint_ptr_vertices,
        .len  = sizeof(joint_ptr_vertices) / sizeof(Vertex),
    };
    AllocBuff cube_idxs = {
        .data = &joint_ptr_indices,
        .len  = sizeof(joint_ptr_indices) / sizeof(u16),
    };
    // Model cube_model = {
    //
    //     .header =
    //         (OutFileModel){
    //             .materials_count =,
    //             .textures_count  =,
    //
    //             .vtx_offset =,
    //             .idx_offset =,
    //             .tri_offset =,
    //             .cls_offset =,
    //
    //             .vtx_count =,
    //             .idx_count =,
    //             .tri_count =,
    //             .cls_count =,
    //
    //             .mat_idx =,
    //
    //             .aabb_min =,
    //             .aabb_max =,
    //         },
    //
    //     .vertices = &joint_ptr_vertices,
    //     .indices  = &joint_ptr_indices,
    //     .tris     = tris,
    //     .clusters = &cluster,
    // };
    // Renderable cube     = load_model(render_state, &cube_model);
    // cube.instance_count = 1;
    //
    // Mat3     identity = mat3_identity();
    // ModelUbo ubo      = {
    //     .model_to_world = identity,
    //     .model_offset   = {.x = 0.0f, .y = 2.0f, .z = 0.0f},
    // };
    //
    // for (u32 idx = 0; idx < armature.joints_count; idx++) {
    //     armature.joints[idx].length *= hu;
    //     armature.state[idx] = rotor3_identity();
    //     armature.goal[idx]  = rotor3_identity();
    //
    //     // TEMP
    //     cube.mat_idx = u32_MAX - (idx % 13);
    //     alloc_list_push(arena, renderables, &cube);
    //     alloc_list_push(arena, model_ubos, &ubo);
    // }
    //
    // return (AnimState){
    //     .armature   = armature,
    //     .model_ubos = model_ubos_start,
    // };
}

void anim_update(rop(rw ScratchArena) scratch, rop(rw AnimState) anim_state, f32 dt) {
    u32 ready = true;
    anim_state->time += dt;
    Mat4* finalized = scratch_alloc_aligned(scratch, Mat4, anim_state->armature.joints_count);
    for (u32 idx = 0; idx < anim_state->armature.joints_count; idx++) {
        Rotor3 current = anim_state->armature.state[idx];
        Rotor3 goal    = anim_state->armature.goal[idx];
        Rotor3 final   = current;

        // Rotor3 min_rot = rotor3_from_euler_angles(anim_state->armature.joints[idx].min_xz,
        //                                           anim_state->armature.joints[idx].min_yz,
        //                                           anim_state->armature.joints[idx].min_xy);
        //
        // Rotor3 max_rot = rotor3_from_euler_angles(anim_state->armature.joints[idx].max_xz,
        //                                           anim_state->armature.joints[idx].max_yz,
        //                                           anim_state->armature.joints[idx].max_xy);

        // if (fmodf(anim_state->time, 4.0f) > 2.0f) {
        //     rotor3_glerp_ip(&final, max_rot, 2.0f, dt);
        // } else {
        //     rotor3_glerp_ip(&final, min_rot, 2.0f, dt);
        // }

        // TODO this is NOT a glerp, it's an interpolation to a target
        rotor3_glerp_shortest_ip(&final, goal, 2.0f, dt);
        f32 how_close = rotor3_dot(final, goal);
        // f32 how_close = rotor3_move_towards_ip(&final, goal, dt);
        rotor3_normalize_ip(&final);

        // ensure all joints are at their targets
        if (how_close < 0.99f) {
            ready = false;
        }

        Mat3 rotation        = rotor3_to_mat3(final);
        Mat4 final_transform = mat3_to_mat4(rotation);

        // TODO todo
        // Mat3 base3      = rotor3_to_mat3(anim_state->armature.joints[idx].base);
        // Mat4 base       = mat3_to_mat4(base3);
        // Mat4 translate  = mat4_identity();
        // translate.c3_r0 = 0.0f;
        // translate.c3_r1 = 0.0f;
        // translate.c3_r2 = anim_state->armature.joints[idx].length;
        //
        // Mat4 parent_transform;
        // if (unlikely(anim_state->armature.joints[idx].parent == u32_MAX)) {
        //     parent_transform = mat4_identity();
        // } else {
        //     parent_transform = finalized[anim_state->armature.joints[idx].parent];
        //     anim_state->model_ubos[idx].world_translation =
        //         anim_state->model_ubos[anim_state->armature.joints[idx].parent].world_translation;
        // }
        //
        // Mat4 model_to_world = mat4_mul_var((Mat4[]){parent_transform, base, final_transform, translate}, 4);
        // anim_state->model_ubos[idx].model_to_world = mat4_to_mat3(model_to_world);
        // anim_state->model_ubos[idx].world_translation   = model_to_world.col3.xyz;
        // anim_state->armature.state[idx]            = final;
        // finalized[idx]                             = model_to_world;

        // DEBUG_LOG("REAL pos: %u", idx);
        // dump_vec3(model_to_world.col3.xyz);
    }

    anim_state->ready = ready;
}

// The lock position constraint could return a spline or something where the root bone could be?
// Some sort of 3d shape?

void anim_walk_forwards(rop(rw ScratchArena) scratch, rop(ro AnimContext) anim_ctx, rop(rw AnimState) anim_state) {
    Rotor3* current_state = scratch_alloc_aligned(scratch, Rotor3, anim_state->armature.joints_count);
    for (u32 idx = 0; idx < anim_state->armature.joints_count; idx++) {
        current_state[idx] = anim_state->armature.state[idx];
    }

    // Mat4*   finalized           = scratch_alloc_aligned(scratch, Mat4, anim_state->armature.joints_count);
    Rotor3* cumulative_rotation = scratch_alloc_aligned(scratch, Rotor3, anim_state->armature.joints_count);
    Vec3*   relative_positions  = scratch_alloc_aligned(scratch, Vec3, anim_state->armature.joints_count);

    cumulative_rotation[0] = anim_state->armature.joints[0].base;
    for (u32 idx = 1; idx < anim_state->armature.joints_count; idx++) {
        // current_pos += vec3_unit_z() * anim_state->armature.joints[idx].length;

        u32 pidx                 = anim_state->armature.joints[idx].parent;
        cumulative_rotation[idx] = rotor3_mul(cumulative_rotation[pidx], anim_state->armature.joints[idx].base);
        Vec3 translation         = vec3_rotated_by_rotor3(vec3_unit_z(), cumulative_rotation[idx]);
        vec3_mul_f32_ip(&translation, anim_state->armature.joints[idx].length);

        // DEBUG_LOG("translated: %u", idx);
        // dump_vec3(translation);

        Vec3 position           = vec3_add(relative_positions[pidx], translation);
        relative_positions[idx] = position;

        // Mat3 rotation        = rotor3_to_mat3(current_state[idx]);
        // Mat4 final_transform = mat3_to_mat4(rotation);
        //
        // Mat4 parent_transform;
        // if (unlikely(anim_state->armature.joints[idx].parent == u32_MAX)) {
        //     parent_transform = mat4_identity();
        // } else {
        //     parent_transform = finalized[anim_state->armature.joints[idx].parent];
        //     anim_state->model_ubos[idx].model_offset =
        //         anim_state->model_ubos[anim_state->armature.joints[idx].parent].model_offset;
        // }
        //
        // Mat3 base3      = rotor3_to_mat3(anim_state->armature.joints[idx].base);
        // Mat4 base       = mat3_to_mat4(base3);
        // Mat4 translate  = mat4_identity();
        // translate.c3_r0 = 0.0f;
        // translate.c3_r1 = 0.0f;
        // translate.c3_r2 = anim_state->armature.joints[idx].length;
        //
        // Mat4 model_to_world = mat4_mul_var((Mat4[]){parent_transform, base, final_transform, translate}, 4);
        // finalized[idx]      = model_to_world;
        //
        // // relative_positions[idx] = mat4_mul_vec4(model_to_world, vec3_to_vec4(current_pos)).xyz;
        // relative_positions[idx] = mat4_mul_vec4(model_to_world, vec3_to_vec4(vec3_unit_z())).xyz;

        // DEBUG_LOG("rel pos: %u", idx);
        // dump_vec3(relative_positions[idx]);
    }

    // Here we are setting the goals by solving the constraints
    if (anim_state->ready == true) {
        anim_state->ready           = false;
        anim_state->anim_left_right = !anim_state->anim_left_right;

        u32 rkidx = anim_ctx->right.leg.thigh;
        u32 lkidx = anim_ctx->left.leg.thigh;
        if (anim_state->anim_left_right == 0) {
            anim_state->armature.goal[rkidx] =
                rotor3_from_euler_angles(0.0f, anim_state->armature.joints[rkidx].min_yz, 0.0f);
            anim_state->armature.goal[lkidx] =
                rotor3_from_euler_angles(0.0f, anim_state->armature.joints[lkidx].max_yz, 0.0f);
        } else {
            anim_state->armature.goal[rkidx] =
                rotor3_from_euler_angles(0.0f, anim_state->armature.joints[rkidx].max_yz, 0.0f);
            anim_state->armature.goal[lkidx] =
                rotor3_from_euler_angles(0.0f, anim_state->armature.joints[lkidx].min_yz, 0.0f);
        }
    }
}

// I probably need to use the lagrangian? method
// (the one where you solve all at once in a matrix with lin alg)
// Not sure how Fabrik handles elbows which can only bend in one direction.
// typedef struct Arm {
//     Joint* joints;
//     u32    joints_count;
//     u32    _padding;
// } Arm;

/* Bones

Arm:

    Shoulder:
    - xy is roll, -90 to 90
    - xz is flye, -110 to 90
    - yz is lateral, -90 to 90

    Elbow:
    - xy is roll, -90 to 90
    - xz is curl, -140 to 0
    - yz is impossible, 0 to 0

    Wrist:
    - xy is roll, 0 to 0
    - xz is curl, -90 to 80
    - yz is side, -30 to 30

    Fingers (same for all joints):
    - xy is roll, 0 to 0
    - xz is curl, -90 to 10
    - yz is side, -20 to 20

    Thumb:
    - xy is roll, 0 to 0
    - xz is curl, -110 to 0
    - yz is side, -45 to 45

Leg:

    Hip/Thigh:
    - xy is roll, -45 to 0
    - xz is curl, -135 to 0
    - yz is side, -45 to 45 (90 if you can do the splits)

    Knee:
    - xy is roll, -20 to 20
    - xz is curl, -110 to 0
    - yz is side, 0 to 0

    Ankle:
    - xy is roll, -45 to 45
    - xz is curl, -30 to 75
    - yz is side, -35 to 15

    Toes (all together? or 2?):
    - xy is roll, 0 to 0
    - xz is curl, -75 to 40
    - yz is side, -10 to 10

Spine:

    // TODO .


 */
