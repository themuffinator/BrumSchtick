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

#include "EntityPropertiesVariableStore.h"

#include "el/Exceptions.h"
#include "el/Value.h"
#include "mdl/Entity.h"
#include "mdl/EntityProperties.h"

#include <algorithm>
#include <string>

namespace tb::mdl
{

EntityPropertiesVariableStore::EntityPropertiesVariableStore(const Entity& entity)
  : m_entity{entity}
{
}

EntityPropertiesVariableStore::EntityPropertiesVariableStore(
  const Entity& entity,
  const Entity* worldEntity,
  const std::vector<GlobalExpressionVariable>& globalExpressionVariables)
  : m_entity{entity}
  , m_worldEntity{worldEntity}
  , m_globalExpressionVariables{&globalExpressionVariables}
{
}

el::VariableStore* EntityPropertiesVariableStore::clone() const
{
  if (m_worldEntity && m_globalExpressionVariables)
  {
    return new EntityPropertiesVariableStore{
      m_entity, m_worldEntity, *m_globalExpressionVariables};
  }
  return new EntityPropertiesVariableStore{m_entity};
}

size_t EntityPropertiesVariableStore::size() const
{
  return names().size();
}

el::Value EntityPropertiesVariableStore::value(const std::string& name) const
{
  const auto* entityValue = m_entity.property(name);

  if (m_worldEntity && m_globalExpressionVariables)
  {
    const auto it = std::ranges::find_if(
      *m_globalExpressionVariables, [&](const auto& entry) { return entry.key == name; });
    if (it != m_globalExpressionVariables->end())
    {
      const auto* worldValue = m_worldEntity->property(name);
      if (it->overrideValue)
      {
        if (worldValue)
        {
          return el::Value{*worldValue};
        }
        if (entityValue)
        {
          return el::Value{*entityValue};
        }
        return el::Value{""};
      }

      if (entityValue)
      {
        return el::Value{*entityValue};
      }
      if (worldValue)
      {
        return el::Value{*worldValue};
      }
      return el::Value{""};
    }
  }

  return entityValue ? el::Value{*entityValue} : el::Value{""};
}

std::vector<std::string> EntityPropertiesVariableStore::names() const
{
  auto names = m_entity.propertyKeys();
  if (m_worldEntity && m_globalExpressionVariables)
  {
    for (const auto& entry : *m_globalExpressionVariables)
    {
      if (std::ranges::find(names, entry.key) == names.end())
      {
        names.push_back(entry.key);
      }
    }
  }
  return names;
}

void EntityPropertiesVariableStore::set(const std::string, const el::Value) {}

} // namespace tb::mdl
