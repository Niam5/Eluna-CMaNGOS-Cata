ALTER TABLE spell_template ADD `Category` INT UNSIGNED NOT NULL DEFAULT '0' AFTER SpellCategoriesId;
ALTER TABLE spell_template ADD `DmgClass` INT UNSIGNED NOT NULL DEFAULT '0' AFTER Category;
ALTER TABLE spell_template ADD `Dispel` INT UNSIGNED NOT NULL DEFAULT '0' AFTER DmgClass;
ALTER TABLE spell_template ADD `Mechanic` INT UNSIGNED NOT NULL DEFAULT '0' AFTER Dispel;
ALTER TABLE spell_template ADD `PreventionType` INT UNSIGNED NOT NULL DEFAULT '0' AFTER Mechanic;
ALTER TABLE spell_template ADD `StartRecoveryCategory` INT UNSIGNED NOT NULL DEFAULT '0' AFTER PreventionType;

UPDATE spell_template INNER JOIN spell_categories ON (spell_template.SpellCategoriesId = spell_categories.Id) SET spell_template.Category = spell_categories.Category;
UPDATE spell_template INNER JOIN spell_categories ON (spell_template.SpellCategoriesId = spell_categories.Id) SET spell_template.DmgClass = spell_categories.DmgClass;
UPDATE spell_template INNER JOIN spell_categories ON (spell_template.SpellCategoriesId = spell_categories.Id) SET spell_template.Dispel = spell_categories.Dispel;
UPDATE spell_template INNER JOIN spell_categories ON (spell_template.SpellCategoriesId = spell_categories.Id) SET spell_template.Mechanic = spell_categories.Mechanic;
UPDATE spell_template INNER JOIN spell_categories ON (spell_template.SpellCategoriesId = spell_categories.Id) SET spell_template.PreventionType = spell_categories.PreventionType;
UPDATE spell_template INNER JOIN spell_categories ON (spell_template.SpellCategoriesId = spell_categories.Id) SET spell_template.StartRecoveryCategory = spell_categories.StartRecoveryCategory;

ALTER TABLE spell_template DROP COLUMN SpellCategoriesId;

DROP TABLE spell_categories;