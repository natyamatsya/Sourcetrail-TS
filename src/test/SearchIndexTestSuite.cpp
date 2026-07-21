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
