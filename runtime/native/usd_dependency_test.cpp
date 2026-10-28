#include <filesystem>
#include <iostream>
#include <stdexcept>

#include <unistd.h>

#include <pxr/base/vt/array.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdShade/material.h>

int main() {
  const auto path = std::filesystem::temp_directory_path()
    / ("remix-usd-" + std::to_string(::getpid()) + "-материалы.usda");
  try {
    auto stage = pxr::UsdStage::CreateInMemory();
    if (!stage) {
      throw std::runtime_error("native USD stage creation failed");
    }
    auto mesh = pxr::UsdGeomMesh::Define(stage, pxr::SdfPath("/World/Mesh"));
    const pxr::VtArray<pxr::GfVec3f> points{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.CreatePointsAttr().Set(points);
    mesh.CreateFaceVertexCountsAttr().Set(pxr::VtArray<int>{3});
    mesh.CreateFaceVertexIndicesAttr().Set(pxr::VtArray<int>{0, 1, 2});
    pxr::UsdShadeMaterial::Define(stage, pxr::SdfPath("/World/Materials/Glass"));
    if (!stage->GetRootLayer()->Export(path.string())) {
      throw std::runtime_error("native USD export failed");
    }
    stage.Reset();
    auto reloaded = pxr::UsdStage::Open(path.string());
    pxr::VtArray<pxr::GfVec3f> readPoints;
    auto readMesh = pxr::UsdGeomMesh::Get(reloaded, pxr::SdfPath("/World/Mesh"));
    if (!readMesh || !readMesh.GetPointsAttr().Get(&readPoints) || readPoints != points
        || !pxr::UsdShadeMaterial::Get(reloaded, pxr::SdfPath("/World/Materials/Glass"))) {
      throw std::runtime_error("native USD geometry/material round trip failed");
    }
    std::filesystem::remove(path);
    std::cout << "PASS native OpenUSD 25.11, geometry and material round trip through a UTF-8 path\n";
    return 0;
  } catch (const std::exception& error) {
    std::filesystem::remove(path);
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
