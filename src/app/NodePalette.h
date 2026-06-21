#pragma once

#include <QWidget>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace ab {

/// A categorized, searchable tree of all node types from the NodeCatalog.
/// Categories collapse/expand; categories with many elements are split into
/// alphabetical subgroups. Activating a leaf requests that node be added.
class NodePalette : public QWidget {
    Q_OBJECT

public:
    explicit NodePalette(QWidget* parent = nullptr);

    /// Gives keyboard focus to the search box (and selects its text).
    void focusSearch();

signals:
    /// Emitted with the catalog typeId the user chose to add.
    void nodeRequested(const QString& typeId);

private:
    void populate();
    void applyFilter(const QString& text);

    /// Elements past this count in a category are grouped by initial letter.
    static constexpr int kGroupThreshold = 20;

    QLineEdit*   m_search = nullptr;
    QTreeWidget* m_tree = nullptr;
};

} // namespace ab
