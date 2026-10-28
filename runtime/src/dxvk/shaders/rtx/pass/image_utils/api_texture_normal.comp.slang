// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include "rtx/pass/image_utils/api_texture_normal.h"
#include "rtx/utility/packing.slangh"

layout(binding = API_TEXTURE_NORMAL_INPUT)
Texture2D<float4> InputTexture;

layout(rg8, binding = API_TEXTURE_NORMAL_OUTPUT)
RWTexture2D<float2> OutputTexture;

layout(push_constant)
ConstantBuffer<ApiTextureNormalArgs> cb;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 threadId : SV_DispatchThreadID) {
  if (any(threadId >= cb.extent)) {
    return;
  }
  const uint2 pixel = cb.offset + threadId;
  float3 normal = InputTexture.Load(int3(pixel, 0)).xyz * 2.0f - 1.0f;
  if (cb.reconstructZ) {
    normal.z = sqrt(max(0.0f, 1.0f - dot(normal.xy, normal.xy)));
  }
  if (cb.flipY) {
    normal.y = -normal.y;
  }
  if (!all(isfinite(normal)) || dot(normal, normal) == 0.0f) {
    normal = float3(0.0f, 0.0f, 1.0f);
  }
  // Quantize before image storage to preserve the API's half-up byte rounding.
  const float2 encoded = sphereDirectionToUnsignedOctahedral(normal);
  OutputTexture[pixel] = floor(encoded * 255.0f + 0.5f) / 255.0f;
}
