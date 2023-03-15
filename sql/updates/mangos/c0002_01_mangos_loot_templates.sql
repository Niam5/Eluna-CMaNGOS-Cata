ALTER TABLE db_version CHANGE COLUMN required_c0001_01_mangos_spell_template required_c0002_01_mangos_loot_templates bit;

ALTER TABLE creature_loot_template ADD `type` tinyint NOT NULL DEFAULT '0' AFTER `item`;
ALTER TABLE disenchant_loot_template ADD `type` tinyint NOT NULL DEFAULT '0' AFTER `item`;
ALTER TABLE fishing_loot_template ADD `type` tinyint NOT NULL DEFAULT '0' AFTER `item`;
ALTER TABLE gameobject_loot_template ADD `type` tinyint NOT NULL DEFAULT '0' AFTER `item`;
ALTER TABLE item_loot_template ADD `type` tinyint NOT NULL DEFAULT '0' AFTER `item`;
ALTER TABLE mail_loot_template ADD `type` tinyint NOT NULL DEFAULT '0' AFTER `item`;
ALTER TABLE milling_loot_template ADD `type` tinyint NOT NULL DEFAULT '0' AFTER `item`;
ALTER TABLE pickpocketing_loot_template ADD `type` tinyint NOT NULL DEFAULT '0' AFTER `item`;
ALTER TABLE prospecting_loot_template ADD `type` tinyint NOT NULL DEFAULT '0' AFTER `item`;
ALTER TABLE reference_loot_template ADD `type` tinyint NOT NULL DEFAULT '0' AFTER `item`;
ALTER TABLE skinning_loot_template ADD `type` tinyint NOT NULL DEFAULT '0' AFTER `item`;
ALTER TABLE spell_loot_template ADD `type` tinyint NOT NULL DEFAULT '0' AFTER `item`;