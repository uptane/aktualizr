-- Don't modify this! Create a new migration instead--see docs/ota-client-guide/modules/ROOT/pages/schema-migrations.adoc
SAVEPOINT ROLLBACK_MIGRATION;

DELETE FROM meta_types WHERE meta_string='offlinesnapshot';
DELETE FROM meta_types WHERE meta_string='offlineupdates';

DELETE FROM version;
INSERT INTO version VALUES(25);

RELEASE ROLLBACK_MIGRATION;
