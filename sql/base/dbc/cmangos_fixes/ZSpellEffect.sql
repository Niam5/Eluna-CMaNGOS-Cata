ALTER TABLE spell_template ADD `Effect1` INT UNSIGNED NOT NULL DEFAULT '0' AFTER Totem2;
ALTER TABLE spell_template ADD `Effect2` INT UNSIGNED NOT NULL DEFAULT '0' AFTER Effect1;
ALTER TABLE spell_template ADD `Effect3` INT UNSIGNED NOT NULL DEFAULT '0' AFTER Effect2;
ALTER TABLE spell_template ADD `EffectDieSides1` INT NOT NULL DEFAULT '0' AFTER Effect3;
ALTER TABLE spell_template ADD `EffectDieSides2` INT NOT NULL DEFAULT '0' AFTER EffectDieSides1;
ALTER TABLE spell_template ADD `EffectDieSides3` INT NOT NULL DEFAULT '0' AFTER EffectDieSides2;
ALTER TABLE spell_template ADD `EffectRealPointsPerLevel1` FLOAT NOT NULL DEFAULT '0' AFTER EffectDieSides3;
ALTER TABLE spell_template ADD `EffectRealPointsPerLevel2` FLOAT NOT NULL DEFAULT '0' AFTER EffectRealPointsPerLevel1;
ALTER TABLE spell_template ADD `EffectRealPointsPerLevel3` FLOAT NOT NULL DEFAULT '0' AFTER EffectRealPointsPerLevel2;
ALTER TABLE spell_template ADD `EffectBasePoints1` INT NOT NULL DEFAULT '0' AFTER EffectRealPointsPerLevel3;
ALTER TABLE spell_template ADD `EffectBasePoints2` INT NOT NULL DEFAULT '0' AFTER EffectBasePoints1;
ALTER TABLE spell_template ADD `EffectBasePoints3` INT NOT NULL DEFAULT '0' AFTER EffectBasePoints2;
ALTER TABLE spell_template ADD `EffectBonusMultiplier1` FLOAT NOT NULL DEFAULT '0' AFTER EffectBasePoints3;
ALTER TABLE spell_template ADD `EffectBonusMultiplier2` FLOAT NOT NULL DEFAULT '0' AFTER EffectBonusMultiplier1;
ALTER TABLE spell_template ADD `EffectBonusMultiplier3` FLOAT NOT NULL DEFAULT '0' AFTER EffectBonusMultiplier2;
ALTER TABLE spell_template ADD `EffectMechanic1` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectBonusMultiplier3;
ALTER TABLE spell_template ADD `EffectMechanic2` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectMechanic1;
ALTER TABLE spell_template ADD `EffectMechanic3` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectMechanic2;
ALTER TABLE spell_template ADD `EffectImplicitTargetA1` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectMechanic3;
ALTER TABLE spell_template ADD `EffectImplicitTargetA2` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectImplicitTargetA1;
ALTER TABLE spell_template ADD `EffectImplicitTargetA3` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectImplicitTargetA2;
ALTER TABLE spell_template ADD `EffectImplicitTargetB1` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectImplicitTargetA3;
ALTER TABLE spell_template ADD `EffectImplicitTargetB2` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectImplicitTargetB1;
ALTER TABLE spell_template ADD `EffectImplicitTargetB3` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectImplicitTargetB2;
ALTER TABLE spell_template ADD `EffectRadiusIndex1` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectImplicitTargetB3;
ALTER TABLE spell_template ADD `EffectRadiusIndex2` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectRadiusIndex1;
ALTER TABLE spell_template ADD `EffectRadiusIndex3` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectRadiusIndex2;
ALTER TABLE spell_template ADD `EffectRadiusMaxIndex1` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectRadiusIndex3;
ALTER TABLE spell_template ADD `EffectRadiusMaxIndex2` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectRadiusMaxIndex1;
ALTER TABLE spell_template ADD `EffectRadiusMaxIndex3` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectRadiusMaxIndex2;
ALTER TABLE spell_template ADD `EffectApplyAuraName1` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectRadiusMaxIndex3;
ALTER TABLE spell_template ADD `EffectApplyAuraName2` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectApplyAuraName1;
ALTER TABLE spell_template ADD `EffectApplyAuraName3` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectApplyAuraName2;
ALTER TABLE spell_template ADD `EffectAmplitude1` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectApplyAuraName3;
ALTER TABLE spell_template ADD `EffectAmplitude2` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectAmplitude1;
ALTER TABLE spell_template ADD `EffectAmplitude3` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectAmplitude2;
ALTER TABLE spell_template ADD `EffectMultipleValue1` FLOAT NOT NULL DEFAULT '0' AFTER EffectAmplitude3;
ALTER TABLE spell_template ADD `EffectMultipleValue2` FLOAT NOT NULL DEFAULT '0' AFTER EffectMultipleValue1;
ALTER TABLE spell_template ADD `EffectMultipleValue3` FLOAT NOT NULL DEFAULT '0' AFTER EffectMultipleValue2;
ALTER TABLE spell_template ADD `EffectDamageMultiplier1` FLOAT NOT NULL DEFAULT '0' AFTER EffectMultipleValue3;
ALTER TABLE spell_template ADD `EffectDamageMultiplier2` FLOAT NOT NULL DEFAULT '0' AFTER EffectDamageMultiplier1;
ALTER TABLE spell_template ADD `EffectDamageMultiplier3` FLOAT NOT NULL DEFAULT '0' AFTER EffectDamageMultiplier2;
ALTER TABLE spell_template ADD `EffectChainTarget1` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectDamageMultiplier3;
ALTER TABLE spell_template ADD `EffectChainTarget2` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectChainTarget1;
ALTER TABLE spell_template ADD `EffectChainTarget3` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectChainTarget2;
ALTER TABLE spell_template ADD `EffectItemType1` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectChainTarget3;
ALTER TABLE spell_template ADD `EffectItemType2` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectItemType1;
ALTER TABLE spell_template ADD `EffectItemType3` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectItemType2;
ALTER TABLE spell_template ADD `EffectMiscValue1` INT NOT NULL DEFAULT '0' AFTER EffectItemType3;
ALTER TABLE spell_template ADD `EffectMiscValue2` INT NOT NULL DEFAULT '0' AFTER EffectMiscValue1;
ALTER TABLE spell_template ADD `EffectMiscValue3` INT NOT NULL DEFAULT '0' AFTER EffectMiscValue2;
ALTER TABLE spell_template ADD `EffectMiscValueB1` INT NOT NULL DEFAULT '0' AFTER EffectMiscValue3;
ALTER TABLE spell_template ADD `EffectMiscValueB2` INT NOT NULL DEFAULT '0' AFTER EffectMiscValueB1;
ALTER TABLE spell_template ADD `EffectMiscValueB3` INT NOT NULL DEFAULT '0' AFTER EffectMiscValueB2;
ALTER TABLE spell_template ADD `EffectTriggerSpell1` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectMiscValueB3;
ALTER TABLE spell_template ADD `EffectTriggerSpell2` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectTriggerSpell1;
ALTER TABLE spell_template ADD `EffectTriggerSpell3` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectTriggerSpell2;
ALTER TABLE spell_template ADD `EffectPointsPerComboPoint1` FLOAT NOT NULL DEFAULT '0' AFTER EffectTriggerSpell3;
ALTER TABLE spell_template ADD `EffectPointsPerComboPoint2` FLOAT NOT NULL DEFAULT '0' AFTER EffectPointsPerComboPoint1;
ALTER TABLE spell_template ADD `EffectPointsPerComboPoint3` FLOAT NOT NULL DEFAULT '0' AFTER EffectPointsPerComboPoint2;
ALTER TABLE spell_template ADD `EffectSpellClassMask1_1` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectPointsPerComboPoint3;
ALTER TABLE spell_template ADD `EffectSpellClassMask1_2` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectSpellClassMask1_1;
ALTER TABLE spell_template ADD `EffectSpellClassMask1_3` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectSpellClassMask1_2;
ALTER TABLE spell_template ADD `EffectSpellClassMask2_1` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectSpellClassMask1_3;
ALTER TABLE spell_template ADD `EffectSpellClassMask2_2` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectSpellClassMask2_1;
ALTER TABLE spell_template ADD `EffectSpellClassMask2_3` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectSpellClassMask2_2;
ALTER TABLE spell_template ADD `EffectSpellClassMask3_1` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectSpellClassMask2_3;
ALTER TABLE spell_template ADD `EffectSpellClassMask3_2` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectSpellClassMask3_1;
ALTER TABLE spell_template ADD `EffectSpellClassMask3_3` INT UNSIGNED NOT NULL DEFAULT '0' AFTER EffectSpellClassMask3_2;

UPDATE spell_effect SET EffectTriggerSpell = 0 WHERE EffectTriggerSpell = -1;

UPDATE spell_template AS t INNER JOIN spell_effect AS e ON t.Id = e.EffectSpellId SET t.Effect1 = e.Effect, t.EffectMultipleValue1 = e.EffectAmplitude, t.EffectApplyAuraName1 = e.EffectAura, t.EffectAmplitude1 = e.EffectAuraPeriod,
t.EffectBasePoints1 = e.EffectBasePoints, t.EffectBonusMultiplier1 = e.EffectBonus, t.EffectDamageMultiplier1 = e.EffectChainAmplitude, t.EffectChainTarget1 = e.EffectChaintargets, t.EffectDieSides1 = e.EffectDieSides, t.EffectItemType1 = e.EffectItemType, t.EffectMechanic1 = e.EffectMechanic,
t.EffectMiscValue1 = e.EffectMiscValue, t.EffectMiscValueB1 = e.EffectMiscValueB, t.EffectPointsPerComboPoint1 = e.EffectPointsPerCombo, t.EffectRadiusIndex1 = e.EffectRadiusIndex, t.EffectRadiusMaxIndex1 = e.EffectRadiusMaxIndex, t.EffectRealPointsPerLevel1 = e.EffectRealPointsPerLevel, t.EffectSpellClassMask1_1 = e.EffectSpellClassMask1,
t.EffectSpellClassMask1_2 = e.EffectSpellClassMask2, t.EffectSpellClassMask1_3 = e.EffectSpellClassMask3, t.EffectTriggerSpell1 = e.EffectTriggerSpell, t.EffectImplicitTargetA1 = e.ImplicitTargetA, t.EffectImplicitTargetB1 = e.ImplicitTargetB WHERE e.EffectIndex = 0;

UPDATE spell_template AS t INNER JOIN spell_effect AS e ON t.Id = e.EffectSpellId SET t.Effect2 = e.Effect, t.EffectMultipleValue2 = e.EffectAmplitude, t.EffectApplyAuraName2 = e.EffectAura, t.EffectAmplitude2 = e.EffectAuraPeriod,
t.EffectBasePoints2 = e.EffectBasePoints, t.EffectBonusMultiplier2 = e.EffectBonus, t.EffectDamageMultiplier2 = e.EffectChainAmplitude, t.EffectChainTarget2 = e.EffectChaintargets, t.EffectDieSides2 = e.EffectDieSides, t.EffectItemType2 = e.EffectItemType, t.EffectMechanic2 = e.EffectMechanic,
t.EffectMiscValue2 = e.EffectMiscValue, t.EffectMiscValueB2 = e.EffectMiscValueB, t.EffectPointsPerComboPoint2 = e.EffectPointsPerCombo, t.EffectRadiusIndex2 = e.EffectRadiusIndex, t.EffectRadiusMaxIndex2 = e.EffectRadiusMaxIndex, t.EffectRealPointsPerLevel2 = e.EffectRealPointsPerLevel, t.EffectSpellClassMask2_1 = e.EffectSpellClassMask1,
t.EffectSpellClassMask2_2 = e.EffectSpellClassMask2, t.EffectSpellClassMask2_3 = e.EffectSpellClassMask3, t.EffectTriggerSpell2 = e.EffectTriggerSpell, t.EffectImplicitTargetA2 = e.ImplicitTargetA, t.EffectImplicitTargetB2 = e.ImplicitTargetB WHERE e.EffectIndex = 1;

UPDATE spell_template AS t INNER JOIN spell_effect AS e ON t.Id = e.EffectSpellId SET t.Effect3 = e.Effect, t.EffectMultipleValue3 = e.EffectAmplitude, t.EffectApplyAuraName3 = e.EffectAura, t.EffectAmplitude3 = e.EffectAuraPeriod,
t.EffectBasePoints3 = e.EffectBasePoints, t.EffectBonusMultiplier3 = e.EffectBonus, t.EffectDamageMultiplier3 = e.EffectChainAmplitude, t.EffectChainTarget3 = e.EffectChaintargets, t.EffectDieSides3 = e.EffectDieSides, t.EffectItemType3 = e.EffectItemType, t.EffectMechanic3 = e.EffectMechanic,
t.EffectMiscValue3 = e.EffectMiscValue, t.EffectMiscValueB3 = e.EffectMiscValueB, t.EffectPointsPerComboPoint3 = e.EffectPointsPerCombo, t.EffectRadiusIndex3 = e.EffectRadiusIndex, t.EffectRadiusMaxIndex3 = e.EffectRadiusMaxIndex, t.EffectRealPointsPerLevel3 = e.EffectRealPointsPerLevel, t.EffectSpellClassMask3_1 = e.EffectSpellClassMask1,
t.EffectSpellClassMask3_2 = e.EffectSpellClassMask2, t.EffectSpellClassMask3_3 = e.EffectSpellClassMask3, t.EffectTriggerSpell3 = e.EffectTriggerSpell, t.EffectImplicitTargetA3 = e.ImplicitTargetA, t.EffectImplicitTargetB3 = e.ImplicitTargetB WHERE e.EffectIndex = 2;

DROP TABLE spell_effect;