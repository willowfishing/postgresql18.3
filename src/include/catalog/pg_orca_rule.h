#ifndef PG_ORCA_RULE_H
#define PG_ORCA_RULE_H

#include "catalog/genbki.h"
#include "catalog/pg_orca_rule_d.h"	/* IWYU pragma: export */

CATALOG(pg_orca_rule,9839,OrcaRuleRelationId)
{
	Oid			oid;			/* oid */

	/* rule id (primary key) */
	int64		id BKI_DEFAULT(0);

	/* rule name (unique) */
	NameData	rule_name;

	/* whether the rule is enabled */
	bool		is_enabled BKI_DEFAULT(t);

#ifdef CATALOG_VARLEN			/* variable-length fields start here */
	/* rule DSL text */
	text		rule_text BKI_FORCE_NOT_NULL;
#endif
} FormData_pg_orca_rule;

typedef FormData_pg_orca_rule *Form_pg_orca_rule;

DECLARE_TOAST(pg_orca_rule, 9835, 9836);

DECLARE_UNIQUE_INDEX_PKEY(pg_orca_rule_id_index, 9840, OrcaRuleIdIndexId, pg_orca_rule, btree(id int8_ops));
DECLARE_UNIQUE_INDEX(pg_orca_rule_name_index, 9837, OrcaRuleNameIndexId, pg_orca_rule, btree(rule_name name_ops));
DECLARE_UNIQUE_INDEX(pg_orca_rule_oid_index, 9838, OrcaRuleOidIndexId, pg_orca_rule, btree(oid oid_ops));

#endif							/* PG_ORCA_RULE_H */
