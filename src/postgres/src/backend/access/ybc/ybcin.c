/*--------------------------------------------------------------------------------------------------
 *
 * ybcin.c
 *	  Implementation of YugaByte indexes.
 *
 * Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 * src/backend/access/ybc/ybcin.c
 *
 * TODO: currently this file contains skeleton index access methods. They will be implemented in
 * coming revisions.
 *--------------------------------------------------------------------------------------------------
 */

#include "postgres.h"

#include "miscadmin.h"
#include "access/relscan.h"
#include "access/sysattr.h"
#include "access/ybcam.h"
#include "access/ybcin.h"
#include "catalog/index.h"
#include "catalog/pg_type.h"
#include "utils/rel.h"
#include "executor/ybcModifyTable.h"

/* --------------------------------------------------------------------------------------------- */

/* Working state for ybcinbuild and its callback */
typedef struct
{
	bool	isprimary;
	double	index_tuples;
} YBCBuildState;

static void
ybcinbuildCallback(Relation index, HeapTuple heapTuple, Datum *values, bool *isnull,
				   bool tupleIsAlive, void *state)
{
	YBCBuildState  *buildstate = (YBCBuildState *)state;

	if (!buildstate->isprimary)
		YBCExecuteInsertIndex(index, values, isnull, heapTuple->t_ybctid);

	buildstate->index_tuples += 1;
}

IndexBuildResult *
ybcinbuild(Relation heap, Relation index, struct IndexInfo *indexInfo)
{
	YBCBuildState	buildstate;
	double			heap_tuples = 0;

	PG_TRY();
	{
		/* Buffer the inserts into the index for initdb */
		if (IsBootstrapProcessingMode())
			YBCStartBufferingWriteOperations();

		/* Do the heap scan */
		buildstate.isprimary = index->rd_index->indisprimary;
		buildstate.index_tuples = 0;
		heap_tuples = IndexBuildHeapScan(heap, index, indexInfo, true, ybcinbuildCallback,
										 &buildstate, NULL);
	}
	PG_CATCH();
	{
		if (IsBootstrapProcessingMode())
			YBCFlushBufferedWriteOperations();
		PG_RE_THROW();
	}
	PG_END_TRY();

	if (IsBootstrapProcessingMode())
		YBCFlushBufferedWriteOperations();

	/*
	 * Return statistics
	 */
	IndexBuildResult *result = (IndexBuildResult *) palloc(sizeof(IndexBuildResult));
	result->heap_tuples  = heap_tuples;
	result->index_tuples = buildstate.index_tuples;
	return result;
}

void
ybcinbuildempty(Relation index)
{
	YBC_LOG_WARNING("Unexpected building of empty unlogged index");
}

bool
ybcininsert(Relation index, Datum *values, bool *isnull, Datum ybctid, Relation heap,
			IndexUniqueCheck checkUnique, struct IndexInfo *indexInfo)
{
	if (!index->rd_index->indisprimary)
		YBCExecuteInsertIndex(index, values, isnull, ybctid);

	return index->rd_index->indisunique ? true : false;
}

void
ybcindelete(Relation index, Datum *values, bool *isnull, Datum ybctid, Relation heap,
			struct IndexInfo *indexInfo)
{
	if (!index->rd_index->indisprimary)
		YBCExecuteDeleteIndex(index, values, isnull, ybctid);
}

IndexBulkDeleteResult *
ybcinbulkdelete(IndexVacuumInfo *info, IndexBulkDeleteResult *stats,
				IndexBulkDeleteCallback callback, void *callback_state)
{
	YBC_LOG_WARNING("Unexpected bulk delete of index via vacuum");
	return NULL;
}

IndexBulkDeleteResult *
ybcinvacuumcleanup(IndexVacuumInfo *info, IndexBulkDeleteResult *stats)
{
	YBC_LOG_WARNING("Unexpected index cleanup via vacuum");
	return NULL;
}

/* --------------------------------------------------------------------------------------------- */

bool ybcincanreturn(Relation index, int attno)
{
	/*
	 * If "canreturn" is true, Postgres will attempt to perform index-only scan on the indexed
	 * columns and expect us to return the column values as an IndexTuple. This will be the case
	 * for secondary index.
	 *
	 * For indexes which are primary keys, we will return the table row as a HeapTuple instead.
	 * For this reason, we set "canreturn" to false for primary keys.
	 */
	return !index->rd_index->indisprimary;
}

void
ybcincostestimate(struct PlannerInfo *root, struct IndexPath *path, double loop_count,
				  Cost *indexStartupCost, Cost *indexTotalCost, Selectivity *indexSelectivity,
				  double *indexCorrelation, double *indexPages)
{
}

bytea *
ybcinoptions(Datum reloptions, bool validate)
{
	return NULL;
}

bool
ybcinproperty(Oid index_oid, int attno, IndexAMProperty prop, const char *propname,
			  bool *res, bool *isnull)
{
	return false;	
}

bool
ybcinvalidate(Oid opclassoid)
{
	return true;
}

/* --------------------------------------------------------------------------------------------- */

IndexScanDesc
ybcinbeginscan(Relation rel, int nkeys, int norderbys)
{
	IndexScanDesc scan;

	/* no order by operators allowed */
	Assert(norderbys == 0);

	/* get the scan */
	scan = RelationGetIndexScan(rel, nkeys, norderbys);
	scan->opaque = NULL;

	return scan;
}

void 
ybcinrescan(IndexScanDesc scan, ScanKey scankey, int nscankeys,	ScanKey orderbys, int norderbys)
{
	if (scan->indexRelation->rd_index->indisprimary)
		ybc_pkey_beginscan(scan->heapRelation, scan->indexRelation, scan, nscankeys, scankey);
	else
		ybc_index_beginscan(scan->indexRelation, scan, nscankeys, scankey);
}

bool
ybcingettuple(IndexScanDesc scan, ScanDirection dir)
{
	scan->xs_ctup.t_ybctid = 0;

	/* 
	 * If IndexTuple is requested or it is a secondary index, return the result as IndexTuple.
	 * Otherwise, return the result as a HeapTuple of the base table.
	 */
	if (scan->xs_want_itup || !scan->indexRelation->rd_index->indisprimary)
	{
		IndexTuple tuple = ybc_index_getnext(scan);

		if (tuple)
		{
			scan->xs_ctup.t_ybctid = tuple->t_ybctid;
			scan->xs_itup = tuple;
			scan->xs_itupdesc = RelationGetDescr(scan->indexRelation);
		}
	}
	else
	{
		HeapTuple tuple = ybc_pkey_getnext(scan);

		if (tuple)
		{
			scan->xs_ctup.t_ybctid = tuple->t_ybctid;
			scan->xs_hitup = tuple;
			scan->xs_hitupdesc = RelationGetDescr(scan->heapRelation);
		}
	}

	return scan->xs_ctup.t_ybctid != 0;
}

void 
ybcinendscan(IndexScanDesc scan)
{
	if (scan->indexRelation->rd_index->indisprimary)
		ybc_pkey_endscan(scan);
	else
		ybc_index_endscan(scan);
}
