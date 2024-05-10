ALTER TABLE spell_template ADD `CategoryRecoveryTime` INT NOT NULL DEFAULT '0' AFTER SpellCooldownsId;
ALTER TABLE spell_template ADD `RecoveryTime` INT NOT NULL DEFAULT '0' AFTER CategoryRecoveryTime;
ALTER TABLE spell_template ADD `StartRecoveryTime` INT NOT NULL DEFAULT '0' AFTER RecoveryTime;

UPDATE spell_template INNER JOIN spell_cooldowns ON (spell_template.SpellCooldownsId = spell_cooldowns.Id) SET spell_template.CategoryRecoveryTime = spell_cooldowns.CategoryRecoveryTime;
UPDATE spell_template INNER JOIN spell_cooldowns ON (spell_template.SpellCooldownsId = spell_cooldowns.Id) SET spell_template.RecoveryTime = spell_cooldowns.RecoveryTime;
UPDATE spell_template INNER JOIN spell_cooldowns ON (spell_template.SpellCooldownsId = spell_cooldowns.Id) SET spell_template.StartRecoveryTime = spell_cooldowns.StartRecoveryTime;

ALTER TABLE spell_template DROP COLUMN SpellCooldownsId;

DROP TABLE spell_cooldowns;