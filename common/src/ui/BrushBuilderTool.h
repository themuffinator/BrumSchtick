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

#pragma once

#include "Notifier.h"
#include "mdl/HitType.h"
#include "ui/CreateBrushesToolBase.h"

#include "vm/mat.h"
#include "vm/plane.h"
#include "vm/ray.h"
#include "vm/vec.h"

#include <optional>
#include <string>
#include <vector>

namespace tb::mdl
{
class PickResult;
} // namespace tb::mdl

namespace tb::render
{
class Camera;
class RenderBatch;
class RenderContext;
} // namespace tb::render

namespace tb::ui
{
class MapDocument;

class BrushBuilderTool : public CreateBrushesToolBase
{
public:
  struct Polygon
  {
    std::vector<vm::vec3d> vertices;
    bool closed = false;
  };

  enum class TransformType
  {
    Translation,
    Rotation,
    Scaling,
    Matrix,
    Expression,
  };

  struct ExpressionStep
  {
    std::string xExpression = "x";
    std::string yExpression = "y";
    std::string zExpression = "z";
  };

  struct TransformStep
  {
    TransformType type = TransformType::Translation;
    bool enabled = true;
    size_t subdivisions = 1;

    vm::vec3d translation = vm::vec3d::zero();
    vm::axis::type rotationAxis = vm::axis::z;
    double rotationAngle = 0.0;
    vm::vec3d scale = vm::vec3d{1.0, 1.0, 1.0};
    vm::mat4x4d matrix = vm::mat4x4d::identity();
    ExpressionStep expression;
  };

  struct VertexHandle
  {
    size_t polygonIndex = 0;
    size_t vertexIndex = 0;
  };

  static const mdl::HitType::Type VertexHitType;

public:
  Notifier<> shapeDidChangeNotifier;
  Notifier<> stepsDidChangeNotifier;

public:
  explicit BrushBuilderTool(MapDocument& document);

  const std::vector<Polygon>& polygons() const;
  const std::vector<TransformStep>& steps() const;

  bool hasOpenPolygon() const;
  bool hasClosedPolygons() const;

  const std::optional<vm::plane3d>& shapePlane() const;
  void setShapePlane(const vm::plane3d& plane);

  void addPoint(const vm::vec3d& point, const vm::vec3d& planeNormal);
  bool closeActivePolygon();
  bool removeLastPoint();
  void clearShape();

  std::optional<vm::vec3d> vertexPosition(const VertexHandle& handle) const;
  bool moveVertex(const VertexHandle& handle, const vm::vec3d& position);

  bool snapToGrid() const;
  void setSnapToGrid(bool snap);

  bool snapToInteger() const;
  void setSnapToInteger(bool snap);

  void addStep(const TransformStep& step);
  bool removeStep(size_t index);
  bool moveStepUp(size_t index);
  bool moveStepDown(size_t index);
  bool updateStep(size_t index, const TransformStep& step);

  void pick(
    const vm::ray3d& pickRay, const render::Camera& camera, mdl::PickResult& pickResult);
  void render(
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch,
    const mdl::PickResult& pickResult);

  vm::vec3d snapPointToPlane(const vm::vec3d& point, const vm::plane3d& plane) const;

private:
  MapDocument& m_document;
  std::vector<Polygon> m_polygons;
  std::vector<TransformStep> m_steps;
  std::optional<vm::plane3d> m_shapePlane;
  bool m_snapToGrid = true;
  bool m_snapToInteger = false;

private:
  void rebuildPreview();
  vm::vec3d shapeOrigin() const;
  std::vector<vm::vec3d> buildSweepPoints(
    const Polygon& polygon, const vm::vec3d& origin) const;

  vm::vec3d snapPointToGrid(const vm::vec3d& point) const;
  vm::vec3d snapPointToGridSize(
    const vm::vec3d& point, const vm::plane3d& plane, double gridSize) const;

  QWidget* doCreatePage(QWidget* parent) override;

  bool doActivate() override;
  bool doDeactivate() override;
  void doBrushesWereCreated() override;
};

} // namespace tb::ui
