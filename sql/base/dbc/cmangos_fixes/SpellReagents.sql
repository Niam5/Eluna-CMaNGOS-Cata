ALTER TABLE spell_template ADD `Reagent1` INT NOT NULL DEFAULT '0' AFTER SpellReagentsId;
ALTER TABLE spell_template ADD `Reagent2` INT NOT NULL DEFAULT '0' AFTER Reagent1;
ALTER TABLE spell_template ADD `Reagent3` INT NOT NULL DEFAULT '0' AFTER Reagent2;
ALTER TABLE spell_template ADD `Reagent4` INT NOT NULL DEFAULT '0' AFTER Reagent3;
ALTER TABLE spell_template ADD `Reagent5` INT NOT NULL DEFAULT '0' AFTER Reagent4;
ALTER TABLE spell_template ADD `Reagent6` INT NOT NULL DEFAULT '0' AFTER Reagent5;
ALTER TABLE spell_template ADD `Reagent7` INT NOT NULL DEFAULT '0' AFTER Reagent6;
ALTER TABLE spell_template ADD `Reagent8` INT NOT NULL DEFAULT '0' AFTER Reagent7;
ALTER TABLE spell_template ADD `ReagentCount1` INT NOT NULL DEFAULT '0' AFTER Reagent8;
ALTER TABLE spell_template ADD `ReagentCount2` INT UNSIGNED NOT NULL DEFAULT '0' AFTER ReagentCount1;
ALTER TABLE spell_template ADD `ReagentCount3` INT UNSIGNED NOT NULL DEFAULT '0' AFTER ReagentCount2;
ALTER TABLE spell_template ADD `ReagentCount4` INT UNSIGNED NOT NULL DEFAULT '0' AFTER ReagentCount3;
ALTER TABLE spell_template ADD `ReagentCount5` INT UNSIGNED NOT NULL DEFAULT '0' AFTER ReagentCount4;
ALTER TABLE spell_template ADD `ReagentCount6` INT UNSIGNED NOT NULL DEFAULT '0' AFTER ReagentCount5;
ALTER TABLE spell_template ADD `ReagentCount7` INT UNSIGNED NOT NULL DEFAULT '0' AFTER ReagentCount6;
ALTER TABLE spell_template ADD `ReagentCount8` INT UNSIGNED NOT NULL DEFAULT '0' AFTER ReagentCount7;

UPDATE spell_template INNER JOIN spell_reagents ON (spell_template.SpellReagentsId = spell_reagents.Id) SET spell_template.Reagent1 = spell_reagents.Reagent1;
UPDATE spell_template INNER JOIN spell_reagents ON (spell_template.SpellReagentsId = spell_reagents.Id) SET spell_template.Reagent2 = spell_reagents.Reagent2;
UPDATE spell_template INNER JOIN spell_reagents ON (spell_template.SpellReagentsId = spell_reagents.Id) SET spell_template.Reagent3 = spell_reagents.Reagent3;
UPDATE spell_template INNER JOIN spell_reagents ON (spell_template.SpellReagentsId = spell_reagents.Id) SET spell_template.Reagent4 = spell_reagents.Reagent4;
UPDATE spell_template INNER JOIN spell_reagents ON (spell_template.SpellReagentsId = spell_reagents.Id) SET spell_template.Reagent5 = spell_reagents.Reagent5;
UPDATE spell_template INNER JOIN spell_reagents ON (spell_template.SpellReagentsId = spell_reagents.Id) SET spell_template.Reagent6 = spell_reagents.Reagent6;
UPDATE spell_template INNER JOIN spell_reagents ON (spell_template.SpellReagentsId = spell_reagents.Id) SET spell_template.Reagent7 = spell_reagents.Reagent7;
UPDATE spell_template INNER JOIN spell_reagents ON (spell_template.SpellReagentsId = spell_reagents.Id) SET spell_template.Reagent8 = spell_reagents.Reagent8;

UPDATE spell_template INNER JOIN spell_reagents ON (spell_template.SpellReagentsId = spell_reagents.Id) SET spell_template.ReagentCount1 = spell_reagents.ReagentCount1;
UPDATE spell_template INNER JOIN spell_reagents ON (spell_template.SpellReagentsId = spell_reagents.Id) SET spell_template.ReagentCount2 = spell_reagents.ReagentCount2;
UPDATE spell_template INNER JOIN spell_reagents ON (spell_template.SpellReagentsId = spell_reagents.Id) SET spell_template.ReagentCount3 = spell_reagents.ReagentCount3;
UPDATE spell_template INNER JOIN spell_reagents ON (spell_template.SpellReagentsId = spell_reagents.Id) SET spell_template.ReagentCount4 = spell_reagents.ReagentCount4;
UPDATE spell_template INNER JOIN spell_reagents ON (spell_template.SpellReagentsId = spell_reagents.Id) SET spell_template.ReagentCount5 = spell_reagents.ReagentCount5;
UPDATE spell_template INNER JOIN spell_reagents ON (spell_template.SpellReagentsId = spell_reagents.Id) SET spell_template.ReagentCount6 = spell_reagents.ReagentCount6;
UPDATE spell_template INNER JOIN spell_reagents ON (spell_template.SpellReagentsId = spell_reagents.Id) SET spell_template.ReagentCount7 = spell_reagents.ReagentCount7;
UPDATE spell_template INNER JOIN spell_reagents ON (spell_template.SpellReagentsId = spell_reagents.Id) SET spell_template.ReagentCount8 = spell_reagents.ReagentCount8;

ALTER TABLE spell_template DROP COLUMN SpellReagentsId;

DROP TABLE spell_reagents;