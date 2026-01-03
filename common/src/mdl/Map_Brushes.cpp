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

#include "mdl/Map_Brushes.h"

#include "Logger.h"
#include "mdl/ApplyAndSwap.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushFaceHandle.h"
#include "mdl/BrushNode.h"
#include "mdl/GameConfig.h"
#include "mdl/GameInfo.h"
#include "mdl/Material.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Transaction.h"
#include "mdl/UVCoordSystem.h"
#include "mdl/UpdateBrushFaceAttributes.h"
#include "mdl/WorldNode.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>

namespace tb::mdl
{

bool createBrush(Map& map, const std::vector<vm::vec3d>& points)
{
  const auto builder = BrushBuilder{
    map.worldNode().mapFormat(),
    map.worldBounds(),
    map.gameInfo().gameConfig.faceAttribsConfig.defaults};

  return builder.createBrush(points, map.currentMaterialName())
         | kdl::and_then([&](auto b) -> Result<void> {
             auto* brushNode = new BrushNode{std::move(b)};

             auto transaction = Transaction{map, "Create Brush"};
             deselectAll(map);
             if (addNodes(map, {{parentForNodes(map), {brushNode}}}).empty())
             {
               transaction.cancel();
               return Error{"Could not add brush to document"};
             }
             selectNodes(map, {brushNode});
             if (!transaction.commit())
             {
               return Error{"Could not add brush to document"};
             }

             return kdl::void_success;
           })
         | kdl::if_error(
           [&](auto e) { map.logger().error() << "Could not create brush: " << e.msg; })
         | kdl::is_success();
}

bool setBrushFaceAttributes(Map& map, const UpdateBrushFaceAttributes& update)
{
  return applyAndSwap(
    map, "Change Face Attributes", map.selection().allBrushFaces(), [&](auto& brushFace) {
      evaluate(update, brushFace);
      return true;
    });
}

bool copyUV(
  Map& map,
  const UVCoordSystemSnapshot& coordSystemSnapshot,
  const BrushFaceAttributes& attribs,
  const vm::plane3d& sourceFacePlane,
  const WrapStyle wrapStyle)
{
  return applyAndSwap(
    map, "Copy UV Alignment", map.selection().brushFaces, [&](auto& face) {
      face.copyUVCoordSystemFromFace(
        coordSystemSnapshot, attribs, sourceFacePlane, wrapStyle);
      return true;
    });
}

bool translateUV(
  Map& map,
  const vm::vec3f& cameraUp,
  const vm::vec3f& cameraRight,
  const vm::vec2f& delta)
{
  return applyAndSwap(map, "Translate UV", map.selection().brushFaces, [&](auto& face) {
    face.moveUV(vm::vec3d{cameraUp}, vm::vec3d{cameraRight}, delta);
    return true;
  });
}

bool rotateUV(Map& map, const float angle)
{
  return applyAndSwap(map, "Rotate UV", map.selection().brushFaces, [&](auto& face) {
    face.rotateUV(angle);
    return true;
  });
}

bool shearUV(Map& map, const vm::vec2f& factors)
{
  return applyAndSwap(map, "Shear UV", map.selection().brushFaces, [&](auto& face) {
    face.shearUV(factors);
    return true;
  });
}

bool flipUV(
  Map& map,
  const vm::vec3f& cameraUp,
  const vm::vec3f& cameraRight,
  const vm::direction cameraRelativeFlipDirection)
{
  const bool isHFlip =
    (cameraRelativeFlipDirection == vm::direction::left
     || cameraRelativeFlipDirection == vm::direction::right);
  return applyAndSwap(
    map,
    isHFlip ? "Flip UV Horizontally" : "Flip UV Vertically",
    map.selection().brushFaces,
    [&](auto& face) {
      face.flipUV(
        vm::vec3d{cameraUp}, vm::vec3d{cameraRight}, cameraRelativeFlipDirection);
      return true;
    });
}

namespace
{
constexpr double EdgeLengthEpsilon = 1e-6;
constexpr double AlignAngleEpsilonDeg = 1.0;
constexpr double AlignOffsetEpsilon = 1e-3;
constexpr double AlignScaleEpsilon = 1e-3;

struct AlignFrame
{
  vm::vec3d right;
  vm::vec3d down;
};

struct EdgeCandidate
{
  vm::vec3d left;
  vm::vec3d right;
  vm::vec3d dir;
  double horizontalness;
  double downCoord;
  double length;
  std::size_t index;
};

struct AlignmentDiff
{
  double rotationDiff = 0.0;
  vm::vec2f offsetDelta = vm::vec2f{0.0f, 0.0f};
  vm::vec2f scaleDelta = vm::vec2f{0.0f, 0.0f};
};

bool isNearZero(const vm::vec3d& vec)
{
  return vm::length(vec) <= EdgeLengthEpsilon;
}

vm::vec3d projectOntoPlane(const vm::vec3d& vec, const vm::vec3d& normal)
{
  return vec - normal * vm::dot(vec, normal);
}

AlignFrame computeAlignFrame(
  const vm::vec3d& normal, const vm::vec3f& cameraUp, const vm::vec3f& cameraRight)
{
  const auto worldDown = vm::vec3d{0.0, 0.0, -1.0};
  auto down = projectOntoPlane(worldDown, normal);

  if (isNearZero(down))
  {
    down = projectOntoPlane(vm::vec3d{-cameraUp}, normal);
  }

  if (isNearZero(down))
  {
    const auto right = projectOntoPlane(vm::vec3d{cameraRight}, normal);
    if (!isNearZero(right))
    {
      const auto normalizedRight = vm::normalize(right);
      return {normalizedRight, vm::normalize(vm::cross(normal, normalizedRight))};
    }

    down = projectOntoPlane(vm::vec3d{0.0, -1.0, 0.0}, normal);
  }

  if (isNearZero(down))
  {
    down = vm::vec3d{0.0, -1.0, 0.0};
  }

  const auto normalizedDown = vm::normalize(down);
  const auto normalizedRight = vm::normalize(vm::cross(normalizedDown, normal));
  return {normalizedRight, normalizedDown};
}

std::vector<EdgeCandidate> collectEdgeCandidates(
  const BrushFace& face, const AlignFrame& frame)
{
  const auto vertices = face.vertexPositions();
  if (vertices.size() < 2u)
  {
    return {};
  }

  const auto worldUp = vm::vec3d{0.0, 0.0, 1.0};
  auto candidates = std::vector<EdgeCandidate>{};
  candidates.reserve(vertices.size());

  for (std::size_t i = 0; i < vertices.size(); ++i)
  {
    auto left = vertices[i];
    auto right = vertices[(i + 1u) % vertices.size()];
    auto delta = right - left;
    const auto length = vm::length(delta);
    if (length <= EdgeLengthEpsilon)
    {
      continue;
    }

    auto dir = delta / length;
    if (vm::dot(dir, frame.right) < 0.0)
    {
      std::swap(left, right);
      dir = -dir;
    }

    const auto horizontalness = std::abs(vm::dot(dir, worldUp));
    const auto downCoord = 0.5 * (vm::dot(left, frame.down) + vm::dot(right, frame.down));

    candidates.push_back(
      {left, right, dir, horizontalness, downCoord, length, i});
  }

  return candidates;
}

double signedAngleDeg(const vm::vec3d& from, const vm::vec3d& to, const vm::vec3d& normal)
{
  const auto cross = vm::cross(from, to);
  const auto sinValue = vm::dot(normal, cross);
  const auto cosValue = vm::dot(from, to);
  return vm::to_degrees(std::atan2(sinValue, cosValue));
}

vm::vec2f computeFitScale(
  const BrushFace& face,
  const vm::vec3d& uAxis,
  const vm::vec3d& vAxis,
  const vm::vec2i& repeats,
  const vm::vec2f& currentScale);

BrushFaceAttributes alignedAttributesForCandidate(
  const BrushFace& face,
  const EdgeCandidate& candidate,
  const vm::vec3d& currentUAxis,
  const vm::vec3d& normal,
  const TextureAlignOptions& options)
{
  auto attributes = face.attributes();
  const auto oldRotation = attributes.rotation();
  const auto rotationDelta =
    static_cast<float>(signedAngleDeg(currentUAxis, candidate.dir, normal));
  const auto newRotation =
    static_cast<float>(vm::normalize_degrees(oldRotation + rotationDelta));

  attributes.setRotation(newRotation);

  if (options.mode == TextureAlignMode::Fit)
  {
    const auto vAxis = vm::normalize(vm::cross(normal, candidate.dir));
    const auto fitScale =
      computeFitScale(face, candidate.dir, vAxis, options.repeats, attributes.scale());
    if (options.scaleU)
    {
      attributes.setXScale(fitScale.x());
    }
    if (options.scaleV)
    {
      attributes.setYScale(fitScale.y());
    }
  }

  if (options.mode != TextureAlignMode::Rotate)
  {
    auto offsetAttributes = attributes;
    offsetAttributes.setXOffset(0.0f);
    offsetAttributes.setYOffset(0.0f);

    auto coordSystem = face.uvCoordSystem().clone();
    coordSystem->setRotation(normal, oldRotation, newRotation);
    const auto uvCoords =
      coordSystem->uvCoords(candidate.left, offsetAttributes, face.textureSize());
    const auto texCoords = uvCoords * face.textureSize();
    const auto offset = offsetAttributes.modOffset(
      vm::vec2f{-texCoords.x(), -texCoords.y()}, face.textureSize());

    attributes.setXOffset(offset.x());
    attributes.setYOffset(offset.y());
  }

  return attributes;
}

AlignmentDiff alignmentDiffForCandidate(
  const BrushFace& face,
  const EdgeCandidate& candidate,
  const vm::vec3d& currentUAxis,
  const vm::vec3d& normal,
  const TextureAlignOptions& options)
{
  const auto& currentAttributes = face.attributes();
  const auto targetAttributes = alignedAttributesForCandidate(
    face, candidate, currentUAxis, normal, options);

  auto diff = AlignmentDiff{};
  diff.rotationDiff =
    std::abs(signedAngleDeg(currentUAxis, candidate.dir, normal));

  if (options.mode != TextureAlignMode::Rotate)
  {
    const auto textureSize = face.textureSize();
    const auto currentOffset =
      currentAttributes.modOffset(currentAttributes.offset(), textureSize);
    const auto targetOffset =
      currentAttributes.modOffset(targetAttributes.offset(), textureSize);
    diff.offsetDelta = targetOffset - currentOffset;
  }

  if (options.mode == TextureAlignMode::Fit)
  {
    if (options.scaleU)
    {
      diff.scaleDelta[0] =
        targetAttributes.xScale() - currentAttributes.xScale();
    }
    if (options.scaleV)
    {
      diff.scaleDelta[1] =
        targetAttributes.yScale() - currentAttributes.yScale();
    }
  }

  return diff;
}

bool isBetterMatch(const AlignmentDiff& lhs, const AlignmentDiff& rhs)
{
  if (lhs.rotationDiff != rhs.rotationDiff)
  {
    return lhs.rotationDiff < rhs.rotationDiff;
  }

  const auto lhsOffset = vm::length(lhs.offsetDelta);
  const auto rhsOffset = vm::length(rhs.offsetDelta);
  if (lhsOffset != rhsOffset)
  {
    return lhsOffset < rhsOffset;
  }

  const auto lhsScale =
    std::abs(lhs.scaleDelta.x()) + std::abs(lhs.scaleDelta.y());
  const auto rhsScale =
    std::abs(rhs.scaleDelta.x()) + std::abs(rhs.scaleDelta.y());
  return lhsScale < rhsScale;
}

bool isAlignedMatch(const AlignmentDiff& diff, const TextureAlignOptions& options)
{
  if (diff.rotationDiff > AlignAngleEpsilonDeg)
  {
    return false;
  }

  if (options.mode != TextureAlignMode::Rotate)
  {
    if (std::abs(diff.offsetDelta.x()) > AlignOffsetEpsilon
        || std::abs(diff.offsetDelta.y()) > AlignOffsetEpsilon)
    {
      return false;
    }
  }

  if (options.mode == TextureAlignMode::Fit)
  {
    if (options.scaleU && std::abs(diff.scaleDelta.x()) > AlignScaleEpsilon)
    {
      return false;
    }
    if (options.scaleV && std::abs(diff.scaleDelta.y()) > AlignScaleEpsilon)
    {
      return false;
    }
  }

  return true;
}

std::optional<EdgeCandidate> pickEdgeCandidate(
  const BrushFace& face,
  const std::vector<EdgeCandidate>& candidates,
  const vm::vec3d& currentUAxis,
  const vm::vec3d& normal,
  const TextureAlignOptions& options)
{
  if (candidates.empty())
  {
    return std::nullopt;
  }

  auto order = std::vector<std::size_t>(candidates.size());
  std::iota(order.begin(), order.end(), 0u);
  std::sort(order.begin(), order.end(), [&](const std::size_t a, const std::size_t b) {
    const auto& lhs = candidates[a];
    const auto& rhs = candidates[b];
    if (lhs.horizontalness != rhs.horizontalness)
    {
      return lhs.horizontalness < rhs.horizontalness;
    }
    if (lhs.downCoord != rhs.downCoord)
    {
      return lhs.downCoord > rhs.downCoord;
    }
    if (lhs.length != rhs.length)
    {
      return lhs.length > rhs.length;
    }
    return lhs.index < rhs.index;
  });

  auto bestMatchIndex = order.front();
  auto bestMatchDiff = alignmentDiffForCandidate(
    face, candidates[bestMatchIndex], currentUAxis, normal, options);
  for (const auto candidateIndex : order)
  {
    const auto diff = alignmentDiffForCandidate(
      face, candidates[candidateIndex], currentUAxis, normal, options);
    if (isBetterMatch(diff, bestMatchDiff))
    {
      bestMatchDiff = diff;
      bestMatchIndex = candidateIndex;
    }
  }

  auto chosenIndex = bestMatchIndex;
  if (isAlignedMatch(bestMatchDiff, options))
  {
    const auto it = std::find(order.begin(), order.end(), bestMatchIndex);
    if (it != order.end())
    {
      auto next = std::next(it);
      if (next == order.end())
      {
        next = order.begin();
      }
      chosenIndex = *next;
    }
  }

  return candidates[chosenIndex];
}

struct AxisExtents
{
  double minU;
  double maxU;
  double minV;
  double maxV;
};

AxisExtents computeAxisExtents(
  const std::vector<vm::vec3d>& vertices,
  const vm::vec3d& uAxis,
  const vm::vec3d& vAxis)
{
  auto extents = AxisExtents{
    std::numeric_limits<double>::max(),
    std::numeric_limits<double>::lowest(),
    std::numeric_limits<double>::max(),
    std::numeric_limits<double>::lowest()};

  for (const auto& vertex : vertices)
  {
    const auto u = vm::dot(vertex, uAxis);
    const auto v = vm::dot(vertex, vAxis);
    extents.minU = std::min(extents.minU, u);
    extents.maxU = std::max(extents.maxU, u);
    extents.minV = std::min(extents.minV, v);
    extents.maxV = std::max(extents.maxV, v);
  }

  return extents;
}

vm::vec2f computeFitScale(
  const BrushFace& face,
  const vm::vec3d& uAxis,
  const vm::vec3d& vAxis,
  const vm::vec2i& repeats,
  const vm::vec2f& currentScale)
{
  const auto vertices = face.vertexPositions();
  if (vertices.empty())
  {
    return currentScale;
  }

  const auto extents = computeAxisExtents(vertices, uAxis, vAxis);
  const auto width = extents.maxU - extents.minU;
  const auto height = extents.maxV - extents.minV;

  const auto textureSize = face.textureSize();
  const auto repeatU = std::max(1, repeats.x());
  const auto repeatV = std::max(1, repeats.y());

  auto scale = currentScale;
  if (width > EdgeLengthEpsilon && textureSize.x() > EdgeLengthEpsilon)
  {
    const auto sign = currentScale.x() < 0.0f ? -1.0f : 1.0f;
    scale[0] =
      sign * static_cast<float>(width / (textureSize.x() * double(repeatU)));
  }
  if (height > EdgeLengthEpsilon && textureSize.y() > EdgeLengthEpsilon)
  {
    const auto sign = currentScale.y() < 0.0f ? -1.0f : 1.0f;
    scale[1] =
      sign * static_cast<float>(height / (textureSize.y() * double(repeatV)));
  }

  return scale;
}

bool alignFaceToEdge(
  BrushFace& face,
  const vm::vec3f& cameraUp,
  const vm::vec3f& cameraRight,
  const TextureAlignOptions& options)
{
  const auto normal = vm::normalize(face.normal());
  const auto currentUAxis = vm::normalize(face.uAxis());
  const auto frame = computeAlignFrame(normal, cameraUp, cameraRight);
  const auto candidates = collectEdgeCandidates(face, frame);

  const auto chosen =
    pickEdgeCandidate(face, candidates, currentUAxis, normal, options);
  if (!chosen)
  {
    return true;
  }

  face.setAttributes(alignedAttributesForCandidate(
    face, *chosen, currentUAxis, normal, options));
  return true;
}
} // namespace

bool alignTexturesToFaceEdge(
  Map& map,
  const vm::vec3f& cameraUp,
  const vm::vec3f& cameraRight,
  const TextureAlignOptions& options)
{
  const auto commandName =
    options.mode == TextureAlignMode::Fit    ? std::string{"Fit Texture To Edge"}
    : options.mode == TextureAlignMode::Rotate ? std::string{"Rotate Texture To Edge"}
                                               : std::string{"Align Texture To Edge"};
  return applyAndSwap(
    map, commandName, map.selection().brushFaces, [&](auto& face) {
      return alignFaceToEdge(face, cameraUp, cameraRight, options);
    });
}

namespace
{
struct HotspotPick
{
  vm::vec2f anchor;
  float score = 0.0f;
};

vm::vec2f alignHotspotAxis(
  const vm::vec2f& center,
  const vm::vec2f& hitTexCoord,
  const vm::vec2f& textureSize,
  const mdl::HotspotRect& rect)
{
  auto anchor = center;

  const auto alignAxis = [&](const float centerCoord, const float hitCoord, const float size,
                             const bool tile) {
    if (!tile || size <= 0.0f)
    {
      return centerCoord;
    }
    const auto steps = std::round((hitCoord - centerCoord) / size);
    return centerCoord + steps * size;
  };

  anchor[0] = alignAxis(center.x(), hitTexCoord.x(), textureSize.x(), rect.tileU);
  anchor[1] = alignAxis(center.y(), hitTexCoord.y(), textureSize.y(), rect.tileV);
  return anchor;
}

std::optional<HotspotPick> pickHotspot(
  const std::vector<mdl::HotspotRect>& hotspots,
  const vm::vec2f& hitTexCoord,
  const vm::vec2f& textureSize)
{
  std::optional<HotspotPick> best;

  for (const auto& rect : hotspots)
  {
    const auto center = rect.min + rect.size * 0.5f;
    const auto anchor = alignHotspotAxis(center, hitTexCoord, textureSize, rect);
    const auto delta = anchor - hitTexCoord;
    const auto weight = rect.weight > 0.0f ? rect.weight : 1.0f;
    const auto score = vm::dot(delta, delta) / weight;

    if (!best || score < best->score)
    {
      best = HotspotPick{anchor, score};
    }
  }

  return best;
}
} // namespace

bool applyHotspotTexturing(
  Map& map, const BrushFaceHandle& faceHandle, const vm::vec3d& hitPoint)
{
  return applyAndSwap(
    map,
    "Align Texture To Hotspot",
    std::vector<BrushFaceHandle>{faceHandle},
    [&](auto& face) {
      const auto* material = face.material();
      if (!material || !material->hasHotspots())
      {
        return true;
      }

      const auto textureSize = face.textureSize();
      const auto hitTexCoord = face.uvCoords(hitPoint) * textureSize;
      const auto pick = pickHotspot(material->hotspots(), hitTexCoord, textureSize);
      if (!pick)
      {
        return true;
      }

      auto attributes = face.attributes();
      const auto offsetDelta = pick->anchor - hitTexCoord;
      const auto newOffset = face.modOffset(attributes.offset() + offsetDelta);
      attributes.setOffset(newOffset);
      face.setAttributes(attributes);
      return true;
    });
}

} // namespace tb::mdl
