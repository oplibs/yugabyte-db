/*-------------------------------------------------------------------------
 * saslprep.c
 *		SASLprep normalization, for SCRAM authentication
 *
 * The SASLprep algorithm is used to process a user-supplied password into
 * canonical form.  For more details, see:
 *
 * [RFC3454] Preparation of Internationalized Strings ("stringprep"),
 *	  http://www.ietf.org/rfc/rfc3454.txt
 *
 * [RFC4013] SASLprep: Stringprep Profile for User Names and Passwords
 *	  http://www.ietf.org/rfc/rfc4013.txt
 *
 *
 * Portions Copyright (c) 2017-2021, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *	  src/common/saslprep.c
 *
 *-------------------------------------------------------------------------
 */
#ifndef FRONTEND
#include "postgres.h"
#else
#include "postgres_fe.h"
#endif

#include "common/saslprep.h"
#include "common/string.h"
#include "common/unicode_norm.h"
#include "mb/pg_wchar.h"

/*
 * In backend, we will use palloc/pfree.  In frontend, use malloc, and
 * return SASLPREP_OOM on out-of-memory.
 */
#ifndef FRONTEND
#define STRDUP(s) pstrdup(s)
#define ALLOC(size) palloc(size)
#define FREE(size) pfree(size)
#else
#define STRDUP(s) strdup(s)
#define ALLOC(size) malloc(size)
#define FREE(size) free(size)
#endif

/* Prototypes for local functions */
static int	codepoint_range_cmp(const void *a, const void *b);
static bool is_code_in_table(pg_wchar code, const pg_wchar *map, int mapsize);
static int	pg_utf8_string_len(const char *source);

/*
 * Stringprep Mapping Tables.
 *
 * The stringprep specification includes a number of tables of Unicode
 * codepoints, used in different parts of the algorithm.  They are below,
 * as arrays of codepoint ranges.  Each range is a pair of codepoints,
 * for the first and last codepoint included the range (inclusive!).
 */

/*
 * C.1.2 Non-ASCII space characters
 *
 * These are all mapped to the ASCII space character (U+00A0).
 */
static const pg_wchar non_ascii_space_ranges[] =
{
	0x00A0, 0x00A0,
	0x1680, 0x1680,
	0x2000, 0x200B,
	0x202F, 0x202F,
	0x205F, 0x205F,
	0x3000, 0x3000
};

/*
 * B.1 Commonly mapped to nothing
 *
 * If any of these appear in the input, they are removed.
 */
static const pg_wchar commonly_mapped_to_nothing_ranges[] =
{
	0x00AD, 0x00AD,
	0x034F, 0x034F,
	0x1806, 0x1806,
	0x180B, 0x180D,
	0x200B, 0x200D,
	0x2060, 0x2060,
	0xFE00, 0xFE0F,
	0xFEFF, 0xFEFF
};

/*
 * prohibited_output_ranges is a union of all the characters from
 * the following tables:
 *
 * C.1.2 Non-ASCII space characters
 * C.2.1 ASCII control characters
 * C.2.2 Non-ASCII control characters
 * C.3 Private Use characters
 * C.4 Non-character code points
 * C.5 Surrogate code points
 * C.6 Inappropriate for plain text characters
 * C.7 Inappropriate for canonical representation characters
 * C.7 Change display properties or deprecated characters
 * C.8 Tagging characters
 *
 * These are the tables that are listed as "prohibited output"
 * characters in the SASLprep profile.
 *
 * The comment after each code range indicates which source table
 * the code came from.  Note that there is some overlap in the source
 * tables, so one code might originate from multiple source tables.
 * Adjacent ranges have also been merged together, to save space.
 */
static const pg_wchar prohibited_output_ranges[] =
{
	0x0000, 0x001F,				/* C.2.1 */
	0x007F, 0x00A0,				/* C.1.2, C.2.1, C.2.2 */
	0x0340, 0x0341,				/* C.8 */
	0x06DD, 0x06DD,				/* C.2.2 */
	0x070F, 0x070F,				/* C.2.2 */
	0x1680, 0x1680,				/* C.1.2 */
	0x180E, 0x180E,				/* C.2.2 */
	0x2000, 0x200F,				/* C.1.2, C.2.2, C.8 */
	0x2028, 0x202F,				/* C.1.2, C.2.2, C.8 */
	0x205F, 0x2063,				/* C.1.2, C.2.2 */
	0x206A, 0x206F,				/* C.2.2, C.8 */
	0x2FF0, 0x2FFB,				/* C.7 */
	0x3000, 0x3000,				/* C.1.2 */
	0xD800, 0xF8FF,				/* C.3, C.5 */
	0xFDD0, 0xFDEF,				/* C.4 */
	0xFEFF, 0xFEFF,				/* C.2.2 */
	0xFFF9, 0xFFFF,				/* C.2.2, C.4, C.6 */
	0x1D173, 0x1D17A,			/* C.2.2 */
	0x1FFFE, 0x1FFFF,			/* C.4 */
	0x2FFFE, 0x2FFFF,			/* C.4 */
	0x3FFFE, 0x3FFFF,			/* C.4 */
	0x4FFFE, 0x4FFFF,			/* C.4 */
	0x5FFFE, 0x5FFFF,			/* C.4 */
	0x6FFFE, 0x6FFFF,			/* C.4 */
	0x7FFFE, 0x7FFFF,			/* C.4 */
	0x8FFFE, 0x8FFFF,			/* C.4 */
	0x9FFFE, 0x9FFFF,			/* C.4 */
	0xAFFFE, 0xAFFFF,			/* C.4 */
	0xBFFFE, 0xBFFFF,			/* C.4 */
	0xCFFFE, 0xCFFFF,			/* C.4 */
	0xDFFFE, 0xDFFFF,			/* C.4 */
	0xE0001, 0xE0001,			/* C.9 */
	0xE0020, 0xE007F,			/* C.9 */
	0xEFFFE, 0xEFFFF,			/* C.4 */
	0xF0000, 0xFFFFF,			/* C.3, C.4 */
	0x100000, 0x10FFFF			/* C.3, C.4 */
};

/* A.1 Unassigned code points in Unicode 3.2 */
static const pg_wchar unassigned_codepoint_ranges[] =
{
	0x0221, 0x0221,
	0x0234, 0x024F,
	0x02AE, 0x02AF,
	0x02EF, 0x02FF,
	0x0350, 0x035F,
	0x0370, 0x0373,
	0x0376, 0x0379,
	0x037B, 0x037D,
	0x037F, 0x0383,
	0x038B, 0x038B,
	0x038D, 0x038D,
	0x03A2, 0x03A2,
	0x03CF, 0x03CF,
	0x03F7, 0x03FF,
	0x0487, 0x0487,
	0x04CF, 0x04CF,
	0x04F6, 0x04F7,
	0x04FA, 0x04FF,
	0x0510, 0x0530,
	0x0557, 0x0558,
	0x0560, 0x0560,
	0x0588, 0x0588,
	0x058B, 0x0590,
	0x05A2, 0x05A2,
	0x05BA, 0x05BA,
	0x05C5, 0x05CF,
	0x05EB, 0x05EF,
	0x05F5, 0x060B,
	0x060D, 0x061A,
	0x061C, 0x061E,
	0x0620, 0x0620,
	0x063B, 0x063F,
	0x0656, 0x065F,
	0x06EE, 0x06EF,
	0x06FF, 0x06FF,
	0x070E, 0x070E,
	0x072D, 0x072F,
	0x074B, 0x077F,
	0x07B2, 0x0900,
	0x0904, 0x0904,
	0x093A, 0x093B,
	0x094E, 0x094F,
	0x0955, 0x0957,
	0x0971, 0x0980,
	0x0984, 0x0984,
	0x098D, 0x098E,
	0x0991, 0x0992,
	0x09A9, 0x09A9,
	0x09B1, 0x09B1,
	0x09B3, 0x09B5,
	0x09BA, 0x09BB,
	0x09BD, 0x09BD,
	0x09C5, 0x09C6,
	0x09C9, 0x09CA,
	0x09CE, 0x09D6,
	0x09D8, 0x09DB,
	0x09DE, 0x09DE,
	0x09E4, 0x09E5,
	0x09FB, 0x0A01,
	0x0A03, 0x0A04,
	0x0A0B, 0x0A0E,
	0x0A11, 0x0A12,
	0x0A29, 0x0A29,
	0x0A31, 0x0A31,
	0x0A34, 0x0A34,
	0x0A37, 0x0A37,
	0x0A3A, 0x0A3B,
	0x0A3D, 0x0A3D,
	0x0A43, 0x0A46,
	0x0A49, 0x0A4A,
	0x0A4E, 0x0A58,
	0x0A5D, 0x0A5D,
	0x0A5F, 0x0A65,
	0x0A75, 0x0A80,
	0x0A84, 0x0A84,
	0x0A8C, 0x0A8C,
	0x0A8E, 0x0A8E,
	0x0A92, 0x0A92,
	0x0AA9, 0x0AA9,
	0x0AB1, 0x0AB1,
	0x0AB4, 0x0AB4,
	0x0ABA, 0x0ABB,
	0x0AC6, 0x0AC6,
	0x0ACA, 0x0ACA,
	0x0ACE, 0x0ACF,
	0x0AD1, 0x0ADF,
	0x0AE1, 0x0AE5,
	0x0AF0, 0x0B00,
	0x0B04, 0x0B04,
	0x0B0D, 0x0B0E,
	0x0B11, 0x0B12,
	0x0B29, 0x0B29,
	0x0B31, 0x0B31,
	0x0B34, 0x0B35,
	0x0B3A, 0x0B3B,
	0x0B44, 0x0B46,
	0x0B49, 0x0B4A,
	0x0B4E, 0x0B55,
	0x0B58, 0x0B5B,
	0x0B5E, 0x0B5E,
	0x0B62, 0x0B65,
	0x0B71, 0x0B81,
	0x0B84, 0x0B84,
	0x0B8B, 0x0B8D,
	0x0B91, 0x0B91,
	0x0B96, 0x0B98,
	0x0B9B, 0x0B9B,
	0x0B9D, 0x0B9D,
	0x0BA0, 0x0BA2,
	0x0BA5, 0x0BA7,
	0x0BAB, 0x0BAD,
	0x0BB6, 0x0BB6,
	0x0BBA, 0x0BBD,
	0x0BC3, 0x0BC5,
	0x0BC9, 0x0BC9,
	0x0BCE, 0x0BD6,
	0x0BD8, 0x0BE6,
	0x0BF3, 0x0C00,
	0x0C04, 0x0C04,
	0x0C0D, 0x0C0D,
	0x0C11, 0x0C11,
	0x0C29, 0x0C29,
	0x0C34, 0x0C34,
	0x0C3A, 0x0C3D,
	0x0C45, 0x0C45,
	0x0C49, 0x0C49,
	0x0C4E, 0x0C54,
	0x0C57, 0x0C5F,
	0x0C62, 0x0C65,
	0x0C70, 0x0C81,
	0x0C84, 0x0C84,
	0x0C8D, 0x0C8D,
	0x0C91, 0x0C91,
	0x0CA9, 0x0CA9,
	0x0CB4, 0x0CB4,
	0x0CBA, 0x0CBD,
	0x0CC5, 0x0CC5,
	0x0CC9, 0x0CC9,
	0x0CCE, 0x0CD4,
	0x0CD7, 0x0CDD,
	0x0CDF, 0x0CDF,
	0x0CE2, 0x0CE5,
	0x0CF0, 0x0D01,
	0x0D04, 0x0D04,
	0x0D0D, 0x0D0D,
	0x0D11, 0x0D11,
	0x0D29, 0x0D29,
	0x0D3A, 0x0D3D,
	0x0D44, 0x0D45,
	0x0D49, 0x0D49,
	0x0D4E, 0x0D56,
	0x0D58, 0x0D5F,
	0x0D62, 0x0D65,
	0x0D70, 0x0D81,
	0x0D84, 0x0D84,
	0x0D97, 0x0D99,
	0x0DB2, 0x0DB2,
	0x0DBC, 0x0DBC,
	0x0DBE, 0x0DBF,
	0x0DC7, 0x0DC9,
	0x0DCB, 0x0DCE,
	0x0DD5, 0x0DD5,
	0x0DD7, 0x0DD7,
	0x0DE0, 0x0DF1,
	0x0DF5, 0x0E00,
	0x0E3B, 0x0E3E,
	0x0E5C, 0x0E80,
	0x0E83, 0x0E83,
	0x0E85, 0x0E86,
	0x0E89, 0x0E89,
	0x0E8B, 0x0E8C,
	0x0E8E, 0x0E93,
	0x0E98, 0x0E98,
	0x0EA0, 0x0EA0,
	0x0EA4, 0x0EA4,
	0x0EA6, 0x0EA6,
	0x0EA8, 0x0EA9,
	0x0EAC, 0x0EAC,
	0x0EBA, 0x0EBA,
	0x0EBE, 0x0EBF,
	0x0EC5, 0x0EC5,
	0x0EC7, 0x0EC7,
	0x0ECE, 0x0ECF,
	0x0EDA, 0x0EDB,
	0x0EDE, 0x0EFF,
	0x0F48, 0x0F48,
	0x0F6B, 0x0F70,
	0x0F8C, 0x0F8F,
	0x0F98, 0x0F98,
	0x0FBD, 0x0FBD,
	0x0FCD, 0x0FCE,
	0x0FD0, 0x0FFF,
	0x1022, 0x1022,
	0x1028, 0x1028,
	0x102B, 0x102B,
	0x1033, 0x1035,
	0x103A, 0x103F,
	0x105A, 0x109F,
	0x10C6, 0x10CF,
	0x10F9, 0x10FA,
	0x10FC, 0x10FF,
	0x115A, 0x115E,
	0x11A3, 0x11A7,
	0x11FA, 0x11FF,
	0x1207, 0x1207,
	0x1247, 0x1247,
	0x1249, 0x1249,
	0x124E, 0x124F,
	0x1257, 0x1257,
	0x1259, 0x1259,
	0x125E, 0x125F,
	0x1287, 0x1287,
	0x1289, 0x1289,
	0x128E, 0x128F,
	0x12AF, 0x12AF,
	0x12B1, 0x12B1,
	0x12B6, 0x12B7,
	0x12BF, 0x12BF,
	0x12C1, 0x12C1,
	0x12C6, 0x12C7,
	0x12CF, 0x12CF,
	0x12D7, 0x12D7,
	0x12EF, 0x12EF,
	0x130F, 0x130F,
	0x1311, 0x1311,
	0x1316, 0x1317,
	0x131F, 0x131F,
	0x1347, 0x1347,
	0x135B, 0x1360,
	0x137D, 0x139F,
	0x13F5, 0x1400,
	0x1677, 0x167F,
	0x169D, 0x169F,
	0x16F1, 0x16FF,
	0x170D, 0x170D,
	0x1715, 0x171F,
	0x1737, 0x173F,
	0x1754, 0x175F,
	0x176D, 0x176D,
	0x1771, 0x1771,
	0x1774, 0x177F,
	0x17DD, 0x17DF,
	0x17EA, 0x17FF,
	0x180F, 0x180F,
	0x181A, 0x181F,
	0x1878, 0x187F,
	0x18AA, 0x1DFF,
	0x1E9C, 0x1E9F,
	0x1EFA, 0x1EFF,
	0x1F16, 0x1F17,
	0x1F1E, 0x1F1F,
	0x1F46, 0x1F47,
	0x1F4E, 0x1F4F,
	0x1F58, 0x1F58,
	0x1F5A, 0x1F5A,
	0x1F5C, 0x1F5C,
	0x1F5E, 0x1F5E,
	0x1F7E, 0x1F7F,
	0x1FB5, 0x1FB5,
	0x1FC5, 0x1FC5,
	0x1FD4, 0x1FD5,
	0x1FDC, 0x1FDC,
	0x1FF0, 0x1FF1,
	0x1FF5, 0x1FF5,
	0x1FFF, 0x1FFF,
	0x2053, 0x2056,
	0x2058, 0x205E,
	0x2064, 0x2069,
	0x2072, 0x2073,
	0x208F, 0x209F,
	0x20B2, 0x20CF,
	0x20EB, 0x20FF,
	0x213B, 0x213C,
	0x214C, 0x2152,
	0x2184, 0x218F,
	0x23CF, 0x23FF,
	0x2427, 0x243F,
	0x244B, 0x245F,
	0x24FF, 0x24FF,
	0x2614, 0x2615,
	0x2618, 0x2618,
	0x267E, 0x267F,
	0x268A, 0x2700,
	0x2705, 0x2705,
	0x270A, 0x270B,
	0x2728, 0x2728,
	0x274C, 0x274C,
	0x274E, 0x274E,
	0x2753, 0x2755,
	0x2757, 0x2757,
	0x275F, 0x2760,
	0x2795, 0x2797,
	0x27B0, 0x27B0,
	0x27BF, 0x27CF,
	0x27EC, 0x27EF,
	0x2B00, 0x2E7F,
	0x2E9A, 0x2E9A,
	0x2EF4, 0x2EFF,
	0x2FD6, 0x2FEF,
	0x2FFC, 0x2FFF,
	0x3040, 0x3040,
	0x3097, 0x3098,
	0x3100, 0x3104,
	0x312D, 0x3130,
	0x318F, 0x318F,
	0x31B8, 0x31EF,
	0x321D, 0x321F,
	0x3244, 0x3250,
	0x327C, 0x327E,
	0x32CC, 0x32CF,
	0x32FF, 0x32FF,
	0x3377, 0x337A,
	0x33DE, 0x33DF,
	0x33FF, 0x33FF,
	0x4DB6, 0x4DFF,
	0x9FA6, 0x9FFF,
	0xA48D, 0xA48F,
	0xA4C7, 0xABFF,
	0xD7A4, 0xD7FF,
	0xFA2E, 0xFA2F,
	0xFA6B, 0xFAFF,
	0xFB07, 0xFB12,
	0xFB18, 0xFB1C,
	0xFB37, 0xFB37,
	0xFB3D, 0xFB3D,
	0xFB3F, 0xFB3F,
	0xFB42, 0xFB42,
	0xFB45, 0xFB45,
	0xFBB2, 0xFBD2,
	0xFD40, 0xFD4F,
	0xFD90, 0xFD91,
	0xFDC8, 0xFDCF,
	0xFDFD, 0xFDFF,
	0xFE10, 0xFE1F,
	0xFE24, 0xFE2F,
	0xFE47, 0xFE48,
	0xFE53, 0xFE53,
	0xFE67, 0xFE67,
	0xFE6C, 0xFE6F,
	0xFE75, 0xFE75,
	0xFEFD, 0xFEFE,
	0xFF00, 0xFF00,
	0xFFBF, 0xFFC1,
	0xFFC8, 0xFFC9,
	0xFFD0, 0xFFD1,
	0xFFD8, 0xFFD9,
	0xFFDD, 0xFFDF,
	0xFFE7, 0xFFE7,
	0xFFEF, 0xFFF8,
	0x10000, 0x102FF,
	0x1031F, 0x1031F,
	0x10324, 0x1032F,
	0x1034B, 0x103FF,
	0x10426, 0x10427,
	0x1044E, 0x1CFFF,
	0x1D0F6, 0x1D0FF,
	0x1D127, 0x1D129,
	0x1D1DE, 0x1D3FF,
	0x1D455, 0x1D455,
	0x1D49D, 0x1D49D,
	0x1D4A0, 0x1D4A1,
	0x1D4A3, 0x1D4A4,
	0x1D4A7, 0x1D4A8,
	0x1D4AD, 0x1D4AD,
	0x1D4BA, 0x1D4BA,
	0x1D4BC, 0x1D4BC,
	0x1D4C1, 0x1D4C1,
	0x1D4C4, 0x1D4C4,
	0x1D506, 0x1D506,
	0x1D50B, 0x1D50C,
	0x1D515, 0x1D515,
	0x1D51D, 0x1D51D,
	0x1D53A, 0x1D53A,
	0x1D53F, 0x1D53F,
	0x1D545, 0x1D545,
	0x1D547, 0x1D549,
	0x1D551, 0x1D551,
	0x1D6A4, 0x1D6A7,
	0x1D7CA, 0x1D7CD,
	0x1D800, 0x1FFFD,
	0x2A6D7, 0x2F7FF,
	0x2FA1E, 0x2FFFD,
	0x30000, 0x3FFFD,
	0x40000, 0x4FFFD,
	0x50000, 0x5FFFD,
	0x60000, 0x6FFFD,
	0x70000, 0x7FFFD,
	0x80000, 0x8FFFD,
	0x90000, 0x9FFFD,
	0xA0000, 0xAFFFD,
	0xB0000, 0xBFFFD,
	0xC0000, 0xCFFFD,
	0xD0000, 0xDFFFD,
	0xE0000, 0xE0000,
	0xE0002, 0xE001F,
	0xE0080, 0xEFFFD
};

/* D.1 Characters with bidirectional property "R" or "AL" */
static const pg_wchar RandALCat_codepoint_ranges[] =
{
	0x05BE, 0x05BE,
	0x05C0, 0x05C0,
	0x05C3, 0x05C3,
	0x05D0, 0x05EA,
	0x05F0, 0x05F4,
	0x061B, 0x061B,
	0x061F, 0x061F,
	0x0621, 0x063A,
	0x0640, 0x064A,
	0x066D, 0x066F,
	0x0671, 0x06D5,
	0x06DD, 0x06DD,
	0x06E5, 0x06E6,
	0x06FA, 0x06FE,
	0x0700, 0x070D,
	0x0710, 0x0710,
	0x0712, 0x072C,
	0x0780, 0x07A5,
	0x07B1, 0x07B1,
	0x200F, 0x200F,
	0xFB1D, 0xFB1D,
	0xFB1F, 0xFB28,
	0xFB2A, 0xFB36,
	0xFB38, 0xFB3C,
	0xFB3E, 0xFB3E,
	0xFB40, 0xFB41,
	0xFB43, 0xFB44,
	0xFB46, 0xFBB1,
	0xFBD3, 0xFD3D,
	0xFD50, 0xFD8F,
	0xFD92, 0xFDC7,
	0xFDF0, 0xFDFC,
	0xFE70, 0xFE74,
	0xFE76, 0xFEFC
};

/* D.2 Characters with bidirectional property "L" */
static const pg_wchar LCat_codepoint_ranges[] =
{
	0x0041, 0x005A,
	0x0061, 0x007A,
	0x00AA, 0x00AA,
	0x00B5, 0x00B5,
	0x00BA, 0x00BA,
	0x00C0, 0x00D6,
	0x00D8, 0x00F6,
	0x00F8, 0x0220,
	0x0222, 0x0233,
	0x0250, 0x02AD,
	0x02B0, 0x02B8,
	0x02BB, 0x02C1,
	0x02D0, 0x02D1,
	0x02E0, 0x02E4,
	0x02EE, 0x02EE,
	0x037A, 0x037A,
	0x0386, 0x0386,
	0x0388, 0x038A,
	0x038C, 0x038C,
	0x038E, 0x03A1,
	0x03A3, 0x03CE,
	0x03D0, 0x03F5,
	0x0400, 0x0482,
	0x048A, 0x04CE,
	0x04D0, 0x04F5,
	0x04F8, 0x04F9,
	0x0500, 0x050F,
	0x0531, 0x0556,
	0x0559, 0x055F,
	0x0561, 0x0587,
	0x0589, 0x0589,
	0x0903, 0x0903,
	0x0905, 0x0939,
	0x093D, 0x0940,
	0x0949, 0x094C,
	0x0950, 0x0950,
	0x0958, 0x0961,
	0x0964, 0x0970,
	0x0982, 0x0983,
	0x0985, 0x098C,
	0x098F, 0x0990,
	0x0993, 0x09A8,
	0x09AA, 0x09B0,
	0x09B2, 0x09B2,
	0x09B6, 0x09B9,
	0x09BE, 0x09C0,
	0x09C7, 0x09C8,
	0x09CB, 0x09CC,
	0x09D7, 0x09D7,
	0x09DC, 0x09DD,
	0x09DF, 0x09E1,
	0x09E6, 0x09F1,
	0x09F4, 0x09FA,
	0x0A05, 0x0A0A,
	0x0A0F, 0x0A10,
	0x0A13, 0x0A28,
	0x0A2A, 0x0A30,
	0x0A32, 0x0A33,
	0x0A35, 0x0A36,
	0x0A38, 0x0A39,
	0x0A3E, 0x0A40,
	0x0A59, 0x0A5C,
	0x0A5E, 0x0A5E,
	0x0A66, 0x0A6F,
	0x0A72, 0x0A74,
	0x0A83, 0x0A83,
	0x0A85, 0x0A8B,
	0x0A8D, 0x0A8D,
	0x0A8F, 0x0A91,
	0x0A93, 0x0AA8,
	0x0AAA, 0x0AB0,
	0x0AB2, 0x0AB3,
	0x0AB5, 0x0AB9,
	0x0ABD, 0x0AC0,
	0x0AC9, 0x0AC9,
	0x0ACB, 0x0ACC,
	0x0AD0, 0x0AD0,
	0x0AE0, 0x0AE0,
	0x0AE6, 0x0AEF,
	0x0B02, 0x0B03,
	0x0B05, 0x0B0C,
	0x0B0F, 0x0B10,
	0x0B13, 0x0B28,
	0x0B2A, 0x0B30,
	0x0B32, 0x0B33,
	0x0B36, 0x0B39,
	0x0B3D, 0x0B3E,
	0x0B40, 0x0B40,
	0x0B47, 0x0B48,
	0x0B4B, 0x0B4C,
	0x0B57, 0x0B57,
	0x0B5C, 0x0B5D,
	0x0B5F, 0x0B61,
	0x0B66, 0x0B70,
	0x0B83, 0x0B83,
	0x0B85, 0x0B8A,
	0x0B8E, 0x0B90,
	0x0B92, 0x0B95,
	0x0B99, 0x0B9A,
	0x0B9C, 0x0B9C,
	0x0B9E, 0x0B9F,
	0x0BA3, 0x0BA4,
	0x0BA8, 0x0BAA,
	0x0BAE, 0x0BB5,
	0x0BB7, 0x0BB9,
	0x0BBE, 0x0BBF,
	0x0BC1, 0x0BC2,
	0x0BC6, 0x0BC8,
	0x0BCA, 0x0BCC,
	0x0BD7, 0x0BD7,
	0x0BE7, 0x0BF2,
	0x0C01, 0x0C03,
	0x0C05, 0x0C0C,
	0x0C0E, 0x0C10,
	0x0C12, 0x0C28,
	0x0C2A, 0x0C33,
	0x0C35, 0x0C39,
	0x0C41, 0x0C44,
	0x0C60, 0x0C61,
	0x0C66, 0x0C6F,
	0x0C82, 0x0C83,
	0x0C85, 0x0C8C,
	0x0C8E, 0x0C90,
	0x0C92, 0x0CA8,
	0x0CAA, 0x0CB3,
	0x0CB5, 0x0CB9,
	0x0CBE, 0x0CBE,
	0x0CC0, 0x0CC4,
	0x0CC7, 0x0CC8,
	0x0CCA, 0x0CCB,
	0x0CD5, 0x0CD6,
	0x0CDE, 0x0CDE,
	0x0CE0, 0x0CE1,
	0x0CE6, 0x0CEF,
	0x0D02, 0x0D03,
	0x0D05, 0x0D0C,
	0x0D0E, 0x0D10,
	0x0D12, 0x0D28,
	0x0D2A, 0x0D39,
	0x0D3E, 0x0D40,
	0x0D46, 0x0D48,
	0x0D4A, 0x0D4C,
	0x0D57, 0x0D57,
	0x0D60, 0x0D61,
	0x0D66, 0x0D6F,
	0x0D82, 0x0D83,
	0x0D85, 0x0D96,
	0x0D9A, 0x0DB1,
	0x0DB3, 0x0DBB,
	0x0DBD, 0x0DBD,
	0x0DC0, 0x0DC6,
	0x0DCF, 0x0DD1,
	0x0DD8, 0x0DDF,
	0x0DF2, 0x0DF4,
	0x0E01, 0x0E30,
	0x0E32, 0x0E33,
	0x0E40, 0x0E46,
	0x0E4F, 0x0E5B,
	0x0E81, 0x0E82,
	0x0E84, 0x0E84,
	0x0E87, 0x0E88,
	0x0E8A, 0x0E8A,
	0x0E8D, 0x0E8D,
	0x0E94, 0x0E97,
	0x0E99, 0x0E9F,
	0x0EA1, 0x0EA3,
	0x0EA5, 0x0EA5,
	0x0EA7, 0x0EA7,
	0x0EAA, 0x0EAB,
	0x0EAD, 0x0EB0,
	0x0EB2, 0x0EB3,
	0x0EBD, 0x0EBD,
	0x0EC0, 0x0EC4,
	0x0EC6, 0x0EC6,
	0x0ED0, 0x0ED9,
	0x0EDC, 0x0EDD,
	0x0F00, 0x0F17,
	0x0F1A, 0x0F34,
	0x0F36, 0x0F36,
	0x0F38, 0x0F38,
	0x0F3E, 0x0F47,
	0x0F49, 0x0F6A,
	0x0F7F, 0x0F7F,
	0x0F85, 0x0F85,
	0x0F88, 0x0F8B,
	0x0FBE, 0x0FC5,
	0x0FC7, 0x0FCC,
	0x0FCF, 0x0FCF,
	0x1000, 0x1021,
	0x1023, 0x1027,
	0x1029, 0x102A,
	0x102C, 0x102C,
	0x1031, 0x1031,
	0x1038, 0x1038,
	0x1040, 0x1057,
	0x10A0, 0x10C5,
	0x10D0, 0x10F8,
	0x10FB, 0x10FB,
	0x1100, 0x1159,
	0x115F, 0x11A2,
	0x11A8, 0x11F9,
	0x1200, 0x1206,
	0x1208, 0x1246,
	0x1248, 0x1248,
	0x124A, 0x124D,
	0x1250, 0x1256,
	0x1258, 0x1258,
	0x125A, 0x125D,
	0x1260, 0x1286,
	0x1288, 0x1288,
	0x128A, 0x128D,
	0x1290, 0x12AE,
	0x12B0, 0x12B0,
	0x12B2, 0x12B5,
	0x12B8, 0x12BE,
	0x12C0, 0x12C0,
	0x12C2, 0x12C5,
	0x12C8, 0x12CE,
	0x12D0, 0x12D6,
	0x12D8, 0x12EE,
	0x12F0, 0x130E,
	0x1310, 0x1310,
	0x1312, 0x1315,
	0x1318, 0x131E,
	0x1320, 0x1346,
	0x1348, 0x135A,
	0x1361, 0x137C,
	0x13A0, 0x13F4,
	0x1401, 0x1676,
	0x1681, 0x169A,
	0x16A0, 0x16F0,
	0x1700, 0x170C,
	0x170E, 0x1711,
	0x1720, 0x1731,
	0x1735, 0x1736,
	0x1740, 0x1751,
	0x1760, 0x176C,
	0x176E, 0x1770,
	0x1780, 0x17B6,
	0x17BE, 0x17C5,
	0x17C7, 0x17C8,
	0x17D4, 0x17DA,
	0x17DC, 0x17DC,
	0x17E0, 0x17E9,
	0x1810, 0x1819,
	0x1820, 0x1877,
	0x1880, 0x18A8,
	0x1E00, 0x1E9B,
	0x1EA0, 0x1EF9,
	0x1F00, 0x1F15,
	0x1F18, 0x1F1D,
	0x1F20, 0x1F45,
	0x1F48, 0x1F4D,
	0x1F50, 0x1F57,
	0x1F59, 0x1F59,
	0x1F5B, 0x1F5B,
	0x1F5D, 0x1F5D,
	0x1F5F, 0x1F7D,
	0x1F80, 0x1FB4,
	0x1FB6, 0x1FBC,
	0x1FBE, 0x1FBE,
	0x1FC2, 0x1FC4,
	0x1FC6, 0x1FCC,
	0x1FD0, 0x1FD3,
	0x1FD6, 0x1FDB,
	0x1FE0, 0x1FEC,
	0x1FF2, 0x1FF4,
	0x1FF6, 0x1FFC,
	0x200E, 0x200E,
	0x2071, 0x2071,
	0x207F, 0x207F,
	0x2102, 0x2102,
	0x2107, 0x2107,
	0x210A, 0x2113,
	0x2115, 0x2115,
	0x2119, 0x211D,
	0x2124, 0x2124,
	0x2126, 0x2126,
	0x2128, 0x2128,
	0x212A, 0x212D,
	0x212F, 0x2131,
	0x2133, 0x2139,
	0x213D, 0x213F,
	0x2145, 0x2149,
	0x2160, 0x2183,
	0x2336, 0x237A,
	0x2395, 0x2395,
	0x249C, 0x24E9,
	0x3005, 0x3007,
	0x3021, 0x3029,
	0x3031, 0x3035,
	0x3038, 0x303C,
	0x3041, 0x3096,
	0x309D, 0x309F,
	0x30A1, 0x30FA,
	0x30FC, 0x30FF,
	0x3105, 0x312C,
	0x3131, 0x318E,
	0x3190, 0x31B7,
	0x31F0, 0x321C,
	0x3220, 0x3243,
	0x3260, 0x327B,
	0x327F, 0x32B0,
	0x32C0, 0x32CB,
	0x32D0, 0x32FE,
	0x3300, 0x3376,
	0x337B, 0x33DD,
	0x33E0, 0x33FE,
	0x3400, 0x4DB5,
	0x4E00, 0x9FA5,
	0xA000, 0xA48C,
	0xAC00, 0xD7A3,
	0xD800, 0xFA2D,
	0xFA30, 0xFA6A,
	0xFB00, 0xFB06,
	0xFB13, 0xFB17,
	0xFF21, 0xFF3A,
	0xFF41, 0xFF5A,
	0xFF66, 0xFFBE,
	0xFFC2, 0xFFC7,
	0xFFCA, 0xFFCF,
	0xFFD2, 0xFFD7,
	0xFFDA, 0xFFDC,
	0x10300, 0x1031E,
	0x10320, 0x10323,
	0x10330, 0x1034A,
	0x10400, 0x10425,
	0x10428, 0x1044D,
	0x1D000, 0x1D0F5,
	0x1D100, 0x1D126,
	0x1D12A, 0x1D166,
	0x1D16A, 0x1D172,
	0x1D183, 0x1D184,
	0x1D18C, 0x1D1A9,
	0x1D1AE, 0x1D1DD,
	0x1D400, 0x1D454,
	0x1D456, 0x1D49C,
	0x1D49E, 0x1D49F,
	0x1D4A2, 0x1D4A2,
	0x1D4A5, 0x1D4A6,
	0x1D4A9, 0x1D4AC,
	0x1D4AE, 0x1D4B9,
	0x1D4BB, 0x1D4BB,
	0x1D4BD, 0x1D4C0,
	0x1D4C2, 0x1D4C3,
	0x1D4C5, 0x1D505,
	0x1D507, 0x1D50A,
	0x1D50D, 0x1D514,
	0x1D516, 0x1D51C,
	0x1D51E, 0x1D539,
	0x1D53B, 0x1D53E,
	0x1D540, 0x1D544,
	0x1D546, 0x1D546,
	0x1D54A, 0x1D550,
	0x1D552, 0x1D6A3,
	0x1D6A8, 0x1D7C9,
	0x20000, 0x2A6D6,
	0x2F800, 0x2FA1D,
	0xF0000, 0xFFFFD,
	0x100000, 0x10FFFD
};

/* End of stringprep tables */


/* Is the given Unicode codepoint in the given table of ranges? */
#define IS_CODE_IN_TABLE(code, map) is_code_in_table(code, map, lengthof(map))

static int
codepoint_range_cmp(const void *a, const void *b)
{
	const pg_wchar *key = (const pg_wchar *) a;
	const pg_wchar *range = (const pg_wchar *) b;

	if (*key < range[0])
		return -1;				/* less than lower bound */
	if (*key > range[1])
		return 1;				/* greater than upper bound */

	return 0;					/* within range */
}

static bool
is_code_in_table(pg_wchar code, const pg_wchar *map, int mapsize)
{
	Assert(mapsize % 2 == 0);

	if (code < map[0] || code > map[mapsize - 1])
		return false;

	if (bsearch(&code, map, mapsize / 2, sizeof(pg_wchar) * 2,
				codepoint_range_cmp))
		return true;
	else
		return false;
}

/*
 * Calculate the length in characters of a null-terminated UTF-8 string.
 *
 * Returns -1 if the input is not valid UTF-8.
 */
static int
pg_utf8_string_len(const char *source)
{
	const unsigned char *p = (const unsigned char *) source;
	int			l;
	int			num_chars = 0;

	while (*p)
	{
		l = pg_utf_mblen(p);

		if (!pg_utf8_islegal(p, l))
			return -1;

		p += l;
		num_chars++;
	}

	return num_chars;
}


/*
 * pg_saslprep - Normalize a password with SASLprep.
 *
 * SASLprep requires the input to be in UTF-8 encoding, but PostgreSQL
 * supports many encodings, so we don't blindly assume that.  pg_saslprep
 * will check if the input looks like valid UTF-8, and returns
 * SASLPREP_INVALID_UTF8 if not.
 *
 * If the string contains prohibited characters (or more precisely, if the
 * output string would contain prohibited characters after normalization),
 * returns SASLPREP_PROHIBITED.
 *
 * On success, returns SASLPREP_SUCCESS, and the normalized string in
 * *output.
 *
 * In frontend, the normalized string is malloc'd, and the caller is
 * responsible for freeing it.  If an allocation fails, returns
 * SASLPREP_OOM.  In backend, the normalized string is palloc'd instead,
 * and a failed allocation leads to ereport(ERROR).
 */
pg_saslprep_rc
pg_saslprep(const char *input, char **output)
{
	pg_wchar   *input_chars = NULL;
	pg_wchar   *output_chars = NULL;
	int			input_size;
	char	   *result;
	int			result_size;
	int			count;
	int			i;
	bool		contains_RandALCat;
	unsigned char *p;
	pg_wchar   *wp;

	/* Ensure we return *output as NULL on failure */
	*output = NULL;

	/*
	 * Quick check if the input is pure ASCII.  An ASCII string requires no
	 * further processing.
	 */
	if (pg_is_ascii(input))
	{
		*output = STRDUP(input);
		if (!(*output))
			goto oom;
		return SASLPREP_SUCCESS;
	}

	/*
	 * Convert the input from UTF-8 to an array of Unicode codepoints.
	 *
	 * This also checks that the input is a legal UTF-8 string.
	 */
	input_size = pg_utf8_string_len(input);
	if (input_size < 0)
		return SASLPREP_INVALID_UTF8;

	input_chars = ALLOC((input_size + 1) * sizeof(pg_wchar));
	if (!input_chars)
		goto oom;

	p = (unsigned char *) input;
	for (i = 0; i < input_size; i++)
	{
		input_chars[i] = utf8_to_unicode(p);
		p += pg_utf_mblen(p);
	}
	input_chars[i] = (pg_wchar) '\0';

	/*
	 * The steps below correspond to the steps listed in [RFC3454], Section
	 * "2. Preparation Overview"
	 */

	/*
	 * 1) Map -- For each character in the input, check if it has a mapping
	 * and, if so, replace it with its mapping.
	 */
	count = 0;
	for (i = 0; i < input_size; i++)
	{
		pg_wchar	code = input_chars[i];

		if (IS_CODE_IN_TABLE(code, non_ascii_space_ranges))
			input_chars[count++] = 0x0020;
		else if (IS_CODE_IN_TABLE(code, commonly_mapped_to_nothing_ranges))
		{
			/* map to nothing */
		}
		else
			input_chars[count++] = code;
	}
	input_chars[count] = (pg_wchar) '\0';
	input_size = count;

	if (input_size == 0)
		goto prohibited;		/* don't allow empty password */

	/*
	 * 2) Normalize -- Normalize the result of step 1 using Unicode
	 * normalization.
	 */
	output_chars = unicode_normalize(UNICODE_NFKC, input_chars);
	if (!output_chars)
		goto oom;

	/*
	 * 3) Prohibit -- Check for any characters that are not allowed in the
	 * output.  If any are found, return an error.
	 */
	for (i = 0; i < input_size; i++)
	{
		pg_wchar	code = input_chars[i];

		if (IS_CODE_IN_TABLE(code, prohibited_output_ranges))
			goto prohibited;
		if (IS_CODE_IN_TABLE(code, unassigned_codepoint_ranges))
			goto prohibited;
	}

	/*
	 * 4) Check bidi -- Possibly check for right-to-left characters, and if
	 * any are found, make sure that the whole string satisfies the
	 * requirements for bidirectional strings.  If the string does not satisfy
	 * the requirements for bidirectional strings, return an error.
	 *
	 * [RFC3454], Section "6. Bidirectional Characters" explains in more
	 * detail what that means:
	 *
	 * "In any profile that specifies bidirectional character handling, all
	 * three of the following requirements MUST be met:
	 *
	 * 1) The characters in section 5.8 MUST be prohibited.
	 *
	 * 2) If a string contains any RandALCat character, the string MUST NOT
	 * contain any LCat character.
	 *
	 * 3) If a string contains any RandALCat character, a RandALCat character
	 * MUST be the first character of the string, and a RandALCat character
	 * MUST be the last character of the string."
	 */
	contains_RandALCat = false;
	for (i = 0; i < input_size; i++)
	{
		pg_wchar	code = input_chars[i];

		if (IS_CODE_IN_TABLE(code, RandALCat_codepoint_ranges))
		{
			contains_RandALCat = true;
			break;
		}
	}

	if (contains_RandALCat)
	{
		pg_wchar	first = input_chars[0];
		pg_wchar	last = input_chars[input_size - 1];

		for (i = 0; i < input_size; i++)
		{
			pg_wchar	code = input_chars[i];

			if (IS_CODE_IN_TABLE(code, LCat_codepoint_ranges))
				goto prohibited;
		}

		if (!IS_CODE_IN_TABLE(first, RandALCat_codepoint_ranges) ||
			!IS_CODE_IN_TABLE(last, RandALCat_codepoint_ranges))
			goto prohibited;
	}

	/*
	 * Finally, convert the result back to UTF-8.
	 */
	result_size = 0;
	for (wp = output_chars; *wp; wp++)
	{
		unsigned char buf[4];

		unicode_to_utf8(*wp, buf);
		result_size += pg_utf_mblen(buf);
	}

	result = ALLOC(result_size + 1);
	if (!result)
		goto oom;

	/*
	 * There are no error exits below here, so the error exit paths don't need
	 * to worry about possibly freeing "result".
	 */
	p = (unsigned char *) result;
	for (wp = output_chars; *wp; wp++)
	{
		unicode_to_utf8(*wp, p);
		p += pg_utf_mblen(p);
	}
	Assert((char *) p == result + result_size);
	*p = '\0';

	FREE(input_chars);
	FREE(output_chars);

	*output = result;
	return SASLPREP_SUCCESS;

prohibited:
	if (input_chars)
		FREE(input_chars);
	if (output_chars)
		FREE(output_chars);

	return SASLPREP_PROHIBITED;

oom:
	if (input_chars)
		FREE(input_chars);
	if (output_chars)
		FREE(output_chars);

	return SASLPREP_OOM;
}
