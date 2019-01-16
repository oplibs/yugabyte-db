/*-------------------------------------------------------------------------
 *
 * int.h
 *	  Routines to perform integer math, while checking for overflows.
 *
 * The routines in this file are intended to be well defined C, without
 * relying on compiler flags like -fwrapv.
 *
 * To reduce the overhead of these routines try to use compiler intrinsics
 * where available. That's not that important for the 16, 32 bit cases, but
 * the 64 bit cases can be considerably faster with intrinsics. In case no
 * intrinsics are available 128 bit math is used where available.
 *
 * Copyright (c) 2017-2018, PostgreSQL Global Development Group
 *
 * src/include/common/int.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef COMMON_INT_H
#define COMMON_INT_H

/*
 * If a + b overflows, return true, otherwise store the result of a + b into
 * *result. The content of *result is implementation defined in case of
 * overflow.
 */
static inline bool
pg_add_s16_overflow(int16 a, int16 b, int16 *result)
{
#if defined(HAVE__BUILTIN_OP_OVERFLOW)
	return __builtin_add_overflow(a, b, result);
#else
	int32		res = (int32) a + (int32) b;

	if (res > PG_INT16_MAX || res < PG_INT16_MIN)
	{
		*result = 0x5EED;		/* to avoid spurious warnings */
		return true;
	}
	*result = (int16) res;
	return false;
#endif
}

/*
 * If a - b overflows, return true, otherwise store the result of a - b into
 * *result. The content of *result is implementation defined in case of
 * overflow.
 */
static inline bool
pg_sub_s16_overflow(int16 a, int16 b, int16 *result)
{
#if defined(HAVE__BUILTIN_OP_OVERFLOW)
	return __builtin_sub_overflow(a, b, result);
#else
	int32		res = (int32) a - (int32) b;

	if (res > PG_INT16_MAX || res < PG_INT16_MIN)
	{
		*result = 0x5EED;		/* to avoid spurious warnings */
		return true;
	}
	*result = (int16) res;
	return false;
#endif
}

/*
 * If a * b overflows, return true, otherwise store the result of a * b into
 * *result. The content of *result is implementation defined in case of
 * overflow.
 */
static inline bool
pg_mul_s16_overflow(int16 a, int16 b, int16 *result)
{
#if defined(HAVE__BUILTIN_OP_OVERFLOW)
	return __builtin_mul_overflow(a, b, result);
#else
	int32		res = (int32) a * (int32) b;

	if (res > PG_INT16_MAX || res < PG_INT16_MIN)
	{
		*result = 0x5EED;		/* to avoid spurious warnings */
		return true;
	}
	*result = (int16) res;
	return false;
#endif
}

/*
 * If a + b overflows, return true, otherwise store the result of a + b into
 * *result. The content of *result is implementation defined in case of
 * overflow.
 */
static inline bool
pg_add_s32_overflow(int32 a, int32 b, int32 *result)
{
#if defined(HAVE__BUILTIN_OP_OVERFLOW)
	return __builtin_add_overflow(a, b, result);
#else
	int64		res = (int64) a + (int64) b;

	if (res > PG_INT32_MAX || res < PG_INT32_MIN)
	{
		*result = 0x5EED;		/* to avoid spurious warnings */
		return true;
	}
	*result = (int32) res;
	return false;
#endif
}

/*
 * If a - b overflows, return true, otherwise store the result of a - b into
 * *result. The content of *result is implementation defined in case of
 * overflow.
 */
static inline bool
pg_sub_s32_overflow(int32 a, int32 b, int32 *result)
{
#if defined(HAVE__BUILTIN_OP_OVERFLOW)
	return __builtin_sub_overflow(a, b, result);
#else
	int64		res = (int64) a - (int64) b;

	if (res > PG_INT32_MAX || res < PG_INT32_MIN)
	{
		*result = 0x5EED;		/* to avoid spurious warnings */
		return true;
	}
	*result = (int32) res;
	return false;
#endif
}

/*
 * If a * b overflows, return true, otherwise store the result of a * b into
 * *result. The content of *result is implementation defined in case of
 * overflow.
 */
static inline bool
pg_mul_s32_overflow(int32 a, int32 b, int32 *result)
{
#if defined(HAVE__BUILTIN_OP_OVERFLOW)
	return __builtin_mul_overflow(a, b, result);
#else
	int64		res = (int64) a * (int64) b;

	if (res > PG_INT32_MAX || res < PG_INT32_MIN)
	{
		*result = 0x5EED;		/* to avoid spurious warnings */
		return true;
	}
	*result = (int32) res;
	return false;
#endif
}

/*
 * If a + b overflows, return true, otherwise store the result of a + b into
 * *result. The content of *result is implementation defined in case of
 * overflow.
 */
static inline bool
pg_add_s64_overflow(int64 a, int64 b, int64 *result)
{
#if defined(HAVE__BUILTIN_OP_OVERFLOW)
	return __builtin_add_overflow(a, b, result);
#elif defined(HAVE_INT128)
	int128		res = (int128) a + (int128) b;

	if (res > PG_INT64_MAX || res < PG_INT64_MIN)
	{
		*result = 0x5EED;		/* to avoid spurious warnings */
		return true;
	}
	*result = (int64) res;
	return false;
#else
	if ((a > 0 && b > 0 && a > PG_INT64_MAX - b) ||
		(a < 0 && b < 0 && a < PG_INT64_MIN - b))
	{
		*result = 0x5EED;		/* to avoid spurious warnings */
		return true;
	}
	*result = a + b;
	return false;
#endif
}

/*
 * If a - b overflows, return true, otherwise store the result of a - b into
 * *result. The content of *result is implementation defined in case of
 * overflow.
 */
static inline bool
pg_sub_s64_overflow(int64 a, int64 b, int64 *result)
{
#if defined(HAVE__BUILTIN_OP_OVERFLOW)
	return __builtin_sub_overflow(a, b, result);
#elif defined(HAVE_INT128)
	int128		res = (int128) a - (int128) b;

	if (res > PG_INT64_MAX || res < PG_INT64_MIN)
	{
		*result = 0x5EED;		/* to avoid spurious warnings */
		return true;
	}
	*result = (int64) res;
	return false;
#else
	if ((a < 0 && b > 0 && a < PG_INT64_MIN + b) ||
		(a > 0 && b < 0 && a > PG_INT64_MAX + b))
	{
		*result = 0x5EED;		/* to avoid spurious warnings */
		return true;
	}
	*result = a - b;
	return false;
#endif
}

/*
 * If a * b overflows, return true, otherwise store the result of a * b into
 * *result. The content of *result is implementation defined in case of
 * overflow.
 */
static inline bool
pg_mul_s64_overflow(int64 a, int64 b, int64 *result)
{
#if defined(HAVE__BUILTIN_OP_OVERFLOW)
	return __builtin_mul_overflow(a, b, result);
#elif defined(HAVE_INT128)
	int128		res = (int128) a * (int128) b;

	if (res > PG_INT64_MAX || res < PG_INT64_MIN)
	{
		*result = 0x5EED;		/* to avoid spurious warnings */
		return true;
	}
	*result = (int64) res;
	return false;
#else
	/*
	 * Overflow can only happen if at least one value is outside the range
	 * sqrt(min)..sqrt(max) so check that first as the division can be quite a
	 * bit more expensive than the multiplication.
	 *
	 * Multiplying by 0 or 1 can't overflow of course and checking for 0
	 * separately avoids any risk of dividing by 0.  Be careful about dividing
	 * INT_MIN by -1 also, note reversing the a and b to ensure we're always
	 * dividing it by a positive value.
	 *
	 */
	if ((a > PG_INT32_MAX || a < PG_INT32_MIN ||
		 b > PG_INT32_MAX || b < PG_INT32_MIN) &&
		a != 0 && a != 1 && b != 0 && b != 1 &&
		((a > 0 && b > 0 && a > PG_INT64_MAX / b) ||
		 (a > 0 && b < 0 && b < PG_INT64_MIN / a) ||
		 (a < 0 && b > 0 && a < PG_INT64_MIN / b) ||
		 (a < 0 && b < 0 && a < PG_INT64_MAX / b)))
	{
		*result = 0x5EED;		/* to avoid spurious warnings */
		return true;
	}
	*result = a * b;
	return false;
#endif
}

#endif							/* COMMON_INT_H */
