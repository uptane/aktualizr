-- Don't modify this! Create a new migration instead--see docs/ota-client-guide/modules/ROOT/pages/schema-migrations.adoc
SAVEPOINT MIGRATION;

INSERT INTO meta_types(rowid,meta,meta_string) VALUES(5,5,'offlinesnapshot');
INSERT INTO meta_types(rowid,meta,meta_string) VALUES(6,6,'offlineupdates');

DELETE FROM version;
INSERT INTO version VALUES(26);

RELEASE MIGRATION;
