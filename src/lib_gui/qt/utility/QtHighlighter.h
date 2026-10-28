#ifndef QT_HIGHLIGHTER_H
#define QT_HIGHLIGHTER_H

#include <optional>
#include <vector>

#include <QRegularExpression>
#include <QTextCharFormat>

#include <aidkit/enum_class.hpp>

class QTextBlock;
class QTextDocument;

class QtHighlighter
{
public:
	class HighlightType final : public aidkit::enum_class<HighlightType>
	{
	public:
		static const HighlightType COMMENT;
		static const HighlightType DIRECTIVE;
		static const HighlightType FUNCTION;
		static const HighlightType KEYWORD;
		static const HighlightType NUMBER;
		static const HighlightType QUOTATION;
		static const HighlightType TEXT;
		static const HighlightType TYPE;

	private:
		using enum_class::enum_class;
	};

	static void loadHighlightingRules();
	static void clearHighlightingRules();

	QtHighlighter(QTextDocument* parent, const std::string& language);
	~QtHighlighter() = default;

	void highlightDocument();
	void highlightRange(int startLine, int endLine);

	void rehighlightLines(const std::vector<int>& lines);

	void applyFormat(int startPosition, int endPosition, const QTextCharFormat& format);

	QTextCharFormat getFormat(int startPosition, int endPosition) const;

private:
	struct HighlightingRule
	{
		HighlightingRule() = default;
		HighlightingRule(
			HighlightType type, const QRegularExpression& regExp, bool priority, bool multiLine = false);

		HighlightType type = HighlightType::TEXT;
		QRegularExpression pattern;
		bool priority = false;
		bool multiLine = false;
	};

	struct HighlightingRange
	{
		HighlightType type = HighlightType::TEXT;
		int start = 0;
		int end = 0;
	};

	// The single-line ranges of a line (strings, line comments, not starting inside another
	// one), found when the line is first highlighted.
	const std::vector<HighlightingRange>& singleLineRanges(const QTextBlock& block);
	// Finds the multi-line ranges (block comments, raw strings) as far as line `lastLine`, on
	// from where the last search stopped, and a range open there to its end: a file is searched
	// only as far as it was shown.
	void findMultiLineRangesUpTo(int lastLine);

	static bool isInRange(int index, const std::vector<HighlightingRange>& ranges);
	static std::vector<HighlightingRange> getRangesForRule(
		const QTextBlock& block, const HighlightingRule& rule);

	void formatBlockForRule(
		const QTextBlock& block,
		const HighlightingRule& rule,
		const std::vector<HighlightingRange> &ranges);
	void formatBlockIfInRange(
		const QTextBlock& block,
		HighlightType type,
		const std::vector<HighlightingRange> &ranges);
	void formatBlockIfInRange(
		const QTextBlock& block, const std::vector<HighlightingRange> &ranges);

	QTextDocument* document() const;

	static std::map<std::string, std::vector<HighlightingRule>> s_highlightingRules;
	static std::map<HighlightType, QTextCharFormat> s_charFormats;

	QTextDocument* m_document;

	// The search for one kind of multi-line range (a start and an end rule), and its ranges.
	struct MultiLineSearch
	{
		size_t startRule = 0;
		size_t endRule = 0;
		int line = 0;	 // where the search goes on
		int offset = 0;
		int openStart = -1;	   // a range found to start here, its end not found yet
		std::vector<HighlightingRange> ranges;
	};

	std::vector<HighlightingRule> m_highlightingRules;
	std::vector<HighlightingRule> m_singleLineRules;
	std::vector<std::optional<std::vector<HighlightingRange>>> m_singleLineRangesByLine;
	std::vector<MultiLineSearch> m_multiLineSearches;
	std::vector<bool> m_highlightedLines;
};

#endif	  // QT_HIGHLIGHTER_H
