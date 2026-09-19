#pragma once

#include <lib/grim/bp.h>
#include <lib/grim/gfx/graphics.h>
#include <lib/grim/math.h>

#include <meshoptimizer.h>

// We should combine all objects into one asset file?
typedef struct OutFileHeader {
    u32 version;
    // u32 file_size; // in 4096 byte chunks?
    // u32 num_clusters;
    // u32 num_textures;
    // u32 num_materials;

    // TODO a separate dir file that lists string name/path to model idx?
    u32 num_models;

    u64 models_offset;
    u64 textures_offset;
    u64 materials_offset;
    u64 vertices_offset;
    u64 indices_offset;
    u64 clusters_offset;
} OutFileHeader;
STATIC_ASSERT(sizeof(OutFileHeader) == 56);

// typedef struct Vertex {
//     Vec3 pos;
//     Vec3 nrm;
//     Vec2 uv;
//
//     // Vec3 tan;
//     // Vec3 bit;
// } Vertex;

typedef struct OutMaterialsHeader {
    u32 materials_count;
} OutMaterialsHeader;

typedef struct OutTexturesHeader {
    u32 textures_count;
} OutTexturesHeader;

typedef struct OutClustersHeader {
    u32 clusters_count;
} OutClustersHeader;

// TODO shared lib/header for this
// typedef struct Material {
//     f32 metallic;
//     f32 roughness;
//     // TODO do I even need this here? Is this just for flashing an enemy red?
//     ie only at runtime?
//     // _Alignas(sizeof(Vec4)) Vec4 base_color;
//     u32 metallic_roughness_texture_idx;
//     u32 base_color_texture_idx;
//     u32 normal_texture_idx;
// } Material;

typedef struct Texture {
    u32 width;
    u32 height;
    u32 depth;
} Texture;

typedef struct ClusterBounds {
    Vec3 center;
    f32  radius;

    // more expensive but better results:
    // f32 cone_apex[3];

    // f32 cone_axis[3];
    // f32 cone_cutoff;

    i8 cone_axis_s8[3];
    i8 cone_cutoff_s8;
} ClusterBounds;
STATIC_ASSERT(sizeof(ClusterBounds) == 20);

// TODO What can we reasonably use for idx type?
// Should we just duplicate edge verts?
// I think that would ultimately be less data, right?
// Lets us use u8
typedef struct Cluster {
    // Vertex* vertices;
    // u8*     indices;

    u32 idx_offset;
    u32 vtx_offset;

    u8 vtx_count;
    u8 tri_count;

    u8 _padding[2];

    ClusterBounds bounds;
    // struct meshopt_Bounds bounds;
} Cluster;
STATIC_ASSERT(sizeof(Cluster) == 32);

typedef struct OutFileModel {
    u32 materials_count;
    u32 textures_count;

    u64 cls_offset;
    u64 vtx_offset;
    u64 idx_offset;

    u32 cls_count;
    u32 vtx_count;
    u32 idx_count;

    u32 mat_idx;

    Vec3 aabb_min;
    Vec3 aabb_max;

    // u32* cluster_idx;
    // u32* materials_idx;
    // u32* textures_idx;
} OutFileModel;
STATIC_ASSERT(sizeof(OutFileModel) == 72);

typedef struct Model {
    OutFileModel header;

    Vertex*  vertices;
    u8*      indices;
    Cluster* clusters;

    // u32  mat_idx;
    // Vec3 aabb_min;
    // Vec3 aabb_max;

    // OutClustersHeader header;
} Model;
STATIC_ASSERT(sizeof(Model) == 96);

typedef struct FileAssetRepo {
    OutFileHeader header;
    FILE*         file_handle;
} FileAssetRepo;

typedef struct AssetRepo {
    Model* models;
    u32    model_count;

    Material* materials;
    u32       material_count;

    Texture* textures;
    u32      texture_count;
} AssetRepo;
