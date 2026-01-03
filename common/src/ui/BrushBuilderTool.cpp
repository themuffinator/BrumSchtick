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

#include "BrushBuilderTool.h"

#include "Error.h" // IWYU pragma: keep
#include "Logger.h"
#include "PreferenceManager.h"
#include "Preferences.h"
#include "el/ELParser.h"
#include "el/EvaluationContext.h"
#include "el/VariableStore.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushNode.h"
#include "mdl/GameConfig.h"
#include "mdl/GameInfo.h"
#include "mdl/Map.h"
#include "mdl/Polyhedron3.h"
#include "mdl/WorldNode.h"
#include "render/Camera.h"
#include "render/RenderService.h"
#include "ui/BrushBuilderToolPage.h"

#include "kd/result.h"
#include "kd/vector_utils.h"

#include "vm/convex_hull.h"
#include "vm/mat_ext.h"
#include "vm/scalar.h"
#include "vm/vec_ext.h"

#include <algorithm>
#include <ranges>

namespace tb::ui
{
namespace
{
struct ParsedExpressions
{
  el::ExpressionNode x;
  el::ExpressionNode y;
  el::ExpressionNode z;
};

std::optional<size_t> openPolygonIndex(const std::vector<BrushBuilderTool::Polygon>& polygons)
{
  for (size_t i = polygons.size(); i-- > 0;)
  {
    if (!polygons[i].closed)
    {
      return i;
    }
  }
  return std::nullopt;
}

std::optional<ParsedExpressions> parseExpressions(
  const BrushBuilderTool::ExpressionStep& expression,
  Logger& logger)
{
  const auto parse = [&](const std::string& expr, const char* axis) {
    auto result = el::ELParser::parseLenient(expr);
    if (result.is_error())
    {
      logger.error() << "Brush Builder expression (" << axis << ") error: "
                     << result.error().msg;
      return std::optional<el::ExpressionNode>{};
    }
    return std::optional<el::ExpressionNode>{std::move(result.value())};
  };

  auto x = parse(expression.xExpression, "x");
  auto y = parse(expression.yExpression, "y");
  auto z = parse(expression.zExpression, "z");
  if (!x || !y || !z)
  {
    return std::nullopt;
  }

  return ParsedExpressions{std::move(*x), std::move(*y), std::move(*z)};
}

vm::vec3d applyExpression(
  const ParsedExpressions& expression,
  const vm::vec3d& point,
  const double t,
  Logger& logger)
{
  auto variables = el::VariableTable{};
  variables.set("x", el::Value{point.x()});
  variables.set("y", el::Value{point.y()});
  variables.set("z", el::Value{point.z()});
  variables.set("t", el::Value{t});

  const auto result = el::withEvaluationContext(
    [&](auto& context) {
      return vm::vec3d{
        expression.x.evaluate(context).numberValue(context),
        expression.y.evaluate(context).numberValue(context),
        expression.z.evaluate(context).numberValue(context)};
    },
    variables);

  if (result.is_error())
  {
    logger.error() << "Brush Builder expression evaluation error: "
                   << result.error().msg;
    return point;
  }

  return result.value();
}

vm::mat4x4d buildStepMatrix(
  const BrushBuilderTool::TransformStep& step,
  const double t,
  const vm::vec3d& origin)
{
  switch (step.type)
  {
  case BrushBuilderTool::TransformType::Translation:
    return vm::translation_matrix(step.translation * t);
  case BrushBuilderTool::TransformType::Rotation: {
    const auto axis = vm::vec3d::axis(size_t(step.rotationAxis));
    const auto angle = vm::to_radians(step.rotationAngle * t);
    return vm::translation_matrix(origin) * vm::rotation_matrix(axis, angle)
           * vm::translation_matrix(-origin);
  }
  case BrushBuilderTool::TransformType::Scaling: {
    const auto factors = vm::vec3d{1.0, 1.0, 1.0} + (step.scale - vm::vec3d{1.0, 1.0, 1.0}) * t;
    return vm::translation_matrix(origin) * vm::scaling_matrix(factors)
           * vm::translation_matrix(-origin);
  }
  case BrushBuilderTool::TransformType::Matrix: {
    const auto identity = vm::mat4x4d::identity();
    return identity + (step.matrix - identity) * t;
  }
  case BrushBuilderTool::TransformType::Expression:
    return vm::mat4x4d::identity();
  }

  return vm::mat4x4d::identity();
}
} // namespace

const mdl::HitType::Type BrushBuilderTool::VertexHitType = mdl::HitType::freeType();

BrushBuilderTool::BrushBuilderTool(MapDocument& document)
  : CreateBrushesToolBase{false, document}
  , m_document{document}
{
  TransformStep step;
  step.type = TransformType::Translation;
  step.translation = vm::vec3d{0.0, 0.0, 64.0};
  m_steps.push_back(step);
}

const std::vector<BrushBuilderTool::Polygon>& BrushBuilderTool::polygons() const
{
  return m_polygons;
}

const std::vector<BrushBuilderTool::TransformStep>& BrushBuilderTool::steps() const
{
  return m_steps;
}

bool BrushBuilderTool::hasOpenPolygon() const
{
  return openPolygonIndex(m_polygons).has_value();
}

bool BrushBuilderTool::hasClosedPolygons() const
{
  return std::ranges::any_of(m_polygons, [](const auto& polygon) { return polygon.closed; });
}

const std::optional<vm::plane3d>& BrushBuilderTool::shapePlane() const
{
  return m_shapePlane;
}

void BrushBuilderTool::setShapePlane(const vm::plane3d& plane)
{
  m_shapePlane = plane;
}

void BrushBuilderTool::addPoint(const vm::vec3d& point, const vm::vec3d& planeNormal)
{
  auto index = openPolygonIndex(m_polygons);
  if (!index)
  {
    m_polygons.push_back(Polygon{});
    index = m_polygons.size() - 1;
  }

  auto& polygon = m_polygons[*index];
  const auto snappedPoint =
    m_shapePlane ? snapPointToPlane(point, *m_shapePlane) : point;

  if (polygon.vertices.empty())
  {
    const auto normal = vm::normalize(planeNormal);
    if (!m_shapePlane)
    {
      m_shapePlane = vm::plane3d{snappedPoint, normal};
    }
  }

  if (!polygon.vertices.empty() && polygon.vertices.back() == snappedPoint)
  {
    return;
  }

  polygon.vertices.push_back(snappedPoint);
  rebuildPreview();
  shapeDidChangeNotifier.notify();
  refreshViews();
}

bool BrushBuilderTool::closeActivePolygon()
{
  const auto index = openPolygonIndex(m_polygons);
  if (!index)
  {
    return false;
  }

  auto& polygon = m_polygons[*index];
  if (polygon.vertices.size() < 3u)
  {
    return false;
  }

  auto hull = vm::convex_hull<double>(polygon.vertices);
  if (hull.size() < 3u)
  {
    return false;
  }

  polygon.vertices = std::move(hull);
  polygon.closed = true;

  rebuildPreview();
  shapeDidChangeNotifier.notify();
  refreshViews();
  return true;
}

bool BrushBuilderTool::removeLastPoint()
{
  const auto index = openPolygonIndex(m_polygons);
  if (!index)
  {
    return false;
  }

  auto& polygon = m_polygons[*index];
  if (polygon.vertices.empty())
  {
    return false;
  }

  polygon.vertices.pop_back();
  if (polygon.vertices.empty())
  {
    m_polygons.erase(m_polygons.begin() + static_cast<ptrdiff_t>(*index));
    if (m_polygons.empty())
    {
      m_shapePlane.reset();
    }
  }

  rebuildPreview();
  shapeDidChangeNotifier.notify();
  refreshViews();
  return true;
}

void BrushBuilderTool::clearShape()
{
  m_polygons.clear();
  m_shapePlane.reset();
  clearBrushes();
  shapeDidChangeNotifier.notify();
  refreshViews();
}

std::optional<vm::vec3d> BrushBuilderTool::vertexPosition(const VertexHandle& handle) const
{
  if (handle.polygonIndex >= m_polygons.size())
  {
    return std::nullopt;
  }
  const auto& polygon = m_polygons[handle.polygonIndex];
  if (handle.vertexIndex >= polygon.vertices.size())
  {
    return std::nullopt;
  }
  return polygon.vertices[handle.vertexIndex];
}

bool BrushBuilderTool::moveVertex(const VertexHandle& handle, const vm::vec3d& position)
{
  if (handle.polygonIndex >= m_polygons.size())
  {
    return false;
  }
  auto& polygon = m_polygons[handle.polygonIndex];
  if (handle.vertexIndex >= polygon.vertices.size())
  {
    return false;
  }

  const auto snappedPosition =
    m_shapePlane ? snapPointToPlane(position, *m_shapePlane) : position;
  polygon.vertices[handle.vertexIndex] = snappedPosition;

  rebuildPreview();
  shapeDidChangeNotifier.notify();
  refreshViews();
  return true;
}

bool BrushBuilderTool::snapToGrid() const
{
  return m_snapToGrid;
}

void BrushBuilderTool::setSnapToGrid(const bool snap)
{
  if (m_snapToGrid == snap)
  {
    return;
  }
  m_snapToGrid = snap;
  rebuildPreview();
  refreshViews();
}

bool BrushBuilderTool::snapToInteger() const
{
  return m_snapToInteger;
}

void BrushBuilderTool::setSnapToInteger(const bool snap)
{
  if (m_snapToInteger == snap)
  {
    return;
  }
  m_snapToInteger = snap;
  rebuildPreview();
  refreshViews();
}

void BrushBuilderTool::addStep(const TransformStep& step)
{
  m_steps.push_back(step);
  rebuildPreview();
  stepsDidChangeNotifier.notify();
  refreshViews();
}

bool BrushBuilderTool::removeStep(const size_t index)
{
  if (index >= m_steps.size())
  {
    return false;
  }

  m_steps.erase(m_steps.begin() + static_cast<ptrdiff_t>(index));
  rebuildPreview();
  stepsDidChangeNotifier.notify();
  refreshViews();
  return true;
}

bool BrushBuilderTool::moveStepUp(const size_t index)
{
  if (index == 0 || index >= m_steps.size())
  {
    return false;
  }

  std::swap(m_steps[index - 1], m_steps[index]);
  rebuildPreview();
  stepsDidChangeNotifier.notify();
  refreshViews();
  return true;
}

bool BrushBuilderTool::moveStepDown(const size_t index)
{
  if (index + 1 >= m_steps.size())
  {
    return false;
  }

  std::swap(m_steps[index], m_steps[index + 1]);
  rebuildPreview();
  stepsDidChangeNotifier.notify();
  refreshViews();
  return true;
}

bool BrushBuilderTool::updateStep(const size_t index, const TransformStep& step)
{
  if (index >= m_steps.size())
  {
    return false;
  }

  m_steps[index] = step;
  rebuildPreview();
  stepsDidChangeNotifier.notify();
  refreshViews();
  return true;
}

void BrushBuilderTool::pick(
  const vm::ray3d& pickRay, const render::Camera& camera, mdl::PickResult& pickResult)
{
  for (size_t polygonIndex = 0; polygonIndex < m_polygons.size(); ++polygonIndex)
  {
    const auto& polygon = m_polygons[polygonIndex];
    for (size_t vertexIndex = 0; vertexIndex < polygon.vertices.size(); ++vertexIndex)
    {
      const auto& point = polygon.vertices[vertexIndex];
      if (
        const auto distance = camera.pickPointHandle(
          pickRay, point, static_cast<double>(pref(Preferences::HandleRadius))))
      {
        const auto hitPoint = vm::point_at_distance(pickRay, *distance);
        pickResult.addHit(mdl::Hit{
          VertexHitType,
          *distance,
          hitPoint,
          VertexHandle{polygonIndex, vertexIndex}});
      }
    }
  }
}

void BrushBuilderTool::render(
  render::RenderContext& renderContext,
  render::RenderBatch& renderBatch,
  const mdl::PickResult& pickResult)
{
  CreateBrushesToolBase::render(renderContext, renderBatch);

  auto renderService = render::RenderService{renderContext, renderBatch};
  renderService.setLineWidth(2.0f);
  renderService.setForegroundColor(pref(Preferences::HandleColor));

  for (const auto& polygon : m_polygons)
  {
    if (polygon.vertices.size() > 1)
    {
      for (size_t i = 1; i < polygon.vertices.size(); ++i)
      {
        renderService.renderLine(
          vm::vec3f{polygon.vertices[i - 1]}, vm::vec3f{polygon.vertices[i]});
      }
      if (polygon.closed && polygon.vertices.size() > 2)
      {
        renderService.renderLine(
          vm::vec3f{polygon.vertices.back()}, vm::vec3f{polygon.vertices.front()});
      }
    }

    for (const auto& vertex : polygon.vertices)
    {
      renderService.renderHandle(vm::vec3f{vertex});
    }
  }

  const auto& hit = pickResult.first(type(VertexHitType));
  if (hit.isMatch())
  {
    const auto handle = hit.target<VertexHandle>();
    if (const auto position = vertexPosition(handle))
    {
      renderService.setForegroundColor(pref(Preferences::SelectedHandleColor));
      renderService.renderHandle(vm::vec3f{*position});
    }
  }
}

vm::vec3d BrushBuilderTool::snapPointToPlane(
  const vm::vec3d& point, const vm::plane3d& plane) const
{
  if (m_snapToInteger)
  {
    return snapPointToGridSize(point, plane, 1.0);
  }

  if (m_snapToGrid)
  {
    return snapPointToGridSize(point, plane, m_document.map().grid().actualSize());
  }

  return point;
}

void BrushBuilderTool::rebuildPreview()
{
  clearBrushes();

  if (!hasClosedPolygons() || m_steps.empty())
  {
    return;
  }

  auto& map = m_document.map();
  const auto builder = mdl::BrushBuilder{
    map.worldNode().mapFormat(),
    map.worldBounds(),
    map.gameInfo().gameConfig.faceAttribsConfig.defaults};
  const auto origin = shapeOrigin();

  auto brushNodes = std::vector<std::unique_ptr<mdl::BrushNode>>{};
  for (const auto& polygon : m_polygons)
  {
    if (!polygon.closed || polygon.vertices.size() < 3u)
    {
      continue;
    }

    auto sweepPoints = buildSweepPoints(polygon, origin);
    if (sweepPoints.size() < 4u)
    {
      continue;
    }

    auto polyhedron = mdl::Polyhedron3{std::move(sweepPoints)};
    builder.createBrush(polyhedron, map.currentMaterialName())
      | kdl::transform([&](auto brush) {
          brushNodes.push_back(std::make_unique<mdl::BrushNode>(std::move(brush)));
        })
      | kdl::transform_error([&](const auto& e) {
          m_document.logger().error() << "Could not build brush: " << e.msg;
        });
  }

  updateBrushes(std::move(brushNodes));
}

vm::vec3d BrushBuilderTool::shapeOrigin() const
{
  auto boundsBuilder = vm::bbox3d::builder{};
  for (const auto& polygon : m_polygons)
  {
    for (const auto& vertex : polygon.vertices)
    {
      boundsBuilder.add(vertex);
    }
  }

  return boundsBuilder.initialized() ? boundsBuilder.bounds().center()
                                     : vm::vec3d::zero();
}

std::vector<vm::vec3d> BrushBuilderTool::buildSweepPoints(
  const Polygon& polygon, const vm::vec3d& origin) const
{
  auto sweepPoints = polygon.vertices;
  auto currentPoints = polygon.vertices;

  for (const auto& step : m_steps)
  {
    if (!step.enabled)
    {
      continue;
    }

    const auto stepStartPoints = currentPoints;
    const auto subdivisions = std::max<size_t>(1u, step.subdivisions);

    std::optional<ParsedExpressions> parsed;
    if (step.type == TransformType::Expression)
    {
      parsed = parseExpressions(step.expression, m_document.logger());
      if (!parsed)
      {
        return {};
      }
    }

    for (size_t i = 1; i <= subdivisions; ++i)
    {
      const auto t = static_cast<double>(i) / static_cast<double>(subdivisions);
      if (step.type == TransformType::Expression)
      {
        currentPoints = stepStartPoints | std::views::transform([&](const auto& point) {
                          return snapPointToGrid(
                            applyExpression(*parsed, point, t, m_document.logger()));
                        })
                        | kdl::ranges::to<std::vector>();
      }
      else
      {
        const auto matrix = buildStepMatrix(step, t, origin);
        currentPoints =
          (matrix * stepStartPoints)
          | std::views::transform([&](const auto& point) { return snapPointToGrid(point); })
          | kdl::ranges::to<std::vector>();
      }

      sweepPoints = kdl::vec_concat(std::move(sweepPoints), currentPoints);
    }
  }

  return kdl::vec_sort_and_remove_duplicates(std::move(sweepPoints));
}

vm::vec3d BrushBuilderTool::snapPointToGrid(const vm::vec3d& point) const
{
  if (!m_snapToGrid && !m_snapToInteger)
  {
    return point;
  }

  auto result = point;
  if (m_snapToGrid)
  {
    const auto size = m_document.map().grid().actualSize();
    result = vm::vec3d{
      vm::snap(result.x(), size),
      vm::snap(result.y(), size),
      vm::snap(result.z(), size)};
  }

  if (m_snapToInteger)
  {
    result = vm::vec3d{
      std::round(result.x()),
      std::round(result.y()),
      std::round(result.z())};
  }

  return result;
}

vm::vec3d BrushBuilderTool::snapPointToGridSize(
  const vm::vec3d& point, const vm::plane3d& plane, const double gridSize) const
{
  auto result = vm::vec3d{};
  switch (vm::find_abs_max_component(plane.normal))
  {
  case vm::axis::x:
    result[1] = vm::snap(point.y(), gridSize);
    result[2] = vm::snap(point.z(), gridSize);
    result[0] = plane.xAt(result.yz());
    break;
  case vm::axis::y:
    result[0] = vm::snap(point.x(), gridSize);
    result[2] = vm::snap(point.z(), gridSize);
    result[1] = plane.yAt(result.xz());
    break;
  case vm::axis::z:
    result[0] = vm::snap(point.x(), gridSize);
    result[1] = vm::snap(point.y(), gridSize);
    result[2] = plane.zAt(result.xy());
    break;
    switchDefault();
  }
  return result;
}

QWidget* BrushBuilderTool::doCreatePage(QWidget* parent)
{
  return new BrushBuilderToolPage{*this, parent};
}

bool BrushBuilderTool::doActivate()
{
  clearShape();
  return true;
}

bool BrushBuilderTool::doDeactivate()
{
  clearShape();
  return true;
}

void BrushBuilderTool::doBrushesWereCreated()
{
  clearShape();
}

} // namespace tb::ui
