ALTER TABLE spell_template ADD `MaxTargetRadius` FLOAT NOT NULL DEFAULT '0' AFTER SpellTargetRestrictionsId;
ALTER TABLE spell_template ADD `MaxAffectedTargets` INT UNSIGNED NOT NULL DEFAULT '0' AFTER MaxTargetRadius;
ALTER TABLE spell_template ADD `MaxTargetLevel` INT UNSIGNED NOT NULL DEFAULT '0' AFTER MaxAffectedTargets;
ALTER TABLE spell_template ADD `TargetCreatureType` INT UNSIGNED NOT NULL DEFAULT '0' AFTER MaxTargetLevel;
ALTER TABLE spell_template ADD `Targets` INT UNSIGNED NOT NULL DEFAULT '0' AFTER TargetCreatureType;

UPDATE spell_template INNER JOIN spell_target_restrictions ON (spell_template.SpellTargetRestrictionsId = spell_target_restrictions.ID) SET spell_template.MaxTargetRadius = spell_target_restrictions.MaxTargetRadius;
UPDATE spell_template INNER JOIN spell_target_restrictions ON (spell_template.SpellTargetRestrictionsId = spell_target_restrictions.ID) SET spell_template.MaxAffectedTargets = spell_target_restrictions.MaxAffectedTargets;
UPDATE spell_template INNER JOIN spell_target_restrictions ON (spell_template.SpellTargetRestrictionsId = spell_target_restrictions.ID) SET spell_template.MaxTargetLevel = spell_target_restrictions.MaxTargetLevel;
UPDATE spell_template INNER JOIN spell_target_restrictions ON (spell_template.SpellTargetRestrictionsId = spell_target_restrictions.ID) SET spell_template.TargetCreatureType = spell_target_restrictions.TargetCreatureType;
UPDATE spell_template INNER JOIN spell_target_restrictions ON (spell_template.SpellTargetRestrictionsId = spell_target_restrictions.ID) SET spell_template.Targets = spell_target_restrictions.Targets;

ALTER TABLE spell_template DROP COLUMN SpellTargetRestrictionsId;

DROP TABLE spell_target_restrictions;