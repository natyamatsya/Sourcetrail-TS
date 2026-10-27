#include "Catch2.hpp"

#include "ColorScheme.h"
#include "FilePath.h"
#include "QtResources.h"
#include "ResourcePaths.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>

#include <string>
#include <vector>

namespace
{
std::vector<FilePath> colorSchemePaths()
{
	const FilePath directory{ResourcePaths::getColorSchemesDirectoryPath()};
	std::vector<FilePath> paths;
	const QStringList fileNames = QDir(QString::fromStdString(directory.str())).entryList({QStringLiteral("*.json")}, QDir::Files);
	for (const QString& fileName: fileNames)
	{
		paths.push_back(directory.getConcatenated(FilePath{fileName.toStdString()}));
	}
	return paths;
}

// The colours a style sheet asks for with its <color:key> placeholders.
std::vector<std::string> styleSheetColorKeys(const char* styleSheetPath)
{
	QFile styleSheet{QString::fromUtf8(styleSheetPath)};
	if (!styleSheet.open(QIODevice::ReadOnly))
	{
		return {};
	}
	const QString text{QString::fromUtf8(styleSheet.readAll())};

	std::vector<std::string> keys;
	const QRegularExpression placeholder{QStringLiteral("<color:([^>]+)>")};
	for (const QRegularExpressionMatch& match: placeholder.globalMatch(text))
	{
		keys.push_back(match.captured(1).toStdString());
	}
	return keys;
}

void requireColors(const std::vector<std::string>& keys)
{
	const std::vector<FilePath> schemes = colorSchemePaths();
	REQUIRE(!schemes.empty());

	for (const FilePath& scheme: schemes)
	{
		REQUIRE_MESSAGE(scheme.str(), ColorScheme::getInstance()->load(scheme));
		for (const std::string& key: keys)
		{
			INFO(scheme.fileName() + ": " + key);
			CHECK(ColorScheme::getInstance()->hasColor(key));
		}
	}

	// Leave the default scheme loaded for the suites after this one.
	ColorScheme::getInstance()->load(ResourcePaths::getColorSchemesDirectoryPath().concatenate("bright.json"));
}
}	 // namespace

TEST_CASE("every colour scheme has the colours the main style sheet asks for")
{
	const std::vector<std::string> keys = styleSheetColorKeys(QtResources::MAIN_CSS);
	REQUIRE(!keys.empty());

	requireColors(keys);
}

TEST_CASE("every colour scheme has the docking chrome's colours")
{
	// The docks are cards on a canvas; main.css paints them with these, in day and night
	// schemes alike.
	requireColors({
		"window/dock/canvas",
		"window/dock/card",
		"window/dock/card_border",
		"window/dock/header",
		"window/dock/header_text",
		"window/dock/handle",
		"window/dock/handle_hover",
		"window/dock/tab_selected",
	});
}

TEST_CASE("every colour scheme has the colours the search bar's style sheets ask for")
{
	// The search bar's flat buttons and field, in day and night schemes alike.
	for (const char* styleSheet: {
			 QtResources::SEARCH_VIEW_CSS,
			 QtResources::UNDOREDO_VIEW_CSS,
			 QtResources::REFRESH_VIEW_CSS,
			 QtResources::BOOKMARK_VIEW_CSS,
		 })
	{
		INFO(styleSheet);
		const std::vector<std::string> keys = styleSheetColorKeys(styleSheet);
		REQUIRE(!keys.empty());

		requireColors(keys);
	}

	// The buttons colour their line icons with these (QtSearchBarButton).
	requireColors({"search/toolbar/icon", "search/toolbar/icon_disabled"});
}
