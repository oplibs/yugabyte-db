/*-------------------------------------------------------------------------
 *
 * plancat.c
 *	   routines for accessing the system catalogs
 *
 *
 * Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/optimizer/util/plancat.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <math.h>

#include "access/genam.h"
#include "access/heapam.h"
#include "access/htup_details.h"
#include "access/nbtree.h"
#include "access/sysattr.h"
#include "access/transam.h"
#include "access/xlog.h"
#include "catalog/catalog.h"
#include "catalog/dependency.h"
#include "catalog/heap.h"
#include "catalog/partition.h"
#include "catalog/pg_am.h"
#include "catalog/pg_statistic_ext.h"
#include "foreign/fdwapi.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "optimizer/clauses.h"
#include "optimizer/cost.h"
#include "optimizer/plancat.h"
#include "optimizer/predtest.h"
#include "optimizer/prep.h"
#include "partitioning/partbounds.h"
#include "parser/parse_relation.h"
#include "parser/parsetree.h"
#include "rewrite/rewriteManip.h"
#include "statistics/statistics.h"
#include "storage/bufmgr.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/partcache.h"
#include "utils/rel.h"
#include "utils/syscache.h"
#include "utils/snapmgr.h"

#include "pg_yb_utils.h"

/* GUC parameter */
int			constraint_exclusion = CONSTRAINT_EXCLUSION_PARTITION;

/* Hook for plugins to get control in get_relation_info() */
get_relation_info_hook_type get_relation_info_hook = NULL;


static void get_relation_foreign_keys(PlannerInfo *root, RelOptInfo *rel,
						  Relation relation, bool inhparent);
static bool infer_collation_opclass_match(InferenceElem *elem, Relation idxRel,
							  List *idxExprs);
static int32 get_rel_data_width(Relation rel, int32 *attr_widths);
static List *get_relation_constraints(PlannerInfo *root,
						 Oid relationObjectId, RelOptInfo *rel,
						 bool include_notnull);
static List *build_index_tlist(PlannerInfo *root, IndexOptInfo *index,
				  Relation heapRelation);
static List *get_relation_statistics(RelOptInfo *rel, Relation relation);
static void set_relation_partition_info(PlannerInfo *root, RelOptInfo *rel,
							Relation relation);
static PartitionScheme find_partition_scheme(PlannerInfo *root, Relation rel);
static void set_baserel_partition_key_exprs(Relation relation,
								RelOptInfo *rel);

/*
 * get_relation_info -
 *	  Retrieves catalog information for a given relation.
 *
 * Given the Oid of the relation, return the following info into fields
 * of the RelOptInfo struct:
 *
 *	min_attr	lowest valid AttrNumber
 *	max_attr	highest valid AttrNumber
 *	indexlist	list of IndexOptInfos for relation's indexes
 *	statlist	list of StatisticExtInfo for relation's statistic objects
 *	serverid	if it's a foreign table, the server OID
 *	fdwroutine	if it's a foreign table, the FDW function pointers
 *	pages		number of pages
 *	tuples		number of tuples
 *	rel_parallel_workers user-defined number of parallel workers
 *
 * Also, add information about the relation's foreign keys to root->fkey_list.
 *
 * Also, initialize the attr_needed[] and attr_widths[] arrays.  In most
 * cases these are left as zeroes, but sometimes we need to compute attr
 * widths here, and we may as well cache the results for costsize.c.
 *
 * If inhparent is true, all we need to do is set up the attr arrays:
 * the RelOptInfo actually represents the appendrel formed by an inheritance
 * tree, and so the parent rel's physical size and index information isn't
 * important for it.
 */
void
get_relation_info(PlannerInfo *root, Oid relationObjectId, bool inhparent,
				  RelOptInfo *rel)
{
	Index		varno = rel->relid;
	Relation	relation;
	bool		hasindex;
	List	   *indexinfos = NIL;

	/*
	 * We need not lock the relation since it was already locked, either by
	 * the rewriter or when expand_inherited_rtentry() added it to the query's
	 * rangetable.
	 */
	relation = heap_open(relationObjectId, NoLock);

	/* Temporary and unlogged relations are inaccessible during recovery. */
	if (!RelationNeedsWAL(relation) && RecoveryInProgress())
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot access temporary or unlogged relations during recovery")));

	rel->min_attr = YBGetFirstLowInvalidAttributeNumber(relation) + 1;
	rel->max_attr = RelationGetNumberOfAttributes(relation);
	rel->reltablespace = RelationGetForm(relation)->reltablespace;

	Assert(rel->max_attr >= rel->min_attr);
	rel->attr_needed = (Relids *)
		palloc0((rel->max_attr - rel->min_attr + 1) * sizeof(Relids));
	rel->attr_widths = (int32 *)
		palloc0((rel->max_attr - rel->min_attr + 1) * sizeof(int32));

	/*
	 * Estimate relation size --- unless it's an inheritance parent, in which
	 * case the size will be computed later in set_append_rel_pathlist, and we
	 * must leave it zero for now to avoid bollixing the total_table_pages
	 * calculation.
	 */
	if (!inhparent)
		estimate_rel_size(relation, rel->attr_widths - rel->min_attr,
						  &rel->pages, &rel->tuples, &rel->allvisfrac);

	/* Retrieve the parallel_workers reloption, or -1 if not set. */
	rel->rel_parallel_workers = RelationGetParallelWorkers(relation, -1);

	/*
	 * Make list of indexes.  Ignore indexes on system catalogs if told to.
	 * Don't bother with indexes for an inheritance parent, either.
	 */
	if (inhparent ||
		(IgnoreSystemIndexes && IsSystemRelation(relation)))
		hasindex = false;
	else
		hasindex = relation->rd_rel->relhasindex;

	if (hasindex)
	{
		List	   *indexoidlist;
		ListCell   *l;
		LOCKMODE	lmode;

		indexoidlist = RelationGetIndexList(relation);

		/*
		 * For each index, we get the same type of lock that the executor will
		 * need, and do not release it.  This saves a couple of trips to the
		 * shared lock manager while not creating any real loss of
		 * concurrency, because no schema changes could be happening on the
		 * index while we hold lock on the parent rel, and neither lock type
		 * blocks any other kind of index operation.
		 */
		if (rel->relid == root->parse->resultRelation)
			lmode = RowExclusiveLock;
		else
			lmode = AccessShareLock;

		foreach(l, indexoidlist)
		{
			Oid			indexoid = lfirst_oid(l);
			Relation	indexRelation;
			Form_pg_index index;
			IndexAmRoutine *amroutine;
			IndexOptInfo *info;
			int			ncolumns,
						nkeycolumns;
			int			i;

			/*
			 * Extract info from the relation descriptor for the index.
			 */
			indexRelation = index_open(indexoid, lmode);
			index = indexRelation->rd_index;

			/*
			 * Ignore invalid indexes, since they can't safely be used for
			 * queries.  Note that this is OK because the data structure we
			 * are constructing is only used by the planner --- the executor
			 * still needs to insert into "invalid" indexes, if they're marked
			 * IndexIsReady.
			 */
			if (!IndexIsValid(index))
			{
				index_close(indexRelation, NoLock);
				continue;
			}

			/*
			 * Ignore partitioned indexes, since they are not usable for
			 * queries.
			 */
			if (indexRelation->rd_rel->relkind == RELKIND_PARTITIONED_INDEX)
			{
				index_close(indexRelation, NoLock);
				continue;
			}

			/*
			 * If the index is valid, but cannot yet be used, ignore it; but
			 * mark the plan we are generating as transient. See
			 * src/backend/access/heap/README.HOT for discussion.
			 */
			if (index->indcheckxmin &&
				!TransactionIdPrecedes(HeapTupleHeaderGetXmin(indexRelation->rd_indextuple->t_data),
									   TransactionXmin))
			{
				root->glob->transientPlan = true;
				index_close(indexRelation, NoLock);
				continue;
			}

			info = makeNode(IndexOptInfo);

			info->indexoid = index->indexrelid;
			info->reltablespace =
				RelationGetForm(indexRelation)->reltablespace;
			info->rel = rel;
			info->ncolumns = ncolumns = index->indnatts;
			info->nkeycolumns = nkeycolumns = index->indnkeyatts;
			info->nhashcolumns = 0;

			info->indexkeys = (int *) palloc(sizeof(int) * ncolumns);
			info->indexcollations = (Oid *) palloc(sizeof(Oid) * nkeycolumns);
			info->opfamily = (Oid *) palloc(sizeof(Oid) * nkeycolumns);
			info->opcintype = (Oid *) palloc(sizeof(Oid) * nkeycolumns);
			info->canreturn = (bool *) palloc(sizeof(bool) * ncolumns);

			for (i = 0; i < ncolumns; i++)
			{
				info->indexkeys[i] = index->indkey.values[i];
				info->canreturn[i] = index_can_return(indexRelation, i + 1);
			}

			for (i = 0; i < nkeycolumns; i++)
			{
				info->opfamily[i] = indexRelation->rd_opfamily[i];
				info->opcintype[i] = indexRelation->rd_opcintype[i];
				info->indexcollations[i] = indexRelation->rd_indcollation[i];
			}

			info->relam = indexRelation->rd_rel->relam;

			/* We copy just the fields we need, not all of rd_amroutine */
			amroutine = indexRelation->rd_amroutine;
			info->amcanorderbyop = amroutine->amcanorderbyop;
			info->amoptionalkey = amroutine->amoptionalkey;
			info->amsearcharray = amroutine->amsearcharray;
			info->amsearchnulls = amroutine->amsearchnulls;
			info->amcanparallel = amroutine->amcanparallel;
			info->amhasgettuple = (amroutine->amgettuple != NULL);
			info->amhasgetbitmap = (amroutine->amgetbitmap != NULL);
			info->amcostestimate = amroutine->amcostestimate;
			Assert(info->amcostestimate != NULL);

			/*
			 * Fetch the ordering information for the index, if any.
			 */
			if (info->relam == BTREE_AM_OID)
			{
				/*
				 * If it's a btree index, we can use its opfamily OIDs
				 * directly as the sort ordering opfamily OIDs.
				 */
				Assert(amroutine->amcanorder);

				info->sortopfamily = info->opfamily;
				info->reverse_sort = (bool *) palloc(sizeof(bool) * nkeycolumns);
				info->nulls_first = (bool *) palloc(sizeof(bool) * nkeycolumns);

				for (i = 0; i < nkeycolumns; i++)
				{
					int16		opt = indexRelation->rd_indoption[i];

					info->reverse_sort[i] = (opt & INDOPTION_DESC) != 0;
					info->nulls_first[i] = (opt & INDOPTION_NULLS_FIRST) != 0;
				}
			}
			else if (amroutine->amcanorder)
			{
				/*
				 * Otherwise, identify the corresponding btree opfamilies by
				 * trying to map this index's "<" operators into btree.  Since
				 * "<" uniquely defines the behavior of a sort order, this is
				 * a sufficient test.
				 *
				 * XXX This method is rather slow and also requires the
				 * undesirable assumption that the other index AM numbers its
				 * strategies the same as btree.  It'd be better to have a way
				 * to explicitly declare the corresponding btree opfamily for
				 * each opfamily of the other index type.  But given the lack
				 * of current or foreseeable amcanorder index types, it's not
				 * worth expending more effort on now.
				 */
				info->sortopfamily = (Oid *) palloc(sizeof(Oid) * nkeycolumns);
				info->reverse_sort = (bool *) palloc(sizeof(bool) * nkeycolumns);
				info->nulls_first = (bool *) palloc(sizeof(bool) * nkeycolumns);

				for (i = 0; i < nkeycolumns; i++)
				{
					int16		opt = indexRelation->rd_indoption[i];
					Oid			ltopr;
					Oid			btopfamily;
					Oid			btopcintype;
					int16		btstrategy;

					if (IsYBRelation(relation) && (opt & INDOPTION_HASH) != 0)
					{
						info->nhashcolumns++;
						info->reverse_sort[i] = false;
						info->nulls_first[i] = false;
					} else {
						info->reverse_sort[i] = (opt & INDOPTION_DESC) != 0;
						info->nulls_first[i] = (opt & INDOPTION_NULLS_FIRST) != 0;
					}

					ltopr = get_opfamily_member(info->opfamily[i],
												info->opcintype[i],
												info->opcintype[i],
												BTLessStrategyNumber);
					if (OidIsValid(ltopr) &&
						get_ordering_op_properties(ltopr,
												   &btopfamily,
												   &btopcintype,
												   &btstrategy) &&
						btopcintype == info->opcintype[i] &&
						btstrategy == BTLessStrategyNumber)
					{
						/* Successful mapping */
						info->sortopfamily[i] = btopfamily;
					}
					else
					{
						/* Fail ... quietly treat index as unordered */
						info->sortopfamily = NULL;
						info->reverse_sort = NULL;
						info->nulls_first = NULL;
						break;
					}
				}
			}
			else
			{
				info->sortopfamily = NULL;
				info->reverse_sort = NULL;
				info->nulls_first = NULL;
			}

			/*
			 * Fetch the index expressions and predicate, if any.  We must
			 * modify the copies we obtain from the relcache to have the
			 * correct varno for the parent relation, so that they match up
			 * correctly against qual clauses.
			 */
			info->indexprs = RelationGetIndexExpressions(indexRelation);
			info->indpred = RelationGetIndexPredicate(indexRelation);
			if (info->indexprs && varno != 1)
				ChangeVarNodes((Node *) info->indexprs, 1, varno, 0);
			if (info->indpred && varno != 1)
				ChangeVarNodes((Node *) info->indpred, 1, varno, 0);

			/* Build targetlist using the completed indexprs data */
			info->indextlist = build_index_tlist(root, info, relation);

			info->indrestrictinfo = NIL;	/* set later, in indxpath.c */
			info->predOK = false;	/* set later, in indxpath.c */
			info->unique = index->indisunique;
			info->immediate = index->indimmediate;
			info->hypothetical = false;

			/*
			 * Estimate the index size.  If it's not a partial index, we lock
			 * the number-of-tuples estimate to equal the parent table; if it
			 * is partial then we have to use the same methods as we would for
			 * a table, except we can be sure that the index is not larger
			 * than the table.
			 */
			if (info->indpred == NIL && !IsYBRelation(indexRelation))
			{
				info->pages = RelationGetNumberOfBlocks(indexRelation);
				info->tuples = rel->tuples;
			}
			else
			{
				double		allvisfrac; /* dummy */

				estimate_rel_size(indexRelation, NULL,
								  &info->pages, &info->tuples, &allvisfrac);
				if (info->tuples > rel->tuples)
					info->tuples = rel->tuples;
			}

			if (info->relam == BTREE_AM_OID)
			{
				/* For btrees, get tree height while we have the index open */
				info->tree_height = _bt_getrootheight(indexRelation);
			}
			else
			{
				/* For other index types, just set it to "unknown" for now */
				info->tree_height = -1;
			}

			index_close(indexRelation, NoLock);

			indexinfos = lcons(info, indexinfos);
		}

		list_free(indexoidlist);
	}

	rel->indexlist = indexinfos;

	rel->statlist = get_relation_statistics(rel, relation);

	/* Grab foreign-table info using the relcache, while we have it */
	if (relation->rd_rel->relkind == RELKIND_FOREIGN_TABLE)
	{
		rel->serverid = GetForeignServerIdByRelId(RelationGetRelid(relation));
		rel->fdwroutine = GetFdwRoutineForRelation(relation, true);
	}
	else
	{
		rel->serverid = InvalidOid;
		rel->fdwroutine = NULL;
	}

	/* Collect info about relation's foreign keys, if relevant */
	get_relation_foreign_keys(root, rel, relation, inhparent);

	/*
	 * Collect info about relation's partitioning scheme, if any. Only
	 * inheritance parents may be partitioned.
	 */
	if (inhparent && relation->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
		set_relation_partition_info(root, rel, relation);

	heap_close(relation, NoLock);

	/*
	 * Allow a plugin to editorialize on the info we obtained from the
	 * catalogs.  Actions might include altering the assumed relation size,
	 * removing an index, or adding a hypothetical index to the indexlist.
	 */
	if (get_relation_info_hook)
		(*get_relation_info_hook) (root, relationObjectId, inhparent, rel);
}

/*
 * get_relation_foreign_keys -
 *	  Retrieves foreign key information for a given relation.
 *
 * ForeignKeyOptInfos for relevant foreign keys are created and added to
 * root->fkey_list.  We do this now while we have the relcache entry open.
 * We could sometimes avoid making useless ForeignKeyOptInfos if we waited
 * until all RelOptInfos have been built, but the cost of re-opening the
 * relcache entries would probably exceed any savings.
 */
static void
get_relation_foreign_keys(PlannerInfo *root, RelOptInfo *rel,
						  Relation relation, bool inhparent)
{
	List	   *rtable = root->parse->rtable;
	List	   *cachedfkeys;
	ListCell   *lc;

	/*
	 * If it's not a baserel, we don't care about its FKs.  Also, if the query
	 * references only a single relation, we can skip the lookup since no FKs
	 * could satisfy the requirements below.
	 */
	if (rel->reloptkind != RELOPT_BASEREL ||
		list_length(rtable) < 2)
		return;

	/*
	 * If it's the parent of an inheritance tree, ignore its FKs.  We could
	 * make useful FK-based deductions if we found that all members of the
	 * inheritance tree have equivalent FK constraints, but detecting that
	 * would require code that hasn't been written.
	 */
	if (inhparent)
		return;

	/*
	 * Extract data about relation's FKs from the relcache.  Note that this
	 * list belongs to the relcache and might disappear in a cache flush, so
	 * we must not do any further catalog access within this function.
	 */
	cachedfkeys = RelationGetFKeyList(relation);

	/*
	 * Figure out which FKs are of interest for this query, and create
	 * ForeignKeyOptInfos for them.  We want only FKs that reference some
	 * other RTE of the current query.  In queries containing self-joins,
	 * there might be more than one other RTE for a referenced table, and we
	 * should make a ForeignKeyOptInfo for each occurrence.
	 *
	 * Ideally, we would ignore RTEs that correspond to non-baserels, but it's
	 * too hard to identify those here, so we might end up making some useless
	 * ForeignKeyOptInfos.  If so, match_foreign_keys_to_quals() will remove
	 * them again.
	 */
	foreach(lc, cachedfkeys)
	{
		ForeignKeyCacheInfo *cachedfk = (ForeignKeyCacheInfo *) lfirst(lc);
		Index		rti;
		ListCell   *lc2;

		/* conrelid should always be that of the table we're considering */
		Assert(cachedfk->conrelid == RelationGetRelid(relation));

		/* Scan to find other RTEs matching confrelid */
		rti = 0;
		foreach(lc2, rtable)
		{
			RangeTblEntry *rte = (RangeTblEntry *) lfirst(lc2);
			ForeignKeyOptInfo *info;

			rti++;
			/* Ignore if not the correct table */
			if (rte->rtekind != RTE_RELATION ||
				rte->relid != cachedfk->confrelid)
				continue;
			/* Ignore if it's an inheritance parent; doesn't really match */
			if (rte->inh)
				continue;
			/* Ignore self-referential FKs; we only care about joins */
			if (rti == rel->relid)
				continue;

			/* OK, let's make an entry */
			info = makeNode(ForeignKeyOptInfo);
			info->con_relid = rel->relid;
			info->ref_relid = rti;
			info->nkeys = cachedfk->nkeys;
			memcpy(info->conkey, cachedfk->conkey, sizeof(info->conkey));
			memcpy(info->confkey, cachedfk->confkey, sizeof(info->confkey));
			memcpy(info->conpfeqop, cachedfk->conpfeqop, sizeof(info->conpfeqop));
			/* zero out fields to be filled by match_foreign_keys_to_quals */
			info->nmatched_ec = 0;
			info->nmatched_rcols = 0;
			info->nmatched_ri = 0;
			memset(info->eclass, 0, sizeof(info->eclass));
			memset(info->rinfos, 0, sizeof(info->rinfos));

			root->fkey_list = lappend(root->fkey_list, info);
		}
	}
}

/*
 * infer_arbiter_indexes -
 *	  Determine the unique indexes used to arbitrate speculative insertion.
 *
 * Uses user-supplied inference clause expressions and predicate to match a
 * unique index from those defined and ready on the heap relation (target).
 * An exact match is required on columns/expressions (although they can appear
 * in any order).  However, the predicate given by the user need only restrict
 * insertion to a subset of some part of the table covered by some particular
 * unique index (in particular, a partial unique index) in order to be
 * inferred.
 *
 * The implementation does not consider which B-Tree operator class any
 * particular available unique index attribute uses, unless one was specified
 * in the inference specification. The same is true of collations.  In
 * particular, there is no system dependency on the default operator class for
 * the purposes of inference.  If no opclass (or collation) is specified, then
 * all matching indexes (that may or may not match the default in terms of
 * each attribute opclass/collation) are used for inference.
 */
List *
infer_arbiter_indexes(PlannerInfo *root)
{
	OnConflictExpr *onconflict = root->parse->onConflict;

	/* Iteration state */
	Relation	relation;
	Oid			relationObjectId;
	Oid			indexOidFromConstraint = InvalidOid;
	List	   *indexList;
	ListCell   *l;

	/* Normalized inference attributes and inference expressions: */
	Bitmapset  *inferAttrs = NULL;
	List	   *inferElems = NIL;

	/* Results */
	List	   *results = NIL;

	/*
	 * Quickly return NIL for ON CONFLICT DO NOTHING without an inference
	 * specification or named constraint.  ON CONFLICT DO UPDATE statements
	 * must always provide one or the other (but parser ought to have caught
	 * that already).
	 */
	if (onconflict->arbiterElems == NIL &&
		onconflict->constraint == InvalidOid)
		return NIL;

	/*
	 * We need not lock the relation since it was already locked, either by
	 * the rewriter or when expand_inherited_rtentry() added it to the query's
	 * rangetable.
	 */
	relationObjectId = rt_fetch(root->parse->resultRelation,
								root->parse->rtable)->relid;

	relation = heap_open(relationObjectId, NoLock);

	/*
	 * Build normalized/BMS representation of plain indexed attributes, as
	 * well as a separate list of expression items.  This simplifies matching
	 * the cataloged definition of indexes.
	 */
	foreach(l, onconflict->arbiterElems)
	{
		InferenceElem *elem = (InferenceElem *) lfirst(l);
		Var		   *var;
		int			attno;

		if (!IsA(elem->expr, Var))
		{
			/* If not a plain Var, just shove it in inferElems for now */
			inferElems = lappend(inferElems, elem->expr);
			continue;
		}

		var = (Var *) elem->expr;
		attno = var->varattno;

		if (attno == 0)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("whole row unique index inference specifications are not supported")));

		inferAttrs = bms_add_member(inferAttrs,
									attno - FirstLowInvalidHeapAttributeNumber);
	}

	/*
	 * Lookup named constraint's index.  This is not immediately returned
	 * because some additional sanity checks are required.
	 */
	if (onconflict->constraint != InvalidOid)
	{
		indexOidFromConstraint = get_constraint_index(onconflict->constraint);

		if (indexOidFromConstraint == InvalidOid)
			ereport(ERROR,
					(errcode(ERRCODE_WRONG_OBJECT_TYPE),
					 errmsg("constraint in ON CONFLICT clause has no associated index")));
	}

	/*
	 * Using that representation, iterate through the list of indexes on the
	 * target relation to try and find a match
	 */
	indexList = RelationGetIndexList(relation);

	foreach(l, indexList)
	{
		Oid			indexoid = lfirst_oid(l);
		Relation	idxRel;
		Form_pg_index idxForm;
		Bitmapset  *indexedAttrs;
		List	   *idxExprs;
		List	   *predExprs;
		AttrNumber	natt;
		ListCell   *el;

		/*
		 * Extract info from the relation descriptor for the index.  We know
		 * that this is a target, so get lock type it is known will ultimately
		 * be required by the executor.
		 *
		 * Let executor complain about !indimmediate case directly, because
		 * enforcement needs to occur there anyway when an inference clause is
		 * omitted.
		 */
		idxRel = index_open(indexoid, RowExclusiveLock);
		idxForm = idxRel->rd_index;

		if (!IndexIsValid(idxForm))
			goto next;

		/*
		 * Note that we do not perform a check against indcheckxmin (like e.g.
		 * get_relation_info()) here to eliminate candidates, because
		 * uniqueness checking only cares about the most recently committed
		 * tuple versions.
		 */

		/*
		 * Look for match on "ON constraint_name" variant, which may not be
		 * unique constraint.  This can only be a constraint name.
		 */
		if (indexOidFromConstraint == idxForm->indexrelid)
		{
			if (!idxForm->indisunique && onconflict->action == ONCONFLICT_UPDATE)
				ereport(ERROR,
						(errcode(ERRCODE_WRONG_OBJECT_TYPE),
						 errmsg("ON CONFLICT DO UPDATE not supported with exclusion constraints")));

			results = lappend_oid(results, idxForm->indexrelid);
			list_free(indexList);
			index_close(idxRel, NoLock);
			heap_close(relation, NoLock);
			return results;
		}
		else if (indexOidFromConstraint != InvalidOid)
		{
			/* No point in further work for index in named constraint case */
			goto next;
		}

		/*
		 * Only considering conventional inference at this point (not named
		 * constraints), so index under consideration can be immediately
		 * skipped if it's not unique
		 */
		if (!idxForm->indisunique)
			goto next;

		/* Build BMS representation of plain (non expression) index attrs */
		indexedAttrs = NULL;
		for (natt = 0; natt < idxForm->indnkeyatts; natt++)
		{
			int			attno = idxRel->rd_index->indkey.values[natt];

			if (attno != 0)
				indexedAttrs = bms_add_member(indexedAttrs,
											  attno - FirstLowInvalidHeapAttributeNumber);
		}

		/* Non-expression attributes (if any) must match */
		if (!bms_equal(indexedAttrs, inferAttrs))
			goto next;

		/* Expression attributes (if any) must match */
		idxExprs = RelationGetIndexExpressions(idxRel);
		foreach(el, onconflict->arbiterElems)
		{
			InferenceElem *elem = (InferenceElem *) lfirst(el);

			/*
			 * Ensure that collation/opclass aspects of inference expression
			 * element match.  Even though this loop is primarily concerned
			 * with matching expressions, it is a convenient point to check
			 * this for both expressions and ordinary (non-expression)
			 * attributes appearing as inference elements.
			 */
			if (!infer_collation_opclass_match(elem, idxRel, idxExprs))
				goto next;

			/*
			 * Plain Vars don't factor into count of expression elements, and
			 * the question of whether or not they satisfy the index
			 * definition has already been considered (they must).
			 */
			if (IsA(elem->expr, Var))
				continue;

			/*
			 * Might as well avoid redundant check in the rare cases where
			 * infer_collation_opclass_match() is required to do real work.
			 * Otherwise, check that element expression appears in cataloged
			 * index definition.
			 */
			if (elem->infercollid != InvalidOid ||
				elem->inferopclass != InvalidOid ||
				list_member(idxExprs, elem->expr))
				continue;

			goto next;
		}

		/*
		 * Now that all inference elements were matched, ensure that the
		 * expression elements from inference clause are not missing any
		 * cataloged expressions.  This does the right thing when unique
		 * indexes redundantly repeat the same attribute, or if attributes
		 * redundantly appear multiple times within an inference clause.
		 */
		if (list_difference(idxExprs, inferElems) != NIL)
			goto next;

		/*
		 * If it's a partial index, its predicate must be implied by the ON
		 * CONFLICT's WHERE clause.
		 */
		predExprs = RelationGetIndexPredicate(idxRel);

		if (!predicate_implied_by(predExprs, (List *) onconflict->arbiterWhere, false))
			goto next;

		results = lappend_oid(results, idxForm->indexrelid);
next:
		index_close(idxRel, NoLock);
	}

	list_free(indexList);
	heap_close(relation, NoLock);

	if (results == NIL)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_COLUMN_REFERENCE),
				 errmsg("there is no unique or exclusion constraint matching the ON CONFLICT specification")));

	return results;
}

/*
 * infer_collation_opclass_match - ensure infer element opclass/collation match
 *
 * Given unique index inference element from inference specification, if
 * collation was specified, or if opclass was specified, verify that there is
 * at least one matching indexed attribute (occasionally, there may be more).
 * Skip this in the common case where inference specification does not include
 * collation or opclass (instead matching everything, regardless of cataloged
 * collation/opclass of indexed attribute).
 *
 * At least historically, Postgres has not offered collations or opclasses
 * with alternative-to-default notions of equality, so these additional
 * criteria should only be required infrequently.
 *
 * Don't give up immediately when an inference element matches some attribute
 * cataloged as indexed but not matching additional opclass/collation
 * criteria.  This is done so that the implementation is as forgiving as
 * possible of redundancy within cataloged index attributes (or, less
 * usefully, within inference specification elements).  If collations actually
 * differ between apparently redundantly indexed attributes (redundant within
 * or across indexes), then there really is no redundancy as such.
 *
 * Note that if an inference element specifies an opclass and a collation at
 * once, both must match in at least one particular attribute within index
 * catalog definition in order for that inference element to be considered
 * inferred/satisfied.
 */
static bool
infer_collation_opclass_match(InferenceElem *elem, Relation idxRel,
							  List *idxExprs)
{
	AttrNumber	natt;
	Oid			inferopfamily = InvalidOid; /* OID of opclass opfamily */
	Oid			inferopcinputtype = InvalidOid; /* OID of opclass input type */
	int			nplain = 0;		/* # plain attrs observed */

	/*
	 * If inference specification element lacks collation/opclass, then no
	 * need to check for exact match.
	 */
	if (elem->infercollid == InvalidOid && elem->inferopclass == InvalidOid)
		return true;

	/*
	 * Lookup opfamily and input type, for matching indexes
	 */
	if (elem->inferopclass)
	{
		inferopfamily = get_opclass_family(elem->inferopclass);
		inferopcinputtype = get_opclass_input_type(elem->inferopclass);
	}

	for (natt = 1; natt <= idxRel->rd_att->natts; natt++)
	{
		Oid			opfamily = idxRel->rd_opfamily[natt - 1];
		Oid			opcinputtype = idxRel->rd_opcintype[natt - 1];
		Oid			collation = idxRel->rd_indcollation[natt - 1];
		int			attno = idxRel->rd_index->indkey.values[natt - 1];

		if (attno != 0)
			nplain++;

		if (elem->inferopclass != InvalidOid &&
			(inferopfamily != opfamily || inferopcinputtype != opcinputtype))
		{
			/* Attribute needed to match opclass, but didn't */
			continue;
		}

		if (elem->infercollid != InvalidOid &&
			elem->infercollid != collation)
		{
			/* Attribute needed to match collation, but didn't */
			continue;
		}

		/* If one matching index att found, good enough -- return true */
		if (IsA(elem->expr, Var))
		{
			if (((Var *) elem->expr)->varattno == attno)
				return true;
		}
		else if (attno == 0)
		{
			Node	   *nattExpr = list_nth(idxExprs, (natt - 1) - nplain);

			/*
			 * Note that unlike routines like match_index_to_operand() we
			 * don't need to care about RelabelType.  Neither the index
			 * definition nor the inference clause should contain them.
			 */
			if (equal(elem->expr, nattExpr))
				return true;
		}
	}

	return false;
}

/*
 * estimate_rel_size - estimate # pages and # tuples in a table or index
 *
 * We also estimate the fraction of the pages that are marked all-visible in
 * the visibility map, for use in estimation of index-only scans.
 *
 * If attr_widths isn't NULL, it points to the zero-index entry of the
 * relation's attr_widths[] cache; we fill this in if we have need to compute
 * the attribute widths for estimation purposes.
 */
void
estimate_rel_size(Relation rel, int32 *attr_widths,
				  BlockNumber *pages, double *tuples, double *allvisfrac)
{
	BlockNumber curpages;
	BlockNumber relpages;
	double		reltuples;
	BlockNumber relallvisible;
	double		density;

	/*
	 * TODO We don't support forwarding size estimates to postgres yet.
	 * Use whatever is in pg_class.
	 */
	if (IsYugaByteEnabled())
	{
		*pages = rel->rd_rel->relpages;
		*tuples = rel->rd_rel->reltuples;
		*allvisfrac = 0;
		return;
	}

	switch (rel->rd_rel->relkind)
	{
		case RELKIND_RELATION:
		case RELKIND_INDEX:
		case RELKIND_MATVIEW:
		case RELKIND_TOASTVALUE:
			/* it has storage, ok to call the smgr */
			curpages = RelationGetNumberOfBlocks(rel);

			/*
			 * HACK: if the relation has never yet been vacuumed, use a
			 * minimum size estimate of 10 pages.  The idea here is to avoid
			 * assuming a newly-created table is really small, even if it
			 * currently is, because that may not be true once some data gets
			 * loaded into it.  Once a vacuum or analyze cycle has been done
			 * on it, it's more reasonable to believe the size is somewhat
			 * stable.
			 *
			 * (Note that this is only an issue if the plan gets cached and
			 * used again after the table has been filled.  What we're trying
			 * to avoid is using a nestloop-type plan on a table that has
			 * grown substantially since the plan was made.  Normally,
			 * autovacuum/autoanalyze will occur once enough inserts have
			 * happened and cause cached-plan invalidation; but that doesn't
			 * happen instantaneously, and it won't happen at all for cases
			 * such as temporary tables.)
			 *
			 * We approximate "never vacuumed" by "has relpages = 0", which
			 * means this will also fire on genuinely empty relations.  Not
			 * great, but fortunately that's a seldom-seen case in the real
			 * world, and it shouldn't degrade the quality of the plan too
			 * much anyway to err in this direction.
			 *
			 * There are two exceptions wherein we don't apply this heuristic.
			 * One is if the table has inheritance children.  Totally empty
			 * parent tables are quite common, so we should be willing to
			 * believe that they are empty.  Also, we don't apply the 10-page
			 * minimum to indexes.
			 */
			if (curpages < 10 &&
				rel->rd_rel->relpages == 0 &&
				!rel->rd_rel->relhassubclass &&
				rel->rd_rel->relkind != RELKIND_INDEX)
				curpages = 10;

			/* report estimated # pages */
			*pages = curpages;
			/* quick exit if rel is clearly empty */
			if (curpages == 0)
			{
				*tuples = 0;
				*allvisfrac = 0;
				break;
			}
			/* coerce values in pg_class to more desirable types */
			relpages = (BlockNumber) rel->rd_rel->relpages;
			reltuples = (double) rel->rd_rel->reltuples;
			relallvisible = (BlockNumber) rel->rd_rel->relallvisible;

			/*
			 * If it's an index, discount the metapage while estimating the
			 * number of tuples.  This is a kluge because it assumes more than
			 * it ought to about index structure.  Currently it's OK for
			 * btree, hash, and GIN indexes but suspect for GiST indexes.
			 */
			if (rel->rd_rel->relkind == RELKIND_INDEX &&
				relpages > 0)
			{
				curpages--;
				relpages--;
			}

			/* estimate number of tuples from previous tuple density */
			if (relpages > 0)
				density = reltuples / (double) relpages;
			else
			{
				/*
				 * When we have no data because the relation was truncated,
				 * estimate tuple width from attribute datatypes.  We assume
				 * here that the pages are completely full, which is OK for
				 * tables (since they've presumably not been VACUUMed yet) but
				 * is probably an overestimate for indexes.  Fortunately
				 * get_relation_info() can clamp the overestimate to the
				 * parent table's size.
				 *
				 * Note: this code intentionally disregards alignment
				 * considerations, because (a) that would be gilding the lily
				 * considering how crude the estimate is, and (b) it creates
				 * platform dependencies in the default plans which are kind
				 * of a headache for regression testing.
				 */
				int32		tuple_width;

				tuple_width = get_rel_data_width(rel, attr_widths);
				tuple_width += MAXALIGN(SizeofHeapTupleHeader);
				tuple_width += sizeof(ItemIdData);
				/* note: integer division is intentional here */
				density = (BLCKSZ - SizeOfPageHeaderData) / tuple_width;
			}
			*tuples = rint(density * (double) curpages);

			/*
			 * We use relallvisible as-is, rather than scaling it up like we
			 * do for the pages and tuples counts, on the theory that any
			 * pages added since the last VACUUM are most likely not marked
			 * all-visible.  But costsize.c wants it converted to a fraction.
			 */
			if (relallvisible == 0 || curpages <= 0)
				*allvisfrac = 0;
			else if ((double) relallvisible >= curpages)
				*allvisfrac = 1;
			else
				*allvisfrac = (double) relallvisible / curpages;
			break;
		case RELKIND_SEQUENCE:
			/* Sequences always have a known size */
			*pages = 1;
			*tuples = 1;
			*allvisfrac = 0;
			break;
		case RELKIND_FOREIGN_TABLE:
			/* Just use whatever's in pg_class */
			*pages = rel->rd_rel->relpages;
			*tuples = rel->rd_rel->reltuples;
			*allvisfrac = 0;
			break;
		default:
			/* else it has no disk storage; probably shouldn't get here? */
			*pages = 0;
			*tuples = 0;
			*allvisfrac = 0;
			break;
	}
}


/*
 * get_rel_data_width
 *
 * Estimate the average width of (the data part of) the relation's tuples.
 *
 * If attr_widths isn't NULL, it points to the zero-index entry of the
 * relation's attr_widths[] cache; use and update that cache as appropriate.
 *
 * Currently we ignore dropped columns.  Ideally those should be included
 * in the result, but we haven't got any way to get info about them; and
 * since they might be mostly NULLs, treating them as zero-width is not
 * necessarily the wrong thing anyway.
 */
static int32
get_rel_data_width(Relation rel, int32 *attr_widths)
{
	int32		tuple_width = 0;
	int			i;

	for (i = 1; i <= RelationGetNumberOfAttributes(rel); i++)
	{
		Form_pg_attribute att = TupleDescAttr(rel->rd_att, i - 1);
		int32		item_width;

		if (att->attisdropped)
			continue;

		/* use previously cached data, if any */
		if (attr_widths != NULL && attr_widths[i] > 0)
		{
			tuple_width += attr_widths[i];
			continue;
		}

		/* This should match set_rel_width() in costsize.c */
		item_width = get_attavgwidth(RelationGetRelid(rel), i);
		if (item_width <= 0)
		{
			item_width = get_typavgwidth(att->atttypid, att->atttypmod);
			Assert(item_width > 0);
		}
		if (attr_widths != NULL)
			attr_widths[i] = item_width;
		tuple_width += item_width;
	}

	return tuple_width;
}

/*
 * get_relation_data_width
 *
 * External API for get_rel_data_width: same behavior except we have to
 * open the relcache entry.
 */
int32
get_relation_data_width(Oid relid, int32 *attr_widths)
{
	int32		result;
	Relation	relation;

	/* As above, assume relation is already locked */
	relation = heap_open(relid, NoLock);

	result = get_rel_data_width(relation, attr_widths);

	heap_close(relation, NoLock);

	return result;
}


/*
 * get_relation_constraints
 *
 * Retrieve the validated CHECK constraint expressions of the given relation.
 *
 * Returns a List (possibly empty) of constraint expressions.  Each one
 * has been canonicalized, and its Vars are changed to have the varno
 * indicated by rel->relid.  This allows the expressions to be easily
 * compared to expressions taken from WHERE.
 *
 * If include_notnull is true, "col IS NOT NULL" expressions are generated
 * and added to the result for each column that's marked attnotnull.
 *
 * Note: at present this is invoked at most once per relation per planner
 * run, and in many cases it won't be invoked at all, so there seems no
 * point in caching the data in RelOptInfo.
 */
static List *
get_relation_constraints(PlannerInfo *root,
						 Oid relationObjectId, RelOptInfo *rel,
						 bool include_notnull)
{
	List	   *result = NIL;
	Index		varno = rel->relid;
	Relation	relation;
	TupleConstr *constr;

	/*
	 * We assume the relation has already been safely locked.
	 */
	relation = heap_open(relationObjectId, NoLock);

	constr = relation->rd_att->constr;
	if (constr != NULL)
	{
		int			num_check = constr->num_check;
		int			i;

		for (i = 0; i < num_check; i++)
		{
			Node	   *cexpr;

			/*
			 * If this constraint hasn't been fully validated yet, we must
			 * ignore it here.
			 */
			if (!constr->check[i].ccvalid)
				continue;

			cexpr = stringToNode(constr->check[i].ccbin);

			/*
			 * Run each expression through const-simplification and
			 * canonicalization.  This is not just an optimization, but is
			 * necessary, because we will be comparing it to
			 * similarly-processed qual clauses, and may fail to detect valid
			 * matches without this.  This must match the processing done to
			 * qual clauses in preprocess_expression()!  (We can skip the
			 * stuff involving subqueries, however, since we don't allow any
			 * in check constraints.)
			 */
			cexpr = eval_const_expressions(root, cexpr);

			cexpr = (Node *) canonicalize_qual((Expr *) cexpr, true);

			/* Fix Vars to have the desired varno */
			if (varno != 1)
				ChangeVarNodes(cexpr, 1, varno, 0);

			/*
			 * Finally, convert to implicit-AND format (that is, a List) and
			 * append the resulting item(s) to our output list.
			 */
			result = list_concat(result,
								 make_ands_implicit((Expr *) cexpr));
		}

		/* Add NOT NULL constraints in expression form, if requested */
		if (include_notnull && constr->has_not_null)
		{
			int			natts = relation->rd_att->natts;

			for (i = 1; i <= natts; i++)
			{
				Form_pg_attribute att = TupleDescAttr(relation->rd_att, i - 1);

				if (att->attnotnull && !att->attisdropped)
				{
					NullTest   *ntest = makeNode(NullTest);

					ntest->arg = (Expr *) makeVar(varno,
												  i,
												  att->atttypid,
												  att->atttypmod,
												  att->attcollation,
												  0);
					ntest->nulltesttype = IS_NOT_NULL;

					/*
					 * argisrow=false is correct even for a composite column,
					 * because attnotnull does not represent a SQL-spec IS NOT
					 * NULL test in such a case, just IS DISTINCT FROM NULL.
					 */
					ntest->argisrow = false;
					ntest->location = -1;
					result = lappend(result, ntest);
				}
			}
		}
	}

	/*
	 * Append partition predicates, if any.
	 *
	 * For selects, partition pruning uses the parent table's partition bound
	 * descriptor, instead of constraint exclusion which is driven by the
	 * individual partition's partition constraint.
	 */
	if (enable_partition_pruning && root->parse->commandType != CMD_SELECT)
	{
		List	   *pcqual = RelationGetPartitionQual(relation);

		if (pcqual)
		{
			/*
			 * Run the partition quals through const-simplification similar to
			 * check constraints.  We skip canonicalize_qual, though, because
			 * partition quals should be in canonical form already; also,
			 * since the qual is in implicit-AND format, we'd have to
			 * explicitly convert it to explicit-AND format and back again.
			 */
			pcqual = (List *) eval_const_expressions(root, (Node *) pcqual);

			/* Fix Vars to have the desired varno */
			if (varno != 1)
				ChangeVarNodes((Node *) pcqual, 1, varno, 0);

			result = list_concat(result, pcqual);
		}
	}

	heap_close(relation, NoLock);

	return result;
}

/*
 * get_relation_statistics
 *		Retrieve extended statistics defined on the table.
 *
 * Returns a List (possibly empty) of StatisticExtInfo objects describing
 * the statistics.  Note that this doesn't load the actual statistics data,
 * just the identifying metadata.  Only stats actually built are considered.
 */
static List *
get_relation_statistics(RelOptInfo *rel, Relation relation)
{
	List	   *statoidlist;
	List	   *stainfos = NIL;
	ListCell   *l;

	/* YugaByte does not support forwarding statistics to Postgres yet */
	if (IsYugaByteEnabled())
		return NIL;

	statoidlist = RelationGetStatExtList(relation);

	foreach(l, statoidlist)
	{
		Oid			statOid = lfirst_oid(l);
		Form_pg_statistic_ext staForm;
		HeapTuple	htup;
		Bitmapset  *keys = NULL;
		int			i;

		htup = SearchSysCache1(STATEXTOID, ObjectIdGetDatum(statOid));
		if (!htup)
			elog(ERROR, "cache lookup failed for statistics object %u", statOid);
		staForm = (Form_pg_statistic_ext) GETSTRUCT(htup);

		/*
		 * First, build the array of columns covered.  This is ultimately
		 * wasted if no stats within the object have actually been built, but
		 * it doesn't seem worth troubling over that case.
		 */
		for (i = 0; i < staForm->stxkeys.dim1; i++)
			keys = bms_add_member(keys, staForm->stxkeys.values[i]);

		/* add one StatisticExtInfo for each kind built */
		if (statext_is_kind_built(htup, STATS_EXT_NDISTINCT))
		{
			StatisticExtInfo *info = makeNode(StatisticExtInfo);

			info->statOid = statOid;
			info->rel = rel;
			info->kind = STATS_EXT_NDISTINCT;
			info->keys = bms_copy(keys);

			stainfos = lcons(info, stainfos);
		}

		if (statext_is_kind_built(htup, STATS_EXT_DEPENDENCIES))
		{
			StatisticExtInfo *info = makeNode(StatisticExtInfo);

			info->statOid = statOid;
			info->rel = rel;
			info->kind = STATS_EXT_DEPENDENCIES;
			info->keys = bms_copy(keys);

			stainfos = lcons(info, stainfos);
		}

		ReleaseSysCache(htup);
		bms_free(keys);
	}

	list_free(statoidlist);

	return stainfos;
}

/*
 * relation_excluded_by_constraints
 *
 * Detect whether the relation need not be scanned because it has either
 * self-inconsistent restrictions, or restrictions inconsistent with the
 * relation's validated CHECK constraints.
 *
 * Note: this examines only rel->relid, rel->reloptkind, and
 * rel->baserestrictinfo; therefore it can be called before filling in
 * other fields of the RelOptInfo.
 */
bool
relation_excluded_by_constraints(PlannerInfo *root,
								 RelOptInfo *rel, RangeTblEntry *rte)
{
	List	   *safe_restrictions;
	List	   *constraint_pred;
	List	   *safe_constraints;
	ListCell   *lc;

	/* As of now, constraint exclusion works only with simple relations. */
	Assert(IS_SIMPLE_REL(rel));

	/*
	 * Regardless of the setting of constraint_exclusion, detect
	 * constant-FALSE-or-NULL restriction clauses.  Because const-folding will
	 * reduce "anything AND FALSE" to just "FALSE", any such case should
	 * result in exactly one baserestrictinfo entry.  This doesn't fire very
	 * often, but it seems cheap enough to be worth doing anyway.  (Without
	 * this, we'd miss some optimizations that 9.5 and earlier found via much
	 * more roundabout methods.)
	 */
	if (list_length(rel->baserestrictinfo) == 1)
	{
		RestrictInfo *rinfo = (RestrictInfo *) linitial(rel->baserestrictinfo);
		Expr	   *clause = rinfo->clause;

		if (clause && IsA(clause, Const) &&
			(((Const *) clause)->constisnull ||
			 !DatumGetBool(((Const *) clause)->constvalue)))
			return true;
	}

	/*
	 * Skip further tests, depending on constraint_exclusion.
	 */
	switch (constraint_exclusion)
	{
		case CONSTRAINT_EXCLUSION_OFF:

			/*
			 * Don't prune if feature turned off -- except if the relation is
			 * a partition.  While partprune.c-style partition pruning is not
			 * yet in use for all cases (update/delete is not handled), it
			 * would be a UI horror to use different user-visible controls
			 * depending on such a volatile implementation detail.  Therefore,
			 * for partitioned tables we use enable_partition_pruning to
			 * control this behavior.
			 */
			if (root->inhTargetKind == INHKIND_PARTITIONED)
				break;
			return false;

		case CONSTRAINT_EXCLUSION_PARTITION:

			/*
			 * When constraint_exclusion is set to 'partition' we only handle
			 * OTHER_MEMBER_RELs, or BASERELs in cases where the result target
			 * is an inheritance parent or a partitioned table.
			 */
			if ((rel->reloptkind != RELOPT_OTHER_MEMBER_REL) &&
				!(rel->reloptkind == RELOPT_BASEREL &&
				  root->inhTargetKind != INHKIND_NONE &&
				  rel->relid == root->parse->resultRelation))
				return false;
			break;

		case CONSTRAINT_EXCLUSION_ON:
			break;				/* always try to exclude */
	}

	/*
	 * Check for self-contradictory restriction clauses.  We dare not make
	 * deductions with non-immutable functions, but any immutable clauses that
	 * are self-contradictory allow us to conclude the scan is unnecessary.
	 *
	 * Note: strip off RestrictInfo because predicate_refuted_by() isn't
	 * expecting to see any in its predicate argument.
	 */
	safe_restrictions = NIL;
	foreach(lc, rel->baserestrictinfo)
	{
		RestrictInfo *rinfo = (RestrictInfo *) lfirst(lc);

		if (!contain_mutable_functions((Node *) rinfo->clause))
			safe_restrictions = lappend(safe_restrictions, rinfo->clause);
	}

	/*
	 * We can use weak refutation here, since we're comparing restriction
	 * clauses with restriction clauses.
	 */
	if (predicate_refuted_by(safe_restrictions, safe_restrictions, true))
		return true;

	/*
	 * Only plain relations have constraints.  In a partitioning hierarchy,
	 * but not with regular table inheritance, it's OK to assume that any
	 * constraints that hold for the parent also hold for every child; for
	 * instance, table inheritance allows the parent to have constraints
	 * marked NO INHERIT, but table partitioning does not.  We choose to check
	 * whether the partitioning parents can be excluded here; doing so
	 * consumes some cycles, but potentially saves us the work of excluding
	 * each child individually.
	 */
	if (rte->rtekind != RTE_RELATION ||
		(rte->inh && rte->relkind != RELKIND_PARTITIONED_TABLE))
		return false;

	/*
	 * OK to fetch the constraint expressions.  Include "col IS NOT NULL"
	 * expressions for attnotnull columns, in case we can refute those.
	 */
	constraint_pred = get_relation_constraints(root, rte->relid, rel, true);

	/*
	 * We do not currently enforce that CHECK constraints contain only
	 * immutable functions, so it's necessary to check here. We daren't draw
	 * conclusions from plan-time evaluation of non-immutable functions. Since
	 * they're ANDed, we can just ignore any mutable constraints in the list,
	 * and reason about the rest.
	 */
	safe_constraints = NIL;
	foreach(lc, constraint_pred)
	{
		Node	   *pred = (Node *) lfirst(lc);

		if (!contain_mutable_functions(pred))
			safe_constraints = lappend(safe_constraints, pred);
	}

	/*
	 * The constraints are effectively ANDed together, so we can just try to
	 * refute the entire collection at once.  This may allow us to make proofs
	 * that would fail if we took them individually.
	 *
	 * Note: we use rel->baserestrictinfo, not safe_restrictions as might seem
	 * an obvious optimization.  Some of the clauses might be OR clauses that
	 * have volatile and nonvolatile subclauses, and it's OK to make
	 * deductions with the nonvolatile parts.
	 *
	 * We need strong refutation because we have to prove that the constraints
	 * would yield false, not just NULL.
	 */
	if (predicate_refuted_by(safe_constraints, rel->baserestrictinfo, false))
		return true;

	return false;
}


/*
 * build_physical_tlist
 *
 * Build a targetlist consisting of exactly the relation's user attributes,
 * in order.  The executor can special-case such tlists to avoid a projection
 * step at runtime, so we use such tlists preferentially for scan nodes.
 *
 * Exception: if there are any dropped or missing columns, we punt and return
 * NIL.  Ideally we would like to handle these cases too.  However this
 * creates problems for ExecTypeFromTL, which may be asked to build a tupdesc
 * for a tlist that includes vars of no-longer-existent types.  In theory we
 * could dig out the required info from the pg_attribute entries of the
 * relation, but that data is not readily available to ExecTypeFromTL.
 * For now, we don't apply the physical-tlist optimization when there are
 * dropped cols.
 *
 * We also support building a "physical" tlist for subqueries, functions,
 * values lists, table expressions, and CTEs, since the same optimization can
 * occur in SubqueryScan, FunctionScan, ValuesScan, CteScan, TableFunc,
 * NamedTuplestoreScan, and WorkTableScan nodes.
 */
List *
build_physical_tlist(PlannerInfo *root, RelOptInfo *rel)
{
	List	   *tlist = NIL;
	Index		varno = rel->relid;
	RangeTblEntry *rte = planner_rt_fetch(varno, root);
	Relation	relation;
	Query	   *subquery;
	Var		   *var;
	ListCell   *l;
	int			attrno,
				numattrs;
	List	   *colvars;

	switch (rte->rtekind)
	{
		case RTE_RELATION:
			/* Assume we already have adequate lock */
			relation = heap_open(rte->relid, NoLock);

			numattrs = RelationGetNumberOfAttributes(relation);
			for (attrno = 1; attrno <= numattrs; attrno++)
			{
				Form_pg_attribute att_tup = TupleDescAttr(relation->rd_att,
														  attrno - 1);

				if (att_tup->attisdropped || att_tup->atthasmissing)
				{
					/* found a dropped or missing col, so punt */
					tlist = NIL;
					break;
				}

				var = makeVar(varno,
							  attrno,
							  att_tup->atttypid,
							  att_tup->atttypmod,
							  att_tup->attcollation,
							  0);

				tlist = lappend(tlist,
								makeTargetEntry((Expr *) var,
												attrno,
												NULL,
												false));
			}

			heap_close(relation, NoLock);
			break;

		case RTE_SUBQUERY:
			subquery = rte->subquery;
			foreach(l, subquery->targetList)
			{
				TargetEntry *tle = (TargetEntry *) lfirst(l);

				/*
				 * A resjunk column of the subquery can be reflected as
				 * resjunk in the physical tlist; we need not punt.
				 */
				var = makeVarFromTargetEntry(varno, tle);

				tlist = lappend(tlist,
								makeTargetEntry((Expr *) var,
												tle->resno,
												NULL,
												tle->resjunk));
			}
			break;

		case RTE_FUNCTION:
		case RTE_TABLEFUNC:
		case RTE_VALUES:
		case RTE_CTE:
		case RTE_NAMEDTUPLESTORE:
			/* Not all of these can have dropped cols, but share code anyway */
			expandRTE(rte, varno, 0, -1, true /* include dropped */ ,
					  NULL, &colvars);
			foreach(l, colvars)
			{
				var = (Var *) lfirst(l);

				/*
				 * A non-Var in expandRTE's output means a dropped column;
				 * must punt.
				 */
				if (!IsA(var, Var))
				{
					tlist = NIL;
					break;
				}

				tlist = lappend(tlist,
								makeTargetEntry((Expr *) var,
												var->varattno,
												NULL,
												false));
			}
			break;

		default:
			/* caller error */
			elog(ERROR, "unsupported RTE kind %d in build_physical_tlist",
				 (int) rte->rtekind);
			break;
	}

	return tlist;
}

/*
 * build_index_tlist
 *
 * Build a targetlist representing the columns of the specified index.
 * Each column is represented by a Var for the corresponding base-relation
 * column, or an expression in base-relation Vars, as appropriate.
 *
 * There are never any dropped columns in indexes, so unlike
 * build_physical_tlist, we need no failure case.
 */
static List *
build_index_tlist(PlannerInfo *root, IndexOptInfo *index,
				  Relation heapRelation)
{
	List	   *tlist = NIL;
	Index		varno = index->rel->relid;
	ListCell   *indexpr_item;
	int			i;

	indexpr_item = list_head(index->indexprs);
	for (i = 0; i < index->ncolumns; i++)
	{
		int			indexkey = index->indexkeys[i];
		Expr	   *indexvar;

		if (indexkey != 0)
		{
			/* simple column */
			Form_pg_attribute att_tup;

			if (indexkey < 0)
				att_tup = SystemAttributeDefinition(indexkey,
													heapRelation->rd_rel->relhasoids);
			else
				att_tup = TupleDescAttr(heapRelation->rd_att, indexkey - 1);

			indexvar = (Expr *) makeVar(varno,
										indexkey,
										att_tup->atttypid,
										att_tup->atttypmod,
										att_tup->attcollation,
										0);
		}
		else
		{
			/* expression column */
			if (indexpr_item == NULL)
				elog(ERROR, "wrong number of index expressions");
			indexvar = (Expr *) lfirst(indexpr_item);
			indexpr_item = lnext(indexpr_item);
		}

		tlist = lappend(tlist,
						makeTargetEntry(indexvar,
										i + 1,
										NULL,
										false));
	}
	if (indexpr_item != NULL)
		elog(ERROR, "wrong number of index expressions");

	return tlist;
}

/*
 * restriction_selectivity
 *
 * Returns the selectivity of a specified restriction operator clause.
 * This code executes registered procedures stored in the
 * operator relation, by calling the function manager.
 *
 * See clause_selectivity() for the meaning of the additional parameters.
 */
Selectivity
restriction_selectivity(PlannerInfo *root,
						Oid operatorid,
						List *args,
						Oid inputcollid,
						int varRelid)
{
	RegProcedure oprrest = get_oprrest(operatorid);
	float8		result;

	/*
	 * if the oprrest procedure is missing for whatever reason, use a
	 * selectivity of 0.5
	 */
	if (!oprrest)
		return (Selectivity) 0.5;

	result = DatumGetFloat8(OidFunctionCall4Coll(oprrest,
												 inputcollid,
												 PointerGetDatum(root),
												 ObjectIdGetDatum(operatorid),
												 PointerGetDatum(args),
												 Int32GetDatum(varRelid)));

	if (result < 0.0 || result > 1.0)
		elog(ERROR, "invalid restriction selectivity: %f", result);

	return (Selectivity) result;
}

/*
 * join_selectivity
 *
 * Returns the selectivity of a specified join operator clause.
 * This code executes registered procedures stored in the
 * operator relation, by calling the function manager.
 */
Selectivity
join_selectivity(PlannerInfo *root,
				 Oid operatorid,
				 List *args,
				 Oid inputcollid,
				 JoinType jointype,
				 SpecialJoinInfo *sjinfo)
{
	RegProcedure oprjoin = get_oprjoin(operatorid);
	float8		result;

	/*
	 * if the oprjoin procedure is missing for whatever reason, use a
	 * selectivity of 0.5
	 */
	if (!oprjoin)
		return (Selectivity) 0.5;

	result = DatumGetFloat8(OidFunctionCall5Coll(oprjoin,
												 inputcollid,
												 PointerGetDatum(root),
												 ObjectIdGetDatum(operatorid),
												 PointerGetDatum(args),
												 Int16GetDatum(jointype),
												 PointerGetDatum(sjinfo)));

	if (result < 0.0 || result > 1.0)
		elog(ERROR, "invalid join selectivity: %f", result);

	return (Selectivity) result;
}

/*
 * has_unique_index
 *
 * Detect whether there is a unique index on the specified attribute
 * of the specified relation, thus allowing us to conclude that all
 * the (non-null) values of the attribute are distinct.
 *
 * This function does not check the index's indimmediate property, which
 * means that uniqueness may transiently fail to hold intra-transaction.
 * That's appropriate when we are making statistical estimates, but beware
 * of using this for any correctness proofs.
 */
bool
has_unique_index(RelOptInfo *rel, AttrNumber attno)
{
	ListCell   *ilist;

	foreach(ilist, rel->indexlist)
	{
		IndexOptInfo *index = (IndexOptInfo *) lfirst(ilist);

		/*
		 * Note: ignore partial indexes, since they don't allow us to conclude
		 * that all attr values are distinct, *unless* they are marked predOK
		 * which means we know the index's predicate is satisfied by the
		 * query. We don't take any interest in expressional indexes either.
		 * Also, a multicolumn unique index doesn't allow us to conclude that
		 * just the specified attr is unique.
		 */
		if (index->unique &&
			index->nkeycolumns == 1 &&
			index->indexkeys[0] == attno &&
			(index->indpred == NIL || index->predOK))
			return true;
	}
	return false;
}


/*
 * has_row_triggers
 *
 * Detect whether the specified relation has any row-level triggers for event.
 */
bool
has_row_triggers(PlannerInfo *root, Index rti, CmdType event)
{
	RangeTblEntry *rte = planner_rt_fetch(rti, root);
	Relation	relation;
	TriggerDesc *trigDesc;
	bool		result = false;

	/* Assume we already have adequate lock */
	relation = heap_open(rte->relid, NoLock);

	trigDesc = relation->trigdesc;
	switch (event)
	{
		case CMD_INSERT:
			if (trigDesc &&
				(trigDesc->trig_insert_after_row ||
				 trigDesc->trig_insert_before_row))
				result = true;
			break;
		case CMD_UPDATE:
			if (trigDesc &&
				(trigDesc->trig_update_after_row ||
				 trigDesc->trig_update_before_row))
				result = true;
			break;
		case CMD_DELETE:
			if (trigDesc &&
				(trigDesc->trig_delete_after_row ||
				 trigDesc->trig_delete_before_row))
				result = true;
			break;
		default:
			elog(ERROR, "unrecognized CmdType: %d", (int) event);
			break;
	}

	heap_close(relation, NoLock);
	return result;
}

/*
 * set_relation_partition_info
 *
 * Set partitioning scheme and related information for a partitioned table.
 */
static void
set_relation_partition_info(PlannerInfo *root, RelOptInfo *rel,
							Relation relation)
{
	PartitionDesc partdesc;
	PartitionKey partkey;

	Assert(relation->rd_rel->relkind == RELKIND_PARTITIONED_TABLE);

	partdesc = RelationGetPartitionDesc(relation);
	partkey = RelationGetPartitionKey(relation);
	rel->part_scheme = find_partition_scheme(root, relation);
	Assert(partdesc != NULL && rel->part_scheme != NULL);
	rel->boundinfo = partition_bounds_copy(partdesc->boundinfo, partkey);
	rel->nparts = partdesc->nparts;
	set_baserel_partition_key_exprs(relation, rel);
	rel->partition_qual = RelationGetPartitionQual(relation);
}

/*
 * find_partition_scheme
 *
 * Find or create a PartitionScheme for this Relation.
 */
static PartitionScheme
find_partition_scheme(PlannerInfo *root, Relation relation)
{
	PartitionKey partkey = RelationGetPartitionKey(relation);
	ListCell   *lc;
	int			partnatts,
				i;
	PartitionScheme part_scheme;

	/* A partitioned table should have a partition key. */
	Assert(partkey != NULL);

	partnatts = partkey->partnatts;

	/* Search for a matching partition scheme and return if found one. */
	foreach(lc, root->part_schemes)
	{
		part_scheme = lfirst(lc);

		/* Match partitioning strategy and number of keys. */
		if (partkey->strategy != part_scheme->strategy ||
			partnatts != part_scheme->partnatts)
			continue;

		/* Match partition key type properties. */
		if (memcmp(partkey->partopfamily, part_scheme->partopfamily,
				   sizeof(Oid) * partnatts) != 0 ||
			memcmp(partkey->partopcintype, part_scheme->partopcintype,
				   sizeof(Oid) * partnatts) != 0 ||
			memcmp(partkey->partcollation, part_scheme->partcollation,
				   sizeof(Oid) * partnatts) != 0)
			continue;

		/*
		 * Length and byval information should match when partopcintype
		 * matches.
		 */
		Assert(memcmp(partkey->parttyplen, part_scheme->parttyplen,
					  sizeof(int16) * partnatts) == 0);
		Assert(memcmp(partkey->parttypbyval, part_scheme->parttypbyval,
					  sizeof(bool) * partnatts) == 0);

		/*
		 * If partopfamily and partopcintype matched, must have the same
		 * partition comparison functions.  Note that we cannot reliably
		 * Assert the equality of function structs themselves for they might
		 * be different across PartitionKey's, so just Assert for the function
		 * OIDs.
		 */
#ifdef USE_ASSERT_CHECKING
		for (i = 0; i < partkey->partnatts; i++)
			Assert(partkey->partsupfunc[i].fn_oid ==
				   part_scheme->partsupfunc[i].fn_oid);
#endif

		/* Found matching partition scheme. */
		return part_scheme;
	}

	/*
	 * Did not find matching partition scheme. Create one copying relevant
	 * information from the relcache. We need to copy the contents of the
	 * array since the relcache entry may not survive after we have closed the
	 * relation.
	 */
	part_scheme = (PartitionScheme) palloc0(sizeof(PartitionSchemeData));
	part_scheme->strategy = partkey->strategy;
	part_scheme->partnatts = partkey->partnatts;

	part_scheme->partopfamily = (Oid *) palloc(sizeof(Oid) * partnatts);
	memcpy(part_scheme->partopfamily, partkey->partopfamily,
		   sizeof(Oid) * partnatts);

	part_scheme->partopcintype = (Oid *) palloc(sizeof(Oid) * partnatts);
	memcpy(part_scheme->partopcintype, partkey->partopcintype,
		   sizeof(Oid) * partnatts);

	part_scheme->partcollation = (Oid *) palloc(sizeof(Oid) * partnatts);
	memcpy(part_scheme->partcollation, partkey->partcollation,
		   sizeof(Oid) * partnatts);

	part_scheme->parttyplen = (int16 *) palloc(sizeof(int16) * partnatts);
	memcpy(part_scheme->parttyplen, partkey->parttyplen,
		   sizeof(int16) * partnatts);

	part_scheme->parttypbyval = (bool *) palloc(sizeof(bool) * partnatts);
	memcpy(part_scheme->parttypbyval, partkey->parttypbyval,
		   sizeof(bool) * partnatts);

	part_scheme->partsupfunc = (FmgrInfo *)
		palloc(sizeof(FmgrInfo) * partnatts);
	for (i = 0; i < partnatts; i++)
		fmgr_info_copy(&part_scheme->partsupfunc[i], &partkey->partsupfunc[i],
					   CurrentMemoryContext);

	/* Add the partitioning scheme to PlannerInfo. */
	root->part_schemes = lappend(root->part_schemes, part_scheme);

	return part_scheme;
}

/*
 * set_baserel_partition_key_exprs
 *
 * Builds partition key expressions for the given base relation and sets them
 * in given RelOptInfo.  Any single column partition keys are converted to Var
 * nodes.  All Var nodes are restamped with the relid of given relation.
 */
static void
set_baserel_partition_key_exprs(Relation relation,
								RelOptInfo *rel)
{
	PartitionKey partkey = RelationGetPartitionKey(relation);
	int			partnatts;
	int			cnt;
	List	  **partexprs;
	ListCell   *lc;
	Index		varno = rel->relid;

	Assert(IS_SIMPLE_REL(rel) && rel->relid > 0);

	/* A partitioned table should have a partition key. */
	Assert(partkey != NULL);

	partnatts = partkey->partnatts;
	partexprs = (List **) palloc(sizeof(List *) * partnatts);
	lc = list_head(partkey->partexprs);

	for (cnt = 0; cnt < partnatts; cnt++)
	{
		Expr	   *partexpr;
		AttrNumber	attno = partkey->partattrs[cnt];

		if (attno != InvalidAttrNumber)
		{
			/* Single column partition key is stored as a Var node. */
			Assert(attno > 0);

			partexpr = (Expr *) makeVar(varno, attno,
										partkey->parttypid[cnt],
										partkey->parttypmod[cnt],
										partkey->parttypcoll[cnt], 0);
		}
		else
		{
			if (lc == NULL)
				elog(ERROR, "wrong number of partition key expressions");

			/* Re-stamp the expression with given varno. */
			partexpr = (Expr *) copyObject(lfirst(lc));
			ChangeVarNodes((Node *) partexpr, 1, varno, 0);
			lc = lnext(lc);
		}

		partexprs[cnt] = list_make1(partexpr);
	}

	rel->partexprs = partexprs;

	/*
	 * A base relation can not have nullable partition key expressions. We
	 * still allocate array of empty expressions lists to keep partition key
	 * expression handling code simple. See build_joinrel_partition_info() and
	 * match_expr_to_partition_keys().
	 */
	rel->nullable_partexprs = (List **) palloc0(sizeof(List *) * partnatts);
}
