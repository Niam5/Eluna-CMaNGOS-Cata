ALTER TABLE spell_template ADD `TotemCategory1` INT UNSIGNED NOT NULL DEFAULT '0' AFTER SpellTotemsId;
ALTER TABLE spell_template ADD `TotemCategory2` INT UNSIGNED NOT NULL DEFAULT '0' AFTER TotemCategory1;
ALTER TABLE spell_template ADD `Totem1` INT UNSIGNED NOT NULL DEFAULT '0' AFTER TotemCategory2;
ALTER TABLE spell_template ADD `Totem2` INT UNSIGNED NOT NULL DEFAULT '0' AFTER Totem1;

UPDATE spell_template INNER JOIN spell_totems ON (spell_template.SpellTotemsId = spell_totems.Id) SET spell_template.TotemCategory1 = spell_totems.TotemCategory1;
UPDATE spell_template INNER JOIN spell_totems ON (spell_template.SpellTotemsId = spell_totems.Id) SET spell_template.TotemCategory2 = spell_totems.TotemCategory2;
UPDATE spell_template INNER JOIN spell_totems ON (spell_template.SpellTotemsId = spell_totems.Id) SET spell_template.Totem1 = spell_totems.Totem1;
UPDATE spell_template INNER JOIN spell_totems ON (spell_template.SpellTotemsId = spell_totems.Id) SET spell_template.Totem2 = spell_totems.Totem2;

ALTER TABLE spell_template DROP COLUMN SpellTotemsId;

DROP TABLE spell_totems;