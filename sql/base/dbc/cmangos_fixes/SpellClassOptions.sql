ALTER TABLE spell_template ADD `SpellFamilyName` INT UNSIGNED NOT NULL DEFAULT '0' AFTER SpellClassOptionsId;
ALTER TABLE spell_template ADD `SpellFamilyFlags` BIGINT UNSIGNED NOT NULL DEFAULT '0' AFTER SpellFamilyName;
ALTER TABLE spell_template ADD `SpellFamilyFlags2` INT UNSIGNED NOT NULL DEFAULT '0' AFTER SpellFamilyFlags;

UPDATE spell_template INNER JOIN spell_class_options ON (spell_template.SpellClassOptionsId = spell_class_options.Id) SET spell_template.SpellFamilyName = spell_class_options.SpellClassSet;
UPDATE spell_template INNER JOIN spell_class_options ON (spell_template.SpellClassOptionsId = spell_class_options.Id) SET spell_template.SpellFamilyFlags = ((spell_class_options.SpellClassMask2 << 32) | SpellClassMask1);
UPDATE spell_template INNER JOIN spell_class_options ON (spell_template.SpellClassOptionsId = spell_class_options.Id) SET spell_template.SpellFamilyFlags2 = spell_class_options.SpellClassMask3;

ALTER TABLE spell_template DROP COLUMN SpellClassOptionsId;

DROP TABLE spell_class_options;