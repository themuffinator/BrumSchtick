/*
 Copyright (C) 2010 Kristian Duske

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

#include "VertexTool.h"

#include "Macros.h"
#include "PreferenceManager.h"
#include "Preferences.h"
#include "mdl/BrushNode.h"
#include "mdl/BrushVertexCommands.h"
#include "mdl/Map_Geometry.h"
#include "mdl/PatchNode.h"
#include "mdl/VertexHandleManager.h"
#include "render/RenderBatch.h"
#include "ui/MapDocument.h"

#include "kd/const_overload.h"
#include "kd/contracts.h"
#include "kd/string_format.h"

#include "vm/polygon.h"
#include "vm/distance.h"
#include "vm/ray.h"

#include <optional>
#include <tuple>
#include <vector>

namespace tb::ui
{

const mdl::HitType::Type VertexTool::PatchRowHitType = mdl::HitType::freeType();
const mdl::HitType::Type VertexTool::PatchColumnHitType = mdl::HitType::freeType();

VertexTool::VertexTool(MapDocument& document)
  : VertexToolBase{document}
  , m_mode{Mode::Move}
{
}

std::vector<mdl::BrushNode*> VertexTool::findIncidentBrushes(
  const vm::vec3d& handle) const
{
  return findIncidentBrushes(m_document.map().vertexHandles(), handle);
}

std::vector<mdl::BrushNode*> VertexTool::findIncidentBrushes(
  const vm::segment3d& handle) const
{
  return findIncidentBrushes(m_document.map().edgeHandles(), handle);
}

std::vector<mdl::BrushNode*> VertexTool::findIncidentBrushes(
  const vm::polygon3d& handle) const
{
  return findIncidentBrushes(m_document.map().faceHandles(), handle);
}

void VertexTool::pick(
  const vm::ray3d& pickRay,
  const render::Camera& camera,
  mdl::PickResult& pickResult) const
{
  auto& map = m_document.map();
  const auto& grid = map.grid();

  map.vertexHandles().pick(pickRay, camera, pickResult);
  map.edgeHandles().pickGridHandle(pickRay, camera, grid, pickResult);
  map.faceHandles().pickGridHandle(pickRay, camera, grid, pickResult);
  pickPatchRowsAndColumns(pickRay, camera, pickResult);
}

bool VertexTool::deselectAll()
{
  if (VertexToolBase::deselectAll())
  {
    resetModeAfterDeselection();
    return true;
  }
  return false;
}

mdl::VertexHandleManager& VertexTool::handleManager()
{
  return KDL_CONST_OVERLOAD(handleManager());
}

const mdl::VertexHandleManager& VertexTool::handleManager() const
{
  return m_document.map().vertexHandles();
}

std::tuple<vm::vec3d, vm::vec3d> VertexTool::handlePositionAndHitPoint(
  const std::vector<mdl::Hit>& hits) const
{
  contract_pre(!hits.empty());

  const auto& hit = hits.front();
  contract_assert(hit.hasType(
    mdl::VertexHandleManager::HandleHitType | mdl::EdgeHandleManager::HandleHitType
    | mdl::FaceHandleManager::HandleHitType | PatchRowHitType | PatchColumnHitType));

  const auto position = hit.hasType(mdl::VertexHandleManager::HandleHitType)
                          ? hit.target<vm::vec3d>()
                        : hit.hasType(mdl::EdgeHandleManager::HandleHitType)
                          ? std::get<1>(hit.target<mdl::EdgeHandleManager::HitData>())
                        : hit.hasType(mdl::FaceHandleManager::HandleHitType)
                          ? std::get<1>(hit.target<mdl::FaceHandleManager::HitData>())
                        : hit.hasType(PatchRowHitType)
                          ? hit.target<PatchRowHitData>().position
                          : hit.target<PatchColumnHitData>().position;

  return {position, hit.hitPoint()};
}

bool VertexTool::startMove(const std::vector<mdl::Hit>& hits)
{
  auto& map = m_document.map();

  const auto& hit = hits.front();
  if (hit.hasType(PatchRowHitType))
  {
    map.edgeHandles().deselectAll();
    map.faceHandles().deselectAll();
    map.vertexHandles().deselectAll();
    selectPatchRow(hit.target<PatchRowHitData>());
    m_mode = Mode::MovePatchRow;
    refreshViews();
  }
  else if (hit.hasType(PatchColumnHitType))
  {
    map.edgeHandles().deselectAll();
    map.faceHandles().deselectAll();
    map.vertexHandles().deselectAll();
    selectPatchColumn(hit.target<PatchColumnHitData>());
    m_mode = Mode::MovePatchColumn;
    refreshViews();
  }
  else if (hit.hasType(
             mdl::EdgeHandleManager::HandleHitType
             | mdl::FaceHandleManager::HandleHitType))
  {
    map.vertexHandles().deselectAll();
    if (hit.hasType(mdl::EdgeHandleManager::HandleHitType))
    {
      const auto& handle =
        std::get<0>(hit.target<const mdl::EdgeHandleManager::HitData&>());
      map.edgeHandles().select(handle);
      m_mode = Mode::SplitEdge;
    }
    else
    {
      const auto& handle =
        std::get<0>(hit.target<const mdl::FaceHandleManager::HitData&>());
      map.faceHandles().select(handle);
      m_mode = Mode::SplitFace;
    }
    refreshViews();
  }
  else
  {
    m_mode = Mode::Move;
  }

  if (!VertexToolBase::startMove(hits))
  {
    m_mode = Mode::Move;
    return false;
  }
  return true;
}

VertexTool::MoveResult VertexTool::move(const vm::vec3d& delta)
{
  auto& map = m_document.map();

  const auto transform = vm::translation_matrix(delta);

  if (
    m_mode == Mode::Move || m_mode == Mode::MovePatchRow
    || m_mode == Mode::MovePatchColumn)
  {
    auto handles = map.vertexHandles().selectedHandles();
    const auto result = transformVertices(map, std::move(handles), transform);
    if (result.success)
    {
      if (!result.hasRemainingVertices)
      {
        return MoveResult::Cancel;
      }
      m_dragHandlePosition = transform * m_dragHandlePosition;
      return MoveResult::Continue;
    }
    return MoveResult::Deny;
  }

  auto brushes = std::vector<mdl::BrushNode*>{};
  if (m_mode == Mode::SplitEdge)
  {
    if (map.edgeHandles().selectedHandleCount() == 1)
    {
      const auto handle = map.edgeHandles().selectedHandles().front();
      brushes = findIncidentBrushes(handle);
    }
  }
  else
  {
    contract_assert(m_mode == Mode::SplitFace);
    if (map.faceHandles().selectedHandleCount() == 1)
    {
      const auto handle = map.faceHandles().selectedHandles().front();
      brushes = findIncidentBrushes(handle);
    }
  }

  if (!brushes.empty())
  {
    const auto newVertexPosition = transform * m_dragHandlePosition;
    if (addVertex(map, newVertexPosition))
    {
      m_mode = Mode::Move;
      map.edgeHandles().deselectAll();
      map.faceHandles().deselectAll();
      m_dragHandlePosition = transform * m_dragHandlePosition;
      map.vertexHandles().select(m_dragHandlePosition);
    }
    return MoveResult::Continue;
  }

  // Catch all failure cases: no brushes were selected or vertices could not be added:
  return MoveResult::Deny;
}

void VertexTool::endMove()
{
  auto& map = m_document.map();

  VertexToolBase::endMove();
  map.edgeHandles().deselectAll();
  map.faceHandles().deselectAll();
  m_mode = Mode::Move;
}
void VertexTool::cancelMove()
{
  auto& map = m_document.map();

  VertexToolBase::cancelMove();
  map.edgeHandles().deselectAll();
  map.faceHandles().deselectAll();
  m_mode = Mode::Move;
}

bool VertexTool::allowAbsoluteSnapping() const
{
  return true;
}

vm::vec3d VertexTool::getHandlePosition(const mdl::Hit& hit) const
{
  contract_pre(hit.isMatch());
  contract_pre(hit.hasType(
    mdl::VertexHandleManager::HandleHitType | mdl::EdgeHandleManager::HandleHitType
    | mdl::FaceHandleManager::HandleHitType | PatchRowHitType | PatchColumnHitType));

  return hit.hasType(mdl::VertexHandleManager::HandleHitType) ? hit.target<vm::vec3d>()
         : hit.hasType(mdl::EdgeHandleManager::HandleHitType)
           ? std::get<1>(hit.target<mdl::EdgeHandleManager::HitData>())
         : hit.hasType(mdl::FaceHandleManager::HandleHitType)
           ? std::get<1>(hit.target<mdl::FaceHandleManager::HitData>())
         : hit.hasType(PatchRowHitType)
           ? hit.target<PatchRowHitData>().position
           : hit.target<PatchColumnHitData>().position;
}

std::string VertexTool::actionName() const
{
  switch (m_mode)
  {
  case Mode::Move:
    return kdl::str_plural(
      m_document.map().vertexHandles().selectedHandleCount(),
      "Move Vertex",
      "Move Vertices");
  case Mode::MovePatchRow:
    return "Move Patch Row";
  case Mode::MovePatchColumn:
    return "Move Patch Column";
  case Mode::SplitEdge:
    return "Split Edge";
  case Mode::SplitFace:
    return "Split Face";
    switchDefault();
  }
}

void VertexTool::removeSelection()
{
  contract_pre(canRemoveSelection());

  auto& map = m_document.map();
  auto handles = map.vertexHandles().selectedHandles();

  const auto commandName =
    kdl::str_plural(handles.size(), "Remove Brush Vertex", "Remove Brush Vertices");
  removeVertices(map, commandName, std::move(handles));
}

void VertexTool::renderGuide(
  render::RenderContext&,
  render::RenderBatch& renderBatch,
  const vm::vec3d& position) const
{
  m_guideRenderer.setPosition(position);
  m_guideRenderer.setColor(RgbaF{pref(Preferences::HandleColor).to<RgbF>(), 0.5f});
  renderBatch.add(&m_guideRenderer);
}

bool VertexTool::doActivate()
{
  VertexToolBase::doActivate();

  auto& map = m_document.map();
  map.edgeHandles().clear();
  map.faceHandles().clear();

  const auto& brushes = selectedBrushes();
  map.edgeHandles().addHandles(brushes);
  map.faceHandles().addHandles(brushes);

  m_mode = Mode::Move;
  return true;
}

bool VertexTool::doDeactivate()
{
  VertexToolBase::doDeactivate();

  auto& map = m_document.map();
  map.edgeHandles().clear();
  map.faceHandles().clear();
  return true;
}

void VertexTool::addHandles(const std::vector<mdl::Node*>& nodes)
{
  auto& map = m_document.map();

  VertexToolBase::addHandles(nodes, map.vertexHandles());
  VertexToolBase::addHandles(nodes, map.edgeHandles());
  VertexToolBase::addHandles(nodes, map.faceHandles());
}

void VertexTool::removeHandles(const std::vector<mdl::Node*>& nodes)
{
  auto& map = m_document.map();

  VertexToolBase::removeHandles(nodes, map.vertexHandles());
  VertexToolBase::removeHandles(nodes, map.edgeHandles());
  VertexToolBase::removeHandles(nodes, map.faceHandles());
}

void VertexTool::addHandles(mdl::BrushVertexCommandT<vm::vec3d>& command)
{
  auto& map = m_document.map();

  command.addHandles(map.vertexHandles());
  command.addHandles(map.edgeHandles());
  command.addHandles(map.faceHandles());
}

void VertexTool::removeHandles(mdl::BrushVertexCommandT<vm::vec3d>& command)
{
  auto& map = m_document.map();

  command.removeHandles(map.vertexHandles());
  command.removeHandles(map.edgeHandles());
  command.removeHandles(map.faceHandles());
}

void VertexTool::pickPatchRowsAndColumns(
  const vm::ray3d& pickRay,
  const render::Camera& camera,
  mdl::PickResult& pickResult) const
{
  const auto& selectedPatches = m_document.map().selection().allPatches();
  const auto handleRadius = double(pref(Preferences::HandleRadius));

  for (const auto* patchNode : selectedPatches)
  {
    const auto& patch = patchNode->patch();

    for (size_t row = 0u; row < patch.pointRowCount(); ++row)
    {
      auto bestRowHit = std::optional<mdl::Hit>{};
      for (size_t col = 0u; col + 1u < patch.pointColumnCount(); ++col)
      {
        const auto segment =
          vm::segment3d{patch.controlPoint(row, col).xyz(), patch.controlPoint(row, col + 1u).xyz()};

        const auto pointDistance = vm::distance(pickRay, segment);
        if (pointDistance.parallel)
        {
          continue;
        }

        if (const auto distance = camera.pickLineSegmentHandle(pickRay, segment, handleRadius))
        {
          const auto projectedPoint = vm::point_at_distance(segment, pointDistance.position2);
          const auto hitPoint = vm::point_at_distance(pickRay, *distance);
          const auto handlePosition =
            vm::squared_distance(projectedPoint, segment.start())
              <= vm::squared_distance(projectedPoint, segment.end())
              ? segment.start()
              : segment.end();
          const auto error = vm::squared_distance(pickRay, projectedPoint).distance;
          auto hit = mdl::Hit{
            PatchRowHitType,
            *distance,
            hitPoint,
            PatchRowHitData{patchNode, row, handlePosition},
            error};

          if (!bestRowHit || hit.error() < bestRowHit->error())
          {
            bestRowHit = std::move(hit);
          }
        }
      }

      if (bestRowHit)
      {
        pickResult.addHit(*bestRowHit);
      }
    }

    for (size_t col = 0u; col < patch.pointColumnCount(); ++col)
    {
      auto bestColumnHit = std::optional<mdl::Hit>{};
      for (size_t row = 0u; row + 1u < patch.pointRowCount(); ++row)
      {
        const auto segment =
          vm::segment3d{patch.controlPoint(row, col).xyz(), patch.controlPoint(row + 1u, col).xyz()};

        const auto pointDistance = vm::distance(pickRay, segment);
        if (pointDistance.parallel)
        {
          continue;
        }

        if (const auto distance = camera.pickLineSegmentHandle(pickRay, segment, handleRadius))
        {
          const auto projectedPoint = vm::point_at_distance(segment, pointDistance.position2);
          const auto hitPoint = vm::point_at_distance(pickRay, *distance);
          const auto handlePosition =
            vm::squared_distance(projectedPoint, segment.start())
              <= vm::squared_distance(projectedPoint, segment.end())
              ? segment.start()
              : segment.end();
          const auto error = vm::squared_distance(pickRay, projectedPoint).distance;
          auto hit = mdl::Hit{
            PatchColumnHitType,
            *distance,
            hitPoint,
            PatchColumnHitData{patchNode, col, handlePosition},
            error};

          if (!bestColumnHit || hit.error() < bestColumnHit->error())
          {
            bestColumnHit = std::move(hit);
          }
        }
      }

      if (bestColumnHit)
      {
        pickResult.addHit(*bestColumnHit);
      }
    }
  }
}

void VertexTool::selectPatchRow(const PatchRowHitData& hitData)
{
  if (hitData.patchNode == nullptr)
  {
    return;
  }

  const auto& patch = hitData.patchNode->patch();
  if (hitData.row >= patch.pointRowCount())
  {
    return;
  }

  auto& vertexHandles = m_document.map().vertexHandles();
  for (size_t col = 0u; col < patch.pointColumnCount(); ++col)
  {
    vertexHandles.select(patch.controlPoint(hitData.row, col).xyz());
  }
}

void VertexTool::selectPatchColumn(const PatchColumnHitData& hitData)
{
  if (hitData.patchNode == nullptr)
  {
    return;
  }

  const auto& patch = hitData.patchNode->patch();
  if (hitData.column >= patch.pointColumnCount())
  {
    return;
  }

  auto& vertexHandles = m_document.map().vertexHandles();
  for (size_t row = 0u; row < patch.pointRowCount(); ++row)
  {
    vertexHandles.select(patch.controlPoint(row, hitData.column).xyz());
  }
}

void VertexTool::resetModeAfterDeselection()
{
  if (!m_document.map().vertexHandles().anySelected())
  {
    m_mode = Mode::Move;
  }
}

} // namespace tb::ui
