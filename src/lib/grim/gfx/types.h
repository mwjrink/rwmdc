#pragma once

#include <lib/grim/assert.h>
#include <lib/grim/bp.h>
#include <lib/grim/math.h>
#include <lib/grim/mem/buff.h>

#include <stddef.h>
#include <vulkan/vulkan.h>

// ====================================================================================================================
// =                                                  Structs                                                         =
// ====================================================================================================================

typedef struct GrimWindow GrimWindow;

typedef struct PhysicalDevicePropFeats {
    // Props
    VkPhysicalDeviceProperties2                        device_properties2;
    VkPhysicalDeviceVulkan11Properties                 vk_11_properties;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR acceleration_structure_properties;
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR    ray_tracing_pipeline_properties;
    VkPhysicalDeviceDescriptorIndexingProperties       descriptor_indexing_properties;
    VkPhysicalDeviceMeshShaderPropertiesEXT            mesh_shader_properties;

    // Features
    VkPhysicalDeviceFeatures2                            device_features2;
    VkPhysicalDeviceVulkan11Features                     vulkan_11_features;
    VkPhysicalDeviceVulkan12Features                     vulkan_12_features;
    VkPhysicalDeviceVulkan13Features                     vulkan_13_features;
    VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR     swapchain_maintenance_1;
    // this is for rust shaders
    // VkPhysicalDeviceShaderFloat16Int8Features    VkPhysicalDeviceShaderFloat16Int8Features;
    // VkPhysicalDeviceScalarBlockLayoutFeatures scalar_block_layout_features;
    // VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_features;
    // VkPhysicalDeviceImagelessFramebufferFeatures imageless_framebuffer_features;
    // VkPhysicalDeviceVulkanMemoryModelFeatures    vulkan_memory_model_features;
    // VkPhysicalDeviceBufferDeviceAddressFeatures          buffer_device_address_features;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR     acceleration_structure_features;
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR        ray_tracing_pipeline_features;
    VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT pageable_device_local_memory_features;
    // VkPhysicalDeviceHostQueryResetFeatures               host_query_reset_features; // promoted to 1.2
    // VkPhysicalDeviceMultiviewFeaturesKHR                 multiview_features; // promoted to 1.1
    VkPhysicalDeviceMeshShaderFeaturesEXT                mesh_shader_features;
    VkPhysicalDeviceRayQueryFeaturesKHR                  ray_query_features;
    VkPhysicalDeviceFragmentShadingRateFeaturesKHR       fragment_shading_rate_features;
    VkPhysicalDeviceShaderObjectFeaturesEXT              shader_object_features;
    VkPhysicalDeviceDepthClipEnableFeaturesEXT           depth_clip_enable_features;
    VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR   ray_tracing_position_fetch_features;
    // VkPhysicalDeviceBufferDeviceAddressFeatures          physical_device_buffer_device_address_features;
} PhysicalDevicePropFeats;
// STATIC_ASSERT(sizeof(PhysicalDevicePropFeats) == 2240);

typedef struct PhysicalDevice {
    VkPhysicalDevice handle;
    rop(ro PhysicalDevicePropFeats) prop_feats;
} PhysicalDevice;
STATIC_ASSERT(sizeof(PhysicalDevice) == 16);

typedef struct CommandBuffer {
    VkCommandBuffer handle;
} CommandBuffer;
STATIC_ASSERT(sizeof(CommandBuffer) == 8);

typedef struct CommandPool {
    VkCommandPool  handle;
    CommandBuffer* command_buffers;
    u32            command_buffer_count;
    u32            _padding;
} CommandPool;
STATIC_ASSERT(sizeof(CommandPool) == 24);

typedef struct QueueFamily {
    struct {
        u8  supports_present : 1;
        u8  cap              : 7;
        u16 _padding         : 16;
    };
    VkQueueFlags flags; // u32
    CommandPool  command_pool;
} QueueFamily;
STATIC_ASSERT(sizeof(QueueFamily) == 32);

typedef struct QueueFamilies {
    u16          graphics_idx;
    u16          present_idx;
    u16          transfer_idx;
    u16          len;
    QueueFamily* families;
    VkQueue*     queues;
} QueueFamilies;
STATIC_ASSERT(sizeof(QueueFamilies) == 24);

typedef struct GraphicsContext {
    VkInstance     instance;
    PhysicalDevice physical_device;
    VkDevice       device;
    QueueFamilies  queue_families;

#ifdef __DEBUG
    VkDebugUtilsMessengerEXT debug_messenger;
#endif
} GraphicsContext;
#ifdef __DEBUG
STATIC_ASSERT(sizeof(GraphicsContext) == 64);
#else
STATIC_ASSERT(sizeof(GraphicsContext) == 56);
#endif

typedef struct BuffPtr {
    // TODO store a VkDeviceAddress here
    u64      offset;
    VkBuffer handle;
} BuffPtr;
STATIC_ASSERT(sizeof(BuffPtr) == 16);

typedef struct BuffSlice {
    union {
        BuffPtr ptr;
        struct {
            u64      offset;
            VkBuffer handle;
        };
    };
    u64 size;
} BuffSlice;
STATIC_ASSERT(sizeof(BuffSlice) == 24);

typedef struct BuffAddressSlice {
    // VkBuffer        handle; // NEW: handle of backing buffer
    // VkDeviceAddress address; // NEW: address of backing buffer
    // VkDeviceAddress offset; // NEW: offset from address
    VkDeviceAddress address; // CURRENT: address of this slice
    u64             size;
} BuffAddressSlice;
STATIC_ASSERT(sizeof(BuffAddressSlice) == 16);

typedef struct Buffer {
    VkBuffer        handle;
    VkDeviceAddress address;
    u64             size;
    VkDeviceMemory  memory;
    VkBufferView    view;
    void*           mapped;
} Buffer;
STATIC_ASSERT(sizeof(Buffer) == 48);

// TODO unify materials and how they work in my engine
typedef struct Material {
    f32 metallic;
    f32 roughness;

    u32 metallic_roughness_texture_idx;

    u32 base_color_texture_idx;
    // alignas(sizeof(Vec4)) Vec4 base_color;

    // TODO make it so the normal texture is always the NEXT texture after base
    // Same with metallic rougness, then we can get rid of 2 idxs here
    u32 normal_texture_idx;
    // u32 _padding[3];
} Material;
STATIC_ASSERT(sizeof(Material) == 20);

typedef struct Vertex {
    Vec3 pos;
    Vec3 nrm;
    Vec2 uv;
} Vertex;
STATIC_ASSERT(sizeof(Vertex) == 32);

typedef struct Image {
    VkImage        handle;
    VkImageView    view;
    VkDeviceMemory memory;
    VkFormat       format;
    u32            width;
    u32            height;
    u32            depth;
} Image;
STATIC_ASSERT(sizeof(Image) == 40);

typedef struct Pipeline {
    VkPipeline       handle;
    VkShaderModule*  shaders;
    VkPipelineLayout layout;
    u32              stage_count;
} Pipeline;
STATIC_ASSERT(sizeof(Pipeline) == 32);

typedef struct SyncObjects {
    // TODO split these up and store them in different places. This struct is stupid
    // swapchain for image avail
    VkSemaphore signal_on_image_available;
    // Queue for internal sync
    VkSemaphore signal_on_render_finish;
    // CommandBuffer for previous submit
    VkFence     open_on_render_finish;
    // Presentation completed
    VkFence     open_on_present_complete;
} SyncObjects;
STATIC_ASSERT(sizeof(SyncObjects) == 32);

typedef struct Swapchain {
    VkSwapchainKHR handle;
    Image*         images;
    SyncObjects*   sync_objects;
    u32            image_count;
    u32            _padding;
} Swapchain;
STATIC_ASSERT(sizeof(Swapchain) == 32);

typedef struct RenderTarget {
    GrimWindow*     window;
    VkSurfaceKHR    surface;
    Swapchain       swapchain;
    VkExtent2D      extent;
    VkFormat        format;
    VkColorSpaceKHR color_space;
} RenderTarget;
STATIC_ASSERT(sizeof(RenderTarget) == 64);

// TODO shrink this struct to essentials
typedef struct RecordingState {
    rwp(ro CommandBuffer) cmd_buffer;
    rwp(ro Pipeline) pipeline;
    rwp(ro RenderTarget) render_target;
    u32 frame_idx;
    struct {
        u8  currently_recording : 1;
        u32 _padding            : 31;
    };
} RecordingState;
STATIC_ASSERT(sizeof(RecordingState) == 32);

typedef struct SwapchainGarbage {
    VkImageView    image_views[3];
    VkSemaphore    signal_on_image_availables[3];
    VkSemaphore    signal_on_render_finishes[3];
    VkFence        open_on_present_complete[3];
    VkSwapchainKHR swapchain;
    u32            garbage_len;
    u32            _padding;
} SwapchainGarbage;
STATIC_ASSERT(sizeof(SwapchainGarbage) == 112);

typedef struct RenderContext {
    rop(ro GraphicsContext) ctx;
    rop(rw RenderTarget) target;
    rop(rw Pipeline) glyph_pipeline;
    rop(rw Pipeline) rect_pipeline;
    struct {
        u8  render_target_resized : 1;
        u32 frames_in_flight      : 4;
        u64 _padding              : 59;
    };
} RenderContext;
STATIC_ASSERT(sizeof(RenderContext) == 40);

typedef struct PushConstant_TextVert {
    VkDeviceAddress draw_glyphs;
    VkDeviceAddress font_glyphs_bounds;

    Vec2 pixel_to_ndc;
    Vec2 scroll_offset;

    u32 draw_glyph_count;
} PushConstant_TextVert;
STATIC_ASSERT(sizeof(PushConstant_TextVert) <= 128);

typedef struct PushConstant_TextFrag {
    VkDeviceAddress font_glyph_bands;
    VkDeviceAddress font_curves;
    VkDeviceAddress font_bands;
} PushConstant_TextFrag;
STATIC_ASSERT(sizeof(PushConstant_TextFrag) <= 128);

typedef struct PushConstant_Rect {
    VkDeviceAddress rects;

    Vec2 pixel_to_ndc;
    Vec2 scroll_offset;

    u32 rect_count;
} PushConstant_Rect;
STATIC_ASSERT(sizeof(PushConstant_Rect) <= 128);

typedef struct GlyphDrawCmd {
    f32 x;
    f32 y;
    f32 sx;
    f32 sy;

    u32 glyph_idx;
    u32 color;
} GlyphDrawCmd;
STATIC_ASSERT(sizeof(GlyphDrawCmd) == 24);

typedef struct GpuRect {
    f32 x;
    f32 y;
    f32 width;
    f32 height;

    u32 color;
    u32 flags;
} GpuRect;
STATIC_ASSERT(sizeof(GpuRect) == 24);

typedef struct GlyphBounds {
    f32 min_x;
    f32 min_y;
    f32 max_x;
    f32 max_y;
} GlyphBounds;
STATIC_ASSERT(sizeof(GlyphBounds) == 16);

typedef struct GlyphBands {
    f32 scale_x;
    f32 scale_y;
    f32 offset_x;
    f32 offset_y;

    u32 origin;
    u32 counts;
} GlyphBands;
STATIC_ASSERT(sizeof(GlyphBands) == 24);

typedef struct PackedCurve {
    u32 p0;
    u32 p1;
    u32 p2;
} PackedCurve;
STATIC_ASSERT(sizeof(PackedCurve) == 12);

typedef struct RenderState {
    rop(rw RenderContext) r_ctx;

    u32 image_idx;
    u32 frame_count;

    Buffer slug_buff;

    PushConstant_TextVert pc_vert;
    PushConstant_TextFrag pc_frag;
    PushConstant_Rect     pc_rect;

    RingBuff swapchain_garbage;
} RenderState;
STATIC_ASSERT(sizeof(RenderState) == 184);

typedef struct FontAtlas {
    u8* data; // contiguous block holding all four regions
    u64 total_bytes;

    u32 glyph_count;
    u32 curve_count;
    u32 band_word_count;
} FontAtlas;
STATIC_ASSERT(sizeof(FontAtlas) == 32);
