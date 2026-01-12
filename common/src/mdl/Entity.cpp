/*
 Copyright (C) 2020 Kristian Duske

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

#include "Entity.h"

#include "mdl/EntityDefinition.h"
#include "mdl/EntityModel.h"
#include "mdl/EntityProperties.h"
#include "mdl/EntityPropertiesVariableStore.h"
#include "mdl/EntityRotation.h"
#include "mdl/ModelDefinition.h"
#include "mdl/PropertyDefinition.h"

#include "kd/ranges/to.h"
#include "kd/reflection_impl.h"
#include "kd/string_utils.h"
#include "kd/vector_utils.h"

#include "vm/mat.h"
#include "vm/mat_ext.h"
#include "vm/vec.h"
#include "vm/vec_io.h"

#include <algorithm>
#include <ranges>

namespace tb::mdl
{

void setDefaultProperties(
  const EntityDefinition& entityDefinition,
  Entity& entity,
  const SetDefaultPropertyMode mode)
{
  for (const auto& propertyDefinition : entityDefinition.propertyDefinitions)
  {
    if (const auto defaultValue = PropertyDefinition::defaultValue(propertyDefinition))
    {
      const auto hasProperty = entity.hasProperty(propertyDefinition.key);
      if (
        mode == SetDefaultPropertyMode::SetAll
        || (mode == SetDefaultPropertyMode::SetExisting && hasProperty)
        || (mode == SetDefaultPropertyMode::SetMissing && !hasProperty))
      {
        entity.addOrUpdateProperty(propertyDefinition.key, *defaultValue);
      }
    }
  }
}

kdl_reflect_impl(Entity);

const vm::bbox3d Entity::DefaultBounds = vm::bbox3d{8.0};

Entity::Entity() = default;

Entity::Entity(std::vector<EntityProperty> properties)
  : m_properties{std::move(properties)}
{
}

const std::vector<EntityProperty>& Entity::properties() const
{
  return m_properties;
}

Entity::Entity(const Entity& other) = default;
Entity::Entity(Entity&& other) = default;

Entity& Entity::operator=(const Entity& other) = default;
Entity& Entity::operator=(Entity&& other) = default;

Entity::~Entity() = default;

void Entity::setProperties(std::vector<EntityProperty> properties)
{
  m_properties = std::move(properties);

  m_cachedClassname = std::nullopt;
  m_cachedOrigin = std::nullopt;
  m_cachedRotation = std::nullopt;
  m_cachedModelTransformation = std::nullopt;
}

const std::vector<std::string>& Entity::protectedProperties() const
{
  return m_protectedProperties;
}

void Entity::setProtectedProperties(std::vector<std::string> protectedProperties)
{
  m_protectedProperties = std::move(protectedProperties);
}

bool Entity::pointEntity() const
{
  return m_pointEntity;
}

void Entity::setPointEntity(const bool pointEntity)
{
  if (m_pointEntity == pointEntity)
  {
    return;
  }

  m_pointEntity = pointEntity;

  m_cachedRotation = std::nullopt;
  m_cachedModelTransformation = std::nullopt;
}

const EntityDefinition* Entity::definition() const
{
  return m_definition.get();
}

const vm::bbox3d& Entity::definitionBounds() const
{
  return definition() && definition()->pointEntityDefinition
           ? definition()->pointEntityDefinition->bounds
           : DefaultBounds;
}

void Entity::setDefinition(const EntityDefinition* definition)
{
  if (m_definition.get() == definition)
  {
    return;
  }

  m_definition = AssetReference{definition};

  m_cachedRotation = std::nullopt;
  m_cachedModelTransformation = std::nullopt;
}

const EntityModel* Entity::model() const
{
  return m_model;
}

void Entity::setModel(const EntityModel* model)
{
  if (m_model == model)
  {
    return;
  }

  m_model = model;

  m_cachedRotation = std::nullopt;
  m_cachedModelTransformation = std::nullopt;
}

const EntityModelFrame* Entity::modelFrame() const
{
  if (!m_model || !m_model->data())
  {
    return nullptr;
  }

  return modelSpecification() | kdl::transform([&](const auto& modelSpecification) {
           return m_model->data()->frame(modelSpecification.frameIndex);
         })
         | kdl::value_or(nullptr);
}

const EntityModelFrame* Entity::modelFrame(
  const EntityPropertyConfig& config, const Entity* worldEntity) const
{
  if (!m_model || !m_model->data())
  {
    return nullptr;
  }

  return modelSpecification(config, worldEntity)
         | kdl::transform([&](const auto& modelSpecification) {
             return m_model->data()->frame(modelSpecification.frameIndex);
           })
         | kdl::value_or(nullptr);
}

Result<ModelSpecification> Entity::modelSpecification() const
{
  if (const auto* pointEntityDefinition = getPointEntityDefinition(definition()))
  {
    const auto variableStore = EntityPropertiesVariableStore{*this};
    return pointEntityDefinition->modelDefinition.modelSpecification(variableStore);
  }
  return ModelSpecification{};
}

Result<ModelSpecification> Entity::modelSpecification(
  const EntityPropertyConfig& config, const Entity* worldEntity) const
{
  if (const auto* pointEntityDefinition = getPointEntityDefinition(definition()))
  {
    if (worldEntity && !config.globalExpressionVariables.empty())
    {
      const auto variableStore = EntityPropertiesVariableStore{
        *this, worldEntity, config.globalExpressionVariables};
      return pointEntityDefinition->modelDefinition.modelSpecification(variableStore);
    }

    const auto variableStore = EntityPropertiesVariableStore{*this};
    return pointEntityDefinition->modelDefinition.modelSpecification(variableStore);
  }
  return ModelSpecification{};
}

const vm::mat4x4d& Entity::modelTransformation(
  const std::optional<el::ExpressionNode>& defaultModelScaleExpression) const
{
  if (!m_cachedModelTransformation)
  {
    if (const auto* pointDefinition = getPointEntityDefinition(definition()))
    {
      const auto variableStore = EntityPropertiesVariableStore{*this};
      const auto scale = safeGetModelScale(
        pointDefinition->modelDefinition, variableStore, defaultModelScaleExpression);
      m_cachedModelTransformation =
        vm::translation_matrix(origin()) * rotation() * vm::scaling_matrix(scale);
    }
    else
    {
      m_cachedModelTransformation = vm::mat4x4d::identity();
    }
  }
  return *m_cachedModelTransformation;
}

vm::mat4x4d Entity::modelTransformation(
  const EntityPropertyConfig& config,
  const Entity* worldEntity,
  const std::optional<el::ExpressionNode>& defaultModelScaleExpression) const
{
  if (!worldEntity || config.globalExpressionVariables.empty())
  {
    return modelTransformation(defaultModelScaleExpression);
  }

  if (const auto* pointDefinition = getPointEntityDefinition(definition()))
  {
    const auto variableStore = EntityPropertiesVariableStore{
      *this, worldEntity, config.globalExpressionVariables};
    const auto scale = safeGetModelScale(
      pointDefinition->modelDefinition, variableStore, defaultModelScaleExpression);
    return vm::translation_matrix(origin()) * rotation() * vm::scaling_matrix(scale);
  }
  return vm::mat4x4d::identity();
}

Result<DecalSpecification> Entity::decalSpecification() const
{
  if (const auto* pointDefinition = getPointEntityDefinition(definition()))
  {
    const auto variableStore = EntityPropertiesVariableStore{*this};
    return pointDefinition->decalDefinition.decalSpecification(variableStore);
  }
  return DecalSpecification{};
}

Result<DecalSpecification> Entity::decalSpecification(
  const EntityPropertyConfig& config, const Entity* worldEntity) const
{
  if (const auto* pointDefinition = getPointEntityDefinition(definition()))
  {
    if (worldEntity && !config.globalExpressionVariables.empty())
    {
      const auto variableStore = EntityPropertiesVariableStore{
        *this, worldEntity, config.globalExpressionVariables};
      return pointDefinition->decalDefinition.decalSpecification(variableStore);
    }

    const auto variableStore = EntityPropertiesVariableStore{*this};
    return pointDefinition->decalDefinition.decalSpecification(variableStore);
  }
  return DecalSpecification{};
}

void Entity::unsetEntityDefinitionAndModel()
{
  if (m_definition.get() == nullptr && m_model == nullptr)
  {
    return;
  }

  m_definition = AssetReference<EntityDefinition>{};
  m_model = nullptr;
  m_cachedRotation = std::nullopt;
  m_cachedModelTransformation = std::nullopt;
}

void Entity::addOrUpdateProperty(
  std::string key, std::string value, const bool defaultToProtected)
{
  auto it = findEntityProperty(m_properties, key);
  if (it != std::end(m_properties))
  {
    it->setValue(std::move(value));
  }
  else
  {
    m_properties.emplace_back(key, std::move(value));

    if (defaultToProtected && !kdl::vec_contains(m_protectedProperties, key))
    {
      m_protectedProperties.push_back(std::move(key));
    }
  }

  m_cachedClassname = std::nullopt;
  m_cachedOrigin = std::nullopt;
  m_cachedRotation = std::nullopt;
  m_cachedModelTransformation = std::nullopt;
}

bool Entity::updatePropertyValue(const size_t index, std::string value)
{
  if (index >= m_properties.size())
  {
    return false;
  }

  auto& property = m_properties[index];
  if (property.value() == value)
  {
    return true;
  }

  property.setValue(std::move(value));

  m_cachedClassname = std::nullopt;
  m_cachedOrigin = std::nullopt;
  m_cachedRotation = std::nullopt;
  m_cachedModelTransformation = std::nullopt;

  return true;
}

bool Entity::updatePropertyKey(const size_t index, std::string key)
{
  if (index >= m_properties.size())
  {
    return false;
  }

  auto& property = m_properties[index];
  if (property.key() == key)
  {
    return true;
  }

  property.setKey(std::move(key));

  m_cachedClassname = std::nullopt;
  m_cachedOrigin = std::nullopt;
  m_cachedRotation = std::nullopt;
  m_cachedModelTransformation = std::nullopt;

  return true;
}

bool Entity::removePropertyAt(const size_t index)
{
  if (index >= m_properties.size())
  {
    return false;
  }

  m_properties.erase(
    m_properties.begin() + static_cast<std::vector<EntityProperty>::difference_type>(index));

  m_cachedClassname = std::nullopt;
  m_cachedOrigin = std::nullopt;
  m_cachedRotation = std::nullopt;
  m_cachedModelTransformation = std::nullopt;

  return true;
}

void Entity::renameProperty(const std::string& oldKey, std::string newKey)
{
  if (oldKey == newKey)
  {
    return;
  }

  auto updated = false;
  for (auto& property : m_properties)
  {
    if (property.hasKey(oldKey))
    {
      property.setKey(newKey);
      updated = true;
    }
  }

  if (!updated)
  {
    return;
  }

  if (const auto protIt = std::ranges::find(m_protectedProperties, oldKey);
      protIt != std::end(m_protectedProperties))
  {
    if (!kdl::vec_contains(m_protectedProperties, newKey))
    {
      *protIt = newKey;
    }
    else
    {
      m_protectedProperties.erase(protIt);
    }
  }

  m_cachedClassname = std::nullopt;
  m_cachedOrigin = std::nullopt;
  m_cachedRotation = std::nullopt;
  m_cachedModelTransformation = std::nullopt;
}

void Entity::removeProperty(const std::string& key)
{
  const auto erasedPropertyCount =
    std::erase_if(m_properties, [&](const auto& property) { return property.hasKey(key); });

  if (erasedPropertyCount)
  {
    m_cachedClassname = std::nullopt;
    m_cachedOrigin = std::nullopt;
    m_cachedRotation = std::nullopt;
    m_cachedModelTransformation = std::nullopt;
  }
}

void Entity::removeNumberedProperty(const std::string& prefix)
{
  const auto erasedPropertyCount = std::erase_if(m_properties, [&](const auto& property) {
    return property.hasNumberedPrefix(prefix);
  });

  if (erasedPropertyCount)
  {
    m_cachedClassname = std::nullopt;
    m_cachedOrigin = std::nullopt;
    m_cachedRotation = std::nullopt;
    m_cachedModelTransformation = std::nullopt;
  }
}

bool Entity::hasProperty(const std::string& key) const
{
  return findEntityProperty(m_properties, key) != std::end(m_properties);
}

bool Entity::hasProperty(const std::string& key, const std::string& value) const
{
  return std::ranges::any_of(m_properties, [&](const auto& property) {
    return property.hasKeyAndValue(key, value);
  });
}

bool Entity::hasPropertyWithPrefix(
  const std::string& prefix, const std::string& value) const
{
  return std::ranges::any_of(m_properties, [&](const auto& property) {
    return property.hasPrefixAndValue(prefix, value);
  });
}

bool Entity::hasNumberedProperty(
  const std::string& prefix, const std::string& value) const
{
  return std::ranges::any_of(m_properties, [&](const auto& property) {
    return property.hasNumberedPrefixAndValue(prefix, value);
  });
}

const std::string* Entity::property(const std::string& key) const
{
  const auto it = findEntityProperty(m_properties, key);
  return it != std::end(m_properties) ? &it->value() : nullptr;
}

const EntityProperty* Entity::propertyAt(const size_t index) const
{
  return index < m_properties.size() ? &m_properties[index] : nullptr;
}

std::vector<std::string> Entity::propertyKeys() const
{
  return m_properties
         | std::views::transform([](const auto& property) { return property.key(); })
         | kdl::ranges::to<std::vector>();
}

const std::string& Entity::classname() const
{
  if (!m_cachedClassname)
  {
    const auto* classnameValue = property(EntityPropertyKeys::Classname);
    m_cachedClassname =
      classnameValue ? *classnameValue : EntityPropertyValues::NoClassname;
  }
  return *m_cachedClassname;
}

void Entity::setClassname(const std::string& classname)
{
  addOrUpdateProperty(EntityPropertyKeys::Classname, classname);
}


namespace
{
auto parseOrigin(const std::string* str)
{
  if (!str)
  {
    return vm::vec3d{0, 0, 0};
  }

  const auto parsed = vm::parse<double, 3>(*str);
  if (!parsed || vm::is_nan(*parsed))
  {
    return vm::vec3d{0, 0, 0};
  }

  return *parsed;
}
} // namespace

const vm::vec3d& Entity::origin() const
{
  if (!m_cachedOrigin)
  {
    const auto* originValue = property(EntityPropertyKeys::Origin);
    m_cachedOrigin = parseOrigin(originValue);
  }
  return *m_cachedOrigin;
}

void Entity::setOrigin(const vm::vec3d& origin)
{
  addOrUpdateProperty(
    EntityPropertyKeys::Origin, kdl::str_to_string(vm::correct(origin)));
}

const vm::mat4x4d& Entity::rotation() const
{
  if (!m_cachedRotation)
  {
    m_cachedRotation = entityRotation(*this);
  }
  return *m_cachedRotation;
}

std::vector<EntityProperty> Entity::propertiesWithKey(const std::string& key) const
{
  return m_properties
         | std::views::filter([&](const auto& property) { return property.hasKey(key); })
         | kdl::ranges::to<std::vector>();
}

std::vector<EntityProperty> Entity::propertiesWithPrefix(const std::string& prefix) const
{
  return m_properties | std::views::filter([&](const auto& property) {
           return property.hasPrefix(prefix);
         })
         | kdl::ranges::to<std::vector>();
}

std::vector<EntityProperty> Entity::numberedProperties(const std::string& prefix) const
{
  return m_properties | std::views::filter([&](const auto& property) {
           return property.hasNumberedPrefix(prefix);
         })
         | kdl::ranges::to<std::vector>();
}

void Entity::transform(const vm::mat4x4d& transformation, const bool updateAngleProperty)
{
  if (m_pointEntity)
  {
    const auto offset = definitionBounds().center();
    const auto center = origin() + offset;
    const auto transformedCenter = transformation * center;
    const auto newOrigin = transformedCenter - offset;
    if (origin() != newOrigin)
    {
      setOrigin(transformedCenter - offset);
    }
  }

  // applying rotation has side effects (e.g. normalizing "angles")
  // so only do it if there is actually some rotation.
  const auto rotation = vm::strip_translation(transformation);
  if (rotation != vm::mat4x4d::identity())
  {
    // applyRotation does not read the origin, so it's ok that it's already updated now
    if (updateAngleProperty)
    {
      applyEntityRotation(*this, rotation);
    }
  }
}

} // namespace tb::mdl
