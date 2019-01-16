/*-------------------------------------------------------------------------
 *
 * pg_resetwal.c
 *	  A utility to "zero out" the xlog when it's corrupt beyond recovery.
 *	  Can also rebuild pg_control if needed.
 *
 * The theory of operation is fairly simple:
 *	  1. Read the existing pg_control (which will include the last
 *		 checkpoint record).  If it is an old format then update to
 *		 current format.
 *	  2. If pg_control is corrupt, attempt to intuit reasonable values,
 *		 by scanning the old xlog if necessary.
 *	  3. Modify pg_control to reflect a "shutdown" state with a checkpoint
 *		 record at the start of xlog.
 *	  4. Flush the existing xlog files and write a new segment with
 *		 just a checkpoint record in it.  The new segment is positioned
 *		 just past the end of the old xlog, so that existing LSNs in
 *		 data pages will appear to be "in the past".
 * This is all pretty straightforward except for the intuition part of
 * step 2 ...
 *
 *
 * Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/bin/pg_resetwal/pg_resetwal.c
 *
 *-------------------------------------------------------------------------
 */

/*
 * We have to use postgres.h not postgres_fe.h here, because there's so much
 * backend-only stuff in the XLOG include files we need.  But we need a
 * frontend-ish environment otherwise.  Hence this ugly hack.
 */
#define FRONTEND 1

#include "postgres.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "access/transam.h"
#include "access/tuptoaster.h"
#include "access/multixact.h"
#include "access/xlog.h"
#include "access/xlog_internal.h"
#include "catalog/catversion.h"
#include "catalog/pg_control.h"
#include "common/fe_memutils.h"
#include "common/file_perm.h"
#include "common/restricted_token.h"
#include "storage/large_object.h"
#include "pg_getopt.h"
#include "getopt_long.h"


static ControlFileData ControlFile; /* pg_control values */
static XLogSegNo newXlogSegNo;	/* new XLOG segment # */
static bool guessed = false;	/* T if we had to guess at any values */
static const char *progname;
static uint32 set_xid_epoch = (uint32) -1;
static TransactionId set_xid = 0;
static TransactionId set_oldest_commit_ts_xid = 0;
static TransactionId set_newest_commit_ts_xid = 0;
static Oid	set_oid = 0;
static MultiXactId set_mxid = 0;
static MultiXactOffset set_mxoff = (MultiXactOffset) -1;
static uint32 minXlogTli = 0;
static XLogSegNo minXlogSegNo = 0;
static int	WalSegSz;
static int	set_wal_segsize;

static void CheckDataVersion(void);
static bool ReadControlFile(void);
static void GuessControlValues(void);
static void PrintControlValues(bool guessed);
static void PrintNewControlValues(void);
static void RewriteControlFile(void);
static void FindEndOfXLOG(void);
static void KillExistingXLOG(void);
static void KillExistingArchiveStatus(void);
static void WriteEmptyXLOG(void);
static void usage(void);


int
main(int argc, char *argv[])
{
	static struct option long_options[] = {
		{"commit-timestamp-ids", required_argument, NULL, 'c'},
		{"pgdata", required_argument, NULL, 'D'},
		{"epoch", required_argument, NULL, 'e'},
		{"force", no_argument, NULL, 'f'},
		{"next-wal-file", required_argument, NULL, 'l'},
		{"multixact-ids", required_argument, NULL, 'm'},
		{"dry-run", no_argument, NULL, 'n'},
		{"next-oid", required_argument, NULL, 'o'},
		{"multixact-offset", required_argument, NULL, 'O'},
		{"next-transaction-id", required_argument, NULL, 'x'},
		{"wal-segsize", required_argument, NULL, 1},
		{NULL, 0, NULL, 0}
	};

	int			c;
	bool		force = false;
	bool		noupdate = false;
	MultiXactId set_oldestmxid = 0;
	char	   *endptr;
	char	   *endptr2;
	char	   *DataDir = NULL;
	char	   *log_fname = NULL;
	int			fd;

	set_pglocale_pgservice(argv[0], PG_TEXTDOMAIN("pg_resetwal"));

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
			puts("pg_resetwal (PostgreSQL) " PG_VERSION);
			exit(0);
		}
	}


	while ((c = getopt_long(argc, argv, "c:D:e:fl:m:no:O:x:", long_options, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
				DataDir = optarg;
				break;

			case 'f':
				force = true;
				break;

			case 'n':
				noupdate = true;
				break;

			case 'e':
				set_xid_epoch = strtoul(optarg, &endptr, 0);
				if (endptr == optarg || *endptr != '\0')
				{
					/*------
					  translator: the second %s is a command line argument (-e, etc) */
					fprintf(stderr, _("%s: invalid argument for option %s\n"), progname, "-e");
					fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
					exit(1);
				}
				if (set_xid_epoch == -1)
				{
					fprintf(stderr, _("%s: transaction ID epoch (-e) must not be -1\n"), progname);
					exit(1);
				}
				break;

			case 'x':
				set_xid = strtoul(optarg, &endptr, 0);
				if (endptr == optarg || *endptr != '\0')
				{
					fprintf(stderr, _("%s: invalid argument for option %s\n"), progname, "-x");
					fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
					exit(1);
				}
				if (set_xid == 0)
				{
					fprintf(stderr, _("%s: transaction ID (-x) must not be 0\n"), progname);
					exit(1);
				}
				break;

			case 'c':
				set_oldest_commit_ts_xid = strtoul(optarg, &endptr, 0);
				if (endptr == optarg || *endptr != ',')
				{
					fprintf(stderr, _("%s: invalid argument for option %s\n"), progname, "-c");
					fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
					exit(1);
				}
				set_newest_commit_ts_xid = strtoul(endptr + 1, &endptr2, 0);
				if (endptr2 == endptr + 1 || *endptr2 != '\0')
				{
					fprintf(stderr, _("%s: invalid argument for option %s\n"), progname, "-c");
					fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
					exit(1);
				}

				if (set_oldest_commit_ts_xid < 2 &&
					set_oldest_commit_ts_xid != 0)
				{
					fprintf(stderr, _("%s: transaction ID (-c) must be either 0 or greater than or equal to 2\n"), progname);
					exit(1);
				}

				if (set_newest_commit_ts_xid < 2 &&
					set_newest_commit_ts_xid != 0)
				{
					fprintf(stderr, _("%s: transaction ID (-c) must be either 0 or greater than or equal to 2\n"), progname);
					exit(1);
				}
				break;

			case 'o':
				set_oid = strtoul(optarg, &endptr, 0);
				if (endptr == optarg || *endptr != '\0')
				{
					fprintf(stderr, _("%s: invalid argument for option %s\n"), progname, "-o");
					fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
					exit(1);
				}
				if (set_oid == 0)
				{
					fprintf(stderr, _("%s: OID (-o) must not be 0\n"), progname);
					exit(1);
				}
				break;

			case 'm':
				set_mxid = strtoul(optarg, &endptr, 0);
				if (endptr == optarg || *endptr != ',')
				{
					fprintf(stderr, _("%s: invalid argument for option %s\n"), progname, "-m");
					fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
					exit(1);
				}

				set_oldestmxid = strtoul(endptr + 1, &endptr2, 0);
				if (endptr2 == endptr + 1 || *endptr2 != '\0')
				{
					fprintf(stderr, _("%s: invalid argument for option %s\n"), progname, "-m");
					fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
					exit(1);
				}
				if (set_mxid == 0)
				{
					fprintf(stderr, _("%s: multitransaction ID (-m) must not be 0\n"), progname);
					exit(1);
				}

				/*
				 * XXX It'd be nice to have more sanity checks here, e.g. so
				 * that oldest is not wrapped around w.r.t. nextMulti.
				 */
				if (set_oldestmxid == 0)
				{
					fprintf(stderr, _("%s: oldest multitransaction ID (-m) must not be 0\n"),
							progname);
					exit(1);
				}
				break;

			case 'O':
				set_mxoff = strtoul(optarg, &endptr, 0);
				if (endptr == optarg || *endptr != '\0')
				{
					fprintf(stderr, _("%s: invalid argument for option %s\n"), progname, "-O");
					fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
					exit(1);
				}
				if (set_mxoff == -1)
				{
					fprintf(stderr, _("%s: multitransaction offset (-O) must not be -1\n"), progname);
					exit(1);
				}
				break;

			case 'l':
				if (strspn(optarg, "01234567890ABCDEFabcdef") != XLOG_FNAME_LEN)
				{
					fprintf(stderr, _("%s: invalid argument for option %s\n"), progname, "-l");
					fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
					exit(1);
				}

				/*
				 * XLogFromFileName requires wal segment size which is not yet
				 * set. Hence wal details are set later on.
				 */
				log_fname = pg_strdup(optarg);
				break;

			case 1:
				set_wal_segsize = strtol(optarg, &endptr, 10) * 1024 * 1024;
				if (endptr == optarg || *endptr != '\0')
				{
					fprintf(stderr,
							_("%s: argument of --wal-segsize must be a number\n"),
							progname);
					exit(1);
				}
				if (!IsValidWalSegSize(set_wal_segsize))
				{
					fprintf(stderr,
							_("%s: argument of --wal-segsize must be a power of 2 between 1 and 1024\n"),
							progname);
					exit(1);
				}
				break;

			default:
				fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
				exit(1);
		}
	}

	if (DataDir == NULL && optind < argc)
		DataDir = argv[optind++];

	/* Complain if any arguments remain */
	if (optind < argc)
	{
		fprintf(stderr, _("%s: too many command-line arguments (first is \"%s\")\n"),
				progname, argv[optind]);
		fprintf(stderr, _("Try \"%s --help\" for more information.\n"),
				progname);
		exit(1);
	}

	if (DataDir == NULL)
	{
		fprintf(stderr, _("%s: no data directory specified\n"), progname);
		fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
		exit(1);
	}

	/*
	 * Don't allow pg_resetwal to be run as root, to avoid overwriting the
	 * ownership of files in the data directory. We need only check for root
	 * -- any other user won't have sufficient permissions to modify files in
	 * the data directory.
	 */
#ifndef WIN32
	if (geteuid() == 0)
	{
		fprintf(stderr, _("%s: cannot be executed by \"root\"\n"),
				progname);
		fprintf(stderr, _("You must run %s as the PostgreSQL superuser.\n"),
				progname);
		exit(1);
	}
#endif

	get_restricted_token(progname);

	/* Set mask based on PGDATA permissions */
	if (!GetDataDirectoryCreatePerm(DataDir))
	{
		fprintf(stderr, _("%s: could not read permissions of directory \"%s\": %s\n"),
				progname, DataDir, strerror(errno));
		exit(1);
	}

	umask(pg_mode_mask);

	if (chdir(DataDir) < 0)
	{
		fprintf(stderr, _("%s: could not change directory to \"%s\": %s\n"),
				progname, DataDir, strerror(errno));
		exit(1);
	}

	/* Check that data directory matches our server version */
	CheckDataVersion();

	/*
	 * Check for a postmaster lock file --- if there is one, refuse to
	 * proceed, on grounds we might be interfering with a live installation.
	 */
	if ((fd = open("postmaster.pid", O_RDONLY, 0)) < 0)
	{
		if (errno != ENOENT)
		{
			fprintf(stderr, _("%s: could not open file \"%s\" for reading: %s\n"),
					progname, "postmaster.pid", strerror(errno));
			exit(1);
		}
	}
	else
	{
		fprintf(stderr, _("%s: lock file \"%s\" exists\n"
						  "Is a server running?  If not, delete the lock file and try again.\n"),
				progname, "postmaster.pid");
		exit(1);
	}

	/*
	 * Attempt to read the existing pg_control file
	 */
	if (!ReadControlFile())
		GuessControlValues();

	/*
	 * If no new WAL segment size was specified, use the control file value.
	 */
	if (set_wal_segsize != 0)
		WalSegSz = set_wal_segsize;
	else
		WalSegSz = ControlFile.xlog_seg_size;

	if (log_fname != NULL)
		XLogFromFileName(log_fname, &minXlogTli, &minXlogSegNo, WalSegSz);

	/*
	 * Also look at existing segment files to set up newXlogSegNo
	 */
	FindEndOfXLOG();

	/*
	 * If we're not going to proceed with the reset, print the current control
	 * file parameters.
	 */
	if ((guessed && !force) || noupdate)
		PrintControlValues(guessed);

	/*
	 * Adjust fields if required by switches.  (Do this now so that printout,
	 * if any, includes these values.)
	 */
	if (set_xid_epoch != -1)
		ControlFile.checkPointCopy.nextXidEpoch = set_xid_epoch;

	if (set_xid != 0)
	{
		ControlFile.checkPointCopy.nextXid = set_xid;

		/*
		 * For the moment, just set oldestXid to a value that will force
		 * immediate autovacuum-for-wraparound.  It's not clear whether adding
		 * user control of this is useful, so let's just do something that's
		 * reasonably safe.  The magic constant here corresponds to the
		 * maximum allowed value of autovacuum_freeze_max_age.
		 */
		ControlFile.checkPointCopy.oldestXid = set_xid - 2000000000;
		if (ControlFile.checkPointCopy.oldestXid < FirstNormalTransactionId)
			ControlFile.checkPointCopy.oldestXid += FirstNormalTransactionId;
		ControlFile.checkPointCopy.oldestXidDB = InvalidOid;
	}

	if (set_oldest_commit_ts_xid != 0)
		ControlFile.checkPointCopy.oldestCommitTsXid = set_oldest_commit_ts_xid;
	if (set_newest_commit_ts_xid != 0)
		ControlFile.checkPointCopy.newestCommitTsXid = set_newest_commit_ts_xid;

	if (set_oid != 0)
		ControlFile.checkPointCopy.nextOid = set_oid;

	if (set_mxid != 0)
	{
		ControlFile.checkPointCopy.nextMulti = set_mxid;

		ControlFile.checkPointCopy.oldestMulti = set_oldestmxid;
		if (ControlFile.checkPointCopy.oldestMulti < FirstMultiXactId)
			ControlFile.checkPointCopy.oldestMulti += FirstMultiXactId;
		ControlFile.checkPointCopy.oldestMultiDB = InvalidOid;
	}

	if (set_mxoff != -1)
		ControlFile.checkPointCopy.nextMultiOffset = set_mxoff;

	if (minXlogTli > ControlFile.checkPointCopy.ThisTimeLineID)
	{
		ControlFile.checkPointCopy.ThisTimeLineID = minXlogTli;
		ControlFile.checkPointCopy.PrevTimeLineID = minXlogTli;
	}

	if (set_wal_segsize != 0)
		ControlFile.xlog_seg_size = WalSegSz;

	if (minXlogSegNo > newXlogSegNo)
		newXlogSegNo = minXlogSegNo;

	/*
	 * If we had to guess anything, and -f was not given, just print the
	 * guessed values and exit.  Also print if -n is given.
	 */
	if ((guessed && !force) || noupdate)
	{
		PrintNewControlValues();
		if (!noupdate)
		{
			printf(_("\nIf these values seem acceptable, use -f to force reset.\n"));
			exit(1);
		}
		else
			exit(0);
	}

	/*
	 * Don't reset from a dirty pg_control without -f, either.
	 */
	if (ControlFile.state != DB_SHUTDOWNED && !force)
	{
		printf(_("The database server was not shut down cleanly.\n"
				 "Resetting the write-ahead log might cause data to be lost.\n"
				 "If you want to proceed anyway, use -f to force reset.\n"));
		exit(1);
	}

	/*
	 * Else, do the dirty deed.
	 */
	RewriteControlFile();
	KillExistingXLOG();
	KillExistingArchiveStatus();
	WriteEmptyXLOG();

	printf(_("Write-ahead log reset\n"));
	return 0;
}


/*
 * Look at the version string stored in PG_VERSION and decide if this utility
 * can be run safely or not.
 *
 * We don't want to inject pg_control and WAL files that are for a different
 * major version; that can't do anything good.  Note that we don't treat
 * mismatching version info in pg_control as a reason to bail out, because
 * recovering from a corrupted pg_control is one of the main reasons for this
 * program to exist at all.  However, PG_VERSION is unlikely to get corrupted,
 * and if it were it would be easy to fix by hand.  So let's make this check
 * to prevent simple user errors.
 */
static void
CheckDataVersion(void)
{
	const char *ver_file = "PG_VERSION";
	FILE	   *ver_fd;
	char		rawline[64];
	int			len;

	if ((ver_fd = fopen(ver_file, "r")) == NULL)
	{
		fprintf(stderr, _("%s: could not open file \"%s\" for reading: %s\n"),
				progname, ver_file, strerror(errno));
		exit(1);
	}

	/* version number has to be the first line read */
	if (!fgets(rawline, sizeof(rawline), ver_fd))
	{
		if (!ferror(ver_fd))
		{
			fprintf(stderr, _("%s: unexpected empty file \"%s\"\n"),
					progname, ver_file);
		}
		else
		{
			fprintf(stderr, _("%s: could not read file \"%s\": %s\n"),
					progname, ver_file, strerror(errno));
		}
		exit(1);
	}

	/* remove trailing newline, handling Windows newlines as well */
	len = strlen(rawline);
	if (len > 0 && rawline[len - 1] == '\n')
	{
		rawline[--len] = '\0';
		if (len > 0 && rawline[len - 1] == '\r')
			rawline[--len] = '\0';
	}

	if (strcmp(rawline, PG_MAJORVERSION) != 0)
	{
		fprintf(stderr, _("%s: data directory is of wrong version\n"
						  "File \"%s\" contains \"%s\", which is not compatible with this program's version \"%s\".\n"),
				progname, ver_file, rawline, PG_MAJORVERSION);
		exit(1);
	}

	fclose(ver_fd);
}


/*
 * Try to read the existing pg_control file.
 *
 * This routine is also responsible for updating old pg_control versions
 * to the current format.  (Currently we don't do anything of the sort.)
 */
static bool
ReadControlFile(void)
{
	int			fd;
	int			len;
	char	   *buffer;
	pg_crc32c	crc;

	if ((fd = open(XLOG_CONTROL_FILE, O_RDONLY | PG_BINARY, 0)) < 0)
	{
		/*
		 * If pg_control is not there at all, or we can't read it, the odds
		 * are we've been handed a bad DataDir path, so give up. User can do
		 * "touch pg_control" to force us to proceed.
		 */
		fprintf(stderr, _("%s: could not open file \"%s\" for reading: %s\n"),
				progname, XLOG_CONTROL_FILE, strerror(errno));
		if (errno == ENOENT)
			fprintf(stderr, _("If you are sure the data directory path is correct, execute\n"
							  "  touch %s\n"
							  "and try again.\n"),
					XLOG_CONTROL_FILE);
		exit(1);
	}

	/* Use malloc to ensure we have a maxaligned buffer */
	buffer = (char *) pg_malloc(PG_CONTROL_FILE_SIZE);

	len = read(fd, buffer, PG_CONTROL_FILE_SIZE);
	if (len < 0)
	{
		fprintf(stderr, _("%s: could not read file \"%s\": %s\n"),
				progname, XLOG_CONTROL_FILE, strerror(errno));
		exit(1);
	}
	close(fd);

	if (len >= sizeof(ControlFileData) &&
		((ControlFileData *) buffer)->pg_control_version == PG_CONTROL_VERSION)
	{
		/* Check the CRC. */
		INIT_CRC32C(crc);
		COMP_CRC32C(crc,
					buffer,
					offsetof(ControlFileData, crc));
		FIN_CRC32C(crc);

		if (!EQ_CRC32C(crc, ((ControlFileData *) buffer)->crc))
		{
			/* We will use the data but treat it as guessed. */
			fprintf(stderr,
					_("%s: pg_control exists but has invalid CRC; proceed with caution\n"),
					progname);
			guessed = true;
		}

		memcpy(&ControlFile, buffer, sizeof(ControlFile));

		/* return false if WAL segment size is not valid */
		if (!IsValidWalSegSize(ControlFile.xlog_seg_size))
		{
			fprintf(stderr,
					ngettext("%s: pg_control specifies invalid WAL segment size (%d byte); proceed with caution\n",
							 "%s: pg_control specifies invalid WAL segment size (%d bytes); proceed with caution\n",
							 ControlFile.xlog_seg_size),
					progname, ControlFile.xlog_seg_size);
			return false;
		}

		return true;
	}

	/* Looks like it's a mess. */
	fprintf(stderr, _("%s: pg_control exists but is broken or wrong version; ignoring it\n"),
			progname);
	return false;
}


/*
 * Guess at pg_control values when we can't read the old ones.
 */
static void
GuessControlValues(void)
{
	uint64		sysidentifier;
	struct timeval tv;

	/*
	 * Set up a completely default set of pg_control values.
	 */
	guessed = true;
	memset(&ControlFile, 0, sizeof(ControlFile));

	ControlFile.pg_control_version = PG_CONTROL_VERSION;
	ControlFile.catalog_version_no = CATALOG_VERSION_NO;

	/*
	 * Create a new unique installation identifier, since we can no longer use
	 * any old XLOG records.  See notes in xlog.c about the algorithm.
	 */
	gettimeofday(&tv, NULL);
	sysidentifier = ((uint64) tv.tv_sec) << 32;
	sysidentifier |= ((uint64) tv.tv_usec) << 12;
	sysidentifier |= getpid() & 0xFFF;

	ControlFile.system_identifier = sysidentifier;

	ControlFile.checkPointCopy.redo = SizeOfXLogLongPHD;
	ControlFile.checkPointCopy.ThisTimeLineID = 1;
	ControlFile.checkPointCopy.PrevTimeLineID = 1;
	ControlFile.checkPointCopy.fullPageWrites = false;
	ControlFile.checkPointCopy.nextXidEpoch = 0;
	ControlFile.checkPointCopy.nextXid = FirstNormalTransactionId;
	ControlFile.checkPointCopy.nextOid = FirstBootstrapObjectId;
	ControlFile.checkPointCopy.nextMulti = FirstMultiXactId;
	ControlFile.checkPointCopy.nextMultiOffset = 0;
	ControlFile.checkPointCopy.oldestXid = FirstNormalTransactionId;
	ControlFile.checkPointCopy.oldestXidDB = InvalidOid;
	ControlFile.checkPointCopy.oldestMulti = FirstMultiXactId;
	ControlFile.checkPointCopy.oldestMultiDB = InvalidOid;
	ControlFile.checkPointCopy.time = (pg_time_t) time(NULL);
	ControlFile.checkPointCopy.oldestActiveXid = InvalidTransactionId;

	ControlFile.state = DB_SHUTDOWNED;
	ControlFile.time = (pg_time_t) time(NULL);
	ControlFile.checkPoint = ControlFile.checkPointCopy.redo;
	ControlFile.unloggedLSN = 1;

	/* minRecoveryPoint, backupStartPoint and backupEndPoint can be left zero */

	ControlFile.wal_level = WAL_LEVEL_MINIMAL;
	ControlFile.wal_log_hints = false;
	ControlFile.track_commit_timestamp = false;
	ControlFile.MaxConnections = 100;
	ControlFile.max_worker_processes = 8;
	ControlFile.max_prepared_xacts = 0;
	ControlFile.max_locks_per_xact = 64;

	ControlFile.maxAlign = MAXIMUM_ALIGNOF;
	ControlFile.floatFormat = FLOATFORMAT_VALUE;
	ControlFile.blcksz = BLCKSZ;
	ControlFile.relseg_size = RELSEG_SIZE;
	ControlFile.xlog_blcksz = XLOG_BLCKSZ;
	ControlFile.xlog_seg_size = DEFAULT_XLOG_SEG_SIZE;
	ControlFile.nameDataLen = NAMEDATALEN;
	ControlFile.indexMaxKeys = INDEX_MAX_KEYS;
	ControlFile.toast_max_chunk_size = TOAST_MAX_CHUNK_SIZE;
	ControlFile.loblksize = LOBLKSIZE;
	ControlFile.float4ByVal = FLOAT4PASSBYVAL;
	ControlFile.float8ByVal = FLOAT8PASSBYVAL;

	/*
	 * XXX eventually, should try to grovel through old XLOG to develop more
	 * accurate values for TimeLineID, nextXID, etc.
	 */
}


/*
 * Print the guessed pg_control values when we had to guess.
 *
 * NB: this display should be just those fields that will not be
 * reset by RewriteControlFile().
 */
static void
PrintControlValues(bool guessed)
{
	char		sysident_str[32];

	if (guessed)
		printf(_("Guessed pg_control values:\n\n"));
	else
		printf(_("Current pg_control values:\n\n"));

	/*
	 * Format system_identifier separately to keep platform-dependent format
	 * code out of the translatable message string.
	 */
	snprintf(sysident_str, sizeof(sysident_str), UINT64_FORMAT,
			 ControlFile.system_identifier);

	printf(_("pg_control version number:            %u\n"),
		   ControlFile.pg_control_version);
	printf(_("Catalog version number:               %u\n"),
		   ControlFile.catalog_version_no);
	printf(_("Database system identifier:           %s\n"),
		   sysident_str);
	printf(_("Latest checkpoint's TimeLineID:       %u\n"),
		   ControlFile.checkPointCopy.ThisTimeLineID);
	printf(_("Latest checkpoint's full_page_writes: %s\n"),
		   ControlFile.checkPointCopy.fullPageWrites ? _("on") : _("off"));
	printf(_("Latest checkpoint's NextXID:          %u:%u\n"),
		   ControlFile.checkPointCopy.nextXidEpoch,
		   ControlFile.checkPointCopy.nextXid);
	printf(_("Latest checkpoint's NextOID:          %u\n"),
		   ControlFile.checkPointCopy.nextOid);
	printf(_("Latest checkpoint's NextMultiXactId:  %u\n"),
		   ControlFile.checkPointCopy.nextMulti);
	printf(_("Latest checkpoint's NextMultiOffset:  %u\n"),
		   ControlFile.checkPointCopy.nextMultiOffset);
	printf(_("Latest checkpoint's oldestXID:        %u\n"),
		   ControlFile.checkPointCopy.oldestXid);
	printf(_("Latest checkpoint's oldestXID's DB:   %u\n"),
		   ControlFile.checkPointCopy.oldestXidDB);
	printf(_("Latest checkpoint's oldestActiveXID:  %u\n"),
		   ControlFile.checkPointCopy.oldestActiveXid);
	printf(_("Latest checkpoint's oldestMultiXid:   %u\n"),
		   ControlFile.checkPointCopy.oldestMulti);
	printf(_("Latest checkpoint's oldestMulti's DB: %u\n"),
		   ControlFile.checkPointCopy.oldestMultiDB);
	printf(_("Latest checkpoint's oldestCommitTsXid:%u\n"),
		   ControlFile.checkPointCopy.oldestCommitTsXid);
	printf(_("Latest checkpoint's newestCommitTsXid:%u\n"),
		   ControlFile.checkPointCopy.newestCommitTsXid);
	printf(_("Maximum data alignment:               %u\n"),
		   ControlFile.maxAlign);
	/* we don't print floatFormat since can't say much useful about it */
	printf(_("Database block size:                  %u\n"),
		   ControlFile.blcksz);
	printf(_("Blocks per segment of large relation: %u\n"),
		   ControlFile.relseg_size);
	printf(_("WAL block size:                       %u\n"),
		   ControlFile.xlog_blcksz);
	printf(_("Bytes per WAL segment:                %u\n"),
		   ControlFile.xlog_seg_size);
	printf(_("Maximum length of identifiers:        %u\n"),
		   ControlFile.nameDataLen);
	printf(_("Maximum columns in an index:          %u\n"),
		   ControlFile.indexMaxKeys);
	printf(_("Maximum size of a TOAST chunk:        %u\n"),
		   ControlFile.toast_max_chunk_size);
	printf(_("Size of a large-object chunk:         %u\n"),
		   ControlFile.loblksize);
	/* This is no longer configurable, but users may still expect to see it: */
	printf(_("Date/time type storage:               %s\n"),
		   _("64-bit integers"));
	printf(_("Float4 argument passing:              %s\n"),
		   (ControlFile.float4ByVal ? _("by value") : _("by reference")));
	printf(_("Float8 argument passing:              %s\n"),
		   (ControlFile.float8ByVal ? _("by value") : _("by reference")));
	printf(_("Data page checksum version:           %u\n"),
		   ControlFile.data_checksum_version);
}


/*
 * Print the values to be changed.
 */
static void
PrintNewControlValues(void)
{
	char		fname[MAXFNAMELEN];

	/* This will be always printed in order to keep format same. */
	printf(_("\n\nValues to be changed:\n\n"));

	XLogFileName(fname, ControlFile.checkPointCopy.ThisTimeLineID,
				 newXlogSegNo, WalSegSz);
	printf(_("First log segment after reset:        %s\n"), fname);

	if (set_mxid != 0)
	{
		printf(_("NextMultiXactId:                      %u\n"),
			   ControlFile.checkPointCopy.nextMulti);
		printf(_("OldestMultiXid:                       %u\n"),
			   ControlFile.checkPointCopy.oldestMulti);
		printf(_("OldestMulti's DB:                     %u\n"),
			   ControlFile.checkPointCopy.oldestMultiDB);
	}

	if (set_mxoff != -1)
	{
		printf(_("NextMultiOffset:                      %u\n"),
			   ControlFile.checkPointCopy.nextMultiOffset);
	}

	if (set_oid != 0)
	{
		printf(_("NextOID:                              %u\n"),
			   ControlFile.checkPointCopy.nextOid);
	}

	if (set_xid != 0)
	{
		printf(_("NextXID:                              %u\n"),
			   ControlFile.checkPointCopy.nextXid);
		printf(_("OldestXID:                            %u\n"),
			   ControlFile.checkPointCopy.oldestXid);
		printf(_("OldestXID's DB:                       %u\n"),
			   ControlFile.checkPointCopy.oldestXidDB);
	}

	if (set_xid_epoch != -1)
	{
		printf(_("NextXID epoch:                        %u\n"),
			   ControlFile.checkPointCopy.nextXidEpoch);
	}

	if (set_oldest_commit_ts_xid != 0)
	{
		printf(_("oldestCommitTsXid:                    %u\n"),
			   ControlFile.checkPointCopy.oldestCommitTsXid);
	}
	if (set_newest_commit_ts_xid != 0)
	{
		printf(_("newestCommitTsXid:                    %u\n"),
			   ControlFile.checkPointCopy.newestCommitTsXid);
	}

	if (set_wal_segsize != 0)
	{
		printf(_("Bytes per WAL segment:                %u\n"),
			   ControlFile.xlog_seg_size);
	}
}


/*
 * Write out the new pg_control file.
 */
static void
RewriteControlFile(void)
{
	int			fd;
	char		buffer[PG_CONTROL_FILE_SIZE];	/* need not be aligned */

	/*
	 * For good luck, apply the same static assertions as in backend's
	 * WriteControlFile().
	 */
	StaticAssertStmt(sizeof(ControlFileData) <= PG_CONTROL_MAX_SAFE_SIZE,
					 "pg_control is too large for atomic disk writes");
	StaticAssertStmt(sizeof(ControlFileData) <= PG_CONTROL_FILE_SIZE,
					 "sizeof(ControlFileData) exceeds PG_CONTROL_FILE_SIZE");

	/*
	 * Adjust fields as needed to force an empty XLOG starting at
	 * newXlogSegNo.
	 */
	XLogSegNoOffsetToRecPtr(newXlogSegNo, SizeOfXLogLongPHD, WalSegSz,
							ControlFile.checkPointCopy.redo);
	ControlFile.checkPointCopy.time = (pg_time_t) time(NULL);

	ControlFile.state = DB_SHUTDOWNED;
	ControlFile.time = (pg_time_t) time(NULL);
	ControlFile.checkPoint = ControlFile.checkPointCopy.redo;
	ControlFile.minRecoveryPoint = 0;
	ControlFile.minRecoveryPointTLI = 0;
	ControlFile.backupStartPoint = 0;
	ControlFile.backupEndPoint = 0;
	ControlFile.backupEndRequired = false;

	/*
	 * Force the defaults for max_* settings. The values don't really matter
	 * as long as wal_level='minimal'; the postmaster will reset these fields
	 * anyway at startup.
	 */
	ControlFile.wal_level = WAL_LEVEL_MINIMAL;
	ControlFile.wal_log_hints = false;
	ControlFile.track_commit_timestamp = false;
	ControlFile.MaxConnections = 100;
	ControlFile.max_worker_processes = 8;
	ControlFile.max_prepared_xacts = 0;
	ControlFile.max_locks_per_xact = 64;

	/* Contents are protected with a CRC */
	INIT_CRC32C(ControlFile.crc);
	COMP_CRC32C(ControlFile.crc,
				(char *) &ControlFile,
				offsetof(ControlFileData, crc));
	FIN_CRC32C(ControlFile.crc);

	/*
	 * We write out PG_CONTROL_FILE_SIZE bytes into pg_control, zero-padding
	 * the excess over sizeof(ControlFileData).  This reduces the odds of
	 * premature-EOF errors when reading pg_control.  We'll still fail when we
	 * check the contents of the file, but hopefully with a more specific
	 * error than "couldn't read pg_control".
	 */
	memset(buffer, 0, PG_CONTROL_FILE_SIZE);
	memcpy(buffer, &ControlFile, sizeof(ControlFileData));

	unlink(XLOG_CONTROL_FILE);

	fd = open(XLOG_CONTROL_FILE,
			  O_RDWR | O_CREAT | O_EXCL | PG_BINARY,
			  pg_file_create_mode);
	if (fd < 0)
	{
		fprintf(stderr, _("%s: could not create pg_control file: %s\n"),
				progname, strerror(errno));
		exit(1);
	}

	errno = 0;
	if (write(fd, buffer, PG_CONTROL_FILE_SIZE) != PG_CONTROL_FILE_SIZE)
	{
		/* if write didn't set errno, assume problem is no disk space */
		if (errno == 0)
			errno = ENOSPC;
		fprintf(stderr, _("%s: could not write pg_control file: %s\n"),
				progname, strerror(errno));
		exit(1);
	}

	if (fsync(fd) != 0)
	{
		fprintf(stderr, _("%s: fsync error: %s\n"), progname, strerror(errno));
		exit(1);
	}

	close(fd);
}


/*
 * Scan existing XLOG files and determine the highest existing WAL address
 *
 * On entry, ControlFile.checkPointCopy.redo and ControlFile.xlog_seg_size
 * are assumed valid (note that we allow the old xlog seg size to differ
 * from what we're using).  On exit, newXlogId and newXlogSeg are set to
 * suitable values for the beginning of replacement WAL (in our seg size).
 */
static void
FindEndOfXLOG(void)
{
	DIR		   *xldir;
	struct dirent *xlde;
	uint64		segs_per_xlogid;
	uint64		xlogbytepos;

	/*
	 * Initialize the max() computation using the last checkpoint address from
	 * old pg_control.  Note that for the moment we are working with segment
	 * numbering according to the old xlog seg size.
	 */
	segs_per_xlogid = (UINT64CONST(0x0000000100000000) / ControlFile.xlog_seg_size);
	newXlogSegNo = ControlFile.checkPointCopy.redo / ControlFile.xlog_seg_size;

	/*
	 * Scan the pg_wal directory to find existing WAL segment files. We assume
	 * any present have been used; in most scenarios this should be
	 * conservative, because of xlog.c's attempts to pre-create files.
	 */
	xldir = opendir(XLOGDIR);
	if (xldir == NULL)
	{
		fprintf(stderr, _("%s: could not open directory \"%s\": %s\n"),
				progname, XLOGDIR, strerror(errno));
		exit(1);
	}

	while (errno = 0, (xlde = readdir(xldir)) != NULL)
	{
		if (IsXLogFileName(xlde->d_name) ||
			IsPartialXLogFileName(xlde->d_name))
		{
			unsigned int tli,
						log,
						seg;
			XLogSegNo	segno;

			/*
			 * Note: We don't use XLogFromFileName here, because we want to
			 * use the segment size from the control file, not the size the
			 * pg_resetwal binary was compiled with
			 */
			sscanf(xlde->d_name, "%08X%08X%08X", &tli, &log, &seg);
			segno = ((uint64) log) * segs_per_xlogid + seg;

			/*
			 * Note: we take the max of all files found, regardless of their
			 * timelines.  Another possibility would be to ignore files of
			 * timelines other than the target TLI, but this seems safer.
			 * Better too large a result than too small...
			 */
			if (segno > newXlogSegNo)
				newXlogSegNo = segno;
		}
	}

	if (errno)
	{
		fprintf(stderr, _("%s: could not read directory \"%s\": %s\n"),
				progname, XLOGDIR, strerror(errno));
		exit(1);
	}

	if (closedir(xldir))
	{
		fprintf(stderr, _("%s: could not close directory \"%s\": %s\n"),
				progname, XLOGDIR, strerror(errno));
		exit(1);
	}

	/*
	 * Finally, convert to new xlog seg size, and advance by one to ensure we
	 * are in virgin territory.
	 */
	xlogbytepos = newXlogSegNo * ControlFile.xlog_seg_size;
	newXlogSegNo = (xlogbytepos + ControlFile.xlog_seg_size - 1) / WalSegSz;
	newXlogSegNo++;
}


/*
 * Remove existing XLOG files
 */
static void
KillExistingXLOG(void)
{
	DIR		   *xldir;
	struct dirent *xlde;
	char		path[MAXPGPATH + sizeof(XLOGDIR)];

	xldir = opendir(XLOGDIR);
	if (xldir == NULL)
	{
		fprintf(stderr, _("%s: could not open directory \"%s\": %s\n"),
				progname, XLOGDIR, strerror(errno));
		exit(1);
	}

	while (errno = 0, (xlde = readdir(xldir)) != NULL)
	{
		if (IsXLogFileName(xlde->d_name) ||
			IsPartialXLogFileName(xlde->d_name))
		{
			snprintf(path, sizeof(path), "%s/%s", XLOGDIR, xlde->d_name);
			if (unlink(path) < 0)
			{
				fprintf(stderr, _("%s: could not delete file \"%s\": %s\n"),
						progname, path, strerror(errno));
				exit(1);
			}
		}
	}

	if (errno)
	{
		fprintf(stderr, _("%s: could not read directory \"%s\": %s\n"),
				progname, XLOGDIR, strerror(errno));
		exit(1);
	}

	if (closedir(xldir))
	{
		fprintf(stderr, _("%s: could not close directory \"%s\": %s\n"),
				progname, XLOGDIR, strerror(errno));
		exit(1);
	}
}


/*
 * Remove existing archive status files
 */
static void
KillExistingArchiveStatus(void)
{
#define ARCHSTATDIR XLOGDIR "/archive_status"

	DIR		   *xldir;
	struct dirent *xlde;
	char		path[MAXPGPATH + sizeof(ARCHSTATDIR)];

	xldir = opendir(ARCHSTATDIR);
	if (xldir == NULL)
	{
		fprintf(stderr, _("%s: could not open directory \"%s\": %s\n"),
				progname, ARCHSTATDIR, strerror(errno));
		exit(1);
	}

	while (errno = 0, (xlde = readdir(xldir)) != NULL)
	{
		if (strspn(xlde->d_name, "0123456789ABCDEF") == XLOG_FNAME_LEN &&
			(strcmp(xlde->d_name + XLOG_FNAME_LEN, ".ready") == 0 ||
			 strcmp(xlde->d_name + XLOG_FNAME_LEN, ".done") == 0 ||
			 strcmp(xlde->d_name + XLOG_FNAME_LEN, ".partial.ready") == 0 ||
			 strcmp(xlde->d_name + XLOG_FNAME_LEN, ".partial.done") == 0))
		{
			snprintf(path, sizeof(path), "%s/%s", ARCHSTATDIR, xlde->d_name);
			if (unlink(path) < 0)
			{
				fprintf(stderr, _("%s: could not delete file \"%s\": %s\n"),
						progname, path, strerror(errno));
				exit(1);
			}
		}
	}

	if (errno)
	{
		fprintf(stderr, _("%s: could not read directory \"%s\": %s\n"),
				progname, ARCHSTATDIR, strerror(errno));
		exit(1);
	}

	if (closedir(xldir))
	{
		fprintf(stderr, _("%s: could not close directory \"%s\": %s\n"),
				progname, ARCHSTATDIR, strerror(errno));
		exit(1);
	}
}


/*
 * Write an empty XLOG file, containing only the checkpoint record
 * already set up in ControlFile.
 */
static void
WriteEmptyXLOG(void)
{
	PGAlignedXLogBlock buffer;
	XLogPageHeader page;
	XLogLongPageHeader longpage;
	XLogRecord *record;
	pg_crc32c	crc;
	char		path[MAXPGPATH];
	int			fd;
	int			nbytes;
	char	   *recptr;

	memset(buffer.data, 0, XLOG_BLCKSZ);

	/* Set up the XLOG page header */
	page = (XLogPageHeader) buffer.data;
	page->xlp_magic = XLOG_PAGE_MAGIC;
	page->xlp_info = XLP_LONG_HEADER;
	page->xlp_tli = ControlFile.checkPointCopy.ThisTimeLineID;
	page->xlp_pageaddr = ControlFile.checkPointCopy.redo - SizeOfXLogLongPHD;
	longpage = (XLogLongPageHeader) page;
	longpage->xlp_sysid = ControlFile.system_identifier;
	longpage->xlp_seg_size = WalSegSz;
	longpage->xlp_xlog_blcksz = XLOG_BLCKSZ;

	/* Insert the initial checkpoint record */
	recptr = (char *) page + SizeOfXLogLongPHD;
	record = (XLogRecord *) recptr;
	record->xl_prev = 0;
	record->xl_xid = InvalidTransactionId;
	record->xl_tot_len = SizeOfXLogRecord + SizeOfXLogRecordDataHeaderShort + sizeof(CheckPoint);
	record->xl_info = XLOG_CHECKPOINT_SHUTDOWN;
	record->xl_rmid = RM_XLOG_ID;

	recptr += SizeOfXLogRecord;
	*(recptr++) = (char) XLR_BLOCK_ID_DATA_SHORT;
	*(recptr++) = sizeof(CheckPoint);
	memcpy(recptr, &ControlFile.checkPointCopy,
		   sizeof(CheckPoint));

	INIT_CRC32C(crc);
	COMP_CRC32C(crc, ((char *) record) + SizeOfXLogRecord, record->xl_tot_len - SizeOfXLogRecord);
	COMP_CRC32C(crc, (char *) record, offsetof(XLogRecord, xl_crc));
	FIN_CRC32C(crc);
	record->xl_crc = crc;

	/* Write the first page */
	XLogFilePath(path, ControlFile.checkPointCopy.ThisTimeLineID,
				 newXlogSegNo, WalSegSz);

	unlink(path);

	fd = open(path, O_RDWR | O_CREAT | O_EXCL | PG_BINARY,
			  pg_file_create_mode);
	if (fd < 0)
	{
		fprintf(stderr, _("%s: could not open file \"%s\": %s\n"),
				progname, path, strerror(errno));
		exit(1);
	}

	errno = 0;
	if (write(fd, buffer.data, XLOG_BLCKSZ) != XLOG_BLCKSZ)
	{
		/* if write didn't set errno, assume problem is no disk space */
		if (errno == 0)
			errno = ENOSPC;
		fprintf(stderr, _("%s: could not write file \"%s\": %s\n"),
				progname, path, strerror(errno));
		exit(1);
	}

	/* Fill the rest of the file with zeroes */
	memset(buffer.data, 0, XLOG_BLCKSZ);
	for (nbytes = XLOG_BLCKSZ; nbytes < WalSegSz; nbytes += XLOG_BLCKSZ)
	{
		errno = 0;
		if (write(fd, buffer.data, XLOG_BLCKSZ) != XLOG_BLCKSZ)
		{
			if (errno == 0)
				errno = ENOSPC;
			fprintf(stderr, _("%s: could not write file \"%s\": %s\n"),
					progname, path, strerror(errno));
			exit(1);
		}
	}

	if (fsync(fd) != 0)
	{
		fprintf(stderr, _("%s: fsync error: %s\n"), progname, strerror(errno));
		exit(1);
	}

	close(fd);
}


static void
usage(void)
{
	printf(_("%s resets the PostgreSQL write-ahead log.\n\n"), progname);
	printf(_("Usage:\n  %s [OPTION]... DATADIR\n\n"), progname);
	printf(_("Options:\n"));
	printf(_("  -c, --commit-timestamp-ids=XID,XID\n"
			 "                                 set oldest and newest transactions bearing\n"
			 "                                 commit timestamp (zero means no change)\n"));
	printf(_(" [-D, --pgdata=]DATADIR          data directory\n"));
	printf(_("  -e, --epoch=XIDEPOCH           set next transaction ID epoch\n"));
	printf(_("  -f, --force                    force update to be done\n"));
	printf(_("  -l, --next-wal-file=WALFILE    set minimum starting location for new WAL\n"));
	printf(_("  -m, --multixact-ids=MXID,MXID  set next and oldest multitransaction ID\n"));
	printf(_("  -n, --dry-run                  no update, just show what would be done\n"));
	printf(_("  -o, --next-oid=OID             set next OID\n"));
	printf(_("  -O, --multixact-offset=OFFSET  set next multitransaction offset\n"));
	printf(_("  -V, --version                  output version information, then exit\n"));
	printf(_("  -x, --next-transaction-id=XID  set next transaction ID\n"));
	printf(_("      --wal-segsize=SIZE         size of WAL segments, in megabytes\n"));
	printf(_("  -?, --help                     show this help, then exit\n"));
	printf(_("\nReport bugs to <pgsql-bugs@postgresql.org>.\n"));
}
