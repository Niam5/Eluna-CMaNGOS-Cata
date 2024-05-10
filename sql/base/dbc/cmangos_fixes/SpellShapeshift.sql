ALTER TABLE spell_template ADD `Stances` INT UNSIGNED NOT NULL DEFAULT '0' AFTER AttributesEx10;
ALTER TABLE spell_template ADD `StancesNot` INT UNSIGNED NOT NULL DEFAULT '0' AFTER Stances;

UPDATE spell_template INNER JOIN spell_shapeshift ON (spell_template.SpellShapeshiftId = spell_shapeshift.Id) SET spell_template.Stances = ABS(spell_shapeshift.Stances);
UPDATE spell_template INNER JOIN spell_shapeshift ON (spell_template.SpellShapeshiftId = spell_shapeshift.Id) SET spell_template.StancesNot = ABS(spell_shapeshift.StancesNot);


ALTER TABLE spell_template DROP COLUMN SpellShapeshiftId;

DROP TABLE spell_shapeshift;