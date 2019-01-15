--
-- YB_FEATURE Testsuite: DELETE
--   An introduction on whether or not a feature is supported in YugaByte.
--   This test suite does not go in depth for each command.
-- 
-- Prepare two identical tables of all supported primitive types.
--
-- INSERT values to be deleted
--
INSERT INTO feature_tab_dml VALUES(
			 88,
			 88,
			 88,
			 88.88,
			 88.88,
			 'eight',
			 'eight',
			 'eight',
			 E'\\x88F1E2D3C4B5A6079889706A5B4C3D2E1F',
			 'August 8, 2019 08:08:08.8888',
			 'August 8, 2019 08:08:08.8888 PST AD',
			 TRUE,
			 '{ 88, 88, 88 }',
			 '{ "eight", "eight", "eight" }');
INSERT INTO feature_tab_dml_identifier VALUES(88, 'eighty eight');
--
-- Select rows before deleting.
SELECT * FROM feature_tab_dml WHERE col_smallint = 88;
--
-- DELETE Statement
--
DELETE FROM feature_tab_dml
			 WHERE		col_smallint = 88
			 RETURNING
								col_smallint,
								col_bigint,
								col_real,
								col_double,
								DATE_TRUNC('day', col_timestamp_tz) expr_date,
								col_array_text[1];
--
-- Select rows after deleting.
SELECT * FROM feature_tab_dml WHERE col_smallint = 88;
