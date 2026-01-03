/*
 Copyright (C) 2023 Kristian Duske

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

#include "DrawShapeToolExtensions.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>

#include <cmath>

#include "Error.h"
#include "mdl/BrushBuilder.h"
#include "mdl/GameConfig.h"
#include "mdl/GameInfo.h"
#include "mdl/Map.h"
#include "mdl/WorldNode.h"
#include "ui/MapDocument.h"
#include "ui/QtUtils.h"

#include "kd/result_fold.h"

#include "vm/scalar.h"

namespace tb::ui
{

namespace
{
struct StairRun
{
  vm::axis::type axis;
  double direction;
};

StairRun stairRunForDirection(const StairDirection direction)
{
  switch (direction)
  {
  case StairDirection::North:
    return {vm::axis::y, 1.0};
  case StairDirection::East:
    return {vm::axis::x, 1.0};
  case StairDirection::South:
    return {vm::axis::y, -1.0};
  case StairDirection::West:
    return {vm::axis::x, -1.0};
  }

  return {vm::axis::y, 1.0};
}

size_t stairStepCount(const double height, const double stepHeight)
{
  if (height <= 0.0 || stepHeight <= 0.0)
  {
    return 0u;
  }

  const auto steps = static_cast<size_t>(std::ceil(height / stepHeight));
  return steps > 0u ? steps : 1u;
}
} // namespace

DrawShapeToolCuboidExtension::DrawShapeToolCuboidExtension(MapDocument& document)
  : DrawShapeToolExtension{document}
{
}

const std::string& DrawShapeToolCuboidExtension::name() const
{
  static const auto name = std::string{"Cuboid"};
  return name;
}

const std::filesystem::path& DrawShapeToolCuboidExtension::iconPath() const
{
  static const auto path = std::filesystem::path{"ShapeTool_Cuboid.svg"};
  return path;
}

DrawShapeToolExtensionPage* DrawShapeToolCuboidExtension::createToolPage(
  ShapeParameters&, QWidget* parent)
{
  return new DrawShapeToolExtensionPage{parent};
}

Result<std::vector<mdl::Brush>> DrawShapeToolCuboidExtension::createBrushes(
  const vm::bbox3d& bounds, const ShapeParameters&) const
{
  auto& map = m_document.map();

  const auto builder = mdl::BrushBuilder{
    map.worldNode().mapFormat(),
    map.worldBounds(),
    map.gameInfo().gameConfig.faceAttribsConfig.defaults};

  return builder.createCuboid(bounds, map.currentMaterialName())
    .transform([](auto brush) { return std::vector{std::move(brush)}; });
}

DrawShapeToolStairsExtensionPage::DrawShapeToolStairsExtensionPage(
  MapDocument& document, ShapeParameters& parameters, QWidget* parent)
  : DrawShapeToolExtensionPage{parent}
  , m_parameters{parameters}
{
  auto* stepHeightLabel = new QLabel{tr("Step Height: ")};
  auto* stepHeightBox = new QDoubleSpinBox{};
  stepHeightBox->setRange(1.0, 4096.0);
  stepHeightBox->setSingleStep(1.0);

  auto* directionLabel = new QLabel{tr("Orientation: ")};
  auto* directionBox = new QComboBox{};
  directionBox->addItems({tr("North"), tr("East"), tr("South"), tr("West")});

  connect(
    stepHeightBox,
    QOverload<double>::of(&QDoubleSpinBox::valueChanged),
    this,
    [&](const auto stepHeight) { m_parameters.setStepHeight(stepHeight); });
  connect(
    directionBox,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    [&](const auto index) {
      m_parameters.setStairDirection(static_cast<StairDirection>(index));
    });

  addWidget(stepHeightLabel);
  addWidget(stepHeightBox);
  addWidget(directionLabel);
  addWidget(directionBox);
  addApplyButton(document);

  const auto updateWidgets = [=, this]() {
    stepHeightBox->setValue(m_parameters.stepHeight());
    directionBox->setCurrentIndex(static_cast<int>(m_parameters.stairDirection()));
  };
  updateWidgets();

  m_notifierConnection +=
    m_parameters.parametersDidChangeNotifier.connect(std::move(updateWidgets));
}

DrawShapeToolStairsExtension::DrawShapeToolStairsExtension(MapDocument& document)
  : DrawShapeToolExtension{document}
{
}

const std::string& DrawShapeToolStairsExtension::name() const
{
  static const auto name = std::string{"Stairs"};
  return name;
}

const std::filesystem::path& DrawShapeToolStairsExtension::iconPath() const
{
  static const auto path = std::filesystem::path{"ShapeTool_Stairs.svg"};
  return path;
}

DrawShapeToolExtensionPage* DrawShapeToolStairsExtension::createToolPage(
  ShapeParameters& parameters, QWidget* parent)
{
  return new DrawShapeToolStairsExtensionPage{m_document, parameters, parent};
}

Result<std::vector<mdl::Brush>> DrawShapeToolStairsExtension::createBrushes(
  const vm::bbox3d& bounds, const ShapeParameters& parameters) const
{
  auto& map = m_document.map();
  const auto builder = mdl::BrushBuilder{
    map.worldNode().mapFormat(),
    map.worldBounds(),
    map.gameInfo().gameConfig.faceAttribsConfig.defaults};

  const auto stepHeight = parameters.stepHeight();
  const auto totalHeight = bounds.size().z();
  const auto steps = stairStepCount(totalHeight, stepHeight);
  if (steps == 0u)
  {
    return Error{"Step height and bounds height must be greater than zero"};
  }

  const auto run = stairRunForDirection(parameters.stairDirection());
  const auto axisIndex = static_cast<size_t>(run.axis);
  const auto runLength = bounds.max[axisIndex] - bounds.min[axisIndex];
  if (runLength <= 0.0)
  {
    return Error{"Bounds size must be greater than zero"};
  }

  const auto stepDepth = runLength / static_cast<double>(steps);
  const auto runStart = run.direction > 0.0 ? bounds.min[axisIndex] : bounds.max[axisIndex];
  const auto baseZ = bounds.min.z();

  auto results = std::vector<Result<mdl::Brush>>{};
  results.reserve(steps);

  for (size_t i = 0u; i < steps; ++i)
  {
    const auto stepBottom = baseZ + stepHeight * static_cast<double>(i);
    const auto stepTop = (i + 1u == steps) ? bounds.max.z()
                                           : baseZ + stepHeight * static_cast<double>(i + 1u);
    const auto stepStart = runStart + run.direction * stepDepth * static_cast<double>(i);
    const auto stepEnd = runStart + run.direction * stepDepth * static_cast<double>(i + 1u);

    auto stepBounds = bounds;
    stepBounds.min[axisIndex] = vm::min(stepStart, stepEnd);
    stepBounds.max[axisIndex] = vm::max(stepStart, stepEnd);
    stepBounds.min[2] = stepBottom;
    stepBounds.max[2] = stepTop;

    results.push_back(builder.createCuboid(stepBounds, map.currentMaterialName()));
  }

  return results | kdl::fold;
}

DrawShapeToolCircularStairsExtensionPage::DrawShapeToolCircularStairsExtensionPage(
  MapDocument& document, ShapeParameters& parameters, QWidget* parent)
  : DrawShapeToolExtensionPage{parent}
  , m_parameters{parameters}
{
  auto* stepHeightLabel = new QLabel{tr("Step Height: ")};
  auto* stepHeightBox = new QDoubleSpinBox{};
  stepHeightBox->setRange(1.0, 4096.0);
  stepHeightBox->setSingleStep(1.0);

  auto* stepsPerRotationLabel = new QLabel{tr("Steps per Rotation: ")};
  auto* stepsPerRotationBox = new QSpinBox{};
  stepsPerRotationBox->setRange(1, 256);

  auto* innerRadiusLabel = new QLabel{tr("Inner Radius: ")};
  auto* innerRadiusBox = new QDoubleSpinBox{};
  innerRadiusBox->setRange(0.0, 4096.0);
  innerRadiusBox->setSingleStep(1.0);

  auto* offsetAngleLabel = new QLabel{tr("Offset Angle: ")};
  auto* offsetAngleBox = new QDoubleSpinBox{};
  offsetAngleBox->setRange(-360.0, 360.0);
  offsetAngleBox->setSingleStep(5.0);

  connect(
    stepHeightBox,
    QOverload<double>::of(&QDoubleSpinBox::valueChanged),
    this,
    [&](const auto stepHeight) { m_parameters.setStepHeight(stepHeight); });
  connect(
    stepsPerRotationBox,
    QOverload<int>::of(&QSpinBox::valueChanged),
    this,
    [&](const auto steps) { m_parameters.setStairsPerRotation(size_t(steps)); });
  connect(
    innerRadiusBox,
    QOverload<double>::of(&QDoubleSpinBox::valueChanged),
    this,
    [&](const auto radius) { m_parameters.setStairInnerRadius(radius); });
  connect(
    offsetAngleBox,
    QOverload<double>::of(&QDoubleSpinBox::valueChanged),
    this,
    [&](const auto angle) { m_parameters.setStairOffsetAngle(angle); });

  addWidget(stepHeightLabel);
  addWidget(stepHeightBox);
  addWidget(stepsPerRotationLabel);
  addWidget(stepsPerRotationBox);
  addWidget(innerRadiusLabel);
  addWidget(innerRadiusBox);
  addWidget(offsetAngleLabel);
  addWidget(offsetAngleBox);
  addApplyButton(document);

  const auto updateWidgets = [=, this]() {
    stepHeightBox->setValue(m_parameters.stepHeight());
    stepsPerRotationBox->setValue(int(m_parameters.stairsPerRotation()));
    innerRadiusBox->setValue(m_parameters.stairInnerRadius());
    offsetAngleBox->setValue(m_parameters.stairOffsetAngle());
  };
  updateWidgets();

  m_notifierConnection +=
    m_parameters.parametersDidChangeNotifier.connect(std::move(updateWidgets));
}

DrawShapeToolCircularStairsExtension::DrawShapeToolCircularStairsExtension(
  MapDocument& document)
  : DrawShapeToolExtension{document}
{
}

const std::string& DrawShapeToolCircularStairsExtension::name() const
{
  static const auto name = std::string{"Circular Stairs"};
  return name;
}

const std::filesystem::path& DrawShapeToolCircularStairsExtension::iconPath() const
{
  static const auto path = std::filesystem::path{"ShapeTool_CircularStairs.svg"};
  return path;
}

DrawShapeToolExtensionPage* DrawShapeToolCircularStairsExtension::createToolPage(
  ShapeParameters& parameters, QWidget* parent)
{
  return new DrawShapeToolCircularStairsExtensionPage{m_document, parameters, parent};
}

Result<std::vector<mdl::Brush>> DrawShapeToolCircularStairsExtension::createBrushes(
  const vm::bbox3d& bounds, const ShapeParameters& parameters) const
{
  auto& map = m_document.map();
  const auto builder = mdl::BrushBuilder{
    map.worldNode().mapFormat(),
    map.worldBounds(),
    map.gameInfo().gameConfig.faceAttribsConfig.defaults};

  const auto stepHeight = parameters.stepHeight();
  const auto totalHeight = bounds.size().z();
  const auto steps = stairStepCount(totalHeight, stepHeight);
  if (steps == 0u)
  {
    return Error{"Step height and bounds height must be greater than zero"};
  }

  const auto stepsPerRotation = parameters.stairsPerRotation();
  if (stepsPerRotation == 0u)
  {
    return Error{"Steps per rotation must be greater than zero"};
  }

  const auto boundsXY = bounds.xy();
  const auto halfSize = boundsXY.size() / 2.0;
  const auto outerRadius = vm::min(halfSize.x(), halfSize.y());
  if (outerRadius <= 0.0)
  {
    return Error{"Bounds size must be greater than zero"};
  }

  auto innerRadius = parameters.stairInnerRadius();
  if (innerRadius < 0.0 || innerRadius >= outerRadius)
  {
    innerRadius = 0.0;
  }

  const auto stepAngle = vm::Cd::two_pi() / static_cast<double>(stepsPerRotation);
  const auto angleOffset = vm::to_radians(parameters.stairOffsetAngle());
  const auto baseZ = bounds.min.z();
  const auto center = boundsXY.center();

  auto results = std::vector<Result<mdl::Brush>>{};
  results.reserve(steps);

  for (size_t i = 0u; i < steps; ++i)
  {
    const auto stepBottom = baseZ + stepHeight * static_cast<double>(i);
    const auto stepTop = (i + 1u == steps) ? bounds.max.z()
                                           : baseZ + stepHeight * static_cast<double>(i + 1u);
    const auto angle0 = angleOffset + stepAngle * static_cast<double>(i);
    const auto angle1 = angle0 + stepAngle;

    const auto outerStart =
      center + vm::vec2d{std::cos(angle0), std::sin(angle0)} * outerRadius;
    const auto outerEnd =
      center + vm::vec2d{std::cos(angle1), std::sin(angle1)} * outerRadius;

    std::vector<vm::vec3d> vertices;
    if (innerRadius <= 0.0)
    {
      const auto bottomCenter = vm::vec3d{center, stepBottom};
      const auto topCenter = vm::vec3d{center, stepTop};
      vertices = {
        {outerStart, stepBottom},
        {outerStart, stepTop},
        {outerEnd, stepBottom},
        {outerEnd, stepTop},
        bottomCenter,
        topCenter};
    }
    else
    {
      const auto innerStart =
        center + vm::vec2d{std::cos(angle0), std::sin(angle0)} * innerRadius;
      const auto innerEnd =
        center + vm::vec2d{std::cos(angle1), std::sin(angle1)} * innerRadius;
      vertices = {
        {outerStart, stepBottom},
        {outerStart, stepTop},
        {outerEnd, stepBottom},
        {outerEnd, stepTop},
        {innerStart, stepBottom},
        {innerStart, stepTop},
        {innerEnd, stepBottom},
        {innerEnd, stepTop}};
    }

    results.push_back(builder.createBrush(vertices, map.currentMaterialName()));
  }

  return results | kdl::fold;
}

DrawShapeToolAxisAlignedShapeExtensionPage::DrawShapeToolAxisAlignedShapeExtensionPage(
  ShapeParameters& parameters, QWidget* parent)
  : DrawShapeToolExtensionPage{parent}
  , m_parameters{parameters}
{
  auto* axisLabel = new QLabel{tr("Axis: ")};
  auto* axisComboBox = new QComboBox{};
  axisComboBox->addItems({tr("X"), tr("Y"), tr("Z")});

  connect(
    axisComboBox,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    [&](const auto index) { m_parameters.setAxis(vm::axis::type(index)); });

  addWidget(axisLabel);
  addWidget(axisComboBox);

  const auto updateWidgets = [=, this]() {
    axisComboBox->setCurrentIndex(int(m_parameters.axis()));
  };
  updateWidgets();

  m_notifierConnection +=
    m_parameters.parametersDidChangeNotifier.connect(std::move(updateWidgets));
}

DrawShapeToolCircularShapeExtensionPage::DrawShapeToolCircularShapeExtensionPage(
  ShapeParameters& parameters, QWidget* parent)
  : DrawShapeToolAxisAlignedShapeExtensionPage{parameters, parent}
  , m_parameters{parameters}
{
  auto* numSidesLabel = new QLabel{tr("Number of Sides: ")};
  auto* numSidesBox = new QSpinBox{};
  numSidesBox->setRange(3, 96);

  auto* precisionBox = new QComboBox{};
  precisionBox->addItems({"12", "24", "48", "96"});

  auto* numSidesWidget = new QStackedWidget{};
  numSidesWidget->addWidget(numSidesBox);
  numSidesWidget->addWidget(precisionBox);

  auto* edgeAlignedCircleButton =
    createBitmapToggleButton("CircleEdgeAligned.svg", tr("Align edge to bounding box"));
  edgeAlignedCircleButton->setIconSize({24, 24});
  edgeAlignedCircleButton->setObjectName("toolButton_withBorder");

  auto* vertexAlignedCircleButton = createBitmapToggleButton(
    "CircleVertexAligned.svg", tr("Align vertices to bounding box"));
  vertexAlignedCircleButton->setIconSize({24, 24});
  vertexAlignedCircleButton->setObjectName("toolButton_withBorder");

  auto* scalableCircleButton =
    createBitmapToggleButton("CircleScalable.svg", tr("Scalable circle shape"));
  scalableCircleButton->setIconSize({24, 24});
  scalableCircleButton->setObjectName("toolButton_withBorder");

  auto* radiusModeButtonGroup = new QButtonGroup{};
  radiusModeButtonGroup->addButton(edgeAlignedCircleButton);
  radiusModeButtonGroup->addButton(vertexAlignedCircleButton);
  radiusModeButtonGroup->addButton(scalableCircleButton);

  connect(
    numSidesBox,
    QOverload<int>::of(&QSpinBox::valueChanged),
    this,
    [&](const auto numSides) {
      m_parameters.setCircleShape(std::visit(
        kdl::overload(
          [&](const mdl::EdgeAlignedCircle&) -> mdl::CircleShape {
            return mdl::EdgeAlignedCircle{size_t(numSides)};
          },
          [&](const mdl::VertexAlignedCircle&) -> mdl::CircleShape {
            return mdl::VertexAlignedCircle{size_t(numSides)};
          },
          [&](const mdl::ScalableCircle& circleShape) -> mdl::CircleShape {
            return circleShape;
          }),
        m_parameters.circleShape()));
    });
  connect(
    precisionBox,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    [&](const auto precision) {
      m_parameters.setCircleShape(std::visit(
        kdl::overload(
          [&](const mdl::ScalableCircle&) -> mdl::CircleShape {
            return mdl::ScalableCircle{size_t(precision)};
          },
          [](const auto& circleShape) -> mdl::CircleShape { return circleShape; }),
        m_parameters.circleShape()));
    });
  connect(edgeAlignedCircleButton, &QToolButton::clicked, this, [=, this]() {
    m_parameters.setCircleShape(
      mdl::convertCircleShape<mdl::EdgeAlignedCircle>(m_parameters.circleShape()));
  });
  connect(vertexAlignedCircleButton, &QToolButton::clicked, this, [=, this]() {
    m_parameters.setCircleShape(
      mdl::convertCircleShape<mdl::VertexAlignedCircle>(m_parameters.circleShape()));
  });
  connect(scalableCircleButton, &QToolButton::clicked, this, [=, this]() {
    m_parameters.setCircleShape(
      mdl::convertCircleShape<mdl::ScalableCircle>(m_parameters.circleShape()));
  });

  addWidget(numSidesLabel);
  addWidget(numSidesWidget);
  addWidget(edgeAlignedCircleButton);
  addWidget(vertexAlignedCircleButton);
  addWidget(scalableCircleButton);

  const auto updateWidgets = [=, this]() {
    std::visit(
      kdl::overload(
        [&](const mdl::EdgeAlignedCircle& circleShape) {
          numSidesBox->setValue(int(circleShape.numSides));
          numSidesWidget->setCurrentWidget(numSidesBox);
        },
        [&](const mdl::VertexAlignedCircle& circleShape) {
          numSidesBox->setValue(int(circleShape.numSides));
          numSidesWidget->setCurrentWidget(numSidesBox);
        },
        [&](const mdl::ScalableCircle& circleShape) {
          precisionBox->setCurrentIndex(int(circleShape.precision));
          numSidesWidget->setCurrentWidget(precisionBox);
        }),
      m_parameters.circleShape());

    edgeAlignedCircleButton->setChecked(
      std::holds_alternative<mdl::EdgeAlignedCircle>(m_parameters.circleShape()));
    vertexAlignedCircleButton->setChecked(
      std::holds_alternative<mdl::VertexAlignedCircle>(m_parameters.circleShape()));
    scalableCircleButton->setChecked(
      std::holds_alternative<mdl::ScalableCircle>(m_parameters.circleShape()));
  };
  updateWidgets();

  m_notifierConnection +=
    m_parameters.parametersDidChangeNotifier.connect(std::move(updateWidgets));
}

DrawShapeToolCylinderShapeExtensionPage::DrawShapeToolCylinderShapeExtensionPage(
  MapDocument& document, ShapeParameters& parameters, QWidget* parent)
  : DrawShapeToolCircularShapeExtensionPage{parameters, parent}
  , m_parameters{parameters}
{
  auto* hollowCheckBox = new QCheckBox{tr("Hollow")};

  auto* thicknessLabel = new QLabel{tr("Thickness: ")};
  auto* thicknessBox = new QDoubleSpinBox{};
  thicknessBox->setEnabled(parameters.hollow());
  thicknessBox->setRange(1, 128);

  connect(hollowCheckBox, &QCheckBox::toggled, this, [&](const auto hollow) {
    m_parameters.setHollow(hollow);
  });
  connect(
    thicknessBox,
    QOverload<double>::of(&QDoubleSpinBox::valueChanged),
    this,
    [&](const auto thickness) { m_parameters.setThickness(thickness); });

  addWidget(hollowCheckBox);
  addWidget(thicknessLabel);
  addWidget(thicknessBox);
  addApplyButton(document);

  const auto updateWidgets = [=, this]() {
    hollowCheckBox->setChecked(m_parameters.hollow());
    thicknessBox->setEnabled(m_parameters.hollow());
    thicknessBox->setValue(m_parameters.thickness());
  };
  updateWidgets();

  m_notifierConnection +=
    m_parameters.parametersDidChangeNotifier.connect(std::move(updateWidgets));
}

DrawShapeToolCylinderExtension::DrawShapeToolCylinderExtension(MapDocument& document)
  : DrawShapeToolExtension{document}
{
}

const std::string& DrawShapeToolCylinderExtension::name() const
{
  static const auto name = std::string{"Cylinder"};
  return name;
}

const std::filesystem::path& DrawShapeToolCylinderExtension::iconPath() const
{
  static const auto path = std::filesystem::path{"ShapeTool_Cylinder.svg"};
  return path;
}

DrawShapeToolExtensionPage* DrawShapeToolCylinderExtension::createToolPage(
  ShapeParameters& parameters, QWidget* parent)
{
  return new DrawShapeToolCylinderShapeExtensionPage{m_document, parameters, parent};
}

Result<std::vector<mdl::Brush>> DrawShapeToolCylinderExtension::createBrushes(
  const vm::bbox3d& bounds, const ShapeParameters& parameters) const
{
  auto& map = m_document.map();

  const auto builder = mdl::BrushBuilder{
    map.worldNode().mapFormat(),
    map.worldBounds(),
    map.gameInfo().gameConfig.faceAttribsConfig.defaults};
  return parameters.hollow()
           ? builder.createHollowCylinder(
               bounds,
               parameters.thickness(),
               parameters.circleShape(),
               parameters.axis(),
               map.currentMaterialName())
           : builder
               .createCylinder(
                 bounds,
                 parameters.circleShape(),
                 parameters.axis(),
                 map.currentMaterialName())
               .transform([](auto brush) { return std::vector{std::move(brush)}; });
}

DrawShapeToolConeShapeExtensionPage::DrawShapeToolConeShapeExtensionPage(
  MapDocument& document, ShapeParameters& parameters, QWidget* parent)
  : DrawShapeToolCircularShapeExtensionPage{parameters, parent}
  , m_parameters{parameters}
{
  addApplyButton(document);
}

DrawShapeToolConeExtension::DrawShapeToolConeExtension(MapDocument& document)
  : DrawShapeToolExtension{document}
{
}

const std::string& DrawShapeToolConeExtension::name() const
{
  static const auto name = std::string{"Cone"};
  return name;
}

const std::filesystem::path& DrawShapeToolConeExtension::iconPath() const
{
  static const auto path = std::filesystem::path{"ShapeTool_Cone.svg"};
  return path;
}

DrawShapeToolExtensionPage* DrawShapeToolConeExtension::createToolPage(
  ShapeParameters& parameters, QWidget* parent)
{
  return new DrawShapeToolConeShapeExtensionPage{m_document, parameters, parent};
}

Result<std::vector<mdl::Brush>> DrawShapeToolConeExtension::createBrushes(
  const vm::bbox3d& bounds, const ShapeParameters& parameters) const
{
  auto& map = m_document.map();

  const auto builder = mdl::BrushBuilder{
    map.worldNode().mapFormat(),
    map.worldBounds(),
    map.gameInfo().gameConfig.faceAttribsConfig.defaults};
  return builder
    .createCone(
      bounds, parameters.circleShape(), parameters.axis(), map.currentMaterialName())
    .transform([](auto brush) { return std::vector{std::move(brush)}; });
}

DrawShapeToolIcoSphereShapeExtensionPage::DrawShapeToolIcoSphereShapeExtensionPage(
  MapDocument& document, ShapeParameters& parameters, QWidget* parent)
  : DrawShapeToolExtensionPage{parent}
  , m_parameters{parameters}
{
  auto* accuracyLabel = new QLabel{tr("Accuracy: ")};
  auto* accuracyBox = new QSpinBox{};
  accuracyBox->setRange(0, 4);

  connect(
    accuracyBox,
    QOverload<int>::of(&QSpinBox::valueChanged),
    this,
    [&](const auto accuracy) { m_parameters.setAccuracy(size_t(accuracy)); });

  addWidget(accuracyLabel);
  addWidget(accuracyBox);
  addApplyButton(document);

  const auto updateWidgets = [=, this]() {
    accuracyBox->setValue(int(m_parameters.accuracy()));
  };
  updateWidgets();

  m_notifierConnection +=
    m_parameters.parametersDidChangeNotifier.connect(std::move(updateWidgets));
}

DrawShapeToolIcoSphereExtension::DrawShapeToolIcoSphereExtension(MapDocument& document)
  : DrawShapeToolExtension{document}
{
}

const std::string& DrawShapeToolIcoSphereExtension::name() const
{
  static const auto name = std::string{"Spheroid (Icosahedron)"};
  return name;
}

const std::filesystem::path& DrawShapeToolIcoSphereExtension::iconPath() const
{
  static const auto path = std::filesystem::path{"ShapeTool_IcoSphere.svg"};
  return path;
}

DrawShapeToolExtensionPage* DrawShapeToolIcoSphereExtension::createToolPage(
  ShapeParameters& parameters, QWidget* parent)
{
  return new DrawShapeToolIcoSphereShapeExtensionPage{m_document, parameters, parent};
}

Result<std::vector<mdl::Brush>> DrawShapeToolIcoSphereExtension::createBrushes(
  const vm::bbox3d& bounds, const ShapeParameters& parameters) const
{
  auto& map = m_document.map();

  const auto builder = mdl::BrushBuilder{
    map.worldNode().mapFormat(),
    map.worldBounds(),
    map.gameInfo().gameConfig.faceAttribsConfig.defaults};

  return builder.createIcoSphere(bounds, parameters.accuracy(), map.currentMaterialName())
    .transform([](auto brush) { return std::vector{std::move(brush)}; });
}

DrawShapeToolUVSphereShapeExtensionPage::DrawShapeToolUVSphereShapeExtensionPage(
  MapDocument& document, ShapeParameters& parameters, QWidget* parent)
  : DrawShapeToolCircularShapeExtensionPage{parameters, parent}
  , m_parameters{parameters}
{
  auto* numRingsLabel = new QLabel{tr("Number of Rings: ")};
  auto* numRingsBox = new QSpinBox{};
  numRingsBox->setRange(1, 256);

  auto* numRingsLayout = new QHBoxLayout{};
  numRingsLayout->setContentsMargins(QMargins{});
  numRingsLayout->setSpacing(LayoutConstants::MediumHMargin);
  numRingsLayout->addWidget(numRingsLabel);
  numRingsLayout->addWidget(numRingsBox);

  auto* numRingsWidget = new QWidget{};
  numRingsWidget->setLayout(numRingsLayout);

  connect(
    numRingsBox,
    QOverload<int>::of(&QSpinBox::valueChanged),
    this,
    [&](const auto numRings) { m_parameters.setNumRings(size_t(numRings)); });

  addWidget(numRingsWidget);
  addApplyButton(document);

  const auto updateWidgets = [=, this]() {
    numRingsWidget->setVisible(
      !std::holds_alternative<mdl::ScalableCircle>(m_parameters.circleShape()));
    numRingsBox->setValue(int(m_parameters.numRings()));
  };
  updateWidgets();

  m_notifierConnection +=
    m_parameters.parametersDidChangeNotifier.connect(std::move(updateWidgets));
}

DrawShapeToolUVSphereExtension::DrawShapeToolUVSphereExtension(MapDocument& document)
  : DrawShapeToolExtension{document}
{
}

const std::string& DrawShapeToolUVSphereExtension::name() const
{
  static const auto name = std::string{"Spheroid (UV)"};
  return name;
}

const std::filesystem::path& DrawShapeToolUVSphereExtension::iconPath() const
{
  static const auto path = std::filesystem::path{"ShapeTool_UVSphere.svg"};
  return path;
}

DrawShapeToolExtensionPage* DrawShapeToolUVSphereExtension::createToolPage(
  ShapeParameters& parameters, QWidget* parent)
{
  return new DrawShapeToolUVSphereShapeExtensionPage{m_document, parameters, parent};
}

Result<std::vector<mdl::Brush>> DrawShapeToolUVSphereExtension::createBrushes(
  const vm::bbox3d& bounds, const ShapeParameters& parameters) const
{
  auto& map = m_document.map();

  const auto builder = mdl::BrushBuilder{
    map.worldNode().mapFormat(),
    map.worldBounds(),
    map.gameInfo().gameConfig.faceAttribsConfig.defaults};
  return builder
    .createUVSphere(
      bounds,
      parameters.circleShape(),
      parameters.numRings(),
      parameters.axis(),
      map.currentMaterialName())
    .transform([](auto brush) { return std::vector{std::move(brush)}; });
}

std::vector<std::unique_ptr<DrawShapeToolExtension>> createDrawShapeToolExtensions(
  MapDocument& document)
{
  auto result = std::vector<std::unique_ptr<DrawShapeToolExtension>>{};
  result.push_back(std::make_unique<DrawShapeToolCuboidExtension>(document));
  result.push_back(std::make_unique<DrawShapeToolStairsExtension>(document));
  result.push_back(std::make_unique<DrawShapeToolCircularStairsExtension>(document));
  result.push_back(std::make_unique<DrawShapeToolCylinderExtension>(document));
  result.push_back(std::make_unique<DrawShapeToolConeExtension>(document));
  result.push_back(std::make_unique<DrawShapeToolUVSphereExtension>(document));
  result.push_back(std::make_unique<DrawShapeToolIcoSphereExtension>(document));
  return result;
}

} // namespace tb::ui
