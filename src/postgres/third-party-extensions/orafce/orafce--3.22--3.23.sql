CREATE OR REPLACE FUNCTION oracle.mod(SMALLINT, SMALLINT)
RETURNS SMALLINT AS $$
SELECT CASE $2 WHEN 0 THEN $1 ELSE pg_catalog.MOD($1, $2) END;
$$ LANGUAGE SQL IMMUTABLE;

CREATE OR REPLACE FUNCTION oracle.mod(INT, INT)
RETURNS INT AS $$
   SELECT CASE $2 WHEN 0 THEN $1 ELSE pg_catalog.MOD($1, $2) END;
$$ LANGUAGE SQL  IMMUTABLE;

CREATE OR REPLACE FUNCTION oracle.mod(BIGINT, BIGINT)
RETURNS BIGINT AS $$
SELECT CASE $2 WHEN 0 THEN $1 ELSE pg_catalog.MOD($1, $2) END;
$$ LANGUAGE sql IMMUTABLE;

CREATE OR REPLACE FUNCTION oracle.mod(NUMERIC, NUMERIC)
RETURNS NUMERIC AS $$
SELECT CASE $2 WHEN 0 THEN $1 ELSE pg_catalog.MOD($1, $2) END;
$$ LANGUAGE sql IMMUTABLE;

DO $$
BEGIN
  IF EXISTS(SELECT * FROM pg_settings WHERE name = 'server_version_num' AND setting::int >= 90600) THEN
    EXECUTE $_$ALTER FUNCTION oracle.mod(SMALLINT, SMALLINT) PARALLEL SAFE$_$;
    EXECUTE $_$ALTER FUNCTION oracle.mod(INT, INT) PARALLEL SAFE$_$;
    EXECUTE $_$ALTER FUNCTION oracle.mod(BIGINT, BIGINT) PARALLEL SAFE$_$;
    EXECUTE $_$ALTER FUNCTION oracle.mod(NUMERIC, NUMERIC) PARALLEL SAFE$_$;
  END IF;
END;
$$;
