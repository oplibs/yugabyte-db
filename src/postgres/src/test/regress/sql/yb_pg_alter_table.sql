--
-- ALTER_TABLE
--

-- Clean up in case a prior regression run failed
SET client_min_messages TO 'warning';
DROP ROLE IF EXISTS regress_alter_table_user1;
RESET client_min_messages;

CREATE USER regress_alter_table_user1;

--
-- add attribute
--

CREATE TABLE attmp (initial int4);

COMMENT ON TABLE attmp_wrong IS 'table comment';
COMMENT ON TABLE attmp IS 'table comment';
COMMENT ON TABLE attmp IS NULL;

ALTER TABLE attmp ADD COLUMN xmin integer; -- fails

ALTER TABLE attmp ADD COLUMN a int4 default 3;

ALTER TABLE attmp ADD COLUMN b name;

ALTER TABLE attmp ADD COLUMN c text;

ALTER TABLE attmp ADD COLUMN d float8;

ALTER TABLE attmp ADD COLUMN e float4;

ALTER TABLE attmp ADD COLUMN f int2;

ALTER TABLE attmp ADD COLUMN g polygon;

ALTER TABLE attmp ADD COLUMN h abstime;

ALTER TABLE attmp ADD COLUMN i char;

ALTER TABLE attmp ADD COLUMN j abstime[];

ALTER TABLE attmp ADD COLUMN k int4;

ALTER TABLE attmp ADD COLUMN l tid;

ALTER TABLE attmp ADD COLUMN m xid;

ALTER TABLE attmp ADD COLUMN n oidvector;

--ALTER TABLE attmp ADD COLUMN o lock;
ALTER TABLE attmp ADD COLUMN p smgr;

ALTER TABLE attmp ADD COLUMN q point;

ALTER TABLE attmp ADD COLUMN r lseg;

ALTER TABLE attmp ADD COLUMN s path;

ALTER TABLE attmp ADD COLUMN t box;

ALTER TABLE attmp ADD COLUMN u tinterval;

ALTER TABLE attmp ADD COLUMN v timestamp;

ALTER TABLE attmp ADD COLUMN w interval;

ALTER TABLE attmp ADD COLUMN x float8[];

ALTER TABLE attmp ADD COLUMN y float4[];

ALTER TABLE attmp ADD COLUMN z int2[];

-- This is the original query. This should be uncommented after we support abstime type (col h) and
-- tinterval type (col u)
-- INSERT INTO attmp (a, b, c, d, e, f, g, h, i, j, k, l, m, n, p, q, r, s, t, u,
-- 	v, w, x, y, z)
--    VALUES (4, 'name', 'text', 4.1, 4.1, 2, '(4.1,4.1,3.1,3.1)',
--         'Mon May  1 00:30:30 1995', 'c', '{Mon May  1 00:30:30 1995, Monday Aug 24 14:43:07 1992, epoch}',
-- 	314159, '(1,1)', '512',
-- 	'1 2 3 4 5 6 7 8', 'magnetic disk', '(1.1,1.1)', '(4.1,4.1,3.1,3.1)',
-- 	'(0,2,4.1,4.1,3.1,3.1)', '(4.1,4.1,3.1,3.1)', '["epoch" "infinity"]',
-- 	'epoch', '01:00:10', '{1.0,2.0,3.0,4.0}', '{1.0,2.0,3.0,4.0}', '{1,2,3,4}');

INSERT INTO attmp (a, b, c, d, e, f, g, i, j, k, l, m, n, p, q, r, s, t,
	v, w, x, y, z)
   VALUES (4, 'name', 'text', 4.1, 4.1, 2, '(4.1,4.1,3.1,3.1)',
        'c', '{Mon May  1 00:30:30 1995, Monday Aug 24 14:43:07 1992, epoch}',
	314159, '(1,1)', '512',
	'1 2 3 4 5 6 7 8', 'magnetic disk', '(1.1,1.1)', '(4.1,4.1,3.1,3.1)',
	'(0,2,4.1,4.1,3.1,3.1)', '(4.1,4.1,3.1,3.1)',
	'epoch', '01:00:10', '{1.0,2.0,3.0,4.0}', '{1.0,2.0,3.0,4.0}', '{1,2,3,4}');
SELECT * FROM attmp;

DROP TABLE attmp;

-- the wolf bug - schema mods caused inconsistent row descriptors
CREATE TABLE attmp (
	initial 	int4
);

ALTER TABLE attmp ADD COLUMN a int4;

ALTER TABLE attmp ADD COLUMN b name;

ALTER TABLE attmp ADD COLUMN c text;

ALTER TABLE attmp ADD COLUMN d float8;

ALTER TABLE attmp ADD COLUMN e float4;

ALTER TABLE attmp ADD COLUMN f int2;

ALTER TABLE attmp ADD COLUMN g polygon;

ALTER TABLE attmp ADD COLUMN h abstime;

ALTER TABLE attmp ADD COLUMN i char;

ALTER TABLE attmp ADD COLUMN j abstime[];

ALTER TABLE attmp ADD COLUMN k int4;

ALTER TABLE attmp ADD COLUMN l tid;

ALTER TABLE attmp ADD COLUMN m xid;

ALTER TABLE attmp ADD COLUMN n oidvector;

--ALTER TABLE attmp ADD COLUMN o lock;
ALTER TABLE attmp ADD COLUMN p smgr;

ALTER TABLE attmp ADD COLUMN q point;

ALTER TABLE attmp ADD COLUMN r lseg;

ALTER TABLE attmp ADD COLUMN s path;

ALTER TABLE attmp ADD COLUMN t box;

ALTER TABLE attmp ADD COLUMN u tinterval;

ALTER TABLE attmp ADD COLUMN v timestamp;

ALTER TABLE attmp ADD COLUMN w interval;

ALTER TABLE attmp ADD COLUMN x float8[];

ALTER TABLE attmp ADD COLUMN y float4[];

ALTER TABLE attmp ADD COLUMN z int2[];

-- This is the original query. This should be uncommented after we support abstime type (col h) and
-- tinterval type (col u)
-- INSERT INTO attmp (a, b, c, d, e, f, g, h, i, j, k, l, m, n, p, q, r, s, t, u,
-- 	v, w, x, y, z)
--    VALUES (4, 'name', 'text', 4.1, 4.1, 2, '(4.1,4.1,3.1,3.1)',
--         'Mon May  1 00:30:30 1995', 'c', '{Mon May  1 00:30:30 1995, Monday Aug 24 14:43:07 1992, epoch}',
-- 	314159, '(1,1)', '512',
-- 	'1 2 3 4 5 6 7 8', 'magnetic disk', '(1.1,1.1)', '(4.1,4.1,3.1,3.1)',
-- 	'(0,2,4.1,4.1,3.1,3.1)', '(4.1,4.1,3.1,3.1)', '["epoch" "infinity"]',
-- 	'epoch', '01:00:10', '{1.0,2.0,3.0,4.0}', '{1.0,2.0,3.0,4.0}', '{1,2,3,4}');

INSERT INTO attmp (a, b, c, d, e, f, g, i, j, k, l, m, n, p, q, r, s, t,
	v, w, x, y, z)
   VALUES (4, 'name', 'text', 4.1, 4.1, 2, '(4.1,4.1,3.1,3.1)',
        'c', '{Mon May  1 00:30:30 1995, Monday Aug 24 14:43:07 1992, epoch}',
	314159, '(1,1)', '512',
	'1 2 3 4 5 6 7 8', 'magnetic disk', '(1.1,1.1)', '(4.1,4.1,3.1,3.1)',
	'(0,2,4.1,4.1,3.1,3.1)', '(4.1,4.1,3.1,3.1)',
	'epoch', '01:00:10', '{1.0,2.0,3.0,4.0}', '{1.0,2.0,3.0,4.0}', '{1,2,3,4}');

SELECT * FROM attmp;

CREATE INDEX attmp_idx ON attmp (a, (d + e), b);

ALTER INDEX attmp_idx ALTER COLUMN 0 SET STATISTICS 1000;

ALTER INDEX attmp_idx ALTER COLUMN 1 SET STATISTICS 1000;

ALTER INDEX attmp_idx ALTER COLUMN 2 SET STATISTICS 1000;

\d+ attmp_idx

ALTER INDEX attmp_idx ALTER COLUMN 3 SET STATISTICS 1000;

ALTER INDEX attmp_idx ALTER COLUMN 4 SET STATISTICS 1000;

ALTER INDEX attmp_idx ALTER COLUMN 2 SET STATISTICS -1;

DROP TABLE attmp;

--
-- rename - check on both non-temp and temp tables
--
CREATE TABLE attmp (regtable int);
CREATE TEMP TABLE attmp (attmptable int);

ALTER TABLE attmp RENAME TO attmp_new;

SELECT * FROM attmp;
SELECT * FROM attmp_new;

ALTER TABLE attmp RENAME TO attmp_new2;

SELECT * FROM attmp;		-- should fail
SELECT * FROM attmp_new;
SELECT * FROM attmp_new2;

DROP TABLE attmp_new;
DROP TABLE attmp_new2;

-- check rename of partitioned tables and indexes also
CREATE TABLE part_attmp (a int primary key) partition by range (a);
CREATE TABLE part_attmp1 PARTITION OF part_attmp FOR VALUES FROM (0) TO (100);
ALTER INDEX part_attmp_pkey RENAME TO part_attmp_index;
ALTER INDEX part_attmp1_pkey RENAME TO part_attmp1_index;
ALTER TABLE part_attmp RENAME TO part_at2tmp;
ALTER TABLE part_attmp1 RENAME TO part_at2tmp1;
SET ROLE regress_alter_table_user1;
ALTER INDEX part_attmp_index RENAME TO fail;
ALTER INDEX part_attmp1_index RENAME TO fail;
ALTER TABLE part_at2tmp RENAME TO fail;
ALTER TABLE part_at2tmp1 RENAME TO fail;
RESET ROLE;
DROP TABLE part_at2tmp;

-- ALTER TABLE ... RENAME on non-table relations
-- renaming indexes (FIXME: this should probably test the index's functionality)
ALTER INDEX IF EXISTS __onek_unique1 RENAME TO attmp_onek_unique1;
ALTER INDEX IF EXISTS __attmp_onek_unique1 RENAME TO onek_unique1;

ALTER INDEX onek_unique1 RENAME TO attmp_onek_unique1;
ALTER INDEX attmp_onek_unique1 RENAME TO onek_unique1;

SET ROLE regress_alter_table_user1;
ALTER INDEX onek_unique1 RENAME TO fail;  -- permission denied
RESET ROLE;

-- renaming index should rename constraint as well
ALTER TABLE onek ADD CONSTRAINT onek_unique1_constraint UNIQUE (unique1);
ALTER INDEX onek_unique1_constraint RENAME TO onek_unique1_constraint_foo;
ALTER TABLE onek DROP CONSTRAINT onek_unique1_constraint_foo;

-- renaming constraint
ALTER TABLE onek ADD CONSTRAINT onek_check_constraint CHECK (unique1 >= 0);
ALTER TABLE onek RENAME CONSTRAINT onek_check_constraint TO onek_check_constraint_foo;

ALTER TABLE onek DROP CONSTRAINT onek_check_constraint_foo;

-- renaming constraint should rename index as well
ALTER TABLE onek ADD CONSTRAINT onek_unique1_constraint UNIQUE (unique1);
DROP INDEX onek_unique1_constraint;  -- to see whether it's there
ALTER TABLE onek RENAME CONSTRAINT onek_unique1_constraint TO onek_unique1_constraint_foo;
DROP INDEX onek_unique1_constraint_foo;  -- to see whether it's there
ALTER TABLE onek DROP CONSTRAINT onek_unique1_constraint_foo;

-- renaming constraints vs. inheritance
CREATE TABLE constraint_rename_test (a int CONSTRAINT con1 CHECK (a > 0), b int, c int);
\d constraint_rename_test
CREATE TABLE constraint_rename_test2 (a int CONSTRAINT con1 CHECK (a > 0), d int) INHERITS (constraint_rename_test);
/*
TODO: Uncomment when inheritance is supported (https://github.com/yugabyte/yugabyte-db/issues/1129).
\d constraint_rename_test2
ALTER TABLE constraint_rename_test2 RENAME CONSTRAINT con1 TO con1foo; -- fail
ALTER TABLE ONLY constraint_rename_test RENAME CONSTRAINT con1 TO con1foo; -- fail
ALTER TABLE constraint_rename_test RENAME CONSTRAINT con1 TO con1foo; -- ok
\d constraint_rename_test
\d constraint_rename_test2
ALTER TABLE constraint_rename_test ADD CONSTRAINT con2 CHECK (b > 0) NO INHERIT;
ALTER TABLE ONLY constraint_rename_test RENAME CONSTRAINT con2 TO con2foo; -- ok
ALTER TABLE constraint_rename_test RENAME CONSTRAINT con2foo TO con2bar; -- ok
\d constraint_rename_test
\d constraint_rename_test2
ALTER TABLE constraint_rename_test ADD CONSTRAINT con3 PRIMARY KEY (a);
ALTER TABLE constraint_rename_test RENAME CONSTRAINT con3 TO con3foo; -- ok
\d constraint_rename_test
\d constraint_rename_test2
DROP TABLE constraint_rename_test2;
DROP TABLE constraint_rename_test;
ALTER TABLE IF EXISTS constraint_not_exist RENAME CONSTRAINT con3 TO con3foo; -- ok
ALTER TABLE IF EXISTS constraint_rename_test ADD CONSTRAINT con4 UNIQUE (a);
*/

-- renaming constraints with cache reset of target relation
CREATE TABLE constraint_rename_cache (a int,
  CONSTRAINT chk_a CHECK (a > 0),
  PRIMARY KEY (a));
ALTER TABLE constraint_rename_cache
  RENAME CONSTRAINT chk_a TO chk_a_new;
ALTER TABLE constraint_rename_cache
  RENAME CONSTRAINT constraint_rename_cache_pkey TO constraint_rename_pkey_new;
CREATE TABLE like_constraint_rename_cache
  (LIKE constraint_rename_cache INCLUDING ALL);
\d like_constraint_rename_cache
DROP TABLE constraint_rename_cache;
DROP TABLE like_constraint_rename_cache;

--
-- lock levels
--
drop type lockmodes;
create type lockmodes as enum (
 'SIReadLock'
,'AccessShareLock'
,'RowShareLock'
,'RowExclusiveLock'
,'ShareUpdateExclusiveLock'
,'ShareLock'
,'ShareRowExclusiveLock'
,'ExclusiveLock'
,'AccessExclusiveLock'
);

drop view my_locks;
create or replace view my_locks as
select case when c.relname like 'pg_toast%' then 'pg_toast' else c.relname end, max(mode::lockmodes) as max_lockmode
from pg_locks l join pg_class c on l.relation = c.oid
where virtualtransaction = (
        select virtualtransaction
        from pg_locks)
and locktype = 'relation'
and relnamespace != (select oid from pg_namespace where nspname = 'pg_catalog')
and c.relname != 'my_locks'
group by c.relname;

create table alterlock (f1 int primary key, f2 text);
insert into alterlock values (1, 'foo');
create table alterlock2 (f3 int primary key, f1 int);
insert into alterlock2 values (1, 1);

begin; alter table alterlock alter column f2 set statistics 150;
select * from my_locks order by 1;
rollback;

begin; alter table alterlock cluster on alterlock_pkey;
select * from my_locks order by 1;
commit;

begin; alter table alterlock set without cluster;
select * from my_locks order by 1;
commit;

begin; alter table alterlock set (fillfactor = 100);
select * from my_locks order by 1;
commit;

begin; alter table alterlock reset (fillfactor);
select * from my_locks order by 1;
commit;

begin; alter table alterlock set (toast.autovacuum_enabled = off);
select * from my_locks order by 1;
commit;

begin; alter table alterlock set (autovacuum_enabled = off);
select * from my_locks order by 1;
commit;

begin; alter table alterlock alter column f2 set (n_distinct = 1);
select * from my_locks order by 1;
rollback;

-- test that mixing options with different lock levels works as expected
begin; alter table alterlock set (autovacuum_enabled = off, fillfactor = 80);
select * from my_locks order by 1;
commit;

begin; alter table alterlock alter column f2 set storage extended;
select * from my_locks order by 1;
rollback;

-- TODO(jason): uncomment when doing issue #9106
-- begin; alter table alterlock alter column f2 set default 'x';
-- select * from my_locks order by 1;
-- rollback;

--
-- alter object set schema
--

create schema alter1;
create schema alter2;

create text search parser alter1.prs(start = prsd_start, gettoken = prsd_nexttoken, end = prsd_end, lextypes = prsd_lextype);
create text search configuration alter1.cfg(parser = alter1.prs);

alter text search parser alter1.prs set schema alter2;
alter text search configuration alter1.cfg set schema alter2;

-- clean up
drop schema alter2 cascade;

--
-- typed tables: OF / NOT OF
--

CREATE TYPE tt_t0 AS (z inet, x int, y numeric(8,2));
ALTER TYPE tt_t0 DROP ATTRIBUTE z;
CREATE TABLE tt0 (x int NOT NULL, y numeric(8,2));	-- OK
CREATE TABLE tt1 (x int, y bigint);					-- wrong base type
CREATE TABLE tt2 (x int, y numeric(9,2));			-- wrong typmod
CREATE TABLE tt3 (y numeric(8,2), x int);			-- wrong column order
CREATE TABLE tt4 (x int);							-- too few columns
CREATE TABLE tt5 (x int, y numeric(8,2), z int);	-- too few columns
CREATE TABLE tt6 () INHERITS (tt0);					-- can't have a parent
CREATE TABLE tt7 (x int, q text, y numeric(8,2)) WITH OIDS;
ALTER TABLE tt7 DROP q;								-- OK

ALTER TABLE tt0 OF tt_t0;
ALTER TABLE tt1 OF tt_t0;
ALTER TABLE tt2 OF tt_t0;
ALTER TABLE tt3 OF tt_t0;
ALTER TABLE tt4 OF tt_t0;
ALTER TABLE tt5 OF tt_t0;
ALTER TABLE tt6 OF tt_t0;
ALTER TABLE tt7 OF tt_t0;

CREATE TYPE tt_t1 AS (x int, y numeric(8,2));
ALTER TABLE tt7 OF tt_t1;			-- reassign an already-typed table
ALTER TABLE tt7 NOT OF;
\d tt7

--
-- ATTACH PARTITION
--

-- check that target table is partitioned
CREATE TABLE unparted (
	a int
);
CREATE TABLE fail_part (like unparted);
ALTER TABLE unparted ATTACH PARTITION fail_part FOR VALUES IN ('a');
DROP TABLE unparted, fail_part;

-- check that partition bound is compatible
CREATE TABLE list_parted (
	a int NOT NULL,
	b char(2) COLLATE "C",
	CONSTRAINT check_a CHECK (a > 0)
) PARTITION BY LIST (a);
CREATE TABLE fail_part (LIKE list_parted);
ALTER TABLE list_parted ATTACH PARTITION fail_part FOR VALUES FROM (1) TO (10);
DROP TABLE fail_part;

-- check that the table being attached exists
ALTER TABLE list_parted ATTACH PARTITION nonexistant FOR VALUES IN (1);

-- check ownership of the source table
CREATE ROLE regress_test_me;
CREATE ROLE regress_test_not_me;
CREATE TABLE not_owned_by_me (LIKE list_parted);
ALTER TABLE not_owned_by_me OWNER TO regress_test_not_me;
SET SESSION AUTHORIZATION regress_test_me;
CREATE TABLE owned_by_me (
	a int
) PARTITION BY LIST (a);
ALTER TABLE owned_by_me ATTACH PARTITION not_owned_by_me FOR VALUES IN (1);
RESET SESSION AUTHORIZATION;
DROP TABLE owned_by_me, not_owned_by_me;
DROP ROLE regress_test_not_me;
DROP ROLE regress_test_me;

-- check that the table being attached is not part of regular inheritance
CREATE TABLE parent (LIKE list_parted);
CREATE TABLE child () INHERITS (parent);
-- Uncomment the following when INHERITS is supported #5956.
/*
ALTER TABLE list_parted ATTACH PARTITION child FOR VALUES IN (1);
ALTER TABLE list_parted ATTACH PARTITION parent FOR VALUES IN (1);
*/
DROP TABLE parent CASCADE;

-- check any TEMP-ness
CREATE TEMP TABLE temp_parted (a int) PARTITION BY LIST (a);
CREATE TABLE perm_part (a int);
ALTER TABLE temp_parted ATTACH PARTITION perm_part FOR VALUES IN (1);
DROP TABLE temp_parted, perm_part;

-- check that the table being attached is not a typed table
CREATE TYPE mytype AS (a int);
CREATE TABLE fail_part OF mytype;
ALTER TABLE list_parted ATTACH PARTITION fail_part FOR VALUES IN (1);
DROP TYPE mytype CASCADE;

-- check existence (or non-existence) of oid column
ALTER TABLE list_parted SET WITH OIDS;
/*
TODO: Uncomment if ALTER TABLE SET WITH OIDS (#1124) is supported.
CREATE TABLE fail_part (a int);
ALTER TABLE list_parted ATTACH PARTITION fail_part FOR VALUES IN (1);
ALTER TABLE list_parted SET WITHOUT OIDS;
ALTER TABLE fail_part SET WITH OIDS;
ALTER TABLE list_parted ATTACH PARTITION fail_part FOR VALUES IN (1);
DROP TABLE fail_part;
*/

-- check that the table being attached has only columns present in the parent
CREATE TABLE fail_part (like list_parted, c int);
ALTER TABLE list_parted ATTACH PARTITION fail_part FOR VALUES IN (1);
DROP TABLE fail_part;

-- check that the table being attached has every column of the parent
CREATE TABLE fail_part (a int NOT NULL);
ALTER TABLE list_parted ATTACH PARTITION fail_part FOR VALUES IN (1);
DROP TABLE fail_part;

-- check that columns match in type, collation and NOT NULL status
CREATE TABLE fail_part (
 	b char(3),
 	a int NOT NULL
);
ALTER TABLE list_parted ATTACH PARTITION fail_part FOR VALUES IN (1);
ALTER TABLE fail_part ALTER b TYPE char (2) COLLATE "POSIX";
-- Uncomment when ALTER TABLE COLLATE is supported (#1013)
-- ALTER TABLE list_parted ATTACH PARTITION fail_part FOR VALUES IN (1);
DROP TABLE fail_part;

-- check that the table being attached has all constraints of the parent
CREATE TABLE fail_part (
	b char(2) COLLATE "C",
	a int NOT NULL
);
ALTER TABLE list_parted ATTACH PARTITION fail_part FOR VALUES IN (1);

-- check that the constraint matches in definition with parent's constraint
ALTER TABLE fail_part ADD CONSTRAINT check_a CHECK (a >= 0);
ALTER TABLE list_parted ATTACH PARTITION fail_part FOR VALUES IN (1);
DROP TABLE fail_part;

-- check the attributes and constraints after partition is attached
CREATE TABLE part_1 (
	a int NOT NULL,
	b char(2) COLLATE "C",
	CONSTRAINT check_a CHECK (a > 0)
);
ALTER TABLE list_parted ATTACH PARTITION part_1 FOR VALUES IN (1);
-- attislocal and conislocal are always false for merged attributes and constraints respectively.
SELECT attislocal, attinhcount FROM pg_attribute WHERE attrelid = 'part_1'::regclass AND attnum > 0;
SELECT conislocal, coninhcount FROM pg_constraint WHERE conrelid = 'part_1'::regclass AND conname = 'check_a';

-- check that the new partition won't overlap with an existing partition
CREATE TABLE fail_part (LIKE part_1 INCLUDING CONSTRAINTS);
ALTER TABLE list_parted ATTACH PARTITION fail_part FOR VALUES IN (1);
DROP TABLE fail_part;
-- check that an existing table can be attached as a default partition
CREATE TABLE def_part (LIKE list_parted INCLUDING CONSTRAINTS);
ALTER TABLE list_parted ATTACH PARTITION def_part DEFAULT;
-- check attaching default partition fails if a default partition already
-- exists
CREATE TABLE fail_def_part (LIKE part_1 INCLUDING CONSTRAINTS);
ALTER TABLE list_parted ATTACH PARTITION fail_def_part DEFAULT;

-- check validation when attaching list partitions
CREATE TABLE list_parted2 (
	a int,
	b char
) PARTITION BY LIST (a);

-- check that violating rows are correctly reported
CREATE TABLE part_2 (LIKE list_parted2);
INSERT INTO part_2 VALUES (3, 'a');
ALTER TABLE list_parted2 ATTACH PARTITION part_2 FOR VALUES IN (2);

-- should be ok after deleting the bad row
DELETE FROM part_2;
ALTER TABLE list_parted2 ATTACH PARTITION part_2 FOR VALUES IN (2);

-- check partition cannot be attached if default has some row for its values
CREATE TABLE list_parted2_def PARTITION OF list_parted2 DEFAULT;
INSERT INTO list_parted2_def VALUES (11, 'z');
CREATE TABLE part_3 (LIKE list_parted2);
ALTER TABLE list_parted2 ATTACH PARTITION part_3 FOR VALUES IN (11);
-- should be ok after deleting the bad row
DELETE FROM list_parted2_def WHERE a = 11;
ALTER TABLE list_parted2 ATTACH PARTITION part_3 FOR VALUES IN (11);

-- adding constraints that describe the desired partition constraint
-- (or more restrictive) will help skip the validation scan
CREATE TABLE part_3_4 (
	LIKE list_parted2,
	CONSTRAINT check_a CHECK (a IN (3))
);

-- however, if a list partition does not accept nulls, there should be
-- an explicit NOT NULL constraint on the partition key column for the
-- validation scan to be skipped;
ALTER TABLE list_parted2 ATTACH PARTITION part_3_4 FOR VALUES IN (3, 4);

-- adding a NOT NULL constraint will cause the scan to be skipped
ALTER TABLE list_parted2 DETACH PARTITION part_3_4;
ALTER TABLE part_3_4 ALTER a SET NOT NULL;
ALTER TABLE list_parted2 ATTACH PARTITION part_3_4 FOR VALUES IN (3, 4);

-- check if default partition scan skipped
ALTER TABLE list_parted2_def ADD CONSTRAINT check_a CHECK (a IN (5, 6));
CREATE TABLE part_55_66 PARTITION OF list_parted2 FOR VALUES IN (55, 66);

-- check validation when attaching range partitions
CREATE TABLE range_parted (
	a int,
	b int
) PARTITION BY RANGE (a, b);

-- check that violating rows are correctly reported
CREATE TABLE part1 (
	a int NOT NULL CHECK (a = 1),
	b int NOT NULL CHECK (b >= 1 AND b <= 10)
);
INSERT INTO part1 VALUES (1, 10);
-- Remember the TO bound is exclusive
ALTER TABLE range_parted ATTACH PARTITION part1 FOR VALUES FROM (1, 1) TO (1, 10);

-- should be ok after deleting the bad row
DELETE FROM part1;
ALTER TABLE range_parted ATTACH PARTITION part1 FOR VALUES FROM (1, 1) TO (1, 10);

-- adding constraints that describe the desired partition constraint
-- (or more restrictive) will help skip the validation scan
CREATE TABLE part2 (
     a int NOT NULL CHECK (a = 1),
     b int NOT NULL CHECK (b >= 10 AND b < 18)
);
ALTER TABLE range_parted ATTACH PARTITION part2 FOR VALUES FROM (1, 10) TO (1, 20);

-- Create default partition
CREATE TABLE partr_def1 PARTITION OF range_parted DEFAULT;

-- Only one default partition is allowed, hence, following should give error
CREATE TABLE partr_def2 (LIKE part1 INCLUDING CONSTRAINTS);
ALTER TABLE range_parted ATTACH PARTITION partr_def2 DEFAULT;

-- Overlapping partitions cannot be attached, hence, following should give error
INSERT INTO partr_def1 VALUES (2, 10);
CREATE TABLE part3 (LIKE range_parted);
ALTER TABLE range_parted ATTACH partition part3 FOR VALUES FROM (2, 10) TO (2, 20);

-- Attaching partitions should be successful when there are no overlapping rows
ALTER TABLE range_parted ATTACH partition part3 FOR VALUES FROM (3, 10) TO (3, 20);

-- check that leaf partitions are scanned when attaching a partitioned
-- table
CREATE TABLE part_5 (
	LIKE list_parted2
) PARTITION BY LIST (b);

-- check that violating rows are correctly reported
CREATE TABLE part_5_a PARTITION OF part_5 FOR VALUES IN ('a');
INSERT INTO part_5_a (a, b) VALUES (6, 'a');
ALTER TABLE list_parted2 ATTACH PARTITION part_5 FOR VALUES IN (5);

-- delete the faulting row and also add a constraint to skip the scan
DELETE FROM part_5_a WHERE a NOT IN (3);
ALTER TABLE part_5 ADD CONSTRAINT check_a CHECK (a IS NOT NULL AND a = 5);
ALTER TABLE list_parted2 ATTACH PARTITION part_5 FOR VALUES IN (5);
ALTER TABLE list_parted2 DETACH PARTITION part_5;
ALTER TABLE part_5 DROP CONSTRAINT check_a;

-- scan should again be skipped, even though NOT NULL is now a column property
ALTER TABLE part_5 ADD CONSTRAINT check_a CHECK (a IN (5)), ALTER a SET NOT NULL;
ALTER TABLE list_parted2 ATTACH PARTITION part_5 FOR VALUES IN (5);

-- Check the case where attnos of the partitioning columns in the table being
-- attached differs from the parent.  It should not affect the constraint-
-- checking logic that allows to skip the scan.
CREATE TABLE part_6 (
	c int,
	LIKE list_parted2,
	CONSTRAINT check_a CHECK (a IS NOT NULL AND a = 6)
);
ALTER TABLE part_6 DROP c;
ALTER TABLE list_parted2 ATTACH PARTITION part_6 FOR VALUES IN (6);

-- Similar to above, but the table being attached is a partitioned table
-- whose partition has still different attnos for the root partitioning
-- columns.
CREATE TABLE part_7 (
	LIKE list_parted2,
	CONSTRAINT check_a CHECK (a IS NOT NULL AND a = 7)
) PARTITION BY LIST (b);
CREATE TABLE part_7_a_null (
	c int,
	d int,
	e int,
	LIKE list_parted2,  -- 'a' will have attnum = 4
	CONSTRAINT check_b CHECK (b IS NULL OR b = 'a'),
	CONSTRAINT check_a CHECK (a IS NOT NULL AND a = 7)
);
ALTER TABLE part_7_a_null DROP c, DROP d, DROP e;
ALTER TABLE part_7 ATTACH PARTITION part_7_a_null FOR VALUES IN ('a', null);
ALTER TABLE list_parted2 ATTACH PARTITION part_7 FOR VALUES IN (7);

-- Same example, but check this time that the constraint correctly detects
-- violating rows
ALTER TABLE list_parted2 DETACH PARTITION part_7;
ALTER TABLE part_7 DROP CONSTRAINT check_a; -- thusly, scan won't be skipped
INSERT INTO part_7 (a, b) VALUES (8, null), (9, 'a');
SELECT tableoid::regclass, a, b FROM part_7 order by a;
ALTER TABLE list_parted2 ATTACH PARTITION part_7 FOR VALUES IN (7);

-- check that leaf partitions of default partition are scanned when
-- attaching a partitioned table.
ALTER TABLE part_5 DROP CONSTRAINT check_a;
CREATE TABLE part5_def PARTITION OF part_5 DEFAULT PARTITION BY LIST(a);
CREATE TABLE part5_def_p1 PARTITION OF part5_def FOR VALUES IN (5);
INSERT INTO part5_def_p1 VALUES (5, 'y');
CREATE TABLE part5_p1 (LIKE part_5);
ALTER TABLE part_5 ATTACH PARTITION part5_p1 FOR VALUES IN ('y');
-- should be ok after deleting the bad row
DELETE FROM part5_def_p1 WHERE b = 'y';
ALTER TABLE part_5 ATTACH PARTITION part5_p1 FOR VALUES IN ('y');

-- check that the table being attached is not already a partition
ALTER TABLE list_parted2 ATTACH PARTITION part_2 FOR VALUES IN (2);

-- check that circular inheritance is not allowed
ALTER TABLE part_5 ATTACH PARTITION list_parted2 FOR VALUES IN ('b');
ALTER TABLE list_parted2 ATTACH PARTITION list_parted2 FOR VALUES IN (0);

-- If a partitioned table being created or an existing table being attached
-- as a partition does not have a constraint that would allow validation scan
-- to be skipped, but an individual partition does, then the partition's
-- validation scan is skipped.
CREATE TABLE quuux (a int, b text) PARTITION BY LIST (a);
CREATE TABLE quuux_default PARTITION OF quuux DEFAULT PARTITION BY LIST (b);
CREATE TABLE quuux_default1 PARTITION OF quuux_default (
	CONSTRAINT check_1 CHECK (a IS NOT NULL AND a = 1)
) FOR VALUES IN ('b');
CREATE TABLE quuux1 (a int, b text);
ALTER TABLE quuux ATTACH PARTITION quuux1 FOR VALUES IN (1); -- validate!
CREATE TABLE quuux2 (a int, b text);
ALTER TABLE quuux ATTACH PARTITION quuux2 FOR VALUES IN (2); -- skip validation
DROP TABLE quuux1, quuux2;
-- should validate for quuux1, but not for quuux2
CREATE TABLE quuux1 PARTITION OF quuux FOR VALUES IN (1);
CREATE TABLE quuux2 PARTITION OF quuux FOR VALUES IN (2);
DROP TABLE quuux;

-- check validation when attaching hash partitions

-- Use hand-rolled hash functions and operator class to get predictable result
-- on different matchines. part_test_int4_ops is defined in insert.sql.

-- check that the new partition won't overlap with an existing partition
CREATE TABLE hash_parted (
	a int,
	b int
) PARTITION BY HASH (a part_test_int4_ops);
CREATE TABLE hpart_1 PARTITION OF hash_parted FOR VALUES WITH (MODULUS 4, REMAINDER 0);
CREATE TABLE fail_part (LIKE hpart_1);
ALTER TABLE hash_parted ATTACH PARTITION fail_part FOR VALUES WITH (MODULUS 8, REMAINDER 4);
ALTER TABLE hash_parted ATTACH PARTITION fail_part FOR VALUES WITH (MODULUS 8, REMAINDER 0);
DROP TABLE fail_part;

-- check validation when attaching hash partitions

-- check that violating rows are correctly reported
CREATE TABLE hpart_2 (LIKE hash_parted);
INSERT INTO hpart_2 VALUES (3, 0);
ALTER TABLE hash_parted ATTACH PARTITION hpart_2 FOR VALUES WITH (MODULUS 4, REMAINDER 1);

-- should be ok after deleting the bad row
DELETE FROM hpart_2;
ALTER TABLE hash_parted ATTACH PARTITION hpart_2 FOR VALUES WITH (MODULUS 4, REMAINDER 1);

-- check that leaf partitions are scanned when attaching a partitioned
-- table
CREATE TABLE hpart_5 (
	LIKE hash_parted
) PARTITION BY LIST (b);

-- check that violating rows are correctly reported
CREATE TABLE hpart_5_a PARTITION OF hpart_5 FOR VALUES IN ('1', '2', '3');
INSERT INTO hpart_5_a (a, b) VALUES (7, 1);
ALTER TABLE hash_parted ATTACH PARTITION hpart_5 FOR VALUES WITH (MODULUS 4, REMAINDER 2);

-- should be ok after deleting the bad row
DELETE FROM hpart_5_a;
ALTER TABLE hash_parted ATTACH PARTITION hpart_5 FOR VALUES WITH (MODULUS 4, REMAINDER 2);

-- check that the table being attach is with valid modulus and remainder value
CREATE TABLE fail_part(LIKE hash_parted);
ALTER TABLE hash_parted ATTACH PARTITION fail_part FOR VALUES WITH (MODULUS 0, REMAINDER 1);
ALTER TABLE hash_parted ATTACH PARTITION fail_part FOR VALUES WITH (MODULUS 8, REMAINDER 8);
ALTER TABLE hash_parted ATTACH PARTITION fail_part FOR VALUES WITH (MODULUS 3, REMAINDER 2);
DROP TABLE fail_part;

--
-- DETACH PARTITION
--

-- check that the table is partitioned at all
CREATE TABLE regular_table (a int);
ALTER TABLE regular_table DETACH PARTITION any_name;
DROP TABLE regular_table;

-- check that the partition being detached exists at all
ALTER TABLE list_parted2 DETACH PARTITION part_4;
ALTER TABLE hash_parted DETACH PARTITION hpart_4;

-- check that the partition being detached is actually a partition of the parent
CREATE TABLE not_a_part (a int);
ALTER TABLE list_parted2 DETACH PARTITION not_a_part;
ALTER TABLE list_parted2 DETACH PARTITION part_1;

ALTER TABLE hash_parted DETACH PARTITION not_a_part;
DROP TABLE not_a_part;

-- check that, after being detached, attinhcount/coninhcount is dropped to 0 and
-- attislocal/conislocal is set to true
ALTER TABLE list_parted2 DETACH PARTITION part_3_4;
SELECT attinhcount, attislocal FROM pg_attribute WHERE attrelid = 'part_3_4'::regclass AND attnum > 0;
SELECT coninhcount, conislocal FROM pg_constraint WHERE conrelid = 'part_3_4'::regclass AND conname = 'check_a';
DROP TABLE part_3_4;

-- check that a detached partition is not dropped on dropping a partitioned table
CREATE TABLE range_parted2 (
    a int
) PARTITION BY RANGE(a);
CREATE TABLE part_rp PARTITION OF range_parted2 FOR VALUES FROM (0) to (100);
ALTER TABLE range_parted2 DETACH PARTITION part_rp;
DROP TABLE range_parted2;
SELECT * from part_rp;
DROP TABLE part_rp;

-- Check ALTER TABLE commands for partitioned tables and partitions

-- cannot add/drop column to/from *only* the parent
ALTER TABLE ONLY list_parted2 ADD COLUMN c int;
ALTER TABLE ONLY list_parted2 DROP COLUMN b;

-- cannot add a column to partition or drop an inherited one
ALTER TABLE part_2 ADD COLUMN c text;
ALTER TABLE part_2 DROP COLUMN b;

-- Nor rename, alter type
ALTER TABLE part_2 RENAME COLUMN b to c;
ALTER TABLE part_2 ALTER COLUMN b TYPE text;

-- cannot add/drop NOT NULL or check constraints to *only* the parent, when
-- partitions exist
ALTER TABLE ONLY list_parted2 ALTER b SET NOT NULL;
ALTER TABLE ONLY list_parted2 ADD CONSTRAINT check_b CHECK (b <> 'zz');

ALTER TABLE list_parted2 ALTER b SET NOT NULL;
ALTER TABLE ONLY list_parted2 ALTER b DROP NOT NULL;
ALTER TABLE list_parted2 ADD CONSTRAINT check_b CHECK (b <> 'zz');
-- Uncomment when #14570 is fixed
/*
ALTER TABLE ONLY list_parted2 DROP CONSTRAINT check_b;

-- It's alright though, if no partitions are yet created
CREATE TABLE parted_no_parts (a int) PARTITION BY LIST (a);
ALTER TABLE ONLY parted_no_parts ALTER a SET NOT NULL;
ALTER TABLE ONLY parted_no_parts ADD CONSTRAINT check_a CHECK (a > 0);
ALTER TABLE ONLY parted_no_parts ALTER a DROP NOT NULL;
ALTER TABLE ONLY parted_no_parts DROP CONSTRAINT check_a;
DROP TABLE parted_no_parts;
*/
-- cannot drop inherited NOT NULL or check constraints from partition
ALTER TABLE list_parted2 ALTER b SET NOT NULL, ADD CONSTRAINT check_a2 CHECK (a > 0);
ALTER TABLE part_2 ALTER b DROP NOT NULL;
ALTER TABLE part_2 DROP CONSTRAINT check_a2;

-- Doesn't make sense to add NO INHERIT constraints on partitioned tables
ALTER TABLE list_parted2 add constraint check_b2 check (b <> 'zz') NO INHERIT;

-- check that a partition cannot participate in regular inheritance
CREATE TABLE inh_test () INHERITS (part_2);
-- Uncomment if INHERITS is supported (#5956).
/*
CREATE TABLE inh_test (LIKE part_2);
ALTER TABLE inh_test INHERIT part_2;
ALTER TABLE part_2 INHERIT inh_test;
*/
-- cannot drop or alter type of partition key columns of lower level
-- partitioned tables; for example, part_5, which is list_parted2's
-- partition, is partitioned on b;
ALTER TABLE list_parted2 DROP COLUMN b;
ALTER TABLE list_parted2 ALTER COLUMN b TYPE text;

-- dropping non-partition key columns should be allowed on the parent table.
ALTER TABLE list_parted DROP COLUMN b;
SELECT * FROM list_parted;

-- cleanup
DROP TABLE list_parted, list_parted2, range_parted;
DROP TABLE fail_def_part;
DROP TABLE hash_parted;

-- more tests for certain multi-level partitioning scenarios
create table p (a int, b int) partition by range (a, b);
create table p1 (b int, a int not null) partition by range (b);
create table p11 (like p1);
alter table p11 drop a;
alter table p11 add a int;
alter table p11 drop a;
alter table p11 add a int not null;
-- attnum for key attribute 'a' is different in p, p1, and p11
select attrelid::regclass, attname, attnum
from pg_attribute
where attname = 'a'
 and (attrelid = 'p'::regclass
   or attrelid = 'p1'::regclass
   or attrelid = 'p11'::regclass)
order by attrelid::regclass::text;

alter table p1 attach partition p11 for values from (2) to (5);

insert into p1 (a, b) values (2, 3);
-- check that partition validation scan correctly detects violating rows
alter table p attach partition p1 for values from (1, 2) to (1, 10);

-- cleanup
drop table p;
drop table p1;

-- validate constraint on partitioned tables should only scan leaf partitions
create table parted_validate_test (a int) partition by list (a);
create table parted_validate_test_1 partition of parted_validate_test for values in (0, 1);
alter table parted_validate_test add constraint parted_validate_test_chka check (a > 0) not valid;
alter table parted_validate_test validate constraint parted_validate_test_chka;
drop table parted_validate_test;

-- test alter column options
CREATE TABLE attmp(i integer);
INSERT INTO attmp VALUES (1);
ALTER TABLE attmp ALTER COLUMN i SET (n_distinct = 1, n_distinct_inherited = 2);
-- Uncomment when ALTER COLUMN is supported (#1200).
-- ALTER TABLE attmp ALTER COLUMN i RESET (n_distinct_inherited);
-- ANALYZE attmp;
DROP TABLE attmp;

DROP USER regress_alter_table_user1;

-- check that violating rows are correctly reported when attaching as the
-- default partition
create table defpart_attach_test (a int) partition by list (a);
create table defpart_attach_test1 partition of defpart_attach_test for values in (1);
create table defpart_attach_test_d (like defpart_attach_test);
insert into defpart_attach_test_d values (1), (2);

-- error because its constraint as the default partition would be violated
-- by the row containing 1
alter table defpart_attach_test attach partition defpart_attach_test_d default;
delete from defpart_attach_test_d where a = 1;
alter table defpart_attach_test_d add check (a > 1);

-- should be attached successfully and without needing to be scanned
alter table defpart_attach_test attach partition defpart_attach_test_d default;

drop table defpart_attach_test;

-- check combinations of temporary and permanent relations when attaching
-- partitions.
create table perm_part_parent (a int) partition by list (a);
create temp table temp_part_parent (a int) partition by list (a);
create table perm_part_child (a int);
create temp table temp_part_child (a int);
alter table temp_part_parent attach partition perm_part_child default; -- error
alter table perm_part_parent attach partition temp_part_child default; -- error
alter table temp_part_parent attach partition temp_part_child default; -- ok
drop table perm_part_parent cascade;
drop table temp_part_parent cascade;

-- check that attaching partitions to a table while it is being used is
-- prevented
create table tab_part_attach (a int) partition by list (a);
create or replace function func_part_attach() returns trigger
  language plpgsql as $$
  begin
    execute 'create table tab_part_attach_1 (a int)';
    execute 'alter table tab_part_attach attach partition tab_part_attach_1 for values in (1)';
    return null;
  end $$;
create trigger trig_part_attach before insert on tab_part_attach
  for each statement execute procedure func_part_attach();
insert into tab_part_attach values (1);
drop table tab_part_attach;
drop function func_part_attach();

-- test case where the partitioning operator is a SQL function whose
-- evaluation results in the table's relcache being rebuilt partway through
-- the execution of an ATTACH PARTITION command
create function at_test_sql_partop (int4, int4) returns int language sql
as $$ select case when $1 = $2 then 0 when $1 > $2 then 1 else -1 end; $$;
create operator class at_test_sql_partop for type int4 using btree as
    operator 1 < (int4, int4), operator 2 <= (int4, int4),
    operator 3 = (int4, int4), operator 4 >= (int4, int4),
    operator 5 > (int4, int4), function 1 at_test_sql_partop(int4, int4);
-- Fails due to #14575, uncomment the following tests once fixed.
create table at_test_sql_partop (a int) partition by range (a at_test_sql_partop);
/*
create table at_test_sql_partop_1 (a int);
alter table at_test_sql_partop attach partition at_test_sql_partop_1 for values from (0) to (10);
drop table at_test_sql_partop;
*/
drop operator class at_test_sql_partop using btree;
drop function at_test_sql_partop;
--
-- Check that attaching or detaching a partitioned partition correctly leads
-- to its partitions' constraint being updated to reflect the parent's
-- newly added/removed constraint
create table target_parted (a int, b int) partition by list (a);
create table attach_parted (a int, b int) partition by list (b);
create table attach_parted_part1 partition of attach_parted for values in (1);
-- insert a row directly into the leaf partition so that its partition
-- constraint is built and stored in the relcache
insert into attach_parted_part1 values (1, 1);
-- the following better invalidate the partition constraint of the leaf
-- partition too...
alter table target_parted attach partition attach_parted for values in (1);
-- ...such that the following insert fails
insert into attach_parted_part1 values (2, 1);
-- ...and doesn't when the partition is detached along with its own partition
alter table target_parted detach partition attach_parted;
insert into attach_parted_part1 values (2, 1);
