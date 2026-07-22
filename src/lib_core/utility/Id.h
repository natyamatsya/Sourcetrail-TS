#ifndef ID_H
#define ID_H

#include <QMetaType>

#include <concepts>
#include <cstddef>
#include <iosfwd>
#include <string>
#include <type_traits>

class Id final {
public:
	using type = long long;

	enum class FirstBits : type
	{
		ONE   = ~(~type(0) >> 1),
		TWO   = ~(~type(0) >> 2),
		THREE = ~(~type(0) >> 3)
	};

	Id() noexcept
		: m_value(0)
	{}

	Id(type value) noexcept
		: m_value(value)
	{
	}

	Id operator ++ () noexcept
	{
		return ++m_value;
	}

	Id operator ++ (int) noexcept
	{
		return m_value++;
	}

	Id operator += (const type value) noexcept
	{
		return m_value += value;
	}

	bool operator < (const Id other) const noexcept
	{
		return m_value < other.m_value;
	}

	bool operator > (const Id other) const noexcept
	{
		return m_value > other.m_value;
	}

	bool operator == (const Id other) const noexcept
	{
		return m_value == other.m_value;
	}

	bool operator != (const Id other) const noexcept
	{
		return m_value != other.m_value;
	}

	explicit operator bool () const noexcept
	{
		return m_value != 0;
	}

	template <std::integral T>
	explicit operator T() const noexcept
	{
		static_assert(sizeof(T) >= sizeof(type));

		return m_value;
	}

	//
	// Unusual operations:
	//

	// In some places certain values are ored/multiplied to the id, but the comments don't explain
	// the intent properly, only that it is done to avoid collisions.

	Id operator | (FirstBits bits) const noexcept
	{
		return m_value | static_cast<type>(bits);
	}

	Id operator * (const type value) const noexcept
	{
		return m_value * value;
	}

private:
	type m_value;
};

// Q_DECLARE_METATYPE is a macro; it cannot cross an `import`. Guarded out of module purviews so the
// QMetaTypeId<Id> / QMetaType::fromType<Id> specialization is not baked into a module's BMI -- MSVC
// rejects re-importing that specialization (C1116). The metatype registration instead happens in the
// classic/importer TUs that include this header textually (playbook: metatype registration stays in
// classic TUs). See docs/technical_notes/cxx20-modules-migration/msvc-bringup-findings.md.
#ifndef SRCTRL_MODULE_PURVIEW
Q_DECLARE_METATYPE(Id)
#endif

std::string to_string(const Id id);

std::ostream &operator << (std::ostream &os, const Id id);

// Support for unordered_map:
namespace std
{
	template<>
		struct hash<Id> {
			public:
				std::size_t operator()(const Id id) const noexcept;

			private:
				std::hash<Id::type> m_hash;
		};
}

#endif
