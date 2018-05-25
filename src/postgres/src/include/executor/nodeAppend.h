/*-------------------------------------------------------------------------
 *
 * nodeAppend.h
 *
 *
 *
 * Portions Copyright (c) 1996-2017, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/executor/nodeAppend.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODEAPPEND_H
#define NODEAPPEND_H

#include "nodes/execnodes.h"

extern AppendState *ExecInitAppend(Append *node, EState *estate, int eflags);
extern void ExecEndAppend(AppendState *node);
extern void ExecReScanAppend(AppendState *node);

#endif							/* NODEAPPEND_H */
