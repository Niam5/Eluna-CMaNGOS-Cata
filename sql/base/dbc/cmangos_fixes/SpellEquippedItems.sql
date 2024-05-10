ALTER TABLE spell_template ADD `EquippedItemClass` INT NOT NULL DEFAULT '-1' AFTER SpellEquippedItemsId;
ALTER TABLE spell_template ADD `EquippedItemInventoryTypeMask` INT NOT NULL DEFAULT '0' AFTER EquippedItemClass;
ALTER TABLE spell_template ADD `EquippedItemSubClassMask` INT NOT NULL DEFAULT '0' AFTER EquippedItemInventoryTypeMask;

UPDATE spell_template INNER JOIN spell_equipped_items ON (spell_template.SpellEquippedItemsId = spell_equipped_items.Id) SET spell_template.EquippedItemClass = spell_equipped_items.EquippedItemClass;
UPDATE spell_template INNER JOIN spell_equipped_items ON (spell_template.SpellEquippedItemsId = spell_equipped_items.Id) SET spell_template.EquippedItemInventoryTypeMask = spell_equipped_items.EquippedItemInventoryTypeMask;
UPDATE spell_template INNER JOIN spell_equipped_items ON (spell_template.SpellEquippedItemsId = spell_equipped_items.Id) SET spell_template.EquippedItemSubClassMask = spell_equipped_items.EquippedItemSubClassMask;

UPDATE spell_template SET EquippedItemClass = -1 WHERE EquippedItemClass = 0;
UPDATE spell_template SET EquippedItemClass = 0 WHERE Id = 57643;

ALTER TABLE spell_template DROP COLUMN SpellEquippedItemsId;

DROP TABLE spell_equipped_items;