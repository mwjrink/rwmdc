#pragma once

#include <lib/grim/bp.h>
#include <lib/grim/mem/arena.h>
#include <stdbool.h>
#include <vulkan/vulkan.h>

typedef struct GrimWindow GrimWindow;

typedef struct GraphicsContext {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceMemoryProperties memory_properties;
    u32 graphics_family;
    u32 present_family;
    u32 timestamp_bits;
    VkQueue graphics_queue;
    VkQueue present_queue;
#ifdef __DEBUG
    VkDebugUtilsMessengerEXT debug_messenger;
#endif
} GraphicsContext;

typedef struct Buffer {
    VkBuffer handle;
    VkDeviceMemory memory;
    u64 size;
    VkDeviceAddress address;
    void *mapped;
    u64 allocation_size;
    VkMemoryPropertyFlags properties;
} Buffer;

typedef struct Image {
    VkImage handle;
    VkImageView view;
} Image;

typedef struct Pipeline {
    VkPipeline handle;
    VkPipelineLayout layout;
} Pipeline;


typedef struct Swapchain {
    VkSwapchainKHR handle;
    Image *images;
    VkSemaphore *render_finished;
    u32 image_count;
    VkPresentModeKHR present_mode;
} Swapchain;

typedef struct RenderTarget {
    GrimWindow *window;
    VkSurfaceKHR surface;
    Swapchain swapchain;
    VkExtent2D extent;
    VkFormat format;
    VkColorSpaceKHR color_space;
} RenderTarget;

typedef struct FrameSlot {
    VkCommandPool pool;
    VkCommandBuffer command_buffer;
    VkFence fence;
    VkSemaphore image_available;
} FrameSlot;

typedef struct RenderContext {
    const GraphicsContext *ctx;
    RenderTarget *target;
    u32 frames_in_flight;
    bool render_target_resized;
    FrameSlot slots[2];
} RenderContext;

typedef struct RenderState {
    RenderContext *r_ctx;
    u64 frame_count;
    u32 image_idx;
} RenderState;

RenderTarget window_create(Arena *arena, u32 width, u32 height);
GraphicsContext graphics_context_create(Arena *arena, RenderTarget *target);
RenderContext render_context_create(Arena *arena, const GraphicsContext *ctx, RenderTarget *target);
RenderState create_render_state(Arena *arena, RenderContext *rc);
bool start_frame(Arena *arena, RenderState *rs);
VkCommandBuffer render_command_buffer(const RenderState *rs);
void enqueue_submit_graphics(RenderState *rs);
void end_frame(Arena *arena, RenderState *rs);
void cleanup_render_state(RenderState *rs);
void cleanup_render_context(RenderContext *rc);
void cleanup_render_target(const GraphicsContext *ctx, RenderTarget *target);
void cleanup_graphics_ctx(GraphicsContext *ctx);
Buffer buffer_create(const GraphicsContext *ctx, u64 size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
void buffer_host_copy(const GraphicsContext *ctx, Buffer *buffer, const void *data, u64 bytes, u64 offset);
void cleanup_buffer(const GraphicsContext *ctx, Buffer *buffer);
VkImageView create_image_view(const GraphicsContext *ctx, VkImage image, VkFormat format);
VkShaderModule read_shader(Arena *arena, const GraphicsContext *ctx, const char *path);
void cleanup_pipeline(const GraphicsContext *ctx, Pipeline *pipeline);
