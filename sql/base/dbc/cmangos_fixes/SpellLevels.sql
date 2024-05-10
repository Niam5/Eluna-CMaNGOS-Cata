ALTER TABLE spell_template ADD `BaseLevel` INT UNSIGNED NOT NULL DEFAULT '0' AFTER InterruptFlags;
ALTER TABLE spell_template ADD `MaxLevel` INT UNSIGNED NOT NULL DEFAULT '0' AFTER BaseLevel;
ALTER TABLE spell_template ADD `SpellLevel` INT UNSIGNED NOT NULL DEFAULT '0' AFTER MaxLevel;

UPDATE spell_template INNER JOIN spell_levels ON (spell_template.SpellLevelsId = spell_levels.Id) SET spell_template.MaxLevel = spell_levels.maxLevel;
UPDATE spell_template INNER JOIN spell_levels ON (spell_template.SpellLevelsId = spell_levels.Id) SET spell_template.BaseLevel = spell_levels.baseLevel;
UPDATE spell_template INNER JOIN spell_levels ON (spell_template.SpellLevelsId = spell_levels.Id) SET spell_template.SpellLevel = spell_levels.spellLevel;

ALTER TABLE spell_template DROP COLUMN SpellLevelsId;

DROP TABLE spell_levels;