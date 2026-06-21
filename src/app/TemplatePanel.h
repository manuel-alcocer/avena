#pragma once

#include <QWidget>

class QTreeWidget;

namespace ab {

/// Lists pipeline templates in two groups — built-in and the user's own saved
/// ones — and lets the user load, save or delete them.
class TemplatePanel : public QWidget {
    Q_OBJECT

public:
    explicit TemplatePanel(QWidget* parent = nullptr);

    /// Rebuilds the list (call after saving/deleting a user template).
    void refresh();

signals:
    void loadBuiltinRequested(const QString& id);
    void loadUserRequested(const QString& path);
    void saveCurrentRequested();
    void deleteUserRequested(const QString& path);

private:
    QTreeWidget* m_tree = nullptr;
};

} // namespace ab
