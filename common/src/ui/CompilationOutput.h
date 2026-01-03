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

#pragma once

#include <QTextEdit>

#include <cstddef>

class QEvent;
class QMouseEvent;
class QPoint;
class QSyntaxHighlighter;

namespace tb::ui
{
class MapFrame;

class CompilationOutput : public QTextEdit
{
  Q_OBJECT
private:
  MapFrame* m_mapFrame = nullptr;
  QSyntaxHighlighter* m_highlighter = nullptr;

public:
  explicit CompilationOutput(MapFrame* mapFrame, QWidget* parent = nullptr);

protected:
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;

private:
  bool handleAnchorAt(const QPoint& pos);
  void updateHoverCursor(const QPoint& pos);
  void selectLineNumber(std::size_t lineNumber);
};

} // namespace tb::ui
