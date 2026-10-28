// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

#include "api016_cameras.h"
#include "api016_error.h"
#include "api016_materials.h"
#include "api016_meshes.h"
#include "api016_textures.h"
#include "dxvk/pch/dxvk.pch.h"
#include "gl_output_reader.h"
#include "runtime_host.h"

namespace {
  namespace api = dxvk::native::api016;
  using dxvk::native::ApiMeshes;
  using dxvk::native::RuntimeHost;
  using dxvk::native::test::require;

  template<typename F>
  void expectInvalid(F&& action) {
    try {
      action();
    } catch (const dxvk::native::ApiError& error) {
      require(error.code() == api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS, "API mesh returned the wrong validation error");
      return;
    }
    throw std::runtime_error("invalid API mesh request succeeded");
  }

  struct Triangle {
    std::array<api::remixapi_HardcodedVertex, 3> vertices { };
    std::array<uint32_t, 3> indices { 0, 1, 2 };
    api::remixapi_MeshInfoSurfaceTriangles surface { };
    api::remixapi_MeshInfo info { };

    Triangle(api::remixapi_MaterialHandle material = nullptr, bool bounded = true, float u = .75f) {
      const float positions[3][3] = { { -1.3f, -1, 3 }, { 1.3f, -1, 3 }, { 0, 1, 3 } };
      for (uint32_t i = 0; i < 3; ++i) {
        std::memcpy(vertices[i].position, positions[i], sizeof(positions[i]));
        vertices[i].normal[2] = -1;
        vertices[i].texcoord[0] = u;
        vertices[i].texcoord[1] = .5f;
        vertices[i].texcoordBounds[2] = 0x3800; // Half 0.5.
        vertices[i].texcoordBounds[3] = 0x3c00; // Half 1.0.
        vertices[i].color = 0xffffffff;
      }
      surface.vertices_values = vertices.data();
      surface.vertices_count = vertices.size();
      surface.indices_values = indices.data();
      surface.indices_count = indices.size();
      surface.material = material;
      surface.flags = bounded ? api::REMIXAPI_MESH_INFO_SURFACE_TRIANGLES_BIT_TEXCOORD_BOUNDS : 0;
      info.sType = api::REMIXAPI_STRUCT_TYPE_MESH_INFO;
      info.surfaces_values = &surface;
      info.surfaces_count = 1;
    }
  };

  void checkDescriptions(RuntimeHost& host, ApiMeshes& meshes) {
    Triangle triangle;
    const auto first = meshes.create(triangle.info);
    const auto second = meshes.create(triangle.info);
    require(first && second && first != second, "API mesh handles were derived from the deprecated zero asset hash");
    const auto snapshot = meshes.resolve(first);
    require(snapshot->size() == 1 && snapshot->front().hasTexcoordBounds, "API mesh dropped its surface bounds flag");
    const auto& geometry = snapshot->front();
    triangle.vertices[0].position[0] = 80;
    triangle.indices[0] = 2;
    require(*static_cast<const float*>(geometry.positionBuffer.mapPtr(0)) == -1.3f
        && *static_cast<const uint32_t*>(geometry.indexBuffer.mapPtr(0)) == 0,
      "API mesh retained caller-owned vertex or index pointers");
    meshes.destroy(first);
    require(*static_cast<const float*>(geometry.positionBuffer.mapPtr(0)) == -1.3f,
      "mesh snapshot lost its uploaded resources when its API handle was destroyed");
    expectInvalid([&] { meshes.resolve(first); });
    expectInvalid([&] { meshes.destroy(first); });
    const auto replacement = meshes.create(triangle.info);
    require(first != replacement && uint32_t(reinterpret_cast<uintptr_t>(first))
        == uint32_t(reinterpret_cast<uintptr_t>(replacement)), "reused mesh slot did not advance its generation");
    expectInvalid([&] { meshes.resolve(first); });
    meshes.destroy(second);
    meshes.destroy(replacement);

    Triangle skinned;
    std::array<float, 15> weights;
    weights.fill(.2f);
    const std::array<uint32_t, 15> boneIndices { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 255 };
    skinned.surface.skinning_hasvalue = 1;
    skinned.surface.skinning_value = { 5, weights.data(), uint32_t(weights.size()),
      boneIndices.data(), uint32_t(boneIndices.size()) };
    const auto skinMesh = meshes.create(skinned.info);
    const auto skin = meshes.resolve(skinMesh);
    const auto& surface = skin->front();
    const auto* pPacked = static_cast<const uint32_t*>(surface.blendIndicesBuffer.mapPtr(0));
    require(surface.numBonesPerVertex == 5 && pPacked[0] == 0x03020100 && pPacked[1] == 4
        && pPacked[4] == 0x0d0c0b0a && pPacked[5] == 255,
      "API mesh did not preserve the original packed skinning layout");
    weights[0] = 10;
    require(*static_cast<const float*>(surface.blendWeightBuffer.mapPtr(0)) == .2f,
      "API mesh retained a pointer to caller-owned bone weights");
    meshes.destroy(skinMesh);
    require(host.context()->getSceneManager().getAssetReplacer()->accessExternalMesh(
      reinterpret_cast<remixapi_MeshHandle>(skinMesh))->empty(), "destroyed API mesh remains in the original registry");
  }

  void checkBadInputs(ApiMeshes& meshes) {
    Triangle triangle;
    triangle.info.sType = api::REMIXAPI_STRUCT_TYPE_MATERIAL_INFO;
    expectInvalid([&] { meshes.create(triangle.info); });
    triangle.info.sType = api::REMIXAPI_STRUCT_TYPE_MESH_INFO;
    triangle.surface.reserved = 1;
    expectInvalid([&] { meshes.create(triangle.info); });
    triangle.surface.reserved = 0;
    triangle.surface.flags = 2;
    expectInvalid([&] { meshes.create(triangle.info); });
    triangle.surface.flags = 1;
    triangle.indices[2] = 3;
    expectInvalid([&] { meshes.create(triangle.info); });
    triangle.indices[2] = 2;
    triangle.surface.vertices_count = std::numeric_limits<uint64_t>::max();
    expectInvalid([&] { meshes.create(triangle.info); });
    triangle.surface.vertices_count = 3;
    triangle.vertices[0].texcoordBounds[0] = 0x7c00;
    expectInvalid([&] { meshes.create(triangle.info); });
    triangle.vertices[0].texcoordBounds[0] = 0;
    triangle.vertices[0].position[0] = std::numeric_limits<float>::quiet_NaN();
    expectInvalid([&] { meshes.create(triangle.info); });
    triangle.vertices[0].position[0] = -1.3f;
    triangle.surface.skinning_hasvalue = 1;
    expectInvalid([&] { meshes.create(triangle.info); });
    triangle.surface.skinning_hasvalue = 0;
    triangle.surface.material = reinterpret_cast<api::remixapi_MaterialHandle>(uintptr_t(123));
    expectInvalid([&] { meshes.create(triangle.info); });
  }

  void checkInterleaving(RuntimeHost& host, ApiMeshes& meshes, uint32_t count) {
    Triangle triangle;
    std::vector<api::remixapi_HardcodedVertex> vertices(count, triangle.vertices[0]);
    triangle.surface.vertices_values = vertices.data();
    triangle.surface.vertices_count = count;
    triangle.surface.indices_count = 0;
    const auto mesh = meshes.create(triangle.info);
    const auto snapshot = meshes.resolve(mesh);
    const auto& input = snapshot->front();
    dxvk::RtxGeometryUtils::InterleavedGeometryDescriptor output;
    const auto stride = dxvk::RtxGeometryUtils::computeOptimalVertexStride(input);
    dxvk::DxvkBufferCreateInfo info { };
    info.size = dxvk::align(stride * count, dxvk::CACHE_LINE_SIZE);
    info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    info.stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    info.access = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    output.buffer = host.device()->createBuffer(info,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      dxvk::DxvkMemoryStats::Category::RTXBuffer, "native mesh interleave readback");
    host.context()->getCommonObjects()->metaGeometryUtils().interleaveGeometry(host.context(), input, output);
    host.context()->flushCommandList();
    host.device()->waitForIdle();
    const auto* pWords = static_cast<const uint32_t*>(output.buffer->mapPtr(0));
    require(output.hasTexcoordBounds && output.stride == 44, "geometry interleaver dropped the UV bounds layout");
    for (uint32_t i = 0; i < count; ++i) {
      const auto* pVertex = pWords + i * output.stride / sizeof(uint32_t);
      require(pVertex[8] == 0 && pVertex[9] == 0x3c003800 && pVertex[10] == 0xffffffff,
        "CPU/GPU geometry interleaving corrupted packed UV bounds or vertex colors");
    }
    meshes.destroy(mesh);
  }

  std::vector<uint8_t> render(RuntimeHost& host, dxvk::native::ApiCameras& cameras,
      dxvk::native::test::Reader& reader, api::remixapi_MeshHandle mesh) {
    std::vector<uint8_t> pixels;
    for (uint32_t frame = 0; frame < 6; ++frame) {
      require(host.beginFrame({ 96, 64, 1 }), "cannot begin API mesh render frame");
      api::remixapi_CameraInfoParameterizedEXT parameters { };
      parameters.sType = api::REMIXAPI_STRUCT_TYPE_CAMERA_INFO_PARAMETERIZED_EXT;
      parameters.forward = { 0, 0, 1 };
      parameters.up = { 0, 1, 0 };
      parameters.right = { 1, 0, 0 };
      parameters.fovYInDegrees = 60;
      parameters.aspect = 1.5f;
      parameters.nearPlane = .05f;
      parameters.farPlane = 100;
      api::remixapi_CameraInfo camera { };
      camera.sType = api::REMIXAPI_STRUCT_TYPE_CAMERA_INFO;
      camera.pNext = &parameters;
      cameras.setup(camera);
      auto state = std::make_unique<dxvk::ExternalDrawState>();
      state->mesh = reinterpret_cast<remixapi_MeshHandle>(mesh);
      state->cameraType = dxvk::CameraType::Main;
      state->drawCall.cameraType = dxvk::CameraType::Main;
      state->doubleSided = true;
      host.context()->commitExternalGeometryToRT(std::move(state));
      host.submit();
      pixels = reader.readAndPresent(host);
    }
    return pixels;
  }

  void checkAtlas(RuntimeHost& host, dxvk::native::ApiTextures& textures,
      dxvk::native::ApiMaterials& materials, ApiMeshes& meshes,
      dxvk::native::ApiCameras& cameras, dxvk::native::test::Reader& reader) {
    std::array<uint32_t, 32 * 16> atlas;
    for (uint32_t y = 0; y < 16; ++y) {
      for (uint32_t x = 0; x < 32; ++x) {
        atlas[y * 32 + x] = x < 16 ? 0xff0000ff : 0xff00ff00;
      }
    }
    api::remixapi_TextureFromMemoryInfo texture { };
    texture.sType = api::REMIXAPI_STRUCT_TYPE_TEXTURE_FROM_MEMORY_INFO;
    texture.hash = 0x801;
    texture.width = 32;
    texture.height = 16;
    texture.pixelData = reinterpret_cast<const uint8_t*>(atlas.data());
    texture.format = api::REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM;
    texture.purpose = api::REMIXAPI_TEXTURE_PURPOSE_DATA;
    const auto image = textures.create(texture);
    api::remixapi_MaterialInfoOpaqueEXT opaque { };
    opaque.sType = api::REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT;
    opaque.albedoConstant = { 1, 1, 1 };
    opaque.opacityConstant = 1;
    opaque.roughnessConstant = .5f;
    opaque.alphaTestType = 7;
    api::remixapi_MaterialInfo info { };
    info.sType = api::REMIXAPI_STRUCT_TYPE_MATERIAL_INFO;
    info.pNext = &opaque;
    info.albedoTexture = info.emissiveTexture = image;
    info.emissiveColorConstant = { 1, 1, 1 };
    info.emissiveIntensity = 2;
    info.spriteSheetRow = info.spriteSheetCol = 1;
    info.wrapModeU = info.wrapModeV = 1;
    const auto material = materials.create(info);
    api::remixapi_TextureHandle animatedImage = nullptr;
    for (uint32_t mode = 0; mode < 6; ++mode) {
      if (mode == 4) {
        std::array<uint32_t, 64 * 16> animation;
        for (uint32_t y = 0; y < 16; ++y) {
          for (uint32_t x = 0; x < 64; ++x) {
            animation[y * 64 + x] = x % 32 < 16 ? 0xff0000ff : 0xff00ff00;
          }
        }
        texture.hash = 0x802;
        texture.width = 64;
        texture.pixelData = reinterpret_cast<const uint8_t*>(animation.data());
        animatedImage = textures.create(texture);
        info.albedoTexture = info.emissiveTexture = animatedImage;
        info.spriteSheetCol = 2;
        info.spriteSheetFps = 20;
        materials.update(material, info);
      }
      Triangle triangle(material, mode != 0 && mode != 4, mode == 2 ? -.25f : .75f);
      if (mode == 3) {
        for (auto& vertex : triangle.vertices) {
          std::memset(vertex.texcoordBounds, 0, sizeof(vertex.texcoordBounds));
        }
      }
      const auto mesh = meshes.create(triangle.info);
      triangle.vertices.fill({ });
      triangle.indices.fill(99);
      const auto pixels = render(host, cameras, reader, mesh);
      size_t red = 0;
      size_t green = 0;
      for (size_t i = 0; i < pixels.size(); i += 4) {
        red += pixels[i] > 100 && pixels[i] > 2 * unsigned(pixels[i + 1]);
        green += pixels[i + 1] > 100 && pixels[i + 1] > 2 * unsigned(pixels[i]);
      }
      const bool wrapped = mode == 1 || mode == 2 || mode == 5;
      std::cout << "API mesh atlas mode=" << mode << ", red=" << red << ", green=" << green << '\n';
      std::ofstream capture("api-mesh-atlas-" + std::to_string(mode) + ".ppm", std::ios::binary);
      capture << "P6\n96 64\n255\n";
      for (size_t i = 0; i < pixels.size(); i += 4) {
        capture.write(reinterpret_cast<const char*>(pixels.data() + i), 3);
      }
      require(wrapped ? (red > 200 && green < 50) : (green > 200 && red < 50),
        "original material rendering ignored atlas bounds, UV wrapping or the disabled-bounds fallback");
      meshes.destroy(mesh);
    }
    materials.destroy(material);
    textures.destroy(animatedImage);
    textures.destroy(image);
  }
}

int main() {
  if (!glfwInit()) {
    return 77;
  }
  dxvk::native::test::Window window;
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  window.pHandle = glfwCreateWindow(128, 96, "Original Remix API mesh verification", nullptr, nullptr);
  if (!window.pHandle) {
    return 77;
  }
  glfwMakeContextCurrent(window.pHandle);
  try {
    RuntimeHost host(std::filesystem::current_path().u8string());
    dxvk::native::test::Reader reader(host.interopInfo());
    dxvk::native::ApiTextures textures(host);
    dxvk::native::ApiMaterials materials(host, textures);
    ApiMeshes meshes(host, materials);
    dxvk::native::ApiCameras cameras(host, materials);
    checkDescriptions(host, meshes);
    checkBadInputs(meshes);
    checkInterleaving(host, meshes, 3);
    checkInterleaving(host, meshes, 1026);
    checkAtlas(host, textures, materials, meshes, cameras, reader);
    std::cout << "PASS native API meshes, caller ownership, skinning layout, generations, CPU/GPU interleaving and atlas wrapping\n";
    return 0;
  } catch (const dxvk::DxvkError& error) {
    std::cerr << "FAIL " << error.message() << '\n';
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
  }
  return 1;
}
