--
-- This file is to check the effect of DDL statements on the catalog version
-- stored in pg_yb_catalog_table. In particular, not all DDL query causes
-- changes in catalog. For example, a grant statement may not really change
-- anything if the same privileges are already granted. In such cases it is
-- benefitial to avoid catalog version increment to prevent catalog cache
-- update which may overload yb-master if there are a large number of active
-- Postgres connections.
--
--
\set template1_db_oid 1
-- Build a SQL query that works with per-database catalog version mode enabled
-- or disabled.
\set display_catalog_version 'SELECT current_version, last_breaking_version FROM pg_yb_catalog_version WHERE db_oid = CASE WHEN (select count(*) from pg_yb_catalog_version) = 1 THEN :template1_db_oid ELSE (SELECT oid FROM pg_database WHERE datname = \'yugabyte\') END'

-- Display the initial catalog version.
:display_catalog_version;

-- Tables with various constraint types should not increment catalog version.
CREATE TABLE t_check (col INT CHECK (col > 0));
:display_catalog_version;
CREATE TABLE t_not_null (col INT NOT NULL);
:display_catalog_version;
CREATE TABLE t_primary_key (col INT PRIMARY KEY);
:display_catalog_version;
CREATE TABLE t_sequence (col SERIAL, value TEXT);
:display_catalog_version;
CREATE TABLE t_unique (col INT UNIQUE);
:display_catalog_version;
CREATE TABLE t_identity (col INT GENERATED ALWAYS AS IDENTITY);
:display_catalog_version;
CREATE TABLE t_primary_key_sequence_identity (c1 INT PRIMARY KEY, c2 SERIAL, c3 INT GENERATED ALWAYS AS IDENTITY);
:display_catalog_version;

-- The CREATE TABLE with FOREIGN KEY will increment current_version.
CREATE TABLE t1 (col INT UNIQUE);
CREATE TABLE t2 (col INT REFERENCES t1(col));
:display_catalog_version;

-- The ALTER TABLE will increment current_version.
ALTER TABLE t1 ADD COLUMN val INT;
:display_catalog_version;

-- The CREATE PROCEDURE will not increment current_version.
CREATE PROCEDURE test() AS $$ BEGIN INSERT INTO t1 VALUES(1); END; $$ LANGUAGE 'plpgsql';
:display_catalog_version;
-- The CALL to PROCEDURE will not increment current_version.
CALL test();
:display_catalog_version;

-- The CREATE FUNCTION will increment current_version.
CREATE TABLE evt_trig_table (id serial PRIMARY KEY, evt_trig_name text);
CREATE OR REPLACE FUNCTION evt_trig_fn() RETURNS event_trigger AS $$ BEGIN INSERT INTO evt_trig_table (evt_trig_name) VALUES (TG_EVENT); END; $$ LANGUAGE plpgsql;
:display_catalog_version;
-- The CREATE EVENT TRIGGER will increment current_version.
CREATE EVENT TRIGGER evt_ddl_start ON ddl_command_start EXECUTE PROCEDURE evt_trig_fn();
:display_catalog_version;
-- The DDLs proceeding the trigger will increment current_version based on the command's individual behaviour.
-- The ALTER TABLE will increment current_version.
ALTER TABLE evt_trig_table ADD COLUMN val INT;
:display_catalog_version;
-- The CREATE TABLE will not increment current_version.
CREATE TABLE post_trigger_table (id INT);
:display_catalog_version;

-- The execution on atomic SPI context function will increment current_version.
CREATE FUNCTION atomic_spi(INT) RETURNS INT AS $$
DECLARE TOTAL INT;
BEGIN
  CREATE TEMP TABLE temp_table(a INT);
  INSERT INTO temp_table VALUES($1);
  INSERT INTO temp_table VALUES(1);
  SELECT SUM(a) INTO TOTAL FROM temp_table;
  DROP TABLE temp_table;
  RETURN TOTAL;
END
$$ LANGUAGE PLPGSQL;

SELECT atomic_spi(1);
:display_catalog_version;
SELECT atomic_spi(2);
:display_catalog_version;

-- The execution on non-atomic SPI context will not increment current_version.
CREATE TABLE non_atomic_spi(a INT, b INT);
CREATE PROCEDURE proc(x INT, y INT) LANGUAGE PLPGSQL AS $$
BEGIN FOR i IN 0..x LOOP INSERT INTO non_atomic_spi(a, b) VALUES (i, y);
IF i % 2 = 0 THEN COMMIT;
ELSE ROLLBACK;
END IF;
END LOOP;
END $$;
CREATE PROCEDURE p1() LANGUAGE PLPGSQL AS $$ BEGIN CALL p2(); END $$;
CREATE PROCEDURE p2() LANGUAGE PLPGSQL AS $$ BEGIN CALL proc(2, 2); END $$;

CALL p1();
:display_catalog_version;