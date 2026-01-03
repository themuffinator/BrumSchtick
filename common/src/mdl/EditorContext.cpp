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

#include "EditorContext.h"

#include "PreferenceManager.h"
#include "Preferences.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityDefinition.h"
#include "mdl/EntityNode.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Node.h"
#include "mdl/PatchNode.h"
#include "mdl/WorldNode.h"

#include "kd/contracts.h"
#include "kd/string_compare.h"
#include "kd/string_utils.h"

#include <algorithm>

namespace tb::mdl
{

namespace
{
bool isMaterialKey(std::string_view key)
{
  return kdl::ci::str_is_equal(key, "texture")
         || kdl::ci::str_is_equal(key, "material")
         || kdl::ci::str_is_equal(key, "mat");
}

bool keyMatches(std::string_view key, std::string_view pattern)
{
  if (kdl::ci::str_is_equal(key, pattern))
  {
    return true;
  }

  if (key.size() <= pattern.size())
  {
    return false;
  }

  if (!kdl::ci::str_is_prefix(key, pattern))
  {
    return false;
  }

  const auto suffix = key.substr(pattern.size());
  return std::ranges::all_of(
    suffix, [](const char c) { return c >= '0' && c <= '9'; });
}

} // namespace

EditorContext::EditorContext()
{
  reset();
}

void EditorContext::reset()
{
  m_hiddenTags = 0;
  m_hiddenEntityDefinitions.reset();
  m_searchText.clear();
  m_searchTerms.clear();
  m_blockSelection = false;
  m_currentGroup = nullptr;
  m_currentLayer = nullptr;
}

TagType::Type EditorContext::hiddenTags() const
{
  return m_hiddenTags;
}

void EditorContext::setHiddenTags(const TagType::Type hiddenTags)
{
  if (hiddenTags != m_hiddenTags)
  {
    m_hiddenTags = hiddenTags;
    editorContextDidChangeNotifier();
  }
}

const std::string& EditorContext::searchText() const
{
  return m_searchText;
}

void EditorContext::setSearchText(std::string searchText)
{
  if (searchText != m_searchText)
  {
    m_searchText = std::move(searchText);
    m_searchTerms = parseSearchTerms(m_searchText);
    editorContextDidChangeNotifier();
  }
}

bool EditorContext::entityDefinitionHidden(const EntityNodeBase& entityNode) const
{
  return entityNode.entity().definition()
         && entityDefinitionHidden(*entityNode.entity().definition());
}

bool EditorContext::entityDefinitionHidden(const EntityDefinition& definition) const
{
  return m_hiddenEntityDefinitions[definition.index];
}

void EditorContext::setEntityDefinitionHidden(
  const EntityDefinition& definition, const bool hidden)
{
  if (entityDefinitionHidden(definition) != hidden)
  {
    m_hiddenEntityDefinitions[definition.index] = hidden;
    editorContextDidChangeNotifier();
  }
}

bool EditorContext::blockSelection() const
{
  return m_blockSelection;
}

void EditorContext::setBlockSelection(const bool blockSelection)
{
  if (m_blockSelection != blockSelection)
  {
    m_blockSelection = blockSelection;
    editorContextDidChangeNotifier();
  }
}

LayerNode* EditorContext::currentLayer() const
{
  return m_currentLayer;
}

void EditorContext::setCurrentLayer(LayerNode* layerNode)
{
  m_currentLayer = layerNode;
}

GroupNode* EditorContext::currentGroup() const
{
  return m_currentGroup;
}

void EditorContext::pushGroup(GroupNode& groupNode)
{
  contract_pre(!m_currentGroup || groupNode.containingGroup() == m_currentGroup);

  if (m_currentGroup)
  {
    m_currentGroup->close();
  }
  m_currentGroup = &groupNode;
  m_currentGroup->open();
}

void EditorContext::popGroup()
{
  contract_pre(m_currentGroup != nullptr);

  m_currentGroup->close();
  m_currentGroup = m_currentGroup->containingGroup();
  if (m_currentGroup)
  {
    m_currentGroup->open();
  }
}

bool EditorContext::visible(const Node& node) const
{
  return node.accept(kdl::overload(
    [&](const WorldNode* worldNode) { return visible(*worldNode); },
    [&](const LayerNode* layerNode) { return visible(*layerNode); },
    [&](const GroupNode* groupNode) { return visible(*groupNode); },
    [&](const EntityNode* entityNode) { return visible(*entityNode); },
    [&](const BrushNode* brushNode) { return visible(*brushNode); },
    [&](const PatchNode* patchNode) { return visible(*patchNode); }));
}

bool EditorContext::visible(const WorldNode& worldNode) const
{
  return worldNode.visible();
}

bool EditorContext::visible(const LayerNode& layerNode) const
{
  return layerNode.visible();
}

bool EditorContext::visible(const GroupNode& groupNode) const
{
  if (groupNode.selected())
  {
    return true;
  }
  if (!anyChildVisible(groupNode))
  {
    return false;
  }
  return groupNode.visible();
}

bool EditorContext::visible(const EntityNode& entityNode) const
{
  if (entityNode.selected())
  {
    return true;
  }

  if (!entityNode.entity().pointEntity())
  {
    return anyChildVisible(entityNode);
  }

  if (!entityNode.visible())
  {
    return false;
  }

  if (entityNode.entity().pointEntity() && !pref(Preferences::ShowPointEntities))
  {
    return false;
  }

  if (entityDefinitionHidden(entityNode))
  {
    return false;
  }

  return !searchActive() || matchesSearch(entityNode);
}

bool EditorContext::visible(const BrushNode& brushNode) const
{
  if (brushNode.selected())
  {
    return true;
  }

  if (!pref(Preferences::ShowBrushes))
  {
    return false;
  }

  if (brushNode.hasTag(m_hiddenTags))
  {
    return false;
  }

  if (brushNode.allFacesHaveAnyTagInMask(m_hiddenTags))
  {
    return false;
  }

  if (const auto* entityNode = brushNode.entity();
      entityNode && entityDefinitionHidden(*entityNode))
  {
    return false;
  }

  return brushNode.visible() && (!searchActive() || matchesSearch(brushNode));
}

bool EditorContext::visible(const BrushNode& brushNode, const BrushFace& face) const
{
  return visible(brushNode) && !face.hasTag(m_hiddenTags);
}

bool EditorContext::visible(const PatchNode& patchNode) const
{
  if (patchNode.selected())
  {
    return true;
  }

  if (patchNode.hasTag(m_hiddenTags))
  {
    return false;
  }

  return patchNode.visible() && (!searchActive() || matchesSearch(patchNode));
}

bool EditorContext::searchActive() const
{
  return !m_searchTerms.empty();
}

bool EditorContext::matchesSearch(const EntityNode& entityNode) const
{
  const auto& entity = entityNode.entity();
  return std::ranges::all_of(
    m_searchTerms, [&](const auto& term) { return matchesEntityTerm(term, entity); });
}

bool EditorContext::matchesSearch(const BrushNode& brushNode) const
{
  const auto* entityNode = brushNode.entity();

  const auto matchesMaterial = [&](const auto& term) {
    for (size_t i = 0; i < brushNode.brush().faceCount(); ++i)
    {
      const auto& face = brushNode.brush().face(i);
      if (matchesMaterialTerm(face.attributes().materialName(), term))
      {
        return true;
      }
    }
    return false;
  };

  const auto matchesEntity = [&](const auto& term) {
    if (!entityNode)
    {
      return false;
    }
    return matchesEntityTerm(term, entityNode->entity());
  };

  return std::ranges::all_of(m_searchTerms, [&](const auto& term) {
    const auto materialTerm =
      term.kind == SearchTerm::Kind::Any
      || (term.kind == SearchTerm::Kind::KeyValue && isMaterialKey(term.key));
    const auto entityTerm =
      term.kind == SearchTerm::Kind::Any
      || (term.kind == SearchTerm::Kind::KeyValue && !isMaterialKey(term.key));

    const auto materialMatches = materialTerm && matchesMaterial(term.value);
    if (materialMatches)
    {
      return true;
    }

    return entityTerm && matchesEntity(term);
  });
}

bool EditorContext::matchesSearch(const PatchNode& patchNode) const
{
  const auto* entityNode = patchNode.entity();

  return std::ranges::all_of(m_searchTerms, [&](const auto& term) {
    const auto materialTerm =
      term.kind == SearchTerm::Kind::Any
      || (term.kind == SearchTerm::Kind::KeyValue && isMaterialKey(term.key));
    const auto entityTerm =
      term.kind == SearchTerm::Kind::Any
      || (term.kind == SearchTerm::Kind::KeyValue && !isMaterialKey(term.key));

    const auto materialMatches =
      materialTerm && matchesMaterialTerm(patchNode.patch().materialName(), term.value);
    if (materialMatches)
    {
      return true;
    }

    return entityTerm && entityNode && matchesEntityTerm(term, entityNode->entity());
  });
}

bool EditorContext::matchesEntityTerm(const SearchTerm& term, const Entity& entity) const
{
  if (term.kind == SearchTerm::Kind::KeyValue)
  {
    if (isMaterialKey(term.key))
    {
      return false;
    }

    return std::ranges::any_of(entity.properties(), [&](const auto& property) {
      return keyMatches(property.key(), term.key)
             && kdl::ci::str_contains(property.value(), term.value);
    });
  }

  return std::ranges::any_of(entity.properties(), [&](const auto& property) {
    return kdl::ci::str_contains(property.value(), term.value);
  });
}

bool EditorContext::matchesMaterialTerm(
  const std::string_view materialName, const std::string_view term) const
{
  if (term.find('/') == std::string_view::npos)
  {
    const auto pos = materialName.find_last_of('/');
    if (pos != std::string_view::npos)
    {
      return kdl::ci::str_contains(materialName.substr(pos + 1), term);
    }
  }

  return kdl::ci::str_contains(materialName, term);
}

std::vector<EditorContext::SearchTerm> EditorContext::parseSearchTerms(
  const std::string_view text)
{
  auto terms = std::vector<SearchTerm>{};

  for (const auto& token : kdl::str_split(text, " "))
  {
    const auto separator = token.find_first_of("=:");
    if (separator != std::string::npos && separator > 0 && separator + 1 < token.size())
    {
      terms.push_back(SearchTerm{
        SearchTerm::Kind::KeyValue,
        token.substr(0, separator),
        token.substr(separator + 1)});
    }
    else if (!token.empty())
    {
      terms.push_back(SearchTerm{SearchTerm::Kind::Any, std::string{}, token});
    }
  }

  return terms;
}

bool EditorContext::anyChildVisible(const Node& node) const
{
  const auto& children = node.children();
  return std::ranges::any_of(
    children, [this](const Node* childNode) { return visible(*childNode); });
}

bool EditorContext::editable(const Node& node) const
{
  return node.editable();
}

bool EditorContext::editable(const BrushNode& brushNode, const BrushFace&) const
{
  return editable(brushNode);
}

bool EditorContext::selectable(const Node& node) const
{
  return node.accept(kdl::overload(
    [&](const WorldNode* worldNode) { return selectable(*worldNode); },
    [&](const LayerNode* layerNode) { return selectable(*layerNode); },
    [&](const GroupNode* groupNode) { return selectable(*groupNode); },
    [&](const EntityNode* entityNode) { return selectable(*entityNode); },
    [&](const BrushNode* brushNode) { return selectable(*brushNode); },
    [&](const PatchNode* patchNode) { return selectable(*patchNode); }));
}

bool EditorContext::selectable(const WorldNode&) const
{
  return false;
}

bool EditorContext::selectable(const LayerNode&) const
{
  return false;
}

bool EditorContext::selectable(const GroupNode& groupNode) const
{
  return visible(groupNode) && editable(groupNode) && !groupNode.opened()
         && inOpenGroup(groupNode);
}

bool EditorContext::selectable(const EntityNode& entityNode) const
{
  return visible(entityNode) && editable(entityNode) && !entityNode.hasChildren()
         && inOpenGroup(entityNode);
}

bool EditorContext::selectable(const BrushNode& brushNode) const
{
  return visible(brushNode) && editable(brushNode) && inOpenGroup(brushNode);
}

bool EditorContext::selectable(const BrushNode& brushNode, const BrushFace& face) const
{
  return visible(brushNode, face) && editable(brushNode, face);
}

bool EditorContext::selectable(const PatchNode& patchNode) const
{
  return visible(patchNode) && editable(patchNode) && inOpenGroup(patchNode);
}

bool EditorContext::canChangeSelection() const
{
  return !m_blockSelection;
}

bool EditorContext::inOpenGroup(const Object& object) const
{
  return object.containingGroupOpened();
}

} // namespace tb::mdl
