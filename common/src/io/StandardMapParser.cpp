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

#include "StandardMapParser.h"

#include "Color.h"
#include "FileLocation.h"
#include "ParserStatus.h"
#include "mdl/BrushFace.h"
#include "mdl/EntityProperties.h"

#include "kd/contracts.h"

#include "vm/vec.h"

#include <algorithm>
#include <format>
#include <string>
#include <vector>

namespace tb::io
{
namespace
{

auto tokenNames()
{
  using namespace QuakeMapToken;

  return QuakeMapTokenizer::TokenNameMap{
    {Integer, "integer"},
    {Decimal, "decimal"},
    {String, "string"},
    {OParenthesis, "'('"},
    {CParenthesis, "')'"},
    {OBrace, "'{'"},
    {CBrace, "'}'"},
    {OBracket, "'['"},
    {CBracket, "']'"},
    {Comment, "comment"},
    {Eof, "end of file"},
  };
}

size_t sanitizePatchCount(
  const size_t count,
  const std::string_view label,
  const FileLocation& location,
  ParserStatus& status)
{
  if (count < 3u)
  {
    status.warn(
      location, std::format("Invalid patch {}, expanding to 3", label));
    return 3u;
  }
  if (count % 2u == 0u)
  {
    const auto expanded = count + 1u;
    status.warn(
      location, std::format("Invalid patch {}, expanding to {}", label, expanded));
    return expanded;
  }
  return count;
}

std::vector<vm::vec<double, 5>> resizePatchControlPoints(
  const size_t sourceRowCount,
  const size_t sourceColumnCount,
  const size_t targetRowCount,
  const size_t targetColumnCount,
  const std::vector<vm::vec<double, 5>>& controlPoints)
{
  if (sourceRowCount == 0u || sourceColumnCount == 0u)
  {
    return std::vector<vm::vec<double, 5>>(targetRowCount * targetColumnCount);
  }

  auto resized = std::vector<vm::vec<double, 5>>{};
  resized.reserve(targetRowCount * targetColumnCount);

  for (size_t row = 0u; row < targetRowCount; ++row)
  {
    const auto sourceRow = std::min(row, sourceRowCount - 1u);
    for (size_t col = 0u; col < targetColumnCount; ++col)
    {
      const auto sourceCol = std::min(col, sourceColumnCount - 1u);
      resized.push_back(
        controlPoints[sourceRow * sourceColumnCount + sourceCol]);
    }
  }

  return resized;
}

std::vector<vm::vec3d> resizePatchControlNormals(
  const size_t sourceRowCount,
  const size_t sourceColumnCount,
  const size_t targetRowCount,
  const size_t targetColumnCount,
  const std::vector<vm::vec3d>& controlNormals)
{
  if (sourceRowCount == 0u || sourceColumnCount == 0u || controlNormals.empty())
  {
    return {};
  }

  auto resized = std::vector<vm::vec3d>{};
  resized.reserve(targetRowCount * targetColumnCount);

  for (size_t row = 0u; row < targetRowCount; ++row)
  {
    const auto sourceRow = std::min(row, sourceRowCount - 1u);
    for (size_t col = 0u; col < targetColumnCount; ++col)
    {
      const auto sourceCol = std::min(col, sourceColumnCount - 1u);
      resized.push_back(
        controlNormals[sourceRow * sourceColumnCount + sourceCol]);
    }
  }

  return resized;
}

} // namespace

const std::string& QuakeMapTokenizer::NumberDelim()
{
  static const std::string numberDelim(Whitespace() + ")");
  return numberDelim;
}

QuakeMapTokenizer::QuakeMapTokenizer(const std::string_view str)
  : Tokenizer{tokenNames(), str, "\"", '\\'}
{
}

void QuakeMapTokenizer::setSkipEol(bool skipEol)
{
  m_skipEol = skipEol;
}

QuakeMapTokenizer::Token QuakeMapTokenizer::emitToken()
{
  while (!eof())
  {
    const auto startLine = line();
    const auto startColumn = column();
    const auto startLocation = location();
    const auto* c = curPos();
    switch (*c)
    {
    case '/':
      advance();
      if (curChar() == '/')
      {
        advance();
        if (curChar() == '/' && lookAhead(1) == ' ')
        {
          advance();
          return Token{
            QuakeMapToken::Comment, c, c + 3, offset(c), startLine, startColumn};
        }
        discardUntil("\n\r");
      }
      break;
    case ';':
      // Heretic2 allows semicolon to start a line comment.
      // QuArK writes comments in this format when saving a Heretic2 .map.
      advance();
      discardUntil("\n\r");
      break;
    case '{':
      advance();
      return Token{QuakeMapToken::OBrace, c, c + 1, offset(c), startLine, startColumn};
    case '}':
      advance();
      return Token{QuakeMapToken::CBrace, c, c + 1, offset(c), startLine, startColumn};
    case '(':
      advance();
      return Token{
        QuakeMapToken::OParenthesis, c, c + 1, offset(c), startLine, startColumn};
    case ')':
      advance();
      return Token{
        QuakeMapToken::CParenthesis, c, c + 1, offset(c), startLine, startColumn};
    case '[':
      advance();
      return Token{QuakeMapToken::OBracket, c, c + 1, offset(c), startLine, startColumn};
    case ']':
      advance();
      return Token{QuakeMapToken::CBracket, c, c + 1, offset(c), startLine, startColumn};
    case '"': { // quoted string
      advance();
      c = curPos();
      const auto* e = readQuotedString('"', "\n}");
      return Token{QuakeMapToken::String, c, e, offset(c), startLine, startColumn};
    }
    case '\r':
      if (lookAhead() == '\n')
      {
        advance();
      }
      // handle carriage return without consecutive linefeed
      // by falling through into the line feed case
      switchFallthrough();
    case '\n':
      if (!m_skipEol)
      {
        advance();
        return Token{QuakeMapToken::Eol, c, c + 1, offset(c), startLine, startColumn};
      }
      switchFallthrough();
    case ' ':
    case '\t':
      discardWhile(Whitespace());
      break;
    default: // whitespace, integer, decimal or word
      if (const auto* e = readInteger(NumberDelim()))
      {
        return Token{QuakeMapToken::Integer, c, e, offset(c), startLine, startColumn};
      }

      if (const auto e = readDecimal(NumberDelim()))
      {
        return Token{QuakeMapToken::Decimal, c, e, offset(c), startLine, startColumn};
      }

      if (const auto e = readUntil(Whitespace()))
      {
        return Token{QuakeMapToken::String, c, e, offset(c), startLine, startColumn};
      }

      throw ParserException{startLocation, std::format("Unexpected character: {}", *c)};
    }
  }
  return Token{QuakeMapToken::Eof, nullptr, nullptr, length(), line(), column()};
}

const std::string StandardMapParser::BrushPrimitiveId = "brushDef";
const std::string StandardMapParser::PatchId = "patchDef2";
const std::string StandardMapParser::Patch3Id = "patchDef3";

StandardMapParser::StandardMapParser(
  const std::string_view str,
  const mdl::MapFormat sourceMapFormat,
  const mdl::MapFormat targetMapFormat)
  : m_tokenizer{str}
  , m_sourceMapFormat{sourceMapFormat}
  , m_targetMapFormat{targetMapFormat}
{
  contract_pre(m_sourceMapFormat != mdl::MapFormat::Unknown);
  contract_pre(targetMapFormat != mdl::MapFormat::Unknown);
}

StandardMapParser::~StandardMapParser() = default;

Result<void> StandardMapParser::parseEntities(ParserStatus& status)
{
  try
  {
    while (m_tokenizer.peekToken(QuakeMapToken::OBrace | QuakeMapToken::Eof)
             .hasType(QuakeMapToken::OBrace))
    {
      parseEntity(status);
    }

    return kdl::void_success;
  }
  catch (const ParserException& e)
  {
    return Error{e.what()};
  }
}

Result<void> StandardMapParser::parseBrushesOrPatches(ParserStatus& status)
{
  try
  {
    while (m_tokenizer.peekToken(QuakeMapToken::OBrace | QuakeMapToken::Eof)
             .hasType(QuakeMapToken::OBrace))
    {
      parseObject(status);
    }

    return kdl::void_success;
  }
  catch (const ParserException& e)
  {
    return Error{e.what()};
  }
}

Result<void> StandardMapParser::parseBrushFaces(ParserStatus& status)
{
  try
  {
    while (m_tokenizer.peekToken(QuakeMapToken::OParenthesis | QuakeMapToken::Eof)
             .hasType(QuakeMapToken::OParenthesis))
    {
      // TODO 2427: detect the face type when parsing Quake3 map faces!
      parseFace(status, false);
    }

    return kdl::void_success;
  }
  catch (const ParserException& e)
  {
    return Error{e.what()};
  }
}

void StandardMapParser::reset()
{
  m_tokenizer.reset();
}

void StandardMapParser::parseEntity(ParserStatus& status)
{
  auto token = m_tokenizer.nextToken(QuakeMapToken::OBrace | QuakeMapToken::Eof);
  if (token.hasType(QuakeMapToken::OBrace))
  {
    const auto startLocation = token.location();

    auto properties = std::vector<mdl::EntityProperty>();
    parseEntityProperties(properties, status);

    onBeginEntity(startLocation, properties, status);
    parseObjects(status);

    token = m_tokenizer.skipAndNextToken(QuakeMapToken::Comment, QuakeMapToken::CBrace);

    onEndEntity(token.location(), status);
  }
}

void StandardMapParser::parseEntityProperties(
  std::vector<mdl::EntityProperty>& properties, ParserStatus& status)
{
  while (m_tokenizer
           .skipAndPeekToken(
             QuakeMapToken::Comment,
             QuakeMapToken::String | QuakeMapToken::OBrace | QuakeMapToken::CBrace)
           .hasType(QuakeMapToken::String))
  {
    parseEntityProperty(properties, status);
  }
}

void StandardMapParser::parseEntityProperty(
  std::vector<mdl::EntityProperty>& properties, ParserStatus& /* status */)
{
  auto token =
    m_tokenizer.skipAndNextToken(QuakeMapToken::Comment, QuakeMapToken::String);

  const auto name = token.data();

  token = m_tokenizer.nextToken(QuakeMapToken::String);
  const auto value = token.data();

  properties.emplace_back(name, value);
}

void StandardMapParser::parseObjects(ParserStatus& status)
{
  auto token = m_tokenizer.skipAndPeekToken(QuakeMapToken::Comment);
  while (token.hasType(QuakeMapToken::OBrace))
  {
    parseObject(status);
    token = m_tokenizer.skipAndPeekToken(QuakeMapToken::Comment);
  }
}

void StandardMapParser::parseObject(ParserStatus& status)
{
  // consume initial opening brace
  auto token = m_tokenizer.skipAndNextToken(
    QuakeMapToken::Comment,
    QuakeMapToken::OBrace | QuakeMapToken::CBrace | QuakeMapToken::Eof);

  if (token.hasType(QuakeMapToken::Eof | QuakeMapToken::CBrace))
  {
    return;
  }

  const auto startLocation = token.location();

  if (m_sourceMapFormat == mdl::MapFormat::Quake3)
  {
    // We expect either a brush primitive, a patch or a regular brush.
    token = m_tokenizer.peekToken(QuakeMapToken::String | QuakeMapToken::OParenthesis);
    if (token.hasType(QuakeMapToken::String))
    {
      expect({BrushPrimitiveId, PatchId, Patch3Id}, token);
      if (token.data() == BrushPrimitiveId)
      {
        parseBrushPrimitive(status, startLocation);
      }
      else
      {
        parsePatch(status, startLocation);
      }
    }
    else
    {
      parseBrush(status, startLocation, false);
    }
  }
  else if (
    m_sourceMapFormat == mdl::MapFormat::Quake3_Valve
    || m_sourceMapFormat == mdl::MapFormat::Quake3_Legacy)
  {
    // We expect either a patch or a regular brush.
    token = m_tokenizer.peekToken(QuakeMapToken::String | QuakeMapToken::OParenthesis);
    if (token.hasType(QuakeMapToken::String))
    {
      expect({PatchId, Patch3Id}, token);
      parsePatch(status, startLocation);
    }
    else
    {
      parseBrush(status, startLocation, false);
    }
  }
  else
  {
    token = m_tokenizer.peekToken(QuakeMapToken::OParenthesis);
    parseBrush(status, startLocation, false);
  }

  // consume final closing brace
  m_tokenizer.nextToken(QuakeMapToken::CBrace);
}

void StandardMapParser::parseBrushPrimitive(
  ParserStatus& status, const FileLocation& startLocation)
{
  auto token = m_tokenizer.nextToken(QuakeMapToken::String);
  expect(BrushPrimitiveId, token);
  m_tokenizer.nextToken(QuakeMapToken::OBrace);
  parseBrush(status, startLocation, true);
  m_tokenizer.nextToken(QuakeMapToken::CBrace);
}

void StandardMapParser::parseBrush(
  ParserStatus& status, const FileLocation& startLocation, const bool primitive)
{
  auto beginBrushCalled = false;

  auto token = m_tokenizer.skipAndPeekToken(
    QuakeMapToken::Comment,
    QuakeMapToken::OParenthesis | QuakeMapToken::CBrace | QuakeMapToken::Eof);
  while (!token.hasType(QuakeMapToken::Eof))
  {
    switch (token.type())
    {
    case QuakeMapToken::OParenthesis:
      // TODO 2427: handle brush primitives
      if (!beginBrushCalled && !primitive)
      {
        onBeginBrush(startLocation, status);
        beginBrushCalled = true;
      }
      parseFace(status, primitive);
      break;
    case QuakeMapToken::CBrace:
      // TODO 2427: handle brush primitives
      if (!primitive)
      {
        if (!beginBrushCalled)
        {
          onBeginBrush(startLocation, status);
        }
        onEndBrush(token.location(), status);
      }
      else
      {
        status.warn(startLocation, "Skipping brush primitive: currently not supported");
      }
      return;
      switchDefault();
    }

    token = m_tokenizer.skipAndPeekToken(
      QuakeMapToken::Comment,
      QuakeMapToken::OParenthesis | QuakeMapToken::CBrace | QuakeMapToken::Eof);
  }
}

void StandardMapParser::parseFace(ParserStatus& status, const bool primitive)
{
  switch (m_sourceMapFormat)
  {
  case mdl::MapFormat::Standard:
    parseQuakeFace(status);
    break;
  case mdl::MapFormat::Quake2:
  case mdl::MapFormat::Quake3_Legacy:
    parseQuake2Face(status);
    break;
  case mdl::MapFormat::Quake2_Valve:
  case mdl::MapFormat::Quake3_Valve:
    parseQuake2ValveFace(status);
    break;
  case mdl::MapFormat::Hexen2:
    parseHexen2Face(status);
    break;
  case mdl::MapFormat::Daikatana:
    parseDaikatanaFace(status);
    break;
  case mdl::MapFormat::Valve:
    parseValveFace(status);
    break;
  case mdl::MapFormat::Quake3:
    if (primitive)
    {
      parsePrimitiveFace(status);
    }
    else
    {
      parseQuake2Face(status);
    }
    break;
  case mdl::MapFormat::Unknown:
    // cannot happen
    break;
    switchDefault();
  }
}

void StandardMapParser::parseQuakeFace(ParserStatus& status)
{
  const auto location = m_tokenizer.location();

  const auto [p1, p2, p3] = parseFacePoints(status);
  const auto materialName = parseMaterialName(status);

  auto attribs = mdl::BrushFaceAttributes{materialName};
  attribs.setXOffset(parseFloat());
  attribs.setYOffset(parseFloat());
  attribs.setRotation(parseFloat());
  attribs.setXScale(parseFloat());
  attribs.setYScale(parseFloat());

  onStandardBrushFace(location, m_targetMapFormat, p1, p2, p3, attribs, status);
}

void StandardMapParser::parseQuake2Face(ParserStatus& status)
{
  const auto location = m_tokenizer.location();

  const auto [p1, p2, p3] = parseFacePoints(status);
  const auto materialName = parseMaterialName(status);

  auto attribs = mdl::BrushFaceAttributes{materialName};
  attribs.setXOffset(parseFloat());
  attribs.setYOffset(parseFloat());
  attribs.setRotation(parseFloat());
  attribs.setXScale(parseFloat());
  attribs.setYScale(parseFloat());

  // Quake 2 extra info is optional
  if (!m_tokenizer.peekToken().hasType(
        QuakeMapToken::OParenthesis | QuakeMapToken::CBrace | QuakeMapToken::Eof))
  {
    attribs.setSurfaceContents(parseInteger());
    attribs.setSurfaceFlags(parseInteger());
    attribs.setSurfaceValue(parseFloat());
  }

  onStandardBrushFace(location, m_targetMapFormat, p1, p2, p3, attribs, status);
}

void StandardMapParser::parseQuake2ValveFace(ParserStatus& status)
{
  const auto location = m_tokenizer.location();

  const auto [p1, p2, p3] = parseFacePoints(status);
  const auto materialName = parseMaterialName(status);

  const auto [uAxis, uOffset, vAxis, vOffset] = parseValveUVAxes(status);

  auto attribs = mdl::BrushFaceAttributes{materialName};
  attribs.setXOffset(uOffset);
  attribs.setYOffset(vOffset);
  attribs.setRotation(parseFloat());
  attribs.setXScale(parseFloat());
  attribs.setYScale(parseFloat());

  // Quake 2 extra info is optional
  if (!m_tokenizer.peekToken().hasType(
        QuakeMapToken::OParenthesis | QuakeMapToken::CBrace | QuakeMapToken::Eof))
  {
    attribs.setSurfaceContents(parseInteger());
    attribs.setSurfaceFlags(parseInteger());
    attribs.setSurfaceValue(parseFloat());
  }

  onValveBrushFace(
    location, m_targetMapFormat, p1, p2, p3, attribs, uAxis, vAxis, status);
}

void StandardMapParser::parseHexen2Face(ParserStatus& status)
{
  const auto location = m_tokenizer.location();

  const auto [p1, p2, p3] = parseFacePoints(status);
  const auto materialName = parseMaterialName(status);

  auto attribs = mdl::BrushFaceAttributes{materialName};
  attribs.setXOffset(parseFloat());
  attribs.setYOffset(parseFloat());
  attribs.setRotation(parseFloat());
  attribs.setXScale(parseFloat());
  attribs.setYScale(parseFloat());

  // Hexen 2 extra info is optional
  if (!m_tokenizer.peekToken().hasType(
        QuakeMapToken::OParenthesis | QuakeMapToken::CBrace | QuakeMapToken::Eof))
  {
    m_tokenizer.nextToken(); // noone seems to know what the extra value does in Hexen 2
  }

  onStandardBrushFace(location, m_targetMapFormat, p1, p2, p3, attribs, status);
}

void StandardMapParser::parseDaikatanaFace(ParserStatus& status)
{
  const auto location = m_tokenizer.location();

  const auto [p1, p2, p3] = parseFacePoints(status);
  const auto materialName = parseMaterialName(status);

  auto attribs = mdl::BrushFaceAttributes{materialName};
  attribs.setXOffset(parseFloat());
  attribs.setYOffset(parseFloat());
  attribs.setRotation(parseFloat());
  attribs.setXScale(parseFloat());
  attribs.setYScale(parseFloat());

  // Daikatana extra info is optional
  if (m_tokenizer.peekToken().hasType(QuakeMapToken::Integer))
  {
    attribs.setSurfaceContents(parseInteger());
    attribs.setSurfaceFlags(parseInteger());
    attribs.setSurfaceValue(parseFloat());

    // Daikatana color triple is optional
    if (m_tokenizer.peekToken().hasType(QuakeMapToken::Integer))
    {
      // red, green, blue
      const auto r = vm::clamp(parseInteger(), 0, 255);
      const auto g = vm::clamp(parseInteger(), 0, 255);
      const auto b = vm::clamp(parseInteger(), 0, 255);
      attribs.setColor(RgbB{
        static_cast<unsigned char>(r),
        static_cast<unsigned char>(g),
        static_cast<unsigned char>(b),
      });
    }
  }

  onStandardBrushFace(location, m_targetMapFormat, p1, p2, p3, attribs, status);
}

void StandardMapParser::parseValveFace(ParserStatus& status)
{
  const auto location = m_tokenizer.location();

  const auto [p1, p2, p3] = parseFacePoints(status);
  const auto materialName = parseMaterialName(status);

  const auto [uAxis, uOffset, vAxis, vOffset] = parseValveUVAxes(status);

  auto attribs = mdl::BrushFaceAttributes{materialName};
  attribs.setXOffset(uOffset);
  attribs.setYOffset(vOffset);
  attribs.setRotation(parseFloat());
  attribs.setXScale(parseFloat());
  attribs.setYScale(parseFloat());

  onValveBrushFace(
    location, m_targetMapFormat, p1, p2, p3, attribs, uAxis, vAxis, status);
}

void StandardMapParser::parsePrimitiveFace(ParserStatus& status)
{
  /* const auto line = */ m_tokenizer.line();

  /* const auto [p1, p2, p3] = */ parseFacePoints(status);

  m_tokenizer.nextToken(QuakeMapToken::OParenthesis);

  /* const auto [uAxis, vAxis] = */ parsePrimitiveUVAxes(status);
  m_tokenizer.nextToken(QuakeMapToken::CParenthesis);

  const auto materialName = parseMaterialName(status);

  // TODO 2427: what to set for offset, rotation, scale?!
  auto attribs = mdl::BrushFaceAttributes{materialName};

  // Quake 2 extra info is optional
  if (!m_tokenizer.peekToken().hasType(
        QuakeMapToken::OParenthesis | QuakeMapToken::CBrace | QuakeMapToken::Eof))
  {
    attribs.setSurfaceContents(parseInteger());
    attribs.setSurfaceFlags(parseInteger());
    attribs.setSurfaceValue(parseFloat());
  }

  // TODO 2427: create a brush face
  // brushFace(line, p1, p2, p3, attribs, uAxis, vAxis, status);
}

void StandardMapParser::parsePatch(
  ParserStatus& status, const FileLocation& startLocation)
{
  auto token = m_tokenizer.nextToken(QuakeMapToken::String);
  expect({PatchId, Patch3Id}, token);
  const bool isPatchDef3 = token.data() == Patch3Id;
  m_tokenizer.nextToken(QuakeMapToken::OBrace);

  auto materialName = parseMaterialName(status);
  m_tokenizer.nextToken(QuakeMapToken::OParenthesis);

  /*
  Quake 3 parses the patches a bit differently. In the GtkRadiant source, the first number
  is the column count and the second is the row count, and the points are transposed
  during parsing. Later, when the points are interpreted, radiant puts the origin (the
  first control point) in the bottom left, but we put it in the top left. For the grid
  computed from the this makes no difference as long as the normals are computed
  correctly.

  I chose to interpret the data this way because it seems more intuitive and easier to
  reason about.
  */

  token = m_tokenizer.nextToken(QuakeMapToken::Integer);
  const auto rowLocation = token.location();
  int rowCountInt = token.toInteger<int>();
  if (rowCountInt <= 0)
  {
    status.warn(rowLocation, "Invalid patch height, assuming 1");
    rowCountInt = 1;
  }

  token = m_tokenizer.nextToken(QuakeMapToken::Integer);
  const auto columnLocation = token.location();
  int columnCountInt = token.toInteger<int>();
  if (columnCountInt <= 0)
  {
    status.warn(columnLocation, "Invalid patch width, assuming 1");
    columnCountInt = 1;
  }

  const auto rowCount = static_cast<size_t>(rowCountInt);
  const auto columnCount = static_cast<size_t>(columnCountInt);

  const auto surfaceContents = parseInteger();
  const auto surfaceFlags = parseInteger();
  const auto surfaceValue = parseFloat();
  m_tokenizer.nextToken(QuakeMapToken::CParenthesis);

  auto controlPoints = std::vector<vm::vec<double, 5>>{};
  controlPoints.reserve(columnCount * rowCount);
  auto controlNormals = std::vector<vm::vec3d>{};
  if (isPatchDef3)
  {
    controlNormals.reserve(columnCount * rowCount);
  }

  m_tokenizer.nextToken(QuakeMapToken::OParenthesis);
  for (size_t i = 0; i < size_t(rowCount); ++i)
  {
    m_tokenizer.nextToken(QuakeMapToken::OParenthesis);
    for (size_t j = 0; j < size_t(columnCount); ++j)
    {
      if (isPatchDef3)
      {
        const auto controlPoint = parseFloatVector<8>(
          QuakeMapToken::OParenthesis, QuakeMapToken::CParenthesis);
        controlPoints.push_back(
          {controlPoint[0], controlPoint[1], controlPoint[2], controlPoint[6], controlPoint[7]});
        controlNormals.push_back(vm::slice<3>(controlPoint, 3));
      }
      else
      {
        const auto controlPoint =
          parseFloatVector<5>(QuakeMapToken::OParenthesis, QuakeMapToken::CParenthesis);
        controlPoints.push_back(controlPoint);
      }
    }
    m_tokenizer.nextToken(QuakeMapToken::CParenthesis);
  }
  m_tokenizer.nextToken(QuakeMapToken::CParenthesis);

  const auto sanitizedRowCount =
    sanitizePatchCount(rowCount, "height", rowLocation, status);
  const auto sanitizedColumnCount =
    sanitizePatchCount(columnCount, "width", columnLocation, status);
  if (sanitizedRowCount != rowCount || sanitizedColumnCount != columnCount)
  {
    controlPoints = resizePatchControlPoints(
      rowCount,
      columnCount,
      sanitizedRowCount,
      sanitizedColumnCount,
      controlPoints);
    if (isPatchDef3)
    {
      controlNormals = resizePatchControlNormals(
        rowCount,
        columnCount,
        sanitizedRowCount,
        sanitizedColumnCount,
        controlNormals);
    }
  }

  token = m_tokenizer.nextToken(QuakeMapToken::CBrace);
  onPatch(
    startLocation,
    token.location(),
    m_targetMapFormat,
    sanitizedRowCount,
    sanitizedColumnCount,
    std::move(controlPoints),
    std::move(controlNormals),
    std::move(materialName),
    surfaceContents,
    surfaceFlags,
    surfaceValue,
    status);
}

std::tuple<vm::vec3d, vm::vec3d, vm::vec3d> StandardMapParser::parseFacePoints(
  ParserStatus& /* status */)
{
  const auto p1 =
    correct(parseFloatVector(QuakeMapToken::OParenthesis, QuakeMapToken::CParenthesis));
  const auto p2 =
    correct(parseFloatVector(QuakeMapToken::OParenthesis, QuakeMapToken::CParenthesis));
  const auto p3 =
    correct(parseFloatVector(QuakeMapToken::OParenthesis, QuakeMapToken::CParenthesis));

  return {p1, p2, p3};
}

std::string StandardMapParser::parseMaterialName(ParserStatus& /* status */)
{
  const auto [materialName, wasQuoted] =
    m_tokenizer.readAnyString(QuakeMapTokenizer::Whitespace());
  return wasQuoted ? kdl::str_unescape(materialName, "\"\\") : std::string{materialName};
}

std::tuple<vm::vec3d, float, vm::vec3d, float> StandardMapParser::parseValveUVAxes(
  ParserStatus& /* status */)
{
  const auto firstAxis =
    parseFloatVector<4>(QuakeMapToken::OBracket, QuakeMapToken::CBracket);
  const auto uAxis = firstAxis.xyz();
  const auto uOffset = static_cast<float>(firstAxis.w());

  const auto secondAxis =
    parseFloatVector<4>(QuakeMapToken::OBracket, QuakeMapToken::CBracket);
  const auto vAxis = secondAxis.xyz();
  const auto vOffset = static_cast<float>(secondAxis.w());

  return {uAxis, uOffset, vAxis, vOffset};
}

std::tuple<vm::vec3d, vm::vec3d> StandardMapParser::parsePrimitiveUVAxes(
  ParserStatus& /* status */)
{
  const auto uAxis =
    correct(parseFloatVector(QuakeMapToken::OParenthesis, QuakeMapToken::CParenthesis));
  const auto vAxis =
    correct(parseFloatVector(QuakeMapToken::OParenthesis, QuakeMapToken::CParenthesis));
  return {uAxis, vAxis};
}

float StandardMapParser::parseFloat()
{
  return m_tokenizer.nextToken(QuakeMapToken::Number).toFloat<float>();
}

int StandardMapParser::parseInteger()
{
  return m_tokenizer.nextToken(QuakeMapToken::Integer).toInteger<int>();
}

} // namespace tb::io
