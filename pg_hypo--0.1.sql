-- This program is open source, licensed under the PostgreSQL License.
-- For license terms, see the LICENSE file.
--
-- Copyright (C) 2015: Julien ROuhaud

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pg_hypo" to load this file. \quit

SET client_encoding = 'UTF8';

CREATE FUNCTION pg_hypo_reset()
    RETURNS void
    LANGUAGE c COST 1000
AS '$libdir/pg_hypo', 'pg_hypo_reset';

CREATE FUNCTION
pg_hypo_add_index_internal(IN indexid oid,
                           IN relid oid,
                           IN indexname text,
                           IN relam Oid,
                           IN ncolumns int,
                           IN indexkeys int,
                           IN indexcollations Oid,
                           IN opfamily Oid,
                           IN opcintype Oid)
    RETURNS bool
    LANGUAGE c COST 1000
AS '$libdir/pg_hypo', 'pg_hypo_add_index_internal';

CREATE FUNCTION pg_hypo(OUT dbid Oid, OUT indexname text, OUT relid Oid, OUT attnum int, OUT amid Oid)
    RETURNS SETOF record
    LANGUAGE c COST 1000
AS '$libdir/pg_hypo', 'pg_hypo';

CREATE FUNCTION
pg_hypo_add_index(IN _nspname name, IN _relname name, IN _attname name, IN _amname text)
    RETURNS bool
AS
$_$
    SELECT pg_hypo_add_index_internal(
        id,
        relid,
        indexname,
        amoid,
        ncolumns,
        attnum,
        indexcollations,
        opfoid,
        atttypid)
    FROM (
        SELECT DISTINCT 1 AS id, c.oid AS relid, 'index_name' AS indexname, am.oid AS amoid, 1 AS ncolumns, a.attnum, 0 AS indexcollations, opf.oid AS opfoid, a.atttypid
        FROM pg_class c
        JOIN pg_namespace n on n.oid = c.relnamespace
        JOIN pg_attribute a ON a.attrelid = c.oid AND a.attnum > 0
        JOIN pg_type t ON t.oid = a.atttypid
        JOIN pg_amop amop ON amop.amoplefttype = t.oid
        JOIN pg_opfamily opf ON opf.oid = amop.amopfamily
        JOIN pg_am am ON am.oid = amop.amopmethod
        WHERE
            n.nspname = _nspname
            AND c.relname = _relname
            AND a.attname = _attname
            AND am.amname = _amname
    ) src;
$_$
LANGUAGE sql;

CREATE FUNCTION pg_hypo_list_indexes(OUT datname name, OUT indexname text, OUT nspname name, OUT relname name, OUT attname name, OUT amname name)
    RETURNS SETOF record
AS
$_$
    SELECT d.datname, h.indexname, n.nspname, c.relname, a.attname, am.amname
    FROM pg_hypo() h
    JOIN pg_database d ON d.datname = current_database()
    JOIN pg_class c ON c.oid = h.relid
    JOIN pg_namespace n ON n.oid = c.relnamespace
    JOIN pg_attribute a on a.attrelid = c.oid
    LEFT JOIN pg_am am ON am.oid = h.amid
    WHERE a.attnum = h.attnum;
$_$
LANGUAGE sql;
