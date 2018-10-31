--
-- BOOLEAN
--

--
-- sanity check - if this fails go insane!
--
SELECT 1 AS one;


-- ******************testing built-in type bool********************

-- check bool input syntax

SELECT true AS true;

SELECT false AS false;

SELECT bool 't' AS true;

SELECT bool '   f           ' AS false;

SELECT bool 'true' AS true;

SELECT bool 'test' AS error;

SELECT bool 'false' AS false;

SELECT bool 'foo' AS error;

SELECT bool 'y' AS true;

SELECT bool 'yes' AS true;

SELECT bool 'yeah' AS error;

SELECT bool 'n' AS false;

SELECT bool 'no' AS false;

SELECT bool 'nay' AS error;

SELECT bool 'on' AS true;

SELECT bool 'off' AS false;

SELECT bool 'of' AS false;

SELECT bool 'o' AS error;

SELECT bool 'on_' AS error;

SELECT bool 'off_' AS error;

SELECT bool '1' AS true;

SELECT bool '11' AS error;

SELECT bool '0' AS false;

SELECT bool '000' AS error;

SELECT bool '' AS error;

-- and, or, not in qualifications

SELECT bool 't' or bool 'f' AS true;

SELECT bool 't' and bool 'f' AS false;

SELECT not bool 'f' AS true;

SELECT bool 't' = bool 'f' AS false;

SELECT bool 't' <> bool 'f' AS true;

SELECT bool 't' > bool 'f' AS true;

SELECT bool 't' >= bool 'f' AS true;

SELECT bool 'f' < bool 't' AS true;

SELECT bool 'f' <= bool 't' AS true;

-- explicit casts to/from text
SELECT 'TrUe'::text::boolean AS true, 'fAlse'::text::boolean AS false;
SELECT '    true   '::text::boolean AS true,
       '     FALSE'::text::boolean AS false;
SELECT true::boolean::text AS true, false::boolean::text AS false;

SELECT '  tru e '::text::boolean AS invalid;    -- error
SELECT ''::text::boolean AS invalid;            -- error

CREATE TABLE BOOLTBL1 (k INT PRIMARY KEY, f1 bool);

INSERT INTO BOOLTBL1 (k, f1) VALUES (1, bool 't');

INSERT INTO BOOLTBL1 (k, f1) VALUES (2, bool 'True');

INSERT INTO BOOLTBL1 (k, f1) VALUES (3, bool 'true');


-- BOOLTBL1 should be full of true's at this point
SELECT '' AS t_3, BOOLTBL1.* FROM BOOLTBL1 ORDER BY BOOLTBL1.k;


SELECT '' AS t_3, BOOLTBL1.*
   FROM BOOLTBL1
   WHERE f1 = bool 'true' ORDER BY BOOLTBL1.k;


SELECT '' AS t_3, BOOLTBL1.*
   FROM BOOLTBL1
   WHERE f1 <> bool 'false' ORDER BY BOOLTBL1.k;

SELECT '' AS zero, BOOLTBL1.*
   FROM BOOLTBL1
   WHERE booleq(bool 'false', f1) ORDER BY BOOLTBL1.k;

INSERT INTO BOOLTBL1 (k, f1) VALUES (4, bool 'f');

SELECT '' AS f_1, BOOLTBL1.*
   FROM BOOLTBL1
   WHERE f1 = bool 'false' ORDER BY BOOLTBL1.k;


CREATE TABLE BOOLTBL2 (k INT PRIMARY KEY, f1 bool);

INSERT INTO BOOLTBL2 (k, f1) VALUES (1, bool 'f');

INSERT INTO BOOLTBL2 (k, f1) VALUES (2, bool 'false');

INSERT INTO BOOLTBL2 (k, f1) VALUES (3, bool 'False');

INSERT INTO BOOLTBL2 (k, f1) VALUES (4, bool 'FALSE');

-- This is now an invalid expression
-- For pre-v6.3 this evaluated to false - thomas 1997-10-23
INSERT INTO BOOLTBL2 (k, f1)
   VALUES (5, bool 'XXX');

-- BOOLTBL2 should be full of false's at this point
SELECT '' AS f_4, BOOLTBL2.* FROM BOOLTBL2 ORDER BY BOOLTBL2.k;


SELECT '' AS tf_12, BOOLTBL1.*, BOOLTBL2.*
   FROM BOOLTBL1, BOOLTBL2
   WHERE BOOLTBL2.f1 <> BOOLTBL1.f1 ORDER BY BOOLTBL1.k, BOOLTBL2.k;


SELECT '' AS tf_12, BOOLTBL1.*, BOOLTBL2.*
   FROM BOOLTBL1, BOOLTBL2
   WHERE boolne(BOOLTBL2.f1,BOOLTBL1.f1) ORDER BY BOOLTBL1.k, BOOLTBL2.k;;


SELECT '' AS ff_4, BOOLTBL1.*, BOOLTBL2.*
   FROM BOOLTBL1, BOOLTBL2
   WHERE BOOLTBL2.f1 = BOOLTBL1.f1 and BOOLTBL1.f1 = bool 'false' ORDER BY BOOLTBL1.k, BOOLTBL2.k;


SELECT '' AS tf_12_ff_4, BOOLTBL1.*, BOOLTBL2.*
   FROM BOOLTBL1, BOOLTBL2
   WHERE BOOLTBL2.f1 = BOOLTBL1.f1 or BOOLTBL1.f1 = bool 'true'
   ORDER BY BOOLTBL1.f1, BOOLTBL2.f1, BOOLTBL1.k, BOOLTBL2.k;

--
-- SQL syntax
-- Try all combinations to ensure that we get nothing when we expect nothing
-- - thomas 2000-01-04
--

SELECT '' AS "True", f1
   FROM BOOLTBL1
   WHERE f1 IS TRUE ORDER BY BOOLTBL1.k;

SELECT '' AS "Not False", f1
   FROM BOOLTBL1
   WHERE f1 IS NOT FALSE ORDER BY BOOLTBL1.k;

SELECT '' AS "False", f1
   FROM BOOLTBL1
   WHERE f1 IS FALSE ORDER BY BOOLTBL1.k;

SELECT '' AS "Not True", f1
   FROM BOOLTBL1
   WHERE f1 IS NOT TRUE ORDER BY BOOLTBL1.k;

SELECT '' AS "True", f1
   FROM BOOLTBL2
   WHERE f1 IS TRUE ORDER BY BOOLTBL2.k;

SELECT '' AS "Not False", f1
   FROM BOOLTBL2
   WHERE f1 IS NOT FALSE ORDER BY BOOLTBL2.k;

SELECT '' AS "False", f1
   FROM BOOLTBL2
   WHERE f1 IS FALSE ORDER BY BOOLTBL2.k;

SELECT '' AS "Not True", f1
   FROM BOOLTBL2
   WHERE f1 IS NOT TRUE ORDER BY BOOLTBL2.k;

--
-- Tests for BooleanTest
--
CREATE TABLE BOOLTBL3 (d text, b bool, o int PRIMARY KEY);
INSERT INTO BOOLTBL3 (d, b, o) VALUES ('true', true, 1);
INSERT INTO BOOLTBL3 (d, b, o) VALUES ('false', false, 2);
INSERT INTO BOOLTBL3 (d, b, o) VALUES ('null', null, 3);

SELECT
    d,
    b IS TRUE AS istrue,
    b IS NOT TRUE AS isnottrue,
    b IS FALSE AS isfalse,
    b IS NOT FALSE AS isnotfalse,
    b IS UNKNOWN AS isunknown,
    b IS NOT UNKNOWN AS isnotunknown
FROM booltbl3 ORDER BY o;

--
-- Clean up
-- Many tables are retained by the regression test, but these do not seem
--  particularly useful so just get rid of them for now.
--  - thomas 1997-11-30
--

DROP TABLE  BOOLTBL1;

DROP TABLE  BOOLTBL2;

DROP TABLE  BOOLTBL3;
