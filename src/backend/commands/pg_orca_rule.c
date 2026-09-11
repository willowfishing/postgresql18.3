/*-------------------------------------------------------------------------
 *
 * pg_orca_rule.c
 *	  ExecInsertRuleStmt: handle INSERT RULE 'name' AS 'text'
 *
 *	  pg_orca custom syntax: adds a query-rewrite rule to the
 *	  pg_orca_rule system catalog.
 *
 * src/backend/commands/pg_orca_rule.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/genam.h"
#include "access/htup_details.h"
#include "access/table.h"
#include "access/xact.h"
#include "catalog/indexing.h"
#include "catalog/pg_orca_rule.h"
#include "commands/pg_orca_rule.h"
#include "miscadmin.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/rel.h"

/*
 * ParseRuleId
 *		Convert a numeric parser node to a positive 32-bit rule id.
 */
static int64
ParseRuleId(Node *node)
{
	int64		ruleid;

	if (IsA(node, Integer))
		ruleid = intVal(node);
	else if (IsA(node, Float))
		ruleid = pg_strtoint64(castNode(Float, node)->fval);
	else
		elog(ERROR, "unexpected rule id node type: %d", (int) nodeTag(node));

	if (ruleid <= 0 || ruleid > PG_UINT32_MAX)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("rule id must be a positive 32-bit integer")));

	return ruleid;
}

/*
 * FindRuleById
 *		Return a *copied* tuple whose id matches rule_id, or NULL.
 *		Caller must heap_freetuple() the returned tuple.
 */
static HeapTuple
FindRuleById(Relation rel, int64 rule_id)
{
	SysScanDesc scan;
	ScanKeyData key;
	HeapTuple	tup;

	ScanKeyInit(&key,
				Anum_pg_orca_rule_id,
				BTEqualStrategyNumber, F_INT8EQ,
				Int64GetDatum(rule_id));

	scan = systable_beginscan(rel, OrcaRuleIdIndexId, true,
							  NULL, 1, &key);
	tup = systable_getnext(scan);

	if (tup != NULL)
		tup = heap_copytuple(tup);
	systable_endscan(scan);

	return tup;
}

/*
 * ExecInsertRuleStmt
 *		INSERT RULE 'rule_name' AS 'rule_text'
 *
 *		Superuser-only; the rule text is stored verbatim.
 */
void
ExecInsertRuleStmt(InsertRuleStmt *stmt)
{
	Relation	rel;
	Oid			ruleoid;
	HeapTuple	tup;
	Datum		values[Natts_pg_orca_rule];
	bool		nulls[Natts_pg_orca_rule];
	NameData	rname;

	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be superuser to create an ORCA rule")));

	/* basic sanity: a non-empty name and non-empty text */
	if (stmt->rule_name == NULL || stmt->rule_name[0] == '\0')
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("rule name must not be empty")));
	if (strlen(stmt->rule_name) >= NAMEDATALEN)
		ereport(ERROR,
				(errcode(ERRCODE_NAME_TOO_LONG),
				 errmsg("rule name is too long"),
				 errdetail("Rule names must be less than %d bytes.",
						   NAMEDATALEN)));
	if (stmt->rule_text == NULL || stmt->rule_text[0] == '\0')
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("rule text must not be empty")));

	rel = table_open(OrcaRuleRelationId, RowExclusiveLock);

	memset(values, 0, sizeof(values));
	memset(nulls, false, sizeof(nulls));

	ruleoid = GetNewOidWithIndex(rel, OrcaRuleOidIndexId, Anum_pg_orca_rule_oid);
	values[Anum_pg_orca_rule_oid - 1] = ObjectIdGetDatum(ruleoid);
	values[Anum_pg_orca_rule_id - 1] = Int64GetDatum((int64) ruleoid);

	namestrcpy(&rname, stmt->rule_name);
	values[Anum_pg_orca_rule_rule_name - 1] = NameGetDatum(&rname);

	values[Anum_pg_orca_rule_rule_text - 1] = CStringGetTextDatum(stmt->rule_text);
	values[Anum_pg_orca_rule_is_enabled - 1] = BoolGetDatum(true);

	tup = heap_form_tuple(RelationGetDescr(rel), values, nulls);

	CatalogTupleInsert(rel, tup);
	heap_freetuple(tup);

	table_close(rel, RowExclusiveLock);
}

/*
 * ExecDeleteRuleStmt
 *		DELETE RULE 'rule_id'
 *
 *		Superuser-only; the rule is deleted from the catalog.
 */
void
ExecDeleteRuleStmt(DeleteRuleStmt *stmt)
{
	Relation	rel;
	HeapTuple	tup;
	int64		ruleid;

	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be superuser to delete an ORCA rule")));

	ruleid = ParseRuleId(stmt->rule_id);

	rel = table_open(OrcaRuleRelationId, RowExclusiveLock);

	tup = FindRuleById(rel, ruleid);
	if (tup == NULL)
	{
		table_close(rel, RowExclusiveLock);
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("rule with id " INT64_FORMAT " does not exist", ruleid)));
	}

	CatalogTupleDelete(rel, &tup->t_self);
	heap_freetuple(tup);
	CommandCounterIncrement();
	table_close(rel, RowExclusiveLock);
}

/*
 * ExecUpdateRuleStmt
 *		UPDATE RULE 'rule_id' AS 'rule_text'
 *
 *		Superuser-only; the rule text is updated in the catalog.
 */
void
ExecUpdateRuleStmt(UpdateRuleStmt *stmt)
{
	Relation	rel;
	TupleDesc	tupdesc;
	HeapTuple	tup;
	HeapTuple	newtup;
	Datum		values[Natts_pg_orca_rule];
	bool		nulls[Natts_pg_orca_rule];
	bool		repl[Natts_pg_orca_rule];
	int64		ruleid;

	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be superuser to update an ORCA rule")));

	ruleid = ParseRuleId(stmt->rule_id);

	if (stmt->rule_text == NULL || stmt->rule_text[0] == '\0')
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("rule text must not be empty")));

	rel = table_open(OrcaRuleRelationId, RowExclusiveLock);
	tupdesc = RelationGetDescr(rel);

	tup = FindRuleById(rel, ruleid);
	if (tup == NULL)
	{
		table_close(rel, RowExclusiveLock);
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("rule with id " INT64_FORMAT " does not exist", ruleid)));
	}

	memset(values, 0, sizeof(values));
	memset(nulls, false, sizeof(nulls));
	memset(repl, false, sizeof(repl));

	values[Anum_pg_orca_rule_rule_text - 1] = CStringGetTextDatum(stmt->rule_text);
	repl[Anum_pg_orca_rule_rule_text - 1] = true;

	newtup = heap_modify_tuple(tup, tupdesc, values, nulls, repl);
	heap_freetuple(tup);

	CatalogTupleUpdate(rel, &newtup->t_self, newtup);
	heap_freetuple(newtup);
	CommandCounterIncrement();

	table_close(rel, RowExclusiveLock);
}
