#include "SearchIndex.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <limits>

#ifndef SRCTRL_MODULE_BUILD
#include "utility.h"
#include "utilityString.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.utility;
#endif

SearchIndex::SearchIndex()
{
	clear();
}

SearchIndex::~SearchIndex() = default;

void SearchIndex::addNode(Id id, std::string name, NodeType type)
{
	SearchNode* currentNode = m_root;

	while (name.size() > 0)
	{
		currentNode->containedTypes.add(type);

		auto it = currentNode->edges.find(name[0]);
		if (it != currentNode->edges.end())
		{
			SearchEdge* currentEdge = it->second;
			const std::string& edgeString = currentEdge->s;

			size_t matchCount = 1;
			for (size_t j = 1; j < edgeString.size() && j < name.size(); j++)
			{
				if (edgeString[j] != name[j])
				{
					break;
				}
				matchCount++;
			}

			if (matchCount < edgeString.size())
			{
				// split current edge
				m_nodes.push_back(std::make_unique<SearchNode>(currentNode->containedTypes));
				SearchNode* n = m_nodes.back().get();

				m_edges.push_back(std::make_unique<SearchEdge>(
					currentEdge->target, edgeString.substr(matchCount)));
				SearchEdge* e = m_edges.back().get();

				n->edges.emplace(e->s[0], e);

				currentEdge->s = edgeString.substr(0, matchCount);
				currentEdge->target = n;
			}

			name = name.substr(matchCount);
			currentNode = currentEdge->target;
		}
		else
		{
			m_nodes.push_back(std::make_unique<SearchNode>(currentNode->containedTypes));
			SearchNode* n = m_nodes.back().get();

			m_edges.push_back(std::make_unique<SearchEdge>(n, std::move(name)));
			SearchEdge* e = m_edges.back().get();

			currentNode->edges.emplace(e->s[0], e);
			currentNode = n;

			name.clear();
		}
	}

	currentNode->elementIds.emplace(id, type);
}

void SearchIndex::finishSetup()
{
	for (auto& p: m_root->edges)
	{
		populateEdgeGate(p.second);
	}
}

void SearchIndex::clear()
{
	m_nodes.clear();
	m_edges.clear();

	m_nodes.push_back(std::make_unique<SearchNode>(NodeTypeSet()));

	m_root = m_nodes.back().get();
}

std::vector<SearchResult> SearchIndex::search(
	const std::string& query,
	NodeTypeSet acceptedNodeTypes,
	size_t maxResultCount,
	size_t  /*maxBestScoredResultsLength: the letters are placed in the whole text*/) const
{
	// find paths containing query
	std::vector<SearchPath> paths;
	searchRecursive(
		SearchPath("", {}, m_root), utility::toLowerCase(query), acceptedNodeTypes, &paths);

	// create scored search results: the best of them by their letters and the name they
	// hit, before any is cut
	std::vector<SearchResult> searchResults = createScoredResults(
		paths, acceptedNodeTypes, maxResultCount * 3);

	// narrow down to max result count: the results are scored at their best already
	if (maxResultCount && searchResults.size() > maxResultCount)
	{
		searchResults.erase(searchResults.begin() + static_cast<std::ptrdiff_t>(maxResultCount), searchResults.end());
	}
	return searchResults;
}

void SearchIndex::populateEdgeGate(SearchEdge* e)
{
	for (auto& p: e->target->edges)
	{
		SearchEdge* targetEdge = p.second;
		populateEdgeGate(targetEdge);
		utility::append(e->gate, targetEdge->gate);
	}

	for (const char& c: e->s)
	{
		e->gate.insert(tolower(c));
	}
}

void SearchIndex::searchRecursive(
	const SearchPath& path,
	const std::string& remainingQuery,
	NodeTypeSet acceptedNodeTypes,
	std::vector<SearchIndex::SearchPath>* results) const
{
	for (const auto& p: path.node->edges)
	{
		const SearchEdge* currentEdge = p.second;

		if (!acceptedNodeTypes.intersectsWith(currentEdge->target->containedTypes))
		{
			continue;
		}

		// test if s passes the edge's gate.
		bool passesGate = true;
		for (const char& c: remainingQuery)
		{
			if (currentEdge->gate.find(c) == currentEdge->gate.end())
			{
				passesGate = false;
				break;
			}
		}

		if (!passesGate)
		{
			continue;
		}

		// consume characters for edge
		const std::string& edgeString = currentEdge->s;
		SearchPath currentPath {path.text + edgeString, path.indices, currentEdge->target};

		size_t j = 0;
		for (size_t i = 0; i < edgeString.size() && j < remainingQuery.size(); i++)
		{
			if (static_cast<char>(tolower(edgeString[i])) == remainingQuery[j])
			{
				currentPath.indices.push_back(path.text.size() + i);
				j++;
			}
		}

		if (j == remainingQuery.size())
		{
			results->push_back(std::move(currentPath));
		}
		else
		{
			searchRecursive(currentPath, remainingQuery.substr(j), acceptedNodeTypes, results);
		}
	}
}

namespace
{
// At least this many completions are scored before the best are kept: a short query's
// result budget (3^(length + 3)) cut them in the order they were found, and the name
// asked for could be among those cut.
constexpr size_t minScoredCandidates = 20000;
}	 // namespace

std::vector<SearchResult> SearchIndex::createScoredResults(
	const std::vector<SearchPath>& paths, NodeTypeSet acceptedNodeTypes, size_t maxResultCount)
{
	// The completions, nearest first: where each path consumed the query, then what lies
	// below, one level after the other across all paths. Each scored by its letters and
	// the name it hits; the best are kept after.
	const size_t candidateBudget = maxResultCount ? std::max(maxResultCount, minScoredCandidates) : 0;
	std::vector<SearchResult> searchResults;
	if (paths.empty())
	{
		return searchResults;
	}

	// The query, as the paths matched it.
	std::string lowerQuery;
	for (const size_t index: paths.front().indices)
	{
		lowerQuery.push_back(static_cast<char>(tolower(paths.front().text[index])));
	}

	std::vector<SearchPath> currentPaths = paths;
	while (!currentPaths.empty() && (!candidateBudget || searchResults.size() < candidateBudget))
	{
		std::vector<SearchPath> nextPaths;

		for (const SearchPath& path: currentPaths)
		{
			if (!path.node->elementIds.empty() &&
				(acceptedNodeTypes.intersectsWith(path.node->containedTypes)))
			{
				std::vector<Id> elementIds;
				for (const auto& p: path.node->elementIds)
				{
					if (acceptedNodeTypes.contains(p.second))
					{
						elementIds.push_back(p.first);
					}
				}

				if (!elementIds.empty())
				{
					// The letters where they score best, not where the search met them
					// first (the `v` of `v12` for `vformat`).
					std::vector<size_t> indices = bestIndices(path.text, lowerQuery);
					if (indices.empty())
					{
						indices = path.indices;
					}
					const int score = scoreText(path.text, indices) + nameBonus(path.text, indices);
					searchResults.emplace_back(path.text, std::move(elementIds), std::move(indices), score);

					if (candidateBudget && searchResults.size() >= candidateBudget)
					{
						break;
					}
				}
			}

			for (auto p: path.node->edges)
			{
				const SearchEdge* edge = p.second;
				nextPaths.emplace_back(path.text + edge->s, path.indices, edge->target);
			}
		}

		currentPaths = std::move(nextPaths);
	}

	// Best first; equal scores in the order found.
	std::ranges::stable_sort(searchResults, [](const SearchResult& a, const SearchResult& b) { return a.score > b.score; });
	if (maxResultCount && searchResults.size() > maxResultCount)
	{
		searchResults.erase(searchResults.begin() + static_cast<std::ptrdiff_t>(maxResultCount), searchResults.end());
	}
	return searchResults;
}

SearchResult SearchIndex::bestScoredResult(
	SearchResult result,
	std::map<std::string, SearchResult>* scoresCache,
	size_t maxBestScoredResultsLength)
{
	const std::string text = result.text;

	if (maxBestScoredResultsLength && result.text.size() > maxBestScoredResultsLength)
	{
		if (result.indices.back() >= maxBestScoredResultsLength)
		{
			return result;
		}

		result.text = result.text.substr(0, maxBestScoredResultsLength);
	}

	auto it = scoresCache->find(result.text);
	if (it != scoresCache->end())
	{
		// std::cout << "cached: " << it->first << " " << it->second.score << std::endl;
		SearchResult cachedResult = it->second;
		cachedResult.text = text;
		cachedResult.elementIds = result.elementIds;
		return cachedResult;
	}

	const std::vector<size_t> indices = result.indices;
	bestScoredResultRecursive(
		utility::toLowerCase(result.text),
		indices,
		indices.back(),
		indices.size() - 1,
		scoresCache,
		&result);

	// std::cout << "save: " << result.text << " " << result.score << std::endl;
	scoresCache->emplace(result.text, result);

	result.text = text;

	return result;
}

void SearchIndex::bestScoredResultRecursive(
	const std::string& lowerText,
	const std::vector<size_t>& indices,
	const size_t lastIndex,
	const size_t indicesPos,
	std::map<std::string, SearchResult>* scoresCache,
	SearchResult* result)
{
	// left for debugging
	// std::cout << lowerText << std::endl;
	// size_t idx = 0;
	// for (size_t i = 0; i < lowerText.size() && idx < indices.size(); i++)
	// {
	// 	if (i == indices[idx])
	// 	{
	// 		if (idx == indicesPos)
	// 			std::cout << "I";
	// 		else
	// 			std::cout << "^";
	// 		idx++;
	// 	}
	// 	else
	// 		std::cout << " ";
	// }
	// std::cout << "\n" << std::endl;

	if (indicesPos + 1 == indices.size())
	{
		for (size_t i = (indices.back() == lastIndex ? lowerText.size() - 1 : indices.back() - 1);
			 i > lastIndex;
			 i--)
		{
			if (lowerText[i] == lowerText[lastIndex])
			{
				std::string lowerTextPart = result->text.substr(0, i + 1);

				auto it = scoresCache->find(lowerTextPart);
				if (it != scoresCache->end())
				{
					// std::cout << "cached: " << it->first << " " << it->second.score << std::endl;
					result->score = it->second.score;
					result->indices = it->second.indices;
					return;
				}

				std::vector<size_t> newIndices = indices;
				newIndices[indicesPos] = i;

				int newScore = scoreText(result->text, newIndices);
				if (newScore > result->score)
				{
					result->score = newScore;
					result->indices = newIndices;
				}

				bestScoredResultRecursive(
					lowerText, newIndices, lastIndex, indicesPos, scoresCache, result);

				// std::cout << "save: " << lowerTextPart << " " << result->score << std::endl;
				scoresCache->emplace(lowerTextPart, *result);
				break;
			}
		}
	}
	else
	{
		size_t oldTextPos = indices[indicesPos];
		size_t nextTextPos = indices[indicesPos + 1];

		for (size_t i = oldTextPos + 1; i < nextTextPos; i++)
		{
			if (lowerText[i] == lowerText[oldTextPos])
			{
				std::vector<size_t> newIndices = indices;
				newIndices[indicesPos] = i;

				int newScore = scoreText(result->text, newIndices);
				if (newScore > result->score)
				{
					result->score = newScore;
					result->indices = newIndices;
				}

				bestScoredResultRecursive(
					lowerText, newIndices, lastIndex, indicesPos, scoresCache, result);
				break;
			}
		}
	}

	for (size_t i = indicesPos; i > 0; i--)
	{
		if (indices[i] - indices[i - 1] > 1)
		{
			bestScoredResultRecursive(lowerText, indices, lastIndex, i - 1, scoresCache, result);
			break;
		}
	}
}

std::vector<size_t> SearchIndex::bestIndices(const std::string& text, const std::string& lowerQuery)
{
	// scoreText, maximized over the placements (dynamic programming over query letter i
	// at text position j): a letter's own bonus, minus the letters skipped since the one
	// before, plus the consecutive bonus when none was; the first letter's late start.
	const int unmatchedLetterBonus = -1;
	const int consecutiveLetterBonus = 4;
	const int camelCaseBonus = 3;
	const int noLetterBonus = 4;
	const int firstLetterBonus = 4;
	const int delayedStartBonus = -1;
	const int minDelayedStartBonus = -20;
	constexpr int none = std::numeric_limits<int>::min() / 2;

	const size_t m = lowerQuery.size();
	const size_t n = text.size();
	if (m == 0 || m > n)
	{
		return {};
	}

	const auto letterBonus = [&](size_t j) {
		if (j == 0)
		{
			return firstLetterBonus;
		}
		if (isNoLetter(text[j - 1]))
		{
			return noLetterBonus;
		}
		if (iswupper(text[j]) &&
			(iswlower(text[j - 1]) || (j + 1 < n && iswlower(text[j + 1]))))
		{
			return camelCaseBonus;
		}
		return 0;
	};

	std::vector<int> score(m * n, none);
	std::vector<size_t> from(m * n, 0);
	for (size_t j = 0; j < n; j++)
	{
		if (static_cast<char>(tolower(text[j])) == lowerQuery[0])
		{
			score[j] = letterBonus(j) + std::max(static_cast<int>(j) * delayedStartBonus, minDelayedStartBonus);
		}
	}
	for (size_t i = 1; i < m; i++)
	{
		// The best earlier letter k <= j - 2, by score[k] + k: skipping j - k - 1 letters
		// costs that much.
		int bestGapped = none;
		size_t bestGappedFrom = 0;
		for (size_t j = 1; j < n; j++)
		{
			if (j >= 2 && score[(i - 1) * n + j - 2] != none)
			{
				const int gapped = score[(i - 1) * n + j - 2] + static_cast<int>(j - 2) * -unmatchedLetterBonus;
				if (gapped > bestGapped)
				{
					bestGapped = gapped;
					bestGappedFrom = j - 2;
				}
			}
			if (static_cast<char>(tolower(text[j])) != lowerQuery[i])
			{
				continue;
			}
			const int previous = score[(i - 1) * n + j - 1];
			int best = none;
			if (previous != none)
			{
				best = previous + consecutiveLetterBonus;
				from[i * n + j] = j - 1;
			}
			if (bestGapped != none)
			{
				const int gapped = bestGapped + static_cast<int>(j - 1) * unmatchedLetterBonus;
				if (gapped > best)
				{
					best = gapped;
					from[i * n + j] = bestGappedFrom;
				}
			}
			if (best != none)
			{
				score[i * n + j] = best + letterBonus(j);
			}
		}
	}

	size_t last = n;
	for (size_t j = 0; j < n; j++)
	{
		if (score[(m - 1) * n + j] != none && (last == n || score[(m - 1) * n + j] > score[(m - 1) * n + last]))
		{
			last = j;
		}
	}
	if (last == n)
	{
		return {};
	}

	std::vector<size_t> indices(m);
	indices[m - 1] = last;
	for (size_t i = m - 1; i > 0; i--)
	{
		indices[i - 1] = from[i * n + indices[i]];
	}
	return indices;
}

int SearchIndex::scoreText(const std::string& text, const std::vector<size_t>& indices)
{
	const int unmatchedLetterBonus = -1;
	const int consecutiveLetterBonus = 4;
	const int camelCaseBonus = 3;
	const int noLetterBonus = 4;
	const int firstLetterBonus = 4;
	const int delayedStartBonus = -1;
	const int minDelayedStartBonus = -20;

	int unmatchedLetterScore = 0;
	int consecutiveLetterScore = 0;
	int camelCaseScore = 0;
	int noLetterScore = 0;
	int firstLetterScore = 0;

	for (int i = 0; i < static_cast<int>(indices.size()); i++)
	{
		// unmatched and consecutive
		if (i > 0)
		{
			unmatchedLetterScore += static_cast<int>(
				(indices[i] - indices[i - 1] - 1) * unmatchedLetterBonus);
			consecutiveLetterScore += (indices[i] - indices[i - 1] == 1) ? consecutiveLetterBonus : 0;
		}

		size_t index = indices[i];

		// first letter
		if (index == 0)
		{
			firstLetterScore += firstLetterBonus;
		}
		// after no letter
		else if (index != 0 && isNoLetter(text[index - 1]))
		{
			noLetterScore += noLetterBonus;
		}
		// camel case
		else if (iswupper(text[index]))
		{
			bool prevIsLower = (index > 0 && iswlower(text[index - 1]));
			bool nextIsLower = (index + 1 < text.size() && iswlower(text[index + 1]));

			if (prevIsLower || nextIsLower)
			{
				camelCaseScore += camelCaseBonus;
			}
		}
	}

	int leadingStartScore = std::max(int(indices[0]) * delayedStartBonus, minDelayedStartBonus);

	int score = unmatchedLetterScore + consecutiveLetterScore + camelCaseScore + noLetterScore +
		firstLetterScore + leadingStartScore;

	// left for debugging
	// std::cout << unmatchedLetterScore << " " << consecutiveLetterScore << " " << camelCaseScore
	// << " " << noLetterScore; std::cout << " " << firstLetterScore << " " << leadingStartScore <<
	// " - " << score << " " << text << std::endl;

	return score;
}

namespace
{
// A name's parts: a path's by its slashes, a symbol's by "::" and '.' outside template
// arguments, each without its template arguments. Lower case.
std::vector<std::string> nameParts(const std::string& name, bool path)
{
	std::vector<std::string> parts(1);
	int templateDepth = 0;
	for (size_t i = 0; i < name.size(); i++)
	{
		const char c = name[i];
		if (path)
		{
			if (c == '/' || c == '\\')
			{
				parts.emplace_back();
				continue;
			}
		}
		else if (c == '<')
		{
			templateDepth++;
			continue;
		}
		else if (c == '>' && templateDepth > 0)
		{
			templateDepth--;
			continue;
		}
		else if (templateDepth == 0 && (c == '.' || (c == ':' && i + 1 < name.size() && name[i + 1] == ':')))
		{
			parts.emplace_back();
			i += (c == ':') ? 1 : 0;
			continue;
		}
		if (templateDepth == 0)
		{
			parts.back().push_back(static_cast<char>(tolower(c)));
		}
	}
	return parts;
}
}	 // namespace

int SearchIndex::nameBonus(const std::string& text, const std::vector<size_t>& indices)
{
	const int wholeNameBonus = 40;
	const int lastPartBonus = 30;	// with the query's qualifiers among the name's
	const int lastPartOnlyBonus = 15;
	const int suffixBonus = 10;

	if (indices.empty() || indices.back() >= text.size())
	{
		return 0;
	}

	std::string query;
	query.reserve(indices.size());
	for (const size_t index: indices)
	{
		query.push_back(static_cast<char>(tolower(text[index])));
	}
	const std::string lowerText = utility::toLowerCase(text);

	if (lowerText == query)
	{
		return wholeNameBonus;
	}

	const bool path = text.find_first_of("/\\") != std::string::npos;
	const std::vector<std::string> textParts = nameParts(text, path);
	const std::vector<std::string> queryParts = nameParts(query, path);
	if (!queryParts.back().empty() && textParts.back() == queryParts.back())
	{
		// The query's qualifiers in order among the name's: the others (an inline
		// namespace, a namespace left out) may be skipped.
		size_t next = 0;
		for (size_t i = 0; i + 1 < textParts.size() && next + 1 < queryParts.size(); i++)
		{
			if (textParts[i] == queryParts[next])
			{
				next++;
			}
		}
		return next + 1 == queryParts.size() ? lastPartBonus : lastPartOnlyBonus;
	}

	if (lowerText.ends_with(query))
	{
		return suffixBonus;
	}
	return 0;
}

SearchResult SearchIndex::rescoreText(
	const std::string& fulltext,
	const std::string& text,
	const std::vector<size_t>& indices,
	int score,
	size_t maxBestScoredResultsLength)
{
	SearchResult result(text, {}, indices, score);
	std::vector<size_t> textIndices;

	// match is already within text
	const int newIdx = static_cast<int>(indices[0] - (fulltext.size() - text.size()));
	if (newIdx >= 0)
	{
		for (size_t idx: indices)
		{
			textIndices.push_back(idx - (fulltext.size() - text.size()));
		}
	}
	// try if match is within text
	else
	{
		size_t idx = 0;
		size_t textSize = text.size();
		if (maxBestScoredResultsLength && textSize > maxBestScoredResultsLength)
		{
			textSize = maxBestScoredResultsLength;
		}

		for (size_t i = 0; i < textSize && idx < indices.size(); i++)
		{
			if (tolower(text[i]) == tolower(fulltext[indices[idx]]))
			{
				textIndices.push_back(i);
				idx++;
			}
		}

		// match was not found
		if (idx != indices.size())
		{
			result.score -= 1;
			return result;
		}
	}

	result.score = scoreText(text, textIndices);
	result.indices = textIndices;

	std::map<std::string, SearchResult> scoresCache;
	result = bestScoredResult(result, &scoresCache, maxBestScoredResultsLength);
	// The name hit is the full text's: its bonus stays with the shorter text shown.
	result.score += nameBonus(fulltext, indices);

	for (size_t i = 0; i < result.indices.size(); i++)
	{
		result.indices[i] += fulltext.size() - text.size();
	}

	return result;
}

bool SearchIndex::isNoLetter(const char c)
{
	switch (c)
	{
	case ' ':
	case '.':
	case ',':
	case '_':
	case ':':
	case '<':
	case '>':
	case '/':
	case '\\':
		return true;
	}
	return false;
}
