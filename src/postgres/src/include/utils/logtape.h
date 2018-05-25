/*-------------------------------------------------------------------------
 *
 * logtape.h
 *	  Management of "logical tapes" within temporary files.
 *
 * See logtape.c for explanations.
 *
 * Portions Copyright (c) 1996-2017, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/utils/logtape.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef LOGTAPE_H
#define LOGTAPE_H

/* LogicalTapeSet is an opaque type whose details are not known outside logtape.c. */

typedef struct LogicalTapeSet LogicalTapeSet;

/*
 * prototypes for functions in logtape.c
 */

extern LogicalTapeSet *LogicalTapeSetCreate(int ntapes);
extern void LogicalTapeSetClose(LogicalTapeSet *lts);
extern void LogicalTapeSetForgetFreeSpace(LogicalTapeSet *lts);
extern size_t LogicalTapeRead(LogicalTapeSet *lts, int tapenum,
				void *ptr, size_t size);
extern void LogicalTapeWrite(LogicalTapeSet *lts, int tapenum,
				 void *ptr, size_t size);
extern void LogicalTapeRewindForRead(LogicalTapeSet *lts, int tapenum,
						 size_t buffer_size);
extern void LogicalTapeRewindForWrite(LogicalTapeSet *lts, int tapenum);
extern void LogicalTapeFreeze(LogicalTapeSet *lts, int tapenum);
extern size_t LogicalTapeBackspace(LogicalTapeSet *lts, int tapenum,
					 size_t size);
extern void LogicalTapeSeek(LogicalTapeSet *lts, int tapenum,
				long blocknum, int offset);
extern void LogicalTapeTell(LogicalTapeSet *lts, int tapenum,
				long *blocknum, int *offset);
extern long LogicalTapeSetBlocks(LogicalTapeSet *lts);

#endif							/* LOGTAPE_H */
