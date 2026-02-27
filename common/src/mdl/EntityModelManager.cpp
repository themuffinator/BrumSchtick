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

#include "EntityModelManager.h"

#include "Logger.h"
#include "io/LoadEntityModel.h"
#include "io/LoadMaterialCollections.h"
#include "io/LoadShaders.h"
#include "io/MaterialUtils.h"
#include "mdl/EntityModel.h"
#include "mdl/GameConfig.h"
#include "mdl/GameInfo.h"
#include "mdl/Quake3Shader.h"
#include "render/MaterialIndexRangeRenderer.h"

#include "kd/contracts.h"
#include "kd/path_utils.h"
#include "kd/ranges/to.h"
#include "kd/result.h"

#include <algorithm>

namespace tb::mdl
{
namespace
{
constexpr size_t TextureResourceCacheMaxEntries = 2048u;

std::filesystem::path normalizeMaterialCacheKey(const std::filesystem::path& materialPath)
{
  return kdl::path_to_lower(materialPath.lexically_normal());
}
} // namespace

EntityModelManager::EntityModelManager(
  const GameInfo& gameInfo,
  const fs::FileSystem& gameFileSystem,
  CreateEntityModelDataResource createResource,
  Logger& logger)
  : m_gameInfo{gameInfo}
  , m_gameFileSystem{gameFileSystem}
  , m_createResource{std::move(createResource)}
  , m_logger{logger}
  , m_shaders{std::make_shared<const std::vector<Quake3Shader>>()}
{
}

EntityModelManager::~EntityModelManager()
{
  clear();
}

void EntityModelManager::clear()
{
  m_renderers.clear();
  m_models.clear();
  m_rendererMismatches.clear();
  m_shaders = std::make_shared<const std::vector<Quake3Shader>>();

  {
    auto lock = std::lock_guard{m_textureResourceCacheMutex};
    m_textureResourceCache.clear();
    m_textureResourceUseGeneration.clear();
    m_textureResourceGeneration = 0u;
    m_textureCacheHits = 0u;
    m_textureCacheMisses = 0u;
    m_textureCacheLateHits = 0u;
    m_textureCacheExpiredPrunes = 0u;
    m_textureCacheEvictions = 0u;
  }

  {
    auto lock = std::lock_guard{m_modelLoadStatsMutex};
    m_modelLoadMaxElapsedMs.clear();
    m_modelLoadCount = 0u;
    m_modelLoadFailureCount = 0u;
    m_modelLoadTotalElapsedMs = 0;
    m_modelLoadMaxElapsedMsGlobal = 0;
    m_modelLoadSlowestPath.clear();
  }

  m_unpreparedRenderers.clear();

  // Remove logging because it might fail when the document is already destroyed.
}

void EntityModelManager::reloadShaders(kdl::task_manager& taskManager)
{
  auto shaders =
    io::loadShaders(
      m_gameFileSystem, m_gameInfo.gameConfig.materialConfig, taskManager, m_logger)
    | kdl::if_error(
      [&](const auto& e) { m_logger.error() << "Failed to reload shaders: " << e.msg; })
    | kdl::value_or(std::vector<Quake3Shader>{});

  m_shaders = std::make_shared<const std::vector<Quake3Shader>>(std::move(shaders));
}

render::MaterialRenderer* EntityModelManager::renderer(
  const ModelSpecification& spec) const
{
  if (auto* entityModel = model(spec.path))
  {
    auto it = m_renderers.find(spec);
    if (it != std::end(m_renderers))
    {
      return it->second.get();
    }

    if (!m_rendererMismatches.contains(spec))
    {
      if (const auto* entityModelData = entityModel->data())
      {
        if (
          auto renderer = entityModelData->buildRenderer(spec.skinIndex, spec.frameIndex))
        {
          const auto [pos, success] = m_renderers.emplace(spec, std::move(renderer));
          contract_assert(success);

          auto* result = pos->second.get();
          m_unpreparedRenderers.push_back(result);
          m_logger.debug() << "Constructed entity model renderer for " << spec;
          return result;
        }

        m_rendererMismatches.insert(spec);
        m_logger.error() << "Failed to construct entity model renderer for " << spec
                         << ", check the skin and frame indices";
      }
    }
  }

  return nullptr;
}

const EntityModelFrame* EntityModelManager::frame(const ModelSpecification& spec) const
{
  if (auto* entityModel = model(spec.path))
  {
    if (const auto* entityModelData = entityModel->data())
    {
      return entityModelData->frame(spec.frameIndex);
    }
  }

  return nullptr;
}

const EntityModel* EntityModelManager::model(const std::filesystem::path& path) const
{
  if (!path.empty())
  {
    auto it = m_models.find(path);
    if (it != std::end(m_models))
    {
      return &it->second;
    }

    return loadModel(path) | kdl::transform([&](auto model) {
             const auto [pos, success] = m_models.emplace(path, std::move(model));
             contract_assert(success);

             m_logger.debug() << "Loading entity model " << path;
             return &(pos->second);
           })
           | kdl::transform_error([&](auto e) {
               m_logger.error() << e.msg;
               return nullptr;
             })
           | kdl::value();
  }

  return nullptr;
}

const std::vector<const EntityModel*> EntityModelManager::
  findEntityModelsByTextureResourceId(const std::vector<ResourceId>& resourceIds) const
{
  using namespace std::ranges;

  const auto filterByResourceId =
    [resourceIdSet = std::unordered_set<ResourceId>{
       resourceIds.begin(), resourceIds.end()}](const auto& model) {
      return resourceIdSet.contains(model.dataResource().id());
    };

  const auto toPointer = [](const auto& model) { return &model; };

  return m_models | views::values | views::filter(filterByResourceId)
         | views::transform(toPointer) | kdl::ranges::to<std::vector>();
}

size_t EntityModelManager::modelCount() const
{
  return m_models.size();
}

EntityModelManager::TextureCacheStats EntityModelManager::textureCacheStats() const
{
  auto lock = std::lock_guard{m_textureResourceCacheMutex};
  return TextureCacheStats{
    .size = m_textureResourceCache.size(),
    .hits = m_textureCacheHits,
    .misses = m_textureCacheMisses,
    .lateHits = m_textureCacheLateHits,
    .expiredPrunes = m_textureCacheExpiredPrunes,
    .evictions = m_textureCacheEvictions,
  };
}

EntityModelManager::ModelLoadStats EntityModelManager::modelLoadStats() const
{
  auto lock = std::lock_guard{m_modelLoadStatsMutex};
  return ModelLoadStats{
    .loadCount = m_modelLoadCount,
    .failureCount = m_modelLoadFailureCount,
    .totalElapsedMs = m_modelLoadTotalElapsedMs,
    .maxElapsedMs = m_modelLoadMaxElapsedMsGlobal,
    .slowestModelPath = m_modelLoadSlowestPath,
  };
}

void EntityModelManager::retainModels(
  const std::unordered_set<std::filesystem::path, kdl::path_hash>& retainedModelPaths)
{
  std::erase_if(m_models, [&](const auto& entry) {
    return !retainedModelPaths.contains(entry.first);
  });

  std::erase_if(m_renderers, [&](const auto& entry) {
    return !retainedModelPaths.contains(entry.first.path);
  });

  std::erase_if(m_rendererMismatches, [&](const auto& spec) {
    return !retainedModelPaths.contains(spec.path);
  });

  auto retainedRenderers = std::unordered_set<render::MaterialRenderer*>{};
  retainedRenderers.reserve(m_renderers.size());
  for (const auto& entry : m_renderers)
  {
    retainedRenderers.insert(entry.second.get());
  }

  std::erase_if(m_unpreparedRenderers, [&](auto* renderer) {
    return !retainedRenderers.contains(renderer);
  });

  {
    auto lock = std::lock_guard{m_textureResourceCacheMutex};
    pruneTextureCacheLocked(TextureResourceCacheMaxEntries);
  }
}

Result<EntityModel> EntityModelManager::loadModel(
  const std::filesystem::path& modelPath) const
{
  const auto materialConfig = m_gameInfo.gameConfig.materialConfig;
  const auto shaderSnapshot = m_shaders;
  const auto materialLoadContext =
    io::createMaterialLoadContext(
      m_gameFileSystem, materialConfig, *shaderSnapshot, m_logger);

  const auto loadMaterial = [this, materialConfig, materialLoadContext, shaderSnapshot](
                              const auto& materialPath) {
    contract_assert(shaderSnapshot != nullptr);
    const auto cacheKey = normalizeMaterialCacheKey(materialPath);
    const auto createResource = [this, cacheKey](auto resourceLoader) {
      {
        auto lock = std::lock_guard{m_textureResourceCacheMutex};

        if (const auto iCache = m_textureResourceCache.find(cacheKey);
            iCache != std::end(m_textureResourceCache))
        {
          if (auto cachedResource = iCache->second.lock())
          {
            ++m_textureCacheHits;
            m_textureResourceUseGeneration[cacheKey] = ++m_textureResourceGeneration;
            return cachedResource;
          }

          ++m_textureCacheExpiredPrunes;
          m_textureResourceCache.erase(iCache);
          m_textureResourceUseGeneration.erase(cacheKey);
        }

        ++m_textureCacheMisses;
      }

      auto createdResource = createResourceSync(std::move(resourceLoader));

      {
        auto lock = std::lock_guard{m_textureResourceCacheMutex};

        if (const auto iCache = m_textureResourceCache.find(cacheKey);
            iCache != std::end(m_textureResourceCache))
        {
          if (auto cachedResource = iCache->second.lock())
          {
            ++m_textureCacheHits;
            ++m_textureCacheLateHits;
            m_textureResourceUseGeneration[cacheKey] = ++m_textureResourceGeneration;
            return cachedResource;
          }

          ++m_textureCacheExpiredPrunes;
          m_textureResourceCache.erase(iCache);
          m_textureResourceUseGeneration.erase(cacheKey);
        }

        m_textureResourceCache[cacheKey] = createdResource;
        m_textureResourceUseGeneration[cacheKey] = ++m_textureResourceGeneration;
        pruneTextureCacheLocked(TextureResourceCacheMaxEntries);
      }

      return createdResource;
    };

    return io::loadMaterial(
             m_gameFileSystem,
             materialConfig,
             materialPath,
             createResource,
             materialLoadContext,
             std::nullopt,
             m_logger)
           | kdl::or_else(io::makeReadMaterialErrorHandler(m_gameFileSystem, m_logger))
           | kdl::value();
  };

  const auto createResource =
    [this, modelPath](ResourceLoader<EntityModelData> resourceLoader) {
      auto timedLoader = [this, modelPath, resourceLoader = std::move(resourceLoader)]()
        -> Result<EntityModelData> {
        const auto begin = std::chrono::steady_clock::now();
        auto result = resourceLoader();
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - begin)
                                 .count();

        recordModelLoad(modelPath, elapsedMs, result.is_error());
        return result;
      };

      return m_createResource(std::move(timedLoader));
    };

  return io::loadEntityModelAsync(
    m_gameFileSystem,
    materialConfig,
    modelPath,
    loadMaterial,
    createResource,
    m_logger);
}

void EntityModelManager::pruneTextureCacheLocked(const size_t maxEntries) const
{
  for (auto it = m_textureResourceCache.begin(); it != m_textureResourceCache.end();)
  {
    if (it->second.expired())
    {
      m_textureResourceUseGeneration.erase(it->first);
      it = m_textureResourceCache.erase(it);
      ++m_textureCacheExpiredPrunes;
    }
    else
    {
      ++it;
    }
  }

  if (m_textureResourceCache.size() <= maxEntries)
  {
    return;
  }

  auto sortedEntries = std::vector<std::pair<std::filesystem::path, size_t>>{};
  sortedEntries.reserve(m_textureResourceUseGeneration.size());
  for (const auto& [path, generation] : m_textureResourceUseGeneration)
  {
    if (m_textureResourceCache.contains(path))
    {
      sortedEntries.emplace_back(path, generation);
    }
  }
  std::sort(sortedEntries.begin(), sortedEntries.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.second < rhs.second;
  });

  auto toEvict = m_textureResourceCache.size() - maxEntries;
  for (const auto& [path, generation] : sortedEntries)
  {
    (void)generation;
    if (toEvict == 0u)
    {
      break;
    }

    if (m_textureResourceCache.erase(path) > 0u)
    {
      m_textureResourceUseGeneration.erase(path);
      ++m_textureCacheEvictions;
      --toEvict;
    }
  }
}

void EntityModelManager::recordModelLoad(
  const std::filesystem::path& modelPath, const long long elapsedMs, const bool failed) const
{
  auto lock = std::lock_guard{m_modelLoadStatsMutex};

  ++m_modelLoadCount;
  m_modelLoadTotalElapsedMs += elapsedMs;
  if (failed)
  {
    ++m_modelLoadFailureCount;
  }

  m_modelLoadMaxElapsedMs[modelPath] =
    std::max(m_modelLoadMaxElapsedMs[modelPath], elapsedMs);
  if (elapsedMs >= m_modelLoadMaxElapsedMsGlobal)
  {
    m_modelLoadMaxElapsedMsGlobal = elapsedMs;
    m_modelLoadSlowestPath = modelPath;
  }
}

void EntityModelManager::prepare(render::VboManager& vboManager)
{
  prepareRenderers(vboManager);
}

void EntityModelManager::prepareRenderers(render::VboManager& vboManager)
{
  for (auto* renderer : m_unpreparedRenderers)
  {
    renderer->prepare(vboManager);
  }
  m_unpreparedRenderers.clear();
}
} // namespace tb::mdl
