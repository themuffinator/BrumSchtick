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

#include "render/LightPreview.h"

#include "Color.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityColorPropertyValue.h"
#include "mdl/EntityNode.h"
#include "mdl/GameInfo.h"
#include "mdl/HitAdapter.h"
#include "mdl/HitFilter.h"
#include "mdl/Map.h"
#include "mdl/Map_Picking.h"
#include "mdl/Material.h"
#include "mdl/PatchNode.h"
#include "mdl/PickResult.h"
#include "mdl/Texture.h"
#include "mdl/WorldNode.h"

#include "kd/overload.h"
#include "kd/string_compare.h"
#include "kd/string_utils.h"

#include "vm/scalar.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <optional>

namespace tb::render
{
namespace
{
constexpr float LightUnitScale = 1.0f / 300.0f;
constexpr float MinLightRadius = 32.0f;
constexpr float StyleFramesPerSecond = 10.0f;

std::optional<float> parseFloat(const std::string& value)
{
  return kdl::str_to_float(value);
}

std::optional<int> parseInt(const std::string& value)
{
  return kdl::str_to_int(value);
}

std::optional<vm::vec3f> parseVec3(const std::string& value)
{
  const auto parts = kdl::str_split(value, " ");
  if (parts.size() < 3)
  {
    return std::nullopt;
  }

  auto x = kdl::str_to_float(parts[0]);
  auto y = kdl::str_to_float(parts[1]);
  auto z = kdl::str_to_float(parts[2]);
  if (!x || !y || !z)
  {
    return std::nullopt;
  }

  return vm::vec3f{*x, *y, *z};
}

std::optional<mdl::EntityColorPropertyValue> parseColorProperty(
  const mdl::Entity& entity, const std::string& key)
{
  if (const auto* value = entity.property(key))
  {
    auto result = mdl::parseEntityColorPropertyValue(entity.definition(), key, *value);
    if (result)
    {
      return result.value();
    }
  }
  return std::nullopt;
}

vm::vec3f colorToVec3f(const RgbF& color)
{
  return vm::vec3f{
    color.get<ColorChannel::r>(),
    color.get<ColorChannel::g>(),
    color.get<ColorChannel::b>()};
}

vm::vec3f colorToVec3f(const Rgb& color)
{
  return colorToVec3f(color.to<RgbF>());
}

int parseStyleIndex(const mdl::Entity& entity)
{
  if (const auto* styleValue = entity.property("style"))
  {
    return kdl::str_to_int(*styleValue).value_or(0);
  }
  return 0;
}

float parseIntensity(const mdl::Entity& entity, const std::optional<float>& extraIntensity)
{
  if (const auto* lightValue = entity.property("light"))
  {
    if (auto parsed = parseFloat(*lightValue))
    {
      return std::max(0.0f, *parsed);
    }
  }

  if (extraIntensity)
  {
    return std::max(0.0f, *extraIntensity);
  }

  return 300.0f;
}

int parseFalloff(const mdl::Entity& entity)
{
  if (const auto* delayValue = entity.property("delay"))
  {
    if (auto parsed = parseInt(*delayValue))
    {
      return *parsed;
    }
  }
  return 0;
}

float parseWait(const mdl::Entity& entity)
{
  if (const auto* waitValue = entity.property("wait"))
  {
    if (auto parsed = parseFloat(*waitValue))
    {
      return std::max(0.01f, *parsed);
    }
  }
  return 1.0f;
}

float attenuationFor(const LightPreview::Light& light, const float distance)
{
  if (light.falloff == 3)
  {
    return 1.0f;
  }

  const auto radius = std::max(light.radius, MinLightRadius);
  const auto scaled = distance / radius;

  switch (light.falloff)
  {
  case 1:
    return 1.0f / (1.0f + scaled);
  case 2:
  case 5:
    return 1.0f / (1.0f + scaled * scaled);
  default:
    return std::max(0.0f, 1.0f - scaled);
  }
}

float intensityFromStyleChar(const char value)
{
  const auto clamped = std::clamp(value, 'a', 'z');
  return float(clamped - 'a') / 25.0f;
}

} // namespace

LightPreview::LightPreview(mdl::Map& map, const float timeSeconds)
  : m_map{map}
{
  m_styleFrame =
    static_cast<uint32_t>(std::floor(std::max(0.0f, timeSeconds) * StyleFramesPerSecond));
  m_revision = (static_cast<uint64_t>(m_map.modificationCount()) << 32) | m_styleFrame;

  m_ambient = vm::vec3f{0.0f, 0.0f, 0.0f};
  const auto& worldEntity = m_map.worldNode().entity();
  if (const auto* ambientValue = worldEntity.property("_ambient"))
  {
    if (auto ambientFloat = parseFloat(*ambientValue))
    {
      const auto scaled = std::max(0.0f, *ambientFloat) * LightUnitScale;
      m_ambient = vm::vec3f{scaled, scaled, scaled};
    }
  }
  else if (const auto* ambientValue = worldEntity.property("light"))
  {
    if (auto ambientFloat = parseFloat(*ambientValue))
    {
      const auto scaled = std::max(0.0f, *ambientFloat) * LightUnitScale;
      m_ambient = vm::vec3f{scaled, scaled, scaled};
    }
  }

  collectStylePatterns();
  collectPointLights();
  collectSurfaceLights();
}

const std::vector<LightPreview::Light>& LightPreview::lights() const
{
  return m_lights;
}

const vm::vec3f& LightPreview::ambient() const
{
  return m_ambient;
}

uint64_t LightPreview::revision() const
{
  return m_revision;
}

vm::vec3f LightPreview::lightingAt(
  const vm::vec3f& position,
  const vm::vec3f& normal,
  const mdl::BrushFace* ignoreFace,
  const mdl::PatchNode* ignorePatch) const
{
  auto result = m_ambient;
  const auto safeNormal = vm::normalize(normal);

  for (const auto& light : m_lights)
  {
    const auto toLight = light.position - position;
    const auto distance = vm::length(toLight);
    if (distance <= 0.001f)
    {
      continue;
    }

    const auto lightDir = toLight / distance;
    if (light.isSurface && vm::dot(light.direction, lightDir) <= 0.0f)
    {
      continue;
    }

    const auto ndotl = std::max(0.0f, vm::dot(safeNormal, lightDir));
    if (ndotl <= 0.0f)
    {
      continue;
    }

    if (light.radius > 0.0f && distance > light.radius * 1.25f)
    {
      continue;
    }

    const auto from = position + safeNormal * 0.1f;
    if (isOccluded(from, light.position, ignoreFace, ignorePatch))
    {
      continue;
    }

    const auto falloff = attenuationFor(light, distance);
    const auto style = styleIntensity(light.style);
    const auto scaledIntensity = light.intensity * LightUnitScale * style;
    result = result + light.color * (scaledIntensity * falloff * ndotl);
  }

  return vm::clamp(result, vm::vec3f{0.0f, 0.0f, 0.0f}, vm::vec3f{1.0f, 1.0f, 1.0f});
}

void LightPreview::collectStylePatterns()
{
  m_stylePatterns = {
    {1, "mmnmmommommnonmmonqnmmo"},
    {2, "abcdefghijklmnopqrstuvwxyzyxwvutsrqponmlkjihgfedcba"},
    {3, "mmmmmaaaaammmmmaaaaaabcdefgabcdefg"},
    {4, "mamamamamama"},
    {5, "jklmnopqrstuvwxyzyxwvutsrqponmlkj"},
    {6, "nmonqnmomnmomomno"},
    {7, "mmmaaaabcdefgmmmmaaaammmaamm"},
    {8, "mmmaaammmaaammmabcdefaaaammmmabcdefmmmaaaa"},
    {9, "aaaaaaaazzzzzzzz"},
    {10, "mmamammmmammamamaaamammma"},
    {11, "abcdefghijklmnopqrrqponmlkjihgfedcba"},
  };

  for (const auto& property : m_map.worldNode().entity().properties())
  {
    if (!property.hasPrefix("style"))
    {
      continue;
    }

    const auto indexPart = property.key().substr(5);
    if (indexPart.empty())
    {
      continue;
    }

    if (auto styleIndex = kdl::str_to_int(indexPart))
    {
      if (*styleIndex > 0)
      {
        m_stylePatterns[*styleIndex] = property.value();
      }
    }
  }
}

void LightPreview::collectPointLights()
{
  const auto& editorContext = m_map.editorContext();
  auto visitor = kdl::overload(
    [&](auto&& thisLambda, mdl::WorldNode* world) { world->visitChildren(thisLambda); },
    [&](auto&& thisLambda, mdl::LayerNode* layer) { layer->visitChildren(thisLambda); },
    [&](auto&& thisLambda, mdl::GroupNode* group) { group->visitChildren(thisLambda); },
    [&](auto&& thisLambda, mdl::EntityNode* entityNode) {
      if (!editorContext.visible(*entityNode))
      {
        return;
      }

      const auto& entity = entityNode->entity();
      const auto& classname = entity.classname();
      if (kdl::cs::str_is_prefix(classname, "light"))
      {
        auto colorValue = parseColorProperty(entity, "_light")
                            .value_or(parseColorProperty(entity, "_color")
                                        .value_or(mdl::EntityColorPropertyValue{
                                          Rgb{RgbF{1.0f, 1.0f, 1.0f}}, {}}));

        auto extraIntensity = colorValue.extraComponents.empty()
                                ? std::optional<float>{}
                                : std::optional<float>{colorValue.extraComponents.front()};

        Light light{};
        light.position = vm::vec3f{entity.origin()};
        light.color = colorToVec3f(colorValue.color);
        light.intensity = parseIntensity(entity, extraIntensity);
        light.radius = std::max(MinLightRadius, light.intensity * parseWait(entity));
        light.falloff = parseFalloff(entity);
        light.style = parseStyleIndex(entity);
        m_lights.push_back(light);
      }

      entityNode->visitChildren(thisLambda);
    },
    [&](mdl::BrushNode*) {},
    [&](mdl::PatchNode*) {});

  m_map.worldNode().accept(visitor);
}

void LightPreview::collectSurfaceLights()
{
  const auto& surfaceFlags = m_map.gameInfo().gameConfig.faceAttribsConfig.surfaceFlags;
  const auto surfaceLightFlag = surfaceFlags.flagValue("light");
  if (surfaceLightFlag == 0)
  {
    return;
  }

  const auto& editorContext = m_map.editorContext();
  auto visitor = kdl::overload(
    [&](auto&& thisLambda, mdl::WorldNode* world) { world->visitChildren(thisLambda); },
    [&](auto&& thisLambda, mdl::LayerNode* layer) { layer->visitChildren(thisLambda); },
    [&](auto&& thisLambda, mdl::GroupNode* group) { group->visitChildren(thisLambda); },
    [&](auto&& thisLambda, mdl::EntityNode* entityNode) {
      if (editorContext.visible(*entityNode))
      {
        entityNode->visitChildren(thisLambda);
      }
    },
    [&](mdl::BrushNode* brushNode) {
      if (!editorContext.visible(*brushNode))
      {
        return;
      }

      for (const auto& face : brushNode->brush().faces())
      {
        if ((face.resolvedSurfaceFlags() & surfaceLightFlag) == 0)
        {
          continue;
        }

        const auto surfaceValue = face.resolvedSurfaceValue();
        if (surfaceValue <= 0.0f)
        {
          continue;
        }

        auto color = vm::vec3f{1.0f, 1.0f, 1.0f};
        if (const auto faceColor = face.resolvedColor())
        {
          color = colorToVec3f(faceColor->to<RgbF>());
        }
        else if (const auto* texture = getTexture(face.material()))
        {
          color = colorToVec3f(texture->averageColor().to<RgbF>());
        }

        Light light{};
        light.position = vm::vec3f{face.center()};
        light.direction = vm::vec3f{face.boundary().normal};
        light.color = color;
        light.intensity = surfaceValue;
        light.radius =
          std::max(MinLightRadius, static_cast<float>(std::sqrt(face.area())) * 4.0f);
        light.falloff = 0;
        light.isSurface = true;
        m_lights.push_back(light);
      }
    },
    [&](mdl::PatchNode*) {});

  m_map.worldNode().accept(visitor);
}

float LightPreview::styleIntensity(const int style) const
{
  if (style <= 0)
  {
    return 1.0f;
  }

  const auto it = m_stylePatterns.find(style);
  if (it == m_stylePatterns.end() || it->second.empty())
  {
    return 1.0f;
  }

  const auto& pattern = it->second;
  const auto index = m_styleFrame % static_cast<uint32_t>(pattern.size());
  return intensityFromStyleChar(static_cast<char>(std::tolower(pattern[index])));
}

bool LightPreview::isOccluded(
  const vm::vec3f& from,
  const vm::vec3f& to,
  const mdl::BrushFace* ignoreFace,
  const mdl::PatchNode* ignorePatch) const
{
  const auto dir = vm::vec3d{to} - vm::vec3d{from};
  const auto dist = vm::length(dir);
  if (dist <= 0.001)
  {
    return false;
  }

  const auto ray = vm::ray3d{vm::vec3d{from}, vm::normalize(dir)};
  auto pickResult = mdl::PickResult::byDistance();
  mdl::pick(m_map, ray, pickResult);

  const auto typeFilter =
    mdl::HitFilters::type(mdl::BrushNode::BrushHitType | mdl::PatchNode::PatchHitType);
  for (const auto& hit : pickResult.all(typeFilter))
  {
    if (hit.distance() >= dist - 0.2)
    {
      break;
    }

    if (ignoreFace)
    {
      if (const auto faceHandle = mdl::hitToFaceHandle(hit))
      {
        if (&faceHandle->face() == ignoreFace)
        {
          continue;
        }
      }
    }

    if (ignorePatch && hit.hasType(mdl::PatchNode::PatchHitType))
    {
      if (hit.target<mdl::PatchNode*>() == ignorePatch)
      {
        continue;
      }
    }

    if (hit.distance() > 0.2)
    {
      return true;
    }
  }

  return false;
}

} // namespace tb::render
