/*-------------------------------------------------------------------------
 *
 * jsonpath_exec.c
 *	 Routines for SQL/JSON path execution.
 *
 * Jsonpath is executed in the global context stored in JsonPathExecContext,
 * which is passed to almost every function involved into execution.  Entry
 * point for jsonpath execution is executeJsonPath() function, which
 * initializes execution context including initial JsonPathItem and JsonbValue,
 * flags, stack for calculation of @ in filters.
 *
 * The result of jsonpath query execution is enum JsonPathExecResult and
 * if succeeded sequence of JsonbValue, written to JsonValueList *found, which
 * is passed through the jsonpath items.  When found == NULL, we're inside
 * exists-query and we're interested only in whether result is empty.  In this
 * case execution is stopped once first result item is found, and the only
 * execution result is JsonPathExecResult.  The values of JsonPathExecResult
 * are following:
 * - jperOk			-- result sequence is not empty
 * - jperNotFound	-- result sequence is empty
 * - jperError		-- error occurred during execution
 *
 * Jsonpath is executed recursively (see executeItem()) starting form the
 * first path item (which in turn might be, for instance, an arithmetic
 * expression evaluated separately).  On each step single JsonbValue obtained
 * from previous path item is processed.  The result of processing is a
 * sequence of JsonbValue (probably empty), which is passed to the next path
 * item one by one.  When there is no next path item, then JsonbValue is added
 * to the 'found' list.  When found == NULL, then execution functions just
 * return jperOk (see executeNextItem()).
 *
 * Many of jsonpath operations require automatic unwrapping of arrays in lax
 * mode.  So, if input value is array, then corresponding operation is
 * processed not on array itself, but on all of its members one by one.
 * executeItemOptUnwrapTarget() function have 'unwrap' argument, which indicates
 * whether unwrapping of array is needed.  When unwrap == true, each of array
 * members is passed to executeItemOptUnwrapTarget() again but with unwrap == false
 * in order to evade subsequent array unwrapping.
 *
 * All boolean expressions (predicates) are evaluated by executeBoolItem()
 * function, which returns tri-state JsonPathBool.  When error is occurred
 * during predicate execution, it returns jpbUnknown.  According to standard
 * predicates can be only inside filters.  But we support their usage as
 * jsonpath expression.  This helps us to implement @@ operator.  In this case
 * resulting JsonPathBool is transformed into jsonb bool or null.
 *
 * Arithmetic and boolean expression are evaluated recursively from expression
 * tree top down to the leaves.  Therefore, for binary arithmetic expressions
 * we calculate operands first.  Then we check that results are numeric
 * singleton lists, calculate the result and pass it to the next path item.
 *
 * Copyright (c) 2019, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *	src/backend/utils/adt/jsonpath_exec.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "catalog/pg_collation.h"
#include "catalog/pg_type.h"
#include "funcapi.h"
#include "lib/stringinfo.h"
#include "miscadmin.h"
#include "regex/regex.h"
#include "utils/builtins.h"
#include "utils/datum.h"
#include "utils/formatting.h"
#include "utils/float.h"
#include "utils/guc.h"
#include "utils/json.h"
#include "utils/jsonpath.h"
#include "utils/date.h"
#include "utils/timestamp.h"
#include "utils/varlena.h"


/*
 * Represents "base object" and it's "id" for .keyvalue() evaluation.
 */
typedef struct JsonBaseObjectInfo
{
	JsonbContainer *jbc;
	int			id;
} JsonBaseObjectInfo;

/*
 * Context of jsonpath execution.
 */
typedef struct JsonPathExecContext
{
	Jsonb	   *vars;			/* variables to substitute into jsonpath */
	JsonbValue *root;			/* for $ evaluation */
	JsonbValue *current;		/* for @ evaluation */
	JsonBaseObjectInfo baseObject;	/* "base object" for .keyvalue()
									 * evaluation */
	int			lastGeneratedObjectId;	/* "id" counter for .keyvalue()
										 * evaluation */
	int			innermostArraySize; /* for LAST array index evaluation */
	bool		laxMode;		/* true for "lax" mode, false for "strict"
								 * mode */
	bool		ignoreStructuralErrors; /* with "true" structural errors such
										 * as absence of required json item or
										 * unexpected json item type are
										 * ignored */
	bool		throwErrors;	/* with "false" all suppressible errors are
								 * suppressed */
} JsonPathExecContext;

/* Context for LIKE_REGEX execution. */
typedef struct JsonLikeRegexContext
{
	text	   *regex;
	int			cflags;
} JsonLikeRegexContext;

#define EmptyJsonLikeRegexContext	{NULL, 0}

/* Result of jsonpath predicate evaluation */
typedef enum JsonPathBool
{
	jpbFalse = 0,
	jpbTrue = 1,
	jpbUnknown = 2
} JsonPathBool;

/* Result of jsonpath expression evaluation */
typedef enum JsonPathExecResult
{
	jperOk = 0,
	jperNotFound = 1,
	jperError = 2
} JsonPathExecResult;

#define jperIsError(jper)			((jper) == jperError)

/*
 * List of jsonb values with shortcut for single-value list.
 */
typedef struct JsonValueList
{
	JsonbValue *singleton;
	List	   *list;
} JsonValueList;

#define EmptyJsonValueList			{NULL, NIL}

typedef struct JsonValueListIterator
{
	JsonbValue *value;
	ListCell   *next;
} JsonValueListIterator;

/* strict/lax flags is decomposed into four [un]wrap/error flags */
#define jspStrictAbsenseOfErrors(cxt)	(!(cxt)->laxMode)
#define jspAutoUnwrap(cxt)				((cxt)->laxMode)
#define jspAutoWrap(cxt)				((cxt)->laxMode)
#define jspIgnoreStructuralErrors(cxt)	((cxt)->ignoreStructuralErrors)
#define jspThrowErrors(cxt)				((cxt)->throwErrors)

/* Convenience macro: return or throw error depending on context */
#define RETURN_ERROR(throw_error) \
do { \
	if (jspThrowErrors(cxt)) \
		throw_error; \
	else \
		return jperError; \
} while (0)

typedef JsonPathBool (*JsonPathPredicateCallback) (JsonPathItem *jsp,
												   JsonbValue *larg,
												   JsonbValue *rarg,
												   void *param);
typedef Numeric (*BinaryArithmFunc) (Numeric num1, Numeric num2, bool *error);

static JsonPathExecResult executeJsonPath(JsonPath *path, Jsonb *vars,
				Jsonb *json, bool throwErrors, JsonValueList *result);
static JsonPathExecResult executeItem(JsonPathExecContext *cxt,
			JsonPathItem *jsp, JsonbValue *jb, JsonValueList *found);
static JsonPathExecResult executeItemOptUnwrapTarget(JsonPathExecContext *cxt,
						   JsonPathItem *jsp, JsonbValue *jb,
						   JsonValueList *found, bool unwrap);
static JsonPathExecResult executeItemUnwrapTargetArray(JsonPathExecContext *cxt,
							 JsonPathItem *jsp, JsonbValue *jb,
							 JsonValueList *found, bool unwrapElements);
static JsonPathExecResult executeNextItem(JsonPathExecContext *cxt,
				JsonPathItem *cur, JsonPathItem *next,
				JsonbValue *v, JsonValueList *found, bool copy);
static JsonPathExecResult executeItemOptUnwrapResult(
						   JsonPathExecContext *cxt, JsonPathItem *jsp, JsonbValue *jb,
						   bool unwrap, JsonValueList *found);
static JsonPathExecResult executeItemOptUnwrapResultNoThrow(
								  JsonPathExecContext *cxt, JsonPathItem *jsp,
								  JsonbValue *jb, bool unwrap, JsonValueList *found);
static JsonPathBool executeBoolItem(JsonPathExecContext *cxt,
				JsonPathItem *jsp, JsonbValue *jb, bool canHaveNext);
static JsonPathBool executeNestedBoolItem(JsonPathExecContext *cxt,
					  JsonPathItem *jsp, JsonbValue *jb);
static JsonPathExecResult executeAnyItem(JsonPathExecContext *cxt,
			   JsonPathItem *jsp, JsonbContainer *jbc, JsonValueList *found,
			   uint32 level, uint32 first, uint32 last,
			   bool ignoreStructuralErrors, bool unwrapNext);
static JsonPathBool executePredicate(JsonPathExecContext *cxt,
				 JsonPathItem *pred, JsonPathItem *larg, JsonPathItem *rarg,
				 JsonbValue *jb, bool unwrapRightArg,
				 JsonPathPredicateCallback exec, void *param);
static JsonPathExecResult executeBinaryArithmExpr(JsonPathExecContext *cxt,
						JsonPathItem *jsp, JsonbValue *jb,
						BinaryArithmFunc func, JsonValueList *found);
static JsonPathExecResult executeUnaryArithmExpr(JsonPathExecContext *cxt,
					   JsonPathItem *jsp, JsonbValue *jb, PGFunction func,
					   JsonValueList *found);
static JsonPathBool executeStartsWith(JsonPathItem *jsp,
				  JsonbValue *whole, JsonbValue *initial, void *param);
static JsonPathBool executeLikeRegex(JsonPathItem *jsp, JsonbValue *str,
				 JsonbValue *rarg, void *param);
static JsonPathExecResult executeNumericItemMethod(JsonPathExecContext *cxt,
						 JsonPathItem *jsp, JsonbValue *jb, bool unwrap, PGFunction func,
						 JsonValueList *found);
static JsonPathExecResult executeKeyValueMethod(JsonPathExecContext *cxt,
					  JsonPathItem *jsp, JsonbValue *jb, JsonValueList *found);
static JsonPathExecResult appendBoolResult(JsonPathExecContext *cxt,
				 JsonPathItem *jsp, JsonValueList *found, JsonPathBool res);
static void getJsonPathItem(JsonPathExecContext *cxt, JsonPathItem *item,
				JsonbValue *value);
static void getJsonPathVariable(JsonPathExecContext *cxt,
					JsonPathItem *variable, Jsonb *vars, JsonbValue *value);
static int	JsonbArraySize(JsonbValue *jb);
static JsonPathBool executeComparison(JsonPathItem *cmp, JsonbValue *lv,
				  JsonbValue *rv, void *p);
static JsonPathBool compareItems(int32 op, JsonbValue *jb1, JsonbValue *jb2);
static int	compareNumeric(Numeric a, Numeric b);
static JsonbValue *copyJsonbValue(JsonbValue *src);
static JsonPathExecResult getArrayIndex(JsonPathExecContext *cxt,
			  JsonPathItem *jsp, JsonbValue *jb, int32 *index);
static JsonBaseObjectInfo setBaseObject(JsonPathExecContext *cxt,
			  JsonbValue *jbv, int32 id);
static void JsonValueListAppend(JsonValueList *jvl, JsonbValue *jbv);
static int	JsonValueListLength(const JsonValueList *jvl);
static bool JsonValueListIsEmpty(JsonValueList *jvl);
static JsonbValue *JsonValueListHead(JsonValueList *jvl);
static List *JsonValueListGetList(JsonValueList *jvl);
static void JsonValueListInitIterator(const JsonValueList *jvl,
						  JsonValueListIterator *it);
static JsonbValue *JsonValueListNext(const JsonValueList *jvl,
				  JsonValueListIterator *it);
static int	JsonbType(JsonbValue *jb);
static JsonbValue *JsonbInitBinary(JsonbValue *jbv, Jsonb *jb);
static int	JsonbType(JsonbValue *jb);
static JsonbValue *getScalar(JsonbValue *scalar, enum jbvType type);
static JsonbValue *wrapItemsInArray(const JsonValueList *items);

/****************** User interface to JsonPath executor ********************/

/*
 * jsonb_path_exists
 *		Returns true if jsonpath returns at least one item for the specified
 *		jsonb value.  This function and jsonb_path_match() are used to
 *		implement @? and @@ operators, which in turn are intended to have an
 *		index support.  Thus, it's desirable to make it easier to achieve
 *		consistency between index scan results and sequential scan results.
 *		So, we throw as less errors as possible.  Regarding this function,
 *		such behavior also matches behavior of JSON_EXISTS() clause of
 *		SQL/JSON.  Regarding jsonb_path_match(), this function doesn't have
 *		an analogy in SQL/JSON, so we define its behavior on our own.
 */
Datum
jsonb_path_exists(PG_FUNCTION_ARGS)
{
	Jsonb	   *jb = PG_GETARG_JSONB_P(0);
	JsonPath   *jp = PG_GETARG_JSONPATH_P(1);
	JsonPathExecResult res;
	Jsonb	   *vars = NULL;
	bool		silent = true;

	if (PG_NARGS() == 4)
	{
		vars = PG_GETARG_JSONB_P(2);
		silent = PG_GETARG_BOOL(3);
	}

	res = executeJsonPath(jp, vars, jb, !silent, NULL);

	PG_FREE_IF_COPY(jb, 0);
	PG_FREE_IF_COPY(jp, 1);

	if (jperIsError(res))
		PG_RETURN_NULL();

	PG_RETURN_BOOL(res == jperOk);
}

/*
 * jsonb_path_exists_opr
 *		Implementation of operator "jsonb @? jsonpath" (2-argument version of
 *		jsonb_path_exists()).
 */
Datum
jsonb_path_exists_opr(PG_FUNCTION_ARGS)
{
	/* just call the other one -- it can handle both cases */
	return jsonb_path_exists(fcinfo);
}

/*
 * jsonb_path_match
 *		Returns jsonpath predicate result item for the specified jsonb value.
 *		See jsonb_path_exists() comment for details regarding error handling.
 */
Datum
jsonb_path_match(PG_FUNCTION_ARGS)
{
	Jsonb	   *jb = PG_GETARG_JSONB_P(0);
	JsonPath   *jp = PG_GETARG_JSONPATH_P(1);
	JsonValueList found = EmptyJsonValueList;
	Jsonb	   *vars = NULL;
	bool		silent = true;

	if (PG_NARGS() == 4)
	{
		vars = PG_GETARG_JSONB_P(2);
		silent = PG_GETARG_BOOL(3);
	}

	(void) executeJsonPath(jp, vars, jb, !silent, &found);

	PG_FREE_IF_COPY(jb, 0);
	PG_FREE_IF_COPY(jp, 1);

	if (JsonValueListLength(&found) == 1)
	{
		JsonbValue *jbv = JsonValueListHead(&found);

		if (jbv->type == jbvBool)
			PG_RETURN_BOOL(jbv->val.boolean);

		if (jbv->type == jbvNull)
			PG_RETURN_NULL();
	}

	if (!silent)
		ereport(ERROR,
				(errcode(ERRCODE_SINGLETON_SQL_JSON_ITEM_REQUIRED),
				 errmsg("single boolean result is expected")));

	PG_RETURN_NULL();
}

/*
 * jsonb_path_match_opr
 *		Implementation of operator "jsonb @@ jsonpath" (2-argument version of
 *		jsonb_path_match()).
 */
Datum
jsonb_path_match_opr(PG_FUNCTION_ARGS)
{
	/* just call the other one -- it can handle both cases */
	return jsonb_path_match(fcinfo);
}

/*
 * jsonb_path_query
 *		Executes jsonpath for given jsonb document and returns result as
 *		rowset.
 */
Datum
jsonb_path_query(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	List	   *found;
	JsonbValue *v;
	ListCell   *c;

	if (SRF_IS_FIRSTCALL())
	{
		JsonPath   *jp;
		Jsonb	   *jb;
		MemoryContext oldcontext;
		Jsonb	   *vars;
		bool		silent;
		JsonValueList found = EmptyJsonValueList;

		funcctx = SRF_FIRSTCALL_INIT();
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		jb = PG_GETARG_JSONB_P_COPY(0);
		jp = PG_GETARG_JSONPATH_P_COPY(1);
		vars = PG_GETARG_JSONB_P_COPY(2);
		silent = PG_GETARG_BOOL(3);

		(void) executeJsonPath(jp, vars, jb, !silent, &found);

		funcctx->user_fctx = JsonValueListGetList(&found);

		MemoryContextSwitchTo(oldcontext);
	}

	funcctx = SRF_PERCALL_SETUP();
	found = funcctx->user_fctx;

	c = list_head(found);

	if (c == NULL)
		SRF_RETURN_DONE(funcctx);

	v = lfirst(c);
	funcctx->user_fctx = list_delete_first(found);

	SRF_RETURN_NEXT(funcctx, JsonbPGetDatum(JsonbValueToJsonb(v)));
}

/*
 * jsonb_path_query_array
 *		Executes jsonpath for given jsonb document and returns result as
 *		jsonb array.
 */
Datum
jsonb_path_query_array(PG_FUNCTION_ARGS)
{
	Jsonb	   *jb = PG_GETARG_JSONB_P(0);
	JsonPath   *jp = PG_GETARG_JSONPATH_P(1);
	JsonValueList found = EmptyJsonValueList;
	Jsonb	   *vars = PG_GETARG_JSONB_P(2);
	bool		silent = PG_GETARG_BOOL(3);

	(void) executeJsonPath(jp, vars, jb, !silent, &found);

	PG_RETURN_JSONB_P(JsonbValueToJsonb(wrapItemsInArray(&found)));
}

/*
 * jsonb_path_query_first
 *		Executes jsonpath for given jsonb document and returns first result
 *		item.  If there are no items, NULL returned.
 */
Datum
jsonb_path_query_first(PG_FUNCTION_ARGS)
{
	Jsonb	   *jb = PG_GETARG_JSONB_P(0);
	JsonPath   *jp = PG_GETARG_JSONPATH_P(1);
	JsonValueList found = EmptyJsonValueList;
	Jsonb	   *vars = PG_GETARG_JSONB_P(2);
	bool		silent = PG_GETARG_BOOL(3);

	(void) executeJsonPath(jp, vars, jb, !silent, &found);

	if (JsonValueListLength(&found) >= 1)
		PG_RETURN_JSONB_P(JsonbValueToJsonb(JsonValueListHead(&found)));
	else
		PG_RETURN_NULL();
}

/********************Execute functions for JsonPath**************************/

/*
 * Interface to jsonpath executor
 *
 * 'path' - jsonpath to be executed
 * 'vars' - variables to be substituted to jsonpath
 * 'json' - target document for jsonpath evaluation
 * 'throwErrors' - whether we should throw suppressible errors
 * 'result' - list to store result items into
 *
 * Returns an error happens during processing or NULL on no error.
 *
 * Note, jsonb and jsonpath values should be avaliable and untoasted during
 * work because JsonPathItem, JsonbValue and result item could have pointers
 * into input values.  If caller needs to just check if document matches
 * jsonpath, then it doesn't provide a result arg.  In this case executor
 * works till first positive result and does not check the rest if possible.
 * In other case it tries to find all the satisfied result items.
 */
static JsonPathExecResult
executeJsonPath(JsonPath *path, Jsonb *vars, Jsonb *json, bool throwErrors,
				JsonValueList *result)
{
	JsonPathExecContext cxt;
	JsonPathExecResult res;
	JsonPathItem jsp;
	JsonbValue	jbv;

	jspInit(&jsp, path);

	if (!JsonbExtractScalar(&json->root, &jbv))
		JsonbInitBinary(&jbv, json);

	if (vars && !JsonContainerIsObject(&vars->root))
	{
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("\"vars\" argument is not an object"),
				 errdetail("Jsonpath parameters should be encoded as key-value pairs of \"vars\" object.")));
	}

	cxt.vars = vars;
	cxt.laxMode = (path->header & JSONPATH_LAX) != 0;
	cxt.ignoreStructuralErrors = cxt.laxMode;
	cxt.root = &jbv;
	cxt.current = &jbv;
	cxt.baseObject.jbc = NULL;
	cxt.baseObject.id = 0;
	cxt.lastGeneratedObjectId = vars ? 2 : 1;
	cxt.innermostArraySize = -1;
	cxt.throwErrors = throwErrors;

	if (jspStrictAbsenseOfErrors(&cxt) && !result)
	{
		/*
		 * In strict mode we must get a complete list of values to check that
		 * there are no errors at all.
		 */
		JsonValueList vals = EmptyJsonValueList;

		res = executeItem(&cxt, &jsp, &jbv, &vals);

		if (jperIsError(res))
			return res;

		return JsonValueListIsEmpty(&vals) ? jperNotFound : jperOk;
	}

	res = executeItem(&cxt, &jsp, &jbv, result);

	Assert(!throwErrors || !jperIsError(res));

	return res;
}

/*
 * Execute jsonpath with automatic unwrapping of current item in lax mode.
 */
static JsonPathExecResult
executeItem(JsonPathExecContext *cxt, JsonPathItem *jsp,
			JsonbValue *jb, JsonValueList *found)
{
	return executeItemOptUnwrapTarget(cxt, jsp, jb, found, jspAutoUnwrap(cxt));
}

/*
 * Main jsonpath executor function: walks on jsonpath structure, finds
 * relevant parts of jsonb and evaluates expressions over them.
 * When 'unwrap' is true current SQL/JSON item is unwrapped if it is an array.
 */
static JsonPathExecResult
executeItemOptUnwrapTarget(JsonPathExecContext *cxt, JsonPathItem *jsp,
						   JsonbValue *jb, JsonValueList *found, bool unwrap)
{
	JsonPathItem elem;
	JsonPathExecResult res = jperNotFound;
	JsonBaseObjectInfo baseObject;

	check_stack_depth();
	CHECK_FOR_INTERRUPTS();

	switch (jsp->type)
	{
			/* all boolean item types: */
		case jpiAnd:
		case jpiOr:
		case jpiNot:
		case jpiIsUnknown:
		case jpiEqual:
		case jpiNotEqual:
		case jpiLess:
		case jpiGreater:
		case jpiLessOrEqual:
		case jpiGreaterOrEqual:
		case jpiExists:
		case jpiStartsWith:
		case jpiLikeRegex:
			{
				JsonPathBool st = executeBoolItem(cxt, jsp, jb, true);

				res = appendBoolResult(cxt, jsp, found, st);
				break;
			}

		case jpiKey:
			if (JsonbType(jb) == jbvObject)
			{
				JsonbValue *v;
				JsonbValue	key;

				key.type = jbvString;
				key.val.string.val = jspGetString(jsp, &key.val.string.len);

				v = findJsonbValueFromContainer(jb->val.binary.data,
												JB_FOBJECT, &key);

				if (v != NULL)
				{
					res = executeNextItem(cxt, jsp, NULL,
										  v, found, false);

					/* free value if it was not added to found list */
					if (jspHasNext(jsp) || !found)
						pfree(v);
				}
				else if (!jspIgnoreStructuralErrors(cxt))
				{
					Assert(found);

					if (!jspThrowErrors(cxt))
						return jperError;

					ereport(ERROR,
							(errcode(ERRCODE_SQL_JSON_MEMBER_NOT_FOUND), \
							 errmsg("JSON object does not contain key \"%s\"",
									pnstrdup(key.val.string.val,
											 key.val.string.len))));
				}
			}
			else if (unwrap && JsonbType(jb) == jbvArray)
				return executeItemUnwrapTargetArray(cxt, jsp, jb, found, false);
			else if (!jspIgnoreStructuralErrors(cxt))
			{
				Assert(found);
				RETURN_ERROR(ereport(ERROR,
									 (errcode(ERRCODE_SQL_JSON_MEMBER_NOT_FOUND),
									  errmsg("jsonpath member accessor can only be applied to an object"))));
			}
			break;

		case jpiRoot:
			jb = cxt->root;
			baseObject = setBaseObject(cxt, jb, 0);
			res = executeNextItem(cxt, jsp, NULL, jb, found, true);
			cxt->baseObject = baseObject;
			break;

		case jpiCurrent:
			res = executeNextItem(cxt, jsp, NULL, cxt->current,
								  found, true);
			break;

		case jpiAnyArray:
			if (JsonbType(jb) == jbvArray)
			{
				bool		hasNext = jspGetNext(jsp, &elem);

				res = executeItemUnwrapTargetArray(cxt, hasNext ? &elem : NULL,
												   jb, found, jspAutoUnwrap(cxt));
			}
			else if (jspAutoWrap(cxt))
				res = executeNextItem(cxt, jsp, NULL, jb, found, true);
			else if (!jspIgnoreStructuralErrors(cxt))
				RETURN_ERROR(ereport(ERROR,
									 (errcode(ERRCODE_SQL_JSON_ARRAY_NOT_FOUND),
									  errmsg("jsonpath wildcard array accessor can only be applied to an array"))));
			break;

		case jpiIndexArray:
			if (JsonbType(jb) == jbvArray || jspAutoWrap(cxt))
			{
				int			innermostArraySize = cxt->innermostArraySize;
				int			i;
				int			size = JsonbArraySize(jb);
				bool		singleton = size < 0;
				bool		hasNext = jspGetNext(jsp, &elem);

				if (singleton)
					size = 1;

				cxt->innermostArraySize = size; /* for LAST evaluation */

				for (i = 0; i < jsp->content.array.nelems; i++)
				{
					JsonPathItem from;
					JsonPathItem to;
					int32		index;
					int32		index_from;
					int32		index_to;
					bool		range = jspGetArraySubscript(jsp, &from,
															 &to, i);

					res = getArrayIndex(cxt, &from, jb, &index_from);

					if (jperIsError(res))
						break;

					if (range)
					{
						res = getArrayIndex(cxt, &to, jb, &index_to);

						if (jperIsError(res))
							break;
					}
					else
						index_to = index_from;

					if (!jspIgnoreStructuralErrors(cxt) &&
						(index_from < 0 ||
						 index_from > index_to ||
						 index_to >= size))
						RETURN_ERROR(ereport(ERROR,
											 (errcode(ERRCODE_INVALID_SQL_JSON_SUBSCRIPT),
											  errmsg("jsonpath array subscript is out of bounds"))));

					if (index_from < 0)
						index_from = 0;

					if (index_to >= size)
						index_to = size - 1;

					res = jperNotFound;

					for (index = index_from; index <= index_to; index++)
					{
						JsonbValue *v;
						bool		copy;

						if (singleton)
						{
							v = jb;
							copy = true;
						}
						else
						{
							v = getIthJsonbValueFromContainer(jb->val.binary.data,
															  (uint32) index);

							if (v == NULL)
								continue;

							copy = false;
						}

						if (!hasNext && !found)
							return jperOk;

						res = executeNextItem(cxt, jsp, &elem, v, found,
											  copy);

						if (jperIsError(res))
							break;

						if (res == jperOk && !found)
							break;
					}

					if (jperIsError(res))
						break;

					if (res == jperOk && !found)
						break;
				}

				cxt->innermostArraySize = innermostArraySize;
			}
			else if (!jspIgnoreStructuralErrors(cxt))
			{
				RETURN_ERROR(ereport(ERROR,
									 (errcode(ERRCODE_SQL_JSON_ARRAY_NOT_FOUND),
									  errmsg("jsonpath array accessor can only be applied to an array"))));
			}
			break;

		case jpiLast:
			{
				JsonbValue	tmpjbv;
				JsonbValue *lastjbv;
				int			last;
				bool		hasNext = jspGetNext(jsp, &elem);

				if (cxt->innermostArraySize < 0)
					elog(ERROR, "evaluating jsonpath LAST outside of array subscript");

				if (!hasNext && !found)
				{
					res = jperOk;
					break;
				}

				last = cxt->innermostArraySize - 1;

				lastjbv = hasNext ? &tmpjbv : palloc(sizeof(*lastjbv));

				lastjbv->type = jbvNumeric;
				lastjbv->val.numeric =
					DatumGetNumeric(DirectFunctionCall1(int4_numeric,
														Int32GetDatum(last)));

				res = executeNextItem(cxt, jsp, &elem,
									  lastjbv, found, hasNext);
			}
			break;

		case jpiAnyKey:
			if (JsonbType(jb) == jbvObject)
			{
				bool		hasNext = jspGetNext(jsp, &elem);

				if (jb->type != jbvBinary)
					elog(ERROR, "invalid jsonb object type: %d", jb->type);

				return executeAnyItem
					(cxt, hasNext ? &elem : NULL,
					 jb->val.binary.data, found, 1, 1, 1,
					 false, jspAutoUnwrap(cxt));
			}
			else if (unwrap && JsonbType(jb) == jbvArray)
				return executeItemUnwrapTargetArray(cxt, jsp, jb, found, false);
			else if (!jspIgnoreStructuralErrors(cxt))
			{
				Assert(found);
				RETURN_ERROR(ereport(ERROR,
									 (errcode(ERRCODE_SQL_JSON_OBJECT_NOT_FOUND),
									  errmsg("jsonpath wildcard member accessor can only be applied to an object"))));
			}
			break;

		case jpiAdd:
			return executeBinaryArithmExpr(cxt, jsp, jb,
										   numeric_add_opt_error, found);

		case jpiSub:
			return executeBinaryArithmExpr(cxt, jsp, jb,
										   numeric_sub_opt_error, found);

		case jpiMul:
			return executeBinaryArithmExpr(cxt, jsp, jb,
										   numeric_mul_opt_error, found);

		case jpiDiv:
			return executeBinaryArithmExpr(cxt, jsp, jb,
										   numeric_div_opt_error, found);

		case jpiMod:
			return executeBinaryArithmExpr(cxt, jsp, jb,
										   numeric_mod_opt_error, found);

		case jpiPlus:
			return executeUnaryArithmExpr(cxt, jsp, jb, NULL, found);

		case jpiMinus:
			return executeUnaryArithmExpr(cxt, jsp, jb, numeric_uminus,
										  found);

		case jpiFilter:
			{
				JsonPathBool st;

				if (unwrap && JsonbType(jb) == jbvArray)
					return executeItemUnwrapTargetArray(cxt, jsp, jb, found,
														false);

				jspGetArg(jsp, &elem);
				st = executeNestedBoolItem(cxt, &elem, jb);
				if (st != jpbTrue)
					res = jperNotFound;
				else
					res = executeNextItem(cxt, jsp, NULL,
										  jb, found, true);
				break;
			}

		case jpiAny:
			{
				bool		hasNext = jspGetNext(jsp, &elem);

				/* first try without any intermediate steps */
				if (jsp->content.anybounds.first == 0)
				{
					bool		savedIgnoreStructuralErrors;

					savedIgnoreStructuralErrors = cxt->ignoreStructuralErrors;
					cxt->ignoreStructuralErrors = true;
					res = executeNextItem(cxt, jsp, &elem,
										  jb, found, true);
					cxt->ignoreStructuralErrors = savedIgnoreStructuralErrors;

					if (res == jperOk && !found)
						break;
				}

				if (jb->type == jbvBinary)
					res = executeAnyItem
						(cxt, hasNext ? &elem : NULL,
						 jb->val.binary.data, found,
						 1,
						 jsp->content.anybounds.first,
						 jsp->content.anybounds.last,
						 true, jspAutoUnwrap(cxt));
				break;
			}

		case jpiNull:
		case jpiBool:
		case jpiNumeric:
		case jpiString:
		case jpiVariable:
			{
				JsonbValue	vbuf;
				JsonbValue *v;
				bool		hasNext = jspGetNext(jsp, &elem);

				if (!hasNext && !found)
				{
					res = jperOk;	/* skip evaluation */
					break;
				}

				v = hasNext ? &vbuf : palloc(sizeof(*v));

				baseObject = cxt->baseObject;
				getJsonPathItem(cxt, jsp, v);

				res = executeNextItem(cxt, jsp, &elem,
									  v, found, hasNext);
				cxt->baseObject = baseObject;
			}
			break;

		case jpiType:
			{
				JsonbValue *jbv = palloc(sizeof(*jbv));

				jbv->type = jbvString;
				jbv->val.string.val = pstrdup(JsonbTypeName(jb));
				jbv->val.string.len = strlen(jbv->val.string.val);

				res = executeNextItem(cxt, jsp, NULL, jbv,
									  found, false);
			}
			break;

		case jpiSize:
			{
				int			size = JsonbArraySize(jb);

				if (size < 0)
				{
					if (!jspAutoWrap(cxt))
					{
						if (!jspIgnoreStructuralErrors(cxt))
							RETURN_ERROR(ereport(ERROR,
												 (errcode(ERRCODE_SQL_JSON_ARRAY_NOT_FOUND),
												  errmsg("jsonpath item method .%s() can only be applied to an array",
														 jspOperationName(jsp->type)))));
						break;
					}

					size = 1;
				}

				jb = palloc(sizeof(*jb));

				jb->type = jbvNumeric;
				jb->val.numeric =
					DatumGetNumeric(DirectFunctionCall1(int4_numeric,
														Int32GetDatum(size)));

				res = executeNextItem(cxt, jsp, NULL, jb, found, false);
			}
			break;

		case jpiAbs:
			return executeNumericItemMethod(cxt, jsp, jb, unwrap, numeric_abs,
											found);

		case jpiFloor:
			return executeNumericItemMethod(cxt, jsp, jb, unwrap, numeric_floor,
											found);

		case jpiCeiling:
			return executeNumericItemMethod(cxt, jsp, jb, unwrap, numeric_ceil,
											found);

		case jpiDouble:
			{
				JsonbValue	jbv;

				if (unwrap && JsonbType(jb) == jbvArray)
					return executeItemUnwrapTargetArray(cxt, jsp, jb, found,
														false);

				if (jb->type == jbvNumeric)
				{
					char	   *tmp = DatumGetCString(DirectFunctionCall1(numeric_out,
																		  NumericGetDatum(jb->val.numeric)));
					bool		have_error = false;

					(void) float8in_internal_opt_error(tmp,
													   NULL,
													   "double precision",
													   tmp,
													   &have_error);

					if (have_error)
						RETURN_ERROR(ereport(ERROR,
											 (errcode(ERRCODE_NON_NUMERIC_SQL_JSON_ITEM),
											  errmsg("jsonpath item method .%s() can only be applied to a numeric value",
													 jspOperationName(jsp->type)))));
					res = jperOk;
				}
				else if (jb->type == jbvString)
				{
					/* cast string as double */
					double		val;
					char	   *tmp = pnstrdup(jb->val.string.val,
											   jb->val.string.len);
					bool		have_error = false;

					val = float8in_internal_opt_error(tmp,
													  NULL,
													  "double precision",
													  tmp,
													  &have_error);

					if (have_error || isinf(val))
						RETURN_ERROR(ereport(ERROR,
											 (errcode(ERRCODE_NON_NUMERIC_SQL_JSON_ITEM),
											  errmsg("jsonpath item method .%s() can only be applied to a numeric value",
													 jspOperationName(jsp->type)))));

					jb = &jbv;
					jb->type = jbvNumeric;
					jb->val.numeric = DatumGetNumeric(DirectFunctionCall1(float8_numeric,
																		  Float8GetDatum(val)));
					res = jperOk;
				}

				if (res == jperNotFound)
					RETURN_ERROR(ereport(ERROR,
										 (errcode(ERRCODE_NON_NUMERIC_SQL_JSON_ITEM),
										  errmsg("jsonpath item method .%s() can only be applied to a string or numeric value",
												 jspOperationName(jsp->type)))));

				res = executeNextItem(cxt, jsp, NULL, jb, found, true);
			}
			break;

		case jpiKeyValue:
			if (unwrap && JsonbType(jb) == jbvArray)
				return executeItemUnwrapTargetArray(cxt, jsp, jb, found, false);

			return executeKeyValueMethod(cxt, jsp, jb, found);

		default:
			elog(ERROR, "unrecognized jsonpath item type: %d", jsp->type);
	}

	return res;
}

/*
 * Unwrap current array item and execute jsonpath for each of its elements.
 */
static JsonPathExecResult
executeItemUnwrapTargetArray(JsonPathExecContext *cxt, JsonPathItem *jsp,
							 JsonbValue *jb, JsonValueList *found,
							 bool unwrapElements)
{
	if (jb->type != jbvBinary)
	{
		Assert(jb->type != jbvArray);
		elog(ERROR, "invalid jsonb array value type: %d", jb->type);
	}

	return executeAnyItem
		(cxt, jsp, jb->val.binary.data, found, 1, 1, 1,
		 false, unwrapElements);
}

/*
 * Execute next jsonpath item if exists.  Otherwise put "v" to the "found"
 * list if provided.
 */
static JsonPathExecResult
executeNextItem(JsonPathExecContext *cxt,
				JsonPathItem *cur, JsonPathItem *next,
				JsonbValue *v, JsonValueList *found, bool copy)
{
	JsonPathItem elem;
	bool		hasNext;

	if (!cur)
		hasNext = next != NULL;
	else if (next)
		hasNext = jspHasNext(cur);
	else
	{
		next = &elem;
		hasNext = jspGetNext(cur, next);
	}

	if (hasNext)
		return executeItem(cxt, next, v, found);

	if (found)
		JsonValueListAppend(found, copy ? copyJsonbValue(v) : v);

	return jperOk;
}

/*
 * Same as executeItem(), but when "unwrap == true" automatically unwraps
 * each array item from the resulting sequence in lax mode.
 */
static JsonPathExecResult
executeItemOptUnwrapResult(JsonPathExecContext *cxt, JsonPathItem *jsp,
						   JsonbValue *jb, bool unwrap,
						   JsonValueList *found)
{
	if (unwrap && jspAutoUnwrap(cxt))
	{
		JsonValueList seq = EmptyJsonValueList;
		JsonValueListIterator it;
		JsonPathExecResult res = executeItem(cxt, jsp, jb, &seq);
		JsonbValue *item;

		if (jperIsError(res))
			return res;

		JsonValueListInitIterator(&seq, &it);
		while ((item = JsonValueListNext(&seq, &it)))
		{
			Assert(item->type != jbvArray);

			if (JsonbType(item) == jbvArray)
				executeItemUnwrapTargetArray(cxt, NULL, item, found, false);
			else
				JsonValueListAppend(found, item);
		}

		return jperOk;
	}

	return executeItem(cxt, jsp, jb, found);
}

/*
 * Same as executeItemOptUnwrapResult(), but with error suppression.
 */
static JsonPathExecResult
executeItemOptUnwrapResultNoThrow(JsonPathExecContext *cxt,
								  JsonPathItem *jsp,
								  JsonbValue *jb, bool unwrap,
								  JsonValueList *found)
{
	JsonPathExecResult res;
	bool		throwErrors = cxt->throwErrors;

	cxt->throwErrors = false;
	res = executeItemOptUnwrapResult(cxt, jsp, jb, unwrap, found);
	cxt->throwErrors = throwErrors;

	return res;
}

/* Execute boolean-valued jsonpath expression. */
static JsonPathBool
executeBoolItem(JsonPathExecContext *cxt, JsonPathItem *jsp,
				JsonbValue *jb, bool canHaveNext)
{
	JsonPathItem larg;
	JsonPathItem rarg;
	JsonPathBool res;
	JsonPathBool res2;

	if (!canHaveNext && jspHasNext(jsp))
		elog(ERROR, "boolean jsonpath item cannot have next item");

	switch (jsp->type)
	{
		case jpiAnd:
			jspGetLeftArg(jsp, &larg);
			res = executeBoolItem(cxt, &larg, jb, false);

			if (res == jpbFalse)
				return jpbFalse;

			/*
			 * SQL/JSON says that we should check second arg in case of
			 * jperError
			 */

			jspGetRightArg(jsp, &rarg);
			res2 = executeBoolItem(cxt, &rarg, jb, false);

			return res2 == jpbTrue ? res : res2;

		case jpiOr:
			jspGetLeftArg(jsp, &larg);
			res = executeBoolItem(cxt, &larg, jb, false);

			if (res == jpbTrue)
				return jpbTrue;

			jspGetRightArg(jsp, &rarg);
			res2 = executeBoolItem(cxt, &rarg, jb, false);

			return res2 == jpbFalse ? res : res2;

		case jpiNot:
			jspGetArg(jsp, &larg);

			res = executeBoolItem(cxt, &larg, jb, false);

			if (res == jpbUnknown)
				return jpbUnknown;

			return res == jpbTrue ? jpbFalse : jpbTrue;

		case jpiIsUnknown:
			jspGetArg(jsp, &larg);
			res = executeBoolItem(cxt, &larg, jb, false);
			return res == jpbUnknown ? jpbTrue : jpbFalse;

		case jpiEqual:
		case jpiNotEqual:
		case jpiLess:
		case jpiGreater:
		case jpiLessOrEqual:
		case jpiGreaterOrEqual:
			jspGetLeftArg(jsp, &larg);
			jspGetRightArg(jsp, &rarg);
			return executePredicate(cxt, jsp, &larg, &rarg, jb, true,
									executeComparison, NULL);

		case jpiStartsWith:		/* 'whole STARTS WITH initial' */
			jspGetLeftArg(jsp, &larg);	/* 'whole' */
			jspGetRightArg(jsp, &rarg); /* 'initial' */
			return executePredicate(cxt, jsp, &larg, &rarg, jb, false,
									executeStartsWith, NULL);

		case jpiLikeRegex:		/* 'expr LIKE_REGEX pattern FLAGS flags' */
			{
				/*
				 * 'expr' is a sequence-returning expression.  'pattern' is a
				 * regex string literal.  SQL/JSON standard requires XQuery
				 * regexes, but we use Postgres regexes here.  'flags' is a
				 * string literal converted to integer flags at compile-time.
				 */
				JsonLikeRegexContext lrcxt = EmptyJsonLikeRegexContext;

				jspInitByBuffer(&larg, jsp->base,
								jsp->content.like_regex.expr);

				return executePredicate(cxt, jsp, &larg, NULL, jb, false,
										executeLikeRegex, &lrcxt);
			}

		case jpiExists:
			jspGetArg(jsp, &larg);

			if (jspStrictAbsenseOfErrors(cxt))
			{
				/*
				 * In strict mode we must get a complete list of values to
				 * check that there are no errors at all.
				 */
				JsonValueList vals = EmptyJsonValueList;
				JsonPathExecResult res =
				executeItemOptUnwrapResultNoThrow(cxt, &larg, jb,
												  false, &vals);

				if (jperIsError(res))
					return jpbUnknown;

				return JsonValueListIsEmpty(&vals) ? jpbFalse : jpbTrue;
			}
			else
			{
				JsonPathExecResult res =
				executeItemOptUnwrapResultNoThrow(cxt, &larg, jb,
												  false, NULL);

				if (jperIsError(res))
					return jpbUnknown;

				return res == jperOk ? jpbTrue : jpbFalse;
			}

		default:
			elog(ERROR, "invalid boolean jsonpath item type: %d", jsp->type);
			return jpbUnknown;
	}
}

/*
 * Execute nested (filters etc.) boolean expression pushing current SQL/JSON
 * item onto the stack.
 */
static JsonPathBool
executeNestedBoolItem(JsonPathExecContext *cxt, JsonPathItem *jsp,
					  JsonbValue *jb)
{
	JsonbValue *prev;
	JsonPathBool res;

	prev = cxt->current;
	cxt->current = jb;
	res = executeBoolItem(cxt, jsp, jb, false);
	cxt->current = prev;

	return res;
}

/*
 * Implementation of several jsonpath nodes:
 *  - jpiAny (.** accessor),
 *  - jpiAnyKey (.* accessor),
 *  - jpiAnyArray ([*] accessor)
 */
static JsonPathExecResult
executeAnyItem(JsonPathExecContext *cxt, JsonPathItem *jsp, JsonbContainer *jbc,
			   JsonValueList *found, uint32 level, uint32 first, uint32 last,
			   bool ignoreStructuralErrors, bool unwrapNext)
{
	JsonPathExecResult res = jperNotFound;
	JsonbIterator *it;
	int32		r;
	JsonbValue	v;

	check_stack_depth();

	if (level > last)
		return res;

	it = JsonbIteratorInit(jbc);

	/*
	 * Recursively iterate over jsonb objects/arrays
	 */
	while ((r = JsonbIteratorNext(&it, &v, true)) != WJB_DONE)
	{
		if (r == WJB_KEY)
		{
			r = JsonbIteratorNext(&it, &v, true);
			Assert(r == WJB_VALUE);
		}

		if (r == WJB_VALUE || r == WJB_ELEM)
		{

			if (level >= first ||
				(first == PG_UINT32_MAX && last == PG_UINT32_MAX &&
				 v.type != jbvBinary))	/* leaves only requested */
			{
				/* check expression */
				if (jsp)
				{
					if (ignoreStructuralErrors)
					{
						bool		savedIgnoreStructuralErrors;

						savedIgnoreStructuralErrors = cxt->ignoreStructuralErrors;
						cxt->ignoreStructuralErrors = true;
						res = executeItemOptUnwrapTarget(cxt, jsp, &v, found, unwrapNext);
						cxt->ignoreStructuralErrors = savedIgnoreStructuralErrors;
					}
					else
						res = executeItemOptUnwrapTarget(cxt, jsp, &v, found, unwrapNext);

					if (jperIsError(res))
						break;

					if (res == jperOk && !found)
						break;
				}
				else if (found)
					JsonValueListAppend(found, copyJsonbValue(&v));
				else
					return jperOk;
			}

			if (level < last && v.type == jbvBinary)
			{
				res = executeAnyItem
					(cxt, jsp, v.val.binary.data, found,
					 level + 1, first, last,
					 ignoreStructuralErrors, unwrapNext);

				if (jperIsError(res))
					break;

				if (res == jperOk && found == NULL)
					break;
			}
		}
	}

	return res;
}

/*
 * Execute unary or binary predicate.
 *
 * Predicates have existence semantics, because their operands are item
 * sequences.  Pairs of items from the left and right operand's sequences are
 * checked.  TRUE returned only if any pair satisfying the condition is found.
 * In strict mode, even if the desired pair has already been found, all pairs
 * still need to be examined to check the absence of errors.  If any error
 * occurs, UNKNOWN (analogous to SQL NULL) is returned.
 */
static JsonPathBool
executePredicate(JsonPathExecContext *cxt, JsonPathItem *pred,
				 JsonPathItem *larg, JsonPathItem *rarg, JsonbValue *jb,
				 bool unwrapRightArg, JsonPathPredicateCallback exec,
				 void *param)
{
	JsonPathExecResult res;
	JsonValueListIterator lseqit;
	JsonValueList lseq = EmptyJsonValueList;
	JsonValueList rseq = EmptyJsonValueList;
	JsonbValue *lval;
	bool		error = false;
	bool		found = false;

	/* Left argument is always auto-unwrapped. */
	res = executeItemOptUnwrapResultNoThrow(cxt, larg, jb, true, &lseq);
	if (jperIsError(res))
		return jpbUnknown;

	if (rarg)
	{
		/* Right argument is conditionally auto-unwrapped. */
		res = executeItemOptUnwrapResultNoThrow(cxt, rarg, jb,
												unwrapRightArg, &rseq);
		if (jperIsError(res))
			return jpbUnknown;
	}

	JsonValueListInitIterator(&lseq, &lseqit);
	while ((lval = JsonValueListNext(&lseq, &lseqit)))
	{
		JsonValueListIterator rseqit;
		JsonbValue *rval;
		bool		first = true;

		if (rarg)
		{
			JsonValueListInitIterator(&rseq, &rseqit);
			rval = JsonValueListNext(&rseq, &rseqit);
		}
		else
		{
			rval = NULL;
		}

		/* Loop over right arg sequence or do single pass otherwise */
		while (rarg ? (rval != NULL) : first)
		{
			JsonPathBool res = exec(pred, lval, rval, param);

			if (res == jpbUnknown)
			{
				if (jspStrictAbsenseOfErrors(cxt))
					return jpbUnknown;

				error = true;
			}
			else if (res == jpbTrue)
			{
				if (!jspStrictAbsenseOfErrors(cxt))
					return jpbTrue;

				found = true;
			}

			first = false;
			if (rarg)
				rval = JsonValueListNext(&rseq, &rseqit);
		}
	}

	if (found)					/* possible only in strict mode */
		return jpbTrue;

	if (error)					/* possible only in lax mode */
		return jpbUnknown;

	return jpbFalse;
}

/*
 * Execute binary arithmetic expression on singleton numeric operands.
 * Array operands are automatically unwrapped in lax mode.
 */
static JsonPathExecResult
executeBinaryArithmExpr(JsonPathExecContext *cxt, JsonPathItem *jsp,
						JsonbValue *jb, BinaryArithmFunc func,
						JsonValueList *found)
{
	JsonPathExecResult jper;
	JsonPathItem elem;
	JsonValueList lseq = EmptyJsonValueList;
	JsonValueList rseq = EmptyJsonValueList;
	JsonbValue *lval;
	JsonbValue *rval;
	Numeric		res;

	jspGetLeftArg(jsp, &elem);

	/*
	 * XXX: By standard only operands of multiplicative expressions are
	 * unwrapped.  We extend it to other binary arithmetics expressions too.
	 */
	jper = executeItemOptUnwrapResult(cxt, &elem, jb, true, &lseq);
	if (jperIsError(jper))
		return jper;

	jspGetRightArg(jsp, &elem);

	jper = executeItemOptUnwrapResult(cxt, &elem, jb, true, &rseq);
	if (jperIsError(jper))
		return jper;

	if (JsonValueListLength(&lseq) != 1 ||
		!(lval = getScalar(JsonValueListHead(&lseq), jbvNumeric)))
		RETURN_ERROR(ereport(ERROR,
							 (errcode(ERRCODE_SINGLETON_SQL_JSON_ITEM_REQUIRED),
							  errmsg("left operand of jsonpath operator %s is not a single numeric value",
									 jspOperationName(jsp->type)))));

	if (JsonValueListLength(&rseq) != 1 ||
		!(rval = getScalar(JsonValueListHead(&rseq), jbvNumeric)))
		RETURN_ERROR(ereport(ERROR,
							 (errcode(ERRCODE_SINGLETON_SQL_JSON_ITEM_REQUIRED),
							  errmsg("right operand of jsonpath operator %s is not a single numeric value",
									 jspOperationName(jsp->type)))));

	if (jspThrowErrors(cxt))
	{
		res = func(lval->val.numeric, rval->val.numeric, NULL);
	}
	else
	{
		bool		error = false;

		res = func(lval->val.numeric, rval->val.numeric, &error);

		if (error)
			return jperError;
	}

	if (!jspGetNext(jsp, &elem) && !found)
		return jperOk;

	lval = palloc(sizeof(*lval));
	lval->type = jbvNumeric;
	lval->val.numeric = res;

	return executeNextItem(cxt, jsp, &elem, lval, found, false);
}

/*
 * Execute unary arithmetic expression for each numeric item in its operand's
 * sequence.  Array operand is automatically unwrapped in lax mode.
 */
static JsonPathExecResult
executeUnaryArithmExpr(JsonPathExecContext *cxt, JsonPathItem *jsp,
					   JsonbValue *jb, PGFunction func, JsonValueList *found)
{
	JsonPathExecResult jper;
	JsonPathExecResult jper2;
	JsonPathItem elem;
	JsonValueList seq = EmptyJsonValueList;
	JsonValueListIterator it;
	JsonbValue *val;
	bool		hasNext;

	jspGetArg(jsp, &elem);
	jper = executeItemOptUnwrapResult(cxt, &elem, jb, true, &seq);

	if (jperIsError(jper))
		return jper;

	jper = jperNotFound;

	hasNext = jspGetNext(jsp, &elem);

	JsonValueListInitIterator(&seq, &it);
	while ((val = JsonValueListNext(&seq, &it)))
	{
		if ((val = getScalar(val, jbvNumeric)))
		{
			if (!found && !hasNext)
				return jperOk;
		}
		else
		{
			if (!found && !hasNext)
				continue;		/* skip non-numerics processing */

			RETURN_ERROR(ereport(ERROR,
								 (errcode(ERRCODE_SQL_JSON_NUMBER_NOT_FOUND),
								  errmsg("operand of unary jsonpath operator %s is not a numeric value",
										 jspOperationName(jsp->type)))));
		}

		if (func)
			val->val.numeric =
				DatumGetNumeric(DirectFunctionCall1(func,
													NumericGetDatum(val->val.numeric)));

		jper2 = executeNextItem(cxt, jsp, &elem, val, found, false);

		if (jperIsError(jper2))
			return jper2;

		if (jper2 == jperOk)
		{
			if (!found)
				return jperOk;
			jper = jperOk;
		}
	}

	return jper;
}

/*
 * STARTS_WITH predicate callback.
 *
 * Check if the 'whole' string starts from 'initial' string.
 */
static JsonPathBool
executeStartsWith(JsonPathItem *jsp, JsonbValue *whole, JsonbValue *initial,
				  void *param)
{
	if (!(whole = getScalar(whole, jbvString)))
		return jpbUnknown;		/* error */

	if (!(initial = getScalar(initial, jbvString)))
		return jpbUnknown;		/* error */

	if (whole->val.string.len >= initial->val.string.len &&
		!memcmp(whole->val.string.val,
				initial->val.string.val,
				initial->val.string.len))
		return jpbTrue;

	return jpbFalse;
}

/*
 * LIKE_REGEX predicate callback.
 *
 * Check if the string matches regex pattern.
 */
static JsonPathBool
executeLikeRegex(JsonPathItem *jsp, JsonbValue *str, JsonbValue *rarg,
				 void *param)
{
	JsonLikeRegexContext *cxt = param;

	if (!(str = getScalar(str, jbvString)))
		return jpbUnknown;

	/* Cache regex text and converted flags. */
	if (!cxt->regex)
	{
		cxt->regex =
			cstring_to_text_with_len(jsp->content.like_regex.pattern,
									 jsp->content.like_regex.patternlen);
		cxt->cflags = jspConvertRegexFlags(jsp->content.like_regex.flags);
	}

	if (RE_compile_and_execute(cxt->regex, str->val.string.val,
							   str->val.string.len,
							   cxt->cflags, DEFAULT_COLLATION_OID, 0, NULL))
		return jpbTrue;

	return jpbFalse;
}

/*
 * Execute numeric item methods (.abs(), .floor(), .ceil()) using the specified
 * user function 'func'.
 */
static JsonPathExecResult
executeNumericItemMethod(JsonPathExecContext *cxt, JsonPathItem *jsp,
						 JsonbValue *jb, bool unwrap, PGFunction func,
						 JsonValueList *found)
{
	JsonPathItem next;
	Datum		datum;

	if (unwrap && JsonbType(jb) == jbvArray)
		return executeItemUnwrapTargetArray(cxt, jsp, jb, found, false);

	if (!(jb = getScalar(jb, jbvNumeric)))
		RETURN_ERROR(ereport(ERROR,
							 (errcode(ERRCODE_NON_NUMERIC_SQL_JSON_ITEM),
							  errmsg("jsonpath item method .%s() can only be applied to a numeric value",
									 jspOperationName(jsp->type)))));

	datum = DirectFunctionCall1(func, NumericGetDatum(jb->val.numeric));

	if (!jspGetNext(jsp, &next) && !found)
		return jperOk;

	jb = palloc(sizeof(*jb));
	jb->type = jbvNumeric;
	jb->val.numeric = DatumGetNumeric(datum);

	return executeNextItem(cxt, jsp, &next, jb, found, false);
}

/*
 * Implementation of .keyvalue() method.
 *
 * .keyvalue() method returns a sequence of object's key-value pairs in the
 * following format: '{ "key": key, "value": value, "id": id }'.
 *
 * "id" field is an object identifier which is constructed from the two parts:
 * base object id and its binary offset in base object's jsonb:
 * id = 10000000000 * base_object_id + obj_offset_in_base_object
 *
 * 10000000000 (10^10) -- is a first round decimal number greater than 2^32
 * (maximal offset in jsonb).  Decimal multiplier is used here to improve the
 * readability of identifiers.
 *
 * Base object is usually a root object of the path: context item '$' or path
 * variable '$var', literals can't produce objects for now.  But if the path
 * contains generated objects (.keyvalue() itself, for example), then they
 * become base object for the subsequent .keyvalue().
 *
 * Id of '$' is 0. Id of '$var' is its ordinal (positive) number in the list
 * of variables (see getJsonPathVariable()).  Ids for generated objects
 * are assigned using global counter JsonPathExecContext.lastGeneratedObjectId.
 */
static JsonPathExecResult
executeKeyValueMethod(JsonPathExecContext *cxt, JsonPathItem *jsp,
					  JsonbValue *jb, JsonValueList *found)
{
	JsonPathExecResult res = jperNotFound;
	JsonPathItem next;
	JsonbContainer *jbc;
	JsonbValue	key;
	JsonbValue	val;
	JsonbValue	idval;
	JsonbValue	keystr;
	JsonbValue	valstr;
	JsonbValue	idstr;
	JsonbIterator *it;
	JsonbIteratorToken tok;
	int64		id;
	bool		hasNext;

	if (JsonbType(jb) != jbvObject || jb->type != jbvBinary)
		RETURN_ERROR(ereport(ERROR,
							 (errcode(ERRCODE_SQL_JSON_OBJECT_NOT_FOUND),
							  errmsg("jsonpath item method .%s() can only be applied to an object",
									 jspOperationName(jsp->type)))));

	jbc = jb->val.binary.data;

	if (!JsonContainerSize(jbc))
		return jperNotFound;	/* no key-value pairs */

	hasNext = jspGetNext(jsp, &next);

	keystr.type = jbvString;
	keystr.val.string.val = "key";
	keystr.val.string.len = 3;

	valstr.type = jbvString;
	valstr.val.string.val = "value";
	valstr.val.string.len = 5;

	idstr.type = jbvString;
	idstr.val.string.val = "id";
	idstr.val.string.len = 2;

	/* construct object id from its base object and offset inside that */
	id = jb->type != jbvBinary ? 0 :
		(int64) ((char *) jbc - (char *) cxt->baseObject.jbc);
	id += (int64) cxt->baseObject.id * INT64CONST(10000000000);

	idval.type = jbvNumeric;
	idval.val.numeric = DatumGetNumeric(DirectFunctionCall1(int8_numeric,
															Int64GetDatum(id)));

	it = JsonbIteratorInit(jbc);

	while ((tok = JsonbIteratorNext(&it, &key, true)) != WJB_DONE)
	{
		JsonBaseObjectInfo baseObject;
		JsonbValue	obj;
		JsonbParseState *ps;
		JsonbValue *keyval;
		Jsonb	   *jsonb;

		if (tok != WJB_KEY)
			continue;

		res = jperOk;

		if (!hasNext && !found)
			break;

		tok = JsonbIteratorNext(&it, &val, true);
		Assert(tok == WJB_VALUE);

		ps = NULL;
		pushJsonbValue(&ps, WJB_BEGIN_OBJECT, NULL);

		pushJsonbValue(&ps, WJB_KEY, &keystr);
		pushJsonbValue(&ps, WJB_VALUE, &key);

		pushJsonbValue(&ps, WJB_KEY, &valstr);
		pushJsonbValue(&ps, WJB_VALUE, &val);

		pushJsonbValue(&ps, WJB_KEY, &idstr);
		pushJsonbValue(&ps, WJB_VALUE, &idval);

		keyval = pushJsonbValue(&ps, WJB_END_OBJECT, NULL);

		jsonb = JsonbValueToJsonb(keyval);

		JsonbInitBinary(&obj, jsonb);

		baseObject = setBaseObject(cxt, &obj, cxt->lastGeneratedObjectId++);

		res = executeNextItem(cxt, jsp, &next, &obj, found, true);

		cxt->baseObject = baseObject;

		if (jperIsError(res))
			return res;

		if (res == jperOk && !found)
			break;
	}

	return res;
}

/*
 * Convert boolean execution status 'res' to a boolean JSON item and execute
 * next jsonpath.
 */
static JsonPathExecResult
appendBoolResult(JsonPathExecContext *cxt, JsonPathItem *jsp,
				 JsonValueList *found, JsonPathBool res)
{
	JsonPathItem next;
	JsonbValue	jbv;

	if (!jspGetNext(jsp, &next) && !found)
		return jperOk;			/* found singleton boolean value */

	if (res == jpbUnknown)
	{
		jbv.type = jbvNull;
	}
	else
	{
		jbv.type = jbvBool;
		jbv.val.boolean = res == jpbTrue;
	}

	return executeNextItem(cxt, jsp, &next, &jbv, found, true);
}

/*
 * Convert jsonpath's scalar or variable node to actual jsonb value.
 *
 * If node is a variable then its id returned, otherwise 0 returned.
 */
static void
getJsonPathItem(JsonPathExecContext *cxt, JsonPathItem *item,
				JsonbValue *value)
{
	switch (item->type)
	{
		case jpiNull:
			value->type = jbvNull;
			break;
		case jpiBool:
			value->type = jbvBool;
			value->val.boolean = jspGetBool(item);
			break;
		case jpiNumeric:
			value->type = jbvNumeric;
			value->val.numeric = jspGetNumeric(item);
			break;
		case jpiString:
			value->type = jbvString;
			value->val.string.val = jspGetString(item,
												 &value->val.string.len);
			break;
		case jpiVariable:
			getJsonPathVariable(cxt, item, cxt->vars, value);
			return;
		default:
			elog(ERROR, "unexpected jsonpath item type");
	}
}

/*
 * Get the value of variable passed to jsonpath executor
 */
static void
getJsonPathVariable(JsonPathExecContext *cxt, JsonPathItem *variable,
					Jsonb *vars, JsonbValue *value)
{
	char	   *varName;
	int			varNameLength;
	JsonbValue	tmp;
	JsonbValue *v;

	if (!vars)
	{
		value->type = jbvNull;
		return;
	}

	Assert(variable->type == jpiVariable);
	varName = jspGetString(variable, &varNameLength);
	tmp.type = jbvString;
	tmp.val.string.val = varName;
	tmp.val.string.len = varNameLength;

	v = findJsonbValueFromContainer(&vars->root, JB_FOBJECT, &tmp);

	if (v)
	{
		*value = *v;
		pfree(v);
	}
	else
	{
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("cannot find jsonpath variable \"%s\"",
						pnstrdup(varName, varNameLength))));
	}

	JsonbInitBinary(&tmp, vars);
	setBaseObject(cxt, &tmp, 1);
}

/**************** Support functions for JsonPath execution *****************/

/*
 * Returns the size of an array item, or -1 if item is not an array.
 */
static int
JsonbArraySize(JsonbValue *jb)
{
	Assert(jb->type != jbvArray);

	if (jb->type == jbvBinary)
	{
		JsonbContainer *jbc = jb->val.binary.data;

		if (JsonContainerIsArray(jbc) && !JsonContainerIsScalar(jbc))
			return JsonContainerSize(jbc);
	}

	return -1;
}

/* Comparison predicate callback. */
static JsonPathBool
executeComparison(JsonPathItem *cmp, JsonbValue *lv, JsonbValue *rv, void *p)
{
	return compareItems(cmp->type, lv, rv);
}

/*
 * Perform per-byte comparison of two strings.
 */
static int
binaryCompareStrings(const char *s1, int len1,
					 const char *s2, int len2)
{
	int			cmp;

	cmp = memcmp(s1, s2, Min(len1, len2));

	if (cmp != 0)
		return cmp;

	if (len1 == len2)
		return 0;

	return len1 < len2 ? -1 : 1;
}

/*
 * Compare two strings in the current server encoding using Unicode codepoint
 * collation.
 */
static int
compareStrings(const char *mbstr1, int mblen1,
			   const char *mbstr2, int mblen2)
{
	if (GetDatabaseEncoding() == PG_SQL_ASCII ||
		GetDatabaseEncoding() == PG_UTF8)
	{
		/*
		 * It's known property of UTF-8 strings that their per-byte comparison
		 * result matches codepoints comparison result.  ASCII can be
		 * considered as special case of UTF-8.
		 */
		return binaryCompareStrings(mbstr1, mblen1, mbstr2, mblen2);
	}
	else
	{
		char	   *utf8str1,
				   *utf8str2;
		int			cmp,
					utf8len1,
					utf8len2;

		/*
		 * We have to convert other encodings to UTF-8 first, then compare.
		 * Input strings may be not null-terminated and pg_server_to_any() may
		 * return them "as is".  So, use strlen() only if there is real
		 * conversion.
		 */
		utf8str1 = pg_server_to_any(mbstr1, mblen1, PG_UTF8);
		utf8str2 = pg_server_to_any(mbstr2, mblen2, PG_UTF8);
		utf8len1 = (mbstr1 == utf8str1) ? mblen1 : strlen(utf8str1);
		utf8len2 = (mbstr2 == utf8str2) ? mblen2 : strlen(utf8str2);

		cmp = binaryCompareStrings(utf8str1, utf8len1, utf8str2, utf8len2);

		/*
		 * If pg_server_to_any() did no real conversion, then we actually
		 * compared original strings.  So, we already done.
		 */
		if (mbstr1 == utf8str1 && mbstr2 == utf8str2)
			return cmp;

		/* Free memory if needed */
		if (mbstr1 != utf8str1)
			pfree(utf8str1);
		if (mbstr2 != utf8str2)
			pfree(utf8str2);

		/*
		 * When all Unicode codepoints are equal, return result of binary
		 * comparison.  In some edge cases, same characters may have different
		 * representations in encoding.  Then our behavior could diverge from
		 * standard.  However, that allow us to do simple binary comparison
		 * for "==" operator, which is performance critical in typical cases.
		 * In future to implement strict standard conformance, we can do
		 * normalization of input JSON strings.
		 */
		if (cmp == 0)
			return binaryCompareStrings(mbstr1, mblen1, mbstr2, mblen2);
		else
			return cmp;
	}
}

/*
 * Compare two SQL/JSON items using comparison operation 'op'.
 */
static JsonPathBool
compareItems(int32 op, JsonbValue *jb1, JsonbValue *jb2)
{
	int			cmp;
	bool		res;

	if (jb1->type != jb2->type)
	{
		if (jb1->type == jbvNull || jb2->type == jbvNull)

			/*
			 * Equality and order comparison of nulls to non-nulls returns
			 * always false, but inequality comparison returns true.
			 */
			return op == jpiNotEqual ? jpbTrue : jpbFalse;

		/* Non-null items of different types are not comparable. */
		return jpbUnknown;
	}

	switch (jb1->type)
	{
		case jbvNull:
			cmp = 0;
			break;
		case jbvBool:
			cmp = jb1->val.boolean == jb2->val.boolean ? 0 :
				jb1->val.boolean ? 1 : -1;
			break;
		case jbvNumeric:
			cmp = compareNumeric(jb1->val.numeric, jb2->val.numeric);
			break;
		case jbvString:
			if (op == jpiEqual)
				return jb1->val.string.len != jb2->val.string.len ||
					memcmp(jb1->val.string.val,
						   jb2->val.string.val,
						   jb1->val.string.len) ? jpbFalse : jpbTrue;

			cmp = compareStrings(jb1->val.string.val, jb1->val.string.len,
								 jb2->val.string.val, jb2->val.string.len);
			break;

		case jbvBinary:
		case jbvArray:
		case jbvObject:
			return jpbUnknown;	/* non-scalars are not comparable */

		default:
			elog(ERROR, "invalid jsonb value type %d", jb1->type);
	}

	switch (op)
	{
		case jpiEqual:
			res = (cmp == 0);
			break;
		case jpiNotEqual:
			res = (cmp != 0);
			break;
		case jpiLess:
			res = (cmp < 0);
			break;
		case jpiGreater:
			res = (cmp > 0);
			break;
		case jpiLessOrEqual:
			res = (cmp <= 0);
			break;
		case jpiGreaterOrEqual:
			res = (cmp >= 0);
			break;
		default:
			elog(ERROR, "unrecognized jsonpath operation: %d", op);
			return jpbUnknown;
	}

	return res ? jpbTrue : jpbFalse;
}

/* Compare two numerics */
static int
compareNumeric(Numeric a, Numeric b)
{
	return DatumGetInt32(DirectFunctionCall2(numeric_cmp,
											 NumericGetDatum(a),
											 NumericGetDatum(b)));
}

static JsonbValue *
copyJsonbValue(JsonbValue *src)
{
	JsonbValue *dst = palloc(sizeof(*dst));

	*dst = *src;

	return dst;
}

/*
 * Execute array subscript expression and convert resulting numeric item to
 * the integer type with truncation.
 */
static JsonPathExecResult
getArrayIndex(JsonPathExecContext *cxt, JsonPathItem *jsp, JsonbValue *jb,
			  int32 *index)
{
	JsonbValue *jbv;
	JsonValueList found = EmptyJsonValueList;
	JsonPathExecResult res = executeItem(cxt, jsp, jb, &found);
	Datum		numeric_index;
	bool		have_error = false;

	if (jperIsError(res))
		return res;

	if (JsonValueListLength(&found) != 1 ||
		!(jbv = getScalar(JsonValueListHead(&found), jbvNumeric)))
		RETURN_ERROR(ereport(ERROR,
							 (errcode(ERRCODE_INVALID_SQL_JSON_SUBSCRIPT),
							  errmsg("jsonpath array subscript is not a single numeric value"))));

	numeric_index = DirectFunctionCall2(numeric_trunc,
										NumericGetDatum(jbv->val.numeric),
										Int32GetDatum(0));

	*index = numeric_int4_opt_error(DatumGetNumeric(numeric_index),
									&have_error);

	if (have_error)
		RETURN_ERROR(ereport(ERROR,
							 (errcode(ERRCODE_INVALID_SQL_JSON_SUBSCRIPT),
							  errmsg("jsonpath array subscript is out of integer range"))));

	return jperOk;
}

/* Save base object and its id needed for the execution of .keyvalue(). */
static JsonBaseObjectInfo
setBaseObject(JsonPathExecContext *cxt, JsonbValue *jbv, int32 id)
{
	JsonBaseObjectInfo baseObject = cxt->baseObject;

	cxt->baseObject.jbc = jbv->type != jbvBinary ? NULL :
		(JsonbContainer *) jbv->val.binary.data;
	cxt->baseObject.id = id;

	return baseObject;
}

static void
JsonValueListAppend(JsonValueList *jvl, JsonbValue *jbv)
{
	if (jvl->singleton)
	{
		jvl->list = list_make2(jvl->singleton, jbv);
		jvl->singleton = NULL;
	}
	else if (!jvl->list)
		jvl->singleton = jbv;
	else
		jvl->list = lappend(jvl->list, jbv);
}

static int
JsonValueListLength(const JsonValueList *jvl)
{
	return jvl->singleton ? 1 : list_length(jvl->list);
}

static bool
JsonValueListIsEmpty(JsonValueList *jvl)
{
	return !jvl->singleton && list_length(jvl->list) <= 0;
}

static JsonbValue *
JsonValueListHead(JsonValueList *jvl)
{
	return jvl->singleton ? jvl->singleton : linitial(jvl->list);
}

static List *
JsonValueListGetList(JsonValueList *jvl)
{
	if (jvl->singleton)
		return list_make1(jvl->singleton);

	return jvl->list;
}

static void
JsonValueListInitIterator(const JsonValueList *jvl, JsonValueListIterator *it)
{
	if (jvl->singleton)
	{
		it->value = jvl->singleton;
		it->next = NULL;
	}
	else if (list_head(jvl->list) != NULL)
	{
		it->value = (JsonbValue *) linitial(jvl->list);
		it->next = lnext(list_head(jvl->list));
	}
	else
	{
		it->value = NULL;
		it->next = NULL;
	}
}

/*
 * Get the next item from the sequence advancing iterator.
 */
static JsonbValue *
JsonValueListNext(const JsonValueList *jvl, JsonValueListIterator *it)
{
	JsonbValue *result = it->value;

	if (it->next)
	{
		it->value = lfirst(it->next);
		it->next = lnext(it->next);
	}
	else
	{
		it->value = NULL;
	}

	return result;
}

/*
 * Initialize a binary JsonbValue with the given jsonb container.
 */
static JsonbValue *
JsonbInitBinary(JsonbValue *jbv, Jsonb *jb)
{
	jbv->type = jbvBinary;
	jbv->val.binary.data = &jb->root;
	jbv->val.binary.len = VARSIZE_ANY_EXHDR(jb);

	return jbv;
}

/*
 * Returns jbv* type of of JsonbValue. Note, it never returns jbvBinary as is.
 */
static int
JsonbType(JsonbValue *jb)
{
	int			type = jb->type;

	if (jb->type == jbvBinary)
	{
		JsonbContainer *jbc = (void *) jb->val.binary.data;

		/* Scalars should be always extracted during jsonpath execution. */
		Assert(!JsonContainerIsScalar(jbc));

		if (JsonContainerIsObject(jbc))
			type = jbvObject;
		else if (JsonContainerIsArray(jbc))
			type = jbvArray;
		else
			elog(ERROR, "invalid jsonb container type: 0x%08x", jbc->header);
	}

	return type;
}

/* Get scalar of given type or NULL on type mismatch */
static JsonbValue *
getScalar(JsonbValue *scalar, enum jbvType type)
{
	/* Scalars should be always extracted during jsonpath execution. */
	Assert(scalar->type != jbvBinary ||
		   !JsonContainerIsScalar(scalar->val.binary.data));

	return scalar->type == type ? scalar : NULL;
}

/* Construct a JSON array from the item list */
static JsonbValue *
wrapItemsInArray(const JsonValueList *items)
{
	JsonbParseState *ps = NULL;
	JsonValueListIterator it;
	JsonbValue *jbv;

	pushJsonbValue(&ps, WJB_BEGIN_ARRAY, NULL);

	JsonValueListInitIterator(items, &it);
	while ((jbv = JsonValueListNext(items, &it)))
		pushJsonbValue(&ps, WJB_ELEM, jbv);

	return pushJsonbValue(&ps, WJB_END_ARRAY, NULL);
}
