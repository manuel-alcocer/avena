#include "app/TemplatePanel.h"

#include "app/TemplateLibrary.h"

#include <QAction>
#include <QFileInfo>
#include <QMenu>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace ab {

namespace {
constexpr int kBuiltinRole = Qt::UserRole + 1;   // template id
constexpr int kUserPathRole = Qt::UserRole + 2;  // user template path

QTreeWidgetItem* makeGroup(QTreeWidget* tree, const QString& title)
{
    auto* item = new QTreeWidgetItem(tree);
    item->setText(0, title);
    item->setFlags(Qt::ItemIsEnabled);
    QFont f = item->font(0);
    f.setBold(true);
    item->setFont(0, f);
    return item;
}
} // namespace

TemplatePanel::TemplatePanel(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);
    m_tree->setIndentation(14);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_tree);

    auto* saveButton = new QPushButton(tr("Save current as template…"), this);
    layout->addWidget(saveButton);

    refresh();

    connect(saveButton, &QPushButton::clicked, this,
            &TemplatePanel::saveCurrentRequested);

    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
                if (!item || !(item->flags() & Qt::ItemIsEnabled))
                    return;
                const QString id = item->data(0, kBuiltinRole).toString();
                const QString path = item->data(0, kUserPathRole).toString();
                if (!id.isEmpty())
                    emit loadBuiltinRequested(id);
                else if (!path.isEmpty())
                    emit loadUserRequested(path);
            });

    connect(m_tree, &QTreeWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
                QTreeWidgetItem* item = m_tree->itemAt(pos);
                if (!item || !(item->flags() & Qt::ItemIsEnabled))
                    return;
                const QString id = item->data(0, kBuiltinRole).toString();
                const QString path = item->data(0, kUserPathRole).toString();
                if (id.isEmpty() && path.isEmpty())
                    return;

                QMenu menu;
                QAction* load = menu.addAction(tr("Load template"));
                connect(load, &QAction::triggered, this, [this, id, path] {
                    if (!id.isEmpty()) emit loadBuiltinRequested(id);
                    else emit loadUserRequested(path);
                });
                if (!path.isEmpty()) {
                    QAction* del = menu.addAction(tr("Delete template"));
                    connect(del, &QAction::triggered, this,
                            [this, path] { emit deleteUserRequested(path); });
                }
                menu.exec(m_tree->viewport()->mapToGlobal(pos));
            });
}

void TemplatePanel::refresh()
{
    m_tree->clear();

    auto* builtinGroup = makeGroup(m_tree, tr("Built-in"));
    for (const TemplateInfo& t : TemplateLibrary::builtins()) {
        auto* item = new QTreeWidgetItem(builtinGroup);
        item->setText(0, t.name);
        item->setData(0, kBuiltinRole, t.id);

        QString tip = t.description;
        if (!t.available) {
            item->setDisabled(true);
            tip += tr("\n\nNot available on this system.");
            if (!t.requirement.isEmpty())
                tip += tr("\nNeeds: %1").arg(t.requirement);
        } else if (!t.requirement.isEmpty()) {
            tip += tr("\n\n%1").arg(t.requirement);
        }
        item->setToolTip(0, tip);
    }
    builtinGroup->setExpanded(true);

    auto* userGroup = makeGroup(m_tree, tr("User templates"));
    const std::vector<QString> files = TemplateLibrary::userTemplateFiles();
    for (const QString& path : files) {
        auto* item = new QTreeWidgetItem(userGroup);
        item->setText(0, QFileInfo(path).completeBaseName());
        item->setData(0, kUserPathRole, path);
        item->setToolTip(0, path);
    }
    if (files.empty()) {
        auto* hint = new QTreeWidgetItem(userGroup);
        hint->setText(0, tr("(none yet — use the button below)"));
        hint->setFlags(Qt::NoItemFlags);
    }
    userGroup->setExpanded(true);
}

} // namespace ab
