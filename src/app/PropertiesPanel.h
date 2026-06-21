#pragma once

#include <QVariant>
#include <QWidget>

class QFormLayout;
class QLabel;
class QPushButton;
class QScrollArea;

namespace ab {

class Graph;
class Node;
struct ElementProperty;

/// Shows and edits the parameters of the selected node. Editors are built by
/// introspecting the underlying GStreamer element's properties (type, range,
/// enum choices); only values that differ from the element default are stored
/// on the node.
class PropertiesPanel : public QWidget {
    Q_OBJECT

public:
    explicit PropertiesPanel(QWidget* parent = nullptr);

    /// The graph, used to refresh Inspector ports when its enable flags change.
    void setGraph(ab::Graph* graph) { m_graph = graph; }

public slots:
    /// Display the given node (nullptr clears the panel).
    void setNode(ab::Node* node);

    /// If `node` is the one currently shown, rebuild to reflect an external
    /// change (e.g. a file dropped onto the node).
    void onNodeModified(ab::Node* node);

private:
    void rebuild();
    void clearForm();
    void resetToDefaults();
    void addInspectorOutputs();
    void addSchemaProperty(const ElementProperty& prop);
    void addRawProperty(const QString& key, const QVariant& value);

    /// Stores `value` as an override, or clears it when equal to `defaultValue`.
    void storeValue(const QString& key, const QVariant& value,
                    const QVariant& defaultValue);

    Graph*       m_graph = nullptr;
    Node*        m_node = nullptr;
    QLabel*      m_titleLabel = nullptr;
    QLabel*      m_placeholder = nullptr;
    QScrollArea* m_scroll = nullptr;
    QFormLayout* m_form = nullptr;
    QWidget*     m_formHost = nullptr;
    QPushButton* m_resetButton = nullptr;
};

} // namespace ab
