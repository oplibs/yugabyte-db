/* pg_hint_plan/pg_hint_plan--1.4.2--1.5.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "ALTER EXTENSION pg_dbms_stats UPDATE TO '1.5'" to load this file. \quit

SELECT pg_catalog.pg_extension_config_dump('hint_plan.hints','');
SELECT pg_catalog.pg_extension_config_dump('hint_plan.hints_id_seq','');

GRANT SELECT ON hint_plan.hints TO PUBLIC;
GRANT USAGE ON SCHEMA hint_plan TO PUBLIC;
