#pragma once

#ifdef __DEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL validation_message(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types,
    const VkDebugUtilsMessengerCallbackDataEXT *message, void *user) {
    (void)types; (void)user;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        ERROR_LOG(SCOPE_GFX_VALIDATION, "%s", message->pMessage);
    else WARNING_LOG(SCOPE_GFX_VALIDATION, "%s", message->pMessage);
    return VK_FALSE;
}
#endif

static bool device_has_swapchain(VkPhysicalDevice device) {
    u32 count = 0;
    check_vkresult(vkEnumerateDeviceExtensionProperties(device, NULL, &count, NULL), SCOPE_GFX_INIT, "Count device extensions");
    VkExtensionProperties *extensions = graphics_alloc(count, sizeof(*extensions));
    check_vkresult(vkEnumerateDeviceExtensionProperties(device, NULL, &count, extensions), SCOPE_GFX_INIT, "Read device extensions");
    bool found = false;
    for (u32 i = 0; i < count; i++)
        if (strcmp(extensions[i].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) found = true;
    free(extensions);
    return found;
}

static bool device_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface, u32 *graphics, u32 *present, u32 *timestamp_bits) {
    u32 count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, NULL);
    VkQueueFamilyProperties *families = graphics_alloc(count, sizeof(*families));
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families);
    *graphics = *present = UINT32_MAX;
    for (u32 i = 0; i < count; i++) {
        if (!families[i].queueCount) continue;
        VkBool32 supports_present = VK_FALSE;
        check_vkresult(vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &supports_present), SCOPE_GFX_INIT, "Query queue presentation support");
        bool supports_graphics = (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        if (supports_graphics && *graphics == UINT32_MAX) *graphics = i;
        if (supports_present && *present == UINT32_MAX) *present = i;
        if (supports_graphics && supports_present) {
            *graphics = *present = i;
            break;
        }
    }
    bool found = *graphics != UINT32_MAX && *present != UINT32_MAX;
    if (found) *timestamp_bits = families[*graphics].timestampValidBits;
    free(families);
    return found;
}

GraphicsContext graphics_context_create(Arena *arena, RenderTarget *target) {
    (void)arena;
    GraphicsContext ctx = {0};
    window_prepare_render(target->window);
    target->extent = (VkExtent2D){target->window->width, target->window->height};
    const char *extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, GRIM_SURFACE_EXTENSION,
#ifdef __DEBUG
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME,
#endif
    };
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "rwmd",
        .apiVersion = VK_API_VERSION_1_3};
    VkInstanceCreateInfo instance_info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app, .enabledExtensionCount = sizeof(extensions) / sizeof(*extensions),
        .ppEnabledExtensionNames = extensions};
#ifdef __DEBUG
    const char *validation_layer = "VK_LAYER_KHRONOS_validation";
    VkDebugUtilsMessengerCreateInfoEXT debug = {.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = validation_message};
    VkValidationFeatureEnableEXT sync_validation = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT validation = {.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .pNext = &debug, .enabledValidationFeatureCount = 1, .pEnabledValidationFeatures = &sync_validation};
    instance_info.enabledLayerCount = 1;
    instance_info.ppEnabledLayerNames = &validation_layer;
    instance_info.pNext = &validation;
#endif
    check_vkresult(vkCreateInstance(&instance_info, NULL, &ctx.instance), SCOPE_GFX_INIT, "Create Vulkan 1.3 instance");
#ifdef __DEBUG
    PFN_vkCreateDebugUtilsMessengerEXT create_debug =
        (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(ctx.instance, "vkCreateDebugUtilsMessengerEXT");
    assert(SCOPE_GFX_VALIDATION, create_debug != NULL);
    check_vkresult(create_debug(ctx.instance, &debug, NULL, &ctx.debug_messenger), SCOPE_GFX_VALIDATION, "Create validation messenger");
#endif
    target->surface = surface_create(ctx.instance, target->window);
    u32 count = 0;
    check_vkresult(vkEnumeratePhysicalDevices(ctx.instance, &count, NULL), SCOPE_GFX_INIT, "Count physical devices");
    if (!count) {
        fprintf(stderr, "No Vulkan physical devices found\n");
        exit(EXIT_FAILURE);
    }
    VkPhysicalDevice *devices = graphics_alloc(count, sizeof(*devices));
    check_vkresult(vkEnumeratePhysicalDevices(ctx.instance, &count, devices), SCOPE_GFX_INIT, "Read physical devices");
    for (u32 i = 0; i < count; i++) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(devices[i], &properties);
        if (properties.apiVersion < VK_API_VERSION_1_3 || !device_has_swapchain(devices[i])) continue;
        VkPhysicalDeviceVulkan13Features features13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceVulkan12Features features12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
            .pNext = &features13};
        VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &features12};
        vkGetPhysicalDeviceFeatures2(devices[i], &features);
        if (!features13.dynamicRendering || !features13.synchronization2 || !features12.bufferDeviceAddress) continue;
        u32 graphics, present, timestamp_bits;
        if (!device_queue_families(devices[i], target->surface, &graphics, &present, &timestamp_bits)) continue;
        u32 formats = 0, modes = 0;
        check_vkresult(vkGetPhysicalDeviceSurfaceFormatsKHR(devices[i], target->surface, &formats, NULL), SCOPE_GFX_INIT, "Count surface formats");
        check_vkresult(vkGetPhysicalDeviceSurfacePresentModesKHR(devices[i], target->surface, &modes, NULL), SCOPE_GFX_INIT, "Count present modes");
        VkSurfaceCapabilitiesKHR capabilities;
        check_vkresult(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(devices[i], target->surface, &capabilities), SCOPE_GFX_INIT, "Query surface capabilities");
        if (!formats || !modes || !(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) continue;
        ctx.physical_device = devices[i];
        ctx.properties = properties;
        ctx.graphics_family = graphics;
        ctx.present_family = present;
        ctx.timestamp_bits = properties.limits.timestampComputeAndGraphics ? timestamp_bits : 0;
        break;
    }
    free(devices);
    if (!ctx.physical_device) {
        fprintf(stderr, "No device supports Vulkan 1.3 dynamic rendering, synchronization2, bufferDeviceAddress and swapchain presentation\n");
        exit(EXIT_FAILURE);
    }
    vkGetPhysicalDeviceMemoryProperties(ctx.physical_device, &ctx.memory_properties);
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queues[2] = {
        {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = ctx.graphics_family, .queueCount = 1, .pQueuePriorities = &priority},
        {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = ctx.present_family, .queueCount = 1, .pQueuePriorities = &priority},
    };
    VkPhysicalDeviceVulkan13Features enabled = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering = VK_TRUE, .synchronization2 = VK_TRUE};
    VkPhysicalDeviceVulkan12Features enabled12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &enabled, .bufferDeviceAddress = VK_TRUE};
    const char *device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo device_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &enabled12,
        .queueCreateInfoCount = ctx.graphics_family == ctx.present_family ? 1u : 2u, .pQueueCreateInfos = queues,
        .enabledExtensionCount = 1, .ppEnabledExtensionNames = device_extensions};
    check_vkresult(vkCreateDevice(ctx.physical_device, &device_info, NULL, &ctx.device), SCOPE_GFX_INIT, "Create Vulkan device");
    vkGetDeviceQueue(ctx.device, ctx.graphics_family, 0, &ctx.graphics_queue);
    vkGetDeviceQueue(ctx.device, ctx.present_family, 0, &ctx.present_queue);
    return ctx;
}

void cleanup_graphics_ctx(GraphicsContext *ctx) {
    if (ctx->device) {
        check_vkresult(vkDeviceWaitIdle(ctx->device), SCOPE_GFX_INIT, "Wait before device destruction");
        vkDestroyDevice(ctx->device, NULL);
    }
#ifdef __DEBUG
    if (ctx->debug_messenger) {
        PFN_vkDestroyDebugUtilsMessengerEXT destroy_debug =
            (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(ctx->instance, "vkDestroyDebugUtilsMessengerEXT");
        destroy_debug(ctx->instance, ctx->debug_messenger, NULL);
    }
#endif
    if (ctx->instance) vkDestroyInstance(ctx->instance, NULL);
    *ctx = (GraphicsContext){0};
}
