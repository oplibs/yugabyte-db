/*
 * pgbench.c
 *
 * A simple benchmark program for PostgreSQL
 * Originally written by Tatsuo Ishii and enhanced by many contributors.
 *
 * src/bin/pgbench/pgbench.c
 * Copyright (c) 2000-2018, PostgreSQL Global Development Group
 * ALL RIGHTS RESERVED;
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without a written agreement
 * is hereby granted, provided that the above copyright notice and this
 * paragraph and the following two paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL THE AUTHOR OR DISTRIBUTORS BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
 * LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS
 * DOCUMENTATION, EVEN IF THE AUTHOR OR DISTRIBUTORS HAVE BEEN ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * THE AUTHOR AND DISTRIBUTORS SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE AUTHOR AND DISTRIBUTORS HAS NO OBLIGATIONS TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 */

#ifdef WIN32
#define FD_SETSIZE 1024			/* set before winsock2.h is included */
#endif							/* ! WIN32 */

#include "postgres_fe.h"
#include "fe_utils/conditional.h"

#include "getopt_long.h"
#include "libpq-fe.h"
#include "portability/instr_time.h"

#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>		/* for getrlimit */
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "pgbench.h"

#define ERRCODE_IN_FAILED_SQL_TRANSACTION  "25P02"
#define ERRCODE_T_R_SERIALIZATION_FAILURE  "40001"
#define ERRCODE_T_R_DEADLOCK_DETECTED  "40P01"
#define ERRCODE_UNDEFINED_TABLE  "42P01"

/*
 * Hashing constants
 */
#define FNV_PRIME			UINT64CONST(0x100000001b3)
#define FNV_OFFSET_BASIS	UINT64CONST(0xcbf29ce484222325)
#define MM2_MUL				UINT64CONST(0xc6a4a7935bd1e995)
#define MM2_MUL_TIMES_8		UINT64CONST(0x35253c9ade8f4ca8)
#define MM2_ROT				47

/*
 * Multi-platform pthread implementations
 */

#ifdef WIN32
/* Use native win32 threads on Windows */
typedef struct win32_pthread *pthread_t;
typedef int pthread_attr_t;

static int	pthread_create(pthread_t *thread, pthread_attr_t *attr, void *(*start_routine) (void *), void *arg);
static int	pthread_join(pthread_t th, void **thread_return);
#elif defined(ENABLE_THREAD_SAFETY)
/* Use platform-dependent pthread capability */
#include <pthread.h>
#else
/* No threads implementation, use none (-j 1) */
#define pthread_t void *
#endif


/********************************************************************
 * some configurable parameters */

/* max number of clients allowed */
#ifdef FD_SETSIZE
#define MAXCLIENTS	(FD_SETSIZE - 10)
#else
#define MAXCLIENTS	1024
#endif

#define DEFAULT_INIT_STEPS "dtgp"	/* default -I setting */

#define LOG_STEP_SECONDS	5	/* seconds between log messages */
#define DEFAULT_NXACTS	10		/* default nxacts */

#define ZIPF_CACHE_SIZE	15		/* cache cells number */

#define MIN_GAUSSIAN_PARAM		2.0 /* minimum parameter for gauss */
#define MAX_ZIPFIAN_PARAM		1000	/* maximum parameter for zipfian */

int			nxacts = 0;			/* number of transactions per client */
int			duration = 0;		/* duration in seconds */
int64		end_time = 0;		/* when to stop in micro seconds, under -T */

/*
 * scaling factor. for example, scale = 10 will make 1000000 tuples in
 * ysql_bench_accounts table.
 */
int			scale = 1;

/*
 * fillfactor. for example, fillfactor = 90 will use only 90 percent
 * space during inserts and leave 10 percent free.
 */
int			fillfactor = 100;
bool			set_fillfactor = false;

/*
 * use unlogged tables?
 */
bool		unlogged_tables = false;

/*
 * log sampling rate (1.0 = log everything, 0.0 = option not given)
 */
double		sample_rate = 0.0;

/*
 * When threads are throttled to a given rate limit, this is the target delay
 * to reach that rate in usec.  0 is the default and means no throttling.
 */
int64		throttle_delay = 0;

/*
 * Transactions which take longer than this limit (in usec) are counted as
 * late, and reported as such, although they are completed anyway. When
 * throttling is enabled, execution time slots that are more than this late
 * are skipped altogether, and counted separately.
 */
int64		latency_limit = 0;

/*
 * tablespace selection
 */
char	   *tablespace = NULL;
char	   *index_tablespace = NULL;

/* random seed used to initialize base_random_sequence */
int64		random_seed = -1;

/*
 * end of configurable parameters
 *********************************************************************/

#define nbranches	1			/* Makes little sense to change this.  Change
								 * -s instead */
#define ntellers	10
#define naccounts	100000

/*
 * The scale factor at/beyond which 32bit integers are incapable of storing
 * 64bit values.
 *
 * Although the actual threshold is 21474, we use 20000 because it is easier to
 * document and remember, and isn't that far away from the real threshold.
 */
#define SCALE_32BIT_THRESHOLD 20000

bool		use_log;			/* log transaction latencies to a file */
bool		use_quiet;			/* quiet logging onto stderr */
int			agg_interval;		/* log aggregates instead of individual
								 * transactions */
bool		per_script_stats = false;	/* whether to collect stats per script */
int			progress = 0;		/* thread progress report every this seconds */
bool		progress_timestamp = false; /* progress report with Unix time */
int			nclients = 1;		/* number of clients */
int			nthreads = 1;		/* number of threads */
bool		is_connect;			/* establish connection for each transaction */
bool		report_per_command = false;	/* report per-command latencies, retries
										 * after the failures and errors
										 * (failures without retrying) */
int			main_pid;			/* main process id used in log filename */

/*
 * There're different types of restrictions for deciding that the current failed
 * transaction can no longer be retried and should be reported as failed:
 * - max_tries can be used to limit the number of tries;
 * - latency_limit can be used to limit the total time of tries.
 *
 * They can be combined together, and you need to use at least one of them to
 * retry the failed transactions. By default, failed transactions are not
 * retried at all.
 */
uint32		max_tries = 0;		/* we cannot retry a failed transaction if its
								 * number of tries reaches this maximum; if its
								 * value is zero, it is not used */

char	   *pghost = "";
char	   *pgport = "";
char	   *login = NULL;
char	   *dbName;
char	   *logfile_prefix = NULL;
const char *progname;

#define WSEP '@'				/* weight separator */

volatile bool timer_exceeded = false;	/* flag from signal handler */

/*
 * Variable definitions.
 *
 * If a variable only has a string value, "svalue" is that value, and value is
 * "not set".  If the value is known, "value" contains the value (in any
 * variant).
 *
 * In this case "svalue" contains the string equivalent of the value, if we've
 * had occasion to compute that, or NULL if we haven't.
 */
typedef struct
{
	char	   *name;			/* variable's name */
	char	   *svalue;			/* its value in string form, if known */
	PgBenchValue value;			/* actual variable's value */
} Variable;

#define MAX_SCRIPTS		128		/* max number of SQL scripts allowed */
#define SHELL_COMMAND_SIZE	256 /* maximum size allowed for shell command */

/*
 * Simple data structure to keep stats about something.
 *
 * XXX probably the first value should be kept and used as an offset for
 * better numerical stability...
 */
typedef struct SimpleStats
{
	int64		count;			/* how many values were encountered */
	double		min;			/* the minimum seen */
	double		max;			/* the maximum seen */
	double		sum;			/* sum of values */
	double		sum2;			/* sum of squared values */
} SimpleStats;

/*
 * Data structure to hold various statistics: per-thread and per-script stats
 * are maintained and merged together.
 */
typedef struct StatsData
{
	time_t		start_time;		/* interval start time, for aggregates */
	int64		cnt;			/* number of sucessfull transactions, including
								 * skipped */
	int64		skipped;		/* number of transactions skipped under --rate
								 * and --latency-limit */
	int64		retries;
	int64		retried;		/* number of transactions that were retried
								 * after a serialization or a deadlock
								 * failure */
	int64		errors;			/* number of transactions that were not retried
								 * after a serialization or a deadlock
								 * failure or had another error (including meta
								 * commands errors) */
	int64		errors_in_failed_tx;	/* number of transactions that failed in
										 * a error
										 * ERRCODE_IN_FAILED_SQL_TRANSACTION */
	SimpleStats latency;
	SimpleStats lag;
} StatsData;

/* Various random sequences are initialized from this one. */
static unsigned short base_random_sequence[3];

/*
 * Data structure for client variables.
 */
typedef struct Variables
{
	Variable   *array;			/* array of variable definitions */
	int			nvariables;		/* number of variables */
	bool		vars_sorted;	/* are variables sorted by name? */
} Variables;

/*
 * Data structure for thread/client random seed.
 */
typedef struct RandomState
{
	unsigned short data[3];
} RandomState;

/*
 * Data structure for repeating a transaction from the beginnning with the same
 * parameters.
 */
typedef struct RetryState
{
	RandomState random_state;	/* random seed */
	Variables   variables;		/* client variables */
} RetryState;

/*
 * For the failures during script execution.
 */
typedef enum FailureStatus
{
	NO_FAILURE = 0,
	ANOTHER_FAILURE,			/* other failures that are not listed by
								 * themselves below */
	SERIALIZATION_FAILURE,
	DEADLOCK_FAILURE,
	IN_FAILED_SQL_TRANSACTION
} FailureStatus;

typedef struct Failure
{
	FailureStatus status;		/* type of the failure */
	int			command;		/* command number in script where the failure
								 * occurred */
} Failure;

/*
 * Connection state machine states.
 */
typedef enum
{
	/*
	 * The client must first choose a script to execute.  Once chosen, it can
	 * either be throttled (state CSTATE_START_THROTTLE under --rate) or start
	 * right away (state CSTATE_START_TX).
	 */
	CSTATE_CHOOSE_SCRIPT,

	/*
	 * In CSTATE_START_THROTTLE state, we calculate when to begin the next
	 * transaction, and advance to CSTATE_THROTTLE.  CSTATE_THROTTLE state
	 * sleeps until that moment.  (If throttling is not enabled, doCustom()
	 * falls directly through from CSTATE_START_THROTTLE to CSTATE_START_TX.)
	 */
	CSTATE_START_THROTTLE,
	CSTATE_THROTTLE,

	/*
	 * CSTATE_START_TX performs start-of-transaction processing.  Establishes
	 * a new connection for the transaction, in --connect mode, and records
	 * the transaction start time.
	 */
	CSTATE_START_TX,

	/*
	 * We loop through these states, to process each command in the script:
	 *
	 * CSTATE_START_COMMAND starts the execution of a command.  On a SQL
	 * command, the command is sent to the server, and we move to
	 * CSTATE_WAIT_RESULT state.  On a \sleep meta-command, the timer is set,
	 * and we enter the CSTATE_SLEEP state to wait for it to expire. Other
	 * meta-commands are executed immediately.
	 *
	 * CSTATE_SKIP_COMMAND for conditional branches which are not executed,
	 * quickly skip commands that do not need any evaluation.
	 *
	 * CSTATE_WAIT_RESULT waits until we get a result set back from the server
	 * for the current command.
	 *
	 * CSTATE_SLEEP waits until the end of \sleep.
	 *
	 * CSTATE_END_COMMAND records the end-of-command timestamp, increments the
	 * command counter, and loops back to CSTATE_START_COMMAND state.
	 */
	CSTATE_START_COMMAND,
	CSTATE_SKIP_COMMAND,
	CSTATE_WAIT_RESULT,
	CSTATE_SLEEP,
	CSTATE_END_COMMAND,

	/*
	 * States for transactions with serialization or deadlock failures.
	 *
	 * First, remember the failure in CSTATE_FAILURE. Then process other
	 * commands of the failed transaction if any and go to CSTATE_RETRY. If we
	 * can re-execute the transaction from the very beginning, report this as a
	 * failure, set the same parameters for the transaction execution as in the
	 * previous tries and process the first transaction command in
	 * CSTATE_START_COMMAND. Otherwise, report this as an error, set the
	 * parameters for the transaction execution as they were before the first
	 * run of this transaction (except for a random state) and go to
	 * CSTATE_END_TX to complete this transaction.
	 */
	CSTATE_FAILURE,
	CSTATE_RETRY,

	/*
	 * CSTATE_END_TX performs end-of-transaction processing.  Calculates
	 * latency, and logs the transaction.  In --connect mode, closes the
	 * current connection.  Chooses the next script to execute and starts over
	 * in CSTATE_START_THROTTLE state, or enters CSTATE_FINISHED if we have no
	 * more work to do.
	 */
	CSTATE_END_TX,

	/*
	 * Final states.  CSTATE_ABORTED means that the script execution was
	 * aborted because a command failed, CSTATE_FINISHED means success.
	 */
	CSTATE_ABORTED,
	CSTATE_FINISHED
} ConnectionStateEnum;

/*
 * Connection state.
 */
typedef struct
{
	PGconn	   *con;			/* connection handle to DB */
	int			id;				/* client No. */
	ConnectionStateEnum state;	/* state machine's current state. */
	ConditionalStack cstack;	/* enclosing conditionals state */
	RandomState random_state;	/* separate randomness for each client */

	int			use_file;		/* index in sql_script for this client */
	int			command;		/* command number in script */

	/* client variables */
	Variables   variables;

	/* various times about current transaction */
	int64		txn_scheduled;	/* scheduled start time of transaction (usec) */
	int64		sleep_until;	/* scheduled start time of next cmd (usec) */
	instr_time	txn_begin;		/* used for measuring schedule lag times */
	instr_time	stmt_begin;		/* used for measuring statement latencies */

	bool		prepared[MAX_SCRIPTS];	/* whether client prepared the script */

	/*
	 * For processing errors and repeating transactions with serialization or
	 * deadlock failures:
	 */
	Failure		first_failure;	/* status and command number of the first
								 * failure in the current transaction execution;
								 * status NO_FAILURE if there were no failures
								 * or errors */
	RetryState  retry_state;
	uint32			retries;	/* how many times have we already retried the
								 * current transaction? */

	/* per client collected stats */
	int64		cnt;			/* client transaction count, for -t */
	int			ecnt;			/* error count */
} CState;

/*
 * Cache cell for random_zipfian call
 */
typedef struct
{
	/* cell keys */
	double		s;				/* s - parameter of random_zipfian function */
	int64		n;				/* number of elements in range (max - min + 1) */

	double		harmonicn;		/* generalizedHarmonicNumber(n, s) */
	double		alpha;
	double		beta;
	double		eta;

	uint64		last_used;		/* last used logical time */
} ZipfCell;

/*
 * Zipf cache for zeta values
 */
typedef struct
{
	uint64		current;		/* counter for LRU cache replacement algorithm */

	int			nb_cells;		/* number of filled cells */
	int			overflowCount;	/* number of cache overflows */
	ZipfCell	cells[ZIPF_CACHE_SIZE];
} ZipfCache;

/*
 * Thread state
 */
typedef struct
{
	int			tid;			/* thread id */
	pthread_t	thread;			/* thread handle */
	CState	   *state;			/* array of CState */
	int			nstate;			/* length of state[] */
	RandomState random_state; 	/* separate randomness for each thread */
	int64		throttle_trigger;	/* previous/next throttling (us) */
	FILE	   *logfile;		/* where to log, or NULL */
	ZipfCache	zipf_cache;		/* for thread-safe  zipfian random number
								 * generation */

	/* per thread collected stats */
	instr_time	start_time;		/* thread start time */
	instr_time	conn_time;
	StatsData	stats;
	int64		latency_late;	/* executed but late transactions (including
								 * errors) */
} TState;

#define INVALID_THREAD		((pthread_t) 0)

/*
 * queries read from files
 */
#define SQL_COMMAND		1
#define META_COMMAND	2
#define MAX_ARGS		10

typedef enum MetaCommand
{
	META_NONE,					/* not a known meta-command */
	META_SET,					/* \set */
	META_SETSHELL,				/* \setshell */
	META_SHELL,					/* \shell */
	META_SLEEP,					/* \sleep */
	META_IF,					/* \if */
	META_ELIF,					/* \elif */
	META_ELSE,					/* \else */
	META_ENDIF					/* \endif */
} MetaCommand;

typedef enum QueryMode
{
	QUERY_SIMPLE,				/* simple query */
	QUERY_EXTENDED,				/* extended query */
	QUERY_PREPARED,				/* extended query with prepared statements */
	NUM_QUERYMODE
} QueryMode;

static QueryMode querymode = QUERY_SIMPLE;
static const char *QUERYMODE[] = {"simple", "extended", "prepared"};

typedef struct
{
	char	   *line;			/* text of command line */
	int			command_num;	/* unique index of this Command struct */
	int			type;			/* command type (SQL_COMMAND or META_COMMAND) */
	MetaCommand meta;			/* meta command identifier, or META_NONE */
	int			argc;			/* number of command words */
	char	   *argv[MAX_ARGS]; /* command word list */
	PgBenchExpr *expr;			/* parsed expression, if needed */
	SimpleStats stats;			/* time spent in this command */
	int64		retries;
	int64		errors;			/* number of failures that were not retried */
	int64		errors_in_failed_tx;	/* number of errors
										 * ERRCODE_IN_FAILED_SQL_TRANSACTION */
} Command;

typedef struct ParsedScript
{
	const char *desc;			/* script descriptor (eg, file name) */
	int			weight;			/* selection weight */
	Command   **commands;		/* NULL-terminated array of Commands */
	StatsData	stats;			/* total time spent in script */
} ParsedScript;

static ParsedScript sql_script[MAX_SCRIPTS];	/* SQL script files */
static int	num_scripts;		/* number of scripts in sql_script[] */
static int	num_commands = 0;	/* total number of Command structs */
static int64 total_weight = 0;

typedef enum DebugLevel
{
	NO_DEBUG = 0,				/* no debugging output (except PGBENCH_DEBUG) */
	DEBUG_FAILS,				/* print only failure messages, errors and
								 * retries */
	DEBUG_ALL,					/* print all debugging output (throttling,
								 * executed/sent/received commands etc.) */
	NUM_DEBUGLEVEL
} DebugLevel;

static DebugLevel debug_level = NO_DEBUG;	/* debug flag */
static const char *DEBUGLEVEL[] = {"no", "fails", "all"};

/* Builtin test scripts */
typedef struct BuiltinScript
{
	const char *name;			/* very short name for -b ... */
	const char *desc;			/* short description */
	const char *script;			/* actual pgbench script */
} BuiltinScript;

static const BuiltinScript builtin_script[] =
{
	{
		"tpcb-like",
		"<builtin: TPC-B (sort of)>",
		"\\set aid random(1, " CppAsString2(naccounts) " * :scale)\n"
		"\\set bid random(1, " CppAsString2(nbranches) " * :scale)\n"
		"\\set tid random(1, " CppAsString2(ntellers) " * :scale)\n"
		"\\set delta random(-5000, 5000)\n"
		"BEGIN;\n"
		"UPDATE ysql_bench_accounts SET abalance = abalance + :delta WHERE aid = :aid;\n"
		"SELECT abalance FROM ysql_bench_accounts WHERE aid = :aid;\n"
		"UPDATE ysql_bench_tellers SET tbalance = tbalance + :delta WHERE tid = :tid;\n"
		"UPDATE ysql_bench_branches SET bbalance = bbalance + :delta WHERE bid = :bid;\n"
		"INSERT INTO ysql_bench_history (tid, bid, aid, delta, mtime) VALUES (:tid, :bid, :aid, :delta, CURRENT_TIMESTAMP);\n"
		"END;\n"
	},
	{
		"simple-update",
		"<builtin: simple update>",
		"\\set aid random(1, " CppAsString2(naccounts) " * :scale)\n"
		"\\set bid random(1, " CppAsString2(nbranches) " * :scale)\n"
		"\\set tid random(1, " CppAsString2(ntellers) " * :scale)\n"
		"\\set delta random(-5000, 5000)\n"
		"BEGIN;\n"
		"UPDATE ysql_bench_accounts SET abalance = abalance + :delta WHERE aid = :aid;\n"
		"SELECT abalance FROM ysql_bench_accounts WHERE aid = :aid;\n"
		"INSERT INTO ysql_bench_history (tid, bid, aid, delta, mtime) VALUES (:tid, :bid, :aid, :delta, CURRENT_TIMESTAMP);\n"
		"END;\n"
	},
	{
		"select-only",
		"<builtin: select only>",
		"\\set aid random(1, " CppAsString2(naccounts) " * :scale)\n"
		"SELECT abalance FROM ysql_bench_accounts WHERE aid = :aid;\n"
	}
};

typedef enum ErrorLevel
{
	/*
	 * To report throttling, executed/sent/received commands etc.
	 */
	ELEVEL_DEBUG,

	/*
	 * Normal failure of the SQL/meta command, or processing of the failed
	 * transaction (its end/retry).
	 */
	ELEVEL_LOG_CLIENT_FAIL,

	/*
	 * Something serious e.g. connection with the backend was lost.. therefore
	 * abort the client.
	 */
	ELEVEL_LOG_CLIENT_ABORTED,

	/*
	 * To report the error/log messages of the main program and/or
	 * PGBENCH_DEBUG.
	 */
	ELEVEL_LOG_MAIN,

	/*
	 * To report the error messages of the main program and to exit immediately.
	 */
	ELEVEL_FATAL
} ErrorLevel;

typedef struct ErrorData
{
	ErrorLevel	elevel;
	PQExpBufferData message;
} ErrorData;

typedef ErrorData *Error;

#if defined(ENABLE_THREAD_SAFETY) && defined(HAVE__VA_ARGS)
/* use the local ErrorData in ereport */
#define LOCAL_ERROR_DATA()	ErrorData edata;

#define errstart(elevel)	errstartImpl(&edata, elevel)
#define errmsg(...)			errmsgImpl(&edata, __VA_ARGS__)
#define errfinish(...)		errfinishImpl(&edata, __VA_ARGS__)
#else							/* !(ENABLE_THREAD_SAFETY && HAVE__VA_ARGS) */
/* use the global ErrorData in ereport... */
#define LOCAL_ERROR_DATA()
static ErrorData edata;
static Error error = &edata;

/* ...and protect it with a mutex if necessary */
#ifdef ENABLE_THREAD_SAFETY
static pthread_mutex_t error_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif							/* ENABLE_THREAD_SAFETY */

#define errstart	errstartImpl
#define errmsg		errmsgImpl
#define errfinish	errfinishImpl
#endif							/* ENABLE_THREAD_SAFETY && HAVE__VA_ARGS */

/*
 * Error reporting API: to be used in this way:
 *		ereport(ELEVEL_LOG,
 *				(errmsg("connection to database \"%s\" failed\n", dbName),
 *				... other errxxx() fields as needed ...));
 *
 * The error level is required, and so is a primary error message. All else is
 * optional.
 *
 * If elevel >= ELEVEL_FATAL, the call will not return; we try to inform the
 * compiler of that via abort(). However, no useful optimization effect is
 * obtained unless the compiler sees elevel as a compile-time constant, else
 * we're just adding code bloat. So, if __builtin_constant_p is available, use
 * that to cause the second if() to vanish completely for non-constant cases. We
 * avoid using a local variable because it's not necessary and prevents gcc from
 * making the unreachability deduction at optlevel -O0.
 */
#ifdef HAVE__BUILTIN_CONSTANT_P
#define ereport(elevel, rest) \
	do { \
		LOCAL_ERROR_DATA() \
		if (errstart(elevel)) \
			errfinish rest; \
		if (__builtin_constant_p(elevel) && (elevel) >= ELEVEL_FATAL) \
			abort(); \
	} while(0)
#else							/* !HAVE__BUILTIN_CONSTANT_P */
#define ereport(elevel, rest) \
	do { \
		const int elevel_ = (elevel); \
		LOCAL_ERROR_DATA() \
		if (errstart(elevel_)) \
			errfinish rest; \
		if (elevel_ >= ELEVEL_FATAL) \
			abort(); \
	} while(0)
#endif							/* HAVE__BUILTIN_CONSTANT_P */


/* Function prototypes */
static void setNullValue(PgBenchValue *pv);
static void setBoolValue(PgBenchValue *pv, bool bval);
static void setIntValue(PgBenchValue *pv, int64 ival);
static void setDoubleValue(PgBenchValue *pv, double dval);
static bool evaluateExpr(TState *, CState *, PgBenchExpr *, PgBenchValue *);
static void doLog(TState *thread, CState *st,
	  StatsData *agg, bool skipped, double latency, double lag);
static void processXactStats(TState *thread, CState *st, instr_time *now,
				 bool skipped, StatsData *agg);
static void pgbench_error(const char *fmt,...) pg_attribute_printf(1, 2);
static void addScript(ParsedScript script);
static void *threadRun(void *arg);
static void setalarm(int seconds);
static void finishCon(CState *st);

#if defined(ENABLE_THREAD_SAFETY) && defined(HAVE__VA_ARGS)
static bool errstartImpl(Error error, ErrorLevel elevel);
static int  errmsgImpl(Error error,
					   const char *fmt,...) pg_attribute_printf(2, 3);
static void errfinishImpl(Error error, int dummy,...);
#else							/* !(ENABLE_THREAD_SAFETY && HAVE__VA_ARGS) */
static bool errstartImpl(ErrorLevel elevel);
static int  errmsgImpl(const char *fmt,...) pg_attribute_printf(1, 2);
static void errfinishImpl(int dummy,...);
#endif							/* ENABLE_THREAD_SAFETY && HAVE__VA_ARGS */

/* callback functions for our flex lexer */
static const PsqlScanCallbacks pgbench_callbacks = {
	NULL,						/* don't need get_variable functionality */
	pgbench_error
};


static void
usage(void)
{
	printf("%s is a benchmarking tool for YSQL.\n\n"
		   "Usage:\n"
		   "  %s [OPTION]... [DBNAME]\n"
		   "\nInitialization options:\n"
		   "  -i, --initialize         invokes initialization mode\n"
		   "  -I, --init-steps=[dtgvpf]+ (default \"dtgp\")\n"
		   "                           run selected initialization steps\n"
		   "  -F, --fillfactor=NUM     set fill factor\n"
		   "  -n, --no-vacuum          do not run VACUUM during initialization\n"
		   "  -q, --quiet              quiet logging (one message each 5 seconds)\n"
		   "  -s, --scale=NUM          scaling factor\n"
		   "  --foreign-keys           create foreign key constraints between tables\n"
		   "  --index-tablespace=TABLESPACE\n"
		   "                           create indexes in the specified tablespace\n"
		   "  --tablespace=TABLESPACE  create tables in the specified tablespace\n"
		   "  --unlogged-tables        create tables as unlogged tables\n"
		   "\nOptions to select what to run:\n"
		   "  -b, --builtin=NAME[@W]   add builtin script NAME weighted at W (default: 1)\n"
		   "                           (use \"-b list\" to list available scripts)\n"
		   "  -f, --file=FILENAME[@W]  add script FILENAME weighted at W (default: 1)\n"
		   "  -N, --skip-some-updates  skip updates of ysql_bench_tellers and ysql_bench_branches\n"
		   "                           (same as \"-b simple-update\")\n"
		   "  -S, --select-only        perform SELECT-only transactions\n"
		   "                           (same as \"-b select-only\")\n"
		   "\nBenchmarking options:\n"
		   "  -c, --client=NUM         number of concurrent database clients (default: 1)\n"
		   "  -C, --connect            establish new connection for each transaction\n"
		   "  -D, --define=VARNAME=VALUE\n"
		   "                           define variable for use by custom script\n"
		   "  -j, --jobs=NUM           number of threads (default: 1)\n"
		   "  -l, --log                write transaction times to log file\n"
		   "  -L, --latency-limit=NUM  count transactions lasting more than NUM ms as late\n"
		   "  -M, --protocol=simple|extended|prepared\n"
		   "                           protocol for submitting queries (default: simple)\n"
		   "  -n, --no-vacuum          do not run VACUUM before tests\n"
		   "  -P, --progress=NUM       show thread progress report every NUM seconds\n"
		   "  -r, --report-per-command report latencies, errors and retries per command\n"
		   "  -R, --rate=NUM           target rate in transactions per second\n"
		   "  -s, --scale=NUM          report this scale factor in output\n"
		   "  -t, --transactions=NUM   number of transactions each client runs (default: 10)\n"
		   "  -T, --time=NUM           duration of benchmark test in seconds\n"
		   "  -v, --vacuum-all         vacuum all four standard tables before tests\n"
		   "  --aggregate-interval=NUM aggregate data over NUM seconds\n"
		   "  --log-prefix=PREFIX      prefix for transaction time log file\n"
		   "                           (default: \"ysql_bench_log\")\n"
		   "  --max-tries=NUM          max number of tries to run transaction\n"
		   "  --progress-timestamp     use Unix epoch timestamps for progress\n"
		   "  --random-seed=SEED       set random seed (\"time\", \"rand\", integer)\n"
		   "  --sampling-rate=NUM      fraction of transactions to log (e.g., 0.01 for 1%%)\n"
		   "\nCommon options:\n"
		   "  -d, --debug=no|fails|all print debugging output (default: no)\n"
		   "  -h, --host=HOSTNAME      database server host or socket directory\n"
		   "  -p, --port=PORT          database server port number\n"
		   "  -U, --username=USERNAME  connect as specified database user\n"
		   "  -V, --version            output version information, then exit\n"
		   "  -?, --help               show this help, then exit\n"
		   "\n"
		   "Report bugs on https://github.com/YugaByte/yugabyte-db/issues/new.\n",
		   progname, progname);
}

/* return whether str matches "^\s*[-+]?[0-9]+$" */
static bool
is_an_int(const char *str)
{
	const char *ptr = str;

	/* skip leading spaces; cast is consistent with strtoint64 */
	while (*ptr && isspace((unsigned char) *ptr))
		ptr++;

	/* skip sign */
	if (*ptr == '+' || *ptr == '-')
		ptr++;

	/* at least one digit */
	if (*ptr && !isdigit((unsigned char) *ptr))
		return false;

	/* eat all digits */
	while (*ptr && isdigit((unsigned char) *ptr))
		ptr++;

	/* must have reached end of string */
	return *ptr == '\0';
}


/*
 * strtoint64 -- convert a string to 64-bit integer
 *
 * This function is a modified version of scanint8() from
 * src/backend/utils/adt/int8.c.
 */
int64
strtoint64(const char *str)
{
	const char *ptr = str;
	int64		result = 0;
	int			sign = 1;

	/*
	 * Do our own scan, rather than relying on sscanf which might be broken
	 * for long long.
	 */

	/* skip leading spaces */
	while (*ptr && isspace((unsigned char) *ptr))
		ptr++;

	/* handle sign */
	if (*ptr == '-')
	{
		ptr++;

		/*
		 * Do an explicit check for INT64_MIN.  Ugly though this is, it's
		 * cleaner than trying to get the loop below to handle it portably.
		 */
		if (strncmp(ptr, "9223372036854775808", 19) == 0)
		{
			result = PG_INT64_MIN;
			ptr += 19;
			goto gotdigits;
		}
		sign = -1;
	}
	else if (*ptr == '+')
		ptr++;

	/* require at least one digit */
	if (!isdigit((unsigned char) *ptr))
	{
		ereport(ELEVEL_LOG_MAIN,
				(errmsg("invalid input syntax for integer: \"%s\"\n", str)));
	}

	/* process digits */
	while (*ptr && isdigit((unsigned char) *ptr))
	{
		int64		tmp = result * 10 + (*ptr++ - '0');

		if ((tmp / 10) != result)	/* overflow? */
		{
			ereport(ELEVEL_LOG_MAIN,
					(errmsg("value \"%s\" is out of range for type bigint\n",
							str)));
		}
		result = tmp;
	}

gotdigits:

	/* allow trailing whitespace, but not other trailing chars */
	while (*ptr != '\0' && isspace((unsigned char) *ptr))
		ptr++;

	if (*ptr != '\0')
	{
		ereport(ELEVEL_LOG_MAIN,
				(errmsg("invalid input syntax for integer: \"%s\"\n", str)));
	}

	return ((sign < 0) ? -result : result);
}

/*
 * Random number generator: uniform distribution from min to max inclusive.
 *
 * Although the limits are expressed as int64, you can't generate the full
 * int64 range in one call, because the difference of the limits mustn't
 * overflow int64.  In practice it's unwise to ask for more than an int32
 * range, because of the limited precision of pg_erand48().
 */
static int64
getrand(RandomState *random_state, int64 min, int64 max)
{
	/*
	 * Odd coding is so that min and max have approximately the same chance of
	 * being selected as do numbers between them.
	 *
	 * pg_erand48() is thread-safe and concurrent, which is why we use it
	 * rather than random(), which in glibc is non-reentrant, and therefore
	 * protected by a mutex, and therefore a bottleneck on machines with many
	 * CPUs.
	 */
	return min + (int64) ((max - min + 1) * pg_erand48(random_state->data));
}

/*
 * random number generator: exponential distribution from min to max inclusive.
 * the parameter is so that the density of probability for the last cut-off max
 * value is exp(-parameter).
 */
static int64
getExponentialRand(RandomState *random_state, int64 min, int64 max,
				   double parameter)
{
	double		cut,
				uniform,
				rand;

	/* abort if wrong parameter, but must really be checked beforehand */
	Assert(parameter > 0.0);
	cut = exp(-parameter);
	/* erand in [0, 1), uniform in (0, 1] */
	uniform = 1.0 - pg_erand48(random_state->data);

	/*
	 * inner expression in (cut, 1] (if parameter > 0), rand in [0, 1)
	 */
	Assert((1.0 - cut) != 0.0);
	rand = -log(cut + (1.0 - cut) * uniform) / parameter;
	/* return int64 random number within between min and max */
	return min + (int64) ((max - min + 1) * rand);
}

/* random number generator: gaussian distribution from min to max inclusive */
static int64
getGaussianRand(RandomState *random_state, int64 min, int64 max,
				double parameter)
{
	double		stdev;
	double		rand;

	/* abort if parameter is too low, but must really be checked beforehand */
	Assert(parameter >= MIN_GAUSSIAN_PARAM);

	/*
	 * Get user specified random number from this loop, with -parameter <
	 * stdev <= parameter
	 *
	 * This loop is executed until the number is in the expected range.
	 *
	 * As the minimum parameter is 2.0, the probability of looping is low:
	 * sqrt(-2 ln(r)) <= 2 => r >= e^{-2} ~ 0.135, then when taking the
	 * average sinus multiplier as 2/pi, we have a 8.6% looping probability in
	 * the worst case. For a parameter value of 5.0, the looping probability
	 * is about e^{-5} * 2 / pi ~ 0.43%.
	 */
	do
	{
		/*
		 * pg_erand48 generates [0,1), but for the basic version of the
		 * Box-Muller transform the two uniformly distributed random numbers
		 * are expected in (0, 1] (see
		 * https://en.wikipedia.org/wiki/Box-Muller_transform)
		 */
		double		rand1 = 1.0 - pg_erand48(random_state->data);
		double		rand2 = 1.0 - pg_erand48(random_state->data);

		/* Box-Muller basic form transform */
		double		var_sqrt = sqrt(-2.0 * log(rand1));

		stdev = var_sqrt * sin(2.0 * M_PI * rand2);

		/*
		 * we may try with cos, but there may be a bias induced if the
		 * previous value fails the test. To be on the safe side, let us try
		 * over.
		 */
	}
	while (stdev < -parameter || stdev >= parameter);

	/* stdev is in [-parameter, parameter), normalization to [0,1) */
	rand = (stdev + parameter) / (parameter * 2.0);

	/* return int64 random number within between min and max */
	return min + (int64) ((max - min + 1) * rand);
}

/*
 * random number generator: generate a value, such that the series of values
 * will approximate a Poisson distribution centered on the given value.
 */
static int64
getPoissonRand(RandomState *random_state, int64 center)
{
	/*
	 * Use inverse transform sampling to generate a value > 0, such that the
	 * expected (i.e. average) value is the given argument.
	 */
	double		uniform;

	/* erand in [0, 1), uniform in (0, 1] */
	uniform = 1.0 - pg_erand48(random_state->data);

	return (int64) (-log(uniform) * ((double) center) + 0.5);
}

/* helper function for getZipfianRand */
static double
generalizedHarmonicNumber(int64 n, double s)
{
	int			i;
	double		ans = 0.0;

	for (i = n; i > 1; i--)
		ans += pow(i, -s);
	return ans + 1.0;
}

/* set harmonicn and other parameters to cache cell */
static void
zipfSetCacheCell(ZipfCell *cell, int64 n, double s)
{
	double		harmonic2;

	cell->n = n;
	cell->s = s;

	harmonic2 = generalizedHarmonicNumber(2, s);
	cell->harmonicn = generalizedHarmonicNumber(n, s);

	cell->alpha = 1.0 / (1.0 - s);
	cell->beta = pow(0.5, s);
	cell->eta = (1.0 - pow(2.0 / n, 1.0 - s)) / (1.0 - harmonic2 / cell->harmonicn);
}

/*
 * search for cache cell with keys (n, s)
 * and create new cell if it does not exist
 */
static ZipfCell *
zipfFindOrCreateCacheCell(ZipfCache *cache, int64 n, double s)
{
	int			i,
				least_recently_used = 0;
	ZipfCell   *cell;

	/* search cached cell for given parameters */
	for (i = 0; i < cache->nb_cells; i++)
	{
		cell = &cache->cells[i];
		if (cell->n == n && cell->s == s)
			return &cache->cells[i];

		if (cell->last_used < cache->cells[least_recently_used].last_used)
			least_recently_used = i;
	}

	/* create new one if it does not exist */
	if (cache->nb_cells < ZIPF_CACHE_SIZE)
		i = cache->nb_cells++;
	else
	{
		/* replace LRU cell if cache is full */
		i = least_recently_used;
		cache->overflowCount++;
	}

	zipfSetCacheCell(&cache->cells[i], n, s);

	cache->cells[i].last_used = cache->current++;
	return &cache->cells[i];
}

/*
 * Computing zipfian using rejection method, based on
 * "Non-Uniform Random Variate Generation",
 * Luc Devroye, p. 550-551, Springer 1986.
 */
static int64
computeIterativeZipfian(RandomState *random_state, int64 n, double s)
{
	double		b = pow(2.0, s - 1.0);
	double		x,
				t,
				u,
				v;

	while (true)
	{
		/* random variates */
		u = pg_erand48(random_state->data);
		v = pg_erand48(random_state->data);

		x = floor(pow(u, -1.0 / (s - 1.0)));

		t = pow(1.0 + 1.0 / x, s - 1.0);
		/* reject if too large or out of bound */
		if (v * x * (t - 1.0) / (b - 1.0) <= t / b && x <= n)
			break;
	}
	return (int64) x;
}

/*
 * Computing zipfian using harmonic numbers, based on algorithm described in
 * "Quickly Generating Billion-Record Synthetic Databases",
 * Jim Gray et al, SIGMOD 1994
 */
static int64
computeHarmonicZipfian(TState *thread, RandomState *random_state, int64 n,
					   double s)
{
	ZipfCell   *cell = zipfFindOrCreateCacheCell(&thread->zipf_cache, n, s);
	double		uniform = pg_erand48(random_state->data);
	double		uz = uniform * cell->harmonicn;

	if (uz < 1.0)
		return 1;
	if (uz < 1.0 + cell->beta)
		return 2;
	return 1 + (int64) (cell->n * pow(cell->eta * uniform - cell->eta + 1.0, cell->alpha));
}

/* random number generator: zipfian distribution from min to max inclusive */
static int64
getZipfianRand(TState *thread, RandomState *random_state, int64 min,
			   int64 max, double s)
{
	int64		n = max - min + 1;

	/* abort if parameter is invalid */
	Assert(s > 0.0 && s != 1.0 && s <= MAX_ZIPFIAN_PARAM);


	return min - 1 + ((s > 1)
					  ? computeIterativeZipfian(random_state, n, s)
					  : computeHarmonicZipfian(thread, random_state, n, s));
}

/*
 * FNV-1a hash function
 */
static int64
getHashFnv1a(int64 val, uint64 seed)
{
	int64		result;
	int			i;

	result = FNV_OFFSET_BASIS ^ seed;
	for (i = 0; i < 8; ++i)
	{
		int32		octet = val & 0xff;

		val = val >> 8;
		result = result ^ octet;
		result = result * FNV_PRIME;
	}

	return result;
}

/*
 * Murmur2 hash function
 *
 * Based on original work of Austin Appleby
 * https://github.com/aappleby/smhasher/blob/master/src/MurmurHash2.cpp
 */
static int64
getHashMurmur2(int64 val, uint64 seed)
{
	uint64		result = seed ^ MM2_MUL_TIMES_8;	/* sizeof(int64) */
	uint64		k = (uint64) val;

	k *= MM2_MUL;
	k ^= k >> MM2_ROT;
	k *= MM2_MUL;

	result ^= k;
	result *= MM2_MUL;

	result ^= result >> MM2_ROT;
	result *= MM2_MUL;
	result ^= result >> MM2_ROT;

	return (int64) result;
}

/*
 * Initialize the given SimpleStats struct to all zeroes
 */
static void
initSimpleStats(SimpleStats *ss)
{
	memset(ss, 0, sizeof(SimpleStats));
}

/*
 * Accumulate one value into a SimpleStats struct.
 */
static void
addToSimpleStats(SimpleStats *ss, double val)
{
	if (ss->count == 0 || val < ss->min)
		ss->min = val;
	if (ss->count == 0 || val > ss->max)
		ss->max = val;
	ss->count++;
	ss->sum += val;
	ss->sum2 += val * val;
}

/*
 * Merge two SimpleStats objects
 */
static void
mergeSimpleStats(SimpleStats *acc, SimpleStats *ss)
{
	if (acc->count == 0 || ss->min < acc->min)
		acc->min = ss->min;
	if (acc->count == 0 || ss->max > acc->max)
		acc->max = ss->max;
	acc->count += ss->count;
	acc->sum += ss->sum;
	acc->sum2 += ss->sum2;
}

/*
 * Initialize a StatsData struct to mostly zeroes, with its start time set to
 * the given value.
 */
static void
initStats(StatsData *sd, time_t start_time)
{
	sd->start_time = start_time;
	sd->cnt = 0;
	sd->skipped = 0;
	sd->retries = 0;
	sd->retried = 0;
	sd->errors = 0;
	sd->errors_in_failed_tx = 0;
	initSimpleStats(&sd->latency);
	initSimpleStats(&sd->lag);
}

/*
 * Accumulate one additional item into the given stats object.
 */
static void
accumStats(StatsData *stats, bool skipped, double lat, double lag,
		   FailureStatus first_error, int64 retries)
{
	/*
	 * Record the number of retries regardless of whether the transaction was
	 * successful or failed.
	 */
	stats->retries += retries;
	if (retries > 0)
		stats->retried++;

	/* Record the failed transaction */
	if (first_error != NO_FAILURE)
	{
		stats->errors++;

		if (first_error == IN_FAILED_SQL_TRANSACTION)
			stats->errors_in_failed_tx++;

		return;
	}

	/* Record the successful transaction */

	stats->cnt++;

	if (skipped)
	{
		/* no latency to record on skipped transactions */
		stats->skipped++;
	}
	else
	{
		addToSimpleStats(&stats->latency, lat);

		/* and possibly the same for schedule lag */
		if (throttle_delay)
			addToSimpleStats(&stats->lag, lag);
	}
}

/* call PQexec() and exit() on failure */
static void
executeStatement(PGconn *con, const char *sql)
{
	PGresult   *res;

	res = PQexec(con, sql);
	if (PQresultStatus(res) != PGRES_COMMAND_OK)
		ereport(ELEVEL_FATAL, (errmsg("%s", PQerrorMessage(con))));
	PQclear(res);
}

/* call PQexec() and complain, but without exiting, on failure */
static void
tryExecuteStatement(PGconn *con, const char *sql)
{
	PGresult   *res;

	res = PQexec(con, sql);
	if (PQresultStatus(res) != PGRES_COMMAND_OK)
	{
		ereport(ELEVEL_LOG_MAIN,
				(errmsg("%s(ignoring this error and continuing anyway)\n",
						PQerrorMessage(con))));
	}
	PQclear(res);
}

/* set up a connection to the backend */
static PGconn *
doConnect(void)
{
	PGconn	   *conn;
	bool		new_pass;
	static bool have_password = false;
	static char password[100];

	/*
	 * Start the connection.  Loop until we have a password if requested by
	 * backend.
	 */
	do
	{
#define PARAMS_ARRAY_SIZE	7

		const char *keywords[PARAMS_ARRAY_SIZE];
		const char *values[PARAMS_ARRAY_SIZE];

		keywords[0] = "host";
		values[0] = pghost;
		keywords[1] = "port";
		values[1] = pgport;
		keywords[2] = "user";
		values[2] = login;
		keywords[3] = "password";
		values[3] = have_password ? password : NULL;
		keywords[4] = "dbname";
		values[4] = dbName;
		keywords[5] = "fallback_application_name";
		values[5] = progname;
		keywords[6] = NULL;
		values[6] = NULL;

		new_pass = false;

		conn = PQconnectdbParams(keywords, values, true);

		if (!conn)
		{
			ereport(ELEVEL_LOG_MAIN,
					(errmsg("connection to database \"%s\" failed\n", dbName)));
			return NULL;
		}

		if (PQstatus(conn) == CONNECTION_BAD &&
			PQconnectionNeedsPassword(conn) &&
			!have_password)
		{
			PQfinish(conn);
			simple_prompt("Password: ", password, sizeof(password), false);
			have_password = true;
			new_pass = true;
		}
	} while (new_pass);

	/* check to see that the backend connection was successfully made */
	if (PQstatus(conn) == CONNECTION_BAD)
	{
		ereport(ELEVEL_LOG_MAIN,
				(errmsg("connection to database \"%s\" failed:\n%s",
						dbName, PQerrorMessage(conn))));
		PQfinish(conn);
		return NULL;
	}

	return conn;
}

/* throw away response from backend */
static void
discard_response(CState *state)
{
	PGresult   *res;

	do
	{
		res = PQgetResult(state->con);
		if (res)
			PQclear(res);
	} while (res);
}

/* qsort comparator for Variable array */
static int
compareVariableNames(const void *v1, const void *v2)
{
	return strcmp(((const Variable *) v1)->name,
				  ((const Variable *) v2)->name);
}

/* Locate a variable by name; returns NULL if unknown */
static Variable *
lookupVariable(Variables *variables, char *name)
{
	Variable	key;

	/* On some versions of Solaris, bsearch of zero items dumps core */
	if (variables->nvariables <= 0)
		return NULL;

	/* Sort if we have to */
	if (!variables->vars_sorted)
	{
		qsort((void *) variables->array, variables->nvariables,
			  sizeof(Variable), compareVariableNames);
		variables->vars_sorted = true;
	}

	/* Now we can search */
	key.name = name;
	return (Variable *) bsearch((void *) &key,
								(void *) variables->array,
								variables->nvariables,
								sizeof(Variable),
								compareVariableNames);
}

/* Get the value of a variable, in string form; returns NULL if unknown */
static char *
getVariable(Variables *variables, char *name)
{
	Variable   *var;
	char		stringform[64];

	var = lookupVariable(variables, name);
	if (var == NULL)
		return NULL;			/* not found */

	if (var->svalue)
		return var->svalue;		/* we have it in string form */

	/* We need to produce a string equivalent of the value */
	Assert(var->value.type != PGBT_NO_VALUE);
	if (var->value.type == PGBT_NULL)
		snprintf(stringform, sizeof(stringform), "NULL");
	else if (var->value.type == PGBT_BOOLEAN)
		snprintf(stringform, sizeof(stringform),
				 "%s", var->value.u.bval ? "true" : "false");
	else if (var->value.type == PGBT_INT)
		snprintf(stringform, sizeof(stringform),
				 INT64_FORMAT, var->value.u.ival);
	else if (var->value.type == PGBT_DOUBLE)
		snprintf(stringform, sizeof(stringform),
				 "%.*g", DBL_DIG, var->value.u.dval);
	else						/* internal error, unexpected type */
		Assert(0);
	var->svalue = pg_strdup(stringform);
	return var->svalue;
}

/* Try to convert variable to a value; return false on failure */
static bool
makeVariableValue(Variable *var)
{
	size_t		slen;

	if (var->value.type != PGBT_NO_VALUE)
		return true;			/* no work */

	slen = strlen(var->svalue);

	if (slen == 0)
		/* what should it do on ""? */
		return false;

	if (pg_strcasecmp(var->svalue, "null") == 0)
	{
		setNullValue(&var->value);
	}

	/*
	 * accept prefixes such as y, ye, n, no... but not for "o". 0/1 are
	 * recognized later as an int, which is converted to bool if needed.
	 */
	else if (pg_strncasecmp(var->svalue, "true", slen) == 0 ||
			 pg_strncasecmp(var->svalue, "yes", slen) == 0 ||
			 pg_strcasecmp(var->svalue, "on") == 0)
	{
		setBoolValue(&var->value, true);
	}
	else if (pg_strncasecmp(var->svalue, "false", slen) == 0 ||
			 pg_strncasecmp(var->svalue, "no", slen) == 0 ||
			 pg_strcasecmp(var->svalue, "off") == 0 ||
			 pg_strcasecmp(var->svalue, "of") == 0)
	{
		setBoolValue(&var->value, false);
	}
	else if (is_an_int(var->svalue))
	{
		setIntValue(&var->value, strtoint64(var->svalue));
	}
	else						/* type should be double */
	{
		double		dv;
		char		xs;

		if (sscanf(var->svalue, "%lf%c", &dv, &xs) != 1)
		{
			ereport(ELEVEL_LOG_CLIENT_FAIL,
					(errmsg("malformed variable \"%s\" value: \"%s\"\n",
							var->name, var->svalue)));
			return false;
		}
		setDoubleValue(&var->value, dv);
	}
	return true;
}

/*
 * Check whether a variable's name is allowed.
 *
 * We allow any non-ASCII character, as well as ASCII letters, digits, and
 * underscore.
 *
 * Keep this in sync with the definitions of variable name characters in
 * "src/fe_utils/psqlscan.l", "src/bin/psql/psqlscanslash.l" and
 * "src/bin/pgbench/exprscan.l".  Also see parseVariable(), below.
 *
 * Note: this static function is copied from "src/bin/psql/variables.c"
 */
static bool
valid_variable_name(const char *name)
{
	const unsigned char *ptr = (const unsigned char *) name;

	/* Mustn't be zero-length */
	if (*ptr == '\0')
		return false;

	while (*ptr)
	{
		if (IS_HIGHBIT_SET(*ptr) ||
			strchr("ABCDEFGHIJKLMNOPQRSTUVWXYZ" "abcdefghijklmnopqrstuvwxyz"
				   "_0123456789", *ptr) != NULL)
			ptr++;
		else
			return false;
	}

	return true;
}

/*
 * Lookup a variable by name, creating it if need be.
 * Caller is expected to assign a value to the variable.
 * On failure (bad name): if this is a client run returns NULL; exits the
 * program otherwise.
 */
static Variable *
lookupCreateVariable(Variables *variables, const char *context, char *name,
					 bool client)
{
	Variable   *var;

	var = lookupVariable(variables, name);
	if (var == NULL)
	{
		Variable   *newvars;

		/*
		 * Check for the name only when declaring a new variable to avoid
		 * overhead.
		 */
		if (!valid_variable_name(name))
		{
			/*
			 * About the error level used: if we process client commands, it a
			 * normal failure; otherwise it is not and we exit the program.
			 */
			ereport(client ? ELEVEL_LOG_CLIENT_FAIL : ELEVEL_FATAL,
					(errmsg("%s: invalid variable name: \"%s\"\n",
							context, name)));
			return NULL;
		}

		/* Create variable at the end of the array */
		if (variables->array)
			newvars = (Variable *) pg_realloc(variables->array,
								(variables->nvariables + 1) * sizeof(Variable));
		else
			newvars = (Variable *) pg_malloc(sizeof(Variable));

		variables->array = newvars;

		var = &newvars[variables->nvariables];

		var->name = pg_strdup(name);
		var->svalue = NULL;
		/* caller is expected to initialize remaining fields */

		variables->nvariables++;
		/* we don't re-sort the array till we have to */
		variables->vars_sorted = false;
	}

	return var;
}

/* Assign a string value to a variable, creating it if need be */
/* Exits on failure (bad name) */
static void
putVariable(Variables *variables, const char *context, char *name,
			const char *value)
{
	Variable   *var;
	char	   *val;

	var = lookupCreateVariable(variables, context, name, false);

	/* dup then free, in case value is pointing at this variable */
	val = pg_strdup(value);

	if (var->svalue)
		free(var->svalue);
	var->svalue = val;
	var->value.type = PGBT_NO_VALUE;
}

/*
 * Assign a value to a variable, creating it if need be.
 * On failure (bad name): if this is a client run returns false; exits the
 * program otherwise.
 */
static bool
putVariableValue(Variables *variables, const char *context, char *name,
				 const PgBenchValue *value, bool client)
{
	Variable   *var;

	var = lookupCreateVariable(variables, context, name, client);
	if (!var)
		return false;

	if (var->svalue)
		free(var->svalue);
	var->svalue = NULL;
	var->value = *value;

	return true;
}

/*
 * Assign an integer value to a variable, creating it if need be.
 * On failure (bad name): if this is a client run returns false; exits the
 * program otherwise.
 */
static bool
putVariableInt(Variables *variables, const char *context, char *name,
			   int64 value, bool client)
{
	PgBenchValue val;

	setIntValue(&val, value);
	return putVariableValue(variables, context, name, &val, client);
}

/*
 * Parse a possible variable reference (:varname).
 *
 * "sql" points at a colon.  If what follows it looks like a valid
 * variable name, return a malloc'd string containing the variable name,
 * and set *eaten to the number of characters consumed.
 * Otherwise, return NULL.
 */
static char *
parseVariable(const char *sql, int *eaten)
{
	int			i = 0;
	char	   *name;

	do
	{
		i++;
	} while (IS_HIGHBIT_SET(sql[i]) ||
			 strchr("ABCDEFGHIJKLMNOPQRSTUVWXYZ" "abcdefghijklmnopqrstuvwxyz"
					"_0123456789", sql[i]) != NULL);
	if (i == 1)
		return NULL;			/* no valid variable name chars */

	name = pg_malloc(i);
	memcpy(name, &sql[1], i - 1);
	name[i - 1] = '\0';

	*eaten = i;
	return name;
}

static char *
replaceVariable(char **sql, char *param, int len, char *value)
{
	int			valueln = strlen(value);

	if (valueln > len)
	{
		size_t		offset = param - *sql;

		*sql = pg_realloc(*sql, strlen(*sql) - len + valueln + 1);
		param = *sql + offset;
	}

	if (valueln != len)
		memmove(param + valueln, param + len, strlen(param + len) + 1);
	memcpy(param, value, valueln);

	return param + valueln;
}

static char *
assignVariables(Variables *variables, char *sql)
{
	char	   *p,
			   *name,
			   *val;

	p = sql;
	while ((p = strchr(p, ':')) != NULL)
	{
		int			eaten;

		name = parseVariable(p, &eaten);
		if (name == NULL)
		{
			while (*p == ':')
			{
				p++;
			}
			continue;
		}

		val = getVariable(variables, name);
		free(name);
		if (val == NULL)
		{
			p++;
			continue;
		}

		p = replaceVariable(&sql, p, eaten, val);
	}

	return sql;
}

static void
getQueryParams(Variables *variables, const Command *command,
			   const char **params)
{
	int			i;

	for (i = 0; i < command->argc - 1; i++)
		params[i] = getVariable(variables, command->argv[i + 1]);
}

static char *
valueTypeName(PgBenchValue *pval)
{
	if (pval->type == PGBT_NO_VALUE)
		return "none";
	else if (pval->type == PGBT_NULL)
		return "null";
	else if (pval->type == PGBT_INT)
		return "int";
	else if (pval->type == PGBT_DOUBLE)
		return "double";
	else if (pval->type == PGBT_BOOLEAN)
		return "boolean";
	else
	{
		/* internal error, should never get there */
		Assert(false);
		return NULL;
	}
}

/* get a value as a boolean, or tell if there is a problem */
static bool
coerceToBool(PgBenchValue *pval, bool *bval)
{
	if (pval->type == PGBT_BOOLEAN)
	{
		*bval = pval->u.bval;
		return true;
	}
	else						/* NULL, INT or DOUBLE */
	{
		ereport(ELEVEL_LOG_CLIENT_FAIL,
				(errmsg("cannot coerce %s to boolean\n", valueTypeName(pval))));
		*bval = false;			/* suppress uninitialized-variable warnings */
		return false;
	}
}

/*
 * Return true or false from an expression for conditional purposes.
 * Non zero numerical values are true, zero and NULL are false.
 */
static bool
valueTruth(PgBenchValue *pval)
{
	switch (pval->type)
	{
		case PGBT_NULL:
			return false;
		case PGBT_BOOLEAN:
			return pval->u.bval;
		case PGBT_INT:
			return pval->u.ival != 0;
		case PGBT_DOUBLE:
			return pval->u.dval != 0.0;
		default:
			/* internal error, unexpected type */
			Assert(0);
			return false;
	}
}

/* get a value as an int, tell if there is a problem */
static bool
coerceToInt(PgBenchValue *pval, int64 *ival)
{
	if (pval->type == PGBT_INT)
	{
		*ival = pval->u.ival;
		return true;
	}
	else if (pval->type == PGBT_DOUBLE)
	{
		double		dval = pval->u.dval;

		if (dval < PG_INT64_MIN || PG_INT64_MAX < dval)
		{
			ereport(ELEVEL_LOG_CLIENT_FAIL,
					(errmsg("double to int overflow for %f\n", dval)));
			return false;
		}
		*ival = (int64) dval;
		return true;
	}
	else						/* BOOLEAN or NULL */
	{
		ereport(ELEVEL_LOG_CLIENT_FAIL,
				(errmsg("cannot coerce %s to int\n", valueTypeName(pval))));
		return false;
	}
}

/* get a value as a double, or tell if there is a problem */
static bool
coerceToDouble(PgBenchValue *pval, double *dval)
{
	if (pval->type == PGBT_DOUBLE)
	{
		*dval = pval->u.dval;
		return true;
	}
	else if (pval->type == PGBT_INT)
	{
		*dval = (double) pval->u.ival;
		return true;
	}
	else						/* BOOLEAN or NULL */
	{
		ereport(ELEVEL_LOG_CLIENT_FAIL,
				(errmsg("cannot coerce %s to double\n", valueTypeName(pval))));
		return false;
	}
}

/* assign a null value */
static void
setNullValue(PgBenchValue *pv)
{
	pv->type = PGBT_NULL;
	pv->u.ival = 0;
}

/* assign a boolean value */
static void
setBoolValue(PgBenchValue *pv, bool bval)
{
	pv->type = PGBT_BOOLEAN;
	pv->u.bval = bval;
}

/* assign an integer value */
static void
setIntValue(PgBenchValue *pv, int64 ival)
{
	pv->type = PGBT_INT;
	pv->u.ival = ival;
}

/* assign a double value */
static void
setDoubleValue(PgBenchValue *pv, double dval)
{
	pv->type = PGBT_DOUBLE;
	pv->u.dval = dval;
}

static bool
isLazyFunc(PgBenchFunction func)
{
	return func == PGBENCH_AND || func == PGBENCH_OR || func == PGBENCH_CASE;
}

/* lazy evaluation of some functions */
static bool
evalLazyFunc(TState *thread, CState *st,
			 PgBenchFunction func, PgBenchExprLink *args, PgBenchValue *retval)
{
	PgBenchValue a1,
				a2;
	bool		ba1,
				ba2;

	Assert(isLazyFunc(func) && args != NULL && args->next != NULL);

	/* args points to first condition */
	if (!evaluateExpr(thread, st, args->expr, &a1))
		return false;

	/* second condition for AND/OR and corresponding branch for CASE */
	args = args->next;

	switch (func)
	{
		case PGBENCH_AND:
			if (a1.type == PGBT_NULL)
			{
				setNullValue(retval);
				return true;
			}

			if (!coerceToBool(&a1, &ba1))
				return false;

			if (!ba1)
			{
				setBoolValue(retval, false);
				return true;
			}

			if (!evaluateExpr(thread, st, args->expr, &a2))
				return false;

			if (a2.type == PGBT_NULL)
			{
				setNullValue(retval);
				return true;
			}
			else if (!coerceToBool(&a2, &ba2))
				return false;
			else
			{
				setBoolValue(retval, ba2);
				return true;
			}

			return true;

		case PGBENCH_OR:

			if (a1.type == PGBT_NULL)
			{
				setNullValue(retval);
				return true;
			}

			if (!coerceToBool(&a1, &ba1))
				return false;

			if (ba1)
			{
				setBoolValue(retval, true);
				return true;
			}

			if (!evaluateExpr(thread, st, args->expr, &a2))
				return false;

			if (a2.type == PGBT_NULL)
			{
				setNullValue(retval);
				return true;
			}
			else if (!coerceToBool(&a2, &ba2))
				return false;
			else
			{
				setBoolValue(retval, ba2);
				return true;
			}

		case PGBENCH_CASE:
			/* when true, execute branch */
			if (valueTruth(&a1))
				return evaluateExpr(thread, st, args->expr, retval);

			/* now args contains next condition or final else expression */
			args = args->next;

			/* final else case? */
			if (args->next == NULL)
				return evaluateExpr(thread, st, args->expr, retval);

			/* no, another when, proceed */
			return evalLazyFunc(thread, st, PGBENCH_CASE, args, retval);

		default:
			/* internal error, cannot get here */
			Assert(0);
			break;
	}
	return false;
}

/* maximum number of function arguments */
#define MAX_FARGS 16

/*
 * Recursive evaluation of standard functions,
 * which do not require lazy evaluation.
 */
static bool
evalStandardFunc(TState *thread, CState *st,
				 PgBenchFunction func, PgBenchExprLink *args,
				 PgBenchValue *retval)
{
	/* evaluate all function arguments */
	int			nargs = 0;
	PgBenchValue vargs[MAX_FARGS];
	PgBenchExprLink *l = args;
	bool		has_null = false;

	for (nargs = 0; nargs < MAX_FARGS && l != NULL; nargs++, l = l->next)
	{
		if (!evaluateExpr(thread, st, l->expr, &vargs[nargs]))
			return false;
		has_null |= vargs[nargs].type == PGBT_NULL;
	}

	if (l != NULL)
	{
		ereport(ELEVEL_LOG_CLIENT_FAIL,
				(errmsg("too many function arguments, maximum is %d\n",
					   MAX_FARGS)));
		return false;
	}

	/* NULL arguments */
	if (has_null && func != PGBENCH_IS && func != PGBENCH_DEBUG)
	{
		setNullValue(retval);
		return true;
	}

	/* then evaluate function */
	switch (func)
	{
			/* overloaded operators */
		case PGBENCH_ADD:
		case PGBENCH_SUB:
		case PGBENCH_MUL:
		case PGBENCH_DIV:
		case PGBENCH_MOD:
		case PGBENCH_EQ:
		case PGBENCH_NE:
		case PGBENCH_LE:
		case PGBENCH_LT:
			{
				PgBenchValue *lval = &vargs[0],
						   *rval = &vargs[1];

				Assert(nargs == 2);

				/* overloaded type management, double if some double */
				if ((lval->type == PGBT_DOUBLE ||
					 rval->type == PGBT_DOUBLE) && func != PGBENCH_MOD)
				{
					double		ld,
								rd;

					if (!coerceToDouble(lval, &ld) ||
						!coerceToDouble(rval, &rd))
						return false;

					switch (func)
					{
						case PGBENCH_ADD:
							setDoubleValue(retval, ld + rd);
							return true;

						case PGBENCH_SUB:
							setDoubleValue(retval, ld - rd);
							return true;

						case PGBENCH_MUL:
							setDoubleValue(retval, ld * rd);
							return true;

						case PGBENCH_DIV:
							setDoubleValue(retval, ld / rd);
							return true;

						case PGBENCH_EQ:
							setBoolValue(retval, ld == rd);
							return true;

						case PGBENCH_NE:
							setBoolValue(retval, ld != rd);
							return true;

						case PGBENCH_LE:
							setBoolValue(retval, ld <= rd);
							return true;

						case PGBENCH_LT:
							setBoolValue(retval, ld < rd);
							return true;

						default:
							/* cannot get here */
							Assert(0);
					}
				}
				else			/* we have integer operands, or % */
				{
					int64		li,
								ri;

					if (!coerceToInt(lval, &li) ||
						!coerceToInt(rval, &ri))
						return false;

					switch (func)
					{
						case PGBENCH_ADD:
							setIntValue(retval, li + ri);
							return true;

						case PGBENCH_SUB:
							setIntValue(retval, li - ri);
							return true;

						case PGBENCH_MUL:
							setIntValue(retval, li * ri);
							return true;

						case PGBENCH_EQ:
							setBoolValue(retval, li == ri);
							return true;

						case PGBENCH_NE:
							setBoolValue(retval, li != ri);
							return true;

						case PGBENCH_LE:
							setBoolValue(retval, li <= ri);
							return true;

						case PGBENCH_LT:
							setBoolValue(retval, li < ri);
							return true;

						case PGBENCH_DIV:
						case PGBENCH_MOD:
							if (ri == 0)
							{
								ereport(ELEVEL_LOG_CLIENT_FAIL,
										(errmsg("division by zero\n")));
								return false;
							}
							/* special handling of -1 divisor */
							if (ri == -1)
							{
								if (func == PGBENCH_DIV)
								{
									/* overflow check (needed for INT64_MIN) */
									if (li == PG_INT64_MIN)
									{
										ereport(
											ELEVEL_LOG_CLIENT_FAIL,
											(errmsg("bigint out of range\n")));
										return false;
									}
									else
										setIntValue(retval, -li);
								}
								else
									setIntValue(retval, 0);
								return true;
							}
							/* else divisor is not -1 */
							if (func == PGBENCH_DIV)
								setIntValue(retval, li / ri);
							else	/* func == PGBENCH_MOD */
								setIntValue(retval, li % ri);

							return true;

						default:
							/* cannot get here */
							Assert(0);
					}
				}

				Assert(0);
				return false;	/* NOTREACHED */
			}

			/* integer bitwise operators */
		case PGBENCH_BITAND:
		case PGBENCH_BITOR:
		case PGBENCH_BITXOR:
		case PGBENCH_LSHIFT:
		case PGBENCH_RSHIFT:
			{
				int64		li,
							ri;

				if (!coerceToInt(&vargs[0], &li) || !coerceToInt(&vargs[1], &ri))
					return false;

				if (func == PGBENCH_BITAND)
					setIntValue(retval, li & ri);
				else if (func == PGBENCH_BITOR)
					setIntValue(retval, li | ri);
				else if (func == PGBENCH_BITXOR)
					setIntValue(retval, li ^ ri);
				else if (func == PGBENCH_LSHIFT)
					setIntValue(retval, li << ri);
				else if (func == PGBENCH_RSHIFT)
					setIntValue(retval, li >> ri);
				else			/* cannot get here */
					Assert(0);

				return true;
			}

			/* logical operators */
		case PGBENCH_NOT:
			{
				bool		b;

				if (!coerceToBool(&vargs[0], &b))
					return false;

				setBoolValue(retval, !b);
				return true;
			}

			/* no arguments */
		case PGBENCH_PI:
			setDoubleValue(retval, M_PI);
			return true;

			/* 1 overloaded argument */
		case PGBENCH_ABS:
			{
				PgBenchValue *varg = &vargs[0];

				Assert(nargs == 1);

				if (varg->type == PGBT_INT)
				{
					int64		i = varg->u.ival;

					setIntValue(retval, i < 0 ? -i : i);
				}
				else
				{
					double		d = varg->u.dval;

					Assert(varg->type == PGBT_DOUBLE);
					setDoubleValue(retval, d < 0.0 ? -d : d);
				}

				return true;
			}

		case PGBENCH_DEBUG:
			{
				PgBenchValue *varg = &vargs[0];
				PQExpBufferData errormsg_buf;

				Assert(nargs == 1);

				initPQExpBuffer(&errormsg_buf);
				printfPQExpBuffer(&errormsg_buf,
								  "debug(script=%d,command=%d): ",
								  st->use_file, st->command + 1);

				if (varg->type == PGBT_NULL)
				{
					appendPQExpBuffer(&errormsg_buf, "null\n");
				}
				else if (varg->type == PGBT_BOOLEAN)
				{
					appendPQExpBuffer(&errormsg_buf,
									  "boolean %s\n",
									  varg->u.bval ? "true" : "false");
				}
				else if (varg->type == PGBT_INT)
				{
					appendPQExpBuffer(&errormsg_buf,
									  "int " INT64_FORMAT "\n", varg->u.ival);
				}
				else if (varg->type == PGBT_DOUBLE)
				{
					appendPQExpBuffer(&errormsg_buf,
									  "double %.*g\n", DBL_DIG, varg->u.dval);
				}
				else			/* internal error, unexpected type */
				{
					Assert(0);
				}

				ereport(ELEVEL_LOG_MAIN, (errmsg("%s", errormsg_buf.data)));
				termPQExpBuffer(&errormsg_buf);

				*retval = *varg;

				return true;
			}

			/* 1 double argument */
		case PGBENCH_DOUBLE:
		case PGBENCH_SQRT:
		case PGBENCH_LN:
		case PGBENCH_EXP:
			{
				double		dval;

				Assert(nargs == 1);

				if (!coerceToDouble(&vargs[0], &dval))
					return false;

				if (func == PGBENCH_SQRT)
					dval = sqrt(dval);
				else if (func == PGBENCH_LN)
					dval = log(dval);
				else if (func == PGBENCH_EXP)
					dval = exp(dval);
				/* else is cast: do nothing */

				setDoubleValue(retval, dval);
				return true;
			}

			/* 1 int argument */
		case PGBENCH_INT:
			{
				int64		ival;

				Assert(nargs == 1);

				if (!coerceToInt(&vargs[0], &ival))
					return false;

				setIntValue(retval, ival);
				return true;
			}

			/* variable number of arguments */
		case PGBENCH_LEAST:
		case PGBENCH_GREATEST:
			{
				bool		havedouble;
				int			i;

				Assert(nargs >= 1);

				/* need double result if any input is double */
				havedouble = false;
				for (i = 0; i < nargs; i++)
				{
					if (vargs[i].type == PGBT_DOUBLE)
					{
						havedouble = true;
						break;
					}
				}
				if (havedouble)
				{
					double		extremum;

					if (!coerceToDouble(&vargs[0], &extremum))
						return false;
					for (i = 1; i < nargs; i++)
					{
						double		dval;

						if (!coerceToDouble(&vargs[i], &dval))
							return false;
						if (func == PGBENCH_LEAST)
							extremum = Min(extremum, dval);
						else
							extremum = Max(extremum, dval);
					}
					setDoubleValue(retval, extremum);
				}
				else
				{
					int64		extremum;

					if (!coerceToInt(&vargs[0], &extremum))
						return false;
					for (i = 1; i < nargs; i++)
					{
						int64		ival;

						if (!coerceToInt(&vargs[i], &ival))
							return false;
						if (func == PGBENCH_LEAST)
							extremum = Min(extremum, ival);
						else
							extremum = Max(extremum, ival);
					}
					setIntValue(retval, extremum);
				}
				return true;
			}

			/* random functions */
		case PGBENCH_RANDOM:
		case PGBENCH_RANDOM_EXPONENTIAL:
		case PGBENCH_RANDOM_GAUSSIAN:
		case PGBENCH_RANDOM_ZIPFIAN:
			{
				int64		imin,
							imax;

				Assert(nargs >= 2);

				if (!coerceToInt(&vargs[0], &imin) ||
					!coerceToInt(&vargs[1], &imax))
					return false;

				/* check random range */
				if (imin > imax)
				{
					ereport(ELEVEL_LOG_CLIENT_FAIL,
							(errmsg("empty range given to random\n")));
					return false;
				}
				else if (imax - imin < 0 || (imax - imin) + 1 < 0)
				{
					/* prevent int overflows in random functions */
					ereport(ELEVEL_LOG_CLIENT_FAIL,
							(errmsg("random range is too large\n")));
					return false;
				}

				if (func == PGBENCH_RANDOM)
				{
					Assert(nargs == 2);
					setIntValue(retval, getrand(&st->random_state, imin, imax));
				}
				else			/* gaussian & exponential */
				{
					double		param;

					Assert(nargs == 3);

					if (!coerceToDouble(&vargs[2], &param))
						return false;

					if (func == PGBENCH_RANDOM_GAUSSIAN)
					{
						if (param < MIN_GAUSSIAN_PARAM)
						{
							ereport(ELEVEL_LOG_CLIENT_FAIL,
									(errmsg("gaussian parameter must be at least %f (not %f)\n",
											MIN_GAUSSIAN_PARAM, param)));
							return false;
						}

						setIntValue(retval,
									getGaussianRand(&st->random_state, imin,
													imax, param));
					}
					else if (func == PGBENCH_RANDOM_ZIPFIAN)
					{
						if (param <= 0.0 || param == 1.0 || param > MAX_ZIPFIAN_PARAM)
						{
							ereport(ELEVEL_LOG_CLIENT_FAIL,
									(errmsg("zipfian parameter must be in range (0, 1) U (1, %d] (got %f)\n",
											MAX_ZIPFIAN_PARAM, param)));
							return false;
						}
						setIntValue(retval,
									getZipfianRand(thread, &st->random_state,
												   imin, imax, param));
					}
					else		/* exponential */
					{
						if (param <= 0.0)
						{
							ereport(ELEVEL_LOG_CLIENT_FAIL,
									(errmsg("exponential parameter must be greater than zero (got %f)\n",
											param)));
							return false;
						}

						setIntValue(retval,
									getExponentialRand(&st->random_state, imin,
													   imax, param));
					}
				}

				return true;
			}

		case PGBENCH_POW:
			{
				PgBenchValue *lval = &vargs[0];
				PgBenchValue *rval = &vargs[1];
				double		ld,
							rd;

				Assert(nargs == 2);

				if (!coerceToDouble(lval, &ld) ||
					!coerceToDouble(rval, &rd))
					return false;

				setDoubleValue(retval, pow(ld, rd));

				return true;
			}

		case PGBENCH_IS:
			{
				Assert(nargs == 2);

				/*
				 * note: this simple implementation is more permissive than
				 * SQL
				 */
				setBoolValue(retval,
							 vargs[0].type == vargs[1].type &&
							 vargs[0].u.bval == vargs[1].u.bval);
				return true;
			}

			/* hashing */
		case PGBENCH_HASH_FNV1A:
		case PGBENCH_HASH_MURMUR2:
			{
				int64		val,
							seed;

				Assert(nargs == 2);

				if (!coerceToInt(&vargs[0], &val) ||
					!coerceToInt(&vargs[1], &seed))
					return false;

				if (func == PGBENCH_HASH_MURMUR2)
					setIntValue(retval, getHashMurmur2(val, seed));
				else if (func == PGBENCH_HASH_FNV1A)
					setIntValue(retval, getHashFnv1a(val, seed));
				else
					/* cannot get here */
					Assert(0);

				return true;
			}

		default:
			/* cannot get here */
			Assert(0);
			/* dead code to avoid a compiler warning */
			return false;
	}
}

/* evaluate some function */
static bool
evalFunc(TState *thread, CState *st,
		 PgBenchFunction func, PgBenchExprLink *args, PgBenchValue *retval)
{
	if (isLazyFunc(func))
		return evalLazyFunc(thread, st, func, args, retval);
	else
		return evalStandardFunc(thread, st, func, args, retval);
}

/*
 * Recursive evaluation of an expression in a pgbench script
 * using the current state of variables.
 * Returns whether the evaluation was ok,
 * the value itself is returned through the retval pointer.
 */
static bool
evaluateExpr(TState *thread, CState *st, PgBenchExpr *expr, PgBenchValue *retval)
{
	switch (expr->etype)
	{
		case ENODE_CONSTANT:
			{
				*retval = expr->u.constant;
				return true;
			}

		case ENODE_VARIABLE:
			{
				Variable   *var;

				if ((var = lookupVariable(&st->variables, expr->u.variable.varname)) == NULL)
				{
					ereport(ELEVEL_LOG_CLIENT_FAIL,
							(errmsg("undefined variable \"%s\"\n",
									expr->u.variable.varname)));
					return false;
				}

				if (!makeVariableValue(var))
					return false;

				*retval = var->value;
				return true;
			}

		case ENODE_FUNCTION:
			return evalFunc(thread, st,
							expr->u.function.function,
							expr->u.function.args,
							retval);

		default:
			/* internal error which should never occur */
			ereport(ELEVEL_FATAL,
					(errmsg("unexpected enode type in evaluation: %d\n",
							expr->etype)));
	}
}

/*
 * Convert command name to meta-command enum identifier
 */
static MetaCommand
getMetaCommand(const char *cmd)
{
	MetaCommand mc;

	if (cmd == NULL)
		mc = META_NONE;
	else if (pg_strcasecmp(cmd, "set") == 0)
		mc = META_SET;
	else if (pg_strcasecmp(cmd, "setshell") == 0)
		mc = META_SETSHELL;
	else if (pg_strcasecmp(cmd, "shell") == 0)
		mc = META_SHELL;
	else if (pg_strcasecmp(cmd, "sleep") == 0)
		mc = META_SLEEP;
	else if (pg_strcasecmp(cmd, "if") == 0)
		mc = META_IF;
	else if (pg_strcasecmp(cmd, "elif") == 0)
		mc = META_ELIF;
	else if (pg_strcasecmp(cmd, "else") == 0)
		mc = META_ELSE;
	else if (pg_strcasecmp(cmd, "endif") == 0)
		mc = META_ENDIF;
	else
		mc = META_NONE;
	return mc;
}

/*
 * Run a shell command. The result is assigned to the variable if not NULL.
 * Return true if succeeded, or false on error.
 */
static bool
runShellCommand(Variables *variables, char *variable, char **argv, int argc)
{
	char		command[SHELL_COMMAND_SIZE];
	int			i,
				len = 0;
	FILE	   *fp;
	char		res[64];
	char	   *endptr;
	int			retval;

	/*----------
	 * Join arguments with whitespace separators. Arguments starting with
	 * exactly one colon are treated as variables:
	 *	name - append a string "name"
	 *	:var - append a variable named 'var'
	 *	::name - append a string ":name"
	 *----------
	 */
	for (i = 0; i < argc; i++)
	{
		char	   *arg;
		int			arglen;

		if (argv[i][0] != ':')
		{
			arg = argv[i];		/* a string literal */
		}
		else if (argv[i][1] == ':')
		{
			arg = argv[i] + 1;	/* a string literal starting with colons */
		}
		else if ((arg = getVariable(variables, argv[i] + 1)) == NULL)
		{
			ereport(ELEVEL_LOG_CLIENT_FAIL,
					(errmsg("%s: undefined variable \"%s\"\n",
							argv[0], argv[i])));
			return false;
		}

		arglen = strlen(arg);
		if (len + arglen + (i > 0 ? 1 : 0) >= SHELL_COMMAND_SIZE - 1)
		{
			ereport(ELEVEL_LOG_CLIENT_FAIL,
					(errmsg("%s: shell command is too long\n", argv[0])));
			return false;
		}

		if (i > 0)
			command[len++] = ' ';
		memcpy(command + len, arg, arglen);
		len += arglen;
	}

	command[len] = '\0';

	/* Fast path for non-assignment case */
	if (variable == NULL)
	{
		if (system(command))
		{
			if (!timer_exceeded)
			{
				ereport(ELEVEL_LOG_CLIENT_FAIL,
						(errmsg("%s: could not launch shell command\n",
								argv[0])));
			}
			return false;
		}
		return true;
	}

	/* Execute the command with pipe and read the standard output. */
	if ((fp = popen(command, "r")) == NULL)
	{
		ereport(ELEVEL_LOG_CLIENT_FAIL,
				(errmsg("%s: could not launch shell command\n", argv[0])));
		return false;
	}
	if (fgets(res, sizeof(res), fp) == NULL)
	{
		if (!timer_exceeded)
		{
			ereport(ELEVEL_LOG_CLIENT_FAIL,
					(errmsg("%s: could not read result of shell command\n",
							argv[0])));
		}
		(void) pclose(fp);
		return false;
	}
	if (pclose(fp) < 0)
	{
		ereport(ELEVEL_LOG_CLIENT_FAIL,
				(errmsg("%s: could not close shell command\n", argv[0])));
		return false;
	}

	/* Check whether the result is an integer and assign it to the variable */
	retval = (int) strtol(res, &endptr, 10);
	while (*endptr != '\0' && isspace((unsigned char) *endptr))
		endptr++;
	if (*res == '\0' || *endptr != '\0')
	{
		ereport(ELEVEL_LOG_CLIENT_FAIL,
				(errmsg("%s: shell command must return an integer (not \"%s\")\n",
						argv[0], res)));
		return false;
	}
	if (!putVariableInt(variables, "setshell", variable, retval, true))
		return false;

#ifdef DEBUG
	printf("shell parameter name: \"%s\", value: \"%s\"\n", argv[1], res);
#endif
	return true;
}

#define MAX_PREPARE_NAME		32
static void
preparedStatementName(char *buffer, int file, int state)
{
	sprintf(buffer, "P%d_%d", file, state);
}

static void
commandFailed(CState *st, const char *cmd, const char *message,
			  ErrorLevel elevel)
{
	switch (elevel)
	{
		case ELEVEL_LOG_CLIENT_FAIL:
			if (st->first_failure.status == NO_FAILURE)
			{
				/*
				 * This is the first failure during the execution of the current
				 * script.
				 */
				ereport(ELEVEL_LOG_CLIENT_FAIL,
						(errmsg("client %d got a failure in command %d (%s) of script %d; %s\n",
								st->id, st->command, cmd, st->use_file,
								message)));
			}
			else
			{
				/*
				 * This is not the first failure during the execution of the
				 * current script.
				 */
				ereport(ELEVEL_LOG_CLIENT_FAIL,
						(errmsg("client %d continues a failed transaction in command %d (%s) of script %d; %s\n",
								st->id, st->command, cmd, st->use_file,
								message)));
			}
			break;
		case ELEVEL_LOG_CLIENT_ABORTED:
			ereport(ELEVEL_LOG_CLIENT_ABORTED,
					(errmsg("client %d aborted in command %d (%s) of script %d; %s\n",
							st->id, st->command, cmd, st->use_file, message)));
			break;
		case ELEVEL_DEBUG:
		case ELEVEL_LOG_MAIN:
		case ELEVEL_FATAL:
		default:
			/* internal error which should never occur */
			ereport(ELEVEL_FATAL,
					(errmsg("unexpected error level when the command failed: %d\n",
							elevel)));
			break;
	}
}

/* return a script number with a weighted choice. */
static int
chooseScript(TState *thread)
{
	int			i = 0;
	int64		w;

	if (num_scripts == 1)
		return 0;

	w = getrand(&thread->random_state, 0, total_weight - 1);
	do
	{
		w -= sql_script[i++].weight;
	} while (w >= 0);

	return i - 1;
}

/* Send a SQL command, using the chosen querymode */
static bool
sendCommand(CState *st, Command *command)
{
	int			r;

	if (querymode == QUERY_SIMPLE)
	{
		char	   *sql;

		sql = pg_strdup(command->argv[0]);
		sql = assignVariables(&st->variables, sql);

		ereport(ELEVEL_DEBUG, (errmsg("client %d sending %s\n", st->id, sql)));
		r = PQsendQuery(st->con, sql);
		free(sql);
	}
	else if (querymode == QUERY_EXTENDED)
	{
		const char *sql = command->argv[0];
		const char *params[MAX_ARGS];

		getQueryParams(&st->variables, command, params);

		ereport(ELEVEL_DEBUG, (errmsg("client %d sending %s\n", st->id, sql)));
		r = PQsendQueryParams(st->con, sql, command->argc - 1,
							  NULL, params, NULL, NULL, 0);
	}
	else if (querymode == QUERY_PREPARED)
	{
		char		name[MAX_PREPARE_NAME];
		const char *params[MAX_ARGS];

		if (!st->prepared[st->use_file])
		{
			int			j;
			Command   **commands = sql_script[st->use_file].commands;

			for (j = 0; commands[j] != NULL; j++)
			{
				PGresult   *res;
				char		name[MAX_PREPARE_NAME];

				if (commands[j]->type != SQL_COMMAND)
					continue;
				preparedStatementName(name, st->use_file, j);
				res = PQprepare(st->con, name,
								commands[j]->argv[0], commands[j]->argc - 1, NULL);
				if (PQresultStatus(res) != PGRES_COMMAND_OK)
				{
					ereport(ELEVEL_LOG_MAIN,
							(errmsg("%s", PQerrorMessage(st->con))));
				}
				PQclear(res);
			}
			st->prepared[st->use_file] = true;
		}

		getQueryParams(&st->variables, command, params);
		preparedStatementName(name, st->use_file, st->command);

		ereport(ELEVEL_DEBUG, (errmsg("client %d sending %s\n", st->id, name)));
		r = PQsendQueryPrepared(st->con, name, command->argc - 1,
								params, NULL, NULL, 0);
	}
	else						/* unknown sql mode */
		r = 0;

	if (r == 0)
	{
		ereport(ELEVEL_DEBUG,
				(errmsg("client %d could not send %s\n",
						st->id, command->argv[0])));
		return false;
	}
	else
		return true;
}

/*
 * Parse the argument to a \sleep command, and return the requested amount
 * of delay, in microseconds.  Returns true on success, false on error.
 */
static bool
evaluateSleep(Variables *variables, int argc, char **argv, int *usecs)
{
	char	   *var;
	int			usec;

	if (*argv[1] == ':')
	{
		if ((var = getVariable(variables, argv[1] + 1)) == NULL)
		{
			ereport(ELEVEL_LOG_CLIENT_FAIL,
					(errmsg("%s: undefined variable \"%s\"\n",
							argv[0], argv[1])));
			return false;
		}
		usec = atoi(var);
	}
	else
		usec = atoi(argv[1]);

	if (argc > 2)
	{
		if (pg_strcasecmp(argv[2], "ms") == 0)
			usec *= 1000;
		else if (pg_strcasecmp(argv[2], "s") == 0)
			usec *= 1000000;
	}
	else
		usec *= 1000000;

	*usecs = usec;
	return true;
}

/*
 * Get the number of all processed transactions including skipped ones and
 * errors.
 */
static int64
getTotalCnt(const CState *st)
{
	return st->cnt + st->ecnt;
}

/*
 * Copy an array of random state.
 */
static void
copyRandomState(RandomState *destination, const RandomState *source)
{
	memcpy(destination->data, source->data, sizeof(unsigned short) * 3);
}

/*
 * Make a deep copy of variables array.
 */
static void
copyVariables(Variables *destination_vars, const Variables *source_vars)
{
	Variable   *destination;
	Variable   *current_destination;
	const Variable *source;
	const Variable *current_source;
	int			nvariables;

	if (!destination_vars || !source_vars)
		return;

	destination = destination_vars->array;
	source = source_vars->array;
	nvariables = source_vars->nvariables;

	for (current_destination = destination;
		 current_destination - destination < destination_vars->nvariables;
		 ++current_destination)
	{
		pg_free(current_destination->name);
		pg_free(current_destination->svalue);
	}

	destination_vars->array = pg_realloc(destination_vars->array,
										 sizeof(Variable) * nvariables);
	destination = destination_vars->array;

	for (current_source = source, current_destination = destination;
		 current_source - source < nvariables;
		 ++current_source, ++current_destination)
	{
		current_destination->name = pg_strdup(current_source->name);
		if (current_source->svalue)
			current_destination->svalue = pg_strdup(current_source->svalue);
		else
			current_destination->svalue = NULL;
		current_destination->value = current_source->value;
	}

	destination_vars->nvariables = nvariables;
	destination_vars->vars_sorted = source_vars->vars_sorted;
}

/*
 * Returns true if this type of failure can be retried.
 */
static bool
canRetryFailure(FailureStatus failure_status)
{
	return (failure_status == SERIALIZATION_FAILURE ||
			failure_status == DEADLOCK_FAILURE);
}

/*
 * Returns true if the failure can be retried.
 */
static bool
canRetry(CState *st, instr_time *now)
{
	FailureStatus failure_status = st->first_failure.status;

	Assert(failure_status != NO_FAILURE);

	/* We can only retry serialization or deadlock failures. */
	if (!canRetryFailure(failure_status))
		return false;

	/*
	 * We must have at least one option to limit the retrying of failed
	 * transactions.
	 */
	Assert(max_tries || latency_limit);

	/*
	 * We cannot retry the failure if we have reached the maximum number of
	 * tries.
	 */
	if (max_tries && st->retries + 1 >= max_tries)
		return false;

	/*
	 * We cannot retry the failure if we spent too much time on this
	 * transaction.
	 */
	if (latency_limit)
	{
		if (INSTR_TIME_IS_ZERO(*now))
			INSTR_TIME_SET_CURRENT(*now);

		if (INSTR_TIME_GET_MICROSEC(*now) - st->txn_scheduled >= latency_limit)
			return false;
	}

	/* OK */
	return true;
}

/*
 * Process the conditional stack depending on the condition value; is used for
 * the meta commands \if and \elif.
 */
static void
executeCondition(CState *st, bool condition)
{
	Command    *command = sql_script[st->use_file].commands[st->command];

	/* execute or not depending on evaluated condition */
	if (command->meta == META_IF)
	{
		conditional_stack_push(st->cstack,
							   condition ? IFSTATE_TRUE : IFSTATE_FALSE);
	}
	else if (command->meta == META_ELIF)
	{
		/* we should get here only if the "elif" needed evaluation */
		Assert(conditional_stack_peek(st->cstack) == IFSTATE_FALSE);
		conditional_stack_poke(st->cstack,
							   condition ? IFSTATE_TRUE : IFSTATE_FALSE);
	}
}

/*
 * Get the failure status from the error code.
 */
static FailureStatus
getFailureStatus(char *sqlState)
{
	if (sqlState)
	{
		if (strcmp(sqlState, ERRCODE_T_R_SERIALIZATION_FAILURE) == 0)
			return SERIALIZATION_FAILURE;
		else if (strcmp(sqlState, ERRCODE_T_R_DEADLOCK_DETECTED) == 0)
			return DEADLOCK_FAILURE;
		else if (strcmp(sqlState, ERRCODE_IN_FAILED_SQL_TRANSACTION) == 0)
			return IN_FAILED_SQL_TRANSACTION;
	}

	return ANOTHER_FAILURE;
}

/*
 * If the latency limit is used, return a percentage of the current transaction
 * latency from the latency limit. Otherwise return zero.
 */
static double
getLatencyUsed(CState *st, instr_time *now)
{
	if (!latency_limit)
		return 0;

	if (INSTR_TIME_IS_ZERO(*now))
		INSTR_TIME_SET_CURRENT(*now);

	return (100.0 * (INSTR_TIME_GET_MICROSEC(*now) - st->txn_scheduled) /
			latency_limit);
}

/*
 * Advance the state machine of a connection, if possible.
 */
static void
doCustom(TState *thread, CState *st, StatsData *agg)
{
	PGresult   *res;
	Command    *command;
	instr_time	now;
	bool		end_tx_processed = false;
	int64		wait;
	FailureStatus failure_status = NO_FAILURE;

	/*
	 * gettimeofday() isn't free, so we get the current timestamp lazily the
	 * first time it's needed, and reuse the same value throughout this
	 * function after that.  This also ensures that e.g. the calculated
	 * latency reported in the log file and in the totals are the same. Zero
	 * means "not set yet".  Reset "now" when we execute shell commands or
	 * expressions, which might take a non-negligible amount of time, though.
	 */
	INSTR_TIME_SET_ZERO(now);

	/*
	 * Loop in the state machine, until we have to wait for a result from the
	 * server (or have to sleep, for throttling or for \sleep).
	 *
	 * Note: In the switch-statement below, 'break' will loop back here,
	 * meaning "continue in the state machine".  Return is used to return to
	 * the caller.
	 */
	for (;;)
	{
		switch (st->state)
		{
				/*
				 * Select transaction to run.
				 */
			case CSTATE_CHOOSE_SCRIPT:

				st->use_file = chooseScript(thread);

				ereport(ELEVEL_DEBUG,
						(errmsg("client %d executing script \"%s\"\n",
								st->id, sql_script[st->use_file].desc)));

				if (throttle_delay > 0)
					st->state = CSTATE_START_THROTTLE;
				else
					st->state = CSTATE_START_TX;
				/* check consistency */
				Assert(conditional_stack_empty(st->cstack));

				/* reset transaction variables to default values */
				st->first_failure.status = NO_FAILURE;
				st->retries = 0;

				break;

				/*
				 * Handle throttling once per transaction by sleeping.
				 */
			case CSTATE_START_THROTTLE:

				/*
				 * Generate a delay such that the series of delays will
				 * approximate a Poisson distribution centered on the
				 * throttle_delay time.
				 *
				 * If transactions are too slow or a given wait is shorter
				 * than a transaction, the next transaction will start right
				 * away.
				 */
				Assert(throttle_delay > 0);
				wait = getPoissonRand(&thread->random_state, throttle_delay);

				thread->throttle_trigger += wait;
				st->txn_scheduled = thread->throttle_trigger;

				/*
				 * stop client if next transaction is beyond pgbench end of
				 * execution
				 */
				if (duration > 0 && st->txn_scheduled > end_time)
				{
					st->state = CSTATE_FINISHED;
					break;
				}

				/*
				 * If --latency-limit is used, and this slot is already late
				 * so that the transaction will miss the latency limit even if
				 * it completed immediately, we skip this time slot and
				 * iterate till the next slot that isn't late yet.  But don't
				 * iterate beyond the -t limit, if one is given.
				 */
				if (latency_limit)
				{
					int64		now_us;

					if (INSTR_TIME_IS_ZERO(now))
						INSTR_TIME_SET_CURRENT(now);
					now_us = INSTR_TIME_GET_MICROSEC(now);
					while (thread->throttle_trigger < now_us - latency_limit &&
						   (nxacts <= 0 || getTotalCnt(st) < nxacts))
					{
						processXactStats(thread, st, &now, true, agg);
						/* next rendez-vous */
						wait = getPoissonRand(&thread->random_state,
											  throttle_delay);
						thread->throttle_trigger += wait;
						st->txn_scheduled = thread->throttle_trigger;
					}
					/* stop client if -t exceeded */
					if (nxacts > 0 && getTotalCnt(st) >= nxacts)
					{
						st->state = CSTATE_FINISHED;
						break;
					}
				}

				st->state = CSTATE_THROTTLE;
				ereport(ELEVEL_DEBUG,
						(errmsg("client %d throttling " INT64_FORMAT " us\n",
								st->id, wait)));
				break;

				/*
				 * Wait until it's time to start next transaction.
				 */
			case CSTATE_THROTTLE:
				if (INSTR_TIME_IS_ZERO(now))
					INSTR_TIME_SET_CURRENT(now);
				if (INSTR_TIME_GET_MICROSEC(now) < st->txn_scheduled)
					return;		/* Still sleeping, nothing to do here */

				/* Else done sleeping, start the transaction */
				st->state = CSTATE_START_TX;
				break;

				/* Start new transaction */
			case CSTATE_START_TX:

				/*
				 * Establish connection on first call, or if is_connect is
				 * true.
				 */
				if (st->con == NULL)
				{
					instr_time	start;

					if (INSTR_TIME_IS_ZERO(now))
						INSTR_TIME_SET_CURRENT(now);
					start = now;
					if ((st->con = doConnect()) == NULL)
					{
						ereport(ELEVEL_LOG_CLIENT_ABORTED,
								(errmsg("client %d aborted while establishing connection\n",
										st->id)));
						st->state = CSTATE_ABORTED;
						break;
					}
					INSTR_TIME_SET_CURRENT(now);
					INSTR_TIME_ACCUM_DIFF(thread->conn_time, now, start);

					/* Reset session-local state */
					memset(st->prepared, 0, sizeof(st->prepared));
				}

				/*
				 * It is the first try to run this transaction. Remember its
				 * parameters just in case if it fails or we should repeat it in
				 * future.
				 */
				copyRandomState(&st->retry_state.random_state,
								&st->random_state);
				copyVariables(&st->retry_state.variables, &st->variables);

				/*
				 * Record transaction start time under logging, progress or
				 * throttling.
				 */
				if (use_log || progress || throttle_delay || latency_limit ||
					per_script_stats)
				{
					if (INSTR_TIME_IS_ZERO(now))
						INSTR_TIME_SET_CURRENT(now);
					st->txn_begin = now;

					/*
					 * When not throttling, this is also the transaction's
					 * scheduled start time.
					 */
					if (!throttle_delay)
						st->txn_scheduled = INSTR_TIME_GET_MICROSEC(now);
				}

				/* Begin with the first command */
				st->command = 0;
				st->state = CSTATE_START_COMMAND;
				break;

				/*
				 * Send a command to server (or execute a meta-command)
				 */
			case CSTATE_START_COMMAND:
				command = sql_script[st->use_file].commands[st->command];

				/*
				 * If we reached the end of the script, move to end-of-xact
				 * processing.
				 */
				if (command == NULL)
				{
					if (st->first_failure.status == NO_FAILURE)
					{
						st->state = CSTATE_END_TX;
					}
					else
					{
						/* check if we can retry the failure */
						st->state = CSTATE_RETRY;
					}
					break;
				}

				/*
				 * Record statement start time if per-command latencies are
				 * requested
				 */
				if (report_per_command)
				{
					if (INSTR_TIME_IS_ZERO(now))
						INSTR_TIME_SET_CURRENT(now);
					st->stmt_begin = now;
				}

				if (command->type == SQL_COMMAND)
				{
					if (!sendCommand(st, command))
					{
						commandFailed(st, "SQL", "SQL command send failed",
									  ELEVEL_LOG_CLIENT_ABORTED);
						st->state = CSTATE_ABORTED;
					}
					else
						st->state = CSTATE_WAIT_RESULT;
				}
				else if (command->type == META_COMMAND)
				{
					int			argc = command->argc,
								i;
					char	  **argv = command->argv;
					PQExpBufferData errmsg_buf;

					initPQExpBuffer(&errmsg_buf);
					printfPQExpBuffer(&errmsg_buf, "client %d executing \\%s",
									  st->id, argv[0]);
					for (i = 1; i < argc; i++)
						appendPQExpBuffer(&errmsg_buf, " %s", argv[i]);
					appendPQExpBufferChar(&errmsg_buf, '\n');
					ereport(ELEVEL_DEBUG, (errmsg("%s", errmsg_buf.data)));
					termPQExpBuffer(&errmsg_buf);

					/* change it if the meta command fails */
					failure_status = NO_FAILURE;

					if (command->meta == META_SLEEP)
					{
						/*
						 * A \sleep doesn't execute anything, we just get the
						 * delay from the argument, and enter the CSTATE_SLEEP
						 * state.  (The per-command latency will be recorded
						 * in CSTATE_SLEEP state, not here, after the delay
						 * has elapsed.)
						 */
						int			usec;

						if (!evaluateSleep(&st->variables, argc, argv, &usec))
						{
							commandFailed(st, "sleep",
										  "execution of meta-command failed",
										  ELEVEL_LOG_CLIENT_FAIL);
							failure_status = ANOTHER_FAILURE;
							st->state = CSTATE_FAILURE;
							break;
						}

						if (INSTR_TIME_IS_ZERO(now))
							INSTR_TIME_SET_CURRENT(now);
						st->sleep_until = INSTR_TIME_GET_MICROSEC(now) + usec;
						st->state = CSTATE_SLEEP;
						break;
					}
					else if (command->meta == META_SET ||
							 command->meta == META_IF ||
							 command->meta == META_ELIF)
					{
						/* backslash commands with an expression to evaluate */
						PgBenchExpr *expr = command->expr;
						PgBenchValue result;

						if (command->meta == META_ELIF &&
							conditional_stack_peek(st->cstack) == IFSTATE_TRUE)
						{
							/*
							 * elif after executed block, skip eval and wait
							 * for endif
							 */
							conditional_stack_poke(st->cstack, IFSTATE_IGNORED);
							goto move_to_end_command;
						}

						if (!evaluateExpr(thread, st, expr, &result))
						{
							commandFailed(st, argv[0],
										  "evaluation of meta-command failed",
										  ELEVEL_LOG_CLIENT_FAIL);

							/*
							 * Do not ruin the following conditional commands,
							 * if any.
							 */
							executeCondition(st, false);

							failure_status = ANOTHER_FAILURE;
							st->state = CSTATE_FAILURE;
							break;
						}

						if (command->meta == META_SET)
						{
							if (!putVariableValue(&st->variables,  argv[0],
												  argv[1], &result, true))
							{
								commandFailed(st, "set",
											  "assignment of meta-command failed",
											  ELEVEL_LOG_CLIENT_FAIL);
								failure_status = ANOTHER_FAILURE;
								st->state = CSTATE_FAILURE;
								break;
							}
						}
						else	/* if and elif evaluated cases */
						{
							executeCondition(st, valueTruth(&result));
						}
					}
					else if (command->meta == META_ELSE)
					{
						switch (conditional_stack_peek(st->cstack))
						{
							case IFSTATE_TRUE:
								conditional_stack_poke(st->cstack, IFSTATE_ELSE_FALSE);
								break;
							case IFSTATE_FALSE: /* inconsistent if active */
							case IFSTATE_IGNORED:	/* inconsistent if active */
							case IFSTATE_NONE:	/* else without if */
							case IFSTATE_ELSE_TRUE: /* else after else */
							case IFSTATE_ELSE_FALSE:	/* else after else */
							default:
								/* dead code if conditional check is ok */
								Assert(false);
						}
						goto move_to_end_command;
					}
					else if (command->meta == META_ENDIF)
					{
						Assert(!conditional_stack_empty(st->cstack));
						conditional_stack_pop(st->cstack);
						goto move_to_end_command;
					}
					else if (command->meta == META_SETSHELL)
					{
						bool		ret = runShellCommand(&st->variables,
														  argv[1], argv + 2,
														  argc - 2);

						if (timer_exceeded) /* timeout */
						{
							st->state = CSTATE_FINISHED;
							break;
						}
						else if (!ret)	/* on error */
						{
							commandFailed(st, "setshell",
										  "execution of meta-command failed",
										  ELEVEL_LOG_CLIENT_FAIL);
							failure_status = ANOTHER_FAILURE;
							st->state = CSTATE_FAILURE;
							break;
						}
						else
						{
							/* succeeded */
						}
					}
					else if (command->meta == META_SHELL)
					{
						bool		ret = runShellCommand(&st->variables, NULL,
														  argv + 1, argc - 1);

						if (timer_exceeded) /* timeout */
						{
							st->state = CSTATE_FINISHED;
							break;
						}
						else if (!ret)	/* on error */
						{
							commandFailed(st, "shell",
										  "execution of meta-command failed",
										  ELEVEL_LOG_CLIENT_FAIL);
							failure_status = ANOTHER_FAILURE;
							st->state = CSTATE_FAILURE;
							break;
						}
						else
						{
							/* succeeded */
						}
					}

			move_to_end_command:

					/*
					 * executing the expression or shell command might take a
					 * non-negligible amount of time, so reset 'now'
					 */
					INSTR_TIME_SET_ZERO(now);

					st->state = CSTATE_END_COMMAND;
				}
				break;

				/*
				 * non executed conditional branch
				 */
			case CSTATE_SKIP_COMMAND:
				Assert(!conditional_active(st->cstack));
				/* quickly skip commands until something to do... */
				while (true)
				{
					command = sql_script[st->use_file].commands[st->command];

					/* cannot reach end of script in that state */
					Assert(command != NULL);

					/*
					 * if this is conditional related, update conditional
					 * state
					 */
					if (command->type == META_COMMAND &&
						(command->meta == META_IF ||
						 command->meta == META_ELIF ||
						 command->meta == META_ELSE ||
						 command->meta == META_ENDIF))
					{
						switch (conditional_stack_peek(st->cstack))
						{
							case IFSTATE_FALSE:
								if (command->meta == META_IF || command->meta == META_ELIF)
								{
									/* we must evaluate the condition */
									st->state = CSTATE_START_COMMAND;
								}
								else if (command->meta == META_ELSE)
								{
									/* we must execute next command */
									conditional_stack_poke(st->cstack, IFSTATE_ELSE_TRUE);
									st->state = CSTATE_START_COMMAND;
									st->command++;
								}
								else if (command->meta == META_ENDIF)
								{
									Assert(!conditional_stack_empty(st->cstack));
									conditional_stack_pop(st->cstack);
									if (conditional_active(st->cstack))
										st->state = CSTATE_START_COMMAND;

									/*
									 * else state remains in
									 * CSTATE_SKIP_COMMAND
									 */
									st->command++;
								}
								break;

							case IFSTATE_IGNORED:
							case IFSTATE_ELSE_FALSE:
								if (command->meta == META_IF)
									conditional_stack_push(st->cstack, IFSTATE_IGNORED);
								else if (command->meta == META_ENDIF)
								{
									Assert(!conditional_stack_empty(st->cstack));
									conditional_stack_pop(st->cstack);
									if (conditional_active(st->cstack))
										st->state = CSTATE_START_COMMAND;
								}
								/* could detect "else" & "elif" after "else" */
								st->command++;
								break;

							case IFSTATE_NONE:
							case IFSTATE_TRUE:
							case IFSTATE_ELSE_TRUE:
							default:

								/*
								 * inconsistent if inactive, unreachable dead
								 * code
								 */
								Assert(false);
						}
					}
					else
					{
						/* skip and consider next */
						st->command++;
					}

					if (st->state != CSTATE_SKIP_COMMAND)
						break;
				}
				break;

				/*
				 * Wait for the current SQL command to complete
				 */
			case CSTATE_WAIT_RESULT:
				{
					char	   *sqlState;

					command = sql_script[st->use_file].commands[st->command];
					ereport(ELEVEL_DEBUG,
							(errmsg("client %d receiving\n", st->id)));
					if (!PQconsumeInput(st->con))
					{				/* there's something wrong */
						commandFailed(st, "SQL",
									  "perhaps the backend died while processing",
									  ELEVEL_LOG_CLIENT_ABORTED);
						st->state = CSTATE_ABORTED;
						break;
					}
					if (PQisBusy(st->con))
						return;		/* don't have the whole result yet */

					/*
					 * Read and discard the query result;
					 */
					res = PQgetResult(st->con);
					sqlState = PQresultErrorField(res, PG_DIAG_SQLSTATE);
					switch (PQresultStatus(res))
					{
						case PGRES_COMMAND_OK:
						case PGRES_TUPLES_OK:
						case PGRES_EMPTY_QUERY:
							/* OK */
							PQclear(res);
							discard_response(st);
							failure_status = NO_FAILURE;
							st->state = CSTATE_END_COMMAND;
							break;
						case PGRES_NONFATAL_ERROR:
						case PGRES_FATAL_ERROR:
							failure_status = getFailureStatus(sqlState);
							commandFailed(st, "SQL", PQerrorMessage(st->con),
										  ELEVEL_LOG_CLIENT_FAIL);
							PQclear(res);
							discard_response(st);
							st->state = CSTATE_FAILURE;
							break;
						default:
							commandFailed(st, "SQL", PQerrorMessage(st->con),
										  ELEVEL_LOG_CLIENT_ABORTED);
							PQclear(res);
							st->state = CSTATE_ABORTED;
							break;
					}
				}
				break;

				/*
				 * Wait until sleep is done. This state is entered after a
				 * \sleep metacommand. The behavior is similar to
				 * CSTATE_THROTTLE, but proceeds to CSTATE_START_COMMAND
				 * instead of CSTATE_START_TX.
				 */
			case CSTATE_SLEEP:
				if (INSTR_TIME_IS_ZERO(now))
					INSTR_TIME_SET_CURRENT(now);
				if (INSTR_TIME_GET_MICROSEC(now) < st->sleep_until)
					return;		/* Still sleeping, nothing to do here */
				/* Else done sleeping. */
				st->state = CSTATE_END_COMMAND;
				break;

				/*
				 * End of command: record stats and proceed to next command.
				 */
			case CSTATE_END_COMMAND:

				/*
				 * command completed: accumulate per-command execution times
				 * in thread-local data structure, if per-command latencies
				 * are requested.
				 */
				if (report_per_command)
				{
					if (INSTR_TIME_IS_ZERO(now))
						INSTR_TIME_SET_CURRENT(now);

					/* XXX could use a mutex here, but we choose not to */
					command = sql_script[st->use_file].commands[st->command];
					addToSimpleStats(&command->stats,
									 INSTR_TIME_GET_DOUBLE(now) -
									 INSTR_TIME_GET_DOUBLE(st->stmt_begin));
				}

				/* Go ahead with next command, to be executed or skipped */
				st->command++;
				st->state = conditional_active(st->cstack) ?
					CSTATE_START_COMMAND : CSTATE_SKIP_COMMAND;
				break;

				/*
				 * Remember the failure and go ahead with next command.
				 */
			case CSTATE_FAILURE:

				Assert(failure_status != NO_FAILURE);

				/*
				 * All subsequent failures will be "retried"/"failed" if the
				 * first failure of this transaction can be/cannot be retried.
				 * Therefore remember only the first failure.
				 */
				if (st->first_failure.status == NO_FAILURE)
				{
					st->first_failure.status = failure_status;
					st->first_failure.command = st->command;
				}

				/* Go ahead with next command, to be executed or skipped */
				st->command++;
				st->state = conditional_active(st->cstack) ?
					CSTATE_START_COMMAND : CSTATE_SKIP_COMMAND;
				break;

			/*
			 * Retry the failed transaction if possible.
			 */
			case CSTATE_RETRY:
				{
					PQExpBufferData errmsg_buf;

					command = sql_script[st->use_file].commands[st->first_failure.command];

					if (canRetry(st, &now))
					{
						/*
						 * The failed transaction will be retried. So accumulate
						 * the retry.
						 */
						st->retries++;
						command->retries++;

						/*
						 * Report this with failures to indicate that the failed
						 * transaction will be retried.
						 */
						initPQExpBuffer(&errmsg_buf);
						printfPQExpBuffer(&errmsg_buf,
										  "client %d repeats the failed transaction (try %d",
										  st->id, st->retries + 1);
						if (max_tries)
							appendPQExpBuffer(&errmsg_buf, "/%d", max_tries);
						if (latency_limit)
						{
							appendPQExpBuffer(&errmsg_buf,
											  ", %.3f%% of the maximum time of tries was used",
											  getLatencyUsed(st, &now));
						}
						appendPQExpBufferStr(&errmsg_buf, ")\n");
						ereport(ELEVEL_LOG_CLIENT_FAIL,
								(errmsg("%s", errmsg_buf.data)));
						termPQExpBuffer(&errmsg_buf);

						/*
						 * Reset the execution parameters as they were at the
						 * beginning of the transaction.
						 */
						copyRandomState(&st->random_state,
										&st->retry_state.random_state);
						copyVariables(&st->variables, &st->retry_state.variables);

						/* Process the first transaction command */
						st->command = 0;
						st->first_failure.status = NO_FAILURE;
						st->state = CSTATE_START_COMMAND;
					}
					else
					{
						/*
						 * We will not be able to retry this failed transaction.
						 * So accumulate the error.
						 */
						command->errors++;
						if (st->first_failure.status ==
							IN_FAILED_SQL_TRANSACTION)
							command->errors_in_failed_tx++;

						/*
						 * Report this with failures to indicate that the failed
						 * transaction will not be retried.
						 */
						initPQExpBuffer(&errmsg_buf);
						printfPQExpBuffer(&errmsg_buf,
										  "client %d ends the failed transaction (try %d",
										  st->id, st->retries + 1);

						/*
						 * Report the actual number and/or time of tries. We do
						 * not need this information if this type of failure can
						 * be never retried.
						 */
						if (canRetryFailure(st->first_failure.status))
						{
							if (max_tries)
							{
								appendPQExpBuffer(&errmsg_buf, "/%d",
												  max_tries);
							}
							if (latency_limit)
							{
								appendPQExpBuffer(&errmsg_buf,
												  ", %.3f%% of the maximum time of tries was used",
												  getLatencyUsed(st, &now));
							}
						}
						appendPQExpBufferStr(&errmsg_buf, ")\n");
						ereport(ELEVEL_LOG_CLIENT_FAIL,
								(errmsg("%s", errmsg_buf.data)));
						termPQExpBuffer(&errmsg_buf);

						/*
						 * Reset the execution parameters as they were at the
						 * beginning of the transaction except for a random
						 * state.
						 */
						copyVariables(&st->variables, &st->retry_state.variables);

						/* End the failed transaction */
						st->state = CSTATE_END_TX;
					}
				}
				break;

				/*
				 * End of transaction.
				 */
			case CSTATE_END_TX:

				/* transaction finished: calculate latency and do log */
				processXactStats(thread, st, &now, false, agg);

				/* conditional stack must be empty */
				if (!conditional_stack_empty(st->cstack))
				{
					ereport(ELEVEL_FATAL,
							(errmsg("end of script reached within a conditional, missing \\endif\n")));
				}

				if (is_connect)
				{
					finishCon(st);
					INSTR_TIME_SET_ZERO(now);
				}

				if ((getTotalCnt(st) >= nxacts && duration <= 0) ||
					timer_exceeded)
				{
					/* exit success */
					st->state = CSTATE_FINISHED;
					break;
				}

				/*
				 * No transaction is underway anymore.
				 */
				st->state = CSTATE_CHOOSE_SCRIPT;

				/*
				 * If we paced through all commands in the script in this
				 * loop, without returning to the caller even once, do it now.
				 * This gives the thread a chance to process other
				 * connections, and to do progress reporting.  This can
				 * currently only happen if the script consists entirely of
				 * meta-commands.
				 */
				if (end_tx_processed)
					return;
				else
				{
					end_tx_processed = true;
					break;
				}

				/*
				 * Final states.  Close the connection if it's still open.
				 */
			case CSTATE_ABORTED:
			case CSTATE_FINISHED:
				finishCon(st);
				return;
		}
	}
}

/*
 * Print log entry after completing one transaction.
 *
 * We print Unix-epoch timestamps in the log, so that entries can be
 * correlated against other logs.  On some platforms this could be obtained
 * from the instr_time reading the caller has, but rather than get entangled
 * with that, we just eat the cost of an extra syscall in all cases.
 */
static void
doLog(TState *thread, CState *st,
	  StatsData *agg, bool skipped, double latency, double lag)
{
	FILE	   *logfile = thread->logfile;

	Assert(use_log);

	/*
	 * Skip the log entry if sampling is enabled and this row doesn't belong
	 * to the random sample.
	 */
	if (sample_rate != 0.0 &&
		pg_erand48(thread->random_state.data) > sample_rate)
		return;

	/* should we aggregate the results or not? */
	if (agg_interval > 0)
	{
		/*
		 * Loop until we reach the interval of the current moment, and print
		 * any empty intervals in between (this may happen with very low tps,
		 * e.g. --rate=0.1).
		 */
		time_t		now = time(NULL);

		while (agg->start_time + agg_interval <= now)
		{
			/* print aggregated report to logfile */
			fprintf(logfile, "%ld " INT64_FORMAT " %.0f %.0f %.0f %.0f " INT64_FORMAT " " INT64_FORMAT,
					(long) agg->start_time,
					agg->cnt,
					agg->latency.sum,
					agg->latency.sum2,
					agg->latency.min,
					agg->latency.max,
					agg->errors,
					agg->errors_in_failed_tx);
			if (throttle_delay)
			{
				fprintf(logfile, " %.0f %.0f %.0f %.0f",
						agg->lag.sum,
						agg->lag.sum2,
						agg->lag.min,
						agg->lag.max);
				if (latency_limit)
					fprintf(logfile, " " INT64_FORMAT, agg->skipped);
			}
			if (max_tries > 1 || latency_limit)
				fprintf(logfile, " " INT64_FORMAT " " INT64_FORMAT,
						agg->retried,
						agg->retries);
			fputc('\n', logfile);

			/* reset data and move to next interval */
			initStats(agg, agg->start_time + agg_interval);
		}

		/* accumulate the current transaction */
		accumStats(agg, skipped, latency, lag, st->first_failure.status,
				   st->retries);
	}
	else
	{
		/* no, print raw transactions */
		struct timeval tv;

		gettimeofday(&tv, NULL);
		if (skipped)
			fprintf(logfile, "%d " INT64_FORMAT " skipped %d %ld %ld",
					st->id, getTotalCnt(st), st->use_file,
					(long) tv.tv_sec, (long) tv.tv_usec);
		else if (st->first_failure.status == NO_FAILURE)
			fprintf(logfile, "%d " INT64_FORMAT " %.0f %d %ld %ld",
					st->id, getTotalCnt(st), latency, st->use_file,
					(long) tv.tv_sec, (long) tv.tv_usec);
		else if (st->first_failure.status == IN_FAILED_SQL_TRANSACTION)
			fprintf(logfile, "%d " INT64_FORMAT " in_failed_tx %d %ld %ld",
					st->id, getTotalCnt(st), st->use_file,
					(long) tv.tv_sec, (long) tv.tv_usec);
		else
			fprintf(logfile, "%d " INT64_FORMAT " failed %d %ld %ld",
					st->id, getTotalCnt(st), st->use_file,
					(long) tv.tv_sec, (long) tv.tv_usec);

		if (throttle_delay)
			fprintf(logfile, " %.0f", lag);
		if (max_tries > 1 || latency_limit)
			fprintf(logfile, " %d", st->retries);
		fputc('\n', logfile);
	}
}

/*
 * Accumulate and report statistics at end of a transaction.
 *
 * (This is also called when a transaction is late and thus skipped.
 * Note that even skipped transactions are counted in the "cnt" fields.)
 */
static void
processXactStats(TState *thread, CState *st, instr_time *now,
				 bool skipped, StatsData *agg)
{
	double		latency = 0.0,
				lag = 0.0;
	bool		thread_details = progress || throttle_delay || latency_limit,
				detailed = thread_details || use_log || per_script_stats;

	if (detailed && !skipped &&
		(st->first_failure.status == NO_FAILURE || latency_limit))
	{
		if (INSTR_TIME_IS_ZERO(*now))
			INSTR_TIME_SET_CURRENT(*now);

		/* compute latency & lag */
		latency = INSTR_TIME_GET_MICROSEC(*now) - st->txn_scheduled;
		lag = INSTR_TIME_GET_MICROSEC(st->txn_begin) - st->txn_scheduled;
	}

	if (thread_details)
	{
		/* keep detailed thread stats */
		accumStats(&thread->stats, skipped, latency, lag,
				   st->first_failure.status, st->retries);

		/* count transactions over the latency limit, if needed */
		if (latency_limit && latency > latency_limit)
			thread->latency_late++;
	}
	else
	{
		/* no detailed stats */
		accumStats(&thread->stats, skipped, 0, 0, st->first_failure.status,
				   st->retries);
	}

	/* client stat is just counting */
	if (st->first_failure.status == NO_FAILURE)
		st->cnt++;
	else
		st->ecnt++;

	if (use_log)
		doLog(thread, st, agg, skipped, latency, lag);

	/* XXX could use a mutex here, but we choose not to */
	if (per_script_stats)
		accumStats(&sql_script[st->use_file].stats, skipped, latency, lag,
				   st->first_failure.status, st->retries);
}


/* discard connections */
static void
disconnect_all(CState *state, int length)
{
	int			i;

	for (i = 0; i < length; i++)
		finishCon(&state[i]);
}

/*
 * Remove old pgbench tables, if any exist
 */
static void
initDropTables(PGconn *con)
{
	ereport(ELEVEL_LOG_MAIN, (errmsg("dropping old tables...\n")));

	/*
	 * We drop all the tables in one command, so that whether there are
	 * foreign key dependencies or not doesn't matter.
	 */
	executeStatement(con, "drop table if exists "
					 "ysql_bench_accounts, "
					 "ysql_bench_branches, "
					 "ysql_bench_history, "
					 "ysql_bench_tellers");
}

/*
 * Create pgbench's standard tables
 */
static void
initCreateTables(PGconn *con, bool use_primary_key)
{
	/*
	 * The scale factor at/beyond which 32-bit integers are insufficient for
	 * storing TPC-B account IDs.
	 *
	 * Although the actual threshold is 21474, we use 20000 because it is
	 * easier to document and remember, and isn't that far away from the real
	 * threshold.
	 */
#define SCALE_32BIT_THRESHOLD 20000

	/*
	 * Note: TPC-B requires at least 100 bytes per row, and the "filler"
	 * fields in these table declarations were intended to comply with that.
	 * The ysql_bench_accounts table complies with that because the "filler"
	 * column is set to blank-padded empty string. But for all other tables
	 * the columns default to NULL and so don't actually take any space.  We
	 * could fix that by giving them non-null default values.  However, that
	 * would completely break comparability of pgbench results with prior
	 * versions. Since pgbench has never pretended to be fully TPC-B compliant
	 * anyway, we stick with the historical behavior.
	 */
	struct ddlinfo
	{
		const char *table;		/* table name */
		const char *smcols;		/* column decls if accountIDs are 32 bits */
		const char *bigcols;	/* column decls if accountIDs are 64 bits */
		const char *pkey;     /* optional use primary key for the table */
		int			declare_fillfactor;
	};
	static const struct ddlinfo DDLs[] = {
		{
			"ysql_bench_history",
			"tid int,bid int,aid    int,delta int,mtime timestamp,filler char(22)",
			"tid int,bid int,aid bigint,delta int,mtime timestamp,filler char(22)",
			"",
			0
		},
		{
			"ysql_bench_tellers",
			"tid int not null,bid int,tbalance int,filler char(84)",
			"tid int not null,bid int,tbalance int,filler char(84)",
      ",PRIMARY KEY(tid)",
			1
		},
		{
			"ysql_bench_accounts",
			"aid    int not null,bid int,abalance int,filler char(84)",
			"aid bigint not null,bid int,abalance int,filler char(84)",
      ",PRIMARY KEY(aid)",
			1
		},
		{
			"ysql_bench_branches",
			"bid int not null,bbalance int,filler char(88)",
			"bid int not null,bbalance int,filler char(88)",
      ",PRIMARY KEY(bid)",
			1
		}
	};
	int			i;

	if (use_primary_key) {
		ereport(ELEVEL_LOG_MAIN, (errmsg("creating tables (with primary keys)...\n")));
	} else {
		ereport(ELEVEL_LOG_MAIN, (errmsg("creating tables...\n")));
	}

	for (i = 0; i < lengthof(DDLs); i++)
	{
		char		opts[256];
		char		buffer[256];
		const struct ddlinfo *ddl = &DDLs[i];
		const char *cols;

		/* Construct new create table statement. */
		opts[0] = '\0';
		if (ddl->declare_fillfactor && set_fillfactor)
			snprintf(opts + strlen(opts), sizeof(opts) - strlen(opts),
					 " with (fillfactor=%d)", fillfactor);
		if (tablespace != NULL)
		{
			char	   *escape_tablespace;

			escape_tablespace = PQescapeIdentifier(con, tablespace,
												   strlen(tablespace));
			snprintf(opts + strlen(opts), sizeof(opts) - strlen(opts),
					 " tablespace %s", escape_tablespace);
			PQfreemem(escape_tablespace);
		}

		cols = (scale >= SCALE_32BIT_THRESHOLD) ? ddl->bigcols : ddl->smcols;

		snprintf(buffer, sizeof(buffer), "create%s table %s(%s%s)%s",
				 unlogged_tables ? " unlogged" : "",
				 ddl->table, cols,
				 use_primary_key ? ddl->pkey : "",
				 opts);

		executeStatement(con, buffer);
	}
}

/*
 * Fill the standard tables with some data
 */
static void
initGenerateData(PGconn *con)
{
	char		sql[256];
	PGresult   *res;
	int			i;
	int64		k;

	/* used to track elapsed time and estimate of the remaining time */
	instr_time	start,
				diff;
	double		elapsed_sec,
				remaining_sec;
	int			log_interval = 1;

	ereport(ELEVEL_LOG_MAIN, (errmsg("generating data...\n")));

	/*
	 * we do all of this in one transaction to enable the backend's
	 * data-loading optimizations
	 */
	executeStatement(con, "begin");

	/*
	 * truncate away any old data, in one command in case there are foreign
	 * keys
	 */
	executeStatement(con, "truncate table "
					 "ysql_bench_accounts, "
					 "ysql_bench_branches, "
					 "ysql_bench_history, "
					 "ysql_bench_tellers");

	/*
	 * fill branches, tellers, accounts in that order in case foreign keys
	 * already exist
	 */
	for (i = 0; i < nbranches * scale; i++)
	{
		/* "filler" column defaults to NULL */
		snprintf(sql, sizeof(sql),
				 "insert into ysql_bench_branches(bid,bbalance) values(%d,0)",
				 i + 1);
		executeStatement(con, sql);
	}

	for (i = 0; i < ntellers * scale; i++)
	{
		/* "filler" column defaults to NULL */
		snprintf(sql, sizeof(sql),
				 "insert into ysql_bench_tellers(tid,bid,tbalance) values (%d,%d,0)",
				 i + 1, i / ntellers + 1);
		executeStatement(con, sql);
	}

	/*
	 * accounts is big enough to be worth using COPY and tracking runtime
	 */
	res = PQexec(con, "copy ysql_bench_accounts from stdin");
	if (PQresultStatus(res) != PGRES_COPY_IN)
		ereport(ELEVEL_FATAL, (errmsg("%s", PQerrorMessage(con))));
	PQclear(res);

	INSTR_TIME_SET_CURRENT(start);

	for (k = 0; k < (int64) naccounts * scale; k++)
	{
		int64		j = k + 1;

		/* "filler" column defaults to blank padded empty string */
		snprintf(sql, sizeof(sql),
				 INT64_FORMAT "\t" INT64_FORMAT "\t%d\t\n",
				 j, k / naccounts + 1, 0);
		if (PQputline(con, sql))
			ereport(ELEVEL_FATAL, (errmsg("PQputline failed\n")));

		/*
		 * If we want to stick with the original logging, print a message each
		 * 100k inserted rows.
		 */
		if ((!use_quiet) && (j % 100000 == 0))
		{
			INSTR_TIME_SET_CURRENT(diff);
			INSTR_TIME_SUBTRACT(diff, start);

			elapsed_sec = INSTR_TIME_GET_DOUBLE(diff);
			remaining_sec = ((double) scale * naccounts - j) * elapsed_sec / j;

			ereport(ELEVEL_LOG_MAIN,
					(errmsg(INT64_FORMAT " of " INT64_FORMAT " tuples (%d%%) done (elapsed %.2f s, remaining %.2f s)\n",
							j, (int64) naccounts * scale,
							(int) (((int64) j * 100) /
								   (naccounts * (int64) scale)),
							elapsed_sec, remaining_sec)));
		}
		/* let's not call the timing for each row, but only each 100 rows */
		else if (use_quiet && (j % 100 == 0))
		{
			INSTR_TIME_SET_CURRENT(diff);
			INSTR_TIME_SUBTRACT(diff, start);

			elapsed_sec = INSTR_TIME_GET_DOUBLE(diff);
			remaining_sec = ((double) scale * naccounts - j) * elapsed_sec / j;

			/* have we reached the next interval (or end)? */
			if ((j == scale * naccounts) || (elapsed_sec >= log_interval * LOG_STEP_SECONDS))
			{
				ereport(ELEVEL_LOG_MAIN,
						(errmsg(INT64_FORMAT " of " INT64_FORMAT " tuples (%d%%) done (elapsed %.2f s, remaining %.2f s)\n",
								j, (int64) naccounts * scale,
								(int) (((int64) j * 100) /
									   (naccounts * (int64) scale)),
								elapsed_sec, remaining_sec)));

				/* skip to the next interval */
				log_interval = (int) ceil(elapsed_sec / LOG_STEP_SECONDS);
			}
		}

	}
	if (PQputline(con, "\\.\n"))
		ereport(ELEVEL_FATAL, (errmsg("very last PQputline failed\n")));
	if (PQendcopy(con))
		ereport(ELEVEL_FATAL, (errmsg("PQendcopy failed\n")));

	executeStatement(con, "commit");
}

/*
 * Invoke vacuum on the standard tables
 */
static void
initVacuum(PGconn *con)
{
	ereport(ELEVEL_LOG_MAIN, (errmsg("vacuuming...\n")));
	executeStatement(con, "vacuum analyze ysql_bench_branches");
	executeStatement(con, "vacuum analyze ysql_bench_tellers");
	executeStatement(con, "vacuum analyze ysql_bench_accounts");
	executeStatement(con, "vacuum analyze ysql_bench_history");
}

/*
 * Create foreign key constraints between the standard tables
 */
static void
initCreateFKeys(PGconn *con)
{
	static const char *const DDLKEYs[] = {
		"alter table ysql_bench_tellers add constraint ysql_bench_tellers_bid_fkey foreign key (bid) references ysql_bench_branches",
		"alter table ysql_bench_accounts add constraint ysql_bench_accounts_bid_fkey foreign key (bid) references ysql_bench_branches",
		"alter table ysql_bench_history add constraint ysql_bench_history_bid_fkey foreign key (bid) references ysql_bench_branches",
		"alter table ysql_bench_history add constraint ysql_bench_history_tid_fkey foreign key (tid) references ysql_bench_tellers",
		"alter table ysql_bench_history add constraint ysql_bench_history_aid_fkey foreign key (aid) references ysql_bench_accounts"
	};
	int			i;

	ereport(ELEVEL_LOG_MAIN, (errmsg("creating foreign keys...\n")));
	for (i = 0; i < lengthof(DDLKEYs); i++)
	{
		executeStatement(con, DDLKEYs[i]);
	}
}

/*
 * Validate an initialization-steps string
 *
 * (We could just leave it to runInitSteps() to fail if there are wrong
 * characters, but since initialization can take awhile, it seems friendlier
 * to check during option parsing.)
 */
static void
checkInitSteps(const char *initialize_steps)
{
	const char *step;

	if (initialize_steps[0] == '\0')
		ereport(ELEVEL_FATAL, (errmsg("no initialization steps specified\n")));

	for (step = initialize_steps; *step != '\0'; step++)
	{
		if (strchr("dtgvpf ", *step) == NULL)
		{
			ereport(ELEVEL_FATAL,
					(errmsg("unrecognized initialization step \"%c\"\n"
							"allowed steps are: \"d\", \"t\", \"g\", \"v\", \"p\", \"f\"\n",
							*step)));
		}
	}
}

/*
 * Invoke each initialization step in the given string
 */
static void
runInitSteps(const char *initialize_steps)
{
	PGconn	   *con;
	const char *step;

	if ((con = doConnect()) == NULL)
		exit(1);

  bool use_primary_key = false;
  for (step = initialize_steps; *step != '\0'; step++)
  {
    use_primary_key |= (*step == 'p');
  }
	for (step = initialize_steps; *step != '\0'; step++)
	{
		switch (*step)
		{
			case 'd':
				initDropTables(con);
				break;
			case 't':
				initCreateTables(con, use_primary_key);
				break;
			case 'g':
				initGenerateData(con);
				break;
			case 'v':
				initVacuum(con);
				break;
			case 'p':
          // handled via 'use_primary_key' in conjunction with 't'
          break;
			case 'f':
				initCreateFKeys(con);
				break;
			case ' ':
				break;			/* ignore */
			default:
				ereport(ELEVEL_LOG_MAIN,
						(errmsg("unrecognized initialization step \"%c\"\n",
								*step)));
				PQfinish(con);
				exit(1);
		}
	}

	ereport(ELEVEL_LOG_MAIN, (errmsg("done.\n")));
	PQfinish(con);
}

/*
 * Replace :param with $n throughout the command's SQL text, which
 * is a modifiable string in cmd->argv[0].
 */
static bool
parseQuery(Command *cmd)
{
	char	   *sql,
			   *p;

	/* We don't want to scribble on cmd->argv[0] until done */
	sql = pg_strdup(cmd->argv[0]);

	cmd->argc = 1;

	p = sql;
	while ((p = strchr(p, ':')) != NULL)
	{
		char		var[13];
		char	   *name;
		int			eaten;

		name = parseVariable(p, &eaten);
		if (name == NULL)
		{
			while (*p == ':')
			{
				p++;
			}
			continue;
		}

		if (cmd->argc >= MAX_ARGS)
		{
			ereport(ELEVEL_LOG_MAIN,
					(errmsg("statement has too many arguments (maximum is %d): %s\n",
							MAX_ARGS - 1, cmd->argv[0])));
			pg_free(name);
			return false;
		}

		sprintf(var, "$%d", cmd->argc);
		p = replaceVariable(&sql, p, eaten, var);

		cmd->argv[cmd->argc] = name;
		cmd->argc++;
	}

	pg_free(cmd->argv[0]);
	cmd->argv[0] = sql;
	return true;
}

/*
 * Simple error-printing function, might be needed by lexer
 */
static void
pgbench_error(const char *fmt,...)
{
	va_list		ap;
	PQExpBufferData errmsg_buf;
	bool		done;

	fflush(stdout);
	initPQExpBuffer(&errmsg_buf);

	/* Loop in case we have to retry after enlarging the buffer. */
	do
	{
		va_start(ap, fmt);
		done = appendPQExpBufferVA(&errmsg_buf, fmt, ap);
		va_end(ap);
	} while (!done);

	ereport(ELEVEL_LOG_MAIN, (errmsg("%s", errmsg_buf.data)));
	termPQExpBuffer(&errmsg_buf);
}

/*
 * syntax error while parsing a script (in practice, while parsing a
 * backslash command, because we don't detect syntax errors in SQL)
 *
 * source: source of script (filename or builtin-script ID)
 * lineno: line number within script (count from 1)
 * line: whole line of backslash command, if available
 * command: backslash command name, if available
 * msg: the actual error message
 * more: optional extra message
 * column: zero-based column number, or -1 if unknown
 */
void
syntax_error(const char *source, int lineno,
			 const char *line, const char *command,
			 const char *msg, const char *more, int column)
{
	PQExpBufferData errmsg_buf;

	initPQExpBuffer(&errmsg_buf);
	printfPQExpBuffer(&errmsg_buf, "%s:%d: %s", source, lineno, msg);
	if (more != NULL)
		appendPQExpBuffer(&errmsg_buf, " (%s)", more);
	if (column >= 0 && line == NULL)
		appendPQExpBuffer(&errmsg_buf, " at column %d", column + 1);
	if (command != NULL)
		appendPQExpBuffer(&errmsg_buf, " in command \"%s\"", command);
	appendPQExpBufferChar(&errmsg_buf, '\n');
	if (line != NULL)
	{
		appendPQExpBuffer(&errmsg_buf, "%s\n", line);
		if (column >= 0)
		{
			int			i;

			for (i = 0; i < column; i++)
				appendPQExpBufferChar(&errmsg_buf, ' ');
			appendPQExpBufferStr(&errmsg_buf, "^ error found here\n");
		}
	}

	ereport(ELEVEL_LOG_MAIN, (errmsg("%s", errmsg_buf.data)));
	termPQExpBuffer(&errmsg_buf);
	exit(1);
}

/*
 * Parse a SQL command; return a Command struct, or NULL if it's a comment
 *
 * On entry, psqlscan.l has collected the command into "buf", so we don't
 * really need to do much here except check for comment and set up a
 * Command struct.
 */
static Command *
process_sql_command(PQExpBuffer buf, const char *source)
{
	Command    *my_command;
	char	   *p;
	char	   *nlpos;

	/* Skip any leading whitespace, as well as "--" style comments */
	p = buf->data;
	for (;;)
	{
		if (isspace((unsigned char) *p))
			p++;
		else if (strncmp(p, "--", 2) == 0)
		{
			p = strchr(p, '\n');
			if (p == NULL)
				return NULL;
			p++;
		}
		else
			break;
	}

	/* If there's nothing but whitespace and comments, we're done */
	if (*p == '\0')
		return NULL;

	/* Allocate and initialize Command structure */
	my_command = (Command *) pg_malloc0(sizeof(Command));
	my_command->command_num = num_commands++;
	my_command->type = SQL_COMMAND;
	my_command->meta = META_NONE;
	initSimpleStats(&my_command->stats);

	/*
	 * Install query text as the sole argv string.  If we are using a
	 * non-simple query mode, we'll extract parameters from it later.
	 */
	my_command->argv[0] = pg_strdup(p);
	my_command->argc = 1;

	/*
	 * If SQL command is multi-line, we only want to save the first line as
	 * the "line" label.
	 */
	nlpos = strchr(p, '\n');
	if (nlpos)
	{
		my_command->line = pg_malloc(nlpos - p + 1);
		memcpy(my_command->line, p, nlpos - p);
		my_command->line[nlpos - p] = '\0';
	}
	else
		my_command->line = pg_strdup(p);

	return my_command;
}

/*
 * Parse a backslash command; return a Command struct, or NULL if comment
 *
 * At call, we have scanned only the initial backslash.
 */
static Command *
process_backslash_command(PsqlScanState sstate, const char *source)
{
	Command    *my_command;
	PQExpBufferData word_buf;
	int			word_offset;
	int			offsets[MAX_ARGS];	/* offsets of argument words */
	int			start_offset;
	int			lineno;
	int			j;

	initPQExpBuffer(&word_buf);

	/* Remember location of the backslash */
	start_offset = expr_scanner_offset(sstate) - 1;
	lineno = expr_scanner_get_lineno(sstate, start_offset);

	/* Collect first word of command */
	if (!expr_lex_one_word(sstate, &word_buf, &word_offset))
	{
		termPQExpBuffer(&word_buf);
		return NULL;
	}

	/* Allocate and initialize Command structure */
	my_command = (Command *) pg_malloc0(sizeof(Command));
	my_command->command_num = num_commands++;
	my_command->type = META_COMMAND;
	my_command->argc = 0;
	initSimpleStats(&my_command->stats);

	/* Save first word (command name) */
	j = 0;
	offsets[j] = word_offset;
	my_command->argv[j++] = pg_strdup(word_buf.data);
	my_command->argc++;

	/* ... and convert it to enum form */
	my_command->meta = getMetaCommand(my_command->argv[0]);

	if (my_command->meta == META_SET ||
		my_command->meta == META_IF ||
		my_command->meta == META_ELIF)
	{
		yyscan_t	yyscanner;

		/* For \set, collect var name */
		if (my_command->meta == META_SET)
		{
			if (!expr_lex_one_word(sstate, &word_buf, &word_offset))
				syntax_error(source, lineno, my_command->line, my_command->argv[0],
							 "missing argument", NULL, -1);

			offsets[j] = word_offset;
			my_command->argv[j++] = pg_strdup(word_buf.data);
			my_command->argc++;
		}

		/* then for all parse the expression */
		yyscanner = expr_scanner_init(sstate, source, lineno, start_offset,
									  my_command->argv[0]);

		if (expr_yyparse(yyscanner) != 0)
		{
			/* dead code: exit done from syntax_error called by yyerror */
			exit(1);
		}

		my_command->expr = expr_parse_result;

		/* Save line, trimming any trailing newline */
		my_command->line = expr_scanner_get_substring(sstate,
													  start_offset,
													  expr_scanner_offset(sstate),
													  true);

		expr_scanner_finish(yyscanner);

		termPQExpBuffer(&word_buf);

		return my_command;
	}

	/* For all other commands, collect remaining words. */
	while (expr_lex_one_word(sstate, &word_buf, &word_offset))
	{
		if (j >= MAX_ARGS)
			syntax_error(source, lineno, my_command->line, my_command->argv[0],
						 "too many arguments", NULL, -1);

		offsets[j] = word_offset;
		my_command->argv[j++] = pg_strdup(word_buf.data);
		my_command->argc++;
	}

	/* Save line, trimming any trailing newline */
	my_command->line = expr_scanner_get_substring(sstate,
												  start_offset,
												  expr_scanner_offset(sstate),
												  true);

	if (my_command->meta == META_SLEEP)
	{
		if (my_command->argc < 2)
			syntax_error(source, lineno, my_command->line, my_command->argv[0],
						 "missing argument", NULL, -1);

		if (my_command->argc > 3)
			syntax_error(source, lineno, my_command->line, my_command->argv[0],
						 "too many arguments", NULL,
						 offsets[3] - start_offset);

		/*
		 * Split argument into number and unit to allow "sleep 1ms" etc. We
		 * don't have to terminate the number argument with null because it
		 * will be parsed with atoi, which ignores trailing non-digit
		 * characters.
		 */
		if (my_command->argc == 2 && my_command->argv[1][0] != ':')
		{
			char	   *c = my_command->argv[1];

			while (isdigit((unsigned char) *c))
				c++;
			if (*c)
			{
				my_command->argv[2] = c;
				offsets[2] = offsets[1] + (c - my_command->argv[1]);
				my_command->argc = 3;
			}
		}

		if (my_command->argc == 3)
		{
			if (pg_strcasecmp(my_command->argv[2], "us") != 0 &&
				pg_strcasecmp(my_command->argv[2], "ms") != 0 &&
				pg_strcasecmp(my_command->argv[2], "s") != 0)
				syntax_error(source, lineno, my_command->line, my_command->argv[0],
							 "unrecognized time unit, must be us, ms or s",
							 my_command->argv[2], offsets[2] - start_offset);
		}
	}
	else if (my_command->meta == META_SETSHELL)
	{
		if (my_command->argc < 3)
			syntax_error(source, lineno, my_command->line, my_command->argv[0],
						 "missing argument", NULL, -1);
	}
	else if (my_command->meta == META_SHELL)
	{
		if (my_command->argc < 2)
			syntax_error(source, lineno, my_command->line, my_command->argv[0],
						 "missing command", NULL, -1);
	}
	else if (my_command->meta == META_ELSE || my_command->meta == META_ENDIF)
	{
		if (my_command->argc != 1)
			syntax_error(source, lineno, my_command->line, my_command->argv[0],
						 "unexpected argument", NULL, -1);
	}
	else
	{
		/* my_command->meta == META_NONE */
		syntax_error(source, lineno, my_command->line, my_command->argv[0],
					 "invalid command", NULL, -1);
	}

	termPQExpBuffer(&word_buf);

	return my_command;
}

static void
ConditionError(const char *desc, int cmdn, const char *msg)
{
	ereport(ELEVEL_FATAL,
			(errmsg("condition error in script \"%s\" command %d: %s\n",
					desc, cmdn, msg)));
}

/*
 * Partial evaluation of conditionals before recording and running the script.
 */
static void
CheckConditional(ParsedScript ps)
{
	/* statically check conditional structure */
	ConditionalStack cs = conditional_stack_create();
	int			i;

	for (i = 0; ps.commands[i] != NULL; i++)
	{
		Command    *cmd = ps.commands[i];

		if (cmd->type == META_COMMAND)
		{
			switch (cmd->meta)
			{
				case META_IF:
					conditional_stack_push(cs, IFSTATE_FALSE);
					break;
				case META_ELIF:
					if (conditional_stack_empty(cs))
						ConditionError(ps.desc, i + 1, "\\elif without matching \\if");
					if (conditional_stack_peek(cs) == IFSTATE_ELSE_FALSE)
						ConditionError(ps.desc, i + 1, "\\elif after \\else");
					break;
				case META_ELSE:
					if (conditional_stack_empty(cs))
						ConditionError(ps.desc, i + 1, "\\else without matching \\if");
					if (conditional_stack_peek(cs) == IFSTATE_ELSE_FALSE)
						ConditionError(ps.desc, i + 1, "\\else after \\else");
					conditional_stack_poke(cs, IFSTATE_ELSE_FALSE);
					break;
				case META_ENDIF:
					if (!conditional_stack_pop(cs))
						ConditionError(ps.desc, i + 1, "\\endif without matching \\if");
					break;
				default:
					/* ignore anything else... */
					break;
			}
		}
	}
	if (!conditional_stack_empty(cs))
		ConditionError(ps.desc, i + 1, "\\if without matching \\endif");
	conditional_stack_destroy(cs);
}

/*
 * Parse a script (either the contents of a file, or a built-in script)
 * and add it to the list of scripts.
 */
static void
ParseScript(const char *script, const char *desc, int weight)
{
	ParsedScript ps;
	PsqlScanState sstate;
	PQExpBufferData line_buf;
	int			alloc_num;
	int			index;

#define COMMANDS_ALLOC_NUM 128
	alloc_num = COMMANDS_ALLOC_NUM;

	/* Initialize all fields of ps */
	ps.desc = desc;
	ps.weight = weight;
	ps.commands = (Command **) pg_malloc(sizeof(Command *) * alloc_num);
	initStats(&ps.stats, 0);

	/* Prepare to parse script */
	sstate = psql_scan_create(&pgbench_callbacks);

	/*
	 * Ideally, we'd scan scripts using the encoding and stdstrings settings
	 * we get from a DB connection.  However, without major rearrangement of
	 * pgbench's argument parsing, we can't have a DB connection at the time
	 * we parse scripts.  Using SQL_ASCII (encoding 0) should work well enough
	 * with any backend-safe encoding, though conceivably we could be fooled
	 * if a script file uses a client-only encoding.  We also assume that
	 * stdstrings should be true, which is a bit riskier.
	 */
	psql_scan_setup(sstate, script, strlen(script), 0, true);

	initPQExpBuffer(&line_buf);

	index = 0;

	for (;;)
	{
		PsqlScanResult sr;
		promptStatus_t prompt;
		Command    *command;

		resetPQExpBuffer(&line_buf);

		sr = psql_scan(sstate, &line_buf, &prompt);

		/* If we collected a SQL command, process that */
		command = process_sql_command(&line_buf, desc);
		if (command)
		{
			ps.commands[index] = command;
			index++;

			if (index >= alloc_num)
			{
				alloc_num += COMMANDS_ALLOC_NUM;
				ps.commands = (Command **)
					pg_realloc(ps.commands, sizeof(Command *) * alloc_num);
			}
		}

		/* If we reached a backslash, process that */
		if (sr == PSCAN_BACKSLASH)
		{
			command = process_backslash_command(sstate, desc);
			if (command)
			{
				ps.commands[index] = command;
				index++;

				if (index >= alloc_num)
				{
					alloc_num += COMMANDS_ALLOC_NUM;
					ps.commands = (Command **)
						pg_realloc(ps.commands, sizeof(Command *) * alloc_num);
				}
			}
		}

		/* Done if we reached EOF */
		if (sr == PSCAN_INCOMPLETE || sr == PSCAN_EOL)
			break;
	}

	ps.commands[index] = NULL;

	addScript(ps);

	termPQExpBuffer(&line_buf);
	psql_scan_finish(sstate);
	psql_scan_destroy(sstate);
}

/*
 * Read the entire contents of file fd, and return it in a malloc'd buffer.
 *
 * The buffer will typically be larger than necessary, but we don't care
 * in this program, because we'll free it as soon as we've parsed the script.
 */
static char *
read_file_contents(FILE *fd)
{
	char	   *buf;
	size_t		buflen = BUFSIZ;
	size_t		used = 0;

	buf = (char *) pg_malloc(buflen);

	for (;;)
	{
		size_t		nread;

		nread = fread(buf + used, 1, BUFSIZ, fd);
		used += nread;
		/* If fread() read less than requested, must be EOF or error */
		if (nread < BUFSIZ)
			break;
		/* Enlarge buf so we can read some more */
		buflen += BUFSIZ;
		buf = (char *) pg_realloc(buf, buflen);
	}
	/* There is surely room for a terminator */
	buf[used] = '\0';

	return buf;
}

/*
 * Given a file name, read it and add its script to the list.
 * "-" means to read stdin.
 * NB: filename must be storage that won't disappear.
 */
static void
process_file(const char *filename, int weight)
{
	FILE	   *fd;
	char	   *buf;

	/* Slurp the file contents into "buf" */
	if (strcmp(filename, "-") == 0)
		fd = stdin;
	else if ((fd = fopen(filename, "r")) == NULL)
	{
		ereport(ELEVEL_FATAL,
				(errmsg("could not open file \"%s\": %s\n",
						filename, strerror(errno))));
	}

	buf = read_file_contents(fd);

	if (ferror(fd))
	{
		ereport(ELEVEL_FATAL,
				(errmsg("could not read file \"%s\": %s\n",
						filename, strerror(errno))));
	}

	if (fd != stdin)
		fclose(fd);

	ParseScript(buf, filename, weight);

	free(buf);
}

/* Parse the given builtin script and add it to the list. */
static void
process_builtin(const BuiltinScript *bi, int weight)
{
	ParseScript(bi->script, bi->desc, weight);
}

/* show available builtin scripts */
static void
listAvailableScripts(void)
{
	int			i;
	PQExpBufferData errmsg_buf;

	initPQExpBuffer(&errmsg_buf);
	printfPQExpBuffer(&errmsg_buf, "Available builtin scripts:\n");
	for (i = 0; i < lengthof(builtin_script); i++)
		appendPQExpBuffer(&errmsg_buf, "\t%s\n", builtin_script[i].name);
	appendPQExpBufferChar(&errmsg_buf, '\n');

	ereport(ELEVEL_LOG_MAIN, (errmsg("%s", errmsg_buf.data)));
	termPQExpBuffer(&errmsg_buf);
}

/* return builtin script "name" if unambiguous, fails if not found */
static const BuiltinScript *
findBuiltin(const char *name)
{
	int			i,
				found = 0,
				len = strlen(name);
	const BuiltinScript *result = NULL;

	for (i = 0; i < lengthof(builtin_script); i++)
	{
		if (strncmp(builtin_script[i].name, name, len) == 0)
		{
			result = &builtin_script[i];
			found++;
		}
	}

	/* ok, unambiguous result */
	if (found == 1)
		return result;

	/* error cases */
	if (found == 0)
	{
		ereport(ELEVEL_LOG_MAIN,
				(errmsg("no builtin script found for name \"%s\"\n", name)));
	}
	else
	{						/* found > 1 */
		ereport(ELEVEL_LOG_MAIN,
				(errmsg("ambiguous builtin name: %d builtin scripts found for prefix \"%s\"\n",
						found, name)));
	}

	listAvailableScripts();
	exit(1);
}

/*
 * Determine the weight specification from a script option (-b, -f), if any,
 * and return it as an integer (1 is returned if there's no weight).  The
 * script name is returned in *script as a malloc'd string.
 */
static int
parseScriptWeight(const char *option, char **script)
{
	char	   *sep;
	int			weight;

	if ((sep = strrchr(option, WSEP)))
	{
		int			namelen = sep - option;
		long		wtmp;
		char	   *badp;

		/* generate the script name */
		*script = pg_malloc(namelen + 1);
		strncpy(*script, option, namelen);
		(*script)[namelen] = '\0';

		/* process digits of the weight spec */
		errno = 0;
		wtmp = strtol(sep + 1, &badp, 10);
		if (errno != 0 || badp == sep + 1 || *badp != '\0')
		{
			ereport(ELEVEL_FATAL,
					(errmsg("invalid weight specification: %s\n", sep)));
		}
		if (wtmp > INT_MAX || wtmp < 0)
		{
			ereport(ELEVEL_FATAL,
					(errmsg("weight specification out of range (0 .. %u): " INT64_FORMAT "\n",
							INT_MAX, (int64) wtmp)));
		}
		weight = wtmp;
	}
	else
	{
		*script = pg_strdup(option);
		weight = 1;
	}

	return weight;
}

/* append a script to the list of scripts to process */
static void
addScript(ParsedScript script)
{
	if (script.commands == NULL || script.commands[0] == NULL)
	{
		ereport(ELEVEL_FATAL,
				(errmsg("empty command list for script \"%s\"\n",
						script.desc)));
	}

	if (num_scripts >= MAX_SCRIPTS)
	{
		ereport(ELEVEL_FATAL,
				(errmsg("at most %d SQL scripts are allowed\n", MAX_SCRIPTS)));
	}

	CheckConditional(script);

	sql_script[num_scripts] = script;
	num_scripts++;
}

static void
printSimpleStats(const char *prefix, SimpleStats *ss)
{
	if (ss->count > 0)
	{
		double		latency = ss->sum / ss->count;
		double		stddev = sqrt(ss->sum2 / ss->count - latency * latency);

		printf("%s average = %.3f ms\n", prefix, 0.001 * latency);
		printf("%s stddev = %.3f ms\n", prefix, 0.001 * stddev);
	}
}

/* print out results */
static void
printResults(TState *threads, StatsData *total, instr_time total_time,
			 instr_time conn_total_time, int64 latency_late)
{
	double		time_include,
				tps_include,
				tps_exclude;
	int64		ntx = total->cnt - total->skipped,
				total_ntx = total->cnt + total->errors;
	int			i,
				totalCacheOverflows = 0;

	time_include = INSTR_TIME_GET_DOUBLE(total_time);

	/* tps is about actually executed transactions */
	tps_include = ntx / time_include;
	tps_exclude = ntx /
		(time_include - (INSTR_TIME_GET_DOUBLE(conn_total_time) / nclients));

	/* Report test parameters. */
	printf("transaction type: %s\n",
		   num_scripts == 1 ? sql_script[0].desc : "multiple scripts");
	printf("scaling factor: %d\n", scale);
	printf("query mode: %s\n", QUERYMODE[querymode]);
	printf("number of clients: %d\n", nclients);
	printf("number of threads: %d\n", nthreads);
	if (duration <= 0)
	{
		printf("number of transactions per client: %d\n", nxacts);
		printf("number of transactions actually processed: " INT64_FORMAT "/" INT64_FORMAT "\n",
			   ntx, total_ntx);
	}
	else
	{
		printf("duration: %d s\n", duration);
		printf("number of transactions actually processed: " INT64_FORMAT "\n",
			   ntx);
	}

	if (total->errors > 0)
		printf("number of errors: " INT64_FORMAT " (%.3f%%)\n",
			   total->errors, 100.0 * total->errors / total_ntx);

	if (total->errors_in_failed_tx > 0)
		printf("number of errors \"in failed SQL transaction\": " INT64_FORMAT " (%.3f%%)\n",
			   total->errors_in_failed_tx,
			   100.0 * total->errors_in_failed_tx / total_ntx);

	/*
	 * It can be non-zero only if max_tries is greater than one or
	 * latency_limit is used.
	 */
	if (total->retried > 0)
	{
		printf("number of retried: " INT64_FORMAT " (%.3f%%)\n",
			   total->retried, 100.0 * total->retried / total_ntx);
		printf("number of retries: " INT64_FORMAT "\n", total->retries);
	}

	if (max_tries)
		printf("maximum number of tries: %d\n", max_tries);

	if (latency_limit)
	{
		printf("number of transactions above the %.1f ms latency limit: " INT64_FORMAT "/" INT64_FORMAT " (%.3f %%)",
			   latency_limit / 1000.0, latency_late, total_ntx,
			   (total_ntx > 0) ? 100.0 * latency_late / total_ntx : 0.0);

		/* this statistics includes both successful and failed transactions */
		if (total->errors > 0)
			printf(" (including errors)");

		printf("\n");
	}

	/* Report zipfian cache overflow */
	for (i = 0; i < nthreads; i++)
	{
		totalCacheOverflows += threads[i].zipf_cache.overflowCount;
	}
	if (totalCacheOverflows > 0)
	{
		printf("zipfian cache array overflowed %d time(s)\n", totalCacheOverflows);
	}

	/* Remaining stats are nonsensical if we failed to execute any xacts */
	if (total->cnt <= 0)
		return;

	if (throttle_delay && latency_limit)
		printf("number of transactions skipped: " INT64_FORMAT " (%.3f %%)\n",
			   total->skipped,
			   100.0 * total->skipped / total->cnt);

	if (throttle_delay || progress || latency_limit)
		printSimpleStats("latency", &total->latency);
	else
	{
		/* no measurement, show average latency computed from run time */
		printf("latency average = %.3f ms",
			   1000.0 * time_include * nclients / total_ntx);

		/* this statistics includes both successful and failed transactions */
		if (total->errors > 0)
			printf(" (including errors)");

		printf("\n");
	}

	if (throttle_delay)
	{
		/*
		 * Report average transaction lag under rate limit throttling.  This
		 * is the delay between scheduled and actual start times for the
		 * transaction.  The measured lag may be caused by thread/client load,
		 * the database load, or the Poisson throttling process.
		 */
		printf("rate limit schedule lag: avg %.3f (max %.3f) ms\n",
			   0.001 * total->lag.sum / total->cnt, 0.001 * total->lag.max);
	}

	printf("tps = %f (including connections establishing)\n", tps_include);
	printf("tps = %f (excluding connections establishing)\n", tps_exclude);

	/* Report per-script/command statistics */
	if (per_script_stats || report_per_command)
	{
		int			i;

		for (i = 0; i < num_scripts; i++)
		{
			if (per_script_stats)
			{
				StatsData  *sstats = &sql_script[i].stats;
				int64		script_total_ntx = sstats->cnt + sstats->errors;

				printf("SQL script %d: %s\n"
					   " - weight: %d (targets %.1f%% of total)\n"
					   " - " INT64_FORMAT " transactions (%.1f%% of total, tps = %f)\n",
					   i + 1, sql_script[i].desc,
					   sql_script[i].weight,
					   100.0 * sql_script[i].weight / total_weight,
					   sstats->cnt,
					   100.0 * sstats->cnt / script_total_ntx,
					   (sstats->cnt - sstats->skipped) / time_include);

				if (total->errors > 0)
					printf(" - number of errors: " INT64_FORMAT " (%.3f%%)\n",
						   sstats->errors,
						   100.0 * sstats->errors / script_total_ntx);

				if (total->errors_in_failed_tx > 0)
					printf(" - number of errors \"in failed SQL transaction\": " INT64_FORMAT " (%.3f%%)\n",
						   sstats->errors_in_failed_tx,
						   (100.0 * sstats->errors_in_failed_tx /
							script_total_ntx));

				/*
				 * It can be non-zero only if max_tries is greater than one or
				 * latency_limit is used.
				 */
				if (total->retried > 0)
				{
					printf(" - number of retried: " INT64_FORMAT " (%.3f%%)\n",
						   sstats->retried,
						   100.0 * sstats->retried / script_total_ntx);
					printf(" - number of retries: " INT64_FORMAT "\n",
						   sstats->retries);
				}

				if (throttle_delay && latency_limit && sstats->cnt > 0)
					printf(" - number of transactions skipped: " INT64_FORMAT " (%.3f%%)\n",
						   sstats->skipped,
						   100.0 * sstats->skipped / sstats->cnt);

				printSimpleStats(" - latency", &sstats->latency);
			}

			/* Report per-command latencies and errors */
			if (report_per_command)
			{
				Command   **commands;

				if (per_script_stats)
					printf(" - statement latencies in milliseconds");
				else
					printf("statement latencies in milliseconds");

				if (total->errors > 0)
				{
					printf("%s errors",
						   ((total->errors_in_failed_tx == 0 &&
							total->retried == 0) ?
							" and" : ","));
				}
				if (total->errors_in_failed_tx > 0)
				{
					printf("%s errors \"in failed SQL transaction\"",
						   total->retried == 0 ? " and" : ",");
				}
				if (total->retried > 0)
				{
					printf(" and retries");
				}
				printf(":\n");

				for (commands = sql_script[i].commands;
					 *commands != NULL;
					 commands++)
				{
					SimpleStats *cstats = &(*commands)->stats;

					printf("   %11.3f",
						   (cstats->count > 0) ?
						   1000.0 * cstats->sum / cstats->count : 0.0);
					if (total->errors > 0)
					{
						printf("  %20" INT64_MODIFIER "d",
							   (*commands)->errors);
					}
					if (total->errors_in_failed_tx > 0)
					{
						printf("  %20" INT64_MODIFIER "d",
							   (*commands)->errors_in_failed_tx);
					}
					if (total->retried > 0)
					{
						printf("  %20" INT64_MODIFIER "d",
							   (*commands)->retries);
					}
					printf("  %s\n", (*commands)->line);
				}
			}
		}
	}
}

/*
 * Set up a random seed according to seed parameter (NULL means default),
 * and initialize base_random_sequence for use in initializing other sequences.
 */
static bool
set_random_seed(const char *seed)
{
	uint64		iseed;

	if (seed == NULL || strcmp(seed, "time") == 0)
	{
		/* rely on current time */
		instr_time	now;

		INSTR_TIME_SET_CURRENT(now);
		iseed = (uint64) INSTR_TIME_GET_MICROSEC(now);
	}
	else if (strcmp(seed, "rand") == 0)
	{
		/* use some "strong" random source */
#ifdef HAVE_STRONG_RANDOM
		if (!pg_strong_random(&iseed, sizeof(iseed)))
#endif
		{
			ereport(ELEVEL_LOG_MAIN,
					(errmsg("cannot seed random from a strong source, none available: use \"time\" or an unsigned integer value.\n")));
			return false;
		}
	}
	else
	{
		/* parse seed unsigned int value */
		char		garbage;

		if (sscanf(seed, UINT64_FORMAT "%c", &iseed, &garbage) != 1)
		{
                        ereport(ELEVEL_LOG_MAIN,
                                        (errmsg("unrecognized random seed option \"%s\": expecting an unsigned integer, \"time\" or \"rand\"\n",
                                                        seed)));
			return false;
		}
	}

	if (seed != NULL)
		ereport(ELEVEL_LOG_MAIN, (errmsg("setting random seed to " UINT64_FORMAT "\n", iseed)));
	random_seed = iseed;

	/* Fill base_random_sequence with low-order bits of seed */
	base_random_sequence[0] = iseed & 0xFFFF;
	base_random_sequence[1] = (iseed >> 16) & 0xFFFF;
	base_random_sequence[2] = (iseed >> 32) & 0xFFFF;

	return true;
}

/*
 * Initialize the random state of the client/thread.
 */
static void
initRandomState(RandomState *random_state)
{
	random_state->data[0] = (unsigned short)
                (pg_jrand48(base_random_sequence) & 0xFFFF);
	random_state->data[1] =(unsigned short)
                (pg_jrand48(base_random_sequence) & 0xFFFF);
	random_state->data[2] = (unsigned short)
                (pg_jrand48(base_random_sequence) & 0xFFFF);

}


int
main(int argc, char **argv)
{
	static struct option long_options[] = {
		/* systematic long/short named options */
		{"builtin", required_argument, NULL, 'b'},
		{"client", required_argument, NULL, 'c'},
		{"connect", no_argument, NULL, 'C'},
		{"debug", required_argument, NULL, 'd'},
		{"define", required_argument, NULL, 'D'},
		{"file", required_argument, NULL, 'f'},
		{"fillfactor", required_argument, NULL, 'F'},
		{"host", required_argument, NULL, 'h'},
		{"initialize", no_argument, NULL, 'i'},
		{"init-steps", required_argument, NULL, 'I'},
		{"jobs", required_argument, NULL, 'j'},
		{"log", no_argument, NULL, 'l'},
		{"latency-limit", required_argument, NULL, 'L'},
		{"no-vacuum", no_argument, NULL, 'n'},
		{"port", required_argument, NULL, 'p'},
		{"progress", required_argument, NULL, 'P'},
		{"protocol", required_argument, NULL, 'M'},
		{"quiet", no_argument, NULL, 'q'},
		{"report-per-command", no_argument, NULL, 'r'},
		{"rate", required_argument, NULL, 'R'},
		{"scale", required_argument, NULL, 's'},
		{"select-only", no_argument, NULL, 'S'},
		{"skip-some-updates", no_argument, NULL, 'N'},
		{"time", required_argument, NULL, 'T'},
		{"transactions", required_argument, NULL, 't'},
		{"username", required_argument, NULL, 'U'},
		{"vacuum-all", no_argument, NULL, 'v'},
		/* long-named only options */
		{"unlogged-tables", no_argument, NULL, 1},
		{"tablespace", required_argument, NULL, 2},
		{"index-tablespace", required_argument, NULL, 3},
		{"sampling-rate", required_argument, NULL, 4},
		{"aggregate-interval", required_argument, NULL, 5},
		{"progress-timestamp", no_argument, NULL, 6},
		{"log-prefix", required_argument, NULL, 7},
		{"foreign-keys", no_argument, NULL, 8},
		{"random-seed", required_argument, NULL, 9},
		{"max-tries", required_argument, NULL, 10},
		{NULL, 0, NULL, 0}
	};

	int			c;
	bool		is_init_mode = false;	/* initialize mode? */
	char	   *initialize_steps = NULL;
	bool		foreign_keys = false;
	bool		is_no_vacuum = false;
	bool		do_vacuum_accounts = false; /* vacuum accounts table? */
	int			optindex;
	bool		scale_given = false;

	bool		benchmarking_option_set = false;
	bool		initialization_option_set = false;
	bool		internal_script_used = false;

	CState	   *state;			/* status of clients */
	TState	   *threads;		/* array of thread */

	instr_time	start_time;		/* start up time */
	instr_time	total_time;
	instr_time	conn_total_time;
	int64		latency_late = 0;
	StatsData	stats;
	int			weight;

	int			i;
	int			nclients_dealt;

#ifdef HAVE_GETRLIMIT
	struct rlimit rlim;
#endif

	PGconn	   *con;
	PGresult   *res;
	char	   *env;

	progname = get_progname(argv[0]);

	if (argc > 1)
	{
		if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-?") == 0)
		{
			usage();
			exit(0);
		}
		if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0)
		{
			puts("ysql_bench (YSQL) " PG_VERSION);
			exit(0);
		}
	}

#ifdef WIN32
	/* stderr is buffered on Win32. */
	setvbuf(stderr, NULL, _IONBF, 0);
#endif

	if ((env = getenv("PGHOST")) != NULL && *env != '\0')
		pghost = env;
	if ((env = getenv("PGPORT")) != NULL && *env != '\0')
		pgport = env;
	else if ((env = getenv("PGUSER")) != NULL && *env != '\0')
		login = env;

	state = (CState *) pg_malloc(sizeof(CState));
	memset(state, 0, sizeof(CState));

	/* set random seed early, because it may be used while parsing scripts. */
	if (!set_random_seed(getenv("PGBENCH_RANDOM_SEED")))
	{
		ereport(ELEVEL_FATAL,
				(errmsg("error while setting random seed from PGBENCH_RANDOM_SEED environment variable\n")));
	}

	while ((c = getopt_long(argc, argv, "iI:h:nvp:d:qb:SNc:j:Crs:t:T:U:lf:D:F:M:P:R:L:", long_options, &optindex)) != -1)
	{
		char	   *script;

		switch (c)
		{
			case 'i':
				is_init_mode = true;
				break;
			case 'I':
				if (initialize_steps)
					pg_free(initialize_steps);
				initialize_steps = pg_strdup(optarg);
				checkInitSteps(initialize_steps);
				initialization_option_set = true;
				break;
			case 'h':
				pghost = pg_strdup(optarg);
				break;
			case 'n':
				is_no_vacuum = true;
				break;
			case 'v':
				benchmarking_option_set = true;
				do_vacuum_accounts = true;
				break;
			case 'p':
				pgport = pg_strdup(optarg);
				break;
			case 'd':
				{
					for (debug_level = 0;
						 debug_level < NUM_DEBUGLEVEL;
						 debug_level++)
					{
						if (strcmp(optarg, DEBUGLEVEL[debug_level]) == 0)
							break;
					}
					if (debug_level >= NUM_DEBUGLEVEL)
					{
						ereport(ELEVEL_FATAL,
								(errmsg("invalid debug level (-d): \"%s\"\n",
										optarg)));
					}
					break;
				}
			case 'c':
				benchmarking_option_set = true;
				nclients = atoi(optarg);
				if (nclients <= 0 || nclients > MAXCLIENTS)
				{
					ereport(ELEVEL_FATAL,
							(errmsg("invalid number of clients: \"%s\"\n",
									optarg)));
				}
#ifdef HAVE_GETRLIMIT
#ifdef RLIMIT_NOFILE			/* most platforms use RLIMIT_NOFILE */
				if (getrlimit(RLIMIT_NOFILE, &rlim) == -1)
#else							/* but BSD doesn't ... */
				if (getrlimit(RLIMIT_OFILE, &rlim) == -1)
#endif							/* RLIMIT_NOFILE */
				{
					ereport(ELEVEL_FATAL,
							(errmsg("getrlimit failed: %s\n",
									strerror(errno))));
				}
				if (rlim.rlim_cur < nclients + 3)
				{
					ereport(ELEVEL_FATAL,
							(errmsg("need at least %d open files, but system limit is %ld\n"
									"Reduce number of clients, or use limit/ulimit to increase the system limit.\n",
									nclients + 3, (long) rlim.rlim_cur)));
				}
#endif							/* HAVE_GETRLIMIT */
				break;
			case 'j':			/* jobs */
				benchmarking_option_set = true;
				nthreads = atoi(optarg);
				if (nthreads <= 0)
				{
					ereport(ELEVEL_FATAL,
							(errmsg("invalid number of threads: \"%s\"\n",
									optarg)));
				}
#ifndef ENABLE_THREAD_SAFETY
				if (nthreads != 1)
				{
					ereport(ELEVEL_FATAL,
							(errmsg("threads are not supported on this platform; use -j1\n")));
				}
#endif							/* !ENABLE_THREAD_SAFETY */
				break;
			case 'C':
				benchmarking_option_set = true;
				is_connect = true;
				break;
			case 'r':
				benchmarking_option_set = true;
				report_per_command = true;
				break;
			case 's':
				scale_given = true;
				scale = atoi(optarg);
				if (scale <= 0)
				{
					ereport(ELEVEL_FATAL,
							(errmsg("invalid scaling factor: \"%s\"\n",
									optarg)));
				}
				break;
			case 't':
				benchmarking_option_set = true;
				nxacts = atoi(optarg);
				if (nxacts <= 0)
				{
					ereport(ELEVEL_FATAL,
							(errmsg("invalid number of transactions: \"%s\"\n",
									optarg)));
				}
				break;
			case 'T':
				benchmarking_option_set = true;
				duration = atoi(optarg);
				if (duration <= 0)
				{
					ereport(ELEVEL_FATAL,
							(errmsg("invalid duration: \"%s\"\n", optarg)));
				}
				break;
			case 'U':
				login = pg_strdup(optarg);
				break;
			case 'l':
				benchmarking_option_set = true;
				use_log = true;
				break;
			case 'q':
				initialization_option_set = true;
				use_quiet = true;
				break;
			case 'b':
				if (strcmp(optarg, "list") == 0)
				{
					listAvailableScripts();
					exit(0);
				}
				weight = parseScriptWeight(optarg, &script);
				process_builtin(findBuiltin(script), weight);
				benchmarking_option_set = true;
				internal_script_used = true;
				break;
			case 'S':
				process_builtin(findBuiltin("select-only"), 1);
				benchmarking_option_set = true;
				internal_script_used = true;
				break;
			case 'N':
				process_builtin(findBuiltin("simple-update"), 1);
				benchmarking_option_set = true;
				internal_script_used = true;
				break;
			case 'f':
				weight = parseScriptWeight(optarg, &script);
				process_file(script, weight);
				benchmarking_option_set = true;
				break;
			case 'D':
				{
					char	   *p;

					benchmarking_option_set = true;

					if ((p = strchr(optarg, '=')) == NULL || p == optarg || *(p + 1) == '\0')
					{
						ereport(ELEVEL_FATAL,
								(errmsg("invalid variable definition: \"%s\"\n",
										optarg)));
					}

					*p++ = '\0';
					putVariable(&state[0].variables, "option", optarg, p);
				}
				break;
			case 'F':
				initialization_option_set = true;
				fillfactor = atoi(optarg);
				set_fillfactor = true;
				if (fillfactor < 10 || fillfactor > 100)
				{
					ereport(ELEVEL_FATAL,
							(errmsg("invalid fillfactor: \"%s\"\n", optarg)));
				}
				break;
			case 'M':
				benchmarking_option_set = true;
				for (querymode = 0; querymode < NUM_QUERYMODE; querymode++)
					if (strcmp(optarg, QUERYMODE[querymode]) == 0)
						break;
				if (querymode >= NUM_QUERYMODE)
				{
					ereport(ELEVEL_FATAL,
							(errmsg("invalid query mode (-M): \"%s\"\n",
									optarg)));
				}
				break;
			case 'P':
				benchmarking_option_set = true;
				progress = atoi(optarg);
				if (progress <= 0)
				{
					ereport(ELEVEL_FATAL,
							(errmsg("invalid thread progress delay: \"%s\"\n",
									optarg)));
				}
				break;
			case 'R':
				{
					/* get a double from the beginning of option value */
					double		throttle_value = atof(optarg);

					benchmarking_option_set = true;

					if (throttle_value <= 0.0)
					{
						ereport(ELEVEL_FATAL,
								(errmsg("invalid rate limit: \"%s\"\n",
										optarg)));
					}
					/* Invert rate limit into a time offset */
					throttle_delay = (int64) (1000000.0 / throttle_value);
				}
				break;
			case 'L':
				{
					double		limit_ms = atof(optarg);

					if (limit_ms <= 0.0)
					{
						ereport(ELEVEL_FATAL,
								(errmsg("invalid latency limit: \"%s\"\n",
										optarg)));
					}
					benchmarking_option_set = true;
					latency_limit = (int64) (limit_ms * 1000);
				}
				break;
			case 1:				/* unlogged-tables */
				initialization_option_set = true;
				unlogged_tables = true;
				break;
			case 2:				/* tablespace */
				initialization_option_set = true;
				tablespace = pg_strdup(optarg);
				break;
			case 3:				/* index-tablespace */
				initialization_option_set = true;
				index_tablespace = pg_strdup(optarg);
				break;
			case 4:				/* sampling-rate */
				benchmarking_option_set = true;
				sample_rate = atof(optarg);
				if (sample_rate <= 0.0 || sample_rate > 1.0)
				{
					ereport(ELEVEL_FATAL,
							(errmsg("invalid sampling rate: \"%s\"\n",
									optarg)));
				}
				break;
			case 5:				/* aggregate-interval */
				benchmarking_option_set = true;
				agg_interval = atoi(optarg);
				if (agg_interval <= 0)
				{
					ereport(ELEVEL_FATAL,
							(errmsg("invalid number of seconds for aggregation: \"%s\"\n",
									optarg)));
				}
				break;
			case 6:				/* progress-timestamp */
				progress_timestamp = true;
				benchmarking_option_set = true;
				break;
			case 7:				/* log-prefix */
				benchmarking_option_set = true;
				logfile_prefix = pg_strdup(optarg);
				break;
			case 8:				/* foreign-keys */
				initialization_option_set = true;
				foreign_keys = true;
				break;
			case 9:				/* random-seed */
				benchmarking_option_set = true;
				if (!set_random_seed(optarg))
				{
					ereport(ELEVEL_FATAL,
							(errmsg("error while setting random seed from --random-seed option\n")));
				}
				break;
			case 10:			/* max-tries */
				{
					int32		max_tries_arg = atoi(optarg);

					if (max_tries_arg <= 0)
					{
						ereport(ELEVEL_FATAL,
								(errmsg("invalid number of maximum tries: \"%s\"\n",
										optarg)));
					}
					benchmarking_option_set = true;
					max_tries = (uint32) max_tries_arg;
				}
				break;
			default:
				ereport(ELEVEL_FATAL,
						(errmsg(_("Try \"%s --help\" for more information.\n"),
								progname)));
				break;
		}
	}

	/* set default script if none */
	if (num_scripts == 0 && !is_init_mode)
	{
		process_builtin(findBuiltin("tpcb-like"), 1);
		benchmarking_option_set = true;
		internal_script_used = true;
	}

	/* if not simple query mode, parse the script(s) to find parameters */
	if (querymode != QUERY_SIMPLE)
	{
		for (i = 0; i < num_scripts; i++)
		{
			Command   **commands = sql_script[i].commands;
			int			j;

			for (j = 0; commands[j] != NULL; j++)
			{
				if (commands[j]->type != SQL_COMMAND)
					continue;
				if (!parseQuery(commands[j]))
					exit(1);
			}
		}
	}

	/* compute total_weight */
	for (i = 0; i < num_scripts; i++)
		/* cannot overflow: weight is 32b, total_weight 64b */
		total_weight += sql_script[i].weight;

	if (total_weight == 0 && !is_init_mode)
	{
		ereport(ELEVEL_FATAL,
				(errmsg("total script weight must not be zero\n")));
	}

	/* show per script stats if several scripts are used */
	if (num_scripts > 1)
		per_script_stats = true;

	/*
	 * Don't need more threads than there are clients.  (This is not merely an
	 * optimization; throttle_delay is calculated incorrectly below if some
	 * threads have no clients assigned to them.)
	 */
	if (nthreads > nclients)
		nthreads = nclients;

	/* compute a per thread delay */
	throttle_delay *= nthreads;

	if (argc > optind)
		dbName = argv[optind];
	else
	{
		if ((env = getenv("PGDATABASE")) != NULL && *env != '\0')
			dbName = env;
		else if (login != NULL && *login != '\0')
			dbName = login;
		else
			dbName = "";
	}

	if (is_init_mode)
	{
		if (benchmarking_option_set)
		{
			ereport(ELEVEL_FATAL,
					(errmsg("some of the specified options cannot be used in initialization (-i) mode\n")));
		}

		if (initialize_steps == NULL)
			initialize_steps = pg_strdup(DEFAULT_INIT_STEPS);

		if (is_no_vacuum)
		{
			/* Remove any vacuum step in initialize_steps */
			char	   *p;

			while ((p = strchr(initialize_steps, 'v')) != NULL)
				*p = ' ';
		}

		if (foreign_keys)
		{
			/* Add 'f' to end of initialize_steps, if not already there */
			if (strchr(initialize_steps, 'f') == NULL)
			{
				initialize_steps = (char *)
					pg_realloc(initialize_steps,
							   strlen(initialize_steps) + 2);
				strcat(initialize_steps, "f");
			}
		}

		runInitSteps(initialize_steps);
		exit(0);
	}
	else
	{
		if (initialization_option_set)
		{
			ereport(ELEVEL_FATAL,
					(errmsg("some of the specified options cannot be used in benchmarking mode\n")));
		}
	}

	if (nxacts > 0 && duration > 0)
	{
		ereport(ELEVEL_FATAL,
				(errmsg("specify either a number of transactions (-t) or a duration (-T), not both\n")));
	}

	/* Use DEFAULT_NXACTS if neither nxacts nor duration is specified. */
	if (nxacts <= 0 && duration <= 0)
		nxacts = DEFAULT_NXACTS;

	/* --sampling-rate may be used only with -l */
	if (sample_rate > 0.0 && !use_log)
	{
		ereport(ELEVEL_FATAL,
				(errmsg("log sampling (--sampling-rate) is allowed only when logging transactions (-l)\n")));
	}

	/* --sampling-rate may not be used with --aggregate-interval */
	if (sample_rate > 0.0 && agg_interval > 0)
	{
		ereport(ELEVEL_FATAL,
				(errmsg("log sampling (--sampling-rate) and aggregation (--aggregate-interval) cannot be used at the same time\n")));
	}

	if (agg_interval > 0 && !use_log)
	{
		ereport(ELEVEL_FATAL,
				(errmsg("log aggregation is allowed only when actually logging transactions\n")));
	}

	if (!use_log && logfile_prefix)
	{
		ereport(ELEVEL_FATAL,
				(errmsg("log file prefix (--log-prefix) is allowed only when logging transactions (-l)\n")));
	}

	if (duration > 0 && agg_interval > duration)
	{
		ereport(ELEVEL_FATAL,
				(errmsg("number of seconds for aggregation (%d) must not be higher than test duration (%d)\n",
						agg_interval, duration)));
	}

	if (duration > 0 && agg_interval > 0 && duration % agg_interval != 0)
	{
		ereport(ELEVEL_FATAL,
				(errmsg("duration (%d) must be a multiple of aggregation interval (%d)\n",
						duration, agg_interval)));
	}

	if (progress_timestamp && progress == 0)
	{
		ereport(ELEVEL_FATAL,
				(errmsg("--progress-timestamp is allowed only under --progress\n")));
	}

	/* If necessary set the default tries limit  */
	if (!max_tries && !latency_limit)
		max_tries = 1;

	/*
	 * save main process id in the global variable because process id will be
	 * changed after fork.
	 */
	main_pid = (int) getpid();

	if (nclients > 1)
	{
		state = (CState *) pg_realloc(state, sizeof(CState) * nclients);
		memset(state + 1, 0, sizeof(CState) * (nclients - 1));

		/* copy any -D switch values to all clients */
		for (i = 1; i < nclients; i++)
		{
			int			j;

			state[i].id = i;
			for (j = 0; j < state[0].variables.nvariables; j++)
			{
				Variable   *var = &state[0].variables.array[j];

				if (var->value.type != PGBT_NO_VALUE)
				{
					putVariableValue(&state[i].variables, "startup", var->name,
									 &var->value, false);
				}
				else
				{
					putVariable(&state[i].variables, "startup", var->name,
								var->svalue);
				}
			}
		}
	}

	/* other CState initializations */
	for (i = 0; i < nclients; i++)
	{
		state[i].cstack = conditional_stack_create();
		initRandomState(&state[i].random_state);
	}

	if (duration <= 0)
		ereport(ELEVEL_DEBUG,
				(errmsg("pghost: %s pgport: %s nclients: %d nxacts: %d dbName: %s\n",
						pghost, pgport, nclients, nxacts, dbName)));
	else
		ereport(ELEVEL_DEBUG,
				(errmsg("pghost: %s pgport: %s nclients: %d duration: %d dbName: %s\n",
						pghost, pgport, nclients, duration, dbName)));

	/* opening connection... */
	con = doConnect();
	if (con == NULL)
		exit(1);

	if (PQstatus(con) == CONNECTION_BAD)
	{
		ereport(ELEVEL_FATAL,
				(errmsg("connection to database \"%s\" failed\n%s",
						dbName, PQerrorMessage(con))));
	}

	if (internal_script_used)
	{
		/*
		 * get the scaling factor that should be same as count(*) from
		 * ysql_bench_branches if this is not a custom query
		 */
		res = PQexec(con, "select count(*) from ysql_bench_branches");
		if (PQresultStatus(res) != PGRES_TUPLES_OK)
		{
			char	   *sqlState = PQresultErrorField(res, PG_DIAG_SQLSTATE);
			PQExpBufferData errmsg_buf;

			initPQExpBuffer(&errmsg_buf);
			printfPQExpBuffer(&errmsg_buf, "%s", PQerrorMessage(con));
			if (sqlState && strcmp(sqlState, ERRCODE_UNDEFINED_TABLE) == 0)
			{
				appendPQExpBuffer(&errmsg_buf,
								  "Perhaps you need to do initialization (\"ysql_bench -i\") in database \"%s\"\n",
								  PQdb(con));
			}

			ereport(ELEVEL_LOG_MAIN, (errmsg("%s", errmsg_buf.data)));
			termPQExpBuffer(&errmsg_buf);
			exit(1);
		}
		scale = atoi(PQgetvalue(res, 0, 0));
		if (scale < 0)
		{
			ereport(ELEVEL_FATAL,
					(errmsg("invalid count(*) from ysql_bench_branches: \"%s\"\n",
							PQgetvalue(res, 0, 0))));
		}
		PQclear(res);

		/* warn if we override user-given -s switch */
		if (scale_given)
			ereport(ELEVEL_LOG_MAIN,
					(errmsg("scale option ignored, using count from ysql_bench_branches table (%d)\n",
							scale)));
	}

	/*
	 * :scale variables normally get -s or database scale, but don't override
	 * an explicit -D switch
	 */
	if (lookupVariable(&state[0].variables, "scale") == NULL)
	{
		for (i = 0; i < nclients; i++)
		{
			putVariableInt(&state[i].variables, "startup", "scale", scale,
						   false);
		}
	}

	/*
	 * Define a :client_id variable that is unique per connection. But don't
	 * override an explicit -D switch.
	 */
	if (lookupVariable(&state[0].variables, "client_id") == NULL)
	{
		for (i = 0; i < nclients; i++)
			putVariableInt(&state[i].variables, "startup", "client_id", i,
						   false);
	}

	/* set default seed for hash functions */
	if (lookupVariable(&state[0].variables, "default_seed") == NULL)
	{
		uint64		seed =
		((uint64) pg_jrand48(base_random_sequence) & 0xFFFFFFFF) |
		(((uint64) pg_jrand48(base_random_sequence) & 0xFFFFFFFF) << 32);

		for (i = 0; i < nclients; i++)
			putVariableInt(&state[i].variables, "startup", "default_seed",
						   (int64) seed, false);
	}

	/* set random seed unless overwritten */
	if (lookupVariable(&state[0].variables, "random_seed") == NULL)
	{
		for (i = 0; i < nclients; i++)
			putVariableInt(&state[i].variables, "startup", "random_seed",
						   random_seed, false);
	}

	if (!is_no_vacuum)
	{
		ereport(ELEVEL_LOG_MAIN, (errmsg("starting vacuum...")));
		tryExecuteStatement(con, "vacuum ysql_bench_branches");
		tryExecuteStatement(con, "vacuum ysql_bench_tellers");
		tryExecuteStatement(con, "truncate ysql_bench_history");
		ereport(ELEVEL_LOG_MAIN, (errmsg("end.\n")));

		if (do_vacuum_accounts)
		{
			ereport(ELEVEL_LOG_MAIN,
					(errmsg("starting vacuum ysql_bench_accounts...")));
			tryExecuteStatement(con, "vacuum analyze ysql_bench_accounts");
			ereport(ELEVEL_LOG_MAIN, (errmsg("end.\n")));
		}
	}
	PQfinish(con);

	/* set up thread data structures */
	threads = (TState *) pg_malloc(sizeof(TState) * nthreads);
	nclients_dealt = 0;

	for (i = 0; i < nthreads; i++)
	{
		TState	   *thread = &threads[i];

		thread->tid = i;
		thread->state = &state[nclients_dealt];
		thread->nstate =
			(nclients - nclients_dealt + nthreads - i - 1) / (nthreads - i);
		initRandomState(&thread->random_state);
		thread->logfile = NULL; /* filled in later */
		thread->latency_late = 0;
		thread->zipf_cache.nb_cells = 0;
		thread->zipf_cache.current = 0;
		thread->zipf_cache.overflowCount = 0;
		initStats(&thread->stats, 0);

		nclients_dealt += thread->nstate;
	}

	/* all clients must be assigned to a thread */
	Assert(nclients_dealt == nclients);

	/* get start up time */
	INSTR_TIME_SET_CURRENT(start_time);

	/* set alarm if duration is specified. */
	if (duration > 0)
		setalarm(duration);

	/* start threads */
#ifdef ENABLE_THREAD_SAFETY
	for (i = 0; i < nthreads; i++)
	{
		TState	   *thread = &threads[i];

		INSTR_TIME_SET_CURRENT(thread->start_time);

		/* compute when to stop */
		if (duration > 0)
			end_time = INSTR_TIME_GET_MICROSEC(thread->start_time) +
				(int64) 1000000 * duration;

		/* the first thread (i = 0) is executed by main thread */
		if (i > 0)
		{
			int			err = pthread_create(&thread->thread, NULL, threadRun, thread);

			if (err != 0 || thread->thread == INVALID_THREAD)
			{
				ereport(ELEVEL_FATAL,
						(errmsg("could not create thread: %s\n",
								strerror(err))));
			}
		}
		else
		{
			thread->thread = INVALID_THREAD;
		}
	}
#else
	INSTR_TIME_SET_CURRENT(threads[0].start_time);
	/* compute when to stop */
	if (duration > 0)
		end_time = INSTR_TIME_GET_MICROSEC(threads[0].start_time) +
			(int64) 1000000 * duration;
	threads[0].thread = INVALID_THREAD;
#endif							/* ENABLE_THREAD_SAFETY */

	/* wait for threads and accumulate results */
	initStats(&stats, 0);
	INSTR_TIME_SET_ZERO(conn_total_time);
	for (i = 0; i < nthreads; i++)
	{
		TState	   *thread = &threads[i];

#ifdef ENABLE_THREAD_SAFETY
		if (threads[i].thread == INVALID_THREAD)
			/* actually run this thread directly in the main thread */
			(void) threadRun(thread);
		else
			/* wait of other threads. should check that 0 is returned? */
			pthread_join(thread->thread, NULL);
#else
		(void) threadRun(thread);
#endif							/* ENABLE_THREAD_SAFETY */

		/* aggregate thread level stats */
		mergeSimpleStats(&stats.latency, &thread->stats.latency);
		mergeSimpleStats(&stats.lag, &thread->stats.lag);
		stats.cnt += thread->stats.cnt;
		stats.skipped += thread->stats.skipped;
		stats.retries += thread->stats.retries;
		stats.retried += thread->stats.retried;
		stats.errors += thread->stats.errors;
		stats.errors_in_failed_tx += thread->stats.errors_in_failed_tx;
		latency_late += thread->latency_late;
		INSTR_TIME_ADD(conn_total_time, thread->conn_time);
	}
	disconnect_all(state, nclients);

	/*
	 * XXX We compute results as though every client of every thread started
	 * and finished at the same time.  That model can diverge noticeably from
	 * reality for a short benchmark run involving relatively many threads.
	 * The first thread may process notably many transactions before the last
	 * thread begins.  Improving the model alone would bring limited benefit,
	 * because performance during those periods of partial thread count can
	 * easily exceed steady state performance.  This is one of the many ways
	 * short runs convey deceptive performance figures.
	 */
	INSTR_TIME_SET_CURRENT(total_time);
	INSTR_TIME_SUBTRACT(total_time, start_time);
	printResults(threads, &stats, total_time, conn_total_time, latency_late);

	return 0;
}

static void *
threadRun(void *arg)
{
	TState	   *thread = (TState *) arg;
	CState	   *state = thread->state;
	instr_time	start,
				end;
	int			nstate = thread->nstate;
	int			remains = nstate;	/* number of remaining clients */
	int			i;

	/* for reporting progress: */
	int64		thread_start = INSTR_TIME_GET_MICROSEC(thread->start_time);
	int64		last_report = thread_start;
	int64		next_report = last_report + (int64) progress * 1000000;
	StatsData	last,
				aggs;

	/*
	 * Initialize throttling rate target for all of the thread's clients.  It
	 * might be a little more accurate to reset thread->start_time here too.
	 * The possible drift seems too small relative to typical throttle delay
	 * times to worry about it.
	 */
	INSTR_TIME_SET_CURRENT(start);
	thread->throttle_trigger = INSTR_TIME_GET_MICROSEC(start);

	INSTR_TIME_SET_ZERO(thread->conn_time);

	initStats(&aggs, time(NULL));
	last = aggs;

	/* open log file if requested */
	if (use_log)
	{
		char		logpath[MAXPGPATH];
		char	   *prefix = logfile_prefix ? logfile_prefix : "ysql_bench_log";

		if (thread->tid == 0)
			snprintf(logpath, sizeof(logpath), "%s.%d", prefix, main_pid);
		else
			snprintf(logpath, sizeof(logpath), "%s.%d.%d", prefix, main_pid, thread->tid);

		thread->logfile = fopen(logpath, "w");

		if (thread->logfile == NULL)
		{
			ereport(ELEVEL_LOG_MAIN,
					(errmsg("could not open logfile \"%s\": %s\n",
							logpath, strerror(errno))));
			goto done;
		}
	}

	if (!is_connect)
	{
		/* make connections to the database */
		for (i = 0; i < nstate; i++)
		{
			if ((state[i].con = doConnect()) == NULL)
				goto done;
		}
	}

	/* time after thread and connections set up */
	INSTR_TIME_SET_CURRENT(thread->conn_time);
	INSTR_TIME_SUBTRACT(thread->conn_time, thread->start_time);

	/* explicitly initialize the state machines */
	for (i = 0; i < nstate; i++)
	{
		state[i].state = CSTATE_CHOOSE_SCRIPT;
	}

	/* loop till all clients have terminated */
	while (remains > 0)
	{
		fd_set		input_mask;
		int			maxsock;	/* max socket number to be waited for */
		int64		min_usec;
		int64		now_usec = 0;	/* set this only if needed */

		/* identify which client sockets should be checked for input */
		FD_ZERO(&input_mask);
		maxsock = -1;
		min_usec = PG_INT64_MAX;
		for (i = 0; i < nstate; i++)
		{
			CState	   *st = &state[i];

			if (st->state == CSTATE_THROTTLE && timer_exceeded)
			{
				/* interrupt client that has not started a transaction */
				st->state = CSTATE_FINISHED;
				finishCon(st);
				remains--;
			}
			else if (st->state == CSTATE_SLEEP || st->state == CSTATE_THROTTLE)
			{
				/* a nap from the script, or under throttling */
				int64		this_usec;

				/* get current time if needed */
				if (now_usec == 0)
				{
					instr_time	now;

					INSTR_TIME_SET_CURRENT(now);
					now_usec = INSTR_TIME_GET_MICROSEC(now);
				}

				/* min_usec should be the minimum delay across all clients */
				this_usec = (st->state == CSTATE_SLEEP ?
							 st->sleep_until : st->txn_scheduled) - now_usec;
				if (min_usec > this_usec)
					min_usec = this_usec;
			}
			else if (st->state == CSTATE_WAIT_RESULT)
			{
				/*
				 * waiting for result from server - nothing to do unless the
				 * socket is readable
				 */
				int			sock = PQsocket(st->con);

				if (sock < 0)
				{
					ereport(ELEVEL_LOG_MAIN,
							(errmsg("invalid socket: %s",
									PQerrorMessage(st->con))));
					goto done;
				}

				FD_SET(sock, &input_mask);
				if (maxsock < sock)
					maxsock = sock;
			}
			else if (st->state != CSTATE_ABORTED &&
					 st->state != CSTATE_FINISHED)
			{
				/*
				 * This client thread is ready to do something, so we don't
				 * want to wait.  No need to examine additional clients.
				 */
				min_usec = 0;
				break;
			}
		}

		/* also wake up to print the next progress report on time */
		if (progress && min_usec > 0 && thread->tid == 0)
		{
			/* get current time if needed */
			if (now_usec == 0)
			{
				instr_time	now;

				INSTR_TIME_SET_CURRENT(now);
				now_usec = INSTR_TIME_GET_MICROSEC(now);
			}

			if (now_usec >= next_report)
				min_usec = 0;
			else if ((next_report - now_usec) < min_usec)
				min_usec = next_report - now_usec;
		}

		/*
		 * If no clients are ready to execute actions, sleep until we receive
		 * data from the server, or a nap-time specified in the script ends,
		 * or it's time to print a progress report.  Update input_mask to show
		 * which client(s) received data.
		 */
		if (min_usec > 0)
		{
			int			nsocks = 0; /* return from select(2) if called */

			if (min_usec != PG_INT64_MAX)
			{
				if (maxsock != -1)
				{
					struct timeval timeout;

					timeout.tv_sec = min_usec / 1000000;
					timeout.tv_usec = min_usec % 1000000;
					nsocks = select(maxsock + 1, &input_mask, NULL, NULL, &timeout);
				}
				else			/* nothing active, simple sleep */
				{
					pg_usleep(min_usec);
				}
			}
			else				/* no explicit delay, select without timeout */
			{
				nsocks = select(maxsock + 1, &input_mask, NULL, NULL, NULL);
			}

			if (nsocks < 0)
			{
				if (errno == EINTR)
				{
					/* On EINTR, go back to top of loop */
					continue;
				}
				/* must be something wrong */
				ereport(ELEVEL_LOG_MAIN,
						(errmsg("select() failed: %s\n", strerror(errno))));
				goto done;
			}
		}
		else
		{
			/* min_usec == 0, i.e. something needs to be executed */

			/* If we didn't call select(), don't try to read any data */
			FD_ZERO(&input_mask);
		}

		/* ok, advance the state machine of each connection */
		for (i = 0; i < nstate; i++)
		{
			CState	   *st = &state[i];

			if (st->state == CSTATE_WAIT_RESULT)
			{
				/* don't call doCustom unless data is available */
				int			sock = PQsocket(st->con);

				if (sock < 0)
				{
					ereport(ELEVEL_LOG_MAIN,
							(errmsg("invalid socket: %s",
									PQerrorMessage(st->con))));
					goto done;
				}

				if (!FD_ISSET(sock, &input_mask))
					continue;
			}
			else if (st->state == CSTATE_FINISHED ||
					 st->state == CSTATE_ABORTED)
			{
				/* this client is done, no need to consider it anymore */
				continue;
			}

			doCustom(thread, st, &aggs);

			/* If doCustom changed client to finished state, reduce remains */
			if (st->state == CSTATE_FINISHED || st->state == CSTATE_ABORTED)
				remains--;
		}

		/* progress report is made by thread 0 for all threads */
		if (progress && thread->tid == 0)
		{
			instr_time	now_time;
			int64		now;

			INSTR_TIME_SET_CURRENT(now_time);
			now = INSTR_TIME_GET_MICROSEC(now_time);
			if (now >= next_report)
			{
				/* generate and show report */
				StatsData	cur;
				int64		run = now - last_report,
							ntx,
							retries,
							retried,
							errors,
							errors_in_failed_tx;
				double		tps,
							total_run,
							latency,
							sqlat,
							lag,
							stdev;
				char		tbuf[315];
				PQExpBufferData progress_buf;

				/*
				 * Add up the statistics of all threads.
				 *
				 * XXX: No locking. There is no guarantee that we get an
				 * atomic snapshot of the transaction count and latencies, so
				 * these figures can well be off by a small amount. The
				 * progress report's purpose is to give a quick overview of
				 * how the test is going, so that shouldn't matter too much.
				 * (If a read from a 64-bit integer is not atomic, you might
				 * get a "torn" read and completely bogus latencies though!)
				 */
				initStats(&cur, 0);
				for (i = 0; i < nthreads; i++)
				{
					mergeSimpleStats(&cur.latency, &thread[i].stats.latency);
					mergeSimpleStats(&cur.lag, &thread[i].stats.lag);
					cur.cnt += thread[i].stats.cnt;
					cur.skipped += thread[i].stats.skipped;
					cur.retries += thread[i].stats.retries;
					cur.retried += thread[i].stats.retried;
					cur.errors += thread[i].stats.errors;
					cur.errors_in_failed_tx +=
						thread[i].stats.errors_in_failed_tx;
				}

				/* we count only actually executed transactions */
				ntx = (cur.cnt - cur.skipped) - (last.cnt - last.skipped);
				total_run = (now - thread_start) / 1000000.0;
				tps = 1000000.0 * ntx / run;
				if (ntx > 0)
				{
					latency = 0.001 * (cur.latency.sum - last.latency.sum) / ntx;
					sqlat = 1.0 * (cur.latency.sum2 - last.latency.sum2) / ntx;
					stdev = 0.001 * sqrt(sqlat - 1000000.0 * latency * latency);
					lag = 0.001 * (cur.lag.sum - last.lag.sum) / ntx;
				}
				else
				{
					latency = sqlat = stdev = lag = 0;
				}
				retries = cur.retries - last.retries;
				retried = cur.retried - last.retried;
				errors = cur.errors - last.errors;
				errors_in_failed_tx = cur.errors_in_failed_tx -
					last.errors_in_failed_tx;

				if (progress_timestamp)
				{
					/*
					 * On some platforms the current system timestamp is
					 * available in now_time, but rather than get entangled
					 * with that, we just eat the cost of an extra syscall in
					 * all cases.
					 */
					struct timeval tv;

					gettimeofday(&tv, NULL);
					snprintf(tbuf, sizeof(tbuf), "%ld.%03ld s",
							 (long) tv.tv_sec, (long) (tv.tv_usec / 1000));
				}
				else
				{
					/* round seconds are expected, but the thread may be late */
					snprintf(tbuf, sizeof(tbuf), "%.1f s", total_run);
				}

				initPQExpBuffer(&progress_buf);
				printfPQExpBuffer(&progress_buf,
								  "progress: %s, %.1f tps, lat %.3f ms stddev %.3f",
								  tbuf, tps, latency, stdev);

				if (errors > 0)
				{
					appendPQExpBuffer(&progress_buf,
									  ", " INT64_FORMAT " failed" , errors);
					if (errors_in_failed_tx > 0)
						appendPQExpBuffer(&progress_buf,
										  " (" INT64_FORMAT " in failed tx)",
										  errors_in_failed_tx);
				}

				if (throttle_delay)
				{
					appendPQExpBuffer(&progress_buf, ", lag %.3f ms", lag);
					if (latency_limit)
						appendPQExpBuffer(&progress_buf,
										  ", " INT64_FORMAT " skipped",
										  cur.skipped - last.skipped);
				}

				/*
				 * It can be non-zero only if max_tries is greater than one or
				 * latency_limit is used.
				 */
				if (retried > 0)
				{
					appendPQExpBuffer(&progress_buf,
									  ", " INT64_FORMAT " retried, " INT64_FORMAT " retries",
									  retried, retries);
				}
				appendPQExpBufferChar(&progress_buf, '\n');

				ereport(ELEVEL_LOG_MAIN, (errmsg("%s", progress_buf.data)));
				termPQExpBuffer(&progress_buf);

				last = cur;
				last_report = now;

				/*
				 * Ensure that the next report is in the future, in case
				 * pgbench/postgres got stuck somewhere.
				 */
				do
				{
					next_report += (int64) progress * 1000000;
				} while (now >= next_report);
			}
		}
	}

done:
	INSTR_TIME_SET_CURRENT(start);
	disconnect_all(state, nstate);
	INSTR_TIME_SET_CURRENT(end);
	INSTR_TIME_ACCUM_DIFF(thread->conn_time, end, start);
	if (thread->logfile)
	{
		if (agg_interval > 0)
		{
			/* log aggregated but not yet reported transactions */
			doLog(thread, state, &aggs, false, 0, 0);
		}
		fclose(thread->logfile);
		thread->logfile = NULL;
	}
	return NULL;
}

static void
finishCon(CState *st)
{
	if (st->con != NULL)
	{
		PQfinish(st->con);
		st->con = NULL;
	}
}

/*
 * Support for duration option: set timer_exceeded after so many seconds.
 */

#ifndef WIN32

static void
handle_sig_alarm(SIGNAL_ARGS)
{
	timer_exceeded = true;
}

static void
setalarm(int seconds)
{
	pqsignal(SIGALRM, handle_sig_alarm);
	alarm(seconds);
}

#else							/* WIN32 */

static VOID CALLBACK
win32_timer_callback(PVOID lpParameter, BOOLEAN TimerOrWaitFired)
{
	timer_exceeded = true;
}

static void
setalarm(int seconds)
{
	HANDLE		queue;
	HANDLE		timer;

	/* This function will be called at most once, so we can cheat a bit. */
	queue = CreateTimerQueue();
	if (seconds > ((DWORD) -1) / 1000 ||
		!CreateTimerQueueTimer(&timer, queue,
							   win32_timer_callback, NULL, seconds * 1000, 0,
							   WT_EXECUTEINTIMERTHREAD | WT_EXECUTEONLYONCE))
		ereport(ELEVEL_FATAL, (errmsg("failed to set timer\n")));
}

/* partial pthread implementation for Windows */

typedef struct win32_pthread
{
	HANDLE		handle;
	void	   *(*routine) (void *);
	void	   *arg;
	void	   *result;
} win32_pthread;

static unsigned __stdcall
win32_pthread_run(void *arg)
{
	win32_pthread *th = (win32_pthread *) arg;

	th->result = th->routine(th->arg);

	return 0;
}

static int
pthread_create(pthread_t *thread,
			   pthread_attr_t *attr,
			   void *(*start_routine) (void *),
			   void *arg)
{
	int			save_errno;
	win32_pthread *th;

	th = (win32_pthread *) pg_malloc(sizeof(win32_pthread));
	th->routine = start_routine;
	th->arg = arg;
	th->result = NULL;

	th->handle = (HANDLE) _beginthreadex(NULL, 0, win32_pthread_run, th, 0, NULL);
	if (th->handle == NULL)
	{
		save_errno = errno;
		free(th);
		return save_errno;
	}

	*thread = th;
	return 0;
}

static int
pthread_join(pthread_t th, void **thread_return)
{
	if (th == NULL || th->handle == NULL)
		return errno = EINVAL;

	if (WaitForSingleObject(th->handle, INFINITE) != WAIT_OBJECT_0)
	{
		_dosmaperr(GetLastError());
		return errno;
	}

	if (thread_return)
		*thread_return = th->result;

	CloseHandle(th->handle);
	free(th);
	return 0;
}

#endif							/* WIN32 */

/*
 * errstartImpl --- begin an error-reporting cycle
 *
 * Initialize the error data and store the given parameters in it.
 * Subsequently, errmsg() and perhaps other routines will be called to further
 * populate the error data.  Finally, errfinish() will be called to actually
 * process the error report.  If multiple threads can use the same error data,
 * the error mutex is locked before the error data is initialized and will be
 * unlocked in the end of the errfinish() call.
 *
 * Returns true in normal case.  Returns false to short-circuit the error
 * report (if the debugging level does not resolve this error/logging level).
 */
static bool
#if defined(ENABLE_THREAD_SAFETY) && defined(HAVE__VA_ARGS)
errstartImpl(Error error, ErrorLevel elevel)
#else							/* !(ENABLE_THREAD_SAFETY && HAVE__VA_ARGS) */
errstartImpl(ErrorLevel elevel)
#endif							/* ENABLE_THREAD_SAFETY && HAVE__VA_ARGS */
{
	bool		start_error_reporting;

	/* Check if we have the appropriate debugging level */
	switch (elevel)
	{
		case ELEVEL_DEBUG:
			/*
			 * Print the message only if there's a debugging mode for all types
			 * of messages.
			 */
			start_error_reporting = debug_level >= DEBUG_ALL;
			break;
		case ELEVEL_LOG_CLIENT_FAIL:
			/*
			 * Print a failure message only if there's at least a debugging mode
			 * for fails.
			 */
			start_error_reporting = debug_level >= DEBUG_FAILS;
			break;
		case ELEVEL_LOG_CLIENT_ABORTED:
		case ELEVEL_LOG_MAIN:
		case ELEVEL_FATAL:
			/*
			 * Always print an error message if the client is aborted or this is
			 * the main program error/log message.
			 */
			start_error_reporting = true;
			break;
		default:
			/* internal error which should never occur */
			ereport(ELEVEL_FATAL,
					(errmsg("unexpected error level: %d\n", elevel)));
			break;
	}

	/* Initialize the error data */
	if (start_error_reporting)
	{
		Assert(error);

#if defined(ENABLE_THREAD_SAFETY) && !defined(HAVE__VA_ARGS)
		pthread_mutex_lock(&error_mutex);
#endif							/* ENABLE_THREAD_SAFETY && !HAVE__VA_ARGS */

		error->elevel = elevel;
		initPQExpBuffer(&error->message);
	}

	return start_error_reporting;
}

/*
 * errmsgImpl --- add a primary error message text to the current error
 */
static int
#if defined(ENABLE_THREAD_SAFETY) && defined(HAVE__VA_ARGS)
errmsgImpl(Error error, const char *fmt,...)
#else							/* !(ENABLE_THREAD_SAFETY && HAVE__VA_ARGS) */
errmsgImpl(const char *fmt,...)
#endif							/* ENABLE_THREAD_SAFETY && HAVE__VA_ARGS */
{
	va_list		ap;
	bool		done;

	Assert(error);

	if (PQExpBufferBroken(&error->message))
	{
		/* Already failed. */
		/* Return value does not matter. */
		return 0;
	}

	/* Loop in case we have to retry after enlarging the buffer. */
	do
	{
		va_start(ap, fmt);
		done = appendPQExpBufferVA(&error->message, fmt, ap);
		va_end(ap);
	} while (!done);

	/* Return value does not matter. */
	return 0;
}

/*
 * errfinishImpl --- end an error-reporting cycle
 *
 * Print the appropriate error report to stderr.
 *
 * If elevel is ELEVEL_FATAL or worse, control does not return to the caller.
 * See ErrorLevel enumeration for the error level definitions.
 *
 * If the error message buffer is empty or broken, prints a corresponding error
 * message and exits the program.
 */
static void
#if defined(ENABLE_THREAD_SAFETY) && defined(HAVE__VA_ARGS)
errfinishImpl(Error error, int dummy,...)
#else							/* !(ENABLE_THREAD_SAFETY && HAVE__VA_ARGS) */
errfinishImpl(int dummy,...)
#endif							/* ENABLE_THREAD_SAFETY && HAVE__VA_ARGS */
{
	bool		error_during_reporting = false;
	ErrorLevel  elevel;

	Assert(error);
	elevel = error->elevel;

	/*
	 * Immediately print the message to stderr so as not to get an endless cycle
	 * of errors...
	 */
	if (PQExpBufferDataBroken(error->message))
	{
		error_during_reporting = true;
		fprintf(stderr, "out of memory\n");
	}
	else if (*(error->message.data) == '\0')
	{
		/* internal error which should never occur */
		error_during_reporting = true;
		fprintf(stderr, "empty error message cannot be reported\n");
	}
	else
	{
		fprintf(stderr, "%s", error->message.data);
	}

	/* Release the error data and exit if needed */

	termPQExpBuffer(&error->message);

#if defined(ENABLE_THREAD_SAFETY) && !defined(HAVE__VA_ARGS)
	pthread_mutex_unlock(&error_mutex);
#endif							/* ENABLE_THREAD_SAFETY && !HAVE__VA_ARGS */

	if (elevel >= ELEVEL_FATAL || error_during_reporting)
		exit(1);
}
