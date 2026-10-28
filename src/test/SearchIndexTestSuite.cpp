#include "Catch2.hpp"

#ifndef SRCTRL_MODULE_BUILD
#include "NameHierarchy.h"
#endif
#include "SearchIndex.h"
#ifndef SRCTRL_MODULE_BUILD
#include "utility.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.data;
import srctrl.utility;
#endif

using namespace std;

static NameHierarchy makeName(const string &prefix, const string &name, const string &postfix, NameDelimiterType delimiterType = NameDelimiterType::CXX)
{
	NameHierarchy nameHierarchy(delimiterType);
	nameHierarchy.push(NameElement(name, prefix, postfix));

	return nameHierarchy;
}

TEST_CASE("search index finds id of element added")
{
	SearchIndex index;
	index.addNode(1, makeName("void", "foo", "() const").getQualifiedName());
	index.finishSetup();
	std::vector<SearchResult> results = index.search("oo", NodeTypeSet::all(), 0);

	REQUIRE(1 == results.size());
	REQUIRE(1 == results[0].elementIds.size());
	REQUIRE(utility::containsElement<Id>(results[0].elementIds, 1));
}

TEST_CASE("search index finds correct indices for query")
{
	SearchIndex index;
	index.addNode(1, makeName("void", "foo", "() const").getQualifiedName());
	index.finishSetup();
	std::vector<SearchResult> results = index.search("oo", NodeTypeSet::all(), 0);

	REQUIRE(1 == results.size());
	REQUIRE(2 == results[0].indices.size());
	REQUIRE(1 == results[0].indices[0]);
	REQUIRE(2 == results[0].indices[1]);
}

TEST_CASE("search index finds ids for ambiguous query")
{
	SearchIndex index;
	index.addNode(1, makeName("void", "for", "() const").getQualifiedName());
	index.addNode(2, makeName("void", "fos", "() const").getQualifiedName());
	index.finishSetup();
	std::vector<SearchResult> results = index.search("fo", NodeTypeSet::all(), 0);

	REQUIRE(2 == results.size());
	REQUIRE(1 == results[0].elementIds.size());
	REQUIRE(utility::containsElement<Id>(results[0].elementIds, 1));
	REQUIRE(1 == results[1].elementIds.size());
	REQUIRE(utility::containsElement<Id>(results[1].elementIds, 2));
}

TEST_CASE("search index does not find anything after clear")
{
	SearchIndex index;
	index.addNode(1, makeName("void", "foo", "() const").getQualifiedName());
	index.finishSetup();
	index.clear();
	std::vector<SearchResult> results = index.search("oo", NodeTypeSet::all(), 0);

	REQUIRE(0 == results.size());
}

TEST_CASE("search index does not find all results when max amount is limited")
{
	SearchIndex index;
	index.addNode(1, makeName("void", "foo1", "() const").getQualifiedName());
	index.addNode(2, makeName("void", "foo2", "() const").getQualifiedName());
	index.finishSetup();
	std::vector<SearchResult> results = index.search("oo", NodeTypeSet::all(), 1);

	REQUIRE(1 == results.size());
}

TEST_CASE("search index query is case insensitive")
{
	SearchIndex index;
	index.addNode(1, makeName("void", "foo1", "() const").getQualifiedName());
	index.addNode(2, makeName("void", "FOO2", "() const").getQualifiedName());
	index.finishSetup();
	std::vector<SearchResult> results = index.search("oo", NodeTypeSet::all(), 0);

	REQUIRE(2 == results.size());
}

TEST_CASE("search index rates higher on consecutive letters")
{
	SearchIndex index;
	index.addNode(1, makeName("void", "oaabbcc", "() const").getQualifiedName());
	index.addNode(2, makeName("void", "ocbcabc", "() const").getQualifiedName());
	index.finishSetup();
	std::vector<SearchResult> results = index.search("abc", NodeTypeSet::all(), 0);

	REQUIRE(2 == results.size());
	REQUIRE("ocbcabc" == results[0].text);
	REQUIRE("oaabbcc" == results[1].text);
}

TEST_CASE("search index ranks the name asked for above names that only contain its letters")
{
	// fmt: `vformat` matched the `v` of `v12` and the `format` of `formatter`, which
	// outscored the function itself.
	SearchIndex index;
	index.addNode(1, "fmt::v12::vformat");
	index.addNode(2, "fmt::v12::formatter<std::basic_string<Char>, Char>");
	index.addNode(3, "fmt::v12::formatter<T, Char>");
	index.addNode(4, "fmt::v12::detail::vformat_to");
	index.addNode(5, "fmt::v12::vformat_to_n_result");
	index.finishSetup();

	const std::vector<SearchResult> results = index.search("vformat", NodeTypeSet::all(), 0);
	REQUIRE(results.size() == 5);
	REQUIRE(results[0].text == "fmt::v12::vformat");
}

TEST_CASE("search index sees through namespaces a qualified query leaves out")
{
	// `fmt::vformat` is how fmt's users name fmt::v12::vformat (an inline namespace).
	SearchIndex index;
	index.addNode(1, "fmt::v12::vformat");
	index.addNode(2, "fmt::v12::formatter<T, Char>");
	index.addNode(3, "fmt::v12::format_facet");
	index.addNode(4, "other::fmt_vformat");
	index.finishSetup();

	const std::vector<SearchResult> results = index.search("fmt::vformat", NodeTypeSet::all(), 0);
	REQUIRE_FALSE(results.empty());
	REQUIRE(results[0].text == "fmt::v12::vformat");
}

TEST_CASE("search index ranks a name ending in the query above one that does not")
{
	SearchIndex index;
	index.addNode(1, "formatter");
	index.addNode(2, "fmt::vformat");
	index.addNode(3, "fmt::format");
	index.finishSetup();

	const std::vector<SearchResult> results = index.search("format", NodeTypeSet::all(), 0);
	REQUIRE(results.size() == 3);
	REQUIRE(results[0].text == "fmt::format");
	REQUIRE(results[1].text == "fmt::vformat");
	REQUIRE(results[2].text == "formatter");
}

TEST_CASE("search index keeps the exact name when the result budget is small")
{
	// Many names that match the query's letters better than the exact name's long
	// qualifier: the budget used to cut the completions before they were ranked.
	SearchIndex index;
	for (int i = 0; i < 500; i++)
	{
		index.addNode(static_cast<Id>(i + 10), "ab" + std::to_string(i));
	}
	index.addNode(1, "some::very::long::qualifier::ab");
	index.finishSetup();

	const std::vector<SearchResult> results = index.search("ab", NodeTypeSet::all(), 10);
	REQUIRE(results.size() == 10);
	REQUIRE(results[0].text == "some::very::long::qualifier::ab");
}

TEST_CASE("search index name bonus for file paths uses the file name")
{
	REQUIRE(SearchIndex::nameBonus("../include/fmt/format.h", {15, 16, 17, 18, 19, 20, 21, 22}) == 30);
	REQUIRE(SearchIndex::nameBonus("../include/fmt/xformat.h", {16, 17, 18, 19, 20, 21, 22, 23}) == 10);
	REQUIRE(SearchIndex::nameBonus("format", {0, 1, 2}) == 0);
}
