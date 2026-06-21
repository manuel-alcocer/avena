#include "app/PropertiesPanel.h"

#include "graph/Graph.h"
#include "graph/Node.h"
#include "graph/NodeCatalog.h"
#include "media/ElementProperties.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSpinBox>
#include <QStringList>
#include <QVBoxLayout>

#include <climits>

namespace ab {

PropertiesPanel::PropertiesPanel(QWidget* parent) : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);

    m_titleLabel = new QLabel(this);
    QFont tf = m_titleLabel->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() + 1.0);
    m_titleLabel->setFont(tf);
    m_titleLabel->setWordWrap(true);
    outer->addWidget(m_titleLabel);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_formHost = new QWidget(m_scroll);
    m_form = new QFormLayout(m_formHost);
    m_form->setContentsMargins(0, 8, 0, 0);
    m_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_scroll->setWidget(m_formHost);
    outer->addWidget(m_scroll, 1);

    m_placeholder = new QLabel(tr("Select a node to edit its parameters."), this);
    m_placeholder->setWordWrap(true);
    m_placeholder->setEnabled(false);   // theme-dimmed text
    outer->addWidget(m_placeholder);

    m_resetButton = new QPushButton(tr("Reset to defaults"), this);
    connect(m_resetButton, &QPushButton::clicked, this, &PropertiesPanel::resetToDefaults);
    outer->addWidget(m_resetButton);

    setNode(nullptr);
}

void PropertiesPanel::resetToDefaults()
{
    if (!m_node)
        return;

    // Drop introspected (element) property overrides → back to element defaults.
    for (const ElementProperty& p : ElementProperties::forElement(m_node->typeId))
        m_node->properties.remove(p.name);

    // Restore catalog defaults (scale/start/end/bypass/…) but keep the file path.
    if (const NodeTypeSpec* spec = NodeCatalog::instance().find(m_node->typeId)) {
        for (auto it = spec->defaultProperties.constBegin();
             it != spec->defaultProperties.constEnd(); ++it) {
            if (it.key() == QLatin1String("location"))
                continue;
            m_node->properties[it.key()] = it.value();
        }
    }
    rebuild();
}

void PropertiesPanel::clearForm()
{
    while (m_form->count() > 0) {
        QLayoutItem* item = m_form->takeAt(0);
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }
}

void PropertiesPanel::setNode(Node* node)
{
    m_node = node;
    rebuild();
}

void PropertiesPanel::onNodeModified(Node* node)
{
    if (node && node == m_node)
        rebuild();
}

void PropertiesPanel::storeValue(const QString& key, const QVariant& value,
                                 const QVariant& defaultValue)
{
    if (!m_node)
        return;
    if (value == defaultValue)
        m_node->properties.remove(key);
    else
        m_node->properties[key] = value;
}

void PropertiesPanel::rebuild()
{
    clearForm();

    const bool hasNode = m_node != nullptr;
    m_titleLabel->setVisible(hasNode);
    m_scroll->setVisible(hasNode);
    m_resetButton->setVisible(hasNode);
    m_placeholder->setVisible(!hasNode);
    if (!hasNode)
        return;

    m_titleLabel->setText(m_node->title);

    // The Stream Inspector shows enable toggles for its value outputs instead of
    // element parameters.
    if (m_node->typeId == QLatin1String("tool.inspector")) {
        addInspectorOutputs();
        return;
    }

    // 1) Editors from the element's introspected schema.
    const std::vector<ElementProperty>& schema =
        ElementProperties::forElement(m_node->typeId);

    QSet<QString> shown;
    for (const ElementProperty& prop : schema) {
        if (prop.kind == ElementProperty::Kind::Unsupported)
            continue;
        addSchemaProperty(prop);
        shown.insert(prop.name);
    }

    // 2) Any stored keys not covered by the schema (e.g. the File Source's
    //    conceptual "location", or values loaded from an older document).
    QStringList extra;
    for (auto it = m_node->properties.constBegin();
         it != m_node->properties.constEnd(); ++it) {
        if (!shown.contains(it.key()))
            extra << it.key();
    }
    extra.sort();
    for (const QString& key : extra)
        addRawProperty(key, m_node->properties.value(key));

    if (m_form->rowCount() == 0) {
        auto* hint = new QLabel(tr("This element has no editable parameters."),
                                m_formHost);
        hint->setWordWrap(true);
        hint->setEnabled(false);
        m_form->addRow(hint);
    }
}

void PropertiesPanel::addInspectorOutputs()
{
    auto* hint = new QLabel(tr("Enable the value outputs you need:"), m_formHost);
    hint->setWordWrap(true);
    hint->setEnabled(false);
    m_form->addRow(hint);

    // All potential outputs; the active ones also depend on the connected stream.
    const QStringList outputs = {
        QStringLiteral("filename"), QStringLiteral("directory"), QStringLiteral("extension"),
        QStringLiteral("width"), QStringLiteral("height"), QStringLiteral("fps"),
        QStringLiteral("bitrate"), QStringLiteral("channels"), QStringLiteral("samplerate")};

    for (const QString& name : outputs) {
        const QString key = QStringLiteral("enable:") + name;
        auto* box = new QCheckBox(name, m_formHost);
        box->setChecked(m_node->properties.value(key).toBool());
        connect(box, &QCheckBox::toggled, this, [this, key](bool on) {
            if (!m_node)
                return;
            m_node->properties[key] = on;
            if (m_graph)
                m_graph->refreshInspectorPorts(m_node->id);
        });
        m_form->addRow(box);
    }
}

void PropertiesPanel::addSchemaProperty(const ElementProperty& prop)
{
    const QString key = prop.name;
    const QVariant current = m_node->properties.value(key, prop.defaultValue);
    const QVariant def = prop.defaultValue;
    QWidget* editor = nullptr;

    switch (prop.kind) {
    case ElementProperty::Kind::Bool: {
        auto* box = new QCheckBox(m_formHost);
        box->setChecked(current.toBool());
        connect(box, &QCheckBox::toggled, this, [this, key, def](bool v) {
            storeValue(key, v, def);
        });
        editor = box;
        break;
    }
    case ElementProperty::Kind::Int: {
        if (prop.fitsInInt) {
            auto* spin = new QSpinBox(m_formHost);
            spin->setRange(static_cast<int>(prop.minimum),
                           static_cast<int>(prop.maximum));
            spin->setValue(current.toInt());
            connect(spin, &QSpinBox::valueChanged, this, [this, key, def](int v) {
                storeValue(key, static_cast<qlonglong>(v), def);
            });
            editor = spin;
        } else {
            // Range exceeds 32 bits: use a double spin box with 0 decimals.
            auto* spin = new QDoubleSpinBox(m_formHost);
            spin->setDecimals(0);
            spin->setRange(prop.minimum, prop.maximum);
            spin->setValue(static_cast<double>(current.toLongLong()));
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this, key, def](double v) {
                storeValue(key, static_cast<qlonglong>(v), def);
            });
            editor = spin;
        }
        break;
    }
    case ElementProperty::Kind::Double: {
        auto* spin = new QDoubleSpinBox(m_formHost);
        spin->setDecimals(3);
        spin->setRange(prop.minimum, prop.maximum);
        spin->setValue(current.toDouble());
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, key, def](double v) {
            storeValue(key, v, def);
        });
        editor = spin;
        break;
    }
    case ElementProperty::Kind::Enum: {
        auto* combo = new QComboBox(m_formHost);
        const int currentValue = current.toInt();
        for (const EnumChoice& c : prop.choices)
            combo->addItem(c.nick, c.value);
        const int idx = combo->findData(currentValue);
        if (idx >= 0)
            combo->setCurrentIndex(idx);
        connect(combo, &QComboBox::currentIndexChanged, this,
                [this, key, def, combo](int) {
                    storeValue(key, combo->currentData().toInt(), def);
                });
        editor = combo;
        break;
    }
    case ElementProperty::Kind::String: {
        auto* line = new QLineEdit(current.toString(), m_formHost);
        connect(line, &QLineEdit::textChanged, this,
                [this, key, def](const QString& v) { storeValue(key, v, def); });
        editor = line;
        break;
    }
    case ElementProperty::Kind::Unsupported:
        return;
    }

    if (!prop.blurb.isEmpty())
        editor->setToolTip(prop.blurb);

    auto* label = new QLabel(key, m_formHost);
    if (!prop.blurb.isEmpty())
        label->setToolTip(prop.blurb);
    m_form->addRow(label, editor);
}

void PropertiesPanel::addRawProperty(const QString& key, const QVariant& value)
{
    QWidget* editor = nullptr;

    switch (value.typeId()) {
    case QMetaType::Bool: {
        auto* box = new QCheckBox(m_formHost);
        box->setChecked(value.toBool());
        connect(box, &QCheckBox::toggled, this, [this, key](bool v) {
            if (m_node) m_node->properties[key] = v;
        });
        editor = box;
        break;
    }
    case QMetaType::Int:
    case QMetaType::LongLong: {
        auto* spin = new QSpinBox(m_formHost);
        spin->setRange(INT_MIN, INT_MAX);
        spin->setValue(value.toInt());
        connect(spin, &QSpinBox::valueChanged, this, [this, key](int v) {
            if (m_node) m_node->properties[key] = v;
        });
        editor = spin;
        break;
    }
    case QMetaType::Double: {
        auto* spin = new QDoubleSpinBox(m_formHost);
        spin->setRange(-1e12, 1e12);
        spin->setValue(value.toDouble());
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, key](double v) {
            if (m_node) m_node->properties[key] = v;
        });
        editor = spin;
        break;
    }
    default: {
        auto* line = new QLineEdit(value.toString(), m_formHost);
        connect(line, &QLineEdit::textChanged, this, [this, key](const QString& v) {
            if (m_node) m_node->properties[key] = v;
        });
        editor = line;
        break;
    }
    }
    m_form->addRow(key, editor);
}

} // namespace ab
