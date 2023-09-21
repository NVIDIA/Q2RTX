// Credits to Paril from KEX/Q2RE.
#pragma once

// stores a level time; most newer engines use int64_t for
// time storage, but seconds are handy for compatibility
// with Quake and older mods.
struct gtime_t {
private:
	// times always start at zero, just to prevent memory issues
	int64_t _ms = 0;

	// internal; use _sec/_ms/_min or gtime_t::from_sec(n)/gtime_t::from_ms(n)/gtime_t::from_min(n)
	constexpr explicit gtime_t( const int64_t &ms ) : _ms( ms ) {
	}

public:
	constexpr gtime_t( ) = default;
	constexpr gtime_t( const gtime_t & ) = default;
	constexpr gtime_t &operator=( const gtime_t & ) = default;

	// constructors are here, explicitly named, so you always
	// know what you're getting.

	// new time from ms
	static constexpr gtime_t from_ms( const int64_t &ms ) {
		return gtime_t( ms );
	}

	// new time from seconds
	template<typename T, typename = std::enable_if_t<std::is_floating_point_v<T> || std::is_integral_v<T>>>
	static constexpr gtime_t from_sec( const T &s ) {
		return gtime_t( static_cast<int64_t>( s * 1000 ) );
	}

	// new time from minutes
	template<typename T, typename = std::enable_if_t<std::is_floating_point_v<T> || std::is_integral_v<T>>>
	static constexpr gtime_t from_min( const T &s ) {
		return gtime_t( static_cast<int64_t>( s * 60000 ) );
	}

	// new time from hz
	static constexpr gtime_t from_hz( const uint64_t &hz ) {
		return from_ms( static_cast<int64_t>( ( 1.0 / hz ) * 1000 ) );
	}

	// get value in minutes (truncated for integers)
	template<typename T = float>
	constexpr T minutes( ) const {
		return static_cast<T>( _ms / static_cast<std::conditional_t<std::is_floating_point_v<T>, T, float>>( 60000 ) );
	}

	// get value in seconds (truncated for integers)
	template<typename T = float>
	constexpr T seconds( ) const {
		return static_cast<T>( _ms / static_cast<std::conditional_t<std::is_floating_point_v<T>, T, float>>( 1000 ) );
	}

	// get value in milliseconds
	constexpr const int64_t &milliseconds( ) const {
		return _ms;
	}

	int64_t frames( ) const {
		return _ms / gi.frame_time_ms;
	}

	// check if non-zero
	constexpr explicit operator bool( ) const {
		return !!_ms;
	}

	// invert time
	constexpr gtime_t operator-( ) const {
		return gtime_t( -_ms );
	}

	// operations with other times as input
	constexpr gtime_t operator+( const gtime_t &r ) const {
		return gtime_t( _ms + r._ms );
	}
	constexpr gtime_t operator-( const gtime_t &r ) const {
		return gtime_t( _ms - r._ms );
	}
	constexpr gtime_t &operator+=( const gtime_t &r ) {
		return *this = *this + r;
	}
	constexpr gtime_t &operator-=( const gtime_t &r ) {
		return *this = *this - r;
	}

	// operations with scalars as input
	template<typename T, typename = std::enable_if_t<std::is_floating_point_v<T> || std::is_integral_v<T>>>
	constexpr gtime_t operator*( const T &r ) const {
		return gtime_t::from_ms( static_cast<int64_t>( _ms * r ) );
	}
	template<typename T, typename = std::enable_if_t<std::is_floating_point_v<T> || std::is_integral_v<T>>>
	constexpr gtime_t operator/( const T &r ) const {
		return gtime_t::from_ms( static_cast<int64_t>( _ms / r ) );
	}
	template<typename T, typename = std::enable_if_t<std::is_floating_point_v<T> || std::is_integral_v<T>>>
	constexpr gtime_t &operator*=( const T &r ) {
		return *this = *this * r;
	}
	template<typename T, typename = std::enable_if_t<std::is_floating_point_v<T> || std::is_integral_v<T>>>
	constexpr gtime_t &operator/=( const T &r ) {
		return *this = *this / r;
	}

	// comparisons with gtime_ts
	constexpr bool operator==( const gtime_t &time ) const {
		return _ms == time._ms;
	}
	constexpr bool operator!=( const gtime_t &time ) const {
		return _ms != time._ms;
	}
	constexpr bool operator<( const gtime_t &time ) const {
		return _ms < time._ms;
	}
	constexpr bool operator>( const gtime_t &time ) const {
		return _ms > time._ms;
	}
	constexpr bool operator<=( const gtime_t &time ) const {
		return _ms <= time._ms;
	}
	constexpr bool operator>=( const gtime_t &time ) const {
		return _ms >= time._ms;
	}
};

// user literals, allowing you to specify times
// as 128_sec and 128_ms
constexpr gtime_t operator"" _min( long double s ) {
	return gtime_t::from_min( s );
}
constexpr gtime_t operator"" _min( unsigned long long int s ) {
	return gtime_t::from_min( s );
}
constexpr gtime_t operator"" _sec( long double s ) {
	return gtime_t::from_sec( s );
}
constexpr gtime_t operator"" _sec( unsigned long long int s ) {
	return gtime_t::from_sec( s );
}
constexpr gtime_t operator"" _ms( long double s ) {
	return gtime_t::from_ms( static_cast<int64_t>( s ) );
}
constexpr gtime_t operator"" _ms( unsigned long long int s ) {
	return gtime_t::from_ms( static_cast<int64_t>( s ) );
}
constexpr gtime_t operator"" _hz( unsigned long long int s ) {
	return gtime_t::from_ms( static_cast<int64_t>( ( 1.0 / s ) * 1000 ) );
}

#define SERVER_TICK_RATE gi.tick_rate // in hz
extern gtime_t FRAME_TIME_S;
extern gtime_t FRAME_TIME_MS;

