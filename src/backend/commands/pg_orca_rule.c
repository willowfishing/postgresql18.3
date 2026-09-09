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

#include "access/heapam.h"
#include "access/htup_details.h"
#include "access/xact.h"
#include "access/table.h"
#include "catalog/indexing.h"
#include "catalog/pg_orca_rule.h"
#include "commands/pg_orca_rule.h"
#include "miscadmin.h"
#include "utils/builtins.h"
#include "utils/rel.h"

/*
 * GetNextRuleId
 *		Return max(id)+1 so rule ids are auto-assigned and not reused.
 */
static int32
GetNextRuleId(Relation rel)
{
	TableScanDesc scan;
	HeapTuple	tup;
	int32		maxid = 0;

	scan = table_beginscan_catalog(rel, 0, NULL);
	while ((tup = heap_getnext(scan, ForwardScanDirection)) != NULL)
	{
		int32		id = ((Form_pg_orca_rule) GETSTRUCT(tup))->id;

		if (id > maxid)
			maxid = id;
	}
	table_endscan(scan);

	return maxid + 1;
}

/*
 * FindRuleByScan
 *		Return a *copied* tuple whose id matches rule_id, or NULL.
 *		Caller must heap_freetuple() the returned tuple.
 */
static HeapTuple
FindRuleByScan(Relation rel, int32 rule_id)
{
	TableScanDesc scan;
	HeapTuple	tup;

	scan = table_beginscan_catalog(rel, 0, NULL);
	while ((tup = heap_getnext(scan, ForwardScanDirection)) != NULL)
	{
		if (((Form_pg_orca_rule) GETSTRUCT(tup))->id == rule_id)
			break;
	}

	if (tup != NULL)
		tup = heap_copytuple(tup);
	table_endscan(scan);

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

	rel = table_open(OrcaRuleRelationId, AccessExclusiveLock);

	memset(values, 0, sizeof(values));
	memset(nulls, false, sizeof(nulls));

	ruleoid = GetNewOidWithIndex(rel, OrcaRuleOidIndexId, Anum_pg_orca_rule_oid);
	values[Anum_pg_orca_rule_oid - 1] = ObjectIdGetDatum(ruleoid);
	values[Anum_pg_orca_rule_id - 1] = Int32GetDatum(GetNextRuleId(rel));

	namestrcpy(&rname, stmt->rule_name);
	values[Anum_pg_orca_rule_rule_name - 1] = NameGetDatum(&rname);

	values[Anum_pg_orca_rule_rule_text - 1] = CStringGetTextDatum(stmt->rule_text);
	values[Anum_pg_orca_rule_is_enabled - 1] = BoolGetDatum(true);

	tup = heap_form_tuple(RelationGetDescr(rel), values, nulls);

	CatalogTupleInsert(rel, tup);
	heap_freetuple(tup);

	table_close(rel, AccessExclusiveLock);
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
	int32		ruleid;

	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be superuser to delete an ORCA rule")));

	if (stmt->rule_id <= 0 || stmt->rule_id > PG_INT32_MAX)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("rule id must be a positive 32-bit integer")));
	ruleid = (int32) stmt->rule_id;

	rel = table_open(OrcaRuleRelationId, RowExclusiveLock);

	tup = FindRuleByScan(rel, ruleid);
	if (tup == NULL)
	{
		table_close(rel, RowExclusiveLock);
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("rule with id %d does not exist", ruleid)));
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
	int32		ruleid;

	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be superuser to update an ORCA rule")));

	if (stmt->rule_id <= 0 || stmt->rule_id > PG_INT32_MAX)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("rule id must be a positive 32-bit integer")));
	ruleid = (int32) stmt->rule_id;

	if (stmt->rule_text == NULL || stmt->rule_text[0] == '\0')
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("rule text must not be empty")));

	rel = table_open(OrcaRuleRelationId, RowExclusiveLock);
	tupdesc = RelationGetDescr(rel);

	tup = FindRuleByScan(rel, ruleid);
	if (tup == NULL)
	{
		table_close(rel, RowExclusiveLock);
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("rule with id %d does not exist", ruleid)));
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