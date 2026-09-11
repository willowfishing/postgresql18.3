# Test concurrent rule ID allocation.

session s1
setup           { BEGIN; }
step s1insert    { INSERT RULE 'isolation_a' AS 'body a'; }
step s1commit    { COMMIT; }

session s2
step s2insert    { INSERT RULE 'isolation_b' AS 'body b'; }
step s2check     { SELECT count(*) = 2 AND count(DISTINCT id) = 2 AS distinct_ids FROM pg_orca_rule WHERE rule_name IN ('isolation_a', 'isolation_b'); }

permutation s1insert s2insert s1commit s2check
