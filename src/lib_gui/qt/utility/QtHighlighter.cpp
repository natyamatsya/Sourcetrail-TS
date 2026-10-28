#include "QtHighlighter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

// std::set (used below) is not pulled in transitively by every STL; make it explicit. Textual unless
// `import std` supplies it -- kept before the imports (include-before-import rule).
#ifndef SRCTRL_IMPORT_STD
#include <set>
#endif

#ifndef SRCTRL_MODULE_BUILD
#include "ColorScheme.h"
#include "FileSystem.h"
#include "ResourcePaths.h"
#include "TextAccess.h"
#endif
#include "logging.h"
#include "tracing.h"
#ifndef SRCTRL_MODULE_BUILD
#include "utility.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.file;
import srctrl.settings;
import srctrl.utility;
#endif

using namespace std;

const QtHighlighter::HighlightType QtHighlighter::HighlightType::COMMENT("comment");
const QtHighlighter::HighlightType QtHighlighter::HighlightType::DIRECTIVE("directive");
const QtHighlighter::HighlightType QtHighlighter::HighlightType::FUNCTION("function");
const QtHighlighter::HighlightType QtHighlighter::HighlightType::KEYWORD("keyword");
const QtHighlighter::HighlightType QtHighlighter::HighlightType::NUMBER("number");
const QtHighlighter::HighlightType QtHighlighter::HighlightType::QUOTATION("quotation");
const QtHighlighter::HighlightType QtHighlighter::HighlightType::TEXT("text");
const QtHighlighter::HighlightType QtHighlighter::HighlightType::TYPE("type");

std::map<std::string, std::vector<QtHighlighter::HighlightingRule>> QtHighlighter::s_highlightingRules;
std::map<QtHighlighter::HighlightType, QTextCharFormat> QtHighlighter::s_charFormats;

namespace
{

string highlightTypeToString(QtHighlighter::HighlightType type)
{
	return string(type.name());
}

QtHighlighter::HighlightType highlightTypeFromString(const string &typeStr)
{
	vector<QtHighlighter::HighlightType> foundEnums = QtHighlighter::HighlightType::find(typeStr);

	return (!foundEnums.empty()) ? foundEnums.front() : QtHighlighter::HighlightType::TEXT;
}

}

void QtHighlighter::loadHighlightingRules()
{
	ColorScheme* scheme = ColorScheme::getInstance().get();

	s_charFormats.clear();
	HighlightType::for_each([&](const HighlightType &type)
	{
		QTextCharFormat format;
		format.setForeground(QColor(scheme->getSyntaxColor(highlightTypeToString(type)).c_str()));
		s_charFormats.emplace(type, format);
	});

	for (const FilePath& path: FileSystem::getFilePathsFromDirectory(
			 ResourcePaths::getSyntaxHighlightingRulesDirectoryPath(), {".rules"}))
	{
		std::string language = path.withoutExtension().fileName();

		std::shared_ptr<TextAccess> textAccess = TextAccess::createFromFile(path);

		QJsonParseError error;
		QJsonDocument doc = QJsonDocument::fromJson(
			QString::fromStdString(textAccess->getText()).toUtf8(), &error);
		if (doc.isNull() || !doc.isArray())
		{
			LOG_ERROR_STREAM(
				<< "Highlighting rules in \"" << path.str()
				<< "\" couldn't be parsed as JSON: "
				   "offset "
				<< error.offset << " - " << error.errorString().toStdString());
			continue;
		}

		std::vector<HighlightingRule> rules;

		for (QJsonArray docArray = doc.array(); QJsonValueRef value: docArray)
		{
			if (!value.isObject())
			{
				continue;
			}

			QJsonObject ruleObj = value.toObject();

			HighlightType type = highlightTypeFromString(
				ruleObj.value(QStringLiteral("type")).toString().toStdString());

			bool priority = ruleObj.value(QStringLiteral("priority")).toBool();

			QJsonArray patterns = ruleObj.value(QStringLiteral("patterns")).toArray();
			for (QJsonValueRef pattern: patterns)
			{
				if (pattern.isString())
				{
					rules.push_back(HighlightingRule(type, QRegularExpression(pattern.toString()), priority));
				}
			}

			QJsonObject range = ruleObj.value(QStringLiteral("range")).toObject();
			if (!range.empty())
			{
				rules.push_back(HighlightingRule(
					type, QRegularExpression(range.value("start").toString()), priority, true));
				rules.push_back(
					HighlightingRule(type, QRegularExpression(range.value("end").toString()), priority, true));
			}
		}

		s_highlightingRules.emplace(language, rules);
	}
}

void QtHighlighter::clearHighlightingRules()
{
	s_highlightingRules.clear();
}

QtHighlighter::QtHighlighter(QTextDocument* document, const std::string& language)
	: m_document(document)
{
	if (!s_highlightingRules.size())
	{
		loadHighlightingRules();
	}

	const auto it = s_highlightingRules.find(language);
	if (it != s_highlightingRules.end())
	{
		m_highlightingRules = it->second;
	}
}

void QtHighlighter::highlightDocument()
{
	TRACE();

	// Nothing is formatted here: highlightRange formats the lines that are shown, and finds
	// what it needs for them then. This went through the whole file before (every line's
	// ranges, the multi-line ones, the text format on all of it), for a file of 14k lines a
	// good part of the time it took to show.
	const int lines{document()->blockCount()};
	m_highlightedLines.assign(lines, false);
	m_singleLineRangesByLine.assign(lines, std::nullopt);

	m_singleLineRules.clear();
	m_multiLineSearches.clear();
	std::optional<size_t> startRule;
	for (size_t i = 0; i < m_highlightingRules.size(); i++)
	{
		const HighlightingRule& rule{m_highlightingRules[i]};
		if (!rule.priority)
		{
			continue;
		}
		if (!rule.multiLine)
		{
			m_singleLineRules.push_back(rule);
		}
		else if (!startRule)
		{
			startRule = i;
		}
		else if (rule.type == m_highlightingRules[*startRule].type)
		{
			m_multiLineSearches.push_back(MultiLineSearch{.startRule = *startRule, .endRule = i});
			startRule.reset();
		}
	}
}

void QtHighlighter::highlightRange(int startLine, int endLine)
{
	if (startLine < 0 || endLine < 0 || startLine > endLine ||
		endLine >= int(m_highlightedLines.size()))
	{
		return;
	}

	bool hasUnhighlightedLines = false;
	for (int i = startLine; i <= endLine; i++)
	{
		if (!m_highlightedLines[i])
		{
			hasUnhighlightedLines = true;
			break;
		}
	}

	if (!hasUnhighlightedLines)
	{
		return;
	}

	findMultiLineRangesUpTo(endLine);

	QTextDocument* doc = document();
	QTextBlock start = doc->findBlockByLineNumber(startLine);
	QTextBlock end = doc->findBlockByLineNumber(endLine + 1);

	/*
	const HighlightingRule* singleLineCommentRule = nullptr;
	const HighlightingRule* quotationRule = nullptr;

	for (const HighlightingRule& rule: m_highlightingRules)
	{
		if (rule.type == HighlightType::COMMENT && !rule.multiLine)
		{
			singleLineCommentRule = &rule;
		}
		else if (rule.type == HighlightType::QUOTATION)
		{
			quotationRule = &rule;
		}
	}
	*/

	int index = startLine;
	for (QTextBlock it = start; it != end; it = it.next())
	{
		if (!m_highlightedLines[index])
		{
			applyFormat(
				it.position(), it.position() + it.length() - 1, s_charFormats[HighlightType::TEXT]);

			const std::vector<HighlightingRange>& lineRanges{singleLineRanges(it)};
			for (const HighlightingRule& rule: m_highlightingRules)
			{
				if (rule.multiLine)
				{
					continue;
				}

				if (rule.priority)
				{
					formatBlockIfInRange(it, rule.type, lineRanges);
				}
				else
				{
					formatBlockForRule(it, rule, lineRanges);
				}
			}

			for (const MultiLineSearch& search: m_multiLineSearches)
			{
				formatBlockIfInRange(it, search.ranges);
			}
		}
		index++;
	}

	for (int i = startLine; i <= endLine; i++)
	{
		m_highlightedLines[i] = true;
	}
}

void QtHighlighter::rehighlightLines(const std::vector<int>& lines)
{
	for (int line: lines)
	{
		if (line >= 0 && line < int(m_highlightedLines.size()))
		{
			m_highlightedLines[line] = false;
		}
	}
}

void QtHighlighter::applyFormat(int startPosition, int endPosition, const QTextCharFormat& format)
{
	QTextCursor cursor(document());
	cursor.setPosition(startPosition);
	cursor.setPosition(endPosition, QTextCursor::KeepAnchor);
	cursor.setCharFormat(format);
}

QTextCharFormat QtHighlighter::getFormat(int  /*startPosition*/, int endPosition) const
{
	QTextCursor cursor(document());
	cursor.setPosition(endPosition);
	return cursor.charFormat();
}

const std::vector<QtHighlighter::HighlightingRange>& QtHighlighter::singleLineRanges(const QTextBlock& block)
{
	std::optional<std::vector<HighlightingRange>>& cached{m_singleLineRangesByLine[block.blockNumber()]};
	if (cached)
	{
		return *cached;
	}

	std::vector<HighlightingRange> ranges;
	for (const HighlightingRule& rule: m_singleLineRules)
	{
		utility::append(ranges, getRangesForRule(block, rule));
	}

	// remove ranges starting inside others
	{
		std::map<std::pair<int, int>, size_t> sortedRangesToIndex;
		for (size_t i = 0; i < ranges.size(); i++)
		{
			sortedRangesToIndex.emplace(pair(ranges[i].start, ranges[i].end), i);
		}

		std::set<size_t> indicesToErase;
		const std::pair<int, int>* topRange = nullptr;
		for (const auto& p: sortedRangesToIndex)
		{
			if (topRange && p.first.first <= topRange->second)
			{
				indicesToErase.insert(p.second);
			}
			else
			{
				topRange = &p.first;
			}
		}

		for (auto it = indicesToErase.rbegin(); it != indicesToErase.rend(); it++)
		{
			ranges.erase(ranges.begin() + *it);
		}
	}

	cached = std::move(ranges);
	return *cached;
}

void QtHighlighter::findMultiLineRangesUpTo(int lastLine)
{
	QTextDocument* doc = document();
	const int lines{doc->blockCount()};
	for (MultiLineSearch& search: m_multiLineSearches)
	{
		const HighlightingRule& startRule{m_highlightingRules[search.startRule]};
		const HighlightingRule& endRule{m_highlightingRules[search.endRule]};
		while (search.line < lines && (search.line <= lastLine || search.openStart >= 0))
		{
			const QTextBlock block{doc->findBlockByNumber(search.line)};
			const QString text{block.text()};
			if (search.openStart < 0)
			{
				// A start that is not in a string or a line comment.
				QRegularExpressionMatch start{startRule.pattern.match(text, search.offset)};
				while (start.hasMatch() &&
					   isInRange(block.position() + static_cast<int>(start.capturedEnd()) - 1, singleLineRanges(block)))
				{
					start = startRule.pattern.match(text, start.capturedEnd() + 1);
				}
				if (!start.hasMatch())
				{
					search.line++;
					search.offset = 0;
					continue;
				}
				search.openStart = block.position() + static_cast<int>(start.capturedStart());
				search.offset = static_cast<int>(start.capturedEnd());
			}

			const QRegularExpressionMatch end{endRule.pattern.match(text, search.offset)};
			if (!end.hasMatch())
			{
				search.line++;
				search.offset = 0;
				continue;
			}
			search.ranges.push_back(HighlightingRange(
				startRule.type, search.openStart, block.position() + static_cast<int>(end.capturedEnd())));
			search.openStart = -1;
			search.offset = static_cast<int>(end.capturedEnd());
		}
	}
}

QtHighlighter::HighlightingRule::HighlightingRule(
	HighlightType type, const QRegularExpression& regExp, bool priority, bool multiLine)
	: type(type), pattern(regExp), priority(priority), multiLine(multiLine)
{
}

bool QtHighlighter::isInRange(int pos, const std::vector<HighlightingRange> &ranges)
{
	for (const HighlightingRange& range: ranges)
	{
		if (pos >= range.start && pos <= range.end)
		{
			return true;
		}
	}
	return false;
}

/* Previous version with QRegExp:
std::vector<std::tuple<QtHighlighter::HighlightType, int, int>> QtHighlighter::getRangesForRule(
	const QTextBlock& block, const HighlightingRule& rule)
{
	const int pos = block.position();
	const QString text = block.text();
	QRegExp expression(rule.pattern);
	int index = expression.indexIn(text);

	std::vector<std::tuple<HighlightType, int, int>> ranges;

	while (index >= 0)
	{
		const int length = expression.matchedLength();
		if (expression.capturedTexts().size() > 1)
		{
			const QString cap = expression.capturedTexts().at(1);
			const int start = text.indexOf(cap, index);
			ranges.push_back(std::make_tuple(rule.type, pos + start, pos + start + cap.length()));
		}
		else
		{
			ranges.push_back(std::make_tuple(rule.type, pos + index, pos + index + length));
		}
		index = expression.indexIn(block.text(), index + length);
	}
	return ranges;
}
*/

std::vector<QtHighlighter::HighlightingRange> QtHighlighter::getRangesForRule(const QTextBlock& block,
	const HighlightingRule& rule)
{
	const int pos = block.position();
	const QString text = block.text();
	std::vector<HighlightingRange> ranges;

	for (const QRegularExpressionMatch &match : rule.pattern.globalMatch(text)) {
		int index = match.capturedStart();
		int length = match.capturedLength();

		if (match.capturedTexts().size() > 1) {
			const QString cap = match.capturedTexts().at(1);
			const int start = text.indexOf(cap, index);
			ranges.push_back(HighlightingRange(rule.type, pos + start, pos + start + cap.length()));
		} else
			ranges.push_back(HighlightingRange(rule.type, pos + index, pos + index + length));
	}
	return ranges;
}

/* Previous version with QRegExp:
void QtHighlighter::formatBlockForRule(const QTextBlock& block, const HighlightingRule& rule,
	std::vector<std::tuple<HighlightType, int, int>>* ranges)
{
	if (s_charFormats.find(rule.type) == s_charFormats.end())
	{
		return;
	}

	const QTextCharFormat& format = s_charFormats.find(rule.type)->second;

	QRegExp expression(rule.pattern);
	int pos = block.position();
	int index = expression.indexIn(block.text());

	while (index >= 0)
	{
		int length = expression.matchedLength();

		if (!isInRange(pos + index, *ranges))
		{
			applyFormat(pos + index, pos + index + length, format);
		}

		index = expression.indexIn(block.text(), index + length);
	}
}
*/

void QtHighlighter::formatBlockForRule( const QTextBlock& block, const HighlightingRule& rule,
	const std::vector<HighlightingRange> &ranges)
{
	if (s_charFormats.find(rule.type) == s_charFormats.end())
		return;

	const QTextCharFormat& format = s_charFormats.find(rule.type)->second;

	int pos = block.position();
	for (const QRegularExpressionMatch &match : rule.pattern.globalMatch(block.text())) {
		int index = match.capturedStart();
		int length = match.capturedLength();
		if (!isInRange(pos + index, ranges))
			applyFormat(pos + index, pos + index + length, format);
	}
}

void QtHighlighter::formatBlockIfInRange(
	const QTextBlock& block,
	HighlightType type,
	const std::vector<HighlightingRange> &ranges)
{
	int startPos = block.position();
	int endPos = startPos + block.length() - 1;

	if (s_charFormats.find(type) == s_charFormats.end())
	{
		return;
	}

	const QTextCharFormat& format = s_charFormats.find(type)->second;

	for (auto range: ranges)
	{
		if (type == range.type)
		{
			int start = std::max(range.start, startPos);
			int end = std::min(range.end, endPos);

			if (start <= end)
			{
				applyFormat(start, end, format);
			}
		}
	}
}

void QtHighlighter::formatBlockIfInRange(
	const QTextBlock& block, const std::vector<HighlightingRange> &ranges)
{
	int startPos = block.position();
	int endPos = startPos + block.length() - 1;

	for (auto range: ranges)
	{
		HighlightType type = range.type;
		if (s_charFormats.find(type) == s_charFormats.end())
		{
			continue;
		}

		const QTextCharFormat& format = s_charFormats.find(type)->second;

		int start = std::max(range.start, startPos);
		int end = std::min(range.end, endPos);

		if (start <= end)
		{
			applyFormat(start, end, format);
		}
	}
}

QTextDocument* QtHighlighter::document() const
{
	return m_document;
}
