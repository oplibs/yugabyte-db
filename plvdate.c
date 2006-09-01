/*
  This code implements one part of functonality of 
  free available library PL/Vision. Please look www.quest.com

  This library isn't optimalized for big numbers, for working
  with n days (n > 10000), can be slow (on my P4 31ms).

  Original author: Steven Feuerstein, 1996 - 2002
  PostgreSQL implementation author: Pavel Stehule, 2006

  This module is under BSD Licence

  History:
    1.0. first public version 13. March 2006
*/

#define PLVDATE_VERSION  "PostgreSQL PLVdate, version 1.0, March 2006"

#include "postgres.h"
#include "utils/date.h"
#include "utils/builtins.h"
#include "utils/nabstime.h"
#include <sys/time.h>
#include <stdlib.h>


/*
 * External (defined in PgSQL datetime.c (timestamp utils))
 */

extern char *days[];

Datum plvdate_add_bizdays (PG_FUNCTION_ARGS);
Datum plvdate_nearest_bizday (PG_FUNCTION_ARGS);
Datum plvdate_next_bizday (PG_FUNCTION_ARGS);
Datum plvdate_bizdays_between (PG_FUNCTION_ARGS);
Datum plvdate_prev_bizday (PG_FUNCTION_ARGS);
Datum plvdate_isbizday (PG_FUNCTION_ARGS);

Datum plvdate_set_nonbizday_dow (PG_FUNCTION_ARGS);
Datum plvdate_unset_nonbizday_dow (PG_FUNCTION_ARGS);
Datum plvdate_set_nonbizday_day (PG_FUNCTION_ARGS);
Datum plvdate_unset_nonbizday_day (PG_FUNCTION_ARGS);

Datum plvdate_use_easter (PG_FUNCTION_ARGS);
Datum plvdate_using_easter (PG_FUNCTION_ARGS);
Datum plvdate_include_start (PG_FUNCTION_ARGS);
Datum plvdate_including_start (PG_FUNCTION_ARGS);

Datum plvdate_default_holydays (PG_FUNCTION_ARGS);

Datum plvdate_version (PG_FUNCTION_ARGS);

Datum plvdate_days_inmonth (PG_FUNCTION_ARGS);
Datum plvdate_isleapyear (PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(plvdate_add_bizdays);
PG_FUNCTION_INFO_V1(plvdate_nearest_bizday);
PG_FUNCTION_INFO_V1(plvdate_next_bizday);
PG_FUNCTION_INFO_V1(plvdate_bizdays_between);
PG_FUNCTION_INFO_V1(plvdate_prev_bizday);
PG_FUNCTION_INFO_V1(plvdate_isbizday);

PG_FUNCTION_INFO_V1(plvdate_set_nonbizday_dow);
PG_FUNCTION_INFO_V1(plvdate_unset_nonbizday_dow);
PG_FUNCTION_INFO_V1(plvdate_set_nonbizday_day);
PG_FUNCTION_INFO_V1(plvdate_unset_nonbizday_day);

PG_FUNCTION_INFO_V1(plvdate_use_easter);
PG_FUNCTION_INFO_V1(plvdate_using_easter);
PG_FUNCTION_INFO_V1(plvdate_include_start);
PG_FUNCTION_INFO_V1(plvdate_including_start);

PG_FUNCTION_INFO_V1(plvdate_default_holydays);

PG_FUNCTION_INFO_V1(plvdate_version);

PG_FUNCTION_INFO_V1(plvdate_days_inmonth);
PG_FUNCTION_INFO_V1(plvdate_isleapyear);


#define CHECK_SEQ_SEARCH(_l, _s) \
do { \
     if ((_l) < 0) {                                                 \
               ereport(ERROR,                                        \
                     (errcode(ERRCODE_INVALID_DATETIME_FORMAT),      \
                      errmsg("invalid value for %s", (_s))));        \
              }                                                      \
} while (0)

extern int ora_seq_search(char *name, char **array, int max);

#define SUNDAY     (1 << 0)
#define SATURDAY   (1 << 6)

static unsigned char nonbizdays = SUNDAY | SATURDAY; 
static bool use_easter = true;
static bool include_start = true;

#define MAX_HOLYDAYS   30
#define MAX_EXCEPTIONS 50

typedef struct {
	char day;
	char month;
} holyday_desc;

typedef struct {
	unsigned char nonbizdays;
	bool use_easter;
	holyday_desc *holydays;
	int holydays_c;
} cultural_info;

static holyday_desc holydays[MAX_HOLYDAYS];  /* sorted array */
static DateADT exceptions[MAX_EXCEPTIONS];   /* sorted array */

static int holydays_c = 0;
static int exceptions_c = 0;

static holyday_desc czech_holydays[] = {
	{1,1}, // Novy rok
	{1,5}, // Svatek prace
	{8,5}, // Den osvobozeni
	{5,7}, // Den slovanskych verozvestu
	{6,7}, // Den upaleni mistra Jana Husa
	{28,9}, // Den ceske statnosti
	{28,10}, // Den vzniku samostatneho ceskoslovenskeho statu
	{17,11}, // Den boje za svobodu a demokracii
	{24,12}, // Stedry den
	{25,12}, // 1. svatek vanocni
	{26,12}  // 2. svatek vanocni
};


static holyday_desc germany_holydays[] = {
	{1,1},{1,5},{25,5},{4,6},{5,6},
	{15,8},{3,10},{25,12},{26,12}
};

static holyday_desc poland_holydays[] = {
	{1,1},{1,5},{3,5},{15,6},{15,8},
	{1,11},{11,11},{25,12},{26,12}
};	

static holyday_desc austria_holydays[] = {
	{1,1},{6,1},{1,5},{25,5},{4,6},
	{5,6},{15,6},{15,8},{26,10},{1,11},
	{8,12},{25,12},{26,12}
};

static holyday_desc slovakia_holydays[] = {
	{1,1},{6,1},{1,5},{8,5},{5,7},
	{29,8},{1,9},{15,9},{1,11},{17,11},
	{24,12},{25,12},{26,12}
};

static holyday_desc russian_holydays[] = {
	{1,1},{2,1},{3,1},{4,1},{5,1},
	{7,1},{23,2},{8,3},{1,5},{9,5},
	{12,6}, {4,11}
};

static holyday_desc england_holydays[] = {
	{1,1},{2,1},{1,5},{29,5},{28,8},
	{25,12},{26,12}
};

static holyday_desc usa_holydays[] = {
	{1,1},{16,1},{20,2},{29,5},{4,7},
	{4,9},{9,10},{11,11},{23,11},{25,12}
};

cultural_info defaults_ci[] = {
	{SUNDAY | SATURDAY, true, czech_holydays, 11},
	{SUNDAY | SATURDAY, true, germany_holydays, 9},
	{SUNDAY | SATURDAY, true, poland_holydays, 9},
	{SUNDAY | SATURDAY, true, austria_holydays, 13},
	{SUNDAY | SATURDAY, true, slovakia_holydays, 13},
	{SUNDAY | SATURDAY, false, russian_holydays, 12},
	{SUNDAY | SATURDAY, true, england_holydays, 7},
	{SUNDAY | SATURDAY, false, usa_holydays, 10}
};

static char *states[] = {
	"Czech", "Germany", "Poland",
	"Austria", "Slovakia", "Russia",
	"Gb", "Usa",
	NULL,
};

static int 
dateadt_comp(const void* a, const void* b)
{
	DateADT *_a = (DateADT*)a;
	DateADT *_b = (DateADT*)b;

	return *_a - *_b;
}

static int 
holyday_desc_comp(const void* a, const void* b)
{
	int result;
	if (0 == (result = ((holyday_desc*)a)->month - ((holyday_desc*)b)->month)) 
		result = ((holyday_desc*)a)->day - ((holyday_desc*)b)->day;
		
	return result;
}


static void 
easter_sunday(int year, int* dd, int* mm)
{
	int b, d, e, q;

	if (year < 1900 || year > 2099)
		ereport(ERROR,
				(errcode(ERRCODE_DATETIME_VALUE_OUT_OF_RANGE),
				 errmsg("date is out of range"),
				 errdetail("Easter is defined only for years between 1900 and 2099")));
	
	b = 255 - 11 * (year % 19);
	d = ((b - 21) % 30) + 21;
	if (d > 38) d -= 1;
	e = (year + year/4 + d + 1) % 7;
	q = d + 7 - e;
	if (q < 32)
	{
		*dd = q; *mm = 3;
	}
	else
	{
		*dd = q - 31; *mm = 4;
	}
}

static Datum
ora_add_bizdays(DateADT day, int days)
{
	int d, dx;
	int y, m, auxd;
	holyday_desc hd;

	d = j2day(day+POSTGRES_EPOCH_JDATE);
	dx = days > 0? 1 : -1;
	
	while (days != 0)
	{
		d = (d+dx) % 7;
		d = (d < 0) ? 6:d;
		day += dx;
		if ((1 << d) & nonbizdays)
			continue;
		
		if (NULL != bsearch(&day, exceptions, exceptions_c, 
							sizeof(DateADT), dateadt_comp))
			continue;

		j2date(day + POSTGRES_EPOCH_JDATE, &y, &m, &auxd);
		hd.day = (char) auxd;
		hd.month = (char) m;

		if (use_easter && (m == 3 || m == 4))
		{
			easter_sunday(y, &auxd, &m);
			if (m == hd.month && (auxd == hd.day || d+1 == hd.day))
				continue;
		}
		if (NULL != bsearch(&hd, holydays, holydays_c, 
							sizeof(holyday_desc), holyday_desc_comp))
			continue;

		days -= dx;
	}

	return day;
}


static int
ora_diff_bizdays(DateADT day1, DateADT day2)
{
	int d, days;
	int y, m, auxd;
	holyday_desc hd;

	int cycle_c = 0;
	bool start_is_bizday = false;

	DateADT aux_day;
	if (day1 > day2)
	{
		aux_day = day1;
		day1 = day2; day2 = aux_day;
	}


	d = j2day(day1+POSTGRES_EPOCH_JDATE);
	days = 0;
	
	while (day1 <= day2)
	{
		++ cycle_c;
		d = (d+1) % 7;
		d = (d < 0) ? 6:d;
		day1 += 1;
		if ((1 << d) & nonbizdays)
			continue;
		
		if (NULL != bsearch(&day1, exceptions, exceptions_c, 
							sizeof(DateADT), dateadt_comp))
			continue;

		j2date(day1 + POSTGRES_EPOCH_JDATE, &y, &m, &auxd);
		hd.day = (char) auxd;
		hd.month = (char) m;

		if (use_easter && (m == 3 || m == 4))
		{
			easter_sunday(y, &auxd, &m);
			if (m == hd.month && (auxd == hd.day || d+1 == hd.day))
				continue;
		}
		if (NULL != bsearch(&hd, holydays, holydays_c, 
							sizeof(holyday_desc), holyday_desc_comp))
			continue;

		days += 1;
		if (cycle_c == 1)
			start_is_bizday = true;
	}
	if (include_start && start_is_bizday && days >= 1)
		days -= 1;

	return days;
}


/****************************************************************
 * PLVdate.add_bizdays
 *
 * Syntax:
 *   FUNCTION add_bizdays(IN dt DATE, IN days int) RETURNS DATE; 
 *
 * Purpouse:
 *   Get the date created by adding <n> business days to a date
 *
 ****************************************************************/


Datum 
plvdate_add_bizdays (PG_FUNCTION_ARGS)
{
	DateADT day = PG_GETARG_DATEADT(0);
	int days = PG_GETARG_INT32(1);

	PG_RETURN_DATEADT(ora_add_bizdays(day,days));
}


/****************************************************************
 * PLVdate.nearest_bizday
 *
 * Syntax:
 *   FUNCTION nearest_bizday(IN dt DATE) RETURNS DATE;
 *
 * Purpouse:
 *   Get the nearest business date to a given date, user defined
 *
 ****************************************************************/

Datum 
plvdate_nearest_bizday (PG_FUNCTION_ARGS)
{
	DateADT dt = PG_GETARG_DATEADT(0);
	DateADT d1, d2, res;

	d1 = ora_add_bizdays(dt, -1);
	d2 = ora_add_bizdays(dt, 1);

	if ((dt - d1) > (d2 - dt))
		res = d2;
	else
		res = d1;

	PG_RETURN_DATEADT(res);
}


/****************************************************************
 * PLVdate.next_bizday
 *
 * Syntax:
 *   FUNCTION next_bizday(IN dt DATE) RETURNS DATE;
 *
 * Purpouse:
 *   Get the next business date from a given date, user defined
 *
 ****************************************************************/

Datum 
plvdate_next_bizday (PG_FUNCTION_ARGS)
{
	DateADT day = PG_GETARG_DATEADT(0);

	PG_RETURN_DATEADT(ora_add_bizdays(day,1));
}


/****************************************************************
 * PLVdate.bizdays_between
 *
 * Syntax:
 *   FUNCTION bizdays_between(IN dt1 DATE, IN dt2 DATE)
 *     RETURNS int; 
 *
 * Purpouse:
 *   Get the number of business days between two dates
 *
 ****************************************************************/

Datum 
plvdate_bizdays_between (PG_FUNCTION_ARGS)
{
	DateADT day1 = PG_GETARG_DATEADT(0);
	DateADT day2 = PG_GETARG_DATEADT(1);

	PG_RETURN_INT32(ora_diff_bizdays(day1,day2));
}


/****************************************************************
 * PLVdate.prev_bizday
 *
 * Syntax:
 *   FUNCTION prev_bizday(IN dt DATE) RETURNS date;
 *
 * Purpouse:
 *   Get the previous business date from a given date, user 
 * defined
 *
 ****************************************************************/

Datum 
plvdate_prev_bizday (PG_FUNCTION_ARGS)
{
	DateADT day = PG_GETARG_DATEADT(0);

	PG_RETURN_DATEADT(ora_add_bizdays(day,-1));
}


/****************************************************************
 * PLVdate.isbizday
 *
 * Syntax:
 *   FUNCTION isbizday(IN dt DATE) RETURNS bool;
 *
 * Purpouse:
 *   Call this function to determine if a date is a business day
 *
 ****************************************************************/

Datum 
plvdate_isbizday (PG_FUNCTION_ARGS)
{
	DateADT day = PG_GETARG_DATEADT(0);
	int y, m, d;
	holyday_desc hd;
	
	if (0 != ((1 << j2day(day+POSTGRES_EPOCH_JDATE)) & nonbizdays))
		return false;

	if (NULL != bsearch(&day, exceptions, exceptions_c, 
						sizeof(DateADT), dateadt_comp))
		return false;

	j2date(day + POSTGRES_EPOCH_JDATE, &y, &m, &d);
	hd.month = m; hd.day = d;

 	if (use_easter && (m == 3 || m == 4))
	{
		easter_sunday(y, &d, &m);
		if (m == hd.month && (d == hd.day || d+1 == hd.day))
			return false;
	}

	PG_RETURN_BOOL (NULL == bsearch(&hd, holydays, holydays_c, 
									sizeof(holyday_desc), holyday_desc_comp));
}


/****************************************************************
 * PLVdate.set_nonbizday
 *
 * Syntax:
 *   FUNCTION set_nonbizday(IN dow VARCHAR) RETURNS void;
 *
 * Purpouse:
 *   Set day of week as non bussines day
 *
 ****************************************************************/

Datum 
plvdate_set_nonbizday_dow (PG_FUNCTION_ARGS)
{
	unsigned char check;

	text *day_txt = PG_GETARG_TEXT_P(0);
	
	int d = ora_seq_search(VARDATA(day_txt), days, VARATT_SIZEP(day_txt) - VARHDRSZ);
	CHECK_SEQ_SEARCH(d, "DAY/Day/day");

	check = nonbizdays | (1 << d);
	if (check == 0x7f)
                ereport(ERROR,
                        (errcode(ERRCODE_DATA_EXCEPTION),
                         errmsg("nonbizday registeration error"),
                         errdetail("Constraint violation."),
                         errhint("One day in week have to be bizday.")));
    
	nonbizdays = nonbizdays | (1 << d);

	PG_RETURN_VOID();
}

/****************************************************************
 * PLVdate.unset_nonbizday
 *
 * Syntax:
 *   FUNCTION unset_nonbizday(IN dow VARCHAR) RETURNS void;
 *
 * Purpouse:
 *   Unset day of week as non bussines day
 *
 ****************************************************************/

Datum 
plvdate_unset_nonbizday_dow (PG_FUNCTION_ARGS)
{
	text *day_txt = PG_GETARG_TEXT_P(0);
	
	int d = ora_seq_search(VARDATA(day_txt), days, VARATT_SIZEP(day_txt) - VARHDRSZ);
	CHECK_SEQ_SEARCH(d, "DAY/Day/day");
    
	nonbizdays = (nonbizdays | (1 << d)) ^ (1 << d);

	PG_RETURN_VOID();
}

/****************************************************************
 * PLVdate.set_nonbizday
 *
 * Syntax:
 *   FUNCTION set_nonbizday(IN day DATE) RETURNS void;
 *   FUNCTION set_nonbizday(IN day DATE, IN repeat := false BOOL) RETURNS void;
 *
 * Purpouse:
 *   Set day as non bussines day, second arg specify year's 
 * periodicity
 *
 ****************************************************************/

Datum 
plvdate_set_nonbizday_day (PG_FUNCTION_ARGS)
{
	DateADT arg1 = PG_GETARG_DATEADT(0);
	bool arg2 = PG_GETARG_BOOL(1);
	int y, m, d;
	holyday_desc hd;
    
	if (arg2)
	{
		if (holydays_c == MAX_HOLYDAYS)
                        ereport(ERROR,
                                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                                 errmsg("nonbizday registeration error"),
                                 errdetail("Too much registered nonbizdays."),
                                 errhint("Increase MAX_HOLYDAYS in 'plvdate.c'.")));

		j2date(arg1 + POSTGRES_EPOCH_JDATE, &y, &m, &d);
		hd.month = m; hd.day = d;

		if (NULL != bsearch(&hd, holydays, holydays_c, sizeof(holyday_desc), holyday_desc_comp))
                        ereport(ERROR,
                                (errcode(ERRCODE_DUPLICATE_OBJECT),
                                 errmsg("nonbizday registeration error"),
                                 errdetail("Date is registered.")));

		holydays[holydays_c].month = m;
		holydays[holydays_c].day = d;
		holydays_c += 1;

		qsort(holydays, holydays_c, sizeof(holyday_desc), holyday_desc_comp);   
	}
	else
	{
		if (exceptions_c == MAX_EXCEPTIONS)
                        ereport(ERROR,
                                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                                 errmsg("nonbizday registeration error"),
                                 errdetail("Too much registered nonrepeated nonbizdays."),
                                 errhint("Increase MAX_EXCEPTIONS in 'plvdate.c'.")));

		if (NULL != bsearch(&arg1, exceptions, exceptions_c, sizeof(DateADT), dateadt_comp))
                        ereport(ERROR,
                                (errcode(ERRCODE_DUPLICATE_OBJECT),
                                 errmsg("nonbizday registeration error"),
                                 errdetail("Date is registered.")));
		
		exceptions[exceptions_c++] = arg1;
		qsort(exceptions, exceptions_c, sizeof(DateADT), dateadt_comp);
	}
    
	PG_RETURN_VOID();
}

/****************************************************************
 * PLVdate.unset_nonbizday
 *
 * Syntax:
 *   FUNCTION unset_nonbizday(IN day DATE) RETURNS void;
 *   FUNCTION unset_nonbizday(IN day DATE, IN repeat := false BOOL) RETURNS void;
 *
 * Purpouse:
 *   Unset day as non bussines day, second arg specify year's 
 * periodicity
 *
 ****************************************************************/
	 
Datum 
plvdate_unset_nonbizday_day (PG_FUNCTION_ARGS)
{
	DateADT arg1 = PG_GETARG_DATEADT(0);
	bool arg2 = PG_GETARG_BOOL(1);
	int y, m, d;
	bool found = false;
	int i;
   
	if (arg2)
	{
		j2date(arg1 + POSTGRES_EPOCH_JDATE, &y, &m, &d);
		for (i = 0; i < holydays_c; i++)
		{
			if (!found && holydays[i].month == m && holydays[i].day == d)
				found = true;
			else if (found)
			{
				holydays[i-1].month = holydays[i].month;
				holydays[i-1].day = holydays[i].day;
			}
		}
		if (found)
			holydays_c -= 1;
	}
	else
	{
		for(i = 0; i < exceptions_c; i++)
			if (!found && exceptions[i] == arg1)
				found = true;
			else if (found)
				exceptions[i-1] = exceptions[i];
		if (found)
			exceptions_c -= 1;
	}
	if (!found)
                ereport(ERROR,
                        (errcode(ERRCODE_UNDEFINED_OBJECT),
                         errmsg("nonbizday unregisteration error"),
                         errdetail("Nonbizday not found.")));

	PG_RETURN_VOID();
}


/****************************************************************
 * PLVdate.use_easter
 *
 * Syntax:
 *   FUNCTION unuse_easter() RETURNS void;
 *   FUNCTION use_easter() RETURNS void;
 *   FUNCTION use_easter(IN bool) RETURNS void
 *
 * Purpouse:
 *   Have to use easter as nonbizday?
 *
 ****************************************************************/

Datum 
plvdate_use_easter (PG_FUNCTION_ARGS)
{
	use_easter = PG_GETARG_BOOL(0);

	PG_RETURN_VOID();
}


/****************************************************************
 * PLVdate.using_easter
 *
 * Syntax:
 *   FUNCTION using_easter() RETURNS bool
 *
 * Purpouse:
 *   Use it easter as nonbizday?
 *
 ****************************************************************/

Datum
plvdate_using_easter (PG_FUNCTION_ARGS)
{
	PG_RETURN_BOOL(use_easter);
}


/****************************************************************
 * PLVdate.include_start
 *
 * Syntax:
 *   FUNCTION include_start() RETURNS void;
 *   FUNCTION noinclude_start() RETURNS void;
 *   FUNCTION include_start(IN bool) RETURNS void
 *
 * Purpouse:
 *   Have to include current day in bizdays_between calculation?
 *
 ****************************************************************/

Datum 
plvdate_include_start (PG_FUNCTION_ARGS)
{
	include_start = PG_GETARG_BOOL(0);

	PG_RETURN_VOID();
}


/****************************************************************
 * PLVdate.including_start
 *
 * Syntax:
 *   FUNCTION including_start() RETURNS bool
 *
 * Purpouse:
 *   include current day in bizdays_between calculation?
 *
 ****************************************************************/

Datum
plvdate_including_start (PG_FUNCTION_ARGS)
{
	PG_RETURN_BOOL(include_start);
}


/*
 * Load some national configurations
 * 
 */

Datum
plvdate_default_holydays (PG_FUNCTION_ARGS)
{
    text *country = PG_GETARG_TEXT_P(0);
	
	int c = ora_seq_search(VARDATA(country), states, VARATT_SIZEP(country) - VARHDRSZ);
	CHECK_SEQ_SEARCH(c, "STATE/State/state");

	nonbizdays = defaults_ci[c].nonbizdays;
	use_easter = defaults_ci[c].use_easter;
	exceptions_c = 0;

	holydays_c = defaults_ci[c].holydays_c;
	memcpy(holydays, defaults_ci[c].holydays, holydays_c*sizeof(holyday_desc));

	PG_RETURN_VOID();
}

/*
 * helper maintaince functions
 */

Datum
plvdate_version (PG_FUNCTION_ARGS)
{
	PG_RETURN_CSTRING(PLVDATE_VERSION);
}


/****************************************************************
 * PLVdate.days_inmonth
 *
 * Syntax:
 *   FUNCTION days_inmonth(date) RETURNS integer
 *
 * Purpouse:
 *   Returns month's length
 *
 ****************************************************************/

Datum 
plvdate_days_inmonth(PG_FUNCTION_ARGS)
{
    DateADT day = PG_GETARG_DATEADT(0);
    int result;
    int y, m, d;
    
    j2date(day + POSTGRES_EPOCH_JDATE, &y, &m, &d);
    
    result = date2j(y, m+1, 1) - date2j(y, m, 1);
                    
    PG_RETURN_INT32(result);
}


/****************************************************************
 * PLVdate.isleapyear
 *
 * Syntax:
 *   FUNCTION isleapyear() RETURNS bool
 *
 * Purpouse:
 *   Returns true, if year is leap
 *
 ****************************************************************/

Datum 
plvdate_isleapyear(PG_FUNCTION_ARGS)
{
	DateADT day = PG_GETARG_DATEADT(0);
	int y, m, d;
	bool result;

	j2date(day + POSTGRES_EPOCH_JDATE, &y, &m, &d);
	result = ((( y % 4) == 0) && ((y % 100) != 0)) || ((y / 400) == 0);
	
	PG_RETURN_BOOL(result);
}
