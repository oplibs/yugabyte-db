--
-- BOX
--

--
-- box logic
--	     o
-- 3	  o--|X
--	  |  o|
-- 2	+-+-+ |
--	| | | |
-- 1	| o-+-o
--	|   |
-- 0	+---+
--
--	0 1 2 3
--

-- boxes are specified by two points, given by four floats x1,y1,x2,y2


CREATE TABLE BOX_TBL (id int primary key, f1 box);

INSERT INTO BOX_TBL (id, f1) VALUES (1, '(2.0,2.0,0.0,0.0)');

INSERT INTO BOX_TBL (id, f1) VALUES (2, '(1.0,1.0,3.0,3.0)');

-- degenerate cases where the box is a line or a point
-- note that lines and points boxes all have zero area
INSERT INTO BOX_TBL (id, f1) VALUES (3, '(2.5, 2.5, 2.5,3.5)');

INSERT INTO BOX_TBL (id, f1) VALUES (4, '(3.0, 3.0,3.0,3.0)');

-- badly formatted box inputs
INSERT INTO BOX_TBL (id, f1) VALUES (5, '(2.3, 4.5)');

INSERT INTO BOX_TBL (id, f1) VALUES (6, 'asdfasdf(ad');


SELECT '' AS four, * FROM BOX_TBL ORDER BY id;

SELECT '' AS four, b.*, area(b.f1) as barea
   FROM BOX_TBL b ORDER BY id;

-- overlap
SELECT '' AS three, b.f1
   FROM BOX_TBL b
   WHERE b.f1 && box '(2.5,2.5,1.0,1.0)' ORDER BY id;

-- left-or-overlap (x only)
SELECT '' AS two, b1.*
   FROM BOX_TBL b1
   WHERE b1.f1 &< box '(2.0,2.0,2.5,2.5)' ORDER BY id;

-- right-or-overlap (x only)
SELECT '' AS two, b1.*
   FROM BOX_TBL b1
   WHERE b1.f1 &> box '(2.0,2.0,2.5,2.5)' ORDER BY id;

-- left of
SELECT '' AS two, b.f1
   FROM BOX_TBL b
   WHERE b.f1 << box '(3.0,3.0,5.0,5.0)' ORDER BY id;

-- area <=
SELECT '' AS four, b.f1
   FROM BOX_TBL b
   WHERE b.f1 <= box '(3.0,3.0,5.0,5.0)' ORDER BY id;

-- area <
SELECT '' AS two, b.f1
   FROM BOX_TBL b
   WHERE b.f1 < box '(3.0,3.0,5.0,5.0)' ORDER BY id;

-- area =
SELECT '' AS two, b.f1
   FROM BOX_TBL b
   WHERE b.f1 = box '(3.0,3.0,5.0,5.0)' ORDER BY id;

-- area >
SELECT '' AS two, b.f1
   FROM BOX_TBL b				-- zero area
   WHERE b.f1 > box '(3.5,3.0,4.5,3.0)' ORDER BY id;

-- area >=
SELECT '' AS four, b.f1
   FROM BOX_TBL b				-- zero area
   WHERE b.f1 >= box '(3.5,3.0,4.5,3.0)' ORDER BY id;

-- right of
SELECT '' AS two, b.f1
   FROM BOX_TBL b
   WHERE box '(3.0,3.0,5.0,5.0)' >> b.f1 ORDER BY id;

-- contained in
SELECT '' AS three, b.f1
   FROM BOX_TBL b
   WHERE b.f1 <@ box '(0,0,3,3)' ORDER BY id;

-- contains
SELECT '' AS three, b.f1
   FROM BOX_TBL b
   WHERE box '(0,0,3,3)' @> b.f1 ORDER BY id;

-- box equality
SELECT '' AS one, b.f1
   FROM BOX_TBL b
   WHERE box '(1,1,3,3)' ~= b.f1 ORDER BY id;

-- center of box, left unary operator
SELECT '' AS four, @@(b1.f1) AS p
   FROM BOX_TBL b1 ORDER BY id;

-- wholly-contained
SELECT '' AS one, b1.*, b2.*
   FROM BOX_TBL b1, BOX_TBL b2
   WHERE b1.f1 @> b2.f1 and not b1.f1 ~= b2.f1 ORDER BY b1.id;

SELECT '' AS four, height(f1), width(f1) FROM BOX_TBL ORDER BY id;

--
-- Test the SP-GiST index
--

-- TODO(neil) TEMPORARY table is not yet supported.
-- CREATE TEMPORARY TABLE box_temp (f1 box);
CREATE TABLE box_temp (f1 box);
INSERT INTO box_temp
	SELECT box(point(i, i), point(i * 2, i * 2))
	FROM generate_series(1, 50) AS i;

-- Indexing non-empty table not yet supported
-- CREATE INDEX box_spgist ON box_temp USING spgist (f1);

INSERT INTO box_temp
	VALUES (NULL),
		   ('(0,0)(0,100)'),
		   ('(-3,4.3333333333)(40,1)'),
		   ('(0,100)(0,infinity)'),
		   ('(-infinity,0)(0,infinity)'),
		   ('(-infinity,-infinity)(infinity,infinity)');

SET enable_seqscan = false;

SELECT * FROM box_temp WHERE f1 << '(10,20),(30,40)' ORDER BY height(f1), width(f1);
EXPLAIN (COSTS OFF) SELECT * FROM box_temp WHERE f1 << '(10,20),(30,40)';

SELECT * FROM box_temp WHERE f1 &< '(10,4.333334),(5,100)' ORDER BY height(f1), width(f1);
EXPLAIN (COSTS OFF) SELECT * FROM box_temp WHERE f1 &< '(10,4.333334),(5,100)';

SELECT * FROM box_temp WHERE f1 && '(15,20),(25,30)' ORDER BY height(f1), width(f1);
EXPLAIN (COSTS OFF) SELECT * FROM box_temp WHERE f1 && '(15,20),(25,30)';

SELECT * FROM box_temp WHERE f1 &> '(40,30),(45,50)' ORDER BY height(f1), width(f1);
EXPLAIN (COSTS OFF) SELECT * FROM box_temp WHERE f1 &> '(40,30),(45,50)';

SELECT * FROM box_temp WHERE f1 >> '(30,40),(40,30)' ORDER BY height(f1), width(f1);
EXPLAIN (COSTS OFF) SELECT * FROM box_temp WHERE f1 >> '(30,40),(40,30)';

SELECT * FROM box_temp WHERE f1 <<| '(10,4.33334),(5,100)' ORDER BY height(f1), width(f1);
EXPLAIN (COSTS OFF) SELECT * FROM box_temp WHERE f1 <<| '(10,4.33334),(5,100)';

SELECT * FROM box_temp WHERE f1 &<| '(10,4.3333334),(5,1)' ORDER BY height(f1), width(f1);
EXPLAIN (COSTS OFF) SELECT * FROM box_temp WHERE f1 &<| '(10,4.3333334),(5,1)';

SELECT * FROM box_temp WHERE f1 |&> '(49.99,49.99),(49.99,49.99)' ORDER BY height(f1), width(f1);
EXPLAIN (COSTS OFF) SELECT * FROM box_temp WHERE f1 |&> '(49.99,49.99),(49.99,49.99)';

SELECT * FROM box_temp WHERE f1 |>> '(37,38),(39,40)' ORDER BY height(f1), width(f1);
EXPLAIN (COSTS OFF) SELECT * FROM box_temp WHERE f1 |>> '(37,38),(39,40)';

SELECT * FROM box_temp WHERE f1 @> '(10,11),(15,16)' ORDER BY height(f1), width(f1);
EXPLAIN (COSTS OFF) SELECT * FROM box_temp WHERE f1 @> '(10,11),(15,15)';

SELECT * FROM box_temp WHERE f1 <@ '(10,15),(30,35)' ORDER BY height(f1), width(f1);
EXPLAIN (COSTS OFF) SELECT * FROM box_temp WHERE f1 <@ '(10,15),(30,35)';

SELECT * FROM box_temp WHERE f1 ~= '(20,20),(40,40)' ORDER BY height(f1), width(f1);
EXPLAIN (COSTS OFF) SELECT * FROM box_temp WHERE f1 ~= '(20,20),(40,40)';

RESET enable_seqscan;

DROP INDEX box_spgist;

--
-- Test the SP-GiST index on the larger volume of data
--
CREATE TABLE quad_box_tbl (b box);
INSERT INTO quad_box_tbl
	SELECT box(point(x * 10, y * 10), point(x * 10 + 5, y * 10 + 5))
	FROM generate_series(1, 100) x,
		 generate_series(1, 100) y;

-- insert repeating data to test allTheSame
INSERT INTO quad_box_tbl
	SELECT '((200, 300),(210, 310))'
	FROM generate_series(1, 1000);

INSERT INTO quad_box_tbl
	VALUES
		(NULL),
		(NULL),
		('((-infinity,-infinity),(infinity,infinity))'),
		('((-infinity,100),(-infinity,500))'),
		('((-infinity,-infinity),(700,infinity))');

-- TODO(neil) Index support
-- CREATE INDEX quad_box_tbl_idx ON quad_box_tbl USING spgist(b);

SET enable_seqscan = OFF;
SET enable_indexscan = ON;
SET enable_bitmapscan = ON;

SELECT count(*) FROM quad_box_tbl WHERE b <<  box '((100,200),(300,500))';
SELECT count(*) FROM quad_box_tbl WHERE b &<  box '((100,200),(300,500))';
SELECT count(*) FROM quad_box_tbl WHERE b &&  box '((100,200),(300,500))';
SELECT count(*) FROM quad_box_tbl WHERE b &>  box '((100,200),(300,500))';
SELECT count(*) FROM quad_box_tbl WHERE b >>  box '((100,200),(300,500))';
SELECT count(*) FROM quad_box_tbl WHERE b >>  box '((100,200),(300,500))';
SELECT count(*) FROM quad_box_tbl WHERE b <<| box '((100,200),(300,500))';
SELECT count(*) FROM quad_box_tbl WHERE b &<| box '((100,200),(300,500))';
SELECT count(*) FROM quad_box_tbl WHERE b |&> box '((100,200),(300,500))';
SELECT count(*) FROM quad_box_tbl WHERE b |>> box '((100,200),(300,500))';
SELECT count(*) FROM quad_box_tbl WHERE b @>  box '((201,301),(202,303))';
SELECT count(*) FROM quad_box_tbl WHERE b <@  box '((100,200),(300,500))';
SELECT count(*) FROM quad_box_tbl WHERE b ~=  box '((200,300),(205,305))';

RESET enable_seqscan;
RESET enable_indexscan;
RESET enable_bitmapscan;
