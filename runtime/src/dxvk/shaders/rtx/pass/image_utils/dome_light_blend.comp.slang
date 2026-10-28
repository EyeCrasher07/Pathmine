// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include "rtx/pass/image_utils/dome_light_blend.h"

layout(binding = DOME_LIGHT_BLEND_SOURCE)
Sampler2D Source;

layout(binding = DOME_LIGHT_BLEND_DESTINATION)
Texture2D<float4> Destination;

layout(rgba16f, binding = DOME_LIGHT_BLEND_OUTPUT)
RWTexture2D<float4> Output;

layout(push_constant)
ConstantBuffer<DomeLightBlendArgs> cb;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 pixel : SV_DispatchThreadID) {
  const uint2 extent = cb.extentBlendAndFlags.xy;
  if (any(pixel >= extent)) {
    return;
  }
  const uint blendType = cb.extentBlendAndFlags.z;
  const uint flags = cb.extentBlendAndFlags.w;
  float4 source = float4(1.0);
  if (flags & 2u) {
    const float2 uv = (float2(pixel) + 0.5) / float2(extent);
    const float phi = (uv.x - 0.5) * 6.283185307179586;
    const float theta = uv.y * 3.141592653589793;
    const float3 direction = float3(sin(phi) * sin(theta), cos(phi) * sin(theta), cos(theta));
    const float3 lightDirection = normalize(mul(cb.worldToLight, float4(direction, 0.0)).xyz);
    const float2 sourceUv = float2(0.5 + atan2(lightDirection.x, lightDirection.y) * 0.159154943091895,
      0.5 - asin(clamp(lightDirection.z, -1.0, 1.0)) * 0.318309886183791);
    source = Source.SampleLevel(sourceUv, 0);
  }
  if (!(flags & 4u)) {
    source.a = 1.0;
  }
  source.rgb *= cb.radianceAndOpacity.rgb;
  float3 contribution;
  float3 destinationFactor = float3(1.0);
  switch (blendType) {
  default:
  case 0:
    contribution = source.rgb * source.a;
    destinationFactor = float3(1.0 - source.a);
    break;
  case 1:
    contribution = source.rgb * source.a;
    break;
  case 2:
    contribution = source.rgb * (1.0 - source.a);
    break;
  case 3:
    contribution = source.rgb * source.rgb;
    destinationFactor = 1.0 - source.rgb;
    break;
  case 4:
    contribution = source.rgb * source.rgb;
    break;
  case 5:
    contribution = source.rgb * (1.0 - source.rgb);
    break;
  case 6:
    contribution = source.rgb;
    break;
  case 7:
    contribution = float3(0.0);
    destinationFactor = source.rgb;
    break;
  case 8:
    contribution = float3(0.0);
    destinationFactor = source.rgb * 2.0;
    break;
  case 9:
    contribution = source.rgb * (1.0 - source.a);
    destinationFactor = float3(source.a);
    break;
  case 10:
    contribution = source.rgb * (1.0 - source.rgb);
    destinationFactor = source.rgb;
    break;
  }
  const float opacity = cb.radianceAndOpacity.a;
  const float3 destination = (flags & 1u) ? Destination.Load(int3(pixel, 0)).rgb : float3(0.0);
  const float3 radiance = destination * lerp(float3(1.0), destinationFactor, opacity) + contribution * opacity;
  Output[pixel] = float4(radiance, 1.0);
}
