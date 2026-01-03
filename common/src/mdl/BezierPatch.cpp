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

#include "kd/const_overload.h"
#include "kd/contracts.h"
#include "kd/reflection_impl.h"

#include "vm/bbox_io.h" // IWYU pragma: keep
#include "vm/bezier_surface.h"
#include "vm/mat_ext.h"
#include "vm/vec_io.h" // IWYU pragma: keep

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
