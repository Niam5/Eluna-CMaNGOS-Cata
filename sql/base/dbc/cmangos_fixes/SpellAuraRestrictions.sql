ALTER TABLE spell_template ADD `CasterAuraState` INT NOT NULL DEFAULT '0' AFTER SpellAuraRestrictionsId;
ALTER TABLE spell_template ADD `TargetAuraState` INT NOT NULL DEFAULT '0' AFTER CasterAuraState;
ALTER TABLE spell_template ADD `CasterAuraStateNot` INT NOT NULL DEFAULT '0' AFTER TargetAuraState;
ALTER TABLE spell_template ADD `TargetAuraStateNot` INT NOT NULL DEFAULT '0'AFTER CasterAuraStateNot;
ALTER TABLE spell_template ADD `casterAuraSpell` INT NOT NULL DEFAULT '0'AFTER TargetAuraStateNot;
ALTER TABLE spell_template ADD `targetAuraSpell` INT NOT NULL DEFAULT '0'AFTER casterAuraSpell;
ALTER TABLE spell_template ADD `excludeCasterAuraSpell` INT NOT NULL DEFAULT '0'AFTER targetAuraSpell;
ALTER TABLE spell_template ADD `excludeTargetAuraSpell` INT NOT NULL DEFAULT '0'AFTER excludeCasterAuraSpell;

UPDATE spell_template INNER JOIN spell_aura_restrictions ON (spell_template.SpellAuraRestrictionsId = spell_aura_restrictions.Id) SET spell_template.CasterAuraState = spell_aura_restrictions.CasterAuraState;
UPDATE spell_template INNER JOIN spell_aura_restrictions ON (spell_template.SpellAuraRestrictionsId = spell_aura_restrictions.Id) SET spell_template.TargetAuraState = spell_aura_restrictions.TargetAuraState;
UPDATE spell_template INNER JOIN spell_aura_restrictions ON (spell_template.SpellAuraRestrictionsId = spell_aura_restrictions.Id) SET spell_template.CasterAuraStateNot = spell_aura_restrictions.CasterAuraStateNot;
UPDATE spell_template INNER JOIN spell_aura_restrictions ON (spell_template.SpellAuraRestrictionsId = spell_aura_restrictions.Id) SET spell_template.TargetAuraStateNot = spell_aura_restrictions.TargetAuraStateNot;
UPDATE spell_template INNER JOIN spell_aura_restrictions ON (spell_template.SpellAuraRestrictionsId = spell_aura_restrictions.Id) SET spell_template.casterAuraSpell = spell_aura_restrictions.casterAuraSpell;
UPDATE spell_template INNER JOIN spell_aura_restrictions ON (spell_template.SpellAuraRestrictionsId = spell_aura_restrictions.Id) SET spell_template.targetAuraSpell = spell_aura_restrictions.targetAuraSpell;
UPDATE spell_template INNER JOIN spell_aura_restrictions ON (spell_template.SpellAuraRestrictionsId = spell_aura_restrictions.Id) SET spell_template.excludeCasterAuraSpell = spell_aura_restrictions.excludeCasterAuraSpell;
UPDATE spell_template INNER JOIN spell_aura_restrictions ON (spell_template.SpellAuraRestrictionsId = spell_aura_restrictions.Id) SET spell_template.excludeTargetAuraSpell = spell_aura_restrictions.excludeTargetAuraSpell;

ALTER TABLE spell_template DROP COLUMN SpellAuraRestrictionsId;

DROP TABLE spell_aura_restrictions;