#include "Catch2.hpp"

#include "ColorScheme.h"
#include "FilePath.h"
#include "QtHighlighter.h"
#include "ResourcePaths.h"

#include <QColor>
#include <QGuiApplication>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <memory>
#include <string>

namespace
{
// An offscreen application, the bright colour scheme, and the code of `lines` in a document.
class HighlighterFixture
{
public:
	HighlighterFixture()
	{
		qputenv("QT_QPA_PLATFORM", "offscreen");
		if (!QGuiApplication::instance())
		{
			static int argc{1};
			static char name[]{"test"};
			static char* argv[]{name, nullptr};
			m_application = std::make_unique<QGuiApplication>(argc, argv);
		}
		ColorScheme::getInstance()->load(ResourcePaths::getColorSchemesDirectoryPath().concatenate("bright.json"));
		QtHighlighter::clearHighlightingRules();
	}

	// The colour of the first character of `text` in line `line`.
	QColor colorOf(const QTextDocument& document, int line, const QString& text) const
	{
		const QTextBlock block{document.findBlockByNumber(line)};
		const int column{static_cast<int>(block.text().indexOf(text))};
		REQUIRE(column >= 0);
		QTextCursor cursor{const_cast<QTextDocument*>(&document)};
		cursor.setPosition(block.position() + column + 1);
		return cursor.charFormat().foreground().color();
	}

	static QColor syntax(const std::string& type)
	{
		return QColor(QString::fromStdString(ColorScheme::getInstance()->getSyntaxColor(type)));
	}

private:
	std::unique_ptr<QGuiApplication> m_application;
};

// Block comments and a raw string over several lines, comment and string markers inside the
// others, and a block comment far down: what the highlighter finds as the lines are shown.
QString sampleCode()
{
	QString code{QStringLiteral(
		"int a = 1; // line comment /* not a block */\n"	// 0
		"const char* s = \"/* not a comment */\";\n"	   // 1
		"/* block\n"									   // 2
		"   comment */ int b;\n"						   // 3
		"auto r = R\"(raw\n"							   // 4
		"string)\";\n")};								   // 5
	for (int i = 0; i < 200; i++)
	{
		code += QStringLiteral("int x;\n");	   // 6 .. 205
	}
	code += QStringLiteral(
		"/* far\n"		  // 206
		"away */ int c;\n");	// 207
	return code;
}
}	 // namespace

TEST_CASE("the highlighter finds comments and strings over lines as far as they are shown")
{
	const HighlighterFixture fixture;
	QTextDocument document{sampleCode()};
	QtHighlighter highlighter{&document, "cpp"};
	highlighter.highlightDocument();

	highlighter.highlightRange(0, 5);
	const QColor comment{HighlighterFixture::syntax("comment")};
	const QColor quotation{HighlighterFixture::syntax("quotation")};
	REQUIRE(comment != quotation);

	CHECK(fixture.colorOf(document, 0, QStringLiteral("line comment")) == comment);
	// A block comment's start in a line comment starts none: the next line is code.
	CHECK(fixture.colorOf(document, 1, QStringLiteral("const")) != comment);
	// Nor in a string.
	CHECK(fixture.colorOf(document, 1, QStringLiteral("/* not a comment")) == quotation);
	CHECK(fixture.colorOf(document, 2, QStringLiteral("block")) == comment);
	CHECK(fixture.colorOf(document, 3, QStringLiteral("comment")) == comment);
	CHECK(fixture.colorOf(document, 3, QStringLiteral("int b")) != comment);
	CHECK(fixture.colorOf(document, 5, QStringLiteral("string")) == quotation);

	// Further down, when that is shown.
	highlighter.highlightRange(200, 207);
	CHECK(fixture.colorOf(document, 205, QStringLiteral("int")) != comment);
	CHECK(fixture.colorOf(document, 206, QStringLiteral("far")) == comment);
	CHECK(fixture.colorOf(document, 207, QStringLiteral("away")) == comment);
	CHECK(fixture.colorOf(document, 207, QStringLiteral("int c")) != comment);
}

TEST_CASE("the highlighter highlights lines far down first as it would after the top")
{
	const HighlighterFixture fixture;
	QTextDocument document{sampleCode()};
	QtHighlighter highlighter{&document, "cpp"};
	highlighter.highlightDocument();

	const QColor comment{HighlighterFixture::syntax("comment")};
	highlighter.highlightRange(206, 207);
	CHECK(fixture.colorOf(document, 206, QStringLiteral("far")) == comment);
	CHECK(fixture.colorOf(document, 207, QStringLiteral("int c")) != comment);

	highlighter.highlightRange(2, 3);
	CHECK(fixture.colorOf(document, 3, QStringLiteral("comment")) == comment);
	CHECK(fixture.colorOf(document, 3, QStringLiteral("int b")) != comment);
}
