--
-- CREATE_TYPE
--

--
-- Note: widget_in/out were created in create_function_1, without any
-- prior shell-type creation.  These commands therefore complete a test
-- of the "old style" approach of making the functions first.
--
CREATE TYPE widget (
   internallength = 24,
   input = widget_in,
   output = widget_out,
   typmod_in = numerictypmodin,
   typmod_out = numerictypmodout,
   alignment = double
);

CREATE TYPE city_budget (
   internallength = 16,
   input = int44in,
   output = int44out,
   element = int4,
   category = 'x',   -- just to verify the system will take it
   preferred = true  -- ditto
);

-- Test creation and destruction of shell types
CREATE TYPE shell;
CREATE TYPE shell;   -- fail, type already present
DROP TYPE shell;
DROP TYPE shell;     -- fail, type not exist

-- also, let's leave one around for purposes of pg_dump testing
CREATE TYPE myshell;

--
-- Test type-related default values (broken in releases before PG 7.2)
--
-- This part of the test also exercises the "new style" approach of making
-- a shell type and then filling it in.
--
CREATE TYPE int42;
CREATE TYPE text_w_default;

-- Make dummy I/O routines using the existing internal support for int4, text
CREATE FUNCTION int42_in(cstring)
   RETURNS int42
   AS 'int4in'
   LANGUAGE internal STRICT IMMUTABLE;
CREATE FUNCTION int42_out(int42)
   RETURNS cstring
   AS 'int4out'
   LANGUAGE internal STRICT IMMUTABLE;
CREATE FUNCTION text_w_default_in(cstring)
   RETURNS text_w_default
   AS 'textin'
   LANGUAGE internal STRICT IMMUTABLE;
CREATE FUNCTION text_w_default_out(text_w_default)
   RETURNS cstring
   AS 'textout'
   LANGUAGE internal STRICT IMMUTABLE;

CREATE TYPE int42 (
   internallength = 4,
   input = int42_in,
   output = int42_out,
   alignment = int4,
   default = 42,
   passedbyvalue
);

CREATE TYPE text_w_default (
   internallength = variable,
   input = text_w_default_in,
   output = text_w_default_out,
   alignment = int4,
   default = 'zippo'
);

CREATE TABLE default_test (f1 text_w_default, f2 int42);

INSERT INTO default_test DEFAULT VALUES;

SELECT * FROM default_test;

-- invalid: non-lowercase quoted identifiers
CREATE TYPE case_int42 (
	"Internallength" = 4,
	"Input" = int42_in,
	"Output" = int42_out,
	"Alignment" = int4,
	"Default" = 42,
	"Passedbyvalue"
);

-- Test stand-alone composite type

CREATE TYPE default_test_row AS (f1 text_w_default, f2 int42);

CREATE FUNCTION get_default_test() RETURNS SETOF default_test_row AS '
  SELECT * FROM default_test;
' LANGUAGE SQL;

SELECT * FROM get_default_test();

-- Test comments
COMMENT ON TYPE bad IS 'bad comment';
COMMENT ON TYPE default_test_row IS 'good comment';
COMMENT ON TYPE default_test_row IS NULL;
COMMENT ON COLUMN default_test_row.nope IS 'bad comment';
COMMENT ON COLUMN default_test_row.f1 IS 'good comment';
COMMENT ON COLUMN default_test_row.f1 IS NULL;

-- Check shell type create for existing types
CREATE TYPE text_w_default;		-- should fail

DROP TYPE default_test_row CASCADE;

DROP TABLE default_test;

-- Check type create with input/output incompatibility
CREATE TYPE not_existing_type (INPUT = array_in,
    OUTPUT = array_out,
    ELEMENT = int,
    INTERNALLENGTH = 32);

-- Check dependency transfer of opaque functions when creating a new type
CREATE FUNCTION base_fn_in(cstring) RETURNS opaque AS 'boolin'
    LANGUAGE internal IMMUTABLE STRICT;
CREATE FUNCTION base_fn_out(opaque) RETURNS opaque AS 'boolout'
    LANGUAGE internal IMMUTABLE STRICT;
CREATE TYPE base_type(INPUT = base_fn_in, OUTPUT = base_fn_out);
\set VERBOSITY terse \\ -- suppress cascade details
DROP FUNCTION base_fn_in(cstring); -- error
DROP FUNCTION base_fn_out(opaque); -- error
DROP TYPE base_type; -- error
DROP TYPE base_type CASCADE;
\set VERBOSITY default

-- Check usage of typmod with a user-defined type
-- (we have borrowed numeric's typmod functions)

CREATE TEMP TABLE mytab (foo widget(42,13,7));     -- should fail
CREATE TEMP TABLE mytab (foo widget(42,13));

SELECT format_type(atttypid,atttypmod) FROM pg_attribute
WHERE attrelid = 'mytab'::regclass AND attnum > 0;

-- might as well exercise the widget type while we're here
INSERT INTO mytab VALUES ('(1,2,3)'), ('(-44,5.5,12)');
TABLE mytab;

-- and test format_type() a bit more, too
select format_type('varchar'::regtype, 42);
select format_type('bpchar'::regtype, null);
-- this behavior difference is intentional
select format_type('bpchar'::regtype, -1);
