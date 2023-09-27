SET enable_seqscan = off;

CREATE TABLE t (val vector(3));
CREATE INDEX ON t USING ivfflat (val) WITH (lists = 0);
CREATE INDEX ON t USING ivfflat (val) WITH (lists = 32769);

SHOW ivfflat.probes;

DROP TABLE t;
