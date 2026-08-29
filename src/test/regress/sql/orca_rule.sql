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
