/*
**  ClanLib SDK
**  Copyright (c) 1997-2015 The ClanLib Team
**
**  This software is provided 'as-is', without any express or implied
**  warranty.  In no event will the authors be held liable for any damages
**  arising from the use of this software.
**
**  Permission is granted to anyone to use this software for any purpose,
**  including commercial applications, and to alter it and redistribute it
**  freely, subject to the following restrictions:
**
**  1. The origin of this software must not be misrepresented; you must not
**     claim that you wrote the original software. If you use this software
**     in a product, an acknowledgment in the product documentation would be
**     appreciated but is not required.
**  2. Altered source versions must be plainly marked as such, and must not be
**     misrepresented as being the original software.
**  3. This notice may not be removed or altered from any source distribution.
**
**  Note: Some of the libraries ClanLib may link to may have additional
**  requirements or restrictions.
**
**  File Author(s):
**
**    Harry Storbacka
*/

#pragma once

#include <memory>

namespace clan
{
	/// \addtogroup clanCore_Math clanCore Math
	/// \{

	class Angle_Impl;

	/// \brief Angle unit
	enum AngleUnit
	{
		angle_degrees,
		angle_radians
	};

	/// \brief Euler angle rotation order
	enum EulerOrder
	{
		order_XYZ,
		order_XZY,
		order_YZX,
		order_YXZ,
		order_ZXY,
		order_ZYX
	};

	/// \brief Angle class.
	class Angle
	{
	public:
		/// \brief Constructs a null Angle object.
		Angle();

		/// \brief Constructs an Angle object.
		Angle(float value, AngleUnit unit);

		/// \brief From radians
		///
		/// \param value = value
		///
		/// \return Angle
		static Angle from_radians(float value);

		/// \brief From degrees
		///
		/// \param value = value
		///
		/// \return Angle
		static Angle from_degrees(float value);

		/// \brief Returns the angle as degrees.
		float to_degrees() const;

		/// \brief Returns the angle as radians.
		float to_radians() const;

		/// \brief Set the angle value in degrees.
		void set_degrees(float value_degrees);

		/// \brief Set the angle value in radians.
		void set_radians(float value_radians);

		/// \brief Converts angle to range [0,360] degrees.
		///
		/// \return reference to this object
		Angle &normalize();

		/// \brief Converts angle to range [-180,180] degrees.
		///
		/// \return reference to this object
		Angle &normalize_180();

		/// \brief += operator.
		void operator += (const Angle &angle);

		/// \brief -= operator.
		void operator -= (const Angle &angle);

		/// \brief *= operator.
		void operator *= (const Angle &angle);

		/// \brief /= operator.
		void operator /= (const Angle &angle);

		/// \brief + operator.
		Angle operator + (const Angle &angle) const;

		/// \brief - operator.
		Angle operator - (const Angle &angle) const;

		/// \brief * operator.
		Angle operator * (const Angle &angle) const;

		/// \brief * operator.
		Angle operator * (float value) const;

		/// \brief / operator.
		Angle operator / (const Angle &angle) const;

		/// \brief / operator.
		Angle operator / (float value) const;

		/// \brief < operator.
		bool operator < (const Angle &angle) const;

		/// \brief < operator.
		bool operator <= (const Angle &angle) const;

		/// \brief > operator.
		bool operator > (const Angle &angle) const;

		/// \brief > operator.
		bool operator >= (const Angle &angle) const;

		/// \brief == operator.
		bool operator== (const Angle &angle) const;

		/// \brief != operator.
		bool operator!= (const Angle &angle) const;

	private:
		float value_rad;
	};

	/// \}
}
