/*-------------------------------------------------------------------------
 *
 * nodeModifyTable.c
 *	  routines to handle ModifyTable nodes.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/executor/nodeModifyTable.c
 *
 * The following only applies to changes made to this file as part of
 * YugaByte development.
 *
 * Portions Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *-------------------------------------------------------------------------
 */
/* INTERFACE ROUTINES
 *		ExecInitModifyTable - initialize the ModifyTable node
 *		ExecModifyTable		- retrieve the next tuple from the node
 *		ExecEndModifyTable	- shut down the ModifyTable node
 *		ExecReScanModifyTable - rescan the ModifyTable node
 *
 *	 NOTES
 *		The ModifyTable node receives input from its outerPlan, which is
 *		the data to insert for INSERT cases, the changed columns' new
 *		values plus row-locating info for UPDATE and MERGE cases, or just the
 *		row-locating info for DELETE cases.
 *
 *		MERGE runs a join between the source relation and the target
 *		table; if any WHEN NOT MATCHED clauses are present, then the
 *		join is an outer join.  In this case, any unmatched tuples will
 *		have NULL row-locating info, and only INSERT can be run. But for
 *		matched tuples, then row-locating info is used to determine the
 *		tuple to UPDATE or DELETE. When all clauses are WHEN MATCHED,
 *		then an inner join is used, so all tuples contain row-locating info.
 *
 *		If the query specifies RETURNING, then the ModifyTable returns a
 *		RETURNING tuple after completing each row insert, update, or delete.
 *		It must be called again to continue the operation.  Without RETURNING,
 *		we just loop within the node until all the work is done, then
 *		return NULL.  This avoids useless call/return overhead.  (MERGE does
 *		not support RETURNING.)
 */

#include "postgres.h"

#include "access/heapam.h"
#include "access/htup_details.h"
#include "access/tableam.h"
#include "access/xact.h"
#include "catalog/catalog.h"
#include "commands/trigger.h"
#include "executor/execPartition.h"
#include "executor/executor.h"
#include "executor/nodeModifyTable.h"
#include "foreign/fdwapi.h"
#include "miscadmin.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/optimizer.h"
#include "rewrite/rewriteHandler.h"
#include "storage/bufmgr.h"
#include "storage/lmgr.h"
#include "utils/builtins.h"
#include "utils/datum.h"
#include "utils/memutils.h"
#include "utils/rel.h"

/*  YB includes. */
#include "access/sysattr.h"
#include "catalog/dependency.h"
#include "catalog/pg_database.h"
#include "catalog/pg_depend.h"
#include "catalog/pg_shdepend.h"
#include "catalog/yb_catalog_version.h"
#include "executor/ybcModifyTable.h"
#include "optimizer/ybcplan.h"
#include "parser/parsetree.h"
#include "utils/syscache.h"
#include "utils/typcache.h"

#include "pg_yb_utils.h"

typedef struct MTTargetRelLookup
{
	Oid			relationOid;	/* hash key, must be first */
	int			relationIndex;	/* rel's index in resultRelInfo[] array */
} MTTargetRelLookup;

/*
 * Context struct for a ModifyTable operation, containing basic execution
 * state and some output variables populated by ExecUpdateAct() and
 * ExecDeleteAct() to report the result of their actions to callers.
 */
typedef struct ModifyTableContext
{
	/* Operation state */
	ModifyTableState *mtstate;
	EPQState   *epqstate;
	EState	   *estate;

	/*
	 * Slot containing tuple obtained from ModifyTable's subplan.  Used to
	 * access "junk" columns that are not going to be stored.
	 */
	TupleTableSlot *planSlot;

	/*
	 * During EvalPlanQual, project and return the new version of the new
	 * tuple
	 */
	TupleTableSlot *(*GetUpdateNewTuple) (ResultRelInfo *resultRelInfo,
										  TupleTableSlot *epqslot,
										  TupleTableSlot *oldSlot,
										  MergeActionState *relaction);

	/* MERGE specific */
	MergeActionState *relaction;	/* MERGE action in progress */

	/*
	 * Information about the changes that were made concurrently to a tuple
	 * being updated or deleted
	 */
	TM_FailureData tmfd;

	/*
	 * The tuple produced by EvalPlanQual to retry from, if a cross-partition
	 * UPDATE requires it
	 */
	TupleTableSlot *cpUpdateRetrySlot;

	/*
	 * The tuple projected by the INSERT's RETURNING clause, when doing a
	 * cross-partition UPDATE
	 */
	TupleTableSlot *cpUpdateReturningSlot;
} ModifyTableContext;

/*
 * Context struct containing output data specific to UPDATE operations.
 */
typedef struct UpdateContext
{
	bool		updated;		/* did UPDATE actually occur? */
	bool		updateIndexes;	/* index update required? */
	bool		crossPartUpdate;	/* was it a cross-partition update? */

	/*
	 * Lock mode to acquire on the latest tuple version before performing
	 * EvalPlanQual on it
	 */
	LockTupleMode lockmode;
} UpdateContext;


static void ExecBatchInsert(ModifyTableState *mtstate,
							ResultRelInfo *resultRelInfo,
							TupleTableSlot **slots,
							TupleTableSlot **planSlots,
							int numSlots,
							EState *estate,
							bool canSetTag);
static void ExecPendingInserts(EState *estate);
static void ExecCrossPartitionUpdateForeignKey(ModifyTableContext *context,
											   ResultRelInfo *sourcePartInfo,
											   ResultRelInfo *destPartInfo,
											   ItemPointer tupleid,
											   TupleTableSlot *oldslot,
											   TupleTableSlot *newslot);
static bool ExecOnConflictUpdate(ModifyTableContext *context,
								 ResultRelInfo *resultRelInfo,
								 ItemPointer conflictTid,
								 TupleTableSlot *excludedSlot,
								 bool canSetTag,
								 TupleTableSlot **returning);
static TupleTableSlot *ExecPrepareTupleRouting(ModifyTableState *mtstate,
											   EState *estate,
											   PartitionTupleRouting *proute,
											   ResultRelInfo *targetRelInfo,
											   TupleTableSlot *slot,
											   ResultRelInfo **partRelInfo);
static TupleTableSlot *internalGetUpdateNewTuple(ResultRelInfo *relinfo,
												 TupleTableSlot *planSlot,
												 TupleTableSlot *oldSlot,
												 MergeActionState *relaction);

static TupleTableSlot *ExecMerge(ModifyTableContext *context,
								 ResultRelInfo *resultRelInfo,
								 ItemPointer tupleid,
								 bool canSetTag);
static void ExecInitMerge(ModifyTableState *mtstate, EState *estate);
static bool ExecMergeMatched(ModifyTableContext *context,
							 ResultRelInfo *resultRelInfo,
							 ItemPointer tupleid,
							 bool canSetTag);
static void ExecMergeNotMatched(ModifyTableContext *context,
								ResultRelInfo *resultRelInfo,
								bool canSetTag);
static TupleTableSlot *mergeGetUpdateNewTuple(ResultRelInfo *relinfo,
											  TupleTableSlot *planSlot,
											  TupleTableSlot *oldSlot,
											  MergeActionState *relaction);


static void YbPostProcessDml(CmdType cmd_type,
							 Relation rel,
							 TupleDesc desc,
							 HeapTuple newtup);
static void YbHandlePossibleObjectPinning(TupleDesc desc,
										  HeapTuple tuple,
										  bool is_shared_dep);

/*
 * Verify that the tuples to be produced by INSERT match the
 * target relation's rowtype
 *
 * We do this to guard against stale plans.  If plan invalidation is
 * functioning properly then we should never get a failure here, but better
 * safe than sorry.  Note that this is called after we have obtained lock
 * on the target rel, so the rowtype can't change underneath us.
 *
 * The plan output is represented by its targetlist, because that makes
 * handling the dropped-column case easier.
 *
 * We used to use this for UPDATE as well, but now the equivalent checks
 * are done in ExecBuildUpdateProjection.
 */
static void
ExecCheckPlanOutput(Relation resultRel, List *targetList)
{
	TupleDesc	resultDesc = RelationGetDescr(resultRel);
	int			attno = 0;
	ListCell   *lc;

	foreach(lc, targetList)
	{
		TargetEntry *tle = (TargetEntry *) lfirst(lc);
		Form_pg_attribute attr;

		Assert(!tle->resjunk);	/* caller removed junk items already */
		if (tle->resjunk)
			continue;			/* ignore junk tlist items */

		if (IsYsqlUpgrade && tle->resno == ObjectIdAttributeNumber)
			continue;			/* ignore oid system column used in YSQL upgrade */

		if (attno >= resultDesc->natts)
			ereport(ERROR,
					(errcode(ERRCODE_DATATYPE_MISMATCH),
					 errmsg("table row type and query-specified row type do not match"),
					 errdetail("Query has too many columns.")));
		attr = TupleDescAttr(resultDesc, attno);
		attno++;

		if (!attr->attisdropped)
		{
			/* Normal case: demand type match */
			if (exprType((Node *) tle->expr) != attr->atttypid)
				ereport(ERROR,
						(errcode(ERRCODE_DATATYPE_MISMATCH),
						 errmsg("table row type and query-specified row type do not match"),
						 errdetail("Table has type %s at ordinal position %d, but query expects %s.",
								   format_type_be(attr->atttypid),
								   attno,
								   format_type_be(exprType((Node *) tle->expr)))));
		}
		else
		{
			/*
			 * For a dropped column, we can't check atttypid (it's likely 0).
			 * In any case the planner has most likely inserted an INT4 null.
			 * What we insist on is just *some* NULL constant.
			 */
			if (!IsA(tle->expr, Const) ||
				!((Const *) tle->expr)->constisnull)
				ereport(ERROR,
						(errcode(ERRCODE_DATATYPE_MISMATCH),
						 errmsg("table row type and query-specified row type do not match"),
						 errdetail("Query provides a value for a dropped column at ordinal position %d.",
								   attno)));
		}
	}
	if (attno != resultDesc->natts)
		ereport(ERROR,
				(errcode(ERRCODE_DATATYPE_MISMATCH),
				 errmsg("table row type and query-specified row type do not match"),
				 errdetail("Query has too few columns.")));
}

/*
 * ExecProcessReturning --- evaluate a RETURNING list
 *
 * resultRelInfo: current result rel
 * tupleSlot: slot holding tuple actually inserted/updated/deleted
 * planSlot: slot holding tuple returned by top subplan node
 *
 * Note: If tupleSlot is NULL, the FDW should have already provided econtext's
 * scan tuple.
 *
 * Returns a slot holding the result tuple
 */
static TupleTableSlot *
ExecProcessReturning(ResultRelInfo *resultRelInfo,
					 TupleTableSlot *tupleSlot,
					 TupleTableSlot *planSlot)
{
	ProjectionInfo *projectReturning = resultRelInfo->ri_projectReturning;
	ExprContext *econtext = projectReturning->pi_exprContext;

	/* Make tuple and any needed join variables available to ExecProject */
	if (tupleSlot)
		econtext->ecxt_scantuple = tupleSlot;
	econtext->ecxt_outertuple = planSlot;

	/*
	 * RETURNING expressions might reference the tableoid column, so
	 * reinitialize tts_tableOid before evaluating them.
	 */
	econtext->ecxt_scantuple->tts_tableOid =
		RelationGetRelid(resultRelInfo->ri_RelationDesc);

	/* Compute the RETURNING expressions */
	return ExecProject(projectReturning);
}

/*
 * ExecCheckTupleVisible -- verify tuple is visible
 *
 * It would not be consistent with guarantees of the higher isolation levels to
 * proceed with avoiding insertion (taking speculative insertion's alternative
 * path) on the basis of another tuple that is not visible to MVCC snapshot.
 * Check for the need to raise a serialization failure, and do so as necessary.
 */
static void
ExecCheckTupleVisible(EState *estate,
					  Relation rel,
					  TupleTableSlot *slot)
{
	if (!IsolationUsesXactSnapshot())
		return;

	if (!table_tuple_satisfies_snapshot(rel, slot, estate->es_snapshot))
	{
		Datum		xminDatum;
		TransactionId xmin;
		bool		isnull;

		xminDatum = slot_getsysattr(slot, MinTransactionIdAttributeNumber, &isnull);
		Assert(!isnull);
		xmin = DatumGetTransactionId(xminDatum);

		/*
		 * We should not raise a serialization failure if the conflict is
		 * against a tuple inserted by our own transaction, even if it's not
		 * visible to our snapshot.  (This would happen, for example, if
		 * conflicting keys are proposed for insertion in a single command.)
		 */
		if (!TransactionIdIsCurrentTransactionId(xmin))
			ereport(ERROR,
					(errcode(ERRCODE_T_R_SERIALIZATION_FAILURE),
					 errmsg("could not serialize access due to concurrent update")));
	}
}

/*
 * ExecCheckTIDVisible -- convenience variant of ExecCheckTupleVisible()
 */
static void
ExecCheckTIDVisible(EState *estate,
					ResultRelInfo *relinfo,
					ItemPointer tid,
					TupleTableSlot *tempSlot)
{
	Relation	rel = relinfo->ri_RelationDesc;

	/* Redundantly check isolation level */
	if (!IsolationUsesXactSnapshot())
		return;

	if (!table_tuple_fetch_row_version(rel, tid, SnapshotAny, tempSlot))
		elog(ERROR, "failed to fetch conflicting tuple for ON CONFLICT");
	ExecCheckTupleVisible(estate, rel, tempSlot);
	ExecClearTuple(tempSlot);
}

/*
 * Initialize to compute stored generated columns for a tuple
 *
 * This fills the resultRelInfo's ri_GeneratedExprs field and makes an
 * associated ResultRelInfoExtra struct to hold ri_extraUpdatedCols.
 * (Currently, ri_extraUpdatedCols is consulted only in UPDATE, but we might
 * as well fill it for INSERT too.)
 */
void
ExecInitStoredGenerated(ResultRelInfo *resultRelInfo,
						EState *estate,
						CmdType cmdtype)
{
	Relation	rel = resultRelInfo->ri_RelationDesc;
	TupleDesc	tupdesc = RelationGetDescr(rel);
	int			natts = tupdesc->natts;
	Bitmapset  *updatedCols;
	ResultRelInfoExtra *rextra;
	MemoryContext oldContext;

	/* Don't call twice */
	Assert(resultRelInfo->ri_GeneratedExprs == NULL);

	/* Nothing to do if no generated columns */
	if (!(tupdesc->constr && tupdesc->constr->has_generated_stored))
		return;

	/*
	 * In an UPDATE, we can skip computing any generated columns that do not
	 * depend on any UPDATE target column.  But if there is a BEFORE ROW
	 * UPDATE trigger, we cannot skip because the trigger might change more
	 * columns.
	 */
	if (cmdtype == CMD_UPDATE &&
		!(rel->trigdesc && rel->trigdesc->trig_update_before_row))
		updatedCols = ExecGetUpdatedCols(resultRelInfo, estate);
	else
		updatedCols = NULL;

	/*
	 * Make sure these data structures are built in the per-query memory
	 * context so they'll survive throughout the query.
	 */
	oldContext = MemoryContextSwitchTo(estate->es_query_cxt);

	resultRelInfo->ri_GeneratedExprs =
		(ExprState **) palloc0(natts * sizeof(ExprState *));
	resultRelInfo->ri_NumGeneratedNeeded = 0;

	rextra = palloc_object(ResultRelInfoExtra);
	rextra->rinfo = resultRelInfo;
	rextra->ri_extraUpdatedCols = NULL;
	estate->es_resultrelinfo_extra = lappend(estate->es_resultrelinfo_extra,
											 rextra);

	for (int i = 0; i < natts; i++)
	{
		if (TupleDescAttr(tupdesc, i)->attgenerated == ATTRIBUTE_GENERATED_STORED)
		{
			Expr	   *expr;

			/* Fetch the GENERATED AS expression tree */
			expr = (Expr *) build_column_default(rel, i + 1);
			if (expr == NULL)
				elog(ERROR, "no generation expression found for column number %d of table \"%s\"",
					 i + 1, RelationGetRelationName(rel));

			/*
			 * If it's an update with a known set of update target columns,
			 * see if we can skip the computation.
			 */
			if (updatedCols)
			{
				Bitmapset  *attrs_used = NULL;

				pull_varattnos((Node *) expr, 1, &attrs_used);

				if (!bms_overlap(updatedCols, attrs_used))
					continue;	/* need not update this column */
			}

			/* No luck, so prepare the expression for execution */
			resultRelInfo->ri_GeneratedExprs[i] = ExecPrepareExpr(expr, estate);
			resultRelInfo->ri_NumGeneratedNeeded++;

			/* And mark this column in rextra->ri_extraUpdatedCols */
			rextra->ri_extraUpdatedCols =
				bms_add_member(rextra->ri_extraUpdatedCols,
							   i + 1 - FirstLowInvalidHeapAttributeNumber);
		}
	}

	MemoryContextSwitchTo(oldContext);
}

/*
 * Compute stored generated columns for a tuple
 */
void
ExecComputeStoredGenerated(ResultRelInfo *resultRelInfo,
						   EState *estate, TupleTableSlot *slot,
						   CmdType cmdtype)
{
	Relation	rel = resultRelInfo->ri_RelationDesc;
	TupleDesc	tupdesc = RelationGetDescr(rel);
	int			natts = tupdesc->natts;
	ExprContext *econtext = GetPerTupleExprContext(estate);
	MemoryContext oldContext;
	Datum	   *values;
	bool	   *nulls;

	/* We should not be called unless this is true */
	Assert(tupdesc->constr && tupdesc->constr->has_generated_stored);

	/*
	 * For relations named directly in the query, ExecInitStoredGenerated
	 * should have been called already; but this might not have happened yet
	 * for a partition child rel.  Also, it's convenient for outside callers
	 * to not have to call ExecInitStoredGenerated explicitly.
	 */
	if (resultRelInfo->ri_GeneratedExprs == NULL)
		ExecInitStoredGenerated(resultRelInfo, estate, cmdtype);

	/*
	 * If no generated columns have been affected by this change, then skip
	 * the rest.
	 */
	if (resultRelInfo->ri_NumGeneratedNeeded == 0)
		return;

	oldContext = MemoryContextSwitchTo(GetPerTupleMemoryContext(estate));

	values = palloc(sizeof(*values) * natts);
	nulls = palloc(sizeof(*nulls) * natts);

	slot_getallattrs(slot);
	memcpy(nulls, slot->tts_isnull, sizeof(*nulls) * natts);

	for (int i = 0; i < natts; i++)
	{
		Form_pg_attribute attr = TupleDescAttr(tupdesc, i);

		if (resultRelInfo->ri_GeneratedExprs[i])
		{
			Datum		val;
			bool		isnull;

			Assert(attr->attgenerated == ATTRIBUTE_GENERATED_STORED);

			econtext->ecxt_scantuple = slot;

			val = ExecEvalExpr(resultRelInfo->ri_GeneratedExprs[i], econtext, &isnull);

			/*
			 * We must make a copy of val as we have no guarantees about where
			 * memory for a pass-by-reference Datum is located.
			 */
			if (!isnull)
				val = datumCopy(val, attr->attbyval, attr->attlen);

			values[i] = val;
			nulls[i] = isnull;
		}
		else
		{
			if (!nulls[i])
				values[i] = datumCopy(slot->tts_values[i], attr->attbyval, attr->attlen);
		}
	}

	ExecClearTuple(slot);
	memcpy(slot->tts_values, values, sizeof(*values) * natts);
	memcpy(slot->tts_isnull, nulls, sizeof(*nulls) * natts);
	ExecStoreVirtualTuple(slot);
	ExecMaterializeSlot(slot);

	MemoryContextSwitchTo(oldContext);
}

/*
 * ExecInitInsertProjection
 *		Do one-time initialization of projection data for INSERT tuples.
 *
 * INSERT queries may need a projection to filter out junk attrs in the tlist.
 *
 * This is also a convenient place to verify that the
 * output of an INSERT matches the target table.
 */
static void
ExecInitInsertProjection(ModifyTableState *mtstate,
						 ResultRelInfo *resultRelInfo)
{
	ModifyTable *node = (ModifyTable *) mtstate->ps.plan;
	Plan	   *subplan = outerPlan(node);
	EState	   *estate = mtstate->ps.state;
	List	   *insertTargetList = NIL;
	bool		need_projection = false;
	ListCell   *l;

	/* Extract non-junk columns of the subplan's result tlist. */
	foreach(l, subplan->targetlist)
	{
		TargetEntry *tle = (TargetEntry *) lfirst(l);

		if (!tle->resjunk)
			insertTargetList = lappend(insertTargetList, tle);
		else
			need_projection = true;
	}

	/*
	 * The junk-free list must produce a tuple suitable for the result
	 * relation.
	 */
	ExecCheckPlanOutput(resultRelInfo->ri_RelationDesc, insertTargetList);

	/* We'll need a slot matching the table's format. */
	resultRelInfo->ri_newTupleSlot =
		table_slot_create(resultRelInfo->ri_RelationDesc,
						  &estate->es_tupleTable);

	/* Build ProjectionInfo if needed (it probably isn't). */
	if (need_projection)
	{
		TupleDesc	relDesc = RelationGetDescr(resultRelInfo->ri_RelationDesc);

		/* need an expression context to do the projection */
		if (mtstate->ps.ps_ExprContext == NULL)
			ExecAssignExprContext(estate, &mtstate->ps);

		resultRelInfo->ri_projectNew =
			ExecBuildProjectionInfo(insertTargetList,
									mtstate->ps.ps_ExprContext,
									resultRelInfo->ri_newTupleSlot,
									&mtstate->ps,
									relDesc);
	}

	resultRelInfo->ri_projectNewInfoValid = true;
}

/*
 * ExecInitUpdateProjection
 *		Do one-time initialization of projection data for UPDATE tuples.
 *
 * UPDATE always needs a projection, because (1) there's always some junk
 * attrs, and (2) we may need to merge values of not-updated columns from
 * the old tuple into the final tuple.  In UPDATE, the tuple arriving from
 * the subplan contains only new values for the changed columns, plus row
 * identity info in the junk attrs.
 *
 * This is "one-time" for any given result rel, but we might touch more than
 * one result rel in the course of an inherited UPDATE, and each one needs
 * its own projection due to possible column order variation.
 *
 * This is also a convenient place to verify that the output of an UPDATE
 * matches the target table (ExecBuildUpdateProjection does that).
 */
static void
ExecInitUpdateProjection(ModifyTableState *mtstate,
						 ResultRelInfo *resultRelInfo)
{
	ModifyTable *node = (ModifyTable *) mtstate->ps.plan;
	Plan	   *subplan = outerPlan(node);
	EState	   *estate = mtstate->ps.state;
	TupleDesc	relDesc = RelationGetDescr(resultRelInfo->ri_RelationDesc);
	int			whichrel;
	List	   *updateColnos;

	/*
	 * Usually, mt_lastResultIndex matches the target rel.  If it happens not
	 * to, we can get the index the hard way with an integer division.
	 */
	whichrel = mtstate->mt_lastResultIndex;
	if (resultRelInfo != mtstate->resultRelInfo + whichrel)
	{
		whichrel = resultRelInfo - mtstate->resultRelInfo;
		Assert(whichrel >= 0 && whichrel < mtstate->mt_nrels);
	}

	updateColnos = (List *) list_nth(node->updateColnosLists, whichrel);

	/*
	 * For UPDATE, we use the old tuple to fill up missing values in the tuple
	 * produced by the subplan to get the new tuple.  We need two slots, both
	 * matching the table's desired format.
	 */
	resultRelInfo->ri_oldTupleSlot =
		table_slot_create(resultRelInfo->ri_RelationDesc,
						  &estate->es_tupleTable);
	resultRelInfo->ri_newTupleSlot =
		table_slot_create(resultRelInfo->ri_RelationDesc,
						  &estate->es_tupleTable);

	/* need an expression context to do the projection */
	if (mtstate->ps.ps_ExprContext == NULL)
		ExecAssignExprContext(estate, &mtstate->ps);

	resultRelInfo->ri_projectNew =
		ExecBuildUpdateProjection(subplan->targetlist,
								  false,	/* subplan did the evaluation */
								  updateColnos,
								  relDesc,
								  mtstate->ps.ps_ExprContext,
								  resultRelInfo->ri_newTupleSlot,
								  &mtstate->ps);

	resultRelInfo->ri_projectNewInfoValid = true;
}

/*
 * ExecGetInsertNewTuple
 *		This prepares a "new" tuple ready to be inserted into given result
 *		relation, by removing any junk columns of the plan's output tuple
 *		and (if necessary) coercing the tuple to the right tuple format.
 */
static TupleTableSlot *
ExecGetInsertNewTuple(ResultRelInfo *relinfo,
					  TupleTableSlot *planSlot)
{
	ProjectionInfo *newProj = relinfo->ri_projectNew;
	ExprContext *econtext;

	/*
	 * If there's no projection to be done, just make sure the slot is of the
	 * right type for the target rel.  If the planSlot is the right type we
	 * can use it as-is, else copy the data into ri_newTupleSlot.
	 */
	if (newProj == NULL)
	{
		if (relinfo->ri_newTupleSlot->tts_ops != planSlot->tts_ops)
		{
			ExecCopySlot(relinfo->ri_newTupleSlot, planSlot);
			return relinfo->ri_newTupleSlot;
		}
		else
			return planSlot;
	}

	/*
	 * Else project; since the projection output slot is ri_newTupleSlot, this
	 * will also fix any slot-type problem.
	 *
	 * Note: currently, this is dead code, because INSERT cases don't receive
	 * any junk columns so there's never a projection to be done.
	 */
	econtext = newProj->pi_exprContext;
	econtext->ecxt_outertuple = planSlot;
	return ExecProject(newProj);
}

/*
 * ExecGetUpdateNewTuple
 *		This prepares a "new" tuple by combining an UPDATE subplan's output
 *		tuple (which contains values of changed columns) with unchanged
 *		columns taken from the old tuple.
 *
 * The subplan tuple might also contain junk columns, which are ignored.
 * Note that the projection also ensures we have a slot of the right type.
 */
TupleTableSlot *
ExecGetUpdateNewTuple(ResultRelInfo *relinfo,
					  TupleTableSlot *planSlot,
					  TupleTableSlot *oldSlot)
{
	/* Use a few extra Asserts to protect against outside callers */
	Assert(relinfo->ri_projectNewInfoValid);
	Assert(planSlot != NULL && !TTS_EMPTY(planSlot));
	Assert(oldSlot != NULL && !TTS_EMPTY(oldSlot));

	return internalGetUpdateNewTuple(relinfo, planSlot, oldSlot, NULL);
}

/*
 * Callback for ModifyTableState->GetUpdateNewTuple for use by regular UPDATE.
 */
static TupleTableSlot *
internalGetUpdateNewTuple(ResultRelInfo *relinfo,
						  TupleTableSlot *planSlot,
						  TupleTableSlot *oldSlot,
						  MergeActionState *relaction)
{
	ProjectionInfo *newProj = relinfo->ri_projectNew;
	ExprContext *econtext;

	econtext = newProj->pi_exprContext;
	econtext->ecxt_outertuple = planSlot;
	econtext->ecxt_scantuple = oldSlot;
	return ExecProject(newProj);
}

/* ----------------------------------------------------------------
 *		ExecInsert
 *
 *		For INSERT, we have to insert the tuple into the target relation
 *		(or partition thereof) and insert appropriate tuples into the index
 *		relations.
 *
 *		slot contains the new tuple value to be stored.
 *
 *		Returns RETURNING result if any, otherwise NULL.
 *		*inserted_tuple is the tuple that's effectively inserted;
 *		*inserted_destrel is the relation where it was inserted.
 *		These are only set on success.
 *
 *		This may change the currently active tuple conversion map in
 *		mtstate->mt_transition_capture, so the callers must take care to
 *		save the previous value to avoid losing track of it.
 * ----------------------------------------------------------------
 */
static TupleTableSlot *
ExecInsert(ModifyTableContext *context,
		   ResultRelInfo *resultRelInfo,
		   TupleTableSlot *slot,
		   bool canSetTag,
		   TupleTableSlot **inserted_tuple,
		   ResultRelInfo **insert_destrel)
{
	ModifyTableState *mtstate = context->mtstate;
	EState	   *estate = context->estate;
	Relation	resultRelationDesc;
	List	   *recheckIndexes = NIL;
	TupleTableSlot *planSlot = context->planSlot;
	TupleTableSlot *result = NULL;
	TransitionCaptureState *ar_insert_trig_tcs;
	ModifyTable *node = (ModifyTable *) mtstate->ps.plan;
	OnConflictAction onconflict = node->onConflictAction;
	PartitionTupleRouting *proute = mtstate->mt_partition_tuple_routing;
	MemoryContext oldContext;

	/*
	 * The attribute "yb_conflict_slot" is only used within ExecInsert.
	 * Initialize its value to NULL.
	 */
	estate->yb_conflict_slot = NULL;

	/*
	 * If the input result relation is a partitioned table, find the leaf
	 * partition to insert the tuple into.
	 */
	if (proute)
	{
		ResultRelInfo *partRelInfo;

		slot = ExecPrepareTupleRouting(mtstate, estate, proute,
									   resultRelInfo, slot,
									   &partRelInfo);
		resultRelInfo = partRelInfo;
	}

	ExecMaterializeSlot(slot);

	resultRelationDesc = resultRelInfo->ri_RelationDesc;

	/*
	 * TODO(alex@yugabyte): Relation structure no longer has OID.
	 *
	 * YB note:
	 * --------
	 * YSQL upgrade introduces a hacky way for INSERT to set OID implemented
	 * via tts_yb_insert_oid. It would become obsolete after upgrade to PG 12
	 * which would make oid a regular column.
	 */
#ifdef YB_TODO
	if (resultRelationDesc->rd_rel->relhasoids)
	{
		Oid tuple_oid = InvalidOid;

		if (IsYsqlUpgrade && IsYBRelation(resultRelationDesc))
			tuple_oid = slot->tts_yb_insert_oid;

		HeapTupleSetOid(tuple, tuple_oid);
	}
#endif

	/*
	 * Open the table's indexes, if we have not done so already, so that we
	 * can add new index entries for the inserted tuple.
	 */
	if (IsYBRelation(resultRelInfo->ri_RelationDesc))
	{
		/*
		 * For a YugaByte table, we need to update the secondary indices for
		 * all of the INSERT, UPDATE, and DELETE statements. The ON CONFLICT UPDATE
		 * execution also needs to process primary key index.
		 */
		if (YBRelHasSecondaryIndices(resultRelInfo->ri_RelationDesc) ||
			node->onConflictAction != ONCONFLICT_NONE)
			ExecOpenIndices(resultRelInfo, onconflict != ONCONFLICT_NONE);
	}
	else
	{
		if (resultRelationDesc->rd_rel->relhasindex &&
			resultRelInfo->ri_IndexRelationDescs == NULL)
			ExecOpenIndices(resultRelInfo, onconflict != ONCONFLICT_NONE);
	}

	/*
	 * BEFORE ROW INSERT Triggers.
	 *
	 * Note: We fire BEFORE ROW TRIGGERS for every attempted insertion in an
	 * INSERT ... ON CONFLICT statement.  We cannot check for constraint
	 * violations before firing these triggers, because they can change the
	 * values to insert.  Also, they can run arbitrary user-defined code with
	 * side-effects that we can't cancel by just not inserting the tuple.
	 */
	if (resultRelInfo->ri_TrigDesc &&
		resultRelInfo->ri_TrigDesc->trig_insert_before_row)
	{
		/* Flush any pending inserts, so rows are visible to the triggers */
		if (estate->es_insert_pending_result_relations != NIL)
			ExecPendingInserts(estate);

		if (!ExecBRInsertTriggers(estate, resultRelInfo, slot))
			return NULL;		/* "do nothing" */
	}

	/* INSTEAD OF ROW INSERT Triggers */
	if (resultRelInfo->ri_TrigDesc &&
		resultRelInfo->ri_TrigDesc->trig_insert_instead_row)
	{
		if (!ExecIRInsertTriggers(estate, resultRelInfo, slot))
			return NULL;		/* "do nothing" */
	}
	else if (resultRelInfo->ri_FdwRoutine)
	{
		/*
		 * GENERATED expressions might reference the tableoid column, so
		 * (re-)initialize tts_tableOid before evaluating them.
		 */
		slot->tts_tableOid = RelationGetRelid(resultRelInfo->ri_RelationDesc);

		/*
		 * Compute stored generated columns
		 */
		if (resultRelationDesc->rd_att->constr &&
			resultRelationDesc->rd_att->constr->has_generated_stored)
			ExecComputeStoredGenerated(resultRelInfo, estate, slot,
									   CMD_INSERT);

		/*
		 * If the FDW supports batching, and batching is requested, accumulate
		 * rows and insert them in batches. Otherwise use the per-row inserts.
		 */
		if (resultRelInfo->ri_BatchSize > 1)
		{
			bool		flushed = false;

			/*
			 * When we've reached the desired batch size, perform the
			 * insertion.
			 */
			if (resultRelInfo->ri_NumSlots == resultRelInfo->ri_BatchSize)
			{
				ExecBatchInsert(mtstate, resultRelInfo,
								resultRelInfo->ri_Slots,
								resultRelInfo->ri_PlanSlots,
								resultRelInfo->ri_NumSlots,
								estate, canSetTag);
				resultRelInfo->ri_NumSlots = 0;
				flushed = true;
			}

			oldContext = MemoryContextSwitchTo(estate->es_query_cxt);

			if (resultRelInfo->ri_Slots == NULL)
			{
				resultRelInfo->ri_Slots = palloc(sizeof(TupleTableSlot *) *
												 resultRelInfo->ri_BatchSize);
				resultRelInfo->ri_PlanSlots = palloc(sizeof(TupleTableSlot *) *
													 resultRelInfo->ri_BatchSize);
			}

			/*
			 * Initialize the batch slots. We don't know how many slots will
			 * be needed, so we initialize them as the batch grows, and we
			 * keep them across batches. To mitigate an inefficiency in how
			 * resource owner handles objects with many references (as with
			 * many slots all referencing the same tuple descriptor) we copy
			 * the appropriate tuple descriptor for each slot.
			 */
			if (resultRelInfo->ri_NumSlots >= resultRelInfo->ri_NumSlotsInitialized)
			{
				TupleDesc	tdesc = CreateTupleDescCopy(slot->tts_tupleDescriptor);
				TupleDesc	plan_tdesc =
				CreateTupleDescCopy(planSlot->tts_tupleDescriptor);

				resultRelInfo->ri_Slots[resultRelInfo->ri_NumSlots] =
					MakeSingleTupleTableSlot(tdesc, slot->tts_ops);

				resultRelInfo->ri_PlanSlots[resultRelInfo->ri_NumSlots] =
					MakeSingleTupleTableSlot(plan_tdesc, planSlot->tts_ops);

				/* remember how many batch slots we initialized */
				resultRelInfo->ri_NumSlotsInitialized++;
			}

			ExecCopySlot(resultRelInfo->ri_Slots[resultRelInfo->ri_NumSlots],
						 slot);

			ExecCopySlot(resultRelInfo->ri_PlanSlots[resultRelInfo->ri_NumSlots],
						 planSlot);

			/*
			 * If these are the first tuples stored in the buffers, add the
			 * target rel and the mtstate to the
			 * es_insert_pending_result_relations and
			 * es_insert_pending_modifytables lists respectively, execpt in
			 * the case where flushing was done above, in which case they
			 * would already have been added to the lists, so no need to do
			 * this.
			 */
			if (resultRelInfo->ri_NumSlots == 0 && !flushed)
			{
				Assert(!list_member_ptr(estate->es_insert_pending_result_relations,
										resultRelInfo));
				estate->es_insert_pending_result_relations =
					lappend(estate->es_insert_pending_result_relations,
							resultRelInfo);
				estate->es_insert_pending_modifytables =
					lappend(estate->es_insert_pending_modifytables, mtstate);
			}
			Assert(list_member_ptr(estate->es_insert_pending_result_relations,
								   resultRelInfo));

			resultRelInfo->ri_NumSlots++;

			MemoryContextSwitchTo(oldContext);

			return NULL;
		}

		/*
		 * insert into foreign table: let the FDW do it
		 */
		slot = resultRelInfo->ri_FdwRoutine->ExecForeignInsert(estate,
															   resultRelInfo,
															   slot,
															   planSlot);

		if (slot == NULL)		/* "do nothing" */
			return NULL;

		/*
		 * AFTER ROW Triggers or RETURNING expressions might reference the
		 * tableoid column, so (re-)initialize tts_tableOid before evaluating
		 * them.  (This covers the case where the FDW replaced the slot.)
		 */
		slot->tts_tableOid = RelationGetRelid(resultRelInfo->ri_RelationDesc);
	}
	else
	{
		WCOKind		wco_kind;

		/*
		 * Constraints and GENERATED expressions might reference the tableoid
		 * column, so (re-)initialize tts_tableOid before evaluating them.
		 */
		slot->tts_tableOid = RelationGetRelid(resultRelationDesc);

		/*
		 * Compute stored generated columns
		 */
		if (resultRelationDesc->rd_att->constr &&
			resultRelationDesc->rd_att->constr->has_generated_stored)
			ExecComputeStoredGenerated(resultRelInfo, estate, slot,
									   CMD_INSERT);

		/*
		 * Check any RLS WITH CHECK policies.
		 *
		 * Normally we should check INSERT policies. But if the insert is the
		 * result of a partition key update that moved the tuple to a new
		 * partition, we should instead check UPDATE policies, because we are
		 * executing policies defined on the target table, and not those
		 * defined on the child partitions.
		 *
		 * If we're running MERGE, we refer to the action that we're executing
		 * to know if we're doing an INSERT or UPDATE to a partition table.
		 */
		if (mtstate->operation == CMD_UPDATE)
			wco_kind = WCO_RLS_UPDATE_CHECK;
		else if (mtstate->operation == CMD_MERGE)
			wco_kind = (context->relaction->mas_action->commandType == CMD_UPDATE) ?
				WCO_RLS_UPDATE_CHECK : WCO_RLS_INSERT_CHECK;
		else
			wco_kind = WCO_RLS_INSERT_CHECK;

		/*
		 * ExecWithCheckOptions() will skip any WCOs which are not of the kind
		 * we are looking for at this point.
		 */
		if (resultRelInfo->ri_WithCheckOptions != NIL)
			ExecWithCheckOptions(wco_kind, resultRelInfo, slot, estate);

		/*
		 * Check the constraints of the tuple.
		 */
		if (resultRelationDesc->rd_att->constr)
			ExecConstraints(resultRelInfo, slot, estate, mtstate);

		/*
		 * Also check the tuple against the partition constraint, if there is
		 * one; except that if we got here via tuple-routing, we don't need to
		 * if there's no BR trigger defined on the partition.
		 */
		if (resultRelationDesc->rd_rel->relispartition &&
			(resultRelInfo->ri_RootResultRelInfo == NULL ||
			 (resultRelInfo->ri_TrigDesc &&
			  resultRelInfo->ri_TrigDesc->trig_insert_before_row)))
			ExecPartitionCheck(resultRelInfo, slot, estate, true);

		if (onconflict != ONCONFLICT_NONE && resultRelInfo->ri_NumIndices > 0)
		{
			/* Perform a speculative insertion. */
			uint32		specToken;
			ItemPointerData conflictTid;
			bool		specConflict;
			List	   *arbiterIndexes;

			arbiterIndexes = resultRelInfo->ri_onConflictArbiterIndexes;

			/*
			 * Do a non-conclusive check for conflicts first.
			 *
			 * We're not holding any locks yet, so this doesn't guarantee that
			 * the later insert won't conflict.  But it avoids leaving behind
			 * a lot of canceled speculative insertions, if you run a lot of
			 * INSERT ON CONFLICT statements that do conflict.
			 *
			 * We loop back here if we find a conflict below, either during
			 * the pre-check, or when we re-check after inserting the tuple
			 * speculatively.  Better allow interrupts in case some bug makes
			 * this an infinite loop.
			 */
	vlock:
			CHECK_FOR_INTERRUPTS();
			specConflict = false;
			if (!ExecCheckIndexConstraints(resultRelInfo, slot, estate,
										   &conflictTid, arbiterIndexes))
			{
				/* committed conflict tuple found */
				if (onconflict == ONCONFLICT_UPDATE)
				{
					/*
					 * In case of ON CONFLICT DO UPDATE, execute the UPDATE
					 * part.  Be prepared to retry if the UPDATE fails because
					 * of another concurrent UPDATE/DELETE to the conflict
					 * tuple.
					 */
					TupleTableSlot *returning = NULL;

					if (ExecOnConflictUpdate(context, resultRelInfo,
											 &conflictTid, slot, canSetTag,
											 &returning))
					{
						InstrCountTuples2(&mtstate->ps, 1);
						result = returning;
						goto conflict_resolved;
					}
					else
						goto vlock;
				}
				else
				{
					/*
					 * In case of ON CONFLICT DO NOTHING, do nothing. However,
					 * verify that the tuple is visible to the executor's MVCC
					 * snapshot at higher isolation levels.
					 *
					 * Using ExecGetReturningSlot() to store the tuple for the
					 * recheck isn't that pretty, but we can't trivially use
					 * the input slot, because it might not be of a compatible
					 * type. As there's no conflicting usage of
					 * ExecGetReturningSlot() in the DO NOTHING case...
					 */
					Assert(onconflict == ONCONFLICT_NOTHING);
					if (IsYBRelation(resultRelationDesc)) {
						// YugaByte does not use Postgres transaction control code.
						InstrCountTuples2(&mtstate->ps, 1);
						result = NULL;
						goto conflict_resolved;
					}
					ExecCheckTIDVisible(estate, resultRelInfo, &conflictTid,
										ExecGetReturningSlot(estate, resultRelInfo));
					InstrCountTuples2(&mtstate->ps, 1);
					return NULL;
				}
			}

			if (IsYBRelation(resultRelationDesc))
			{
				/*
				 * YB_TODO(neil@yugabyte) Write Yugabyte API to work with slot.
				 *
				 * Current Yugabyte API works with HeapTuple instead of slot.
				 * - Create tuple as a workaround to compile.
				 * - Pass slot to Yugabyte call once the API is fixed.
				 */
				bool shouldFree = true;
				HeapTuple tuple = ExecFetchSlotHeapTuple(slot, true, &shouldFree);

				/*
				 * YugaByte handles transaction-control internally, so speculative token are not being
				 * locked and released in this call.
				 * TODO(Mikhail) Verify the YugaByte transaction support works properly for on-conflict.
				 */
				YBCHeapInsert(resultRelInfo, slot, tuple, estate);

				/* insert index entries for tuple */
				recheckIndexes = ExecInsertIndexTuples(resultRelInfo, slot, estate, true, true,
													   &specConflict, arbiterIndexes,
													   NIL /* no_update_index_list */);
			}
			else
			{
				/*
				 * Before we start insertion proper, acquire our "speculative
				 * insertion lock".  Others can use that to wait for us to decide
				 * if we're going to go ahead with the insertion, instead of
				 * waiting for the whole transaction to complete.
				 */
				specToken = SpeculativeInsertionLockAcquire(GetCurrentTransactionId());

				/* insert the tuple, with the speculative token */
				table_tuple_insert_speculative(resultRelationDesc, slot,
											   estate->es_output_cid,
											   0,
											   NULL,
											   specToken);

				/* insert index entries for tuple */
				recheckIndexes = ExecInsertIndexTuples(resultRelInfo,
													   slot, estate, false, true,
													   &specConflict,
													   arbiterIndexes,
													   NIL /* no_update_index_list */);

				/* adjust the tuple's state accordingly */
				table_tuple_complete_speculative(resultRelationDesc, slot,
												 specToken, !specConflict);

				/*
				 * Wake up anyone waiting for our decision.  They will re-check
				 * the tuple, see that it's no longer speculative, and wait on our
				 * XID as if this was a regularly inserted tuple all along.  Or if
				 * we killed the tuple, they will see it's dead, and proceed as if
				 * the tuple never existed.
				 */
				SpeculativeInsertionLockRelease(GetCurrentTransactionId());

				/*
				 * If there was a conflict, start from the beginning.  We'll do
				 * the pre-check again, which will now find the conflicting tuple
				 * (unless it aborts before we get there).
				 */
				if (specConflict)
				{
					list_free(recheckIndexes);
					goto vlock;
				}
			}

			/* Since there was no more insertion conflict, we're done with ON CONFLICT DO UPDATE */
		}
		else
		{
			/* insert the tuple normally */
			if (IsYBRelation(resultRelationDesc))
			{
				/*
				 * YB_TODO(neil@yugabyte) Write Yugabyte API to work with slot.
				 *
				 * Current Yugabyte API works with HeapTuple instead of slot.
				 * - Create tuple as a workaround to compile.
				 * - Pass slot to Yugabyte call once the API is fixed.
				 */
				MemoryContext oldContext = MemoryContextSwitchTo(GetPerTupleMemoryContext(estate));
				bool shouldFree = true;
				HeapTuple tuple = ExecFetchSlotHeapTuple(slot, true, &shouldFree);

				YBCHeapInsert(resultRelInfo, slot, tuple, estate);

				/* insert index entries for tuple */
				if (YBCRelInfoHasSecondaryIndices(resultRelInfo))
					recheckIndexes = ExecInsertIndexTuples(resultRelInfo, slot, estate, false, true,
														   NULL, NIL,
														   NIL /* no_update_index_list */);
				MemoryContextSwitchTo(oldContext);
			}
			else
			{
				table_tuple_insert(resultRelationDesc, slot,
								   estate->es_output_cid,
								   0, NULL);

				/* insert index entries for tuple */
				if (resultRelInfo->ri_NumIndices > 0)
					recheckIndexes = ExecInsertIndexTuples(resultRelInfo,
														   slot, estate, false,
														   false, NULL, NIL,
														   NIL /* no_update_index_list */);
			}
		}
	}

#ifdef YB_TODO
	/* YB_TODO(neil) Postgres no longer uses tuple. Pg15 uses slot.
	 * Either convert slot to tuple or change yb API to support slot.
	 */
	YbPostProcessDml(CMD_INSERT,
					 resultRelationDesc,
					 slot->tts_tupleDescriptor,
					 tuple);
#endif

	if (canSetTag)
		(estate->es_processed)++;

	/*
	 * If this insert is the result of a partition key update that moved the
	 * tuple to a new partition, put this row into the transition NEW TABLE,
	 * if there is one. We need to do this separately for DELETE and INSERT
	 * because they happen on different tables.
	 */
	ar_insert_trig_tcs = mtstate->mt_transition_capture;
	if (mtstate->operation == CMD_UPDATE && mtstate->mt_transition_capture
		&& mtstate->mt_transition_capture->tcs_update_new_table)
	{
		ExecARUpdateTriggers(estate, resultRelInfo,
							 NULL, NULL,
							 NULL,
							 NULL,
							 slot,
							 NULL,
							 mtstate->mt_transition_capture,
							 false);

		/*
		 * We've already captured the NEW TABLE row, so make sure any AR
		 * INSERT trigger fired below doesn't capture it again.
		 */
		ar_insert_trig_tcs = NULL;
	}

	/* AFTER ROW INSERT Triggers */
	ExecARInsertTriggers(estate, resultRelInfo, slot, recheckIndexes,
						 ar_insert_trig_tcs);

	list_free(recheckIndexes);

	/*
	 * Check any WITH CHECK OPTION constraints from parent views.  We are
	 * required to do this after testing all constraints and uniqueness
	 * violations per the SQL spec, so we do it after actually inserting the
	 * record into the heap and all indexes.
	 *
	 * ExecWithCheckOptions will elog(ERROR) if a violation is found, so the
	 * tuple will never be seen, if it violates the WITH CHECK OPTION.
	 *
	 * ExecWithCheckOptions() will skip any WCOs which are not of the kind we
	 * are looking for at this point.
	 */
	if (resultRelInfo->ri_WithCheckOptions != NIL)
		ExecWithCheckOptions(WCO_VIEW_CHECK, resultRelInfo, slot, estate);

	/* Process RETURNING if present */
	if (resultRelInfo->ri_projectReturning)
		result = ExecProcessReturning(resultRelInfo, slot, planSlot);

	if (inserted_tuple)
		*inserted_tuple = slot;
	if (insert_destrel)
		*insert_destrel = resultRelInfo;

conflict_resolved:
	if (estate->yb_conflict_slot != NULL) {
		ExecDropSingleTupleTableSlot(estate->yb_conflict_slot);
		estate->yb_conflict_slot = NULL;
	}
	return result;
}

/* ----------------------------------------------------------------
 *		ExecBatchInsert
 *
 *		Insert multiple tuples in an efficient way.
 *		Currently, this handles inserting into a foreign table without
 *		RETURNING clause.
 * ----------------------------------------------------------------
 */
static void
ExecBatchInsert(ModifyTableState *mtstate,
				ResultRelInfo *resultRelInfo,
				TupleTableSlot **slots,
				TupleTableSlot **planSlots,
				int numSlots,
				EState *estate,
				bool canSetTag)
{
	int			i;
	int			numInserted = numSlots;
	TupleTableSlot *slot = NULL;
	TupleTableSlot **rslots;

	/*
	 * insert into foreign table: let the FDW do it
	 */
	rslots = resultRelInfo->ri_FdwRoutine->ExecForeignBatchInsert(estate,
																  resultRelInfo,
																  slots,
																  planSlots,
																  &numInserted);

	for (i = 0; i < numInserted; i++)
	{
		slot = rslots[i];

		/*
		 * AFTER ROW Triggers might reference the tableoid column, so
		 * (re-)initialize tts_tableOid before evaluating them.
		 */
		slot->tts_tableOid = RelationGetRelid(resultRelInfo->ri_RelationDesc);

		/* AFTER ROW INSERT Triggers */
		ExecARInsertTriggers(estate, resultRelInfo, slot, NIL,
							 mtstate->mt_transition_capture);

		/*
		 * Check any WITH CHECK OPTION constraints from parent views.  See the
		 * comment in ExecInsert.
		 */
		if (resultRelInfo->ri_WithCheckOptions != NIL)
			ExecWithCheckOptions(WCO_VIEW_CHECK, resultRelInfo, slot, estate);
	}

	if (canSetTag && numInserted > 0)
		estate->es_processed += numInserted;
}

/*
 * ExecPendingInserts -- flushes all pending inserts to the foreign tables
 */
static void
ExecPendingInserts(EState *estate)
{
	ListCell   *l1,
			   *l2;

	forboth(l1, estate->es_insert_pending_result_relations,
			l2, estate->es_insert_pending_modifytables)
	{
		ResultRelInfo *resultRelInfo = (ResultRelInfo *) lfirst(l1);
		ModifyTableState *mtstate = (ModifyTableState *) lfirst(l2);

		Assert(mtstate);
		ExecBatchInsert(mtstate, resultRelInfo,
						resultRelInfo->ri_Slots,
						resultRelInfo->ri_PlanSlots,
						resultRelInfo->ri_NumSlots,
						estate, mtstate->canSetTag);
		resultRelInfo->ri_NumSlots = 0;
	}

	list_free(estate->es_insert_pending_result_relations);
	list_free(estate->es_insert_pending_modifytables);
	estate->es_insert_pending_result_relations = NIL;
	estate->es_insert_pending_modifytables = NIL;
}

/*
 * ExecDeletePrologue -- subroutine for ExecDelete
 *
 * Prepare executor state for DELETE.  Actually, the only thing we have to do
 * here is execute BEFORE ROW triggers.  We return false if one of them makes
 * the delete a no-op; otherwise, return true.
 */
static bool
ExecDeletePrologue(ModifyTableContext *context, ResultRelInfo *resultRelInfo,
				   ItemPointer tupleid, HeapTuple oldtuple,
				   TupleTableSlot **epqreturnslot)
{
	/* BEFORE ROW DELETE triggers */
	if (resultRelInfo->ri_TrigDesc &&
		resultRelInfo->ri_TrigDesc->trig_delete_before_row)
	{
		/* Flush any pending inserts, so rows are visible to the triggers */
		if (context->estate->es_insert_pending_result_relations != NIL)
			ExecPendingInserts(context->estate);

		return ExecBRDeleteTriggers(context->estate, context->epqstate,
									resultRelInfo, tupleid, oldtuple,
									epqreturnslot);
	}

	return true;
}

/*
 * ExecDeleteAct -- subroutine for ExecDelete
 *
 * Actually delete the tuple from a plain table.
 *
 * Caller is in charge of doing EvalPlanQual as necessary
 */
static TM_Result
ExecDeleteAct(ModifyTableContext *context, ResultRelInfo *resultRelInfo,
			  ItemPointer tupleid, bool changingPart)
{
	EState	   *estate = context->estate;

	return table_tuple_delete(resultRelInfo->ri_RelationDesc, tupleid,
							  estate->es_output_cid,
							  estate->es_snapshot,
							  estate->es_crosscheck_snapshot,
							  true /* wait for commit */ ,
							  &context->tmfd,
							  changingPart);
}

/*
 * ExecDeleteEpilogue -- subroutine for ExecDelete
 *
 * Closing steps of tuple deletion; this invokes AFTER FOR EACH ROW triggers,
 * including the UPDATE triggers if the deletion is being done as part of a
 * cross-partition tuple move.
 */
static void
ExecDeleteEpilogue(ModifyTableContext *context, ResultRelInfo *resultRelInfo,
				   ItemPointer tupleid, HeapTuple oldtuple, bool changingPart)
{
	ModifyTableState *mtstate = context->mtstate;
	EState	   *estate = context->estate;
	TransitionCaptureState *ar_delete_trig_tcs;

	/*
	 * If this delete is the result of a partition key update that moved the
	 * tuple to a new partition, put this row into the transition OLD TABLE,
	 * if there is one. We need to do this separately for DELETE and INSERT
	 * because they happen on different tables.
	 */
	ar_delete_trig_tcs = mtstate->mt_transition_capture;
	if (mtstate->operation == CMD_UPDATE && mtstate->mt_transition_capture &&
		mtstate->mt_transition_capture->tcs_update_old_table)
	{
		ExecARUpdateTriggers(estate, resultRelInfo,
							 NULL, NULL,
							 tupleid, oldtuple,
							 NULL, NULL, mtstate->mt_transition_capture,
							 false);

		/*
		 * We've already captured the OLD TABLE row, so make sure any AR
		 * DELETE trigger fired below doesn't capture it again.
		 */
		ar_delete_trig_tcs = NULL;
	}

	/* AFTER ROW DELETE Triggers */
	ExecARDeleteTriggers(estate, resultRelInfo, tupleid, oldtuple,
						 ar_delete_trig_tcs, changingPart);
}

/* ----------------------------------------------------------------
 *		ExecDelete
 *
 *		DELETE is like UPDATE, except that we delete the tuple and no
 *		index modifications are needed.
 *
 *		When deleting from a table, tupleid identifies the tuple to
 *		delete and oldtuple is NULL.  When deleting from a view,
 *		oldtuple is passed to the INSTEAD OF triggers and identifies
 *		what to delete, and tupleid is invalid.  When deleting from a
 *		foreign table, tupleid is invalid; the FDW has to figure out
 *		which row to delete using data from the planSlot.  oldtuple is
 *		passed to foreign table triggers; it is NULL when the foreign
 *		table has no relevant triggers.  We use tupleDeleted to indicate
 *		whether the tuple is actually deleted, callers can use it to
 *		decide whether to continue the operation.  When this DELETE is a
 *		part of an UPDATE of partition-key, then the slot returned by
 *		EvalPlanQual() is passed back using output parameter epqreturnslot.
 *
 *		Returns RETURNING result if any, otherwise NULL.
 * ----------------------------------------------------------------
 */
static TupleTableSlot *
ExecDelete(ModifyTableContext *context,
		   ResultRelInfo *resultRelInfo,
		   ItemPointer tupleid,
		   HeapTuple oldtuple,
		   bool processReturning,
		   bool changingPart,
		   bool canSetTag,
		   bool *tupleDeleted,
		   TupleTableSlot **epqreturnslot)
{
	EState	   *estate = context->estate;
	Relation	resultRelationDesc = resultRelInfo->ri_RelationDesc;
	TupleTableSlot *slot = NULL;
	TM_Result	result;

	if (tupleDeleted)
		*tupleDeleted = false;

	/*
	 * Prepare for the delete.  This includes BEFORE ROW triggers, so we're
	 * done if it says we are.
	 */
	if (!ExecDeletePrologue(context, resultRelInfo, tupleid, oldtuple,
							epqreturnslot))
		return NULL;

	/* INSTEAD OF ROW DELETE Triggers */
	if (resultRelInfo->ri_TrigDesc &&
		resultRelInfo->ri_TrigDesc->trig_delete_instead_row)
	{
		bool		dodelete;

		Assert(oldtuple != NULL);
		dodelete = ExecIRDeleteTriggers(estate, resultRelInfo, oldtuple);

		if (!dodelete)			/* "do nothing" */
			return NULL;
	}
	else if (resultRelInfo->ri_FdwRoutine)
	{
		/*
		 * delete from foreign table: let the FDW do it
		 *
		 * We offer the returning slot as a place to store RETURNING data,
		 * although the FDW can return some other slot if it wants.
		 */
		slot = ExecGetReturningSlot(estate, resultRelInfo);
		slot = resultRelInfo->ri_FdwRoutine->ExecForeignDelete(estate,
															   resultRelInfo,
															   slot,
															   context->planSlot);

		if (slot == NULL)		/* "do nothing" */
			return NULL;

		/*
		 * RETURNING expressions might reference the tableoid column, so
		 * (re)initialize tts_tableOid before evaluating them.
		 */
		if (TTS_EMPTY(slot))
			ExecStoreAllNullTuple(slot);

		slot->tts_tableOid = RelationGetRelid(resultRelationDesc);
	}
	else if (IsYBRelation(resultRelationDesc))
	{
#if YB_TODO
		/* YB_TODO(neil) Check this code. It's from
		   commit efccad40c368e0148bd08bf8a6b77608fdfebcda
		   Author: Alex Abdugafarov <frozenspider@users.noreply.github.com>
		   Date:   Thu Apr 28 23:06:03 2022 +0500
		 */
		bool row_found = YBCExecuteDelete(resultRelationDesc, context->planSlot, estate,
										  context->mtstate, changingPart);
#endif
		bool row_found = YBCExecuteDelete(resultRelationDesc,
										  context->planSlot,
										  ((ModifyTable *)context->mtstate->ps.plan)->ybReturningColumns,
										  context->mtstate->yb_fetch_target_tuple,
										  estate->yb_es_is_single_row_modify_txn,
										  changingPart,
										  estate);

		if (!row_found)
		{
			/*
			 * No row was found. This is possible if it's a single row txn
			 * and there is no row to delete (since we do not first do a scan).
			 */
			return NULL;
		}

		if (YBCRelInfoHasSecondaryIndices(resultRelInfo))
		{
			Datum	ybctid = YBCGetYBTupleIdFromSlot(context->planSlot);

			/* Delete index entries of the old tuple */
			ExecDeleteIndexTuples(resultRelInfo, ybctid, oldtuple, estate);
		}
	}
	else
	{
		/*
		 * delete the tuple
		 *
		 * Note: if context->estate->es_crosscheck_snapshot isn't
		 * InvalidSnapshot, we check that the row to be deleted is visible to
		 * that snapshot, and throw a can't-serialize error if not. This is a
		 * special-case behavior needed for referential integrity updates in
		 * transaction-snapshot mode transactions.
		 */
ldelete:;
		result = ExecDeleteAct(context, resultRelInfo, tupleid, changingPart);

		switch (result)
		{
			case TM_SelfModified:

				/*
				 * The target tuple was already updated or deleted by the
				 * current command, or by a later command in the current
				 * transaction.  The former case is possible in a join DELETE
				 * where multiple tuples join to the same target tuple. This
				 * is somewhat questionable, but Postgres has always allowed
				 * it: we just ignore additional deletion attempts.
				 *
				 * The latter case arises if the tuple is modified by a
				 * command in a BEFORE trigger, or perhaps by a command in a
				 * volatile function used in the query.  In such situations we
				 * should not ignore the deletion, but it is equally unsafe to
				 * proceed.  We don't want to discard the original DELETE
				 * while keeping the triggered actions based on its deletion;
				 * and it would be no better to allow the original DELETE
				 * while discarding updates that it triggered.  The row update
				 * carries some information that might be important according
				 * to business rules; so throwing an error is the only safe
				 * course.
				 *
				 * If a trigger actually intends this type of interaction, it
				 * can re-execute the DELETE and then return NULL to cancel
				 * the outer delete.
				 */
				if (context->tmfd.cmax != estate->es_output_cid)
					ereport(ERROR,
							(errcode(ERRCODE_TRIGGERED_DATA_CHANGE_VIOLATION),
							 errmsg("tuple to be deleted was already modified by an operation triggered by the current command"),
							 errhint("Consider using an AFTER trigger instead of a BEFORE trigger to propagate changes to other rows.")));

				/* Else, already deleted by self; nothing to do */
				return NULL;

			case TM_Ok:
				break;

			case TM_Updated:
				{
					TupleTableSlot *inputslot;
					TupleTableSlot *epqslot;

					if (IsolationUsesXactSnapshot())
						ereport(ERROR,
								(errcode(ERRCODE_T_R_SERIALIZATION_FAILURE),
								 errmsg("could not serialize access due to concurrent update")));

					/*
					 * Already know that we're going to need to do EPQ, so
					 * fetch tuple directly into the right slot.
					 */
					EvalPlanQualBegin(context->epqstate);
					inputslot = EvalPlanQualSlot(context->epqstate, resultRelationDesc,
												 resultRelInfo->ri_RangeTableIndex);

					result = table_tuple_lock(resultRelationDesc, tupleid,
											  estate->es_snapshot,
											  inputslot, estate->es_output_cid,
											  LockTupleExclusive, LockWaitBlock,
											  TUPLE_LOCK_FLAG_FIND_LAST_VERSION,
											  &context->tmfd);

					switch (result)
					{
						case TM_Ok:
							Assert(context->tmfd.traversed);
							epqslot = EvalPlanQual(context->epqstate,
												   resultRelationDesc,
												   resultRelInfo->ri_RangeTableIndex,
												   inputslot);
							if (TupIsNull(epqslot))
								/* Tuple not passing quals anymore, exiting... */
								return NULL;

							/*
							 * If requested, skip delete and pass back the
							 * updated row.
							 */
							if (epqreturnslot)
							{
								*epqreturnslot = epqslot;
								return NULL;
							}
							else
								goto ldelete;

						case TM_SelfModified:

							/*
							 * This can be reached when following an update
							 * chain from a tuple updated by another session,
							 * reaching a tuple that was already updated in
							 * this transaction. If previously updated by this
							 * command, ignore the delete, otherwise error
							 * out.
							 *
							 * See also TM_SelfModified response to
							 * table_tuple_delete() above.
							 */
							if (context->tmfd.cmax != estate->es_output_cid)
								ereport(ERROR,
										(errcode(ERRCODE_TRIGGERED_DATA_CHANGE_VIOLATION),
										 errmsg("tuple to be deleted was already modified by an operation triggered by the current command"),
										 errhint("Consider using an AFTER trigger instead of a BEFORE trigger to propagate changes to other rows.")));
							return NULL;

						case TM_Deleted:
							/* tuple already deleted; nothing to do */
							return NULL;

						default:

							/*
							 * TM_Invisible should be impossible because we're
							 * waiting for updated row versions, and would
							 * already have errored out if the first version
							 * is invisible.
							 *
							 * TM_Updated should be impossible, because we're
							 * locking the latest version via
							 * TUPLE_LOCK_FLAG_FIND_LAST_VERSION.
							 */
							elog(ERROR, "unexpected table_tuple_lock status: %u",
								 result);
							return NULL;
					}

					Assert(false);
					break;
				}

			case TM_Deleted:
				if (IsolationUsesXactSnapshot())
					ereport(ERROR,
							(errcode(ERRCODE_T_R_SERIALIZATION_FAILURE),
							 errmsg("could not serialize access due to concurrent delete")));
				/* tuple already deleted; nothing to do */
				return NULL;

			default:
				elog(ERROR, "unrecognized table_tuple_delete status: %u",
					 result);
				return NULL;
		}

		/*
		 * Note: Normally one would think that we have to delete index tuples
		 * associated with the heap tuple now...
		 *
		 * ... but in POSTGRES, we have no need to do this because VACUUM will
		 * take care of it later.  We can't delete index tuples immediately
		 * anyway, since the tuple is still visible to other transactions.
		 */
	}

	YbPostProcessDml(CMD_DELETE,
					 resultRelationDesc,
					 NULL /* desc */,
					 NULL /* newtup */);

	if (canSetTag)
		(estate->es_processed)++;

	/* Tell caller that the delete actually happened. */
	if (tupleDeleted)
		*tupleDeleted = true;

	ExecDeleteEpilogue(context, resultRelInfo, tupleid, oldtuple, changingPart);

	/* Process RETURNING if present and if requested */
	if (processReturning && resultRelInfo->ri_projectReturning)
	{
		/*
		 * We have to put the target tuple into a slot, which means first we
		 * gotta fetch it.  We can use the trigger tuple slot.
		 */
		TupleTableSlot *rslot;

		if (resultRelInfo->ri_FdwRoutine)
		{
			/* FDW must have provided a slot containing the deleted row */
			Assert(!TupIsNull(slot));
		}
		else if (IsYBRelation(resultRelationDesc))
		{
#ifdef YB_TODO
			/* YB_TODO(neil) Need to trace Alex's commit efccad40c368e0148bd08bf8a6b77608fdfebcda */
			if (mtstate->yb_fetch_target_tuple)
			{
				slot = ExecFilterJunk(resultRelInfo->ri_junkFilter, planSlot);
			}
			/* Previous code */
			if (context->mtstate->yb_mt_is_single_row_update_or_delete)
			{
				slot = context->planSlot;
			}
#endif

#ifdef YB_TODO
			/* TODO(neil@yugabyte)
			 * - Check to make sure that ExecFilterJunk is no longer needed here.
			 * - Find where Yugabyte should plugin new code.
			 */
			else
			{
				slot = ExecFilterJunk(resultRelInfo->ri_junkFilter, context->planSlot);
			}
#endif
		}
		else
		{
			slot = ExecGetReturningSlot(estate, resultRelInfo);
			if (oldtuple != NULL)
			{
				ExecForceStoreHeapTuple(oldtuple, slot, false);
			}
			else
			{
				if (!table_tuple_fetch_row_version(resultRelationDesc, tupleid,
												   SnapshotAny, slot))
					elog(ERROR, "failed to fetch deleted tuple for DELETE RETURNING");
			}
		}

		rslot = ExecProcessReturning(resultRelInfo, slot, context->planSlot);

		/*
		 * Before releasing the target tuple again, make sure rslot has a
		 * local copy of any pass-by-reference values.
		 */
		ExecMaterializeSlot(rslot);

		ExecClearTuple(slot);

		return rslot;
	}

	return NULL;
}

/* ----------------------------------------------------------------
 * YBEqualDatums
 *
 * Function compares values of lhs and rhs datums with respect to value type and collation.
 *
 * Returns true in case value of lhs and rhs datums match.
 * ----------------------------------------------------------------
 */
static bool
YBEqualDatums(Datum lhs, Datum rhs, Oid atttypid, Oid collation)
{
	LOCAL_FCINFO(locfcinfo, 2);
	TypeCacheEntry *typentry = lookup_type_cache(atttypid, TYPECACHE_CMP_PROC_FINFO);
	if (!OidIsValid(typentry->cmp_proc_finfo.fn_oid))
		ereport(ERROR,
		        (errcode(ERRCODE_UNDEFINED_FUNCTION),
		         errmsg("could not identify a comparison function for type %s",
		                format_type_be(typentry->type_id))));

	/* YB_TODOneil) Need to verify if this code works. FuncCallInfo now uses Pg15 structure */
	InitFunctionCallInfoData(*locfcinfo, &typentry->cmp_proc_finfo, 2, collation, NULL, NULL);
	locfcinfo->args[0].value = lhs;
	locfcinfo->args[0].isnull = false;
	locfcinfo->args[1].value = rhs;
	locfcinfo->args[1].isnull = false;
	locfcinfo->isnull = false;
	return DatumGetInt32(FunctionCallInvoke(locfcinfo)) == 0;
}

/* ----------------------------------------------------------------
 * YBBuildExtraUpdatedCols
 *
 * Function compares attribute value in oldtuple and newtuple for attributes which are not in the
 * updatedCols set. Returns set of changed attributes or NULL.
 * ----------------------------------------------------------------
 */
static Bitmapset*
YBBuildExtraUpdatedCols(Relation rel,
                        HeapTuple oldtuple,
                        HeapTuple newtuple,
                        Bitmapset *updatedCols)
{
	if (bms_is_member(InvalidAttrNumber, updatedCols))
		/* No extra work required in case the whore row is changed */
		return NULL;

	Bitmapset *result = NULL;
	AttrNumber firstLowInvalidAttributeNumber = YBGetFirstLowInvalidAttributeNumber(rel);
	TupleDesc tupleDesc = RelationGetDescr(rel);
	for (int idx = 0; idx < tupleDesc->natts; ++idx)
	{
		FormData_pg_attribute *att_desc = TupleDescAttr(tupleDesc, idx);

		AttrNumber attnum = att_desc->attnum;

		/* Skip virtual (system) and dropped columns */
		if (!IsRealYBColumn(rel, attnum))
			continue;

		int bms_idx = attnum - firstLowInvalidAttributeNumber;
		if (bms_is_member(bms_idx, updatedCols))
			continue;

		bool old_is_null = false;
		bool new_is_null = false;
		Datum old_value = heap_getattr(oldtuple, attnum, tupleDesc, &old_is_null);
		Datum new_value = heap_getattr(newtuple, attnum, tupleDesc, &new_is_null);
		if (old_is_null != new_is_null ||
		    (!new_is_null && !YBEqualDatums(old_value,
		                                    new_value,
		                                    att_desc->atttypid,
		                                    att_desc->attcollation)))
		{
			result = bms_add_member(result, bms_idx);
		}
	}
	return result;
}


/*
 * ExecCrossPartitionUpdate --- Move an updated tuple to another partition.
 *
 * This works by first deleting the old tuple from the current partition,
 * followed by inserting the new tuple into the root parent table, that is,
 * mtstate->rootResultRelInfo.  It will be re-routed from there to the
 * correct partition.
 *
 * Returns true if the tuple has been successfully moved, or if it's found
 * that the tuple was concurrently deleted so there's nothing more to do
 * for the caller.
 *
 * False is returned if the tuple we're trying to move is found to have been
 * concurrently updated.  In that case, the caller must check if the updated
 * tuple (in updateCxt->cpUpdateRetrySlot) still needs to be re-routed, and
 * call this function again or perform a regular update accordingly.
 */
static bool
ExecCrossPartitionUpdate(ModifyTableContext *context,
						 ResultRelInfo *resultRelInfo,
						 ItemPointer tupleid, HeapTuple oldtuple,
						 TupleTableSlot *slot,
						 bool canSetTag,
						 UpdateContext *updateCxt,
						 TupleTableSlot **inserted_tuple,
						 ResultRelInfo **insert_destrel)
{
	ModifyTableState *mtstate = context->mtstate;
	EState	   *estate = mtstate->ps.state;
	TupleConversionMap *tupconv_map;
	bool		tuple_deleted;
	TupleTableSlot *epqslot = NULL;
	bool    prev_yb_is_single_row_modify_txn = estate->yb_es_is_single_row_modify_txn;

	context->cpUpdateReturningSlot = NULL;
	context->cpUpdateRetrySlot = NULL;

	/*
	 * Disallow an INSERT ON CONFLICT DO UPDATE that causes the original row
	 * to migrate to a different partition.  Maybe this can be implemented
	 * some day, but it seems a fringe feature with little redeeming value.
	 */
	if (((ModifyTable *) mtstate->ps.plan)->onConflictAction == ONCONFLICT_UPDATE)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("invalid ON UPDATE specification"),
				 errdetail("The result tuple would appear in a different partition than the original tuple.")));

	/*
	 * When an UPDATE is run directly on a leaf partition, simply fail with a
	 * partition constraint violation error.
	 */
	if (resultRelInfo == mtstate->rootResultRelInfo)
		ExecPartitionCheckEmitError(resultRelInfo, slot, estate);

	/* Initialize tuple routing info if not already done. */
	if (mtstate->mt_partition_tuple_routing == NULL)
	{
		Relation	rootRel = mtstate->rootResultRelInfo->ri_RelationDesc;
		MemoryContext oldcxt;

		/* Things built here have to last for the query duration. */
		oldcxt = MemoryContextSwitchTo(estate->es_query_cxt);

		mtstate->mt_partition_tuple_routing =
			ExecSetupPartitionTupleRouting(estate, rootRel);

		/*
		 * Before a partition's tuple can be re-routed, it must first be
		 * converted to the root's format, so we'll need a slot for storing
		 * such tuples.
		 */
		Assert(mtstate->mt_root_tuple_slot == NULL);
		mtstate->mt_root_tuple_slot = table_slot_create(rootRel, NULL);

		MemoryContextSwitchTo(oldcxt);
	}

	/*
	 * Row movement, part 1.  Delete the tuple, but skip RETURNING processing.
	 * We want to return rows from INSERT.
	 */
	ExecDelete(context, resultRelInfo,
			   tupleid, oldtuple,
			   false,			/* processReturning */
			   true,			/* changingPart */
			   false,			/* canSetTag */
			   &tuple_deleted, &epqslot);

	/*
	 * For some reason if DELETE didn't happen (e.g. trigger prevented it, or
	 * it was already deleted by self, or it was concurrently deleted by
	 * another transaction), then we should skip the insert as well;
	 * otherwise, an UPDATE could cause an increase in the total number of
	 * rows across all partitions, which is clearly wrong.
	 *
	 * For a normal UPDATE, the case where the tuple has been the subject of a
	 * concurrent UPDATE or DELETE would be handled by the EvalPlanQual
	 * machinery, but for an UPDATE that we've translated into a DELETE from
	 * this partition and an INSERT into some other partition, that's not
	 * available, because CTID chains can't span relation boundaries.  We
	 * mimic the semantics to a limited extent by skipping the INSERT if the
	 * DELETE fails to find a tuple.  This ensures that two concurrent
	 * attempts to UPDATE the same tuple at the same time can't turn one tuple
	 * into two, and that an UPDATE of a just-deleted tuple can't resurrect
	 * it.
	 */
	if (!tuple_deleted)
	{
		/*
		 * epqslot will be typically NULL.  But when ExecDelete() finds that
		 * another transaction has concurrently updated the same row, it
		 * re-fetches the row, skips the delete, and epqslot is set to the
		 * re-fetched tuple slot.  In that case, we need to do all the checks
		 * again.
		 */
		if (TupIsNull(epqslot))
			return true;
		else
		{
			/* Fetch the most recent version of old tuple. */
			TupleTableSlot *oldSlot;

			/* ... but first, make sure ri_oldTupleSlot is initialized. */
			if (unlikely(!resultRelInfo->ri_projectNewInfoValid))
				ExecInitUpdateProjection(mtstate, resultRelInfo);
			oldSlot = resultRelInfo->ri_oldTupleSlot;
			if (!table_tuple_fetch_row_version(resultRelInfo->ri_RelationDesc,
											   tupleid,
											   SnapshotAny,
											   oldSlot))
				elog(ERROR, "failed to fetch tuple being updated");
			/* and project the new tuple to retry the UPDATE with */
			context->cpUpdateRetrySlot =
				context->GetUpdateNewTuple(resultRelInfo, epqslot, oldSlot,
										   context->relaction);
			return false;
		}
	}

	/*
	 * resultRelInfo is one of the per-relation resultRelInfos.  So we should
	 * convert the tuple into root's tuple descriptor if needed, since
	 * ExecInsert() starts the search from root.
	 */
	tupconv_map = ExecGetChildToRootMap(resultRelInfo);
	if (tupconv_map != NULL)
		slot = execute_attr_map_slot(tupconv_map->attrMap,
									 slot,
									 mtstate->mt_root_tuple_slot);

	/* Tuple routing starts from the root table. */
	context->cpUpdateReturningSlot =
		ExecInsert(context, mtstate->rootResultRelInfo, slot, canSetTag,
				   inserted_tuple, insert_destrel);

	/* Revert ExecPrepareTupleRouting's node change. */
	estate->yb_es_is_single_row_modify_txn = prev_yb_is_single_row_modify_txn;

	/*
	 * Reset the transition state that may possibly have been written by
	 * INSERT.
	 */
	if (mtstate->mt_transition_capture)
		mtstate->mt_transition_capture->tcs_original_insert_tuple = NULL;

	/* We're done moving. */
	return true;
}

/*
 * ExecUpdatePrologue -- subroutine for ExecUpdate
 *
 * Prepare executor state for UPDATE.  This includes running BEFORE ROW
 * triggers.  We return false if one of them makes the update a no-op;
 * otherwise, return true.
 */
static bool
ExecUpdatePrologue(ModifyTableContext *context, ResultRelInfo *resultRelInfo,
				   ItemPointer tupleid, HeapTuple oldtuple, TupleTableSlot *slot)
{
	Relation	resultRelationDesc = resultRelInfo->ri_RelationDesc;

	ExecMaterializeSlot(slot);

	/*
	 * Open the table's indexes, if we have not done so already, so that we
	 * can add new index entries for the updated tuple.
	 */
	if (resultRelationDesc->rd_rel->relhasindex &&
		resultRelInfo->ri_IndexRelationDescs == NULL)
		ExecOpenIndices(resultRelInfo, false);

	/* BEFORE ROW UPDATE triggers */
	if (resultRelInfo->ri_TrigDesc &&
		resultRelInfo->ri_TrigDesc->trig_update_before_row)
	{
		/* Flush any pending inserts, so rows are visible to the triggers */
		if (context->estate->es_insert_pending_result_relations != NIL)
			ExecPendingInserts(context->estate);

		return ExecBRUpdateTriggers(context->estate, context->epqstate,
									resultRelInfo, tupleid, oldtuple, slot,
									&context->tmfd);
	}

	return true;
}

/*
 * ExecUpdatePrepareSlot -- subroutine for ExecUpdate
 *
 * Apply the final modifications to the tuple slot before the update.
 */
static void
ExecUpdatePrepareSlot(ResultRelInfo *resultRelInfo,
					  TupleTableSlot *slot,
					  EState *estate)
{
	Relation	resultRelationDesc = resultRelInfo->ri_RelationDesc;

	/*
	 * Constraints and GENERATED expressions might reference the tableoid
	 * column, so (re-)initialize tts_tableOid before evaluating them.
	 */
	slot->tts_tableOid = RelationGetRelid(resultRelationDesc);

	/*
	 * Compute stored generated columns
	 */
	if (resultRelationDesc->rd_att->constr &&
		resultRelationDesc->rd_att->constr->has_generated_stored)
		ExecComputeStoredGenerated(resultRelInfo, estate, slot,
								   CMD_UPDATE);
}

/*
 * ExecUpdateAct -- subroutine for ExecUpdate
 *
 * Actually update the tuple, when operating on a plain table.  If the
 * table is a partition, and the command was called referencing an ancestor
 * partitioned table, this routine migrates the resulting tuple to another
 * partition.
 *
 * The caller is in charge of keeping indexes current as necessary.  The
 * caller is also in charge of doing EvalPlanQual if the tuple is found to
 * be concurrently updated.  However, in case of a cross-partition update,
 * this routine does it.
 *
 * Caller is in charge of doing EvalPlanQual as necessary, and of keeping
 * indexes current for the update.
 */
static TM_Result
ExecUpdateAct(ModifyTableContext *context, ResultRelInfo *resultRelInfo,
			  ItemPointer tupleid, HeapTuple oldtuple, TupleTableSlot *slot,
			  bool canSetTag, UpdateContext *updateCxt)
{
	EState	   *estate = context->estate;
	Relation	resultRelationDesc = resultRelInfo->ri_RelationDesc;
	bool		partition_constraint_failed;
	TM_Result	result;

	updateCxt->crossPartUpdate = false;

	/*
	 * If we generate a new candidate tuple after EvalPlanQual testing, we
	 * must loop back here and recheck any RLS policies and constraints. (We
	 * don't need to redo triggers, however.  If there are any BEFORE triggers
	 * then trigger.c will have done table_tuple_lock to lock the correct
	 * tuple, so there's no need to do them again.)
	 */
lreplace:;

	/* ensure slot is independent, consider e.g. EPQ */
	ExecMaterializeSlot(slot);

	/*
	 * If partition constraint fails, this row might get moved to another
	 * partition, in which case we should check the RLS CHECK policy just
	 * before inserting into the new partition, rather than doing it here.
	 * This is because a trigger on that partition might again change the row.
	 * So skip the WCO checks if the partition constraint fails.
	 */
	partition_constraint_failed =
		resultRelationDesc->rd_rel->relispartition &&
		!ExecPartitionCheck(resultRelInfo, slot, estate, false);

	/* Check any RLS UPDATE WITH CHECK policies */
	if (!partition_constraint_failed &&
		resultRelInfo->ri_WithCheckOptions != NIL)
	{
		/*
		 * ExecWithCheckOptions() will skip any WCOs which are not of the kind
		 * we are looking for at this point.
		 */
		ExecWithCheckOptions(WCO_RLS_UPDATE_CHECK,
							 resultRelInfo, slot, estate);
	}

	/*
	 * If a partition check failed, try to move the row into the right
	 * partition.
	 */
	if (partition_constraint_failed)
	{
		TupleTableSlot *inserted_tuple;
		ResultRelInfo *insert_destrel = NULL;

		/*
		 * ExecCrossPartitionUpdate will first DELETE the row from the
		 * partition it's currently in and then insert it back into the root
		 * table, which will re-route it to the correct partition.  However,
		 * if the tuple has been concurrently updated, a retry is needed.
		 */
		if (ExecCrossPartitionUpdate(context, resultRelInfo,
									 tupleid, oldtuple, slot,
									 canSetTag, updateCxt,
									 &inserted_tuple,
									 &insert_destrel))
		{
			/* success! */
			updateCxt->updated = true;
			updateCxt->crossPartUpdate = true;

			/*
			 * If the partitioned table being updated is referenced in foreign
			 * keys, queue up trigger events to check that none of them were
			 * violated.  No special treatment is needed in
			 * non-cross-partition update situations, because the leaf
			 * partition's AR update triggers will take care of that.  During
			 * cross-partition updates implemented as delete on the source
			 * partition followed by insert on the destination partition,
			 * AR-UPDATE triggers of the root table (that is, the table
			 * mentioned in the query) must be fired.
			 *
			 * NULL insert_destrel means that the move failed to occur, that
			 * is, the update failed, so no need to anything in that case.
			 */
			if (insert_destrel &&
				resultRelInfo->ri_TrigDesc &&
				resultRelInfo->ri_TrigDesc->trig_update_after_row)
				ExecCrossPartitionUpdateForeignKey(context,
												   resultRelInfo,
												   insert_destrel,
												   tupleid, slot,
												   inserted_tuple);

			return TM_Ok;
		}

		/*
		 * No luck, a retry is needed.  If running MERGE, we do not do so
		 * here; instead let it handle that on its own rules.
		 */
		if (context->relaction != NULL)
			return TM_Updated;

		/*
		 * ExecCrossPartitionUpdate installed an updated version of the new
		 * tuple in the retry slot; start over.
		 */
		slot = context->cpUpdateRetrySlot;
		goto lreplace;
	}

	/*
	 * Check the constraints of the tuple.  We've already checked the
	 * partition constraint above; however, we must still ensure the tuple
	 * passes all other constraints, so we will call ExecConstraints() and
	 * have it validate all remaining checks.
	 */
	if (resultRelationDesc->rd_att->constr)
		ExecConstraints(resultRelInfo, slot, estate, context->mtstate);

	/*
	 * replace the heap tuple
	 *
	 * Note: if es_crosscheck_snapshot isn't InvalidSnapshot, we check that
	 * the row to be updated is visible to that snapshot, and throw a
	 * can't-serialize error if not. This is a special-case behavior needed
	 * for referential integrity updates in transaction-snapshot mode
	 * transactions.
	 */
	result = table_tuple_update(resultRelationDesc, tupleid, slot,
								estate->es_output_cid,
								estate->es_snapshot,
								estate->es_crosscheck_snapshot,
								true /* wait for commit */ ,
								&context->tmfd, &updateCxt->lockmode,
								&updateCxt->updateIndexes);
	if (result == TM_Ok)
		updateCxt->updated = true;

	return result;
}

/*
 * ExecUpdateEpilogue -- subroutine for ExecUpdate
 *
 * Closing steps of updating a tuple.  Must be called if ExecUpdateAct
 * returns indicating that the tuple was updated.
 */
static void
ExecUpdateEpilogue(ModifyTableContext *context, UpdateContext *updateCxt,
				   ResultRelInfo *resultRelInfo, ItemPointer tupleid,
				   HeapTuple oldtuple, TupleTableSlot *slot,
				   List *recheckIndexes)
{
	ModifyTableState *mtstate = context->mtstate;

	/* insert index entries for tuple if necessary */
	if (resultRelInfo->ri_NumIndices > 0 && updateCxt->updateIndexes)
		recheckIndexes = ExecInsertIndexTuples(resultRelInfo,
											   slot, context->estate,
											   true, false,
											   NULL, NIL,
											   NIL /* no_update_index_list */);

	/* AFTER ROW UPDATE Triggers */
	ExecARUpdateTriggers(context->estate, resultRelInfo,
						 NULL, NULL,
						 tupleid, oldtuple, slot,
						 recheckIndexes,
						 mtstate->operation == CMD_INSERT ?
						 mtstate->mt_oc_transition_capture :
						 mtstate->mt_transition_capture,
						 false);

	/*
	 * Check any WITH CHECK OPTION constraints from parent views.  We are
	 * required to do this after testing all constraints and uniqueness
	 * violations per the SQL spec, so we do it after actually updating the
	 * record in the heap and all indexes.
	 *
	 * ExecWithCheckOptions() will skip any WCOs which are not of the kind we
	 * are looking for at this point.
	 */
	if (resultRelInfo->ri_WithCheckOptions != NIL)
		ExecWithCheckOptions(WCO_VIEW_CHECK, resultRelInfo,
							 slot, context->estate);
}

/*
 * Queues up an update event using the target root partitioned table's
 * trigger to check that a cross-partition update hasn't broken any foreign
 * keys pointing into it.
 */
static void
ExecCrossPartitionUpdateForeignKey(ModifyTableContext *context,
								   ResultRelInfo *sourcePartInfo,
								   ResultRelInfo *destPartInfo,
								   ItemPointer tupleid,
								   TupleTableSlot *oldslot,
								   TupleTableSlot *newslot)
{
	ListCell   *lc;
	ResultRelInfo *rootRelInfo;
	List	   *ancestorRels;

	rootRelInfo = sourcePartInfo->ri_RootResultRelInfo;
	ancestorRels = ExecGetAncestorResultRels(context->estate, sourcePartInfo);

	/*
	 * For any foreign keys that point directly into a non-root ancestors of
	 * the source partition, we can in theory fire an update event to enforce
	 * those constraints using their triggers, if we could tell that both the
	 * source and the destination partitions are under the same ancestor. But
	 * for now, we simply report an error that those cannot be enforced.
	 */
	foreach(lc, ancestorRels)
	{
		ResultRelInfo *rInfo = lfirst(lc);
		TriggerDesc *trigdesc = rInfo->ri_TrigDesc;
		bool		has_noncloned_fkey = false;

		/* Root ancestor's triggers will be processed. */
		if (rInfo == rootRelInfo)
			continue;

		if (trigdesc && trigdesc->trig_update_after_row)
		{
			for (int i = 0; i < trigdesc->numtriggers; i++)
			{
				Trigger    *trig = &trigdesc->triggers[i];

				if (!trig->tgisclone &&
					RI_FKey_trigger_type(trig->tgfoid) == RI_TRIGGER_PK)
				{
					has_noncloned_fkey = true;
					break;
				}
			}
		}

		if (has_noncloned_fkey)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("cannot move tuple across partitions when a non-root ancestor of the source partition is directly referenced in a foreign key"),
					 errdetail("A foreign key points to ancestor \"%s\" but not the root ancestor \"%s\".",
							   RelationGetRelationName(rInfo->ri_RelationDesc),
							   RelationGetRelationName(rootRelInfo->ri_RelationDesc)),
					 errhint("Consider defining the foreign key on table \"%s\".",
							 RelationGetRelationName(rootRelInfo->ri_RelationDesc))));
	}

	/* Perform the root table's triggers. */
	ExecARUpdateTriggers(context->estate,
						 rootRelInfo, sourcePartInfo, destPartInfo,
						 tupleid, NULL, newslot, NIL, NULL, true);
}

/* ----------------------------------------------------------------
 *		ExecUpdate
 *
 *		note: we can't run UPDATE queries with transactions
 *		off because UPDATEs are actually INSERTs and our
 *		scan will mistakenly loop forever, updating the tuple
 *		it just inserted..  This should be fixed but until it
 *		is, we don't want to get stuck in an infinite loop
 *		which corrupts your database..
 *
 *		When updating a table, tupleid identifies the tuple to
 *		update and oldtuple is NULL.  When updating a view, oldtuple
 *		is passed to the INSTEAD OF triggers and identifies what to
 *		update, and tupleid is invalid.  When updating a foreign table,
 *		tupleid is invalid; the FDW has to figure out which row to
 *		update using data from the planSlot.  oldtuple is passed to
 *		foreign table triggers; it is NULL when the foreign table has
 *		no relevant triggers.
 *
 *		slot contains the new tuple value to be stored.
 *		planSlot is the output of the ModifyTable's subplan; we use it
 *		to access values from other input tables (for RETURNING),
 *		row-ID junk columns, etc.
 *
 *		Returns RETURNING result if any, otherwise NULL.
 * ----------------------------------------------------------------
 */
static TupleTableSlot *
ExecUpdate(ModifyTableContext *context, ResultRelInfo *resultRelInfo,
		   ItemPointer tupleid, HeapTuple oldtuple, TupleTableSlot *slot,
		   bool canSetTag)
{
	EState	   *estate = context->estate;
	Relation	resultRelationDesc = resultRelInfo->ri_RelationDesc;
	UpdateContext updateCxt = {0};
	List	   *recheckIndexes = NIL;
	TM_Result	result;

	/* Yugabyte variables
	 * YB_TODO(neil) Reintro local variable if needed. Otherwise, remove them.
	 */
	ModifyTableState *mtstate = context->mtstate;
	/* ModifyTable *node = (ModifyTable *)mtstate->ps.plan; */
	/* OnConflictAction onconflict = node->onConflictAction; */
	bool shouldFree = true;
	HeapTuple tuple = NULL;

	/*
	 * abort the operation if not running transactions
	 */
	if (IsBootstrapProcessingMode())
		elog(ERROR, "cannot UPDATE during bootstrap");

#ifdef YB_TODO
	/* YB_TODO(neil) Postgres has refactor this code block to "ExecUpdatePrologue" */
	if (IsYBRelation(resultRelInfo->ri_RelationDesc))
	{
		/*
		 * For a YugaByte table, we need to update the secondary indices for
		 * all of the INSERT, UPDATE, and DELETE statements. The ON CONFLICT UPDATE
		 * execution also needs to process primary key index.
		 */
		if (YBRelHasSecondaryIndices(resultRelInfo->ri_RelationDesc) ||
			node->onConflictAction != ONCONFLICT_NONE)
			ExecOpenIndices(resultRelInfo, onconflict != ONCONFLICT_NONE);
	}
	bool beforeRowUpdateTriggerFired = false;

	/* BEFORE ROW UPDATE Triggers */
	if (resultRelInfo->ri_TrigDesc &&
		resultRelInfo->ri_TrigDesc->trig_update_before_row)
	{
		beforeRowUpdateTriggerFired = true;
	}
#endif

	/*
	 * Prepare for the update.  This includes BEFORE ROW triggers, so we're
	 * done if it says we are.
	 */
	if (!ExecUpdatePrologue(context, resultRelInfo, tupleid, oldtuple, slot))
		return NULL;

	/* INSTEAD OF ROW UPDATE Triggers */
	if (resultRelInfo->ri_TrigDesc &&
		resultRelInfo->ri_TrigDesc->trig_update_instead_row)
	{
		if (!ExecIRUpdateTriggers(estate, resultRelInfo,
								  oldtuple, slot))
			return NULL;		/* "do nothing" */
	}
	else if (resultRelInfo->ri_FdwRoutine)
	{
		ExecUpdatePrepareSlot(resultRelInfo, slot, estate);

		/*
		 * update in foreign table: let the FDW do it
		 */
		slot = resultRelInfo->ri_FdwRoutine->ExecForeignUpdate(estate,
															   resultRelInfo,
															   slot,
															   context->planSlot);

		if (slot == NULL)		/* "do nothing" */
			return NULL;

		/*
		 * AFTER ROW Triggers or RETURNING expressions might reference the
		 * tableoid column, so (re-)initialize tts_tableOid before evaluating
		 * them.  (This covers the case where the FDW replaced the slot.)
		 */
		slot->tts_tableOid = RelationGetRelid(resultRelationDesc);
	}
	else if (IsYBRelation(resultRelationDesc))
	{
		bool partition_constraint_failed;

		/* YB_TODO(neil) Check this code.
		yb_lreplace:;
		*/
		/*
		 * Check the constraints of the tuple.
		 */
		if (resultRelationDesc->rd_att->constr)
			ExecConstraints(resultRelInfo, slot, estate, mtstate);

		/*
		 * Verify that the update does not violate partition constraints.
		 * YB_TODO(neil@yugabyte) Verify that we should not need to check for (qual == NULL).
		 * Note
		 *   Old code: qual = resultRelInfo->ri_PartitionCheck;
		 * Changed to
		 *   New code: qual = RelationGetPartitionQual(resultRelInfo->ri_RelationDesc);
		 */
		partition_constraint_failed =
			!ExecPartitionCheck(resultRelInfo, slot, estate, false /* emitError */);

		if (!partition_constraint_failed && resultRelInfo->ri_WithCheckOptions != NIL)
		{
			/*
			 * ExecWithCheckOptions() will skip any WCOs which are not of the
			 * kind we are looking for at this point.
			 */
			ExecWithCheckOptions(WCO_RLS_UPDATE_CHECK, resultRelInfo, slot, estate);
		}


		/*
		 * If a partition check failed, try to move the row into the right
		 * partition.
		 */
		if (partition_constraint_failed)
		{
#ifdef YB_TODO
			/* YB_TODO(neil)
			 * ExecCrossPartitionUpdate() is replaced by ExecUpdateAct()
			 */
			TupleTableSlot *inserted_tuple, *retry_slot;
			bool            retry;


			/*
			 * ExecCrossPartitionUpdate will first DELETE the row from the
			 * partition it's currently in and then insert it back into the
			 * root table, which will re-route it to the correct partition.
			 * The first part may have to be repeated if it is detected that
			 * the tuple we're trying to move has been concurrently updated.
 			 */

			retry = !ExecCrossPartitionUpdate(mtstate, resultRelInfo, tupleid,
											  oldtuple, slot, context->planSlot,
											  epqstate, canSetTag,
											  &retry_slot, &inserted_tuple);
			if (retry)
			{
				slot = retry_slot;
				goto yb_lreplace;
 			}

			return inserted_tuple;
#endif
			return NULL;
 		}

		/*
		 * YB_TODO(neil@yugabyte) Write Yugabyte API to work with slot.
		 *
		 * Current Yugabyte API works with HeapTuple instead of slot.
		 * - Create tuple as a workaround to compile.
		 * - Pass slot to Yugabyte call once the API is fixed.
		 */
		shouldFree = true;
		tuple = ExecFetchSlotHeapTuple(slot, true, &shouldFree);

		RangeTblEntry *rte = rt_fetch(resultRelInfo->ri_RangeTableIndex,
									  estate->es_range_table);

		bool row_found = false;

		Bitmapset *actualUpdatedCols = rte->updatedCols;
		Bitmapset *extraUpdatedCols = NULL;
#ifdef YB_TODO
		/* YB_TODO(neil)
		 * Looking at ExecUpdatePrologue() and work on beforeRowUpdateTriggerFired.
		 */
		if (beforeRowUpdateTriggerFired)
		{
			/* trigger might have changed tuple */
			extraUpdatedCols = YBBuildExtraUpdatedCols(
				resultRelationDesc, oldtuple, tuple, rte->updatedCols);
			if (extraUpdatedCols)
			{
				extraUpdatedCols = bms_add_members(extraUpdatedCols, rte->updatedCols);
				actualUpdatedCols = extraUpdatedCols;
			}
		}
#endif

		Bitmapset *primary_key_bms = YBGetTablePrimaryKeyBms(resultRelationDesc);
		bool is_pk_updated = bms_overlap(primary_key_bms, actualUpdatedCols);
		bms_free(primary_key_bms);

		/*
		 * TODO(alex): It probably makes more sense to pass a
		 *             transformed slot instead of a plan slot? Note though
		 *             that it can have tuple materialized already.
		 */

		ModifyTable *plan = (ModifyTable *) mtstate->ps.plan;
		if (is_pk_updated)
		{
			YBCExecuteUpdateReplace(resultRelationDesc, context->planSlot, tuple, estate);
			row_found = true;
		}
		else
		{
			row_found = YBCExecuteUpdate(resultRelationDesc,
										 resultRelInfo,
										 context->planSlot,
										 oldtuple,
										 tuple,
										 estate,
										 plan,
										 mtstate->yb_fetch_target_tuple,
										 estate->yb_es_is_single_row_modify_txn,
										 actualUpdatedCols,
										 canSetTag);
		}

		bms_free(extraUpdatedCols);
		if (!row_found)
		{
			/*
			 * No row was found. This is possible if it's a single row txn
			 * and there is no row to update (since we do not first do a scan).
			 */
			return NULL;
		}

		/*
		 * Update indices selectively if necessary, updates w/o fetched target tuple
		 * do not affect indices.
		 */
		if (YBCRelInfoHasSecondaryIndices(resultRelInfo) &&
			mtstate->yb_fetch_target_tuple)
		{
			Datum	ybctid = YBCGetYBTupleIdFromSlot(context->planSlot);
			List *no_update_index_list = ((ModifyTable *)mtstate->ps.plan)->no_update_index_list;

			/* Delete index entries of the old tuple */
			ExecDeleteIndexTuplesOptimized(resultRelInfo, ybctid, oldtuple, estate,
										   no_update_index_list);

			/* Insert new index entries for tuple */
			recheckIndexes = ExecInsertIndexTuples(
			    resultRelInfo, slot, estate, false, true, NULL, NIL, no_update_index_list);
		}
	}
	else
	{
		/* YB_TODO(neil) May need to pass mtstate instead of estate here */
		/* Fill in the slot appropriately */
		ExecUpdatePrepareSlot(resultRelInfo, slot, estate);

redo_act:
		result = ExecUpdateAct(context, resultRelInfo, tupleid, oldtuple, slot,
							   canSetTag, &updateCxt);

		/*
		 * If ExecUpdateAct reports that a cross-partition update was done,
		 * then the RETURNING tuple (if any) has been projected and there's
		 * nothing else for us to do.
		 */
		if (updateCxt.crossPartUpdate)
			return context->cpUpdateReturningSlot;

		switch (result)
		{
			case TM_SelfModified:

				/*
				 * The target tuple was already updated or deleted by the
				 * current command, or by a later command in the current
				 * transaction.  The former case is possible in a join UPDATE
				 * where multiple tuples join to the same target tuple. This
				 * is pretty questionable, but Postgres has always allowed it:
				 * we just execute the first update action and ignore
				 * additional update attempts.
				 *
				 * The latter case arises if the tuple is modified by a
				 * command in a BEFORE trigger, or perhaps by a command in a
				 * volatile function used in the query.  In such situations we
				 * should not ignore the update, but it is equally unsafe to
				 * proceed.  We don't want to discard the original UPDATE
				 * while keeping the triggered actions based on it; and we
				 * have no principled way to merge this update with the
				 * previous ones.  So throwing an error is the only safe
				 * course.
				 *
				 * If a trigger actually intends this type of interaction, it
				 * can re-execute the UPDATE (assuming it can figure out how)
				 * and then return NULL to cancel the outer update.
				 */
				if (context->tmfd.cmax != estate->es_output_cid)
					ereport(ERROR,
							(errcode(ERRCODE_TRIGGERED_DATA_CHANGE_VIOLATION),
							 errmsg("tuple to be updated was already modified by an operation triggered by the current command"),
							 errhint("Consider using an AFTER trigger instead of a BEFORE trigger to propagate changes to other rows.")));

				/* Else, already updated by self; nothing to do */
				return NULL;

			case TM_Ok:
				break;

			case TM_Updated:
				{
					TupleTableSlot *inputslot;
					TupleTableSlot *epqslot;
					TupleTableSlot *oldSlot;

					if (IsolationUsesXactSnapshot())
						ereport(ERROR,
								(errcode(ERRCODE_T_R_SERIALIZATION_FAILURE),
								 errmsg("could not serialize access due to concurrent update")));

					/*
					 * Already know that we're going to need to do EPQ, so
					 * fetch tuple directly into the right slot.
					 */
					inputslot = EvalPlanQualSlot(context->epqstate, resultRelationDesc,
												 resultRelInfo->ri_RangeTableIndex);

					result = table_tuple_lock(resultRelationDesc, tupleid,
											  estate->es_snapshot,
											  inputslot, estate->es_output_cid,
											  updateCxt.lockmode, LockWaitBlock,
											  TUPLE_LOCK_FLAG_FIND_LAST_VERSION,
											  &context->tmfd);

					switch (result)
					{
						case TM_Ok:
							Assert(context->tmfd.traversed);

							epqslot = EvalPlanQual(context->epqstate,
												   resultRelationDesc,
												   resultRelInfo->ri_RangeTableIndex,
												   inputslot);
							if (TupIsNull(epqslot))
								/* Tuple not passing quals anymore, exiting... */
								return NULL;

							/* Make sure ri_oldTupleSlot is initialized. */
							if (unlikely(!resultRelInfo->ri_projectNewInfoValid))
								ExecInitUpdateProjection(context->mtstate,
														 resultRelInfo);

							/* Fetch the most recent version of old tuple. */
							oldSlot = resultRelInfo->ri_oldTupleSlot;
							if (!table_tuple_fetch_row_version(resultRelationDesc,
															   tupleid,
															   SnapshotAny,
															   oldSlot))
								elog(ERROR, "failed to fetch tuple being updated");
							slot = ExecGetUpdateNewTuple(resultRelInfo,
														 epqslot, oldSlot);
							goto redo_act;

						case TM_Deleted:
							/* tuple already deleted; nothing to do */
							return NULL;

						case TM_SelfModified:

							/*
							 * This can be reached when following an update
							 * chain from a tuple updated by another session,
							 * reaching a tuple that was already updated in
							 * this transaction. If previously modified by
							 * this command, ignore the redundant update,
							 * otherwise error out.
							 *
							 * See also TM_SelfModified response to
							 * table_tuple_update() above.
							 */
							if (context->tmfd.cmax != estate->es_output_cid)
								ereport(ERROR,
										(errcode(ERRCODE_TRIGGERED_DATA_CHANGE_VIOLATION),
										 errmsg("tuple to be updated was already modified by an operation triggered by the current command"),
										 errhint("Consider using an AFTER trigger instead of a BEFORE trigger to propagate changes to other rows.")));
							return NULL;

						default:
							/* see table_tuple_lock call in ExecDelete() */
							elog(ERROR, "unexpected table_tuple_lock status: %u",
								 result);
							return NULL;
					}
				}

				break;

			case TM_Deleted:
				if (IsolationUsesXactSnapshot())
					ereport(ERROR,
							(errcode(ERRCODE_T_R_SERIALIZATION_FAILURE),
							 errmsg("could not serialize access due to concurrent delete")));
				/* tuple already deleted; nothing to do */
				return NULL;

			default:
				elog(ERROR, "unrecognized table_tuple_update status: %u",
					 result);
				return NULL;
		}
	}

	YbPostProcessDml(CMD_UPDATE,
					 resultRelationDesc,
					 slot->tts_tupleDescriptor,
					 tuple);

	if (canSetTag)
		(estate->es_processed)++;

	if (!IsYugaByteEnabled() || !((ModifyTable *)mtstate->ps.plan)->no_row_trigger)
	{
		ExecUpdateEpilogue(context, &updateCxt, resultRelInfo, tupleid, oldtuple,
						   slot, recheckIndexes);
	}
	list_free(recheckIndexes);

	/* Process RETURNING if present */
	if (resultRelInfo->ri_projectReturning)
	{
#ifdef YB_TODO
		/* YB_TODO(neil@yugabyte)
		 * - Check to make sure that ExecFilterJunk is no longer needed here.
		 * - Find where Yugabyte should plugin new code.
		 */
		/*
		 * Prepare the updated tuple in inner slot for RETURNING clause execution.
		 * For ON CONFLICT DO UPDATE, the INSERT returning clause is setup
		 * differently, so junkFilter is not needed.
		 */
		if (IsYBRelation(resultRelationDesc) && resultRelInfo->ri_junkFilter)
			slot = ExecFilterJunk(resultRelInfo->ri_junkFilter, context->planSlot);
#endif
		return ExecProcessReturning(resultRelInfo, slot, context->planSlot);
	}

	return NULL;
}

/*
 * ExecOnConflictUpdate --- execute UPDATE of INSERT ON CONFLICT DO UPDATE
 *
 * Try to lock tuple for update as part of speculative insertion.  If
 * a qual originating from ON CONFLICT DO UPDATE is satisfied, update
 * (but still lock row, even though it may not satisfy estate's
 * snapshot).
 *
 * Returns true if we're done (with or without an update), or false if
 * the caller must retry the INSERT from scratch.
 */
static bool
ExecOnConflictUpdate(ModifyTableContext *context,
					 ResultRelInfo *resultRelInfo,
					 ItemPointer conflictTid,
					 TupleTableSlot *excludedSlot,
					 bool canSetTag,
					 TupleTableSlot **returning)
{
	ModifyTableState *mtstate = context->mtstate;
	ExprContext *econtext = mtstate->ps.ps_ExprContext;
	Relation	relation = resultRelInfo->ri_RelationDesc;
	ExprState  *onConflictSetWhere = resultRelInfo->ri_onConflict->oc_WhereClause;
	HeapTuple	oldtuple = NULL;
	TupleTableSlot *existing = resultRelInfo->ri_onConflict->oc_Existing;
	TM_FailureData tmfd;
	LockTupleMode lockmode;
	TM_Result	test;
	Datum		xminDatum;
	TransactionId xmin;
	bool		isnull;

	/*
	 * This routine selects data and check with indexes for conflicts.
	 * - When selecting data from disk, Postgres prepares a heap buffer to hold the selected row
	 *   and performs transaction-control locking operations on the selected row.
	 * - YugaByte usually uses Postgres heap buffer after selecting data from storage (DocDB).
	 *   However, in this case, it's complicated to use the heap buffer because the two systems use
	 *   different tuple IDs - "ybctid" vs "ctid".
	 * - Also, YugaByte manages transactions separately from Postgres's plan execution.
	 *
	 * Coding-wise, Posgres writes tuple to heap buffer and writes its tuple ID to "conflictTid".
	 * However, YugaByte writes the conflict tuple including its "ybctid" to execution state "estate"
	 * and then frees the slot when done.
	 */
	if (IsYBBackedRelation(relation)) {
		/* Initialize result without calling postgres. */
		test = TM_Ok;
		ItemPointerSetInvalid(&tmfd.ctid);
		goto yb_skip_transaction_control_check;
	}

	/* Determine lock mode to use */
	lockmode = ExecUpdateLockMode(context->estate, resultRelInfo);

	/*
	 * Lock tuple for update.  Don't follow updates when tuple cannot be
	 * locked without doing so.  A row locking conflict here means our
	 * previous conclusion that the tuple is conclusively committed is not
	 * true anymore.
	 */
	test = table_tuple_lock(relation, conflictTid,
							context->estate->es_snapshot,
							existing, context->estate->es_output_cid,
							lockmode, LockWaitBlock, 0,
							&tmfd);
	switch (test)
	{
		case TM_Ok:
			/* success! */
			break;

		case TM_Invisible:

			/*
			 * This can occur when a just inserted tuple is updated again in
			 * the same command. E.g. because multiple rows with the same
			 * conflicting key values are inserted.
			 *
			 * This is somewhat similar to the ExecUpdate() TM_SelfModified
			 * case.  We do not want to proceed because it would lead to the
			 * same row being updated a second time in some unspecified order,
			 * and in contrast to plain UPDATEs there's no historical behavior
			 * to break.
			 *
			 * It is the user's responsibility to prevent this situation from
			 * occurring.  These problems are why the SQL standard similarly
			 * specifies that for SQL MERGE, an exception must be raised in
			 * the event of an attempt to update the same row twice.
			 */
			xminDatum = slot_getsysattr(existing,
										MinTransactionIdAttributeNumber,
										&isnull);
			Assert(!isnull);
			xmin = DatumGetTransactionId(xminDatum);

			if (TransactionIdIsCurrentTransactionId(xmin))
				ereport(ERROR,
						(errcode(ERRCODE_CARDINALITY_VIOLATION),
				/* translator: %s is a SQL command name */
						 errmsg("%s command cannot affect row a second time",
								"ON CONFLICT DO UPDATE"),
						 errhint("Ensure that no rows proposed for insertion within the same command have duplicate constrained values.")));

			/* This shouldn't happen */
			elog(ERROR, "attempted to lock invisible tuple");
			break;

		case TM_SelfModified:

			/*
			 * This state should never be reached. As a dirty snapshot is used
			 * to find conflicting tuples, speculative insertion wouldn't have
			 * seen this row to conflict with.
			 */
			elog(ERROR, "unexpected self-updated tuple");
			break;

		case TM_Updated:
			if (IsolationUsesXactSnapshot())
				ereport(ERROR,
						(errcode(ERRCODE_T_R_SERIALIZATION_FAILURE),
						 errmsg("could not serialize access due to concurrent update")));

			/*
			 * As long as we don't support an UPDATE of INSERT ON CONFLICT for
			 * a partitioned table we shouldn't reach to a case where tuple to
			 * be lock is moved to another partition due to concurrent update
			 * of the partition key.
			 */
			Assert(!ItemPointerIndicatesMovedPartitions(&tmfd.ctid));

			/*
			 * Tell caller to try again from the very start.
			 *
			 * It does not make sense to use the usual EvalPlanQual() style
			 * loop here, as the new version of the row might not conflict
			 * anymore, or the conflicting tuple has actually been deleted.
			 */
			ExecClearTuple(existing);
			return false;

		case TM_Deleted:
			if (IsolationUsesXactSnapshot())
				ereport(ERROR,
						(errcode(ERRCODE_T_R_SERIALIZATION_FAILURE),
						 errmsg("could not serialize access due to concurrent delete")));

			/* see TM_Updated case */
			Assert(!ItemPointerIndicatesMovedPartitions(&tmfd.ctid));
			ExecClearTuple(existing);
			return false;

		default:
			elog(ERROR, "unrecognized table_tuple_lock status: %u", test);
	}

yb_skip_transaction_control_check:
	/* Success, the tuple is locked. */

	/*
	 * Verify that the tuple is visible to our MVCC snapshot if the current
	 * isolation level mandates that.
	 *
	 * It's not sufficient to rely on the check within ExecUpdate() as e.g.
	 * CONFLICT ... WHERE clause may prevent us from reaching that.
	 *
	 * This means we only ever continue when a new command in the current
	 * transaction could see the row, even though in READ COMMITTED mode the
	 * tuple will not be visible according to the current statement's
	 * snapshot.  This is in line with the way UPDATE deals with newer tuple
	 * versions.
	 */
	if (!IsYugaByteEnabled())
		ExecCheckTupleVisible(context->estate, relation, existing);
	else
	{
		/*
		 * YB_TODO(neil@yugabyte) Write Yugabyte API to work with slot.
		 *
		 * Current Yugabyte API works with HeapTuple instead of slot.
		 * - Create tuple as a workaround to compile.
		 * - Pass slot to Yugabyte call once the API is fixed.
		 */
		bool shouldFree = true;
		oldtuple = ExecFetchSlotHeapTuple(context->estate->yb_conflict_slot, true, &shouldFree);
		TABLETUPLE_YBCTID(context->planSlot) = HEAPTUPLE_YBCTID(oldtuple);
	}

	/*
	 * Make tuple and any needed join variables available to ExecQual and
	 * ExecProject.  The EXCLUDED tuple is installed in ecxt_innertuple, while
	 * the target's existing tuple is installed in the scantuple.  EXCLUDED
	 * has been made to reference INNER_VAR in setrefs.c, but there is no
	 * other redirection.
	 */
	econtext->ecxt_scantuple = existing;
	econtext->ecxt_innertuple = excludedSlot;
	econtext->ecxt_outertuple = NULL;

	if (!ExecQual(onConflictSetWhere, econtext))
	{
		ExecClearTuple(existing);	/* see return below */
		InstrCountFiltered1(&mtstate->ps, 1);
		return true;			/* done with the tuple */
	}

	if (resultRelInfo->ri_WithCheckOptions != NIL)
	{
		/*
		 * Check target's existing tuple against UPDATE-applicable USING
		 * security barrier quals (if any), enforced here as RLS checks/WCOs.
		 *
		 * The rewriter creates UPDATE RLS checks/WCOs for UPDATE security
		 * quals, and stores them as WCOs of "kind" WCO_RLS_CONFLICT_CHECK,
		 * but that's almost the extent of its special handling for ON
		 * CONFLICT DO UPDATE.
		 *
		 * The rewriter will also have associated UPDATE applicable straight
		 * RLS checks/WCOs for the benefit of the ExecUpdate() call that
		 * follows.  INSERTs and UPDATEs naturally have mutually exclusive WCO
		 * kinds, so there is no danger of spurious over-enforcement in the
		 * INSERT or UPDATE path.
		 */
		ExecWithCheckOptions(WCO_RLS_CONFLICT_CHECK, resultRelInfo,
							 existing,
							 mtstate->ps.state);
	}

	/* Project the new tuple version */
	ExecProject(resultRelInfo->ri_onConflict->oc_ProjInfo);

	/*
	 * Note that it is possible that the target tuple has been modified in
	 * this session, after the above table_tuple_lock. We choose to not error
	 * out in that case, in line with ExecUpdate's treatment of similar cases.
	 * This can happen if an UPDATE is triggered from within ExecQual(),
	 * ExecWithCheckOptions() or ExecProject() above, e.g. by selecting from a
	 * wCTE in the ON CONFLICT's SET.
	 */

	/* Execute UPDATE with projection */
#ifdef YB_TODO
	/* YB_TODO(neil) Postgres changes its function signature. Need fix while compiling */
	*returning = ExecUpdate(mtstate, resultRelInfo, conflictTid, oldtuple,
							resultRelInfo->ri_onConflict->oc_ProjSlot,
							context->planSlot,
							&mtstate->mt_epqstate, mtstate->ps.state,
							canSetTag);
#endif
	*returning = ExecUpdate(context, resultRelInfo,
							conflictTid, NULL,
							resultRelInfo->ri_onConflict->oc_ProjSlot,
							canSetTag);

	/*
	 * Clear out existing tuple, as there might not be another conflict among
	 * the next input rows. Don't want to hold resources till the end of the
	 * query.
	 */
	ExecClearTuple(existing);
	return true;
}

/*
 * Perform MERGE.
 */
static TupleTableSlot *
ExecMerge(ModifyTableContext *context, ResultRelInfo *resultRelInfo,
		  ItemPointer tupleid, bool canSetTag)
{
	bool		matched;

	/*-----
	 * If we are dealing with a WHEN MATCHED case (tupleid is valid), we
	 * execute the first action for which the additional WHEN MATCHED AND
	 * quals pass.  If an action without quals is found, that action is
	 * executed.
	 *
	 * Similarly, if we are dealing with WHEN NOT MATCHED case, we look at
	 * the given WHEN NOT MATCHED actions in sequence until one passes.
	 *
	 * Things get interesting in case of concurrent update/delete of the
	 * target tuple. Such concurrent update/delete is detected while we are
	 * executing a WHEN MATCHED action.
	 *
	 * A concurrent update can:
	 *
	 * 1. modify the target tuple so that it no longer satisfies the
	 *    additional quals attached to the current WHEN MATCHED action
	 *
	 *    In this case, we are still dealing with a WHEN MATCHED case.
	 *    We recheck the list of WHEN MATCHED actions from the start and
	 *    choose the first one that satisfies the new target tuple.
	 *
	 * 2. modify the target tuple so that the join quals no longer pass and
	 *    hence the source tuple no longer has a match.
	 *
	 *    In this case, the source tuple no longer matches the target tuple,
	 *    so we now instead find a qualifying WHEN NOT MATCHED action to
	 *    execute.
	 *
	 * XXX Hmmm, what if the updated tuple would now match one that was
	 * considered NOT MATCHED so far?
	 *
	 * A concurrent delete changes a WHEN MATCHED case to WHEN NOT MATCHED.
	 *
	 * ExecMergeMatched takes care of following the update chain and
	 * re-finding the qualifying WHEN MATCHED action, as long as the updated
	 * target tuple still satisfies the join quals, i.e., it remains a WHEN
	 * MATCHED case. If the tuple gets deleted or the join quals fail, it
	 * returns and we try ExecMergeNotMatched. Given that ExecMergeMatched
	 * always make progress by following the update chain and we never switch
	 * from ExecMergeNotMatched to ExecMergeMatched, there is no risk of a
	 * livelock.
	 */
	matched = tupleid != NULL;
	if (matched)
		matched = ExecMergeMatched(context, resultRelInfo, tupleid, canSetTag);

	/*
	 * Either we were dealing with a NOT MATCHED tuple or ExecMergeMatched()
	 * returned "false", indicating the previously MATCHED tuple no longer
	 * matches.
	 */
	if (!matched)
		ExecMergeNotMatched(context, resultRelInfo, canSetTag);

	/* No RETURNING support yet */
	return NULL;
}

/*
 * Check and execute the first qualifying MATCHED action. The current target
 * tuple is identified by tupleid.
 *
 * We start from the first WHEN MATCHED action and check if the WHEN quals
 * pass, if any. If the WHEN quals for the first action do not pass, we
 * check the second, then the third and so on. If we reach to the end, no
 * action is taken and we return true, indicating that no further action is
 * required for this tuple.
 *
 * If we do find a qualifying action, then we attempt to execute the action.
 *
 * If the tuple is concurrently updated, EvalPlanQual is run with the updated
 * tuple to recheck the join quals. Note that the additional quals associated
 * with individual actions are evaluated by this routine via ExecQual, while
 * EvalPlanQual checks for the join quals. If EvalPlanQual tells us that the
 * updated tuple still passes the join quals, then we restart from the first
 * action to look for a qualifying action. Otherwise, we return false --
 * meaning that a NOT MATCHED action must now be executed for the current
 * source tuple.
 */
static bool
ExecMergeMatched(ModifyTableContext *context, ResultRelInfo *resultRelInfo,
				 ItemPointer tupleid, bool canSetTag)
{
	ModifyTableState *mtstate = context->mtstate;
	TupleTableSlot *newslot;
	EState	   *estate = context->estate;
	ExprContext *econtext = mtstate->ps.ps_ExprContext;
	bool		isNull;
	EPQState   *epqstate = &mtstate->mt_epqstate;
	ListCell   *l;

	/*
	 * If there are no WHEN MATCHED actions, we are done.
	 */
	if (resultRelInfo->ri_matchedMergeAction == NIL)
		return true;

	/*
	 * Make tuple and any needed join variables available to ExecQual and
	 * ExecProject. The target's existing tuple is installed in the scantuple.
	 * Again, this target relation's slot is required only in the case of a
	 * MATCHED tuple and UPDATE/DELETE actions.
	 */
	econtext->ecxt_scantuple = resultRelInfo->ri_oldTupleSlot;
	econtext->ecxt_innertuple = context->planSlot;
	econtext->ecxt_outertuple = NULL;

lmerge_matched:;

	/*
	 * This routine is only invoked for matched rows, and we must have found
	 * the tupleid of the target row in that case; fetch that tuple.
	 *
	 * We use SnapshotAny for this because we might get called again after
	 * EvalPlanQual returns us a new tuple, which may not be visible to our
	 * MVCC snapshot.
	 */

	if (!table_tuple_fetch_row_version(resultRelInfo->ri_RelationDesc,
									   tupleid,
									   SnapshotAny,
									   resultRelInfo->ri_oldTupleSlot))
		elog(ERROR, "failed to fetch the target tuple");

	foreach(l, resultRelInfo->ri_matchedMergeAction)
	{
		MergeActionState *relaction = (MergeActionState *) lfirst(l);
		CmdType		commandType = relaction->mas_action->commandType;
		List	   *recheckIndexes = NIL;
		TM_Result	result;
		UpdateContext updateCxt = {0};

		/*
		 * Test condition, if any.
		 *
		 * In the absence of any condition, we perform the action
		 * unconditionally (no need to check separately since ExecQual() will
		 * return true if there are no conditions to evaluate).
		 */
		if (!ExecQual(relaction->mas_whenqual, econtext))
			continue;

		/*
		 * Check if the existing target tuple meets the USING checks of
		 * UPDATE/DELETE RLS policies. If those checks fail, we throw an
		 * error.
		 *
		 * The WITH CHECK quals are applied in ExecUpdate() and hence we need
		 * not do anything special to handle them.
		 *
		 * NOTE: We must do this after WHEN quals are evaluated, so that we
		 * check policies only when they matter.
		 */
		if (resultRelInfo->ri_WithCheckOptions)
		{
			ExecWithCheckOptions(commandType == CMD_UPDATE ?
								 WCO_RLS_MERGE_UPDATE_CHECK : WCO_RLS_MERGE_DELETE_CHECK,
								 resultRelInfo,
								 resultRelInfo->ri_oldTupleSlot,
								 context->mtstate->ps.state);
		}

		/* Perform stated action */
		switch (commandType)
		{
			case CMD_UPDATE:

				/*
				 * Project the output tuple, and use that to update the table.
				 * We don't need to filter out junk attributes, because the
				 * UPDATE action's targetlist doesn't have any.
				 */
				newslot = ExecProject(relaction->mas_proj);

				context->relaction = relaction;
				context->GetUpdateNewTuple = mergeGetUpdateNewTuple;
				context->cpUpdateRetrySlot = NULL;

				if (!ExecUpdatePrologue(context, resultRelInfo,
										tupleid, NULL, newslot))
				{
					result = TM_Ok;
					break;
				}
				ExecUpdatePrepareSlot(resultRelInfo, newslot, context->estate);
				result = ExecUpdateAct(context, resultRelInfo, tupleid, NULL,
									   newslot, mtstate->canSetTag, &updateCxt);
				if (result == TM_Ok && updateCxt.updated)
				{
					ExecUpdateEpilogue(context, &updateCxt, resultRelInfo,
									   tupleid, NULL, newslot, recheckIndexes);
					mtstate->mt_merge_updated += 1;
				}

				break;

			case CMD_DELETE:
				context->relaction = relaction;
				if (!ExecDeletePrologue(context, resultRelInfo, tupleid,
										NULL, NULL))
				{
					result = TM_Ok;
					break;
				}
				result = ExecDeleteAct(context, resultRelInfo, tupleid, false);
				if (result == TM_Ok)
				{
					ExecDeleteEpilogue(context, resultRelInfo, tupleid, NULL,
									   false);
					mtstate->mt_merge_deleted += 1;
				}
				break;

			case CMD_NOTHING:
				/* Doing nothing is always OK */
				result = TM_Ok;
				break;

			default:
				elog(ERROR, "unknown action in MERGE WHEN MATCHED clause");
		}

		switch (result)
		{
			case TM_Ok:
				/* all good; perform final actions */
				if (canSetTag && commandType != CMD_NOTHING)
					(estate->es_processed)++;

				break;

			case TM_SelfModified:

				/*
				 * The SQL standard disallows this for MERGE.
				 */
				if (TransactionIdIsCurrentTransactionId(context->tmfd.xmax))
					ereport(ERROR,
							(errcode(ERRCODE_CARDINALITY_VIOLATION),
					/* translator: %s is a SQL command name */
							 errmsg("%s command cannot affect row a second time",
									"MERGE"),
							 errhint("Ensure that not more than one source row matches any one target row.")));
				/* This shouldn't happen */
				elog(ERROR, "attempted to update or delete invisible tuple");
				break;

			case TM_Deleted:
				if (IsolationUsesXactSnapshot())
					ereport(ERROR,
							(errcode(ERRCODE_T_R_SERIALIZATION_FAILURE),
							 errmsg("could not serialize access due to concurrent delete")));

				/*
				 * If the tuple was already deleted, return to let caller
				 * handle it under NOT MATCHED clauses.
				 */
				return false;

			case TM_Updated:
				{
					Relation	resultRelationDesc;
					TupleTableSlot *epqslot,
							   *inputslot;
					LockTupleMode lockmode;

					/*
					 * The target tuple was concurrently updated by some other
					 * transaction.
					 */

					/*
					 * If cpUpdateRetrySlot is set, ExecCrossPartitionUpdate()
					 * must have detected that the tuple was concurrently
					 * updated, so we restart the search for an appropriate
					 * WHEN MATCHED clause to process the updated tuple.
					 *
					 * In this case, ExecDelete() would already have performed
					 * EvalPlanQual() on the latest version of the tuple,
					 * which in turn would already have been loaded into
					 * ri_oldTupleSlot, so no need to do either of those
					 * things.
					 *
					 * XXX why do we not check the WHEN NOT MATCHED list in
					 * this case?
					 */
					if (!TupIsNull(context->cpUpdateRetrySlot))
						goto lmerge_matched;

					/*
					 * Otherwise, we run the EvalPlanQual() with the new
					 * version of the tuple. If EvalPlanQual() does not return
					 * a tuple, then we switch to the NOT MATCHED list of
					 * actions. If it does return a tuple and the join qual is
					 * still satisfied, then we just need to recheck the
					 * MATCHED actions, starting from the top, and execute the
					 * first qualifying action.
					 */
					resultRelationDesc = resultRelInfo->ri_RelationDesc;
					lockmode = ExecUpdateLockMode(estate, resultRelInfo);

					inputslot = EvalPlanQualSlot(epqstate, resultRelationDesc,
												 resultRelInfo->ri_RangeTableIndex);

					result = table_tuple_lock(resultRelationDesc, tupleid,
											  estate->es_snapshot,
											  inputslot, estate->es_output_cid,
											  lockmode, LockWaitBlock,
											  TUPLE_LOCK_FLAG_FIND_LAST_VERSION,
											  &context->tmfd);
					switch (result)
					{
						case TM_Ok:
							epqslot = EvalPlanQual(epqstate,
												   resultRelationDesc,
												   resultRelInfo->ri_RangeTableIndex,
												   inputslot);

							/*
							 * If we got no tuple, or the tuple we get has a
							 * NULL ctid, go back to caller: this one is not a
							 * MATCHED tuple anymore, so they can retry with
							 * NOT MATCHED actions.
							 */
							if (TupIsNull(epqslot))
								return false;

							(void) ExecGetJunkAttribute(epqslot,
														resultRelInfo->ri_RowIdAttNo,
														&isNull);
							if (isNull)
								return false;

							/*
							 * When a tuple was updated and migrated to
							 * another partition concurrently, the current
							 * MERGE implementation can't follow.  There's
							 * probably a better way to handle this case, but
							 * it'd require recognizing the relation to which
							 * the tuple moved, and setting our current
							 * resultRelInfo to that.
							 */
							if (ItemPointerIndicatesMovedPartitions(&context->tmfd.ctid))
								ereport(ERROR,
										(errcode(ERRCODE_T_R_SERIALIZATION_FAILURE),
										 errmsg("tuple to be deleted was already moved to another partition due to concurrent update")));

							/*
							 * A non-NULL ctid means that we are still dealing
							 * with MATCHED case. Restart the loop so that we
							 * apply all the MATCHED rules again, to ensure
							 * that the first qualifying WHEN MATCHED action
							 * is executed.
							 *
							 * Update tupleid to that of the new tuple, for
							 * the refetch we do at the top.
							 */
							ItemPointerCopy(&context->tmfd.ctid, tupleid);
							goto lmerge_matched;

						case TM_Deleted:

							/*
							 * tuple already deleted; tell caller to run NOT
							 * MATCHED actions
							 */
							return false;

						case TM_SelfModified:

							/*
							 * This can be reached when following an update
							 * chain from a tuple updated by another session,
							 * reaching a tuple that was already updated in
							 * this transaction. If previously modified by
							 * this command, ignore the redundant update,
							 * otherwise error out.
							 *
							 * See also response to TM_SelfModified in
							 * ExecUpdate().
							 */
							if (context->tmfd.cmax != estate->es_output_cid)
								ereport(ERROR,
										(errcode(ERRCODE_TRIGGERED_DATA_CHANGE_VIOLATION),
										 errmsg("tuple to be updated or deleted was already modified by an operation triggered by the current command"),
										 errhint("Consider using an AFTER trigger instead of a BEFORE trigger to propagate changes to other rows.")));
							return false;

						default:
							/* see table_tuple_lock call in ExecDelete() */
							elog(ERROR, "unexpected table_tuple_lock status: %u",
								 result);
							return false;
					}
				}

			case TM_Invisible:
			case TM_WouldBlock:
			case TM_BeingModified:
				/* these should not occur */
				elog(ERROR, "unexpected tuple operation result: %d", result);
				break;
		}

		/*
		 * We've activated one of the WHEN clauses, so we don't search
		 * further. This is required behaviour, not an optimization.
		 */
		break;
	}

	/*
	 * Successfully executed an action or no qualifying action was found.
	 */
	return true;
}

/*
 * Execute the first qualifying NOT MATCHED action.
 */
static void
ExecMergeNotMatched(ModifyTableContext *context, ResultRelInfo *resultRelInfo,
					bool canSetTag)
{
	ModifyTableState *mtstate = context->mtstate;
	ExprContext *econtext = mtstate->ps.ps_ExprContext;
	List	   *actionStates = NIL;
	ListCell   *l;

	/*
	 * For INSERT actions, the root relation's merge action is OK since the
	 * INSERT's targetlist and the WHEN conditions can only refer to the
	 * source relation and hence it does not matter which result relation we
	 * work with.
	 *
	 * XXX does this mean that we can avoid creating copies of actionStates on
	 * partitioned tables, for not-matched actions?
	 */
	actionStates = resultRelInfo->ri_notMatchedMergeAction;

	/*
	 * Make source tuple available to ExecQual and ExecProject. We don't need
	 * the target tuple, since the WHEN quals and targetlist can't refer to
	 * the target columns.
	 */
	econtext->ecxt_scantuple = NULL;
	econtext->ecxt_innertuple = context->planSlot;
	econtext->ecxt_outertuple = NULL;

	foreach(l, actionStates)
	{
		MergeActionState *action = (MergeActionState *) lfirst(l);
		CmdType		commandType = action->mas_action->commandType;
		TupleTableSlot *newslot;

		/*
		 * Test condition, if any.
		 *
		 * In the absence of any condition, we perform the action
		 * unconditionally (no need to check separately since ExecQual() will
		 * return true if there are no conditions to evaluate).
		 */
		if (!ExecQual(action->mas_whenqual, econtext))
			continue;

		/* Perform stated action */
		switch (commandType)
		{
			case CMD_INSERT:

				/*
				 * Project the tuple.  In case of a partitioned table, the
				 * projection was already built to use the root's descriptor,
				 * so we don't need to map the tuple here.
				 */
				newslot = ExecProject(action->mas_proj);
				context->relaction = action;

				(void) ExecInsert(context, mtstate->rootResultRelInfo, newslot,
								  canSetTag, NULL, NULL);
				mtstate->mt_merge_inserted += 1;
				break;
			case CMD_NOTHING:
				/* Do nothing */
				break;
			default:
				elog(ERROR, "unknown action in MERGE WHEN NOT MATCHED clause");
		}

		/*
		 * We've activated one of the WHEN clauses, so we don't search
		 * further. This is required behaviour, not an optimization.
		 */
		break;
	}
}

/*
 * Initialize state for execution of MERGE.
 */
void
ExecInitMerge(ModifyTableState *mtstate, EState *estate)
{
	ModifyTable *node = (ModifyTable *) mtstate->ps.plan;
	ResultRelInfo *rootRelInfo = mtstate->rootResultRelInfo;
	ResultRelInfo *resultRelInfo;
	ExprContext *econtext;
	ListCell   *lc;
	int			i;

	if (node->mergeActionLists == NIL)
		return;

	mtstate->mt_merge_subcommands = 0;

	if (mtstate->ps.ps_ExprContext == NULL)
		ExecAssignExprContext(estate, &mtstate->ps);
	econtext = mtstate->ps.ps_ExprContext;

	/*
	 * Create a MergeActionState for each action on the mergeActionList and
	 * add it to either a list of matched actions or not-matched actions.
	 *
	 * Similar logic appears in ExecInitPartitionInfo(), so if changing
	 * anything here, do so there too.
	 */
	i = 0;
	foreach(lc, node->mergeActionLists)
	{
		List	   *mergeActionList = lfirst(lc);
		TupleDesc	relationDesc;
		ListCell   *l;

		resultRelInfo = mtstate->resultRelInfo + i;
		i++;
		relationDesc = RelationGetDescr(resultRelInfo->ri_RelationDesc);

		/* initialize slots for MERGE fetches from this rel */
		if (unlikely(!resultRelInfo->ri_projectNewInfoValid))
			ExecInitMergeTupleSlots(mtstate, resultRelInfo);

		foreach(l, mergeActionList)
		{
			MergeAction *action = (MergeAction *) lfirst(l);
			MergeActionState *action_state;
			TupleTableSlot *tgtslot;
			TupleDesc	tgtdesc;
			List	  **list;

			/*
			 * Build action merge state for this rel.  (For partitions,
			 * equivalent code exists in ExecInitPartitionInfo.)
			 */
			action_state = makeNode(MergeActionState);
			action_state->mas_action = action;
			action_state->mas_whenqual = ExecInitQual((List *) action->qual,
													  &mtstate->ps);

			/*
			 * We create two lists - one for WHEN MATCHED actions and one for
			 * WHEN NOT MATCHED actions - and stick the MergeActionState into
			 * the appropriate list.
			 */
			if (action_state->mas_action->matched)
				list = &resultRelInfo->ri_matchedMergeAction;
			else
				list = &resultRelInfo->ri_notMatchedMergeAction;
			*list = lappend(*list, action_state);

			switch (action->commandType)
			{
				case CMD_INSERT:
					ExecCheckPlanOutput(rootRelInfo->ri_RelationDesc,
										action->targetList);

					/*
					 * If the MERGE targets a partitioned table, any INSERT
					 * actions must be routed through it, not the child
					 * relations. Initialize the routing struct and the root
					 * table's "new" tuple slot for that, if not already done.
					 * The projection we prepare, for all relations, uses the
					 * root relation descriptor, and targets the plan's root
					 * slot.  (This is consistent with the fact that we
					 * checked the plan output to match the root relation,
					 * above.)
					 */
					if (rootRelInfo->ri_RelationDesc->rd_rel->relkind ==
						RELKIND_PARTITIONED_TABLE)
					{
						if (mtstate->mt_partition_tuple_routing == NULL)
						{
							/*
							 * Initialize planstate for routing if not already
							 * done.
							 *
							 * Note that the slot is managed as a standalone
							 * slot belonging to ModifyTableState, so we pass
							 * NULL for the 2nd argument.
							 */
							mtstate->mt_root_tuple_slot =
								table_slot_create(rootRelInfo->ri_RelationDesc,
												  NULL);
							mtstate->mt_partition_tuple_routing =
								ExecSetupPartitionTupleRouting(estate,
															   rootRelInfo->ri_RelationDesc);
						}
						tgtslot = mtstate->mt_root_tuple_slot;
						tgtdesc = RelationGetDescr(rootRelInfo->ri_RelationDesc);
					}
					else
					{
						/* not partitioned? use the stock relation and slot */
						tgtslot = resultRelInfo->ri_newTupleSlot;
						tgtdesc = RelationGetDescr(resultRelInfo->ri_RelationDesc);
					}

					action_state->mas_proj =
						ExecBuildProjectionInfo(action->targetList, econtext,
												tgtslot,
												&mtstate->ps,
												tgtdesc);

					mtstate->mt_merge_subcommands |= MERGE_INSERT;
					break;
				case CMD_UPDATE:
					action_state->mas_proj =
						ExecBuildUpdateProjection(action->targetList,
												  true,
												  action->updateColnos,
												  relationDesc,
												  econtext,
												  resultRelInfo->ri_newTupleSlot,
												  &mtstate->ps);
					mtstate->mt_merge_subcommands |= MERGE_UPDATE;
					break;
				case CMD_DELETE:
					mtstate->mt_merge_subcommands |= MERGE_DELETE;
					break;
				case CMD_NOTHING:
					break;
				default:
					elog(ERROR, "unknown operation");
					break;
			}
		}
	}
}

/*
 * Initializes the tuple slots in a ResultRelInfo for any MERGE action.
 *
 * We mark 'projectNewInfoValid' even though the projections themselves
 * are not initialized here.
 */
void
ExecInitMergeTupleSlots(ModifyTableState *mtstate,
						ResultRelInfo *resultRelInfo)
{
	EState	   *estate = mtstate->ps.state;

	Assert(!resultRelInfo->ri_projectNewInfoValid);

	resultRelInfo->ri_oldTupleSlot =
		table_slot_create(resultRelInfo->ri_RelationDesc,
						  &estate->es_tupleTable);
	resultRelInfo->ri_newTupleSlot =
		table_slot_create(resultRelInfo->ri_RelationDesc,
						  &estate->es_tupleTable);
	resultRelInfo->ri_projectNewInfoValid = true;
}

/*
 * Callback for ModifyTableContext->GetUpdateNewTuple for use by MERGE.  It
 * computes the updated tuple by projecting from the current merge action's
 * projection.
 */
static TupleTableSlot *
mergeGetUpdateNewTuple(ResultRelInfo *relinfo,
					   TupleTableSlot *planSlot,
					   TupleTableSlot *oldSlot,
					   MergeActionState *relaction)
{
	ExprContext *econtext = relaction->mas_proj->pi_exprContext;

	econtext->ecxt_scantuple = oldSlot;
	econtext->ecxt_innertuple = planSlot;

	return ExecProject(relaction->mas_proj);
}

/*
 * Process BEFORE EACH STATEMENT triggers
 */
static void
fireBSTriggers(ModifyTableState *node)
{
	ModifyTable *plan = (ModifyTable *) node->ps.plan;
	ResultRelInfo *resultRelInfo = node->rootResultRelInfo;

	switch (node->operation)
	{
		case CMD_INSERT:
			ExecBSInsertTriggers(node->ps.state, resultRelInfo);
			if (plan->onConflictAction == ONCONFLICT_UPDATE)
				ExecBSUpdateTriggers(node->ps.state,
									 resultRelInfo);
			break;
		case CMD_UPDATE:
			ExecBSUpdateTriggers(node->ps.state, resultRelInfo);
			break;
		case CMD_DELETE:
			ExecBSDeleteTriggers(node->ps.state, resultRelInfo);
			break;
		case CMD_MERGE:
			if (node->mt_merge_subcommands & MERGE_INSERT)
				ExecBSInsertTriggers(node->ps.state, resultRelInfo);
			if (node->mt_merge_subcommands & MERGE_UPDATE)
				ExecBSUpdateTriggers(node->ps.state, resultRelInfo);
			if (node->mt_merge_subcommands & MERGE_DELETE)
				ExecBSDeleteTriggers(node->ps.state, resultRelInfo);
			break;
		default:
			elog(ERROR, "unknown operation");
			break;
	}
}

/*
 * Process AFTER EACH STATEMENT triggers
 */
static void
fireASTriggers(ModifyTableState *node)
{
	ModifyTable *plan = (ModifyTable *) node->ps.plan;
	ResultRelInfo *resultRelInfo = node->rootResultRelInfo;

	switch (node->operation)
	{
		case CMD_INSERT:
			if (plan->onConflictAction == ONCONFLICT_UPDATE)
				ExecASUpdateTriggers(node->ps.state,
									 resultRelInfo,
									 node->mt_oc_transition_capture);
			ExecASInsertTriggers(node->ps.state, resultRelInfo,
								 node->mt_transition_capture);
			break;
		case CMD_UPDATE:
			ExecASUpdateTriggers(node->ps.state, resultRelInfo,
								 node->mt_transition_capture);
			break;
		case CMD_DELETE:
			ExecASDeleteTriggers(node->ps.state, resultRelInfo,
								 node->mt_transition_capture);
			break;
		case CMD_MERGE:
			if (node->mt_merge_subcommands & MERGE_DELETE)
				ExecASDeleteTriggers(node->ps.state, resultRelInfo,
									 node->mt_transition_capture);
			if (node->mt_merge_subcommands & MERGE_UPDATE)
				ExecASUpdateTriggers(node->ps.state, resultRelInfo,
									 node->mt_transition_capture);
			if (node->mt_merge_subcommands & MERGE_INSERT)
				ExecASInsertTriggers(node->ps.state, resultRelInfo,
									 node->mt_transition_capture);
			break;
		default:
			elog(ERROR, "unknown operation");
			break;
	}
}

/*
 * Set up the state needed for collecting transition tuples for AFTER
 * triggers.
 */
static void
ExecSetupTransitionCaptureState(ModifyTableState *mtstate, EState *estate)
{
	ModifyTable *plan = (ModifyTable *) mtstate->ps.plan;
	ResultRelInfo *targetRelInfo = mtstate->rootResultRelInfo;

	/* Check for transition tables on the directly targeted relation. */
	mtstate->mt_transition_capture =
		MakeTransitionCaptureState(targetRelInfo->ri_TrigDesc,
								   RelationGetRelid(targetRelInfo->ri_RelationDesc),
								   mtstate->operation);
	if (plan->operation == CMD_INSERT &&
		plan->onConflictAction == ONCONFLICT_UPDATE)
		mtstate->mt_oc_transition_capture =
			MakeTransitionCaptureState(targetRelInfo->ri_TrigDesc,
									   RelationGetRelid(targetRelInfo->ri_RelationDesc),
									   CMD_UPDATE);
}

/*
 * ExecPrepareTupleRouting --- prepare for routing one tuple
 *
 * Determine the partition in which the tuple in slot is to be inserted,
 * and return its ResultRelInfo in *partRelInfo.  The return value is
 * a slot holding the tuple of the partition rowtype.
 *
 * This also sets the transition table information in mtstate based on the
 * selected partition.
 */
static TupleTableSlot *
ExecPrepareTupleRouting(ModifyTableState *mtstate,
						EState *estate,
						PartitionTupleRouting *proute,
						ResultRelInfo *targetRelInfo,
						TupleTableSlot *slot,
						ResultRelInfo **partRelInfo)
{
	ResultRelInfo *partrel;
	TupleConversionMap *map;

	/*
	 * Lookup the target partition's ResultRelInfo.  If ExecFindPartition does
	 * not find a valid partition for the tuple in 'slot' then an error is
	 * raised.  An error may also be raised if the found partition is not a
	 * valid target for INSERTs.  This is required since a partitioned table
	 * UPDATE to another partition becomes a DELETE+INSERT.
	 */
	partrel = ExecFindPartition(mtstate, targetRelInfo, proute, slot, estate);

	/*
	 * If we're capturing transition tuples, we might need to convert from the
	 * partition rowtype to root partitioned table's rowtype.  But if there
	 * are no BEFORE triggers on the partition that could change the tuple, we
	 * can just remember the original unconverted tuple to avoid a needless
	 * round trip conversion.
	 */
	if (mtstate->mt_transition_capture != NULL)
	{
		bool		has_before_insert_row_trig;

		has_before_insert_row_trig = (partrel->ri_TrigDesc &&
									  partrel->ri_TrigDesc->trig_insert_before_row);

		mtstate->mt_transition_capture->tcs_original_insert_tuple =
			!has_before_insert_row_trig ? slot : NULL;
	}

	/*
	 * Convert the tuple, if necessary.
	 */
	map = partrel->ri_RootToPartitionMap;
	if (map != NULL)
	{
		TupleTableSlot *new_slot = partrel->ri_PartitionTupleSlot;
		slot = execute_attr_map_slot(map->attrMap, slot, new_slot);
	}

#ifdef YB_TODO
	/* YB_TODO(neil@yugabyte)
	 * This code is no longer needed?
	 */
	/*
	 * For a partitioned relation, table constraints (such as FK) are visible on a
	 * target partition rather than an original insert target.
	 * As such, we should reevaluate single-row transaction constraints after
	 * we determine the concrete partition.
	 */
	if (estate->yb_es_is_single_row_modify_txn)
	{
		estate->yb_es_is_single_row_modify_txn = YBCIsSingleRowTxnCapableRel(partrel);
	}
#endif

	*partRelInfo = partrel;
	return slot;
}

/* ----------------------------------------------------------------
 *	   ExecModifyTable
 *
 *		Perform table modifications as required, and return RETURNING results
 *		if needed.
 * ----------------------------------------------------------------
 */
static TupleTableSlot *
ExecModifyTable(PlanState *pstate)
{
	ModifyTableState *node = castNode(ModifyTableState, pstate);
	ModifyTableContext context;
	EState	   *estate = node->ps.state;
	CmdType		operation = node->operation;
	ResultRelInfo *resultRelInfo;
	PlanState  *subplanstate;
	TupleTableSlot *slot;
	TupleTableSlot *oldSlot;
	ItemPointerData tuple_ctid;
	HeapTupleData oldtupdata;
	HeapTuple	oldtuple;
	ItemPointer tupleid;

	CHECK_FOR_INTERRUPTS();

	/*
	 * This should NOT get called during EvalPlanQual; we should have passed a
	 * subplan tree to EvalPlanQual, instead.  Use a runtime test not just
	 * Assert because this condition is easy to miss in testing.  (Note:
	 * although ModifyTable should not get executed within an EvalPlanQual
	 * operation, we do have to allow it to be initialized and shut down in
	 * case it is within a CTE subplan.  Hence this test must be here, not in
	 * ExecInitModifyTable.)
	 */
	if (estate->es_epq_active != NULL)
		elog(ERROR, "ModifyTable should not be called during EvalPlanQual");

	/*
	 * If we've already completed processing, don't try to do more.  We need
	 * this test because ExecPostprocessPlan might call us an extra time, and
	 * our subplan's nodes aren't necessarily robust against being called
	 * extra times.
	 */
	if (node->mt_done)
		return NULL;

	/*
	 * On first call, fire BEFORE STATEMENT triggers before proceeding.
	 */
	if (node->fireBSTriggers)
	{
		fireBSTriggers(node);
		node->fireBSTriggers = false;
	}

	/* Preload local variables */
	resultRelInfo = node->resultRelInfo + node->mt_lastResultIndex;
	subplanstate = outerPlanState(node);

	/* Set global context */
	context.mtstate = node;
	context.epqstate = &node->mt_epqstate;
	context.estate = estate;

	/*
	 * Fetch rows from subplan, and execute the required table modification
	 * for each row.
	 */
	for (;;)
	{
		/*
		 * Reset the per-output-tuple exprcontext.  This is needed because
		 * triggers expect to use that context as workspace.  It's a bit ugly
		 * to do this below the top level of the plan, however.  We might need
		 * to rethink this later.
		 */
		ResetPerTupleExprContext(estate);

		/*
		 * Reset per-tuple memory context used for processing on conflict and
		 * returning clauses, to free any expression evaluation storage
		 * allocated in the previous cycle.
		 */
		if (pstate->ps_ExprContext)
			ResetExprContext(pstate->ps_ExprContext);

		context.planSlot = ExecProcNode(subplanstate);

		/* No more tuples to process? */
		if (TupIsNull(context.planSlot))
			break;

		/*
		 * When there are multiple result relations, each tuple contains a
		 * junk column that gives the OID of the rel from which it came.
		 * Extract it and select the correct result relation.
		 */
		if (AttributeNumberIsValid(node->mt_resultOidAttno))
		{
			Datum		datum;
			bool		isNull;
			Oid			resultoid;

			datum = ExecGetJunkAttribute(context.planSlot, node->mt_resultOidAttno,
										 &isNull);
			if (isNull)
			{
				/*
				 * For commands other than MERGE, any tuples having InvalidOid
				 * for tableoid are errors.  For MERGE, we may need to handle
				 * them as WHEN NOT MATCHED clauses if any, so do that.
				 *
				 * Note that we use the node's toplevel resultRelInfo, not any
				 * specific partition's.
				 */
				if (operation == CMD_MERGE)
				{
					EvalPlanQualSetSlot(&node->mt_epqstate, context.planSlot);

					ExecMerge(&context, node->resultRelInfo, NULL, node->canSetTag);
					continue;	/* no RETURNING support yet */
				}

				elog(ERROR, "tableoid is NULL");
			}
			resultoid = DatumGetObjectId(datum);

			/* If it's not the same as last time, we need to locate the rel */
			if (resultoid != node->mt_lastResultOid)
				resultRelInfo = ExecLookupResultRelByOid(node, resultoid,
														 false, true);
		}

		/*
		 * If resultRelInfo->ri_usesFdwDirectModify is true, all we need to do
		 * here is compute the RETURNING expressions.
		 */
		if (resultRelInfo->ri_usesFdwDirectModify)
		{
			Assert(resultRelInfo->ri_projectReturning);

			/*
			 * A scan slot containing the data that was actually inserted,
			 * updated or deleted has already been made available to
			 * ExecProcessReturning by IterateDirectModify, so no need to
			 * provide it here.
			 */
			slot = ExecProcessReturning(resultRelInfo, NULL, context.planSlot);

			return slot;
		}

		EvalPlanQualSetSlot(&node->mt_epqstate, context.planSlot);
		slot = context.planSlot;

		tupleid = NULL;
		oldtuple = NULL;

		/*
		 * For UPDATE/DELETE/MERGE, fetch the row identity info for the tuple
		 * to be updated/deleted/merged.  For a heap relation, that's a TID;
		 * otherwise we may have a wholerow junk attr that carries the old
		 * tuple in toto.  Keep this in step with the part of
		 * ExecInitModifyTable that sets up ri_RowIdAttNo.
		 */
		if (operation == CMD_UPDATE || operation == CMD_DELETE ||
			operation == CMD_MERGE)
		{
			char		relkind;
			Datum		datum;
			bool		isNull;

			relkind = resultRelInfo->ri_RelationDesc->rd_rel->relkind;
			/*
			 * For YugaByte relations extract the old row from the wholerow junk
			 * attribute if needed.
			 * 1. For tables with secondary indexes we need the (old) ybctid for
			 *    removing old index entries (for UPDATE and DELETE)
			 * 2. For tables with row triggers we need to pass the old row for
			 *    trigger execution.
			 */
			if (IsYBRelation(resultRelInfo->ri_RelationDesc) &&
				(YBCRelInfoHasSecondaryIndices(resultRelInfo) ||
				 YBRelHasOldRowTriggers(resultRelInfo->ri_RelationDesc,
										operation)))
			{
				AttrNumber  resno;
				Plan	   *subplan = outerPlan(node);

				resno = ExecFindJunkAttributeInTlist(subplan->targetlist, "wholerow");
				datum = ExecGetJunkAttribute(slot, resno, &isNull);

				/* shouldn't ever get a null result... */
				if (isNull)
					elog(ERROR, "wholerow is NULL");

				oldtupdata.t_data = DatumGetHeapTupleHeader(datum);
				oldtupdata.t_len =
					HeapTupleHeaderGetDatumLength(oldtupdata.t_data);
				ItemPointerSetInvalid(&(oldtupdata.t_self));
				/* Historically, view triggers see invalid t_tableOid. */
				oldtupdata.t_tableOid =
					(relkind == RELKIND_VIEW) ? InvalidOid :
					RelationGetRelid(resultRelInfo->ri_RelationDesc);

				resno = ExecFindJunkAttributeInTlist(subplan->targetlist, "ybctid");
				datum = ExecGetJunkAttribute(slot, resno, &isNull);

				/* shouldn't ever get a null result... */
				if (isNull)
					elog(ERROR, "ybctid is NULL");

				HEAPTUPLE_YBCTID(&oldtupdata) = datum;

				oldtuple = &oldtupdata;
			}
			else if (relkind == RELKIND_RELATION ||
					 relkind == RELKIND_MATVIEW ||
					 relkind == RELKIND_PARTITIONED_TABLE)
			{
				/* ri_RowIdAttNo refers to a ctid attribute */
				Assert(AttributeNumberIsValid(resultRelInfo->ri_RowIdAttNo));
				datum = ExecGetJunkAttribute(slot,
											 resultRelInfo->ri_RowIdAttNo,
											 &isNull);

				/*
				 * For commands other than MERGE, any tuples having a null row
				 * identifier are errors.  For MERGE, we may need to handle
				 * them as WHEN NOT MATCHED clauses if any, so do that.
				 *
				 * Note that we use the node's toplevel resultRelInfo, not any
				 * specific partition's.
				 */
				if (isNull)
				{
					if (operation == CMD_MERGE)
					{
						EvalPlanQualSetSlot(&node->mt_epqstate, context.planSlot);

						ExecMerge(&context, node->resultRelInfo, NULL, node->canSetTag);
						continue;	/* no RETURNING support yet */
					}

					elog(ERROR, "ctid is NULL");
				}

				tupleid = (ItemPointer) DatumGetPointer(datum);
				tuple_ctid = *tupleid;	/* be sure we don't free ctid!! */
				tupleid = &tuple_ctid;
			}

			/*
			 * Use the wholerow attribute, when available, to reconstruct the
			 * old relation tuple.  The old tuple serves one or both of two
			 * purposes: 1) it serves as the OLD tuple for row triggers, 2) it
			 * provides values for any unchanged columns for the NEW tuple of
			 * an UPDATE, because the subplan does not produce all the columns
			 * of the target table.
			 *
			 * Note that the wholerow attribute does not carry system columns,
			 * so foreign table triggers miss seeing those, except that we
			 * know enough here to set t_tableOid.  Quite separately from
			 * this, the FDW may fetch its own junk attrs to identify the row.
			 *
			 * Other relevant relkinds, currently limited to views, always
			 * have a wholerow attribute.
			 */
			else if (AttributeNumberIsValid(resultRelInfo->ri_RowIdAttNo))
			{
				datum = ExecGetJunkAttribute(slot,
											 resultRelInfo->ri_RowIdAttNo,
											 &isNull);
				/* shouldn't ever get a null result... */
				if (isNull)
					elog(ERROR, "wholerow is NULL");

				oldtupdata.t_data = DatumGetHeapTupleHeader(datum);
				oldtupdata.t_len =
					HeapTupleHeaderGetDatumLength(oldtupdata.t_data);
				ItemPointerSetInvalid(&(oldtupdata.t_self));
				/* Historically, view triggers see invalid t_tableOid. */
				oldtupdata.t_tableOid =
					(relkind == RELKIND_VIEW) ? InvalidOid :
					RelationGetRelid(resultRelInfo->ri_RelationDesc);

				oldtuple = &oldtupdata;
			}
			else
			{
				/* Only foreign tables are allowed to omit a row-ID attr */
				Assert(relkind == RELKIND_FOREIGN_TABLE);
			}
		}

		switch (operation)
		{
			case CMD_INSERT:
				/* Initialize projection info if first time for this table */
				/* YB_TODO(neil@yugabyte) Check to see if this is still needed.
				 *   bool prev_yb_is_single_row_modify_txn =
				 *       estate->es_yb_is_single_row_modify_txn;
				 */

				if (unlikely(!resultRelInfo->ri_projectNewInfoValid))
					ExecInitInsertProjection(node, resultRelInfo);
				slot = ExecGetInsertNewTuple(resultRelInfo, context.planSlot);
				slot = ExecInsert(&context, resultRelInfo, slot,
								  node->canSetTag, NULL, NULL);

				/* YB_TODO(neil) Fixed function signature
				slot = ExecGetInsertNewTuple(resultRelInfo, context.planSlot);
				slot = ExecInsert(node, resultRelInfo, slot, context.planSlot,
								  estate, node->canSetTag);
				*/

				/* YB_TODO(neil@yugabyte)
				 * Work on the optimization for single row insert.
				 */
				/* Revert ExecPrepareTupleRouting's state change. */
				/* estate->es_yb_is_single_row_modify_txn = prev_yb_is_single_row_modify_txn; */
				break;

			case CMD_UPDATE:
				/* Initialize projection info if first time for this table */
				if (unlikely(!resultRelInfo->ri_projectNewInfoValid))
					ExecInitUpdateProjection(node, resultRelInfo);

				/*
				 * Make the new tuple by combining plan's output tuple with
				 * the old tuple being updated.
				 */
				oldSlot = resultRelInfo->ri_oldTupleSlot;
				if (oldtuple != NULL)
				{
					/* Use the wholerow junk attr as the old tuple. */
					ExecForceStoreHeapTuple(oldtuple, oldSlot, false);
				}
				else
				{
					/* Fetch the most recent version of old tuple. */
					Relation	relation = resultRelInfo->ri_RelationDesc;

					if (!table_tuple_fetch_row_version(relation, tupleid,
													   SnapshotAny,
													   oldSlot))
						elog(ERROR, "failed to fetch tuple being updated");
				}
				slot = internalGetUpdateNewTuple(resultRelInfo, context.planSlot,
												 oldSlot, NULL);
				context.GetUpdateNewTuple = internalGetUpdateNewTuple;
				context.relaction = NULL;

				/* Now apply the update. */
				slot = ExecUpdate(&context, resultRelInfo, tupleid, oldtuple,
								  slot, node->canSetTag);
				break;

			case CMD_DELETE:
				slot = ExecDelete(&context, resultRelInfo, tupleid, oldtuple,
								  true, false, node->canSetTag, NULL, NULL);
				break;

			case CMD_MERGE:
				slot = ExecMerge(&context, resultRelInfo, tupleid, node->canSetTag);
				break;

			default:
				elog(ERROR, "unknown operation");
				break;
		}

		/*
		 * If we got a RETURNING result, return it to caller.  We'll continue
		 * the work on next call.
		 */
		if (slot)
			return slot;
	}

	/*
	 * Insert remaining tuples for batch insert.
	 */
	if (estate->es_insert_pending_result_relations != NIL)
		ExecPendingInserts(estate);

	/*
	 * We're done, but fire AFTER STATEMENT triggers before exiting.
	 */
	fireASTriggers(node);

	node->mt_done = true;

	return NULL;
}

/*
 * ExecLookupResultRelByOid
 * 		If the table with given OID is among the result relations to be
 * 		updated by the given ModifyTable node, return its ResultRelInfo.
 *
 * If not found, return NULL if missing_ok, else raise error.
 *
 * If update_cache is true, then upon successful lookup, update the node's
 * one-element cache.  ONLY ExecModifyTable may pass true for this.
 */
ResultRelInfo *
ExecLookupResultRelByOid(ModifyTableState *node, Oid resultoid,
						 bool missing_ok, bool update_cache)
{
	if (node->mt_resultOidHash)
	{
		/* Use the pre-built hash table to locate the rel */
		MTTargetRelLookup *mtlookup;

		mtlookup = (MTTargetRelLookup *)
			hash_search(node->mt_resultOidHash, &resultoid, HASH_FIND, NULL);
		if (mtlookup)
		{
			if (update_cache)
			{
				node->mt_lastResultOid = resultoid;
				node->mt_lastResultIndex = mtlookup->relationIndex;
			}
			return node->resultRelInfo + mtlookup->relationIndex;
		}
	}
	else
	{
		/* With few target rels, just search the ResultRelInfo array */
		for (int ndx = 0; ndx < node->mt_nrels; ndx++)
		{
			ResultRelInfo *rInfo = node->resultRelInfo + ndx;

			if (RelationGetRelid(rInfo->ri_RelationDesc) == resultoid)
			{
				if (update_cache)
				{
					node->mt_lastResultOid = resultoid;
					node->mt_lastResultIndex = ndx;
				}
				return rInfo;
			}
		}
	}

	if (!missing_ok)
		elog(ERROR, "incorrect result relation OID %u", resultoid);
	return NULL;
}

/* ----------------------------------------------------------------
 *		ExecInitModifyTable
 * ----------------------------------------------------------------
 */
ModifyTableState *
ExecInitModifyTable(ModifyTable *node, EState *estate, int eflags)
{
	ModifyTableState *mtstate;
	Plan	   *subplan = outerPlan(node);
	CmdType		operation = node->operation;
	int			nrels = list_length(node->resultRelations);
	ResultRelInfo *resultRelInfo;
	List	   *arowmarks;
	ListCell   *l;
	int			i;
	Relation	rel;

	/* check for unsupported flags */
	Assert(!(eflags & (EXEC_FLAG_BACKWARD | EXEC_FLAG_MARK)));

	/*
	 * create state structure
	 */
	mtstate = makeNode(ModifyTableState);
	mtstate->ps.plan = (Plan *) node;
	mtstate->ps.state = estate;
	mtstate->ps.ExecProcNode = ExecModifyTable;

	mtstate->operation = operation;
	mtstate->canSetTag = node->canSetTag;
	mtstate->mt_done = false;

	mtstate->mt_nrels = nrels;
	mtstate->resultRelInfo = (ResultRelInfo *)
		palloc(nrels * sizeof(ResultRelInfo));

	mtstate->mt_merge_inserted = 0;
	mtstate->mt_merge_updated = 0;
	mtstate->mt_merge_deleted = 0;

	mtstate->yb_fetch_target_tuple = !YbCanSkipFetchingTargetTupleForModifyTable(node);

	/*----------
	 * Resolve the target relation. This is the same as:
	 *
	 * - the relation for which we will fire FOR STATEMENT triggers,
	 * - the relation into whose tuple format all captured transition tuples
	 *   must be converted, and
	 * - the root partitioned table used for tuple routing.
	 *
	 * If it's a partitioned table, the root partition doesn't appear
	 * elsewhere in the plan and its RT index is given explicitly in
	 * node->rootRelation.  Otherwise (i.e. table inheritance) the target
	 * relation is the first relation in the node->resultRelations list.
	 *----------
	 */
	if (node->rootRelation > 0)
	{
		mtstate->rootResultRelInfo = makeNode(ResultRelInfo);
		ExecInitResultRelation(estate, mtstate->rootResultRelInfo,
							   node->rootRelation);
	}
	else
	{
		mtstate->rootResultRelInfo = mtstate->resultRelInfo;
		ExecInitResultRelation(estate, mtstate->resultRelInfo,
							   linitial_int(node->resultRelations));
	}

	/* set up epqstate with dummy subplan data for the moment */
	EvalPlanQualInit(&mtstate->mt_epqstate, estate, NULL, NIL, node->epqParam);
	mtstate->fireBSTriggers = true;

	/*
	 * Build state for collecting transition tuples.  This requires having a
	 * valid trigger query context, so skip it in explain-only mode.
	 */
	if (!(eflags & EXEC_FLAG_EXPLAIN_ONLY))
		ExecSetupTransitionCaptureState(mtstate, estate);

	/*
	 * Open all the result relations and initialize the ResultRelInfo structs.
	 * (But root relation was initialized above, if it's part of the array.)
	 * We must do this before initializing the subplan, because direct-modify
	 * FDWs expect their ResultRelInfos to be available.
	 */
	resultRelInfo = mtstate->resultRelInfo;
	i = 0;
	foreach(l, node->resultRelations)
	{
		Index		resultRelation = lfirst_int(l);

		if (resultRelInfo != mtstate->rootResultRelInfo)
		{
			ExecInitResultRelation(estate, resultRelInfo, resultRelation);

			/*
			 * For child result relations, store the root result relation
			 * pointer.  We do so for the convenience of places that want to
			 * look at the query's original target relation but don't have the
			 * mtstate handy.
			 */
			resultRelInfo->ri_RootResultRelInfo = mtstate->rootResultRelInfo;
		}

		/* Initialize the usesFdwDirectModify flag */
		resultRelInfo->ri_usesFdwDirectModify =
			bms_is_member(i, node->fdwDirectModifyPlans);

		/*
		 * Verify result relation is a valid target for the current operation
		 */
		CheckValidResultRel(resultRelInfo, operation);

		resultRelInfo++;
		i++;
	}

	/*
	 * Now we may initialize the subplan.
	 */
	outerPlanState(mtstate) = ExecInitNode(subplan, estate, eflags);

	/*
	 * Do additional per-result-relation initialization.
	 */
	for (i = 0; i < nrels; i++)
	{
		resultRelInfo = &mtstate->resultRelInfo[i];

		/* Let FDWs init themselves for foreign-table result rels */
		if (!resultRelInfo->ri_usesFdwDirectModify &&
			resultRelInfo->ri_FdwRoutine != NULL &&
			resultRelInfo->ri_FdwRoutine->BeginForeignModify != NULL)
		{
			List	   *fdw_private = (List *) list_nth(node->fdwPrivLists, i);

			resultRelInfo->ri_FdwRoutine->BeginForeignModify(mtstate,
															 resultRelInfo,
															 fdw_private,
															 i,
															 eflags);
		}

		/*
		 * For UPDATE/DELETE/MERGE, find the appropriate junk attr now, either
		 * a 'ctid' or 'wholerow' attribute depending on relkind.  For foreign
		 * tables, the FDW might have created additional junk attr(s), but
		 * those are no concern of ours.
		 */
		if (operation == CMD_UPDATE || operation == CMD_DELETE ||
			operation == CMD_MERGE)
		{
			char		relkind;

			relkind = resultRelInfo->ri_RelationDesc->rd_rel->relkind;
			if (IsYBRelation(resultRelInfo->ri_RelationDesc))
			{
				if (!mtstate->yb_fetch_target_tuple) {
					resultRelInfo->ri_RowIdAttNo =
						ExecFindJunkAttributeInTlist(subplan->targetlist, "ybctid");
					if (!AttributeNumberIsValid(resultRelInfo->ri_RowIdAttNo))
						elog(ERROR, "could not find junk ybctid column");
				}
			}
			else if (relkind == RELKIND_RELATION ||
				relkind == RELKIND_MATVIEW ||
				relkind == RELKIND_PARTITIONED_TABLE)
			{
				resultRelInfo->ri_RowIdAttNo =
					ExecFindJunkAttributeInTlist(subplan->targetlist, "ctid");
				if (!AttributeNumberIsValid(resultRelInfo->ri_RowIdAttNo))
					elog(ERROR, "could not find junk ctid column");
			}
			else if (relkind == RELKIND_FOREIGN_TABLE)
			{
				/*
				 * We don't support MERGE with foreign tables for now.  (It's
				 * problematic because the implementation uses CTID.)
				 */
				Assert(operation != CMD_MERGE);

				/*
				 * When there is a row-level trigger, there should be a
				 * wholerow attribute.  We also require it to be present in
				 * UPDATE and MERGE, so we can get the values of unchanged
				 * columns.
				 */
				resultRelInfo->ri_RowIdAttNo =
					ExecFindJunkAttributeInTlist(subplan->targetlist,
												 "wholerow");
				if ((mtstate->operation == CMD_UPDATE || mtstate->operation == CMD_MERGE) &&
					!AttributeNumberIsValid(resultRelInfo->ri_RowIdAttNo))
					elog(ERROR, "could not find junk wholerow column");
			}
			else
			{
				/* No support for MERGE */
				Assert(operation != CMD_MERGE);
				/* Other valid target relkinds must provide wholerow */
				resultRelInfo->ri_RowIdAttNo =
					ExecFindJunkAttributeInTlist(subplan->targetlist,
												 "wholerow");
				if (!AttributeNumberIsValid(resultRelInfo->ri_RowIdAttNo))
					elog(ERROR, "could not find junk wholerow column");
			}
		}

		/*
		 * For INSERT/UPDATE/MERGE, prepare to evaluate any generated columns.
		 * We must do this now, even if we never insert or update any rows,
		 * because we have to fill resultRelInfo->ri_extraUpdatedCols for
		 * possible use by the trigger machinery.
		 */
		if (operation == CMD_INSERT || operation == CMD_UPDATE || operation == CMD_MERGE)
			ExecInitStoredGenerated(resultRelInfo, estate, operation);
	}

	/*
	 * If this is an inherited update/delete/merge, there will be a junk
	 * attribute named "tableoid" present in the subplan's targetlist.  It
	 * will be used to identify the result relation for a given tuple to be
	 * updated/deleted/merged.
	 */
	mtstate->mt_resultOidAttno =
		ExecFindJunkAttributeInTlist(subplan->targetlist, "tableoid");
	Assert(AttributeNumberIsValid(mtstate->mt_resultOidAttno) || nrels == 1);
	mtstate->mt_lastResultOid = InvalidOid; /* force lookup at first tuple */
	mtstate->mt_lastResultIndex = 0;	/* must be zero if no such attr */

	/* Get the root target relation */
	rel = mtstate->rootResultRelInfo->ri_RelationDesc;

	/*
	 * Build state for tuple routing if it's a partitioned INSERT.  An UPDATE
	 * or MERGE might need this too, but only if it actually moves tuples
	 * between partitions; in that case setup is done by
	 * ExecCrossPartitionUpdate.
	 */
	if (rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE &&
		operation == CMD_INSERT)
		mtstate->mt_partition_tuple_routing =
			ExecSetupPartitionTupleRouting(estate, rel);

	/*
	 * Initialize any WITH CHECK OPTION constraints if needed.
	 */
	resultRelInfo = mtstate->resultRelInfo;
	foreach(l, node->withCheckOptionLists)
	{
		List	   *wcoList = (List *) lfirst(l);
		List	   *wcoExprs = NIL;
		ListCell   *ll;

		foreach(ll, wcoList)
		{
			WithCheckOption *wco = (WithCheckOption *) lfirst(ll);
			ExprState  *wcoExpr = ExecInitQual((List *) wco->qual,
											   &mtstate->ps);

			wcoExprs = lappend(wcoExprs, wcoExpr);
		}

		resultRelInfo->ri_WithCheckOptions = wcoList;
		resultRelInfo->ri_WithCheckOptionExprs = wcoExprs;
		resultRelInfo++;
	}

	/*
	 * Initialize RETURNING projections if needed.
	 */
	if (node->returningLists)
	{
		TupleTableSlot *slot;
		ExprContext *econtext;

		/*
		 * Initialize result tuple slot and assign its rowtype using the first
		 * RETURNING list.  We assume the rest will look the same.
		 */
		mtstate->ps.plan->targetlist = (List *) linitial(node->returningLists);

		/* Set up a slot for the output of the RETURNING projection(s) */
		ExecInitResultTupleSlotTL(&mtstate->ps, &TTSOpsVirtual);
		slot = mtstate->ps.ps_ResultTupleSlot;

		/* Need an econtext too */
		if (mtstate->ps.ps_ExprContext == NULL)
			ExecAssignExprContext(estate, &mtstate->ps);
		econtext = mtstate->ps.ps_ExprContext;

		/*
		 * Build a projection for each result rel.
		 */
		resultRelInfo = mtstate->resultRelInfo;
		foreach(l, node->returningLists)
		{
			List	   *rlist = (List *) lfirst(l);

			resultRelInfo->ri_returningList = rlist;
			resultRelInfo->ri_projectReturning =
				ExecBuildProjectionInfo(rlist, econtext, slot, &mtstate->ps,
										resultRelInfo->ri_RelationDesc->rd_att);
			resultRelInfo++;
		}
	}
	else
	{
		/*
		 * We still must construct a dummy result tuple type, because InitPlan
		 * expects one (maybe should change that?).
		 */
		mtstate->ps.plan->targetlist = NIL;
		ExecInitResultTypeTL(&mtstate->ps);

		mtstate->ps.ps_ExprContext = NULL;
	}

	/* Set the list of arbiter indexes if needed for ON CONFLICT */
	resultRelInfo = mtstate->resultRelInfo;
	if (node->onConflictAction != ONCONFLICT_NONE)
	{
		/* insert may only have one relation, inheritance is not expanded */
		Assert(nrels == 1);
		resultRelInfo->ri_onConflictArbiterIndexes = node->arbiterIndexes;
	}

	/*
	 * If needed, Initialize target list, projection and qual for ON CONFLICT
	 * DO UPDATE.
	 */
	if (node->onConflictAction == ONCONFLICT_UPDATE)
	{
		OnConflictSetState *onconfl = makeNode(OnConflictSetState);
		ExprContext *econtext;
		TupleDesc	relationDesc;

		/* already exists if created by RETURNING processing above */
		if (mtstate->ps.ps_ExprContext == NULL)
			ExecAssignExprContext(estate, &mtstate->ps);

		econtext = mtstate->ps.ps_ExprContext;
		relationDesc = resultRelInfo->ri_RelationDesc->rd_att;

		/* create state for DO UPDATE SET operation */
		resultRelInfo->ri_onConflict = onconfl;

		/* initialize slot for the existing tuple */
		onconfl->oc_Existing =
			table_slot_create(resultRelInfo->ri_RelationDesc,
							  &mtstate->ps.state->es_tupleTable);

		/*
		 * Create the tuple slot for the UPDATE SET projection. We want a slot
		 * of the table's type here, because the slot will be used to insert
		 * into the table, and for RETURNING processing - which may access
		 * system attributes.
		 */
		onconfl->oc_ProjSlot =
			table_slot_create(resultRelInfo->ri_RelationDesc,
							  &mtstate->ps.state->es_tupleTable);

		/* build UPDATE SET projection state */
		onconfl->oc_ProjInfo =
			ExecBuildUpdateProjection(node->onConflictSet,
									  true,
									  node->onConflictCols,
									  relationDesc,
									  econtext,
									  onconfl->oc_ProjSlot,
									  &mtstate->ps);

		/* initialize state to evaluate the WHERE clause, if any */
		if (node->onConflictWhere)
		{
			ExprState  *qualexpr;

			qualexpr = ExecInitQual((List *) node->onConflictWhere,
									&mtstate->ps);
			onconfl->oc_WhereClause = qualexpr;
		}
	}

	/*
	 * If we have any secondary relations in an UPDATE or DELETE, they need to
	 * be treated like non-locked relations in SELECT FOR UPDATE, ie, the
	 * EvalPlanQual mechanism needs to be told about them.  Locate the
	 * relevant ExecRowMarks.
	 */
	arowmarks = NIL;
	foreach(l, node->rowMarks)
	{
		PlanRowMark *rc = lfirst_node(PlanRowMark, l);
		ExecRowMark *erm;
		ExecAuxRowMark *aerm;

		/* ignore "parent" rowmarks; they are irrelevant at runtime */
		if (rc->isParent)
			continue;

		/* Find ExecRowMark and build ExecAuxRowMark */
		erm = ExecFindRowMark(estate, rc->rti, false);
		aerm = ExecBuildAuxRowMark(erm, subplan->targetlist);
		arowmarks = lappend(arowmarks, aerm);
	}

	/* For a MERGE command, initialize its state */
	if (mtstate->operation == CMD_MERGE)
		ExecInitMerge(mtstate, estate);

	EvalPlanQualSetPlan(&mtstate->mt_epqstate, subplan, arowmarks);

	/*
	 * If there are a lot of result relations, use a hash table to speed the
	 * lookups.  If there are not a lot, a simple linear search is faster.
	 *
	 * It's not clear where the threshold is, but try 64 for starters.  In a
	 * debugging build, use a small threshold so that we get some test
	 * coverage of both code paths.
	 */
#ifdef USE_ASSERT_CHECKING
#define MT_NRELS_HASH 4
#else
#define MT_NRELS_HASH 64
#endif
	if (nrels >= MT_NRELS_HASH)
	{
		HASHCTL		hash_ctl;

		hash_ctl.keysize = sizeof(Oid);
		hash_ctl.entrysize = sizeof(MTTargetRelLookup);
		hash_ctl.hcxt = CurrentMemoryContext;
		mtstate->mt_resultOidHash =
			hash_create("ModifyTable target hash",
						nrels, &hash_ctl,
						HASH_ELEM | HASH_BLOBS | HASH_CONTEXT);
		for (i = 0; i < nrels; i++)
		{
			Oid			hashkey;
			MTTargetRelLookup *mtlookup;
			bool		found;

			resultRelInfo = &mtstate->resultRelInfo[i];
			hashkey = RelationGetRelid(resultRelInfo->ri_RelationDesc);
			mtlookup = (MTTargetRelLookup *)
				hash_search(mtstate->mt_resultOidHash, &hashkey,
							HASH_ENTER, &found);
			Assert(!found);
			mtlookup->relationIndex = i;
		}
	}
	else
		mtstate->mt_resultOidHash = NULL;

	/*
	 * Determine if the FDW supports batch insert and determine the batch size
	 * (a FDW may support batching, but it may be disabled for the
	 * server/table).
	 *
	 * We only do this for INSERT, so that for UPDATE/DELETE the batch size
	 * remains set to 0.
	 */
	if (operation == CMD_INSERT)
	{
		/* insert may only have one relation, inheritance is not expanded */
		Assert(nrels == 1);
		resultRelInfo = mtstate->resultRelInfo;
		if (!resultRelInfo->ri_usesFdwDirectModify &&
			resultRelInfo->ri_FdwRoutine != NULL &&
			resultRelInfo->ri_FdwRoutine->GetForeignModifyBatchSize &&
			resultRelInfo->ri_FdwRoutine->ExecForeignBatchInsert)
		{
			resultRelInfo->ri_BatchSize =
				resultRelInfo->ri_FdwRoutine->GetForeignModifyBatchSize(resultRelInfo);
			Assert(resultRelInfo->ri_BatchSize >= 1);
		}
		else
			resultRelInfo->ri_BatchSize = 1;
	}
#ifdef YB_TODO
	else
	{
		/* YB_TODO(neil@yugabyte)
		 * Pg13 reimplement junk filter. We need to change this code accordingly.
		 *
		 * If it's a YB single row UPDATE/DELETE we do not perform an
		 * initial scan to populate the ybctid, so there is no junk
		 * attribute to extract.
		 */
		if (IsYBRelation(mtstate->resultRelInfo->ri_RelationDesc))
		{
			junk_filter_needed = mtstate->yb_fetch_target_tuple;
		}
		else
		{
			junk_filter_needed = true;
		}
	}
#endif

	/*
	 * Lastly, if this is not the primary (canSetTag) ModifyTable node, add it
	 * to estate->es_auxmodifytables so that it will be run to completion by
	 * ExecPostprocessPlan.  (It'd actually work fine to add the primary
	 * ModifyTable node too, but there's no need.)  Note the use of lcons not
	 * lappend: we need later-initialized ModifyTable nodes to be shut down
	 * before earlier ones.  This ensures that we don't throw away RETURNING
	 * rows that need to be seen by a later CTE subplan.
	 */
	if (!mtstate->canSetTag)
		estate->es_auxmodifytables = lcons(mtstate,
										   estate->es_auxmodifytables);

	return mtstate;
}

/* ----------------------------------------------------------------
 *		ExecEndModifyTable
 *
 *		Shuts down the plan.
 *
 *		Returns nothing of interest.
 * ----------------------------------------------------------------
 */
void
ExecEndModifyTable(ModifyTableState *node)
{
	int			i;

	/*
	 * Allow any FDWs to shut down
	 */
	for (i = 0; i < node->mt_nrels; i++)
	{
		int			j;
		ResultRelInfo *resultRelInfo = node->resultRelInfo + i;

		if (!resultRelInfo->ri_usesFdwDirectModify &&
			resultRelInfo->ri_FdwRoutine != NULL &&
			resultRelInfo->ri_FdwRoutine->EndForeignModify != NULL)
			resultRelInfo->ri_FdwRoutine->EndForeignModify(node->ps.state,
														   resultRelInfo);

		/*
		 * Cleanup the initialized batch slots. This only matters for FDWs
		 * with batching, but the other cases will have ri_NumSlotsInitialized
		 * == 0.
		 */
		for (j = 0; j < resultRelInfo->ri_NumSlotsInitialized; j++)
		{
			ExecDropSingleTupleTableSlot(resultRelInfo->ri_Slots[j]);
			ExecDropSingleTupleTableSlot(resultRelInfo->ri_PlanSlots[j]);
		}
	}

	/*
	 * Close all the partitioned tables, leaf partitions, and their indices
	 * and release the slot used for tuple routing, if set.
	 */
	if (node->mt_partition_tuple_routing)
	{
		ExecCleanupTupleRouting(node, node->mt_partition_tuple_routing);

		if (node->mt_root_tuple_slot)
			ExecDropSingleTupleTableSlot(node->mt_root_tuple_slot);
	}

	/*
	 * Free the exprcontext
	 */
	ExecFreeExprContext(&node->ps);

	/*
	 * clean out the tuple table
	 */
	if (node->ps.ps_ResultTupleSlot)
		ExecClearTuple(node->ps.ps_ResultTupleSlot);

	/*
	 * Terminate EPQ execution if active
	 */
	EvalPlanQualEnd(&node->mt_epqstate);

	/*
	 * shut down subplan
	 */
	ExecEndNode(outerPlanState(node));
}

void
ExecReScanModifyTable(ModifyTableState *node)
{
	/*
	 * Currently, we don't need to support rescan on ModifyTable nodes. The
	 * semantics of that would be a bit debatable anyway.
	 */
	elog(ERROR, "ExecReScanModifyTable is not implemented");
}

/*
 * Should be called after INSERT/UPDATE/DELETE has been processed.
 * For DELETE, both desc and newtup will be NULL.
 */
static void YbPostProcessDml(CmdType cmd_type,
							 Relation rel,
							 TupleDesc desc,
							 HeapTuple newtup)
{
	if (!IsYBRelation(rel) || IsBootstrapProcessingMode())
		return; /* Nothing to do*/

	if (!YbIsSystemCatalogChange(rel))
		return; /* We only care about system table changes here. */

	/* This routine is for a very specific set of DMLs. */
	Assert(cmd_type != CMD_INSERT ||
		   cmd_type != CMD_UPDATE ||
		   cmd_type != CMD_DELETE);

	/*
	 * TODO(alex, myang):
	 *   Mark system catalogs as directly modified so that we know to increment
	 *   catalog version. Handle shared table modification as well!
	 */

	/*
	 * Update pinned objects cache if pg_depend/pg_shdepend was modified.
	 */
	bool is_shared_dep;

	if (RelationGetRelid(rel) == DependRelationId)
		is_shared_dep = false;
	else if (RelationGetRelid(rel) == SharedDependRelationId)
		is_shared_dep = true;
	else
		return; /* Nothing more to do */

	if (cmd_type == CMD_INSERT)
	{
		YbHandlePossibleObjectPinning(desc, newtup, is_shared_dep);
	}
	else
	{
		/*
		 * In YB, we do not fetch the deleted tuple content and thus we don't
		 * know what exactly was deleted.
		 * As a workaround, we invalidate the cache as a whole.
		 * For simplicity, we do the same for UPDATE.
		 */
		 YbResetPinnedCache();
	}
}

/*
 * When inserting a new value into pg_depend or pg_shdepend, it should be reflected in the
 * YB pinned object cache.
 * This function takes care of that.
 */
static void
YbHandlePossibleObjectPinning(TupleDesc desc,
							  HeapTuple tuple,
							  bool is_shared_dep)
{
	Assert(tuple != NULL);

	int attnum_deptype = is_shared_dep ? Anum_pg_shdepend_deptype
									   : Anum_pg_depend_deptype;
	int attnum_refclassid = is_shared_dep ? Anum_pg_shdepend_refclassid
										  : Anum_pg_depend_refclassid;
	int attnum_refobjid = is_shared_dep ? Anum_pg_shdepend_refobjid
										: Anum_pg_depend_refobjid;
#ifdef YB_TODO
	/* Pg15  No longer use pin */
	char pin_deptype = is_shared_dep ? SHARED_DEPENDENCY_PIN
									 : DEPENDENCY_PIN;
#else
	char pin_deptype = DEPENDENCY_PARTITION_PRI;
#endif

	bool is_null; /* Can never really be null. */

	char deptype = DatumGetChar(
		heap_getattr(tuple, attnum_deptype, desc, &is_null));
	Assert(!is_null);

	if (deptype != pin_deptype)
		return; /* Nothing to do here. */

	Oid refclassid = DatumGetObjectId(
		heap_getattr(tuple, attnum_refclassid, desc, &is_null));
	Assert(!is_null);

	Oid refobjid = DatumGetObjectId(
		heap_getattr(tuple, attnum_refobjid, desc, &is_null));
	Assert(!is_null);

	YbPinObjectIfNeeded(refclassid, refobjid, is_shared_dep);
}
