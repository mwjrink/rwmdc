#pragma once

#include <lib/grim/bp.h>
#include <lib/grim/gfx/graphics.h>
#include <lib/grim/logger.h>
#include <lib/grim/mem/arena.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

// NOTE This comes last so that it overrides any other defined assert
#include <lib/grim/assert.h>

// #include <aftermath/GFSDK_Aftermath.h>
// #include <aftermath/GFSDK_Aftermath_GpuCrashDump.h>

#define GPU_MAX_ALLOC_SIZE GB(4)

// ====================================================================================================================
// =                                                  Macros                                                          =
// ====================================================================================================================

// #define AFTERMATH 1
#if AFTERMATH
#define check_vkresult(result, message)                                                                                \
    if (result != VK_SUCCESS) {                                                                                        \
        CRITICAL_LOG(message);                                                                                         \
        DEBUG_LOG("Failed with result: %i", result);                                                                   \
        if (result == VK_ERROR_DEVICE_LOST) {                                                                          \
            DEBUG_LOG("DEVICE LOST");                                                                                  \
            GFSDK_Aftermath_CrashDump_Status status = GFSDK_Aftermath_CrashDump_Status_Unknown;                        \
            GFSDK_Aftermath_GetCrashDumpStatus(&status);                                                               \
            DEBUG_LOG("WAITING! status: %u", status);                                                                  \
                                                                                                                       \
            u64 total_wait_time = 0;                                                                                   \
            u32 timeout         = false;                                                                               \
            while (status != GFSDK_Aftermath_CrashDump_Status_CollectingDataFailed &&                                  \
                   status != GFSDK_Aftermath_CrashDump_Status_Finished && !timeout) {                                  \
                sleep(5);                                                                                              \
                DEBUG_LOG("WAITING! status: %u", status);                                                              \
                total_wait_time += 5;                                                                                  \
                if (total_wait_time > 5 * 60) {                                                                        \
                    timeout = true;                                                                                    \
                }                                                                                                      \
                GFSDK_Aftermath_GetCrashDumpStatus(&status);                                                           \
            }                                                                                                          \
        }                                                                                                              \
        exit(1);                                                                                                       \
    }
#else
#define check_vkresult(result, scope, message)                                                                         \
    if (result != VK_SUCCESS) {                                                                                        \
        CRITICAL_LOG(scope, message);                                                                                  \
        DEBUG_LOG(scope, "Failed with result: %i", result);                                                            \
        exit(1);                                                                                                       \
    }
#endif

// ====================================================================================================================
// =                                                  External                                                        =
// ====================================================================================================================

struct vk_func_ptrs {
    PFN_vkCreateDebugUtilsMessengerEXT     _vkCreateDebugUtilsMessengerEXT;
    PFN_vkDestroyDebugUtilsMessengerEXT    _vkDestroyDebugUtilsMessengerEXT;
    PFN_vkCmdDrawMeshTasksIndirectCountEXT _vkCmdDrawMeshTasksIndirectCountEXT;
} vk_func_ptrs = {0};

// ====================================================================================================================
// =                                                  Functions                                                       =
// ====================================================================================================================

// ================  Windowing  ================

GrimWindow window_open(u32 width, u32 height);
void       window_setup_listeners(rop(rw GrimWindow) window);
void       window_poll_events(rop(rw GrimWindow) window);
void       close_window(GrimWindow* window);

VkSurfaceKHR surface_create(ro VkInstance instance, rop(ro GrimWindow) window);

// ================  Context  ================

VkInstance     create_instance(rop(rw Arena) arena);
PhysicalDevice select_physical_device(rop(rw Arena) arena, ro VkInstance instance);

void init_ext_func_ptrs(VkInstance instance);

QueueFamilies find_queue_families(rop(rw Arena) arena, ro VkSurfaceKHR surface, ro VkPhysicalDevice physical_device);
VkDevice      create_logical_device(rop(rw Arena) arena,
                                    rop(rw PhysicalDevice) physical_device,
                                    rop(rw QueueFamilies) queue_families);

// ================  Debug  ================

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT      message_severity,
                                              VkDebugUtilsMessageTypeFlagsEXT             message_type,
                                              const VkDebugUtilsMessengerCallbackDataEXT* cb_data,
                                              void*                                       user_data);

void                     init_debug_func_ptrs(ro VkInstance instance);
VkDebugUtilsMessengerEXT setup_debug_messenger(ro VkInstance instance);

// ================  Image  ================

VkImageView create_image_view(rop(ro GraphicsContext) ctx, ro VkImage image, ro VkFormat format);
void        cleanup_image(rop(ro GraphicsContext) ctx, rop(rw Image) image);
void        cleanup_image_view(rop(ro GraphicsContext) ctx, rop(rw Image) image);

// ================  Command Pool & Buffer ================

void command_buffer_begin_record(rop(ro GraphicsContext) ctx, rop(ro CommandBuffer) command_buffer, u32 one_time);
void command_buffer_end_record(rop(ro GraphicsContext) ctx, rop(ro CommandBuffer) command_buffer);

// ================  Commands  ================

void cmd_draw(rop(ro RecordingState) state, u32 instance_count);
void cmd_bind_pipeline(rop(ro GraphicsContext) ctx, rop(ro RecordingState) state, rop(ro Pipeline) pipeline);
void cmd_image_transition(rop(ro GraphicsContext) ctx,
                          rop(ro CommandBuffer) cmd_buffer,
                          VkImage       image,
                          VkImageLayout src_layout,
                          VkImageLayout dst_layout);

// ================  Pipeline  ================

VkFormat format_pick(rop(ro GraphicsContext) ctx,
                     AllocBuff            candidates,
                     VkImageTiling        tiling,
                     VkFormatFeatureFlags features);
u32      format_is_depth(VkFormat format);
VkFormat format_pick_depth(rop(ro GraphicsContext) ctx);
u32      format_has_stencil_component(VkFormat format);

void cleanup_pipeline(rop(ro GraphicsContext) ctx, rop(rw Pipeline) pipeline);

// ================  Render Target  ================

VkSurfaceKHR wayland_surface_create(ro VkInstance instance, rop(ro GrimWindow) window);

// ================ Swapchain ================

SwapchainGarbage recreate_swapchain(rop(rw Arena) arena,
                                    rop(ro GraphicsContext) ctx,
                                    rop(rw RenderTarget) render_target);
u8               check_dispose_ready(rop(ro GraphicsContext) ctx, rop(ro SwapchainGarbage) garbage);
void             dispose_swapchain_garbage(rop(ro GraphicsContext) ctx, rop(rw SwapchainGarbage) garbage);

void create_swapchain(rop(rw Arena) arena,
                      rop(ro GraphicsContext) ctx,
                      ro VkSurfaceKHR surface,
                      rop(rw RenderTarget) render_target);
void swapchain_get_images(rop(rw Arena) arena, rop(ro GraphicsContext) ctx, rop(rw RenderTarget) render_target);

void cleanup_swapchain(rop(ro GraphicsContext) ctx, rop(rw Swapchain) swapchain);

// ================  Shaders  ================

VkShaderModule read_shader(rop(rw Arena) arena, rop(ro GraphicsContext) ctx, rop(ro char) path);

void cleanup_shader(rop(ro GraphicsContext) ctx, rop(rw VkShaderModule) shader);

// ================  Sync Objects  ================

SyncObjects create_sync_objects(rop(ro GraphicsContext) ctx);
void        recreate_sync_objects_semaphores(rop(ro GraphicsContext) ctx, rop(rw SyncObjects) sync_objects);
void        recreate_present_complete(rop(ro GraphicsContext) ctx, rop(rw SyncObjects) sync_object);

void cleanup_sync_objects(rop(ro GraphicsContext) ctx, rop(rw SyncObjects) sync_objects);

// ================  Buffers  ================

void buffer_copy(rop(rw Arena) arena, rop(ro GraphicsContext) ctx, Buffer dst, Buffer src, u64 size, u64 offset);
void buffer_copy_with_staging(rop(rw Arena) arena, rop(ro GraphicsContext) ctx, Buffer dst_buffer, void* src, u64 size);

void* buffer_map(rop(ro GraphicsContext) ctx, rop(rw Buffer) buffer);
void  buffer_unmap(rop(ro GraphicsContext) ctx, rop(rw Buffer) buffer);
void  buffer_host_copy(rop(ro GraphicsContext) ctx, rop(ro Buffer) buffer, void* src, u64 byte_len, u64 offset);

Buffer buffer_create(rop(ro GraphicsContext) ctx, u64 size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
void   cleanup_buffer(rop(ro GraphicsContext) ctx, rop(rw Buffer) buffer);

// ================  Rendering  ================

void     process_garbage(rop(rw RenderState) render_state);
void     empty_garbage(rop(rw RenderState) render_state);
void     enqueue_submit_graphics(rop(ro GraphicsContext) ctx,
                                 rop(ro RenderTarget) render_target,
                                 rop(ro CommandBuffer) buffers,
                                 ro u32 frame_idx,
                                 ro u32 image_idx);
VkResult enqueue_present(rop(ro GraphicsContext) ctx, rop(ro RenderTarget) render_target, ro u32 image_idx);

// ================  Utils  ================

void log_available_layers();
void log_available_extensions();

const char* present_mode_to_str(ro VkPresentModeKHR present_mode);
const char* surface_format_to_str(ro VkFormat surface_format);
const char* colorspace_to_str(ro VkColorSpaceKHR colorspace);

// ====================================================================================================================
// =                                                  Includes =
// ====================================================================================================================

#include <lib/grim/gfx/aftermath.h>
#include <lib/grim/gfx/buffer.h>
#include <lib/grim/gfx/command_buffer.h>
#include <lib/grim/gfx/commands.h>
#include <lib/grim/gfx/context.h>
#include <lib/grim/gfx/debug.h>
#include <lib/grim/gfx/enqueue.h>
#include <lib/grim/gfx/ext.h>
#include <lib/grim/gfx/image.h>
#include <lib/grim/gfx/pipeline.h>
#include <lib/grim/gfx/render_graph.h>
#include <lib/grim/gfx/render_target.h>
#include <lib/grim/gfx/rendering.h>
#include <lib/grim/gfx/shader.h>
#include <lib/grim/gfx/swapchain.h>
#include <lib/grim/gfx/sync_objects.h>
#include <lib/grim/gfx/types.h>
#include <lib/grim/gfx/vkutils.h>
