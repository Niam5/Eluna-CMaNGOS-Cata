/* Old spell_aura_options content*/
INSERT INTO `spell_aura_options` (`Id`,`StackAmount`,`ProcChance`,`ProcCharges`,`ProcFlags`) values 
(8900,0,101,0,0),
(8901,0,15,0,40),
(8902,0,1,0,0);

ALTER TABLE spell_template ADD `StackAmount` INT NOT NULL DEFAULT '0' AFTER SpellAuraOptionsId;
ALTER TABLE spell_template ADD `ProcChance` INT NOT NULL DEFAULT '0' AFTER StackAmount;
ALTER TABLE spell_template ADD `ProcCharges` INT NOT NULL DEFAULT '0' AFTER ProcChance;
ALTER TABLE spell_template ADD `ProcFlags` INT NOT NULL DEFAULT '0'AFTER ProcCharges;

UPDATE spell_template INNER JOIN spell_aura_options ON (spell_template.SpellAuraOptionsId = spell_aura_options.Id) SET spell_template.StackAmount = spell_aura_options.StackAmount;
UPDATE spell_template INNER JOIN spell_aura_options ON (spell_template.SpellAuraOptionsId = spell_aura_options.Id) SET spell_template.ProcChance = spell_aura_options.ProcChance;
UPDATE spell_template INNER JOIN spell_aura_options ON (spell_template.SpellAuraOptionsId = spell_aura_options.Id) SET spell_template.ProcCharges = spell_aura_options.ProcCharges;
UPDATE spell_template INNER JOIN spell_aura_options ON (spell_template.SpellAuraOptionsId = spell_aura_options.Id) SET spell_template.ProcFlags = spell_aura_options.ProcFlags;

ALTER TABLE spell_template DROP COLUMN SpellAuraOptionsId;

DROP TABLE spell_aura_options;
