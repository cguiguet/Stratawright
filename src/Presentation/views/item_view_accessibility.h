#pragma once

class QAccessibleInterface;
class QObject;
class QString;

namespace presentation::accessibility {

// Qt 6.8's macOS accessibility bridge over-releases the row and cell elements it builds for item
// views (lists, tables, trees, combo box popups), and the app crashes in
// -[QMacAccessibilityElement dealloc] as soon as an accessibility client (VoiceOver, window
// managers, screen readers of all kinds) walks one. This factory gives item views a plain widget
// interface instead, without rows or cells, so that code never runs. It only takes effect when
// installed after QApplication is created, so it is consulted before Qt's own factory.
QAccessibleInterface* plainItemViewInterface(const QString& className, QObject* object);

// Installs plainItemViewInterface on macOS; does nothing elsewhere.
void installItemViewWorkaround();

} // namespace presentation::accessibility
