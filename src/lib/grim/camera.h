#pragma once

#include <lib/grim/assert.h>
#include <lib/grim/bp.h>
#include <lib/grim/math.h>
#include <lib/grim/phx/avbd.h>

typedef struct CameraTransform {
    Vec3   position;  // position
    Rotor3 direction; // rotor
} CameraTransform;
STATIC_ASSERT(sizeof(CameraTransform) == 28);

typedef struct CameraLens {
    f32 near_plane_distance;
    f32 far_plane_distance;
    f32 aspect_ratio;
    f32 vertical_fov;
} CameraLens;
STATIC_ASSERT(sizeof(CameraLens) == 16);

typedef struct Camera {
    CameraLens lens;
    Mat4       lens_mat;

    CameraTransform current;

    CameraTransform target;

    CameraTransform arm;      // offset
    f32             arm_zoom; // zii so < 0 is zoom in, 0 is no zoom, > 0 is zoom out
} Camera;
STATIC_ASSERT(sizeof(Camera) == 168);

typedef struct CameraMatrices {
    Vec3 cam_location;
    Mat4 view_to_clip;
    Mat4 world_to_view;
} CameraMatrices;
STATIC_ASSERT(sizeof(CameraMatrices) == 140);

CameraLens camera_lens_default() {
    return (CameraLens){
        .near_plane_distance = 1.0f,              // 1m
        .far_plane_distance  = 1000.0f,           // 1000m
        .aspect_ratio        = 1920.0f / 1080.0f, // TODO get this from swapchain/rendertarget width and height
        .vertical_fov        = rad(45.0f),
    };
}

void camera_update_lens(rop(rw Camera) camera, u32 width, u32 height) {
    camera->lens.aspect_ratio = (f32)width / (f32)height;
    CameraLens lens           = camera->lens;
    camera->lens_mat = mat4_perspective_infinite_z_vk(lens.vertical_fov, lens.aspect_ratio, lens.near_plane_distance);
}

Camera camera_new() {
    Vec3   pos = {.x = 0.0f, .y = 0.0f, .z = 0.0f};
    // Vec3   trg = vec3_normalize(vec3_sub((Vec3){0}, pos));
    // Rotor3 dir = rotor3_from_vec3_to_vec3(vec3_unit_z(), trg);
    Rotor3 dir = {.s = 1.0f};

    CameraTransform def = (CameraTransform){
        .direction = dir,
        .position  = pos,
    };

    CameraLens lens = camera_lens_default();
    Mat4 lens_mat   = mat4_perspective_infinite_z_vk(lens.vertical_fov, lens.aspect_ratio, lens.near_plane_distance);

    return (Camera){
        .lens     = lens,
        .lens_mat = lens_mat,
        .current  = def,
        .target   = def,
        .arm =
            (CameraTransform){
                .direction = rotor3_from_pitch(rad(20.0f)),
                // .direction = {.s = 1.0f},
                .position  = {.x = 0.0f, .y = 2.0f, .z = -7.5f},
            },
        .arm_zoom = 0.0, // zii
    };
}

void camera_update(rop(rw Camera) camera, f32 dt) {
    CameraTransform final_transform   = camera->target;
    CameraTransform current_transform = camera->current;

    // apply arm
    f32 arm_zoom  = camera->arm_zoom;
    // f32 real_zoom = 0.0f;
    // if (arm_zoom < 0.0f) {
    //     real_zoom = 1.0f / (1.0f - arm_zoom);
    // } else {
    //     real_zoom = 1.0f + arm_zoom + (arm_zoom * arm_zoom * 0.5f);
    // }
    f32 real_zoom = (f32)cmov32(
        ((u32)arm_zoom) >> 31, (1.0f / (1.0f - arm_zoom)), (1.0f + arm_zoom + (arm_zoom * arm_zoom * 0.5f)));

    rotor3_mul_ip(&final_transform.direction, camera->arm.direction);
    Vec3 final_arm = vec3_rotated_by_rotor3(camera->arm.position, current_transform.direction);
    vec3_mul_f32_ip(&final_arm, real_zoom);
    vec3_add_ip(&final_transform.position, final_arm);

    // TODO not sure if this is necessary and it's relatively expensive
    rotor3_normalize_ip(&final_transform.direction);

    // apply smoothing
    // final_transform.position = vec3_glerp(camera->current.position, final_transform.position, 20.0f, dt);
    // final_transform.direction = rotor3_glerp(camera->current.direction, final_transform.direction, 20.0f, dt);
    // TODO the issue with this is the position lerp is linear, even when it should not be
    // we should be deriving position always, right?
    // vec3_glerp_ip(&camera->current.position, final_transform.position, 20.0f, dt);
    camera->current.position = final_transform.position;
    rotor3_glerp_shortest_ip(&camera->current.direction, final_transform.direction, 20.0f, dt);
    // rotor3_spherical_glerp_ip(&camera->current.direction, final_transform.direction, 20.0f, dt);

    // TODO not sure if this is necessary and it's relatively expensive. ~150ns
    rotor3_normalize_ip(&camera->current.direction);

    // dump_rotor3(final_transform.direction);

    // camera->current = final_transform;
    // Rotor3 curr = camera->current.direction;
    // f32    y_component = 2.0f * (curr.s * curr.bivec.yz + curr.bivec.xy * (-curr.bivec.xz));
    // DEBUG_LOG("the thing: %f", curr.s * curr.bivec.xz - curr.bivec.yz * curr.bivec.xy);
    // DEBUG_LOG("robot says: %f", y_component);
    // DEBUG_LOG("the thing2: %f %f", curr.s, curr.bivec.yz + curr.bivec.xy);
    // dump_rotor3(curr);
}

// TODO we can probably simplify this
void camera_rotate_yaw_pitch(rop(rw Camera) camera, f32 yaw, f32 pitch) {
    Rotor3 t_dir  = camera->target.direction;
    f32    y_comp = 2.0f * (t_dir.s * t_dir.bivec.yz + t_dir.bivec.xy * (-t_dir.bivec.xz));

    if (fabsf(y_comp) > 0.95f && y_comp > 0.0f) {
        pitch = clamp_bot(0.0f, pitch);
    }
    // setif((fabsf(y_comp) > 0.95f && y_comp > 0.0f), pitch, clamp_bot(0.0f, pitch));

    if (fabsf(y_comp) > 0.8f && y_comp < 0.0f) {
        pitch = clamp_top(pitch, 0.0f);
    }
    // setif((fabsf(y_comp) > 0.8f && y_comp < 0.0f), pitch, clamp_top(pitch, 0.0f));

    Rotor3 r_yaw   = rotor3_from_yaw(yaw);
    Rotor3 r_pitch = rotor3_from_pitch(pitch);

    // lhs for local reference frame
    rotor3_mul_ip(&camera->target.direction, r_pitch);
    // rhs for global reference frame
    rotor3_mul_ip_rhs(r_yaw, &camera->target.direction);

    rotor3_normalize_ip(&camera->target.direction);
    // TODO need to figure out how to not let the cam flip when vertical up or down
    // dump_rotor3(camera->target.direction);
}

void camera_set_target(rop(rw Camera) camera, Transform target) {
    CRITICAL_LOG(SCOPE_CAMERA, "camera_set_target is UBER broken.");
    exit(1);
    camera->target.position  = target.pos;
    camera->target.direction = target.rotation;
}

void camera_set_target_pos(rop(rw Camera) camera, Vec3 pos) {
    camera->target.position = pos;
}

void camera_set_target_dir(rop(rw Camera) camera, Rotor3 dir) {
    camera->target.direction = dir;
}

// BUG this is broken. it's completely off when you rip out y then put it back in, right? what if unit_x -> unit_y in
// the rotation?
void camera_translate_target(rop(rw Camera) camera, Vec3 pos) {
    // TODO upwards movement should go up in y no matter what?
    // We also need to remove the downward motion when on the ground
    // Or instead of rotating by direction, rotate by something else?
    f32 y = pos.y;
    pos.y = 0.0f;
    vec3_rotated_by_rotor3_ip(&pos, camera->current.direction);
    pos.y += y;
    vec3_add_ip(&camera->target.position, pos);
}

void camera_zoom(rop(rw Camera) camera, f32 amount) {
    // camera->arm.position += amount; // position is offset, don't want this to be exponential so not just 10%

    camera->arm_zoom += amount;

    // Vec3 zoom = vec3_mul_f32(vec3_unit_z(), amount);
    // vec3_rotated_by_rotor3_ip(&zoom, camera->current.direction);
    // vec3_add_ip(&camera->target.position, zoom);
}

void camera_zoom_reset(rop(rw Camera) camera) {
    camera->arm_zoom = 0.0; // TODO should this be a lens param?
}

CameraMatrices camera_get_matrices(rop(ro Camera) camera) {
    // PERF I hate caching but... it seems worth here?
    // lens is almost never changed unless the window size changes.
    // Not worth recalculating this matrix every frame for that.
    // TODO check if the memory fetch is slower than recalc
    Mat4 view_to_clip = camera->lens_mat;

    CameraTransform current = camera->current;

    Mat4 world_to_view = mat4_world_to_view(current.direction);

    return (CameraMatrices){
        .cam_location  = current.position,
        .world_to_view = world_to_view,
        .view_to_clip  = view_to_clip,
    };
}

// Mat4 camera_transform_to_mat4(rop(ro CameraTransform) transform) {
//     // Mat4 view_to_clip = mat4_perspective_infinite_z_vk(lens);
//     // let view_to_clip =
//     //     ultraviolet::projection::perspective_infinite_z_vk(self.lens.vertical_fov, self.lens.aspect_ratio,
//     0.1);
//     //
//     // let eye_pos : Vec3 = self.rig.final_transform.position.into();
//     // let cam_rot : Rotor3 = self.rig.final_transform.rotation.into();
//     // let world_to_view = Mat4::look_at(eye_pos,
//     //                                   // eye_pos.clamped(
//     //                                   //     Vec3::new(f32::MIN, 0.0, f32::MIN),
//     //                                   //     Vec3::new(f32::MAX, f32::MAX, f32::MAX),
//     //                                   // ),
//     //                                   Vec3::from(eye_pos) + Vec3::unit_z().rotated_by(cam_rot.into()),
//     //                                   Vec3::unit_y(), );
//     //
//     // CameraState {
//     // lens:
//     //     CameraLensMatrices{
//     //         view_to_clip,
//     //         clip_to_view : view_to_clip.inversed(),
//     //     },
//     //         body : CameraBodyMatrices{
//     //             world_to_view,
//     //             view_to_world : world_to_view.inversed(),
//     //         },
//     // }
//     return (Mat4){
//         //
//     };
// }
