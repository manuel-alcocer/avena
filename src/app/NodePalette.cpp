#include "app/NodePalette.h"

#include "canvas/DragTypes.h"
#include "graph/NodeCatalog.h"

#include <QAction>
#include <QHeaderView>
#include <QIcon>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <vector>

namespace ab {

namespace {

constexpr int kTypeIdRole = Qt::UserRole + 1;

/// A leaf carries a non-empty typeId; group/category rows do not.
bool isLeaf(const QTreeWidgetItem* item)
{
    return !item->data(0, kTypeIdRole).toString().isEmpty();
}

/// QTreeWidget that drags the catalog typeId of a leaf as our node MIME type.
class NodeTreeWidget : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;

protected:
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override
    {
        for (const QTreeWidgetItem* it : items) {
            const QString typeId = it->data(0, kTypeIdRole).toString();
            if (!typeId.isEmpty()) {
                auto* mime = new QMimeData;
                mime->setData(QLatin1String(kNodeMimeType), typeId.toUtf8());
                mime->setText(typeId);
                return mime;
            }
        }
        return nullptr;
    }
};

QIcon colorSwatch(const QColor& color)
{
    QPixmap pm(12, 12);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRoundedRect(0, 0, 12, 12, 3, 3);
    p.end();
    return QIcon(pm);
}

QChar initialKey(const QString& name)
{
    if (name.isEmpty())
        return QLatin1Char('#');
    const QChar c = name.at(0).toUpper();
    return c.isLetter() ? c : QLatin1Char('#');
}

QTreeWidgetItem* makeLeaf(QTreeWidgetItem* parent, const NodeTypeSpec& spec)
{
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, spec.displayName);
    item->setData(0, kTypeIdRole, spec.typeId);
    item->setToolTip(0, spec.typeId);
    item->setIcon(0, colorSwatch(spec.color));
    return item;
}

void styleGroup(QTreeWidgetItem* item)
{
    item->setFlags(Qt::ItemIsEnabled);   // not selectable; expandable
    QFont f = item->font(0);
    f.setBold(true);
    item->setFont(0, f);
}

/// Filters a subtree to entries matching `needle`; returns whether anything is
/// still visible. Internal rows auto-expand while searching and collapse when
/// the query is cleared.
bool filterItem(QTreeWidgetItem* item, const QString& needle)
{
    if (isLeaf(item)) {
        const bool match = needle.isEmpty()
            || item->text(0).contains(needle, Qt::CaseInsensitive)
            || item->data(0, kTypeIdRole).toString().contains(needle, Qt::CaseInsensitive);
        item->setHidden(!match);
        return match;
    }

    bool anyVisible = false;
    for (int i = 0; i < item->childCount(); ++i)
        anyVisible = filterItem(item->child(i), needle) || anyVisible;

    item->setHidden(!anyVisible);
    item->setExpanded(!needle.isEmpty() && anyVisible);
    return anyVisible;
}

} // namespace

NodePalette::NodePalette(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search elements…"));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);

    m_tree = new NodeTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);
    m_tree->setUniformRowHeights(true);
    m_tree->setExpandsOnDoubleClick(true);
    m_tree->setIndentation(14);
    m_tree->setDragEnabled(true);
    m_tree->setDragDropMode(QAbstractItemView::DragOnly);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_tree);

    populate();

    connect(m_search, &QLineEdit::textChanged, this, &NodePalette::applyFilter);

    connect(m_tree, &QTreeWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
                QTreeWidgetItem* item = m_tree->itemAt(pos);
                if (!item || !isLeaf(item))
                    return;
                const QString typeId = item->data(0, kTypeIdRole).toString();
                QMenu menu;
                QAction* add = menu.addAction(tr("Add to pipeline"));
                connect(add, &QAction::triggered, this,
                        [this, typeId] { emit nodeRequested(typeId); });
                menu.exec(m_tree->viewport()->mapToGlobal(pos));
            });
}

void NodePalette::populate()
{
    m_tree->clear();
    const NodeCatalog& catalog = NodeCatalog::instance();

    for (const QString& category : catalog.categories()) {
        std::vector<const NodeTypeSpec*> specs;
        for (const NodeTypeSpec& spec : catalog.specs())
            if (spec.category == category)
                specs.push_back(&spec);

        auto* categoryItem = new QTreeWidgetItem(m_tree);
        categoryItem->setText(0, category.toUpper());
        styleGroup(categoryItem);

        if (static_cast<int>(specs.size()) <= kGroupThreshold) {
            for (const NodeTypeSpec* spec : specs)
                makeLeaf(categoryItem, *spec);
        } else {
            // Group by initial letter (specs arrive sorted by display name).
            QChar currentLetter;
            QTreeWidgetItem* letterItem = nullptr;
            for (const NodeTypeSpec* spec : specs) {
                const QChar letter = initialKey(spec->displayName);
                if (letter != currentLetter || !letterItem) {
                    currentLetter = letter;
                    letterItem = new QTreeWidgetItem(categoryItem);
                    letterItem->setText(0, QString(letter));
                    styleGroup(letterItem);
                }
                makeLeaf(letterItem, *spec);
            }
        }

        categoryItem->setExpanded(false);
    }
}

void NodePalette::applyFilter(const QString& text)
{
    const QString needle = text.trimmed();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        filterItem(m_tree->topLevelItem(i), needle);
}

void NodePalette::focusSearch()
{
    m_search->setFocus();
    m_search->selectAll();
}

} // namespace ab
