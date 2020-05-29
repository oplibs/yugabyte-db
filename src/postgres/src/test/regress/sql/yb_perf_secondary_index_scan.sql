--
-- This file is to check correctness of data after applying secondary index scan optimization.
--
--
-- Logical test on small size table, using column-ref expression index.
--   * The text_books test case is commented out because DocDB misbehaved on ASAN build.
--   * This test has been split to yb_secondary_index_scan.sql to be run in different session.
--   * See Github issue 4488. Once the issue is fixed we can merge them back together.
--
-- CREATE TABLE text_books ( id int PRIMARY KEY, author text, year int);
-- CREATE INDEX text_books_author_first_name ON text_books (author);
--
-- INSERT INTO text_books (id, author, year)
--   VALUES (1, '{ "first_name": "William", "last_name": "Shakespeare" }', 1623);
-- INSERT INTO text_books (id, author, year)
--   VALUES (2, '{ "first_name": "William", "last_name": "Shakespeare" }', 1603);
-- INSERT INTO text_books (id, author, year)
--   VALUES (3, '{ "first_name": "Charles", "last_name": "Dickens" }', 1838);
-- INSERT INTO text_books (id, author, year)
--   VALUES (4, '{ "first_name": "Charles", "last_name": "Dickens" }', 1950);
-- INSERT INTO text_books (id, author, year)
--   VALUES (5, '{ "first_name": "Stephen", "last_name": "Hawking" }', 1988);
--
-- EXPLAIN (COSTS OFF) SELECT id FROM text_books WHERE author = 'Hello World' ORDER BY year;
-- SELECT id FROM text_books WHERE author = 'Hello World' ORDER BY year;
-- EXPLAIN (COSTS OFF) SELECT id FROM text_books
--   WHERE author = '{ "first_name": "William", "last_name": "Shakespeare" }' ORDER BY year;
-- SELECT id FROM text_books
--   WHERE author = '{ "first_name": "William", "last_name": "Shakespeare" }' ORDER BY year;
-- Drop INDEX and run again.
-- DROP index text_books_author_first_name;
-- EXPLAIN (COSTS OFF) SELECT id FROM text_books WHERE author = 'Hello World' ORDER BY year;
-- SELECT id FROM text_books WHERE author = 'Hello World' ORDER BY year;
-- EXPLAIN (COSTS OFF) SELECT id FROM text_books
--   WHERE author = '{ "first_name": "William", "last_name": "Shakespeare" }' ORDER BY year;
-- SELECT id FROM text_books
--   WHERE author = '{ "first_name": "William", "last_name": "Shakespeare" }' ORDER BY year;
--
-- Logical test on small size table, using JSONB expression index.
--
-- CREATE TABLE books ( id int PRIMARY KEY, details jsonb );
-- CREATE INDEX books_author_first_name ON books ((details->'author'->>'first_name'));
-- INSERT INTO books (id, details)
--   VALUES (1, '{ "name": "Macbeth",
--                 "author": { "first_name": "William", "last_name": "Shakespeare" },
--                 "year": 1623,
--                 "editors": ["John", "Elizabeth", "Jeff"] }');
-- INSERT INTO books (id, details)
--   VALUES (2, '{ "name": "Hamlet",
--                 "author": { "first_name": "William", "last_name": "Shakespeare" },
--                 "year": 1603,
--                 "editors": ["Lysa", "Mark", "Robert"] }');
-- INSERT INTO books (id, details)
--   VALUES (3, '{ "name": "Oliver Twist",
--                 "author": { "first_name": "Charles", "last_name": "Dickens" },
--                 "year": 1838,
--                 "genre": "novel",
--                 "editors": ["Mark", "Tony", "Britney"] }');
-- INSERT INTO books (id, details)
--   VALUES (4, '{ "name": "Great Expectations",
--                 "author": { "first_name": "Charles", "last_name": "Dickens" },
--                 "year": 1950,
--                 "genre": "novel",
--                 "editors": ["Robert", "John", "Melisa"] }');
-- INSERT INTO books (id, details)
--   VALUES (5, '{ "name": "A Brief History of Time",
--                 "author": { "first_name": "Stephen", "last_name": "Hawking" },
--                 "year": 1988,
--                 "genre": "science",
--                 "editors": ["Melisa", "Mark", "John"] }');
-- EXPLAIN (COSTS OFF) SELECT id FROM books WHERE details->'author'->>'first_name' = 'Hello World'
--   ORDER BY details->>'name';
-- SELECT id FROM books WHERE details->'author'->>'first_name' = 'Hello World'
--   ORDER BY details->>'name';
-- EXPLAIN (COSTS OFF) SELECT id FROM books WHERE details->'author'->>'first_name' = 'Charles'
--   ORDER BY details->>'name';
-- SELECT id FROM books WHERE details->'author'->>'first_name' = 'Charles'
--   ORDER BY details->>'name';
-- Drop INDEX and run again.
-- DROP index books_author_first_name;
-- EXPLAIN (COSTS OFF) SELECT id FROM books WHERE details->'author'->>'first_name' = 'Hello World'
--   ORDER BY details->>'name';
-- SELECT id FROM books WHERE details->'author'->>'first_name' = 'Hello World'
--   ORDER BY details->>'name';
-- EXPLAIN (COSTS OFF) SELECT id FROM books WHERE details->'author'->>'first_name' = 'Charles'
--   ORDER BY details->>'name';
-- SELECT id FROM books WHERE details->'author'->>'first_name' = 'Charles'
--   ORDER BY details->>'name';
--
-- Logical test on larger size table.
-- Table definition
--
-- CREATE TABLE airports(ident TEXT,
--                       type TEXT,
--                       name TEXT,
--                       elevation_ft INT,
--                       continent TEXT,
--                       iso_country CHAR(2),
--                       iso_region CHAR(7),
--                       municipality TEXT,
--                       gps_code TEXT,
--                       iata_code TEXT,
--                       local_code TEXT,
--                       coordinates TEXT,
--                       PRIMARY KEY (ident));
--
-- Index for SELECTing ybctid of the same airport country using HASH key.
-- CREATE INDEX airport_type_region_idx ON airports((type, iso_region) HASH, ident ASC);
--
--
-- The following queries are to ensure the data is in indexing order.
-- NOTE: In the above indexes, range column "ident" is in ASC.
--
-- Column 'ident' should be sorted in ASC for this SELECT
EXPLAIN (COSTS OFF) SELECT * FROM airports WHERE type = 'closed' AND iso_region = 'US-CA';
SELECT * FROM airports WHERE type = 'closed' AND iso_region = 'US-CA';
--
-- This query the first 10 rows.
EXPLAIN (COSTS OFF) SELECT * FROM airports WHERE type = 'medium_airport' AND iso_region = 'US-CA'
  ORDER BY ident LIMIT 10;
SELECT * FROM airports WHERE type = 'medium_airport' AND iso_region = 'US-CA'
  ORDER BY ident LIMIT 10;
--
-- This query the last 10 rows.
EXPLAIN (COSTS OFF) SELECT * FROM airports WHERE type = 'medium_airport' AND iso_region = 'US-CA'
  ORDER BY ident DESC LIMIT 10;
SELECT * FROM airports WHERE type = 'medium_airport' AND iso_region = 'US-CA'
  ORDER BY ident DESC LIMIT 10;
