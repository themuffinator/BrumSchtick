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

#include "BrushBuilderToolPage.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

#include "ui/BorderLine.h"
#include "ui/QtUtils.h"
#include "ui/SpinControl.h"
#include "ui/ViewConstants.h"

#include "vm/vec.h"

namespace tb::ui
{
namespace
{
SpinControl* createSpinControl(const double min, const double max, QWidget* parent = nullptr)
{
  auto* control = new SpinControl{parent};
  control->setRange(min, max);
  control->setDigits(0, 3);
  return control;
}
} // namespace

BrushBuilderToolPage::BrushBuilderToolPage(BrushBuilderTool& tool, QWidget* parent)
  : QWidget{parent}
  , m_tool{tool}
{
  createGui();
  connectObservers();
  updateShapeStatus();
  updateStepsList();
  updateStepEditor();
  updateStepButtons();
  updateApplyState();
}

void BrushBuilderToolPage::createGui()
{
  auto* titleLabel = new QLabel{tr("Brush Builder")};
  m_shapeStatusLabel = new QLabel{};
  makeSmall(m_shapeStatusLabel);

  m_snapToGridCheck = new QCheckBox{tr("Snap to grid")};
  m_snapToIntegerCheck = new QCheckBox{tr("Snap to integer")};

  m_clearShapeButton = new QPushButton{tr("Clear Shape")};
  m_applyButton = new QPushButton{tr("Create")};

  connect(m_snapToGridCheck, &QCheckBox::toggled, this, [&](const bool checked) {
    if (m_updating)
    {
      return;
    }
    m_tool.setSnapToGrid(checked);
  });
  connect(m_snapToIntegerCheck, &QCheckBox::toggled, this, [&](const bool checked) {
    if (m_updating)
    {
      return;
    }
    m_tool.setSnapToInteger(checked);
  });
  connect(m_clearShapeButton, &QPushButton::clicked, this, [&]() {
    m_tool.clearShape();
  });
  connect(m_applyButton, &QPushButton::clicked, this, [&]() {
    m_tool.createBrushes();
  });

  m_stepsList = new QListWidget{};
  m_stepsList->setSelectionMode(QAbstractItemView::SingleSelection);
  m_stepsList->setMinimumWidth(200);
  m_stepsList->setMaximumHeight(110);
  connect(m_stepsList, &QListWidget::currentRowChanged, this, [&]() {
    updateStepEditor();
    updateStepButtons();
  });

  m_addStepButton = new QToolButton{};
  m_addStepButton->setText(tr("Add"));
  m_addStepButton->setPopupMode(QToolButton::MenuButtonPopup);
  auto* addMenu = new QMenu{m_addStepButton};
  addMenu->addAction(tr("Translate"), [this]() {
    addStep(BrushBuilderTool::TransformType::Translation);
  });
  addMenu->addAction(tr("Rotate"), [this]() {
    addStep(BrushBuilderTool::TransformType::Rotation);
  });
  addMenu->addAction(tr("Scale"), [this]() {
    addStep(BrushBuilderTool::TransformType::Scaling);
  });
  addMenu->addAction(tr("Matrix"), [this]() {
    addStep(BrushBuilderTool::TransformType::Matrix);
  });
  addMenu->addAction(tr("Expression"), [this]() {
    addStep(BrushBuilderTool::TransformType::Expression);
  });
  m_addStepButton->setMenu(addMenu);
  connect(m_addStepButton, &QToolButton::clicked, this, [this]() {
    addStep(BrushBuilderTool::TransformType::Translation);
  });

  m_removeStepButton = new QPushButton{tr("Remove")};
  m_moveStepUpButton = new QPushButton{tr("Up")};
  m_moveStepDownButton = new QPushButton{tr("Down")};

  connect(m_removeStepButton, &QPushButton::clicked, this, [&]() {
    const auto index = currentStepIndex();
    if (index >= 0)
    {
      m_tool.removeStep(static_cast<size_t>(index));
    }
  });
  connect(m_moveStepUpButton, &QPushButton::clicked, this, [&]() {
    const auto index = currentStepIndex();
    if (index >= 0)
    {
      m_tool.moveStepUp(static_cast<size_t>(index));
    }
  });
  connect(m_moveStepDownButton, &QPushButton::clicked, this, [&]() {
    const auto index = currentStepIndex();
    if (index >= 0)
    {
      m_tool.moveStepDown(static_cast<size_t>(index));
    }
  });

  m_stepTypeCombo = new QComboBox{};
  m_stepTypeCombo->addItem(stepTypeLabel(BrushBuilderTool::TransformType::Translation));
  m_stepTypeCombo->addItem(stepTypeLabel(BrushBuilderTool::TransformType::Rotation));
  m_stepTypeCombo->addItem(stepTypeLabel(BrushBuilderTool::TransformType::Scaling));
  m_stepTypeCombo->addItem(stepTypeLabel(BrushBuilderTool::TransformType::Matrix));
  m_stepTypeCombo->addItem(stepTypeLabel(BrushBuilderTool::TransformType::Expression));

  m_stepEnabledCheck = new QCheckBox{tr("Enabled")};
  m_stepSubdivisionsSpin = new QSpinBox{};
  m_stepSubdivisionsSpin->setRange(1, 1024);

  connect(
    m_stepTypeCombo,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    [&](const int index) {
      updateCurrentStep([&](auto& step) {
        step.type = static_cast<BrushBuilderTool::TransformType>(index);
      });
    });
  connect(m_stepEnabledCheck, &QCheckBox::toggled, this, [&](const bool checked) {
    updateCurrentStep([&](auto& step) { step.enabled = checked; });
  });
  connect(
    m_stepSubdivisionsSpin,
    QOverload<int>::of(&QSpinBox::valueChanged),
    this,
    [&](const int value) {
      updateCurrentStep(
        [&](auto& step) { step.subdivisions = static_cast<size_t>(value); });
    });

  m_translationX = createSpinControl(-999999.0, 999999.0, this);
  m_translationY = createSpinControl(-999999.0, 999999.0, this);
  m_translationZ = createSpinControl(-999999.0, 999999.0, this);

  const auto updateTranslation = [this]() {
    updateCurrentStep([&](auto& step) {
      step.translation = vm::vec3d{
        m_translationX->value(), m_translationY->value(), m_translationZ->value()};
    });
  };
  connect(m_translationX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, updateTranslation);
  connect(m_translationY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, updateTranslation);
  connect(m_translationZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, updateTranslation);

  m_rotationAxisCombo = new QComboBox{};
  m_rotationAxisCombo->addItem("X");
  m_rotationAxisCombo->addItem("Y");
  m_rotationAxisCombo->addItem("Z");
  m_rotationAngleSpin = createSpinControl(-360.0, 360.0, this);

  connect(
    m_rotationAxisCombo,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    [&](const int index) {
      updateCurrentStep([&](auto& step) {
        step.rotationAxis = static_cast<vm::axis::type>(index);
      });
    });
  connect(
    m_rotationAngleSpin,
    QOverload<double>::of(&QDoubleSpinBox::valueChanged),
    this,
    [&](const double value) {
      updateCurrentStep([&](auto& step) { step.rotationAngle = value; });
    });

  m_scaleX = createSpinControl(-999999.0, 999999.0, this);
  m_scaleY = createSpinControl(-999999.0, 999999.0, this);
  m_scaleZ = createSpinControl(-999999.0, 999999.0, this);

  const auto updateScale = [this]() {
    updateCurrentStep([&](auto& step) {
      step.scale = vm::vec3d{m_scaleX->value(), m_scaleY->value(), m_scaleZ->value()};
    });
  };
  connect(m_scaleX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, updateScale);
  connect(m_scaleY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, updateScale);
  connect(m_scaleZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, updateScale);

  for (auto& edit : m_matrixEdits)
  {
    edit = createSpinControl(-999999.0, 999999.0, this);
    edit->setMaximumWidth(70);
  }

  const auto updateMatrix = [this]() {
    updateCurrentStep([&](auto& step) {
      for (size_t r = 0; r < 4; ++r)
      {
        for (size_t c = 0; c < 4; ++c)
        {
          step.matrix[c][r] = m_matrixEdits[r * 4 + c]->value();
        }
      }
    });
  };
  for (auto* edit : m_matrixEdits)
  {
    connect(edit, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, updateMatrix);
  }

  m_expressionX = new QLineEdit{};
  m_expressionY = new QLineEdit{};
  m_expressionZ = new QLineEdit{};
  m_expressionX->setPlaceholderText("x");
  m_expressionY->setPlaceholderText("y");
  m_expressionZ->setPlaceholderText("z");

  const auto updateExpression = [this]() {
    updateCurrentStep([&](auto& step) {
      step.expression.xExpression = m_expressionX->text().toStdString();
      step.expression.yExpression = m_expressionY->text().toStdString();
      step.expression.zExpression = m_expressionZ->text().toStdString();
    });
  };
  connect(m_expressionX, &QLineEdit::editingFinished, this, updateExpression);
  connect(m_expressionY, &QLineEdit::editingFinished, this, updateExpression);
  connect(m_expressionZ, &QLineEdit::editingFinished, this, updateExpression);

  auto* translationWidget = new QWidget{};
  auto* translationLayout = new QGridLayout{};
  translationLayout->setContentsMargins(0, 0, 0, 0);
  translationLayout->setHorizontalSpacing(LayoutConstants::NarrowHMargin);
  translationLayout->setVerticalSpacing(LayoutConstants::NarrowVMargin);
  translationLayout->addWidget(new QLabel{tr("Translate")}, 0, 0);
  translationLayout->addWidget(new QLabel{"X"}, 0, 1);
  translationLayout->addWidget(m_translationX, 0, 2);
  translationLayout->addWidget(new QLabel{"Y"}, 0, 3);
  translationLayout->addWidget(m_translationY, 0, 4);
  translationLayout->addWidget(new QLabel{"Z"}, 0, 5);
  translationLayout->addWidget(m_translationZ, 0, 6);
  translationLayout->setColumnStretch(7, 1);
  translationWidget->setLayout(translationLayout);

  auto* rotationWidget = new QWidget{};
  auto* rotationLayout = new QHBoxLayout{};
  rotationLayout->setContentsMargins(0, 0, 0, 0);
  rotationLayout->setSpacing(LayoutConstants::NarrowHMargin);
  rotationLayout->addWidget(new QLabel{tr("Axis")});
  rotationLayout->addWidget(m_rotationAxisCombo);
  rotationLayout->addWidget(new QLabel{tr("Angle")});
  rotationLayout->addWidget(m_rotationAngleSpin);
  rotationLayout->addStretch(1);
  rotationWidget->setLayout(rotationLayout);

  auto* scaleWidget = new QWidget{};
  auto* scaleLayout = new QGridLayout{};
  scaleLayout->setContentsMargins(0, 0, 0, 0);
  scaleLayout->setHorizontalSpacing(LayoutConstants::NarrowHMargin);
  scaleLayout->setVerticalSpacing(LayoutConstants::NarrowVMargin);
  scaleLayout->addWidget(new QLabel{tr("Scale")}, 0, 0);
  scaleLayout->addWidget(new QLabel{"X"}, 0, 1);
  scaleLayout->addWidget(m_scaleX, 0, 2);
  scaleLayout->addWidget(new QLabel{"Y"}, 0, 3);
  scaleLayout->addWidget(m_scaleY, 0, 4);
  scaleLayout->addWidget(new QLabel{"Z"}, 0, 5);
  scaleLayout->addWidget(m_scaleZ, 0, 6);
  scaleLayout->setColumnStretch(7, 1);
  scaleWidget->setLayout(scaleLayout);

  auto* matrixWidget = new QWidget{};
  auto* matrixLayout = new QGridLayout{};
  matrixLayout->setContentsMargins(0, 0, 0, 0);
  matrixLayout->setHorizontalSpacing(LayoutConstants::NarrowHMargin);
  matrixLayout->setVerticalSpacing(LayoutConstants::NarrowVMargin);
  for (size_t r = 0; r < 4; ++r)
  {
    for (size_t c = 0; c < 4; ++c)
    {
      matrixLayout->addWidget(m_matrixEdits[r * 4 + c], int(r), int(c));
    }
  }
  matrixWidget->setLayout(matrixLayout);

  auto* expressionWidget = new QWidget{};
  auto* expressionLayout = new QGridLayout{};
  expressionLayout->setContentsMargins(0, 0, 0, 0);
  expressionLayout->setHorizontalSpacing(LayoutConstants::NarrowHMargin);
  expressionLayout->setVerticalSpacing(LayoutConstants::NarrowVMargin);
  expressionLayout->addWidget(new QLabel{"X"}, 0, 0);
  expressionLayout->addWidget(m_expressionX, 0, 1);
  expressionLayout->addWidget(new QLabel{"Y"}, 0, 2);
  expressionLayout->addWidget(m_expressionY, 0, 3);
  expressionLayout->addWidget(new QLabel{"Z"}, 0, 4);
  expressionLayout->addWidget(m_expressionZ, 0, 5);
  expressionLayout->setColumnStretch(6, 1);
  expressionWidget->setLayout(expressionLayout);

  m_stepEditorStack = new QStackedWidget{};
  m_stepEditorStack->addWidget(translationWidget);
  m_stepEditorStack->addWidget(rotationWidget);
  m_stepEditorStack->addWidget(scaleWidget);
  m_stepEditorStack->addWidget(matrixWidget);
  m_stepEditorStack->addWidget(expressionWidget);

  auto* shapeLayout = new QVBoxLayout{};
  shapeLayout->setContentsMargins(0, 0, 0, 0);
  shapeLayout->setSpacing(LayoutConstants::NarrowVMargin);
  shapeLayout->addWidget(titleLabel);
  shapeLayout->addWidget(m_shapeStatusLabel);
  auto* snapLayout = new QHBoxLayout{};
  snapLayout->setContentsMargins(0, 0, 0, 0);
  snapLayout->setSpacing(LayoutConstants::NarrowHMargin);
  snapLayout->addWidget(m_snapToGridCheck);
  snapLayout->addWidget(m_snapToIntegerCheck);
  snapLayout->addStretch(1);
  shapeLayout->addLayout(snapLayout);
  auto* shapeButtonLayout = new QHBoxLayout{};
  shapeButtonLayout->setContentsMargins(0, 0, 0, 0);
  shapeButtonLayout->setSpacing(LayoutConstants::NarrowHMargin);
  shapeButtonLayout->addWidget(m_clearShapeButton);
  shapeButtonLayout->addWidget(m_applyButton);
  shapeButtonLayout->addStretch(1);
  shapeLayout->addLayout(shapeButtonLayout);

  auto* stepsLayout = new QVBoxLayout{};
  stepsLayout->setContentsMargins(0, 0, 0, 0);
  stepsLayout->setSpacing(LayoutConstants::NarrowVMargin);
  stepsLayout->addWidget(new QLabel{tr("Steps")});
  stepsLayout->addWidget(m_stepsList);
  auto* stepsButtonsLayout = new QHBoxLayout{};
  stepsButtonsLayout->setContentsMargins(0, 0, 0, 0);
  stepsButtonsLayout->setSpacing(LayoutConstants::NarrowHMargin);
  stepsButtonsLayout->addWidget(m_addStepButton);
  stepsButtonsLayout->addWidget(m_removeStepButton);
  stepsButtonsLayout->addWidget(m_moveStepUpButton);
  stepsButtonsLayout->addWidget(m_moveStepDownButton);
  stepsButtonsLayout->addStretch(1);
  stepsLayout->addLayout(stepsButtonsLayout);

  auto* editorLayout = new QVBoxLayout{};
  editorLayout->setContentsMargins(0, 0, 0, 0);
  editorLayout->setSpacing(LayoutConstants::NarrowVMargin);
  auto* editorHeaderLayout = new QHBoxLayout{};
  editorHeaderLayout->setContentsMargins(0, 0, 0, 0);
  editorHeaderLayout->setSpacing(LayoutConstants::NarrowHMargin);
  editorHeaderLayout->addWidget(new QLabel{tr("Type")});
  editorHeaderLayout->addWidget(m_stepTypeCombo);
  editorHeaderLayout->addWidget(m_stepEnabledCheck);
  editorHeaderLayout->addWidget(new QLabel{tr("Subdivs")});
  editorHeaderLayout->addWidget(m_stepSubdivisionsSpin);
  editorHeaderLayout->addStretch(1);
  editorLayout->addLayout(editorHeaderLayout);
  editorLayout->addWidget(m_stepEditorStack);

  auto* layout = new QHBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(LayoutConstants::WideHMargin);
  layout->addLayout(shapeLayout);
  layout->addWidget(new BorderLine{BorderLine::Direction::Vertical});
  layout->addLayout(stepsLayout);
  layout->addWidget(new BorderLine{BorderLine::Direction::Vertical});
  layout->addLayout(editorLayout);
  layout->addStretch(1);
  setLayout(layout);
}

void BrushBuilderToolPage::connectObservers()
{
  m_notifierConnection += m_tool.shapeDidChangeNotifier.connect(this, [&]() {
    updateShapeStatus();
    updateApplyState();
  });
  m_notifierConnection += m_tool.stepsDidChangeNotifier.connect(this, [&]() {
    updateStepsList();
    updateStepEditor();
    updateStepButtons();
    updateApplyState();
  });
}

void BrushBuilderToolPage::updateShapeStatus()
{
  m_updating = true;
  size_t openPolygons = 0u;
  size_t closedPolygons = 0u;
  for (const auto& polygon : m_tool.polygons())
  {
    if (polygon.closed)
    {
      ++closedPolygons;
    }
    else
    {
      ++openPolygons;
    }
  }

  if (openPolygons == 0u && closedPolygons == 0u)
  {
    m_shapeStatusLabel->setText(tr("No shape"));
  }
  else
  {
    m_shapeStatusLabel->setText(
      tr("Closed %1, Open %2").arg(closedPolygons).arg(openPolygons));
  }

  m_snapToGridCheck->setChecked(m_tool.snapToGrid());
  m_snapToIntegerCheck->setChecked(m_tool.snapToInteger());
  m_clearShapeButton->setEnabled(!m_tool.polygons().empty());
  m_updating = false;
}

void BrushBuilderToolPage::updateApplyState()
{
  m_applyButton->setEnabled(m_tool.hasClosedPolygons() && !m_tool.steps().empty());
}

void BrushBuilderToolPage::updateStepsList()
{
  m_updating = true;
  const int previousIndex = m_stepsList->currentRow();
  QSignalBlocker blocker(m_stepsList);
  m_stepsList->clear();

  const auto& steps = m_tool.steps();
  for (size_t i = 0; i < steps.size(); ++i)
  {
    auto label = stepTypeLabel(steps[i].type);
    if (!steps[i].enabled)
    {
      label += tr(" (disabled)");
    }
    m_stepsList->addItem(tr("%1. %2").arg(i + 1).arg(label));
  }

  if (!steps.empty())
  {
    const auto index = std::clamp(
      previousIndex >= 0 ? previousIndex : 0, 0, int(steps.size() - 1));
    m_stepsList->setCurrentRow(index);
  }
  else
  {
    m_stepsList->setCurrentRow(-1);
  }
  m_updating = false;
}

void BrushBuilderToolPage::updateStepEditor()
{
  const auto index = currentStepIndex();
  const auto& steps = m_tool.steps();
  const auto hasSelection =
    index >= 0 && static_cast<size_t>(index) < steps.size();

  m_stepTypeCombo->setEnabled(hasSelection);
  m_stepEnabledCheck->setEnabled(hasSelection);
  m_stepSubdivisionsSpin->setEnabled(hasSelection);
  m_stepEditorStack->setEnabled(hasSelection);

  if (!hasSelection)
  {
    return;
  }

  m_updating = true;
  const auto& step = steps[static_cast<size_t>(index)];
  m_stepTypeCombo->setCurrentIndex(stepTypeIndex(step.type));
  m_stepEnabledCheck->setChecked(step.enabled);
  m_stepSubdivisionsSpin->setValue(static_cast<int>(step.subdivisions));

  m_translationX->setValue(step.translation.x());
  m_translationY->setValue(step.translation.y());
  m_translationZ->setValue(step.translation.z());

  m_rotationAxisCombo->setCurrentIndex(static_cast<int>(step.rotationAxis));
  m_rotationAngleSpin->setValue(step.rotationAngle);

  m_scaleX->setValue(step.scale.x());
  m_scaleY->setValue(step.scale.y());
  m_scaleZ->setValue(step.scale.z());

  for (size_t r = 0; r < 4; ++r)
  {
    for (size_t c = 0; c < 4; ++c)
    {
      m_matrixEdits[r * 4 + c]->setValue(step.matrix[c][r]);
    }
  }

  m_expressionX->setText(QString::fromStdString(step.expression.xExpression));
  m_expressionY->setText(QString::fromStdString(step.expression.yExpression));
  m_expressionZ->setText(QString::fromStdString(step.expression.zExpression));

  m_stepEditorStack->setCurrentIndex(stepTypeIndex(step.type));
  m_updating = false;
}

void BrushBuilderToolPage::updateStepButtons()
{
  const auto index = currentStepIndex();
  const auto count = static_cast<int>(m_tool.steps().size());
  const auto hasSelection = index >= 0 && index < count;
  m_removeStepButton->setEnabled(hasSelection);
  m_moveStepUpButton->setEnabled(hasSelection && index > 0);
  m_moveStepDownButton->setEnabled(hasSelection && (index + 1) < count);
}

int BrushBuilderToolPage::currentStepIndex() const
{
  return m_stepsList->currentRow();
}

void BrushBuilderToolPage::addStep(const BrushBuilderTool::TransformType type)
{
  BrushBuilderTool::TransformStep step;
  step.type = type;
  if (type == BrushBuilderTool::TransformType::Translation)
  {
    step.translation = vm::vec3d{0.0, 0.0, 64.0};
  }
  m_tool.addStep(step);
}

void BrushBuilderToolPage::updateCurrentStep(
  const std::function<void(BrushBuilderTool::TransformStep&)>& fn)
{
  if (m_updating)
  {
    return;
  }
  const auto index = currentStepIndex();
  if (index < 0)
  {
    return;
  }

  const auto& steps = m_tool.steps();
  if (static_cast<size_t>(index) >= steps.size())
  {
    return;
  }

  auto step = steps[static_cast<size_t>(index)];
  fn(step);
  m_tool.updateStep(static_cast<size_t>(index), step);
}

QString BrushBuilderToolPage::stepTypeLabel(const BrushBuilderTool::TransformType type)
{
  switch (type)
  {
  case BrushBuilderTool::TransformType::Translation:
    return tr("Translate");
  case BrushBuilderTool::TransformType::Rotation:
    return tr("Rotate");
  case BrushBuilderTool::TransformType::Scaling:
    return tr("Scale");
  case BrushBuilderTool::TransformType::Matrix:
    return tr("Matrix");
  case BrushBuilderTool::TransformType::Expression:
    return tr("Expression");
  }

  return tr("Unknown");
}

int BrushBuilderToolPage::stepTypeIndex(const BrushBuilderTool::TransformType type)
{
  return static_cast<int>(type);
}

} // namespace tb::ui
