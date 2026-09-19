#pragma once

#include <lib/grim/gfx/internal_graphics.h>

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT      message_severity,
                                              VkDebugUtilsMessageTypeFlagsEXT             message_type,
                                              const VkDebugUtilsMessengerCallbackDataEXT* cb_data,
                                              void*                                       user_data) {

    char* message_type_tag = NULL;

    // TODO do something with:
    // cb_data->pObjects & objectCount
    // cb_data->pCmdBufLabels & count
    // cb_data->pQueueLabels & count
    // Track the objects in the debugger?
    // TODO track per frame logs in the debugger, batch/bunch them together and if they are identical just do an x10

    switch (message_type) {
        case VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT: {
            message_type_tag = A_START A_BOLD ";" A_BG(128, 100, 32) A_END " GENERAL " A_RESET "\n";
        } break;
        case VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT: {
            message_type_tag = A_START A_BOLD ";" A_BG(128, 0, 0) A_END " VALIDATION " A_RESET "\n";
        } break;
        case VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT: {
            message_type_tag = A_START A_BOLD ";" A_BG(64, 128, 200) A_END " PERFORMANCE " A_RESET "\n";
        } break;
        case VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT: {
            message_type_tag = A_START A_BOLD ";" A_BG(128, 32, 100) A_END " DEVICE_ADDRESS_BINDING " A_RESET "\n";
        } break;
        case VK_DEBUG_UTILS_MESSAGE_TYPE_FLAG_BITS_MAX_ENUM_EXT: {
            message_type_tag = A_START A_BOLD ";" A_BG(255, 64, 64) A_END " MAX_BITS " A_RESET "\n";
        } break;
        default: {
            message_type_tag = "UNKNOWN TYPE";
        } break;
    }

    switch (message_severity) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: {
            VERBOSE_LOG(SCOPE_GFX_VALIDATION,
                        "%s    %i - %s\n    %s\n",
                        message_type_tag,
                        cb_data->messageIdNumber,
                        cb_data->pMessageIdName,
                        cb_data->pMessage);
        } break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: {
            INFO_LOG(SCOPE_GFX_VALIDATION,
                     "%s    %i - %s\n    %s\n",
                     message_type_tag,
                     cb_data->messageIdNumber,
                     cb_data->pMessageIdName,
                     cb_data->pMessage);
        } break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: {
            WARNING_LOG(SCOPE_GFX_VALIDATION,
                        "%s    %i - %s\n    %s\n",
                        message_type_tag,
                        cb_data->messageIdNumber,
                        cb_data->pMessageIdName,
                        cb_data->pMessage);
        } break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: {
            ERROR_LOG(SCOPE_GFX_VALIDATION,
                      "%s    %i - %s\n    %s\n",
                      message_type_tag,
                      cb_data->messageIdNumber,
                      cb_data->pMessageIdName,
                      cb_data->pMessage);
        } break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_FLAG_BITS_MAX_ENUM_EXT: {
            CRITICAL_LOG(SCOPE_GFX_VALIDATION,
                         "%s    %i - %s\n    %s\n",
                         message_type_tag,
                         cb_data->messageIdNumber,
                         cb_data->pMessageIdName,
                         cb_data->pMessage);
        } break;
        default: {
            CRITICAL_LOG(SCOPE_GFX_VALIDATION, "UKNOWN LOG LEVEL, MAYBE A COMBO?");
            DEBUG_LOG(SCOPE_GFX_VALIDATION,
                      "%s    %i - %s\n    %s\n",
                      message_type_tag,
                      cb_data->messageIdNumber,
                      cb_data->pMessageIdName,
                      cb_data->pMessage);
        } break;
    }

    return VK_FALSE;
}

void init_debug_func_ptrs(VkInstance instance) {
    vk_func_ptrs._vkCreateDebugUtilsMessengerEXT =
        (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (unlikely(vk_func_ptrs._vkCreateDebugUtilsMessengerEXT == NULL)) {
        CRITICAL_LOG(SCOPE_GFX_DEBUG, "Failed to load create debug utils messenger fn_ptr.");
        exit(1);
    }

    vk_func_ptrs._vkDestroyDebugUtilsMessengerEXT =
        (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (unlikely(vk_func_ptrs._vkDestroyDebugUtilsMessengerEXT == NULL)) {
        CRITICAL_LOG(SCOPE_GFX_DEBUG, "Failed to load destroy debug utils messenger fn_ptr.");
        exit(1);
    }
}

VkDebugUtilsMessengerEXT setup_debug_messenger(VkInstance instance) {
    VkDebugUtilsMessengerCreateInfoEXT create_info = {0};
    create_info.sType                              = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    create_info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    create_info.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT;
    create_info.pfnUserCallback = debug_callback;
    create_info.pUserData       = NULL;

    VkDebugUtilsMessengerEXT debug_messenger;
    VkResult                 result = vkCreateDebugUtilsMessengerEXT(instance, &create_info, NULL, &debug_messenger);
    check_vkresult(result, SCOPE_GFX_DEBUG, "Failed to setup debug messenger!");

    return debug_messenger;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDebugUtilsMessengerEXT(VkInstance                                instance,
                                                              const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                                              const VkAllocationCallbacks*              pAllocator,
                                                              VkDebugUtilsMessengerEXT*                 pMessenger) {
#if defined(__DEBUG)
    if (unlikely(vk_func_ptrs._vkCreateDebugUtilsMessengerEXT == NULL)) {
        CRITICAL_LOG(SCOPE_GFX_DEBUG,
                     "_vkCreateDebugUtilsMessengerEXT was NULL, did you forget to call init_debug_func_ptrs?");
        exit(1);
    }
#endif

    return vk_func_ptrs._vkCreateDebugUtilsMessengerEXT(instance, pCreateInfo, pAllocator, pMessenger);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDebugUtilsMessengerEXT(VkInstance                   instance,
                                                           VkDebugUtilsMessengerEXT     pMessenger,
                                                           const VkAllocationCallbacks* pAllocator) {

#if defined(__DEBUG)
    if (unlikely(vk_func_ptrs._vkDestroyDebugUtilsMessengerEXT == NULL)) {
        CRITICAL_LOG(SCOPE_GFX_DEBUG,
                     "_vkDestroyDebugUtilsMessengerEXT was NULL, did you forget to call init_debug_func_ptrs?");
        exit(1);
    }
#endif

    return vk_func_ptrs._vkDestroyDebugUtilsMessengerEXT(instance, pMessenger, pAllocator);
}
