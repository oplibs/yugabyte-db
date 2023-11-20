-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION yb_auh" to load this file. \quit

CREATE FUNCTION pg_active_universe_history(
    OUT auh_sample_time timestamptz,
    OUT top_level_request_id text,
    OUT request_id BIGINT,
    OUT wait_event_component text,
    OUT wait_event_class text,
    OUT wait_event text,
    OUT wait_event_aux text,
    OUT top_level_node_id text,
    OUT query_id BIGINT,
    OUT client_node_ip text,
    OUT start_ts_of_wait_event timestamptz,
    OUT sample_rate FLOAT
)

RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pg_active_universe_history'
LANGUAGE C STRICT VOLATILE PARALLEL SAFE;

CREATE FUNCTION yb_tables(
    OUT table_id text,
    OUT table_name text,
    OUT table_type text,
    OUT relation_type text,
    OUT namespace_id text,
    OUT namespace_name text,
    OUT database_type text,
    OUT pgschema_name text,
    OUT colocated boolean,
    OUT parent_table_id text
)

RETURNS SETOF record
AS 'MODULE_PATHNAME', 'yb_tables'
LANGUAGE C STRICT VOLATILE PARALLEL SAFE;

-- Register a view on the function for ease of use.
CREATE VIEW pg_active_universe_history AS
  SELECT * FROM pg_active_universe_history();

GRANT SELECT ON pg_active_universe_history TO PUBLIC;

CREATE VIEW yb_tables AS
  SELECT * FROM yb_tables();

GRANT SELECT ON yb_tables TO PUBLIC;