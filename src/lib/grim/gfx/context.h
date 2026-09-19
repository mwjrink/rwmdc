#pragma once

#include <lib/grim/gfx/internal_graphics.h>

#if AFTERMATH
#include <aftermath/GFSDK_Aftermath.h>
#include <aftermath/GFSDK_Aftermath_GpuCrashDump.h>
#include <lib/grim/gfx/aftermath.h>
#endif

VkInstance create_instance(rop(rw Arena) arena) {
    u64 create_instance_profiling = time_start();
    arena_ckpt(arena);
    time_checkpoint(SCOPE_DEBUG, "arena_ckpt", create_instance_profiling);

#ifdef PRINT_AVAILABLE_LAYERS
    log_available_layers();
#endif

#ifdef PRINT_AVAILABLE_EXTENSIONS
    log_available_extensions();
#endif

    VkApplicationInfo app_info  = {0};
    app_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName   = "rwmd";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
    app_info.pEngineName        = "rwmd";
    app_info.engineVersion      = VK_MAKE_VERSION(0, 0, 1);
    app_info.apiVersion         = VK_API_VERSION_1_4;

    VkInstanceCreateInfo create_info = {0};
    create_info.sType                = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo     = &app_info;

    AllocDynList ext_names = alloc_list_create_with_cap(arena, sizeof(char*), 8);

#ifdef __linux__

#if defined(WAYLAND)
    alloc_list_push_ptr(arena, &ext_names, "VK_KHR_surface");
    alloc_list_push_ptr(arena, &ext_names, "VK_KHR_wayland_surface");
#elif defined(X11)
    alloc_list_push_ptr(arena, &ext_names, "VK_KHR_surface");
    alloc_list_push_ptr(arena, &ext_names, "VK_KHR_xlib_surface");
    // OR
    alloc_list_push_ptr(arena, &ext_names, "VK_KHR_xcb_surface");
#endif

#elif defined(__APPLE__)
    alloc_list_push_ptr(arena, &ext_names, "VK_KHR_surface");
    alloc_list_push_ptr(arena, &ext_names, "VK_MVK_macos_surface");
    // OR
    alloc_list_push_ptr(arena, &ext_names, "VK_EXT_metal_surface");

    alloc_list_push_ptr(arena, &ext_names, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#elif defined(_WIN32)
    alloc_list_push_ptr(arena, &ext_names, "VK_KHR_surface");
    alloc_list_push_ptr(arena, &ext_names, "VK_KHR_win32_surface");
#endif

    AllocDynList layer_names = alloc_list_create_with_cap(arena, sizeof(char*), 8);
#ifdef __DEBUG
    alloc_list_push_ptr(arena, &ext_names, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    alloc_list_push_ptr(arena, &layer_names, "VK_LAYER_KHRONOS_validation");
    // alloc_list_push_ptr(arena, &layer_names, "VK_LAYER_LUNARG_api_dump");
    // alloc_list_push_ptr(arena, &layer_names, "VK_LAYER_LUNARG_monitor");
    // alloc_list_push_ptr(arena, &layer_names, "VK_LAYER_LUNARG_screenshot");
#endif
    // alloc_list_push_ptr(arena, &ext_names, VK_EXT_DEVICE_ADDRESS_BINDING_REPORT_EXTENSION_NAME);

    alloc_list_push_ptr(arena, &ext_names, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);

    create_info.enabledExtensionCount   = ext_names.len;
    create_info.ppEnabledExtensionNames = ext_names.data;

    create_info.enabledLayerCount   = layer_names.len;
    create_info.ppEnabledLayerNames = layer_names.data;

#ifdef __DEBUG
    VkDebugUtilsMessengerCreateInfoEXT debug_create_info = {0};
    debug_create_info.sType                              = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debug_create_info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug_create_info.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug_create_info.pfnUserCallback = debug_callback;
    debug_create_info.pUserData       = NULL;

    create_info.pNext = &debug_create_info;

    {
        // enabled_validation_features_count
        u32                           evf_count = 0;
        VkValidationFeatureEnableEXT* evf       = arena_alloc_aligned(arena, VkValidationFeatureEnableEXT, 8);
        // evf[evf_count]                          = VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT;
        // evf_count += 1;
        // evf[evf_count] = VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT;
        // evf_count += 1;
        evf[evf_count]                          = VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT;
        evf_count += 1;
#if defined(__SHADER_PRINT)
        evf[evf_count] = VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT;
        evf_count += 1;
#endif
        evf[evf_count] = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        evf_count += 1;
        VkValidationFeaturesEXT validation_features       = {0};
        validation_features.sType                         = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        validation_features.enabledValidationFeatureCount = evf_count;
        validation_features.pEnabledValidationFeatures    = evf;

        debug_create_info.pNext = &validation_features;
    }
#endif

    time_checkpoint(SCOPE_DEBUG, "setup", create_instance_profiling);

    VkInstance instance = {0};
    // Second param here is allocator, provide arena impl?
    // It SHOULD NOT matter:
    // https://docs.vulkan.org/spec/latest/chapters/memory.html#memory-allocation
    VkResult   result   = vkCreateInstance(&create_info, NULL, &instance);
    check_vkresult(result, SCOPE_GFX_INIT, "Failed to create vk instance. Shutting down.");

    time_checkpoint(SCOPE_DEBUG, "vk_create_instance", create_instance_profiling);

    arena_pop(arena);

    return instance;
}

void sanitize_pd_prop_feats(rop(rw PhysicalDevicePropFeats) prop_feats) {
    *prop_feats = (PhysicalDevicePropFeats){0};

    // Props
    void* next_prop = NULL;

    // clang-format off
    prop_feats->vk_11_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
    prop_feats->vk_11_properties.pNext = next_prop;
    next_prop = &prop_feats->vk_11_properties;

    // prop_feats->acceleration_structure_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
    // prop_feats->acceleration_structure_properties.pNext = next_prop;
    // next_prop = &prop_feats->acceleration_structure_properties;

    // prop_feats->ray_tracing_pipeline_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
    // prop_feats->ray_tracing_pipeline_properties.pNext = next_prop;
    // next_prop = &prop_feats->ray_tracing_pipeline_properties;

    // prop_feats->descriptor_indexing_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;
    // prop_feats->descriptor_indexing_properties.pNext = next_prop;
    // next_prop = &prop_feats->descriptor_indexing_properties;

    // prop_feats->mesh_shader_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT;
    // prop_feats->mesh_shader_properties.pNext = next_prop;
    // next_prop = &prop_feats->mesh_shader_properties;

    prop_feats->device_properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    prop_feats->device_properties2.pNext = next_prop;

    // Features
    void* next_feat = NULL;

    prop_feats->vulkan_11_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    prop_feats->vulkan_11_features.pNext = next_feat;
    next_feat = &prop_feats->vulkan_11_features;

    prop_feats->vulkan_12_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    prop_feats->vulkan_12_features.pNext = next_feat;
    next_feat = &prop_feats->vulkan_12_features;

    prop_feats->vulkan_13_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    prop_feats->vulkan_13_features.pNext = next_feat;
    next_feat = &prop_feats->vulkan_13_features;

    prop_feats->swapchain_maintenance_1.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
    prop_feats->swapchain_maintenance_1.pNext = next_feat;
    next_feat = &prop_feats->swapchain_maintenance_1;

    // prop_feats->shader_float16int8features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16INT8FEATURES;
    // prop_feats->shader_float16int8features.pNext = next_feat;
    // next_feat = &prop_feats->shader_float16int8features;

    // prop_feats->scalar_block_layout_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES;
    // prop_feats->scalar_block_layout_features.pNext = next_feat;
    // next_feat = &prop_feats->scalar_block_layout_features;

    // prop_feats->descriptor_indexing_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    // prop_feats->descriptor_indexing_features.pNext = next_feat;
    // next_feat = &prop_feats->descriptor_indexing_features;

    // prop_feats->imageless_framebuffer_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES;
    // prop_feats->imageless_framebuffer_features.pNext = next_feat;
    // next_feat = &prop_feats->imageless_framebuffer_features;

    // prop_feats->vulkan_memory_model_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES;
    // prop_feats->vulkan_memory_model_features.pNext = next_feat;
    // next_feat = &prop_feats->vulkan_memory_model_features;

    // prop_feats->buffer_device_address_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    // prop_feats->buffer_device_address_features.pNext = next_feat;
    // next_feat = &prop_feats->buffer_device_address_features;

    // prop_feats->acceleration_structure_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    // prop_feats->acceleration_structure_features.pNext = next_feat;
    // next_feat = &prop_feats->acceleration_structure_features;

    // prop_feats->ray_tracing_pipeline_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    // prop_feats->ray_tracing_pipeline_features.pNext = next_feat;
    // next_feat = &prop_feats->ray_tracing_pipeline_features;

    prop_feats->pageable_device_local_memory_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PAGEABLE_DEVICE_LOCAL_MEMORY_FEATURES_EXT;
    prop_feats->pageable_device_local_memory_features.pNext = next_feat;
    next_feat = &prop_feats->pageable_device_local_memory_features;

    // prop_feats->host_query_reset_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES;
    // prop_feats->host_query_reset_features.pNext = next_feat;
    // next_feat = &prop_feats->host_query_reset_features;

    // prop_feats->multiview_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES;
    // prop_feats->multiview_features.pNext = next_feat;
    // next_feat = &prop_feats->multiview_features;

    // prop_feats->mesh_shader_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
    // prop_feats->mesh_shader_features.pNext = next_feat;
    // next_feat = &prop_feats->mesh_shader_features;

    // prop_feats->ray_query_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    // prop_feats->ray_query_features.pNext = next_feat;
    // next_feat = &prop_feats->ray_query_features;

    // prop_feats->fragment_shading_rate_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR;
    // prop_feats->fragment_shading_rate_features.pNext = next_feat;
    // next_feat = &prop_feats->fragment_shading_rate_features;

    prop_feats->shader_object_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT;
    prop_feats->shader_object_features.pNext = next_feat;
    next_feat = &prop_feats->shader_object_features;

    // prop_feats->depth_clip_enable_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT;
    // prop_feats->depth_clip_enable_features.pNext = next_feat;
    // next_feat = &prop_feats->depth_clip_enable_features;

    // prop_feats->ray_tracing_position_fetch_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_POSITION_FETCH_FEATURES_KHR;
    // prop_feats->ray_tracing_position_fetch_features.pNext = next_feat;
    // next_feat = &prop_feats->ray_tracing_position_fetch_features;

    prop_feats->device_features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    prop_feats->device_features2.pNext = next_feat;

    // clang-format on
}

PhysicalDevice select_physical_device(rop(rw Arena) arena, ro VkInstance instance) {
    PhysicalDevicePropFeats* prop_feats = arena_alloc(arena, sizeof(PhysicalDevicePropFeats));

    arena_ckpt(arena);

    u32 device_count = 0;
    vkEnumeratePhysicalDevices(instance, &device_count, NULL);
    if (device_count == 0) {
        CRITICAL_LOG(SCOPE_GFX_INIT, "No vulkan devices found!");
        exit(1);
    }

    PhysicalDevice physical_device = {
        .handle     = VK_NULL_HANDLE,
        .prop_feats = prop_feats,
    };

    VkPhysicalDevice* physical_devices = arena_alloc(arena, sizeof(VkPhysicalDevice) * device_count);
    vkEnumeratePhysicalDevices(instance, &device_count, physical_devices);
    for (u32 i = 0; i < device_count; i++) {
        sanitize_pd_prop_feats(prop_feats);

        vkGetPhysicalDeviceProperties2(physical_devices[i], &prop_feats->device_properties2);

        vkGetPhysicalDeviceFeatures2(physical_devices[i], &prop_feats->device_features2);

        // TODO actually check the prop & feat flags we need here

        if (prop_feats->device_properties2.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
            prop_feats->device_features2.features.geometryShader) {

            // TODO check for the features we enable here

            physical_device.handle = physical_devices[i];

            break;
        }
    }

    arena_pop(arena);

    if (physical_device.handle == VK_NULL_HANDLE) {
        CRITICAL_LOG(SCOPE_GFX_INIT, "Failed to find a suitable physical device.");
        exit(1);
    }

    return physical_device;
}

QueueFamilies find_queue_families(rop(rw Arena) arena, ro VkSurfaceKHR surface, ro VkPhysicalDevice physical_device) {

    u32 queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties2(physical_device, &queue_family_count, NULL);
    if (queue_family_count == 0) {
        CRITICAL_LOG(SCOPE_GFX_INIT, "No queues found on selected device.");
        exit(1);
    }

    QueueFamily* queue_families = arena_alloc(arena, sizeof(QueueFamily) * queue_family_count);

    arena_ckpt(arena);
    VkQueueFamilyProperties2* queue_family_props =
        arena_alloc(arena, sizeof(VkQueueFamilyProperties2) * queue_family_count);

    for (u32 i = 0; i < queue_family_count; i++) {
        queue_family_props[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
        queue_family_props[i].pNext = NULL;
    }

    vkGetPhysicalDeviceQueueFamilyProperties2(physical_device, &queue_family_count, queue_family_props);

    u16 present_idx, graphics_idx, transfer_idx = u16_MAX;
    // TODO make sure there is at least 1 graphics queue on this device. Check this in pick physical device
    for (u32 idx = 0; idx < queue_family_count; idx++) {
        // .queueFlags;
        // .queueCount;
        // .minImageTransferGranularity;
        // .timestampValidBits;

        VkBool32 supports_present  = 0;
        u8       supports_graphics = 0;
        VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, idx, surface, &supports_present);
        check_vkresult(result, SCOPE_GFX_INIT, "Failed to query queue family for present support.");

        INFO_LOG(SCOPE_GFX_INIT, "QueueFamily %u:", idx);
        INFO_LOG(SCOPE_GFX_INIT, "    supports_present: %b", supports_present);
        INFO_LOG(SCOPE_GFX_INIT, "    queue_count: %u", queue_family_props[idx].queueFamilyProperties.queueCount);
        INFO_LOG(SCOPE_GFX_INIT, "    Supported Operations: ");

        if ((queue_family_props[idx].queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            INFO_LOG(SCOPE_GFX_INIT, "Graphics, ");
            // TODO this assumes only one graphics queue
            graphics_idx      = (u16)idx;
            supports_graphics = true;
        }

        if ((queue_family_props[idx].queueFamilyProperties.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0) {
            INFO_LOG(SCOPE_GFX_INIT, "Compute, ");
        }

        if ((queue_family_props[idx].queueFamilyProperties.queueFlags & VK_QUEUE_TRANSFER_BIT) != 0) {
            INFO_LOG(SCOPE_GFX_INIT, "Transfer, ");
            // try to find a transfer queue that does not support graphics.
            // TODO schedule/load balance between the transfer queues
            if (supports_graphics == false) {
                transfer_idx = (u16)idx;
            }
        }

        if ((queue_family_props[idx].queueFamilyProperties.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) != 0) {
            INFO_LOG(SCOPE_GFX_INIT, "Sparse Binding, ");
        }

        INFO_LOG(SCOPE_GFX_INIT, "\n");

        if (supports_present == true) {
            // TODO this assumes only one present queue
            present_idx = (u16)idx;
        }

        queue_families[idx]                  = (QueueFamily){0};
        queue_families[idx].supports_present = (u8)supports_present;
        queue_families[idx].flags            = queue_family_props[idx].queueFamilyProperties.queueFlags;
        queue_families[idx].cap = (u8)clamp_top(queue_family_props[idx].queueFamilyProperties.queueCount, u8_MAX);
    }

    if (transfer_idx == u16_MAX) {
        transfer_idx = graphics_idx;
    }

    arena_pop(arena);
    return (QueueFamilies){
        .len          = (u16)queue_family_count,
        .graphics_idx = graphics_idx,
        .present_idx  = present_idx,
        .transfer_idx = transfer_idx,
        .families     = queue_families,
        .queues       = NULL,
    };
}

VkDevice create_logical_device(rop(rw Arena) arena,
                               rop(rw PhysicalDevice) physical_device,
                               rop(rw QueueFamilies) queue_families) {
    VkDevice device;

    arena_ckpt(arena);

    VkDeviceQueueCreateInfo* queue_create_infos =
        arena_alloc(arena, sizeof(VkDeviceQueueCreateInfo) * queue_families->len);

    // TODO this priorities is silly but I'm just using one queue per family so it works for now
    f32 priorities = 1.0;
    for (u32 idx = 0; idx < queue_families->len; idx++) {
        queue_create_infos[idx].sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_create_infos[idx].queueFamilyIndex = idx;
        queue_create_infos[idx].queueCount       = 1;
        queue_create_infos[idx].flags            = 0;
        queue_create_infos[idx].pQueuePriorities = &priorities;
        queue_create_infos[idx].pNext            = NULL;
    }

    VkDeviceCreateInfo create_info   = {0};
    create_info.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.pQueueCreateInfos    = queue_create_infos;
    create_info.queueCreateInfoCount = queue_families->len;
    create_info.pEnabledFeatures     = NULL; // must be null when using dev_feats2

    // TODO check if supported

    PhysicalDevicePropFeats* prop_feats = arena_alloc(arena, sizeof(PhysicalDevicePropFeats));
    sanitize_pd_prop_feats(prop_feats);

    prop_feats->swapchain_maintenance_1.swapchainMaintenance1                    = VK_TRUE;
    prop_feats->vulkan_11_features.shaderDrawParameters                          = VK_TRUE;
    prop_feats->vulkan_12_features.runtimeDescriptorArray                        = VK_TRUE;
    prop_feats->vulkan_12_features.descriptorIndexing                            = VK_TRUE;
    prop_feats->vulkan_12_features.shaderSampledImageArrayNonUniformIndexing     = VK_TRUE;
    prop_feats->vulkan_12_features.timelineSemaphore                             = VK_TRUE;
    prop_feats->vulkan_12_features.bufferDeviceAddress                           = VK_TRUE;
    prop_feats->vulkan_12_features.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
    prop_feats->vulkan_12_features.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
    prop_feats->vulkan_12_features.descriptorBindingUpdateUnusedWhilePending     = VK_TRUE;
    prop_feats->vulkan_12_features.descriptorBindingSampledImageUpdateAfterBind  = VK_TRUE;
    prop_feats->vulkan_12_features.descriptorBindingPartiallyBound               = VK_TRUE;
    prop_feats->vulkan_12_features.descriptorBindingVariableDescriptorCount      = VK_TRUE;
    prop_feats->vulkan_12_features.scalarBlockLayout                             = VK_TRUE;
    prop_feats->vulkan_12_features.shaderInt8                                    = VK_TRUE;
    prop_feats->vulkan_12_features.storageBuffer8BitAccess                       = VK_TRUE;
    prop_feats->vulkan_13_features.dynamicRendering                              = VK_TRUE;
    prop_feats->vulkan_13_features.synchronization2                              = VK_TRUE;
    prop_feats->vulkan_13_features.maintenance4                                  = VK_TRUE;
    prop_feats->shader_object_features.shaderObject                              = VK_TRUE;
    // prop_feats->depth_clip_enable_features.depthClipEnable                       = VK_TRUE;
    // prop_feats->acceleration_structure_features.accelerationStructure            = VK_TRUE;
    // prop_feats->ray_tracing_pipeline_features.rayTracingPipeline                 = VK_TRUE;
    // prop_feats->ray_tracing_position_fetch_features.rayTracingPositionFetch      = VK_TRUE;
    // prop_feats->mesh_shader_features.meshShader                                  = VK_TRUE;
    // prop_feats->mesh_shader_features.taskShader                                  = VK_TRUE;
    prop_feats->device_features2.features                                        = (VkPhysicalDeviceFeatures){
        .shaderInt64       = VK_TRUE,
        .samplerAnisotropy = VK_TRUE,
    };

    /*
      VkPhysicalDeviceMaintenance6Features vk_physical_device_maintenance6features = {0};
      {
         vk_physical_device_maintenance6features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_FEATURES,
         vk_physical_device_maintenance6features.maintenance6 = true,
      }
      */

    create_info.pNext = &prop_feats->device_features2;

    // deprecated, don't use
    create_info.enabledLayerCount   = 0;
    create_info.ppEnabledLayerNames = NULL;

    AllocDynList ext_names = alloc_list_create_with_cap(arena, sizeof(char*), 16);

    alloc_list_push_ptr(arena, &ext_names, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    // alloc_list_push_ptr(arena, &ext_names, VK_EXT_MESH_SHADER_EXTENSION_NAME);
    alloc_list_push_ptr(arena, &ext_names, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);

// #define AFTERMATH 1
#if AFTERMATH
    alloc_list_push_ptr(arena, &ext_names, VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME);
    alloc_list_push_ptr(arena, &ext_names, VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);

    // needs to be called before device creation
    i32 aftermath_result = GFSDK_Aftermath_EnableGpuCrashDumps(GFSDK_Aftermath_Version_API,
                                                               GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_Vulkan,
                                                               GFSDK_Aftermath_GpuCrashDumpFeatureFlags_Default,
                                                               gpu_crash_dump,
                                                               shader_debug_info,
                                                               gpu_crash_dump_description,
                                                               resolve_marker,
                                                               NULL);
    DEBUG_LOG("Aftermath Result: %i", aftermath_result);

    // VkDeviceDiagnosticsConfigFlagBitsNV
#endif

    // alloc_list_push_ptr(arena, &ext_names, VK_EXT_SHADER_OBJECT_EXTENSION_NAME);
    // alloc_list_push_ptr(arena, &ext_names, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    // alloc_list_push_ptr(arena, &ext_names, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    // alloc_list_push_ptr(arena, &ext_names, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    // alloc_list_push_ptr(arena, &ext_names, VK_KHR_RAY_TRACING_POSITION_FETCH_EXTENSION_NAME);

    // alloc_list_push_ptr(arena, &ext_names, VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME);
    // alloc_list_push_ptr(arena, &ext_names, VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME);

    // #ifdef __DLSS
    // alloc_list_push_ptr(arena, &ext_names, VK_NVX_BINARY_IMPORT_EXTENSION_NAME);
    // alloc_list_push_ptr(arena, &ext_names, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
    // alloc_list_push_ptr(arena, &ext_names, VK_NVX_IMAGE_VIEW_HANDLE_EXTENSION_NAME);
    // #endif

    // Ray Tracing
    // alloc_list_push_ptr(arena, &ext_names, VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME);
    // alloc_list_push_ptr(arena, &ext_names, VK_KHR_RAY_QUERY_EXTENSION_NAME);

    // HDR
    // alloc_list_push_ptr(arena, &ext_names, VK_EXT_HDR_METADATA_EXTENSION_NAME);

    create_info.enabledExtensionCount   = ext_names.len;
    create_info.ppEnabledExtensionNames = ext_names.data;

    VkResult result = vkCreateDevice(physical_device->handle, &create_info, NULL, &device);
    check_vkresult(result, SCOPE_GFX_INIT, "Failed to create logical device.");

    arena_pop(arena);

    queue_families->queues = arena_alloc(arena, sizeof(VkQueue) * queue_families->len);
    for (u32 idx = 0; idx < queue_families->len; idx++) {
        // TODO queue idx is 0 here because we only create 1 queue per idx
        vkGetDeviceQueue(device, idx, 0, queue_families->queues + idx);
    }

    return device;
}

void cleanup_graphics_ctx(rop(rw GraphicsContext) ctx) {
    cleanup_command_pool(ctx, &ctx->queue_families.families[ctx->queue_families.transfer_idx].command_pool);

    vkDestroyDevice(ctx->device, NULL);

#if defined(__DEBUG)
    vkDestroyDebugUtilsMessengerEXT(ctx->instance, ctx->debug_messenger, NULL);
#endif

    vkDestroyInstance(ctx->instance, NULL);
}

GraphicsContext graphics_context_create(rop(rw Arena) arena, rop(rw RenderTarget) render_target) {
    u64        start    = time_start();
    VkInstance instance = create_instance(arena);
    time_checkpoint(SCOPE_DEBUG, "create_instance_total", start);

    init_ext_func_ptrs(instance);
    time_checkpoint(SCOPE_DEBUG, "init_ext_func_ptrs", start);

#if defined(__DEBUG)
    init_debug_func_ptrs(instance);
    VkDebugUtilsMessengerEXT debug_messenger = setup_debug_messenger(instance);
    time_checkpoint(SCOPE_DEBUG, "setup_debug_messenger", start);
#endif

    VkSurfaceKHR surface   = surface_create(instance, render_target->window);
    render_target->surface = surface;
    time_checkpoint(SCOPE_DEBUG, "surface_create", start);

    PhysicalDevice physical_device = select_physical_device(arena, instance);
    QueueFamilies  queue_families  = find_queue_families(arena, surface, physical_device.handle);
    VkDevice       device          = create_logical_device(arena, &physical_device, &queue_families);

    GraphicsContext ctx = (GraphicsContext){.instance        = instance,
                                            .physical_device = physical_device,
                                            .device          = device,
                                            .queue_families  = queue_families,

#if defined(__DEBUG)
                                            .debug_messenger = debug_messenger
#endif

    };

    // TODO render_target passthrough struct pointer is inconsistent with the rest of the codebase
    // it lets us use the old swapchain handle, if it exists, in the new creation (a vk thing)
    // Also lets us fill out multiple values, swapchain, format, extent & colorpsace
    create_swapchain(arena, &ctx, surface, render_target);

    CommandPool pool = create_transient_command_pool(&ctx, ctx.queue_families.transfer_idx);
    ctx.queue_families.families[ctx.queue_families.transfer_idx].command_pool = pool;

    return ctx;
}
