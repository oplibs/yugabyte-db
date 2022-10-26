/*-------------------------------------------------------------------------
 *
 * local_source.c
 *	  Functions for using a local data directory as the source.
 *
 * Portions Copyright (c) 2013-2021, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */
#include "postgres_fe.h"

#include <fcntl.h>
#include <unistd.h>

#include "datapagemap.h"
#include "file_ops.h"
#include "filemap.h"
#include "pg_rewind.h"
#include "rewind_source.h"

typedef struct
{
	rewind_source common;		/* common interface functions */

	const char *datadir;		/* path to the source data directory */
} local_source;

static void local_traverse_files(rewind_source *source,
								 process_file_callback_t callback);
static char *local_fetch_file(rewind_source *source, const char *path,
							  size_t *filesize);
static void local_fetch_file_range(rewind_source *source, const char *path,
								   off_t off, size_t len);
static void local_finish_fetch(rewind_source *source);
static void local_destroy(rewind_source *source);

rewind_source *
init_local_source(const char *datadir)
{
	local_source *src;

	src = pg_malloc0(sizeof(local_source));

	src->common.traverse_files = local_traverse_files;
	src->common.fetch_file = local_fetch_file;
	src->common.queue_fetch_range = local_fetch_file_range;
	src->common.finish_fetch = local_finish_fetch;
	src->common.get_current_wal_insert_lsn = NULL;
	src->common.destroy = local_destroy;

	src->datadir = datadir;

	return &src->common;
}

static void
local_traverse_files(rewind_source *source, process_file_callback_t callback)
{
	traverse_datadir(((local_source *) source)->datadir, &process_source_file);
}

static char *
local_fetch_file(rewind_source *source, const char *path, size_t *filesize)
{
	return slurpFile(((local_source *) source)->datadir, path, filesize);
}

/*
 * Copy a file from source to target, starting at 'off', for 'len' bytes.
 */
static void
local_fetch_file_range(rewind_source *source, const char *path, off_t off,
					   size_t len)
{
	const char *datadir = ((local_source *) source)->datadir;
	PGAlignedBlock buf;
	char		srcpath[MAXPGPATH];
	int			srcfd;
	off_t		begin = off;
	off_t		end = off + len;

	snprintf(srcpath, sizeof(srcpath), "%s/%s", datadir, path);

	srcfd = open(srcpath, O_RDONLY | PG_BINARY, 0);
	if (srcfd < 0)
		pg_fatal("could not open source file \"%s\": %m",
				 srcpath);

	if (lseek(srcfd, begin, SEEK_SET) == -1)
		pg_fatal("could not seek in source file: %m");

	open_target_file(path, false);

	while (end - begin > 0)
	{
		ssize_t		readlen;
		size_t		len;

		if (end - begin > sizeof(buf))
			len = sizeof(buf);
		else
			len = end - begin;

		readlen = read(srcfd, buf.data, len);

		if (readlen < 0)
			pg_fatal("could not read file \"%s\": %m", srcpath);
		else if (readlen == 0)
			pg_fatal("unexpected EOF while reading file \"%s\"", srcpath);

		write_target_range(buf.data, begin, readlen);
		begin += readlen;
	}

	if (close(srcfd) != 0)
		pg_fatal("could not close file \"%s\": %m", srcpath);
}

static void
local_finish_fetch(rewind_source *source)
{
	/*
	 * Nothing to do, local_fetch_file_range() copies the ranges immediately.
	 */
}

static void
local_destroy(rewind_source *source)
{
	pfree(source);
}
