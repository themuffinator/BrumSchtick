/*
 Copyright (C) 2025 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "mdl/Map_Geometry.h"

#include "Logger.h"
#include "PreferenceManager.h"
#include "Preferences.h"
#include "mdl/AddRemoveNodesCommand.h"
#include "mdl/ApplyAndSwap.h"
#include "mdl/Brush.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/BrushVertexCommands.h"
#include "mdl/EntityNode.h"
#include "mdl/GameConfig.h"
#include "mdl/GameInfo.h"
#include "mdl/Grid.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/LinkedGroupUtils.h"
#include "mdl/Map.h"
#include "mdl/Map_Groups.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Material.h"
#include "mdl/ModelUtils.h"
#include "mdl/Node.h"
#include "mdl/PatchNode.h"
#include "mdl/Polyhedron3.h"
#include "mdl/SetLinkIdsCommand.h"
#include "mdl/SwapNodeContentsCommand.h"
#include "mdl/Texture.h"
#include "mdl/Transaction.h"
#include "mdl/VertexHandleManager.h"
#include "mdl/WorldNode.h"

#include "kd/overload.h"
#include "kd/ranges/as_rvalue_view.h"
#include "kd/ranges/to.h"
#include "kd/reflection_impl.h"
#include "kd/result_fold.h"
#include "kd/string_format.h"
#include "kd/task_manager.h"

#include "vm/constants.h"
#include "vm/quat.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>
#include <optional>
#include <random>
#include <ranges>
#include <utility>

namespace tb::mdl
{

namespace
{
struct PatchSample
{
  vm::vec3d position;
  vm::vec2d uv;
};

struct FaceUvMapping
{
  vm::vec3d uAxis;
  vm::vec3d vAxis;
  vm::vec2f offset;
};

std::optional<FaceUvMapping> computeFaceUvMapping(
  const vm::plane3d& plane, const std::vector<PatchSample>& samples)
{
  const auto epsilon = vm::constants<double>::point_status_epsilon();

  for (size_t i = 0; i + 2 < samples.size(); ++i)
  {
    const auto& s0 = samples[i];
    for (size_t j = i + 1; j + 1 < samples.size(); ++j)
    {
      const auto& s1 = samples[j];
      for (size_t k = j + 1; k < samples.size(); ++k)
      {
        const auto& s2 = samples[k];

        const auto v1 = s1.position - s0.position;
        const auto v2 = s2.position - s0.position;
        if (vm::squared_length(vm::cross(v1, v2))
            <= vm::constants<double>::almost_zero())
        {
          continue;
        }

        auto t1 = vm::normalize(v1);
        auto t2 = v2 - vm::dot(v2, t1) * t1;
        if (vm::squared_length(t2) <= vm::constants<double>::almost_zero())
        {
          continue;
        }
        t2 = vm::normalize(t2);

        const auto a1 = vm::dot(v1, t1);
        const auto b1 = vm::dot(v1, t2);
        const auto a2 = vm::dot(v2, t1);
        const auto b2 = vm::dot(v2, t2);
        const auto det = a1 * b2 - a2 * b1;
        if (vm::abs(det) <= epsilon)
        {
          continue;
        }

        const auto du1 = s1.uv.x() - s0.uv.x();
        const auto dv1 = s1.uv.y() - s0.uv.y();
        const auto du2 = s2.uv.x() - s0.uv.x();
        const auto dv2 = s2.uv.y() - s0.uv.y();

        const auto m00 = (du1 * b2 - du2 * b1) / det;
        const auto m01 = (a1 * du2 - a2 * du1) / det;
        const auto m10 = (dv1 * b2 - dv2 * b1) / det;
        const auto m11 = (a1 * dv2 - a2 * dv1) / det;

        const auto uAxis = t1 * m00 + t2 * m01;
        const auto vAxis = t1 * m10 + t2 * m11;
        const auto offset = vm::vec2f{
          float(s0.uv.x() - vm::dot(s0.position, uAxis)),
          float(s0.uv.y() - vm::dot(s0.position, vAxis))};

        if (vm::abs(plane.point_distance(s0.position)) > epsilon
            || vm::abs(plane.point_distance(s1.position)) > epsilon
            || vm::abs(plane.point_distance(s2.position)) > epsilon)
        {
          continue;
        }

        return FaceUvMapping{uAxis, vAxis, offset};
      }
    }
  }

  return std::nullopt;
}

Result<Brush> applyPatchUvToBrushFaces(
  Map& map,
  const PatchNode& patchNode,
  const Brush& brush,
  const std::string& patchMaterial,
  const std::string& fallbackMaterial)
{
  const auto& grid = patchNode.grid();
  auto samples = std::vector<PatchSample>{};
  samples.reserve(grid.points.size());
  for (const auto& point : grid.points)
  {
    samples.push_back(PatchSample{point.position, point.uvCoords});
  }

  auto faces = std::vector<BrushFace>{};
  faces.reserve(brush.faceCount());

  const auto mapFormat = map.worldNode().mapFormat();
  const auto pushFallbackFace = [&](const BrushFace& face, const std::string& material) {
    auto fallback = face;
    fallback.setAttributes(BrushFaceAttributes{material});
    faces.push_back(std::move(fallback));
  };

  for (const auto& face : brush.faces())
  {
    const auto mapping = computeFaceUvMapping(face.boundary(), samples);
    if (!mapping)
    {
      auto attributes = BrushFaceAttributes{fallbackMaterial};
      BrushFace::create(
        face.points()[0], face.points()[1], face.points()[2], attributes, mapFormat)
        | kdl::transform([&](auto newFace) { faces.push_back(std::move(newFace)); })
        | kdl::transform_error([&](auto e) {
            map.logger().error()
              << "Could not build fallback face for patch conversion: " << e.msg;
            pushFallbackFace(face, fallbackMaterial);
          });
      continue;
    }

    auto attributes = BrushFaceAttributes{patchMaterial};
    attributes.setOffset(mapping->offset);
    attributes.setScale(vm::vec2f{1.0f, 1.0f});
    attributes.setRotation(0.0f);
    attributes.setSurfaceContents(patchNode.patch().surfaceContents());
    attributes.setSurfaceFlags(patchNode.patch().surfaceFlags());
    attributes.setSurfaceValue(patchNode.patch().surfaceValue());

    BrushFace::createFromValve(
      face.points()[0],
      face.points()[1],
      face.points()[2],
      attributes,
      mapping->uAxis,
      mapping->vAxis,
      mapFormat)
      | kdl::transform([&](auto newFace) { faces.push_back(std::move(newFace)); })
      | kdl::transform_error([&](auto e) {
          map.logger().error()
            << "Could not build face from patch projection: " << e.msg;
          pushFallbackFace(face, fallbackMaterial);
        });
  }

  return Brush::create(map.worldBounds(), std::move(faces));
}

const BrushEdge* findEdgeByPositions(
  const Brush& brush, const vm::segment3d& edgePosition, const double epsilon)
{
  for (const auto* edge : brush.edges())
  {
    if (edge->hasPositions(edgePosition.start(), edgePosition.end(), epsilon))
    {
      return edge;
    }
  }

  return nullptr;
}

void setFaceAttributes(const std::vector<BrushFace>& faces, BrushFace& toSet)
{
  contract_pre(!faces.empty());

  auto faceIt = std::begin(faces);
  const auto faceEnd = std::end(faces);
  auto bestMatch = faceIt++;

  while (faceIt != faceEnd)
  {
    const auto& face = *faceIt;

    const auto bestDiff = bestMatch->boundary().normal - toSet.boundary().normal;
    const auto curDiff = face.boundary().normal - toSet.boundary().normal;
    if (vm::squared_length(curDiff) < vm::squared_length(bestDiff))
    {
      bestMatch = faceIt;
    }

    ++faceIt;
  }

  toSet.setAttributes(*bestMatch);
}

vm::vec3d brushInteriorPoint(const Brush& brush)
{
  const auto vertices = brush.vertexPositions();
  if (vertices.empty())
  {
    return brush.bounds().center();
  }

  auto sum = vm::vec3d{0.0, 0.0, 0.0};
  for (const auto& vertex : vertices)
  {
    sum = sum + vertex;
  }

  return sum / static_cast<double>(vertices.size());
}

void orientFaceToBrush(const Brush& brush, BrushFace& face)
{
  if (face.boundary().point_distance(brushInteriorPoint(brush)) > 0.0)
  {
    face.invert();
  }
}

bool chamferBrushEdge(
  Map& map,
  Brush& brush,
  const vm::segment3d& edgePosition,
  const double distance,
  const size_t segments,
  bool& didChamfer)
{
  const auto* edge = findEdgeByPositions(
    brush, edgePosition, vm::constants<double>::point_status_epsilon());
  if (edge == nullptr || !edge->fullySpecified())
  {
    return true;
  }

  const auto* faceGeometry1 = edge->firstFace();
  const auto* faceGeometry2 = edge->secondFace();
  if (faceGeometry1 == nullptr || faceGeometry2 == nullptr)
  {
    return true;
  }

  const auto faceIndex1 = faceGeometry1->payload();
  const auto faceIndex2 = faceGeometry2->payload();
  if (!faceIndex1 || !faceIndex2)
  {
    return true;
  }

  const auto& face1 = brush.face(*faceIndex1);
  const auto& face2 = brush.face(*faceIndex2);

  const auto axis = edge->segment().direction();
  if (vm::squared_length(axis) <= vm::constants<double>::almost_zero())
  {
    return true;
  }

  auto n1 = face1.normal();
  auto n2 = face2.normal();

  n1 = n1 - axis * vm::dot(n1, axis);
  n2 = n2 - axis * vm::dot(n2, axis);

  if (
    vm::squared_length(n1) <= vm::constants<double>::almost_zero()
    || vm::squared_length(n2) <= vm::constants<double>::almost_zero())
  {
    return true;
  }

  n1 = vm::normalize(n1);
  n2 = vm::normalize(n2);

  const auto dotNormals = vm::dot(n1, n2);
  if (std::abs(dotNormals) >= 1.0 - vm::constants<double>::almost_zero())
  {
    return true;
  }

  const auto angle =
    std::atan2(vm::dot(axis, vm::cross(n1, n2)), dotNormals);
  if (std::abs(angle) <= vm::constants<double>::almost_zero())
  {
    return true;
  }

  const auto step = angle / static_cast<double>(segments);
  const auto start = edge->segment().start();
  const auto& worldBounds = map.worldBounds();

  for (size_t i = 0; i < segments; ++i)
  {
    const auto n0 =
      vm::quatd{axis, step * static_cast<double>(i)} * n1;
    const auto n1Step =
      vm::quatd{axis, step * static_cast<double>(i + 1u)} * n1;

    const auto p0 = start - n0 * distance;
    const auto p1 = start - n1Step * distance;
    const auto p2 = p0 + axis;

    const auto success =
      BrushFace::create(
        p0,
        p2,
        p1,
        BrushFaceAttributes(map.currentMaterialName()),
        map.worldNode().mapFormat())
      | kdl::and_then([&](BrushFace&& clipFace) {
          orientFaceToBrush(brush, clipFace);
          setFaceAttributes(brush.faces(), clipFace);
          return brush.clip(worldBounds, std::move(clipFace));
        })
      | kdl::if_error([&](auto e) {
          map.logger().error() << "Could not chamfer brush edge: " << e.msg;
        })
      | kdl::is_success();

    if (!success)
    {
      return false;
    }

    didChamfer = true;
  }

  return true;
}

std::optional<size_t> selectedPatchRowOrColumn(
  const BezierPatch& patch,
  const std::vector<vm::vec3d>& selectedHandlePositions,
  const bool column)
{
  if (selectedHandlePositions.empty())
  {
    return std::nullopt;
  }

  constexpr auto HandleMatchEpsilon = 0.001;
  const auto isSelectedControlPoint = [&](const size_t row, const size_t col) {
    const auto position = patch.controlPoint(row, col).xyz();
    return std::ranges::any_of(selectedHandlePositions, [&](const auto& selectedPosition) {
      return vm::is_equal(position, selectedPosition, HandleMatchEpsilon);
    });
  };

  if (!column)
  {
    for (size_t col = 0u; col < patch.pointColumnCount(); ++col)
    {
      for (size_t row = 1u; row < patch.pointRowCount(); row += 2u)
      {
        if (isSelectedControlPoint(row, col))
        {
          return row;
        }
      }
    }

    for (size_t col = 0u; col < patch.pointColumnCount(); ++col)
    {
      for (size_t row = 0u; row < patch.pointRowCount(); row += 2u)
      {
        if (isSelectedControlPoint(row, col))
        {
          return row;
        }
      }
    }

    return std::nullopt;
  }

  for (size_t row = 0u; row < patch.pointRowCount(); ++row)
  {
    for (size_t col = 1u; col < patch.pointColumnCount(); col += 2u)
    {
      if (isSelectedControlPoint(row, col))
      {
        return col;
      }
    }
  }

  for (size_t row = 0u; row < patch.pointRowCount(); ++row)
  {
    for (size_t col = 0u; col < patch.pointColumnCount(); col += 2u)
    {
      if (isSelectedControlPoint(row, col))
      {
        return col;
      }
    }
  }

  return std::nullopt;
}

template <typename F>
bool applyPatchOperation(Map& map, const std::string& commandName, const F& operation)
{
  const auto& selectedPatches = map.selection().allPatches();
  if (selectedPatches.empty())
  {
    return false;
  }

  bool didChange = false;

  auto newNodes = applyToNodeContents(
    selectedPatches,
    kdl::overload(
      [](Layer&) { return true; },
      [](Group&) { return true; },
      [](Entity&) { return true; },
      [](Brush&) { return true; },
      [&](BezierPatch& patch) {
        didChange = operation(patch) || didChange;
        return true;
      }));

  if (!newNodes || !didChange)
  {
    return false;
  }

  auto changedLinkedGroups = collectContainingGroups(
    *newNodes | std::views::keys | kdl::ranges::to<std::vector>());

  return updateNodeContents(
    map, commandName, std::move(*newNodes), std::move(changedLinkedGroups));
}

std::pair<size_t, size_t> patchTextureSize(const BezierPatch& patch)
{
  if (const auto* material = patch.material(); material && material->texture())
  {
    return {
      std::max<size_t>(1u, material->texture()->width()),
      std::max<size_t>(1u, material->texture()->height())};
  }

  return {1u, 1u};
}

struct PatchAxisMap
{
  size_t x;
  size_t y;
  size_t z;
};

PatchAxisMap axisMapForPatchNormal(const vm::axis::type axis)
{
  switch (axis)
  {
  case vm::axis::x:
    // Patch lies in the YZ plane.
    return {1u, 2u, 0u};
  case vm::axis::y:
    // Patch lies in the ZX plane.
    return {2u, 0u, 1u};
  case vm::axis::z:
  default:
    // Patch lies in the XY plane.
    return {0u, 1u, 2u};
  }
}

bool supportsPatchPrimitives(const MapFormat format)
{
  return format == MapFormat::Quake3 || format == MapFormat::Quake3_Legacy
         || format == MapFormat::Quake3_Valve;
}

size_t sanitizePatchDimension(
  size_t value, const size_t minValue, const size_t maxValue = 31u)
{
  value = std::clamp(value, minValue, maxValue);
  if (value % 2u == 0u)
  {
    value = value < maxValue ? value + 1u : value - 1u;
  }
  return value;
}

vm::bbox3d patchCreationBounds(const Map& map)
{
  const auto bounds = map.referenceBounds();
  const auto center = bounds.center();
  auto halfSize = bounds.size() / 2.0;

  const auto minHalfExtent = std::max(1.0, map.grid().actualSize());
  for (size_t i = 0u; i < 3u; ++i)
  {
    if (halfSize[i] <= vm::constants<double>::almost_zero())
    {
      halfSize[i] = minHalfExtent;
    }
  }

  return {center - halfSize, center + halfSize};
}

BezierPatch::Point makePatchPoint(
  const vm::vec3d& position, const double s = 0.0, const double t = 0.0)
{
  return {position.x(), position.y(), position.z(), s, t};
}

void setPatchPoint(
  std::vector<BezierPatch::Point>& controlPoints,
  const size_t columns,
  const size_t row,
  const size_t col,
  const vm::vec3d& position,
  const double s = 0.0,
  const double t = 0.0)
{
  controlPoints[row * columns + col] = makePatchPoint(position, s, t);
}

vm::vec3d makeMappedPoint(
  const std::array<vm::vec3d, 3>& vPos,
  const PatchAxisMap& axisMap,
  const size_t xIndex,
  const size_t yIndex,
  const size_t zIndex)
{
  auto result = vm::vec3d{0.0, 0.0, 0.0};
  result[axisMap.x] = vPos[xIndex][axisMap.x];
  result[axisMap.y] = vPos[yIndex][axisMap.y];
  result[axisMap.z] = vPos[zIndex][axisMap.z];
  return result;
}

bool hasNonDegenerateDimensions(const BezierPatch& patch)
{
  constexpr auto epsilon = 0.01;
  const auto size = patch.bounds().size();
  auto significantComponents = size_t{0u};
  for (size_t i = 0u; i < 3u; ++i)
  {
    if (size[i] > epsilon)
    {
      ++significantComponents;
    }
  }
  return significantComponents >= 2;
}

template <typename I>
std::optional<BezierPatch> makePatch(
  const size_t rows,
  const size_t cols,
  I begin,
  I end,
  const std::string& materialName,
  const int surfaceContents = 0,
  const int surfaceFlags = 0,
  const float surfaceValue = 0.0f,
  const bool requireNonDegenerate = true)
{
  auto controlPoints = std::vector<BezierPatch::Point>(begin, end);
  if (controlPoints.size() != rows * cols)
  {
    return std::nullopt;
  }

  auto patch = BezierPatch{
    rows,
    cols,
    std::move(controlPoints),
    materialName,
    surfaceContents,
    surfaceFlags,
    surfaceValue};

  if (requireNonDegenerate && !hasNonDegenerateDimensions(patch))
  {
    return std::nullopt;
  }

  return patch;
}

enum class CapEdge
{
  FirstRow,
  LastRow,
};

std::vector<vm::vec3d> patchEdge(const BezierPatch& patch, const CapEdge edge)
{
  const auto row = edge == CapEdge::FirstRow ? 0u : patch.pointRowCount() - 1u;

  auto result = std::vector<vm::vec3d>{};
  result.reserve(patch.pointColumnCount());
  for (size_t i = 0u; i < patch.pointColumnCount(); ++i)
  {
    const auto col =
      edge == CapEdge::FirstRow ? i : patch.pointColumnCount() - 1u - i;
    result.push_back(patch.controlPoint(row, col).xyz());
  }
  return result;
}

bool patchWidthCompatibleWithCap(const PatchCapType type, const size_t width)
{
  switch (type)
  {
  case PatchCapType::EndCap:
  case PatchCapType::InvertedEndCap:
    return width == 5u;
  case PatchCapType::Bevel:
  case PatchCapType::InvertedBevel:
    return width == 3u || width == 5u;
  case PatchCapType::Cylinder:
    return width == 9u;
  }

  return false;
}

std::optional<BezierPatch> makeCapPatchFromSeam(
  const std::vector<vm::vec3d>& seam,
  const PatchCapType type,
  const std::string& materialName,
  const int surfaceContents,
  const int surfaceFlags,
  const float surfaceValue)
{
  if (seam.size() < 3u)
  {
    return std::nullopt;
  }

  auto points = std::vector<BezierPatch::Point>{};
  auto rows = size_t{0u};
  auto cols = size_t{0u};

  switch (type)
  {
  case PatchCapType::InvertedBevel: {
    rows = 3u;
    cols = 3u;
    points.resize(rows * cols);
    const auto& p0 = seam[0];
    const auto& p1 = seam[1];
    const auto& p2 = seam[2];
    setPatchPoint(points, cols, 0u, 0u, p0);
    setPatchPoint(points, cols, 0u, 1u, p1);
    setPatchPoint(points, cols, 0u, 2u, p1);
    setPatchPoint(points, cols, 1u, 0u, p1);
    setPatchPoint(points, cols, 1u, 1u, p1);
    setPatchPoint(points, cols, 1u, 2u, p1);
    setPatchPoint(points, cols, 2u, 0u, p2);
    setPatchPoint(points, cols, 2u, 1u, p1);
    setPatchPoint(points, cols, 2u, 2u, p1);
    break;
  }
  case PatchCapType::Bevel: {
    rows = 3u;
    cols = 3u;
    points.resize(rows * cols);
    const auto& p0 = seam[0];
    const auto& p1 = seam[1];
    const auto& p2 = seam[2];
    const auto p3 = p2 + (p0 - p1);
    setPatchPoint(points, cols, 0u, 0u, p3);
    setPatchPoint(points, cols, 0u, 1u, p3);
    setPatchPoint(points, cols, 0u, 2u, p2);
    setPatchPoint(points, cols, 1u, 0u, p3);
    setPatchPoint(points, cols, 1u, 1u, p3);
    setPatchPoint(points, cols, 1u, 2u, p1);
    setPatchPoint(points, cols, 2u, 0u, p3);
    setPatchPoint(points, cols, 2u, 1u, p3);
    setPatchPoint(points, cols, 2u, 2u, p0);
    break;
  }
  case PatchCapType::EndCap: {
    if (seam.size() < 5u)
    {
      return std::nullopt;
    }
    rows = 3u;
    cols = 3u;
    points.resize(rows * cols);
    const auto& p0 = seam[0];
    const auto& p1 = seam[1];
    const auto& p2 = seam[2];
    const auto& p3 = seam[3];
    const auto& p4 = seam[4];
    const auto p5 = (p0 + p4) / 2.0;
    setPatchPoint(points, cols, 0u, 0u, p0);
    setPatchPoint(points, cols, 0u, 1u, p5);
    setPatchPoint(points, cols, 0u, 2u, p4);
    setPatchPoint(points, cols, 1u, 0u, p1);
    setPatchPoint(points, cols, 1u, 1u, p2);
    setPatchPoint(points, cols, 1u, 2u, p3);
    setPatchPoint(points, cols, 2u, 0u, p2);
    setPatchPoint(points, cols, 2u, 1u, p2);
    setPatchPoint(points, cols, 2u, 2u, p2);
    break;
  }
  case PatchCapType::InvertedEndCap: {
    if (seam.size() < 5u)
    {
      return std::nullopt;
    }
    rows = 3u;
    cols = 5u;
    points.resize(rows * cols);
    const auto& p0 = seam[0];
    const auto& p1 = seam[1];
    const auto& p2 = seam[2];
    const auto& p3 = seam[3];
    const auto& p4 = seam[4];
    setPatchPoint(points, cols, 0u, 0u, p4);
    setPatchPoint(points, cols, 0u, 1u, p3);
    setPatchPoint(points, cols, 0u, 2u, p2);
    setPatchPoint(points, cols, 0u, 3u, p1);
    setPatchPoint(points, cols, 0u, 4u, p0);
    setPatchPoint(points, cols, 1u, 0u, p3);
    setPatchPoint(points, cols, 1u, 1u, p3);
    setPatchPoint(points, cols, 1u, 2u, p2);
    setPatchPoint(points, cols, 1u, 3u, p1);
    setPatchPoint(points, cols, 1u, 4u, p1);
    setPatchPoint(points, cols, 2u, 0u, p3);
    setPatchPoint(points, cols, 2u, 1u, p3);
    setPatchPoint(points, cols, 2u, 2u, p2);
    setPatchPoint(points, cols, 2u, 3u, p1);
    setPatchPoint(points, cols, 2u, 4u, p1);
    break;
  }
  case PatchCapType::Cylinder: {
    auto seamPoints = seam;
    auto mid = (seamPoints.size() - 1u) / 2u;
    const bool degenerate = (mid % 2u) != 0u;
    rows = mid + (degenerate ? 2u : 1u);
    cols = 3u;
    if (degenerate)
    {
      ++mid;
      seamPoints.push_back(seamPoints.back());
      seamPoints.push_back(seamPoints.back());
    }

    points.resize(rows * cols);
    const auto h = rows - 1u;
    for (size_t i = 0u; i < rows; ++i)
    {
      setPatchPoint(points, cols, i, 0u, seamPoints[i]);
      setPatchPoint(points, cols, i, 2u, seamPoints[h + (h - i)]);
    }
    break;
  }
  }

  auto cap = makePatch(
    rows,
    cols,
    std::begin(points),
    std::end(points),
    materialName,
    surfaceContents,
    surfaceFlags,
    surfaceValue,
    false);
  if (!cap)
  {
    return std::nullopt;
  }

  if (type == PatchCapType::Cylinder)
  {
    cap->redisperse(BezierPatch::MatrixMajor::Column);
  }
  cap->capTexture();
  return cap;
}

std::optional<BezierPatch> makeWallPatch(
  const BezierPatch& source,
  const BezierPatch& target,
  const int edgeIndex,
  const std::string& materialName)
{
  const auto rows = size_t{3u};
  const auto cols =
    edgeIndex < 2 ? source.pointColumnCount() : source.pointRowCount();
  auto points = std::vector<BezierPatch::Point>{};
  points.resize(rows * cols);

  for (size_t i = 0u; i < cols; ++i)
  {
    BezierPatch::Point sourcePoint;
    BezierPatch::Point targetPoint;
    switch (edgeIndex)
    {
    case 0:
      sourcePoint = source.controlPoint(0u, i);
      targetPoint = target.controlPoint(0u, i);
      break;
    case 1:
      sourcePoint = source.controlPoint(source.pointRowCount() - 1u, i);
      targetPoint = target.controlPoint(target.pointRowCount() - 1u, i);
      break;
    case 2:
      sourcePoint = source.controlPoint(i, 0u);
      targetPoint = target.controlPoint(i, 0u);
      break;
    case 3:
      sourcePoint = source.controlPoint(i, source.pointColumnCount() - 1u);
      targetPoint = target.controlPoint(i, target.pointColumnCount() - 1u);
      break;
    default:
      return std::nullopt;
    }

    const auto middlePosition = (sourcePoint.xyz() + targetPoint.xyz()) / 2.0;
    const auto middleS = (sourcePoint[3] + targetPoint[3]) / 2.0;
    const auto middleT = (sourcePoint[4] + targetPoint[4]) / 2.0;

    setPatchPoint(points, cols, 0u, i, sourcePoint.xyz(), sourcePoint[3], sourcePoint[4]);
    setPatchPoint(points, cols, 1u, i, middlePosition, middleS, middleT);
    setPatchPoint(points, cols, 2u, i, targetPoint.xyz(), targetPoint[3], targetPoint[4]);
  }

  auto wall = makePatch(
    rows,
    cols,
    std::begin(points),
    std::end(points),
    materialName,
    source.surfaceContents(),
    source.surfaceFlags(),
    source.surfaceValue());
  if (!wall)
  {
    return std::nullopt;
  }

  if (edgeIndex == 0 || edgeIndex == 3)
  {
    wall->invertMatrix();
  }

  wall->naturalTexture();
  return wall;
}

bool arePatchEdgeRowsClosed(const BezierPatch& patch)
{
  constexpr auto epsilon = 0.1;
  for (size_t col = 0u; col < patch.pointColumnCount(); ++col)
  {
    if (
      vm::squared_distance(
        patch.controlPoint(0u, col).xyz(),
        patch.controlPoint(patch.pointRowCount() - 1u, col).xyz())
      > epsilon * epsilon)
    {
      return false;
    }
  }
  return true;
}

bool arePatchEdgeColumnsClosed(const BezierPatch& patch)
{
  constexpr auto epsilon = 0.1;
  for (size_t row = 0u; row < patch.pointRowCount(); ++row)
  {
    if (
      vm::squared_distance(
        patch.controlPoint(row, 0u).xyz(),
        patch.controlPoint(row, patch.pointColumnCount() - 1u).xyz())
      > epsilon * epsilon)
    {
      return false;
    }
  }
  return true;
}

BezierPatch makeThickenedOppositePatch(
  const BezierPatch& source,
  const double thickness,
  const PatchThickenAxis axis)
{
  auto opposite = source;
  opposite.setControlNormals({});

  std::optional<PatchGrid> sourceGrid = std::nullopt;
  if (axis == PatchThickenAxis::Normal)
  {
    sourceGrid = makePatchGrid(source, 0u);
  }

  const auto axisNormal = [&](const size_t row, const size_t col) {
    switch (axis)
    {
    case PatchThickenAxis::X:
      return vm::vec3d{1.0, 0.0, 0.0};
    case PatchThickenAxis::Y:
      return vm::vec3d{0.0, 1.0, 0.0};
    case PatchThickenAxis::Z:
      return vm::vec3d{0.0, 0.0, 1.0};
    case PatchThickenAxis::Normal:
      return sourceGrid->point(row, col).normal;
    }

    return vm::vec3d{0.0, 0.0, 1.0};
  };

  for (size_t row = 0u; row < source.pointRowCount(); ++row)
  {
    for (size_t col = 0u; col < source.pointColumnCount(); ++col)
    {
      auto point = source.controlPoint(row, col);
      const auto shifted = point.xyz() + axisNormal(row, col) * thickness;
      point[0] = shifted.x();
      point[1] = shifted.y();
      point[2] = shifted.z();
      opposite.setControlPoint(row, col, point);
    }
  }

  opposite.invertMatrix();
  return opposite;
}

std::optional<BezierPatch> makePatchPrefab(
  const vm::bbox3d& bounds,
  const PatchPrefabType type,
  const vm::axis::type axis,
  size_t width,
  size_t height,
  const bool redisperse,
  const std::string& materialName)
{
  const auto axisMap = axisMapForPatchNormal(axis);
  const auto vPos = std::array<vm::vec3d, 3>{bounds.min, bounds.center(), bounds.max};

  auto points = std::vector<BezierPatch::Point>{};
  auto rows = size_t{0u};
  auto cols = size_t{0u};

  if (type == PatchPrefabType::Plane)
  {
    rows = sanitizePatchDimension(height, 3u);
    cols = sanitizePatchDimension(width, 3u);
    points.resize(rows * cols);

    const auto minX = vPos[0][axisMap.x];
    const auto maxX = vPos[2][axisMap.x];
    const auto minY = vPos[0][axisMap.y];
    const auto maxY = vPos[2][axisMap.y];
    const auto fixedZ = vPos[2][axisMap.z];

    const auto xStep = std::abs((minX - maxX) / static_cast<double>(cols - 1u));
    const auto yStep = std::abs((minY - maxY) / static_cast<double>(rows - 1u));

    auto curY = minY;
    for (size_t row = 0u; row < rows; ++row)
    {
      auto curX = minX;
      for (size_t col = 0u; col < cols; ++col)
      {
        auto position = vm::vec3d{0.0, 0.0, 0.0};
        position[axisMap.x] = curX;
        position[axisMap.y] = curY;
        position[axisMap.z] = fixedZ;
        setPatchPoint(points, cols, row, col, position);
        curX += xStep;
      }
      curY += yStep;
    }
  }
  else if (
    type == PatchPrefabType::SquareCylinder || type == PatchPrefabType::Cylinder
    || type == PatchPrefabType::Cone || type == PatchPrefabType::Sphere)
  {
    cols = 9u;
    rows = type == PatchPrefabType::Sphere ? 5u : 3u;
    points.resize(rows * cols);

    constexpr auto cylIndex = std::array<unsigned char, 18>{
      0, 0, 1, 0, 2, 0, 2, 1, 2, 2, 1, 2, 0, 2, 0, 1, 0, 0};

    const auto startRow = type == PatchPrefabType::Sphere ? 1u : 0u;
    const auto startCol = type == PatchPrefabType::SquareCylinder ? 0u : 1u;
    for (size_t h = 0u; h < 3u; ++h)
    {
      const auto row = startRow + h;
      for (size_t w = 0u; w < 8u; ++w)
      {
        const auto xIndex = static_cast<size_t>(cylIndex[2u * w]);
        const auto yIndex = static_cast<size_t>(cylIndex[2u * w + 1u]);
        setPatchPoint(
          points,
          cols,
          row,
          startCol + w,
          makeMappedPoint(vPos, axisMap, xIndex, yIndex, h));
      }
    }

    switch (type)
    {
    case PatchPrefabType::SquareCylinder:
      for (size_t row = 0u; row < 3u; ++row)
      {
        setPatchPoint(points, cols, row, 8u, points[row * cols + 0u].xyz());
      }
      break;
    case PatchPrefabType::Cylinder:
      for (size_t row = 0u; row < 3u; ++row)
      {
        setPatchPoint(points, cols, row, 0u, points[row * cols + 8u].xyz());
      }
      break;
    case PatchPrefabType::Cone:
      for (size_t row = 0u; row < 2u; ++row)
      {
        setPatchPoint(points, cols, row, 0u, points[row * cols + 8u].xyz());
      }
      for (size_t col = 0u; col < cols; ++col)
      {
        setPatchPoint(points, cols, 2u, col, vPos[1]);
        points[2u * cols + col][axisMap.z] = vPos[2][axisMap.z];
      }
      break;
    case PatchPrefabType::Sphere:
      for (size_t row = 1u; row < 4u; ++row)
      {
        setPatchPoint(points, cols, row, 0u, points[row * cols + 8u].xyz());
      }
      for (size_t col = 0u; col < cols; ++col)
      {
        auto top = vPos[1];
        top[axisMap.z] = vPos[0][axisMap.z];
        setPatchPoint(points, cols, 0u, col, top);

        auto bottom = vPos[1];
        bottom[axisMap.z] = vPos[2][axisMap.z];
        setPatchPoint(points, cols, 4u, col, bottom);
      }
      break;
    default:
      break;
    }
  }
  else if (type == PatchPrefabType::ExactCylinder)
  {
    cols = sanitizePatchDimension(width, 7u);
    rows = sanitizePatchDimension(height, 3u);
    points.resize(rows * cols);

    const auto n = static_cast<double>((cols - 1u) / 2u);
    const auto f = 1.0 / std::cos(std::numbers::pi / n);
    for (size_t col = 0u; col < cols; ++col)
    {
      const auto angle = (std::numbers::pi * static_cast<double>(col)) / n;
      const auto x =
        vPos[1][axisMap.x]
        + (vPos[2][axisMap.x] - vPos[1][axisMap.x]) * std::cos(angle)
            * ((col & 1u) != 0u ? f : 1.0);
      const auto y =
        vPos[1][axisMap.y]
        + (vPos[2][axisMap.y] - vPos[1][axisMap.y]) * std::sin(angle)
            * ((col & 1u) != 0u ? f : 1.0);
      for (size_t row = 0u; row < rows; ++row)
      {
        const auto z = vPos[0][axisMap.z]
                       + (vPos[2][axisMap.z] - vPos[0][axisMap.z])
                           * (static_cast<double>(row)
                              / static_cast<double>(rows - 1u));
        auto position = vm::vec3d{0.0, 0.0, 0.0};
        position[axisMap.x] = x;
        position[axisMap.y] = y;
        position[axisMap.z] = z;
        setPatchPoint(points, cols, row, col, position);
      }
    }
  }
  else if (type == PatchPrefabType::ExactCone)
  {
    cols = sanitizePatchDimension(width, 7u);
    rows = sanitizePatchDimension(height, 3u);
    points.resize(rows * cols);

    const auto n = static_cast<double>((cols - 1u) / 2u);
    const auto f = 1.0 / std::cos(std::numbers::pi / n);
    for (size_t col = 0u; col < cols; ++col)
    {
      const auto angle = (std::numbers::pi * static_cast<double>(col)) / n;
      for (size_t row = 0u; row < rows; ++row)
      {
        const auto heightFactor =
          1.0 - static_cast<double>(row) / static_cast<double>(rows - 1u);
        const auto x =
          vPos[1][axisMap.x]
          + heightFactor * (vPos[2][axisMap.x] - vPos[1][axisMap.x]) * std::cos(angle)
              * ((col & 1u) != 0u ? f : 1.0);
        const auto y =
          vPos[1][axisMap.y]
          + heightFactor * (vPos[2][axisMap.y] - vPos[1][axisMap.y]) * std::sin(angle)
              * ((col & 1u) != 0u ? f : 1.0);
        const auto z = vPos[0][axisMap.z]
                       + (vPos[2][axisMap.z] - vPos[0][axisMap.z])
                           * (static_cast<double>(row)
                              / static_cast<double>(rows - 1u));

        auto position = vm::vec3d{0.0, 0.0, 0.0};
        position[axisMap.x] = x;
        position[axisMap.y] = y;
        position[axisMap.z] = z;
        setPatchPoint(points, cols, row, col, position);
      }
    }
  }
  else if (type == PatchPrefabType::ExactSphere)
  {
    cols = sanitizePatchDimension(width, 7u);
    rows = sanitizePatchDimension(height, 5u);
    points.resize(rows * cols);

    const auto n = static_cast<double>((cols - 1u) / 2u);
    const auto m = static_cast<double>((rows - 1u) / 2u);
    const auto f = 1.0 / std::cos(std::numbers::pi / n);
    const auto g = 1.0 / std::cos(std::numbers::pi / (2.0 * m));

    for (size_t col = 0u; col < cols; ++col)
    {
      const auto yaw = (std::numbers::pi * static_cast<double>(col)) / n;
      for (size_t row = 0u; row < rows; ++row)
      {
        const auto pitch = (std::numbers::pi * static_cast<double>(row)) / (2.0 * m);
        const auto sinPitch = std::sin(pitch) * ((row & 1u) != 0u ? g : 1.0);
        const auto x =
          vPos[1][axisMap.x]
          + (vPos[2][axisMap.x] - vPos[1][axisMap.x]) * sinPitch * std::cos(yaw)
              * ((col & 1u) != 0u ? f : 1.0);
        const auto y =
          vPos[1][axisMap.y]
          + (vPos[2][axisMap.y] - vPos[1][axisMap.y]) * sinPitch * std::sin(yaw)
              * ((col & 1u) != 0u ? f : 1.0);
        const auto z =
          vPos[1][axisMap.z]
          + (vPos[2][axisMap.z] - vPos[1][axisMap.z])
              * (-std::cos(pitch) * ((row & 1u) != 0u ? g : 1.0));

        auto position = vm::vec3d{0.0, 0.0, 0.0};
        position[axisMap.x] = x;
        position[axisMap.y] = y;
        position[axisMap.z] = z;
        setPatchPoint(points, cols, row, col, position);
      }
    }
  }
  else if (type == PatchPrefabType::Bevel)
  {
    rows = 3u;
    cols = 3u;
    points.resize(rows * cols);
    constexpr auto bevelIndex = std::array<unsigned char, 6>{0, 0, 2, 0, 2, 2};
    for (size_t row = 0u; row < 3u; ++row)
    {
      for (size_t col = 0u; col < 3u; ++col)
      {
        const auto xIndex = static_cast<size_t>(bevelIndex[2u * col]);
        const auto yIndex = static_cast<size_t>(bevelIndex[2u * col + 1u]);
        setPatchPoint(
          points,
          cols,
          row,
          col,
          makeMappedPoint(vPos, axisMap, xIndex, yIndex, row));
      }
    }
  }
  else if (type == PatchPrefabType::EndCap)
  {
    rows = 3u;
    cols = 5u;
    points.resize(rows * cols);
    constexpr auto endCapIndex =
      std::array<unsigned char, 10>{2, 0, 2, 2, 1, 2, 0, 2, 0, 0};
    for (size_t row = 0u; row < 3u; ++row)
    {
      for (size_t col = 0u; col < 5u; ++col)
      {
        const auto xIndex = static_cast<size_t>(endCapIndex[2u * col]);
        const auto yIndex = static_cast<size_t>(endCapIndex[2u * col + 1u]);
        setPatchPoint(
          points,
          cols,
          row,
          col,
          makeMappedPoint(vPos, axisMap, xIndex, yIndex, row));
      }
    }
  }

  auto patch = makePatch(
    rows, cols, std::begin(points), std::end(points), materialName);
  if (!patch)
  {
    return std::nullopt;
  }

  if (redisperse && type != PatchPrefabType::Plane)
  {
    patch->redisperse(BezierPatch::MatrixMajor::Column);
    patch->redisperse(BezierPatch::MatrixMajor::Row);
  }

  if (type == PatchPrefabType::Plane)
  {
    patch->capTexture();
  }
  else
  {
    patch->naturalTexture();
  }

  return patch;
}

Node* patchCreationParent(Map& map)
{
  const auto& selectedNodes = map.selection().nodes;
  if (!selectedNodes.empty())
  {
    auto* first = selectedNodes.front();
    if (
      dynamic_cast<LayerNode*>(first) != nullptr || dynamic_cast<GroupNode*>(first) != nullptr
      || dynamic_cast<EntityNode*>(first) != nullptr)
    {
      return first;
    }
    if (auto* parent = first->parent())
    {
      return parent;
    }
  }
  return parentForNodes(map, selectedNodes);
}
} // namespace

kdl_reflect_impl(TransformVerticesResult);

bool transformSelection(
  Map& map, const std::string& commandName, const vm::mat4x4d& transformation)
{
  if (map.vertexHandles().anySelected())
  {
    return transformVertices(map, map.vertexHandles().selectedHandles(), transformation)
      .success;
  }

  auto nodesToTransform = std::vector<Node*>{};
  auto entitiesToTransform = std::unordered_map<EntityNodeBase*, size_t>{};

  for (auto* node : map.selection().nodes)
  {
    node->accept(kdl::overload(
      [&](auto&& thisLambda, WorldNode* worldNode) {
        worldNode->visitChildren(thisLambda);
      },
      [&](auto&& thisLambda, LayerNode* layerNode) {
        layerNode->visitChildren(thisLambda);
      },
      [&](auto&& thisLambda, GroupNode* groupNode) {
        nodesToTransform.push_back(groupNode);
        groupNode->visitChildren(thisLambda);
      },
      [&](auto&& thisLambda, EntityNode* entityNode) {
        if (!entityNode->hasChildren())
        {
          nodesToTransform.push_back(entityNode);
        }
        else
        {
          entityNode->visitChildren(thisLambda);
        }
      },
      [&](BrushNode* brushNode) {
        nodesToTransform.push_back(brushNode);
        entitiesToTransform[brushNode->entity()]++;
      },
      [&](PatchNode* patchNode) {
        nodesToTransform.push_back(patchNode);
        entitiesToTransform[patchNode->entity()]++;
      }));
  }

  // add entities if all of their children are transformed
  for (const auto& [entityNode, transformedChildCount] : entitiesToTransform)
  {
    if (
      transformedChildCount == entityNode->childCount()
      && !isWorldspawn(entityNode->entity().classname()))
    {
      nodesToTransform.push_back(entityNode);
    }
  }

  using TransformResult = Result<std::pair<Node*, NodeContents>>;

  const auto alignmentLock = pref(Preferences::AlignmentLock);
  const auto updateAngleProperty =
    map.worldNode().entityPropertyConfig().updateAnglePropertyAfterTransform;

  auto tasks =
    nodesToTransform | std::views::transform([&](auto& node) {
      return std::function{[&]() {
        return node->accept(kdl::overload(
          [&](WorldNode*) -> TransformResult { contract_assert(false); },
          [&](LayerNode*) -> TransformResult { contract_assert(false); },
          [&](GroupNode* groupNode) -> TransformResult {
            auto group = groupNode->group();
            group.transform(transformation);
            return std::make_pair(groupNode, NodeContents{std::move(group)});
          },
          [&](EntityNode* entityNode) -> TransformResult {
            auto entity = entityNode->entity();
            entity.transform(transformation, updateAngleProperty);
            return std::make_pair(entityNode, NodeContents{std::move(entity)});
          },
          [&](BrushNode* brushNode) -> TransformResult {
            const auto* containingGroup = brushNode->containingGroup();
            const bool lockAlignment =
            alignmentLock
            || (containingGroup && containingGroup->closed() && collectLinkedNodes({&map.worldNode()}, *brushNode).size() > 1);

            auto brush = brushNode->brush();
            return brush.transform(map.worldBounds(), transformation, lockAlignment)
                   | kdl::and_then([&]() -> TransformResult {
                       return std::make_pair(brushNode, NodeContents{std::move(brush)});
                     });
          },
          [&](PatchNode* patchNode) -> TransformResult {
            auto patch = patchNode->patch();
            patch.transform(transformation);
            return std::make_pair(patchNode, NodeContents{std::move(patch)});
          }));
      }};
    });

  const auto success = map.taskManager().run_tasks_and_wait(tasks) | kdl::fold
                       | kdl::transform([&](auto nodesToUpdate) {
                           return updateNodeContents(
                             map,
                             commandName,
                             std::move(nodesToUpdate),
                             collectContainingGroups(map.selection().nodes));
                         })
                       | kdl::value_or(false);

  if (success)
  {
    map.pushRepeatableCommand([&, commandName, transformation]() {
      transformSelection(map, commandName, transformation);
    });
  }

  return success;
}

bool translateSelection(mdl::Map& map, const vm::vec3d& delta)
{
  return transformSelection(map, "Translate Objects", vm::translation_matrix(delta));
}

bool rotateSelection(
  mdl::Map& map, const vm::vec3d& center, const vm::vec3d& axis, double angle)
{
  const auto transformation = vm::translation_matrix(center)
                              * vm::rotation_matrix(axis, angle)
                              * vm::translation_matrix(-center);
  return transformSelection(map, "Rotate Objects", transformation);
}

bool scaleSelection(mdl::Map& map, const vm::bbox3d& oldBBox, const vm::bbox3d& newBBox)
{
  const auto transformation = vm::scale_bbox_matrix(oldBBox, newBBox);
  return transformSelection(map, "Scale Objects", transformation);
}

bool scaleSelection(mdl::Map& map, const vm::vec3d& center, const vm::vec3d& scaleFactors)
{
  const auto transformation = vm::translation_matrix(center)
                              * vm::scaling_matrix(scaleFactors)
                              * vm::translation_matrix(-center);
  return transformSelection(map, "Scale Objects", transformation);
}

bool shearSelection(
  mdl::Map& map,
  const vm::bbox3d& box,
  const vm::vec3d& sideToShear,
  const vm::vec3d& delta)
{
  const auto transformation = vm::shear_bbox_matrix(box, sideToShear, delta);
  return transformSelection(map, "Scale Objects", transformation);
}

bool flipSelection(mdl::Map& map, const vm::vec3d& center, const vm::axis::type axis)
{
  const auto transformation = vm::translation_matrix(center)
                              * vm::mirror_matrix<double>(axis)
                              * vm::translation_matrix(-center);
  return transformSelection(map, "Flip Objects", transformation);
}


TransformVerticesResult transformVertices(
  Map& map, std::vector<vm::vec3d> vertexPositions, const vm::mat4x4d& transform)
{
  auto newVertexPositions = std::vector<vm::vec3d>{};
  const auto& selection = map.selection();
  const auto& selectedBrushes = selection.allBrushes();
  const auto& selectedPatches = selection.allPatches();
  auto selectedNodes = std::vector<Node*>{};
  selectedNodes.reserve(selectedBrushes.size() + selectedPatches.size());
  for (auto* brushNode : selectedBrushes)
  {
    selectedNodes.push_back(brushNode);
  }
  for (auto* patchNode : selectedPatches)
  {
    selectedNodes.push_back(patchNode);
  }

  if (selectedNodes.empty())
  {
    return TransformVerticesResult{false, false};
  }

  constexpr auto HandleMatchEpsilon = 0.001;
  const auto isMatchingHandlePosition = [&](const vm::vec3d& position) {
    return std::ranges::any_of(vertexPositions, [&](const auto& vertexPosition) {
      return vm::is_equal(vertexPosition, position, HandleMatchEpsilon);
    });
  };

  auto newNodes = applyToNodeContents(
    selectedNodes,
    kdl::overload(
      [](Layer&) { return true; },
      [](Group&) { return true; },
      [](Entity&) { return true; },
      [&](Brush& brush) {
        const auto verticesToMove = vertexPositions
                                    | std::views::filter([&](const auto& vertex) {
                                        return brush.hasVertex(vertex);
                                      })
                                    | kdl::ranges::to<std::vector>();
        if (verticesToMove.empty())
        {
          return true;
        }

        if (!brush.canTransformVertices(map.worldBounds(), verticesToMove, transform))
        {
          return false;
        }

        return brush.transformVertices(
                 map.worldBounds(), verticesToMove, transform, pref(Preferences::UVLock))
               | kdl::transform([&]() {
                   auto newPositions =
                     brush.findClosestVertexPositions(transform * verticesToMove);
                   newVertexPositions = kdl::vec_concat(
                     std::move(newVertexPositions), std::move(newPositions));
                 })
                | kdl::if_error([&](auto e) {
                    map.logger().error() << "Could not move brush vertices: " << e.msg;
                  })
                | kdl::is_success();
      },
      [&](BezierPatch& patch) {
        bool movedControlPoint = false;
        for (size_t row = 0u; row < patch.pointRowCount(); ++row)
        {
          for (size_t col = 0u; col < patch.pointColumnCount(); ++col)
          {
            const auto controlPoint = patch.controlPoint(row, col);
            const auto oldPosition = controlPoint.xyz();
            if (!isMatchingHandlePosition(oldPosition))
            {
              continue;
            }

            const auto newPosition = transform * oldPosition;
            patch.setControlPoint(
              row,
              col,
              BezierPatch::Point{newPosition, controlPoint[3], controlPoint[4]});
            newVertexPositions.push_back(newPosition);
            movedControlPoint = true;
          }
        }

        // If any point was edited, drop authored normals to avoid stale patchDef3 normals.
        if (movedControlPoint && patch.hasControlNormals())
        {
          patch.setControlNormals({});
        }

        return true;
      }));

  if (!newNodes)
  {
    return TransformVerticesResult{false, false};
  }

  kdl::vec_sort_and_remove_duplicates(newVertexPositions);
  const auto hasRemainingVertices = !newVertexPositions.empty();

  auto commandName = kdl::str_plural(vertexPositions.size(), "Move Vertex", "Move Vertices");
  auto transaction = Transaction{map, commandName};

  const auto changedLinkedGroups = collectContainingGroups(
    *newNodes | std::views::keys | kdl::ranges::to<std::vector>());

  auto command = std::make_unique<BrushVertexCommand>(
    std::move(commandName),
    std::move(*newNodes),
    std::move(vertexPositions),
    std::move(newVertexPositions));

  if (!map.executeAndStore(std::move(command)))
  {
    transaction.cancel();
    return TransformVerticesResult{false, false};
  }

  setHasPendingChanges(changedLinkedGroups, true);

  if (!transaction.commit())
  {
    return TransformVerticesResult{false, false};
  }

  return {true, hasRemainingVertices};
}

bool transformEdges(
  Map& map, std::vector<vm::segment3d> edgePositions, const vm::mat4x4d& transform)
{
  auto newEdgePositions = std::vector<vm::segment3d>{};
  const auto& selectedBrushes = map.selection().allBrushes();
  auto newNodes = applyToNodeContents(
    selectedBrushes,
    kdl::overload(
      [](Layer&) { return true; },
      [](Group&) { return true; },
      [](Entity&) { return true; },
      [&](Brush& brush) {
        const auto edgesToMove =
          edgePositions
          | std::views::filter([&](const auto& edge) { return brush.hasEdge(edge); })
          | kdl::ranges::to<std::vector>();
        if (edgesToMove.empty())
        {
          return true;
        }

        if (!brush.canTransformEdges(map.worldBounds(), edgesToMove, transform))
        {
          return false;
        }

        return brush.transformEdges(
                 map.worldBounds(), edgesToMove, transform, pref(Preferences::UVLock))
               | kdl::transform([&]() {
                   auto newPositions = brush.findClosestEdgePositions(
                     edgesToMove | std::views::transform([&](const auto& edge) {
                       return edge.transform(transform);
                     })
                     | kdl::ranges::to<std::vector>());
                   newEdgePositions = kdl::vec_concat(
                     std::move(newEdgePositions), std::move(newPositions));
                 })
               | kdl::if_error([&](auto e) {
                   map.logger().error() << "Could not move brush edges: " << e.msg;
                 })
               | kdl::is_success();
      },
      [](BezierPatch&) { return true; }));

  if (newNodes)
  {
    kdl::vec_sort_and_remove_duplicates(newEdgePositions);

    const auto commandName =
      kdl::str_plural(edgePositions.size(), "Move Brush Edge", "Move Brush Edges");
    auto transaction = Transaction{map, commandName};

    const auto changedLinkedGroups = collectContainingGroups(
      *newNodes | std::views::keys | kdl::ranges::to<std::vector>());

    const auto result = map.executeAndStore(std::make_unique<BrushEdgeCommand>(
      commandName,
      std::move(*newNodes),
      std::move(edgePositions),
      std::move(newEdgePositions)));

    if (!result)
    {
      transaction.cancel();
      return false;
    }

    setHasPendingChanges(changedLinkedGroups, true);
    return transaction.commit();
  }

  return false;
}

bool transformFaces(
  Map& map, std::vector<vm::polygon3d> facePositions, const vm::mat4x4d& transform)
{
  auto newFacePositions = std::vector<vm::polygon3d>{};
  const auto& selectedBrushes = map.selection().allBrushes();
  auto newNodes = applyToNodeContents(
    selectedBrushes,
    kdl::overload(
      [](Layer&) { return true; },
      [](Group&) { return true; },
      [](Entity&) { return true; },
      [&](Brush& brush) {
        const auto facesToMove =
          facePositions
          | std::views::filter([&](const auto& face) { return brush.hasFace(face); })
          | kdl::ranges::to<std::vector>();
        if (facesToMove.empty())
        {
          return true;
        }

        if (!brush.canTransformFaces(map.worldBounds(), facesToMove, transform))
        {
          return false;
        }

        return brush.transformFaces(
                 map.worldBounds(), facesToMove, transform, pref(Preferences::UVLock))
               | kdl::transform([&]() {
                   auto newPositions = brush.findClosestFacePositions(
                     facesToMove | std::views::transform([&](const auto& face) {
                       return face.transform(transform);
                     })
                     | kdl::ranges::to<std::vector>());
                   newFacePositions = kdl::vec_concat(
                     std::move(newFacePositions), std::move(newPositions));
                 })
               | kdl::if_error([&](auto e) {
                   map.logger().error() << "Could not move brush faces: " << e.msg;
                 })
               | kdl::is_success();
      },
      [](BezierPatch&) { return true; }));

  if (newNodes)
  {
    kdl::vec_sort_and_remove_duplicates(newFacePositions);

    const auto commandName =
      kdl::str_plural(facePositions.size(), "Move Brush Face", "Move Brush Faces");
    auto transaction = Transaction{map, commandName};

    auto changedLinkedGroups = collectContainingGroups(
      *newNodes | std::views::keys | kdl::ranges::to<std::vector>());

    const auto result = map.executeAndStore(std::make_unique<BrushFaceCommand>(
      commandName,
      std::move(*newNodes),
      std::move(facePositions),
      std::move(newFacePositions)));

    if (!result)
    {
      transaction.cancel();
      return false;
    }

    setHasPendingChanges(changedLinkedGroups, true);
    return transaction.commit();
  }

  return false;
}

bool addVertex(Map& map, const vm::vec3d& vertexPosition)
{
  const auto& selectedBrushes = map.selection().allBrushes();
  auto newNodes = applyToNodeContents(
    selectedBrushes,
    kdl::overload(
      [](Layer&) { return true; },
      [](Group&) { return true; },
      [](Entity&) { return true; },
      [&](Brush& brush) {
        if (!brush.canAddVertex(map.worldBounds(), vertexPosition))
        {
          return false;
        }

        return brush.addVertex(map.worldBounds(), vertexPosition)
               | kdl::if_error([&](auto e) {
                   map.logger().error() << "Could not add brush vertex: " << e.msg;
                 })
               | kdl::is_success();
      },
      [](BezierPatch&) { return true; }));

  if (newNodes)
  {
    const auto commandName = "Add Brush Vertex";
    auto transaction = Transaction{map, commandName};

    const auto changedLinkedGroups = collectContainingGroups(
      *newNodes | std::views::keys | kdl::ranges::to<std::vector>());

    const auto result = map.executeAndStore(std::make_unique<BrushVertexCommand>(
      commandName,
      std::move(*newNodes),
      std::vector<vm::vec3d>{},
      std::vector<vm::vec3d>{vertexPosition}));

    if (!result)
    {
      transaction.cancel();
      return false;
    }

    setHasPendingChanges(changedLinkedGroups, true);
    return transaction.commit();
  }

  return false;
}

bool removeVertices(
  Map& map, const std::string& commandName, std::vector<vm::vec3d> vertexPositions)
{
  const auto& selectedBrushes = map.selection().allBrushes();
  auto newNodes = applyToNodeContents(
    selectedBrushes,
    kdl::overload(
      [](Layer&) { return true; },
      [](Group&) { return true; },
      [](Entity&) { return true; },
      [&](Brush& brush) {
        const auto verticesToRemove = vertexPositions
                                      | std::views::filter([&](const auto& vertex) {
                                          return brush.hasVertex(vertex);
                                        })
                                      | kdl::ranges::to<std::vector>();
        if (verticesToRemove.empty())
        {
          return true;
        }

        if (!brush.canRemoveVertices(map.worldBounds(), verticesToRemove))
        {
          return false;
        }

        return brush.removeVertices(map.worldBounds(), verticesToRemove)
               | kdl::if_error([&](auto e) {
                   map.logger().error() << "Could not remove brush vertices: " << e.msg;
                 })
               | kdl::is_success();
      },
      [](BezierPatch&) { return true; }));

  if (newNodes)
  {
    auto transaction = Transaction{map, commandName};

    auto changedLinkedGroups = collectContainingGroups(
      *newNodes | std::views::keys | kdl::ranges::to<std::vector>());

    const auto result = map.executeAndStore(std::make_unique<BrushVertexCommand>(
      commandName,
      std::move(*newNodes),
      std::move(vertexPositions),
      std::vector<vm::vec3d>{}));

    if (!result)
    {
      transaction.cancel();
      return false;
    }

    setHasPendingChanges(changedLinkedGroups, true);
    return transaction.commit();
  }

  return false;
}

bool snapVertices(Map& map, const double snapTo)
{
  size_t succeededBrushCount = 0;
  size_t failedBrushCount = 0;

  const auto allSelectedBrushes = map.selection().allBrushes();
  const bool applyAndSwapSuccess = applyAndSwap(
    map,
    "Snap Brush Vertices",
    allSelectedBrushes,
    collectContainingGroups(kdl::vec_static_cast<Node*>(allSelectedBrushes)),
    kdl::overload(
      [](Layer&) { return true; },
      [](Group&) { return true; },
      [](Entity&) { return true; },
      [&](Brush& originalBrush) {
        if (originalBrush.canSnapVertices(map.worldBounds(), snapTo))
        {
          originalBrush.snapVertices(map.worldBounds(), snapTo, pref(Preferences::UVLock))
            | kdl::transform([&]() { succeededBrushCount += 1; })
            | kdl::transform_error([&](auto e) {
                map.logger().error() << "Could not snap vertices: " << e.msg;
                failedBrushCount += 1;
              });
        }
        else
        {
          failedBrushCount += 1;
        }
        return true;
      },
      [](BezierPatch&) { return true; }));

  if (!applyAndSwapSuccess)
  {
    return false;
  }
  if (succeededBrushCount > 0)
  {
    map.logger().info() << std::format(
      "Snapped vertices of {} {}",
      succeededBrushCount,
      kdl::str_plural(succeededBrushCount, "brush", "brushes"));
  }
  if (failedBrushCount > 0)
  {
    map.logger().info() << std::format(
      "Failed to snap vertices of {} {}",
      failedBrushCount,
      kdl::str_plural(failedBrushCount, "brush", "brushes"));
  }

  return true;
}

bool chamferEdges(
  Map& map,
  std::vector<vm::segment3d> edgePositions,
  const double distance,
  const size_t segments)
{
  if (edgePositions.empty() || distance <= 0.0 || segments == 0u)
  {
    return false;
  }

  kdl::vec_sort_and_remove_duplicates(edgePositions);

  bool didChamfer = false;
  const auto& selectedBrushes = map.selection().allBrushes();

  auto newNodes = applyToNodeContents(
    selectedBrushes,
    kdl::overload(
      [](Layer&) { return true; },
      [](Group&) { return true; },
      [](Entity&) { return true; },
      [&](Brush& brush) {
        const auto edgesToChamfer =
          edgePositions
          | std::views::filter([&](const auto& edge) { return brush.hasEdge(edge); })
          | kdl::ranges::to<std::vector>();
        if (edgesToChamfer.empty())
        {
          return true;
        }

        for (const auto& edge : edgesToChamfer)
        {
          if (!chamferBrushEdge(map, brush, edge, distance, segments, didChamfer))
          {
            return false;
          }
        }

        return true;
      },
      [](BezierPatch&) { return true; }));

  if (!newNodes || !didChamfer)
  {
    return false;
  }

  const auto commandName = kdl::str_plural(
    edgePositions.size(), "Chamfer Brush Edge", "Chamfer Brush Edges");
  auto changedLinkedGroups = collectContainingGroups(
    *newNodes | std::views::keys | kdl::ranges::to<std::vector>());

  return updateNodeContents(
    map, commandName, std::move(*newNodes), std::move(changedLinkedGroups));
}

bool csgConvexMerge(Map& map)
{
  const auto& selection = map.selection();
  const auto& selectedBrushes = selection.allBrushes();

  if (!selection.hasBrushFaces() && !selection.hasOnlyBrushes())
  {
    return false;
  }

  auto points = std::vector<vm::vec3d>{};

  if (selection.hasBrushFaces())
  {
    for (const auto& handle : selection.brushFaces)
    {
      for (const auto* vertex : handle.face().vertices())
      {
        points.push_back(vertex->position());
      }
    }
  }
  else if (selection.hasOnlyBrushes())
  {
    for (const auto* brushNode : selectedBrushes)
    {
      for (const auto* vertex : brushNode->brush().vertices())
      {
        points.push_back(vertex->position());
      }
    }
  }

  auto polyhedron = Polyhedron3{std::move(points)};
  if (!polyhedron.polyhedron() || !polyhedron.closed())
  {
    return false;
  }

  const auto builder = BrushBuilder{
    map.worldNode().mapFormat(),
    map.worldBounds(),
    map.gameInfo().gameConfig.faceAttribsConfig.defaults};
  return builder.createBrush(polyhedron, map.currentMaterialName())
         | kdl::transform([&](auto b) {
             auto attributeBrushes = std::vector<const Brush*>{};
             if (selection.hasBrushFaces())
             {
               auto faceBrushes =
                 selection.brushFaces
                 | std::views::transform([](const auto& handle) { return handle.node(); })
                 | kdl::ranges::to<std::vector>();
               faceBrushes = kdl::vec_sort_and_remove_duplicates(std::move(faceBrushes));
               attributeBrushes =
                 faceBrushes | std::views::transform([](const auto* brushNode) {
                   return &brushNode->brush();
                 })
                 | kdl::ranges::to<std::vector>();
             }
             else
             {
               attributeBrushes =
                 selectedBrushes | std::views::transform([](const auto* brushNode) {
                   return &brushNode->brush();
                 })
                 | kdl::ranges::to<std::vector>();
             }
             b.cloneFaceAttributesFrom(attributeBrushes);

             const auto toRemove = selection.hasBrushFaces()
                                     ? selection.nodes
                                     : kdl::vec_static_cast<Node*>(selectedBrushes);

             // We could be merging brushes that have different parents; use the parent
             // of the first brush.
             auto* parentNode = static_cast<Node*>(nullptr);
             if (!selectedBrushes.empty())
             {
               parentNode = selectedBrushes.front()->parent();
             }
             else if (!selection.brushFaces.empty())
             {
               parentNode = selection.brushFaces.front().node()->parent();
             }
             else
             {
               parentNode = parentForNodes(map);
             }

             auto* brushNode = new BrushNode{std::move(b)};

             auto transaction = Transaction{map, "CSG Convex Merge"};
             deselectAll(map);
             if (addNodes(map, {{parentNode, {brushNode}}}).empty())
             {
               transaction.cancel();
               return;
             }
             removeNodes(map, toRemove);
             selectNodes(map, {brushNode});
             transaction.commit();
           })
         | kdl::if_error(
           [&](auto e) { map.logger().error() << "Could not create brush: " << e.msg; })
         | kdl::is_success();
}

bool csgSubtract(Map& map)
{
  const auto subtrahendNodes = map.selection().allBrushes();
  if (subtrahendNodes.empty())
  {
    return false;
  }

  auto transaction = Transaction{map, "CSG Subtract"};
  // Select touching, but don't delete the subtrahends yet
  selectTouchingNodes(map, false);

  const auto minuendNodes = map.selection().allBrushes();
  const auto subtrahends = subtrahendNodes
                           | std::views::transform([](const auto* subtrahendNode) {
                               return &subtrahendNode->brush();
                             })
                           | kdl::ranges::to<std::vector>();

  auto toAdd = std::map<Node*, std::vector<Node*>>{};
  auto toRemove =
    std::vector<Node*>{std::begin(subtrahendNodes), std::end(subtrahendNodes)};

  return minuendNodes | std::views::transform([&](auto* minuendNode) {
           const auto& minuend = minuendNode->brush();
           auto currentSubtractionResults = minuend.subtract(
             map.worldNode().mapFormat(),
             map.worldBounds(),
             map.currentMaterialName(),
             subtrahends);

           return currentSubtractionResults
                  | std::views::filter([](const auto r) { return r | kdl::is_success(); })
                  | kdl::views::as_rvalue | kdl::fold
                  | kdl::transform([&](auto currentBrushes) {
                      if (!currentBrushes.empty())
                      {
                        auto resultNodes = currentBrushes | kdl::views::as_rvalue
                                           | std::views::transform([&](auto b) {
                                               return new BrushNode{std::move(b)};
                                             })
                                           | kdl::ranges::to<std::vector>();
                        auto& toAddForParent = toAdd[minuendNode->parent()];
                        toAddForParent = kdl::vec_concat(
                          std::move(toAddForParent), std::move(resultNodes));
                      }

                      toRemove.push_back(minuendNode);
                    });
         })
         | kdl::fold | kdl::transform([&]() {
             deselectAll(map);
             const auto added = addNodes(map, toAdd);
             removeNodes(map, toRemove);
             selectNodes(map, added);

             return transaction.commit();
           })
         | kdl::transform_error([&](const auto& e) {
             map.logger().error() << "Could not subtract brushes: " << e;
             transaction.cancel();
             return false;
           })
         | kdl::value();
}

bool csgIntersect(Map& map)
{
  const auto brushes = map.selection().allBrushes();
  if (brushes.size() < 2u)
  {
    return false;
  }

  auto intersection = brushes.front()->brush();

  bool valid = true;
  for (auto it = std::next(std::begin(brushes)), end = std::end(brushes);
       it != end && valid;
       ++it)
  {
    auto* brushNode = *it;
    const auto& brush = brushNode->brush();
    valid = intersection.intersect(map.worldBounds(), brush) | kdl::if_error([&](auto e) {
              map.logger().error() << "Could not intersect brushes: " << e.msg;
            })
            | kdl::is_success();
  }

  const auto toRemove = std::vector<Node*>{std::begin(brushes), std::end(brushes)};

  auto transaction = Transaction{map, "CSG Intersect"};
  deselectNodes(map, toRemove);

  if (valid)
  {
    auto* intersectionNode = new BrushNode{std::move(intersection)};
    if (addNodes(map, {{parentForNodes(map, toRemove), {intersectionNode}}}).empty())
    {
      transaction.cancel();
      return false;
    }
    removeNodes(map, toRemove);
    selectNodes(map, {intersectionNode});
  }
  else
  {
    removeNodes(map, toRemove);
  }

  return transaction.commit();
}

bool csgHollow(Map& map)
{
  const auto brushNodes = map.selection().allBrushes();
  if (brushNodes.empty())
  {
    return false;
  }

  bool didHollowAnything = false;
  auto toAdd = std::map<Node*, std::vector<Node*>>{};
  auto toRemove = std::vector<Node*>{};

  for (auto* brushNode : brushNodes)
  {
    const auto& originalBrush = brushNode->brush();

    auto shrunkenBrush = originalBrush;
    shrunkenBrush.expand(map.worldBounds(), -double(map.grid().actualSize()), true)
      | kdl::and_then([&]() {
          didHollowAnything = true;

          return originalBrush.subtract(
                   map.worldNode().mapFormat(),
                   map.worldBounds(),
                   map.currentMaterialName(),
                   shrunkenBrush)
                 | kdl::fold | kdl::transform([&](auto fragments) {
                     auto fragmentNodes =
                       fragments | kdl::views::as_rvalue
                       | std::views::transform([](auto&& b) {
                           return new BrushNode{std::forward<decltype(b)>(b)};
                         })
                       | kdl::ranges::to<std::vector>();

                     auto& toAddForParent = toAdd[brushNode->parent()];
                     toAddForParent =
                       kdl::vec_concat(std::move(toAddForParent), fragmentNodes);
                     toRemove.push_back(brushNode);
                   });
        })
      | kdl::transform_error(
        [&](const auto& e) { map.logger().error() << "Could not hollow brush: " << e; });
  }

  if (!didHollowAnything)
  {
    return false;
  }

  auto transaction = Transaction{map, "CSG Hollow"};
  deselectAll(map);
  const auto added = addNodes(map, toAdd);
  if (added.empty())
  {
    transaction.cancel();
    return false;
  }
  removeNodes(map, toRemove);
  selectNodes(map, added);

  return transaction.commit();
}

bool supportsPatchPrimitives(const Map& map)
{
  return supportsPatchPrimitives(map.worldNode().mapFormat());
}

bool createPatchPrefab(
  Map& map,
  const PatchPrefabType type,
  const vm::axis::type axis,
  size_t width,
  size_t height,
  const bool redisperse)
{
  if (!supportsPatchPrimitives(map))
  {
    return false;
  }

  if (type != PatchPrefabType::ExactCylinder && type != PatchPrefabType::ExactCone
      && type != PatchPrefabType::ExactSphere && type != PatchPrefabType::Plane)
  {
    width = 3u;
    height = 3u;
  }

  const auto materialName = map.currentMaterialName();
  auto patch = makePatchPrefab(
    patchCreationBounds(map),
    type,
    axis,
    width,
    height,
    redisperse,
    materialName);
  if (!patch)
  {
    return false;
  }

  auto* node = new PatchNode{std::move(*patch)};
  auto* parent = patchCreationParent(map);
  if (parent == nullptr || !parent->canAddChild(node))
  {
    delete node;
    return false;
  }

  auto commandName = std::string{"Create Patch"};
  switch (type)
  {
  case PatchPrefabType::Plane:
    commandName = "Create Patch Plane";
    break;
  case PatchPrefabType::Bevel:
    commandName = "Create Patch Bevel";
    break;
  case PatchPrefabType::EndCap:
    commandName = "Create Patch End Cap";
    break;
  case PatchPrefabType::Cylinder:
    commandName = "Create Patch Cylinder";
    break;
  case PatchPrefabType::SquareCylinder:
    commandName = "Create Patch Square Cylinder";
    break;
  case PatchPrefabType::Cone:
    commandName = "Create Patch Cone";
    break;
  case PatchPrefabType::Sphere:
    commandName = "Create Patch Sphere";
    break;
  case PatchPrefabType::ExactCylinder:
    commandName = "Create Patch Exact Cylinder";
    break;
  case PatchPrefabType::ExactSphere:
    commandName = "Create Patch Exact Sphere";
    break;
  case PatchPrefabType::ExactCone:
    commandName = "Create Patch Exact Cone";
    break;
  }

  auto transaction = Transaction{map, commandName};
  deselectAll(map);
  const auto added = addNodes(map, {{parent, {node}}});
  if (added.empty())
  {
    transaction.cancel();
    return false;
  }
  selectNodes(map, added);
  return transaction.commit();
}

bool capSelectedPatches(Map& map, const PatchCapType type)
{
  const auto& selection = map.selection();
  const auto& selectedPatches = selection.allPatches();
  if (selectedPatches.empty())
  {
    return false;
  }

  auto toAdd = std::map<Node*, std::vector<Node*>>{};
  auto created = size_t{0u};
  const auto defaultMaterialName = map.currentMaterialName();

  for (const auto* patchNode : selectedPatches)
  {
    const auto& patch = patchNode->patch();
    if (!patchWidthCompatibleWithCap(type, patch.pointColumnCount()))
    {
      map.logger().error()
        << "Cannot cap patch: incompatible width " << patch.pointColumnCount();
      continue;
    }

    const auto materialName =
      defaultMaterialName.empty() ? patch.materialName() : defaultMaterialName;
    const auto edges =
      std::array<CapEdge, 2>{CapEdge::FirstRow, CapEdge::LastRow};
    for (const auto edge : edges)
    {
      const auto seam = patchEdge(patch, edge);
      auto cap = makeCapPatchFromSeam(
        seam,
        type,
        materialName,
        patch.surfaceContents(),
        patch.surfaceFlags(),
        patch.surfaceValue());
      if (!cap)
      {
        continue;
      }

      toAdd[patchNode->parent()].push_back(new PatchNode{std::move(*cap)});
      ++created;
    }
  }

  if (created == 0u)
  {
    return false;
  }

  auto transaction = Transaction{map, "Cap Patches"};
  const auto added = addNodes(map, toAdd);
  if (added.empty())
  {
    transaction.cancel();
    return false;
  }
  selectNodes(map, added);
  return transaction.commit();
}

bool deformPatches(Map& map, const int deform, const vm::axis::type axis)
{
  if (deform == 0)
  {
    return false;
  }

  auto randomEngine = std::mt19937{std::random_device{}()};
  auto distribution = std::uniform_real_distribution<double>{0.0, 1.0};

  return applyPatchOperation(
    map,
    "Deform Patches",
    [&](BezierPatch& patch) {
      for (size_t row = 0u; row < patch.pointRowCount(); ++row)
      {
        for (size_t col = 0u; col < patch.pointColumnCount(); ++col)
        {
          auto point = patch.controlPoint(row, col);
          point[axis] += static_cast<double>(deform) * distribution(randomEngine);
          patch.setControlPoint(row, col, point);
        }
      }
      if (patch.hasControlNormals())
      {
        patch.setControlNormals({});
      }
      return true;
    });
}

bool thickenPatches(
  Map& map,
  const double thickness,
  const bool createSideWalls,
  const PatchThickenAxis axis)
{
  const auto& selection = map.selection();
  const auto& selectedPatches = selection.allPatches();
  if (selectedPatches.empty())
  {
    return false;
  }

  auto toAdd = std::map<Node*, std::vector<Node*>>{};
  auto created = size_t{0u};

  for (const auto* patchNode : selectedPatches)
  {
    const auto& sourcePatch = patchNode->patch();
    const auto materialName =
      sourcePatch.materialName().empty() ? map.currentMaterialName() : sourcePatch.materialName();

    auto oppositePatch = makeThickenedOppositePatch(sourcePatch, thickness, axis);
    if (!hasNonDegenerateDimensions(oppositePatch))
    {
      continue;
    }

    toAdd[patchNode->parent()].push_back(new PatchNode{oppositePatch});
    ++created;

    if (createSideWalls)
    {
      const auto rowsClosed = arePatchEdgeRowsClosed(sourcePatch);
      const auto colsClosed = arePatchEdgeColumnsClosed(sourcePatch);

      if (!rowsClosed)
      {
        for (const auto edgeIndex : {0, 1})
        {
          if (auto wall =
                makeWallPatch(sourcePatch, oppositePatch, edgeIndex, materialName))
          {
            toAdd[patchNode->parent()].push_back(new PatchNode{std::move(*wall)});
            ++created;
          }
        }
      }

      if (!colsClosed)
      {
        for (const auto edgeIndex : {2, 3})
        {
          if (auto wall =
                makeWallPatch(sourcePatch, oppositePatch, edgeIndex, materialName))
          {
            toAdd[patchNode->parent()].push_back(new PatchNode{std::move(*wall)});
            ++created;
          }
        }
      }
    }
  }

  if (created == 0u)
  {
    return false;
  }

  auto transaction = Transaction{map, "Thicken Patches"};
  const auto added = addNodes(map, toAdd);
  if (added.empty())
  {
    transaction.cancel();
    return false;
  }
  selectNodes(map, added);
  return transaction.commit();
}

bool convertPatchesToConvexBrushes(Map& map)
{
  const auto& selection = map.selection();
  const auto& patchNodes = selection.allPatches();
  if (patchNodes.empty())
  {
    return false;
  }

  const auto builder = BrushBuilder{
    map.worldNode().mapFormat(),
    map.worldBounds(),
    map.gameInfo().gameConfig.faceAttribsConfig.defaults};

  auto toAdd = std::map<Node*, std::vector<Node*>>{};
  auto toRemove = std::vector<Node*>{};
  size_t convertedCount = 0u;

  for (auto* patchNode : patchNodes)
  {
    auto points = std::vector<vm::vec3d>{};
    points.reserve(patchNode->grid().points.size());
    for (const auto& point : patchNode->grid().points)
    {
      points.push_back(point.position);
    }
    kdl::vec_sort_and_remove_duplicates(points);

    const auto patchMaterialName = patchNode->patch().materialName().empty()
                                     ? map.currentMaterialName()
                                     : patchNode->patch().materialName();

    builder.createBrush(points, patchMaterialName)
      | kdl::and_then([&](auto brush) {
          return applyPatchUvToBrushFaces(
            map, *patchNode, brush, patchMaterialName, std::string{"common/caulk"});
        })
      | kdl::transform([&](auto brush) {
          auto* brushNode = new BrushNode{std::move(brush)};
          toAdd[patchNode->parent()].push_back(brushNode);
          toRemove.push_back(patchNode);
          convertedCount += 1u;
        })
      | kdl::transform_error([&](auto e) {
          map.logger().error() << "Could not convert patch to brush: " << e.msg;
        });
  }

  if (convertedCount == 0u)
  {
    return false;
  }

  auto transaction = Transaction{map, "Convert Patches to Brushes"};
  deselectAll(map);
  const auto added = addNodes(map, toAdd);
  if (added.empty())
  {
    transaction.cancel();
    return false;
  }
  removeNodes(map, toRemove);
  selectNodes(map, added);

  return transaction.commit();
}

bool insertPatchColumns(Map& map, const bool first)
{
  const auto selectedHandles = map.vertexHandles().selectedHandles();
  return applyPatchOperation(
    map,
    first ? "Insert First Patch Columns" : "Insert Last Patch Columns",
    [&](BezierPatch& patch) {
      patch.insertRemove(
        true,
        true,
        first,
        selectedPatchRowOrColumn(patch, selectedHandles, true));
      return true;
    });
}

bool insertPatchRows(Map& map, const bool first)
{
  const auto selectedHandles = map.vertexHandles().selectedHandles();
  return applyPatchOperation(
    map,
    first ? "Insert First Patch Rows" : "Insert Last Patch Rows",
    [&](BezierPatch& patch) {
      patch.insertRemove(
        true,
        false,
        first,
        selectedPatchRowOrColumn(patch, selectedHandles, false));
      return true;
    });
}

bool deletePatchColumns(Map& map, const bool first)
{
  const auto selectedHandles = map.vertexHandles().selectedHandles();
  return applyPatchOperation(
    map,
    first ? "Delete First Patch Columns" : "Delete Last Patch Columns",
    [&](BezierPatch& patch) {
      if (patch.pointColumnCount() <= 3u)
      {
        return false;
      }

      patch.insertRemove(
        false,
        true,
        first,
        selectedPatchRowOrColumn(patch, selectedHandles, true));
      return true;
    });
}

bool deletePatchRows(Map& map, const bool first)
{
  const auto selectedHandles = map.vertexHandles().selectedHandles();
  return applyPatchOperation(
    map,
    first ? "Delete First Patch Rows" : "Delete Last Patch Rows",
    [&](BezierPatch& patch) {
      if (patch.pointRowCount() <= 3u)
      {
        return false;
      }

      patch.insertRemove(
        false,
        false,
        first,
        selectedPatchRowOrColumn(patch, selectedHandles, false));
      return true;
    });
}

bool invertPatchMatrix(Map& map)
{
  return applyPatchOperation(map, "Invert Patch Matrix", [](BezierPatch& patch) {
    patch.invertMatrix();
    return true;
  });
}

bool transposePatchMatrix(Map& map)
{
  return applyPatchOperation(map, "Transpose Patch Matrix", [](BezierPatch& patch) {
    patch.transposeMatrix();
    return true;
  });
}

bool redispersePatchRows(Map& map)
{
  return applyPatchOperation(map, "Redisperse Patch Rows", [](BezierPatch& patch) {
    patch.redisperse(BezierPatch::MatrixMajor::Row);
    return true;
  });
}

bool redispersePatchColumns(Map& map)
{
  return applyPatchOperation(map, "Redisperse Patch Columns", [](BezierPatch& patch) {
    patch.redisperse(BezierPatch::MatrixMajor::Column);
    return true;
  });
}

bool smoothPatchRows(Map& map)
{
  return applyPatchOperation(map, "Smooth Patch Rows", [](BezierPatch& patch) {
    patch.smooth(BezierPatch::MatrixMajor::Row);
    return true;
  });
}

bool smoothPatchColumns(Map& map)
{
  return applyPatchOperation(map, "Smooth Patch Columns", [](BezierPatch& patch) {
    patch.smooth(BezierPatch::MatrixMajor::Column);
    return true;
  });
}

bool resetPatchTexture(Map& map)
{
  return applyPatchOperation(map, "Reset Patch Texture", [](BezierPatch& patch) {
    const auto [textureWidth, textureHeight] = patchTextureSize(patch);
    patch.capTexture(textureWidth, textureHeight);
    return true;
  });
}

bool naturalizePatchTexture(Map& map)
{
  return applyPatchOperation(map, "Naturalize Patch Texture", [](BezierPatch& patch) {
    const auto [textureWidth, textureHeight] = patchTextureSize(patch);
    patch.naturalTexture(textureWidth, textureHeight);
    return true;
  });
}

bool flipPatchTextureHorizontally(Map& map)
{
  return applyPatchOperation(map, "Flip Patch Texture Horizontally", [](BezierPatch& patch) {
    patch.flipTexture(0u);
    return true;
  });
}

bool flipPatchTextureVertically(Map& map)
{
  return applyPatchOperation(map, "Flip Patch Texture Vertically", [](BezierPatch& patch) {
    patch.flipTexture(1u);
    return true;
  });
}

bool extrudeBrushes(
  Map& map, const std::vector<vm::polygon3d>& faces, const vm::vec3d& delta)
{
  const auto nodes = map.selection().nodes;
  return applyAndSwap(
    map,
    "Resize Brushes",
    nodes,
    collectContainingGroups(nodes),
    kdl::overload(
      [](Layer&) { return true; },
      [](Group&) { return true; },
      [](Entity&) { return true; },
      [&](Brush& brush) {
        const auto faceIndex = brush.findFace(faces);
        if (!faceIndex)
        {
          // we allow resizing only some of the brushes
          return true;
        }

        return brush.moveBoundary(
                 map.worldBounds(), *faceIndex, delta, pref(Preferences::AlignmentLock))
               | kdl::transform(
                 [&]() { return map.worldBounds().contains(brush.bounds()); })
               | kdl::transform_error([&](auto e) {
                   map.logger().error() << "Could not resize brush: " << e.msg;
                   return false;
                 })
               | kdl::value();
      },
      [](BezierPatch&) { return true; }));
}

} // namespace tb::mdl
