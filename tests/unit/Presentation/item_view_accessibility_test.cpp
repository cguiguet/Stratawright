#include <catch2/catch_test_macros.hpp>

#include "Presentation/views/item_view_accessibility.h"

#include <QAccessible>
#include <QApplication>
#include <QComboBox>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QTreeWidget>

using presentation::accessibility::plainItemViewInterface;

TEST_CASE("Item views get an accessible interface without rows or cells", "[presentation][accessibility]") {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    int argc = 1;
    char name[] = "presentation_unit_tests";
    char* argv[] = {name, nullptr};
    QApplication app(argc, argv);

    // Without the workaround, Qt describes a list as a table, which is what crashes on macOS.
    QListWidget untouched;
    REQUIRE(QAccessible::queryAccessibleInterface(&untouched)->tableInterface() != nullptr);

    QAccessible::installFactory(&plainItemViewInterface);

    QListWidget list;
    list.addItem(QStringLiteral("one"));
    QTableWidget table(2, 2);
    QTreeWidget tree;
    QComboBox combo;
    combo.addItem(QStringLiteral("44100 Hz"));
    QPushButton button(QStringLiteral("OK"));

    for (QWidget* view : {static_cast<QWidget*>(&list), static_cast<QWidget*>(&table),
                          static_cast<QWidget*>(&tree), static_cast<QWidget*>(combo.view())}) {
        QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(view);
        REQUIRE(iface != nullptr);
        CHECK(iface->tableInterface() == nullptr);
    }
    CHECK(QAccessible::queryAccessibleInterface(&list)->role() == QAccessible::List);
    CHECK(QAccessible::queryAccessibleInterface(&table)->role() == QAccessible::Table);
    CHECK(QAccessible::queryAccessibleInterface(&tree)->role() == QAccessible::Tree);
    // Other widgets keep Qt's own interfaces.
    CHECK(QAccessible::queryAccessibleInterface(&button)->role() == QAccessible::Button);

    QAccessible::removeFactory(&plainItemViewInterface);
}
