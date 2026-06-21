#pragma once

#include <QString>
#include <QVariant>

#include <vector>

namespace ab {

/// One selectable value of an enum-typed GObject property.
struct EnumChoice {
    QString nick;
    int     value = 0;
};

/// A writable GObject property of a GStreamer element, introspected from its
/// GParamSpec so the UI can build an appropriate editor.
struct ElementProperty {
    enum class Kind { Bool, Int, Double, String, Enum, Unsupported };

    QString  name;
    QString  blurb;            ///< Human-readable description (tooltip).
    Kind     kind = Kind::Unsupported;
    QVariant defaultValue;

    // Numeric range (Int/Double).
    double minimum = 0.0;
    double maximum = 0.0;
    bool   fitsInInt = true;   ///< Int range fits a 32-bit QSpinBox.

    // Enum choices.
    std::vector<EnumChoice> choices;
};

namespace ElementProperties {

/// True if property introspection is available (built with GStreamer).
bool available();

/// Returns the writable, editable properties of a GStreamer element factory,
/// cached per name. Empty for unknown elements or non-GStreamer builds.
const std::vector<ElementProperty>& forElement(const QString& factoryName);

} // namespace ElementProperties
} // namespace ab
