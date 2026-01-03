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

#include "SmartVectorEditor.h"

#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "mdl/EntityNodeBase.h"
#include "mdl/Grid.h"
#include "mdl/Map.h"
#include "mdl/PropertyDefinition.h"
#include "ui/MapDocument.h"
#include "ui/SpinControl.h"
#include "ui/ViewConstants.h"

#include "kd/contracts.h"
#include "kd/set_temp.h"
#include "kd/string_utils.h"

#include "vm/vec.h"
#include "vm/vec_io.h"

#include <array>
#include <limits>

namespace tb::ui
{

SmartVectorEditor::SmartVectorEditor(MapDocument& document, QWidget* parent)
  : SmartPropertyEditor{document, parent}
{
  createGui();

  m_notifierConnection += document.gridDidChangeNotifier.connect(
    this, &SmartVectorEditor::updateIncrements);
  updateIncrements();
}

void SmartVectorEditor::createGui()
{
  contract_pre(m_x == nullptr);
  contract_pre(m_y == nullptr);
  contract_pre(m_z == nullptr);
  contract_pre(m_pickButton == nullptr);

  const auto max = std::numeric_limits<double>::max();
  const auto min = -max;

  auto* xLabel = new QLabel{tr("X")};
  auto* yLabel = new QLabel{tr("Y")};
  auto* zLabel = new QLabel{tr("Z")};

  m_x = new SpinControl{};
  m_y = new SpinControl{};
  m_z = new SpinControl{};

  for (auto* control : {m_x, m_y, m_z})
  {
    control->setRange(min, max);
    control->setDigits(0, 6);
    connect(
      control,
      QOverload<double>::of(&QDoubleSpinBox::valueChanged),
      this,
      &SmartVectorEditor::vectorChanged);
  }

  m_pickButton = new QPushButton{tr("Pick in map")};
  m_pickButton->setToolTip(tr("Pick a position in the map view."));
  connect(m_pickButton, &QPushButton::clicked, this, &SmartVectorEditor::pickInMap);

  auto* grid = new QGridLayout{};
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setSpacing(LayoutConstants::NarrowVMargin);
  grid->addWidget(xLabel, 0, 0);
  grid->addWidget(m_x, 0, 1);
  grid->addWidget(yLabel, 1, 0);
  grid->addWidget(m_y, 1, 1);
  grid->addWidget(zLabel, 2, 0);
  grid->addWidget(m_z, 2, 1);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(
    LayoutConstants::WideHMargin,
    LayoutConstants::WideVMargin,
    LayoutConstants::WideHMargin,
    LayoutConstants::WideVMargin);
  layout->setSpacing(LayoutConstants::NarrowVMargin);
  layout->addLayout(grid);
  layout->addWidget(m_pickButton);
  layout->addStretch(1);
  setLayout(layout);
}

void SmartVectorEditor::updateIncrements()
{
  const auto gridSize = document().map().grid().actualSize();
  const auto regular = gridSize != 0.0 ? gridSize : 1.0;

  for (auto* control : {m_x, m_y, m_z})
  {
    control->setIncrements(regular, 2.0 * regular, 1.0);
  }
}

void SmartVectorEditor::vectorChanged()
{
  if (m_ignoreUpdates)
  {
    return;
  }

  const auto vec = vm::vec3d{m_x->value(), m_y->value(), m_z->value()};
  addOrUpdateProperty(kdl::str_to_string(vm::correct(vec)));
}

void SmartVectorEditor::pickInMap()
{
  document().startEntityPropertyPick(propertyKey(), nodes());
}

void SmartVectorEditor::updateValueControl(
  SpinControl* control, const bool multi, const std::optional<double> value)
{
  if (multi)
  {
    control->setSpecialValueText("multi");
    control->setValue(control->minimum());
  }
  else
  {
    control->setSpecialValueText("");
    control->setValue(value.value_or(0.0));
  }
}

void SmartVectorEditor::doUpdateVisual(
  const std::vector<mdl::EntityNodeBase*>& nodes)
{
  contract_pre(m_x != nullptr);
  contract_pre(m_y != nullptr);
  contract_pre(m_z != nullptr);
  contract_pre(m_pickButton != nullptr);

  const auto ignoreUpdates = kdl::set_temp{m_ignoreUpdates};
  const auto blockX = QSignalBlocker{m_x};
  const auto blockY = QSignalBlocker{m_y};
  const auto blockZ = QSignalBlocker{m_z};

  for (auto* control : {m_x, m_y, m_z})
  {
    control->setEnabled(false);
    control->setSpecialValueText("");
  }
  m_pickButton->setVisible(false);
  m_pickButton->setEnabled(false);

  if (nodes.empty())
  {
    return;
  }

  const auto* propertyDef = mdl::selectPropertyDefinition(propertyKey(), nodes);
  if (!propertyDef)
  {
    return;
  }

  const auto isOrigin =
    std::holds_alternative<mdl::PropertyValueTypes::Origin>(propertyDef->valueType);
  const auto isVector =
    std::holds_alternative<mdl::PropertyValueTypes::Vector>(propertyDef->valueType);

  if (!isOrigin && !isVector)
  {
    return;
  }

  for (auto* control : {m_x, m_y, m_z})
  {
    control->setEnabled(true);
  }

  m_pickButton->setVisible(isOrigin);
  m_pickButton->setEnabled(isOrigin);

  std::array<std::optional<double>, 3> values{};
  std::array<bool, 3> multi = {false, false, false};
  bool anyValue = false;
  bool anyMissing = false;
  bool parseFailed = false;

  for (const auto* node : nodes)
  {
    const auto* propValue = node->entity().property(propertyKey());
    if (!propValue)
    {
      anyMissing = true;
      continue;
    }

    anyValue = true;
    const auto parsed = vm::parse<double, 3>(*propValue);
    if (!parsed)
    {
      parseFailed = true;
      break;
    }

    for (size_t i = 0; i < 3; ++i)
    {
      if (!values[i])
      {
        values[i] = (*parsed)[i];
      }
      else if (*values[i] != (*parsed)[i])
      {
        multi[i] = true;
      }
    }
  }

  if (parseFailed || (anyMissing && anyValue))
  {
    multi = {true, true, true};
  }

  updateValueControl(m_x, multi[0], values[0]);
  updateValueControl(m_y, multi[1], values[1]);
  updateValueControl(m_z, multi[2], values[2]);
}

} // namespace tb::ui
