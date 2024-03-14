/*-------------------------------------------------------------------------
 *
 * core.c
 *	  Routines copied from PostgreSQL core distribution.
 *
 * The main purpose of this files is having access to static functions in core.
 * Another purpose is tweaking functions behavior by replacing part of them by
 * macro definitions. See at the end of pg_hint_plan.c for details. Anyway,
 * this file *must* contain required functions without making any change.
 *
 * This file contains the following functions from corresponding files.
 *
 * src/backend/optimizer/path/allpaths.c
 *
 *  public functions:
 *     standard_join_search(): This funcion is not static. The reason for
 *        including this function is make_rels_by_clause_joins. In order to
 *        avoid generating apparently unwanted join combination, we decided to
 *        change the behavior of make_join_rel, which is called under this
 *        function.
 *
 *	static functions:
 *	   set_plain_rel_pathlist()
 *	   set_append_rel_pathlist()
 *	   create_plain_partial_paths()
 *
 * src/backend/optimizer/path/joinrels.c
 *
 *	public functions:
 *     join_search_one_level(): We have to modify this to call my definition of
 * 		    make_rels_by_clause_joins.
 *
 *	static functions:
 *     make_rels_by_clause_joins()
 *     make_rels_by_clauseless_joins()
 *     join_is_legal()
 *     has_join_restriction()
 *     mark_dummy_rel()
 *     restriction_is_constant_false()
 *     try_partitionwise_join()
 *
 * Portions Copyright (c) 1996-2020, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */

static void populate_joinrel_with_paths(PlannerInfo *root, RelOptInfo *rel1,
							RelOptInfo *rel2, RelOptInfo *joinrel,
							SpecialJoinInfo *sjinfo, List *restrictlist);

/*
 * set_plain_rel_pathlist
 *	  Build access paths for a plain relation (no subquery, no inheritance)
 */
static void
set_plain_rel_pathlist(PlannerInfo *root, RelOptInfo *rel, RangeTblEntry *rte)
{
	Relids		required_outer;

	/*
	 * We don't support pushing join clauses into the quals of a seqscan, but
	 * it could still have required parameterization due to LATERAL refs in
	 * its tlist.
	 */
	required_outer = rel->lateral_relids;

	/* Consider sequential scan */
	add_path(rel, create_seqscan_path(root, rel, required_outer, 0));

	/* If appropriate, consider parallel sequential scan */
	if (rel->consider_parallel && required_outer == NULL)
		create_plain_partial_paths(root, rel);

	/* Consider index scans */
	create_index_paths(root, rel);

	/* Consider TID scans */
	create_tidscan_paths(root, rel);
}


/*
 * set_append_rel_pathlist
 *	  Build access paths for an "append relation"
 */
static void
set_append_rel_pathlist(PlannerInfo *root, RelOptInfo *rel,
						Index rti, RangeTblEntry *rte)
{
	int			parentRTindex = rti;
	List	   *live_childrels = NIL;
	ListCell   *l;

	/*
	 * Generate access paths for each member relation, and remember the
	 * non-dummy children.
	 */
	foreach(l, root->append_rel_list)
	{
		AppendRelInfo *appinfo = (AppendRelInfo *) lfirst(l);
		int			childRTindex;
		RangeTblEntry *childRTE;
		RelOptInfo *childrel;

		/* append_rel_list contains all append rels; ignore others */
		if (appinfo->parent_relid != parentRTindex)
			continue;

		/* Re-locate the child RTE and RelOptInfo */
		childRTindex = appinfo->child_relid;
		childRTE = root->simple_rte_array[childRTindex];
		childrel = root->simple_rel_array[childRTindex];

		/*
		 * If set_append_rel_size() decided the parent appendrel was
		 * parallel-unsafe at some point after visiting this child rel, we
		 * need to propagate the unsafety marking down to the child, so that
		 * we don't generate useless partial paths for it.
		 */
		if (!rel->consider_parallel)
			childrel->consider_parallel = false;

		/*
		 * Compute the child's access paths.
		 */
		set_rel_pathlist(root, childrel, childRTindex, childRTE);

		/*
		 * If child is dummy, ignore it.
		 */
		if (IS_DUMMY_REL(childrel))
			continue;

		/* Bubble up childrel's partitioned children. */
		if (rel->part_scheme)
			rel->partitioned_child_rels =
				list_concat(rel->partitioned_child_rels,
							list_copy(childrel->partitioned_child_rels));

		/*
		 * Child is live, so add it to the live_childrels list for use below.
		 */
		live_childrels = lappend(live_childrels, childrel);
	}

	/* Add paths to the append relation. */
	add_paths_to_append_rel(root, rel, live_childrels);
}


/*
 * standard_join_search
 *	  Find possible joinpaths for a query by successively finding ways
 *	  to join component relations into join relations.
 *
 * 'levels_needed' is the number of iterations needed, ie, the number of
 *		independent jointree items in the query.  This is > 1.
 *
 * 'initial_rels' is a list of RelOptInfo nodes for each independent
 *		jointree item.  These are the components to be joined together.
 *		Note that levels_needed == list_length(initial_rels).
 *
 * Returns the final level of join relations, i.e., the relation that is
 * the result of joining all the original relations together.
 * At least one implementation path must be provided for this relation and
 * all required sub-relations.
 *
 * To support loadable plugins that modify planner behavior by changing the
 * join searching algorithm, we provide a hook variable that lets a plugin
 * replace or supplement this function.  Any such hook must return the same
 * final join relation as the standard code would, but it might have a
 * different set of implementation paths attached, and only the sub-joinrels
 * needed for these paths need have been instantiated.
 *
 * Note to plugin authors: the functions invoked during standard_join_search()
 * modify root->join_rel_list and root->join_rel_hash.  If you want to do more
 * than one join-order search, you'll probably need to save and restore the
 * original states of those data structures.  See geqo_eval() for an example.
 */
RelOptInfo *
standard_join_search(PlannerInfo *root, int levels_needed, List *initial_rels)
{
	int			lev;
	RelOptInfo *rel;

	/*
	 * This function cannot be invoked recursively within any one planning
	 * problem, so join_rel_level[] can't be in use already.
	 */
	Assert(root->join_rel_level == NULL);

	/*
	 * We employ a simple "dynamic programming" algorithm: we first find all
	 * ways to build joins of two jointree items, then all ways to build joins
	 * of three items (from two-item joins and single items), then four-item
	 * joins, and so on until we have considered all ways to join all the
	 * items into one rel.
	 *
	 * root->join_rel_level[j] is a list of all the j-item rels.  Initially we
	 * set root->join_rel_level[1] to represent all the single-jointree-item
	 * relations.
	 */
	root->join_rel_level = (List **) palloc0((levels_needed + 1) * sizeof(List *));

	root->join_rel_level[1] = initial_rels;

	for (lev = 2; lev <= levels_needed; lev++)
	{
		ListCell   *lc;

		/*
		 * Determine all possible pairs of relations to be joined at this
		 * level, and build paths for making each one from every available
		 * pair of lower-level relations.
		 */
		join_search_one_level(root, lev);

		/*
		 * Run generate_partitionwise_join_paths() and generate_gather_paths()
		 * for each just-processed joinrel.  We could not do this earlier
		 * because both regular and partial paths can get added to a
		 * particular joinrel at multiple times within join_search_one_level.
		 *
		 * After that, we're done creating paths for the joinrel, so run
		 * set_cheapest().
		 */
		foreach(lc, root->join_rel_level[lev])
		{
			rel = (RelOptInfo *) lfirst(lc);

			/* Create paths for partitionwise joins. */
			generate_partitionwise_join_paths(root, rel);

			/*
			 * Except for the topmost scan/join rel, consider gathering
			 * partial paths.  We'll do the same for the topmost scan/join rel
			 * once we know the final targetlist (see grouping_planner).
			 */
			if (lev < levels_needed)
				generate_gather_paths(root, rel, false);

			/* Find and save the cheapest paths for this rel */
			set_cheapest(rel);

#ifdef OPTIMIZER_DEBUG
			debug_print_rel(root, rel);
#endif
		}
	}

	/*
	 * We should have a single rel at the final level.
	 */
	if (root->join_rel_level[levels_needed] == NIL)
		elog(ERROR, "failed to build any %d-way joins", levels_needed);
	Assert(list_length(root->join_rel_level[levels_needed]) == 1);

	rel = (RelOptInfo *) linitial(root->join_rel_level[levels_needed]);

	root->join_rel_level = NULL;

	return rel;
}


/*
 * create_plain_partial_paths
 *	  Build partial access paths for parallel scan of a plain relation
 */
static void
create_plain_partial_paths(PlannerInfo *root, RelOptInfo *rel)
{
	int			parallel_workers;

	parallel_workers = compute_parallel_worker(rel, rel->pages, -1,
											   max_parallel_workers_per_gather);

	/* If any limit was set to zero, the user doesn't want a parallel scan. */
	if (parallel_workers <= 0)
		return;

	/* Add an unordered partial path based on a parallel sequential scan. */
	add_partial_path(rel, create_seqscan_path(root, rel, NULL, parallel_workers));
}


/*
 * join_search_one_level
 *	  Consider ways to produce join relations containing exactly 'level'
 *	  jointree items.  (This is one step of the dynamic-programming method
 *	  embodied in standard_join_search.)  Join rel nodes for each feasible
 *	  combination of lower-level rels are created and returned in a list.
 *	  Implementation paths are created for each such joinrel, too.
 *
 * level: level of rels we want to make this time
 * root->join_rel_level[j], 1 <= j < level, is a list of rels containing j items
 *
 * The result is returned in root->join_rel_level[level].
 */
void
join_search_one_level(PlannerInfo *root, int level)
{
	List	  **joinrels = root->join_rel_level;
	ListCell   *r;
	int			k;

	Assert(joinrels[level] == NIL);

	/* Set join_cur_level so that new joinrels are added to proper list */
	root->join_cur_level = level;

	/*
	 * First, consider left-sided and right-sided plans, in which rels of
	 * exactly level-1 member relations are joined against initial relations.
	 * We prefer to join using join clauses, but if we find a rel of level-1
	 * members that has no join clauses, we will generate Cartesian-product
	 * joins against all initial rels not already contained in it.
	 */
	foreach(r, joinrels[level - 1])
	{
		RelOptInfo *old_rel = (RelOptInfo *) lfirst(r);

		if (old_rel->joininfo != NIL || old_rel->has_eclass_joins ||
			has_join_restriction(root, old_rel))
		{
			/*
			 * There are join clauses or join order restrictions relevant to
			 * this rel, so consider joins between this rel and (only) those
			 * initial rels it is linked to by a clause or restriction.
			 *
			 * At level 2 this condition is symmetric, so there is no need to
			 * look at initial rels before this one in the list; we already
			 * considered such joins when we were at the earlier rel.  (The
			 * mirror-image joins are handled automatically by make_join_rel.)
			 * In later passes (level > 2), we join rels of the previous level
			 * to each initial rel they don't already include but have a join
			 * clause or restriction with.
			 */
			ListCell   *other_rels;

			if (level == 2)		/* consider remaining initial rels */
				other_rels = lnext(r);
			else				/* consider all initial rels */
				other_rels = list_head(joinrels[1]);

			make_rels_by_clause_joins(root,
									  old_rel,
									  other_rels);
		}
		else
		{
			/*
			 * Oops, we have a relation that is not joined to any other
			 * relation, either directly or by join-order restrictions.
			 * Cartesian product time.
			 *
			 * We consider a cartesian product with each not-already-included
			 * initial rel, whether it has other join clauses or not.  At
			 * level 2, if there are two or more clauseless initial rels, we
			 * will redundantly consider joining them in both directions; but
			 * such cases aren't common enough to justify adding complexity to
			 * avoid the duplicated effort.
			 */
			make_rels_by_clauseless_joins(root,
										  old_rel,
										  list_head(joinrels[1]));
		}
	}

	/*
	 * Now, consider "bushy plans" in which relations of k initial rels are
	 * joined to relations of level-k initial rels, for 2 <= k <= level-2.
	 *
	 * We only consider bushy-plan joins for pairs of rels where there is a
	 * suitable join clause (or join order restriction), in order to avoid
	 * unreasonable growth of planning time.
	 */
	for (k = 2;; k++)
	{
		int			other_level = level - k;

		/*
		 * Since make_join_rel(x, y) handles both x,y and y,x cases, we only
		 * need to go as far as the halfway point.
		 */
		if (k > other_level)
			break;

		foreach(r, joinrels[k])
		{
			RelOptInfo *old_rel = (RelOptInfo *) lfirst(r);
			ListCell   *other_rels;
			ListCell   *r2;

			/*
			 * We can ignore relations without join clauses here, unless they
			 * participate in join-order restrictions --- then we might have
			 * to force a bushy join plan.
			 */
			if (old_rel->joininfo == NIL && !old_rel->has_eclass_joins &&
				!has_join_restriction(root, old_rel))
				continue;

			if (k == other_level)
				other_rels = lnext(r);	/* only consider remaining rels */
			else
				other_rels = list_head(joinrels[other_level]);

			for_each_cell(r2, other_rels)
			{
				RelOptInfo *new_rel = (RelOptInfo *) lfirst(r2);

				if (!bms_overlap(old_rel->relids, new_rel->relids))
				{
					/*
					 * OK, we can build a rel of the right level from this
					 * pair of rels.  Do so if there is at least one relevant
					 * join clause or join order restriction.
					 */
					if (have_relevant_joinclause(root, old_rel, new_rel) ||
						have_join_order_restriction(root, old_rel, new_rel))
					{
						(void) make_join_rel(root, old_rel, new_rel);
					}
				}
			}
		}
	}

	/*----------
	 * Last-ditch effort: if we failed to find any usable joins so far, force
	 * a set of cartesian-product joins to be generated.  This handles the
	 * special case where all the available rels have join clauses but we
	 * cannot use any of those clauses yet.  This can only happen when we are
	 * considering a join sub-problem (a sub-joinlist) and all the rels in the
	 * sub-problem have only join clauses with rels outside the sub-problem.
	 * An example is
	 *
	 *		SELECT ... FROM a INNER JOIN b ON TRUE, c, d, ...
	 *		WHERE a.w = c.x and b.y = d.z;
	 *
	 * If the "a INNER JOIN b" sub-problem does not get flattened into the
	 * upper level, we must be willing to make a cartesian join of a and b;
	 * but the code above will not have done so, because it thought that both
	 * a and b have joinclauses.  We consider only left-sided and right-sided
	 * cartesian joins in this case (no bushy).
	 *----------
	 */
	if (joinrels[level] == NIL)
	{
		/*
		 * This loop is just like the first one, except we always call
		 * make_rels_by_clauseless_joins().
		 */
		foreach(r, joinrels[level - 1])
		{
			RelOptInfo *old_rel = (RelOptInfo *) lfirst(r);

			make_rels_by_clauseless_joins(root,
										  old_rel,
										  list_head(joinrels[1]));
		}

		/*----------
		 * When special joins are involved, there may be no legal way
		 * to make an N-way join for some values of N.  For example consider
		 *
		 * SELECT ... FROM t1 WHERE
		 *	 x IN (SELECT ... FROM t2,t3 WHERE ...) AND
		 *	 y IN (SELECT ... FROM t4,t5 WHERE ...)
		 *
		 * We will flatten this query to a 5-way join problem, but there are
		 * no 4-way joins that join_is_legal() will consider legal.  We have
		 * to accept failure at level 4 and go on to discover a workable
		 * bushy plan at level 5.
		 *
		 * However, if there are no special joins and no lateral references
		 * then join_is_legal() should never fail, and so the following sanity
		 * check is useful.
		 *----------
		 */
		if (joinrels[level] == NIL &&
			root->join_info_list == NIL &&
			!root->hasLateralRTEs)
			elog(ERROR, "failed to build any %d-way joins", level);
	}
}


/*
 * make_rels_by_clause_joins
 *	  Build joins between the given relation 'old_rel' and other relations
 *	  that participate in join clauses that 'old_rel' also participates in
 *	  (or participate in join-order restrictions with it).
 *	  The join rels are returned in root->join_rel_level[join_cur_level].
 *
 * Note: at levels above 2 we will generate the same joined relation in
 * multiple ways --- for example (a join b) join c is the same RelOptInfo as
 * (b join c) join a, though the second case will add a different set of Paths
 * to it.  This is the reason for using the join_rel_level mechanism, which
 * automatically ensures that each new joinrel is only added to the list once.
 *
 * 'old_rel' is the relation entry for the relation to be joined
 * 'other_rels': the first cell in a linked list containing the other
 * rels to be considered for joining
 *
 * Currently, this is only used with initial rels in other_rels, but it
 * will work for joining to joinrels too.
 */
static void
make_rels_by_clause_joins(PlannerInfo *root,
						  RelOptInfo *old_rel,
						  ListCell *other_rels)
{
	ListCell   *l;

	for_each_cell(l, other_rels)
	{
		RelOptInfo *other_rel = (RelOptInfo *) lfirst(l);

		if (!bms_overlap(old_rel->relids, other_rel->relids) &&
			(have_relevant_joinclause(root, old_rel, other_rel) ||
			 have_join_order_restriction(root, old_rel, other_rel)))
		{
			(void) make_join_rel(root, old_rel, other_rel);
		}
	}
}


/*
 * make_rels_by_clauseless_joins
 *	  Given a relation 'old_rel' and a list of other relations
 *	  'other_rels', create a join relation between 'old_rel' and each
 *	  member of 'other_rels' that isn't already included in 'old_rel'.
 *	  The join rels are returned in root->join_rel_level[join_cur_level].
 *
 * 'old_rel' is the relation entry for the relation to be joined
 * 'other_rels': the first cell of a linked list containing the
 * other rels to be considered for joining
 *
 * Currently, this is only used with initial rels in other_rels, but it would
 * work for joining to joinrels too.
 */
static void
make_rels_by_clauseless_joins(PlannerInfo *root,
							  RelOptInfo *old_rel,
							  ListCell *other_rels)
{
	ListCell   *l;

	for_each_cell(l, other_rels)
	{
		RelOptInfo *other_rel = (RelOptInfo *) lfirst(l);

		if (!bms_overlap(other_rel->relids, old_rel->relids))
		{
			(void) make_join_rel(root, old_rel, other_rel);
		}
	}
}


/*
 * join_is_legal
 *	   Determine whether a proposed join is legal given the query's
 *	   join order constraints; and if it is, determine the join type.
 *
 * Caller must supply not only the two rels, but the union of their relids.
 * (We could simplify the API by computing joinrelids locally, but this
 * would be redundant work in the normal path through make_join_rel.)
 *
 * On success, *sjinfo_p is set to NULL if this is to be a plain inner join,
 * else it's set to point to the associated SpecialJoinInfo node.  Also,
 * *reversed_p is set true if the given relations need to be swapped to
 * match the SpecialJoinInfo node.
 */
static bool
join_is_legal(PlannerInfo *root, RelOptInfo *rel1, RelOptInfo *rel2,
			  Relids joinrelids,
			  SpecialJoinInfo **sjinfo_p, bool *reversed_p)
{
	SpecialJoinInfo *match_sjinfo;
	bool		reversed;
	bool		unique_ified;
	bool		must_be_leftjoin;
	ListCell   *l;

	/*
	 * Ensure output params are set on failure return.  This is just to
	 * suppress uninitialized-variable warnings from overly anal compilers.
	 */
	*sjinfo_p = NULL;
	*reversed_p = false;

	/*
	 * If we have any special joins, the proposed join might be illegal; and
	 * in any case we have to determine its join type.  Scan the join info
	 * list for matches and conflicts.
	 */
	match_sjinfo = NULL;
	reversed = false;
	unique_ified = false;
	must_be_leftjoin = false;

	foreach(l, root->join_info_list)
	{
		SpecialJoinInfo *sjinfo = (SpecialJoinInfo *) lfirst(l);

		/*
		 * This special join is not relevant unless its RHS overlaps the
		 * proposed join.  (Check this first as a fast path for dismissing
		 * most irrelevant SJs quickly.)
		 */
		if (!bms_overlap(sjinfo->min_righthand, joinrelids))
			continue;

		/*
		 * Also, not relevant if proposed join is fully contained within RHS
		 * (ie, we're still building up the RHS).
		 */
		if (bms_is_subset(joinrelids, sjinfo->min_righthand))
			continue;

		/*
		 * Also, not relevant if SJ is already done within either input.
		 */
		if (bms_is_subset(sjinfo->min_lefthand, rel1->relids) &&
			bms_is_subset(sjinfo->min_righthand, rel1->relids))
			continue;
		if (bms_is_subset(sjinfo->min_lefthand, rel2->relids) &&
			bms_is_subset(sjinfo->min_righthand, rel2->relids))
			continue;

		/*
		 * If it's a semijoin and we already joined the RHS to any other rels
		 * within either input, then we must have unique-ified the RHS at that
		 * point (see below).  Therefore the semijoin is no longer relevant in
		 * this join path.
		 */
		if (sjinfo->jointype == JOIN_SEMI)
		{
			if (bms_is_subset(sjinfo->syn_righthand, rel1->relids) &&
				!bms_equal(sjinfo->syn_righthand, rel1->relids))
				continue;
			if (bms_is_subset(sjinfo->syn_righthand, rel2->relids) &&
				!bms_equal(sjinfo->syn_righthand, rel2->relids))
				continue;
		}

		/*
		 * If one input contains min_lefthand and the other contains
		 * min_righthand, then we can perform the SJ at this join.
		 *
		 * Reject if we get matches to more than one SJ; that implies we're
		 * considering something that's not really valid.
		 */
		if (bms_is_subset(sjinfo->min_lefthand, rel1->relids) &&
			bms_is_subset(sjinfo->min_righthand, rel2->relids))
		{
			if (match_sjinfo)
				return false;	/* invalid join path */
			match_sjinfo = sjinfo;
			reversed = false;
		}
		else if (bms_is_subset(sjinfo->min_lefthand, rel2->relids) &&
				 bms_is_subset(sjinfo->min_righthand, rel1->relids))
		{
			if (match_sjinfo)
				return false;	/* invalid join path */
			match_sjinfo = sjinfo;
			reversed = true;
		}
		else if (sjinfo->jointype == JOIN_SEMI &&
				 bms_equal(sjinfo->syn_righthand, rel2->relids) &&
				 create_unique_path(root, rel2, rel2->cheapest_total_path,
									sjinfo) != NULL)
		{
			/*----------
			 * For a semijoin, we can join the RHS to anything else by
			 * unique-ifying the RHS (if the RHS can be unique-ified).
			 * We will only get here if we have the full RHS but less
			 * than min_lefthand on the LHS.
			 *
			 * The reason to consider such a join path is exemplified by
			 *	SELECT ... FROM a,b WHERE (a.x,b.y) IN (SELECT c1,c2 FROM c)
			 * If we insist on doing this as a semijoin we will first have
			 * to form the cartesian product of A*B.  But if we unique-ify
			 * C then the semijoin becomes a plain innerjoin and we can join
			 * in any order, eg C to A and then to B.  When C is much smaller
			 * than A and B this can be a huge win.  So we allow C to be
			 * joined to just A or just B here, and then make_join_rel has
			 * to handle the case properly.
			 *
			 * Note that actually we'll allow unique-ified C to be joined to
			 * some other relation D here, too.  That is legal, if usually not
			 * very sane, and this routine is only concerned with legality not
			 * with whether the join is good strategy.
			 *----------
			 */
			if (match_sjinfo)
				return false;	/* invalid join path */
			match_sjinfo = sjinfo;
			reversed = false;
			unique_ified = true;
		}
		else if (sjinfo->jointype == JOIN_SEMI &&
				 bms_equal(sjinfo->syn_righthand, rel1->relids) &&
				 create_unique_path(root, rel1, rel1->cheapest_total_path,
									sjinfo) != NULL)
		{
			/* Reversed semijoin case */
			if (match_sjinfo)
				return false;	/* invalid join path */
			match_sjinfo = sjinfo;
			reversed = true;
			unique_ified = true;
		}
		else
		{
			/*
			 * Otherwise, the proposed join overlaps the RHS but isn't a valid
			 * implementation of this SJ.  But don't panic quite yet: the RHS
			 * violation might have occurred previously, in one or both input
			 * relations, in which case we must have previously decided that
			 * it was OK to commute some other SJ with this one.  If we need
			 * to perform this join to finish building up the RHS, rejecting
			 * it could lead to not finding any plan at all.  (This can occur
			 * because of the heuristics elsewhere in this file that postpone
			 * clauseless joins: we might not consider doing a clauseless join
			 * within the RHS until after we've performed other, validly
			 * commutable SJs with one or both sides of the clauseless join.)
			 * This consideration boils down to the rule that if both inputs
			 * overlap the RHS, we can allow the join --- they are either
			 * fully within the RHS, or represent previously-allowed joins to
			 * rels outside it.
			 */
			if (bms_overlap(rel1->relids, sjinfo->min_righthand) &&
				bms_overlap(rel2->relids, sjinfo->min_righthand))
				continue;		/* assume valid previous violation of RHS */

			/*
			 * The proposed join could still be legal, but only if we're
			 * allowed to associate it into the RHS of this SJ.  That means
			 * this SJ must be a LEFT join (not SEMI or ANTI, and certainly
			 * not FULL) and the proposed join must not overlap the LHS.
			 */
			if (sjinfo->jointype != JOIN_LEFT ||
				bms_overlap(joinrelids, sjinfo->min_lefthand))
				return false;	/* invalid join path */

			/*
			 * To be valid, the proposed join must be a LEFT join; otherwise
			 * it can't associate into this SJ's RHS.  But we may not yet have
			 * found the SpecialJoinInfo matching the proposed join, so we
			 * can't test that yet.  Remember the requirement for later.
			 */
			must_be_leftjoin = true;
		}
	}

	/*
	 * Fail if violated any SJ's RHS and didn't match to a LEFT SJ: the
	 * proposed join can't associate into an SJ's RHS.
	 *
	 * Also, fail if the proposed join's predicate isn't strict; we're
	 * essentially checking to see if we can apply outer-join identity 3, and
	 * that's a requirement.  (This check may be redundant with checks in
	 * make_outerjoininfo, but I'm not quite sure, and it's cheap to test.)
	 */
	if (must_be_leftjoin &&
		(match_sjinfo == NULL ||
		 match_sjinfo->jointype != JOIN_LEFT ||
		 !match_sjinfo->lhs_strict))
		return false;			/* invalid join path */

	/*
	 * We also have to check for constraints imposed by LATERAL references.
	 */
	if (root->hasLateralRTEs)
	{
		bool		lateral_fwd;
		bool		lateral_rev;
		Relids		join_lateral_rels;

		/*
		 * The proposed rels could each contain lateral references to the
		 * other, in which case the join is impossible.  If there are lateral
		 * references in just one direction, then the join has to be done with
		 * a nestloop with the lateral referencer on the inside.  If the join
		 * matches an SJ that cannot be implemented by such a nestloop, the
		 * join is impossible.
		 *
		 * Also, if the lateral reference is only indirect, we should reject
		 * the join; whatever rel(s) the reference chain goes through must be
		 * joined to first.
		 *
		 * Another case that might keep us from building a valid plan is the
		 * implementation restriction described by have_dangerous_phv().
		 */
		lateral_fwd = bms_overlap(rel1->relids, rel2->lateral_relids);
		lateral_rev = bms_overlap(rel2->relids, rel1->lateral_relids);
		if (lateral_fwd && lateral_rev)
			return false;		/* have lateral refs in both directions */
		if (lateral_fwd)
		{
			/* has to be implemented as nestloop with rel1 on left */
			if (match_sjinfo &&
				(reversed ||
				 unique_ified ||
				 match_sjinfo->jointype == JOIN_FULL))
				return false;	/* not implementable as nestloop */
			/* check there is a direct reference from rel2 to rel1 */
			if (!bms_overlap(rel1->relids, rel2->direct_lateral_relids))
				return false;	/* only indirect refs, so reject */
			/* check we won't have a dangerous PHV */
			if (have_dangerous_phv(root, rel1->relids, rel2->lateral_relids))
				return false;	/* might be unable to handle required PHV */
		}
		else if (lateral_rev)
		{
			/* has to be implemented as nestloop with rel2 on left */
			if (match_sjinfo &&
				(!reversed ||
				 unique_ified ||
				 match_sjinfo->jointype == JOIN_FULL))
				return false;	/* not implementable as nestloop */
			/* check there is a direct reference from rel1 to rel2 */
			if (!bms_overlap(rel2->relids, rel1->direct_lateral_relids))
				return false;	/* only indirect refs, so reject */
			/* check we won't have a dangerous PHV */
			if (have_dangerous_phv(root, rel2->relids, rel1->lateral_relids))
				return false;	/* might be unable to handle required PHV */
		}

		/*
		 * LATERAL references could also cause problems later on if we accept
		 * this join: if the join's minimum parameterization includes any rels
		 * that would have to be on the inside of an outer join with this join
		 * rel, then it's never going to be possible to build the complete
		 * query using this join.  We should reject this join not only because
		 * it'll save work, but because if we don't, the clauseless-join
		 * heuristics might think that legality of this join means that some
		 * other join rel need not be formed, and that could lead to failure
		 * to find any plan at all.  We have to consider not only rels that
		 * are directly on the inner side of an OJ with the joinrel, but also
		 * ones that are indirectly so, so search to find all such rels.
		 */
		join_lateral_rels = min_join_parameterization(root, joinrelids,
													  rel1, rel2);
		if (join_lateral_rels)
		{
			Relids		join_plus_rhs = bms_copy(joinrelids);
			bool		more;

			do
			{
				more = false;
				foreach(l, root->join_info_list)
				{
					SpecialJoinInfo *sjinfo = (SpecialJoinInfo *) lfirst(l);

					/* ignore full joins --- their ordering is predetermined */
					if (sjinfo->jointype == JOIN_FULL)
						continue;

					if (bms_overlap(sjinfo->min_lefthand, join_plus_rhs) &&
						!bms_is_subset(sjinfo->min_righthand, join_plus_rhs))
					{
						join_plus_rhs = bms_add_members(join_plus_rhs,
														sjinfo->min_righthand);
						more = true;
					}
				}
			} while (more);
			if (bms_overlap(join_plus_rhs, join_lateral_rels))
				return false;	/* will not be able to join to some RHS rel */
		}
	}

	/* Otherwise, it's a valid join */
	*sjinfo_p = match_sjinfo;
	*reversed_p = reversed;
	return true;
}


/*
 * has_join_restriction
 *		Detect whether the specified relation has join-order restrictions,
 *		due to being inside an outer join or an IN (sub-SELECT),
 *		or participating in any LATERAL references or multi-rel PHVs.
 *
 * Essentially, this tests whether have_join_order_restriction() could
 * succeed with this rel and some other one.  It's OK if we sometimes
 * say "true" incorrectly.  (Therefore, we don't bother with the relatively
 * expensive has_legal_joinclause test.)
 */
static bool
has_join_restriction(PlannerInfo *root, RelOptInfo *rel)
{
	ListCell   *l;

	if (rel->lateral_relids != NULL || rel->lateral_referencers != NULL)
		return true;

	foreach(l, root->placeholder_list)
	{
		PlaceHolderInfo *phinfo = (PlaceHolderInfo *) lfirst(l);

		if (bms_is_subset(rel->relids, phinfo->ph_eval_at) &&
			!bms_equal(rel->relids, phinfo->ph_eval_at))
			return true;
	}

	foreach(l, root->join_info_list)
	{
		SpecialJoinInfo *sjinfo = (SpecialJoinInfo *) lfirst(l);

		/* ignore full joins --- other mechanisms preserve their ordering */
		if (sjinfo->jointype == JOIN_FULL)
			continue;

		/* ignore if SJ is already contained in rel */
		if (bms_is_subset(sjinfo->min_lefthand, rel->relids) &&
			bms_is_subset(sjinfo->min_righthand, rel->relids))
			continue;

		/* restricted if it overlaps LHS or RHS, but doesn't contain SJ */
		if (bms_overlap(sjinfo->min_lefthand, rel->relids) ||
			bms_overlap(sjinfo->min_righthand, rel->relids))
			return true;
	}

	return false;
}


/*
 * Mark a relation as proven empty.
 *
 * During GEQO planning, this can get invoked more than once on the same
 * baserel struct, so it's worth checking to see if the rel is already marked
 * dummy.
 *
 * Also, when called during GEQO join planning, we are in a short-lived
 * memory context.  We must make sure that the dummy path attached to a
 * baserel survives the GEQO cycle, else the baserel is trashed for future
 * GEQO cycles.  On the other hand, when we are marking a joinrel during GEQO,
 * we don't want the dummy path to clutter the main planning context.  Upshot
 * is that the best solution is to explicitly make the dummy path in the same
 * context the given RelOptInfo is in.
 */
void
mark_dummy_rel(RelOptInfo *rel)
{
	MemoryContext oldcontext;

	/* Already marked? */
	if (is_dummy_rel(rel))
		return;

	/* No, so choose correct context to make the dummy path in */
	oldcontext = MemoryContextSwitchTo(GetMemoryChunkContext(rel));

	/* Set dummy size estimate */
	rel->rows = 0;

	/* Evict any previously chosen paths */
	rel->pathlist = NIL;
	rel->partial_pathlist = NIL;

	/* Set up the dummy path */
	add_path(rel, (Path *) create_append_path(NULL, rel, NIL, NIL,
											  rel->lateral_relids,
											  0, false, NIL, -1));

	/* Set or update cheapest_total_path and related fields */
	set_cheapest(rel);

	MemoryContextSwitchTo(oldcontext);
}


/*
 * restriction_is_constant_false --- is a restrictlist just FALSE?
 *
 * In cases where a qual is provably constant FALSE, eval_const_expressions
 * will generally have thrown away anything that's ANDed with it.  In outer
 * join situations this will leave us computing cartesian products only to
 * decide there's no match for an outer row, which is pretty stupid.  So,
 * we need to detect the case.
 *
 * If only_pushed_down is true, then consider only quals that are pushed-down
 * from the point of view of the joinrel.
 */
static bool
restriction_is_constant_false(List *restrictlist,
							  RelOptInfo *joinrel,
							  bool only_pushed_down)
{
	ListCell   *lc;

	/*
	 * Despite the above comment, the restriction list we see here might
	 * possibly have other members besides the FALSE constant, since other
	 * quals could get "pushed down" to the outer join level.  So we check
	 * each member of the list.
	 */
	foreach(lc, restrictlist)
	{
		RestrictInfo *rinfo = lfirst_node(RestrictInfo, lc);

		if (only_pushed_down && !RINFO_IS_PUSHED_DOWN(rinfo, joinrel->relids))
			continue;

		if (rinfo->clause && IsA(rinfo->clause, Const))
		{
			Const	   *con = (Const *) rinfo->clause;

			/* constant NULL is as good as constant FALSE for our purposes */
			if (con->constisnull)
				return true;
			if (!DatumGetBool(con->constvalue))
				return true;
		}
	}
	return false;
}


/*
 * Assess whether join between given two partitioned relations can be broken
 * down into joins between matching partitions; a technique called
 * "partitionwise join"
 *
 * Partitionwise join is possible when a. Joining relations have same
 * partitioning scheme b. There exists an equi-join between the partition keys
 * of the two relations.
 *
 * Partitionwise join is planned as follows (details: optimizer/README.)
 *
 * 1. Create the RelOptInfos for joins between matching partitions i.e
 * child-joins and add paths to them.
 *
 * 2. Construct Append or MergeAppend paths across the set of child joins.
 * This second phase is implemented by generate_partitionwise_join_paths().
 *
 * The RelOptInfo, SpecialJoinInfo and restrictlist for each child join are
 * obtained by translating the respective parent join structures.
 */
static void
try_partitionwise_join(PlannerInfo *root, RelOptInfo *rel1, RelOptInfo *rel2,
					   RelOptInfo *joinrel, SpecialJoinInfo *parent_sjinfo,
					   List *parent_restrictlist)
{
	bool		rel1_is_simple = IS_SIMPLE_REL(rel1);
	bool		rel2_is_simple = IS_SIMPLE_REL(rel2);
	int			nparts;
	int			cnt_parts;

	/* Guard against stack overflow due to overly deep partition hierarchy. */
	check_stack_depth();

	/* Nothing to do, if the join relation is not partitioned. */
	if (!IS_PARTITIONED_REL(joinrel))
		return;

	/* The join relation should have consider_partitionwise_join set. */
	Assert(joinrel->consider_partitionwise_join);

	/*
	 * Since this join relation is partitioned, all the base relations
	 * participating in this join must be partitioned and so are all the
	 * intermediate join relations.
	 */
	Assert(IS_PARTITIONED_REL(rel1) && IS_PARTITIONED_REL(rel2));
	Assert(REL_HAS_ALL_PART_PROPS(rel1) && REL_HAS_ALL_PART_PROPS(rel2));

	/* The joining relations should have consider_partitionwise_join set. */
	Assert(rel1->consider_partitionwise_join &&
		   rel2->consider_partitionwise_join);

	/*
	 * The partition scheme of the join relation should match that of the
	 * joining relations.
	 */
	Assert(joinrel->part_scheme == rel1->part_scheme &&
		   joinrel->part_scheme == rel2->part_scheme);

	/*
	 * Since we allow partitionwise join only when the partition bounds of the
	 * joining relations exactly match, the partition bounds of the join
	 * should match those of the joining relations.
	 */
	Assert(partition_bounds_equal(joinrel->part_scheme->partnatts,
								  joinrel->part_scheme->parttyplen,
								  joinrel->part_scheme->parttypbyval,
								  joinrel->boundinfo, rel1->boundinfo));
	Assert(partition_bounds_equal(joinrel->part_scheme->partnatts,
								  joinrel->part_scheme->parttyplen,
								  joinrel->part_scheme->parttypbyval,
								  joinrel->boundinfo, rel2->boundinfo));

	nparts = joinrel->nparts;

	/*
	 * Create child-join relations for this partitioned join, if those don't
	 * exist. Add paths to child-joins for a pair of child relations
	 * corresponding to the given pair of parent relations.
	 */
	for (cnt_parts = 0; cnt_parts < nparts; cnt_parts++)
	{
		RelOptInfo *child_rel1 = rel1->part_rels[cnt_parts];
		RelOptInfo *child_rel2 = rel2->part_rels[cnt_parts];
		bool		rel1_empty = (child_rel1 == NULL ||
								  IS_DUMMY_REL(child_rel1));
		bool		rel2_empty = (child_rel2 == NULL ||
								  IS_DUMMY_REL(child_rel2));
		SpecialJoinInfo *child_sjinfo;
		List	   *child_restrictlist;
		RelOptInfo *child_joinrel;
		Relids		child_joinrelids;
		AppendRelInfo **appinfos;
		int			nappinfos;

		/*
		 * Check for cases where we can prove that this segment of the join
		 * returns no rows, due to one or both inputs being empty (including
		 * inputs that have been pruned away entirely).  If so just ignore it.
		 * These rules are equivalent to populate_joinrel_with_paths's rules
		 * for dummy input relations.
		 */
		switch (parent_sjinfo->jointype)
		{
			case JOIN_INNER:
			case JOIN_SEMI:
				if (rel1_empty || rel2_empty)
					continue;	/* ignore this join segment */
				break;
			case JOIN_LEFT:
			case JOIN_ANTI:
				if (rel1_empty)
					continue;	/* ignore this join segment */
				break;
			case JOIN_FULL:
				if (rel1_empty && rel2_empty)
					continue;	/* ignore this join segment */
				break;
			default:
				/* other values not expected here */
				elog(ERROR, "unrecognized join type: %d",
					 (int) parent_sjinfo->jointype);
				break;
		}

		/*
		 * If a child has been pruned entirely then we can't generate paths
		 * for it, so we have to reject partitionwise joining unless we were
		 * able to eliminate this partition above.
		 */
		if (child_rel1 == NULL || child_rel2 == NULL)
		{
			/*
			 * Mark the joinrel as unpartitioned so that later functions treat
			 * it correctly.
			 */
			joinrel->nparts = 0;
			return;
		}

		/*
		 * If a leaf relation has consider_partitionwise_join=false, it means
		 * that it's a dummy relation for which we skipped setting up tlist
		 * expressions and adding EC members in set_append_rel_size(), so
		 * again we have to fail here.
		 */
		if (rel1_is_simple && !child_rel1->consider_partitionwise_join)
		{
			Assert(child_rel1->reloptkind == RELOPT_OTHER_MEMBER_REL);
			Assert(IS_DUMMY_REL(child_rel1));
			joinrel->nparts = 0;
			return;
		}
		if (rel2_is_simple && !child_rel2->consider_partitionwise_join)
		{
			Assert(child_rel2->reloptkind == RELOPT_OTHER_MEMBER_REL);
			Assert(IS_DUMMY_REL(child_rel2));
			joinrel->nparts = 0;
			return;
		}

		/* We should never try to join two overlapping sets of rels. */
		Assert(!bms_overlap(child_rel1->relids, child_rel2->relids));
		child_joinrelids = bms_union(child_rel1->relids, child_rel2->relids);
		appinfos = find_appinfos_by_relids(root, child_joinrelids, &nappinfos);

		/*
		 * Construct SpecialJoinInfo from parent join relations's
		 * SpecialJoinInfo.
		 */
		child_sjinfo = build_child_join_sjinfo(root, parent_sjinfo,
											   child_rel1->relids,
											   child_rel2->relids);

		/*
		 * Construct restrictions applicable to the child join from those
		 * applicable to the parent join.
		 */
		child_restrictlist =
			(List *) adjust_appendrel_attrs(root,
											(Node *) parent_restrictlist,
											nappinfos, appinfos);
		pfree(appinfos);

		child_joinrel = joinrel->part_rels[cnt_parts];
		if (!child_joinrel)
		{
			child_joinrel = build_child_join_rel(root, child_rel1, child_rel2,
												 joinrel, child_restrictlist,
												 child_sjinfo,
												 child_sjinfo->jointype);
			joinrel->part_rels[cnt_parts] = child_joinrel;
		}

		Assert(bms_equal(child_joinrel->relids, child_joinrelids));

		populate_joinrel_with_paths(root, child_rel1, child_rel2,
									child_joinrel, child_sjinfo,
									child_restrictlist);
	}
}
