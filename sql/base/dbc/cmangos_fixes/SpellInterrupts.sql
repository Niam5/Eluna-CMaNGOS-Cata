ALTER TABLE spell_template ADD `AuraInterruptFlags` INT NOT NULL DEFAULT '0' AFTER SpellinterruptsId;
ALTER TABLE spell_template ADD `ChannelInterruptFlags` INT NOT NULL DEFAULT '0' AFTER AuraInterruptFlags;
ALTER TABLE spell_template ADD `InterruptFlags` INT NOT NULL DEFAULT '0' AFTER ChannelInterruptFlags;

UPDATE spell_template INNER JOIN spell_interrupts ON (spell_template.SpellinterruptsId = spell_interrupts.Id) SET spell_template.AuraInterruptFlags = spell_interrupts.AuraInterruptFlags;
UPDATE spell_template INNER JOIN spell_interrupts ON (spell_template.SpellinterruptsId = spell_interrupts.Id) SET spell_template.ChannelInterruptFlags = spell_interrupts.ChannelInterruptFlags;
UPDATE spell_template INNER JOIN spell_interrupts ON (spell_template.SpellinterruptsId = spell_interrupts.Id) SET spell_template.InterruptFlags = spell_interrupts.InterruptFlags;

ALTER TABLE spell_template DROP COLUMN SpellinterruptsId;

DROP TABLE spell_interrupts;