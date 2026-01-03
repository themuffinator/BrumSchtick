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

#include "vm/vec.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace tb::mdl
{
class BrushFace;
class Map;
class PatchNode;
} // namespace tb::mdl

namespace tb::render
{

class LightPreview
{
public:
  struct Light
  {
    vm::vec3f position;
    vm::vec3f direction;
    vm::vec3f color;
    float intensity = 0.0f;
    float radius = 0.0f;
    float coneCos = -1.0f;
    int style = 0;
    int falloff = 0;
    bool isSurface = false;
  };

private:
  mdl::Map& m_map;
  std::vector<Light> m_lights;
  vm::vec3f m_ambient;
  uint64_t m_revision = 0;
  uint32_t m_styleFrame = 0;
  std::unordered_map<int, std::string> m_stylePatterns;

public:
  LightPreview(mdl::Map& map, float timeSeconds);

  const std::vector<Light>& lights() const;
  const vm::vec3f& ambient() const;
  uint64_t revision() const;

  vm::vec3f lightingAt(
    const vm::vec3f& position,
    const vm::vec3f& normal,
    const mdl::BrushFace* ignoreFace = nullptr,
    const mdl::PatchNode* ignorePatch = nullptr) const;

private:
  void collectStylePatterns();
  void collectPointLights();
  void collectSurfaceLights();
  float styleIntensity(int style) const;
  bool isOccluded(
    const vm::vec3f& from,
    const vm::vec3f& to,
    const mdl::BrushFace* ignoreFace,
    const mdl::PatchNode* ignorePatch) const;
};

} // namespace tb::render
