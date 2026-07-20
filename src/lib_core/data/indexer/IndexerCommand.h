#ifndef INDEXER_COMMAND_H
#define INDEXER_COMMAND_H

#include <concepts>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include <stdcompat/optional>

#include "FilePath.h"
#include "IndexerCommandType.h"

// Contract every language's indexer-command payload satisfies. A payload is a plain value type (NO base
// class, NO virtuals) holding its language-specific data; it gets wrapped into the type-erased
// IndexerCommand below. Adding a language is just writing a payload that models this -- an open set.
template <class T>
concept IndexerCommandC = requires(const T& command, std::size_t stringSize) {
	{ command.getIndexerCommandType() } -> std::convertible_to<IndexerCommandType>;
	{ command.getByteSize(stringSize) } -> std::convertible_to<std::size_t>;
	{ command.getIndexerCommandHash() } -> std::convertible_to<std::string>;
};

// A type-erased, move-only indexer command (Iglberger-style external polymorphism): value semantics, no
// inheritance for callers, no shared_ptr. The data common to every command (source file, source group)
// lives here directly; the language-specific payload is erased behind Concept/Model. Model<Payload> is
// only instantiated where Payload is complete (the construction sites), so e.g. the Cxx payload's Model
// is emitted in lib_cxx and lib_core never names it.
class IndexerCommand
{
public:
	template <IndexerCommandC Payload>
	IndexerCommand(const FilePath& sourceFilePath, Payload payload)
		: m_sourceFilePath(sourceFilePath)
		, m_self(std::make_unique<Model<Payload>>(std::move(payload)))
	{
	}

	IndexerCommand(IndexerCommand&&) noexcept = default;
	IndexerCommand& operator=(IndexerCommand&&) noexcept = default;

	IndexerCommandType getIndexerCommandType() const { return m_self->getIndexerCommandType(); }
	std::string getIndexerCommandHash() const { return m_self->getIndexerCommandHash(); }

	std::size_t getByteSize(std::size_t stringSize) const
	{
		return m_sourceFilePath.str().size() + m_sourceGroupId.size() + m_self->getByteSize(stringSize);
	}

	const FilePath& getSourceFilePath() const { return m_sourceFilePath; }

	//! Id of the source group this command belongs to (fan-out S1). Tagged by
	//! CombinedIndexerCommandProvider when commands are consumed for indexing;
	//! empty until then. Lets subprocesses filter the shared command queue by group (fan-out S2).
	const std::string& getSourceGroupId() const { return m_sourceGroupId; }
	void setSourceGroupId(const std::string& sourceGroupId) { m_sourceGroupId = sourceGroupId; }

	//! std::function::target-style typed access to the erased payload; empty if it isn't a T.
	//! Returns optional<T&> (P2988) rather than a raw pointer: a maybe-reference with value
	//! semantics -- contextually bool, deref/arrow like a pointer, but not ownable or null-arithmetic.
	template <class T>
	stdcompat::optional<const T&> target() const
	{
		const auto* model = dynamic_cast<const Model<T>*>(m_self.get());
		if (model == nullptr)
			return stdcompat::nullopt;
		return model->m_payload;
	}

	template <class T>
	stdcompat::optional<T&> target()
	{
		auto* model = dynamic_cast<Model<T>*>(m_self.get());
		if (model == nullptr)
			return stdcompat::nullopt;
		return model->m_payload;
	}

private:
	struct Concept
	{
		virtual ~Concept() = default;
		virtual IndexerCommandType getIndexerCommandType() const = 0;
		virtual std::size_t getByteSize(std::size_t stringSize) const = 0;
		virtual std::string getIndexerCommandHash() const = 0;
	};

	template <class T>
	struct Model final: Concept
	{
		explicit Model(T payload): m_payload(std::move(payload)) {}

		IndexerCommandType getIndexerCommandType() const override { return m_payload.getIndexerCommandType(); }
		std::size_t getByteSize(std::size_t stringSize) const override { return m_payload.getByteSize(stringSize); }
		std::string getIndexerCommandHash() const override { return m_payload.getIndexerCommandHash(); }

		T m_payload;
	};

	FilePath m_sourceFilePath;
	std::string m_sourceGroupId;
	std::unique_ptr<Concept> m_self;
};

#endif	  // INDEXER_COMMAND_H
