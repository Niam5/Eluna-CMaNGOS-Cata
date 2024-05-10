ALTER TABLE spell_template ADD `FacingCasterFlags` INT NOT NULL DEFAULT '0' AFTER SpellCastingRequirementsId;
ALTER TABLE spell_template ADD `AreaGroupId` INT NOT NULL DEFAULT '0' AFTER FacingCasterFlags;
ALTER TABLE spell_template ADD `RequiresSpellFocus` INT NOT NULL DEFAULT '0' AFTER AreaGroupId;

UPDATE spell_template INNER JOIN spell_casting_requirements ON (spell_template.SpellCastingRequirementsId = spell_casting_requirements.Id) SET spell_template.FacingCasterFlags = spell_casting_requirements.FacingCasterFlags;
UPDATE spell_template INNER JOIN spell_casting_requirements ON (spell_template.SpellCastingRequirementsId = spell_casting_requirements.Id) SET spell_template.AreaGroupId = spell_casting_requirements.AreaGroupId;
UPDATE spell_template INNER JOIN spell_casting_requirements ON (spell_template.SpellCastingRequirementsId = spell_casting_requirements.Id) SET spell_template.RequiresSpellFocus = spell_casting_requirements.RequiresSpellFocus;

ALTER TABLE spell_template DROP COLUMN SpellCastingRequirementsId;

DROP TABLE spell_casting_requirements;