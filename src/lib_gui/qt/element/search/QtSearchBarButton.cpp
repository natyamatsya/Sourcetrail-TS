#include "QtSearchBarButton.h"

#ifndef SRCTRL_MODULE_BUILD
#include "ApplicationSettings.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.settings;
#endif

namespace
{
// The search bar's buttons are flat, their line icons coloured as search/toolbar asks; the code
// view's keep the filled look of search/button.
const char* colorKey(QtSearchBarButton::Size size)
{
	return size == QtSearchBarButton::Size::SMALL ? "search/button" : "search/toolbar";
}

constexpr int toolbarButtonSize{28};
constexpr int fieldButtonSize{24};
constexpr int lineIconSize{16};
}	 // namespace

QtSearchBarButton::QtSearchBarButton(const FilePath& iconPath, Size size)
	: QtSelfRefreshIconButton(QLatin1String(""), iconPath, colorKey(size), nullptr)
	, m_size(size)
{
	refresh();
}

void QtSearchBarButton::refresh()
{
	QtSelfRefreshIconButton::refresh();

	if (m_size != Size::SMALL)
	{
		const int buttonSize{m_size == Size::TOOLBAR ? toolbarButtonSize : fieldButtonSize};
		setFixedSize(buttonSize, buttonSize);
		setIconSize(QSize(lineIconSize, lineIconSize));
		return;
	}

	const int size = 10;
	const float height = std::max(
		static_cast<float>(ApplicationSettings::getInstance()->getFontSize() + size),
		static_cast<float>(size + 14));

	setFixedHeight(static_cast<int>(height));
}
