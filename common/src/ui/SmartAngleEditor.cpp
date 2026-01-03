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

#include "SmartAngleEditor.h"

#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "mdl/EntityNodeBase.h"
#include "mdl/Grid.h"
#include "mdl/Map.h"
#include "mdl/Map_Entities.h"
#include "mdl/PropertyDefinition.h"
#include "ui/MapDocument.h"
#include "ui/SpinControl.h"
#include "ui/ViewConstants.h"

#include "kd/contracts.h"
#include "kd/set_temp.h"
#include "kd/string_utils.h"

#include "vm/scalar.h"

#include <limits>

namespace tb::ui
{

SmartAngleEditor::SmartAngleEditor(MapDocument& document, QWidget* parent)
  : SmartPropertyEditor{document, parent}
{
  createGui();

  m_notifierConnection += document.gridDidChangeNotifier.connect(
    this, &SmartAngleEditor::updateIncrements);
  updateIncrements();
}

void SmartAngleEditor::createGui()
{
  contract_pre(m_angle == nullptr);

  auto* label = new QLabel{tr("Angle")};
  m_angle = new SpinControl{};

  const auto max = std::numeric_limits<double>::max();
  const auto min = -max;
  m_angle->setRange(min, max);
  m_angle->setDigits(0, 6);

  connect(
    m_angle,
    QOverload<double>::of(&QDoubleSpinBox::valueChanged),
    this,
    &SmartAngleEditor::angleChanged);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(
    LayoutConstants::WideHMargin,
    LayoutConstants::WideVMargin,
    LayoutConstants::WideHMargin,
    LayoutConstants::WideVMargin);
  layout->setSpacing(LayoutConstants::NarrowVMargin);
  layout->addWidget(label);
  layout->addWidget(m_angle);
  layout->addStretch(1);
  setLayout(layout);
}

void SmartAngleEditor::updateIncrements()
{
  const auto angleStep = vm::to_degrees(document().map().grid().angle());
  m_angle->setIncrements(angleStep != 0.0 ? angleStep : 1.0, 90.0, 1.0);
}

void SmartAngleEditor::angleChanged(const double value)
{
  if (m_ignoreUpdates)
  {
    return;
  }

  addOrUpdateProperty(kdl::str_to_string(value));
}

void SmartAngleEditor::doUpdateVisual(
  const std::vector<mdl::EntityNodeBase*>& nodes)
{
  contract_pre(m_angle != nullptr);

  const auto ignoreUpdates = kdl::set_temp{m_ignoreUpdates};
  const auto blockSignals = QSignalBlocker{m_angle};

  m_angle->setEnabled(false);
  m_angle->setSpecialValueText("");

  if (nodes.empty())
  {
    return;
  }

  const auto* propertyDef = mdl::selectPropertyDefinition(propertyKey(), nodes);
  if (
    !propertyDef
    || !std::holds_alternative<mdl::PropertyValueTypes::Angle>(propertyDef->valueType))
  {
    return;
  }

  m_angle->setEnabled(true);

  std::optional<double> value;
  bool anyValue = false;
  bool anyMissing = false;
  bool mixed = false;

  for (const auto* node : nodes)
  {
    const auto* propValue = node->entity().property(propertyKey());
    if (!propValue)
    {
      anyMissing = true;
      continue;
    }

    anyValue = true;
    const auto parsed = kdl::str_to_float(*propValue);
    if (!parsed)
    {
      mixed = true;
      break;
    }

    if (!value)
    {
      value = *parsed;
    }
    else if (*value != *parsed)
    {
      mixed = true;
      break;
    }
  }

  if (!mixed && anyMissing && anyValue)
  {
    mixed = true;
  }

  if (mixed)
  {
    m_angle->setSpecialValueText("multi");
    m_angle->setValue(m_angle->minimum());
  }
  else
  {
    m_angle->setValue(value.value_or(0.0));
  }
}

} // namespace tb::ui
