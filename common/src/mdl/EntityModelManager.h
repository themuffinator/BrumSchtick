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

#pragma once

#include "Result.h"
#include "mdl/EntityModel.h"
#include "mdl/ModelSpecification.h"
#include "mdl/TextureResource.h"

#include "kd/path_hash.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kdl
{
class task_manager;
}

namespace tb
{
class Logger;

namespace fs
{
class FileSystem;
}

namespace render
{
class MaterialRenderer;
class VboManager;
} // namespace render

namespace mdl
{
class EntityModelFrame;
class EntityNode;
class Quake3Shader;

enum class Orientation;

struct GameInfo;

class EntityModelManager
{
public:
  struct TextureCacheStats
  {
    size_t size = 0u;
    size_t hits = 0u;
    size_t misses = 0u;
    size_t lateHits = 0u;
    size_t expiredPrunes = 0u;
    size_t evictions = 0u;
  };

  struct ModelLoadStats
  {
    size_t loadCount = 0u;
    size_t failureCount = 0u;
    long long totalElapsedMs = 0;
    long long maxElapsedMs = 0;
    std::filesystem::path slowestModelPath;
  };

private:
  const GameInfo& m_gameInfo;
  const fs::FileSystem& m_gameFileSystem;

  CreateEntityModelDataResource m_createResource;
  Logger& m_logger;

  // Cache Quake 3 shaders to use when loading models
  std::shared_ptr<const std::vector<Quake3Shader>> m_shaders;

  mutable std::unordered_map<std::filesystem::path, EntityModel, kdl::path_hash> m_models;
  mutable std::
    unordered_map<ModelSpecification, std::unique_ptr<render::MaterialRenderer>>
      m_renderers;
  mutable std::unordered_set<ModelSpecification> m_rendererMismatches;
  mutable std::unordered_map<
    std::filesystem::path,
    std::weak_ptr<TextureResource>,
    kdl::path_hash>
    m_textureResourceCache;
  mutable std::unordered_map<std::filesystem::path, size_t, kdl::path_hash>
    m_textureResourceUseGeneration;
  mutable std::mutex m_textureResourceCacheMutex;
  mutable size_t m_textureResourceGeneration = 0u;
  mutable size_t m_textureCacheHits = 0u;
  mutable size_t m_textureCacheMisses = 0u;
  mutable size_t m_textureCacheLateHits = 0u;
  mutable size_t m_textureCacheExpiredPrunes = 0u;
  mutable size_t m_textureCacheEvictions = 0u;

  mutable std::mutex m_modelLoadStatsMutex;
  mutable std::unordered_map<std::filesystem::path, long long, kdl::path_hash>
    m_modelLoadMaxElapsedMs;
  mutable size_t m_modelLoadCount = 0u;
  mutable size_t m_modelLoadFailureCount = 0u;
  mutable long long m_modelLoadTotalElapsedMs = 0;
  mutable long long m_modelLoadMaxElapsedMsGlobal = 0;
  mutable std::filesystem::path m_modelLoadSlowestPath;

  mutable std::vector<render::MaterialRenderer*> m_unpreparedRenderers;

public:
  EntityModelManager(
    const GameInfo& gameInfo,
    const fs::FileSystem& gameFilesystem,
    CreateEntityModelDataResource createResource,
    Logger& logger);
  ~EntityModelManager();

  void clear();
  void reloadShaders(kdl::task_manager& taskManager);

  render::MaterialRenderer* renderer(const ModelSpecification& spec) const;

  const EntityModelFrame* frame(const ModelSpecification& spec) const;
  const EntityModel* model(const std::filesystem::path& path) const;

  const std::vector<const EntityModel*> findEntityModelsByTextureResourceId(
    const std::vector<ResourceId>& resourceIds) const;

  size_t modelCount() const;
  TextureCacheStats textureCacheStats() const;
  ModelLoadStats modelLoadStats() const;

  void retainModels(
    const std::unordered_set<std::filesystem::path, kdl::path_hash>& retainedModelPaths);

private:
  void pruneTextureCacheLocked(size_t maxEntries) const;
  void recordModelLoad(const std::filesystem::path& modelPath, long long elapsedMs, bool failed)
    const;
  Result<EntityModel> loadModel(const std::filesystem::path& path) const;

public:
  void prepare(render::VboManager& vboManager);

private:
  void prepareRenderers(render::VboManager& vboManager);
};

} // namespace mdl
} // namespace tb
