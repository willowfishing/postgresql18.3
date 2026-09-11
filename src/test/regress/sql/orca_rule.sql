--
-- INSERT RULE
--

-- Basic insertion and command tag.
INSERT RULE 'basic' AS 'rule body';
\echo :ROW_COUNT

SELECT rule_name, rule_text, is_enabled
FROM pg_orca_rule
WHERE rule_name = 'basic';

-- Names are unique.
INSERT RULE 'basic' AS 'duplicate';

-- Empty values and overlength names are rejected before catalog insertion.
INSERT RULE '' AS 'rule body';
INSERT RULE 'empty_body' AS '';

SELECT repeat('x', 64) AS long_rule_name \gset
INSERT RULE :'long_rule_name' AS 'rule body';

SELECT repeat('界', 21) AS max_multibyte_rule_name \gset
INSERT RULE :'max_multibyte_rule_name' AS 'rule body';

SELECT repeat('界', 22) AS long_multibyte_rule_name \gset
INSERT RULE :'long_multibyte_rule_name' AS 'rule body';

-- The command cannot modify the catalog in a read-only transaction.
BEGIN READ ONLY;
INSERT RULE 'read_only' AS 'rule body';
ROLLBACK;

-- Only superusers may insert rules.
CREATE ROLE regress_insert_rule_user;
SET ROLE regress_insert_rule_user;
INSERT RULE 'unprivileged' AS 'rule body';
RESET ROLE;
DROP ROLE regress_insert_rule_user;

-- WITH is not part of INSERT RULE and must not be silently ignored.
WITH ignored AS (SELECT 1 / 0)
INSERT RULE 'with_clause' AS 'rule body';

-- Rule text is TOAST-able.
SELECT string_agg(md5(g::text), '') AS long_rule_text
FROM generate_series(1, 400) AS g \gset

INSERT RULE 'long_body' AS :'long_rule_text';

SELECT octet_length(rule_text) AS rule_text_length
FROM pg_orca_rule
WHERE rule_name = 'long_body';

SELECT reltoastrelid <> 0 AS has_toast
FROM pg_class
WHERE oid = 'pg_orca_rule'::regclass;

-- INSERT RULE does not support or fire DDL event triggers.
CREATE TABLE insert_rule_event_log (tag text);

CREATE FUNCTION insert_rule_event_trigger()
RETURNS event_trigger
LANGUAGE plpgsql
AS $$
BEGIN
	INSERT INTO insert_rule_event_log VALUES (tg_tag);
END
$$;

CREATE EVENT TRIGGER insert_rule_event_trigger
	ON ddl_command_start
	EXECUTE FUNCTION insert_rule_event_trigger();

INSERT RULE 'event_trigger_safe' AS 'rule body';

ALTER EVENT TRIGGER insert_rule_event_trigger DISABLE;

SELECT count(*) AS fired_event_triggers
FROM insert_rule_event_log;

DROP EVENT TRIGGER insert_rule_event_trigger;
DROP FUNCTION insert_rule_event_trigger();
DROP TABLE insert_rule_event_log;

--
-- UPDATE RULE / DELETE RULE
--

-- Basic update + delete roundtrip, driven by the new id column.
INSERT RULE 'mut_r1' AS 'body v1';
SELECT id AS mut_r1_id FROM pg_orca_rule WHERE rule_name = 'mut_r1' \gset
UPDATE RULE :mut_r1_id AS 'body v2';

SELECT rule_name, rule_text, is_enabled
FROM pg_orca_rule
WHERE rule_name = 'mut_r1';

DELETE RULE :mut_r1_id;
SELECT count(*) AS remaining FROM pg_orca_rule WHERE rule_name = 'mut_r1';

-- The id column is the primary key.
SELECT indexrelid::regclass, indisprimary
FROM pg_index
WHERE indrelid = 'pg_orca_rule'::regclass
ORDER BY 1;

-- Deleting or updating a missing rule fails.
DELETE RULE 999999;
UPDATE RULE 999999 AS 'rule body';

-- Rule ids must be positive integers.
DELETE RULE 0;
UPDATE RULE 0 AS 'rule body';

-- Rule ids accept the full positive 32-bit range.
DELETE RULE 2147483648;
UPDATE RULE 2147483648 AS 'rule body';
DELETE RULE 4294967296;
UPDATE RULE 4294967296 AS 'rule body';

-- Empty update text is rejected.
INSERT RULE 'mut_sample' AS 'body';
SELECT id AS mut_sample_id FROM pg_orca_rule WHERE rule_name = 'mut_sample' \gset
SELECT :mut_sample_id <> :mut_r1_id AS id_not_reused;
UPDATE RULE :mut_sample_id AS '';

-- Cannot modify the catalog in a read-only transaction.
BEGIN READ ONLY;
UPDATE RULE :mut_sample_id AS 'body v2';
ROLLBACK;

BEGIN READ ONLY;
DELETE RULE :mut_sample_id;
ROLLBACK;

-- Only superusers may update / delete rules.
CREATE ROLE regress_mut_rule_user;
SET ROLE regress_mut_rule_user;
UPDATE RULE :mut_sample_id AS 'body v2';
DELETE RULE :mut_sample_id;
RESET ROLE;
DROP ROLE regress_mut_rule_user;

-- DELETE RULE/UPDATE RULE must not fire DDL event triggers.
CREATE TABLE mutate_rule_event_log (tag text);
CREATE FUNCTION mutate_rule_event_trigger()
RETURNS event_trigger
LANGUAGE plpgsql
AS $$
BEGIN
	INSERT INTO mutate_rule_event_log VALUES (tg_tag);
END
$$;

CREATE EVENT TRIGGER mutate_rule_event_trigger
	ON ddl_command_start
	EXECUTE FUNCTION mutate_rule_event_trigger();

UPDATE RULE :mut_sample_id AS 'body v3';
DELETE RULE :mut_sample_id;

ALTER EVENT TRIGGER mutate_rule_event_trigger DISABLE;

SELECT count(*) AS fired_event_no FROM mutate_rule_event_log;

DROP EVENT TRIGGER mutate_rule_event_trigger;
DROP FUNCTION mutate_rule_event_trigger();
DROP TABLE mutate_rule_event_log;

--
-- Extra coverage: auto id, rowcount, WITH rejection, NULL checks
--

-- Multiple new rules get distinct OID-backed ids.
INSERT RULE 'extra_a' AS 'body a';
INSERT RULE 'extra_b' AS 'body b';
INSERT RULE 'extra_c' AS 'body c';

SELECT count(*) AS rule_count,
	count(DISTINCT id) AS distinct_ids,
	bool_and(id = oid::bigint) AS ids_match_oids
FROM pg_orca_rule
WHERE rule_name IN ('extra_a', 'extra_b', 'extra_c');

-- The id column is never null after INSERT.
SELECT count(*) AS id_not_null
FROM pg_orca_rule
WHERE id IS NOT NULL;

SELECT count(*) AS id_is_null
FROM pg_orca_rule
WHERE id IS NULL;

-- WITH must be rejected for DELETE/UPDATE RULE too.
WITH ignored AS (SELECT 1 / 0)
DELETE RULE 999999;

WITH ignored AS (SELECT 1 / 0)
UPDATE RULE 999999 AS 'body';

-- UPDATE RULE reports row count and preserves other columns.
INSERT RULE 'auto_cnt' AS 'body v1';
SELECT id AS auto_cnt_id FROM pg_orca_rule WHERE rule_name = 'auto_cnt' \gset
UPDATE RULE :auto_cnt_id AS 'body v2';
\echo :ROW_COUNT
SELECT rule_name, is_enabled, rule_text
FROM pg_orca_rule
WHERE id = :auto_cnt_id;

-- DELETE RULE reports row count.
DELETE RULE :auto_cnt_id;
\echo :ROW_COUNT
SELECT count(*) AS cnt_from FROM pg_orca_rule WHERE id = :auto_cnt_id;
\unset auto_cnt_id
