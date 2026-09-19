#pragma once

#include <lib/grim/assert.h>
#include <lib/grim/bp.h>
#include <lib/grim/logger.h>
#include <math.h>

#define rad(degree) (degree * ((f32)M_PI / 180.0f))
#define degree(rad) (rad * (180.0f / (f32)M_PI))

typedef struct Vec2 {
    union {
        struct {
            f32 x;
            f32 y;
        };
        struct {
            f32 r;
            f32 g;
        };
        f32 el[2];
    };
} Vec2;
STATIC_ASSERT(sizeof(Vec2) == 8);

typedef struct Vec3 {
    union {
        struct {
            f32 x;
            f32 y;
            f32 z;
        };
        struct {
            f32 r;
            f32 g;
            f32 b;
        };
        f32 el[3];
    };
} Vec3;
STATIC_ASSERT(sizeof(Vec3) == 12);

typedef struct Vec4 {
    union {
        struct {
            f32 x;
            f32 y;
            f32 z;
            f32 w;
        };
        struct {
            f32 r;
            f32 g;
            f32 b;
            f32 a;
        };
        struct {
            Vec3 xyz;
            f32  _w;
        };
        struct {
            Vec3 rgb;
            f32  _a;
        };
        f32 el[4];
    };
} Vec4;
STATIC_ASSERT(sizeof(Vec4) == 16);

typedef struct Bivec3 {
    union {
        struct {
            f32 xy;
            f32 xz;
            f32 yz;
        };
        f32 el[3];
    };
} Bivec3;
STATIC_ASSERT(sizeof(Bivec3) == 12);

typedef struct Rotor3 {
    f32    s;
    Bivec3 bivec;
} Rotor3;
STATIC_ASSERT(sizeof(Rotor3) == 16);

typedef struct Mat3 {
    union {
        struct {
            Vec3 col0;
            Vec3 col1;
            Vec3 col2;
        };
        struct {
            f32 c0_r0;
            f32 c0_r1;
            f32 c0_r2;

            f32 c1_r0;
            f32 c1_r1;
            f32 c1_r2;

            f32 c2_r0;
            f32 c2_r1;
            f32 c2_r2;
        };
        Vec3 col[3];
        f32  el[9];
    };
} Mat3;
STATIC_ASSERT(sizeof(Mat3) == 36);

typedef struct Mat4 {
    union {
        struct {
            Vec4 col0;
            Vec4 col1;
            Vec4 col2;
            Vec4 col3;
        };
        struct {
            f32 c0_r0;
            f32 c0_r1;
            f32 c0_r2;
            f32 c0_r3;

            f32 c1_r0;
            f32 c1_r1;
            f32 c1_r2;
            f32 c1_r3;

            f32 c2_r0;
            f32 c2_r1;
            f32 c2_r2;
            f32 c2_r3;

            f32 c3_r0;
            f32 c3_r1;
            f32 c3_r2;
            f32 c3_r3;
        };
        Vec4 col[4];
        f32  el[16];
    };
} Mat4;
STATIC_ASSERT(sizeof(Mat4) == 64);

void dump_mat4(ro Mat4 mat) {
    DEBUG_LOG(SCOPE_DEBUG,
              "\n"
              "%f %f %f %f \n"
              "%f %f %f %f \n"
              "%f %f %f %f \n"
              "%f %f %f %f \n",
              (f64)mat.c0_r0,
              (f64)mat.c1_r0,
              (f64)mat.c2_r0,
              (f64)mat.c3_r0,

              (f64)mat.c0_r1,
              (f64)mat.c1_r1,
              (f64)mat.c2_r1,
              (f64)mat.c3_r1,

              (f64)mat.c0_r2,
              (f64)mat.c1_r2,
              (f64)mat.c2_r2,
              (f64)mat.c3_r2,

              (f64)mat.c0_r3,
              (f64)mat.c1_r3,
              (f64)mat.c2_r3,
              (f64)mat.c3_r3);
}

void dump_mat3(ro Mat3 mat) {
    DEBUG_LOG(SCOPE_DEBUG,
              "\n"
              "%f %f %f \n"
              "%f %f %f \n"
              "%f %f %f \n",

              (f64)mat.c0_r0,
              (f64)mat.c1_r0,
              (f64)mat.c2_r0,

              (f64)mat.c0_r1,
              (f64)mat.c1_r1,
              (f64)mat.c2_r1,

              (f64)mat.c0_r2,
              (f64)mat.c1_r2,
              (f64)mat.c2_r2);
}

void dump_vec2(Vec2 vec) {
    DEBUG_LOG(SCOPE_DEBUG, "%f %f \n", (f64)vec.x, (f64)vec.y);
}

void dump_vec3(Vec3 vec) {
    DEBUG_LOG(SCOPE_DEBUG, "%f %f %f \n", (f64)vec.x, (f64)vec.y, (f64)vec.z);
}

void dump_rotor3(Rotor3 rot) {
    DEBUG_LOG(
        SCOPE_DEBUG, "xy%f xz%f yz%f s%f \n", (f64)rot.bivec.xy, (f64)rot.bivec.xz, (f64)rot.bivec.yz, (f64)rot.s);
}

void dump_vec4(Vec4 vec) {
    DEBUG_LOG(SCOPE_DEBUG, "%f %f %f %f\n", (f64)vec.x, (f64)vec.y, (f64)vec.z, (f64)vec.w);
}

Mat4 mat4_identity() {
    return (Mat4){
        .col0 = (Vec4){.x = 1.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f},
        .col1 = (Vec4){.x = 0.0f, .y = 1.0f, .z = 0.0f, .w = 0.0f},
        .col2 = (Vec4){.x = 0.0f, .y = 0.0f, .z = 1.0f, .w = 0.0f},
        .col3 = (Vec4){.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f},
    };
}

Mat3 mat3_identity() {
    return (Mat3){
        .col0 = (Vec3){.x = 1.0f, .y = 0.0f, .z = 0.0f},
        .col1 = (Vec3){.x = 0.0f, .y = 1.0f, .z = 0.0f},
        .col2 = (Vec3){.x = 0.0f, .y = 0.0f, .z = 1.0f},
    };
}

// Tested in compiler explorer, this is vectorized very well
// -O3 -march=znver4
Mat4 mat4_mul(Mat4 lhs, Mat4 rhs) {
    // Swapped lhs and rhs here to match mat mul: M1 * M2 means M2 first then M1
    Vec4 sa = lhs.col[0];
    Vec4 sb = lhs.col[1];
    Vec4 sc = lhs.col[2];
    Vec4 sd = lhs.col[3];

    Vec4 oa = rhs.col[0];
    Vec4 ob = rhs.col[1];
    Vec4 oc = rhs.col[2];
    Vec4 od = rhs.col[3];

    return (Mat4){
        .col =
            {
                {
                    .x = (sa.x * oa.x) + (sb.x * oa.y) + (sc.x * oa.z) + (sd.x * oa.w),
                    .y = (sa.y * oa.x) + (sb.y * oa.y) + (sc.y * oa.z) + (sd.y * oa.w),
                    .z = (sa.z * oa.x) + (sb.z * oa.y) + (sc.z * oa.z) + (sd.z * oa.w),
                    .w = (sa.w * oa.x) + (sb.w * oa.y) + (sc.w * oa.z) + (sd.w * oa.w),
                },
                {
                    .x = (sa.x * ob.x) + (sb.x * ob.y) + (sc.x * ob.z) + (sd.x * ob.w),
                    .y = (sa.y * ob.x) + (sb.y * ob.y) + (sc.y * ob.z) + (sd.y * ob.w),
                    .z = (sa.z * ob.x) + (sb.z * ob.y) + (sc.z * ob.z) + (sd.z * ob.w),
                    .w = (sa.w * ob.x) + (sb.w * ob.y) + (sc.w * ob.z) + (sd.w * ob.w),
                },
                {
                    .x = (sa.x * oc.x) + (sb.x * oc.y) + (sc.x * oc.z) + (sd.x * oc.w),
                    .y = (sa.y * oc.x) + (sb.y * oc.y) + (sc.y * oc.z) + (sd.y * oc.w),
                    .z = (sa.z * oc.x) + (sb.z * oc.y) + (sc.z * oc.z) + (sd.z * oc.w),
                    .w = (sa.w * oc.x) + (sb.w * oc.y) + (sc.w * oc.z) + (sd.w * oc.w),
                },
                {
                    .x = (sa.x * od.x) + (sb.x * od.y) + (sc.x * od.z) + (sd.x * od.w),
                    .y = (sa.y * od.x) + (sb.y * od.y) + (sc.y * od.z) + (sd.y * od.w),
                    .z = (sa.z * od.x) + (sb.z * od.y) + (sc.z * od.z) + (sd.z * od.w),
                    .w = (sa.w * od.x) + (sb.w * od.y) + (sc.w * od.z) + (sd.w * od.w),
                },
            },
    };
}

Vec4 mat4_mul_vec4(Mat4 mat, Vec4 vec) {
    Vec4 a = mat.col[0];
    Vec4 b = mat.col[1];
    Vec4 c = mat.col[2];
    Vec4 d = mat.col[3];
    return (Vec4){
        .x = a.x * vec.x + b.x * vec.y + c.x * vec.z + d.x * vec.w,
        .y = a.y * vec.x + b.y * vec.y + c.y * vec.z + d.y * vec.w,
        .z = a.z * vec.x + b.z * vec.y + c.z * vec.z + d.z * vec.w,
        .w = a.w * vec.x + b.w * vec.y + c.w * vec.z + d.w * vec.w,
    };
}

Mat4 mat4_mul_var(Mat4* arr, u32 len) {
    Mat4 base = arr[0];
    for (u32 idx = 1; idx < len; idx++) {
        base = mat4_mul(base, arr[idx]);
    }
    return base;
}

void mat4_mul_ip(rop(rw Mat4) lhs, Mat4 rhs) {
    // Swapped lhs and rhs here to match mat mul: M1 * M2 means M2 first then M1
    Vec4 sa = lhs->col[0];
    Vec4 sb = lhs->col[1];
    Vec4 sc = lhs->col[2];
    Vec4 sd = lhs->col[3];

    Vec4 oa = rhs.col[0];
    Vec4 ob = rhs.col[1];
    Vec4 oc = rhs.col[2];
    Vec4 od = rhs.col[3];

    lhs->col[0] = (Vec4){
        .x = (sa.x * oa.x) + (sb.x * oa.y) + (sc.x * oa.z) + (sd.x * oa.w),
        .y = (sa.y * oa.x) + (sb.y * oa.y) + (sc.y * oa.z) + (sd.y * oa.w),
        .z = (sa.z * oa.x) + (sb.z * oa.y) + (sc.z * oa.z) + (sd.z * oa.w),
        .w = (sa.w * oa.x) + (sb.w * oa.y) + (sc.w * oa.z) + (sd.w * oa.w),
    };
    lhs->col[1] = (Vec4){
        .x = (sa.x * ob.x) + (sb.x * ob.y) + (sc.x * ob.z) + (sd.x * ob.w),
        .y = (sa.y * ob.x) + (sb.y * ob.y) + (sc.y * ob.z) + (sd.y * ob.w),
        .z = (sa.z * ob.x) + (sb.z * ob.y) + (sc.z * ob.z) + (sd.z * ob.w),
        .w = (sa.w * ob.x) + (sb.w * ob.y) + (sc.w * ob.z) + (sd.w * ob.w),
    };
    lhs->col[2] = (Vec4){
        .x = (sa.x * oc.x) + (sb.x * oc.y) + (sc.x * oc.z) + (sd.x * oc.w),
        .y = (sa.y * oc.x) + (sb.y * oc.y) + (sc.y * oc.z) + (sd.y * oc.w),
        .z = (sa.z * oc.x) + (sb.z * oc.y) + (sc.z * oc.z) + (sd.z * oc.w),
        .w = (sa.w * oc.x) + (sb.w * oc.y) + (sc.w * oc.z) + (sd.w * oc.w),
    };
    lhs->col[3] = (Vec4){
        .x = (sa.x * od.x) + (sb.x * od.y) + (sc.x * od.z) + (sd.x * od.w),
        .y = (sa.y * od.x) + (sb.y * od.y) + (sc.y * od.z) + (sd.y * od.w),
        .z = (sa.z * od.x) + (sb.z * od.y) + (sc.z * od.z) + (sd.z * od.w),
        .w = (sa.w * od.x) + (sb.w * od.y) + (sc.w * od.z) + (sd.w * od.w),
    };
}

void mat3_mul_ip(rop(rw Mat3) lhs, Mat3 rhs) {
    // Swapped lhs and rhs here to match mat mul: M1 * M2 means M2 first then M1
    Vec3 sa = lhs->col[0];
    Vec3 sb = lhs->col[1];
    Vec3 sc = lhs->col[2];

    Vec3 oa = rhs.col[0];
    Vec3 ob = rhs.col[1];
    Vec3 oc = rhs.col[2];

    lhs->col[0] = (Vec3){
        .x = (sa.x * oa.x) + (sb.x * oa.y) + (sc.x * oa.z),
        .y = (sa.y * oa.x) + (sb.y * oa.y) + (sc.y * oa.z),
        .z = (sa.z * oa.x) + (sb.z * oa.y) + (sc.z * oa.z),
    };
    lhs->col[1] = (Vec3){
        .x = (sa.x * ob.x) + (sb.x * ob.y) + (sc.x * ob.z),
        .y = (sa.y * ob.x) + (sb.y * ob.y) + (sc.y * ob.z),
        .z = (sa.z * ob.x) + (sb.z * ob.y) + (sc.z * ob.z),
    };
    lhs->col[2] = (Vec3){
        .x = (sa.x * oc.x) + (sb.x * oc.y) + (sc.x * oc.z),
        .y = (sa.y * oc.x) + (sb.y * oc.y) + (sc.y * oc.z),
        .z = (sa.z * oc.x) + (sb.z * oc.y) + (sc.z * oc.z),
    };
}

void mat4_mul_ip_rhs(Mat4 lhs, rop(rw Mat4) rhs) {
    Vec4 sa = lhs.col[0];
    Vec4 sb = lhs.col[1];
    Vec4 sc = lhs.col[2];
    Vec4 sd = lhs.col[3];

    Vec4 oa = rhs->col[0];
    Vec4 ob = rhs->col[1];
    Vec4 oc = rhs->col[2];
    Vec4 od = rhs->col[3];

    rhs->col[0] = (Vec4){
        .x = (sa.x * oa.x) + (sb.x * oa.y) + (sc.x * oa.z) + (sd.x * oa.w),
        .y = (sa.y * oa.x) + (sb.y * oa.y) + (sc.y * oa.z) + (sd.y * oa.w),
        .z = (sa.z * oa.x) + (sb.z * oa.y) + (sc.z * oa.z) + (sd.z * oa.w),
        .w = (sa.w * oa.x) + (sb.w * oa.y) + (sc.w * oa.z) + (sd.w * oa.w),
    };
    rhs->col[1] = (Vec4){
        .x = (sa.x * ob.x) + (sb.x * ob.y) + (sc.x * ob.z) + (sd.x * ob.w),
        .y = (sa.y * ob.x) + (sb.y * ob.y) + (sc.y * ob.z) + (sd.y * ob.w),
        .z = (sa.z * ob.x) + (sb.z * ob.y) + (sc.z * ob.z) + (sd.z * ob.w),
        .w = (sa.w * ob.x) + (sb.w * ob.y) + (sc.w * ob.z) + (sd.w * ob.w),
    };
    rhs->col[2] = (Vec4){
        .x = (sa.x * oc.x) + (sb.x * oc.y) + (sc.x * oc.z) + (sd.x * oc.w),
        .y = (sa.y * oc.x) + (sb.y * oc.y) + (sc.y * oc.z) + (sd.y * oc.w),
        .z = (sa.z * oc.x) + (sb.z * oc.y) + (sc.z * oc.z) + (sd.z * oc.w),
        .w = (sa.w * oc.x) + (sb.w * oc.y) + (sc.w * oc.z) + (sd.w * oc.w),
    };
    rhs->col[3] = (Vec4){
        .x = (sa.x * od.x) + (sb.x * od.y) + (sc.x * od.z) + (sd.x * od.w),
        .y = (sa.y * od.x) + (sb.y * od.y) + (sc.y * od.z) + (sd.y * od.w),
        .z = (sa.z * od.x) + (sb.z * od.y) + (sc.z * od.z) + (sd.z * od.w),
        .w = (sa.w * od.x) + (sb.w * od.y) + (sc.w * od.z) + (sd.w * od.w),
    };
}

Rotor3 rotor3_identity() {
    return (Rotor3){
        .s     = 1.0f,
        .bivec = {0},
    };
}

Vec3 vec3_unit_x() {
    return (Vec3){.x = 1.0f, .y = 0.0f, .z = 0.0f};
}

Vec3 vec3_unit_y() {
    return (Vec3){.x = 0.0f, .y = 1.0f, .z = 0.0f};
}

Vec3 vec3_unit_z() {
    return (Vec3){.x = 0.0f, .y = 0.0f, .z = 1.0f};
}

Vec3 vec3_rotated_by_rotor3(Vec3 vec, Rotor3 rot) {
    f32 fx = rot.s * vec.x + rot.bivec.xy * vec.y + rot.bivec.xz * vec.z;
    f32 fy = rot.s * vec.y - rot.bivec.xy * vec.x + rot.bivec.yz * vec.z;
    f32 fz = rot.s * vec.z - rot.bivec.xz * vec.x - rot.bivec.yz * vec.y;
    f32 fw = rot.bivec.xy * vec.z - rot.bivec.xz * vec.y + rot.bivec.yz * vec.x;

    return (Vec3){
        .x = rot.s * fx + rot.bivec.xy * fy + rot.bivec.xz * fz + rot.bivec.yz * fw,
        .y = rot.s * fy - rot.bivec.xy * fx - rot.bivec.xz * fw + rot.bivec.yz * fz,
        .z = rot.s * fz + rot.bivec.xy * fw - rot.bivec.xz * fx - rot.bivec.yz * fy,
    };
}

void vec3_rotated_by_rotor3_ip(rop(rw Vec3) vec, Rotor3 rot) {
    f32 fx = rot.s * vec->x + rot.bivec.xy * vec->y + rot.bivec.xz * vec->z;
    f32 fy = rot.s * vec->y - rot.bivec.xy * vec->x + rot.bivec.yz * vec->z;
    f32 fz = rot.s * vec->z - rot.bivec.xz * vec->x - rot.bivec.yz * vec->y;
    f32 fw = rot.bivec.xy * vec->z - rot.bivec.xz * vec->y + rot.bivec.yz * vec->x;

    vec->x = rot.s * fx + rot.bivec.xy * fy + rot.bivec.xz * fz + rot.bivec.yz * fw;
    vec->y = rot.s * fy - rot.bivec.xy * fx - rot.bivec.xz * fw + rot.bivec.yz * fz;
    vec->z = rot.s * fz + rot.bivec.xy * fw - rot.bivec.xz * fx - rot.bivec.yz * fy;
}

Bivec3 bivec3_unit_xy() {
    return (Bivec3){
        .xy = 1.0f,
        .xz = 0.0f,
        .yz = 0.0f,
    };
}

Bivec3 bivec3_unit_xz() {
    return (Bivec3){
        .xy = 0.0f,
        .xz = 1.0f,
        .yz = 0.0f,
    };
}

Bivec3 bivec3_unit_yz() {
    return (Bivec3){
        .xy = 0.0f,
        .xz = 0.0f,
        .yz = 1.0f,
    };
}

Bivec3 bivec3_div(Bivec3 lhs, Bivec3 rhs) {
    return (Bivec3){
        .xy = lhs.xy / rhs.xy,
        .xz = lhs.xz / rhs.xz,
        .yz = lhs.yz / rhs.yz,
    };
}

Rotor3 rotor3_div(Rotor3 lhs, Rotor3 rhs) {
    return (Rotor3){
        .s     = lhs.s / rhs.s,
        .bivec = bivec3_div(lhs.bivec, rhs.bivec),
    };
}

// equivalent to rotate rhs by lhs
Rotor3 rotor3_mul(Rotor3 lhs, Rotor3 rhs) {
    return (Rotor3){
        .s = lhs.s * rhs.s - lhs.bivec.xy * rhs.bivec.xy - lhs.bivec.xz * rhs.bivec.xz - lhs.bivec.yz * rhs.bivec.yz,
        .bivec =
            (Bivec3){
                .xy = lhs.bivec.xy * rhs.s + lhs.s * rhs.bivec.xy + lhs.bivec.yz * rhs.bivec.xz -
                      lhs.bivec.xz * rhs.bivec.yz,
                .xz = lhs.bivec.xz * rhs.s + lhs.s * rhs.bivec.xz - lhs.bivec.yz * rhs.bivec.xy +
                      lhs.bivec.xy * rhs.bivec.yz,
                .yz = lhs.bivec.yz * rhs.s + lhs.s * rhs.bivec.yz + lhs.bivec.xz * rhs.bivec.xy -
                      lhs.bivec.xy * rhs.bivec.xz,
            },
    };
}

// lhs for local, rhs for global frame. IE yaw is global, pitch is local
void rotor3_mul_ip(rop(rw Rotor3) lhs, Rotor3 rhs) {
    *lhs = rotor3_mul(*lhs, rhs);

    // // TODO this old_lhs thing feels a bit silly.
    // // maybe construct a new one and then replace it?
    // Rotor3 old_lhs = *lhs;
    // lhs->s         = old_lhs.s * rhs.s - old_lhs.bivec.xy * rhs.bivec.xy - old_lhs.bivec.xz * rhs.bivec.xz -
    //                  old_lhs.bivec.yz * rhs.bivec.yz;
    // lhs->bivec = (Bivec3){
    //     .xy = old_lhs.bivec.xy * rhs.s + old_lhs.s * rhs.bivec.xy + old_lhs.bivec.yz * rhs.bivec.xz -
    //           old_lhs.bivec.xz * rhs.bivec.yz,
    //     .xz = old_lhs.bivec.xz * rhs.s + old_lhs.s * rhs.bivec.xz - old_lhs.bivec.yz * rhs.bivec.xy +
    //           old_lhs.bivec.xy * rhs.bivec.yz,
    //     .yz = old_lhs.bivec.yz * rhs.s + old_lhs.s * rhs.bivec.yz + old_lhs.bivec.xz * rhs.bivec.xy -
    //           old_lhs.bivec.xy * rhs.bivec.xz,
    // };
}

void rotor3_mul_ip_rhs(Rotor3 lhs, rop(rw Rotor3) rhs) {
    *rhs = rotor3_mul(lhs, *rhs);

    // // TODO this old_lhs thing feels a bit silly.
    // // maybe construct a new one and then replace it?
    // Rotor3 old_rhs = *rhs;
    // rhs->s         = lhs.s * old_rhs.s - lhs.bivec.xy * old_rhs.bivec.xy - lhs.bivec.xz * old_rhs.bivec.xz -
    //                  lhs.bivec.yz * old_rhs.bivec.yz;
    // rhs->bivec = (Bivec3){
    //     .xy = lhs.bivec.xy * old_rhs.s + lhs.s * old_rhs.bivec.xy + lhs.bivec.yz * old_rhs.bivec.xz -
    //           lhs.bivec.xz * old_rhs.bivec.yz,
    //     .xz = lhs.bivec.xz * old_rhs.s + lhs.s * old_rhs.bivec.xz - lhs.bivec.yz * old_rhs.bivec.xy +
    //           lhs.bivec.xy * old_rhs.bivec.yz,
    //     .yz = lhs.bivec.yz * old_rhs.s + lhs.s * old_rhs.bivec.yz + lhs.bivec.xz * old_rhs.bivec.xy -
    //           lhs.bivec.xy * old_rhs.bivec.xz,
    // };
}

Bivec3 bivec3_mul_f32(Bivec3 bivec, f32 val) {
    return (Bivec3){
        .xy = bivec.xy * val,
        .xz = bivec.xz * val,
        .yz = bivec.yz * val,
    };
}

void bivec3_mul_f32_ip(rop(rw Bivec3) bivec, f32 val) {
    bivec->xy = bivec->xy * val;
    bivec->xz = bivec->xz * val;
    bivec->yz = bivec->yz * val;
}

Rotor3 rotor3_unit() {
    return (Rotor3){.s = 1.0f};
}

Rotor3 rotor3_inv(Rotor3 rotor) {
    return (Rotor3){
        .s     = rotor.s,
        .bivec = bivec3_mul_f32(rotor.bivec, -1.0f),
    };
}

Rotor3 rotor3_from_quaternion(f32 array[4]) {
    return (Rotor3){
        .s = array[3],
        .bivec =
            (Bivec3){
                .xy = -array[2],
                .xz = array[1],
                .yz = -array[0],
            },
    };
}

Rotor3 rotor3_from_angle_plane(f32 angle, Bivec3 plane) {
    f32 half_angle = angle * 0.5f;
    f32 s = sinf(half_angle), c = cosf(half_angle);
    return (Rotor3){
        .s     = c,
        .bivec = bivec3_mul_f32(plane, -1.0f * s),
    };
}

Rotor3 rotor3_from_yaw(f32 yaw) {
    return rotor3_from_angle_plane(yaw, bivec3_unit_xz());
}

Rotor3 rotor3_from_pitch(f32 pitch) {
    return rotor3_from_angle_plane(pitch, bivec3_unit_yz());
}

Rotor3 rotor3_from_euler_angles(f32 yaw, f32 pitch, f32 roll) {
    Rotor3 yaw_r = rotor3_from_angle_plane(yaw, bivec3_unit_xz());
    Rotor3 pit_r = rotor3_from_angle_plane(pitch, bivec3_unit_yz());
    Rotor3 rol_r = rotor3_from_angle_plane(roll, bivec3_unit_xy());

    Rotor3 final = rotor3_mul(yaw_r, pit_r);
    final        = rotor3_mul(final, rol_r);

    return final;
}

Bivec3 bivec3_mul(Bivec3 bivec, Bivec3 rhs) {
    return (Bivec3){
        .xy = bivec.xy * rhs.xy,
        .xz = bivec.xz * rhs.xz,
        .yz = bivec.yz * rhs.yz,
    };
}
Rotor3 mul_rotor3(Rotor3 rotor, f32 val) {
    return (Rotor3){
        .bivec = bivec3_mul_f32(rotor.bivec, val),
        .s     = rotor.s * val,
    };
}

// TODO sse / avx2
f32 vec3_dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 vec3_mul_ew(Vec3 a, Vec3 b) {
    return (Vec3){
        .x = a.x * b.x,
        .y = a.y * b.y,
        .z = a.z * b.z,
    };
}

// TODO sse / avx2
Bivec3 bivec3_wedge(Vec3 a, Vec3 b) {
    return (Bivec3){
        .xy = a.x * b.y - a.y * b.x,
        .xz = a.x * b.z - a.z * b.x,
        .yz = a.y * b.z - a.z * b.y,
    };
}

f32 bivec3_mag_sq(Bivec3 bivec) {
    return bivec.xy * bivec.xy + bivec.xz * bivec.xz + bivec.yz * bivec.yz;
}

f32 rotor3_mag_sq(Rotor3 rotor) {
    return rotor.s * rotor.s + bivec3_mag_sq(rotor.bivec);
}

Vec3 vec3_add(Vec3 lhs, Vec3 rhs) {
    return (Vec3){
        .x = lhs.x + rhs.x,
        .y = lhs.y + rhs.y,
        .z = lhs.z + rhs.z,
    };
}

Vec3 vec3_sub(Vec3 lhs, Vec3 rhs) {
    return (Vec3){
        .x = lhs.x - rhs.x,
        .y = lhs.y - rhs.y,
        .z = lhs.z - rhs.z,
    };
}

Bivec3 bivec3_sub(Bivec3 lhs, Bivec3 rhs) {
    return (Bivec3){
        .xy = lhs.xy - rhs.xy,
        .xz = lhs.xz - rhs.xz,
        .yz = lhs.yz - rhs.yz,
    };
}

Rotor3 rotor3_sub(Rotor3 lhs, Rotor3 rhs) {
    return (Rotor3){
        .s     = lhs.s - rhs.s,
        .bivec = bivec3_sub(lhs.bivec, rhs.bivec),
    };
}

Mat3 rotor3_to_mat3(Rotor3 rotor) {
    f32 s2      = rotor.s * rotor.s;
    f32 bxy2    = rotor.bivec.xy * rotor.bivec.xy;
    f32 bxz2    = rotor.bivec.xz * rotor.bivec.xz;
    f32 byz2    = rotor.bivec.yz * rotor.bivec.yz;
    f32 s_bxy   = rotor.s * rotor.bivec.xy;
    f32 s_bxz   = rotor.s * rotor.bivec.xz;
    f32 s_byz   = rotor.s * rotor.bivec.yz;
    f32 bxz_byz = rotor.bivec.xz * rotor.bivec.yz;
    f32 bxy_byz = rotor.bivec.xy * rotor.bivec.yz;
    f32 bxy_bxz = rotor.bivec.xy * rotor.bivec.xz;
    f32 two     = 2.0;

    return (Mat3){
        .col =
            {
                (Vec3){
                    .x = s2 - bxy2 - bxz2 + byz2,
                    .y = -two * (bxz_byz + s_bxy),
                    .z = two * (bxy_byz - s_bxz),
                },
                (Vec3){
                    .x = two * (s_bxy - bxz_byz),
                    .y = s2 - bxy2 + bxz2 - byz2,
                    .z = -two * (s_byz + bxy_bxz),
                },
                (Vec3){
                    .x = two * (s_bxz + bxy_byz),
                    .y = two * (s_byz - bxy_bxz),
                    .z = s2 + bxy2 - bxz2 - byz2,
                },
            },
    };
}

Vec4 vec3_to_vec4(Vec3 vec) {
    Vec4 result = {0};
    memcpy(&result, &vec, sizeof(Vec3));
    return result;
}

Mat4 mat3_to_mat4(Mat3 mat) {
    return (Mat4){
        .col0 = vec3_to_vec4(mat.col0),
        .col1 = vec3_to_vec4(mat.col1),
        .col2 = vec3_to_vec4(mat.col2),
        .col3 = (Vec4){.w = 1.0f},
    };
}

Mat3 mat4_to_mat3(Mat4 mat) {
    return (Mat3){
        .col0 = mat.col0.xyz,
        .col1 = mat.col1.xyz,
        .col2 = mat.col2.xyz,
    };
}

f32 vec3_mag_sq(Vec3 val) {
    return val.x * val.x + val.y * val.y + val.z * val.z;
}

f32 vec3_mag(Vec3 val) {
    // TODO is it worth changing this to vec3_dot(val, val)
    return sqrtf(vec3_mag_sq(val));
}

Vec3 vec3_mul_f32(Vec3 val, f32 mag) {
    return (Vec3){
        .x = val.x * mag,
        .y = val.y * mag,
        .z = val.z * mag,
    };
}

void vec3_mul_f32_ip(rop(rw Vec3) val, f32 mag) {
    val->x = val->x * mag;
    val->y = val->y * mag;
    val->z = val->z * mag;
}

void vec3_mul_u32_ip(rop(rw Vec3) val, u32 mag) {
    val->x = val->x * (f32)mag;
    val->y = val->y * (f32)mag;
    val->z = val->z * (f32)mag;
}

Vec3 vec3_div_f32(Vec3 val, f32 mag) {
    return (Vec3){
        .x = val.x / mag,
        .y = val.y / mag,
        .z = val.z / mag,
    };
}

void vec3_div_f32_ip(rop(rw Vec3) val, f32 mag) {
    val->x = val->x / mag;
    val->y = val->y / mag;
    val->z = val->z / mag;
}

void vec3_normalize_ip(rop(rw Vec3) val) {
    f32 mag = vec3_mag(*val);
    vec3_div_f32_ip(val, mag);
}

Vec3 vec3_normalize(Vec3 val) {
    f32 mag = vec3_mag(val);
    return vec3_div_f32(val, mag);
}

Vec3 vec3_cross(Vec3 lhs, Vec3 rhs) {
    return (Vec3){
        .x = lhs.y * rhs.z - lhs.z * rhs.y,
        .y = lhs.z * rhs.x - lhs.x * rhs.z,
        .z = lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

Mat4 mat4_look_at(Vec3 eye, Vec3 at, Vec3 up) {
    Vec3 f = vec3_normalize(vec3_sub(at, eye));
    // DEBUG_LOG(SCOPE_MATH,"look at f: ");
    // dump_vec3(f);

    Vec3 r = vec3_normalize(vec3_cross(f, up));
    // DEBUG_LOG(SCOPE_MATH,"look at r: ");
    // dump_vec3(r);

    Vec3 u = vec3_cross(r, f);
    // DEBUG_LOG(SCOPE_MATH,"look at u: ");
    // dump_vec3(u);

    return (Mat4){
        .col0 = (Vec4){.x = r.x, .y = u.x, .z = -1.0f * f.x, .w = 0.0f},
        .col1 = (Vec4){.x = r.y, .y = u.y, .z = -1.0f * f.y, .w = 0.0f},
        .col2 = (Vec4){.x = r.z, .y = u.z, .z = -1.0f * f.z, .w = 0.0f},
        .col3 =
            (Vec4){
                .x = vec3_dot(vec3_mul_f32(r, -1.0f), eye),
                .y = vec3_dot(vec3_mul_f32(u, -1.0f), eye),
                .z = vec3_dot(f, eye),
                .w = 1.0f,
            },
    };
}

Mat4 mat4_world_to_view(Rotor3 dir) {
    f32 w = dir.s;
    f32 x = -dir.bivec.yz;
    f32 y = dir.bivec.xz;
    f32 z = -dir.bivec.xy;

    f32 x2 = x * 2.0f;
    f32 y2 = y * 2.0f;
    f32 z2 = z * 2.0f;

    f32 xx2 = x * x2;
    f32 yy2 = y * y2;
    f32 zz2 = z * z2;

    f32 xy2 = x * y2;
    f32 xz2 = x * z2;
    f32 yz2 = y * z2;

    f32 wx2 = w * x2;
    f32 wy2 = w * y2;
    f32 wz2 = w * z2;

    return (Mat4){
        .col0 = {.x = 1.0f - (yy2 + zz2), .y = xy2 - wz2, .z = -(xz2 + wy2), .w = 0.0f},
        .col1 = {.x = xy2 + wz2, .y = 1.0f - (xx2 + zz2), .z = wx2 - yz2, .w = 0.0f},
        .col2 = {.x = xz2 - wy2, .y = yz2 + wx2, .z = xx2 + yy2 - 1.0f, .w = 0.0f},
        .col3 = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f},
    };
}

Mat4 mat4_perspective_vk(f32 vert_fov, f32 aspect_ratio, f32 z_near, f32 z_far) {
    f32 t   = tanf(vert_fov / 2.0f);
    f32 sy  = 1.0f / t;
    f32 sx  = sy / aspect_ratio;
    f32 nmf = z_near - z_far;
    return (Mat4){
        .col0 = (Vec4){.x = sx, .y = 0.0f, .z = 0.0f, .w = 0.0f},
        .col1 = (Vec4){.x = 0.0f, .y = -1.0f * sy, .z = 0.0f, .w = 0.0f},
        .col2 = (Vec4){.x = 0.0f, .y = 0.0f, .z = z_far / nmf, .w = -1.0f},
        .col3 = (Vec4){.x = 0.0f, .y = 0.0f, .z = z_near * z_far / nmf, .w = 0.0f},
    };
}

Mat4 mat4_perspective_infinite_z_vk(f32 vert_fov, f32 aspect_ratio, f32 z_near) {
    f32 t  = tanf(vert_fov / 2.0f);
    f32 sy = 1.0f / t;
    f32 sx = sy / aspect_ratio;

    // TODO all funcs should be like this, thanks glm for inspiration
    // way better to memset then individually set mem
    Mat4 perspective          = {0};
    perspective.el[0 + 0 * 4] = sx;
    perspective.el[1 + 1 * 4] = -1.0f * sy;
    perspective.el[2 + 2 * 4] = -1.0f;
    perspective.el[3 + 2 * 4] = -1.0f;
    perspective.el[2 + 3 * 4] = -1.0f * z_near;
    // return (Mat4){
    //     .col0 = (Vec4){.x = sx, .y = 0.0f, .z = 0.0f, .w = 0.0f},
    //     .col1 = (Vec4){.x = 0.0f, .y = -1.0f * sy, .z = 0.0f, .w = 0.0f},
    //     .col2 = (Vec4){.x = 0.0f, .y = 0.0f, .z = -1.0f, .w = 1.0f},
    //     .col3 = (Vec4){.x = 0.0f, .y = 0.0f, .z = -1.0f * z_near, .w = 0.0f},
    // };
    return perspective;
}

void vec3_add_ip(rop(rw Vec3) lhs, Vec3 rhs) {
    lhs->x += rhs.x;
    lhs->y += rhs.y;
    lhs->z += rhs.z;
}

void bivec3_add_ip(rop(rw Bivec3) lhs, Bivec3 rhs) {
    lhs->xy += rhs.xy;
    lhs->xz += rhs.xz;
    lhs->yz += rhs.yz;
}

// Not sure this is a real math op
// void rotor3_add_ip(rop(rw Rotor3) lhs, Rotor3 rhs) {
//     lhs->s += rhs.s;
//     bivec3_add_ip(&lhs->bivec, rhs.bivec);
// }

// This is identical to mag_sq
f32 bivec3_dot(Bivec3 lhs, Bivec3 rhs) {
    return lhs.xy * rhs.xy + lhs.xz * rhs.xz + lhs.yz * rhs.yz;
}

// This is identical to mag_sq
f32 rotor3_dot(Rotor3 lhs, Rotor3 rhs) {
    return lhs.s * rhs.s + bivec3_dot(lhs.bivec, rhs.bivec);
}

Rotor3 rotor3_mul_f32(Rotor3 lhs, f32 rhs) {
    return (Rotor3){
        .s     = lhs.s * rhs,
        .bivec = bivec3_mul_f32(lhs.bivec, rhs),
    };
}

void rotor3_mul_f32_ip(rop(rw Rotor3) lhs, f32 rhs) {
    lhs->s = lhs->s * rhs;
    bivec3_mul_f32_ip(&lhs->bivec, rhs);
}

void bivec3_div_f32_ip(rop(rw Bivec3) lhs, f32 rhs) {
    lhs->xy /= rhs;
    lhs->xz /= rhs;
    lhs->yz /= rhs;
}

Bivec3 bivec3_div_f32(Bivec3 lhs, f32 rhs) {
    return (Bivec3){
        .xy = lhs.xy / rhs,
        .xz = lhs.xz / rhs,
        .yz = lhs.yz / rhs,
    };
}

Rotor3 rotor3_div_f32(Rotor3 lhs, f32 rhs) {
    return (Rotor3){
        .s     = lhs.s / rhs,
        .bivec = bivec3_div_f32(lhs.bivec, rhs),
    };
}

void rotor3_div_f32_ip(rop(rw Rotor3) lhs, f32 rhs) {
    lhs->s /= rhs;
    bivec3_div_f32_ip(&lhs->bivec, rhs);
}

Rotor3 rotor3_normalize(Rotor3 rotor) {
    f32 mag_sq = rotor3_mag_sq(rotor);
    f32 mag    = sqrtf(mag_sq);
    return rotor3_div_f32(rotor, mag);
}

// normalize removes the scale component of the rotor, it does not change the angle or the axis
void rotor3_normalize_ip(rop(rw Rotor3) rotor) {
    f32 mag_sq = rotor3_mag_sq(*rotor);
    f32 mag    = sqrtf(mag_sq);
    rotor3_div_f32_ip(rotor, mag);
}

// **CRITICAL WARNING** Make sure your vecs are normalized or the rotation will be skewed
//
// Create a rotor that defines a rotation from a to b
Rotor3 rotor3_from_vec3_to_vec3(Vec3 from, Vec3 to) {
    Rotor3 temp = (Rotor3){
        .s     = 1.0f + vec3_dot(to, from),
        .bivec = bivec3_wedge(to, from),
    };

    /*
    https://docs.rs/ultraviolet/latest/ultraviolet/rotor/index.html

    A rotor can be thought of in multiple ways, the first of which is that a
    rotor is the result of the ‘geometric product’ of two vectors, denoted for
    two vectors u and v as simply uv. This operation is defined as

    uv = u · v + u ∧ v

    As can be seen, this operation results in the addition of two different
    types of values: first, the dot product will result in a scalar, and second,
    the exterior (wedge) product will result in a bivector. The addition of
    these two different types is not defined, but can be understood in a similar
    way as complex numbers, i.e. as a ‘bundle’ of two different kinds of values.

    The reason we call this type of value a ‘rotor’ is that if you both left-
    and right-multiply (using the geometric product) a rotor with a vector, you
    will rotate the sandwiched vector. For example, if you start with two
    vectors, a and b, and create a rotor ab from them, then rotate a vector u
    with this rotor by doing ba u ab, you will end up rotating the vector u by
    in the plane that corresponds to a ∧ b (i.e. the plane which is parallel
    with both vectors), by twice the angle between a and b, in the opposite
    direction of the one that would bring a towards b within that plane.
     */
    return rotor3_normalize(temp);
}

void f32_glerp_ip(rop(rw f32) from, f32 to, f32 decay, f32 dt) {
    *from = to + (*from - to) * expf(-1.0f * decay * dt);
}

f32 f32_glerp(f32 from, f32 to, f32 decay, f32 dt) {
    // #define expDecay(a, b, decay, dt)                 (b + (a - b) * exp(-decay * dt))
    // #define lerp(a, b, dt)                            expDecay(a, b, 0.5f, dt)
    return to + (from - to) * expf(-1.0f * decay * dt);
}

// spring lerp
f32 f32_sprlerp(f32 from, f32 to, f32 decay, f32 springiness, f32 dt) {
    // #define springDecay(a, b, decay, springiness, dt) (b - (b - a) * exp(-decay * dt) * cosf(springiness * dt))
    return to - (from - to) * expf(-1.0f * decay * dt) * cosf(springiness * dt);
}

// You should always normalize after this.
void bivec3_glerp_ip(rop(rw Bivec3) src, Bivec3 trg, f32 decay, f32 dt) {
    f32_glerp_ip(&src->xy, trg.xy, decay, dt);
    f32_glerp_ip(&src->xz, trg.xz, decay, dt);
    f32_glerp_ip(&src->yz, trg.yz, decay, dt);
}

// You should always normalize after this.
void rotor3_glerp_ip(rop(rw Rotor3) src, Rotor3 trg, f32 decay, f32 dt) {
    f32_glerp_ip(&src->s, trg.s, decay, dt);
    bivec3_glerp_ip(&src->bivec, trg.bivec, decay, dt);
}

// You should always normalize after this.
void rotor3_glerp_shortest_ip(rop(rw Rotor3) src, Rotor3 trg, f32 decay, f32 dt) {
    f32    diff_sign  = sign(rotor3_dot(*src, trg));
    Rotor3 signed_trg = rotor3_mul_f32(trg, diff_sign);

    f32_glerp_ip(&src->s, signed_trg.s, decay, dt);
    bivec3_glerp_ip(&src->bivec, signed_trg.bivec, decay, dt);
}

// You should always normalize after this.
f32 rotor3_move_towards_ip(rop(rw Rotor3) src, Rotor3 trg, f32 speed) {
    f32    diff       = rotor3_dot(*src, trg);
    f32    diff_sign  = sign(diff);
    Rotor3 signed_trg = rotor3_mul_f32(trg, diff_sign);

    Rotor3 diff_r = rotor3_sub(signed_trg, *src);
    rotor3_mul_f32_ip(&diff_r, speed);
    rotor3_normalize_ip(&diff_r);

    // TODO this does not account for sign. The clamp could be wrong
    src->s = clamp_top(src->s + diff_r.s, signed_trg.s);

    src->bivec.xy = clamp_top(src->bivec.xy + diff_r.bivec.xy, signed_trg.bivec.xy);
    src->bivec.yz = clamp_top(src->bivec.yz + diff_r.bivec.yz, signed_trg.bivec.xy);
    src->bivec.xz = clamp_top(src->bivec.xz + diff_r.bivec.xz, signed_trg.bivec.xy);

    return diff;
}

void vec3_glerp_ip(rop(rw Vec3) src, Vec3 trg, f32 decay, f32 dt) {
    f32_glerp_ip(&src->x, trg.x, decay, dt);
    f32_glerp_ip(&src->y, trg.y, decay, dt);
    f32_glerp_ip(&src->z, trg.z, decay, dt);
}

Bivec3 bivec3_glerp(Bivec3 src, Bivec3 trg, f32 decay, f32 dt) {
    return (Bivec3){
        .xy = f32_glerp(src.xy, trg.xy, decay, dt),
        .xz = f32_glerp(src.xz, trg.xz, decay, dt),
        .yz = f32_glerp(src.yz, trg.yz, decay, dt),
    };
}

Rotor3 rotor3_glerp(Rotor3 src, Rotor3 trg, f32 decay, f32 dt) {
    // TODO is this actually worth it?
    // Bivec3 new_src = src.bivec;
    // bivec3_glerp_ip(&new_src, trg->bivec, decay, dt);
    return (Rotor3){
        .s     = f32_glerp(src.s, trg.s, decay, dt),
        // .bivec = new_src,
        .bivec = bivec3_glerp(src.bivec, trg.bivec, decay, dt),
    };
}

Vec3 vec3_glerp(Vec3 src, Vec3 trg, f32 decay, f32 dt) {
    return (Vec3){
        .x = f32_glerp(src.x, trg.x, decay, dt),
        .y = f32_glerp(src.y, trg.y, decay, dt),
        .z = f32_glerp(src.z, trg.z, decay, dt),
    };
}

// Expensive constant angle Spherical-Linear interpolation with 0.0f <= t <= 1.0f
Rotor3 rotor3_spherical_lerp(Rotor3 src, Rotor3 trg, f32 t) {
    f32 dot = rotor3_dot(src, trg);
    if (dot < 1.0f) {
        trg = rotor3_mul_f32(trg, -1.0f);
        dot = -1.0f * dot;
    }
    if (dot > (1.0f - f32_EPSILON)) {
        return trg;
    }
    dot            = clamp(-1.0f, dot, 1.0f);
    f32    theta_0 = acosf(dot);
    f32    theta   = theta_0 * t;
    Rotor3 v2      = rotor3_normalize(rotor3_sub(trg, rotor3_mul_f32(src, dot)));
    // sincosf is non standard, compiler usually optimizes this to a single instruction anyways
    f32    s = sinf(theta), c = cosf(theta);

    Rotor3 n = src;
    n.s      = (c * src.s) + (s * v2.s);

    n.bivec.xy = (c * src.bivec.xy) + (s * v2.bivec.xy);
    n.bivec.xz = (c * src.bivec.xz) + (s * v2.bivec.xz);
    n.bivec.yz = (c * src.bivec.yz) + (s * v2.bivec.yz);

    return rotor3_normalize(n);
}

Rotor3 rotor3_spherical_glerp(Rotor3 src, Rotor3 trg, f32 decay, f32 dt) {
    f32 dot = rotor3_dot(src, trg);
    if (dot < 1.0f) {
        trg = rotor3_mul_f32(trg, -1.0f);
        dot = -1.0f * dot;
    }
    if (dot > (1.0f - f32_EPSILON)) {
        return trg;
    }
    dot            = clamp(-1.0f, dot, 1.0f);
    f32    theta_0 = acosf(dot);
    f32    theta   = theta_0 * expf(-1.0f * decay * dt);
    Rotor3 v2      = rotor3_normalize(rotor3_sub(trg, rotor3_mul_f32(src, dot)));
    // sincosf is non standard, compiler usually optimizes this to a single instruction anyways
    f32    s = sinf(theta), c = cosf(theta);

    Rotor3 n = src;
    n.s      = (c * src.s) + (s * v2.s);

    n.bivec.xy = (c * src.bivec.xy) + (s * v2.bivec.xy);
    n.bivec.xz = (c * src.bivec.xz) + (s * v2.bivec.xz);
    n.bivec.yz = (c * src.bivec.yz) + (s * v2.bivec.yz);

    return rotor3_normalize(n);
}

void rotor3_spherical_glerp_ip(rop(rw Rotor3) src, Rotor3 trg, f32 decay, f32 dt) {
    f32 dot = rotor3_dot(*src, trg);
    if (dot < 1.0f) {
        trg = rotor3_mul_f32(trg, -1.0f);
        dot = -1.0f * dot;
    }
    if (dot > (1.0f - f32_EPSILON)) {
        return;
    }
    dot            = clamp(-1.0f, dot, 1.0f);
    f32    theta_0 = acosf(dot);
    f32    theta   = theta_0 * expf(-1.0f * decay * dt);
    Rotor3 v2      = rotor3_normalize(rotor3_sub(trg, rotor3_mul_f32(*src, dot)));
    // sincosf is non standard, compiler usually optimizes this to a single instruction anyways
    f32    s = sinf(theta), c = cosf(theta);

    src->s = (c * src->s) + (s * v2.s);

    src->bivec.xy = (c * src->bivec.xy) + (s * v2.bivec.xy);
    src->bivec.xz = (c * src->bivec.xz) + (s * v2.bivec.xz);
    src->bivec.yz = (c * src->bivec.yz) + (s * v2.bivec.yz);

    rotor3_normalize_ip(src);
}
