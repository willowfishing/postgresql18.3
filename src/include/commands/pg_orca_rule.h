#ifndef PG_ORCA_RULE_H_CMDS
#define PG_ORCA_RULE_H_CMDS

#include "nodes/parsenodes.h"
#include "catalog/objectaddress.h"

typedef struct ParseState ParseState;

extern ObjectAddress ExecInsertRuleStmt(ParseState *pstate,
										InsertRuleStmt *stmt);

#endif							/* PG_ORCA_RULE_H_CMDS */
