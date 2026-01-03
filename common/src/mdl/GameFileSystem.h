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

#include "fs/VirtualFileSystem.h"

#include <filesystem>
#include <vector>

namespace tb
{
class Logger;

namespace mdl
{
struct GameConfig;

class GameFileSystem : public fs::VirtualFileSystem
{
private:
  Logger& m_logger;
  std::vector<fs::VirtualMountPointId> m_wadMountPoints;

public:
  explicit GameFileSystem(Logger& logger);

  void initialize(
    const GameConfig& config,
    const std::filesystem::path& gamePath,
    const std::vector<std::filesystem::path>& additionalSearchPaths);
  void reloadWads(
    const std::filesystem::path& rootPath,
    const std::vector<std::filesystem::path>& wadSearchPaths,
    const std::vector<std::filesystem::path>& wadPaths);

private:
  void addDefaultAssetPaths(const GameConfig& config);
  void addGameFileSystems(
    const GameConfig& config,
    const std::filesystem::path& gamePath,
    const std::vector<std::filesystem::path>& additionalSearchPaths);
  void addSearchPath(
    const GameConfig& config,
    const std::filesystem::path& gamePath,
    const std::filesystem::path& searchPath);
  void addFileSystemPath(const std::filesystem::path& path);
  void addFileSystemPackages(
    const GameConfig& config, const std::filesystem::path& searchPath);

  void mountWads(
    const std::filesystem::path& rootPath,
    const std::vector<std::filesystem::path>& wadSearchPaths,
    const std::vector<std::filesystem::path>& wadPaths);
  void unmountWads();
};

} // namespace mdl
} // namespace tb
