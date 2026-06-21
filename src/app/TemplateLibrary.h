#pragma once

#include <QString>

#include <vector>

namespace ab {

class Graph;

/// A named, pre-built pipeline the user can drop onto an empty canvas.
struct TemplateInfo {
    QString id;
    QString name;
    QString description;
    QString requirement;       ///< Hardware/plugin needed (empty for software).
    bool    available = true;  ///< False when its encoders aren't installed.
};

namespace TemplateLibrary {

/// Built-in pipeline templates (H.264/H.265/MKV/WebM/MP3…). Only those whose
/// encoders are actually installed are offered.
std::vector<TemplateInfo> builtins();

/// Populates `graph` (assumed empty) with the named built-in template.
bool buildBuiltin(const QString& id, Graph& graph, QString* error = nullptr);

/// Directory holding the user's saved templates (created on demand).
QString userTemplateDir();

/// Absolute paths of the user's saved `.abk` templates.
std::vector<QString> userTemplateFiles();

/// Saves `graph` as a user template named `name`; returns its path (or empty
/// on failure, with `error` set).
QString saveUserTemplate(const Graph& graph, const QString& name, QString* error = nullptr);

} // namespace TemplateLibrary
} // namespace ab
