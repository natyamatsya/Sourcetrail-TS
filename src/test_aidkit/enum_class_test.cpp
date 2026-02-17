// Copyright 2015 Peter Most, PERA Software Solutions GmbH
//
// This file is part of the CppAidKit library.
//
// CppAidKit is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// CppAidKit is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with CppAidKit. If not, see <http://www.gnu.org/licenses/>.

#include <catch2/catch_all.hpp>
#include <aidkit/enum_class.hpp>

using namespace std;
using namespace aidkit;

//#########################################################################################################

// Enum for testing default value assignment:

class Color : public aidkit::enum_class<Color> {
	public:
		static const Color Red;
		static const Color Green;
		static const Color Blue;

	private:
		using enum_class::enum_class;
};

const Color Color::Red("Red");
const Color Color::Green("Green");
const Color Color::Blue("Blue");

static void colorFunction(Color)
{
}

// Explicite template instantiation to detect syntax errors:

template class aidkit::enum_class<Color>;

TEST_CASE("ColorEnum: call by value")
{
	colorFunction(Color::Blue);
}

TEST_CASE("ColorEnum: value")
{
	REQUIRE(Color::Red.value() == 0);
	REQUIRE(Color::Green.value() == 1);
	REQUIRE(Color::Blue.value() == 2);
}

TEST_CASE("ColorEnum: values")
{
	vector<Color> colors;
	Color::for_each([&](const Color &color) {
		colors.push_back(color);
	});
	REQUIRE(colors.size() == 3);
}

TEST_CASE("ColorEnum: name")
{
	REQUIRE(Color::Red.name() == "Red");
	REQUIRE(Color::Green.name() == "Green");
	REQUIRE(Color::Blue.name() == "Blue");
}

TEST_CASE("ColorEnum: find by value")
{
	vector<Color> result = Color::find(2);
	REQUIRE(result.size() == 1);
	REQUIRE(result[0] == Color::Blue);
}

TEST_CASE("ColorEnum: find by name")
{
	vector<Color> result = Color::find("Green");
	REQUIRE(result.size() == 1);
	REQUIRE(result[0] == Color::Green);
}

TEST_CASE("ColorEnum: equality")
{
	REQUIRE(Color::Red == Color::Red);
	REQUIRE(Color::Green == Color::Green);
	REQUIRE(Color::Blue == Color::Blue);
}

TEST_CASE("ColorEnum: less than")
{
	REQUIRE(Color::Red < Color::Green);
	REQUIRE(Color::Green < Color::Blue);
}

TEST_CASE("ColorEnum: assignment")
{
	Color color = Color::Red;
	REQUIRE(color == Color::Red);

	color = Color::Blue;
	REQUIRE(color == Color::Blue);
}

//##################################################################################################

// Enum for testing explicit value assignment and duplicated enums:

class Number : public enum_class<Number> {
	public:
		static const Number Ten;
		static const Number Twenty;
		static const Number Thirty;
		static const Number TwentyToo;

	private:
		using enum_class::enum_class;
};

const Number Number::Ten(10, "Ten");
const Number Number::Twenty(20, "Twenty");
const Number Number::Thirty(30, "Thirty");
const Number Number::TwentyToo(20, "TwentyToo");

TEST_CASE("NumberEnum: value")
{
	REQUIRE(Number::Ten.value() == 10);
	REQUIRE(Number::Twenty.value() == 20);
	REQUIRE(Number::Thirty.value() == 30);
	REQUIRE(Number::TwentyToo.value() == 20);
}

TEST_CASE("NumberEnum: find duplicates")
{
	vector<Number> numbers = Number::find(20);
	REQUIRE(numbers.size() == 2);
	REQUIRE(numbers[0] == Number::Twenty);
	REQUIRE(numbers[1] == Number::TwentyToo);
}

//#########################################################################################################

// Enum for testing explicit start value:

class Animal : public enum_class<Animal> {
	public:
		static const Animal Cat;
		static const Animal Dog;

	private:
		using enum_class::enum_class;
};

const Animal Animal::Cat(10);
const Animal Animal::Dog;

TEST_CASE("AnimalEnum: value")
{
	REQUIRE(Animal::Cat.value() == 10);
	REQUIRE(Animal::Dog.value() == 11);
}

TEST_CASE("AnimalEnum: name")
{
	REQUIRE(Animal::Cat.name() == "");
	REQUIRE(Animal::Dog.name() == "");
}
