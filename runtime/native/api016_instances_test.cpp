// Copyright (c) 2026 EyeCrasher.
// SPDX-License-Identifier: MIT
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>

#include "api016_cameras.h"
#include "api016_error.h"
#include "api016_instances.h"
#include "api016_materials.h"
#include "api016_meshes.h"
#include "api016_options.h"
#include "api016_textures.h"
#include "dxvk/pch/dxvk.pch.h"
#include "gl_output_reader.h"
#include "runtime_host.h"

namespace {
  namespace api = dxvk::native::api016;
  using dxvk::native::ApiInstances;
  using dxvk::native::ApiMeshes;
  using dxvk::native::RuntimeHost;
  using dxvk::native::test::require;

  template<typename F>
  void expectInvalid(F&& action) {
    try {
      action();
    } catch (const dxvk::native::ApiError& error) {
      require(error.code() == api::REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS, "API scene returned the wrong validation error");
      return;
    }
    throw std::runtime_error("invalid API scene request succeeded");
  }

  api::remixapi_Transform transform(float x = 0) {
    api::remixapi_Transform result { };
    result.matrix[0][0] = result.matrix[1][1] = result.matrix[2][2] = 1;
    result.matrix[0][3] = x;
    return result;
  }

  api::remixapi_InstanceInfo instanceInfo(api::remixapi_MeshHandle mesh, float x = 0) {
    api::remixapi_InstanceInfo info { };
    info.sType = api::REMIXAPI_STRUCT_TYPE_INSTANCE_INFO;
    info.mesh = mesh;
    info.transform = transform(x);
    info.doubleSided = 1;
    return info;
  }

  api::remixapi_MeshHandle triangle(ApiMeshes& meshes, api::remixapi_MaterialHandle material, bool skin = false, uint32_t vertexCount = 3) {
    std::vector<api::remixapi_HardcodedVertex> vertices(vertexCount);
    const float positions[3][3] = { { -1.3f, -1, 3 }, { 1.3f, -1, 3 }, { 0, 1, 3 } };
    for (size_t i = 0; i < vertices.size(); ++i) {
      std::copy_n(positions[i % 3], 3, vertices[i].position);
      vertices[i].normal[2] = -1;
      vertices[i].texcoord[0] = float(i) * .5f;
      vertices[i].texcoord[1] = .5f;
      vertices[i].color = 0xffffffff;
    }
    api::remixapi_MeshInfoSurfaceTriangles surface { };
    surface.vertices_values = vertices.data();
    surface.vertices_count = vertices.size();
    surface.material = material;
    std::vector<float> weights(vertexCount * 2);
    std::vector<uint32_t> bones(vertexCount * 2);
    const float pattern[3] = { 1, .5f, 0 };
    for (uint32_t i = 0; i < vertexCount; ++i) {
      weights[i * 2] = pattern[i % 3];
      weights[i * 2 + 1] = 1 - weights[i * 2];
      bones[i * 2] = 0;
      bones[i * 2 + 1] = 1;
    }
    if (skin) {
      surface.skinning_hasvalue = 1;
      surface.skinning_value = { 2, weights.data(), uint32_t(weights.size()),
        bones.data(), uint32_t(bones.size()) };
    }
    api::remixapi_MeshInfo info { };
    info.sType = api::REMIXAPI_STRUCT_TYPE_MESH_INFO;
    info.surfaces_count = 1;
    info.surfaces_values = &surface;
    return meshes.create(info);
  }

  struct Material {
    api::remixapi_MaterialInfoOpaqueEXT opaque { };
    api::remixapi_MaterialInfo info { };

    explicit Material(api::remixapi_Float3D color) {
      opaque.sType = api::REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT;
      opaque.albedoConstant = { 1, 1, 1 };
      opaque.opacityConstant = 1;
      opaque.roughnessConstant = .5f;
      opaque.alphaTestType = 7;
      info.sType = api::REMIXAPI_STRUCT_TYPE_MATERIAL_INFO;
      info.pNext = &opaque;
      info.emissiveColorConstant = color;
      info.emissiveIntensity = 2;
      info.spriteSheetRow = info.spriteSheetCol = 1;
      info.wrapModeU = info.wrapModeV = 1;
    }
  };

  struct Frames {
    RuntimeHost& host;
    dxvk::native::ApiCameras& cameras;
    dxvk::native::test::Reader& reader;
    std::vector<uint8_t> pixels;

    void begin() {
      require(host.beginFrame({ 96, 64, 1 }), "cannot begin retained scene frame");
      api::remixapi_CameraInfoParameterizedEXT parameters { };
      parameters.sType = api::REMIXAPI_STRUCT_TYPE_CAMERA_INFO_PARAMETERIZED_EXT;
      parameters.forward = { 0, 0, 1 };
      parameters.up = { 0, 1, 0 };
      parameters.right = { 1, 0, 0 };
      parameters.fovYInDegrees = 60;
      parameters.aspect = 1.5f;
      parameters.nearPlane = .05f;
      parameters.farPlane = 100;
      api::remixapi_CameraInfo info { };
      info.sType = api::REMIXAPI_STRUCT_TYPE_CAMERA_INFO;
      info.pNext = &parameters;
      cameras.setup(info);
    }

    void end() {
      host.submit();
      pixels = reader.readAndPresent(host);
    }

    void step(uint32_t count = 1) {
      for (uint32_t i = 0; i < count; ++i) {
        begin();
        end();
      }
    }

    size_t color(uint32_t channel) const {
      size_t count = 0;
      for (size_t i = 0; i < pixels.size(); i += 4) {
        const auto value = pixels[i + channel];
        count += value > 100 && value > 2 * unsigned(pixels[i + (channel + 1) % 3])
          && value > 2 * unsigned(pixels[i + (channel + 2) % 3]);
      }
      return count;
    }

    double centroid() const {
      const auto& depth = reader.depthPixels();
      const auto extent = host.interopInfo().depthExtent;
      size_t count = 0;
      uint64_t sumX = 0;
      for (size_t i = 0; i < depth.size(); ++i) {
        if (std::isfinite(depth[i]) && depth[i] != depth.front()) {
          ++count;
          sumX += i % extent.width;
        }
      }
      require(count > 100, "retained scene did not produce visible geometry in primary depth");
      return (double(sumX) / count + .5) / extent.width;
    }

    void save(const char* pPath) const {
      std::ofstream file(pPath, std::ios::binary);
      file << "P6\n96 64\n255\n";
      for (size_t i = 0; i < pixels.size(); i += 4) {
        file.write(reinterpret_cast<const char*>(pixels.data() + i), 3);
      }
      require(bool(file), "cannot save retained scene capture");
    }
  };

  dxvk::RtInstance* onlyInstance(RuntimeHost& host) {
    const auto& instances = host.context()->getSceneManager().getInstanceTable();
    require(instances.size() == 1 && instances.front(), "retained scene did not keep exactly one renderer instance");
    return instances.front();
  }

  void requireEmpty(RuntimeHost& host, const Frames& frames) {
    require(host.context()->getSceneManager().getInstanceTable().empty(), "removed scene objects still own renderer instances");
    if (frames.color(0) || frames.color(1) || frames.color(2)) {
      frames.save("api-instance-empty-failure.ppm");
      std::cout << "Empty scene: surfaces=" << host.context()->getSceneManager().getAccelManager().getSurfaceCount()
        << ", RGB counts=" << frames.color(0) << ',' << frames.color(1) << ',' << frames.color(2) << '\n';
    }
    require(frames.color(0) == 0 && frames.color(1) == 0 && frames.color(2) == 0,
      "removed scene objects are still visible");
  }

  void checkBadInputs(ApiInstances& instances, api::remixapi_MeshHandle mesh) {
    auto info = instanceInfo(mesh);
    info.sType = api::REMIXAPI_STRUCT_TYPE_MESH_INFO;
    expectInvalid([&] { instances.add(info); });
    info = instanceInfo(nullptr);
    expectInvalid([&] { instances.add(info); });
    info = instanceInfo(mesh);
    info.transform.matrix[1][2] = std::numeric_limits<float>::quiet_NaN();
    expectInvalid([&] { instances.add(info); });
    info = instanceInfo(mesh);
    info.categoryFlags = 1u << 31;
    expectInvalid([&] { instances.add(info); });
    info = instanceInfo(mesh);
    api::remixapi_InstanceInfoBlendEXT blend { };
    blend.sType = api::REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_BLEND_EXT;
    blend.alphaTestCompareOp = 999;
    info.pNext = &blend;
    expectInvalid([&] { instances.add(info); });
    blend.alphaTestCompareOp = VK_COMPARE_OP_ALWAYS;
    blend.pNext = &blend;
    expectInvalid([&] { instances.add(info); });
    api::remixapi_InstanceInfoGpuInstancingEXT gpu { };
    gpu.sType = api::REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_GPU_INSTANCING_EXT;
    gpu.instanceTransforms_count = 2;
    info.pNext = &gpu;
    expectInvalid([&] { instances.add(info); });
    api::remixapi_InstanceInfoBoneTransformsEXT bones { };
    bones.sType = api::REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_BONE_TRANSFORMS_EXT;
    bones.boneTransforms_count = 1;
    info.pNext = &bones;
    expectInvalid([&] { instances.add(info); });
    expectInvalid([&] { instances.remove(nullptr); });
  }

  void checkRetained(RuntimeHost& host, ApiInstances& instances, ApiMeshes& meshes,
      dxvk::native::ApiMaterials& materials, Frames& frames,
      api::remixapi_MaterialHandle material, api::remixapi_MaterialHandle blue) {
    const auto mesh = triangle(meshes, material);
    auto info = instanceInfo(mesh);
    checkBadInputs(instances, mesh);
    const auto handle = instances.add(info);
    info.transform = transform(50);
    frames.step(6);
    require(frames.color(0) > 200, "retained object lost its copied transform before first submission");
    const double center = frames.centroid();
    const auto* pOriginal = onlyInstance(host);
    const auto* pBlas = pOriginal->getBlas();
    const auto generation = host.context()->getSceneManager().getInstanceManager().getSceneGeneration();
    for (uint32_t i = 0; i < 72; ++i) {
      frames.step();
      const auto* pInstance = onlyInstance(host);
      require(pInstance == pOriginal && pInstance->getBlas() == pBlas && pInstance->surface.isPreservePath,
        "unchanged retained object was rebuilt or expired without an API update");
      require(host.context()->getSceneManager().getInstanceManager().getSceneGeneration() == generation,
        "unchanged retained object invalidated acceleration-structure caches");
    }
    require(frames.color(0) > 200, "retained object disappeared after the transient scene lifetime");
    frames.save("api-instance-retained.ppm");
    info = instanceInfo(mesh);
    const auto duplicate1 = instances.add(info);
    const auto duplicate2 = instances.add(info);
    frames.step(2);
    const auto& table = host.context()->getSceneManager().getInstanceTable();
    require(table.size() == 3 && std::set<dxvk::RtInstance*>(table.begin(), table.end()).size() == 3,
      "coincident retained objects sharing one mesh were collapsed into one instance");
    instances.remove(duplicate1);
    instances.remove(duplicate2);
    info.transform = transform(.6f);
    instances.update(handle, info);
    frames.step(6);
    require(onlyInstance(host) == pOriginal && frames.centroid() > center + .07,
      "retained transform update lost identity or failed to move GPU geometry");
    require(pOriginal->surface.isStatic && pOriginal->surface.prevObjectToWorld == pOriginal->surface.objectToWorld,
      "settled retained object kept motion from an earlier transform update");
    info.transform = transform(20);
    info.transform.matrix[2][0] = std::numeric_limits<float>::infinity();
    expectInvalid([&] { instances.update(handle, info); });
    frames.step();
    require(onlyInstance(host) == pOriginal && pOriginal->surface.objectToWorld[3].x == .6f,
      "failed retained update partially replaced its live renderer state");

    Material green({ 0, 1, 0 });
    materials.update(material, green.info);
    frames.step(6);
    require(onlyInstance(host) == pOriginal && frames.color(1) > 200 && frames.color(0) < 50,
      "retained object did not notice a material update without a scene update");
    frames.save("api-instance-material-update.ppm");
    host.context()->getSceneManager().clear(host.context(), true);
    frames.step(6);
    require(frames.color(1) > 200, "retained API object was lost by a renderer cache reset");

    const auto blueMesh = triangle(meshes, blue);
    info = instanceInfo(blueMesh, -.6f);
    const auto* pBeforeMesh = onlyInstance(host);
    instances.update(handle, info);
    frames.step(6);
    require(onlyInstance(host) == pBeforeMesh && frames.color(2) > 200 && frames.color(1) < 50,
      "retained mesh replacement failed to rebind its original renderer instance");
    frames.save("api-instance-mesh-update.ppm");
    meshes.destroy(blueMesh);
    frames.step(3);
    requireEmpty(host, frames);
    info = instanceInfo(mesh);
    instances.update(handle, info);
    frames.step(6);
    require(frames.color(1) > 200, "retained instance could not recover with a new mesh after mesh destruction");
    instances.remove(handle);
    expectInvalid([&] { instances.remove(handle); });
    expectInvalid([&] { instances.update(handle, info); });
    frames.step(3);
    requireEmpty(host, frames);

    const auto removedBeforeSubmit = instances.add(info);
    instances.remove(removedBeforeSubmit);
    const auto replacement = instances.add(info);
    require(replacement != removedBeforeSubmit && uint32_t(reinterpret_cast<uintptr_t>(replacement))
        == uint32_t(reinterpret_cast<uintptr_t>(removedBeforeSubmit)), "reused scene slot did not advance its generation");
    expectInvalid([&] { instances.update(removedBeforeSubmit, info); });
    instances.remove(replacement);
    const auto emptyGeneration = host.context()->getSceneManager().getInstanceManager().getSceneGeneration();
    frames.step();
    requireEmpty(host, frames);
    require(host.context()->getSceneManager().getInstanceManager().getSceneGeneration() == emptyGeneration,
      "remove-before-submit still created a renderer object");
    for (uint32_t i = 0; i < 128; ++i) {
      const auto temporary = instances.add(info);
      instances.update(temporary, info);
      instances.remove(temporary);
    }
    frames.step();
    requireEmpty(host, frames);
    meshes.destroy(mesh);
    std::cout << "PASS retained identity, 72 unchanged frames, updates, coincident objects, cache reset, mesh destruction and stale generations\n";
  }

  void checkExtensions(RuntimeHost& host, ApiInstances& instances, ApiMeshes& meshes,
      Frames& frames, api::remixapi_MaterialHandle material) {
    for (const uint32_t vertexCount : { 3u, 270u }) {
      const auto mesh = triangle(meshes, material, true, vertexCount);
      auto info = instanceInfo(mesh);
      std::array<api::remixapi_Transform, 2> bone { transform(.2f), transform(.8f) };
      api::remixapi_InstanceInfoBoneTransformsEXT bones { };
      bones.sType = api::REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_BONE_TRANSFORMS_EXT;
      bones.boneTransforms_values = bone.data();
      bones.boneTransforms_count = bone.size();
      info.pNext = &bones;
      const auto skin = instances.add(info);
      bone.fill(transform(80));
      bones.boneTransforms_count = 0;
      frames.step(6);
      const auto* pInstance = onlyInstance(host);
      const auto& skinning = pInstance->getBlas()->input.getSkinningState();
      std::cout << "Skinning: vertices=" << vertexCount << ", bones=" << skinning.numBones << ", vertex bones=" << skinning.numBonesPerVertex
        << ", x=" << (skinning.pBoneMatrices.empty() ? -999 : skinning.pBoneMatrices[0][3].x)
        << ", centroid=" << frames.centroid() << '\n';
      frames.save(vertexCount == 3 ? "api-instance-skinning-cpu.ppm" : "api-instance-skinning-gpu.ppm");
      require(skinning.numBones == 2 && skinning.pBoneMatrices[0][3].x == .2f
          && skinning.pBoneMatrices[1][3].x == .8f && frames.centroid() > .57,
        "retained bone data did not survive caller mutation or drive original skinning");
      const auto& geometry = pInstance->getBlas()->modifiedGeometryData;
      dxvk::DxvkBufferCreateInfo readbackInfo { };
      readbackInfo.size = geometry.vertexCount * geometry.positionBuffer.stride();
      readbackInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      readbackInfo.stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
      readbackInfo.access = VK_ACCESS_TRANSFER_WRITE_BIT;
      const auto readback = host.device()->createBuffer(readbackInfo,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        dxvk::DxvkMemoryStats::Category::RTXBuffer, "native skinned position readback");
      host.context()->copyBuffer(readback, 0, geometry.positionBuffer.buffer(),
        geometry.positionBuffer.offset(), readbackInfo.size);
      host.context()->flushCommandList();
      host.device()->waitForIdle();
      const float expectedX[3] = { -1.1f, 1.8f, .8f };
      for (uint32_t i = 0; i < geometry.vertexCount; ++i) {
        const auto* pPosition = static_cast<const float*>(readback->mapPtr(i * geometry.positionBuffer.stride()));
        require(std::abs(pPosition[0] - expectedX[i % 3]) < .0001f,
          "original skinning read the wrong per-vertex bone weights or wrote incorrect GPU positions");
      }
      instances.remove(skin);
      frames.step(2);
      meshes.destroy(mesh);

    }

    const auto plainMesh = triangle(meshes, material);
    auto info = instanceInfo(plainMesh);
    std::array<api::remixapi_Transform, 2> transforms { transform(-.9f), transform(.9f) };
    api::remixapi_InstanceInfoGpuInstancingEXT gpu { };
    gpu.sType = api::REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_GPU_INSTANCING_EXT;
    gpu.instanceTransforms_values = transforms.data();
    gpu.instanceTransforms_count = transforms.size();
    info.pNext = &gpu;
    const auto instanced = instances.add(info);
    transforms.fill(transform(80));
    gpu.instanceTransforms_count = 0;
    frames.step(6);
    const auto& saved = onlyInstance(host)->getBlas()->input.getTransformData().instancesToObject;
    require(saved && saved->size() == 2 && (*saved)[0][3].x == -.9f && (*saved)[1][3].x == .9f,
      "retained GPU-instancing transforms refer to caller-owned memory");
    require(host.context()->getSceneManager().getAccelManager().getSurfaceCount() == 2,
      "original GPU instancing did not create two ray-traced surfaces");
    require(frames.color(2) > 300, "GPU-instanced geometry was absent from the rendered image");
    const auto& depth = frames.reader.depthPixels();
    const auto width = host.interopInfo().depthExtent.width;
    uint32_t minX = width;
    uint32_t maxX = 0;
    for (size_t i = 0; i < depth.size(); ++i) {
      if (std::isfinite(depth[i]) && depth[i] != depth.front()) {
        minX = std::min(minX, uint32_t(i % width));
        maxX = std::max(maxX, uint32_t(i % width));
      }
    }
    require(maxX > minX && maxX - minX > width * .7f,
      "GPU-instance transforms did not produce separated geometry in primary depth");
    frames.save("api-instance-gpu-instancing.ppm");
    instances.remove(instanced);
    frames.step(2);
    requireEmpty(host, frames);
    meshes.destroy(plainMesh);
    std::cout << "PASS copied bone and GPU-instance transforms through original rendering\n";
  }

  void checkTransient(RuntimeHost& host, ApiInstances& instances, ApiMeshes& meshes,
      Frames& frames, api::remixapi_MaterialHandle material) {
    dxvk::native::ApiOptions options;
    options.set("rtx.numFramesToKeepInstances", "100");
    frames.step(2);
    const auto mesh = triangle(meshes, material);
    auto info = instanceInfo(mesh);
    api::remixapi_InstanceInfoBlendEXT blend { };
    blend.sType = api::REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_BLEND_EXT;
    blend.alphaTestEnabled = 1;
    blend.alphaTestCompareOp = VK_COMPARE_OP_ALWAYS;
    blend.textureColorArg1Source = 1;
    blend.textureColorOperation = 1;
    blend.textureAlphaArg1Source = 1;
    blend.textureAlphaOperation = 1;
    blend.tFactor = 0xff6699cc;
    blend.writeMask = 15;
    info.pNext = &blend;
    frames.begin();
    instances.draw(info);
    blend.alphaTestCompareOp = VK_COMPARE_OP_NEVER;
    blend.tFactor = 0;
    frames.end();
    const auto& legacy = onlyInstance(host)->getBlas()->input.getMaterialData();
    require(legacy.alphaTestCompareOp == VK_COMPARE_OP_ALWAYS && legacy.tFactor == 0xff6699cc,
      "transient blend extension was not copied during the API call");
    frames.save("api-instance-transient.ppm");
    frames.step(3);
    requireEmpty(host, frames);
    meshes.destroy(mesh);
    std::cout << "PASS transient draw expiry despite a 100-frame ordinary-object lifetime\n";
  }

  void checkTransientHistory(RuntimeHost& host, ApiInstances& instances, ApiMeshes& meshes,
      Frames& frames, api::remixapi_MaterialHandle material) {
    const auto mesh = triangle(meshes, material);
    const auto retained = instances.add(instanceInfo(mesh, .8f));
    frames.step(2);
    auto* pRetained = onlyInstance(host);
    dxvk::RtInstance* pLastTransient = nullptr;
    for (uint32_t frame = 0; frame < 4; ++frame) {
      const auto previousRetainedSurface = pRetained->getPreviousSurfaceIndex();
      require(previousRetainedSurface != SURFACE_INDEX_INVALID,
        "retained API object lost its surface history");
      frames.begin();
      instances.draw(instanceInfo(mesh, -.8f));
      const auto& table = host.context()->getSceneManager().getInstanceTable();
      require(table.size() == 2, "retained and transient API objects sharing one mesh were collapsed");
      auto* pTransient = table[0] == pRetained ? table[1] : table[0];
      require(pTransient != pRetained && pTransient->getPreviousSurfaceIndex() == SURFACE_INDEX_INVALID,
        "transient API draw acquired a retained surface history");
      require(!pLastTransient || pLastTransient == pTransient,
        "transient API history invalidation rebuilt the cached renderer object");
      require(pRetained->getPreviousSurfaceIndex() == previousRetainedSurface,
        "transient API draw invalidated another retained object's history");
      frames.end();
      pLastTransient = pTransient;
    }
    instances.remove(retained);
    frames.step(3);
    requireEmpty(host, frames);
    meshes.destroy(mesh);
    std::cout << "PASS transient API surface history, retained history and cached object reuse\n";
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
  window.pHandle = glfwCreateWindow(128, 96, "Original Remix retained scene verification", nullptr, nullptr);
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
    ApiInstances instances(host, meshes);
    dxvk::native::ApiCameras cameras(host, materials);
    Frames frames { host, cameras, reader };
    Material red({ 1, 0, 0 });
    Material blue({ 0, 0, 1 });
    const auto redMaterial = materials.create(red.info);
    const auto blueMaterial = materials.create(blue.info);
    checkRetained(host, instances, meshes, materials, frames, redMaterial, blueMaterial);
    checkExtensions(host, instances, meshes, frames, blueMaterial);
    checkTransient(host, instances, meshes, frames, blueMaterial);
    checkTransientHistory(host, instances, meshes, frames, blueMaterial);
    const auto shutdownMesh = triangle(meshes, blueMaterial);
    instances.add(instanceInfo(shutdownMesh));
    frames.step(2);
    require(frames.color(2) > 200, "active-scene shutdown fixture was not rendered");
    materials.destroy(redMaterial);
    std::cout << "PASS native API retained and transient scene instances, leaving one active object for shutdown\n";
    return 0;
  } catch (const dxvk::DxvkError& error) {
    std::cerr << "FAIL " << error.message() << '\n';
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
  }
  return 1;
}
