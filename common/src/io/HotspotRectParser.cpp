/*
 Copyright (C) 2026 Kristian Duske

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

#include "io/HotspotRectParser.h"

#include "kd/string_utils.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace tb::io
{
namespace
{

std::string trim(std::string_view value)
{
  const auto start = value.find_first_not_of(" \t\r\n");
  if (start == std::string_view::npos)
  {
    return "";
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return std::string{value.substr(start, end - start + 1)};
}

std::string stripComments(const std::string& line)
{
  const auto slashPos = line.find("//");
  const auto hashPos = line.find('#');
  const auto cutPos =
    std::min(slashPos == std::string::npos ? line.size() : slashPos,
             hashPos == std::string::npos ? line.size() : hashPos);
  return line.substr(0u, cutPos);
}

std::vector<std::string> tokenizeLine(const std::string& line)
{
  auto tokens = std::vector<std::string>{};
  const auto size = line.size();
  size_t i = 0u;

  while (i < size)
  {
    while (i < size && std::isspace(static_cast<unsigned char>(line[i])))
    {
      ++i;
    }
    if (i >= size)
    {
      break;
    }

    const auto c = line[i];
    if (c == '{' || c == '}')
    {
      tokens.emplace_back(1, c);
      ++i;
      continue;
    }

    if (c == '"')
    {
      ++i;
      const auto start = i;
      while (i < size && line[i] != '"')
      {
        ++i;
      }
      tokens.emplace_back(line.substr(start, i - start));
      if (i < size && line[i] == '"')
      {
        ++i;
      }
      continue;
    }

    const auto start = i;
    while (
      i < size && !std::isspace(static_cast<unsigned char>(line[i])) && line[i] != '{'
      && line[i] != '}')
    {
      ++i;
    }
    tokens.emplace_back(line.substr(start, i - start));
  }

  return tokens;
}

std::string toLower(std::string_view value)
{
  auto result = std::string{value};
  std::ranges::transform(result, result.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return result;
}

bool isRectanglesKeyword(const std::string& value)
{
  const auto lower = toLower(value);
  return lower == "rectangles" || lower == "rectangle";
}

std::optional<std::string> currentTextureName(const std::vector<std::string>& stack)
{
  for (auto it = stack.rbegin(); it != stack.rend(); ++it)
  {
    if (!it->empty() && !isRectanglesKeyword(*it))
    {
      return *it;
    }
  }
  return std::nullopt;
}

std::vector<float> parseNumbers(const std::string& line)
{
  static const auto kNumberRegex = std::regex(R"([-+]?\d*\.?\d+)");
  auto values = std::vector<float>{};
  auto begin = std::sregex_iterator(line.begin(), line.end(), kNumberRegex);
  auto end = std::sregex_iterator();
  for (auto it = begin; it != end; ++it)
  {
    values.push_back(std::stof(it->str()));
  }
  return values;
}

std::optional<float> parseWeightToken(const std::string& token)
{
  const auto pos = token.find('=');
  if (pos == std::string::npos)
  {
    return std::nullopt;
  }
  const auto key = toLower(token.substr(0, pos));
  if (key != "weight" && key != "w")
  {
    return std::nullopt;
  }
  return kdl::str_to_float(token.substr(pos + 1));
}

float parseWeight(const std::vector<std::string>& tokens)
{
  for (size_t i = 0u; i < tokens.size(); ++i)
  {
    if (const auto value = parseWeightToken(tokens[i]))
    {
      return *value;
    }

    const auto lower = toLower(tokens[i]);
    if ((lower == "weight" || lower == "w") && i + 1u < tokens.size())
    {
      if (const auto value = kdl::str_to_float(tokens[i + 1u]))
      {
        return *value;
      }
    }
  }

  return 1.0f;
}

bool hasToken(const std::vector<std::string>& tokens, const std::string& token)
{
  return std::ranges::any_of(tokens, [&](const auto& value) {
    return toLower(value) == token;
  });
}

} // namespace

Result<HotspotRectMap> parseHotspotRectFile(
  const std::string_view contents, std::optional<std::string> defaultTextureName)
{
  auto result = HotspotRectMap{};
  auto scopeStack = std::vector<std::string>{};
  std::optional<std::string> pendingBlockName;

  std::istringstream stream{std::string{contents}};
  std::string rawLine;
  while (std::getline(stream, rawLine))
  {
    const auto cleanedLine = trim(stripComments(rawLine));
    if (cleanedLine.empty())
    {
      continue;
    }

    const auto tokens = tokenizeLine(cleanedLine);
    for (size_t i = 0u; i < tokens.size(); ++i)
    {
      if (tokens[i] == "{")
      {
        if (i > 0u)
        {
          scopeStack.push_back(tokens[i - 1u]);
        }
        else if (pendingBlockName)
        {
          scopeStack.push_back(*pendingBlockName);
        }
        else
        {
          scopeStack.emplace_back();
        }
        pendingBlockName.reset();
      }
      else if (tokens[i] == "}" && !scopeStack.empty())
      {
        scopeStack.pop_back();
      }
    }

    if (tokens.size() == 1u && tokens.front() != "{" && tokens.front() != "}")
    {
      if (!kdl::str_to_float(tokens.front()))
      {
        pendingBlockName = tokens.front();
      }
    }

    auto textureName = currentTextureName(scopeStack);
    if (!textureName && defaultTextureName)
    {
      textureName = *defaultTextureName;
    }

    if (!textureName)
    {
      continue;
    }

    const auto values = parseNumbers(cleanedLine);
    if (values.size() < 4u)
    {
      continue;
    }

    const auto rectMin = vm::vec2f{values[0], values[1]};
    const auto rectSize = vm::vec2f{values[2], values[3]};
    if (rectSize.x() <= 0.0f || rectSize.y() <= 0.0f)
    {
      continue;
    }

    const auto tileU = hasToken(tokens, "tileu") || hasToken(tokens, "tile_u")
                       || hasToken(tokens, "repeatu") || hasToken(tokens, "repeat_u")
                       || hasToken(tokens, "tilex") || hasToken(tokens, "tile-h")
                       || hasToken(tokens, "tileh");
    const auto tileV = hasToken(tokens, "tilev") || hasToken(tokens, "tile_v")
                       || hasToken(tokens, "repeatv") || hasToken(tokens, "repeat_v")
                       || hasToken(tokens, "tiley") || hasToken(tokens, "tile-v");

    const auto weight = parseWeight(tokens);
    auto rect = mdl::HotspotRect{rectMin, rectSize, tileU, tileV, weight};
    result[*textureName].push_back(std::move(rect));
  }

  return result;
}

} // namespace tb::io
