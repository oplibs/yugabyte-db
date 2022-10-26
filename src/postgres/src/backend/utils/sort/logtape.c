/*-------------------------------------------------------------------------
 *
 * logtape.c
 *	  Management of "logical tapes" within temporary files.
 *
 * This module exists to support sorting via multiple merge passes (see
 * tuplesort.c).  Merging is an ideal algorithm for tape devices, but if
 * we implement it on disk by creating a separate file for each "tape",
 * there is an annoying problem: the peak space usage is at least twice
 * the volume of actual data to be sorted.  (This must be so because each
 * datum will appear in both the input and output tapes of the final
 * merge pass.  For seven-tape polyphase merge, which is otherwise a
 * pretty good algorithm, peak usage is more like 4x actual data volume.)
 *
 * We can work around this problem by recognizing that any one tape
 * dataset (with the possible exception of the final output) is written
 * and read exactly once in a perfectly sequential manner.  Therefore,
 * a datum once read will not be required again, and we can recycle its
 * space for use by the new tape dataset(s) being generated.  In this way,
 * the total space usage is essentially just the actual data volume, plus
 * insignificant bookkeeping and start/stop overhead.
 *
 * Few OSes allow arbitrary parts of a file to be released back to the OS,
 * so we have to implement this space-recycling ourselves within a single
 * logical file.  logtape.c exists to perform this bookkeeping and provide
 * the illusion of N independent tape devices to tuplesort.c.  Note that
 * logtape.c itself depends on buffile.c to provide a "logical file" of
 * larger size than the underlying OS may support.
 *
 * For simplicity, we allocate and release space in the underlying file
 * in BLCKSZ-size blocks.  Space allocation boils down to keeping track
 * of which blocks in the underlying file belong to which logical tape,
 * plus any blocks that are free (recycled and not yet reused).
 * The blocks in each logical tape form a chain, with a prev- and next-
 * pointer in each block.
 *
 * The initial write pass is guaranteed to fill the underlying file
 * perfectly sequentially, no matter how data is divided into logical tapes.
 * Once we begin merge passes, the access pattern becomes considerably
 * less predictable --- but the seeking involved should be comparable to
 * what would happen if we kept each logical tape in a separate file,
 * so there's no serious performance penalty paid to obtain the space
 * savings of recycling.  We try to localize the write accesses by always
 * writing to the lowest-numbered free block when we have a choice; it's
 * not clear this helps much, but it can't hurt.  (XXX perhaps a LIFO
 * policy for free blocks would be better?)
 *
 * To further make the I/Os more sequential, we can use a larger buffer
 * when reading, and read multiple blocks from the same tape in one go,
 * whenever the buffer becomes empty.
 *
 * To support the above policy of writing to the lowest free block, the
 * freelist is a min heap.
 *
 * Since all the bookkeeping and buffer memory is allocated with palloc(),
 * and the underlying file(s) are made with OpenTemporaryFile, all resources
 * for a logical tape set are certain to be cleaned up even if processing
 * is aborted by ereport(ERROR).  To avoid confusion, the caller should take
 * care that all calls for a single LogicalTapeSet are made in the same
 * palloc context.
 *
 * To support parallel sort operations involving coordinated callers to
 * tuplesort.c routines across multiple workers, it is necessary to
 * concatenate each worker BufFile/tapeset into one single logical tapeset
 * managed by the leader.  Workers should have produced one final
 * materialized tape (their entire output) when this happens in leader.
 * There will always be the same number of runs as input tapes, and the same
 * number of input tapes as participants (worker Tuplesortstates).
 *
 * Portions Copyright (c) 1996-2021, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/utils/sort/logtape.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include <fcntl.h>

#include "storage/buffile.h"
#include "utils/builtins.h"
#include "utils/logtape.h"
#include "utils/memdebug.h"
#include "utils/memutils.h"

/*
 * A TapeBlockTrailer is stored at the end of each BLCKSZ block.
 *
 * The first block of a tape has prev == -1.  The last block of a tape
 * stores the number of valid bytes on the block, inverted, in 'next'
 * Therefore next < 0 indicates the last block.
 */
typedef struct TapeBlockTrailer
{
	long		prev;			/* previous block on this tape, or -1 on first
								 * block */
	long		next;			/* next block on this tape, or # of valid
								 * bytes on last block (if < 0) */
} TapeBlockTrailer;

#define TapeBlockPayloadSize  (BLCKSZ - sizeof(TapeBlockTrailer))
#define TapeBlockGetTrailer(buf) \
	((TapeBlockTrailer *) ((char *) buf + TapeBlockPayloadSize))

#define TapeBlockIsLast(buf) (TapeBlockGetTrailer(buf)->next < 0)
#define TapeBlockGetNBytes(buf) \
	(TapeBlockIsLast(buf) ? \
	 (- TapeBlockGetTrailer(buf)->next) : TapeBlockPayloadSize)
#define TapeBlockSetNBytes(buf, nbytes) \
	(TapeBlockGetTrailer(buf)->next = -(nbytes))

/*
 * When multiple tapes are being written to concurrently (as in HashAgg),
 * avoid excessive fragmentation by preallocating block numbers to individual
 * tapes. Each preallocation doubles in size starting at
 * TAPE_WRITE_PREALLOC_MIN blocks up to TAPE_WRITE_PREALLOC_MAX blocks.
 *
 * No filesystem operations are performed for preallocation; only the block
 * numbers are reserved. This may lead to sparse writes, which will cause
 * ltsWriteBlock() to fill in holes with zeros.
 */
#define TAPE_WRITE_PREALLOC_MIN 8
#define TAPE_WRITE_PREALLOC_MAX 128

/*
 * This data structure represents a single "logical tape" within the set
 * of logical tapes stored in the same file.
 *
 * While writing, we hold the current partially-written data block in the
 * buffer.  While reading, we can hold multiple blocks in the buffer.  Note
 * that we don't retain the trailers of a block when it's read into the
 * buffer.  The buffer therefore contains one large contiguous chunk of data
 * from the tape.
 */
typedef struct LogicalTape
{
	bool		writing;		/* T while in write phase */
	bool		frozen;			/* T if blocks should not be freed when read */
	bool		dirty;			/* does buffer need to be written? */

	/*
	 * Block numbers of the first, current, and next block of the tape.
	 *
	 * The "current" block number is only valid when writing, or reading from
	 * a frozen tape.  (When reading from an unfrozen tape, we use a larger
	 * read buffer that holds multiple blocks, so the "current" block is
	 * ambiguous.)
	 *
	 * When concatenation of worker tape BufFiles is performed, an offset to
	 * the first block in the unified BufFile space is applied during reads.
	 */
	long		firstBlockNumber;
	long		curBlockNumber;
	long		nextBlockNumber;
	long		offsetBlockNumber;

	/*
	 * Buffer for current data block(s).
	 */
	char	   *buffer;			/* physical buffer (separately palloc'd) */
	int			buffer_size;	/* allocated size of the buffer */
	int			max_size;		/* highest useful, safe buffer_size */
	int			pos;			/* next read/write position in buffer */
	int			nbytes;			/* total # of valid bytes in buffer */

	/*
	 * Preallocated block numbers are held in an array sorted in descending
	 * order; blocks are consumed from the end of the array (lowest block
	 * numbers first).
	 */
	long	   *prealloc;
	int			nprealloc;		/* number of elements in list */
	int			prealloc_size;	/* number of elements list can hold */
} LogicalTape;

/*
 * This data structure represents a set of related "logical tapes" sharing
 * space in a single underlying file.  (But that "file" may be multiple files
 * if needed to escape OS limits on file size; buffile.c handles that for us.)
 * The number of tapes is fixed at creation.
 */
struct LogicalTapeSet
{
	BufFile    *pfile;			/* underlying file for whole tape set */

	/*
	 * File size tracking.  nBlocksWritten is the size of the underlying file,
	 * in BLCKSZ blocks.  nBlocksAllocated is the number of blocks allocated
	 * by ltsReleaseBlock(), and it is always greater than or equal to
	 * nBlocksWritten.  Blocks between nBlocksAllocated and nBlocksWritten are
	 * blocks that have been allocated for a tape, but have not been written
	 * to the underlying file yet.  nHoleBlocks tracks the total number of
	 * blocks that are in unused holes between worker spaces following BufFile
	 * concatenation.
	 */
	long		nBlocksAllocated;	/* # of blocks allocated */
	long		nBlocksWritten; /* # of blocks used in underlying file */
	long		nHoleBlocks;	/* # of "hole" blocks left */

	/*
	 * We store the numbers of recycled-and-available blocks in freeBlocks[].
	 * When there are no such blocks, we extend the underlying file.
	 *
	 * If forgetFreeSpace is true then any freed blocks are simply forgotten
	 * rather than being remembered in freeBlocks[].  See notes for
	 * LogicalTapeSetForgetFreeSpace().
	 */
	bool		forgetFreeSpace;	/* are we remembering free blocks? */
	long	   *freeBlocks;		/* resizable array holding minheap */
	long		nFreeBlocks;	/* # of currently free blocks */
	Size		freeBlocksLen;	/* current allocated length of freeBlocks[] */
	bool		enable_prealloc;	/* preallocate write blocks? */

	/* The array of logical tapes. */
	int			nTapes;			/* # of logical tapes in set */
	LogicalTape *tapes;			/* has nTapes nentries */
};

static void ltsWriteBlock(LogicalTapeSet *lts, long blocknum, void *buffer);
static void ltsReadBlock(LogicalTapeSet *lts, long blocknum, void *buffer);
static long ltsGetBlock(LogicalTapeSet *lts, LogicalTape *lt);
static long ltsGetFreeBlock(LogicalTapeSet *lts);
static long ltsGetPreallocBlock(LogicalTapeSet *lts, LogicalTape *lt);
static void ltsReleaseBlock(LogicalTapeSet *lts, long blocknum);
static void ltsConcatWorkerTapes(LogicalTapeSet *lts, TapeShare *shared,
								 SharedFileSet *fileset);
static void ltsInitTape(LogicalTape *lt);
static void ltsInitReadBuffer(LogicalTapeSet *lts, LogicalTape *lt);


/*
 * Write a block-sized buffer to the specified block of the underlying file.
 *
 * No need for an error return convention; we ereport() on any error.
 */
static void
ltsWriteBlock(LogicalTapeSet *lts, long blocknum, void *buffer)
{
	/*
	 * BufFile does not support "holes", so if we're about to write a block
	 * that's past the current end of file, fill the space between the current
	 * end of file and the target block with zeros.
	 *
	 * This can happen either when tapes preallocate blocks; or for the last
	 * block of a tape which might not have been flushed.
	 *
	 * Note that BufFile concatenation can leave "holes" in BufFile between
	 * worker-owned block ranges.  These are tracked for reporting purposes
	 * only.  We never read from nor write to these hole blocks, and so they
	 * are not considered here.
	 */
	while (blocknum > lts->nBlocksWritten)
	{
		PGAlignedBlock zerobuf;

		MemSet(zerobuf.data, 0, sizeof(zerobuf));

		ltsWriteBlock(lts, lts->nBlocksWritten, zerobuf.data);
	}

	/* Write the requested block */
	if (BufFileSeekBlock(lts->pfile, blocknum) != 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not seek to block %ld of temporary file",
						blocknum)));
	BufFileWrite(lts->pfile, buffer, BLCKSZ);

	/* Update nBlocksWritten, if we extended the file */
	if (blocknum == lts->nBlocksWritten)
		lts->nBlocksWritten++;
}

/*
 * Read a block-sized buffer from the specified block of the underlying file.
 *
 * No need for an error return convention; we ereport() on any error.   This
 * module should never attempt to read a block it doesn't know is there.
 */
static void
ltsReadBlock(LogicalTapeSet *lts, long blocknum, void *buffer)
{
	size_t		nread;

	if (BufFileSeekBlock(lts->pfile, blocknum) != 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not seek to block %ld of temporary file",
						blocknum)));
	nread = BufFileRead(lts->pfile, buffer, BLCKSZ);
	if (nread != BLCKSZ)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not read block %ld of temporary file: read only %zu of %zu bytes",
						blocknum, nread, (size_t) BLCKSZ)));
}

/*
 * Read as many blocks as we can into the per-tape buffer.
 *
 * Returns true if anything was read, 'false' on EOF.
 */
static bool
ltsReadFillBuffer(LogicalTapeSet *lts, LogicalTape *lt)
{
	lt->pos = 0;
	lt->nbytes = 0;

	do
	{
		/* Applying an offset to a null pointer is undefined behavior. */
		/* It is possible that if lt->buffer is NULL, we would always exit */
		/* on datablocknum == -1L, so just set thisbuf = NULL in that case. */
		/* https://github.com/yugabyte/yugabyte-db/issues/10295 */
		char	   *thisbuf = lt->buffer ? lt->buffer + lt->nbytes : NULL;
		long		datablocknum = lt->nextBlockNumber;

		/* Fetch next block number */
		if (datablocknum == -1L)
			break;				/* EOF */
		/* Apply worker offset, needed for leader tapesets */
		datablocknum += lt->offsetBlockNumber;

		/* Read the block */
		ltsReadBlock(lts, datablocknum, (void *) thisbuf);
		if (!lt->frozen)
			ltsReleaseBlock(lts, datablocknum);
		lt->curBlockNumber = lt->nextBlockNumber;

		lt->nbytes += TapeBlockGetNBytes(thisbuf);
		if (TapeBlockIsLast(thisbuf))
		{
			lt->nextBlockNumber = -1L;
			/* EOF */
			break;
		}
		else
			lt->nextBlockNumber = TapeBlockGetTrailer(thisbuf)->next;

		/* Advance to next block, if we have buffer space left */
	} while (lt->buffer_size - lt->nbytes > BLCKSZ);

	return (lt->nbytes > 0);
}

static inline void
swap_nodes(long *heap, unsigned long a, unsigned long b)
{
	unsigned long swap;

	swap = heap[a];
	heap[a] = heap[b];
	heap[b] = swap;
}

static inline unsigned long
left_offset(unsigned long i)
{
	return 2 * i + 1;
}

static inline unsigned long
right_offset(unsigned i)
{
	return 2 * i + 2;
}

static inline unsigned long
parent_offset(unsigned long i)
{
	return (i - 1) / 2;
}

/*
 * Get the next block for writing.
 */
static long
ltsGetBlock(LogicalTapeSet *lts, LogicalTape *lt)
{
	if (lts->enable_prealloc)
		return ltsGetPreallocBlock(lts, lt);
	else
		return ltsGetFreeBlock(lts);
}

/*
 * Select the lowest currently unused block from the tape set's global free
 * list min heap.
 */
static long
ltsGetFreeBlock(LogicalTapeSet *lts)
{
	long	   *heap = lts->freeBlocks;
	long		blocknum;
	int			heapsize;
	unsigned long pos;

	/* freelist empty; allocate a new block */
	if (lts->nFreeBlocks == 0)
		return lts->nBlocksAllocated++;

	if (lts->nFreeBlocks == 1)
	{
		lts->nFreeBlocks--;
		return lts->freeBlocks[0];
	}

	/* take top of minheap */
	blocknum = heap[0];

	/* replace with end of minheap array */
	heap[0] = heap[--lts->nFreeBlocks];

	/* sift down */
	pos = 0;
	heapsize = lts->nFreeBlocks;
	while (true)
	{
		unsigned long left = left_offset(pos);
		unsigned long right = right_offset(pos);
		unsigned long min_child;

		if (left < heapsize && right < heapsize)
			min_child = (heap[left] < heap[right]) ? left : right;
		else if (left < heapsize)
			min_child = left;
		else if (right < heapsize)
			min_child = right;
		else
			break;

		if (heap[min_child] >= heap[pos])
			break;

		swap_nodes(heap, min_child, pos);
		pos = min_child;
	}

	return blocknum;
}

/*
 * Return the lowest free block number from the tape's preallocation list.
 * Refill the preallocation list with blocks from the tape set's free list if
 * necessary.
 */
static long
ltsGetPreallocBlock(LogicalTapeSet *lts, LogicalTape *lt)
{
	/* sorted in descending order, so return the last element */
	if (lt->nprealloc > 0)
		return lt->prealloc[--lt->nprealloc];

	if (lt->prealloc == NULL)
	{
		lt->prealloc_size = TAPE_WRITE_PREALLOC_MIN;
		lt->prealloc = (long *) palloc(sizeof(long) * lt->prealloc_size);
	}
	else if (lt->prealloc_size < TAPE_WRITE_PREALLOC_MAX)
	{
		/* when the preallocation list runs out, double the size */
		lt->prealloc_size *= 2;
		if (lt->prealloc_size > TAPE_WRITE_PREALLOC_MAX)
			lt->prealloc_size = TAPE_WRITE_PREALLOC_MAX;
		lt->prealloc = (long *) repalloc(lt->prealloc,
										 sizeof(long) * lt->prealloc_size);
	}

	/* refill preallocation list */
	lt->nprealloc = lt->prealloc_size;
	for (int i = lt->nprealloc; i > 0; i--)
	{
		lt->prealloc[i - 1] = ltsGetFreeBlock(lts);

		/* verify descending order */
		Assert(i == lt->nprealloc || lt->prealloc[i - 1] > lt->prealloc[i]);
	}

	return lt->prealloc[--lt->nprealloc];
}

/*
 * Return a block# to the freelist.
 */
static void
ltsReleaseBlock(LogicalTapeSet *lts, long blocknum)
{
	long	   *heap;
	unsigned long pos;

	/*
	 * Do nothing if we're no longer interested in remembering free space.
	 */
	if (lts->forgetFreeSpace)
		return;

	/*
	 * Enlarge freeBlocks array if full.
	 */
	if (lts->nFreeBlocks >= lts->freeBlocksLen)
	{
		/*
		 * If the freelist becomes very large, just return and leak this free
		 * block.
		 */
		if (lts->freeBlocksLen * 2 * sizeof(long) > MaxAllocSize)
			return;

		lts->freeBlocksLen *= 2;
		lts->freeBlocks = (long *) repalloc(lts->freeBlocks,
											lts->freeBlocksLen * sizeof(long));
	}

	heap = lts->freeBlocks;
	pos = lts->nFreeBlocks;

	/* place entry at end of minheap array */
	heap[pos] = blocknum;
	lts->nFreeBlocks++;

	/* sift up */
	while (pos != 0)
	{
		unsigned long parent = parent_offset(pos);

		if (heap[parent] < heap[pos])
			break;

		swap_nodes(heap, parent, pos);
		pos = parent;
	}
}

/*
 * Claim ownership of a set of logical tapes from existing shared BufFiles.
 *
 * Caller should be leader process.  Though tapes are marked as frozen in
 * workers, they are not frozen when opened within leader, since unfrozen tapes
 * use a larger read buffer. (Frozen tapes have smaller read buffer, optimized
 * for random access.)
 */
static void
ltsConcatWorkerTapes(LogicalTapeSet *lts, TapeShare *shared,
					 SharedFileSet *fileset)
{
	LogicalTape *lt = NULL;
	long		tapeblocks = 0L;
	long		nphysicalblocks = 0L;
	int			i;

	/* Should have at least one worker tape, plus leader's tape */
	Assert(lts->nTapes >= 2);

	/*
	 * Build concatenated view of all BufFiles, remembering the block number
	 * where each source file begins.  No changes are needed for leader/last
	 * tape.
	 */
	for (i = 0; i < lts->nTapes - 1; i++)
	{
		char		filename[MAXPGPATH];
		BufFile    *file;
		int64		filesize;

		lt = &lts->tapes[i];

		pg_itoa(i, filename);
		file = BufFileOpenFileSet(&fileset->fs, filename, O_RDONLY, false);
		filesize = BufFileSize(file);

		/*
		 * Stash first BufFile, and concatenate subsequent BufFiles to that.
		 * Store block offset into each tape as we go.
		 */
		lt->firstBlockNumber = shared[i].firstblocknumber;
		if (i == 0)
		{
			lts->pfile = file;
			lt->offsetBlockNumber = 0L;
		}
		else
		{
			lt->offsetBlockNumber = BufFileAppend(lts->pfile, file);
		}
		/* Don't allocate more for read buffer than could possibly help */
		lt->max_size = Min(MaxAllocSize, filesize);
		tapeblocks = filesize / BLCKSZ;
		nphysicalblocks += tapeblocks;
	}

	/*
	 * Set # of allocated blocks, as well as # blocks written.  Use extent of
	 * new BufFile space (from 0 to end of last worker's tape space) for this.
	 * Allocated/written blocks should include space used by holes left
	 * between concatenated BufFiles.
	 */
	lts->nBlocksAllocated = lt->offsetBlockNumber + tapeblocks;
	lts->nBlocksWritten = lts->nBlocksAllocated;

	/*
	 * Compute number of hole blocks so that we can later work backwards, and
	 * instrument number of physical blocks.  We don't simply use physical
	 * blocks directly for instrumentation because this would break if we ever
	 * subsequently wrote to the leader tape.
	 *
	 * Working backwards like this keeps our options open.  If shared BufFiles
	 * ever support being written to post-export, logtape.c can automatically
	 * take advantage of that.  We'd then support writing to the leader tape
	 * while recycling space from worker tapes, because the leader tape has a
	 * zero offset (write routines won't need to have extra logic to apply an
	 * offset).
	 *
	 * The only thing that currently prevents writing to the leader tape from
	 * working is the fact that BufFiles opened using BufFileOpenFileSet() are
	 * read-only by definition, but that could be changed if it seemed
	 * worthwhile.  For now, writing to the leader tape will raise a "Bad file
	 * descriptor" error, so tuplesort must avoid writing to the leader tape
	 * altogether.
	 */
	lts->nHoleBlocks = lts->nBlocksAllocated - nphysicalblocks;
}

/*
 * Initialize per-tape struct.  Note we allocate the I/O buffer lazily.
 */
static void
ltsInitTape(LogicalTape *lt)
{
	lt->writing = true;
	lt->frozen = false;
	lt->dirty = false;
	lt->firstBlockNumber = -1L;
	lt->curBlockNumber = -1L;
	lt->nextBlockNumber = -1L;
	lt->offsetBlockNumber = 0L;
	lt->buffer = NULL;
	lt->buffer_size = 0;
	/* palloc() larger than MaxAllocSize would fail */
	lt->max_size = MaxAllocSize;
	lt->pos = 0;
	lt->nbytes = 0;
	lt->prealloc = NULL;
	lt->nprealloc = 0;
	lt->prealloc_size = 0;
}

/*
 * Lazily allocate and initialize the read buffer. This avoids waste when many
 * tapes are open at once, but not all are active between rewinding and
 * reading.
 */
static void
ltsInitReadBuffer(LogicalTapeSet *lts, LogicalTape *lt)
{
	Assert(lt->buffer_size > 0);
	lt->buffer = palloc(lt->buffer_size);

	/* Read the first block, or reset if tape is empty */
	lt->nextBlockNumber = lt->firstBlockNumber;
	lt->pos = 0;
	lt->nbytes = 0;
	ltsReadFillBuffer(lts, lt);
}

/*
 * Create a set of logical tapes in a temporary underlying file.
 *
 * Each tape is initialized in write state.  Serial callers pass ntapes,
 * NULL argument for shared, and -1 for worker.  Parallel worker callers
 * pass ntapes, a shared file handle, NULL shared argument,  and their own
 * worker number.  Leader callers, which claim shared worker tapes here,
 * must supply non-sentinel values for all arguments except worker number,
 * which should be -1.
 *
 * Leader caller is passing back an array of metadata each worker captured
 * when LogicalTapeFreeze() was called for their final result tapes.  Passed
 * tapes array is actually sized ntapes - 1, because it includes only
 * worker tapes, whereas leader requires its own leader tape.  Note that we
 * rely on the assumption that reclaimed worker tapes will only be read
 * from once by leader, and never written to again (tapes are initialized
 * for writing, but that's only to be consistent).  Leader may not write to
 * its own tape purely due to a restriction in the shared buffile
 * infrastructure that may be lifted in the future.
 */
LogicalTapeSet *
LogicalTapeSetCreate(int ntapes, bool preallocate, TapeShare *shared,
					 SharedFileSet *fileset, int worker)
{
	LogicalTapeSet *lts;
	int			i;

	/*
	 * Create top-level struct including per-tape LogicalTape structs.
	 */
	Assert(ntapes > 0);
	lts = (LogicalTapeSet *) palloc(sizeof(LogicalTapeSet));
	lts->nBlocksAllocated = 0L;
	lts->nBlocksWritten = 0L;
	lts->nHoleBlocks = 0L;
	lts->forgetFreeSpace = false;
	lts->freeBlocksLen = 32;	/* reasonable initial guess */
	lts->freeBlocks = (long *) palloc(lts->freeBlocksLen * sizeof(long));
	lts->nFreeBlocks = 0;
	lts->enable_prealloc = preallocate;
	lts->nTapes = ntapes;
	lts->tapes = (LogicalTape *) palloc(ntapes * sizeof(LogicalTape));

	for (i = 0; i < ntapes; i++)
		ltsInitTape(&lts->tapes[i]);

	/*
	 * Create temp BufFile storage as required.
	 *
	 * Leader concatenates worker tapes, which requires special adjustment to
	 * final tapeset data.  Things are simpler for the worker case and the
	 * serial case, though.  They are generally very similar -- workers use a
	 * shared fileset, whereas serial sorts use a conventional serial BufFile.
	 */
	if (shared)
		ltsConcatWorkerTapes(lts, shared, fileset);
	else if (fileset)
	{
		char		filename[MAXPGPATH];

		pg_itoa(worker, filename);
		lts->pfile = BufFileCreateFileSet(&fileset->fs, filename);
	}
	else
		lts->pfile = BufFileCreateTemp(false);

	return lts;
}

/*
 * Close a logical tape set and release all resources.
 */
void
LogicalTapeSetClose(LogicalTapeSet *lts)
{
	LogicalTape *lt;
	int			i;

	BufFileClose(lts->pfile);
	for (i = 0; i < lts->nTapes; i++)
	{
		lt = &lts->tapes[i];
		if (lt->buffer)
			pfree(lt->buffer);
	}
	pfree(lts->tapes);
	pfree(lts->freeBlocks);
	pfree(lts);
}

/*
 * Mark a logical tape set as not needing management of free space anymore.
 *
 * This should be called if the caller does not intend to write any more data
 * into the tape set, but is reading from un-frozen tapes.  Since no more
 * writes are planned, remembering free blocks is no longer useful.  Setting
 * this flag lets us avoid wasting time and space in ltsReleaseBlock(), which
 * is not designed to handle large numbers of free blocks.
 */
void
LogicalTapeSetForgetFreeSpace(LogicalTapeSet *lts)
{
	lts->forgetFreeSpace = true;
}

/*
 * Write to a logical tape.
 *
 * There are no error returns; we ereport() on failure.
 */
void
LogicalTapeWrite(LogicalTapeSet *lts, int tapenum,
				 void *ptr, size_t size)
{
	LogicalTape *lt;
	size_t		nthistime;

	Assert(tapenum >= 0 && tapenum < lts->nTapes);
	lt = &lts->tapes[tapenum];
	Assert(lt->writing);
	Assert(lt->offsetBlockNumber == 0L);

	/* Allocate data buffer and first block on first write */
	if (lt->buffer == NULL)
	{
		lt->buffer = (char *) palloc(BLCKSZ);
		lt->buffer_size = BLCKSZ;
	}
	if (lt->curBlockNumber == -1)
	{
		Assert(lt->firstBlockNumber == -1);
		Assert(lt->pos == 0);

		lt->curBlockNumber = ltsGetBlock(lts, lt);
		lt->firstBlockNumber = lt->curBlockNumber;

		TapeBlockGetTrailer(lt->buffer)->prev = -1L;
	}

	Assert(lt->buffer_size == BLCKSZ);
	while (size > 0)
	{
		if (lt->pos >= (int) TapeBlockPayloadSize)
		{
			/* Buffer full, dump it out */
			long		nextBlockNumber;

			if (!lt->dirty)
			{
				/* Hmm, went directly from reading to writing? */
				elog(ERROR, "invalid logtape state: should be dirty");
			}

			/*
			 * First allocate the next block, so that we can store it in the
			 * 'next' pointer of this block.
			 */
			nextBlockNumber = ltsGetBlock(lts, lt);

			/* set the next-pointer and dump the current block. */
			TapeBlockGetTrailer(lt->buffer)->next = nextBlockNumber;
			ltsWriteBlock(lts, lt->curBlockNumber, (void *) lt->buffer);

			/* initialize the prev-pointer of the next block */
			TapeBlockGetTrailer(lt->buffer)->prev = lt->curBlockNumber;
			lt->curBlockNumber = nextBlockNumber;
			lt->pos = 0;
			lt->nbytes = 0;
		}

		nthistime = TapeBlockPayloadSize - lt->pos;
		if (nthistime > size)
			nthistime = size;
		Assert(nthistime > 0);

		memcpy(lt->buffer + lt->pos, ptr, nthistime);

		lt->dirty = true;
		lt->pos += nthistime;
		if (lt->nbytes < lt->pos)
			lt->nbytes = lt->pos;
		ptr = (void *) ((char *) ptr + nthistime);
		size -= nthistime;
	}
}

/*
 * Rewind logical tape and switch from writing to reading.
 *
 * The tape must currently be in writing state, or "frozen" in read state.
 *
 * 'buffer_size' specifies how much memory to use for the read buffer.
 * Regardless of the argument, the actual amount of memory used is between
 * BLCKSZ and MaxAllocSize, and is a multiple of BLCKSZ.  The given value is
 * rounded down and truncated to fit those constraints, if necessary.  If the
 * tape is frozen, the 'buffer_size' argument is ignored, and a small BLCKSZ
 * byte buffer is used.
 */
void
LogicalTapeRewindForRead(LogicalTapeSet *lts, int tapenum, size_t buffer_size)
{
	LogicalTape *lt;

	Assert(tapenum >= 0 && tapenum < lts->nTapes);
	lt = &lts->tapes[tapenum];

	/*
	 * Round and cap buffer_size if needed.
	 */
	if (lt->frozen)
		buffer_size = BLCKSZ;
	else
	{
		/* need at least one block */
		if (buffer_size < BLCKSZ)
			buffer_size = BLCKSZ;

		/* palloc() larger than max_size is unlikely to be helpful */
		if (buffer_size > lt->max_size)
			buffer_size = lt->max_size;

		/* round down to BLCKSZ boundary */
		buffer_size -= buffer_size % BLCKSZ;
	}

	if (lt->writing)
	{
		/*
		 * Completion of a write phase.  Flush last partial data block, and
		 * rewind for normal (destructive) read.
		 */
		if (lt->dirty)
		{
			/*
			 * As long as we've filled the buffer at least once, its contents
			 * are entirely defined from valgrind's point of view, even though
			 * contents beyond the current end point may be stale.  But it's
			 * possible - at least in the case of a parallel sort - to sort
			 * such small amount of data that we do not fill the buffer even
			 * once.  Tell valgrind that its contents are defined, so it
			 * doesn't bleat.
			 */
			VALGRIND_MAKE_MEM_DEFINED(lt->buffer + lt->nbytes,
									  lt->buffer_size - lt->nbytes);

			TapeBlockSetNBytes(lt->buffer, lt->nbytes);
			ltsWriteBlock(lts, lt->curBlockNumber, (void *) lt->buffer);
		}
		lt->writing = false;
	}
	else
	{
		/*
		 * This is only OK if tape is frozen; we rewind for (another) read
		 * pass.
		 */
		Assert(lt->frozen);
	}

	if (lt->buffer)
		pfree(lt->buffer);

	/* the buffer is lazily allocated, but set the size here */
	lt->buffer = NULL;
	lt->buffer_size = buffer_size;

	/* free the preallocation list, and return unused block numbers */
	if (lt->prealloc != NULL)
	{
		for (int i = lt->nprealloc; i > 0; i--)
			ltsReleaseBlock(lts, lt->prealloc[i - 1]);
		pfree(lt->prealloc);
		lt->prealloc = NULL;
		lt->nprealloc = 0;
		lt->prealloc_size = 0;
	}
}

/*
 * Rewind logical tape and switch from reading to writing.
 *
 * NOTE: we assume the caller has read the tape to the end; otherwise
 * untouched data will not have been freed. We could add more code to free
 * any unread blocks, but in current usage of this module it'd be useless
 * code.
 */
void
LogicalTapeRewindForWrite(LogicalTapeSet *lts, int tapenum)
{
	LogicalTape *lt;

	Assert(tapenum >= 0 && tapenum < lts->nTapes);
	lt = &lts->tapes[tapenum];

	Assert(!lt->writing && !lt->frozen);
	lt->writing = true;
	lt->dirty = false;
	lt->firstBlockNumber = -1L;
	lt->curBlockNumber = -1L;
	lt->pos = 0;
	lt->nbytes = 0;
	if (lt->buffer)
		pfree(lt->buffer);
	lt->buffer = NULL;
	lt->buffer_size = 0;
}

/*
 * Read from a logical tape.
 *
 * Early EOF is indicated by return value less than #bytes requested.
 */
size_t
LogicalTapeRead(LogicalTapeSet *lts, int tapenum,
				void *ptr, size_t size)
{
	LogicalTape *lt;
	size_t		nread = 0;
	size_t		nthistime;

	Assert(tapenum >= 0 && tapenum < lts->nTapes);
	lt = &lts->tapes[tapenum];
	Assert(!lt->writing);

	if (lt->buffer == NULL)
		ltsInitReadBuffer(lts, lt);

	while (size > 0)
	{
		if (lt->pos >= lt->nbytes)
		{
			/* Try to load more data into buffer. */
			if (!ltsReadFillBuffer(lts, lt))
				break;			/* EOF */
		}

		nthistime = lt->nbytes - lt->pos;
		if (nthistime > size)
			nthistime = size;
		Assert(nthistime > 0);

		memcpy(ptr, lt->buffer + lt->pos, nthistime);

		lt->pos += nthistime;
		ptr = (void *) ((char *) ptr + nthistime);
		size -= nthistime;
		nread += nthistime;
	}

	return nread;
}

/*
 * "Freeze" the contents of a tape so that it can be read multiple times
 * and/or read backwards.  Once a tape is frozen, its contents will not
 * be released until the LogicalTapeSet is destroyed.  This is expected
 * to be used only for the final output pass of a merge.
 *
 * This *must* be called just at the end of a write pass, before the
 * tape is rewound (after rewind is too late!).  It performs a rewind
 * and switch to read mode "for free".  An immediately following rewind-
 * for-read call is OK but not necessary.
 *
 * share output argument is set with details of storage used for tape after
 * freezing, which may be passed to LogicalTapeSetCreate within leader
 * process later.  This metadata is only of interest to worker callers
 * freezing their final output for leader (single materialized tape).
 * Serial sorts should set share to NULL.
 */
void
LogicalTapeFreeze(LogicalTapeSet *lts, int tapenum, TapeShare *share)
{
	LogicalTape *lt;

	Assert(tapenum >= 0 && tapenum < lts->nTapes);
	lt = &lts->tapes[tapenum];
	Assert(lt->writing);
	Assert(lt->offsetBlockNumber == 0L);

	/*
	 * Completion of a write phase.  Flush last partial data block, and rewind
	 * for nondestructive read.
	 */
	if (lt->dirty)
	{
		/*
		 * As long as we've filled the buffer at least once, its contents are
		 * entirely defined from valgrind's point of view, even though
		 * contents beyond the current end point may be stale.  But it's
		 * possible - at least in the case of a parallel sort - to sort such
		 * small amount of data that we do not fill the buffer even once. Tell
		 * valgrind that its contents are defined, so it doesn't bleat.
		 */
		VALGRIND_MAKE_MEM_DEFINED(lt->buffer + lt->nbytes,
								  lt->buffer_size - lt->nbytes);

		TapeBlockSetNBytes(lt->buffer, lt->nbytes);
		ltsWriteBlock(lts, lt->curBlockNumber, (void *) lt->buffer);
		lt->writing = false;
	}
	lt->writing = false;
	lt->frozen = true;

	/*
	 * The seek and backspace functions assume a single block read buffer.
	 * That's OK with current usage.  A larger buffer is helpful to make the
	 * read pattern of the backing file look more sequential to the OS, when
	 * we're reading from multiple tapes.  But at the end of a sort, when a
	 * tape is frozen, we only read from a single tape anyway.
	 */
	if (!lt->buffer || lt->buffer_size != BLCKSZ)
	{
		if (lt->buffer)
			pfree(lt->buffer);
		lt->buffer = palloc(BLCKSZ);
		lt->buffer_size = BLCKSZ;
	}

	/* Read the first block, or reset if tape is empty */
	lt->curBlockNumber = lt->firstBlockNumber;
	lt->pos = 0;
	lt->nbytes = 0;

	if (lt->firstBlockNumber == -1L)
		lt->nextBlockNumber = -1L;
	ltsReadBlock(lts, lt->curBlockNumber, (void *) lt->buffer);
	if (TapeBlockIsLast(lt->buffer))
		lt->nextBlockNumber = -1L;
	else
		lt->nextBlockNumber = TapeBlockGetTrailer(lt->buffer)->next;
	lt->nbytes = TapeBlockGetNBytes(lt->buffer);

	/* Handle extra steps when caller is to share its tapeset */
	if (share)
	{
		BufFileExportFileSet(lts->pfile);
		share->firstblocknumber = lt->firstBlockNumber;
	}
}

/*
 * Add additional tapes to this tape set. Not intended to be used when any
 * tapes are frozen.
 */
void
LogicalTapeSetExtend(LogicalTapeSet *lts, int nAdditional)
{
	int			i;
	int			nTapesOrig = lts->nTapes;

	lts->nTapes += nAdditional;

	lts->tapes = (LogicalTape *) repalloc(lts->tapes,
										  lts->nTapes * sizeof(LogicalTape));

	for (i = nTapesOrig; i < lts->nTapes; i++)
		ltsInitTape(&lts->tapes[i]);
}

/*
 * Backspace the tape a given number of bytes.  (We also support a more
 * general seek interface, see below.)
 *
 * *Only* a frozen-for-read tape can be backed up; we don't support
 * random access during write, and an unfrozen read tape may have
 * already discarded the desired data!
 *
 * Returns the number of bytes backed up.  It can be less than the
 * requested amount, if there isn't that much data before the current
 * position.  The tape is positioned to the beginning of the tape in
 * that case.
 */
size_t
LogicalTapeBackspace(LogicalTapeSet *lts, int tapenum, size_t size)
{
	LogicalTape *lt;
	size_t		seekpos = 0;

	Assert(tapenum >= 0 && tapenum < lts->nTapes);
	lt = &lts->tapes[tapenum];
	Assert(lt->frozen);
	Assert(lt->buffer_size == BLCKSZ);

	if (lt->buffer == NULL)
		ltsInitReadBuffer(lts, lt);

	/*
	 * Easy case for seek within current block.
	 */
	if (size <= (size_t) lt->pos)
	{
		lt->pos -= (int) size;
		return size;
	}

	/*
	 * Not-so-easy case, have to walk back the chain of blocks.  This
	 * implementation would be pretty inefficient for long seeks, but we
	 * really aren't doing that (a seek over one tuple is typical).
	 */
	seekpos = (size_t) lt->pos; /* part within this block */
	while (size > seekpos)
	{
		long		prev = TapeBlockGetTrailer(lt->buffer)->prev;

		if (prev == -1L)
		{
			/* Tried to back up beyond the beginning of tape. */
			if (lt->curBlockNumber != lt->firstBlockNumber)
				elog(ERROR, "unexpected end of tape");
			lt->pos = 0;
			return seekpos;
		}

		ltsReadBlock(lts, prev, (void *) lt->buffer);

		if (TapeBlockGetTrailer(lt->buffer)->next != lt->curBlockNumber)
			elog(ERROR, "broken tape, next of block %ld is %ld, expected %ld",
				 prev,
				 TapeBlockGetTrailer(lt->buffer)->next,
				 lt->curBlockNumber);

		lt->nbytes = TapeBlockPayloadSize;
		lt->curBlockNumber = prev;
		lt->nextBlockNumber = TapeBlockGetTrailer(lt->buffer)->next;

		seekpos += TapeBlockPayloadSize;
	}

	/*
	 * 'seekpos' can now be greater than 'size', because it points to the
	 * beginning the target block.  The difference is the position within the
	 * page.
	 */
	lt->pos = seekpos - size;
	return size;
}

/*
 * Seek to an arbitrary position in a logical tape.
 *
 * *Only* a frozen-for-read tape can be seeked.
 *
 * Must be called with a block/offset previously returned by
 * LogicalTapeTell().
 */
void
LogicalTapeSeek(LogicalTapeSet *lts, int tapenum,
				long blocknum, int offset)
{
	LogicalTape *lt;

	Assert(tapenum >= 0 && tapenum < lts->nTapes);
	lt = &lts->tapes[tapenum];
	Assert(lt->frozen);
	Assert(offset >= 0 && offset <= TapeBlockPayloadSize);
	Assert(lt->buffer_size == BLCKSZ);

	if (lt->buffer == NULL)
		ltsInitReadBuffer(lts, lt);

	if (blocknum != lt->curBlockNumber)
	{
		ltsReadBlock(lts, blocknum, (void *) lt->buffer);
		lt->curBlockNumber = blocknum;
		lt->nbytes = TapeBlockPayloadSize;
		lt->nextBlockNumber = TapeBlockGetTrailer(lt->buffer)->next;
	}

	if (offset > lt->nbytes)
		elog(ERROR, "invalid tape seek position");
	lt->pos = offset;
}

/*
 * Obtain current position in a form suitable for a later LogicalTapeSeek.
 *
 * NOTE: it'd be OK to do this during write phase with intention of using
 * the position for a seek after freezing.  Not clear if anyone needs that.
 */
void
LogicalTapeTell(LogicalTapeSet *lts, int tapenum,
				long *blocknum, int *offset)
{
	LogicalTape *lt;

	Assert(tapenum >= 0 && tapenum < lts->nTapes);
	lt = &lts->tapes[tapenum];

	if (lt->buffer == NULL)
		ltsInitReadBuffer(lts, lt);

	Assert(lt->offsetBlockNumber == 0L);

	/* With a larger buffer, 'pos' wouldn't be the same as offset within page */
	Assert(lt->buffer_size == BLCKSZ);

	*blocknum = lt->curBlockNumber;
	*offset = lt->pos;
}

/*
 * Obtain total disk space currently used by a LogicalTapeSet, in blocks.
 *
 * This should not be called while there are open write buffers; otherwise it
 * may not account for buffered data.
 */
long
LogicalTapeSetBlocks(LogicalTapeSet *lts)
{
#ifdef USE_ASSERT_CHECKING
	for (int i = 0; i < lts->nTapes; i++)
	{
		LogicalTape *lt = &lts->tapes[i];

		Assert(!lt->writing || lt->buffer == NULL);
	}
#endif
	return lts->nBlocksWritten - lts->nHoleBlocks;
}
