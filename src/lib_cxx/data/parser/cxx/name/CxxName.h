#ifndef CXX_NAME_H
#define CXX_NAME_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <concepts>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <stdcompat/optional>

#include "NameHierarchy.h"
#endif

// The single operation every C++ name model must provide. This concept replaces
// the former `virtual NameHierarchy toNameHierarchy() const = 0` of the abstract
// CxxName base class.
SRCTRL_EXPORT template <class T>
concept CxxNameC = requires(const T& t) {
	{ t.toNameHierarchy() } -> std::same_as<NameHierarchy>;
};

// CxxName: a type-erased value type that holds any CxxNameC model (CxxDeclName,
// CxxTypeName, ...). It replaces the old abstract base + shared_ptr<CxxName>
// hierarchy: the concrete name classes no longer inherit from CxxName, they only
// satisfy the concept. Copies share the underlying model (shared_ptr), preserving
// the aliasing the name resolvers rely on when they transfer parent chains.
SRCTRL_EXPORT class CxxName
{
public:
	CxxName() = default;

	// Build an erased name by constructing the concrete model T in place from the
	// forwarded arguments. T is never moved or copied out of here, so the concrete
	// leaves keep their const / move-only members untouched.
	template <CxxNameC T, class... Args>
	static CxxName make(Args&&... args)
	{
		CxxName name;
		name.m_self = std::make_shared<Model<T>>(std::forward<Args>(args)...);
		return name;
	}

	explicit operator bool() const { return static_cast<bool>(m_self); }

	NameHierarchy toNameHierarchy() const { return m_self->toNameHierarchy(); }

	void setParent(CxxName parent) { m_self->setParent(std::move(parent)); }

	// std::function::target-style access to the erased model, for the few sites
	// that need the concrete leaf back (e.g. re-reading a resolved decl name as a
	// type name). Returns optional<T&> (P2988) -- a maybe-reference with value
	// semantics; empty unless the held model is exactly a T.
	template <class T>
	stdcompat::optional<const T&> target() const
	{
		const auto* model = dynamic_cast<const Model<T>*>(m_self.get());
		if (model == nullptr)
			return stdcompat::nullopt;
		return model->m_model;
	}

private:
	struct Concept
	{
		virtual ~Concept() = default;
		virtual NameHierarchy toNameHierarchy() const = 0;
		virtual void setParent(CxxName parent) = 0;
	};

	template <class T>
	struct Model final: Concept
	{
		template <class... Args>
		explicit Model(Args&&... args): m_model(std::forward<Args>(args)...)
		{
		}

		NameHierarchy toNameHierarchy() const override { return m_model.toNameHierarchy(); }
		void setParent(CxxName parent) override { m_model.setParent(std::move(parent)); }

		T m_model;
	};

	std::shared_ptr<Concept> m_self;
};

// CxxNameParent: non-polymorphic mixin that gives the concrete name leaves their
// parent linkage and the shared template-suffix helper. Plain data reuse, no
// virtuals — distinct from the erased CxxName seam above.
SRCTRL_EXPORT class CxxNameParent
{
public:
	void setParent(CxxName parent) { m_parent = std::move(parent); }
	const CxxName& getParent() const { return m_parent; }

	static std::string getTemplateSuffix(const std::vector<std::string>& elements);

private:
	CxxName m_parent;
};

#include "CxxName.inl"

#endif	  // CXX_NAME_H
