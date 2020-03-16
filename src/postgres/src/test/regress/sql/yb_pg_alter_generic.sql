--
-- Test for ALTER some_object {RENAME TO, OWNER TO, SET SCHEMA}
--
-- Based on "alter_generic" test.
-- The following sections are missing as they are not supported yet:
-- * ALTER FUNCTION / AGGREGATE
-- * ALTER CONVERSION
-- * ALTER FOREIGN DATA
-- * ALTER LANGUAGE
-- * ALTER OPERATOR CLASS
-- * ALTER STATISTICS
-- * ALTER TEXT SEARCH DICTIONARY / CONFIGURATION / TEMPLATE / PARSER
--

-- Clean up in case a prior regression run failed
SET client_min_messages TO 'warning';

DROP ROLE IF EXISTS regress_alter_generic_user1;
DROP ROLE IF EXISTS regress_alter_generic_user2;
DROP ROLE IF EXISTS regress_alter_generic_user3;

RESET client_min_messages;

CREATE USER regress_alter_generic_user3;
CREATE USER regress_alter_generic_user2;
CREATE USER regress_alter_generic_user1 IN ROLE regress_alter_generic_user3;

CREATE SCHEMA alt_nsp1;
CREATE SCHEMA alt_nsp2;

GRANT ALL ON SCHEMA alt_nsp1, alt_nsp2 TO public;

SET search_path = alt_nsp1, public;

--
-- Operator
--
SET SESSION AUTHORIZATION regress_alter_generic_user1;

CREATE OPERATOR @-@ ( leftarg = int4, rightarg = int4, procedure = int4mi );
CREATE OPERATOR @+@ ( leftarg = int4, rightarg = int4, procedure = int4pl );

ALTER OPERATOR @+@(int4, int4) OWNER TO regress_alter_generic_user2;  -- failed (no role membership)
ALTER OPERATOR @+@(int4, int4) OWNER TO regress_alter_generic_user3;  -- OK
ALTER OPERATOR @-@(int4, int4) SET SCHEMA alt_nsp2;           -- OK

SET SESSION AUTHORIZATION regress_alter_generic_user2;

CREATE OPERATOR @-@ ( leftarg = int4, rightarg = int4, procedure = int4mi );

ALTER OPERATOR @+@(int4, int4) OWNER TO regress_alter_generic_user2;  -- failed (not owner)
ALTER OPERATOR @-@(int4, int4) OWNER TO regress_alter_generic_user3;  -- failed (no role membership)
ALTER OPERATOR @+@(int4, int4) SET SCHEMA alt_nsp2;   -- failed (not owner)
-- can't test this: the error message includes the raw oid of namespace
-- ALTER OPERATOR @-@(int4, int4) SET SCHEMA alt_nsp2;   -- failed (name conflict)

RESET SESSION AUTHORIZATION;

SELECT n.nspname, oprname, a.rolname,
    oprleft::regtype, oprright::regtype, oprcode::regproc
  FROM pg_operator o, pg_namespace n, pg_authid a
  WHERE o.oprnamespace = n.oid AND o.oprowner = a.oid
    AND n.nspname IN ('alt_nsp1', 'alt_nsp2')
  ORDER BY nspname, oprname;

--
-- OpFamily and OpClass
--
CREATE OPERATOR FAMILY alt_opf1 USING hash;
CREATE OPERATOR FAMILY alt_opf2 USING hash;
ALTER OPERATOR FAMILY alt_opf1 USING hash OWNER TO regress_alter_generic_user1;
ALTER OPERATOR FAMILY alt_opf2 USING hash OWNER TO regress_alter_generic_user1;

SET SESSION AUTHORIZATION regress_alter_generic_user1;

ALTER OPERATOR FAMILY alt_opf1 USING hash RENAME TO alt_opf2;  -- failed (name conflict)
ALTER OPERATOR FAMILY alt_opf1 USING hash RENAME TO alt_opf3;  -- OK
ALTER OPERATOR FAMILY alt_opf2 USING hash OWNER TO regress_alter_generic_user2;  -- failed (no role membership)
ALTER OPERATOR FAMILY alt_opf2 USING hash OWNER TO regress_alter_generic_user3;  -- OK
ALTER OPERATOR FAMILY alt_opf2 USING hash SET SCHEMA alt_nsp2;  -- OK

RESET SESSION AUTHORIZATION;

CREATE OPERATOR FAMILY alt_opf1 USING hash;
CREATE OPERATOR FAMILY alt_opf2 USING hash;
ALTER OPERATOR FAMILY alt_opf1 USING hash OWNER TO regress_alter_generic_user2;
ALTER OPERATOR FAMILY alt_opf2 USING hash OWNER TO regress_alter_generic_user2;

SET SESSION AUTHORIZATION regress_alter_generic_user2;

ALTER OPERATOR FAMILY alt_opf3 USING hash RENAME TO alt_opf4;	-- failed (not owner)
ALTER OPERATOR FAMILY alt_opf1 USING hash RENAME TO alt_opf4;  -- OK
ALTER OPERATOR FAMILY alt_opf3 USING hash OWNER TO regress_alter_generic_user2;  -- failed (not owner)
ALTER OPERATOR FAMILY alt_opf2 USING hash OWNER TO regress_alter_generic_user3;  -- failed (no role membership)
ALTER OPERATOR FAMILY alt_opf3 USING hash SET SCHEMA alt_nsp2;  -- failed (not owner)
ALTER OPERATOR FAMILY alt_opf2 USING hash SET SCHEMA alt_nsp2;  -- failed (name conflict)

RESET SESSION AUTHORIZATION;

SELECT nspname, opfname, amname, rolname
  FROM pg_opfamily o, pg_am m, pg_namespace n, pg_authid a
  WHERE o.opfmethod = m.oid AND o.opfnamespace = n.oid AND o.opfowner = a.oid
    AND n.nspname IN ('alt_nsp1', 'alt_nsp2')
	AND NOT opfname LIKE 'alt_opc%'
  ORDER BY nspname, opfname;

-- ALTER OPERATOR FAMILY ... ADD/DROP

-- Should work. Textbook case of CREATE / ALTER ADD / ALTER DROP / DROP
BEGIN TRANSACTION;
CREATE OPERATOR FAMILY alt_opf4 USING btree;
ALTER OPERATOR FAMILY alt_opf4 USING btree ADD
  -- int4 vs int2
  OPERATOR 1 < (int4, int2) ,
  OPERATOR 2 <= (int4, int2) ,
  OPERATOR 3 = (int4, int2) ,
  OPERATOR 4 >= (int4, int2) ,
  OPERATOR 5 > (int4, int2) ,
  FUNCTION 1 btint42cmp(int4, int2);

ALTER OPERATOR FAMILY alt_opf4 USING btree DROP
  -- int4 vs int2
  OPERATOR 1 (int4, int2) ,
  OPERATOR 2 (int4, int2) ,
  OPERATOR 3 (int4, int2) ,
  OPERATOR 4 (int4, int2) ,
  OPERATOR 5 (int4, int2) ,
  FUNCTION 1 (int4, int2) ;
DROP OPERATOR FAMILY alt_opf4 USING btree;
ROLLBACK;

-- Should fail. Invalid values for ALTER OPERATOR FAMILY .. ADD / DROP
CREATE OPERATOR FAMILY alt_opf4 USING btree;
ALTER OPERATOR FAMILY alt_opf4 USING invalid_index_method ADD  OPERATOR 1 < (int4, int2); -- invalid indexing_method
ALTER OPERATOR FAMILY alt_opf4 USING btree ADD OPERATOR 6 < (int4, int2); -- operator number should be between 1 and 5
ALTER OPERATOR FAMILY alt_opf4 USING btree ADD OPERATOR 0 < (int4, int2); -- operator number should be between 1 and 5
ALTER OPERATOR FAMILY alt_opf4 USING btree ADD OPERATOR 1 < ; -- operator without argument types
ALTER OPERATOR FAMILY alt_opf4 USING btree ADD FUNCTION 0 btint42cmp(int4, int2); -- function number should be between 1 and 5
ALTER OPERATOR FAMILY alt_opf4 USING btree ADD FUNCTION 6 btint42cmp(int4, int2); -- function number should be between 1 and 5
ALTER OPERATOR FAMILY alt_opf4 USING btree ADD STORAGE invalid_storage; -- Ensure STORAGE is not a part of ALTER OPERATOR FAMILY
DROP OPERATOR FAMILY alt_opf4 USING btree;

-- Should fail. Need to be SUPERUSER to do ALTER OPERATOR FAMILY .. ADD / DROP
BEGIN TRANSACTION;
CREATE ROLE regress_alter_generic_user5 NOSUPERUSER;
CREATE OPERATOR FAMILY alt_opf5 USING btree;
SET ROLE regress_alter_generic_user5;
ALTER OPERATOR FAMILY alt_opf5 USING btree ADD OPERATOR 1 < (int4, int2), FUNCTION 1 btint42cmp(int4, int2);
RESET ROLE;
DROP OPERATOR FAMILY alt_opf5 USING btree;
ROLLBACK;

-- Should fail. Need rights to namespace for ALTER OPERATOR FAMILY .. ADD / DROP
BEGIN TRANSACTION;
CREATE ROLE regress_alter_generic_user6;
CREATE SCHEMA alt_nsp6;
REVOKE ALL ON SCHEMA alt_nsp6 FROM regress_alter_generic_user6;
CREATE OPERATOR FAMILY alt_nsp6.alt_opf6 USING btree;
SET ROLE regress_alter_generic_user6;
ALTER OPERATOR FAMILY alt_nsp6.alt_opf6 USING btree ADD OPERATOR 1 < (int4, int2);
ROLLBACK;

-- Should fail. Only two arguments required for ALTER OPERATOR FAMILY ... DROP OPERATOR
CREATE OPERATOR FAMILY alt_opf7 USING btree;
ALTER OPERATOR FAMILY alt_opf7 USING btree ADD OPERATOR 1 < (int4, int2);
ALTER OPERATOR FAMILY alt_opf7 USING btree DROP OPERATOR 1 (int4, int2, int8);
DROP OPERATOR FAMILY alt_opf7 USING btree;

-- Should work. During ALTER OPERATOR FAMILY ... DROP OPERATOR
-- when left type is the same as right type, a DROP with only one argument type should work
CREATE OPERATOR FAMILY alt_opf8 USING btree;
ALTER OPERATOR FAMILY alt_opf8 USING btree ADD OPERATOR 1 < (int4, int4);
DROP OPERATOR FAMILY alt_opf8 USING btree;

-- Should work. Textbook case of ALTER OPERATOR FAMILY ... ADD OPERATOR with FOR ORDER BY
CREATE OPERATOR FAMILY alt_opf9 USING gist;
ALTER OPERATOR FAMILY alt_opf9 USING gist ADD OPERATOR 1 < (int4, int4) FOR ORDER BY float_ops;
DROP OPERATOR FAMILY alt_opf9 USING gist;

-- Should fail. Ensure correct ordering methods in ALTER OPERATOR FAMILY ... ADD OPERATOR .. FOR ORDER BY
CREATE OPERATOR FAMILY alt_opf10 USING btree;
ALTER OPERATOR FAMILY alt_opf10 USING btree ADD OPERATOR 1 < (int4, int4) FOR ORDER BY float_ops;
DROP OPERATOR FAMILY alt_opf10 USING btree;

-- Should work. Textbook case of ALTER OPERATOR FAMILY ... ADD OPERATOR with FOR ORDER BY
CREATE OPERATOR FAMILY alt_opf11 USING gist;
ALTER OPERATOR FAMILY alt_opf11 USING gist ADD OPERATOR 1 < (int4, int4) FOR ORDER BY float_ops;
ALTER OPERATOR FAMILY alt_opf11 USING gist DROP OPERATOR 1 (int4, int4);
DROP OPERATOR FAMILY alt_opf11 USING gist;

-- Should fail. btree comparison functions should return INTEGER in ALTER OPERATOR FAMILY ... ADD FUNCTION
BEGIN TRANSACTION;
CREATE OPERATOR FAMILY alt_opf12 USING btree;
CREATE FUNCTION fn_opf12  (int4, int2) RETURNS BIGINT AS 'SELECT NULL::BIGINT;' LANGUAGE SQL;
ALTER OPERATOR FAMILY alt_opf12 USING btree ADD FUNCTION 1 fn_opf12(int4, int2);
DROP OPERATOR FAMILY alt_opf12 USING btree;
ROLLBACK;

-- Should fail. hash comparison functions should return INTEGER in ALTER OPERATOR FAMILY ... ADD FUNCTION
BEGIN TRANSACTION;
CREATE OPERATOR FAMILY alt_opf13 USING hash;
CREATE FUNCTION fn_opf13  (int4) RETURNS BIGINT AS 'SELECT NULL::BIGINT;' LANGUAGE SQL;
ALTER OPERATOR FAMILY alt_opf13 USING hash ADD FUNCTION 1 fn_opf13(int4);
DROP OPERATOR FAMILY alt_opf13 USING hash;
ROLLBACK;

-- Should fail. btree comparison functions should have two arguments in ALTER OPERATOR FAMILY ... ADD FUNCTION
BEGIN TRANSACTION;
CREATE OPERATOR FAMILY alt_opf14 USING btree;
CREATE FUNCTION fn_opf14 (int4) RETURNS BIGINT AS 'SELECT NULL::BIGINT;' LANGUAGE SQL;
ALTER OPERATOR FAMILY alt_opf14 USING btree ADD FUNCTION 1 fn_opf14(int4);
DROP OPERATOR FAMILY alt_opf14 USING btree;
ROLLBACK;

-- Should fail. hash comparison functions should have one argument in ALTER OPERATOR FAMILY ... ADD FUNCTION
BEGIN TRANSACTION;
CREATE OPERATOR FAMILY alt_opf15 USING hash;
CREATE FUNCTION fn_opf15 (int4, int2) RETURNS BIGINT AS 'SELECT NULL::BIGINT;' LANGUAGE SQL;
ALTER OPERATOR FAMILY alt_opf15 USING hash ADD FUNCTION 1 fn_opf15(int4, int2);
DROP OPERATOR FAMILY alt_opf15 USING hash;
ROLLBACK;

-- Should fail. In gist throw an error when giving different data types for function argument
-- without defining left / right type in ALTER OPERATOR FAMILY ... ADD FUNCTION
CREATE OPERATOR FAMILY alt_opf16 USING gist;
ALTER OPERATOR FAMILY alt_opf16 USING gist ADD FUNCTION 1 btint42cmp(int4, int2);
DROP OPERATOR FAMILY alt_opf16 USING gist;

-- Should fail. duplicate operator number / function number in ALTER OPERATOR FAMILY ... ADD FUNCTION
CREATE OPERATOR FAMILY alt_opf17 USING btree;
ALTER OPERATOR FAMILY alt_opf17 USING btree ADD OPERATOR 1 < (int4, int4), OPERATOR 1 < (int4, int4); -- operator # appears twice in same statement
ALTER OPERATOR FAMILY alt_opf17 USING btree ADD OPERATOR 1 < (int4, int4); -- operator 1 requested first-time
ALTER OPERATOR FAMILY alt_opf17 USING btree ADD OPERATOR 1 < (int4, int4); -- operator 1 requested again in separate statement
ALTER OPERATOR FAMILY alt_opf17 USING btree ADD
  OPERATOR 1 < (int4, int2) ,
  OPERATOR 2 <= (int4, int2) ,
  OPERATOR 3 = (int4, int2) ,
  OPERATOR 4 >= (int4, int2) ,
  OPERATOR 5 > (int4, int2) ,
  FUNCTION 1 btint42cmp(int4, int2) ,
  FUNCTION 1 btint42cmp(int4, int2);    -- procedure 1 appears twice in same statement
ALTER OPERATOR FAMILY alt_opf17 USING btree ADD
  OPERATOR 1 < (int4, int2) ,
  OPERATOR 2 <= (int4, int2) ,
  OPERATOR 3 = (int4, int2) ,
  OPERATOR 4 >= (int4, int2) ,
  OPERATOR 5 > (int4, int2) ,
  FUNCTION 1 btint42cmp(int4, int2);    -- procedure 1 appears first time
ALTER OPERATOR FAMILY alt_opf17 USING btree ADD
  OPERATOR 1 < (int4, int2) ,
  OPERATOR 2 <= (int4, int2) ,
  OPERATOR 3 = (int4, int2) ,
  OPERATOR 4 >= (int4, int2) ,
  OPERATOR 5 > (int4, int2) ,
  FUNCTION 1 btint42cmp(int4, int2);    -- procedure 1 requested again in separate statement
DROP OPERATOR FAMILY alt_opf17 USING btree;


-- Should fail. Ensure that DROP requests for missing OPERATOR / FUNCTIONS
-- return appropriate message in ALTER OPERATOR FAMILY ... DROP OPERATOR / FUNCTION
CREATE OPERATOR FAMILY alt_opf18 USING btree;
ALTER OPERATOR FAMILY alt_opf18 USING btree DROP OPERATOR 1 (int4, int4);
ALTER OPERATOR FAMILY alt_opf18 USING btree ADD
  OPERATOR 1 < (int4, int2) ,
  OPERATOR 2 <= (int4, int2) ,
  OPERATOR 3 = (int4, int2) ,
  OPERATOR 4 >= (int4, int2) ,
  OPERATOR 5 > (int4, int2) ,
  FUNCTION 1 btint42cmp(int4, int2);
ALTER OPERATOR FAMILY alt_opf18 USING btree DROP FUNCTION 2 (int4, int4);
DROP OPERATOR FAMILY alt_opf18 USING btree;

---
--- Cleanup resources
---
\set VERBOSITY terse \\ -- suppress cascade details

DROP SCHEMA alt_nsp1 CASCADE;
DROP SCHEMA alt_nsp2 CASCADE;

DROP USER regress_alter_generic_user1;
DROP USER regress_alter_generic_user2;
DROP USER regress_alter_generic_user3;
