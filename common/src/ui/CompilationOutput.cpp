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

#include "CompilationOutput.h"

#include <QMouseEvent>
#include <QPalette>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextDocument>

#include "mdl/Map_Selection.h"
#include "ui/MapDocument.h"
#include "ui/MapFrame.h"

#include "kd/contracts.h"

#include <vector>

namespace tb::ui
{
namespace
{
const auto kLineAnchorPrefix = QStringLiteral("line:");

struct LineMatchPattern
{
  QRegularExpression regex;
  int lineGroup = 1;
};

class CompilationOutputHighlighter : public QSyntaxHighlighter
{
private:
  std::vector<LineMatchPattern> m_patterns;
  QTextCharFormat m_linkFormat;

public:
  explicit CompilationOutputHighlighter(QTextDocument* document, const QColor& linkColor)
    : QSyntaxHighlighter{document}
  {
    m_linkFormat.setForeground(linkColor);
    m_linkFormat.setFontUnderline(true);
    m_linkFormat.setUnderlineColor(linkColor);
    m_linkFormat.setAnchor(true);

    m_patterns = {
      {QRegularExpression{
         R"(\bline\b(?:\s+|\s*:\s*)(\d+)\b)",
         QRegularExpression::CaseInsensitiveOption},
       1},
      {QRegularExpression{
         R"(\.map\s*:\s*(\d+)\b)", QRegularExpression::CaseInsensitiveOption},
       1}};
  }

protected:
  void highlightBlock(const QString& text) override
  {
    for (const auto& pattern : m_patterns)
    {
      auto matches = pattern.regex.globalMatch(text);
      while (matches.hasNext())
      {
        const auto match = matches.next();
        const auto numberText = match.captured(pattern.lineGroup);
        if (numberText.isEmpty())
        {
          continue;
        }

        auto ok = false;
        const auto lineNumber = numberText.toLongLong(&ok);
        if (!ok || lineNumber <= 0)
        {
          continue;
        }

        auto format = m_linkFormat;
        format.setAnchorHref(kLineAnchorPrefix + numberText);
        setFormat(
          match.capturedStart(pattern.lineGroup),
          match.capturedLength(pattern.lineGroup),
          format);
      }
    }
  }
};

} // namespace

CompilationOutput::CompilationOutput(MapFrame* mapFrame, QWidget* parent)
  : QTextEdit{parent}
  , m_mapFrame{mapFrame}
{
  contract_pre(mapFrame != nullptr);

  setReadOnly(true);
  setUndoRedoEnabled(false);
  setMouseTracking(true);
  viewport()->setMouseTracking(true);

  m_highlighter =
    new CompilationOutputHighlighter{document(), palette().color(QPalette::Link)};
}

void CompilationOutput::mouseReleaseEvent(QMouseEvent* event)
{
  QTextEdit::mouseReleaseEvent(event);

  if (event->button() == Qt::LeftButton && !textCursor().hasSelection())
  {
    handleAnchorAt(event->pos());
  }
}

void CompilationOutput::mouseMoveEvent(QMouseEvent* event)
{
  QTextEdit::mouseMoveEvent(event);
  updateHoverCursor(event->pos());
}

void CompilationOutput::leaveEvent(QEvent* event)
{
  QTextEdit::leaveEvent(event);
  viewport()->unsetCursor();
}

bool CompilationOutput::handleAnchorAt(const QPoint& pos)
{
  const auto anchor = anchorAt(pos);
  if (!anchor.startsWith(kLineAnchorPrefix))
  {
    return false;
  }

  const auto numberText = anchor.mid(kLineAnchorPrefix.size());
  auto ok = false;
  const auto lineNumber = numberText.toLongLong(&ok);
  if (!ok || lineNumber <= 0)
  {
    return false;
  }

  selectLineNumber(static_cast<std::size_t>(lineNumber));
  return true;
}

void CompilationOutput::updateHoverCursor(const QPoint& pos)
{
  const auto anchor = anchorAt(pos);
  if (anchor.startsWith(kLineAnchorPrefix))
  {
    viewport()->setCursor(Qt::PointingHandCursor);
  }
  else
  {
    viewport()->unsetCursor();
  }
}

void CompilationOutput::selectLineNumber(const std::size_t lineNumber)
{
  if (!m_mapFrame || !m_mapFrame->canSelect())
  {
    return;
  }

  selectNodesWithFilePosition(m_mapFrame->document().map(), {lineNumber});
}

} // namespace tb::ui
