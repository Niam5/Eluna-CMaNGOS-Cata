ALTER TABLE db_version CHANGE COLUMN required_c0003_01_mangos_spell_data_fixes required_c0003_02_mangos_quest_data bit;

INSERT INTO creature_questrelation (id, quest) SELECT entry, quest FROM `quest_relations` WHERE `actor` = 0 AND `role` = 0;
INSERT INTO creature_involvedrelation (id, quest) SELECT entry, quest FROM `quest_relations` WHERE `actor` = 0 AND `role` = 1;

INSERT INTO gameobject_questrelation (id, quest) SELECT entry, quest FROM `quest_relations` WHERE `actor` = 1 AND `role` = 0;
INSERT INTO gameobject_involvedrelation (id, quest) SELECT entry, quest FROM `quest_relations` WHERE `actor` = 1 AND `role` = 1;

INSERT INTO areatrigger_involvedrelation (id, quest) SELECT entry, quest FROM `quest_relations` WHERE `actor` = 2;

DROP TABLE quest_relations;