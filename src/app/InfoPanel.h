#pragma once

#include <QWidget>

class QFormLayout;
class QLabel;

namespace ab {

class Node;

/// Read-only information about the selected node: media details (duration,
/// streams, codecs…) for file nodes, or the element/port summary for others.
class InfoPanel : public QWidget {
    Q_OBJECT

public:
    explicit InfoPanel(QWidget* parent = nullptr);

public slots:
    void setNode(ab::Node* node);
    /// Refresh if `node` is the one being shown (e.g. after a file is assigned).
    void onNodeModified(ab::Node* node);

private:
    void rebuild();
    void clearForm();
    void addMediaInfo(const QString& location);
    void addNodeInfo();

    Node*        m_node = nullptr;
    QLabel*      m_title = nullptr;
    QLabel*      m_placeholder = nullptr;
    QFormLayout* m_form = nullptr;
    QWidget*     m_formHost = nullptr;
};

} // namespace ab
