#ifndef MATH_VECTOR2_H_
#define MATH_VECTOR2_H_

#include "Math/Math.h"	// VFPU Math
#include "Utility/Alignment.h"

class v2
{
public:
	v2() : x( 0.f ), y( 0.f )
#ifdef DAEDALUS_CTR
		, z( 0.f )
#endif
	{}
	v2( float _x, float _y ) : x( _x ), y( _y )
#ifdef DAEDALUS_CTR
		, z( 0.f )
#endif
	{}
#ifdef DAEDALUS_CTR
	v2( float _x, float _y, float _z ) : x( _x ), y( _y ), z( _z ) {}
#endif

	v2 operator+( const v2 & v ) const
	{
#ifdef DAEDALUS_CTR
		return v2( x + v.x, y + v.y, z + v.z );
#else
		return v2( x + v.x, y + v.y );
#endif
	}

	v2 operator-( const v2 & v ) const
	{
#ifdef DAEDALUS_CTR
		return v2( x - v.x, y - v.y, z - v.z );
#else
		return v2( x - v.x, y - v.y );
#endif
	}

	v2 operator+() const
	{
		return *this;
	}

	v2 operator-() const
	{
#ifdef DAEDALUS_CTR
		return v2( -x, -y, -z );
#else
		return v2( -x, -y );
#endif
	}

	v2 operator*( float s ) const
	{
#ifdef DAEDALUS_CTR
		return v2( x * s, y * s, z * s );
#else
		return v2( x * s, y * s );
#endif
	}

	inline friend v2 operator*( float s, const v2 & v )
	{
#ifdef DAEDALUS_CTR
		return v2( v.x * s, v.y * s, v.z * s );
#else
		return v2( v.x * s, v.y * s );
#endif
	}

	v2 operator/( float s ) const
	{
		float r( 1.0f / s );
#ifdef DAEDALUS_CTR
		return v2( x * r, y * r, z * r );
#else
		return v2( x * r, y * r );
#endif
	}

	const v2 & operator+=( const v2 & rhs )
	{
		x += rhs.x;
		y += rhs.y;
#ifdef DAEDALUS_CTR
		z += rhs.z;
#endif
		return *this;
	}

	const v2 & operator*=( float s )
	{
		x *= s;
		y *= s;
#ifdef DAEDALUS_CTR
		z *= s;
#endif
		return *this;
	}

	float Length() const
	{
#ifdef DAEDALUS_CTR
		return sqrtf( (x*x)+(y*y)+(z*z) );
#else
		return sqrtf( (x*x)+(y*y) );
#endif
	}

	float LengthSq() const
	{
#ifdef DAEDALUS_CTR
		return (x*x)+(y*y)+(z*z);
#else
		return (x*x)+(y*y);
#endif
	}

	float x, y;
#ifdef DAEDALUS_CTR
	float z;
#endif
};

#endif // MATH_VECTOR2_H_
