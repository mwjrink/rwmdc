#pragma once

#include <lib/grim/logger.h>

#include <aftermath/GFSDK_Aftermath.h>
#include <aftermath/GFSDK_Aftermath_GpuCrashDump.h>

#include <stdio.h>

static void gpu_crash_dump(const void* pGpuCrashDump, const uint32_t gpuCrashDumpSize, void* pUserData) {
    DEBUG_LOG(SCOPE_GFX_AFTERMATH, "gpu_crash_dump");
    FILE* dump_fd = fopen("aftermath/ICRASH.nv-gpudmp", "wcb");
    fwrite(pGpuCrashDump, gpuCrashDumpSize, 1, dump_fd);
    fflush(dump_fd);
    fclose(dump_fd);
    DEBUG_LOG(SCOPE_GFX_AFTERMATH, "Crash dump written");

    // GFSDK_Aftermath_DisableGpuCrashDumps();
}

static void shader_debug_info(const void* pShaderDebugInfo, const uint32_t shaderDebugInfoSize, void* pUserData) {
    DEBUG_LOG(SCOPE_GFX_AFTERMATH, "shader_debug_info");
}

static void gpu_crash_dump_description(PFN_GFSDK_Aftermath_AddGpuCrashDumpDescription addValue, void* pUserData) {
    DEBUG_LOG(SCOPE_GFX_AFTERMATH, "gpu_crash_dump_description");
}

static void resolve_marker(const void*                       pMarkerData,
                           const uint32_t                    markerDataSize,
                           void*                             pUserData,
                           PFN_GFSDK_Aftermath_ResolveMarker resolveMarker) {
    DEBUG_LOG(SCOPE_GFX_AFTERMATH, "resolve_marker");
}
