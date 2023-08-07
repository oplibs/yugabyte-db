/*-------------------------------------------------------------------------
 *
 * executor.h
 *	  support for the POSTGRES executor module
 *
 *
 * Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/executor/executor.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "executor/execdesc.h"
#include "executor/execPartition.h"
#include "nodes/parsenodes.h"
#include "utils/memutils.h"


/*
 * The "eflags" argument to ExecutorStart and the various ExecInitNode
 * routines is a bitwise OR of the following flag bits, which tell the
 * called plan node what to expect.  Note that the flags will get modified
 * as they are passed down the plan tree, since an upper node may require
 * functionality in its subnode not demanded of the plan as a whole
 * (example: MergeJoin requires mark/restore capability in its inner input),
 * or an upper node may shield its input from some functionality requirement
 * (example: Materialize shields its input from needing to do backward scan).
 *
 * EXPLAIN_ONLY indicates that the plan tree is being initialized just so
 * EXPLAIN can print it out; it will not be run.  Hence, no side-effects
 * of startup should occur.  However, error checks (such as permission checks)
 * should be performed.
 *
 * REWIND indicates that the plan node should try to efficiently support
 * rescans without parameter changes.  (Nodes must support ExecReScan calls
 * in any case, but if this flag was not given, they are at liberty to do it
 * through complete recalculation.  Note that a parameter change forces a
 * full recalculation in any case.)
 *
 * BACKWARD indicates that the plan node must respect the es_direction flag.
 * When this is not passed, the plan node will only be run forwards.
 *
 * MARK indicates that the plan node must support Mark/Restore calls.
 * When this is not passed, no Mark/Restore will occur.
 *
 * SKIP_TRIGGERS tells ExecutorStart/ExecutorFinish to skip calling
 * AfterTriggerBeginQuery/AfterTriggerEndQuery.  This does not necessarily
 * mean that the plan can't queue any AFTER triggers; just that the caller
 * is responsible for there being a trigger context for them to be queued in.
 *
 * WITH/WITHOUT_OIDS tell the executor to emit tuples with or without space
 * for OIDs, respectively.  These are currently used only for CREATE TABLE AS.
 * If neither is set, the plan may or may not produce tuples including OIDs.
 *
 * YB_AGG_PARENT tells the plan node that the parent node is an Agg node.  This
 * context is used to signal the plan node to make an extra effort to determine
 * whether aggregates can be pushed down.  It should only be set for plan nodes
 * that have no children, such as IndexOnlyScan.
 */
#define EXEC_FLAG_EXPLAIN_ONLY	0x0001	/* EXPLAIN, no ANALYZE */
#define EXEC_FLAG_REWIND		0x0002	/* need efficient rescan */
#define EXEC_FLAG_BACKWARD		0x0004	/* need backward scan */
#define EXEC_FLAG_MARK			0x0008	/* need mark/restore */
#define EXEC_FLAG_SKIP_TRIGGERS 0x0010	/* skip AfterTrigger calls */
#define EXEC_FLAG_WITH_OIDS		0x0020	/* force OIDs in returned tuples */
#define EXEC_FLAG_WITHOUT_OIDS	0x0040	/* force no OIDs in returned tuples */
#define EXEC_FLAG_WITH_NO_DATA	0x0080	/* rel scannability doesn't matter */

#define EXEC_FLAG_YB_AGG_PARENT	0x8000	/* parent node is Agg */


/* Hook for plugins to get control in ExecutorStart() */
typedef void (*ExecutorStart_hook_type) (QueryDesc *queryDesc, int eflags);
extern PGDLLIMPORT ExecutorStart_hook_type ExecutorStart_hook;

/* Hook for plugins to get control in ExecutorRun() */
typedef void (*ExecutorRun_hook_type) (QueryDesc *queryDesc,
									   ScanDirection direction,
									   uint64 count,
									   bool execute_once);
extern PGDLLIMPORT ExecutorRun_hook_type ExecutorRun_hook;

/* Hook for plugins to get control in ExecutorFinish() */
typedef void (*ExecutorFinish_hook_type) (QueryDesc *queryDesc);
extern PGDLLIMPORT ExecutorFinish_hook_type ExecutorFinish_hook;

/* Hook for plugins to get control in ExecutorEnd() */
typedef void (*ExecutorEnd_hook_type) (QueryDesc *queryDesc);
extern PGDLLIMPORT ExecutorEnd_hook_type ExecutorEnd_hook;

/* Hook for plugins to get control in ExecCheckRTPerms() */
typedef bool (*ExecutorCheckPerms_hook_type) (List *, bool);
extern PGDLLIMPORT ExecutorCheckPerms_hook_type ExecutorCheckPerms_hook;


/*
 * prototypes from functions in execAmi.c
 */
struct Path;					/* avoid including relation.h here */

extern void ExecReScan(PlanState *node);
extern void ExecMarkPos(PlanState *node);
extern void ExecRestrPos(PlanState *node);
extern bool ExecSupportsMarkRestore(struct Path *pathnode);
extern bool ExecSupportsBackwardScan(Plan *node);
extern bool ExecMaterializesOutput(NodeTag plantype);

/*
 * prototypes from functions in execCurrent.c
 */
extern bool execCurrentOf(CurrentOfExpr *cexpr,
			  ExprContext *econtext,
			  Oid table_oid,
			  ItemPointer current_tid);

/*
 * prototypes from functions in execGrouping.c
 */
extern ExprState *execTuplesMatchPrepare(TupleDesc desc,
					   int numCols,
					   AttrNumber *keyColIdx,
					   Oid *eqOperators,
					   PlanState *parent);
extern void execTuplesHashPrepare(int numCols,
					  Oid *eqOperators,
					  Oid **eqFuncOids,
					  FmgrInfo **hashFunctions);
extern ExprState *ybPrepareOuterExprsEqualFn(List *outer_exprs,
				Oid *eqOps, PlanState *parent);
extern TupleHashTable BuildTupleHashTable(PlanState *parent,
					TupleDesc inputDesc,
					int numCols, AttrNumber *keyColIdx,
					Oid *eqfuncoids,
					FmgrInfo *hashfunctions,
					long nbuckets, Size additionalsize,
					MemoryContext tablecxt,
					MemoryContext tempcxt, bool use_variable_hash_iv);
extern TupleHashTable YbBuildTupleHashTableExt(PlanState *parent,
						 TupleDesc inputDesc,
						 int numCols, ExprState **keyColExprs,
						 ExprState *eqExpr,
						 Oid *eqfuncoids,
						 FmgrInfo *hashfunctions,
						 long nbuckets, Size additionalsize,
						 MemoryContext metacxt,
						 MemoryContext tablecxt,
						 MemoryContext tempcxt,
						 ExprContext *expr_cxt,
						 bool use_variable_hash_iv);
extern TupleHashTable BuildTupleHashTableExt(PlanState *parent,
					TupleDesc inputDesc,
					int numCols, AttrNumber *keyColIdx,
					Oid *eqfuncoids,
					FmgrInfo *hashfunctions,
					long nbuckets, Size additionalsize,
					MemoryContext metacxt,
					MemoryContext tablecxt,
					MemoryContext tempcxt, bool use_variable_hash_iv);
extern TupleHashEntry LookupTupleHashEntry(TupleHashTable hashtable,
					 TupleTableSlot *slot,
					 bool *isnew);
extern TupleHashEntry FindTupleHashEntry(TupleHashTable hashtable,
				   TupleTableSlot *slot,
				   ExprState *eqcomp,
				   FmgrInfo *hashfunctions,
				   AttrNumber *keyColIdx);
extern void ResetTupleHashTable(TupleHashTable hashtable);

/*
 * prototypes from functions in execJunk.c
 */
extern JunkFilter *ExecInitJunkFilter(List *targetList, bool hasoid,
				   TupleTableSlot *slot);
extern JunkFilter *ExecInitJunkFilterConversion(List *targetList,
							 TupleDesc cleanTupType,
							 TupleTableSlot *slot);
extern AttrNumber ExecFindJunkAttribute(JunkFilter *junkfilter,
					  const char *attrName);
extern AttrNumber ExecFindJunkAttributeInTlist(List *targetlist,
							 const char *attrName);
extern Datum ExecGetJunkAttribute(TupleTableSlot *slot, AttrNumber attno,
					 bool *isNull);
extern TupleTableSlot *ExecFilterJunk(JunkFilter *junkfilter,
			   TupleTableSlot *slot);


/*
 * prototypes from functions in execMain.c
 */
extern void ExecutorStart(QueryDesc *queryDesc, int eflags);
extern void standard_ExecutorStart(QueryDesc *queryDesc, int eflags);
extern void ExecutorRun(QueryDesc *queryDesc,
			ScanDirection direction, uint64 count, bool execute_once);
extern void standard_ExecutorRun(QueryDesc *queryDesc,
					 ScanDirection direction, uint64 count, bool execute_once);
extern void ExecutorFinish(QueryDesc *queryDesc);
extern void standard_ExecutorFinish(QueryDesc *queryDesc);
extern void ExecutorEnd(QueryDesc *queryDesc);
extern void standard_ExecutorEnd(QueryDesc *queryDesc);
extern void ExecutorRewind(QueryDesc *queryDesc);
extern bool ExecCheckRTPerms(List *rangeTable, bool ereport_on_violation);
extern void CheckValidResultRel(ResultRelInfo *resultRelInfo, CmdType operation);
extern void InitResultRelInfo(ResultRelInfo *resultRelInfo,
				  Relation resultRelationDesc,
				  Index resultRelationIndex,
				  ResultRelInfo *partition_root_rri,
				  int instrument_options);
extern ResultRelInfo *ExecGetTriggerResultRel(EState *estate, Oid relid);
extern void ExecCleanUpTriggerState(EState *estate);
extern bool ExecContextForcesOids(PlanState *planstate, bool *hasoids);
extern void ExecConstraints(ResultRelInfo *resultRelInfo,
                            TupleTableSlot *slot,
                            EState *estate,
                            ModifyTableState *mtstate);
extern bool ExecPartitionCheck(ResultRelInfo *resultRelInfo,
				   TupleTableSlot *slot, EState *estate, bool emitError);
extern void ExecPartitionCheckEmitError(ResultRelInfo *resultRelInfo,
							TupleTableSlot *slot, EState *estate);
extern void ExecWithCheckOptions(WCOKind kind, ResultRelInfo *resultRelInfo,
					 TupleTableSlot *slot, EState *estate);
extern LockTupleMode ExecUpdateLockMode(EState *estate, ResultRelInfo *relinfo);
extern ExecRowMark *ExecFindRowMark(EState *estate, Index rti, bool missing_ok);
extern ExecAuxRowMark *ExecBuildAuxRowMark(ExecRowMark *erm, List *targetlist);
extern TupleTableSlot *EvalPlanQual(EState *estate, EPQState *epqstate,
			 Relation relation, Index rti, int lockmode,
			 ItemPointer tid, TransactionId priorXmax);
extern HeapTuple EvalPlanQualFetch(EState *estate, Relation relation,
				  int lockmode, LockWaitPolicy wait_policy, ItemPointer tid,
				  TransactionId priorXmax);
extern void EvalPlanQualInit(EPQState *epqstate, EState *estate,
				 Plan *subplan, List *auxrowmarks, int epqParam);
extern void EvalPlanQualSetPlan(EPQState *epqstate,
					Plan *subplan, List *auxrowmarks);
extern void EvalPlanQualSetTuple(EPQState *epqstate, Index rti,
					 HeapTuple tuple);
extern HeapTuple EvalPlanQualGetTuple(EPQState *epqstate, Index rti);

#define EvalPlanQualSetSlot(epqstate, slot)  ((epqstate)->origslot = (slot))
extern void EvalPlanQualFetchRowMarks(EPQState *epqstate);
extern TupleTableSlot *EvalPlanQualNext(EPQState *epqstate);
extern void EvalPlanQualBegin(EPQState *epqstate, EState *parentestate);
extern void EvalPlanQualEnd(EPQState *epqstate);

/*
 * functions in execProcnode.c
 */
extern PlanState *ExecInitNode(Plan *node, EState *estate, int eflags);
extern void ExecSetExecProcNode(PlanState *node, ExecProcNodeMtd function);
extern Node *MultiExecProcNode(PlanState *node);
extern void ExecEndNode(PlanState *node);
extern bool ExecShutdownNode(PlanState *node);
extern void ExecSetTupleBound(int64 tuples_needed, PlanState *child_node);


/* ----------------------------------------------------------------
 *		ExecProcNode
 *
 *		Execute the given node to return a(nother) tuple.
 * ----------------------------------------------------------------
 */
#ifndef FRONTEND
static inline TupleTableSlot *
ExecProcNode(PlanState *node)
{
	if (node->chgParam != NULL) /* something changed? */
		ExecReScan(node);		/* let ReScan handle this */

	return node->ExecProcNode(node);
}
#endif

/*
 * prototypes from functions in execExpr.c
 */
extern ExprState *ExecInitExpr(Expr *node, PlanState *parent);
extern ExprState *ExecInitExprWithParams(Expr *node, ParamListInfo ext_params);
extern ExprState *ExecInitQual(List *qual, PlanState *parent);
extern ExprState *ExecInitCheck(List *qual, PlanState *parent);
extern List *ExecInitExprList(List *nodes, PlanState *parent);
extern ExprState *ExecBuildAggTrans(AggState *aggstate, struct AggStatePerPhaseData *phase,
				  bool doSort, bool doHash);
extern ExprState *ExecBuildGroupingEqual(TupleDesc ldesc, TupleDesc rdesc,
					   int numCols,
					   AttrNumber *keyColIdx,
					   Oid *eqfunctions,
					   PlanState *parent);
extern ProjectionInfo *ExecBuildProjectionInfo(List *targetList,
						ExprContext *econtext,
						TupleTableSlot *slot,
						PlanState *parent,
						TupleDesc inputDesc);
extern ProjectionInfo *ExecBuildProjectionInfoExt(List *targetList,
												  ExprContext *econtext,
												  TupleTableSlot *slot,
												  bool assignJunkEntries,
												  PlanState *parent,
												  TupleDesc inputDesc);
extern ExprState *ExecPrepareExpr(Expr *node, EState *estate);
extern ExprState *ExecPrepareQual(List *qual, EState *estate);
extern ExprState *ExecPrepareCheck(List *qual, EState *estate);
extern List *ExecPrepareExprList(List *nodes, EState *estate);

/*
 * ExecEvalExpr
 *
 * Evaluate expression identified by "state" in the execution context
 * given by "econtext".  *isNull is set to the is-null flag for the result,
 * and the Datum value is the function result.
 *
 * The caller should already have switched into the temporary memory
 * context econtext->ecxt_per_tuple_memory.  The convenience entry point
 * ExecEvalExprSwitchContext() is provided for callers who don't prefer to
 * do the switch in an outer loop.
 */
#ifndef FRONTEND
static inline Datum
ExecEvalExpr(ExprState *state,
			 ExprContext *econtext,
			 bool *isNull)
{
	return state->evalfunc(state, econtext, isNull);
}
#endif

/*
 * ExecEvalExprSwitchContext
 *
 * Same as ExecEvalExpr, but get into the right allocation context explicitly.
 */
#ifndef FRONTEND
static inline Datum
ExecEvalExprSwitchContext(ExprState *state,
						  ExprContext *econtext,
						  bool *isNull)
{
	Datum		retDatum;
	MemoryContext oldContext;

	oldContext = MemoryContextSwitchTo(econtext->ecxt_per_tuple_memory);
	retDatum = state->evalfunc(state, econtext, isNull);
	MemoryContextSwitchTo(oldContext);
	return retDatum;
}
#endif

/*
 * ExecProject
 *
 * Projects a tuple based on projection info and stores it in the slot passed
 * to ExecBuildProjectInfo().
 *
 * Note: the result is always a virtual tuple; therefore it may reference
 * the contents of the exprContext's scan tuples and/or temporary results
 * constructed in the exprContext.  If the caller wishes the result to be
 * valid longer than that data will be valid, he must call ExecMaterializeSlot
 * on the result slot.
 */
#ifndef FRONTEND
static inline TupleTableSlot *
ExecProject(ProjectionInfo *projInfo)
{
	ExprContext *econtext = projInfo->pi_exprContext;
	ExprState  *state = &projInfo->pi_state;
	TupleTableSlot *slot = state->resultslot;
	bool		isnull;

	/*
	 * Clear any former contents of the result slot.  This makes it safe for
	 * us to use the slot's Datum/isnull arrays as workspace.
	 */
	ExecClearTuple(slot);

	/* Run the expression, discarding scalar result from the last column. */
	(void) ExecEvalExprSwitchContext(state, econtext, &isnull);

	/*
	 * Successfully formed a result row.  Mark the result slot as containing a
	 * valid virtual tuple (inlined version of ExecStoreVirtualTuple()).
	 */
	slot->tts_isempty = false;
	slot->tts_nvalid = slot->tts_tupleDescriptor->natts;

	return slot;
}
#endif

/*
 * ExecQual - evaluate a qual prepared with ExecInitQual (possibly via
 * ExecPrepareQual).  Returns true if qual is satisfied, else false.
 *
 * Note: ExecQual used to have a third argument "resultForNull".  The
 * behavior of this function now corresponds to resultForNull == false.
 * If you want the resultForNull == true behavior, see ExecCheck.
 */
#ifndef FRONTEND
static inline bool
ExecQual(ExprState *state, ExprContext *econtext)
{
	Datum		ret;
	bool		isnull;

	/* short-circuit (here and in ExecInitQual) for empty restriction list */
	if (state == NULL)
		return true;

	/* verify that expression was compiled using ExecInitQual */
	Assert(state->flags & EEO_FLAG_IS_QUAL);

	ret = ExecEvalExprSwitchContext(state, econtext, &isnull);

	/* EEOP_QUAL should never return NULL */
	Assert(!isnull);

	return DatumGetBool(ret);
}
#endif

/*
 * ExecQualAndReset() - evaluate qual with ExecQual() and reset expression
 * context.
 */
#ifndef FRONTEND
static inline bool
ExecQualAndReset(ExprState *state, ExprContext *econtext)
{
	bool		ret = ExecQual(state, econtext);

	/* inline ResetExprContext, to avoid ordering issue in this file */
	MemoryContextReset(econtext->ecxt_per_tuple_memory);
	return ret;
}
#endif

extern bool ExecCheck(ExprState *state, ExprContext *context);

/*
 * prototypes from functions in execSRF.c
 */
extern SetExprState *ExecInitTableFunctionResult(Expr *expr,
							ExprContext *econtext, PlanState *parent);
extern Tuplestorestate *ExecMakeTableFunctionResult(SetExprState *setexpr,
							ExprContext *econtext,
							MemoryContext argContext,
							TupleDesc expectedDesc,
							bool randomAccess);
extern SetExprState *ExecInitFunctionResultSet(Expr *expr,
						  ExprContext *econtext, PlanState *parent);
extern Datum ExecMakeFunctionResultSet(SetExprState *fcache,
						  ExprContext *econtext,
						  MemoryContext argContext,
						  bool *isNull,
						  ExprDoneCond *isDone);

/*
 * prototypes from functions in execScan.c
 */
typedef TupleTableSlot *(*ExecScanAccessMtd) (ScanState *node);
typedef bool (*ExecScanRecheckMtd) (ScanState *node, TupleTableSlot *slot);

extern TupleTableSlot *ExecScan(ScanState *node, ExecScanAccessMtd accessMtd,
		 ExecScanRecheckMtd recheckMtd);
extern void ExecAssignScanProjectionInfo(ScanState *node);
extern void ExecAssignScanProjectionInfoWithVarno(ScanState *node, Index varno);
extern void ExecScanReScan(ScanState *node);

/*
 * prototypes from functions in execTuples.c
 */
extern void ExecInitResultTypeTL(PlanState *planstate);
extern void ExecInitResultSlot(PlanState *planstate);
extern void ExecInitResultTupleSlotTL(PlanState *planstate);
extern void ExecInitScanTupleSlot(EState *estate, ScanState *scanstate, TupleDesc tupleDesc);
extern TupleTableSlot *ExecInitExtraTupleSlot(EState *estate,
					   TupleDesc tupleDesc);
extern TupleTableSlot *ExecInitNullTupleSlot(EState *estate,
					  TupleDesc tupType);
extern TupleDesc ExecTypeFromTL(List *targetList, bool hasoid);
extern TupleDesc ExecCleanTypeFromTL(List *targetList, bool hasoid);
extern TupleDesc ExecTypeFromExprList(List *exprList);
extern void ExecTypeSetColNames(TupleDesc typeInfo, List *namesList);
extern void UpdateChangedParamSet(PlanState *node, Bitmapset *newchg);

typedef struct TupOutputState
{
	TupleTableSlot *slot;
	DestReceiver *dest;
} TupOutputState;

extern TupOutputState *begin_tup_output_tupdesc(DestReceiver *dest,
						 TupleDesc tupdesc);
extern void do_tup_output(TupOutputState *tstate, Datum *values, bool *isnull);
extern void do_text_output_multiline(TupOutputState *tstate, const char *txt);
extern void end_tup_output(TupOutputState *tstate);

/*
 * Write a single line of text given as a C string.
 *
 * Should only be used with a single-TEXT-attribute tupdesc.
 */
#define do_text_output_oneline(tstate, str_to_emit) \
	do { \
		Datum	values_[1]; \
		bool	isnull_[1]; \
		values_[0] = PointerGetDatum(cstring_to_text(str_to_emit)); \
		isnull_[0] = false; \
		do_tup_output(tstate, values_, isnull_); \
		pfree(DatumGetPointer(values_[0])); \
	} while (0)


/*
 * prototypes from functions in execUtils.c
 */
extern EState *CreateExecutorState(void);
extern void FreeExecutorState(EState *estate);
extern ExprContext *CreateExprContext(EState *estate);
extern ExprContext *CreateStandaloneExprContext(void);
extern void FreeExprContext(ExprContext *econtext, bool isCommit);
extern void ReScanExprContext(ExprContext *econtext);

#define ResetExprContext(econtext) \
	MemoryContextReset((econtext)->ecxt_per_tuple_memory)

extern ExprContext *MakePerTupleExprContext(EState *estate);

/* Get an EState's per-output-tuple exprcontext, making it if first use */
#define GetPerTupleExprContext(estate) \
	((estate)->es_per_tuple_exprcontext ? \
	 (estate)->es_per_tuple_exprcontext : \
	 MakePerTupleExprContext(estate))

#define GetPerTupleMemoryContext(estate) \
	(GetPerTupleExprContext(estate)->ecxt_per_tuple_memory)

/* Reset an EState's per-output-tuple exprcontext, if one's been created */
#define ResetPerTupleExprContext(estate) \
	do { \
		if ((estate)->es_per_tuple_exprcontext) \
			ResetExprContext((estate)->es_per_tuple_exprcontext); \
	} while (0)

extern void ExecAssignExprContext(EState *estate, PlanState *planstate);
extern TupleDesc ExecGetResultType(PlanState *planstate);
extern void ExecAssignProjectionInfo(PlanState *planstate,
						 TupleDesc inputDesc);
extern void ExecConditionalAssignProjectionInfo(PlanState *planstate,
									TupleDesc inputDesc, Index varno);
extern void ExecFreeExprContext(PlanState *planstate);
extern void ExecAssignScanType(ScanState *scanstate, TupleDesc tupDesc);
extern void ExecCreateScanSlotFromOuterPlan(EState *estate, ScanState *scanstate);

extern bool ExecRelationIsTargetRelation(EState *estate, Index scanrelid);

extern Relation ExecOpenScanRelation(EState *estate, Index scanrelid, int eflags);
extern void ExecCloseScanRelation(Relation scanrel);

extern int	executor_errposition(EState *estate, int location);

extern void RegisterExprContextCallback(ExprContext *econtext,
							ExprContextCallbackFunction function,
							Datum arg);
extern void UnregisterExprContextCallback(ExprContext *econtext,
							  ExprContextCallbackFunction function,
							  Datum arg);

extern void ExecLockNonLeafAppendTables(List *partitioned_rels, EState *estate);

extern Datum GetAttributeByName(HeapTupleHeader tuple, const char *attname,
				   bool *isNull);
extern Datum GetAttributeByNum(HeapTupleHeader tuple, AttrNumber attrno,
				  bool *isNull);

extern int	ExecTargetListLength(List *targetlist);
extern int	ExecCleanTargetListLength(List *targetlist);

extern Bitmapset *ExecGetInsertedCols(ResultRelInfo *relinfo, EState *estate);
extern Bitmapset *ExecGetUpdatedCols(ResultRelInfo *relinfo, EState *estate);

/*
 * prototypes from functions in execIndexing.c
 */
extern void ExecOpenIndices(ResultRelInfo *resultRelInfo, bool speculative);
extern void ExecCloseIndices(ResultRelInfo *resultRelInfo);
extern List *ExecInsertIndexTuples(TupleTableSlot *slot, HeapTuple tuple,
					  EState *estate, bool noDupErr, bool *specConflict,
					  List *arbiterIndexes);
extern List *ExecInsertIndexTuplesOptimized(TupleTableSlot *slot, HeapTuple tuple,
					  EState *estate, bool noDupErr, bool *specConflict,
					  List *arbiterIndexes, List *no_update_index_list);
extern void ExecDeleteIndexTuples(Datum ybctid, HeapTuple tuple, EState *estate);
extern void ExecDeleteIndexTuplesOptimized(Datum ybctid, HeapTuple tuple, EState *estate,
					  List *no_update_index_list);
extern bool ContainsIndexRelation(Oid indexrelid, List *no_update_index_list);
extern bool ExecCheckIndexConstraints(TupleTableSlot *slot, EState *estate,
						  ItemPointer conflictTid, List *arbiterIndexes);
extern void check_exclusion_constraint(Relation heap, Relation index,
						   IndexInfo *indexInfo,
						   ItemPointer tupleid,
						   Datum *values, bool *isnull,
						   EState *estate, bool newIndex);

/*
 * prototypes from functions in execReplication.c
 */
extern bool RelationFindReplTupleByIndex(Relation rel, Oid idxoid,
							 LockTupleMode lockmode,
							 TupleTableSlot *searchslot,
							 TupleTableSlot *outslot);
extern bool RelationFindReplTupleSeq(Relation rel, LockTupleMode lockmode,
						 TupleTableSlot *searchslot, TupleTableSlot *outslot);

extern void ExecSimpleRelationInsert(EState *estate, TupleTableSlot *slot);
extern void ExecSimpleRelationUpdate(EState *estate, EPQState *epqstate,
						 TupleTableSlot *searchslot, TupleTableSlot *slot);
extern void ExecSimpleRelationDelete(EState *estate, EPQState *epqstate,
						 TupleTableSlot *searchslot);
extern void CheckCmdReplicaIdentity(Relation rel, CmdType cmd);

extern void CheckSubscriptionRelkind(char relkind, const char *nspname,
						 const char *relname);

#endif							/* EXECUTOR_H  */
