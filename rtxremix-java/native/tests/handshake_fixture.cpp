/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "bridge_platform.h"
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

// Test-only loader/version-table fixtures. These libraries have no renderer,
// device, scene, presentation, or production runtime implementation.
#ifdef FIXTURE_missing_export
extern "C" REMIXAPI int test_fixture_without_remix_export() {
    return 0;
}
#else
static remixapi_ErrorCode shutdownFixture() {
    return REMIXAPI_ERROR_CODE_SUCCESS;
}

static remixapi_ErrorCode unusedTextureUpdate(const remixapi_TexturePatchInfo*) {
    return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
}

static remixapi_ErrorCode unusedMaterialUpdate(remixapi_MaterialHandle, const remixapi_MaterialInfo*) {
    return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
}

static remixapi_ErrorCode unusedResolvedValue(remixapi_ConfigVariableResolvedInfo*) {
    return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
}

static remixapi_ErrorCode checkPresentArguments(const remixapi_PresentInfo* info) {
    return info && !info->hwndOverride && info->skipVulkanSwapchain
            && info->interopFlipY && info->clientDoneSemaphore_ValueToWait == 42
            && info->disableCpuWaitForExternalSemaphores
            ? REMIXAPI_ERROR_CODE_SUCCESS : REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
}

static remixapi_ErrorCode checkConfigLayerPath(const char* path, float priority, float threshold,
        remixapi_ConfigLayerHandle* handle) {
    if (!path || !handle || priority != 41.f || threshold != 0.25f) {
        return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file || std::string(std::istreambuf_iterator<char>(file), {}) != path) {
        return REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
    }
    *handle = reinterpret_cast<remixapi_ConfigLayerHandle>(uintptr_t(1));
    return REMIXAPI_ERROR_CODE_SUCCESS;
}

static remixapi_ErrorCode releaseConfigLayer(remixapi_ConfigLayerHandle handle) {
    return handle == reinterpret_cast<remixapi_ConfigLayerHandle>(uintptr_t(1))
        ? REMIXAPI_ERROR_CODE_SUCCESS : REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
}

#ifdef FIXTURE_handshake
#ifndef FIXTURE_no_shader_counter
extern "C" REMIXAPI uint32_t REMIXAPI_CALL remixinternal_GetShaderCompilationCount() {
    return 7;
}
#endif

extern "C" REMIXAPI remixapi_ErrorCode REMIXAPI_CALL remixapi_BeginFrame(uint32_t width, uint32_t height) {
    return width < (1u << 14) && height < (1u << 14)
        ? REMIXAPI_ERROR_CODE_SUCCESS : REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
}

extern "C" REMIXAPI remixapi_ErrorCode REMIXAPI_CALL remixapi_GetOverlayInteropInfo(
        remixapi_dxvk_InteropInfo* info, uint32_t* visible) {
    if (!info || !visible) return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    *info = {};
    *visible = 0;
    return REMIXAPI_ERROR_CODE_SUCCESS;
}

extern "C" REMIXAPI remixapi_ErrorCode REMIXAPI_CALL remixapi_InjectOverlayInput(
        uint32_t type, int32_t code, double x, double y) {
    return type == 3 && code == 65 && x == 1 && y == 2
        ? REMIXAPI_ERROR_CODE_SUCCESS : REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
}
#endif

extern "C" REMIXAPI remixapi_ErrorCode REMIXAPI_CALL remixapi_InitializeLibrary(
        const remixapi_InitializeLibraryInfo* info, remixapi_Interface* output) {
    if (!info || !output || info->sType != REMIXAPI_STRUCT_TYPE_INITIALIZE_LIBRARY_INFO) {
        return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }
    if (info->version != REMIXAPI_VERSION_MAKE(
            REMIXAPI_VERSION_MAJOR, REMIXAPI_VERSION_MINOR, REMIXAPI_VERSION_PATCH)) {
        return REMIXAPI_ERROR_CODE_INCOMPATIBLE_VERSION;
    }
#ifdef FIXTURE_incompatible
    return REMIXAPI_ERROR_CODE_INCOMPATIBLE_VERSION;
#else
    std::memset(output, 0, sizeof(*output));
#ifdef FIXTURE_handshake
    output->Shutdown = shutdownFixture;
    output->Present = checkPresentArguments;
    output->UpdateTextureFromMemory = unusedTextureUpdate;
    output->UpdateMaterial = unusedMaterialUpdate;
    output->GetConfigVariableResolved = unusedResolvedValue;
    output->AcquireConfigLayer = checkConfigLayerPath;
    output->ReleaseConfigLayer = releaseConfigLayer;
#endif
    return REMIXAPI_ERROR_CODE_SUCCESS;
#endif
}
#endif
