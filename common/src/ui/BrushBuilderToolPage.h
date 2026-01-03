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

#include <QWidget>

#include "NotifierConnection.h"
#include "ui/BrushBuilderTool.h"

#include <array>
#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QToolButton;

namespace tb::ui
{
class SpinControl;

class BrushBuilderToolPage : public QWidget
{
  Q_OBJECT
private:
  BrushBuilderTool& m_tool;
  NotifierConnection m_notifierConnection;
  bool m_updating = false;

  QLabel* m_shapeStatusLabel = nullptr;
  QCheckBox* m_snapToGridCheck = nullptr;
  QCheckBox* m_snapToIntegerCheck = nullptr;
  QPushButton* m_clearShapeButton = nullptr;
  QPushButton* m_applyButton = nullptr;

  QListWidget* m_stepsList = nullptr;
  QToolButton* m_addStepButton = nullptr;
  QPushButton* m_removeStepButton = nullptr;
  QPushButton* m_moveStepUpButton = nullptr;
  QPushButton* m_moveStepDownButton = nullptr;

  QComboBox* m_stepTypeCombo = nullptr;
  QCheckBox* m_stepEnabledCheck = nullptr;
  QSpinBox* m_stepSubdivisionsSpin = nullptr;
  QStackedWidget* m_stepEditorStack = nullptr;

  SpinControl* m_translationX = nullptr;
  SpinControl* m_translationY = nullptr;
  SpinControl* m_translationZ = nullptr;

  QComboBox* m_rotationAxisCombo = nullptr;
  SpinControl* m_rotationAngleSpin = nullptr;

  SpinControl* m_scaleX = nullptr;
  SpinControl* m_scaleY = nullptr;
  SpinControl* m_scaleZ = nullptr;

  std::array<SpinControl*, 16> m_matrixEdits{};

  QLineEdit* m_expressionX = nullptr;
  QLineEdit* m_expressionY = nullptr;
  QLineEdit* m_expressionZ = nullptr;

public:
  BrushBuilderToolPage(BrushBuilderTool& tool, QWidget* parent = nullptr);

private:
  void createGui();
  void connectObservers();

  void updateShapeStatus();
  void updateApplyState();
  void updateStepsList();
  void updateStepEditor();
  void updateStepButtons();

  int currentStepIndex() const;
  void addStep(BrushBuilderTool::TransformType type);
  void updateCurrentStep(const std::function<void(BrushBuilderTool::TransformStep&)>& fn);

  static QString stepTypeLabel(BrushBuilderTool::TransformType type);
  static int stepTypeIndex(BrushBuilderTool::TransformType type);
};

} // namespace tb::ui
