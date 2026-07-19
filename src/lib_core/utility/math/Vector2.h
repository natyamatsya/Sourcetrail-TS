#ifndef VECTOR_2_H
#define VECTOR_2_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "Property.h"
#include "LogFacade.h"

#include "VectorBase.h"
#endif

SRCTRL_EXPORT template <class T>
class Vector2 final : public VectorBase<T, 2>
{
public:
	Vector2();
	Vector2(const T& x, const T& y);
	Vector2(const VectorBase<T, 2>& vector);
	Vector2(const Vector2<T>& vector);

	Vector2 &operator = (const Vector2 &) = default;

	~Vector2();

	T getValue(const unsigned int index) const;
	void setValue(const unsigned int index, const T& value);

	T operator[](const unsigned int index);

	Property<T> x;
	Property<T> y;

	Vector2<T> normalize();
	Vector2<T> normalized() const;

	template <class U>
	void operator=(const Vector2<U>& other);

protected:
	static const unsigned int m_xIndex = 0;
	static const unsigned int m_yIndex = 1;
};

template <class T>
Vector2<T>::Vector2()
	: VectorBase<T, 2>()
	, x(&VectorBase<T, 2>::m_values[m_xIndex])
	, y(&VectorBase<T, 2>::m_values[m_yIndex])
{
	setValue(m_xIndex, 0);
	setValue(m_yIndex, 0);
}

template <class T>
Vector2<T>::Vector2(const T& x, const T& y)
	: VectorBase<T, 2>()
	, x(&VectorBase<T, 2>::m_values[m_xIndex])
	, y(&VectorBase<T, 2>::m_values[m_yIndex])
{
	setValue(m_xIndex, x);
	setValue(m_yIndex, y);
}

template <class T>
Vector2<T>::Vector2(const VectorBase<T, 2>& vector)
	: VectorBase<T, 2>(vector)
	, x(&VectorBase<T, 2>::m_values[m_xIndex])
	, y(&VectorBase<T, 2>::m_values[m_yIndex])
{
}

template <class T>
Vector2<T>::Vector2(const Vector2<T>& vector)
	: VectorBase<T, 2>(vector)
	, x(&VectorBase<T, 2>::m_values[m_xIndex])
	, y(&VectorBase<T, 2>::m_values[m_yIndex])
{
}

template <class T>
Vector2<T>::~Vector2() = default;

template <class T>
T Vector2<T>::getValue(const unsigned int index) const
{
	try
	{
		return VectorBase<T, 2>::getValue(index);
	}
	catch (std::exception& e)
	{
		srctrl::log::error(e.what());
		return 0;
	}
}

template <class T>
void Vector2<T>::setValue(const unsigned int index, const T& value)
{
	try
	{
		VectorBase<T, 2>::setValue(index, value);
	}
	catch (std::exception& e)
	{
		srctrl::log::error(e.what());
	}
}

template <class T>
T Vector2<T>::operator[](const unsigned int index)
{
	try
	{
		return VectorBase<T, 2>::getValue(index);
	}
	catch (std::exception& e)
	{
		srctrl::log::error(e.what());
		return 0;
	}
}

template <class T>
Vector2<T> Vector2<T>::normalize()
{
	return VectorBase<T, 2>::normalize();
}

template <class T>
Vector2<T> Vector2<T>::normalized() const
{
	return VectorBase<T, 2>::normalized();
}

template <class T>
template <class U>
void Vector2<T>::operator=(const Vector2<U>& other)
{
	this->assign(other);
	x = Property<T>(&VectorBase<T, 2>::m_values[m_xIndex]);
	y = Property<T>(&VectorBase<T, 2>::m_values[m_yIndex]);
}

SRCTRL_EXPORT typedef Vector2<float> Vec2f;
SRCTRL_EXPORT typedef Vector2<int> Vec2i;

#endif	  // VECTOR_2_H
