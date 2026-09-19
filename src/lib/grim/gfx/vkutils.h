#pragma once

#include <lib/grim/gfx/internal_graphics.h>

void log_available_layers() {
    uint32_t layerCount;
    // First call to get the count
    vkEnumerateInstanceLayerProperties(&layerCount, NULL);

    VkLayerProperties pLayers[layerCount];
    // Second call to get the actual layer data
    vkEnumerateInstanceLayerProperties(&layerCount, pLayers);

    INFO_LOG(SCOPE_GFX_INIT, "Available Vulkan Layers: \n");
    for (uint32_t i = 0; i < layerCount; ++i) {
        INFO_LOG(SCOPE_GFX_INIT, "    - %s (Version: %i)\n", pLayers[i].layerName, pLayers[i].specVersion);
    }
}

void log_available_extensions() {
    u32 extension_count;
    // First call to get the count
    vkEnumerateInstanceExtensionProperties(NULL, &extension_count, NULL);

    VkExtensionProperties pExtensions[extension_count];
    // Second call to get the actual layer data
    vkEnumerateInstanceExtensionProperties(NULL, &extension_count, pExtensions);

    INFO_LOG(SCOPE_GFX_INIT, "Available Vulkan Extensions: \n");
    for (u32 i = 0; i < extension_count; ++i) {
        INFO_LOG(SCOPE_GFX_INIT, "    - %s (Version: %i)\n", pExtensions[i].extensionName, pExtensions[i].specVersion);
    }
}

const char* present_mode_to_str(VkPresentModeKHR present_mode) {
    switch (present_mode) {
        // No vsync
        case VK_PRESENT_MODE_IMMEDIATE_KHR: {
            return "Immediate";
        } break;
        case VK_PRESENT_MODE_MAILBOX_KHR: {
            return "Mailbox";
        } break;
        // Required to be supported
        case VK_PRESENT_MODE_FIFO_KHR: {
            return "FIFO";
        } break;
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR: {
            return "FIFO Relaxed";
        } break;
        // Provided by VK_KHR_shared_presentable_image
        case VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR: {
            return "Shared Demand Refresh";
        } break; // Provided by VK_KHR_shared_presentable_image
        case VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR: {
            return "Shared Continuous Refresh";
        } break;
        // Provided by VK_KHR_present_mode_fifo_latest_ready
        case VK_PRESENT_MODE_FIFO_LATEST_READY_KHR: {
            return "FIFO Latest Ready";
        } break;
        default: {
            return "Unknown present mode!";
        } break;
    }
}

const char* surface_format_to_str(VkFormat surface_format) {
    switch (surface_format) {
        case VK_FORMAT_UNDEFINED: {
            return "VK_FORMAT_UNDEFINED";
        } break;
        case VK_FORMAT_R4G4_UNORM_PACK8: {
            return "VK_FORMAT_R4G4_UNORM_PACK8";
        } break;
        case VK_FORMAT_R4G4B4A4_UNORM_PACK16: {
            return "VK_FORMAT_R4G4B4A4_UNORM_PACK16";
        } break;
        case VK_FORMAT_B4G4R4A4_UNORM_PACK16: {
            return "VK_FORMAT_B4G4R4A4_UNORM_PACK16";
        } break;
        case VK_FORMAT_R5G6B5_UNORM_PACK16: {
            return "VK_FORMAT_R5G6B5_UNORM_PACK16";
        } break;
        case VK_FORMAT_B5G6R5_UNORM_PACK16: {
            return "VK_FORMAT_B5G6R5_UNORM_PACK16";
        } break;
        case VK_FORMAT_R5G5B5A1_UNORM_PACK16: {
            return "VK_FORMAT_R5G5B5A1_UNORM_PACK16";
        } break;
        case VK_FORMAT_B5G5R5A1_UNORM_PACK16: {
            return "VK_FORMAT_B5G5R5A1_UNORM_PACK16";
        } break;
        case VK_FORMAT_A1R5G5B5_UNORM_PACK16: {
            return "VK_FORMAT_A1R5G5B5_UNORM_PACK16";
        } break;
        case VK_FORMAT_R8_UNORM: {
            return "VK_FORMAT_R8_UNORM";
        } break;
        case VK_FORMAT_R8_SNORM: {
            return "VK_FORMAT_R8_SNORM";
        } break;
        case VK_FORMAT_R8_USCALED: {
            return "VK_FORMAT_R8_USCALED";
        } break;
        case VK_FORMAT_R8_SSCALED: {
            return "VK_FORMAT_R8_SSCALED";
        } break;
        case VK_FORMAT_R8_UINT: {
            return "VK_FORMAT_R8_UINT";
        } break;
        case VK_FORMAT_R8_SINT: {
            return "VK_FORMAT_R8_SINT";
        } break;
        case VK_FORMAT_R8_SRGB: {
            return "VK_FORMAT_R8_SRGB";
        } break;
        case VK_FORMAT_R8G8_UNORM: {
            return "VK_FORMAT_R8G8_UNORM";
        } break;
        case VK_FORMAT_R8G8_SNORM: {
            return "VK_FORMAT_R8G8_SNORM";
        } break;
        case VK_FORMAT_R8G8_USCALED: {
            return "VK_FORMAT_R8G8_USCALED";
        } break;
        case VK_FORMAT_R8G8_SSCALED: {
            return "VK_FORMAT_R8G8_SSCALED";
        } break;
        case VK_FORMAT_R8G8_UINT: {
            return "VK_FORMAT_R8G8_UINT";
        } break;
        case VK_FORMAT_R8G8_SINT: {
            return "VK_FORMAT_R8G8_SINT";
        } break;
        case VK_FORMAT_R8G8_SRGB: {
            return "VK_FORMAT_R8G8_SRGB";
        } break;
        case VK_FORMAT_R8G8B8_UNORM: {
            return "VK_FORMAT_R8G8B8_UNORM";
        } break;
        case VK_FORMAT_R8G8B8_SNORM: {
            return "VK_FORMAT_R8G8B8_SNORM";
        } break;
        case VK_FORMAT_R8G8B8_USCALED: {
            return "VK_FORMAT_R8G8B8_USCALED";
        } break;
        case VK_FORMAT_R8G8B8_SSCALED: {
            return "VK_FORMAT_R8G8B8_SSCALED";
        } break;
        case VK_FORMAT_R8G8B8_UINT: {
            return "VK_FORMAT_R8G8B8_UINT";
        } break;
        case VK_FORMAT_R8G8B8_SINT: {
            return "VK_FORMAT_R8G8B8_SINT";
        } break;
        case VK_FORMAT_R8G8B8_SRGB: {
            return "VK_FORMAT_R8G8B8_SRGB";
        } break;
        case VK_FORMAT_B8G8R8_UNORM: {
            return "VK_FORMAT_B8G8R8_UNORM";
        } break;
        case VK_FORMAT_B8G8R8_SNORM: {
            return "VK_FORMAT_B8G8R8_SNORM";
        } break;
        case VK_FORMAT_B8G8R8_USCALED: {
            return "VK_FORMAT_B8G8R8_USCALED";
        } break;
        case VK_FORMAT_B8G8R8_SSCALED: {
            return "VK_FORMAT_B8G8R8_SSCALED";
        } break;
        case VK_FORMAT_B8G8R8_UINT: {
            return "VK_FORMAT_B8G8R8_UINT";
        } break;
        case VK_FORMAT_B8G8R8_SINT: {
            return "VK_FORMAT_B8G8R8_SINT";
        } break;
        case VK_FORMAT_B8G8R8_SRGB: {
            return "VK_FORMAT_B8G8R8_SRGB";
        } break;
        case VK_FORMAT_R8G8B8A8_UNORM: {
            return "VK_FORMAT_R8G8B8A8_UNORM";
        } break;
        case VK_FORMAT_R8G8B8A8_SNORM: {
            return "VK_FORMAT_R8G8B8A8_SNORM";
        } break;
        case VK_FORMAT_R8G8B8A8_USCALED: {
            return "VK_FORMAT_R8G8B8A8_USCALED";
        } break;
        case VK_FORMAT_R8G8B8A8_SSCALED: {
            return "VK_FORMAT_R8G8B8A8_SSCALED";
        } break;
        case VK_FORMAT_R8G8B8A8_UINT: {
            return "VK_FORMAT_R8G8B8A8_UINT";
        } break;
        case VK_FORMAT_R8G8B8A8_SINT: {
            return "VK_FORMAT_R8G8B8A8_SINT";
        } break;
        case VK_FORMAT_R8G8B8A8_SRGB: {
            return "VK_FORMAT_R8G8B8A8_SRGB";
        } break;
        case VK_FORMAT_B8G8R8A8_UNORM: {
            return "VK_FORMAT_B8G8R8A8_UNORM";
        } break;
        case VK_FORMAT_B8G8R8A8_SNORM: {
            return "VK_FORMAT_B8G8R8A8_SNORM";
        } break;
        case VK_FORMAT_B8G8R8A8_USCALED: {
            return "VK_FORMAT_B8G8R8A8_USCALED";
        } break;
        case VK_FORMAT_B8G8R8A8_SSCALED: {
            return "VK_FORMAT_B8G8R8A8_SSCALED";
        } break;
        case VK_FORMAT_B8G8R8A8_UINT: {
            return "VK_FORMAT_B8G8R8A8_UINT";
        } break;
        case VK_FORMAT_B8G8R8A8_SINT: {
            return "VK_FORMAT_B8G8R8A8_SINT";
        } break;
        case VK_FORMAT_B8G8R8A8_SRGB: {
            return "VK_FORMAT_B8G8R8A8_SRGB";
        } break;
        case VK_FORMAT_A8B8G8R8_UNORM_PACK32: {
            return "VK_FORMAT_A8B8G8R8_UNORM_PACK32";
        } break;
        case VK_FORMAT_A8B8G8R8_SNORM_PACK32: {
            return "VK_FORMAT_A8B8G8R8_SNORM_PACK32";
        } break;
        case VK_FORMAT_A8B8G8R8_USCALED_PACK32: {
            return "VK_FORMAT_A8B8G8R8_USCALED_PACK32";
        } break;
        case VK_FORMAT_A8B8G8R8_SSCALED_PACK32: {
            return "VK_FORMAT_A8B8G8R8_SSCALED_PACK32";
        } break;
        case VK_FORMAT_A8B8G8R8_UINT_PACK32: {
            return "VK_FORMAT_A8B8G8R8_UINT_PACK32";
        } break;
        case VK_FORMAT_A8B8G8R8_SINT_PACK32: {
            return "VK_FORMAT_A8B8G8R8_SINT_PACK32";
        } break;
        case VK_FORMAT_A8B8G8R8_SRGB_PACK32: {
            return "VK_FORMAT_A8B8G8R8_SRGB_PACK32";
        } break;
        case VK_FORMAT_A2R10G10B10_UNORM_PACK32: {
            return "VK_FORMAT_A2R10G10B10_UNORM_PACK32";
        } break;
        case VK_FORMAT_A2R10G10B10_SNORM_PACK32: {
            return "VK_FORMAT_A2R10G10B10_SNORM_PACK32";
        } break;
        case VK_FORMAT_A2R10G10B10_USCALED_PACK32: {
            return "VK_FORMAT_A2R10G10B10_USCALED_PACK32";
        } break;
        case VK_FORMAT_A2R10G10B10_SSCALED_PACK32: {
            return "VK_FORMAT_A2R10G10B10_SSCALED_PACK32";
        } break;
        case VK_FORMAT_A2R10G10B10_UINT_PACK32: {
            return "VK_FORMAT_A2R10G10B10_UINT_PACK32";
        } break;
        case VK_FORMAT_A2R10G10B10_SINT_PACK32: {
            return "VK_FORMAT_A2R10G10B10_SINT_PACK32";
        } break;
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32: {
            return "VK_FORMAT_A2B10G10R10_UNORM_PACK32";
        } break;
        case VK_FORMAT_A2B10G10R10_SNORM_PACK32: {
            return "VK_FORMAT_A2B10G10R10_SNORM_PACK32";
        } break;
        case VK_FORMAT_A2B10G10R10_USCALED_PACK32: {
            return "VK_FORMAT_A2B10G10R10_USCALED_PACK32";
        } break;
        case VK_FORMAT_A2B10G10R10_SSCALED_PACK32: {
            return "VK_FORMAT_A2B10G10R10_SSCALED_PACK32";
        } break;
        case VK_FORMAT_A2B10G10R10_UINT_PACK32: {
            return "VK_FORMAT_A2B10G10R10_UINT_PACK32";
        } break;
        case VK_FORMAT_A2B10G10R10_SINT_PACK32: {
            return "VK_FORMAT_A2B10G10R10_SINT_PACK32";
        } break;
        case VK_FORMAT_R16_UNORM: {
            return "VK_FORMAT_R16_UNORM";
        } break;
        case VK_FORMAT_R16_SNORM: {
            return "VK_FORMAT_R16_SNORM";
        } break;
        case VK_FORMAT_R16_USCALED: {
            return "VK_FORMAT_R16_USCALED";
        } break;
        case VK_FORMAT_R16_SSCALED: {
            return "VK_FORMAT_R16_SSCALED";
        } break;
        case VK_FORMAT_R16_UINT: {
            return "VK_FORMAT_R16_UINT";
        } break;
        case VK_FORMAT_R16_SINT: {
            return "VK_FORMAT_R16_SINT";
        } break;
        case VK_FORMAT_R16_SFLOAT: {
            return "VK_FORMAT_R16_SFLOAT";
        } break;
        case VK_FORMAT_R16G16_UNORM: {
            return "VK_FORMAT_R16G16_UNORM";
        } break;
        case VK_FORMAT_R16G16_SNORM: {
            return "VK_FORMAT_R16G16_SNORM";
        } break;
        case VK_FORMAT_R16G16_USCALED: {
            return "VK_FORMAT_R16G16_USCALED";
        } break;
        case VK_FORMAT_R16G16_SSCALED: {
            return "VK_FORMAT_R16G16_SSCALED";
        } break;
        case VK_FORMAT_R16G16_UINT: {
            return "VK_FORMAT_R16G16_UINT";
        } break;
        case VK_FORMAT_R16G16_SINT: {
            return "VK_FORMAT_R16G16_SINT";
        } break;
        case VK_FORMAT_R16G16_SFLOAT: {
            return "VK_FORMAT_R16G16_SFLOAT";
        } break;
        case VK_FORMAT_R16G16B16_UNORM: {
            return "VK_FORMAT_R16G16B16_UNORM";
        } break;
        case VK_FORMAT_R16G16B16_SNORM: {
            return "VK_FORMAT_R16G16B16_SNORM";
        } break;
        case VK_FORMAT_R16G16B16_USCALED: {
            return "VK_FORMAT_R16G16B16_USCALED";
        } break;
        case VK_FORMAT_R16G16B16_SSCALED: {
            return "VK_FORMAT_R16G16B16_SSCALED";
        } break;
        case VK_FORMAT_R16G16B16_UINT: {
            return "VK_FORMAT_R16G16B16_UINT";
        } break;
        case VK_FORMAT_R16G16B16_SINT: {
            return "VK_FORMAT_R16G16B16_SINT";
        } break;
        case VK_FORMAT_R16G16B16_SFLOAT: {
            return "VK_FORMAT_R16G16B16_SFLOAT";
        } break;
        case VK_FORMAT_R16G16B16A16_UNORM: {
            return "VK_FORMAT_R16G16B16A16_UNORM";
        } break;
        case VK_FORMAT_R16G16B16A16_SNORM: {
            return "VK_FORMAT_R16G16B16A16_SNORM";
        } break;
        case VK_FORMAT_R16G16B16A16_USCALED: {
            return "VK_FORMAT_R16G16B16A16_USCALED";
        } break;
        case VK_FORMAT_R16G16B16A16_SSCALED: {
            return "VK_FORMAT_R16G16B16A16_SSCALED";
        } break;
        case VK_FORMAT_R16G16B16A16_UINT: {
            return "VK_FORMAT_R16G16B16A16_UINT";
        } break;
        case VK_FORMAT_R16G16B16A16_SINT: {
            return "VK_FORMAT_R16G16B16A16_SINT";
        } break;
        case VK_FORMAT_R16G16B16A16_SFLOAT: {
            return "VK_FORMAT_R16G16B16A16_SFLOAT";
        } break;
        case VK_FORMAT_R32_UINT: {
            return "VK_FORMAT_R32_UINT";
        } break;
        case VK_FORMAT_R32_SINT: {
            return "VK_FORMAT_R32_SINT";
        } break;
        case VK_FORMAT_R32_SFLOAT: {
            return "VK_FORMAT_R32_SFLOAT";
        } break;
        case VK_FORMAT_R32G32_UINT: {
            return "VK_FORMAT_R32G32_UINT";
        } break;
        case VK_FORMAT_R32G32_SINT: {
            return "VK_FORMAT_R32G32_SINT";
        } break;
        case VK_FORMAT_R32G32_SFLOAT: {
            return "VK_FORMAT_R32G32_SFLOAT";
        } break;
        case VK_FORMAT_R32G32B32_UINT: {
            return "VK_FORMAT_R32G32B32_UINT";
        } break;
        case VK_FORMAT_R32G32B32_SINT: {
            return "VK_FORMAT_R32G32B32_SINT";
        } break;
        case VK_FORMAT_R32G32B32_SFLOAT: {
            return "VK_FORMAT_R32G32B32_SFLOAT";
        } break;
        case VK_FORMAT_R32G32B32A32_UINT: {
            return "VK_FORMAT_R32G32B32A32_UINT";
        } break;
        case VK_FORMAT_R32G32B32A32_SINT: {
            return "VK_FORMAT_R32G32B32A32_SINT";
        } break;
        case VK_FORMAT_R32G32B32A32_SFLOAT: {
            return "VK_FORMAT_R32G32B32A32_SFLOAT";
        } break;
        case VK_FORMAT_R64_UINT: {
            return "VK_FORMAT_R64_UINT";
        } break;
        case VK_FORMAT_R64_SINT: {
            return "VK_FORMAT_R64_SINT";
        } break;
        case VK_FORMAT_R64_SFLOAT: {
            return "VK_FORMAT_R64_SFLOAT";
        } break;
        case VK_FORMAT_R64G64_UINT: {
            return "VK_FORMAT_R64G64_UINT";
        } break;
        case VK_FORMAT_R64G64_SINT: {
            return "VK_FORMAT_R64G64_SINT";
        } break;
        case VK_FORMAT_R64G64_SFLOAT: {
            return "VK_FORMAT_R64G64_SFLOAT";
        } break;
        case VK_FORMAT_R64G64B64_UINT: {
            return "VK_FORMAT_R64G64B64_UINT";
        } break;
        case VK_FORMAT_R64G64B64_SINT: {
            return "VK_FORMAT_R64G64B64_SINT";
        } break;
        case VK_FORMAT_R64G64B64_SFLOAT: {
            return "VK_FORMAT_R64G64B64_SFLOAT";
        } break;
        case VK_FORMAT_R64G64B64A64_UINT: {
            return "VK_FORMAT_R64G64B64A64_UINT";
        } break;
        case VK_FORMAT_R64G64B64A64_SINT: {
            return "VK_FORMAT_R64G64B64A64_SINT";
        } break;
        case VK_FORMAT_R64G64B64A64_SFLOAT: {
            return "VK_FORMAT_R64G64B64A64_SFLOAT";
        } break;
        case VK_FORMAT_B10G11R11_UFLOAT_PACK32: {
            return "VK_FORMAT_B10G11R11_UFLOAT_PACK32";
        } break;
        case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32: {
            return "VK_FORMAT_E5B9G9R9_UFLOAT_PACK32";
        } break;
        case VK_FORMAT_D16_UNORM: {
            return "VK_FORMAT_D16_UNORM";
        } break;
        case VK_FORMAT_X8_D24_UNORM_PACK32: {
            return "VK_FORMAT_X8_D24_UNORM_PACK32";
        } break;
        case VK_FORMAT_D32_SFLOAT: {
            return "VK_FORMAT_D32_SFLOAT";
        } break;
        case VK_FORMAT_S8_UINT: {
            return "VK_FORMAT_S8_UINT";
        } break;
        case VK_FORMAT_D16_UNORM_S8_UINT: {
            return "VK_FORMAT_D16_UNORM_S8_UINT";
        } break;
        case VK_FORMAT_D24_UNORM_S8_UINT: {
            return "VK_FORMAT_D24_UNORM_S8_UINT";
        } break;
        case VK_FORMAT_D32_SFLOAT_S8_UINT: {
            return "VK_FORMAT_D32_SFLOAT_S8_UINT";
        } break;
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK: {
            return "VK_FORMAT_BC1_RGB_UNORM_BLOCK";
        } break;
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK: {
            return "VK_FORMAT_BC1_RGB_SRGB_BLOCK";
        } break;
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: {
            return "VK_FORMAT_BC1_RGBA_UNORM_BLOCK";
        } break;
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK: {
            return "VK_FORMAT_BC1_RGBA_SRGB_BLOCK";
        } break;
        case VK_FORMAT_BC2_UNORM_BLOCK: {
            return "VK_FORMAT_BC2_UNORM_BLOCK";
        } break;
        case VK_FORMAT_BC2_SRGB_BLOCK: {
            return "VK_FORMAT_BC2_SRGB_BLOCK";
        } break;
        case VK_FORMAT_BC3_UNORM_BLOCK: {
            return "VK_FORMAT_BC3_UNORM_BLOCK";
        } break;
        case VK_FORMAT_BC3_SRGB_BLOCK: {
            return "VK_FORMAT_BC3_SRGB_BLOCK";
        } break;
        case VK_FORMAT_BC4_UNORM_BLOCK: {
            return "VK_FORMAT_BC4_UNORM_BLOCK";
        } break;
        case VK_FORMAT_BC4_SNORM_BLOCK: {
            return "VK_FORMAT_BC4_SNORM_BLOCK";
        } break;
        case VK_FORMAT_BC5_UNORM_BLOCK: {
            return "VK_FORMAT_BC5_UNORM_BLOCK";
        } break;
        case VK_FORMAT_BC5_SNORM_BLOCK: {
            return "VK_FORMAT_BC5_SNORM_BLOCK";
        } break;
        case VK_FORMAT_BC6H_UFLOAT_BLOCK: {
            return "VK_FORMAT_BC6H_UFLOAT_BLOCK";
        } break;
        case VK_FORMAT_BC6H_SFLOAT_BLOCK: {
            return "VK_FORMAT_BC6H_SFLOAT_BLOCK";
        } break;
        case VK_FORMAT_BC7_UNORM_BLOCK: {
            return "VK_FORMAT_BC7_UNORM_BLOCK";
        } break;
        case VK_FORMAT_BC7_SRGB_BLOCK: {
            return "VK_FORMAT_BC7_SRGB_BLOCK";
        } break;
        case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK: {
            return "VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK";
        } break;
        case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK: {
            return "VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK";
        } break;
        case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK: {
            return "VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK";
        } break;
        case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK: {
            return "VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK";
        } break;
        case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK: {
            return "VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK";
        } break;
        case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK: {
            return "VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK";
        } break;
        case VK_FORMAT_EAC_R11_UNORM_BLOCK: {
            return "VK_FORMAT_EAC_R11_UNORM_BLOCK";
        } break;
        case VK_FORMAT_EAC_R11_SNORM_BLOCK: {
            return "VK_FORMAT_EAC_R11_SNORM_BLOCK";
        } break;
        case VK_FORMAT_EAC_R11G11_UNORM_BLOCK: {
            return "VK_FORMAT_EAC_R11G11_UNORM_BLOCK";
        } break;
        case VK_FORMAT_EAC_R11G11_SNORM_BLOCK: {
            return "VK_FORMAT_EAC_R11G11_SNORM_BLOCK";
        } break;
        case VK_FORMAT_ASTC_4x4_UNORM_BLOCK: {
            return "VK_FORMAT_ASTC_4x4_UNORM_BLOCK";
        } break;
        case VK_FORMAT_ASTC_4x4_SRGB_BLOCK: {
            return "VK_FORMAT_ASTC_4x4_SRGB_BLOCK";
        } break;
        case VK_FORMAT_ASTC_5x4_UNORM_BLOCK: {
            return "VK_FORMAT_ASTC_5x4_UNORM_BLOCK";
        } break;
        case VK_FORMAT_ASTC_5x4_SRGB_BLOCK: {
            return "VK_FORMAT_ASTC_5x4_SRGB_BLOCK";
        } break;
        case VK_FORMAT_ASTC_5x5_UNORM_BLOCK: {
            return "VK_FORMAT_ASTC_5x5_UNORM_BLOCK";
        } break;
        case VK_FORMAT_ASTC_5x5_SRGB_BLOCK: {
            return "VK_FORMAT_ASTC_5x5_SRGB_BLOCK";
        } break;
        case VK_FORMAT_ASTC_6x5_UNORM_BLOCK: {
            return "VK_FORMAT_ASTC_6x5_UNORM_BLOCK";
        } break;
        case VK_FORMAT_ASTC_6x5_SRGB_BLOCK: {
            return "VK_FORMAT_ASTC_6x5_SRGB_BLOCK";
        } break;
        case VK_FORMAT_ASTC_6x6_UNORM_BLOCK: {
            return "VK_FORMAT_ASTC_6x6_UNORM_BLOCK";
        } break;
        case VK_FORMAT_ASTC_6x6_SRGB_BLOCK: {
            return "VK_FORMAT_ASTC_6x6_SRGB_BLOCK";
        } break;
        case VK_FORMAT_ASTC_8x5_UNORM_BLOCK: {
            return "VK_FORMAT_ASTC_8x5_UNORM_BLOCK";
        } break;
        case VK_FORMAT_ASTC_8x5_SRGB_BLOCK: {
            return "VK_FORMAT_ASTC_8x5_SRGB_BLOCK";
        } break;
        case VK_FORMAT_ASTC_8x6_UNORM_BLOCK: {
            return "VK_FORMAT_ASTC_8x6_UNORM_BLOCK";
        } break;
        case VK_FORMAT_ASTC_8x6_SRGB_BLOCK: {
            return "VK_FORMAT_ASTC_8x6_SRGB_BLOCK";
        } break;
        case VK_FORMAT_ASTC_8x8_UNORM_BLOCK: {
            return "VK_FORMAT_ASTC_8x8_UNORM_BLOCK";
        } break;
        case VK_FORMAT_ASTC_8x8_SRGB_BLOCK: {
            return "VK_FORMAT_ASTC_8x8_SRGB_BLOCK";
        } break;
        case VK_FORMAT_ASTC_10x5_UNORM_BLOCK: {
            return "VK_FORMAT_ASTC_10x5_UNORM_BLOCK";
        } break;
        case VK_FORMAT_ASTC_10x5_SRGB_BLOCK: {
            return "VK_FORMAT_ASTC_10x5_SRGB_BLOCK";
        } break;
        case VK_FORMAT_ASTC_10x6_UNORM_BLOCK: {
            return "VK_FORMAT_ASTC_10x6_UNORM_BLOCK";
        } break;
        case VK_FORMAT_ASTC_10x6_SRGB_BLOCK: {
            return "VK_FORMAT_ASTC_10x6_SRGB_BLOCK";
        } break;
        case VK_FORMAT_ASTC_10x8_UNORM_BLOCK: {
            return "VK_FORMAT_ASTC_10x8_UNORM_BLOCK";
        } break;
        case VK_FORMAT_ASTC_10x8_SRGB_BLOCK: {
            return "VK_FORMAT_ASTC_10x8_SRGB_BLOCK";
        } break;
        case VK_FORMAT_ASTC_10x10_UNORM_BLOCK: {
            return "VK_FORMAT_ASTC_10x10_UNORM_BLOCK";
        } break;
        case VK_FORMAT_ASTC_10x10_SRGB_BLOCK: {
            return "VK_FORMAT_ASTC_10x10_SRGB_BLOCK";
        } break;
        case VK_FORMAT_ASTC_12x10_UNORM_BLOCK: {
            return "VK_FORMAT_ASTC_12x10_UNORM_BLOCK";
        } break;
        case VK_FORMAT_ASTC_12x10_SRGB_BLOCK: {
            return "VK_FORMAT_ASTC_12x10_SRGB_BLOCK";
        } break;
        case VK_FORMAT_ASTC_12x12_UNORM_BLOCK: {
            return "VK_FORMAT_ASTC_12x12_UNORM_BLOCK";
        } break;
        case VK_FORMAT_ASTC_12x12_SRGB_BLOCK: {
            return "VK_FORMAT_ASTC_12x12_SRGB_BLOCK";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_G8B8G8R8_422_UNORM: {
            return "VK_FORMAT_G8B8G8R8_422_UNORM";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_B8G8R8G8_422_UNORM: {
            return "VK_FORMAT_B8G8R8G8_422_UNORM";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM: {
            return "VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM: {
            return "VK_FORMAT_G8_B8R8_2PLANE_420_UNORM";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM: {
            return "VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_G8_B8R8_2PLANE_422_UNORM: {
            return "VK_FORMAT_G8_B8R8_2PLANE_422_UNORM";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM: {
            return "VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_R10X6_UNORM_PACK16: {
            return "VK_FORMAT_R10X6_UNORM_PACK16";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_R10X6G10X6_UNORM_2PACK16: {
            return "VK_FORMAT_R10X6G10X6_UNORM_2PACK16";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16: {
            return "VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16: {
            return "VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16: {
            return "VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16: {
            return "VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16: {
            return "VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16: {
            return "VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16: {
            return "VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16: {
            return "VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_R12X4_UNORM_PACK16: {
            return "VK_FORMAT_R12X4_UNORM_PACK16";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_R12X4G12X4_UNORM_2PACK16: {
            return "VK_FORMAT_R12X4G12X4_UNORM_2PACK16";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16: {
            return "VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16: {
            return "VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16: {
            return "VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16: {
            return "VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16: {
            return "VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16: {
            return "VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16: {
            return "VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16: {
            return "VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_G16B16G16R16_422_UNORM: {
            return "VK_FORMAT_G16B16G16R16_422_UNORM";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_B16G16R16G16_422_UNORM: {
            return "VK_FORMAT_B16G16R16G16_422_UNORM";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM: {
            return "VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_G16_B16R16_2PLANE_420_UNORM: {
            return "VK_FORMAT_G16_B16R16_2PLANE_420_UNORM";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM: {
            return "VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_G16_B16R16_2PLANE_422_UNORM: {
            return "VK_FORMAT_G16_B16R16_2PLANE_422_UNORM";
        } break;
            // Provided by VK_VERSION_1_1
        case VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM: {
            return "VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM";
        } break;
            // Provided by VK_VERSION_1_3
        case VK_FORMAT_G8_B8R8_2PLANE_444_UNORM: {
            return "VK_FORMAT_G8_B8R8_2PLANE_444_UNORM";
        } break;
            // Provided by VK_VERSION_1_3
        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16: {
            return "VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16";
        } break;
            // Provided by VK_VERSION_1_3
        case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16: {
            return "VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16";
        } break;
            // Provided by VK_VERSION_1_3
        case VK_FORMAT_G16_B16R16_2PLANE_444_UNORM: {
            return "VK_FORMAT_G16_B16R16_2PLANE_444_UNORM";
        } break;
            // Provided by VK_VERSION_1_3
        case VK_FORMAT_A4R4G4B4_UNORM_PACK16: {
            return "VK_FORMAT_A4R4G4B4_UNORM_PACK16";
        } break;
            // Provided by VK_VERSION_1_3
        case VK_FORMAT_A4B4G4R4_UNORM_PACK16: {
            return "VK_FORMAT_A4B4G4R4_UNORM_PACK16";
        } break;
            // Provided by VK_VERSION_1_3
        case VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK: {
            return "VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK";
        } break;
            // Provided by VK_VERSION_1_3
        case VK_FORMAT_ASTC_5x4_SFLOAT_BLOCK: {
            return "VK_FORMAT_ASTC_5x4_SFLOAT_BLOCK";
        } break;
            // Provided by VK_VERSION_1_3
        case VK_FORMAT_ASTC_5x5_SFLOAT_BLOCK: {
            return "VK_FORMAT_ASTC_5x5_SFLOAT_BLOCK";
        } break;
            // Provided by VK_VERSION_1_3
        case VK_FORMAT_ASTC_6x5_SFLOAT_BLOCK: {
            return "VK_FORMAT_ASTC_6x5_SFLOAT_BLOCK";
        } break;
            // Provided by VK_VERSION_1_3
        case VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK: {
            return "VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK";
        } break;
            // Provided by VK_VERSION_1_3
        case VK_FORMAT_ASTC_8x5_SFLOAT_BLOCK: {
            return "VK_FORMAT_ASTC_8x5_SFLOAT_BLOCK";
        } break;
            // Provided by VK_VERSION_1_3
        case VK_FORMAT_ASTC_8x6_SFLOAT_BLOCK: {
            return "VK_FORMAT_ASTC_8x6_SFLOAT_BLOCK";
        } break;
            // Provided by VK_VERSION_1_3
        case VK_FORMAT_ASTC_8x8_SFLOAT_BLOCK: {
            return "VK_FORMAT_ASTC_8x8_SFLOAT_BLOCK";
        } break;
            // Provided by VK_VERSION_1_3
        case VK_FORMAT_ASTC_10x5_SFLOAT_BLOCK: {
            return "VK_FORMAT_ASTC_10x5_SFLOAT_BLOCK";
        } break;
            // Provided by VK_VERSION_1_3
        case VK_FORMAT_ASTC_10x6_SFLOAT_BLOCK: {
            return "VK_FORMAT_ASTC_10x6_SFLOAT_BLOCK";
        } break;
            // Provided by VK_VERSION_1_3
        case VK_FORMAT_ASTC_10x8_SFLOAT_BLOCK: {
            return "VK_FORMAT_ASTC_10x8_SFLOAT_BLOCK";
        } break;
            // Provided by VK_VERSION_1_3
        case VK_FORMAT_ASTC_10x10_SFLOAT_BLOCK: {
            return "VK_FORMAT_ASTC_10x10_SFLOAT_BLOCK";
        } break;
            // Provided by VK_VERSION_1_3
        case VK_FORMAT_ASTC_12x10_SFLOAT_BLOCK: {
            return "VK_FORMAT_ASTC_12x10_SFLOAT_BLOCK";
        } break;
            // Provided by VK_VERSION_1_3
        case VK_FORMAT_ASTC_12x12_SFLOAT_BLOCK: {
            return "VK_FORMAT_ASTC_12x12_SFLOAT_BLOCK";
        } break;
            // Provided by VK_VERSION_1_4
        case VK_FORMAT_A1B5G5R5_UNORM_PACK16: {
            return "VK_FORMAT_A1B5G5R5_UNORM_PACK16";
        } break;
            // Provided by VK_VERSION_1_4
        case VK_FORMAT_A8_UNORM: {
            return "VK_FORMAT_A8_UNORM";
        } break;
            // Provided by VK_IMG_format_pvrtc
        case VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG: {
            return "VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG";
        } break;
            // Provided by VK_IMG_format_pvrtc
        case VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG: {
            return "VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG";
        } break;
            // Provided by VK_IMG_format_pvrtc
        case VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG: {
            return "VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG";
        } break;
            // Provided by VK_IMG_format_pvrtc
        case VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG: {
            return "VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG";
        } break;
            // Provided by VK_IMG_format_pvrtc
        case VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG: {
            return "VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG";
        } break;
            // Provided by VK_IMG_format_pvrtc
        case VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG: {
            return "VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG";
        } break;
            // Provided by VK_IMG_format_pvrtc
        case VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG: {
            return "VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG";
        } break;
            // Provided by VK_IMG_format_pvrtc
        case VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG: {
            return "VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG";
        } break;
            // Provided by VK_ARM_tensors
        case VK_FORMAT_R8_BOOL_ARM: {
            return "VK_FORMAT_R8_BOOL_ARM";
        } break;
            // Provided by VK_NV_optical_flow
        case VK_FORMAT_R16G16_SFIXED5_NV: {
            return "VK_FORMAT_R16G16_SFIXED5_NV";
        } break;
            // Provided by VK_ARM_format_pack
        case VK_FORMAT_R10X6_UINT_PACK16_ARM: {
            return "VK_FORMAT_R10X6_UINT_PACK16_ARM";
        } break;
            // Provided by VK_ARM_format_pack
        case VK_FORMAT_R10X6G10X6_UINT_2PACK16_ARM: {
            return "VK_FORMAT_R10X6G10X6_UINT_2PACK16_ARM";
        } break;
            // Provided by VK_ARM_format_pack
        case VK_FORMAT_R10X6G10X6B10X6A10X6_UINT_4PACK16_ARM: {
            return "VK_FORMAT_R10X6G10X6B10X6A10X6_UINT_4PACK16_ARM";
        } break;
            // Provided by VK_ARM_format_pack
        case VK_FORMAT_R12X4_UINT_PACK16_ARM: {
            return "VK_FORMAT_R12X4_UINT_PACK16_ARM";
        } break;
            // Provided by VK_ARM_format_pack
        case VK_FORMAT_R12X4G12X4_UINT_2PACK16_ARM: {
            return "VK_FORMAT_R12X4G12X4_UINT_2PACK16_ARM";
        } break;
            // Provided by VK_ARM_format_pack
        case VK_FORMAT_R12X4G12X4B12X4A12X4_UINT_4PACK16_ARM: {
            return "VK_FORMAT_R12X4G12X4B12X4A12X4_UINT_4PACK16_ARM";
        } break;
            // Provided by VK_ARM_format_pack
        case VK_FORMAT_R14X2_UINT_PACK16_ARM: {
            return "VK_FORMAT_R14X2_UINT_PACK16_ARM";
        } break;
            // Provided by VK_ARM_format_pack
        case VK_FORMAT_R14X2G14X2_UINT_2PACK16_ARM: {
            return "VK_FORMAT_R14X2G14X2_UINT_2PACK16_ARM";
        } break;
            // Provided by VK_ARM_format_pack
        case VK_FORMAT_R14X2G14X2B14X2A14X2_UINT_4PACK16_ARM: {
            return "VK_FORMAT_R14X2G14X2B14X2A14X2_UINT_4PACK16_ARM";
        } break;
            // Provided by VK_ARM_format_pack
        case VK_FORMAT_R14X2_UNORM_PACK16_ARM: {
            return "VK_FORMAT_R14X2_UNORM_PACK16_ARM";
        } break;
            // Provided by VK_ARM_format_pack
        case VK_FORMAT_R14X2G14X2_UNORM_2PACK16_ARM: {
            return "VK_FORMAT_R14X2G14X2_UNORM_2PACK16_ARM";
        } break;
            // Provided by VK_ARM_format_pack
        case VK_FORMAT_R14X2G14X2B14X2A14X2_UNORM_4PACK16_ARM: {
            return "VK_FORMAT_R14X2G14X2B14X2A14X2_UNORM_4PACK16_ARM";
        } break;
            // Provided by VK_ARM_format_pack
        case VK_FORMAT_G14X2_B14X2R14X2_2PLANE_420_UNORM_3PACK16_ARM: {
            return "VK_FORMAT_G14X2_B14X2R14X2_2PLANE_420_UNORM_3PACK16_ARM";
        } break;
            // Provided by VK_ARM_format_pack
        case VK_FORMAT_G14X2_B14X2R14X2_2PLANE_422_UNORM_3PACK16_ARM: {
            return "VK_FORMAT_G14X2_B14X2R14X2_2PLANE_422_UNORM_3PACK16_ARM";
        } break;
            // Provided by VK_EXT_texture_compression_astc_hdr
            // case VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK_EXT: { return "VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK_EXT"; }
            // break;
            //   // Provided by VK_EXT_texture_compression_astc_hdr
            // case VK_FORMAT_ASTC_5x4_SFLOAT_BLOCK_EXT: { return "VK_FORMAT_ASTC_5x4_SFLOAT_BLOCK_EXT"; }
            // break;
            //   // Provided by VK_EXT_texture_compression_astc_hdr
            // case VK_FORMAT_ASTC_5x5_SFLOAT_BLOCK_EXT: { return "VK_FORMAT_ASTC_5x5_SFLOAT_BLOCK_EXT"; }
            // break;
            //   // Provided by VK_EXT_texture_compression_astc_hdr
            // case VK_FORMAT_ASTC_6x5_SFLOAT_BLOCK_EXT: { return "VK_FORMAT_ASTC_6x5_SFLOAT_BLOCK_EXT"; }
            // break;
            //   // Provided by VK_EXT_texture_compression_astc_hdr
            // case VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK_EXT: { return "VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK_EXT"; }
            // break;
            //   // Provided by VK_EXT_texture_compression_astc_hdr
            // case VK_FORMAT_ASTC_8x5_SFLOAT_BLOCK_EXT: { return "VK_FORMAT_ASTC_8x5_SFLOAT_BLOCK_EXT"; }
            // break;
            //   // Provided by VK_EXT_texture_compression_astc_hdr
            // case VK_FORMAT_ASTC_8x6_SFLOAT_BLOCK_EXT: { return "VK_FORMAT_ASTC_8x6_SFLOAT_BLOCK_EXT"; }
            // break;
            //   // Provided by VK_EXT_texture_compression_astc_hdr
            // case VK_FORMAT_ASTC_8x8_SFLOAT_BLOCK_EXT: { return "VK_FORMAT_ASTC_8x8_SFLOAT_BLOCK_EXT"; }
            // break;
            //   // Provided by VK_EXT_texture_compression_astc_hdr
            // case VK_FORMAT_ASTC_10x5_SFLOAT_BLOCK_EXT: { return "VK_FORMAT_ASTC_10x5_SFLOAT_BLOCK_EXT"; }
            // break;
            //   // Provided by VK_EXT_texture_compression_astc_hdr
            // case VK_FORMAT_ASTC_10x6_SFLOAT_BLOCK_EXT: { return "VK_FORMAT_ASTC_10x6_SFLOAT_BLOCK_EXT"; }
            // break;
            //   // Provided by VK_EXT_texture_compression_astc_hdr
            // case VK_FORMAT_ASTC_10x8_SFLOAT_BLOCK_EXT: { return "VK_FORMAT_ASTC_10x8_SFLOAT_BLOCK_EXT"; }
            // break;
            //   // Provided by VK_EXT_texture_compression_astc_hdr
            // case VK_FORMAT_ASTC_10x10_SFLOAT_BLOCK_EXT: { return "VK_FORMAT_ASTC_10x10_SFLOAT_BLOCK_EXT";
            // } break;
            //   // Provided by VK_EXT_texture_compression_astc_hdr
            // case VK_FORMAT_ASTC_12x10_SFLOAT_BLOCK_EXT: { return "VK_FORMAT_ASTC_12x10_SFLOAT_BLOCK_EXT";
            // } break;
            //   // Provided by VK_EXT_texture_compression_astc_hdr
            // case VK_FORMAT_ASTC_12x12_SFLOAT_BLOCK_EXT: { return "VK_FORMAT_ASTC_12x12_SFLOAT_BLOCK_EXT";
            // } break;
            //   // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_G8B8G8R8_422_UNORM_KHR: { return "VK_FORMAT_G8B8G8R8_422_UNORM_KHR"; } break;
            //   // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_B8G8R8G8_422_UNORM_KHR: { return "VK_FORMAT_B8G8R8G8_422_UNORM_KHR"; } break;
            //   // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM_KHR: { INFO_LOG(" VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM_KHR";
            // } break;
            //   // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR: { INFO_LOG(" VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR"; }
            // break;
            //   // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM_KHR: { INFO_LOG(" VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM_KHR";
            // } break; Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_G8_B8R8_2PLANE_422_UNORM_KHR: {
            //     return "VK_FORMAT_G8_B8R8_2PLANE_422_UNORM_KHR";
            // } break;
            //     // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM_KHR: {
            //     return "VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM_KHR";
            // } break;
            //     // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_R10X6_UNORM_PACK16_KHR: {
            //     return "VK_FORMAT_R10X6_UNORM_PACK16_KHR";
            // } break;
            //     // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_R10X6G10X6_UNORM_2PACK16_KHR: {
            //     return "VK_FORMAT_R10X6G10X6_UNORM_2PACK16_KHR";
            // } break;
            //     // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16_KHR: {
            //     return "VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16_KHR";
            // } break;
            //     // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16_KHR: {
            //     return "VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16_KHR";
            // } break;
            //     // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16_KHR: {
            //     return "VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16_KHR";
            // } break;
            //     // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16_KHR: {
            //     return "VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16_KHR";
            // } break;
            //     // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16_KHR: {
            //     return "VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16_KHR";
            // } break;
            //     // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16_KHR: {
            //     return "VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16_KHR";
            // } break;
            //     // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16_KHR: {
            //     return "VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16_KHR";
            // } break;
            //     // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16_KHR: {
            //     return "VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16_KHR";
            // } break;
            //     // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_R12X4_UNORM_PACK16_KHR: {
            //     return "VK_FORMAT_R12X4_UNORM_PACK16_KHR";
            // } break;
            //     // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_R12X4G12X4_UNORM_2PACK16_KHR: {
            //     return "VK_FORMAT_R12X4G12X4_UNORM_2PACK16_KHR";
            // } break;
            //     // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16_KHR: {
            //     return "VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16_KHR";
            // } break;
            //     // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16_KHR: {
            //     return "VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16_KHR";
            // } break;
            //     // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16_KHR: {
            //     return "VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16_KHR";
            // } break;
            //     // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16_KHR: {
            //     return "VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16_KHR";
            // } break;
            //     // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16_KHR: {
            //     return "VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16_KHR";
            // } break;
            // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16_KHR: {
            //     return "VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16_KHR";
            // } break;
            //     // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16_KHR: {
            //     return "VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16_KHR";
            // } break;
            //     // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16_KHR: {
            //     return "VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16_KHR";
            // } break;
            //     // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_G16B16G16R16_422_UNORM_KHR: {
            //     return "VK_FORMAT_G16B16G16R16_422_UNORM_KHR";
            // } break;
            //     // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_B16G16R16G16_422_UNORM_KHR: {
            //     return "VK_FORMAT_B16G16R16G16_422_UNORM_KHR";
            // } break;
            //     // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM_KHR: {
            //     return "VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM_KHR";
            // } break;
            //     // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_G16_B16R16_2PLANE_420_UNORM_KHR: {
            //     return "VK_FORMAT_G16_B16R16_2PLANE_420_UNORM_KHR";
            // } break;
            //     // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM_KHR: {
            //     return "VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM_KHR";
            // } break;
            //     // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_G16_B16R16_2PLANE_422_UNORM_KHR: {
            //     return "VK_FORMAT_G16_B16R16_2PLANE_422_UNORM_KHR";
            // } break;
            //     // Provided by VK_KHR_sampler_ycbcr_conversion
            // case VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM_KHR: {
            //     return "VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM_KHR";
            // } break;
            //     // Provided by VK_EXT_ycbcr_2plane_444_formats
            // case VK_FORMAT_G8_B8R8_2PLANE_444_UNORM_EXT: {
            //     return "VK_FORMAT_G8_B8R8_2PLANE_444_UNORM_EXT";
            // } break;
            //     // Provided by VK_EXT_ycbcr_2plane_444_formats
            // case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16_EXT: {
            //     return "VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16_EXT";
            // } break;
            //     // Provided by VK_EXT_ycbcr_2plane_444_formats
            // case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16_EXT: {
            //     return "VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16_EXT";
            // } break;
            //     // Provided by VK_EXT_ycbcr_2plane_444_formats
            // case VK_FORMAT_G16_B16R16_2PLANE_444_UNORM_EXT: {
            //     return "VK_FORMAT_G16_B16R16_2PLANE_444_UNORM_EXT";
            // } break;
            //     // Provided by VK_EXT_4444_formats
            // case VK_FORMAT_A4R4G4B4_UNORM_PACK16_EXT: {
            //     return "VK_FORMAT_A4R4G4B4_UNORM_PACK16_EXT";
            // } break;
            //     // Provided by VK_EXT_4444_formats
            // case VK_FORMAT_A4B4G4R4_UNORM_PACK16_EXT: {
            //     return "VK_FORMAT_A4B4G4R4_UNORM_PACK16_EXT";
            // } break;
            //     // Provided by VK_NV_optical_flow
            //     // VK_FORMAT_R16G16_S10_5_NV is a legacy alias
            // case VK_FORMAT_R16G16_S10_5_NV: {
            //     return "VK_FORMAT_R16G16_S10_5_NV";
            // } break;
            //     // Provided by VK_KHR_maintenance5
            // case VK_FORMAT_A1B5G5R5_UNORM_PACK16_KHR: {
            //     return "VK_FORMAT_A1B5G5R5_UNORM_PACK16_KHR";
            // } break;
            //     // Provided by VK_KHR_maintenance5
            // case VK_FORMAT_A8_UNORM_KHR: {
            //     return "VK_FORMAT_A8_UNORM_KHR";
            // } break;

        default: {
            return "Unknown Surface Format";
        } break;
    }
}

const char* colorspace_to_str(VkColorSpaceKHR colorspace) {
    switch (colorspace) {
        case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR: {
            return "sRGB Non-Linear";
        } break;
        // Provided by VK_EXT_swapchain_colorspace
        case VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT: {
            return "Display P3 Non-Linear";
        } break;
        case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT: {
            return "Extended sRGB Linear";
        } break;
        case VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT: {
            // Also covers VK_COLOR_SPACE_DCI_P3_LINEAR_EXT
            return "Display P3 Linear";
        } break;
        case VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT: {
            return "DCI P3 Non-Linear";
        } break;
        case VK_COLOR_SPACE_BT709_LINEAR_EXT: {
            return "BT709 Linear";
        } break;
        case VK_COLOR_SPACE_BT709_NONLINEAR_EXT: {
            return "BT709 Non-Linear";
        } break;
        case VK_COLOR_SPACE_BT2020_LINEAR_EXT: {
            return "BT2020 Linear";
        } break;
        case VK_COLOR_SPACE_HDR10_ST2084_EXT: {
            return "HDR10 ST2084";
        } break;
        // VK_COLOR_SPACE_DOLBYVISION_EXT is legacy, but no reason was given in the API XML
        case VK_COLOR_SPACE_DOLBYVISION_EXT: {
            return "Dolby Vision";
        } break;
        case VK_COLOR_SPACE_HDR10_HLG_EXT: {
            return "HDR10 HLG";
        } break;
        case VK_COLOR_SPACE_ADOBERGB_LINEAR_EXT: {
            return "Adobe RGB Linear";
        } break;
        case VK_COLOR_SPACE_ADOBERGB_NONLINEAR_EXT: {
            return "Adobe RGB Non-Linear";
        } break;
        case VK_COLOR_SPACE_PASS_THROUGH_EXT: {
            return "Pass Through";
        } break;
        case VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT: {
            return "Extended sRGB Non-Linear";
        } break;

        // Provided by VK_AMD_display_native_hdr
        case VK_COLOR_SPACE_DISPLAY_NATIVE_AMD: {
            return "Display Native AMD";
        } break;

        default: {
            return "Unknown color space!";
        } break;
    }
}
