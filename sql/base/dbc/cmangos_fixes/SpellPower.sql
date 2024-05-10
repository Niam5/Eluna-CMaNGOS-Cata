ALTER TABLE spell_template ADD `ManaCost` INT NOT NULL DEFAULT '0' AFTER SpellPowerId;
ALTER TABLE spell_template ADD `ManaCostPerLevel` INT UNSIGNED NOT NULL DEFAULT '0' AFTER ManaCost;
ALTER TABLE spell_template ADD `ManaCostPercentage` INT NOT NULL DEFAULT '0' AFTER ManaCostPerLevel;
ALTER TABLE spell_template ADD `ManaPerSecond` INT UNSIGNED NOT NULL DEFAULT '0' AFTER ManaCostPercentage;
ALTER TABLE spell_template ADD `ManaPerSecondPerLevel` INT NOT NULL DEFAULT '0' AFTER ManaPerSecond;
ALTER TABLE spell_template ADD `ManaCostPercentageFloat` FLOAT NOT NULL DEFAULT '0' AFTER ManaPerSecondPerLevel;

UPDATE spell_template INNER JOIN spell_power ON (spell_template.SpellPowerId = spell_power.Id) SET spell_template.ManaCost = spell_power.ManaCost;
UPDATE spell_template INNER JOIN spell_power ON (spell_template.SpellPowerId = spell_power.Id) SET spell_template.ManaCostPerLevel = spell_power.ManaCostPerLevel;
UPDATE spell_template INNER JOIN spell_power ON (spell_template.SpellPowerId = spell_power.Id) SET spell_template.ManaCostPercentage = spell_power.ManaCostPercentage;
UPDATE spell_template INNER JOIN spell_power ON (spell_template.SpellPowerId = spell_power.Id) SET spell_template.ManaPerSecond = spell_power.ManaPerSecond;
UPDATE spell_template INNER JOIN spell_power ON (spell_template.SpellPowerId = spell_power.Id) SET spell_template.ManaPerSecondPerLevel = spell_power.ManaPerSecondPerLevel;
UPDATE spell_template INNER JOIN spell_power ON (spell_template.SpellPowerId = spell_power.Id) SET spell_template.ManaCostPercentageFloat = spell_power.ManaCostPercentageFloat;

ALTER TABLE spell_template DROP COLUMN SpellPowerId;

DROP TABLE spell_power;