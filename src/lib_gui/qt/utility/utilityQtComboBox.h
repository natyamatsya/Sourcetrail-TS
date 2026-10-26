#ifndef UTILITY_QT_COMBO_BOX_H
#define UTILITY_QT_COMBO_BOX_H

#include <format>
#include <string>

#include <QComboBox>

// Header-only on purpose: utilityQt.cpp reaches the main window (and KDDockWidgets), which a
// caller of this need not link.
namespace utility
{
// A combo box's items for a message: "a", "b", "c".
inline std::string quotedItems(const QComboBox &comboBox)
{
	std::string items;
	for (int i = 0; i < comboBox.count(); i++)
	{
		items += std::format("{}\"{}\"", items.empty() ? "" : ", ", comboBox.itemText(i).toStdString());
	}
	return items;
}
}	 // namespace utility

#endif	  // UTILITY_QT_COMBO_BOX_H
