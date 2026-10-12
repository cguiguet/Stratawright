#include "item_view_accessibility.h"

#include <QAbstractItemView>
#include <QAccessible>
#include <QAccessibleWidget>
#include <QTableView>
#include <QTreeView>

namespace presentation::accessibility {

QAccessibleInterface* plainItemViewInterface(const QString& /*className*/, QObject* object) {
    auto* view = qobject_cast<QAbstractItemView*>(object);
    if (!view) {
        return nullptr;
    }
    QAccessible::Role role = QAccessible::List;
    if (qobject_cast<QTreeView*>(view)) {
        role = QAccessible::Tree;
    } else if (qobject_cast<QTableView*>(view)) {
        role = QAccessible::Table;
    }
    return new QAccessibleWidget(view, role);
}

void installItemViewWorkaround() {
#ifdef Q_OS_MACOS
    QAccessible::installFactory(&plainItemViewInterface);
#endif
}

} // namespace presentation::accessibility
