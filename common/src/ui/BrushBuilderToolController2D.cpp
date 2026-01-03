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

#include "BrushBuilderToolController2D.h"

#include "mdl/Hit.h"
#include "mdl/HitFilter.h"
#include "mdl/Map.h"
#include "mdl/PickResult.h"
#include "render/Camera.h"
#include "ui/BrushBuilderTool.h"
#include "ui/HandleDragTracker.h"
#include "ui/InputState.h"
#include "ui/MapDocument.h"

#include "vm/intersection.h"
#include "vm/plane.h"
#include "vm/vec.h"

#include <optional>

namespace tb::ui
{
namespace
{
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

bool isFirstVertexOfOpenPolygon(
  const BrushBuilderTool::VertexHandle& handle,
  const std::vector<BrushBuilderTool::Polygon>& polygons)
{
  const auto openIndex = openPolygonIndex(polygons);
  return openIndex && handle.polygonIndex == *openIndex && handle.vertexIndex == 0;
}

class MoveVertexDragDelegate : public HandleDragTrackerDelegate
{
private:
  BrushBuilderTool& m_tool;
  BrushBuilderTool::VertexHandle m_handle;
  vm::vec3d m_initialPosition;
  vm::plane3d m_plane;

public:
  MoveVertexDragDelegate(
    BrushBuilderTool& tool,
    const BrushBuilderTool::VertexHandle& handle,
    const vm::vec3d& initialPosition,
    const vm::plane3d& plane)
    : m_tool{tool}
    , m_handle{handle}
    , m_initialPosition{initialPosition}
    , m_plane{plane}
  {
  }

  HandlePositionProposer start(
    const InputState&,
    const vm::vec3d&,
    const vm::vec3d& handleOffset) override
  {
    return makeHandlePositionProposer(
      makePlaneHandlePicker(m_plane, handleOffset),
      [this](const InputState&, const DragState&, const vm::vec3d& proposedHandlePosition) {
        return std::optional<vm::vec3d>{
          m_tool.snapPointToPlane(proposedHandlePosition, m_plane)};
      });
  }

  DragStatus update(
    const InputState&,
    const DragState&,
    const vm::vec3d& proposedHandlePosition) override
  {
    return m_tool.moveVertex(m_handle, proposedHandlePosition) ? DragStatus::Continue
                                                               : DragStatus::Deny;
  }

  void end(const InputState&, const DragState&) override {}

  void cancel(const DragState&) override
  {
    m_tool.moveVertex(m_handle, m_initialPosition);
  }

  void render(
    const InputState& inputState,
    const DragState&,
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch) const override
  {
    m_tool.render(renderContext, renderBatch, inputState.pickResult());
  }
};

std::optional<vm::vec3d> intersectRayPlane(
  const vm::ray3d& pickRay, const vm::plane3d& plane)
{
  if (const auto distance = vm::intersect_ray_plane(pickRay, plane))
  {
    return vm::point_at_distance(pickRay, *distance);
  }
  return std::nullopt;
}
} // namespace

BrushBuilderToolController2D::BrushBuilderToolController2D(
  BrushBuilderTool& tool, MapDocument& document)
  : m_tool{tool}
  , m_document{document}
{
}

Tool& BrushBuilderToolController2D::tool()
{
  return m_tool;
}

const Tool& BrushBuilderToolController2D::tool() const
{
  return m_tool;
}

void BrushBuilderToolController2D::pick(
  const InputState& inputState, mdl::PickResult& pickResult)
{
  m_tool.pick(inputState.pickRay(), inputState.camera(), pickResult);
}

bool BrushBuilderToolController2D::mouseClick(const InputState& inputState)
{
  using namespace mdl::HitFilters;

  if (
    !inputState.mouseButtonsPressed(MouseButtons::Left)
    || !inputState.modifierKeysPressed(ModifierKeys::None))
  {
    return false;
  }

  const auto& hit = inputState.pickResult().first(type(BrushBuilderTool::VertexHitType));
  if (hit.isMatch())
  {
    const auto handle = hit.target<BrushBuilderTool::VertexHandle>();
    if (isFirstVertexOfOpenPolygon(handle, m_tool.polygons()))
    {
      return m_tool.closeActivePolygon();
    }
    return true;
  }

  const auto& camera = inputState.camera();
  const auto plane = m_tool.shapePlane().value_or(
    vm::plane3d{m_document.map().referenceBounds().center(), vm::vec3d{camera.direction()}});
  const auto point = intersectRayPlane(inputState.pickRay(), plane);
  if (!point)
  {
    return false;
  }

  if (!m_tool.shapePlane())
  {
    m_tool.setShapePlane(plane);
  }
  m_tool.addPoint(*point, plane.normal);
  return true;
}

bool BrushBuilderToolController2D::mouseDoubleClick(const InputState& inputState)
{
  if (
    !inputState.mouseButtonsPressed(MouseButtons::Left)
    || !inputState.modifierKeysPressed(ModifierKeys::None))
  {
    return false;
  }
  return m_tool.closeActivePolygon();
}

std::unique_ptr<GestureTracker> BrushBuilderToolController2D::acceptMouseDrag(
  const InputState& inputState)
{
  using namespace mdl::HitFilters;

  if (
    inputState.mouseButtons() != MouseButtons::Left
    || inputState.modifierKeys() != ModifierKeys::None)
  {
    return nullptr;
  }

  const auto& hit = inputState.pickResult().first(type(BrushBuilderTool::VertexHitType));
  if (!hit.isMatch())
  {
    return nullptr;
  }

  const auto handle = hit.target<BrushBuilderTool::VertexHandle>();
  const auto position = m_tool.vertexPosition(handle);
  if (!position)
  {
    return nullptr;
  }

  const auto plane = m_tool.shapePlane().value_or(
    vm::plane3d{*position, vm::vec3d{inputState.camera().direction()}});
  return createHandleDragTracker(
    MoveVertexDragDelegate{m_tool, handle, *position, plane},
    inputState,
    *position,
    hit.hitPoint());
}

void BrushBuilderToolController2D::render(
  const InputState& inputState,
  render::RenderContext& renderContext,
  render::RenderBatch& renderBatch)
{
  m_tool.render(renderContext, renderBatch, inputState.pickResult());
}

bool BrushBuilderToolController2D::cancel()
{
  if (m_tool.removeLastPoint())
  {
    return true;
  }
  if (m_tool.hasClosedPolygons())
  {
    m_tool.clearShape();
    return true;
  }
  return false;
}

} // namespace tb::ui
