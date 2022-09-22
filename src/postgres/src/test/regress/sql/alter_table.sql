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

INSERT INTO attmp (a, b, c, d, e, f, g, h, i, j, k, l, m, n, p, q, r, s, t, u,
	v, w, x, y, z)
   VALUES (4, 'name', 'text', 4.1, 4.1, 2, '(4.1,4.1,3.1,3.1)',
        'Mon May  1 00:30:30 1995', 'c', '{Mon May  1 00:30:30 1995, Monday Aug 24 14:43:07 1992, epoch}',
	314159, '(1,1)', '512',
	'1 2 3 4 5 6 7 8', 'magnetic disk', '(1.1,1.1)', '(4.1,4.1,3.1,3.1)',
	'(0,2,4.1,4.1,3.1,3.1)', '(4.1,4.1,3.1,3.1)', '["epoch" "infinity"]',
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

INSERT INTO attmp (a, b, c, d, e, f, g, h, i, j, k, l, m, n, p, q, r, s, t, u,
	v, w, x, y, z)
   VALUES (4, 'name', 'text', 4.1, 4.1, 2, '(4.1,4.1,3.1,3.1)',
        'Mon May  1 00:30:30 1995', 'c', '{Mon May  1 00:30:30 1995, Monday Aug 24 14:43:07 1992, epoch}',
	314159, '(1,1)', '512',
	'1 2 3 4 5 6 7 8', 'magnetic disk', '(1.1,1.1)', '(4.1,4.1,3.1,3.1)',
	'(0,2,4.1,4.1,3.1,3.1)', '(4.1,4.1,3.1,3.1)', '["epoch" "infinity"]',
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

--
-- check renaming to a table's array type's autogenerated name
-- (the array type's name should get out of the way)
--
CREATE TABLE attmp_array (id int);
CREATE TABLE attmp_array2 (id int);
SELECT typname FROM pg_type WHERE oid = 'attmp_array[]'::regtype;
SELECT typname FROM pg_type WHERE oid = 'attmp_array2[]'::regtype;
ALTER TABLE attmp_array2 RENAME TO _attmp_array;
SELECT typname FROM pg_type WHERE oid = 'attmp_array[]'::regtype;
SELECT typname FROM pg_type WHERE oid = '_attmp_array[]'::regtype;
DROP TABLE _attmp_array;
DROP TABLE attmp_array;

-- renaming to table's own array type's name is an interesting corner case
CREATE TABLE attmp_array (id int);
SELECT typname FROM pg_type WHERE oid = 'attmp_array[]'::regtype;
ALTER TABLE attmp_array RENAME TO _attmp_array;
SELECT typname FROM pg_type WHERE oid = '_attmp_array[]'::regtype;
DROP TABLE _attmp_array;

-- ALTER TABLE ... RENAME on non-table relations
-- renaming indexes (FIXME: this should probably test the index's functionality)
ALTER INDEX IF EXISTS __onek_unique1 RENAME TO attmp_onek_unique1;
ALTER INDEX IF EXISTS __attmp_onek_unique1 RENAME TO onek_unique1;

ALTER INDEX onek_unique1 RENAME TO attmp_onek_unique1;
ALTER INDEX attmp_onek_unique1 RENAME TO onek_unique1;

SET ROLE regress_alter_table_user1;
ALTER INDEX onek_unique1 RENAME TO fail;  -- permission denied
RESET ROLE;

-- renaming views
CREATE VIEW attmp_view (unique1) AS SELECT unique1 FROM tenk1;
ALTER TABLE attmp_view RENAME TO attmp_view_new;

SET ROLE regress_alter_table_user1;
ALTER VIEW attmp_view_new RENAME TO fail;  -- permission denied
RESET ROLE;

-- hack to ensure we get an indexscan here
set enable_seqscan to off;
set enable_bitmapscan to off;
-- 5 values, sorted
SELECT unique1 FROM tenk1 WHERE unique1 < 5;
reset enable_seqscan;
reset enable_bitmapscan;

DROP VIEW attmp_view_new;
-- toast-like relation name
alter table stud_emp rename to pg_toast_stud_emp;
alter table pg_toast_stud_emp rename to stud_emp;

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

-- FOREIGN KEY CONSTRAINT adding TEST

CREATE TABLE attmp2 (a int primary key);

CREATE TABLE attmp3 (a int, b int);

CREATE TABLE attmp4 (a int, b int, unique(a,b));

CREATE TABLE attmp5 (a int, b int);

-- Insert rows into attmp2 (pktable)
INSERT INTO attmp2 values (1);
INSERT INTO attmp2 values (2);
INSERT INTO attmp2 values (3);
INSERT INTO attmp2 values (4);

-- Insert rows into attmp3
INSERT INTO attmp3 values (1,10);
INSERT INTO attmp3 values (1,20);
INSERT INTO attmp3 values (5,50);

-- Try (and fail) to add constraint due to invalid source columns
ALTER TABLE attmp3 add constraint attmpconstr foreign key(c) references attmp2 match full;

-- Try (and fail) to add constraint due to invalid destination columns explicitly given
ALTER TABLE attmp3 add constraint attmpconstr foreign key(a) references attmp2(b) match full;

-- Try (and fail) to add constraint due to invalid data
ALTER TABLE attmp3 add constraint attmpconstr foreign key (a) references attmp2 match full;

-- Delete failing row
DELETE FROM attmp3 where a=5;

-- Try (and succeed)
ALTER TABLE attmp3 add constraint attmpconstr foreign key (a) references attmp2 match full;
ALTER TABLE attmp3 drop constraint attmpconstr;

INSERT INTO attmp3 values (5,50);

-- Try NOT VALID and then VALIDATE CONSTRAINT, but fails. Delete failure then re-validate
ALTER TABLE attmp3 add constraint attmpconstr foreign key (a) references attmp2 match full NOT VALID;
ALTER TABLE attmp3 validate constraint attmpconstr;

-- Delete failing row
DELETE FROM attmp3 where a=5;

-- Try (and succeed) and repeat to show it works on already valid constraint
ALTER TABLE attmp3 validate constraint attmpconstr;
ALTER TABLE attmp3 validate constraint attmpconstr;

-- Try a non-verified CHECK constraint
ALTER TABLE attmp3 ADD CONSTRAINT b_greater_than_ten CHECK (b > 10); -- fail
ALTER TABLE attmp3 ADD CONSTRAINT b_greater_than_ten CHECK (b > 10) NOT VALID; -- succeeds
ALTER TABLE attmp3 VALIDATE CONSTRAINT b_greater_than_ten; -- fails
DELETE FROM attmp3 WHERE NOT b > 10;
ALTER TABLE attmp3 VALIDATE CONSTRAINT b_greater_than_ten; -- succeeds
ALTER TABLE attmp3 VALIDATE CONSTRAINT b_greater_than_ten; -- succeeds

-- Test inherited NOT VALID CHECK constraints
select * from attmp3;
CREATE TABLE attmp6 () INHERITS (attmp3);
CREATE TABLE attmp7 () INHERITS (attmp3);

INSERT INTO attmp6 VALUES (6, 30), (7, 16);
ALTER TABLE attmp3 ADD CONSTRAINT b_le_20 CHECK (b <= 20) NOT VALID;
ALTER TABLE attmp3 VALIDATE CONSTRAINT b_le_20;	-- fails
DELETE FROM attmp6 WHERE b > 20;
ALTER TABLE attmp3 VALIDATE CONSTRAINT b_le_20;	-- succeeds

-- An already validated constraint must not be revalidated
CREATE FUNCTION boo(int) RETURNS int IMMUTABLE STRICT LANGUAGE plpgsql AS $$ BEGIN RAISE NOTICE 'boo: %', $1; RETURN $1; END; $$;
INSERT INTO attmp7 VALUES (8, 18);
ALTER TABLE attmp7 ADD CONSTRAINT identity CHECK (b = boo(b));
ALTER TABLE attmp3 ADD CONSTRAINT IDENTITY check (b = boo(b)) NOT VALID;
ALTER TABLE attmp3 VALIDATE CONSTRAINT identity;

-- A NO INHERIT constraint should not be looked for in children during VALIDATE CONSTRAINT
create table parent_noinh_convalid (a int);
create table child_noinh_convalid () inherits (parent_noinh_convalid);
insert into parent_noinh_convalid values (1);
insert into child_noinh_convalid values (1);
alter table parent_noinh_convalid add constraint check_a_is_2 check (a = 2) no inherit not valid;
-- fail, because of the row in parent
alter table parent_noinh_convalid validate constraint check_a_is_2;
delete from only parent_noinh_convalid;
-- ok (parent itself contains no violating rows)
alter table parent_noinh_convalid validate constraint check_a_is_2;
select convalidated from pg_constraint where conrelid = 'parent_noinh_convalid'::regclass and conname = 'check_a_is_2';
-- cleanup
drop table parent_noinh_convalid, child_noinh_convalid;

-- Try (and fail) to create constraint from attmp5(a) to attmp4(a) - unique constraint on
-- attmp4 is a,b

ALTER TABLE attmp5 add constraint attmpconstr foreign key(a) references attmp4(a) match full;

DROP TABLE attmp7;

DROP TABLE attmp6;

DROP TABLE attmp5;

DROP TABLE attmp4;

DROP TABLE attmp3;

DROP TABLE attmp2;

-- NOT VALID with plan invalidation -- ensure we don't use a constraint for
-- exclusion until validated
set constraint_exclusion TO 'partition';
create table nv_parent (d date, check (false) no inherit not valid);
-- not valid constraint added at creation time should automatically become valid
\d nv_parent

create table nv_child_2010 () inherits (nv_parent);
create table nv_child_2011 () inherits (nv_parent);
alter table nv_child_2010 add check (d between '2010-01-01'::date and '2010-12-31'::date) not valid;
alter table nv_child_2011 add check (d between '2011-01-01'::date and '2011-12-31'::date) not valid;
explain (costs off) select * from nv_parent where d between '2011-08-01' and '2011-08-31';
create table nv_child_2009 (check (d between '2009-01-01'::date and '2009-12-31'::date)) inherits (nv_parent);
explain (costs off) select * from nv_parent where d between '2011-08-01'::date and '2011-08-31'::date;
explain (costs off) select * from nv_parent where d between '2009-08-01'::date and '2009-08-31'::date;
-- after validation, the constraint should be used
alter table nv_child_2011 VALIDATE CONSTRAINT nv_child_2011_d_check;
explain (costs off) select * from nv_parent where d between '2009-08-01'::date and '2009-08-31'::date;

-- add an inherited NOT VALID constraint
alter table nv_parent add check (d between '2001-01-01'::date and '2099-12-31'::date) not valid;
\d nv_child_2009
-- we leave nv_parent and children around to help test pg_dump logic

-- Foreign key adding test with mixed types

-- Note: these tables are TEMP to avoid name conflicts when this test
-- is run in parallel with foreign_key.sql.

CREATE TEMP TABLE PKTABLE (ptest1 int PRIMARY KEY);
INSERT INTO PKTABLE VALUES(42);
CREATE TEMP TABLE FKTABLE (ftest1 inet);
-- This next should fail, because int=inet does not exist
ALTER TABLE FKTABLE ADD FOREIGN KEY(ftest1) references pktable;
-- This should also fail for the same reason, but here we
-- give the column name
ALTER TABLE FKTABLE ADD FOREIGN KEY(ftest1) references pktable(ptest1);
DROP TABLE FKTABLE;
-- This should succeed, even though they are different types,
-- because int=int8 exists and is a member of the integer opfamily
CREATE TEMP TABLE FKTABLE (ftest1 int8);
ALTER TABLE FKTABLE ADD FOREIGN KEY(ftest1) references pktable;
-- Check it actually works
INSERT INTO FKTABLE VALUES(42);		-- should succeed
INSERT INTO FKTABLE VALUES(43);		-- should fail
DROP TABLE FKTABLE;
-- This should fail, because we'd have to cast numeric to int which is
-- not an implicit coercion (or use numeric=numeric, but that's not part
-- of the integer opfamily)
CREATE TEMP TABLE FKTABLE (ftest1 numeric);
ALTER TABLE FKTABLE ADD FOREIGN KEY(ftest1) references pktable;
DROP TABLE FKTABLE;
DROP TABLE PKTABLE;
-- On the other hand, this should work because int implicitly promotes to
-- numeric, and we allow promotion on the FK side
CREATE TEMP TABLE PKTABLE (ptest1 numeric PRIMARY KEY);
INSERT INTO PKTABLE VALUES(42);
CREATE TEMP TABLE FKTABLE (ftest1 int);
ALTER TABLE FKTABLE ADD FOREIGN KEY(ftest1) references pktable;
-- Check it actually works
INSERT INTO FKTABLE VALUES(42);		-- should succeed
INSERT INTO FKTABLE VALUES(43);		-- should fail
DROP TABLE FKTABLE;
DROP TABLE PKTABLE;

CREATE TEMP TABLE PKTABLE (ptest1 int, ptest2 inet,
                           PRIMARY KEY(ptest1, ptest2));
-- This should fail, because we just chose really odd types
CREATE TEMP TABLE FKTABLE (ftest1 cidr, ftest2 timestamp);
ALTER TABLE FKTABLE ADD FOREIGN KEY(ftest1, ftest2) references pktable;
DROP TABLE FKTABLE;
-- Again, so should this...
CREATE TEMP TABLE FKTABLE (ftest1 cidr, ftest2 timestamp);
ALTER TABLE FKTABLE ADD FOREIGN KEY(ftest1, ftest2)
     references pktable(ptest1, ptest2);
DROP TABLE FKTABLE;
-- This fails because we mixed up the column ordering
CREATE TEMP TABLE FKTABLE (ftest1 int, ftest2 inet);
ALTER TABLE FKTABLE ADD FOREIGN KEY(ftest1, ftest2)
     references pktable(ptest2, ptest1);
-- As does this...
ALTER TABLE FKTABLE ADD FOREIGN KEY(ftest2, ftest1)
     references pktable(ptest1, ptest2);
DROP TABLE FKTABLE;
DROP TABLE PKTABLE;

-- Test that ALTER CONSTRAINT updates trigger deferrability properly

CREATE TEMP TABLE PKTABLE (ptest1 int primary key);
CREATE TEMP TABLE FKTABLE (ftest1 int);

ALTER TABLE FKTABLE ADD CONSTRAINT fknd FOREIGN KEY(ftest1) REFERENCES pktable
  ON DELETE CASCADE ON UPDATE NO ACTION NOT DEFERRABLE;
ALTER TABLE FKTABLE ADD CONSTRAINT fkdd FOREIGN KEY(ftest1) REFERENCES pktable
  ON DELETE CASCADE ON UPDATE NO ACTION DEFERRABLE INITIALLY DEFERRED;
ALTER TABLE FKTABLE ADD CONSTRAINT fkdi FOREIGN KEY(ftest1) REFERENCES pktable
  ON DELETE CASCADE ON UPDATE NO ACTION DEFERRABLE INITIALLY IMMEDIATE;

ALTER TABLE FKTABLE ADD CONSTRAINT fknd2 FOREIGN KEY(ftest1) REFERENCES pktable
  ON DELETE CASCADE ON UPDATE NO ACTION DEFERRABLE INITIALLY DEFERRED;
ALTER TABLE FKTABLE ALTER CONSTRAINT fknd2 NOT DEFERRABLE;
ALTER TABLE FKTABLE ADD CONSTRAINT fkdd2 FOREIGN KEY(ftest1) REFERENCES pktable
  ON DELETE CASCADE ON UPDATE NO ACTION NOT DEFERRABLE;
ALTER TABLE FKTABLE ALTER CONSTRAINT fkdd2 DEFERRABLE INITIALLY DEFERRED;
ALTER TABLE FKTABLE ADD CONSTRAINT fkdi2 FOREIGN KEY(ftest1) REFERENCES pktable
  ON DELETE CASCADE ON UPDATE NO ACTION NOT DEFERRABLE;
ALTER TABLE FKTABLE ALTER CONSTRAINT fkdi2 DEFERRABLE INITIALLY IMMEDIATE;

SELECT conname, tgfoid::regproc, tgtype, tgdeferrable, tginitdeferred
FROM pg_trigger JOIN pg_constraint con ON con.oid = tgconstraint
WHERE tgrelid = 'pktable'::regclass
ORDER BY 1,2,3;
SELECT conname, tgfoid::regproc, tgtype, tgdeferrable, tginitdeferred
FROM pg_trigger JOIN pg_constraint con ON con.oid = tgconstraint
WHERE tgrelid = 'fktable'::regclass
ORDER BY 1,2,3;

-- temp tables should go away by themselves, need not drop them.

-- test check constraint adding

create table atacc1 ( test int );
-- add a check constraint
alter table atacc1 add constraint atacc_test1 check (test>3);
-- should fail
insert into atacc1 (test) values (2);
-- should succeed
insert into atacc1 (test) values (4);
drop table atacc1;

-- let's do one where the check fails when added
create table atacc1 ( test int );
-- insert a soon to be failing row
insert into atacc1 (test) values (2);
-- add a check constraint (fails)
alter table atacc1 add constraint atacc_test1 check (test>3);
insert into atacc1 (test) values (4);
drop table atacc1;

-- let's do one where the check fails because the column doesn't exist
create table atacc1 ( test int );
-- add a check constraint (fails)
alter table atacc1 add constraint atacc_test1 check (test1>3);
drop table atacc1;

-- something a little more complicated
create table atacc1 ( test int, test2 int, test3 int);
-- add a check constraint (fails)
alter table atacc1 add constraint atacc_test1 check (test+test2<test3*4);
-- should fail
insert into atacc1 (test,test2,test3) values (4,4,2);
-- should succeed
insert into atacc1 (test,test2,test3) values (4,4,5);
drop table atacc1;

-- lets do some naming tests
create table atacc1 (test int check (test>3), test2 int);
alter table atacc1 add check (test2>test);
-- should fail for $2
insert into atacc1 (test2, test) values (3, 4);
drop table atacc1;

-- inheritance related tests
create table atacc1 (test int);
create table atacc2 (test2 int);
create table atacc3 (test3 int) inherits (atacc1, atacc2);
alter table atacc2 add constraint foo check (test2>0);
-- fail and then succeed on atacc2
insert into atacc2 (test2) values (-3);
insert into atacc2 (test2) values (3);
-- fail and then succeed on atacc3
insert into atacc3 (test2) values (-3);
insert into atacc3 (test2) values (3);
drop table atacc3;
drop table atacc2;
drop table atacc1;

-- same things with one created with INHERIT
create table atacc1 (test int);
create table atacc2 (test2 int);
create table atacc3 (test3 int) inherits (atacc1, atacc2);
alter table atacc3 no inherit atacc2;
-- fail
alter table atacc3 no inherit atacc2;
-- make sure it really isn't a child
insert into atacc3 (test2) values (3);
select test2 from atacc2;
-- fail due to missing constraint
alter table atacc2 add constraint foo check (test2>0);
alter table atacc3 inherit atacc2;
-- fail due to missing column
alter table atacc3 rename test2 to testx;
alter table atacc3 inherit atacc2;
-- fail due to mismatched data type
alter table atacc3 add test2 bool;
alter table atacc3 inherit atacc2;
alter table atacc3 drop test2;
-- succeed
alter table atacc3 add test2 int;
update atacc3 set test2 = 4 where test2 is null;
alter table atacc3 add constraint foo check (test2>0);
alter table atacc3 inherit atacc2;
-- fail due to duplicates and circular inheritance
alter table atacc3 inherit atacc2;
alter table atacc2 inherit atacc3;
alter table atacc2 inherit atacc2;
-- test that we really are a child now (should see 4 not 3 and cascade should go through)
select test2 from atacc2;
drop table atacc2 cascade;
drop table atacc1;

-- adding only to a parent is allowed as of 9.2

create table atacc1 (test int);
create table atacc2 (test2 int) inherits (atacc1);
-- ok:
alter table atacc1 add constraint foo check (test>0) no inherit;
-- check constraint is not there on child
insert into atacc2 (test) values (-3);
-- check constraint is there on parent
insert into atacc1 (test) values (-3);
insert into atacc1 (test) values (3);
-- fail, violating row:
alter table atacc2 add constraint foo check (test>0) no inherit;
drop table atacc2;
drop table atacc1;

-- test unique constraint adding

create table atacc1 ( test int ) with oids;
-- add a unique constraint
alter table atacc1 add constraint atacc_test1 unique (test);
-- insert first value
insert into atacc1 (test) values (2);
-- should fail
insert into atacc1 (test) values (2);
-- should succeed
insert into atacc1 (test) values (4);
-- try adding a unique oid constraint
alter table atacc1 add constraint atacc_oid1 unique(oid);
-- try to create duplicates via alter table using - should fail
alter table atacc1 alter column test type integer using 0;
drop table atacc1;

-- let's do one where the unique constraint fails when added
create table atacc1 ( test int );
-- insert soon to be failing rows
insert into atacc1 (test) values (2);
insert into atacc1 (test) values (2);
-- add a unique constraint (fails)
alter table atacc1 add constraint atacc_test1 unique (test);
insert into atacc1 (test) values (3);
drop table atacc1;

-- let's do one where the unique constraint fails
-- because the column doesn't exist
create table atacc1 ( test int );
-- add a unique constraint (fails)
alter table atacc1 add constraint atacc_test1 unique (test1);
drop table atacc1;

-- something a little more complicated
create table atacc1 ( test int, test2 int);
-- add a unique constraint
alter table atacc1 add constraint atacc_test1 unique (test, test2);
-- insert initial value
insert into atacc1 (test,test2) values (4,4);
-- should fail
insert into atacc1 (test,test2) values (4,4);
-- should all succeed
insert into atacc1 (test,test2) values (4,5);
insert into atacc1 (test,test2) values (5,4);
insert into atacc1 (test,test2) values (5,5);
drop table atacc1;

-- lets do some naming tests
create table atacc1 (test int, test2 int, unique(test));
alter table atacc1 add unique (test2);
-- should fail for @@ second one @@
insert into atacc1 (test2, test) values (3, 3);
insert into atacc1 (test2, test) values (2, 3);
drop table atacc1;

-- test primary key constraint adding

create table atacc1 ( test int ) with oids;
-- add a primary key constraint
alter table atacc1 add constraint atacc_test1 primary key (test);
-- insert first value
insert into atacc1 (test) values (2);
-- should fail
insert into atacc1 (test) values (2);
-- should succeed
insert into atacc1 (test) values (4);
-- inserting NULL should fail
insert into atacc1 (test) values(NULL);
-- try adding a second primary key (should fail)
alter table atacc1 add constraint atacc_oid1 primary key(oid);
-- drop first primary key constraint
alter table atacc1 drop constraint atacc_test1 restrict;
-- try adding a primary key on oid (should succeed)
alter table atacc1 add constraint atacc_oid1 primary key(oid);
drop table atacc1;

-- let's do one where the primary key constraint fails when added
create table atacc1 ( test int );
-- insert soon to be failing rows
insert into atacc1 (test) values (2);
insert into atacc1 (test) values (2);
-- add a primary key (fails)
alter table atacc1 add constraint atacc_test1 primary key (test);
insert into atacc1 (test) values (3);
drop table atacc1;

-- let's do another one where the primary key constraint fails when added
create table atacc1 ( test int );
-- insert soon to be failing row
insert into atacc1 (test) values (NULL);
-- add a primary key (fails)
alter table atacc1 add constraint atacc_test1 primary key (test);
insert into atacc1 (test) values (3);
drop table atacc1;

-- let's do one where the primary key constraint fails
-- because the column doesn't exist
create table atacc1 ( test int );
-- add a primary key constraint (fails)
alter table atacc1 add constraint atacc_test1 primary key (test1);
drop table atacc1;

-- adding a new column as primary key to a non-empty table.
-- should fail unless the column has a non-null default value.
create table atacc1 ( test int );
insert into atacc1 (test) values (0);
-- add a primary key column without a default (fails).
alter table atacc1 add column test2 int primary key;
-- now add a primary key column with a default (succeeds).
alter table atacc1 add column test2 int default 0 primary key;
drop table atacc1;

-- something a little more complicated
create table atacc1 ( test int, test2 int);
-- add a primary key constraint
alter table atacc1 add constraint atacc_test1 primary key (test, test2);
-- try adding a second primary key - should fail
alter table atacc1 add constraint atacc_test2 primary key (test);
-- insert initial value
insert into atacc1 (test,test2) values (4,4);
-- should fail
insert into atacc1 (test,test2) values (4,4);
insert into atacc1 (test,test2) values (NULL,3);
insert into atacc1 (test,test2) values (3, NULL);
insert into atacc1 (test,test2) values (NULL,NULL);
-- should all succeed
insert into atacc1 (test,test2) values (4,5);
insert into atacc1 (test,test2) values (5,4);
insert into atacc1 (test,test2) values (5,5);
drop table atacc1;

-- lets do some naming tests
create table atacc1 (test int, test2 int, primary key(test));
-- only first should succeed
insert into atacc1 (test2, test) values (3, 3);
insert into atacc1 (test2, test) values (2, 3);
insert into atacc1 (test2, test) values (1, NULL);
drop table atacc1;

-- alter table / alter column [set/drop] not null tests
-- try altering system catalogs, should fail
alter table pg_class alter column relname drop not null;
alter table pg_class alter relname set not null;

-- try altering non-existent table, should fail
alter table non_existent alter column bar set not null;
alter table non_existent alter column bar drop not null;

-- test setting columns to null and not null and vice versa
-- test checking for null values and primary key
create table atacc1 (test int not null) with oids;
alter table atacc1 add constraint "atacc1_pkey" primary key (test);
alter table atacc1 alter column test drop not null;
alter table atacc1 drop constraint "atacc1_pkey";
alter table atacc1 alter column test drop not null;
insert into atacc1 values (null);
alter table atacc1 alter test set not null;
delete from atacc1;
alter table atacc1 alter test set not null;

-- try altering a non-existent column, should fail
alter table atacc1 alter bar set not null;
alter table atacc1 alter bar drop not null;

-- try altering the oid column, should fail
alter table atacc1 alter oid set not null;
alter table atacc1 alter oid drop not null;

-- try creating a view and altering that, should fail
create view myview as select * from atacc1;
alter table myview alter column test drop not null;
alter table myview alter column test set not null;
drop view myview;

drop table atacc1;

-- test inheritance
create table parent (a int);
create table child (b varchar(255)) inherits (parent);

alter table parent alter a set not null;
insert into parent values (NULL);
insert into child (a, b) values (NULL, 'foo');
alter table parent alter a drop not null;
insert into parent values (NULL);
insert into child (a, b) values (NULL, 'foo');
alter table only parent alter a set not null;
alter table child alter a set not null;
delete from parent;
alter table only parent alter a set not null;
insert into parent values (NULL);
alter table child alter a set not null;
insert into child (a, b) values (NULL, 'foo');
delete from child;
alter table child alter a set not null;
insert into child (a, b) values (NULL, 'foo');
drop table child;
drop table parent;

-- test setting and removing default values
create table def_test (
	c1	int4 default 5,
	c2	text default 'initial_default'
);
insert into def_test default values;
alter table def_test alter column c1 drop default;
insert into def_test default values;
alter table def_test alter column c2 drop default;
insert into def_test default values;
alter table def_test alter column c1 set default 10;
alter table def_test alter column c2 set default 'new_default';
insert into def_test default values;
select * from def_test;

-- set defaults to an incorrect type: this should fail
alter table def_test alter column c1 set default 'wrong_datatype';
alter table def_test alter column c2 set default 20;

-- set defaults on a non-existent column: this should fail
alter table def_test alter column c3 set default 30;

-- set defaults on views: we need to create a view, add a rule
-- to allow insertions into it, and then alter the view to add
-- a default
create view def_view_test as select * from def_test;
create rule def_view_test_ins as
	on insert to def_view_test
	do instead insert into def_test select new.*;
insert into def_view_test default values;
alter table def_view_test alter column c1 set default 45;
insert into def_view_test default values;
alter table def_view_test alter column c2 set default 'view_default';
insert into def_view_test default values;
select * from def_view_test;

drop rule def_view_test_ins on def_view_test;
drop view def_view_test;
drop table def_test;

-- alter table / drop column tests
-- try altering system catalogs, should fail
alter table pg_class drop column relname;

-- try altering non-existent table, should fail
alter table nosuchtable drop column bar;

-- test dropping columns
create table atacc1 (a int4 not null, b int4, c int4 not null, d int4) with oids;
insert into atacc1 values (1, 2, 3, 4);
alter table atacc1 drop a;
alter table atacc1 drop a;

-- SELECTs
select * from atacc1;
select * from atacc1 order by a;
select * from atacc1 order by "........pg.dropped.1........";
select * from atacc1 group by a;
select * from atacc1 group by "........pg.dropped.1........";
select atacc1.* from atacc1;
select a from atacc1;
select atacc1.a from atacc1;
select b,c,d from atacc1;
select a,b,c,d from atacc1;
select * from atacc1 where a = 1;
select "........pg.dropped.1........" from atacc1;
select atacc1."........pg.dropped.1........" from atacc1;
select "........pg.dropped.1........",b,c,d from atacc1;
select * from atacc1 where "........pg.dropped.1........" = 1;

-- UPDATEs
update atacc1 set a = 3;
update atacc1 set b = 2 where a = 3;
update atacc1 set "........pg.dropped.1........" = 3;
update atacc1 set b = 2 where "........pg.dropped.1........" = 3;

-- INSERTs
insert into atacc1 values (10, 11, 12, 13);
insert into atacc1 values (default, 11, 12, 13);
insert into atacc1 values (11, 12, 13);
insert into atacc1 (a) values (10);
insert into atacc1 (a) values (default);
insert into atacc1 (a,b,c,d) values (10,11,12,13);
insert into atacc1 (a,b,c,d) values (default,11,12,13);
insert into atacc1 (b,c,d) values (11,12,13);
insert into atacc1 ("........pg.dropped.1........") values (10);
insert into atacc1 ("........pg.dropped.1........") values (default);
insert into atacc1 ("........pg.dropped.1........",b,c,d) values (10,11,12,13);
insert into atacc1 ("........pg.dropped.1........",b,c,d) values (default,11,12,13);

-- DELETEs
delete from atacc1 where a = 3;
delete from atacc1 where "........pg.dropped.1........" = 3;
delete from atacc1;

-- try dropping a non-existent column, should fail
alter table atacc1 drop bar;

-- try dropping the oid column, should succeed
alter table atacc1 drop oid;

-- try dropping the xmin column, should fail
alter table atacc1 drop xmin;

-- try creating a view and altering that, should fail
create view myview as select * from atacc1;
select * from myview;
alter table myview drop d;
drop view myview;

-- test some commands to make sure they fail on the dropped column
analyze atacc1(a);
analyze atacc1("........pg.dropped.1........");
vacuum analyze atacc1(a);
vacuum analyze atacc1("........pg.dropped.1........");
comment on column atacc1.a is 'testing';
comment on column atacc1."........pg.dropped.1........" is 'testing';
alter table atacc1 alter a set storage plain;
alter table atacc1 alter "........pg.dropped.1........" set storage plain;
alter table atacc1 alter a set statistics 0;
alter table atacc1 alter "........pg.dropped.1........" set statistics 0;
alter table atacc1 alter a set default 3;
alter table atacc1 alter "........pg.dropped.1........" set default 3;
alter table atacc1 alter a drop default;
alter table atacc1 alter "........pg.dropped.1........" drop default;
alter table atacc1 alter a set not null;
alter table atacc1 alter "........pg.dropped.1........" set not null;
alter table atacc1 alter a drop not null;
alter table atacc1 alter "........pg.dropped.1........" drop not null;
alter table atacc1 rename a to x;
alter table atacc1 rename "........pg.dropped.1........" to x;
alter table atacc1 add primary key(a);
alter table atacc1 add primary key("........pg.dropped.1........");
alter table atacc1 add unique(a);
alter table atacc1 add unique("........pg.dropped.1........");
alter table atacc1 add check (a > 3);
alter table atacc1 add check ("........pg.dropped.1........" > 3);
create table atacc2 (id int4 unique);
alter table atacc1 add foreign key (a) references atacc2(id);
alter table atacc1 add foreign key ("........pg.dropped.1........") references atacc2(id);
alter table atacc2 add foreign key (id) references atacc1(a);
alter table atacc2 add foreign key (id) references atacc1("........pg.dropped.1........");
drop table atacc2;
create index "testing_idx" on atacc1(a);
create index "testing_idx" on atacc1("........pg.dropped.1........");

-- test create as and select into
insert into atacc1 values (21, 22, 23);
create table attest1 as select * from atacc1;
select * from attest1;
drop table attest1;
select * into attest2 from atacc1;
select * from attest2;
drop table attest2;

-- try dropping all columns
alter table atacc1 drop c;
alter table atacc1 drop d;
alter table atacc1 drop b;
select * from atacc1;

drop table atacc1;

-- test constraint error reporting in presence of dropped columns
create table atacc1 (id serial primary key, value int check (value < 10));
insert into atacc1(value) values (100);
alter table atacc1 drop column value;
alter table atacc1 add column value int check (value < 10);
insert into atacc1(value) values (100);
insert into atacc1(id, value) values (null, 0);
drop table atacc1;

-- test inheritance
create table parent (a int, b int, c int);
insert into parent values (1, 2, 3);
alter table parent drop a;
create table child (d varchar(255)) inherits (parent);
insert into child values (12, 13, 'testing');

select * from parent;
select * from child;
alter table parent drop c;
select * from parent;
select * from child;

drop table child;
drop table parent;

-- check error cases for inheritance column merging
create table parent (a float8, b numeric(10,4), c text collate "C");

create table child (a float4) inherits (parent); -- fail
create table child (b decimal(10,7)) inherits (parent); -- fail
create table child (c text collate "POSIX") inherits (parent); -- fail
create table child (a double precision, b decimal(10,4)) inherits (parent);

drop table child;
drop table parent;

-- test copy in/out
create table attest (a int4, b int4, c int4);
insert into attest values (1,2,3);
alter table attest drop a;
copy attest to stdout;
copy attest(a) to stdout;
copy attest("........pg.dropped.1........") to stdout;
copy attest from stdin;
10	11	12
\.
select * from attest;
copy attest from stdin;
21	22
\.
select * from attest;
copy attest(a) from stdin;
copy attest("........pg.dropped.1........") from stdin;
copy attest(b,c) from stdin;
31	32
\.
select * from attest;
drop table attest;

-- test inheritance

create table dropColumn (a int, b int, e int);
create table dropColumnChild (c int) inherits (dropColumn);
create table dropColumnAnother (d int) inherits (dropColumnChild);

-- these two should fail
alter table dropColumnchild drop column a;
alter table only dropColumnChild drop column b;



-- these three should work
alter table only dropColumn drop column e;
alter table dropColumnChild drop column c;
alter table dropColumn drop column a;

create table renameColumn (a int);
create table renameColumnChild (b int) inherits (renameColumn);
create table renameColumnAnother (c int) inherits (renameColumnChild);

-- these three should fail
alter table renameColumnChild rename column a to d;
alter table only renameColumnChild rename column a to d;
alter table only renameColumn rename column a to d;

-- these should work
alter table renameColumn rename column a to d;
alter table renameColumnChild rename column b to a;

-- these should work
alter table if exists doesnt_exist_tab rename column a to d;
alter table if exists doesnt_exist_tab rename column b to a;

-- this should work
alter table renameColumn add column w int;

-- this should fail
alter table only renameColumn add column x int;


-- Test corner cases in dropping of inherited columns

create table p1 (f1 int, f2 int);
create table c1 (f1 int not null) inherits(p1);

-- should be rejected since c1.f1 is inherited
alter table c1 drop column f1;
-- should work
alter table p1 drop column f1;
-- c1.f1 is still there, but no longer inherited
select f1 from c1;
alter table c1 drop column f1;
select f1 from c1;

drop table p1 cascade;

create table p1 (f1 int, f2 int);
create table c1 () inherits(p1);

-- should be rejected since c1.f1 is inherited
alter table c1 drop column f1;
alter table p1 drop column f1;
-- c1.f1 is dropped now, since there is no local definition for it
select f1 from c1;

drop table p1 cascade;

create table p1 (f1 int, f2 int);
create table c1 () inherits(p1);

-- should be rejected since c1.f1 is inherited
alter table c1 drop column f1;
alter table only p1 drop column f1;
-- c1.f1 is NOT dropped, but must now be considered non-inherited
alter table c1 drop column f1;

drop table p1 cascade;

create table p1 (f1 int, f2 int);
create table c1 (f1 int not null) inherits(p1);

-- should be rejected since c1.f1 is inherited
alter table c1 drop column f1;
alter table only p1 drop column f1;
-- c1.f1 is still there, but no longer inherited
alter table c1 drop column f1;

drop table p1 cascade;

create table p1(id int, name text);
create table p2(id2 int, name text, height int);
create table c1(age int) inherits(p1,p2);
create table gc1() inherits (c1);

select relname, attname, attinhcount, attislocal
from pg_class join pg_attribute on (pg_class.oid = pg_attribute.attrelid)
where relname in ('p1','p2','c1','gc1') and attnum > 0 and not attisdropped
order by relname, attnum;

-- should work
alter table only p1 drop column name;
-- should work. Now c1.name is local and inhcount is 0.
alter table p2 drop column name;
-- should be rejected since its inherited
alter table gc1 drop column name;
-- should work, and drop gc1.name along
alter table c1 drop column name;
-- should fail: column does not exist
alter table gc1 drop column name;
-- should work and drop the attribute in all tables
alter table p2 drop column height;

-- IF EXISTS test
create table dropColumnExists ();
alter table dropColumnExists drop column non_existing; --fail
alter table dropColumnExists drop column if exists non_existing; --succeed

select relname, attname, attinhcount, attislocal
from pg_class join pg_attribute on (pg_class.oid = pg_attribute.attrelid)
where relname in ('p1','p2','c1','gc1') and attnum > 0 and not attisdropped
order by relname, attnum;

drop table p1, p2 cascade;

-- test attinhcount tracking with merged columns

create table depth0();
create table depth1(c text) inherits (depth0);
create table depth2() inherits (depth1);
alter table depth0 add c text;

select attrelid::regclass, attname, attinhcount, attislocal
from pg_attribute
where attnum > 0 and attrelid::regclass in ('depth0', 'depth1', 'depth2')
order by attrelid::regclass::text, attnum;

--
-- Test the ALTER TABLE SET WITH/WITHOUT OIDS command
--
create table altstartwith (col integer) with oids;

insert into altstartwith values (1);

select oid > 0, * from altstartwith;

alter table altstartwith set without oids;

select oid > 0, * from altstartwith; -- fails
select * from altstartwith;

alter table altstartwith set with oids;

select oid > 0, * from altstartwith;

drop table altstartwith;

-- Check inheritance cases
create table altwithoid (col integer) with oids;

-- Inherits parents oid column anyway
create table altinhoid () inherits (altwithoid) without oids;

insert into altinhoid values (1);

select oid > 0, * from altwithoid;
select oid > 0, * from altinhoid;

alter table altwithoid set without oids;

select oid > 0, * from altwithoid; -- fails
select oid > 0, * from altinhoid; -- fails
select * from altwithoid;
select * from altinhoid;

alter table altwithoid set with oids;

select oid > 0, * from altwithoid;
select oid > 0, * from altinhoid;

drop table altwithoid cascade;

create table altwithoid (col integer) without oids;

-- child can have local oid column
create table altinhoid () inherits (altwithoid) with oids;

insert into altinhoid values (1);

select oid > 0, * from altwithoid; -- fails
select oid > 0, * from altinhoid;

alter table altwithoid set with oids;

select oid > 0, * from altwithoid;
select oid > 0, * from altinhoid;

-- the child's local definition should remain
alter table altwithoid set without oids;

select oid > 0, * from altwithoid; -- fails
select oid > 0, * from altinhoid;

drop table altwithoid cascade;

-- test renumbering of child-table columns in inherited operations

create table p1 (f1 int);
create table c1 (f2 text, f3 int) inherits (p1);

alter table p1 add column a1 int check (a1 > 0);
alter table p1 add column f2 text;

insert into p1 values (1,2,'abc');
insert into c1 values(11,'xyz',33,0); -- should fail
insert into c1 values(11,'xyz',33,22);

select * from p1;
update p1 set a1 = a1 + 1, f2 = upper(f2);
select * from p1;

drop table p1 cascade;

-- test that operations with a dropped column do not try to reference
-- its datatype

create domain mytype as text;
create temp table foo (f1 text, f2 mytype, f3 text);

insert into foo values('bb','cc','dd');
select * from foo;

drop domain mytype cascade;

select * from foo;
insert into foo values('qq','rr');
select * from foo;
update foo set f3 = 'zz';
select * from foo;
select f3,max(f1) from foo group by f3;

-- Simple tests for alter table column type
alter table foo alter f1 TYPE integer; -- fails
alter table foo alter f1 TYPE varchar(10);

create table anothertab (atcol1 serial8, atcol2 boolean,
	constraint anothertab_chk check (atcol1 <= 3));

insert into anothertab (atcol1, atcol2) values (default, true);
insert into anothertab (atcol1, atcol2) values (default, false);
select * from anothertab;

alter table anothertab alter column atcol1 type boolean; -- fails
alter table anothertab alter column atcol1 type boolean using atcol1::int; -- fails
alter table anothertab alter column atcol1 type integer;

select * from anothertab;

insert into anothertab (atcol1, atcol2) values (45, null); -- fails
insert into anothertab (atcol1, atcol2) values (default, null);

select * from anothertab;

alter table anothertab alter column atcol2 type text
      using case when atcol2 is true then 'IT WAS TRUE'
                 when atcol2 is false then 'IT WAS FALSE'
                 else 'IT WAS NULL!' end;

select * from anothertab;
alter table anothertab alter column atcol1 type boolean
        using case when atcol1 % 2 = 0 then true else false end; -- fails
alter table anothertab alter column atcol1 drop default;
alter table anothertab alter column atcol1 type boolean
        using case when atcol1 % 2 = 0 then true else false end; -- fails
alter table anothertab drop constraint anothertab_chk;
alter table anothertab drop constraint anothertab_chk; -- fails
alter table anothertab drop constraint IF EXISTS anothertab_chk; -- succeeds

alter table anothertab alter column atcol1 type boolean
        using case when atcol1 % 2 = 0 then true else false end;

select * from anothertab;

drop table anothertab;

create table another (f1 int, f2 text);

insert into another values(1, 'one');
insert into another values(2, 'two');
insert into another values(3, 'three');

select * from another;

alter table another
  alter f1 type text using f2 || ' more',
  alter f2 type bigint using f1 * 10;

select * from another;

drop table another;

-- table's row type
create table tab1 (a int, b text);
create table tab2 (x int, y tab1);
alter table tab1 alter column b type varchar; -- fails

-- Alter column type that's part of a partitioned index
create table at_partitioned (a int, b text) partition by range (a);
create table at_part_1 partition of at_partitioned for values from (0) to (1000);
insert into at_partitioned values (512, '0.123');
create table at_part_2 (b text, a int);
insert into at_part_2 values ('1.234', 1024);
create index on at_partitioned (b);
create index on at_partitioned (a);
\d at_part_1
\d at_part_2
alter table at_partitioned attach partition at_part_2 for values from (1000) to (2000);
\d at_part_2
alter table at_partitioned alter column b type numeric using b::numeric;
\d at_part_1
\d at_part_2

-- disallow recursive containment of row types
create temp table recur1 (f1 int);
alter table recur1 add column f2 recur1; -- fails
alter table recur1 add column f2 recur1[]; -- fails
create domain array_of_recur1 as recur1[];
alter table recur1 add column f2 array_of_recur1; -- fails
create temp table recur2 (f1 int, f2 recur1);
alter table recur1 add column f2 recur2; -- fails
alter table recur1 add column f2 int;
alter table recur1 alter column f2 type recur2; -- fails

-- SET STORAGE may need to add a TOAST table
create table test_storage (a text);
alter table test_storage alter a set storage plain;
alter table test_storage add b int default 0; -- rewrite table to remove its TOAST table
alter table test_storage alter a set storage extended; -- re-add TOAST table

select reltoastrelid <> 0 as has_toast_table
from pg_class
where oid = 'test_storage'::regclass;

-- ALTER COLUMN TYPE with a check constraint and a child table (bug #13779)
CREATE TABLE test_inh_check (a float check (a > 10.2), b float);
CREATE TABLE test_inh_check_child() INHERITS(test_inh_check);
\d test_inh_check
\d test_inh_check_child
select relname, conname, coninhcount, conislocal, connoinherit
  from pg_constraint c, pg_class r
  where relname like 'test_inh_check%' and c.conrelid = r.oid
  order by 1, 2;
ALTER TABLE test_inh_check ALTER COLUMN a TYPE numeric;
\d test_inh_check
\d test_inh_check_child
select relname, conname, coninhcount, conislocal, connoinherit
  from pg_constraint c, pg_class r
  where relname like 'test_inh_check%' and c.conrelid = r.oid
  order by 1, 2;
-- also try noinherit, local, and local+inherited cases
ALTER TABLE test_inh_check ADD CONSTRAINT bnoinherit CHECK (b > 100) NO INHERIT;
ALTER TABLE test_inh_check_child ADD CONSTRAINT blocal CHECK (b < 1000);
ALTER TABLE test_inh_check_child ADD CONSTRAINT bmerged CHECK (b > 1);
ALTER TABLE test_inh_check ADD CONSTRAINT bmerged CHECK (b > 1);
\d test_inh_check
\d test_inh_check_child
select relname, conname, coninhcount, conislocal, connoinherit
  from pg_constraint c, pg_class r
  where relname like 'test_inh_check%' and c.conrelid = r.oid
  order by 1, 2;
ALTER TABLE test_inh_check ALTER COLUMN b TYPE numeric;
\d test_inh_check
\d test_inh_check_child
select relname, conname, coninhcount, conislocal, connoinherit
  from pg_constraint c, pg_class r
  where relname like 'test_inh_check%' and c.conrelid = r.oid
  order by 1, 2;

-- ALTER COLUMN TYPE with different schema in children
-- Bug at https://postgr.es/m/20170102225618.GA10071@telsasoft.com
CREATE TABLE test_type_diff (f1 int);
CREATE TABLE test_type_diff_c (extra smallint) INHERITS (test_type_diff);
ALTER TABLE test_type_diff ADD COLUMN f2 int;
INSERT INTO test_type_diff_c VALUES (1, 2, 3);
ALTER TABLE test_type_diff ALTER COLUMN f2 TYPE bigint USING f2::bigint;

CREATE TABLE test_type_diff2 (int_two int2, int_four int4, int_eight int8);
CREATE TABLE test_type_diff2_c1 (int_four int4, int_eight int8, int_two int2);
CREATE TABLE test_type_diff2_c2 (int_eight int8, int_two int2, int_four int4);
CREATE TABLE test_type_diff2_c3 (int_two int2, int_four int4, int_eight int8);
ALTER TABLE test_type_diff2_c1 INHERIT test_type_diff2;
ALTER TABLE test_type_diff2_c2 INHERIT test_type_diff2;
ALTER TABLE test_type_diff2_c3 INHERIT test_type_diff2;
INSERT INTO test_type_diff2_c1 VALUES (1, 2, 3);
INSERT INTO test_type_diff2_c2 VALUES (4, 5, 6);
INSERT INTO test_type_diff2_c3 VALUES (7, 8, 9);
ALTER TABLE test_type_diff2 ALTER COLUMN int_four TYPE int8 USING int_four::int8;
-- whole-row references are disallowed
ALTER TABLE test_type_diff2 ALTER COLUMN int_four TYPE int4 USING (pg_column_size(test_type_diff2));

-- check for rollback of ANALYZE corrupting table property flags (bug #11638)
CREATE TABLE check_fk_presence_1 (id int PRIMARY KEY, t text);
CREATE TABLE check_fk_presence_2 (id int REFERENCES check_fk_presence_1, t text);
BEGIN;
ALTER TABLE check_fk_presence_2 DROP CONSTRAINT check_fk_presence_2_id_fkey;
ANALYZE check_fk_presence_2;
ROLLBACK;
\d check_fk_presence_2
DROP TABLE check_fk_presence_1, check_fk_presence_2;

-- check column addition within a view (bug #14876)
create table at_base_table(id int, stuff text);
insert into at_base_table values (23, 'skidoo');
create view at_view_1 as select * from at_base_table bt;
create view at_view_2 as select *, to_json(v1) as j from at_view_1 v1;
\d+ at_view_1
\d+ at_view_2
explain (verbose, costs off) select * from at_view_2;
select * from at_view_2;

create or replace view at_view_1 as select *, 2+2 as more from at_base_table bt;
\d+ at_view_1
\d+ at_view_2
explain (verbose, costs off) select * from at_view_2;
select * from at_view_2;

drop view at_view_2;
drop view at_view_1;
drop table at_base_table;

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
        from pg_locks
        where transactionid = txid_current()::integer)
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

begin; alter table alterlock alter column f2 set default 'x';
select * from my_locks order by 1;
rollback;

begin;
create trigger ttdummy
	before delete or update on alterlock
	for each row
	execute procedure
	ttdummy (1, 1);
select * from my_locks order by 1;
rollback;

begin;
select * from my_locks order by 1;
alter table alterlock2 add foreign key (f1) references alterlock (f1);
select * from my_locks order by 1;
rollback;

begin;
alter table alterlock2
add constraint alterlock2nv foreign key (f1) references alterlock (f1) NOT VALID;
select * from my_locks order by 1;
commit;
begin;
alter table alterlock2 validate constraint alterlock2nv;
select * from my_locks order by 1;
rollback;

create or replace view my_locks as
select case when c.relname like 'pg_toast%' then 'pg_toast' else c.relname end, max(mode::lockmodes) as max_lockmode
from pg_locks l join pg_class c on l.relation = c.oid
where virtualtransaction = (
        select virtualtransaction
        from pg_locks
        where transactionid = txid_current()::integer)
and locktype = 'relation'
and relnamespace != (select oid from pg_namespace where nspname = 'pg_catalog')
and c.relname = 'my_locks'
group by c.relname;

-- raise exception
alter table my_locks set (autovacuum_enabled = false);
alter view my_locks set (autovacuum_enabled = false);
alter table my_locks reset (autovacuum_enabled);
alter view my_locks reset (autovacuum_enabled);

begin;
alter view my_locks set (security_barrier=off);
select * from my_locks order by 1;
alter view my_locks reset (security_barrier);
rollback;

-- this test intentionally applies the ALTER TABLE command against a view, but
-- uses a view option so we expect this to succeed. This form of SQL is
-- accepted for historical reasons, as shown in the docs for ALTER VIEW
begin;
alter table my_locks set (security_barrier=off);
select * from my_locks order by 1;
alter table my_locks reset (security_barrier);
rollback;

-- cleanup
drop table alterlock2;
drop table alterlock;
drop view my_locks;
drop type lockmodes;

--
-- alter function
--
create function test_strict(text) returns text as
    'select coalesce($1, ''got passed a null'');'
    language sql returns null on null input;
select test_strict(NULL);
alter function test_strict(text) called on null input;
select test_strict(NULL);

create function non_strict(text) returns text as
    'select coalesce($1, ''got passed a null'');'
    language sql called on null input;
select non_strict(NULL);
alter function non_strict(text) returns null on null input;
select non_strict(NULL);

--
-- alter object set schema
--

create schema alter1;
create schema alter2;

create table alter1.t1(f1 serial primary key, f2 int check (f2 > 0));

create view alter1.v1 as select * from alter1.t1;

create function alter1.plus1(int) returns int as 'select $1+1' language sql;

create domain alter1.posint integer check (value > 0);

create type alter1.ctype as (f1 int, f2 text);

create function alter1.same(alter1.ctype, alter1.ctype) returns boolean language sql
as 'select $1.f1 is not distinct from $2.f1 and $1.f2 is not distinct from $2.f2';

create operator alter1.=(procedure = alter1.same, leftarg  = alter1.ctype, rightarg = alter1.ctype);

create operator class alter1.ctype_hash_ops default for type alter1.ctype using hash as
  operator 1 alter1.=(alter1.ctype, alter1.ctype);

create conversion alter1.ascii_to_utf8 for 'sql_ascii' to 'utf8' from ascii_to_utf8;

create text search parser alter1.prs(start = prsd_start, gettoken = prsd_nexttoken, end = prsd_end, lextypes = prsd_lextype);
create text search configuration alter1.cfg(parser = alter1.prs);
create text search template alter1.tmpl(init = dsimple_init, lexize = dsimple_lexize);
create text search dictionary alter1.dict(template = alter1.tmpl);

insert into alter1.t1(f2) values(11);
insert into alter1.t1(f2) values(12);

alter table alter1.t1 set schema alter1; -- no-op, same schema
alter table alter1.t1 set schema alter2;
alter table alter1.v1 set schema alter2;
alter function alter1.plus1(int) set schema alter2;
alter domain alter1.posint set schema alter2;
alter operator class alter1.ctype_hash_ops using hash set schema alter2;
alter operator family alter1.ctype_hash_ops using hash set schema alter2;
alter operator alter1.=(alter1.ctype, alter1.ctype) set schema alter2;
alter function alter1.same(alter1.ctype, alter1.ctype) set schema alter2;
alter type alter1.ctype set schema alter1; -- no-op, same schema
alter type alter1.ctype set schema alter2;
alter conversion alter1.ascii_to_utf8 set schema alter2;
alter text search parser alter1.prs set schema alter2;
alter text search configuration alter1.cfg set schema alter2;
alter text search template alter1.tmpl set schema alter2;
alter text search dictionary alter1.dict set schema alter2;

-- this should succeed because nothing is left in alter1
drop schema alter1;

insert into alter2.t1(f2) values(13);
insert into alter2.t1(f2) values(14);

select * from alter2.t1;

select * from alter2.v1;

select alter2.plus1(41);

-- clean up
drop schema alter2 cascade;

--
-- composite types
--

CREATE TYPE test_type AS (a int);
\d test_type

ALTER TYPE nosuchtype ADD ATTRIBUTE b text; -- fails

ALTER TYPE test_type ADD ATTRIBUTE b text;
\d test_type

ALTER TYPE test_type ADD ATTRIBUTE b text; -- fails

ALTER TYPE test_type ALTER ATTRIBUTE b SET DATA TYPE varchar;
\d test_type

ALTER TYPE test_type ALTER ATTRIBUTE b SET DATA TYPE integer;
\d test_type

ALTER TYPE test_type DROP ATTRIBUTE b;
\d test_type

ALTER TYPE test_type DROP ATTRIBUTE c; -- fails

ALTER TYPE test_type DROP ATTRIBUTE IF EXISTS c;

ALTER TYPE test_type DROP ATTRIBUTE a, ADD ATTRIBUTE d boolean;
\d test_type

ALTER TYPE test_type RENAME ATTRIBUTE a TO aa;
ALTER TYPE test_type RENAME ATTRIBUTE d TO dd;
\d test_type

DROP TYPE test_type;

CREATE TYPE test_type1 AS (a int, b text);
CREATE TABLE test_tbl1 (x int, y test_type1);
ALTER TYPE test_type1 ALTER ATTRIBUTE b TYPE varchar; -- fails

CREATE TYPE test_type2 AS (a int, b text);
CREATE TABLE test_tbl2 OF test_type2;
CREATE TABLE test_tbl2_subclass () INHERITS (test_tbl2);
\d test_type2
\d test_tbl2

ALTER TYPE test_type2 ADD ATTRIBUTE c text; -- fails
ALTER TYPE test_type2 ADD ATTRIBUTE c text CASCADE;
\d test_type2
\d test_tbl2

ALTER TYPE test_type2 ALTER ATTRIBUTE b TYPE varchar; -- fails
ALTER TYPE test_type2 ALTER ATTRIBUTE b TYPE varchar CASCADE;
\d test_type2
\d test_tbl2

ALTER TYPE test_type2 DROP ATTRIBUTE b; -- fails
ALTER TYPE test_type2 DROP ATTRIBUTE b CASCADE;
\d test_type2
\d test_tbl2

ALTER TYPE test_type2 RENAME ATTRIBUTE a TO aa; -- fails
ALTER TYPE test_type2 RENAME ATTRIBUTE a TO aa CASCADE;
\d test_type2
\d test_tbl2
\d test_tbl2_subclass

DROP TABLE test_tbl2_subclass;

CREATE TYPE test_typex AS (a int, b text);
CREATE TABLE test_tblx (x int, y test_typex check ((y).a > 0));
ALTER TYPE test_typex DROP ATTRIBUTE a; -- fails
ALTER TYPE test_typex DROP ATTRIBUTE a CASCADE;
\d test_tblx
DROP TABLE test_tblx;
DROP TYPE test_typex;

-- This test isn't that interesting on its own, but the purpose is to leave
-- behind a table to test pg_upgrade with. The table has a composite type
-- column in it, and the composite type has a dropped attribute.
CREATE TYPE test_type3 AS (a int);
CREATE TABLE test_tbl3 (c) AS SELECT '(1)'::test_type3;
ALTER TYPE test_type3 DROP ATTRIBUTE a, ADD ATTRIBUTE b int;

CREATE TYPE test_type_empty AS ();
DROP TYPE test_type_empty;

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

-- make sure we can drop a constraint on the parent but it remains on the child
CREATE TABLE test_drop_constr_parent (c text CHECK (c IS NOT NULL));
CREATE TABLE test_drop_constr_child () INHERITS (test_drop_constr_parent);
ALTER TABLE ONLY test_drop_constr_parent DROP CONSTRAINT "test_drop_constr_parent_c_check";
-- should fail
INSERT INTO test_drop_constr_child (c) VALUES (NULL);
DROP TABLE test_drop_constr_parent CASCADE;

--
-- IF EXISTS test
--
ALTER TABLE IF EXISTS tt8 ADD COLUMN f int;
ALTER TABLE IF EXISTS tt8 ADD CONSTRAINT xxx PRIMARY KEY(f);
ALTER TABLE IF EXISTS tt8 ADD CHECK (f BETWEEN 0 AND 10);
ALTER TABLE IF EXISTS tt8 ALTER COLUMN f SET DEFAULT 0;
ALTER TABLE IF EXISTS tt8 RENAME COLUMN f TO f1;
ALTER TABLE IF EXISTS tt8 SET SCHEMA alter2;

CREATE TABLE tt8(a int);
CREATE SCHEMA alter2;

ALTER TABLE IF EXISTS tt8 ADD COLUMN f int;
ALTER TABLE IF EXISTS tt8 ADD CONSTRAINT xxx PRIMARY KEY(f);
ALTER TABLE IF EXISTS tt8 ADD CHECK (f BETWEEN 0 AND 10);
ALTER TABLE IF EXISTS tt8 ALTER COLUMN f SET DEFAULT 0;
ALTER TABLE IF EXISTS tt8 RENAME COLUMN f TO f1;
ALTER TABLE IF EXISTS tt8 SET SCHEMA alter2;

\d alter2.tt8

DROP TABLE alter2.tt8;
DROP SCHEMA alter2;

--
-- Check conflicts between index and CHECK constraint names
--
CREATE TABLE tt9(c integer);
ALTER TABLE tt9 ADD CHECK(c > 1);
ALTER TABLE tt9 ADD CHECK(c > 2);  -- picks nonconflicting name
ALTER TABLE tt9 ADD CONSTRAINT foo CHECK(c > 3);
ALTER TABLE tt9 ADD CONSTRAINT foo CHECK(c > 4);  -- fail, dup name
ALTER TABLE tt9 ADD UNIQUE(c);
ALTER TABLE tt9 ADD UNIQUE(c);  -- picks nonconflicting name
ALTER TABLE tt9 ADD CONSTRAINT tt9_c_key UNIQUE(c);  -- fail, dup name
ALTER TABLE tt9 ADD CONSTRAINT foo UNIQUE(c);  -- fail, dup name
ALTER TABLE tt9 ADD CONSTRAINT tt9_c_key CHECK(c > 5);  -- fail, dup name
ALTER TABLE tt9 ADD CONSTRAINT tt9_c_key2 CHECK(c > 6);
ALTER TABLE tt9 ADD UNIQUE(c);  -- picks nonconflicting name
\d tt9
DROP TABLE tt9;


-- Check that comments on constraints and indexes are not lost at ALTER TABLE.
CREATE TABLE comment_test (
  id int,
  positive_col int CHECK (positive_col > 0),
  indexed_col int,
  CONSTRAINT comment_test_pk PRIMARY KEY (id));
CREATE INDEX comment_test_index ON comment_test(indexed_col);

COMMENT ON COLUMN comment_test.id IS 'Column ''id'' on comment_test';
COMMENT ON INDEX comment_test_index IS 'Simple index on comment_test';
COMMENT ON CONSTRAINT comment_test_positive_col_check ON comment_test IS 'CHECK constraint on comment_test.positive_col';
COMMENT ON CONSTRAINT comment_test_pk ON comment_test IS 'PRIMARY KEY constraint of comment_test';
COMMENT ON INDEX comment_test_pk IS 'Index backing the PRIMARY KEY of comment_test';

SELECT col_description('comment_test'::regclass, 1) as comment;
SELECT indexrelid::regclass::text as index, obj_description(indexrelid, 'pg_class') as comment FROM pg_index where indrelid = 'comment_test'::regclass ORDER BY 1, 2;
SELECT conname as constraint, obj_description(oid, 'pg_constraint') as comment FROM pg_constraint where conrelid = 'comment_test'::regclass ORDER BY 1, 2;

-- Change the datatype of all the columns. ALTER TABLE is optimized to not
-- rebuild an index if the new data type is binary compatible with the old
-- one. Check do a dummy ALTER TABLE that doesn't change the datatype
-- first, to test that no-op codepath, and another one that does.
ALTER TABLE comment_test ALTER COLUMN indexed_col SET DATA TYPE int;
ALTER TABLE comment_test ALTER COLUMN indexed_col SET DATA TYPE text;
ALTER TABLE comment_test ALTER COLUMN id SET DATA TYPE int;
ALTER TABLE comment_test ALTER COLUMN id SET DATA TYPE text;
ALTER TABLE comment_test ALTER COLUMN positive_col SET DATA TYPE int;
ALTER TABLE comment_test ALTER COLUMN positive_col SET DATA TYPE bigint;

-- Check that the comments are intact.
SELECT col_description('comment_test'::regclass, 1) as comment;
SELECT indexrelid::regclass::text as index, obj_description(indexrelid, 'pg_class') as comment FROM pg_index where indrelid = 'comment_test'::regclass ORDER BY 1, 2;
SELECT conname as constraint, obj_description(oid, 'pg_constraint') as comment FROM pg_constraint where conrelid = 'comment_test'::regclass ORDER BY 1, 2;

-- Check compatibility for foreign keys and comments. This is done
-- separately as rebuilding the column type of the parent leads
-- to an error and would reduce the test scope.
CREATE TABLE comment_test_child (
  id text CONSTRAINT comment_test_child_fk REFERENCES comment_test);
CREATE INDEX comment_test_child_fk ON comment_test_child(id);
COMMENT ON COLUMN comment_test_child.id IS 'Column ''id'' on comment_test_child';
COMMENT ON INDEX comment_test_child_fk IS 'Index backing the FOREIGN KEY of comment_test_child';
COMMENT ON CONSTRAINT comment_test_child_fk ON comment_test_child IS 'FOREIGN KEY constraint of comment_test_child';

-- Change column type of parent
ALTER TABLE comment_test ALTER COLUMN id SET DATA TYPE text;
ALTER TABLE comment_test ALTER COLUMN id SET DATA TYPE int USING id::integer;

-- Comments should be intact
SELECT col_description('comment_test_child'::regclass, 1) as comment;
SELECT indexrelid::regclass::text as index, obj_description(indexrelid, 'pg_class') as comment FROM pg_index where indrelid = 'comment_test_child'::regclass ORDER BY 1, 2;
SELECT conname as constraint, obj_description(oid, 'pg_constraint') as comment FROM pg_constraint where conrelid = 'comment_test_child'::regclass ORDER BY 1, 2;

-- Check that we map relation oids to filenodes and back correctly.  Only
-- display bad mappings so the test output doesn't change all the time.  A
-- filenode function call can return NULL for a relation dropped concurrently
-- with the call's surrounding query, so ignore a NULL mapped_oid for
-- relations that no longer exist after all calls finish.
CREATE TEMP TABLE filenode_mapping AS
SELECT
    oid, mapped_oid, reltablespace, relfilenode, relname
FROM pg_class,
    pg_filenode_relation(reltablespace, pg_relation_filenode(oid)) AS mapped_oid
WHERE relkind IN ('r', 'i', 'S', 't', 'm') AND mapped_oid IS DISTINCT FROM oid;

SELECT m.* FROM filenode_mapping m LEFT JOIN pg_class c ON c.oid = m.oid
WHERE c.oid IS NOT NULL OR m.mapped_oid IS NOT NULL;

-- Checks on creating and manipulation of user defined relations in
-- pg_catalog.
--
-- XXX: It would be useful to add checks around trying to manipulate
-- catalog tables, but that might have ugly consequences when run
-- against an existing server with allow_system_table_mods = on.

SHOW allow_system_table_mods;
-- disallowed because of search_path issues with pg_dump
CREATE TABLE pg_catalog.new_system_table();
-- instead create in public first, move to catalog
CREATE TABLE new_system_table(id serial primary key, othercol text);
ALTER TABLE new_system_table SET SCHEMA pg_catalog;

-- XXX: it's currently impossible to move relations out of pg_catalog
ALTER TABLE new_system_table SET SCHEMA public;
-- move back, will be ignored -- already there
ALTER TABLE new_system_table SET SCHEMA pg_catalog;
ALTER TABLE new_system_table RENAME TO old_system_table;
CREATE INDEX old_system_table__othercol ON old_system_table (othercol);
INSERT INTO old_system_table(othercol) VALUES ('somedata'), ('otherdata');
UPDATE old_system_table SET id = -id;
DELETE FROM old_system_table WHERE othercol = 'somedata';
TRUNCATE old_system_table;
ALTER TABLE old_system_table DROP CONSTRAINT new_system_table_pkey;
ALTER TABLE old_system_table DROP COLUMN othercol;
DROP TABLE old_system_table;

-- set logged
CREATE UNLOGGED TABLE unlogged1(f1 SERIAL PRIMARY KEY, f2 TEXT);
-- check relpersistence of an unlogged table
SELECT relname, relkind, relpersistence FROM pg_class WHERE relname ~ '^unlogged1'
UNION ALL
SELECT 'toast table', t.relkind, t.relpersistence FROM pg_class r JOIN pg_class t ON t.oid = r.reltoastrelid WHERE r.relname ~ '^unlogged1'
UNION ALL
SELECT 'toast index', ri.relkind, ri.relpersistence FROM pg_class r join pg_class t ON t.oid = r.reltoastrelid JOIN pg_index i ON i.indrelid = t.oid JOIN pg_class ri ON ri.oid = i.indexrelid WHERE r.relname ~ '^unlogged1'
ORDER BY relname;
CREATE UNLOGGED TABLE unlogged2(f1 SERIAL PRIMARY KEY, f2 INTEGER REFERENCES unlogged1); -- foreign key
CREATE UNLOGGED TABLE unlogged3(f1 SERIAL PRIMARY KEY, f2 INTEGER REFERENCES unlogged3); -- self-referencing foreign key
ALTER TABLE unlogged3 SET LOGGED; -- skip self-referencing foreign key
ALTER TABLE unlogged2 SET LOGGED; -- fails because a foreign key to an unlogged table exists
ALTER TABLE unlogged1 SET LOGGED;
-- check relpersistence of an unlogged table after changing to permanent
SELECT relname, relkind, relpersistence FROM pg_class WHERE relname ~ '^unlogged1'
UNION ALL
SELECT 'toast table', t.relkind, t.relpersistence FROM pg_class r JOIN pg_class t ON t.oid = r.reltoastrelid WHERE r.relname ~ '^unlogged1'
UNION ALL
SELECT 'toast index', ri.relkind, ri.relpersistence FROM pg_class r join pg_class t ON t.oid = r.reltoastrelid JOIN pg_index i ON i.indrelid = t.oid JOIN pg_class ri ON ri.oid = i.indexrelid WHERE r.relname ~ '^unlogged1'
ORDER BY relname;
ALTER TABLE unlogged1 SET LOGGED; -- silently do nothing
DROP TABLE unlogged3;
DROP TABLE unlogged2;
DROP TABLE unlogged1;
-- set unlogged
CREATE TABLE logged1(f1 SERIAL PRIMARY KEY, f2 TEXT);
-- check relpersistence of a permanent table
SELECT relname, relkind, relpersistence FROM pg_class WHERE relname ~ '^logged1'
UNION ALL
SELECT 'toast table', t.relkind, t.relpersistence FROM pg_class r JOIN pg_class t ON t.oid = r.reltoastrelid WHERE r.relname ~ '^logged1'
UNION ALL
SELECT 'toast index', ri.relkind, ri.relpersistence FROM pg_class r join pg_class t ON t.oid = r.reltoastrelid JOIN pg_index i ON i.indrelid = t.oid JOIN pg_class ri ON ri.oid = i.indexrelid WHERE r.relname ~ '^logged1'
ORDER BY relname;
CREATE TABLE logged2(f1 SERIAL PRIMARY KEY, f2 INTEGER REFERENCES logged1); -- foreign key
CREATE TABLE logged3(f1 SERIAL PRIMARY KEY, f2 INTEGER REFERENCES logged3); -- self-referencing foreign key
ALTER TABLE logged1 SET UNLOGGED; -- fails because a foreign key from a permanent table exists
ALTER TABLE logged3 SET UNLOGGED; -- skip self-referencing foreign key
ALTER TABLE logged2 SET UNLOGGED;
ALTER TABLE logged1 SET UNLOGGED;
-- check relpersistence of a permanent table after changing to unlogged
SELECT relname, relkind, relpersistence FROM pg_class WHERE relname ~ '^logged1'
UNION ALL
SELECT 'toast table', t.relkind, t.relpersistence FROM pg_class r JOIN pg_class t ON t.oid = r.reltoastrelid WHERE r.relname ~ '^logged1'
UNION ALL
SELECT 'toast index', ri.relkind, ri.relpersistence FROM pg_class r join pg_class t ON t.oid = r.reltoastrelid JOIN pg_index i ON i.indrelid = t.oid JOIN pg_class ri ON ri.oid = i.indexrelid WHERE r.relname ~ '^logged1'
ORDER BY relname;
ALTER TABLE logged1 SET UNLOGGED; -- silently do nothing
DROP TABLE logged3;
DROP TABLE logged2;
DROP TABLE logged1;

-- test ADD COLUMN IF NOT EXISTS
CREATE TABLE test_add_column(c1 integer);
\d test_add_column
ALTER TABLE test_add_column
	ADD COLUMN c2 integer;
\d test_add_column
ALTER TABLE test_add_column
	ADD COLUMN c2 integer; -- fail because c2 already exists
ALTER TABLE ONLY test_add_column
	ADD COLUMN c2 integer; -- fail because c2 already exists
\d test_add_column
ALTER TABLE test_add_column
	ADD COLUMN IF NOT EXISTS c2 integer; -- skipping because c2 already exists
ALTER TABLE ONLY test_add_column
	ADD COLUMN IF NOT EXISTS c2 integer; -- skipping because c2 already exists
\d test_add_column
ALTER TABLE test_add_column
	ADD COLUMN c2 integer, -- fail because c2 already exists
	ADD COLUMN c3 integer;
\d test_add_column
ALTER TABLE test_add_column
	ADD COLUMN IF NOT EXISTS c2 integer, -- skipping because c2 already exists
	ADD COLUMN c3 integer; -- fail because c3 already exists
\d test_add_column
ALTER TABLE test_add_column
	ADD COLUMN IF NOT EXISTS c2 integer, -- skipping because c2 already exists
	ADD COLUMN IF NOT EXISTS c3 integer; -- skipping because c3 already exists
\d test_add_column
ALTER TABLE test_add_column
	ADD COLUMN IF NOT EXISTS c2 integer, -- skipping because c2 already exists
	ADD COLUMN IF NOT EXISTS c3 integer, -- skipping because c3 already exists
	ADD COLUMN c4 integer;
\d test_add_column
DROP TABLE test_add_column;

-- unsupported constraint types for partitioned tables
CREATE TABLE partitioned (
	a int,
	b int
) PARTITION BY RANGE (a, (a+b+1));
ALTER TABLE partitioned ADD EXCLUDE USING gist (a WITH &&);

-- cannot drop column that is part of the partition key
ALTER TABLE partitioned DROP COLUMN a;
ALTER TABLE partitioned ALTER COLUMN a TYPE char(5);
ALTER TABLE partitioned DROP COLUMN b;
ALTER TABLE partitioned ALTER COLUMN b TYPE char(5);

-- partitioned table cannot participate in regular inheritance
CREATE TABLE nonpartitioned (
	a int,
	b int
);
ALTER TABLE partitioned INHERIT nonpartitioned;
ALTER TABLE nonpartitioned INHERIT partitioned;

-- cannot add NO INHERIT constraint to partitioned tables
ALTER TABLE partitioned ADD CONSTRAINT chk_a CHECK (a > 0) NO INHERIT;

DROP TABLE partitioned, nonpartitioned;

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
ALTER TABLE list_parted ATTACH PARTITION child FOR VALUES IN (1);
ALTER TABLE list_parted ATTACH PARTITION parent FOR VALUES IN (1);
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
CREATE TABLE fail_part (a int);
ALTER TABLE list_parted ATTACH PARTITION fail_part FOR VALUES IN (1);

ALTER TABLE list_parted SET WITHOUT OIDS;
ALTER TABLE fail_part SET WITH OIDS;
ALTER TABLE list_parted ATTACH PARTITION fail_part FOR VALUES IN (1);
DROP TABLE fail_part;

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
ALTER TABLE list_parted ATTACH PARTITION fail_part FOR VALUES IN (1);
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
ALTER TABLE ONLY list_parted2 DROP CONSTRAINT check_b;

-- It's alright though, if no partitions are yet created
CREATE TABLE parted_no_parts (a int) PARTITION BY LIST (a);
ALTER TABLE ONLY parted_no_parts ALTER a SET NOT NULL;
ALTER TABLE ONLY parted_no_parts ADD CONSTRAINT check_a CHECK (a > 0);
ALTER TABLE ONLY parted_no_parts ALTER a DROP NOT NULL;
ALTER TABLE ONLY parted_no_parts DROP CONSTRAINT check_a;
DROP TABLE parted_no_parts;

-- cannot drop inherited NOT NULL or check constraints from partition
ALTER TABLE list_parted2 ALTER b SET NOT NULL, ADD CONSTRAINT check_a2 CHECK (a > 0);
ALTER TABLE part_2 ALTER b DROP NOT NULL;
ALTER TABLE part_2 DROP CONSTRAINT check_a2;

-- Doesn't make sense to add NO INHERIT constraints on partitioned tables
ALTER TABLE list_parted2 add constraint check_b2 check (b <> 'zz') NO INHERIT;

-- check that a partition cannot participate in regular inheritance
CREATE TABLE inh_test () INHERITS (part_2);
CREATE TABLE inh_test (LIKE part_2);
ALTER TABLE inh_test INHERIT part_2;
ALTER TABLE part_2 INHERIT inh_test;

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
ALTER TABLE attmp ALTER COLUMN i RESET (n_distinct_inherited);
ANALYZE attmp;
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
create table at_test_sql_partop (a int) partition by range (a at_test_sql_partop);
create table at_test_sql_partop_1 (a int);
alter table at_test_sql_partop attach partition at_test_sql_partop_1 for values from (0) to (10);
drop table at_test_sql_partop;
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