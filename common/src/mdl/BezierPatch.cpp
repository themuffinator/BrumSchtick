/*
 Copyright (C) 2021 Kristian Duske

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

#include "BezierPatch.h"

#include "mdl/Material.h"
#include "mdl/Texture.h"

#include "kd/const_overload.h"
#include "kd/contracts.h"
#include "kd/reflection_impl.h"

#include "vm/bbox_io.h" // IWYU pragma: keep
#include "vm/bezier_surface.h"
#include "vm/mat_ext.h"
#include "vm/vec_io.h" // IWYU pragma: keep

#include <algorithm>
#include <cmath>

namespace tb::mdl
{

kdl_reflect_impl(BezierPatch);

namespace
{
vm::bbox3d computeBounds(const std::vector<BezierPatch::Point>& points)
{
  auto builder = vm::bbox3d::builder{};
  for (const auto& point : points)
  {
    builder.add(point.xyz());
  }
  return builder.bounds();
}

struct AverageAxes
{
  vm::vec3d widthDir;
  vm::vec3d heightDir;
};

AverageAxes computeAverageAxes(const BezierPatch& patch)
{
  auto widthDir = vm::vec3d{0.0, 0.0, 0.0};
  auto heightDir = vm::vec3d{0.0, 0.0, 0.0};

  const auto width = patch.pointColumnCount();
  const auto height = patch.pointRowCount();

  for (size_t row = 0u; row < height; ++row)
  {
    widthDir =
      widthDir + patch.controlPoint(row, width - 1u).xyz() - patch.controlPoint(row, 0u).xyz();
  }
  for (size_t col = 0u; col < width; ++col)
  {
    heightDir = heightDir + patch.controlPoint(height - 1u, col).xyz()
                - patch.controlPoint(0u, col).xyz();
  }

  const auto nearZero = [](const vm::vec3d& dir) {
    return vm::squared_length(dir)
           <= vm::constants<double>::almost_zero() * vm::constants<double>::almost_zero();
  };

  if (nearZero(widthDir))
  {
    auto bestLength = 0.0;
    for (size_t row = 0u; row < height; ++row)
    {
      for (size_t col = 0u; col + 1u < width; ++col)
      {
        const auto dir =
          patch.controlPoint(row, col + 1u).xyz() - patch.controlPoint(row, col).xyz();
        const auto length = vm::length(dir);
        if (length > bestLength)
        {
          bestLength = length;
          widthDir = dir;
        }
      }
    }
  }

  if (nearZero(heightDir))
  {
    auto bestLength = 0.0;
    for (size_t col = 0u; col < width; ++col)
    {
      for (size_t row = 0u; row + 1u < height; ++row)
      {
        const auto dir =
          patch.controlPoint(row + 1u, col).xyz() - patch.controlPoint(row, col).xyz();
        const auto length = vm::length(dir);
        if (length > bestLength)
        {
          bestLength = length;
          heightDir = dir;
        }
      }
    }
  }

  if (nearZero(vm::cross(widthDir, heightDir)))
  {
    widthDir = vm::vec3d{1.0, 0.0, 0.0};
    heightDir = vm::vec3d{0.0, 1.0, 0.0};
  }

  return AverageAxes{widthDir, heightDir};
}
} // namespace

BezierPatch::BezierPatch(
  const size_t pointRowCount,
  const size_t pointColumnCount,
  std::vector<Point> controlPoints,
  std::string materialName,
  const int surfaceContents,
  const int surfaceFlags,
  const float surfaceValue,
  std::vector<Normal> controlNormals)
  : m_pointRowCount{pointRowCount}
  , m_pointColumnCount{pointColumnCount}
  , m_controlPoints{std::move(controlPoints)}
  , m_controlNormals{std::move(controlNormals)}
  , m_bounds(computeBounds(m_controlPoints))
  , m_materialName{std::move(materialName)}
  , m_surfaceContents{surfaceContents}
  , m_surfaceFlags{surfaceFlags}
  , m_surfaceValue{surfaceValue}
{
  contract_pre(m_pointRowCount > 2 && m_pointColumnCount > 2);
  contract_pre(m_pointRowCount % 2 == 1 && m_pointColumnCount % 2 == 1);
  contract_pre(m_controlPoints.size() == m_pointRowCount * m_pointColumnCount);
  contract_pre(
    m_controlNormals.empty()
    || m_controlNormals.size() == m_pointRowCount * m_pointColumnCount);
}

BezierPatch::~BezierPatch() = default;

BezierPatch::BezierPatch(const BezierPatch& other) = default;
BezierPatch::BezierPatch(BezierPatch&& other) noexcept = default;

BezierPatch& BezierPatch::operator=(const BezierPatch& other) = default;
BezierPatch& BezierPatch::operator=(BezierPatch&& other) noexcept = default;

size_t BezierPatch::pointRowCount() const
{
  return m_pointRowCount;
}

size_t BezierPatch::pointColumnCount() const
{
  return m_pointColumnCount;
}

size_t BezierPatch::quadRowCount() const
{
  return m_pointRowCount - 1u;
}

size_t BezierPatch::quadColumnCount() const
{
  return m_pointColumnCount - 1u;
}

size_t BezierPatch::surfaceRowCount() const
{
  return quadRowCount() / 2u;
}

size_t BezierPatch::surfaceColumnCount() const
{
  return quadColumnCount() / 2u;
}

const std::vector<BezierPatch::Point>& BezierPatch::controlPoints() const
{
  return m_controlPoints;
}

const std::vector<BezierPatch::Normal>& BezierPatch::controlNormals() const
{
  return m_controlNormals;
}

bool BezierPatch::hasControlNormals() const
{
  return !m_controlNormals.empty();
}

BezierPatch::Point& BezierPatch::controlPoint(const size_t row, const size_t col)
{
  return KDL_CONST_OVERLOAD(controlPoint(row, col));
}

const BezierPatch::Point& BezierPatch::controlPoint(
  const size_t row, const size_t col) const
{
  contract_pre(row < m_pointRowCount);
  contract_pre(col < m_pointColumnCount);

  return m_controlPoints[row * m_pointColumnCount + col];
}

BezierPatch::Normal& BezierPatch::controlNormal(const size_t row, const size_t col)
{
  return KDL_CONST_OVERLOAD(controlNormal(row, col));
}

const BezierPatch::Normal& BezierPatch::controlNormal(
  const size_t row, const size_t col) const
{
  contract_pre(!m_controlNormals.empty());
  contract_pre(row < m_pointRowCount);
  contract_pre(col < m_pointColumnCount);

  return m_controlNormals[row * m_pointColumnCount + col];
}

void BezierPatch::setControlNormals(std::vector<Normal> controlNormals)
{
  contract_pre(
    controlNormals.empty() || controlNormals.size() == m_controlPoints.size());
  m_controlNormals = std::move(controlNormals);
}

void BezierPatch::setControlPoint(const size_t row, const size_t col, Point controlPoint)
{
  contract_pre(row < m_pointRowCount);
  contract_pre(col < m_pointColumnCount);

  m_controlPoints[row * m_pointColumnCount + col] = std::move(controlPoint);
  m_bounds = computeBounds(m_controlPoints);
}

const vm::bbox3d& BezierPatch::bounds() const
{
  return m_bounds;
}

const std::string& BezierPatch::materialName() const
{
  return m_materialName;
}

void BezierPatch::setMaterialName(std::string materialName)
{
  m_materialName = std::move(materialName);
}

int BezierPatch::surfaceContents() const
{
  return m_surfaceContents;
}

int BezierPatch::surfaceFlags() const
{
  return m_surfaceFlags;
}

float BezierPatch::surfaceValue() const
{
  return m_surfaceValue;
}

void BezierPatch::setSurfaceAttributes(
  const int surfaceContents, const int surfaceFlags, const float surfaceValue)
{
  m_surfaceContents = surfaceContents;
  m_surfaceFlags = surfaceFlags;
  m_surfaceValue = surfaceValue;
}

const Material* BezierPatch::material() const
{
  return m_materialReference.get();
}

bool BezierPatch::setMaterial(Material* material)
{
  if (material == this->material())
  {
    return false;
  }

  m_materialReference = AssetReference{material};
  return true;
}

void BezierPatch::transform(const vm::mat4x4d& transformation)
{
  auto builder = vm::bbox3d::builder{};
  for (auto& controlPoint : m_controlPoints)
  {
    controlPoint =
      Point{transformation * controlPoint.xyz(), controlPoint[3], controlPoint[4]};
    builder.add(controlPoint.xyz());
  }
  m_bounds = builder.bounds();

  if (!m_controlNormals.empty())
  {
    const auto linearTransform = vm::strip_translation(transformation);
    const auto inverted = vm::invert(linearTransform);
    const auto normalTransform =
      inverted ? vm::transpose(*inverted) : linearTransform;
    const auto epsilon = vm::constants<double>::almost_zero();

    for (auto& controlNormal : m_controlNormals)
    {
      if (!vm::is_zero(controlNormal, epsilon))
      {
        controlNormal = vm::normalize(normalTransform * controlNormal);
      }
    }
  }

  using std::swap;

  if (!vm::is_orientation_preserving_transform(transformation))
  {
    // reverse the control points along the u axis so that it's not inside out
    // see https://github.com/TrenchBroom/TrenchBroom/issues/4842
    for (size_t c = 0; c < m_pointColumnCount / 2; ++c)
    {
      const auto d = m_pointColumnCount - c - 1;
      for (size_t r = 0; r < m_pointRowCount; ++r)
      {
        swap(controlPoint(r, c), controlPoint(r, d));
        if (!m_controlNormals.empty())
        {
          swap(controlNormal(r, c), controlNormal(r, d));
        }
      }
    }
  }
}

void BezierPatch::invertMatrix()
{
  using std::swap;

  for (size_t row = 0u; row < m_pointRowCount / 2u; ++row)
  {
    const auto otherRow = m_pointRowCount - row - 1u;
    for (size_t col = 0u; col < m_pointColumnCount; ++col)
    {
      swap(controlPoint(row, col), controlPoint(otherRow, col));
      if (!m_controlNormals.empty())
      {
        swap(controlNormal(row, col), controlNormal(otherRow, col));
      }
    }
  }

  controlPointsChanged();
}

void BezierPatch::transposeMatrix()
{
  const auto oldRowCount = m_pointRowCount;
  const auto oldColumnCount = m_pointColumnCount;

  auto transposedPoints = std::vector<Point>{};
  transposedPoints.resize(oldRowCount * oldColumnCount);

  for (size_t row = 0u; row < oldRowCount; ++row)
  {
    for (size_t col = 0u; col < oldColumnCount; ++col)
    {
      transposedPoints[col * oldRowCount + row] = controlPoint(row, col);
    }
  }

  auto transposedNormals = std::vector<Normal>{};
  if (!m_controlNormals.empty())
  {
    transposedNormals.resize(oldRowCount * oldColumnCount);
    for (size_t row = 0u; row < oldRowCount; ++row)
    {
      for (size_t col = 0u; col < oldColumnCount; ++col)
      {
        transposedNormals[col * oldRowCount + row] = controlNormal(row, col);
      }
    }
  }

  m_pointRowCount = oldColumnCount;
  m_pointColumnCount = oldRowCount;
  m_controlPoints = std::move(transposedPoints);
  m_controlNormals = std::move(transposedNormals);

  controlPointsChanged();
}

void BezierPatch::redisperse(const MatrixMajor major)
{
  const auto width = major == MatrixMajor::Column ? (m_pointColumnCount - 1u) / 2u
                                                   : (m_pointRowCount - 1u) / 2u;
  const auto height = major == MatrixMajor::Column ? m_pointRowCount : m_pointColumnCount;

  const auto pointAt = [&](const size_t w, const size_t h) -> Point& {
    return major == MatrixMajor::Column ? controlPoint(w, h) : controlPoint(h, w);
  };

  for (size_t h = 0u; h < height; ++h)
  {
    for (size_t w = 0u; w < width; ++w)
    {
      auto& p1 = pointAt(2u * w, h);
      auto& p2 = pointAt(2u * w + 1u, h);
      auto& p3 = pointAt(2u * w + 2u, h);
      p2 = Point{(p1.xyz() + p3.xyz()) / 2.0, p2[3], p2[4]};
    }
  }

  // Control normals would be stale after changing control point positions.
  m_controlNormals.clear();
  controlPointsChanged();
}

void BezierPatch::smooth(const MatrixMajor major)
{
  const auto width = major == MatrixMajor::Column ? (m_pointColumnCount - 1u) / 2u
                                                   : (m_pointRowCount - 1u) / 2u;
  const auto height = major == MatrixMajor::Column ? m_pointRowCount : m_pointColumnCount;

  const auto pointAt = [&](const size_t w, const size_t h) -> Point& {
    return major == MatrixMajor::Column ? controlPoint(w, h) : controlPoint(h, w);
  };

  auto wrap = true;
  for (size_t h = 0u; h < height; ++h)
  {
    if (vm::squared_distance(pointAt(0u, h).xyz(), pointAt(2u * width, h).xyz()) > 1.0)
    {
      wrap = false;
      break;
    }
  }

  for (size_t h = 0u; h < height; ++h)
  {
    for (size_t w = 0u; w + 1u < width; ++w)
    {
      auto& p1 = pointAt(2u * w + 1u, h);
      auto& p2 = pointAt(2u * w + 2u, h);
      auto& p3 = pointAt(2u * w + 3u, h);
      p2 = Point{(p1.xyz() + p3.xyz()) / 2.0, p2[3], p2[4]};
    }

    if (wrap)
    {
      auto& p1 = pointAt(2u * width - 1u, h);
      auto& p2 = pointAt(0u, h);
      auto& p2b = pointAt(2u * width, h);
      auto& p3 = pointAt(1u, h);
      const auto wrapped = (p1.xyz() + p3.xyz()) / 2.0;
      p2 = Point{wrapped, p2[3], p2[4]};
      p2b = Point{wrapped, p2b[3], p2b[4]};
    }
  }

  // Control normals would be stale after changing control point positions.
  m_controlNormals.clear();
  controlPointsChanged();
}

void BezierPatch::insertRemove(
  const bool insert,
  const bool column,
  const bool first,
  const std::optional<size_t> selectedPosition)
{
  if (insert)
  {
    insertPoints(column ? MatrixMajor::Column : MatrixMajor::Row, first, selectedPosition);
  }
  else
  {
    removePoints(column ? MatrixMajor::Column : MatrixMajor::Row, first, selectedPosition);
  }
}

void BezierPatch::insertPoints(
  const MatrixMajor major, const bool first, const std::optional<size_t> selectedPosition)
{
  auto width = major == MatrixMajor::Row ? m_pointColumnCount : m_pointRowCount;
  auto height = major == MatrixMajor::Row ? m_pointRowCount : m_pointColumnCount;

  auto pos = selectedPosition.value_or(0u);
  const auto hasSelectedPosition = selectedPosition && *selectedPosition < height;
  if (!hasSelectedPosition)
  {
    pos = first ? 2u : height - 1u;
  }

  if (pos >= height)
  {
    pos = first ? 2u : height - 1u;
  }
  else if (pos == 0u)
  {
    pos = 2u;
  }
  else if (pos % 2u == 1u)
  {
    ++pos;
  }

  const auto oldColumnCount = m_pointColumnCount;
  const auto oldPoints = m_controlPoints;

  const auto newRowCount =
    major == MatrixMajor::Row ? m_pointRowCount + 2u : m_pointRowCount;
  const auto newColumnCount =
    major == MatrixMajor::Column ? m_pointColumnCount + 2u : m_pointColumnCount;

  auto newPoints = std::vector<Point>{};
  newPoints.resize(newRowCount * newColumnCount);

  const auto oldPointAt = [&](const size_t w, const size_t h) -> const Point& {
    return major == MatrixMajor::Row
             ? oldPoints[h * oldColumnCount + w]
             : oldPoints[w * oldColumnCount + h];
  };

  const auto newPointAt = [&](const size_t w, const size_t h) -> Point& {
    return major == MatrixMajor::Row
             ? newPoints[h * newColumnCount + w]
             : newPoints[w * newColumnCount + h];
  };

  for (size_t w = 0u; w < width; ++w)
  {
    auto h2 = 0u;
    for (size_t h = 0u; h < height; ++h, ++h2)
    {
      if (h == pos)
      {
        h2 += 2u;
      }
      newPointAt(w, h2) = oldPointAt(w, h);
    }

    const auto& p1 = oldPointAt(w, pos);
    auto& p2 = newPointAt(w, pos);
    auto& r2a = newPointAt(w, pos + 1u);
    auto& r2b = newPointAt(w, pos - 1u);
    const auto& c2a = oldPointAt(w, pos - 2u);
    const auto& c2b = oldPointAt(w, pos - 1u);

    newPointAt(w, pos + 2u) = p1;
    r2a = c2b;

    r2a = Point{
      (c2b.xyz() + p1.xyz()) / 2.0, (c2b[3] + p1[3]) * 0.5, (c2b[4] + p1[4]) * 0.5};
    r2b = Point{
      (c2a.xyz() + c2b.xyz()) / 2.0, (c2a[3] + c2b[3]) * 0.5, (c2a[4] + c2b[4]) * 0.5};
    p2 = Point{
      (r2a.xyz() + r2b.xyz()) / 2.0, (r2a[3] + r2b[3]) * 0.5, (r2a[4] + r2b[4]) * 0.5};
  }

  m_pointRowCount = newRowCount;
  m_pointColumnCount = newColumnCount;
  m_controlPoints = std::move(newPoints);
  m_controlNormals.clear();
  controlPointsChanged();
}

void BezierPatch::removePoints(
  const MatrixMajor major, const bool first, const std::optional<size_t> selectedPosition)
{
  auto width = major == MatrixMajor::Row ? m_pointColumnCount : m_pointRowCount;
  auto height = major == MatrixMajor::Row ? m_pointRowCount : m_pointColumnCount;

  auto pos = selectedPosition.value_or(0u);
  const auto hasSelectedPosition = selectedPosition && *selectedPosition < height;
  if (!hasSelectedPosition)
  {
    pos = first ? 2u : height - 3u;
  }

  if (pos >= height)
  {
    pos = first ? 2u : height - 3u;
  }
  else if (pos == 0u)
  {
    pos = 2u;
  }
  else if (pos > height - 3u)
  {
    pos = height - 3u;
  }
  else if (pos % 2u == 1u)
  {
    ++pos;
  }

  const auto oldColumnCount = m_pointColumnCount;
  const auto oldPoints = m_controlPoints;

  const auto newRowCount =
    major == MatrixMajor::Row ? m_pointRowCount - 2u : m_pointRowCount;
  const auto newColumnCount =
    major == MatrixMajor::Column ? m_pointColumnCount - 2u : m_pointColumnCount;

  auto newPoints = std::vector<Point>{};
  newPoints.resize(newRowCount * newColumnCount);

  const auto oldPointAt = [&](const size_t w, const size_t h) -> const Point& {
    return major == MatrixMajor::Row
             ? oldPoints[h * oldColumnCount + w]
             : oldPoints[w * oldColumnCount + h];
  };

  const auto newPointAt = [&](const size_t w, const size_t h) -> Point& {
    return major == MatrixMajor::Row
             ? newPoints[h * newColumnCount + w]
             : newPoints[w * newColumnCount + h];
  };

  for (size_t w = 0u; w < width; ++w)
  {
    auto h2 = 0u;
    for (size_t h = 0u; h < height; ++h)
    {
      if (h == pos)
      {
        h += 2u;
      }
      if (h >= height)
      {
        break;
      }

      newPointAt(w, h2++) = oldPointAt(w, h);
    }

    const auto& removed = oldPointAt(w, pos);
    const auto& before2 = oldPointAt(w, pos - 2u);
    const auto& after2 = oldPointAt(w, pos + 2u);
    auto& target = newPointAt(w, pos - 1u);

    const auto midpoint = Point{
      (after2.xyz() + before2.xyz()) / 2.0,
      (after2[3] + before2[3]) * 0.5,
      (after2[4] + before2[4]) * 0.5};

    target = Point{
      2.0 * removed.xyz() - midpoint.xyz(),
      2.0 * removed[3] - midpoint[3],
      2.0 * removed[4] - midpoint[4]};
  }

  m_pointRowCount = newRowCount;
  m_pointColumnCount = newColumnCount;
  m_controlPoints = std::move(newPoints);
  m_controlNormals.clear();
  controlPointsChanged();
}

void BezierPatch::flipTexture(const size_t axis)
{
  contract_pre(axis < 2u);

  const auto component = axis == 0u ? 3u : 4u;
  for (auto& controlPoint : m_controlPoints)
  {
    controlPoint[component] = -controlPoint[component];
  }
}

void BezierPatch::translateTexture(const double s, const double t)
{
  const auto texture = material();
  const auto textureWidth =
    texture && texture->texture() ? std::max<size_t>(1u, texture->texture()->width()) : 1u;
  const auto textureHeight =
    texture && texture->texture() ? std::max<size_t>(1u, texture->texture()->height()) : 1u;

  const auto uShift = -s / static_cast<double>(textureWidth);
  const auto vShift = t / static_cast<double>(textureHeight);

  for (auto& controlPoint : m_controlPoints)
  {
    controlPoint[3] += uShift;
    controlPoint[4] += vShift;
  }
}

void BezierPatch::scaleTexture(const double s, const double t)
{
  for (auto& controlPoint : m_controlPoints)
  {
    controlPoint[3] *= s;
    controlPoint[4] *= t;
  }
}

void BezierPatch::rotateTexture(const double angleDegrees)
{
  const auto radians = vm::to_radians(angleDegrees);
  const auto sine = std::sin(radians);
  const auto cosine = std::cos(radians);

  for (auto& controlPoint : m_controlPoints)
  {
    const auto u = controlPoint[3];
    const auto v = controlPoint[4];
    controlPoint[3] = u * cosine - v * sine;
    controlPoint[4] = v * cosine + u * sine;
  }
}

void BezierPatch::setTextureRepeat(double s, double t)
{
  const auto sIncrement = (s == 0.0 ? 1.0 : s) / double(m_pointColumnCount - 1u);
  const auto tIncrement = (t == 0.0 ? 1.0 : t) / double(m_pointRowCount - 1u);

  auto texT = 0.0;
  for (size_t row = 0u; row < m_pointRowCount; ++row)
  {
    auto texS = 0.0;
    for (size_t col = 0u; col < m_pointColumnCount; ++col)
    {
      auto& point = controlPoint(row, col);
      point[3] = texS;
      point[4] = texT;
      texS += sIncrement;
    }
    texT += tIncrement;
  }
}

void BezierPatch::capTexture(size_t textureWidth, size_t textureHeight)
{
  textureWidth = std::max<size_t>(1u, textureWidth);
  textureHeight = std::max<size_t>(1u, textureHeight);

  const auto axes = computeAverageAxes(*this);

  auto normal = vm::cross(axes.widthDir, axes.heightDir);
  const auto nearZero = [](const vm::vec3d& dir) {
    return vm::squared_length(dir)
           <= vm::constants<double>::almost_zero() * vm::constants<double>::almost_zero();
  };
  if (nearZero(normal))
  {
    normal = vm::vec3d{0.0, 0.0, 1.0};
  }
  else
  {
    normal = vm::normalize(normal);
  }

  const auto projectOntoPlane = [&](const vm::vec3d& dir) {
    return dir - vm::dot(dir, normal) * normal;
  };

  auto uAxis = projectOntoPlane(axes.widthDir);
  if (nearZero(uAxis))
  {
    const auto reference =
      std::abs(normal.z()) < 0.999 ? vm::vec3d{0.0, 0.0, 1.0} : vm::vec3d{0.0, 1.0, 0.0};
    uAxis = vm::cross(reference, normal);
  }
  if (nearZero(uAxis))
  {
    uAxis = vm::vec3d{1.0, 0.0, 0.0};
  }
  uAxis = vm::normalize(uAxis);

  auto vAxis = vm::cross(normal, uAxis);
  if (nearZero(vAxis))
  {
    vAxis = projectOntoPlane(axes.heightDir);
  }
  if (nearZero(vAxis))
  {
    vAxis = vm::vec3d{0.0, 1.0, 0.0};
  }
  vAxis = vm::normalize(vAxis);

  if (vm::dot(uAxis, axes.widthDir) < 0.0)
  {
    uAxis = -uAxis;
  }
  if (vm::dot(vAxis, axes.heightDir) < 0.0)
  {
    vAxis = -vAxis;
  }

  const auto invTextureWidth = 1.0 / static_cast<double>(textureWidth);
  const auto invTextureHeight = 1.0 / static_cast<double>(textureHeight);

  for (auto& controlPoint : m_controlPoints)
  {
    const auto& position = controlPoint.xyz();
    controlPoint[3] = vm::dot(position, uAxis) * invTextureWidth;
    controlPoint[4] = -vm::dot(position, vAxis) * invTextureHeight;
  }
}

void BezierPatch::naturalTexture(size_t textureWidth, size_t textureHeight)
{
  textureWidth = std::max<size_t>(1u, textureWidth);
  textureHeight = std::max<size_t>(1u, textureHeight);

  {
    const auto texSize = static_cast<double>(textureWidth);

    auto texBest = 0.0;
    auto tex = 0.0;
    for (size_t col = 0u; col < m_pointColumnCount; ++col)
    {
      for (size_t row = 0u; row < m_pointRowCount; ++row)
      {
        controlPoint(row, col)[3] = tex;
      }

      if (col + 1u == m_pointColumnCount)
      {
        break;
      }

      for (size_t row = 0u; row < m_pointRowCount; ++row)
      {
        const auto length = tex
                            + vm::length(controlPoint(row, col).xyz()
                                         - controlPoint(row, col + 1u).xyz())
                                / texSize;
        if (std::abs(length) > std::abs(texBest))
        {
          texBest = length;
        }
      }

      tex = texBest;
    }
  }

  {
    const auto texSize = -static_cast<double>(textureHeight);

    auto texBest = 0.0;
    auto tex = 0.0;
    for (size_t row = 0u; row < m_pointRowCount; ++row)
    {
      for (size_t col = 0u; col < m_pointColumnCount; ++col)
      {
        controlPoint(row, col)[4] = tex;
      }

      if (row + 1u == m_pointRowCount)
      {
        break;
      }

      for (size_t col = 0u; col < m_pointColumnCount; ++col)
      {
        const auto length = tex
                            + vm::length(controlPoint(row, col).xyz()
                                         - controlPoint(row + 1u, col).xyz())
                                / texSize;
        if (std::abs(length) > std::abs(texBest))
        {
          texBest = length;
        }
      }

      tex = texBest;
    }
  }
}

void BezierPatch::controlPointsChanged()
{
  m_bounds = computeBounds(m_controlPoints);
}

template <typename Vec>
using SurfaceControlPoints = std::array<std::array<Vec, 3u>, 3u>;

template <typename Vec>
static SurfaceControlPoints<Vec> collectSurfaceControlPoints(
  const std::vector<Vec>& controlPoints,
  const size_t pointColumnCount,
  const size_t surfaceRow,
  const size_t surfaceCol)
{
  // at which column and row do we need to start collecting control points for the
  // surface?
  const size_t rowOffset = 2u * surfaceRow;
  const size_t colOffset = 2u * surfaceCol;

  // collect 3*3 control points
  auto result = SurfaceControlPoints<Vec>{};
  for (size_t row = 0; row < 3u; ++row)
  {
    for (size_t col = 0; col < 3u; ++col)
    {
      result[row][col] =
        controlPoints[(row + rowOffset) * pointColumnCount + col + colOffset];
    }
  }
  return result;
}

template <typename Vec>
static std::vector<SurfaceControlPoints<Vec>> collectAllSurfaceControlPoints(
  const std::vector<Vec>& controlPoints,
  const size_t pointRowCount,
  const size_t pointColumnCount)
{
  // determine how many 3*3 surfaces the patch has in each direction
  const size_t surfaceRowCount = (pointRowCount - 1u) / 2u;
  const size_t surfaceColumnCount = (pointColumnCount - 1u) / 2u;

  // collect the control points for each surface
  auto result = std::vector<SurfaceControlPoints<Vec>>{};
  result.reserve(surfaceRowCount * surfaceColumnCount);

  for (size_t surfaceRow = 0u; surfaceRow < surfaceRowCount; ++surfaceRow)
  {
    for (size_t surfaceCol = 0u; surfaceCol < surfaceColumnCount; ++surfaceCol)
    {
      result.push_back(collectSurfaceControlPoints(
        controlPoints, pointColumnCount, surfaceRow, surfaceCol));
    }
  }
  return result;
}

template <typename Vec>
std::vector<Vec> evaluatePatchGrid(
  const std::vector<Vec>& controlPoints,
  const size_t pointRowCount,
  const size_t pointColumnCount,
  const size_t subdivisionsPerSurface)
{
  // collect the control points for each surface in this patch
  const auto allSurfaceControlPoints =
    collectAllSurfaceControlPoints(controlPoints, pointRowCount, pointColumnCount);

  const auto quadsPerSurfaceSide = (1u << subdivisionsPerSurface);

  // determine dimensions of the resulting point grid
  const size_t surfaceRowCount = (pointRowCount - 1u) / 2u;
  const size_t surfaceColumnCount = (pointColumnCount - 1u) / 2u;
  const size_t gridPointRowCount = surfaceRowCount * quadsPerSurfaceSide + 1u;
  const size_t gridPointColumnCount = surfaceColumnCount * quadsPerSurfaceSide + 1u;

  auto grid = std::vector<Vec>{};
  grid.reserve(gridPointRowCount * gridPointColumnCount);

  /*
  Next we sample the surfaces to compute each point in the grid.

  Consider the following example of a Bezier patch consisting of 4 surfaces A, B, C, D. In
  the diagram, an asterisk (*) represents a point on the grid, and o represents a point on
  the grid which is shared by adjacent surfaces. Each surface is subdivided into 3*3
  parts, which yields 4*4=16 grid points per surface.

  We compute the grid row by row, so in each iteration, we need to determine which surface
  should be sampled for the grid point. For the shared points, we could sample either
  surface, but we decided (arbitrarily) that for a shared point, we will sample the
  previous surface. In the diagram, the surface column / row index indicates which surface
  will be sampled for each grid point. Suppose we want to compute the grid point at column
  3, row 2. This is a shared point of surfaces A and B, and per our rule, we will sample
  surface A.

  This also affects how we compute the u and v values which we use to sample each surface.
  Note that for shared grid points, either u or v or both are always 1. This is necessary
  because we are still sampling the preceeding surface for the shared grid points.

            0   1/4  2/4  3/4   1   1/4  2/4  3/4   1 -- value of u
            0    0    0    0    0    1    1    1    1 -- surface column index
            0    1    2    3    4    5    6    7    8 -- grid column index
  0    0  0 *----*----*----*----o----*----*----*----*
            |                   |                   |
  1/4  0  1 *    *    *    *    o    *    *    *    *
            |       A           |       B           |
  2/4  0  2 *    *    *    *    o    *    *    *    *
            |                   |                   |
  3/4  0  3 *    *    *    *    o    *    *    *    *
            |                   |                   |
  1    0  4 o----o----o----o----o----o----o----o----o
            |                   |                   |
  1/4  1  5 *    *    *    *    o    *    *    *    *
            |       C           |       D           |
  2/4  1  6 *    *    *    *    o    *    *    *    *
            |                   |                   |
  3/4  1  7 *    *    *    *    o    *    *    *    *
            |                   |                   |
  1    1  8 *----*----*----*----o----*----*----*----*
  |    |  |
  |    |  grid row index
  |    |
  |    surface row index
  |
  value of v
  */

  for (size_t gridRow = 0u; gridRow < gridPointRowCount; ++gridRow)
  {
    const size_t surfaceRow =
      (gridRow > 0u ? gridRow - 1u : gridRow) / quadsPerSurfaceSide;
    const double v = static_cast<double>(gridRow - surfaceRow * quadsPerSurfaceSide)
                     / static_cast<double>(quadsPerSurfaceSide);

    for (size_t gridCol = 0u; gridCol < gridPointColumnCount; ++gridCol)
    {
      const size_t surfaceCol =
        (gridCol > 0u ? gridCol - 1u : gridCol) / quadsPerSurfaceSide;
      const double u = static_cast<double>(gridCol - surfaceCol * quadsPerSurfaceSide)
                       / static_cast<double>(quadsPerSurfaceSide);

      const auto& surfaceControlPoints =
        allSurfaceControlPoints[surfaceRow * surfaceColumnCount + surfaceCol];
      auto point = vm::evaluate_quadratic_bezier_surface(surfaceControlPoints, u, v);
      grid.push_back(std::move(point));
    }
  }

  return grid;
}

std::vector<BezierPatch::Point> BezierPatch::evaluate(
  const size_t subdivisionsPerSurface) const
{
  return evaluatePatchGrid(
    m_controlPoints, m_pointRowCount, m_pointColumnCount, subdivisionsPerSurface);
}

std::vector<BezierPatch::Normal> BezierPatch::evaluateNormals(
  const size_t subdivisionsPerSurface) const
{
  if (m_controlNormals.empty())
  {
    return {};
  }

  return evaluatePatchGrid(
    m_controlNormals, m_pointRowCount, m_pointColumnCount, subdivisionsPerSurface);
}

} // namespace tb::mdl
