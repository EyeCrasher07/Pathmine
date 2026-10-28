// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstring>

#include "dxvk/dxvk_device.h"
#include "dxvk/rtx_render/rtx_types.h"

namespace dxvk::native::test {
  struct Vertex {
    float position[3];
    float normal[3];
    float texcoord[2];
    uint32_t color;
  };

  inline dxvk::RasterGeometry createTriangle(const dxvk::Rc<dxvk::DxvkDevice>& device,
      remixapi_MaterialHandle material) {
    const std::array<Vertex, 3> vertices {{
      { { -1.3f, -1.0f, 3.0f }, { 0, 0, -1 }, { 0, 0 }, 0xffffffff },
      { {  1.3f, -1.0f, 3.0f }, { 0, 0, -1 }, { 1, 0 }, 0xffffffff },
      { {  0.0f,  1.0f, 3.0f }, { 0, 0, -1 }, { 0.5f, 1 }, 0xffffffff }
    }};
    dxvk::DxvkBufferCreateInfo info { };
    info.size = sizeof(vertices);
    info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
      | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
      | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
    info.access = VK_ACCESS_TRANSFER_WRITE_BIT;
    const auto buffer = device->createBuffer(info,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      dxvk::DxvkMemoryStats::Category::RTXBuffer, "native original-renderer triangle");
    const dxvk::DxvkBufferSlice slice { buffer };
    std::memcpy(slice.mapPtr(0), vertices.data(), sizeof(vertices));
    dxvk::RasterGeometry geometry;
    geometry.externalMaterial = material;
    geometry.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    geometry.frontFace = VK_FRONT_FACE_CLOCKWISE;
    geometry.cullMode = VK_CULL_MODE_NONE;
    geometry.vertexCount = vertices.size();
    geometry.positionBuffer = { slice, offsetof(Vertex, position), sizeof(Vertex), VK_FORMAT_R32G32B32_SFLOAT };
    geometry.normalBuffer = { slice, offsetof(Vertex, normal), sizeof(Vertex), VK_FORMAT_R32G32B32_SFLOAT };
    geometry.texcoordBuffer = { slice, offsetof(Vertex, texcoord), sizeof(Vertex), VK_FORMAT_R32G32_SFLOAT };
    geometry.color0Buffer = { slice, offsetof(Vertex, color), sizeof(Vertex), VK_FORMAT_B8G8R8A8_UNORM };
    geometry.hashes[dxvk::HashComponents::VertexPosition] = 0x1001;
    geometry.hashes[dxvk::HashComponents::VertexTexcoord] = 0x1002;
    geometry.hashes[dxvk::HashComponents::GeometryDescriptor] = 0x1003;
    geometry.hashes[dxvk::HashComponents::VertexLayout] = 0x1004;
    geometry.hashes.precombine();
    return geometry;
  }

}
