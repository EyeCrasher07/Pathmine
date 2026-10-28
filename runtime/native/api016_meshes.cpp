/*
 * Copyright (c) 2021-2026, NVIDIA CORPORATION. All rights reserved.
 * Copyright (c) 2026 EyeCrasher.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>

#include "api016_error.h"
#include "api016_extensions.h"
#include "api016_materials.h"
#include "api016_meshes.h"
#include "runtime_host.h"
#include "dxvk/rtx_render/rtx_context.h"
#include "rtx/utility/f16_conversion.h"

namespace dxvk::native {
  namespace {
    namespace api = api016;

    [[noreturn]] void invalid(const char* pMessage) {
      throw ApiError(api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS, pMessage);
    }

    // Bounds stay adjacent to UVs through both original geometry-cache paths.
    struct Vertex {
      float position[3];
      float normal[3];
      float texcoord[2];
      uint16_t bounds[4];
      uint32_t color;
    };
    static_assert(sizeof(Vertex) == 44);

    remixapi_MeshHandle toEngineHandle(api::remixapi_MeshHandle handle) {
      return reinterpret_cast<remixapi_MeshHandle>(handle);
    }

    size_t checkedBytes(uint64_t count, size_t stride) {
      if (count > std::numeric_limits<uint32_t>::max() || count > std::numeric_limits<size_t>::max() / stride) {
        invalid("API mesh buffer size exceeds the supported capacity");
      }
      return size_t(count) * stride;
    }

    void validateSurface(const api::remixapi_MeshInfoSurfaceTriangles& surface, const ApiMaterials& materials) {
      if (surface.reserved || (surface.flags & ~api::REMIXAPI_MESH_INFO_SURFACE_TRIANGLES_BIT_TEXCOORD_BOUNDS)
          || !surface.vertices_count || !surface.vertices_values
          || (surface.indices_count && !surface.indices_values)
          || (surface.indices_count ? surface.indices_count : surface.vertices_count) % 3) {
        invalid("Invalid API triangle surface");
      }
      checkedBytes(surface.vertices_count, sizeof(api::remixapi_HardcodedVertex));
      checkedBytes(surface.indices_count, sizeof(uint32_t));
      if (surface.material) {
        materials.resolve(surface.material);
      }
      for (uint64_t i = 0; i < surface.indices_count; ++i) {
        if (surface.indices_values[i] >= surface.vertices_count) {
          invalid("API mesh index is outside its vertex array");
        }
      }
      for (uint64_t i = 0; i < surface.vertices_count; ++i) {
        const auto& vertex = surface.vertices_values[i];
        for (float value : vertex.position) {
          if (!std::isfinite(value)) {
            invalid("Non-finite API vertex position");
          }
        }
        for (float value : vertex.normal) {
          if (!std::isfinite(value)) {
            invalid("Non-finite API vertex normal");
          }
        }
        for (float value : vertex.texcoord) {
          if (!std::isfinite(value)) {
            invalid("Non-finite API vertex texture coordinate");
          }
        }
        if (surface.flags & api::REMIXAPI_MESH_INFO_SURFACE_TRIANGLES_BIT_TEXCOORD_BOUNDS) {
          float bounds[4];
          for (uint32_t component = 0; component < 4; ++component) {
            bounds[component] = f16tof32(vertex.texcoordBounds[component]);
            if (!std::isfinite(bounds[component])) {
              invalid("Non-finite API texture bounds");
            }
          }
          if (bounds[2] < bounds[0] || bounds[3] < bounds[1]) {
            invalid("Inverted API texture bounds");
          }
        }
      }
      if (surface.skinning_hasvalue) {
        const auto& skinning = surface.skinning_value;
        const uint64_t count = surface.vertices_count * skinning.bonesPerVertex;
        if (!skinning.bonesPerVertex || skinning.bonesPerVertex > REMIXAPI_INSTANCE_INFO_MAX_BONES_COUNT
            || !skinning.blendWeights_values || !skinning.blendIndices_values
            || count != skinning.blendWeights_count || count != skinning.blendIndices_count) {
          invalid("Invalid API mesh skinning arrays");
        }
        for (uint64_t i = 0; i < count; ++i) {
          if (!std::isfinite(skinning.blendWeights_values[i])
              || skinning.blendIndices_values[i] >= REMIXAPI_INSTANCE_INFO_MAX_BONES_COUNT) {
            invalid("Invalid API mesh bone weight or index");
          }
        }
      }
    }

    Rc<DxvkBuffer> allocate(const Rc<DxvkDevice>& device, size_t bytes) {
      if (!bytes) {
        return nullptr;
      }
      DxvkBufferCreateInfo info { };
      info.size = align(bytes, CACHE_LINE_SIZE);
      if (info.size > device->properties().core.properties.limits.maxStorageBufferRange) {
        invalid("API mesh buffer exceeds the device storage-buffer limit");
      }
      info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
      info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
      info.access = VK_ACCESS_TRANSFER_WRITE_BIT;
      return device->createBuffer(info, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        DxvkMemoryStats::Category::RTXBuffer, "native API mesh buffer");
    }

    RasterGeometry convert(const Rc<DxvkDevice>& device, const api::remixapi_MeshInfoSurfaceTriangles& input) {
      const DxvkBufferSlice vertices { allocate(device, checkedBytes(input.vertices_count, sizeof(Vertex))) };
      auto* pVertices = static_cast<Vertex*>(vertices.mapPtr(0));
      for (size_t i = 0; i < input.vertices_count; ++i) {
        const auto& source = input.vertices_values[i];
        std::memcpy(pVertices[i].position, source.position, sizeof(source.position));
        std::memcpy(pVertices[i].normal, source.normal, sizeof(source.normal));
        std::memcpy(pVertices[i].texcoord, source.texcoord, sizeof(source.texcoord));
        std::memcpy(pVertices[i].bounds, source.texcoordBounds, sizeof(source.texcoordBounds));
        pVertices[i].color = source.color;
      }
      const size_t indexBytes = checkedBytes(input.indices_count, sizeof(uint32_t));
      DxvkBufferSlice indices;
      if (indexBytes) {
        indices = DxvkBufferSlice(allocate(device, indexBytes));
        std::memcpy(indices.mapPtr(0), input.indices_values, indexBytes);
      }
      RasterGeometry output;
      output.externalMaterial = reinterpret_cast<remixapi_MaterialHandle>(input.material);
      output.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      output.cullMode = VK_CULL_MODE_NONE;
      output.frontFace = VK_FRONT_FACE_CLOCKWISE;
      output.vertexCount = uint32_t(input.vertices_count);
      output.indexCount = uint32_t(input.indices_count);
      output.positionBuffer = { vertices, offsetof(Vertex, position), sizeof(Vertex), VK_FORMAT_R32G32B32_SFLOAT };
      output.normalBuffer = { vertices, offsetof(Vertex, normal), sizeof(Vertex), VK_FORMAT_R32G32B32_SFLOAT };
      output.texcoordBuffer = { vertices, offsetof(Vertex, texcoord), sizeof(Vertex), VK_FORMAT_R32G32_SFLOAT };
      output.hasTexcoordBounds = input.flags & api::REMIXAPI_MESH_INFO_SURFACE_TRIANGLES_BIT_TEXCOORD_BOUNDS;
      output.color0Buffer = { vertices, offsetof(Vertex, color), sizeof(Vertex), VK_FORMAT_B8G8R8A8_UNORM };
      output.indexBuffer = { indices, 0, sizeof(uint32_t), VK_INDEX_TYPE_UINT32 };
      if (input.skinning_hasvalue) {
        const auto& skinning = input.skinning_value;
        const size_t wordsPerVertex = divCeil(skinning.bonesPerVertex, 4u);
        const size_t weightBytes = checkedBytes(skinning.blendWeights_count, sizeof(float));
        const size_t packedBytes = checkedBytes(input.vertices_count * wordsPerVertex, sizeof(uint32_t));
        const DxvkBufferSlice weights { allocate(device, weightBytes + packedBytes), 0, weightBytes };
        std::memcpy(weights.mapPtr(0), skinning.blendWeights_values, weightBytes);
        const DxvkBufferSlice packed { weights.buffer(), weightBytes, packedBytes };
        auto* pPacked = static_cast<uint32_t*>(packed.mapPtr(0));
        std::memset(pPacked, 0, packedBytes);
        for (size_t vertex = 0; vertex < input.vertices_count; ++vertex) {
          for (uint32_t bone = 0; bone < skinning.bonesPerVertex; ++bone) {
            pPacked[vertex * wordsPerVertex + bone / 4] |=
              skinning.blendIndices_values[vertex * skinning.bonesPerVertex + bone] << (8 * (bone % 4));
          }
        }
        output.numBonesPerVertex = skinning.bonesPerVertex;
        output.blendWeightBuffer = { weights, 0, uint32_t(skinning.bonesPerVertex * sizeof(float)), VK_FORMAT_R32_SFLOAT };
        output.blendIndicesBuffer = { packed, 0, uint32_t(wordsPerVertex * sizeof(uint32_t)), VK_FORMAT_R8G8B8A8_USCALED };
      }
      static std::atomic<uint64_t> nextHash { 0x4e41544956000000ull };
      output.hashes[HashComponents::Indices] = output.hashes[HashComponents::VertexPosition] = nextHash.fetch_add(1);
      output.hashes[HashComponents::VertexTexcoord] = nextHash.fetch_add(1);
      output.hashes[HashComponents::GeometryDescriptor] = nextHash.fetch_add(1);
      output.hashes[HashComponents::VertexLayout] = nextHash.fetch_add(1);
      output.hashes.precombine();
      return output;
    }
  }

  struct ApiMeshes::Impl {
    static constexpr uint32_t kNoSlot = std::numeric_limits<uint32_t>::max();
    struct Slot {
      uint32_t generation = 1;
      uint32_t nextFree = kNoSlot;
      bool active = false;
    };
    RuntimeHost& host;
    const ApiMaterials& materials;
    std::vector<Slot> slots;
    uint32_t firstFree = kNoSlot;

    Impl(RuntimeHost& value, const ApiMaterials& inputMaterials) : host(value), materials(inputMaterials) { }

    api::remixapi_MeshHandle handleFor(uint32_t index) const {
      return reinterpret_cast<api::remixapi_MeshHandle>(uint64_t(slots[index].generation) << 32 | uint64_t(index + 1));
    }

    uint32_t find(api::remixapi_MeshHandle handle) const {
      const auto bits = reinterpret_cast<uintptr_t>(handle);
      const uint32_t index = uint32_t(bits) - 1;
      if (!uint32_t(bits) || index >= slots.size() || !slots[index].active
          || slots[index].generation != uint32_t(bits >> 32)) {
        invalid("Unknown or destroyed API mesh handle");
      }
      return index;
    }

    void free(uint32_t index) {
      auto& slot = slots[index];
      slot.active = false;
      if (slot.generation < std::numeric_limits<uint32_t>::max()) {
        ++slot.generation;
        slot.nextFree = firstFree;
        firstFree = index;
      }
    }
  };

  ApiMeshes::ApiMeshes(RuntimeHost& host, const ApiMaterials& materials)
    : m_pImpl(std::make_unique<Impl>(host, materials)) { }

  ApiMeshes::~ApiMeshes() {
    auto& state = *m_pImpl;
    for (uint32_t index = 0; index < state.slots.size(); ++index) {
      if (state.slots[index].active) {
        state.host.context()->getSceneManager().destroyExternalMesh(state.host.context(), toEngineHandle(state.handleFor(index)));
      }
    }
  }

  api016::remixapi_MeshHandle ApiMeshes::create(const api016::remixapi_MeshInfo& info) {
    auto& state = *m_pImpl;
    if (info.sType != api::REMIXAPI_STRUCT_TYPE_MESH_INFO || !info.surfaces_count || !info.surfaces_values) {
      invalid("Invalid API mesh description");
    }
    visitApiExtensions(info.pNext, [](api::remixapi_StructType, const void*) { });
    for (uint32_t i = 0; i < info.surfaces_count; ++i) {
      validateSurface(info.surfaces_values[i], state.materials);
    }
    std::vector<RasterGeometry> surfaces;
    surfaces.reserve(info.surfaces_count);
    for (uint32_t i = 0; i < info.surfaces_count; ++i) {
      surfaces.emplace_back(convert(state.host.device(), info.surfaces_values[i]));
    }
    uint32_t index = state.firstFree;
    if (index == Impl::kNoSlot) {
      if (state.slots.size() == Impl::kNoSlot) {
        invalid("API mesh handle capacity exhausted");
      }
      index = uint32_t(state.slots.size());
      state.slots.emplace_back();
    } else {
      state.firstFree = state.slots[index].nextFree;
    }
    const auto handle = state.handleFor(index);
    try {
      state.host.context()->getSceneManager().getAssetReplacer()->registerExternalMesh(toEngineHandle(handle), std::move(surfaces));
    } catch (...) {
      state.free(index);
      throw;
    }
    state.slots[index].active = true;
    return handle;
  }

  void ApiMeshes::destroy(api016::remixapi_MeshHandle handle) {
    auto& state = *m_pImpl;
    const auto index = state.find(handle);
    state.host.context()->getSceneManager().destroyExternalMesh(state.host.context(), toEngineHandle(handle));
    state.free(index);
  }

  std::shared_ptr<const std::vector<RasterGeometry>> ApiMeshes::resolve(api016::remixapi_MeshHandle handle) const {
    const auto& state = *m_pImpl;
    state.find(handle);
    return state.host.context()->getSceneManager().getAssetReplacer()->accessExternalMesh(toEngineHandle(handle));
  }
}
