/*
 * src/bin/pg_archivecleanup/pg_archivecleanup.c
 *
 * pg_archivecleanup.c
 *
 * Production-ready example of an archive_cleanup_command
 * used to clean an archive when using standby_mode = on in 9.0
 * or for standalone use for any version of PostgreSQL 8.0+.
 *
 * Original author:		Simon Riggs  simon@2ndquadrant.com
 * Current maintainer:	Simon Riggs
 */
#include "postgres_fe.h"

#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>

#include "pg_getopt.h"

#include "access/xlog_internal.h"

const char *progname;

/* Options and defaults */
bool		debug = false;		/* are we debugging? */
bool		dryrun = false;		/* are we performing a dry-run operation? */
char	   *additional_ext = NULL;	/* Extension to remove from filenames */

char	   *archiveLocation;	/* where to find the archive? */
char	   *restartWALFileName; /* the file from which we can restart restore */
char		WALFilePath[MAXPGPATH * 2]; /* the file path including archive */
char		exclusiveCleanupFileName[MAXFNAMELEN];	/* the oldest file we want
													 * to remain in archive */


/* =====================================================================
 *
 *		  Customizable section
 *
 * =====================================================================
 *
 *	Currently, this section assumes that the Archive is a locally
 *	accessible directory. If you want to make other assumptions,
 *	such as using a vendor-specific archive and access API, these
 *	routines are the ones you'll need to change. You're
 *	encouraged to submit any changes to pgsql-hackers@postgresql.org
 *	or personally to the current maintainer. Those changes may be
 *	folded in to later versions of this program.
 */

/*
 *	Initialize allows customized commands into the archive cleanup program.
 *
 *	You may wish to add code to check for tape libraries, etc..
 */
static void
Initialize(void)
{
	/*
	 * This code assumes that archiveLocation is a directory, so we use stat
	 * to test if it's accessible.
	 */
	struct stat stat_buf;

	if (stat(archiveLocation, &stat_buf) != 0 ||
		!S_ISDIR(stat_buf.st_mode))
	{
		fprintf(stderr, _("%s: archive location \"%s\" does not exist\n"),
				progname, archiveLocation);
		exit(2);
	}
}

static void
TrimExtension(char *filename, char *extension)
{
	int			flen;
	int			elen;

	if (extension == NULL)
		return;

	elen = strlen(extension);
	flen = strlen(filename);

	if (flen > elen && strcmp(filename + flen - elen, extension) == 0)
		filename[flen - elen] = '\0';
}

static void
CleanupPriorWALFiles(void)
{
	int			rc;
	DIR		   *xldir;
	struct dirent *xlde;
	char		walfile[MAXPGPATH];

	if ((xldir = opendir(archiveLocation)) != NULL)
	{
		while (errno = 0, (xlde = readdir(xldir)) != NULL)
		{
			/*
			 * Truncation is essentially harmless, because we skip names of
			 * length other than XLOG_FNAME_LEN.  (In principle, one could use
			 * a 1000-character additional_ext and get trouble.)
			 */
			strlcpy(walfile, xlde->d_name, MAXPGPATH);
			TrimExtension(walfile, additional_ext);

			/*
			 * We ignore the timeline part of the XLOG segment identifiers in
			 * deciding whether a segment is still needed.  This ensures that
			 * we won't prematurely remove a segment from a parent timeline.
			 * We could probably be a little more proactive about removing
			 * segments of non-parent timelines, but that would be a whole lot
			 * more complicated.
			 *
			 * We use the alphanumeric sorting property of the filenames to
			 * decide which ones are earlier than the exclusiveCleanupFileName
			 * file. Note that this means files are not removed in the order
			 * they were originally written, in case this worries you.
			 */
			if ((IsXLogFileName(walfile) || IsPartialXLogFileName(walfile)) &&
				strcmp(walfile + 8, exclusiveCleanupFileName + 8) < 0)
			{
				/*
				 * Use the original file name again now, including any
				 * extension that might have been chopped off before testing
				 * the sequence.
				 */
				snprintf(WALFilePath, sizeof(WALFilePath), "%s/%s",
						 archiveLocation, xlde->d_name);

				if (dryrun)
				{
					/*
					 * Prints the name of the file to be removed and skips the
					 * actual removal.  The regular printout is so that the
					 * user can pipe the output into some other program.
					 */
					printf("%s\n", WALFilePath);
					if (debug)
						fprintf(stderr,
								_("%s: file \"%s\" would be removed\n"),
								progname, WALFilePath);
					continue;
				}

				if (debug)
					fprintf(stderr, _("%s: removing file \"%s\"\n"),
							progname, WALFilePath);

				rc = unlink(WALFilePath);
				if (rc != 0)
				{
					fprintf(stderr, _("%s: ERROR: could not remove file \"%s\": %s\n"),
							progname, WALFilePath, strerror(errno));
					break;
				}
			}
		}

		if (errno)
			fprintf(stderr, _("%s: could not read archive location \"%s\": %s\n"),
					progname, archiveLocation, strerror(errno));
		if (closedir(xldir))
			fprintf(stderr, _("%s: could not close archive location \"%s\": %s\n"),
					progname, archiveLocation, strerror(errno));
	}
	else
		fprintf(stderr, _("%s: could not open archive location \"%s\": %s\n"),
				progname, archiveLocation, strerror(errno));
}

/*
 * SetWALFileNameForCleanup()
 *
 *	  Set the earliest WAL filename that we want to keep on the archive
 *	  and decide whether we need cleanup
 */
static void
SetWALFileNameForCleanup(void)
{
	bool		fnameOK = false;

	TrimExtension(restartWALFileName, additional_ext);

	/*
	 * If restartWALFileName is a WAL file name then just use it directly. If
	 * restartWALFileName is a .partial or .backup filename, make sure we use
	 * the prefix of the filename, otherwise we will remove wrong files since
	 * 000000010000000000000010.partial and
	 * 000000010000000000000010.00000020.backup are after
	 * 000000010000000000000010.
	 */
	if (IsXLogFileName(restartWALFileName))
	{
		strcpy(exclusiveCleanupFileName, restartWALFileName);
		fnameOK = true;
	}
	else if (IsPartialXLogFileName(restartWALFileName))
	{
		int			args;
		uint32		tli = 1,
					log = 0,
					seg = 0;

		args = sscanf(restartWALFileName, "%08X%08X%08X.partial",
					  &tli, &log, &seg);
		if (args == 3)
		{
			fnameOK = true;

			/*
			 * Use just the prefix of the filename, ignore everything after
			 * first period
			 */
			XLogFileNameById(exclusiveCleanupFileName, tli, log, seg);
		}
	}
	else if (IsBackupHistoryFileName(restartWALFileName))
	{
		int			args;
		uint32		tli = 1,
					log = 0,
					seg = 0,
					offset = 0;

		args = sscanf(restartWALFileName, "%08X%08X%08X.%08X.backup", &tli, &log, &seg, &offset);
		if (args == 4)
		{
			fnameOK = true;

			/*
			 * Use just the prefix of the filename, ignore everything after
			 * first period
			 */
			XLogFileNameById(exclusiveCleanupFileName, tli, log, seg);
		}
	}

	if (!fnameOK)
	{
		fprintf(stderr, _("%s: invalid file name argument\n"), progname);
		fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
		exit(2);
	}
}

/* =====================================================================
 *		  End of Customizable section
 * =====================================================================
 */

static void
usage(void)
{
	printf(_("%s removes older WAL files from PostgreSQL archives.\n\n"), progname);
	printf(_("Usage:\n"));
	printf(_("  %s [OPTION]... ARCHIVELOCATION OLDESTKEPTWALFILE\n"), progname);
	printf(_("\nOptions:\n"));
	printf(_("  -d             generate debug output (verbose mode)\n"));
	printf(_("  -n             dry run, show the names of the files that would be removed\n"));
	printf(_("  -V, --version  output version information, then exit\n"));
	printf(_("  -x EXT         clean up files if they have this extension\n"));
	printf(_("  -?, --help     show this help, then exit\n"));
	printf(_("\n"
			 "For use as archive_cleanup_command in recovery.conf when standby_mode = on:\n"
			 "  archive_cleanup_command = 'pg_archivecleanup [OPTION]... ARCHIVELOCATION %%r'\n"
			 "e.g.\n"
			 "  archive_cleanup_command = 'pg_archivecleanup /mnt/server/archiverdir %%r'\n"));
	printf(_("\n"
			 "Or for use as a standalone archive cleaner:\n"
			 "e.g.\n"
			 "  pg_archivecleanup /mnt/server/archiverdir 000000010000000000000010.00000020.backup\n"));
	printf(_("\nReport bugs to <pgsql-bugs@postgresql.org>.\n"));
}

/*------------ MAIN ----------------------------------------*/
int
main(int argc, char **argv)
{
	int			c;

	set_pglocale_pgservice(argv[0], PG_TEXTDOMAIN("pg_archivecleanup"));
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
			puts("pg_archivecleanup (PostgreSQL) " PG_VERSION);
			exit(0);
		}
	}

	while ((c = getopt(argc, argv, "x:dn")) != -1)
	{
		switch (c)
		{
			case 'd':			/* Debug mode */
				debug = true;
				break;
			case 'n':			/* Dry-Run mode */
				dryrun = true;
				break;
			case 'x':
				additional_ext = pg_strdup(optarg); /* Extension to remove
													 * from xlogfile names */
				break;
			default:
				fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
				exit(2);
				break;
		}
	}

	/*
	 * We will go to the archiveLocation to check restartWALFileName.
	 * restartWALFileName may not exist anymore, which would not be an error,
	 * so we separate the archiveLocation and restartWALFileName so we can
	 * check separately whether archiveLocation exists, if not that is an
	 * error
	 */
	if (optind < argc)
	{
		archiveLocation = argv[optind];
		optind++;
	}
	else
	{
		fprintf(stderr, _("%s: must specify archive location\n"), progname);
		fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
		exit(2);
	}

	if (optind < argc)
	{
		restartWALFileName = argv[optind];
		optind++;
	}
	else
	{
		fprintf(stderr, _("%s: must specify oldest kept WAL file\n"), progname);
		fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
		exit(2);
	}

	if (optind < argc)
	{
		fprintf(stderr, _("%s: too many command-line arguments\n"), progname);
		fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
		exit(2);
	}

	/*
	 * Check archive exists and other initialization if required.
	 */
	Initialize();

	/*
	 * Check filename is a valid name, then process to find cut-off
	 */
	SetWALFileNameForCleanup();

	if (debug)
	{
		snprintf(WALFilePath, MAXPGPATH, "%s/%s",
				 archiveLocation, exclusiveCleanupFileName);
		fprintf(stderr, _("%s: keeping WAL file \"%s\" and later\n"),
				progname, WALFilePath);
	}

	/*
	 * Remove WAL files older than cut-off
	 */
	CleanupPriorWALFiles();

	exit(0);
}
