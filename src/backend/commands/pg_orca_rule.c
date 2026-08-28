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

#include "access/htup_details.h"
#include "access/xact.h"
#include "access/table.h"
#include "catalog/indexing.h"
#include "catalog/pg_orca_rule.h"
#include "commands/pg_orca_rule.h"
#include "miscadmin.h"
#include "parser/parse_node.h"
#include "utils/builtins.h"
#include "utils/rel.h"

/*
 * ExecInsertRuleStmt
 *		INSERT RULE 'rule_name' AS 'rule_text'
 *
 *		Superuser-only; the rule text is stored verbatim (syntax
 *		validation is delegated to the ORCA extension via a callback).
 */
ObjectAddress
ExecInsertRuleStmt(ParseState *pstate, InsertRuleStmt *stmt)
{
	Relation	rel;
	Oid			ruleoid;
	HeapTuple	tup;
	Datum		values[Natts_pg_orca_rule];
	bool		nulls[Natts_pg_orca_rule];
	NameData	rname;
	ObjectAddress myself;

	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be superuser to create an ORCA rule")));

	/* basic sanity: a non-empty name and non-empty text */
	if (stmt->rule_name == NULL || stmt->rule_name[0] == '\0')
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("rule name must not be empty")));
	if (stmt->rule_text == NULL || stmt->rule_text[0] == '\0')
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("rule text must not be empty")));

	rel = table_open(OrcaRuleRelationId, RowExclusiveLock);

	memset(values, 0, sizeof(values));
	memset(nulls, false, sizeof(nulls));

	ruleoid = GetNewOidWithIndex(rel, OrcaRuleOidIndexId, Anum_pg_orca_rule_oid);
	values[Anum_pg_orca_rule_oid - 1] = ObjectIdGetDatum(ruleoid);

	namestrcpy(&rname, stmt->rule_name);
	values[Anum_pg_orca_rule_rule_name - 1] = NameGetDatum(&rname);

	values[Anum_pg_orca_rule_rule_text - 1] = CStringGetTextDatum(stmt->rule_text);
	values[Anum_pg_orca_rule_is_enabled - 1] = BoolGetDatum(true);

	tup = heap_form_tuple(RelationGetDescr(rel), values, nulls);

	CatalogTupleInsert(rel, tup);
	heap_freetuple(tup);

	CommandCounterIncrement();

	table_close(rel, RowExclusiveLock);

	ObjectAddressSet(myself, OrcaRuleRelationId, ruleoid);
	return myself;
}