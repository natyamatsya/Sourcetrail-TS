#ifndef PRESENTER_H
#define PRESENTER_H

#include <memory>
#include <type_traits>

// What a machine keeps of whatever shows its states (a dialog, a fake in a test),
// of any type: a non-owning handle. A kind of presenter derives its handle from
// it, with a concept for what such a presenter has and a table of functions made
// for each presenter type that meets it:
//
//   template <typename T>
//   concept PresentsLight = requires(T& presenter) { presenter.showLight(true); };
//
//   struct LightOperations { void (*show)(void* presenter, bool on); };
//
//   class LightPresenter: public Presenter<LightOperations>
//   {
//   public:
//       using Presenter::Presenter;
//       template <PresentsLight T>
//           requires(!std::derived_from<T, Presenter>)   // a handle is copied
//       LightPresenter(T& presenter): Presenter(presenter, &operations<T>) {}
//       void showLight(bool on) const { m_operations->show(m_presenter, on); }
//   private:
//       template <PresentsLight T>
//       static constexpr LightOperations operations {
//           [](void* p, bool on) { static_cast<T*>(p)->showLight(on); }};
//   };
//
// Handles compare by the object they refer to: made from different bases of one
// object, they are equal as long as those bases are polymorphic. An empty handle
// refers to none: nobody shows anything. The presenter
// outlives the handles to it (it detaches itself from the machines as it goes).
template <typename Operations>
class Presenter
{
public:
	Presenter() = default;

	explicit operator bool() const
	{
		return m_presenter != nullptr;
	}

	friend bool operator==(const Presenter& a, const Presenter& b)
	{
		return a.m_object == b.m_object;
	}

protected:
	template <typename T>
	Presenter(T& presenter, const Operations* operations)
		: m_presenter(std::addressof(presenter)), m_operations(operations), m_object(objectOf(presenter))
	{
	}

	void* m_presenter = nullptr;	// as the T the table was made for
	const Operations* m_operations = nullptr;

private:
	// The whole object: one presenter seen through two of its bases is one.
	template <typename T>
	static const void* objectOf(T& presenter)
	{
		if constexpr (std::is_polymorphic_v<T>)
		{
			return dynamic_cast<const void*>(std::addressof(presenter));
		}
		else
		{
			return std::addressof(presenter);
		}
	}

	const void* m_object = nullptr;
};

#endif	  // PRESENTER_H
