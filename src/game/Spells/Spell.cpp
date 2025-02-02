/*
 * This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "Spells/Spell.h"
#include "Database/DatabaseEnv.h"
#include "Server/WorldPacket.h"
#include "Server/WorldSession.h"
#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"
#include "Server/Opcodes.h"
#include "Log/Log.h"
#include "Maps/MapManager.h"
#include "World/World.h"
#include "Globals/ObjectMgr.h"
#include "Spells/SpellMgr.h"
#include "Entities/Player.h"
#include "Entities/Pet.h"
#include "Entities/Unit.h"
#include "Entities/DynamicObject.h"
#include "Groups/Group.h"
#include "Entities/UpdateData.h"
#include "Globals/ObjectAccessor.h"
#include "Grids/CellImpl.h"
#include "Policies/Singleton.h"
#include "Globals/SharedDefines.h"
#include "Loot/LootMgr.h"
#include "Vmap/VMapFactory.h"
#include "BattleGround/BattleGround.h"
#include "Util/Util.h"
#include "Chat/Chat.h"
#include "Server/DB2Stores.h"
#include "Entities/Vehicle.h"
#include "Entities/TemporarySpawn.h"
#include "MotionGenerators/PathFinder.h"
#include "Entities/ObjectGuid.h"
#ifdef BUILD_ELUNA
#include "LuaEngine/LuaEngine.h"
#endif

extern pEffect SpellEffects[TOTAL_SPELL_EFFECTS];

class PrioritizeManaUnitWraper
{
    public:
        explicit PrioritizeManaUnitWraper(Unit* unit) : i_unit(unit)
        {
            uint32 maxmana = unit->GetMaxPower(POWER_MANA);
            i_percent = maxmana ? unit->GetPower(POWER_MANA) * 100 / maxmana : 101;
        }
        Unit* getUnit() const { return i_unit; }
        uint32 getPercent() const { return i_percent; }
    private:
        Unit* i_unit;
        uint32 i_percent;
};

struct PrioritizeMana
{
    int operator()(PrioritizeManaUnitWraper const& x, PrioritizeManaUnitWraper const& y) const
    {
        return x.getPercent() > y.getPercent();
    }
};

typedef std::priority_queue<PrioritizeManaUnitWraper, std::vector<PrioritizeManaUnitWraper>, PrioritizeMana> PrioritizeManaUnitQueue;

class PrioritizeHealthUnitWraper
{
    public:
        explicit PrioritizeHealthUnitWraper(Unit* unit) : i_unit(unit)
        {
            i_percent = unit->GetHealth() * 100 / unit->GetMaxHealth();
        }
        Unit* getUnit() const { return i_unit; }
        uint32 getPercent() const { return i_percent; }
    private:
        Unit* i_unit;
        uint32 i_percent;
};

struct PrioritizeHealth
{
    int operator()(PrioritizeHealthUnitWraper const& x, PrioritizeHealthUnitWraper const& y) const
    {
        return x.getPercent() > y.getPercent();
    }
};

typedef std::priority_queue<PrioritizeHealthUnitWraper, std::vector<PrioritizeHealthUnitWraper>, PrioritizeHealth> PrioritizeHealthUnitQueue;

bool IsQuestTameSpell(uint32 spellId)
{
    SpellEntry const* spellproto = sSpellTemplate.LookupEntry<SpellEntry>(spellId);
    if (!spellproto)
        return false;

    return spellproto->Effect[EFFECT_INDEX_0] == SPELL_EFFECT_THREAT &&
        spellproto->Effect[EFFECT_INDEX_1] == SPELL_EFFECT_APPLY_AURA && spellproto->EffectApplyAuraName[EFFECT_INDEX_1] == SPELL_AURA_DUMMY;
}

// SpellCastTargets class
// **********************

SpellCastTargets::SpellCastTargets()
{
    m_unitTarget = nullptr;
    m_itemTarget = nullptr;
    m_GOTarget   = nullptr;

    m_itemTargetEntry  = 0;

    m_srcX = m_srcY = m_srcZ = m_destX = m_destY = m_destZ = 0.0f;
    m_strTarget.clear();
    m_targetMask = 0;

    m_elevation = 0.0f;
    m_speed = 0.0f;
}

SpellCastTargets::~SpellCastTargets()
{
}

void SpellCastTargets::setUnitTarget(Unit* target)
{
    if (!target)
        return;

    m_destX = target->GetPositionX();
    m_destY = target->GetPositionY();
    m_destZ = target->GetPositionZ();
    m_unitTarget = target;
    m_unitTargetGUID = target->GetObjectGuid();
    m_targetMask |= TARGET_FLAG_UNIT;
}

void SpellCastTargets::setDestination(float x, float y, float z)
{
    m_destX = x;
    m_destY = y;
    m_destZ = z;
    m_targetMask |= TARGET_FLAG_DEST_LOCATION;
}

void SpellCastTargets::setSource(float x, float y, float z)
{
    m_srcX = x;
    m_srcY = y;
    m_srcZ = z;
    m_targetMask |= TARGET_FLAG_SOURCE_LOCATION;
}

void SpellCastTargets::setGOTarget(GameObject* target)
{
    m_GOTarget = target;
    m_GOTargetGUID = target->GetObjectGuid();
    //    m_targetMask |= TARGET_FLAG_OBJECT;
}

void SpellCastTargets::setItemTarget(Item* item)
{
    if (!item)
        return;

    m_itemTarget = item;
    m_itemTargetGUID = item->GetObjectGuid();
    m_itemTargetEntry = item->GetEntry();
    m_targetMask |= TARGET_FLAG_ITEM;
}

void SpellCastTargets::setTradeItemTarget(Player* caster)
{
    m_itemTargetGUID = ObjectGuid(uint64(TRADE_SLOT_NONTRADED));
    m_itemTargetEntry = 0;
    m_targetMask |= TARGET_FLAG_TRADE_ITEM;

    Update(caster);
}

void SpellCastTargets::setCorpseTarget(Corpse* corpse)
{
    m_CorpseTargetGUID = corpse->GetObjectGuid();
}

void SpellCastTargets::Update(Unit* caster)
{
    m_GOTarget   = m_GOTargetGUID ? caster->GetMap()->GetGameObject(m_GOTargetGUID) : nullptr;
    m_unitTarget = m_unitTargetGUID ?
                   (m_unitTargetGUID == caster->GetObjectGuid() ? caster : ObjectAccessor::GetUnit(*caster, m_unitTargetGUID)) :
                   nullptr;

    m_itemTarget = nullptr;
    if (caster->GetTypeId() == TYPEID_PLAYER)
    {
        Player* player = ((Player*)caster);

        if (m_targetMask & TARGET_FLAG_ITEM)
            m_itemTarget = player->GetItemByGuid(m_itemTargetGUID);
        else if (m_targetMask & TARGET_FLAG_TRADE_ITEM)
        {
            if (TradeData* pTrade = player->GetTradeData())
                if (m_itemTargetGUID.GetRawValue() < TRADE_SLOT_COUNT)
                    m_itemTarget = pTrade->GetTraderData()->GetItem(TradeSlots(m_itemTargetGUID.GetRawValue()));
        }

        if (m_itemTarget)
            m_itemTargetEntry = m_itemTarget->GetEntry();
    }
}

void SpellCastTargets::read(ByteBuffer& data, Unit* caster)
{
    data >> m_targetMask;

    if (m_targetMask == TARGET_FLAG_SELF)
    {
        m_destX = caster->GetPositionX();
        m_destY = caster->GetPositionY();
        m_destZ = caster->GetPositionZ();
        m_unitTarget = caster;
        m_unitTargetGUID = caster->GetObjectGuid();
        return;
    }

    // TARGET_FLAG_UNK2 is used for non-combat pets, maybe other?
    if (m_targetMask & (TARGET_FLAG_UNIT | TARGET_FLAG_UNK2))
        data >> m_unitTargetGUID.ReadAsPacked();

    if (m_targetMask & (TARGET_FLAG_OBJECT | TARGET_FLAG_GAMEOBJECT_ITEM))
        data >> m_GOTargetGUID.ReadAsPacked();

    if ((m_targetMask & (TARGET_FLAG_ITEM | TARGET_FLAG_TRADE_ITEM)) && caster->GetTypeId() == TYPEID_PLAYER)
        data >> m_itemTargetGUID.ReadAsPacked();

    if (m_targetMask & (TARGET_FLAG_CORPSE_ALLY | TARGET_FLAG_PVP_CORPSE))
        data >> m_CorpseTargetGUID.ReadAsPacked();

    if (m_targetMask & TARGET_FLAG_SOURCE_LOCATION)
    {
        data >> m_srcTransportGUID.ReadAsPacked();
        data >> m_srcX >> m_srcY >> m_srcZ;
        if (!MaNGOS::IsValidMapCoord(m_srcX, m_srcY, m_srcZ))
            throw ByteBufferException(false, data.rpos(), 0, data.size());
    }

    if (m_targetMask & TARGET_FLAG_DEST_LOCATION)
    {
        data >> m_destTransportGUID.ReadAsPacked();
        data >> m_destX >> m_destY >> m_destZ;
        if (!MaNGOS::IsValidMapCoord(m_destX, m_destY, m_destZ))
            throw ByteBufferException(false, data.rpos(), 0, data.size());
    }

    if (m_targetMask & TARGET_FLAG_STRING)
        data >> m_strTarget;

    // find real units/GOs
    Update(caster);
}

void SpellCastTargets::write(ByteBuffer& data) const
{
    data << uint32(m_targetMask);

    if (m_targetMask & (TARGET_FLAG_UNIT | TARGET_FLAG_PVP_CORPSE | TARGET_FLAG_OBJECT | TARGET_FLAG_CORPSE_ALLY | TARGET_FLAG_UNK2))
    {
        if (m_targetMask & TARGET_FLAG_UNIT)
        {
            if (m_unitTarget)
                data << m_unitTarget->GetPackGUID();
            else
                data << uint8(0);
        }
        else if (m_targetMask & TARGET_FLAG_OBJECT)
        {
            if (m_GOTarget)
                data << m_GOTarget->GetPackGUID();
            else
                data << uint8(0);
        }
        else if (m_targetMask & (TARGET_FLAG_CORPSE_ALLY | TARGET_FLAG_PVP_CORPSE))
            data << m_CorpseTargetGUID.WriteAsPacked();
        else
            data << uint8(0);
    }

    if (m_targetMask & (TARGET_FLAG_ITEM | TARGET_FLAG_TRADE_ITEM))
    {
        if (m_itemTarget)
            data << m_itemTarget->GetPackGUID();
        else
            data << uint8(0);
    }

    if (m_targetMask & TARGET_FLAG_SOURCE_LOCATION)
    {
        data << m_srcTransportGUID.WriteAsPacked();
        data << m_srcX << m_srcY << m_srcZ;
    }

    if (m_targetMask & TARGET_FLAG_DEST_LOCATION)
    {
        data << m_destTransportGUID.WriteAsPacked();
        data << m_destX << m_destY << m_destZ;
    }

    if (m_targetMask & TARGET_FLAG_STRING)
        data << m_strTarget;
}

void SpellCastTargets::ReadAdditionalData(WorldPacket& data, uint8& cast_flags)
{
    if (cast_flags & 0x02)              // has trajectory data
    {
        data >> m_elevation >> m_speed;

        bool hasMovementData;
        data >> hasMovementData;
        if (hasMovementData)
        {
            MovementInfo mi;
            data >> mi;
            setSource(mi.GetPos()->x, mi.GetPos()->y, mi.GetPos()->z);
        }
    }
    else if (cast_flags & 0x08)         // has archaeology weight
    {
        uint32 count;
        uint8 type;
        data >> count;
        for (uint32 i = 0; i < count; ++i)
        {
            data >> type;
            switch (type)
            {
                case 1:                         // Fragments
                    data >> Unused<uint32>();   // Currency entry
                    data >> Unused<uint32>();   // Currency count
                    break;
                case 2:                         // Keystones
                    data >> Unused<uint32>();   // Item entry
                    data >> Unused<uint32>();   // Item count
                    break;
            }
        }
    }
}

// SpellLog class
// **************

void SpellLog::Initialize()
{
    m_spellLogData.Initialize(SMSG_SPELLLOGEXECUTE);
    //m_spellLogData << m_spell->GetCaster()->GetPackGUID();
    m_spellLogData.appendPackGUID(m_spell->GetCaster()->GetObjectGuid().GetRawValue());
    m_spellLogData << uint32(m_spell->m_spellInfo->Id);
    m_spellLogDataEffectsCounterPos = m_spellLogData.wpos();
    m_spellLogData << uint32(0);                            //placeholder
    m_spellLogDataEffectsCounter = 0;
}

void SpellLog::FinalizePrevious()
{
    if (m_currentEffect < uint32(TOTAL_SPELL_EFFECTS))
    {
        // not first effect processed so we have to finalize the previous one by putting correct amount of targets
        m_spellLogData.put<uint32>(m_spellLogDataTargetsCounterPos, m_spellLogDataTargetsCounter);
        m_currentEffect = uint32(TOTAL_SPELL_EFFECTS);
    }
}

void SpellLog::SetCurrentEffect(uint32 effect)
{
    // check if its same effect so we don't have to put header
    if (effect == m_currentEffect)
    {
        ++m_spellLogDataTargetsCounter;
        return;
    }

    FinalizePrevious();

    m_spellLogData << effect;
    m_spellLogDataTargetsCounterPos = m_spellLogData.wpos();
    m_spellLogData << uint32(0);                            // placeholder for target counter
    m_currentEffect = effect;
    m_spellLogDataTargetsCounter = 1;
}

void SpellLog::SendToSet()
{
    if (!m_spellLogDataEffectsCounter)
        return;

    // check if one of previous target is not finalized
    FinalizePrevious();

    // put total effect counter in packet
    m_spellLogData.put<uint32>(m_spellLogDataEffectsCounterPos, m_spellLogDataEffectsCounter);
    m_spell->GetCaster()->SendMessageToSet(m_spellLogData, true);

    // make it ready for another log if need
    Initialize();
}

// Spell class
// ***********

Spell::Spell(Unit* caster, SpellEntry const* info, uint32 triggeredFlags, ObjectGuid originalCasterGUID, SpellEntry const* triggeredBy) :
    m_spellEvent(nullptr), m_spellLog(this)
{
    MANGOS_ASSERT(caster != nullptr && info != nullptr);
    MANGOS_ASSERT(info == sSpellTemplate.LookupEntry<SpellEntry>(info->Id) && "`info` must be pointer to sSpellTemplate element");

    if (info->SpellDifficultyId && caster->IsInWorld() && caster->GetMap()->IsDungeon())
    {
        if (SpellEntry const* spellEntry = GetSpellEntryByDifficulty(info->SpellDifficultyId, caster->GetMap()->GetDifficulty(), caster->GetMap()->IsRaid()))
            m_spellInfo = spellEntry;
        else
            m_spellInfo = info;
    }
    else
        m_spellInfo = info;

    m_triggeredBySpellInfo = triggeredBy;
    m_caster = caster;
    m_referencedFromCurrentSpell = false;
    m_executedCurrently = false;
    m_delayStart = 0;
    m_delayAtDamageCount = 0;

    m_applyMultiplierMask = 0;

    // Get data for type of attack
    m_attackType = GetWeaponAttackType(m_spellInfo);

    m_spellSchoolMask = GetSpellSchoolMask(info);           // Can be override for some spell (wand shoot for example)

    if (m_attackType == RANGED_ATTACK)
    {
        // wand case
        if ((m_caster->getClassMask() & CLASSMASK_WAND_USERS) != 0 && m_caster->GetTypeId() == TYPEID_PLAYER)
        {
            if (Item* pItem = ((Player*)m_caster)->GetWeaponForAttack(RANGED_ATTACK))
                m_spellSchoolMask = SpellSchoolMask(1 << pItem->GetProto()->DamageType);
        }
    }
    // Set health leech amount to zero
    m_healthLeech = 0;

    m_originalCasterGUID = originalCasterGUID ? originalCasterGUID : m_caster->GetObjectGuid();

    UpdateOriginalCasterPointer();

    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
        m_currentBasePoints[i] = m_spellInfo->CalculateSimpleValue(SpellEffectIndex(i));

    m_spellState = SPELL_STATE_CREATED;

    m_castPositionX = m_castPositionY = m_castPositionZ = 0;
    m_TriggerSpells.clear();
    m_preCastSpells.clear();
    m_IsTriggeredSpell = triggeredFlags & TRIGGERED_OLD_TRIGGERED;
    // m_AreaAura = false;
    m_CastItem = nullptr;

    unitTarget = nullptr;
    itemTarget = nullptr;
    gameObjTarget = nullptr;
    focusObject = nullptr;
    m_cast_count = 0;
    m_glyphIndex = 0;
    m_triggeredByAuraSpell  = nullptr;
    m_spellAuraHolder = nullptr;

    // Auto Shot & Shoot (wand)
    m_autoRepeat = IsAutoRepeatRangedSpell(m_spellInfo);

    m_runesState = 0;
    m_powerCost = 0;                                        // setup to correct value in Spell::prepare, don't must be used before.
    m_usedHolyPower = 0;
    m_casttime = 0;                                         // setup to correct value in Spell::prepare, don't must be used before.
    m_timer = 0;                                            // will set to cast time in prepare
    m_duration = 0;

    m_needAliveTargetMask = 0;

    m_ignoreHitResult = !!(triggeredFlags & TRIGGERED_IGNORE_HIT_CALCULATION);
    m_ignoreUnselectableTarget = m_IsTriggeredSpell || (triggeredFlags & TRIGGERED_IGNORE_UNSELECTABLE_FLAG) != 0;
    m_ignoreUnattackableTarget = !!(triggeredFlags & TRIGGERED_IGNORE_UNATTACKABLE_FLAG);
    m_triggerAutorepeat = !!(triggeredFlags & TRIGGERED_AUTOREPEAT);

    m_reflectable = IsReflectableSpell(m_spellInfo);
    m_spellFlags = SPELL_FLAG_NORMAL;

    m_affectedTargetCount = GetAffectedTargets(m_spellInfo, caster);

    m_scriptValue = 0;

    CleanupTargetList();

    m_spellLog.Initialize();
    m_needSpellLog = (m_spellInfo->Attributes & (SPELL_ATTR_HIDE_IN_COMBAT_LOG | SPELL_ATTR_HIDDEN_CLIENTSIDE)) == 0;
}

Spell::~Spell()
{
}

template<typename T>
WorldObject* Spell::FindCorpseUsing()
{
    // non-standard target selection
    SpellRangeEntry const* srange = sSpellRangeStore.LookupEntry(m_spellInfo->rangeIndex);
    float max_range = GetSpellMaxRange(srange);

    WorldObject* result = nullptr;

    T u_check(m_caster, max_range);
    MaNGOS::WorldObjectSearcher<T> searcher(result, u_check);

    Cell::VisitGridObjects(m_caster, searcher, max_range);

    if (!result)
        Cell::VisitWorldObjects(m_caster, searcher, max_range);

    return result;
}

void Spell::FillTargetMap()
{
    // TODO: ADD the correct target FILLS!!!!!!

    UnitList tmpUnitLists[MAX_EFFECT_INDEX];                // Stores the temporary Target Lists for each effect
    uint8 effToIndex[MAX_EFFECT_INDEX] = {0, 1, 2};         // Helper array, to link to another tmpUnitList, if the targets for both effects match
    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        // not call for empty effect.
        // Also some spells use not used effect targets for store targets for dummy effect in triggered spells
        if(m_spellInfo->Effect[i] == SPELL_EFFECT_NONE)
            continue;

        // targets for TARGET_LOCATION_SCRIPT_NEAR_CASTER (A) and TARGET_UNIT_SCRIPT_NEAR_CASTER
        // for TARGET_GAMEOBJECT_SCRIPT_NEAR_CASTER  (A) all is checked in Spell::CheckCast and in Spell::CheckItem
        // filled in Spell::CheckCast call
        if (m_spellInfo->EffectImplicitTargetA[i] == TARGET_LOCATION_SCRIPT_NEAR_CASTER ||
            m_spellInfo->EffectImplicitTargetA[i] == TARGET_GAMEOBJECT_SCRIPT_NEAR_CASTER ||
            (m_spellInfo->EffectImplicitTargetA[i] == TARGET_UNIT_SCRIPT_NEAR_CASTER && m_spellInfo->EffectImplicitTargetB[i] != TARGET_UNIT_CASTER) ||
            (m_spellInfo->EffectImplicitTargetB[i] == TARGET_UNIT_SCRIPT_NEAR_CASTER && m_spellInfo->EffectImplicitTargetA[i] != TARGET_UNIT_CASTER))
            continue;

        // TODO: find a way so this is not needed?
        // for area auras always add caster as target (needed for totems for example)
        if (IsAreaAuraEffect(m_spellInfo->Effect[i]))
            AddUnitTarget(m_caster, SpellEffectIndex(i));

        // no double fill for same targets
        for (int j = 0; j < i; ++j)
        {
            // Check if same target, but handle i.e. AreaAuras different
            if (m_spellInfo->EffectImplicitTargetA[i] == m_spellInfo->EffectImplicitTargetA[j] && m_spellInfo->EffectImplicitTargetB[i] == m_spellInfo->EffectImplicitTargetB[j]
                && m_spellInfo->Effect[j] != SPELL_EFFECT_NONE
                && !IsAreaAuraEffect(m_spellInfo->Effect[i]) && !IsAreaAuraEffect(m_spellInfo->Effect[j]))
                // Add further conditions here if required
            {
                effToIndex[i] = j;                          // effect i has same targeting list as effect j
                break;
            }
        }

        if (effToIndex[i] == i)                             // New target combination
        {
            // TargetA/TargetB dependent from each other, we not switch to full support this dependences
            // but need it support in some know cases
            switch(m_spellInfo->EffectImplicitTargetA[i])
            {
                case TARGET_NONE:
                    switch(m_spellInfo->EffectImplicitTargetB[i])
                    {
                        case TARGET_NONE:
                            if (m_caster->GetObjectGuid().IsPet())
                                SetTargetMap(SpellEffectIndex(i), TARGET_UNIT_CASTER, tmpUnitLists[i /*==effToIndex[i]*/]);
                            else
                                SetTargetMap(SpellEffectIndex(i), TARGET_LOCATION_CASTER_DEST, tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                        default:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                    }
                    break;
                case TARGET_UNIT_CASTER:
                    switch(m_spellInfo->EffectImplicitTargetB[i])
                    {
                        case TARGET_NONE:                   // Fill Target based on A only
                        case TARGET_LOCATION_CASTER_DEST:
                        case TARGET_UNIT_SCRIPT_NEAR_CASTER:                 // B-target only used with CheckCast here
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                        case TARGET_ENUM_UNITS_SCRIPT_AOE_AT_SRC_LOC:         // use B case that not dependent from from A in fact
                            if((m_targets.m_targetMask & TARGET_FLAG_DEST_LOCATION) == 0)
                                m_targets.setDestination(m_caster->GetPositionX(), m_caster->GetPositionY(), m_caster->GetPositionZ());
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                        case TARGET_LOCATION_UNIT_BACK:              // use B case that not dependent from from A in fact
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                        default:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                    }
                    break;
                case TARGET_LOCATION_CASTER_DEST:
                    switch(m_spellInfo->EffectImplicitTargetB[i])
                    {
                        case TARGET_NONE:
                        case TARGET_LOCATION_CASTER_DEST:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                        // dest point setup required
                        case TARGET_ENUM_UNITS_SCRIPT_AOE_AT_SRC_LOC:
                        case TARGET_ENUM_UNITS_SCRIPT_AOE_AT_DEST_LOC:
                        case TARGET_ENUM_UNITS_ENEMY_AOE_AT_SRC_LOC:
                        case TARGET_ENUM_UNITS_ENEMY_AOE_AT_DEST_LOC:
                        case TARGET_ENUM_UNITS_ENEMY_AOE_AT_DYNOBJ_LOC:
                        case TARGET_ENUM_UNITS_FRIEND_AOE_AT_SRC_LOC:
                        case TARGET_ENUM_GAMEOBJECTS_SCRIPT_AOE_AT_DEST_LOC:
                        case TARGET_LOCATION_RANDOM_SIDE:
                            // triggered spells get dest point from default target set, ignore it
                            if (!(m_targets.m_targetMask & TARGET_FLAG_DEST_LOCATION) || m_IsTriggeredSpell)
                                if (WorldObject* castObject = GetCastingObject())
                                    m_targets.setDestination(castObject->GetPositionX(), castObject->GetPositionY(), castObject->GetPositionZ());
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                        case TARGET_LOCATION_RANDOM_CIRCUMFERENCE:
                        // target pre-selection required
                        case TARGET_LOCATION_CASTER_HOME_BIND:
                        case TARGET_LOCATION_DATABASE:
                        case TARGET_LOCATION_CASTER_SRC:
                        case TARGET_LOCATION_SCRIPT_NEAR_CASTER:
                        case TARGET_LOCATION_CASTER_TARGET_POSITION:
                        case TARGET_LOCATION_UNIT_POSITION_63:
                        case TARGET_LOCATION_DYNOBJ_POSITION:
                        case TARGET_LOCATION_NORTH:
                        case TARGET_LOCATION_SOUTH:
                        case TARGET_LOCATION_EAST:
                        case TARGET_LOCATION_WEST:
                        case TARGET_LOCATION_NE:
                        case TARGET_LOCATION_NW:
                        case TARGET_LOCATION_SE:
                        case TARGET_LOCATION_SW:
                            // need some target for processing
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                        default:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                    }
                    break;
                case TARGET_LOCATION_CASTER_SRC:
                    switch(m_spellInfo->EffectImplicitTargetB[i])
                    {
                        case TARGET_ENUM_UNITS_ENEMY_AOE_AT_SRC_LOC:
                            // Note: this hack with search required until GO casting not implemented
                            // environment damage spells already have around enemies targeting but this not help in case nonexistent GO casting support
                            // currently each enemy selected explicitly and self cast damage
                            if (m_spellInfo->Effect[i] == SPELL_EFFECT_ENVIRONMENTAL_DAMAGE)
                            {
                                if(m_targets.getUnitTarget())
                                    tmpUnitLists[i /*==effToIndex[i]*/].push_back(m_targets.getUnitTarget());
                            }
                            else
                            {
                                SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                                SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            }
                            break;
                        case 0:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            tmpUnitLists[i /*==effToIndex[i]*/].push_back(m_caster);
                            break;
                        default:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                    }
                    break;
                case TARGET_LOCATION_DATABASE:
                    switch(m_spellInfo->EffectImplicitTargetB[i])
                    {
                        case 0:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);


                        // need some target for processing
                            SetTargetMap(SpellEffectIndex(i), TARGET_LOCATION_CASTER_DEST, tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                        case TARGET_ENUM_UNITS_SCRIPT_AOE_AT_SRC_LOC:         // All 17/7 pairs used for dest teleportation, A processed in effect code
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                        default:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                    }
                    case TARGET_LOCATION_CURRENT_REFERENCE:
                    switch(m_spellInfo->EffectImplicitTargetB[i])
                    {
                        case TARGET_NONE:
                        case TARGET_LOCATION_CASTER_DEST:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                        case TARGET_ENUM_UNITS_SCRIPT_AOE_AT_DEST_LOC:
                            // triggered spells get dest point from default target set, ignore it
                            if (!(m_targets.m_targetMask & TARGET_FLAG_DEST_LOCATION) || m_IsTriggeredSpell)
                                if (WorldObject* castObject = GetCastingObject())
                                    m_targets.setDestination(castObject->GetPositionX(), castObject->GetPositionY(), castObject->GetPositionZ());
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                        // most A/B target pairs is self->negative and not expect adding caster to target list
                        default:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                    }
                    break;
                case TARGET_LOCATION_UNIT_POSITION_63:
                    switch(m_spellInfo->EffectImplicitTargetB[i])
                    {
                        case TARGET_NONE:
                        case TARGET_LOCATION_CASTER_DEST:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            if (Unit* currentTarget = m_targets.getUnitTarget())
                                tmpUnitLists[i /*==effToIndex[i]*/].push_back(currentTarget);
                            break;
                        default:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                    }
                    break;
                case TARGET_UNIT_SCRIPT_NEAR_CASTER:
                    switch (m_spellInfo->EffectImplicitTargetB[i])
                    {
                        case TARGET_UNIT_CASTER:
                            // Fill target based on B only, A is only used with CheckCast here.
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                        default:
                            break;
                    }
                    break;
                default:
                    switch(m_spellInfo->EffectImplicitTargetB[i])
                    {
                        case TARGET_NONE:
                        case TARGET_LOCATION_CASTER_DEST:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                        case TARGET_LOCATION_SCRIPT_NEAR_CASTER:         // B case filled in CheckCast but we need fill unit list base at A case
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                           break;
                        default:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                    }
                    break;
            }
        }

        if (m_caster->GetTypeId() == TYPEID_PLAYER)
        {
            Player* me = (Player*)m_caster;
            for (UnitList::const_iterator itr = tmpUnitLists[effToIndex[i]].begin(); itr != tmpUnitLists[effToIndex[i]].end(); ++itr)
            {
                Player* targetOwner = (*itr)->GetBeneficiaryPlayer();
                if (targetOwner && targetOwner != me && targetOwner->IsPvP() && !me->IsInDuelWith(targetOwner))
                {
                    me->UpdatePvP(true);
                    me->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_ENTER_PVP_COMBAT);
                    break;
                }
            }
        }

        for (UnitList::iterator itr = tmpUnitLists[effToIndex[i]].begin(); itr != tmpUnitLists[effToIndex[i]].end();)
        {
            if (!CheckTarget(*itr, SpellEffectIndex(i)))
            {
                itr = tmpUnitLists[effToIndex[i]].erase(itr);
                continue;
            }
            else
                ++itr;
        }

        if (m_affectedTargetCount && tmpUnitLists[effToIndex[i]].size() > m_affectedTargetCount)
        {
            // remove random units from the map
            while (tmpUnitLists[effToIndex[i]].size() > m_affectedTargetCount)
            {
                uint32 poz = urand(0, tmpUnitLists[effToIndex[i]].size() - 1);
                for (UnitList::iterator itr = tmpUnitLists[effToIndex[i]].begin(); itr != tmpUnitLists[effToIndex[i]].end(); ++itr, --poz)
                {
                    if (!*itr) continue;

                    if (!poz)
                    {
                        itr = tmpUnitLists[effToIndex[i]].erase(itr);
                        break;
                    }
                }
            }
        }

        for (UnitList::const_iterator iunit = tmpUnitLists[effToIndex[i]].begin(); iunit != tmpUnitLists[effToIndex[i]].end(); ++iunit)
            AddUnitTarget((*iunit), SpellEffectIndex(i));
    }
}

void Spell::prepareDataForTriggerSystem()
{
    //==========================================================================================
    // Now fill data for trigger system, need know:
    // an spell trigger another or not ( m_canTrigger )
    // Create base triggers flags for Attacker and Victim ( m_procAttacker and  m_procVictim)
    //==========================================================================================
    // Fill flag can spell trigger or not
    // TODO: possible exist spell attribute for this
    m_canTrigger = false;

    if (m_CastItem || m_spellInfo->HasAttribute(SPELL_ATTR_EX3_CANT_TRIGGER_PROC))
        m_canTrigger = false;                               // Do not trigger from item cast spell
    else if (!m_IsTriggeredSpell)
        m_canTrigger = true;                                // Normal cast - can trigger
    else if (!m_triggeredByAuraSpell)
        m_canTrigger = true;                                // Triggered from SPELL_EFFECT_TRIGGER_SPELL - can trigger
    else if (m_spellInfo->HasAttribute(SPELL_ATTR_EX2_TRIGGERED_CAN_TRIGGER_PROC) || m_spellInfo->HasAttribute(SPELL_ATTR_EX3_TRIGGERED_CAN_TRIGGER_SPECIAL))
        m_canTrigger = true;                                // Spells with these special attributes can trigger even if triggeredByAuraSpell

    if (!m_canTrigger)                                      // Exceptions (some periodic triggers)
    {
        switch (m_spellInfo->SpellFamilyName)
        {
            case SPELLFAMILY_MAGE:
                // Arcane Missles / Blizzard triggers need do it
                if (m_spellInfo->IsFitToFamilyMask(uint64(0x0000000000200080)))
                    m_canTrigger = true;
                // Clearcasting trigger need do it
                else if (m_spellInfo->IsFitToFamilyMask(uint64(0x0000000200000000), 0x00000008))
                    m_canTrigger = true;
                // Replenish Mana, item spell with triggered cases (Mana Agate, etc mana gems)
                else if (m_spellInfo->IsFitToFamilyMask(uint64(0x0000010000000000)))
                    m_canTrigger = true;
                break;
            case SPELLFAMILY_WARLOCK:
                // For Hellfire Effect / Rain of Fire / Seed of Corruption triggers need do it
                if (m_spellInfo->IsFitToFamilyMask(uint64(0x0000800000000060)))
                    m_canTrigger = true;
                break;
            case SPELLFAMILY_PRIEST:
                // For Penance,Mind Sear,Mind Flay heal/damage triggers need do it
                if (m_spellInfo->IsFitToFamilyMask(uint64(0x0001800000800000), 0x00000040))
                    m_canTrigger = true;
                break;
            case SPELLFAMILY_ROGUE:
                // For poisons need do it
                if (m_spellInfo->IsFitToFamilyMask(uint64(0x000000101001E000)))
                    m_canTrigger = true;
                break;
            case SPELLFAMILY_HUNTER:
                // Hunter Rapid Killing/Explosive Trap Effect/Immolation Trap Effect/Frost Trap Aura/Snake Trap Effect/Explosive Shot
                if (m_spellInfo->IsFitToFamilyMask(uint64(0x0100200000000214), 0x200))
                    m_canTrigger = true;
                break;
            case SPELLFAMILY_PALADIN:
                // For Judgements (all) / Holy Shock triggers need do it
                if (m_spellInfo->IsFitToFamilyMask(uint64(0x0001000900B80400)))
                    m_canTrigger = true;
                break;
            default:
                break;
        }
    }

    // Get data for type of attack and fill base info for trigger
    switch (m_spellInfo->DmgClass)
    {
        case SPELL_DAMAGE_CLASS_MELEE:
            m_procAttacker = PROC_FLAG_SUCCESSFUL_MELEE_SPELL_HIT;
            if (m_attackType == OFF_ATTACK)
                m_procAttacker |= PROC_FLAG_SUCCESSFUL_OFFHAND_HIT;
            m_procVictim   = PROC_FLAG_TAKEN_MELEE_SPELL_HIT;
            break;
        case SPELL_DAMAGE_CLASS_RANGED:
            // Auto attack
            if (m_spellInfo->HasAttribute(SPELL_ATTR_EX2_AUTOREPEAT_FLAG))
            {
                m_procAttacker = PROC_FLAG_SUCCESSFUL_RANGED_HIT;
                m_procVictim   = PROC_FLAG_TAKEN_RANGED_HIT;
            }
            else // Ranged spell attack
            {
                m_procAttacker = PROC_FLAG_SUCCESSFUL_RANGED_SPELL_HIT;
                m_procVictim   = PROC_FLAG_TAKEN_RANGED_SPELL_HIT;
            }
            break;
        default:
            if (IsPositiveSpell(m_spellInfo->Id))           // Check for positive spell
            {
                if (m_spellInfo->DmgClass & SPELL_DAMAGE_CLASS_NONE) // if dmg class none
                {
                    m_procAttacker = PROC_FLAG_DONE_SPELL_NONE_DMG_CLASS_POS;
                    m_procVictim = PROC_FLAG_TAKEN_SPELL_NONE_DMG_CLASS_POS;
                }
                else
                {
                    m_procAttacker = PROC_FLAG_DONE_SPELL_MAGIC_DMG_CLASS_POS;
                    m_procVictim = PROC_FLAG_TAKEN_SPELL_MAGIC_DMG_CLASS_POS;
                }
            }
            else if (m_spellInfo->HasAttribute(SPELL_ATTR_EX2_AUTOREPEAT_FLAG))   // Wands auto attack
            {
                m_procAttacker = PROC_FLAG_SUCCESSFUL_RANGED_HIT;
                m_procVictim   = PROC_FLAG_TAKEN_RANGED_HIT;
            }
            else                                           // Negative spell
            {
                if (m_spellInfo->DmgClass & SPELL_DAMAGE_CLASS_NONE) // if dmg class none
                {
                    m_procAttacker = PROC_FLAG_DONE_SPELL_NONE_DMG_CLASS_NEG;
                    m_procVictim = PROC_FLAG_TAKEN_SPELL_NONE_DMG_CLASS_NEG;
                }
                else
                {
                    m_procAttacker = PROC_FLAG_DONE_SPELL_MAGIC_DMG_CLASS_NEG;
                    m_procVictim = PROC_FLAG_TAKEN_SPELL_MAGIC_DMG_CLASS_NEG;
                }
            }
            break;
    }

    // some negative spells have positive effects to another or same targets
    // avoid triggering negative hit for only positive targets
    m_negativeEffectMask = 0x0;
    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
        if (!IsPositiveEffect(m_spellInfo, SpellEffectIndex(i)))
            m_negativeEffectMask |= (1 << i);

    // Hunter traps spells (for Entrapment trigger)
    // Gives your Immolation Trap, Frost Trap, Explosive Trap, and Snake Trap ....
    if (m_spellInfo->IsFitToFamily(SPELLFAMILY_HUNTER, uint64(0x000020000000001C)))
        m_procAttacker |= PROC_FLAG_ON_TRAP_ACTIVATION;

    if (IsNextMeleeSwingSpell())
    {
        m_procAttacker |= PROC_FLAG_SUCCESSFUL_MELEE_HIT;
        m_procVictim |= PROC_FLAG_TAKEN_MELEE_HIT;
    }
}

void Spell::CleanupTargetList()
{
    m_UniqueTargetInfo.clear();
    m_UniqueGOTargetInfo.clear();
    m_UniqueItemInfo.clear();
    m_delayMoment = 0;
}

void Spell::AddUnitTarget(Unit* pVictim, SpellEffectIndex effIndex)
{
    // Check for effect immune skip if immuned
    bool immuned = pVictim->IsImmuneToSpellEffect(m_spellInfo, effIndex, pVictim == m_caster);

    if (pVictim->GetTypeId() == TYPEID_UNIT && ((Creature*)pVictim)->IsTotem() && (m_spellFlags & SPELL_FLAG_REDIRECTED))
        immuned = false;

    ObjectGuid targetGUID = pVictim->GetObjectGuid();

    // Lookup target in already in list
    for (TargetList::iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
    {
        if (targetGUID == ihit->targetGUID)                 // Found in list
        {
            if (!immuned)
                ihit->effectMask |= 1 << effIndex;          // Add only effect mask if not immuned
            return;
        }
    }

    // This is new target calculate data for him

    // Get spell hit result on target
    TargetInfo target;
    target.targetGUID = targetGUID;                         // Store target GUID
    target.effectMask = immuned ? 0 : (1 << effIndex);      // Store index of effect if not immuned
    target.processed  = false;                              // Effects not applied on target

    // Calculate hit result
    target.missCondition = m_ignoreHitResult ? SPELL_MISS_NONE : m_caster->SpellHitResult(pVictim, m_spellInfo, m_reflectable);

    // spell fly from visual cast object
    WorldObject* affectiveObject = GetAffectiveCasterObject();

    // Spell have speed (possible inherited from triggering spell) - need calculate incoming time
    float speed = m_spellInfo->speed == 0.0f && m_triggeredBySpellInfo ? m_triggeredBySpellInfo->speed : m_spellInfo->speed;
    if (speed > 0.0f && affectiveObject && (pVictim != affectiveObject || (m_targets.m_targetMask & (TARGET_FLAG_SOURCE_LOCATION | TARGET_FLAG_DEST_LOCATION))))
    {
        // calculate spell incoming interval
        float dist;                                         // distance to impact
        if (pVictim == affectiveObject)                     // Calculate dist to destination target also for self-cast spells
        {
            if (m_targets.m_targetMask & TARGET_FLAG_DEST_LOCATION)
                dist = affectiveObject->GetDistance(m_targets.m_destX, m_targets.m_destY, m_targets.m_destZ);
            else                                            // Must have Source Target
                dist = affectiveObject->GetDistance(m_targets.m_srcX, m_targets.m_srcY, m_targets.m_srcZ);
        }
        else                                                // normal unit target, take distance
            dist = affectiveObject->GetDistance(pVictim->GetPositionX(), pVictim->GetPositionY(), pVictim->GetPositionZ());

        if (dist < 5.0f)
            dist = 5.0f;
        target.timeDelay = (uint64) floor(dist / speed * 1000.0f);

        // Calculate minimum incoming time
        if (m_delayMoment == 0 || m_delayMoment > target.timeDelay)
            m_delayMoment = target.timeDelay;
    }
    // Spell casted on self - mostly TRIGGER_MISSILE code
    else if (m_spellInfo->speed > 0.0f && affectiveObject && pVictim == affectiveObject)
    {
        float dist = 0.0f;
        if (m_targets.m_targetMask & TARGET_FLAG_DEST_LOCATION)
            dist = affectiveObject->GetDistance(m_targets.m_destX, m_targets.m_destY, m_targets.m_destZ);

        target.timeDelay = (uint64) floor(dist / m_spellInfo->speed * 1000.0f);
    }
    else
        target.timeDelay = uint64(0);

    // If target reflect spell back to caster
    if (target.missCondition == SPELL_MISS_REFLECT)
    {
        // Victim reflects, apply reflect procs
        m_caster->ProcDamageAndSpell(pVictim, PROC_FLAG_NONE, PROC_FLAG_TAKEN_SPELL_MAGIC_DMG_CLASS_NEG, PROC_EX_REFLECT, 1, BASE_ATTACK, m_spellInfo);
        // Calculate reflected spell result on caster
        target.reflectResult =  m_caster->SpellHitResult(m_caster, m_spellInfo, m_reflectable);
        // Caster reflects back spell which was already reflected by victim
        if (target.reflectResult == SPELL_MISS_REFLECT)
        {
            // Apply reflect procs on self
            m_caster->ProcDamageAndSpell(m_caster, PROC_FLAG_NONE, PROC_FLAG_TAKEN_SPELL_MAGIC_DMG_CLASS_NEG, PROC_EX_REFLECT, 1, BASE_ATTACK, m_spellInfo);
            // Full circle: it's impossible to reflect further, "Immune" shows up
            target.reflectResult = SPELL_MISS_IMMUNE;
        }
        // Increase time interval for reflected spells by 1.5
        target.timeDelay += target.timeDelay >> 1;

        m_spellFlags |= SPELL_FLAG_REFLECTED;
    }
    else
        target.reflectResult = SPELL_MISS_NONE;

    // Add target to list
    m_UniqueTargetInfo.push_back(target);
}

void Spell::AddUnitTarget(ObjectGuid unitGuid, SpellEffectIndex effIndex)
{
    if (Unit* unit = m_caster->GetObjectGuid() == unitGuid ? m_caster : ObjectAccessor::GetUnit(*m_caster, unitGuid))
        AddUnitTarget(unit, effIndex);
}

void Spell::AddGOTarget(GameObject* pVictim, SpellEffectIndex effIndex)
{
    ObjectGuid targetGUID = pVictim->GetObjectGuid();

    // Lookup target in already in list
    for (auto& ihit : m_UniqueGOTargetInfo)
    {
        if (targetGUID == ihit.targetGUID)                 // Found in list
        {
            ihit.effectMask |= (1 << effIndex);            // Add only effect mask
            return;
        }
    }

    // This is new target calculate data for him

    GOTargetInfo target;
    target.targetGUID = targetGUID;
    target.effectMask = (1 << effIndex);
    target.processed  = false;                              // Effects not apply on target

    // spell fly from visual cast object
    WorldObject* affectiveObject = GetAffectiveCasterObject();

    // Spell can have speed - need calculate incoming time
    float speed = m_spellInfo->speed == 0.0f && m_triggeredBySpellInfo ? m_triggeredBySpellInfo->speed : m_spellInfo->speed;
    if (speed > 0.0f && affectiveObject && pVictim != affectiveObject)
    {
        // calculate spell incoming interval
        float dist = affectiveObject->GetDistance(pVictim->GetPositionX(), pVictim->GetPositionY(), pVictim->GetPositionZ());
        if (dist < 5.0f)
            dist = 5.0f;
        target.timeDelay = (uint64) floor(dist / speed * 1000.0f);
        if (m_delayMoment == 0 || m_delayMoment > target.timeDelay)
            m_delayMoment = target.timeDelay;
    }
    else
        target.timeDelay = uint64(0);

    // Add target to list
    m_UniqueGOTargetInfo.push_back(target);
}

void Spell::AddGOTarget(ObjectGuid goGuid, SpellEffectIndex effIndex)
{
    if (GameObject* go = m_caster->GetMap()->GetGameObject(goGuid))
        AddGOTarget(go, effIndex);
}

void Spell::AddItemTarget(Item* pitem, SpellEffectIndex effIndex)
{
    // Lookup target in already in list
    for (auto& ihit : m_UniqueItemInfo)
    {
        if (pitem == ihit.item)                            // Found in list
        {
            ihit.effectMask |= (1 << effIndex);            // Add only effect mask
            return;
        }
    }

    // This is new target add data

    ItemTargetInfo target;
    target.item       = pitem;
    target.effectMask = (1 << effIndex);
    m_UniqueItemInfo.push_back(target);
}

void Spell::DoAllEffectOnTarget(TargetInfo* target)
{
    if (target->processed)                                  // Check target
        return;
    target->processed = true;                               // Target checked in apply effects procedure

    // Get mask of effects for target
    uint32 mask = target->effectMask;

    Unit* unit = m_caster->GetObjectGuid() == target->targetGUID ? m_caster : ObjectAccessor::GetUnit(*m_caster, target->targetGUID);
    if (!unit)
        return;

    // Get original caster (if exist) and calculate damage/healing from him data
    Unit* real_caster = GetAffectiveCaster();
    // FIXME: in case wild GO heal/damage spells will be used target bonuses
    Unit* caster = real_caster ? real_caster : m_caster;

    SpellMissInfo missInfo = target->missCondition;
    // Need init unitTarget by default unit (can changed in code on reflect)
    // Or on missInfo!=SPELL_MISS_NONE unitTarget undefined (but need in trigger subsystem)
    unitTarget = unit;

    // Reset damage/healing counter
    ResetEffectDamageAndHeal();

    // Fill base trigger info
    uint32 procAttacker = m_procAttacker;
    uint32 procVictim   = m_procVictim;
    uint32 procEx       = PROC_EX_NONE;

    // drop proc flags in case target not affected negative effects in negative spell
    // for example caster bonus or animation,
    // except miss case where will assigned PROC_EX_* flags later
    if (((procAttacker | procVictim) & NEGATIVE_TRIGGER_MASK) &&
            !(target->effectMask & m_negativeEffectMask) && missInfo == SPELL_MISS_NONE)
    {
        procAttacker = PROC_FLAG_NONE;
        procVictim   = PROC_FLAG_NONE;
    }

    float speed = m_spellInfo->speed == 0.0f && m_triggeredBySpellInfo ? m_triggeredBySpellInfo->speed : m_spellInfo->speed;
    if (speed > 0.0f)
    {
        // mark effects that were already handled in Spell::HandleDelayedSpellLaunch on spell launch as processed
        for (int32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            if (IsEffectHandledOnDelayedSpellLaunch(m_spellInfo, SpellEffectIndex(i)))
                mask &= ~(1 << i);

        // maybe used in effects that are handled on hit
        m_damage += target->damage;
    }

    if (missInfo == SPELL_MISS_NONE)                        // In case spell hit target, do all effect on that target
        DoSpellHitOnUnit(unit, mask);
    else if (missInfo != SPELL_MISS_EVADE)
    {
        if (missInfo == SPELL_MISS_REFLECT)                // In case spell reflect from target, do all effect on caster (if hit)
        {
            if (target->reflectResult == SPELL_MISS_NONE)       // If reflected spell hit caster -> do all effect on him
            {
                DoSpellHitOnUnit(m_caster, mask);
                unitTarget = m_caster;
            }
        }

        // Failed hostile spell hits count as attack made against target (if detected)
        if (real_caster && real_caster != unit)
        {
            if (!m_spellInfo->HasAttribute(SPELL_ATTR_EX3_NO_INITIAL_AGGRO) &&
                !m_spellInfo->HasAttribute(SPELL_ATTR_EX_NO_THREAT) &&
                !IsPositiveSpell(m_spellInfo->Id, real_caster, unit) &&
                m_caster->isVisibleForOrDetect(unit, unit, false))
            {
                if (!unit->IsInCombat() && unit->GetTypeId() != TYPEID_PLAYER && ((Creature*)unit)->AI())
                    ((Creature*)unit)->AI()->AttackedBy(real_caster);

                unit->AddThreat(real_caster);
                unit->SetInCombatWithAggressor(real_caster);
                real_caster->SetInCombatWithVictim(unit);
            }
        }
    }

    // All calculated do it!
    // Do healing and triggers
    if (m_healing)
    {
        bool crit = real_caster && real_caster->RollSpellCritOutcome(unitTarget, m_spellSchoolMask, m_spellInfo);
        uint32 addhealth = m_healing;
        if (crit)
        {
            procEx |= PROC_EX_CRITICAL_HIT;
            addhealth = caster->CalculateCritAmount(nullptr, addhealth, m_spellInfo, true);
        }
        else
            procEx |= PROC_EX_NORMAL_HIT;

        uint32 absorb = 0;
        unitTarget->CalculateHealAbsorb(addhealth, &absorb);
        addhealth -= absorb;

        // Do triggers for unit (reflect triggers passed on hit phase for correct drop charge)
        if (m_canTrigger && missInfo != SPELL_MISS_REFLECT)
        {
            caster->ProcDamageAndSpell(unitTarget, real_caster ? procAttacker : uint32(PROC_FLAG_NONE), procVictim, procEx, addhealth, m_attackType, m_spellInfo, !!m_triggeredByAuraSpell);
        }

        int32 gain = caster->DealHeal(unitTarget, addhealth, m_spellInfo, crit, absorb);

        if (real_caster)
            unitTarget->getHostileRefManager().threatAssist(real_caster, float(gain) * 0.5f * sSpellMgr.GetSpellThreatMultiplier(m_spellInfo), m_spellInfo);
    }
    // Do damage and triggers
    else if (m_damage)
    {
        // Fill base damage struct (unitTarget - is real spell target)
        SpellNonMeleeDamage damageInfo(caster, unitTarget, m_spellInfo->Id, m_spellSchoolMask);

        if (speed > 0.0f)
        {
            damageInfo.damage = m_damage;
            damageInfo.HitInfo = target->HitInfo;
        }
        // Add bonuses and fill damageInfo struct
        else
            caster->CalculateSpellDamage(&damageInfo, m_damage, m_spellInfo, m_attackType);

        unitTarget->CalculateAbsorbResistBlock(caster, &damageInfo, m_spellInfo);

        caster->DealDamageMods(damageInfo.target, damageInfo.damage, &damageInfo.absorb, SPELL_DIRECT_DAMAGE, m_spellInfo);

        // Send log damage message to client
        caster->SendSpellNonMeleeDamageLog(&damageInfo);

        procEx = createProcExtendMask(&damageInfo, missInfo);
        procVictim |= PROC_FLAG_TAKEN_ANY_DAMAGE;

        // Do triggers for unit (reflect triggers passed on hit phase for correct drop charge)
        if (m_canTrigger && missInfo != SPELL_MISS_REFLECT)
            caster->ProcDamageAndSpell(unitTarget, real_caster ? procAttacker : uint32(PROC_FLAG_NONE), procVictim, procEx, damageInfo.damage, m_attackType, m_spellInfo, !!m_triggeredByAuraSpell);

        // trigger weapon enchants for weapon based spells; exclude spells that stop attack, because may break CC
        if (m_caster->GetTypeId() == TYPEID_PLAYER && m_spellInfo->EquippedItemClass == ITEM_CLASS_WEAPON &&
                !m_spellInfo->HasAttribute(SPELL_ATTR_STOP_ATTACK_TARGET))
            ((Player*)m_caster)->CastItemCombatSpell(unitTarget, m_attackType);

        // Haunt (NOTE: for avoid use additional field damage stored in dummy value (replace unused 100%)
        // apply before deal damage because aura can be removed at target kill
        if (m_spellInfo->SpellFamilyName == SPELLFAMILY_WARLOCK && m_spellInfo->SpellIconID == 3172 &&
            (m_spellInfo->SpellFamilyFlags & uint64(0x0004000000000000)))
            if(Aura* dummy = unitTarget->GetDummyAura(m_spellInfo->Id))
                dummy->GetModifier()->m_amount = damageInfo.damage;

        caster->DealSpellDamage(&damageInfo, true);

        // Scourge Strike, here because needs to use final damage in second part of the spell
        if (m_spellInfo->SpellFamilyName == SPELLFAMILY_DEATHKNIGHT && m_spellInfo->SpellFamilyFlags & uint64(0x0800000000000000))
        {
            uint32 count = 0;
            Unit::SpellAuraHolderMap const& auras = unitTarget->GetSpellAuraHolderMap();
            for (Unit::SpellAuraHolderMap::const_iterator itr = auras.begin(); itr != auras.end(); ++itr)
            {
                if(itr->second->GetSpellProto()->Dispel == DISPEL_DISEASE &&
                    itr->second->GetCasterGuid() == caster->GetObjectGuid())
                    ++count;
            }

            if (count)
            {
                int32 bp = count * CalculateDamage(EFFECT_INDEX_2, unitTarget) * damageInfo.damage / 100;
                if (bp)
                    caster->CastCustomSpell(unitTarget, 70890, &bp, nullptr, nullptr, TRIGGERED_OLD_TRIGGERED);
            }
        }
    }
    // Passive spell hits/misses or active spells only misses (only triggers if proc flags set)
    else if (procAttacker || procVictim)
    {
        // Fill base damage struct (unitTarget - is real spell target)
        SpellNonMeleeDamage damageInfo(caster, unitTarget, m_spellInfo->Id, m_spellSchoolMask);
        procEx = createProcExtendMask(&damageInfo, missInfo);
        // Do triggers for unit (reflect triggers passed on hit phase for correct drop charge)
        if (m_canTrigger && missInfo != SPELL_MISS_REFLECT)
            // traps need to be procced at trap triggerer
            caster->ProcDamageAndSpell(procAttacker & PROC_FLAG_ON_TRAP_ACTIVATION ? m_targets.getUnitTarget() : unit, real_caster ? procAttacker : uint32(PROC_FLAG_NONE), procVictim, procEx, 0, m_attackType, m_spellInfo, !!m_triggeredByAuraSpell);
    }

    // Call scripted function for AI if this spell is casted upon a creature
    if (unit->GetTypeId() == TYPEID_UNIT)
    {
        // cast at creature (or GO) quest objectives update at successful cast finished (+channel finished)
        // ignore pets or autorepeat/melee casts for speed (not exist quest for spells (hm... )
        if (real_caster && !((Creature*)unit)->IsPet() && !IsAutoRepeat() && !IsNextMeleeSwingSpell() && !IsChannelActive())
            if (Player* p = real_caster->GetBeneficiaryPlayer())
                p->RewardPlayerAndGroupAtCast(unit, m_spellInfo->Id);

        if (((Creature*)unit)->AI())
            ((Creature*)unit)->AI()->SpellHit(m_caster, m_spellInfo);
    }

    // Call scripted function for AI if this spell is casted by a creature
    if (m_caster->GetTypeId() == TYPEID_UNIT && ((Creature*)m_caster)->AI())
        ((Creature*)m_caster)->AI()->SpellHitTarget(unit, m_spellInfo);
    if (real_caster && real_caster != m_caster && real_caster->GetTypeId() == TYPEID_UNIT && ((Creature*)real_caster)->AI())
        ((Creature*)real_caster)->AI()->SpellHitTarget(unit, m_spellInfo);

    if (m_spellAuraHolder)
        m_spellAuraHolder->SetState(SPELLAURAHOLDER_STATE_READY);
}

void Spell::DoSpellHitOnUnit(Unit* unit, uint32 effectMask)
{
    if (!unit)
        return;

    Unit* realCaster = GetAffectiveCaster();

    // Recheck immune (only for delayed spells)
    const float speed = m_spellInfo->speed == 0.0f && m_triggeredBySpellInfo ? m_triggeredBySpellInfo->speed : m_spellInfo->speed;
    const bool traveling = (speed > 0.0f);
    if (traveling && (
                unit->IsImmuneToDamage(GetSpellSchoolMask(m_spellInfo)) ||
                unit->IsImmuneToSpell(m_spellInfo, unit == realCaster)))
    {
        if (realCaster)
            realCaster->SendSpellMiss(unit, m_spellInfo->Id, SPELL_MISS_IMMUNE);

        ResetEffectDamageAndHeal();
        return;
    }

    if (unit->GetTypeId() == TYPEID_PLAYER)
    {
        ((Player*)unit)->GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_BE_SPELL_TARGET, m_spellInfo->Id);
        ((Player*)unit)->GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_BE_SPELL_TARGET2, m_spellInfo->Id);
    }

    if (realCaster && realCaster->GetTypeId() == TYPEID_PLAYER)
        ((Player*)realCaster)->GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_CAST_SPELL2, m_spellInfo->Id, 0, unit);

    if (realCaster && realCaster != unit)
    {
        // Recheck  UNIT_FLAG_NON_ATTACKABLE for delayed spells
        if (traveling &&
                unit->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE) &&
                unit->GetMasterGuid() != m_caster->GetObjectGuid())
        {
            ResetEffectDamageAndHeal();
            return;
        }

        if (!realCaster->IsFriendlyTo(unit))
        {
            // for delayed spells ignore not visible explicit target
            if (traveling && unit == m_targets.getUnitTarget() &&
                    !unit->isVisibleForOrDetect(m_caster, m_caster, false))
            {
                ResetEffectDamageAndHeal();
                return;
            }
        }
        else
        {
            // for delayed spells ignore negative spells (after duel end) for friendly targets
            if (traveling && !IsPositiveSpell(m_spellInfo->Id, realCaster, unit))
            {
                ResetEffectDamageAndHeal();
                return;
            }
        }
    }

    // Get Data Needed for Diminishing Returns, some effects may have multiple auras, so this must be done on spell hit, not aura add
    m_diminishGroup = GetDiminishingReturnsGroupForSpell(m_spellInfo, !!m_triggeredByAuraSpell);
    m_diminishLevel = unit->GetDiminishing(m_diminishGroup);
    // Increase Diminishing on unit, current informations for actually casts will use values above
    if ((GetDiminishingReturnsGroupType(m_diminishGroup) == DRTYPE_PLAYER && unit->GetTypeId() == TYPEID_PLAYER) ||
            GetDiminishingReturnsGroupType(m_diminishGroup) == DRTYPE_ALL)
        unit->IncrDiminishing(m_diminishGroup);

    // Apply additional spell effects to target
    CastPreCastSpells(unit);

    if (IsSpellAppliesAura(m_spellInfo, effectMask))
    {
        m_spellAuraHolder = CreateSpellAuraHolder(m_spellInfo, unit, realCaster, m_CastItem, m_triggeredBySpellInfo);
        m_spellAuraHolder->setDiminishGroup(m_diminishGroup);
    }
    else
        m_spellAuraHolder = nullptr;

    for (int effectNumber = 0; effectNumber < MAX_EFFECT_INDEX; ++effectNumber)
    {
        if (effectMask & (1 << effectNumber))
        {
            HandleEffects(unit, nullptr, nullptr, SpellEffectIndex(effectNumber), m_damageMultipliers[effectNumber]);
            if (m_applyMultiplierMask & (1 << effectNumber))
            {
                // Get multiplier
                float multiplier = m_spellInfo->EffectDamageMultiplier[effectNumber];
                // Apply multiplier mods
                if (realCaster)
                    if (Player* modOwner = realCaster->GetSpellModOwner())
                        modOwner->ApplySpellMod(m_spellInfo->Id, SPELLMOD_EFFECT_PAST_FIRST, multiplier);
                m_damageMultipliers[effectNumber] *= multiplier;
            }
        }
    }

    if (realCaster && realCaster != unit)
    {
        if (!realCaster->IsFriendlyTo(unit))
        {
            // not break stealth by cast targeting
            if (!m_spellInfo->HasAttribute(SPELL_ATTR_EX_NOT_BREAK_STEALTH))
                unit->RemoveSpellsCausingAura(SPELL_AURA_MOD_STEALTH);

            // Hostile spell hits count as attack made against target (if detected), stealth removed at Spell::cast if spell break it
            if (!m_spellInfo->HasAttribute(SPELL_ATTR_EX3_NO_INITIAL_AGGRO) &&
                !m_spellInfo->HasAttribute(SPELL_ATTR_EX_NO_THREAT) &&
                !IsPositiveSpell(m_spellInfo->Id, realCaster, unit) &&
                m_caster->isVisibleForOrDetect(unit, unit, false))
            {
                // use speedup check to avoid re-remove after above lines
                if (m_spellInfo->HasAttribute(SPELL_ATTR_EX_NOT_BREAK_STEALTH))
                    unit->RemoveSpellsCausingAura(SPELL_AURA_MOD_STEALTH);

                // caster can be detected but have stealth aura
                m_caster->RemoveSpellsCausingAura(SPELL_AURA_MOD_STEALTH);

                if (!unit->IsStandState() && !unit->hasUnitState(UNIT_STAT_STUNNED))
                    unit->SetStandState(UNIT_STAND_STATE_STAND);

                // TODO:: why testing AI here???
                if (!unit->IsInCombat() && unit->GetTypeId() != TYPEID_PLAYER && ((Creature*)unit)->AI())
                    unit->AttackedBy(realCaster);

                unit->AddThreat(realCaster);
                unit->SetInCombatWithAggressor(realCaster);
                realCaster->SetInCombatWithVictim(unit);
            }
        }
        else
        {
            // assisting case, healing and resurrection
            if (unit->IsInCombat() && !m_spellInfo->HasAttribute(SPELL_ATTR_EX3_NO_INITIAL_AGGRO))
            {
                realCaster->SetInCombatWithAssisted(unit);
                unit->getHostileRefManager().threatAssist(realCaster, 0.0f, m_spellInfo);
            }
        }
    }

    // now apply all created auras
    if (m_spellAuraHolder)
    {
        // normally shouldn't happen
        if (!m_spellAuraHolder->IsEmptyHolder())
        {
            int32 duration = m_spellAuraHolder->GetAuraMaxDuration();
            int32 originalDuration = duration;

            if (duration > 0)
            {
                int32 limitduration = GetDiminishingReturnsLimitDuration(m_diminishGroup, m_spellInfo);
                unit->ApplyDiminishingToDuration(m_diminishGroup, duration, m_caster, m_diminishLevel, limitduration, m_spellFlags & SPELL_FLAG_REFLECTED);

                // Fully diminished
                if (duration == 0)
                {
                    delete m_spellAuraHolder;
                    m_spellAuraHolder = nullptr;
                    return;
                }
            }

            duration = unit->CalculateAuraDuration(m_spellInfo, effectMask, duration, m_caster, this);

            if (duration != originalDuration)
            {
                m_spellAuraHolder->SetAuraMaxDuration(duration);
                m_spellAuraHolder->SetAuraDuration(duration);
            }

            if (!unit->AddSpellAuraHolder(m_spellAuraHolder))
            {
                delete m_spellAuraHolder;
                m_spellAuraHolder = nullptr;
            }
        }
        else
        {
            delete m_spellAuraHolder;
            m_spellAuraHolder = nullptr;
        }
    }
}

void Spell::DoAllEffectOnTarget(GOTargetInfo* target)
{
    if (target->processed)                                  // Check target
        return;
    target->processed = true;                               // Target checked in apply effects procedure

    uint32 effectMask = target->effectMask;
    if (!effectMask)
        return;

    GameObject* go = m_caster->GetMap()->GetGameObject(target->targetGUID);
    if (!go)
        return;

    for (int effectNumber = 0; effectNumber < MAX_EFFECT_INDEX; ++effectNumber)
        if (effectMask & (1 << effectNumber))
            HandleEffects(nullptr, nullptr, go, SpellEffectIndex(effectNumber));

    // cast at creature (or GO) quest objectives update at successful cast finished (+channel finished)
    // ignore autorepeat/melee casts for speed (not exist quest for spells (hm... )
    if (!IsAutoRepeat() && !IsNextMeleeSwingSpell() && !IsChannelActive())
    {
        if (Player* p = m_caster->GetBeneficiaryPlayer())
            p->RewardPlayerAndGroupAtCast(go, m_spellInfo->Id);
    }
}

void Spell::DoAllEffectOnTarget(ItemTargetInfo* target)
{
    uint32 effectMask = target->effectMask;
    if (!target->item || !effectMask)
        return;

    for (int effectNumber = 0; effectNumber < MAX_EFFECT_INDEX; ++effectNumber)
        if (effectMask & (1 << effectNumber))
            HandleEffects(nullptr, target->item, nullptr, SpellEffectIndex(effectNumber));
}

void Spell::HandleDelayedSpellLaunch(TargetInfo* target)
{
    // Get mask of effects for target
    uint32 mask = target->effectMask;

    Unit* unit = m_caster->GetObjectGuid() == target->targetGUID ? m_caster : ObjectAccessor::GetUnit(*m_caster, target->targetGUID);
    if (!unit)
        return;

    // Get original caster (if exist) and calculate damage/healing from him data
    Unit* real_caster = GetAffectiveCaster();
    // FIXME: in case wild GO heal/damage spells will be used target bonuses
    Unit* caster = real_caster ? real_caster : m_caster;

    SpellMissInfo missInfo = target->missCondition;
    // Need init unitTarget by default unit (can changed in code on reflect)
    // Or on missInfo!=SPELL_MISS_NONE unitTarget undefined (but need in trigger subsystem)
    unitTarget = unit;

    // Reset damage/healing counter
    m_damage = 0;
    m_healing = 0; // healing maybe not needed at this point

    // Fill base damage struct (unitTarget - is real spell target)
    SpellNonMeleeDamage damageInfo(caster, unitTarget, m_spellInfo->Id, m_spellSchoolMask);

    // keep damage amount for reflected spells
    if (missInfo == SPELL_MISS_NONE || (missInfo == SPELL_MISS_REFLECT && target->reflectResult == SPELL_MISS_NONE))
    {
        for (int32 effectNumber = 0; effectNumber < MAX_EFFECT_INDEX; ++effectNumber)
        {
            if (mask & (1 << effectNumber) && IsEffectHandledOnDelayedSpellLaunch(m_spellInfo, SpellEffectIndex(effectNumber)))
            {
                HandleEffects(unit, nullptr, nullptr, SpellEffectIndex(effectNumber), m_damageMultipliers[effectNumber]);
                if (m_applyMultiplierMask & (1 << effectNumber))
                {
                    // Get multiplier
                    float multiplier = m_spellInfo->EffectDamageMultiplier[effectNumber];
                    // Apply multiplier mods
                    if (real_caster)
                        if (Player* modOwner = real_caster->GetSpellModOwner())
                            modOwner->ApplySpellMod(m_spellInfo->Id, SPELLMOD_EFFECT_PAST_FIRST, multiplier);
                    m_damageMultipliers[effectNumber] *= multiplier;
                }
            }
        }

        if (m_damage > 0)
            caster->CalculateSpellDamage(&damageInfo, m_damage, m_spellInfo, m_attackType);
    }

    target->damage = damageInfo.damage;
    target->HitInfo = damageInfo.HitInfo;
}

void Spell::InitializeDamageMultipliers()
{
    for (int32 i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        if (m_spellInfo->Effect[i] == 0)
            continue;

        uint32 EffectChainTarget = m_spellInfo->EffectChainTarget[i];
        if (Unit* realCaster = GetAffectiveCaster())
            if (Player* modOwner = realCaster->GetSpellModOwner())
                modOwner->ApplySpellMod(m_spellInfo->Id, SPELLMOD_JUMP_TARGETS, EffectChainTarget);

        m_damageMultipliers[i] = 1.0f;
        if ((m_spellInfo->EffectImplicitTargetA[i] == TARGET_UNIT_ENEMY || m_spellInfo->EffectImplicitTargetA[i] == TARGET_UNIT_FRIEND_CHAIN_HEAL) &&
            (EffectChainTarget > 1) )
            m_applyMultiplierMask |= (1 << i);
    }
}

bool Spell::IsAliveUnitPresentInTargetList()
{
    // Not need check return true
    if (m_needAliveTargetMask == 0)
        return true;

    uint8 needAliveTargetMask = m_needAliveTargetMask;

    for (TargetList::const_iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
    {
        if (ihit->missCondition == SPELL_MISS_NONE && (needAliveTargetMask & ihit->effectMask))
        {
            Unit* unit = m_caster->GetObjectGuid() == ihit->targetGUID ? m_caster : ObjectAccessor::GetUnit(*m_caster, ihit->targetGUID);

            // either unit is alive and normal spell, or unit dead and deathonly-spell
            if (unit && (unit->IsAlive() != IsDeathOnlySpell(m_spellInfo)))
                needAliveTargetMask &= ~ihit->effectMask;   // remove from need alive mask effect that have alive target
        }
    }

    // is all effects from m_needAliveTargetMask have alive targets
    return needAliveTargetMask == 0;
}

// Helper for Chain Healing
// Spell target first
// Raidmates then descending by injury suffered (MaxHealth - Health)
// Other players/mobs then descending by injury suffered (MaxHealth - Health)
struct ChainHealingOrder
{
    const Unit* MainTarget;
    ChainHealingOrder(Unit const* Target) : MainTarget(Target) {};
    // functor for operator ">"
    bool operator()(Unit const* _Left, Unit const* _Right) const
    {
        return (ChainHealingHash(_Left) < ChainHealingHash(_Right));
    }
    int32 ChainHealingHash(Unit const* Target) const
    {
        if (Target == MainTarget)
            return 0;
        else if (Target->GetTypeId() == TYPEID_PLAYER && MainTarget->GetTypeId() == TYPEID_PLAYER &&
                 ((Player const*)Target)->IsInSameRaidWith((Player const*)MainTarget))
        {
            if (Target->GetHealth() == Target->GetMaxHealth())
                return 40000;
            else
                return 20000 - Target->GetMaxHealth() + Target->GetHealth();
        }
        else
            return 40000 - Target->GetMaxHealth() + Target->GetHealth();
    }
};

class ChainHealingFullHealth
{
    public:
        const Unit* MainTarget;
        ChainHealingFullHealth(const Unit* Target) : MainTarget(Target) {};

        bool operator()(const Unit* Target)
        {
            return (Target != MainTarget && Target->GetHealth() == Target->GetMaxHealth());
        }
};

void Spell::SetTargetMap(SpellEffectIndex effIndex, uint32 targetMode, UnitList& targetUnitMap)
{
    uint32 EffectChainTarget =  m_spellInfo->EffectChainTarget[effIndex];
    uint32 unMaxTargets = m_spellInfo->MaxAffectedTargets;

    float radius;
    GetSpellRangeAndRadius(effIndex, radius, EffectChainTarget); 

    std::list<GameObject*> tempTargetGOList;

    switch (targetMode)
    {
        case TARGET_LOCATION_CASTER_RANDOM_SIDE:
            // special case for Fatal Attraction (BT, Mother Shahraz)
            if (m_spellInfo->Id == 40869)
                radius = 30.0f;

            // Get a random point in circle. Use sqrt(rand) to correct distribution when converting polar to Cartesian coordinates.
            radius *= sqrtf(rand_norm_f());
        // no 'break' expected since we use code in case TARGET_LOCATION_CASTER_RANDOM_CIRCUMFERENCE!!!
        case TARGET_LOCATION_CASTER_RANDOM_CIRCUMFERENCE:
        {
            // Get a random point AT the circumference
            float angle = 2.0f * M_PI_F * rand_norm_f();
            float dest_x, dest_y, dest_z;
            m_caster->GetClosePoint(dest_x, dest_y, dest_z, 0.0f, radius, angle);
            m_targets.setDestination(dest_x, dest_y, dest_z);

            // This targetMode is often used as 'last' implicitTarget for positive spells, that just require coordinates
            // and no unitTarget (e.g. summon effects). As MaNGOS always needs a unitTarget we add just the caster here.
            // Logic: This is first target, and no second target => use m_caster -- This is second target: use m_caster if the spell is positive or a summon spell
            if ((m_spellInfo->EffectImplicitTargetA[effIndex] == targetMode && m_spellInfo->EffectImplicitTargetB[effIndex] == TARGET_NONE) ||
                    (m_spellInfo->EffectImplicitTargetB[effIndex] == targetMode && (IsPositiveSpell(m_spellInfo) || m_spellInfo->Effect[effIndex] == SPELL_EFFECT_SUMMON)))
                targetUnitMap.push_back(m_caster);
            break;
        }
        case TARGET_LOCATION_RANDOM_CIRCUMFERENCE:
        case TARGET_LOCATION_RANDOM_SIDE:
        {
            // Get a random point IN the CIRCEL around current M_TARGETS COORDINATES(!).
            if (radius > 0.0f)
            {
                // Use sqrt(rand) to correct distribution when converting polar to Cartesian coordinates.
                radius *= sqrtf(rand_norm_f());
                float angle = 2.0f * M_PI_F * rand_norm_f();
                float dest_x = m_targets.m_destX + cos(angle) * radius;
                float dest_y = m_targets.m_destY + sin(angle) * radius;
                float dest_z = m_caster->GetPositionZ();
                if (!MapManager::IsValidMapCoord(m_caster->GetMapId(), dest_x, dest_y, dest_z))
                {
                    sLog.outError("Spell::SetTargetMap: invalid map coordinates for spell %u eff_idx %u target mode %u: mapid %u x %f y %f z %f\n"
                        "spell radius: %f caster position: x %f y %f z %f\n"
                        "base dest position: x %f y %f z %f",
                        m_spellInfo->Id, effIndex, targetMode, m_caster->GetMapId(), dest_x, dest_y, dest_z,
                        radius, m_caster->GetPositionX(), m_caster->GetPositionY(), m_caster->GetPositionZ(),
                        m_targets.m_destX, m_targets.m_destY, m_caster->GetPositionZ());
                    m_targets.setDestination(m_caster->GetPositionX(), m_caster->GetPositionY(), m_caster->GetPositionZ());
                }
                else
                {
                    m_caster->UpdateGroundPositionZ(dest_x, dest_y, dest_z);
                    m_targets.setDestination(dest_x, dest_y, dest_z);
                }
            }

            // This targetMode is often used as 'last' implicitTarget for positive spells, that just require coordinates
            // and no unitTarget (e.g. summon effects). As MaNGOS always needs a unitTarget we add just the caster here.
            // Logic: This is first target, and no second target => use m_caster -- This is second target: use m_caster if the spell is positive or a summon spell
            if ((m_spellInfo->EffectImplicitTargetA[effIndex] == targetMode && m_spellInfo->EffectImplicitTargetB[effIndex] == TARGET_NONE) ||
                    (m_spellInfo->EffectImplicitTargetB[effIndex] == targetMode && (IsPositiveSpell(m_spellInfo) || m_spellInfo->Effect[effIndex] == SPELL_EFFECT_SUMMON)))
                targetUnitMap.push_back(m_caster);

            break;
        }
        case TARGET_LOCATION_CASTER_FRONT_RIGHT:
        case TARGET_LOCATION_CASTER_BACK_RIGHT:
        case TARGET_LOCATION_CASTER_BACK_LEFT:
        case TARGET_LOCATION_CASTER_FRONT_LEFT:
        {
            float angle = m_caster->GetOrientation();
            switch (targetMode)
            {
                case TARGET_LOCATION_CASTER_FRONT_LEFT:  angle += M_PI_F * 0.25f; break;            // front - left
                case TARGET_LOCATION_CASTER_BACK_LEFT:   angle += M_PI_F * 0.75f; break;            // back  - left
                case TARGET_LOCATION_CASTER_BACK_RIGHT: angle += M_PI_F * 1.25f; break;            // back  - right
                case TARGET_LOCATION_CASTER_FRONT_RIGHT: angle += M_PI_F * 1.75f; break;            // front - right
            }

            float x, y;
            float z = m_caster->GetPositionZ();
            // Do not search for a free spot. TODO: Should there be searched for a free spot. There was once a discussion that in case this space was impossible (LOS) m_caster's position should be used.
            // TODO Bring this back to memory and search for it!
            m_caster->GetNearPoint2D(x, y, radius, angle);
            m_caster->UpdateAllowedPositionZ(x, y, z);
            m_targets.setDestination(x, y, z);

            // Add Summoner
            targetUnitMap.push_back(m_caster);
            break;
        }
        case TARGET_UNIT_CASTER:
        case TARGET_LOCATION_CURRENT_REFERENCE:
            targetUnitMap.push_back(m_caster);
            break;
        case TARGET_UNIT_ENEMY_NEAR_CASTER:
        case TARGET_UNIT_FRIEND_NEAR_CASTER:
        case TARGET_UNIT_NEAR_CASTER:
        {
            m_targets.m_targetMask = 0;
            unMaxTargets = EffectChainTarget;
            float max_range = radius + unMaxTargets * CHAIN_SPELL_JUMP_RADIUS;

            UnitList tempTargetUnitMap;

            switch (targetMode)
            {
                case TARGET_UNIT_ENEMY_NEAR_CASTER:
                {
                    MaNGOS::AnyAoETargetUnitInObjectRangeCheck u_check(m_caster, max_range);
                    MaNGOS::UnitListSearcher<MaNGOS::AnyAoETargetUnitInObjectRangeCheck> searcher(tempTargetUnitMap, u_check);
                    Cell::VisitAllObjects(m_caster, searcher, max_range);
                    break;
                }
                case TARGET_UNIT_NEAR_CASTER: // TODO: Rename TARGET_UNIT_NEAR_CASTER to something better and find real difference with TARGET_UNIT_FRIEND_NEAR_CASTER.
                case TARGET_UNIT_FRIEND_NEAR_CASTER:
                {
                    MaNGOS::AnyFriendlyUnitInObjectRangeCheck u_check(m_caster, max_range);
                    MaNGOS::UnitListSearcher<MaNGOS::AnyFriendlyUnitInObjectRangeCheck> searcher(tempTargetUnitMap, u_check);
                    Cell::VisitAllObjects(m_caster, searcher, max_range);
                    break;
                }
            }

            if (tempTargetUnitMap.empty())
                break;

            tempTargetUnitMap.sort(TargetDistanceOrderNear(m_caster));

            // Now to get us a random target that's in the initial range of the spell
            uint32 t = 0;
            UnitList::iterator itr = tempTargetUnitMap.begin();
            while (itr != tempTargetUnitMap.end() && (*itr)->IsWithinDist(m_caster, radius))
                ++t, ++itr;

            if (!t)
                break;

            itr = tempTargetUnitMap.begin();
            std::advance(itr, urand() % t);
            Unit* pUnitTarget = *itr;
            targetUnitMap.push_back(pUnitTarget);

            tempTargetUnitMap.erase(itr);

            tempTargetUnitMap.sort(TargetDistanceOrderNear(pUnitTarget));

            t = unMaxTargets - 1;
            Unit* prev = pUnitTarget;
            UnitList::iterator next = tempTargetUnitMap.begin();

            while (t && next != tempTargetUnitMap.end())
            {
                if (!prev->IsWithinDist(*next, CHAIN_SPELL_JUMP_RADIUS))
                    break;

                if (!m_spellInfo->HasAttribute(SPELL_ATTR_EX2_IGNORE_LOS) && !prev->IsWithinLOSInMap(*next))
                {
                    ++next;
                    continue;
                }
                prev = *next;
                targetUnitMap.push_back(prev);
                tempTargetUnitMap.erase(next);
                tempTargetUnitMap.sort(TargetDistanceOrderNear(prev));
                next = tempTargetUnitMap.begin();
                --t;
            }
            break;
        }
        case TARGET_UNIT_CASTER_PET:
        {
            Pet* tmpUnit = m_caster->GetPet();
            if (!tmpUnit) break;
            targetUnitMap.push_back(tmpUnit);
            break;
        }
        case TARGET_UNIT_ENEMY:
        {
            if (EffectChainTarget <= 1)
            {
                if (Unit* pUnitTarget = m_caster->SelectMagnetTarget(m_targets.getUnitTarget(), this, effIndex))
                {
                    if (m_targets.getUnitTarget() && m_targets.getUnitTarget() != pUnitTarget)
                        m_spellFlags |= SPELL_FLAG_REDIRECTED;

                    m_targets.setUnitTarget(pUnitTarget);
                    targetUnitMap.push_back(pUnitTarget);
                }
            }
            else
            {
                Unit* pUnitTarget = m_targets.getUnitTarget();
                WorldObject* originalCaster = GetAffectiveCasterObject();
                if (!pUnitTarget || !originalCaster)
                    break;

                unMaxTargets = EffectChainTarget;

                float max_range;
                if(m_spellInfo->DmgClass == SPELL_DAMAGE_CLASS_MELEE)
                    max_range = radius;
                else
                    // FIXME: This very like horrible hack and wrong for most spells
                    max_range = radius + unMaxTargets * CHAIN_SPELL_JUMP_RADIUS;

                UnitList tempTargetUnitMap;
                {
                    MaNGOS::AnyAoEVisibleTargetUnitInObjectRangeCheck u_check(pUnitTarget, originalCaster, max_range);
                    MaNGOS::UnitListSearcher<MaNGOS::AnyAoEVisibleTargetUnitInObjectRangeCheck> searcher(tempTargetUnitMap, u_check);
                    Cell::VisitAllObjects(m_caster, searcher, max_range);
                }

                if (tempTargetUnitMap.empty())
                    break;

                tempTargetUnitMap.sort(TargetDistanceOrderNear(pUnitTarget));

                if (*tempTargetUnitMap.begin() == pUnitTarget)
                    tempTargetUnitMap.erase(tempTargetUnitMap.begin());

                targetUnitMap.push_back(pUnitTarget);
                uint32 t = unMaxTargets - 1;
                Unit* prev = pUnitTarget;
                UnitList::iterator next = tempTargetUnitMap.begin();

                while (t && next != tempTargetUnitMap.end())
                {
                    if (!prev->IsWithinDist(*next, CHAIN_SPELL_JUMP_RADIUS))
                        break;

                    if (!m_spellInfo->HasAttribute(SPELL_ATTR_EX2_IGNORE_LOS) && !prev->IsWithinLOSInMap(*next))
                    {
                        ++next;
                        continue;
                    }

                    if (!prev->IsWithinLOSInMap(*next)
                        || (m_spellInfo->HasAttribute(SPELL_ATTR_EX6_IGNORE_CC_TARGETS) && !(*next)->CanFreeMove()))
                    {
                        ++next;
                        continue;
                    }
                    prev = *next;
                    targetUnitMap.push_back(prev);
                    tempTargetUnitMap.erase(next);
                    tempTargetUnitMap.sort(TargetDistanceOrderNear(prev));
                    next = tempTargetUnitMap.begin();

                    --t;
                }
            }
            break;
        case TARGET_ENUM_UNITS_ENEMY_AOE_AT_SRC_LOC:
            FillAreaTargets(targetUnitMap, radius, PUSH_DEST_CENTER, SPELL_TARGETS_AOE_DAMAGE);

            switch (m_spellInfo->Id)
            {
                // Do not target current victim
                case 30769:                                 // Pick Red Riding Hood
                case 30843:                                 // Enfeeble
                case 31347:                                 // Doom
                case 37676:                                 // Insidious Whisper
                case 38028:                                 // Watery Grave
                case 40618:                                 // Insignificance
                case 41376:                                 // Spite
                case 62166:                                 // Stone Grip
                case 63981:                                 // Stone Grip (h)
                {
                    if (Unit* pVictim = m_caster->GetVictim())
                        targetUnitMap.remove(pVictim);
                    break;
                }
                // Other special cases
                case 42005:                                 // Bloodboil (spell hits only the 5 furthest away targets)
                {
                    if (targetUnitMap.size() > unMaxTargets)
                    {
                        targetUnitMap.sort(TargetDistanceOrderFarAway(m_caster));
                        targetUnitMap.resize(unMaxTargets);
                    }
                    break;
                }
                default:
                    break;
            }
            break;
        case TARGET_ENUM_UNITS_SCRIPT_AOE_AT_SRC_LOC:
        {
            SpellTargets targetB = SPELL_TARGETS_AOE_DAMAGE;
            switch (m_spellInfo->Effect[effIndex])
            {
                case SPELL_EFFECT_QUEST_COMPLETE:
                case SPELL_EFFECT_KILL_CREDIT_PERSONAL:
                case SPELL_EFFECT_KILL_CREDIT_GROUP:
                    targetB = SPELL_TARGETS_ALL;
                    break;
                default:
                    // Select friendly targets for positive effect
                    if (IsPositiveEffect(m_spellInfo, effIndex))
                        targetB = SPELL_TARGETS_FRIENDLY;
                    break;
            }

            UnitList tempTargetUnitMap;
            SQLMultiStorage::SQLMSIteratorBounds<SpellTargetEntry> bounds = sSpellScriptTargetStorage.getBounds<SpellTargetEntry>(m_spellInfo->Id);

            // fill real target list if no spell script target defined
            FillAreaTargets(bounds.first != bounds.second ? tempTargetUnitMap : targetUnitMap,
                            radius, PUSH_DEST_CENTER, bounds.first != bounds.second ? SPELL_TARGETS_ALL : targetB);

            if (!tempTargetUnitMap.empty())
                CheckSpellScriptTargets(bounds, tempTargetUnitMap, targetUnitMap, effIndex);
            break;
        }
        case TARGET_ENUM_UNITS_SCRIPT_AOE_AT_DEST_LOC:
        {
            if (m_spellInfo->Effect[effIndex] == SPELL_EFFECT_PERSISTENT_AREA_AURA)
                break;
            else if (m_spellInfo->Effect[effIndex] == SPELL_EFFECT_SUMMON)
            {
                targetUnitMap.push_back(m_caster);
                break;
            }

            UnitList tempTargetUnitMap;
            SQLMultiStorage::SQLMSIteratorBounds<SpellTargetEntry> bounds = sSpellScriptTargetStorage.getBounds<SpellTargetEntry>(m_spellInfo->Id);
            // fill real target list if no spell script target defined
            FillAreaTargets(bounds.first != bounds.second ? tempTargetUnitMap : targetUnitMap, radius, PUSH_DEST_CENTER, SPELL_TARGETS_ALL);

            if (!tempTargetUnitMap.empty())
                CheckSpellScriptTargets(bounds, tempTargetUnitMap, targetUnitMap, effIndex);
            break;
        }
        case TARGET_ENUM_GAMEOBJECTS_SCRIPT_AOE_AT_SRC_LOC:
        case TARGET_ENUM_GAMEOBJECTS_SCRIPT_AOE_AT_DEST_LOC:
        case TARGET_ENUM_GAMEOBJECTS_IN_CONE:
        {
            float x, y, z;

            if (targetMode == TARGET_ENUM_GAMEOBJECTS_SCRIPT_AOE_AT_SRC_LOC && (m_targets.m_targetMask & TARGET_FLAG_SOURCE_LOCATION))
                m_targets.getSource(x, y, z);
            else if (targetMode == TARGET_ENUM_GAMEOBJECTS_SCRIPT_AOE_AT_DEST_LOC)
                m_targets.getDestination(x, y, z);
            else                                            // can also happen for GO_AROUND_SOURCE without SOURCE_LOCATION
                m_caster->GetPosition(x, y, z);

            bool fixedTargetExist = false;
            SQLMultiStorage::SQLMSIteratorBounds<SpellTargetEntry> bounds = sSpellScriptTargetStorage.getBounds<SpellTargetEntry>(m_spellInfo->Id);
            for (SQLMultiStorage::SQLMultiSIterator<SpellTargetEntry> i_spellST = bounds.first; i_spellST != bounds.second; ++i_spellST)
            {
                if (i_spellST->CanNotHitWithSpellEffect(effIndex))
                    continue;

                if (i_spellST->type == SPELL_TARGET_TYPE_GAMEOBJECT)
                {
                    fixedTargetExist = true;
                    // search all GO's with entry, within range of m_destN
                    MaNGOS::GameObjectEntryInPosRangeCheck go_check(*m_caster, i_spellST->targetEntry, x, y, z, radius);
                    MaNGOS::GameObjectListSearcher<MaNGOS::GameObjectEntryInPosRangeCheck> checker(tempTargetGOList, go_check);
                    Cell::VisitGridObjects(m_caster, checker, radius + GetSpellMaxRange(sSpellRangeStore.LookupEntry(m_spellInfo->rangeIndex)));
                }
            }

            if (!fixedTargetExist)
            {
                // Generic handling for spells that require GO-type 33
                if (m_spellInfo->Effect[effIndex] == SPELL_EFFECT_WMO_DAMAGE || m_spellInfo->Effect[effIndex] == SPELL_EFFECT_WMO_REPAIR || m_spellInfo->Effect[effIndex] == SPELL_EFFECT_WMO_CHANGE)
                {
                    MaNGOS::GameObjectTypeInPosRangeCheck go_check(*m_caster, GAMEOBJECT_TYPE_DESTRUCTIBLE_BUILDING, x, y, z, radius, m_spellInfo->Effect[effIndex] == SPELL_EFFECT_WMO_DAMAGE, m_spellInfo->Effect[effIndex] == SPELL_EFFECT_WMO_REPAIR);
                    MaNGOS::GameObjectListSearcher<MaNGOS::GameObjectTypeInPosRangeCheck> checker(tempTargetGOList, go_check);
                    Cell::VisitGridObjects(m_caster, checker, radius + GetSpellMaxRange(sSpellRangeStore.LookupEntry(m_spellInfo->rangeIndex)));
                }
            }

            // Filter some targets for special target-type
            for (std::list<GameObject*>::iterator itr = tempTargetGOList.begin(); itr != tempTargetGOList.end();)
            {
                switch (targetMode)
                {
                    case TARGET_ENUM_GAMEOBJECTS_IN_CONE:
                        if (!m_caster->HasInArc(*itr, M_PI_F / 2))
                        {
                            tempTargetGOList.erase(itr++);
                            continue;
                        }
                    // no break here
                    case TARGET_ENUM_GAMEOBJECTS_SCRIPT_AOE_AT_SRC_LOC:
                    case TARGET_ENUM_GAMEOBJECTS_SCRIPT_AOE_AT_DEST_LOC:
                    default:
                        ++itr;
                }
            }
            break;
        }
        case TARGET_ENUM_UNITS_ENEMY_AOE_AT_DEST_LOC:
        {
            switch(m_spellInfo->Effect[effIndex])
            {
                case SPELL_EFFECT_PERSISTENT_AREA_AURA:
                    break;
                case SPELL_EFFECT_SUMMON:
                    targetUnitMap.push_back(m_caster);
                    break;
                default:
                    FillAreaTargets(targetUnitMap, radius, PUSH_DEST_CENTER, SPELL_TARGETS_AOE_DAMAGE);

                    // Mind Sear, triggered
                    if (m_spellInfo->IsFitToFamily(SPELLFAMILY_PRIEST, uint64(0x0008000000000000)))
                        if (Unit* unitTarget = m_targets.getUnitTarget())
                            targetUnitMap.remove(unitTarget);

                    break;
            }
            break;
        }
        case TARGET_LOCATION_UNIT_POSITION_63:
        {
            if (Unit* currentTarget = m_targets.getUnitTarget())
                m_targets.setDestination(currentTarget->GetPositionX(), currentTarget->GetPositionY(), currentTarget->GetPositionZ());
            break;
        }
        case TARGET_ENUM_UNITS_PARTY_WITHIN_CASTER_RANGE:
        {
            if (m_caster->GetObjectGuid().IsPet())
            {
                // only affect pet and owner
                targetUnitMap.push_back(m_caster);
                if (Unit* owner = m_caster->GetOwner())
                    targetUnitMap.push_back(owner);
            }
            else
            {
                FillRaidOrPartyTargets(targetUnitMap, m_caster, m_caster, radius, false, true, true);
            }
            break;
        }
        case TARGET_ENUM_UNITS_PARTY_AOE_AT_DEST_LOC:
        case TARGET_ENUM_UNITS_PARTY_AOE_AT_SRC_LOC:
        {
            FillRaidOrPartyTargets(targetUnitMap, m_caster, m_caster, radius, false, true, true);
            break;
        }
        case TARGET_ENUM_UNITS_RAID_WITHIN_CASTER_RANGE:
        {
            if (m_spellInfo->Id == 57669)                   // Replenishment (special target selection)
            {
                // in arena, target should be only caster
                if (m_caster->GetMap()->IsBattleArena())
                    targetUnitMap.push_back(m_caster);
                else
                    FillRaidOrPartyManaPriorityTargets(targetUnitMap, m_caster, m_caster, radius, 10, true, false, true);
            }
            else if (m_spellInfo->Id == 52759)              // Ancestral Awakening (special target selection)
                FillRaidOrPartyHealthPriorityTargets(targetUnitMap, m_caster, m_caster, radius, 1, true, false, true);
            else
                FillRaidOrPartyTargets(targetUnitMap, m_caster, m_caster, radius, true, true, IsPositiveSpell(m_spellInfo->Id));
            break;
        }
        case TARGET_UNIT_FRIEND:
        case TARGET_UNIT_RAID:
        {
            if (Unit* unitTarget = m_targets.getUnitTarget())
            {
                if (unitTarget->IsFriendlyTo(m_caster))
                    targetUnitMap.push_back(unitTarget);
                else
                {
                    if (m_spellInfo->HasAttribute(SPELL_ATTR_EX5_ALLOW_TARGET_OF_TARGET_AS_TARGET))
                    {
                        if (Unit* targetOfUnitTarget = m_caster->GetMap()->GetUnit(unitTarget->GetTargetGuid()))
                        {
                            if (targetOfUnitTarget->IsFriendlyTo(m_caster))
                            {
                                switch (m_spellInfo->Id)
                                {
                                    case 3411: // Intervene target must be in group with caster
                                        if (targetOfUnitTarget->GetTypeId() == TYPEID_PLAYER && m_caster->GetTypeId() == TYPEID_PLAYER)
                                            if (((Player*)targetOfUnitTarget)->IsInSameGroupWith((Player*)m_caster))
                                                targetUnitMap.push_back(targetOfUnitTarget);
                                        break;
                                    case 31789: // Righteous defense needs player target
                                        if (targetOfUnitTarget->GetTypeId() == TYPEID_PLAYER)
                                            targetUnitMap.push_back(targetOfUnitTarget);
                                        break;
                                    default:
                                        targetUnitMap.push_back(targetOfUnitTarget);
                                        break;
                                }
                            }
                        }
                    }
                }
            }
            break;
        }
        case TARGET_UNIT_CASTER_COMPANION:
            if (Unit* target = m_targets.getUnitTarget())
                if (target->GetTypeId() == TYPEID_UNIT && ((Creature*)target)->IsPet() && ((Pet*)target)->getPetType() == MINI_PET)
                    targetUnitMap.push_back(target);
            break;
        case TARGET_UNIT_CASTER_SUMMONER:
        {
            WorldObject* caster = GetAffectiveCasterObject();
            if (!caster)
                return;

            if (caster->GetTypeId() == TYPEID_UNIT && ((Creature*)caster)->IsTemporarySummon())
                targetUnitMap.push_back(((Creature*)caster)->GetSummoner());
            else if (caster->GetTypeId() == TYPEID_GAMEOBJECT && !((GameObject*)caster)->HasStaticDBSpawnData())
                targetUnitMap.push_back(((GameObject*)caster)->GetOwner());
            else
                sLog.outError("SPELL: Spell ID %u with target ID %u was used by non temporary summon object %s.", m_spellInfo->Id, targetMode, caster->GetGuidStr().c_str());
            break;
        }
        case TARGET_UNIT_CASTER_VEHICLE:
            if (m_caster->IsBoarded() && m_caster->GetTransportInfo()->IsOnVehicle())
                targetUnitMap.push_back((Unit*)m_caster->GetTransportInfo()->GetTransport());
            break;
        case TARGET_UNIT_CASTER_DRIVER:
            if (m_caster->IsVehicle())
                if (Unit* vehicleDriver = m_caster->GetCharmer())
                    targetUnitMap.push_back(vehicleDriver);
            break;
        case TARGET_UNIT_CASTER_PASSENGER_0:
        case TARGET_UNIT_CASTER_PASSENGER_1:
        case TARGET_UNIT_CASTER_PASSENGER_2:
        case TARGET_UNIT_CASTER_PASSENGER_3:
        case TARGET_UNIT_CASTER_PASSENGER_4:
        case TARGET_UNIT_CASTER_PASSENGER_5:
        case TARGET_UNIT_CASTER_PASSENGER_6:
        case TARGET_UNIT_CASTER_PASSENGER_7:
            if (m_caster->IsVehicle())
                if (Unit* passenger = m_caster->GetVehicleInfo()->GetPassenger(targetMode - TARGET_UNIT_CASTER_PASSENGER_0))
                    targetUnitMap.push_back(passenger);
            break;
        {
            // Check original caster is GO - set its coordinates as src cast
            if (WorldObject* caster = GetCastingObject())
                m_targets.setSource(caster->GetPositionX(), caster->GetPositionY(), caster->GetPositionZ());
            break;
        }
        case TARGET_ENUM_UNITS_ENEMY_WITHIN_CASTER_RANGE:
            FillAreaTargets(targetUnitMap, radius, PUSH_SELF_CENTER, SPELL_TARGETS_HOSTILE);
            break;
        case TARGET_ENUM_UNITS_FRIEND_AOE_AT_SRC_LOC:
            switch (m_spellInfo->Id)
            {
                case 64844:                                 // Divine Hymn
                    // target amount stored in parent spell dummy effect but hard to access
                    FillRaidOrPartyHealthPriorityTargets(targetUnitMap, m_caster, m_caster, radius, 3, true, false, true);
                    break;
                case 64904:                                 // Hymn of Hope
                    // target amount stored in parent spell dummy effect but hard to access
                    FillRaidOrPartyManaPriorityTargets(targetUnitMap, m_caster, m_caster, radius, 3, true, false, true);
                    break;
                default:
                    // selected friendly units (for casting objects) around casting object
                    FillAreaTargets(targetUnitMap, radius, PUSH_SELF_CENTER, SPELL_TARGETS_FRIENDLY, GetCastingObject());
                    break;
            }
            break;
        case TARGET_ENUM_UNITS_FRIEND_AOE_AT_DEST_LOC:
            // Death Pact (in fact selection by player selection)
            if (m_spellInfo->Id == 48743)
            {
                // checked in Spell::CheckCast
                if (m_caster->GetTypeId() == TYPEID_PLAYER)
                    if (Unit* target = m_caster->GetMap()->GetPet(((Player*)m_caster)->GetSelectionGuid()))
                        targetUnitMap.push_back(target);
            }
            // Circle of Healing
            else if (m_spellInfo->SpellFamilyName == SPELLFAMILY_PRIEST && m_spellInfo->SpellVisual[0] == 8253)
            {
                Unit* target = m_targets.getUnitTarget();
                if (!target)
                    target = m_caster;

                uint32 count = 5;
                // Glyph of Circle of Healing
                if (Aura const* glyph = m_caster->GetDummyAura(55675))
                    count += glyph->GetModifier()->m_amount;

                FillRaidOrPartyHealthPriorityTargets(targetUnitMap, m_caster, target, radius, count, true, false, true);
            }
            // Wild Growth
            else if (m_spellInfo->SpellFamilyName == SPELLFAMILY_DRUID && m_spellInfo->SpellIconID == 2864)
            {
                Unit* target = m_targets.getUnitTarget();
                if (!target)
                    target = m_caster;
                uint32 count = CalculateDamage(EFFECT_INDEX_2, m_caster); // stored in dummy effect, affected by mods

                FillRaidOrPartyHealthPriorityTargets(targetUnitMap, m_caster, target, radius, count, true, false, true);
            }
            else
                FillAreaTargets(targetUnitMap, radius, PUSH_DEST_CENTER, SPELL_TARGETS_FRIENDLY);
            break;
        // TARGET_UNIT_PARTY means that the spells can only be casted on a party member and not on the caster (some seals, fire shield from imp, etc..)
        case TARGET_UNIT_PARTY:
        {
            Unit* target = m_targets.getUnitTarget();
            // Those spells apparently can't be casted on the caster.
            if (target && target != m_caster)
            {
                // Can only be casted on group's members or its pets
                Group*  pGroup = nullptr;

                Unit* owner = m_caster->GetMaster();
                Unit* targetOwner = target->GetMaster();
                if (owner)
                {
                    if (owner->GetTypeId() == TYPEID_PLAYER)
                    {
                        if (target == owner)
                        {
                            targetUnitMap.push_back(target);
                            break;
                        }
                        pGroup = ((Player*)owner)->GetGroup();
                    }
                }
                else if (m_caster->GetTypeId() == TYPEID_PLAYER)
                {
                    if (targetOwner == m_caster && target->GetTypeId() == TYPEID_UNIT && ((Creature*)target)->IsPet())
                    {
                        targetUnitMap.push_back(target);
                        break;
                    }
                    pGroup = ((Player*)m_caster)->GetGroup();
                }

                if (pGroup)
                {
                    // Our target can also be a player's pet who's grouped with us or our pet. But can't be controlled player
                    if (targetOwner)
                    {
                        if (targetOwner->GetTypeId() == TYPEID_PLAYER &&
                                target->GetTypeId() == TYPEID_UNIT && (((Creature*)target)->IsPet()) &&
                                target->GetOwnerGuid() == targetOwner->GetObjectGuid() &&
                                pGroup->IsMember(((Player*)targetOwner)->GetObjectGuid()))
                        {
                            targetUnitMap.push_back(target);
                        }
                    }
                    // 1Our target can be a player who is on our group
                    else if (target->GetTypeId() == TYPEID_PLAYER && pGroup->IsMember(((Player*)target)->GetObjectGuid()))
                    {
                        targetUnitMap.push_back(target);
                    }
                }
            }
            break;
        }
        case TARGET_GAMEOBJECT:
            if (m_targets.getGOTarget())
                AddGOTarget(m_targets.getGOTarget(), effIndex);
            break;
        case TARGET_ENUM_UNITS_ENEMY_IN_CONE_24:
        {
            SpellNotifyPushType pushType = PUSH_IN_FRONT;
            switch (m_spellInfo->SpellVisual[0])            // Some spell require a different target fill
            {
                case 3879: pushType = PUSH_IN_BACK;     break;
                case 7441: pushType = PUSH_IN_FRONT_15; break;
                case 8669: pushType = PUSH_IN_FRONT_15; break;
            }
            FillAreaTargets(targetUnitMap, radius, pushType, SPELL_TARGETS_AOE_DAMAGE);
            break;
        }
        case TARGET_ENUM_UNITS_ENEMY_IN_CONE_54:
            FillAreaTargets(targetUnitMap, radius, PUSH_IN_FRONT_90, SPELL_TARGETS_AOE_DAMAGE);
            break;
        case TARGET_ENUM_UNITS_FRIEND_IN_CONE:
            FillAreaTargets(targetUnitMap, radius, PUSH_IN_FRONT_30, SPELL_TARGETS_FRIENDLY);
            break;
        case TARGET_ENUM_UNITS_SCRIPT_IN_CONE_60:
        case TARGET_ENUM_UNITS_SCRIPT_IN_CONE_110:
        {
            SpellTargets targetB = SPELL_TARGETS_AOE_DAMAGE;

            if (m_spellInfo->Effect[effIndex] == SPELL_EFFECT_SCRIPT_EFFECT)
                targetB = SPELL_TARGETS_ALL;

            UnitList tempTargetUnitMap;
            SQLMultiStorage::SQLMSIteratorBounds<SpellTargetEntry> bounds = sSpellScriptTargetStorage.getBounds<SpellTargetEntry>(m_spellInfo->Id);

            // fill real target list if no spell script target defined
            FillAreaTargets(bounds.first != bounds.second ? tempTargetUnitMap : targetUnitMap,
                            radius, PUSH_IN_FRONT_15, bounds.first != bounds.second ? SPELL_TARGETS_ALL : targetB);

            if (!tempTargetUnitMap.empty())
                CheckSpellScriptTargets(bounds, tempTargetUnitMap, targetUnitMap, effIndex);
            break;
        }
        case TARGET_ENUM_UNITS_ENEMY_IN_CONE_104:
            FillAreaTargets(targetUnitMap, radius, PUSH_IN_FRONT_30, SPELL_TARGETS_AOE_DAMAGE);
            break;
        case TARGET_UNIT:
        {
            if (Unit* target = m_targets.getUnitTarget())
            {
                if (m_caster->IsFriendlyTo(target))
                {
                    targetUnitMap.push_back(target);
                }
                else
                {
                    if (Unit* pUnitTarget = m_caster->SelectMagnetTarget(target, this, effIndex))
                    {
                        if (target != pUnitTarget)
                        {
                            m_targets.setUnitTarget(pUnitTarget);
                            m_spellFlags |= SPELL_FLAG_REDIRECTED;
                        }
                        targetUnitMap.push_back(pUnitTarget);
                    }
                }
            }
            break;
        }
        case TARGET_LOCKED:
            if (m_targets.getGOTargetGuid())
                AddGOTarget(m_targets.getGOTarget(), effIndex);
            else if (m_targets.getItemTarget())
                AddItemTarget(m_targets.getItemTarget(), effIndex);
            break;
        case TARGET_UNIT_CASTER_MASTER:
            if (Unit* owner = m_caster->GetMaster())
                targetUnitMap.push_back(owner);
            break;
        case TARGET_ENUM_UNITS_ENEMY_AOE_AT_DYNOBJ_LOC:
            // targets the ground, not the units in the area
            if (m_spellInfo->Effect[effIndex] !=SPELL_EFFECT_PERSISTENT_AREA_AURA)
                FillAreaTargets(targetUnitMap, radius, PUSH_DEST_CENTER, SPELL_TARGETS_AOE_DAMAGE);
            break;
        case TARGET_LOCATION_UNIT_MINION_POSITION:
            if(m_spellInfo->Effect[effIndex] != SPELL_EFFECT_DUEL)
                targetUnitMap.push_back(m_caster);
            break;
        case TARGET_UNIT_CHANNEL_TARGET:
        {
            if (Unit* pUnitTarget = m_caster->SelectMagnetTarget(m_caster->GetMap()->GetUnit(m_caster->GetChannelObjectGuid())))
            {
                if (m_targets.getUnitTarget() && m_targets.getUnitTarget() != pUnitTarget)
                    m_spellFlags |= SPELL_FLAG_REDIRECTED;

                m_targets.setUnitTarget(pUnitTarget);
                targetUnitMap.push_back(pUnitTarget);
            }
            break;
        }
        case TARGET_UNIT_FRIEND_AND_PARTY:
        {
            Unit* owner = m_caster->GetMaster();
            Player* pTarget = nullptr;

            if (owner)
            {
                targetUnitMap.push_back(m_caster);
                if (owner->GetTypeId() == TYPEID_PLAYER)
                    pTarget = (Player*)owner;
            }
            else if (m_caster->GetTypeId() == TYPEID_PLAYER)
            {
                if (Unit* target = m_targets.getUnitTarget())
                {
                    if (target->GetTypeId() != TYPEID_PLAYER)
                    {
                        if (((Creature*)target)->IsPet())
                        {
                            Unit* targetOwner = target->GetOwner();
                            if (targetOwner->GetTypeId() == TYPEID_PLAYER)
                                pTarget = (Player*)targetOwner;
                        }
                    }
                    else
                        pTarget = (Player*)target;
                }
            }

            Group* pGroup = pTarget ? pTarget->GetGroup() : nullptr;

            if (pGroup)
            {
                uint8 subgroup = pTarget->GetSubGroup();

                for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
                {
                    Player* Target = itr->getSource();

                    // IsHostileTo check duel and controlled by enemy
                    if (Target && Target->GetSubGroup() == subgroup && !m_caster->IsHostileTo(Target))
                    {
                        if (pTarget->IsWithinDistInMap(Target, radius))
                            targetUnitMap.push_back(Target);

                        if (Pet* pet = Target->GetPet())
                            if (pTarget->IsWithinDistInMap(pet, radius))
                                targetUnitMap.push_back(pet);
                    }
                }
            }
            else if (owner)
            {
                if (m_caster->IsWithinDistInMap(owner, radius))
                    targetUnitMap.push_back(owner);
            }
            else if (pTarget)
            {
                targetUnitMap.push_back(pTarget);

                if (Pet* pet = pTarget->GetPet())
                    if (m_caster->IsWithinDistInMap(pet, radius))
                        targetUnitMap.push_back(pet);
            }
            break;
        }
        case TARGET_UNIT_SCRIPT_NEAR_CASTER:
        {
            if (m_targets.getUnitTarget())
                targetUnitMap.push_back(m_targets.getUnitTarget());
            if (m_targets.getItemTarget())
                AddItemTarget(m_targets.getItemTarget(), effIndex);
            break;
        }
        case TARGET_LOCATION_CASTER_FISHING_SPOT:
            targetUnitMap.push_back(m_caster);
            break;
        case TARGET_UNIT_FRIEND_CHAIN_HEAL:
        {
            Unit* pUnitTarget = m_targets.getUnitTarget();
            if (!pUnitTarget)
                break;

            if (EffectChainTarget <= 1)
                targetUnitMap.push_back(pUnitTarget);
            else
            {
                unMaxTargets = EffectChainTarget;
                float max_range = radius + unMaxTargets * CHAIN_SPELL_JUMP_RADIUS;

                UnitList tempTargetUnitMap;

                FillAreaTargets(tempTargetUnitMap, max_range, PUSH_SELF_CENTER, SPELL_TARGETS_FRIENDLY);

                if (m_caster != pUnitTarget && std::find(tempTargetUnitMap.begin(), tempTargetUnitMap.end(), m_caster) == tempTargetUnitMap.end())
                    tempTargetUnitMap.push_front(m_caster);

                tempTargetUnitMap.sort(TargetDistanceOrderNear(pUnitTarget));

                if (tempTargetUnitMap.empty())
                    break;

                if (*tempTargetUnitMap.begin() == pUnitTarget)
                    tempTargetUnitMap.erase(tempTargetUnitMap.begin());

                targetUnitMap.push_back(pUnitTarget);
                uint32 t = unMaxTargets - 1;
                Unit* prev = pUnitTarget;
                UnitList::iterator next = tempTargetUnitMap.begin();

                while (t && next != tempTargetUnitMap.end())
                {
                    if (!prev->IsWithinDist(*next, CHAIN_SPELL_JUMP_RADIUS))
                        break;

                    if (!m_spellInfo->HasAttribute(SPELL_ATTR_EX2_IGNORE_LOS) && !prev->IsWithinLOSInMap(*next))
                    {
                        ++next;
                        continue;
                    }

                    if ((*next)->GetHealth() == (*next)->GetMaxHealth())
                    {
                        next = tempTargetUnitMap.erase(next);
                        continue;
                    }

                    prev = *next;
                    targetUnitMap.push_back(prev);
                    tempTargetUnitMap.erase(next);
                    tempTargetUnitMap.sort(TargetDistanceOrderNear(prev));
                    next = tempTargetUnitMap.begin();

                    --t;
                }
            }
            break;
        }
        case TARGET_LOCATION_CASTER_TARGET_POSITION:
        {
            Unit* currentTarget = m_targets.getUnitTarget();
            if (currentTarget)
            {
                targetUnitMap.push_back(currentTarget);
                m_targets.setDestination(currentTarget->GetPositionX(), currentTarget->GetPositionY(), currentTarget->GetPositionZ());
            }
            break;
        }
        case TARGET_UNIT_RAID_AND_CLASS:
        {
            Player* targetPlayer = m_targets.getUnitTarget() && m_targets.getUnitTarget()->GetTypeId() == TYPEID_PLAYER
                                   ? (Player*)m_targets.getUnitTarget() : nullptr;

            Group* pGroup = targetPlayer ? targetPlayer->GetGroup() : nullptr;
            if (pGroup)
            {
                for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
                {
                    Player* Target = itr->getSource();

                    // IsHostileTo check duel and controlled by enemy
                    if (Target && targetPlayer->IsWithinDistInMap(Target, radius) &&
                            targetPlayer->getClass() == Target->getClass() &&
                            !m_caster->IsHostileTo(Target))
                    {
                        targetUnitMap.push_back(Target);
                    }
                }
            }
            else if (m_targets.getUnitTarget())
                targetUnitMap.push_back(m_targets.getUnitTarget());
            break;
        }
        case TARGET_LOCATION_DATABASE:
        {
            if (SpellTargetPosition const* st = sSpellMgr.GetSpellTargetPosition(m_spellInfo->Id))
            {
                m_targets.setDestination(st->target_X, st->target_Y, st->target_Z);
                // TODO - maybe use an (internal) value for the map for neat far teleport handling

                // far-teleport spells are handled in SpellEffect, elsewise report an error about an unexpected map (spells are always locally)
                if (st->target_mapId != m_caster->GetMapId() && m_spellInfo->Effect[effIndex] != SPELL_EFFECT_TELEPORT_UNITS && m_spellInfo->Effect[effIndex] != SPELL_EFFECT_BIND)
                    sLog.outError("SPELL: wrong map (%u instead %u) target coordinates for spell ID %u", st->target_mapId, m_caster->GetMapId(), m_spellInfo->Id);
            }
            else
                sLog.outError("SPELL: unknown target coordinates for spell ID %u", m_spellInfo->Id);
            break;
        }
        case TARGET_LOCATION_UNIT_FRONT:
        case TARGET_LOCATION_UNIT_BACK:
        case TARGET_LOCATION_UNIT_RIGHT:
        case TARGET_LOCATION_UNIT_LEFT:
        {
            Unit* pTarget = nullptr;

            // explicit cast data from client or server-side cast
            // some spell at client send caster
            if (m_targets.getUnitTarget() && m_targets.getUnitTarget() != m_caster)
                pTarget = m_targets.getUnitTarget();
            else if (m_caster->GetVictim())
                pTarget = m_caster->GetVictim();
            else if (m_caster->GetTypeId() == TYPEID_PLAYER)
                pTarget = ObjectAccessor::GetUnit(*m_caster, ((Player*)m_caster)->GetSelectionGuid());
            else if (m_targets.getUnitTarget())
                pTarget = m_caster;

            if (pTarget)
            {
                float angle = 0.0f;

                switch (targetMode)
                {
                    case TARGET_LOCATION_UNIT_FRONT:                        break;
                    case TARGET_LOCATION_UNIT_BACK:      angle = M_PI_F;       break;
                    case TARGET_LOCATION_UNIT_RIGHT:  angle = -M_PI_F / 2;  break;
                    case TARGET_LOCATION_UNIT_LEFT:   angle = M_PI_F / 2;   break;
                }

                float _target_x, _target_y, _target_z;
                pTarget->GetClosePoint(_target_x, _target_y, _target_z, pTarget->GetObjectBoundingRadius(), radius, angle);
                if (pTarget->IsWithinLOS(_target_x, _target_y, _target_z))
                {
                    targetUnitMap.push_back(m_caster);
                    m_targets.setDestination(_target_x, _target_y, _target_z);
                }
            }
            break;
        }
        case TARGET_LOCATION_DYNOBJ_POSITION:
            // if parent spell create dynamic object extract area from it
            if (DynamicObject* dynObj = m_caster->GetDynObject(m_triggeredByAuraSpell ? m_triggeredByAuraSpell->Id : m_spellInfo->Id))
                m_targets.setDestination(dynObj->GetPositionX(), dynObj->GetPositionY(), dynObj->GetPositionZ());
            // else use destination of target if no destination set (ie for Mind Sear - 53022)
            else if (!(m_targets.m_targetMask & TARGET_FLAG_DEST_LOCATION) && m_targets.m_targetMask & TARGET_FLAG_UNIT)
                m_targets.setDestination(m_targets.m_destX, m_targets.m_destY, m_targets.m_destZ);
            break;
        case TARGET_LOCATION_CASTER_FRONT:
        case TARGET_LOCATION_CASTER_BACK:
        case TARGET_LOCATION_CASTER_LEFT:
        case TARGET_LOCATION_CASTER_RIGHT:
        {
            if (!(m_targets.m_targetMask & TARGET_FLAG_DEST_LOCATION))
            {
                // General override, we don't want to use max spell range here.
                // Note: 0.0 radius is also for index 36. It is possible that 36 must be defined as
                // "at the base of", in difference to 0 which appear to be "directly in front of".
                // TODO: some summoned will make caster be half inside summoned object. Need to fix
                // that in the below code (nearpoint vs closepoint, etc).
                if (m_spellInfo->EffectRadiusIndex[effIndex] == 0)
                    radius = 0.0f;

                if (m_spellInfo->Id == 50019)               // Hawk Hunting, problematic 50K radius
                    radius = 10.0f;

                float angle = m_caster->GetOrientation();
                switch (targetMode)
                {
                    case TARGET_LOCATION_CASTER_FRONT:                           break;
                    case TARGET_LOCATION_CASTER_BACK:      angle += M_PI_F;      break;
                    case TARGET_LOCATION_CASTER_LEFT:   angle += M_PI_F / 2;  break;
                    case TARGET_LOCATION_CASTER_RIGHT:  angle -= M_PI_F / 2;  break;
                }

                float x, y;
                m_caster->GetNearPoint2D(x, y, radius + m_caster->GetObjectBoundingRadius(), angle);
                m_targets.setDestination(x, y, m_caster->GetPositionZ());
            }

            targetUnitMap.push_back(m_caster);
            break;
        }
        case TARGET_LOCATION_NORTH:
        case TARGET_LOCATION_SOUTH:
        case TARGET_LOCATION_EAST:
        case TARGET_LOCATION_WEST:
        case TARGET_LOCATION_NE:
        case TARGET_LOCATION_NW:
        case TARGET_LOCATION_SE:
        case TARGET_LOCATION_SW:
        {
            if (!(m_targets.m_targetMask & TARGET_FLAG_DEST_LOCATION))
            {
                Unit* currentTarget = m_targets.getUnitTarget() ? m_targets.getUnitTarget() : m_caster;
                float angle = currentTarget != m_caster ? currentTarget->GetAngle(m_caster) : m_caster->GetOrientation();

                switch (targetMode)
                {
                    case TARGET_LOCATION_NORTH:                         break;
                    case TARGET_LOCATION_SOUTH: angle +=   M_PI_F;        break;
                    case TARGET_LOCATION_EAST:  angle -=   M_PI_F / 2;    break;
                    case TARGET_LOCATION_WEST:  angle +=   M_PI_F / 2;    break;
                    case TARGET_LOCATION_NE:    angle -=   M_PI_F / 4;    break;
                    case TARGET_LOCATION_NW:    angle +=   M_PI_F / 4;    break;
                    case TARGET_LOCATION_SE:    angle -= 3 * M_PI_F / 4;    break;
                    case TARGET_LOCATION_SW:    angle += 3 * M_PI_F / 4;    break;
                }

                float x, y;
                currentTarget->GetNearPoint2D(x, y, radius + currentTarget->GetObjectBoundingRadius(), angle);
                m_targets.setDestination(x, y, currentTarget->GetPositionZ());
            }
            break;
        }
        case TARGET_LOCATION_TRAJECTORY:
        {
            if (!(m_targets.m_targetMask & TARGET_FLAG_DEST_LOCATION))
            {
                SpellRangeEntry const* rEntry = sSpellRangeStore.LookupEntry(m_spellInfo->rangeIndex);
                float minRange = GetSpellMinRange(rEntry);
                float maxRange = GetSpellMaxRange(rEntry);
                float dist = minRange + rand_norm_f() * (maxRange - minRange);

                float _target_x, _target_y, _target_z;
                m_caster->GetClosePoint(_target_x, _target_y, _target_z, m_caster->GetObjectBoundingRadius(), dist);
                m_targets.setDestination(_target_x, _target_y, _target_z);
            }

            targetUnitMap.push_back(m_caster);
            break;
        }
        case TARGET_LOCATION_CASTER_DEST:
        {
            // add here custom effects that need default target.
            // FOR EVERY TARGET TYPE THERE IS A DIFFERENT FILL!!
            switch(m_spellInfo->Effect[effIndex])
            {
                case SPELL_EFFECT_DUMMY:
                {
                    switch (m_spellInfo->Id)
                    {
                        case 20577:                         // Cannibalize
                        {
                            WorldObject* result = FindCorpseUsing<MaNGOS::CannibalizeObjectCheck> ();

                            if (result)
                            {
                                switch (result->GetTypeId())
                                {
                                    case TYPEID_UNIT:
                                    case TYPEID_PLAYER:
                                        targetUnitMap.push_back((Unit*)result);
                                        break;
                                    case TYPEID_CORPSE:
                                        m_targets.setCorpseTarget((Corpse*)result);
                                        if (Player* owner = ObjectAccessor::FindPlayer(((Corpse*)result)->GetOwnerGuid()))
                                            targetUnitMap.push_back(owner);
                                        break;
                                }
                            }
                            else
                            {
                                // clear cooldown at fail
                                if (m_caster->GetTypeId() == TYPEID_PLAYER)
                                    m_caster->RemoveSpellCooldown(m_spellInfo->Id, true);
                                SendCastResult(SPELL_FAILED_NO_EDIBLE_CORPSES);
                                finish(false);
                            }
                            break;
                        }
                        default:
                            if (m_targets.getUnitTarget())
                                targetUnitMap.push_back(m_targets.getUnitTarget());
                            break;
                    }
                    // Add AoE target-mask to self, if no target-dest provided already
                    if ((m_targets.m_targetMask & TARGET_FLAG_DEST_LOCATION) == 0)
                        m_targets.setDestination(m_caster->GetPositionX(), m_caster->GetPositionY(), m_caster->GetPositionZ());
                    break;
                }
                case SPELL_EFFECT_BIND:
                case SPELL_EFFECT_PARRY:
                case SPELL_EFFECT_BLOCK:
                case SPELL_EFFECT_CREATE_ITEM:
                case SPELL_EFFECT_WEAPON:
                case SPELL_EFFECT_TRIGGER_SPELL:
                case SPELL_EFFECT_TRIGGER_MISSILE:
                case SPELL_EFFECT_LEARN_SPELL:
                case SPELL_EFFECT_SKILL_STEP:
                case SPELL_EFFECT_PROFICIENCY:
                case SPELL_EFFECT_SUMMON_OBJECT_WILD:
                case SPELL_EFFECT_SELF_RESURRECT:
                case SPELL_EFFECT_REPUTATION:
                case SPELL_EFFECT_SEND_TAXI:
                    if (m_targets.getUnitTarget())
                        targetUnitMap.push_back(m_targets.getUnitTarget());
                    // Triggered spells have additional spell targets - cast them even if no explicit unit target is given (required for spell 50516 for example)
                    else if (m_spellInfo->Effect[effIndex] == SPELL_EFFECT_TRIGGER_SPELL)
                        targetUnitMap.push_back(m_caster);
                    break;
                case SPELL_EFFECT_SUMMON_PLAYER:
                    if (m_caster->GetTypeId() == TYPEID_PLAYER && ((Player*)m_caster)->GetSelectionGuid())
                        if (Player* target = sObjectMgr.GetPlayer(((Player*)m_caster)->GetSelectionGuid()))
                            targetUnitMap.push_back(target);
                    break;
                case SPELL_EFFECT_RESURRECT:
                case SPELL_EFFECT_RESURRECT_NEW:
                    if (m_targets.getUnitTarget())
                        targetUnitMap.push_back(m_targets.getUnitTarget());
                    if (m_targets.getCorpseTargetGuid())
                    {
                        if (Corpse* corpse = m_caster->GetMap()->GetCorpse(m_targets.getCorpseTargetGuid()))
                            if (Player* owner = ObjectAccessor::FindPlayer(corpse->GetOwnerGuid()))
                                targetUnitMap.push_back(owner);
                    }
                    break;
                case SPELL_EFFECT_TELEPORT_UNITS:
                case SPELL_EFFECT_SUMMON:
                case SPELL_EFFECT_SUMMON_CHANGE_ITEM:
                case SPELL_EFFECT_TRANS_DOOR:
                case SPELL_EFFECT_ADD_FARSIGHT:
                case SPELL_EFFECT_APPLY_GLYPH:
                case SPELL_EFFECT_STUCK:
                case SPELL_EFFECT_BREAK_PLAYER_TARGETING:
                case SPELL_EFFECT_SUMMON_ALL_TOTEMS:
                case SPELL_EFFECT_FEED_PET:
                case SPELL_EFFECT_DESTROY_ALL_TOTEMS:
                case SPELL_EFFECT_SKILL:
                    targetUnitMap.push_back(m_caster);
                    break;
                case SPELL_EFFECT_PERSISTENT_AREA_AURA:
                    if (Unit* currentTarget = m_targets.getUnitTarget())
                        m_targets.setDestination(currentTarget->GetPositionX(), currentTarget->GetPositionY(), currentTarget->GetPositionZ());
                    break;
                case SPELL_EFFECT_LEARN_PET_SPELL:
                    if (Pet* pet = m_caster->GetPet())
                        targetUnitMap.push_back(pet);
                    break;
                case SPELL_EFFECT_ENCHANT_ITEM:
                case SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY:
                case SPELL_EFFECT_ENCHANT_ITEM_PRISMATIC:
                case SPELL_EFFECT_DISENCHANT:
                case SPELL_EFFECT_PROSPECTING:
                case SPELL_EFFECT_MILLING:
                    if (m_targets.getItemTarget())
                        AddItemTarget(m_targets.getItemTarget(), effIndex);
                    break;
                case SPELL_EFFECT_APPLY_AURA:
                    switch(m_spellInfo->EffectApplyAuraName[effIndex])
                    {
                        case SPELL_AURA_ADD_FLAT_MODIFIER:  // some spell mods auras have 0 target modes instead expected TARGET_SELF(1) (and present for other ranks for same spell for example)
                        case SPELL_AURA_ADD_PCT_MODIFIER:
                            targetUnitMap.push_back(m_caster);
                            break;
                        default:                            // apply to target in other case
                            if (m_targets.getUnitTarget())
                                targetUnitMap.push_back(m_targets.getUnitTarget());
                            break;
                    }
                    break;
                case SPELL_EFFECT_APPLY_AREA_AURA_PARTY:
                    // AreaAura
                    if ((m_spellInfo->Attributes == (SPELL_ATTR_NOT_SHAPESHIFT | SPELL_ATTR_DONT_AFFECT_SHEATH_STATE | SPELL_ATTR_CASTABLE_WHILE_MOUNTED | SPELL_ATTR_CASTABLE_WHILE_SITTING)) || (m_spellInfo->Attributes == SPELL_ATTR_NOT_SHAPESHIFT))
                        SetTargetMap(effIndex, TARGET_UNIT_FRIEND_AND_PARTY, targetUnitMap);
                    break;
                case SPELL_EFFECT_SKIN_PLAYER_CORPSE:
                    if (m_targets.getUnitTarget())
                        targetUnitMap.push_back(m_targets.getUnitTarget());
                    else if (m_targets.getCorpseTargetGuid())
                    {
                        if (Corpse* corpse = m_caster->GetMap()->GetCorpse(m_targets.getCorpseTargetGuid()))
                            if (Player* owner = ObjectAccessor::FindPlayer(corpse->GetOwnerGuid()))
                                targetUnitMap.push_back(owner);
                    }
                    break;
                default:
                    break;
            }
            break;
        }
        case TARGET_LOCATION_CASTER_FRONT_LEAP:
        case TARGET_LOCATION_UNIT_RANDOM_CIRCUMFERENCE:
        {
            Unit* pUnitTarget = m_targets.getUnitTarget();

            if (!pUnitTarget)
                break;

            float dist = GetSpellRadius(sSpellRadiusStore.LookupEntry(m_spellInfo->EffectRadiusIndex[effIndex]));
            const float IN_OR_UNDER_LIQUID_RANGE = 0.8f;                // range to make player under liquid or on liquid surface from liquid level

            G3D::Vector3 prevPos, nextPos;
            float orientation = pUnitTarget->GetOrientation();

            prevPos.x = pUnitTarget->GetPositionX();
            prevPos.y = pUnitTarget->GetPositionY();
            prevPos.z = pUnitTarget->GetPositionZ();

            float groundZ = prevPos.z;
            bool isPrevInLiquid = false;

            // falling case
            if (!pUnitTarget->GetMap()->GetHeightInRange(pUnitTarget->GetPhaseMask(), prevPos.x, prevPos.y, groundZ, 3.0f) && pUnitTarget->m_movementInfo.HasMovementFlag(MOVEFLAG_FALLING))
            {
                nextPos.x = prevPos.x + dist * cos(orientation);
                nextPos.y = prevPos.y + dist * sin(orientation);
                nextPos.z = prevPos.z - 2.0f; // little hack to avoid the impression to go up when teleporting instead of continue to fall. This value may need some tweak

                                              //
                GridMapLiquidData liquidData;
                if (pUnitTarget->GetMap()->GetTerrain()->IsInWater(nextPos.x, nextPos.y, nextPos.z, &liquidData))
                {
                    if (fabs(nextPos.z - liquidData.level) < 10.0f)
                        nextPos.z = liquidData.level - IN_OR_UNDER_LIQUID_RANGE;
                }
                else
                {
                    // fix z to ground if near of it
                    pUnitTarget->GetMap()->GetHeightInRange(pUnitTarget->GetPhaseMask(), nextPos.x, nextPos.y, nextPos.z, 10.0f);
                }

                // check any obstacle and fix coords
                pUnitTarget->GetMap()->GetHitPosition(prevPos.x, prevPos.y, prevPos.z + 0.5f, nextPos.x, nextPos.y, nextPos.z, pUnitTarget->GetPhaseMask(), -0.5f);
            }
            else
            {
                // fix origin position if player was jumping and near of the ground but not in ground
                if (fabs(prevPos.z - groundZ) > 0.5f)
                    prevPos.z = groundZ;

                //check if in liquid
                isPrevInLiquid = pUnitTarget->GetMap()->GetTerrain()->IsInWater(prevPos.x, prevPos.y, prevPos.z);

                const float step = 2.0f;                                    // step length before next check slope/edge/water
                const float maxSlope = 50.0f;                               // 50(degree) max seem best value for walkable slope
                const float MAX_SLOPE_IN_RADIAN = maxSlope / 180.0f * M_PI_F;
                float nextZPointEstimation = 1.0f;
                float destx = prevPos.x + dist * cos(orientation);
                float desty = prevPos.y + dist * sin(orientation);
                const uint32 numChecks = ceil(fabs(dist / step));
                const float DELTA_X = (destx - prevPos.x) / numChecks;
                const float DELTA_Y = (desty - prevPos.y) / numChecks;

                for (uint32 i = 1; i < numChecks + 1; ++i)
                {
                    // compute next point average position
                    nextPos.x = prevPos.x + DELTA_X;
                    nextPos.y = prevPos.y + DELTA_Y;
                    nextPos.z = prevPos.z + nextZPointEstimation;

                    bool isInLiquid = false;
                    bool isInLiquidTested = false;
                    bool isOnGround = false;
                    GridMapLiquidData liquidData;

                    // try fix height for next position
                    if (!pUnitTarget->GetMap()->GetHeightInRange(pUnitTarget->GetPhaseMask(), nextPos.x, nextPos.y, nextPos.z))
                    {
                        // we cant so test if we are on water
                        if (!pUnitTarget->GetMap()->GetTerrain()->IsInWater(nextPos.x, nextPos.y, nextPos.z, &liquidData))
                        {
                            // not in water and cannot get correct height, maybe flying?
                            //sLog.outString("Can't get height of point %u, point value %s", i, nextPos.toString().c_str());
                            nextPos = prevPos;
                            break;
                        }
                        else
                        {
                            isInLiquid = true;
                            isInLiquidTested = true;
                        }
                    }
                    else
                        isOnGround = true;                                  // player is on ground

                    if (isInLiquid || (!isInLiquidTested && pUnitTarget->GetMap()->GetTerrain()->IsInWater(nextPos.x, nextPos.y, nextPos.z, &liquidData)))
                    {
                        if (!isPrevInLiquid && fabs(liquidData.level - prevPos.z) > 2.0f)
                        {
                            // on edge of water with difference a bit to high to continue
                            //sLog.outString("Ground vs liquid edge detected!");
                            nextPos = prevPos;
                            break;
                        }

                        if ((liquidData.level - IN_OR_UNDER_LIQUID_RANGE) > nextPos.z)
                            nextPos.z = prevPos.z;                                      // we are under water so next z equal prev z
                        else
                            nextPos.z = liquidData.level - IN_OR_UNDER_LIQUID_RANGE;    // we are on water surface, so next z equal liquid level

                        isInLiquid = true;

                        float ground = nextPos.z;
                        if (pUnitTarget->GetMap()->GetHeightInRange(pUnitTarget->GetPhaseMask(), nextPos.x, nextPos.y, ground))
                        {
                            if (nextPos.z < ground)
                            {
                                nextPos.z = ground;
                                isOnGround = true;                          // player is on ground of the water
                            }
                        }
                    }

                    //unitTarget->SummonCreature(VISUAL_WAYPOINT, nextPos.x, nextPos.y, nextPos.z, 0, TEMPSPAWN_TIMED_DESPAWN, 15000);
                    float hitZ = nextPos.z + 1.5f;
                    if (pUnitTarget->GetMap()->GetHitPosition(prevPos.x, prevPos.y, prevPos.z + 1.5f, nextPos.x, nextPos.y, hitZ, pUnitTarget->GetPhaseMask(), -1.0f))
                    {
                        //sLog.outString("Blink collision detected!");
                        nextPos = prevPos;
                        break;
                    }

                    if (isOnGround)
                    {
                        // project vector to get only positive value
                        float ac = fabs(prevPos.z - nextPos.z);

                        // compute slope (in radian)
                        float slope = atan(ac / step);

                        // check slope value
                        if (slope > MAX_SLOPE_IN_RADIAN)
                        {
                            //sLog.outString("bad slope detected! %4.2f max %4.2f, ac(%4.2f)", slope * 180 / M_PI_F, maxSlope, ac);
                            nextPos = prevPos;
                            break;
                        }
                        //sLog.outString("slope is ok! %4.2f max %4.2f, ac(%4.2f)", slope * 180 / M_PI_F, maxSlope, ac);
                    }

                    //sLog.outString("point %u is ok, coords %s", i, nextPos.toString().c_str());
                    nextZPointEstimation = (nextPos.z - prevPos.z) / 2.0f;
                    isPrevInLiquid = isInLiquid;
                    prevPos = nextPos;
                }
            }
            m_targets.setDestination(nextPos.x, nextPos.y, nextPos.z);
        }
        default:
            // sLog.outError( "SPELL: Unknown implicit target (%u) for spell ID %u", targetMode, m_spellInfo->Id );
            break;
    }

    // remove caster from the list if required by attribute
    if (m_spellInfo->HasAttribute(SPELL_ATTR_EX_CANT_TARGET_SELF))
        if (targetMode != TARGET_UNIT_CASTER && targetMode != TARGET_LOCATION_CURRENT_REFERENCE && (m_spellInfo->Effect[effIndex] != SPELL_EFFECT_SUMMON))
            targetUnitMap.remove(m_caster);

    if (!tempTargetGOList.empty())                          // GO CASE
    {
        if (unMaxTargets && tempTargetGOList.size() > unMaxTargets)
        {
            // make sure one go is always removed per iteration
            uint32 removed_utarget = 0;
            for (std::list<GameObject*>::iterator itr = tempTargetGOList.begin(), next; itr != tempTargetGOList.end(); itr = next)
            {
                next = itr;
                ++next;
                if (!*itr) continue;
                if ((*itr) == m_targets.getGOTarget())
                {
                    tempTargetGOList.erase(itr);
                    removed_utarget = 1;
                    //        break;
                }
            }
            // remove random units from the map
            while (tempTargetGOList.size() > unMaxTargets - removed_utarget)
            {
                uint32 poz = urand(0, tempTargetGOList.size() - 1);
                for (std::list<GameObject*>::iterator itr = tempTargetGOList.begin(); itr != tempTargetGOList.end(); ++itr, --poz)
                {
                    if (!*itr) continue;

                    if (!poz)
                    {
                        tempTargetGOList.erase(itr);
                        break;
                    }
                }
            }
        }
        // Add resulting GOs as GOTargets
        for (std::list<GameObject*>::iterator iter = tempTargetGOList.begin(); iter != tempTargetGOList.end(); ++iter)
            AddGOTarget(*iter, effIndex);
    }
}
}

void Spell::CheckSpellScriptTargets(SQLMultiStorage::SQLMSIteratorBounds<SpellTargetEntry> &bounds, UnitList &tempTargetUnitMap, UnitList &targetUnitMap, SpellEffectIndex effIndex)
{
    for (UnitList::const_iterator iter = tempTargetUnitMap.begin(); iter != tempTargetUnitMap.end(); ++iter)
    {
        if ((*iter)->GetTypeId() != TYPEID_UNIT)
            continue;

        for (SQLMultiStorage::SQLMultiSIterator<SpellTargetEntry> i_spellST = bounds.first; i_spellST != bounds.second; ++i_spellST)
        {
            if (i_spellST->CanNotHitWithSpellEffect(effIndex))
                continue;

            // only creature entries supported for this target type
            if (i_spellST->type == SPELL_TARGET_TYPE_GAMEOBJECT)
                continue;

            if ((*iter)->GetEntry() == i_spellST->targetEntry)
            {
                if (i_spellST->type == SPELL_TARGET_TYPE_DEAD && ((Creature*)(*iter))->IsCorpse())
                    targetUnitMap.push_back((*iter));
                else if (i_spellST->type == SPELL_TARGET_TYPE_CREATURE && (*iter)->IsAlive())
                    targetUnitMap.push_back((*iter));
                break;
            }
        }
    }
}

SpellCastResult Spell::PreCastCheck(Aura* triggeredByAura /*= nullptr*/)
{
    // Prevent casting at cast another spell (ServerSide check)
    if (m_caster->IsNonMeleeSpellCasted(false, true, true) && m_cast_count && !m_spellInfo->HasAttribute(SPELL_ATTR_EX4_CAN_CAST_WHILE_CASTING))
        return SPELL_FAILED_SPELL_IN_PROGRESS;

    SpellCastResult result = CheckCast(true);
    if (result != SPELL_CAST_OK && !IsAutoRepeat())         // always cast autorepeat dummy for triggering
    {
        if (triggeredByAura)
        {
            SendChannelUpdate(0);
            triggeredByAura->GetHolder()->SetAuraDuration(0);
        }
        return result;
    }

    return SPELL_CAST_OK;
}

SpellCastResult Spell::SpellStart(SpellCastTargets const* targets, Aura* triggeredByAura)
{
    m_spellState = SPELL_STATE_TARGETING;
    m_targets = *targets;

    if (m_CastItem)
        m_CastItemGuid = m_CastItem->GetObjectGuid();

    m_castPositionX = m_caster->GetPositionX();
    m_castPositionY = m_caster->GetPositionY();
    m_castPositionZ = m_caster->GetPositionZ();
    m_castOrientation = m_caster->GetOrientation();

    if (triggeredByAura)
        m_triggeredByAuraSpell = triggeredByAura->GetSpellProto();

    // create and add update event for this spell
    SpellEvent* Event = new SpellEvent(this);
    m_caster->m_Events.AddEvent(Event, m_caster->m_Events.CalculateTime(1));

    // Fill cost data
    m_powerCost = m_IsTriggeredSpell ? 0 : CalculatePowerCost(m_spellInfo, m_caster, this, m_CastItem);

    SpellCastResult result = PreCastCheck();
    if (result != SPELL_CAST_OK)
    {
        SendCastResult(result);
        finish(false);
        return result;
    }
    else
        Prepare();

    return SPELL_CAST_OK;
}

void Spell::Prepare()
{
    m_spellState = SPELL_STATE_CASTING;

    // Prepare data for triggers
    prepareDataForTriggerSystem();

    // calculate cast time (calculated after first CheckCast check to prevent charge counting for first CheckCast fail)
    m_casttime = GetSpellCastTime(m_spellInfo, this);
    m_duration = CalculateSpellDuration(m_spellInfo, m_caster);

    // set timer base at cast time
    ReSetTimer();

    if (!m_IsTriggeredSpell)
        m_caster->RemoveAurasOnCast(m_spellInfo);

    // add non-triggered (with cast time and without)
    if (!m_IsTriggeredSpell)
    {
        // add to cast type slot
        if (!m_spellInfo->HasAttribute(SPELL_ATTR_EX4_CAN_CAST_WHILE_CASTING) && !m_triggerAutorepeat)
            m_caster->SetCurrentCastedSpell(this);

        // will show cast bar
        SendSpellStart();

        // add gcd server side (client side is handled by client itself)
        m_caster->AddGCD(*m_spellInfo);

        // Execute instant spells immediate
        if (m_timer == 0 && !IsNextMeleeSwingSpell() && (!IsAutoRepeat() || m_triggerAutorepeat) && !IsChanneledSpell(m_spellInfo))
            cast();
    }
    // execute triggered without cast time explicitly in call point
    else if (m_timer == 0)
        cast(true);
    // else triggered with cast time will execute execute at next tick or later
    // without adding to cast type slot
    // will not show cast bar but will show effects at casting time etc
}

void Spell::cancel()
{
    if (m_spellState == SPELL_STATE_FINISHED)
        return;

    // channeled spells don't display interrupted message even if they are interrupted, possible other cases with no "Interrupted" message
    bool sendInterrupt = (IsChanneledSpell(m_spellInfo) || m_autoRepeat) ? false : true;

    m_autoRepeat = false;
    switch (m_spellState)
    {
        case SPELL_STATE_CREATED:
        case SPELL_STATE_TARGETING:
        case SPELL_STATE_CASTING:
            m_caster->ResetGCD(m_spellInfo);

        //(no break)
        case SPELL_STATE_TRAVELING:
        {
            SendInterrupted(0);

            if (sendInterrupt)
                SendCastResult(SPELL_FAILED_INTERRUPTED);
        } break;

        case SPELL_STATE_LANDING:
            sLog.outError("Spell [%u] is interrupted while processing", m_spellInfo->Id);
            // no break

        case SPELL_STATE_CHANNELING:
        {
            for (TargetList::iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
            {
                if (ihit->missCondition == SPELL_MISS_NONE)
                {
                    Unit* unit = m_caster->GetObjectGuid() == (*ihit).targetGUID ? m_caster : ObjectAccessor::GetUnit(*m_caster, ihit->targetGUID);
                    if (unit && unit->IsAlive())
                        unit->RemoveAurasByCasterSpell(m_spellInfo->Id, m_caster->GetObjectGuid());

                    // prevent other effects applying if spell is already interrupted
                    // i.e. if effects have different targets and it was interrupted on one of them when
                    // haven't yet applied to another
                    ihit->processed = true;
                }
            }

            SendChannelUpdate(0);
            SendInterrupted(0);

            if (sendInterrupt)
                SendCastResult(SPELL_FAILED_INTERRUPTED);
        } break;

        case SPELL_STATE_FINISHED: break; // should not occur
    }

    finish(false);
    m_caster->RemoveDynObject(m_spellInfo->Id);
    m_caster->RemoveGameObject(m_spellInfo->Id, true);
}

void Spell::cast(bool skipCheck)
{
    SetExecutedCurrently(true);

    if (!m_caster->CheckAndIncreaseCastCounter())
    {
        if (m_triggeredByAuraSpell)
            sLog.outError("Spell %u triggered by aura spell %u too deep in cast chain for cast. Cast not allowed for prevent overflow stack crash.", m_spellInfo->Id, m_triggeredByAuraSpell->Id);
        else
            sLog.outError("Spell %u too deep in cast chain for cast. Cast not allowed for prevent overflow stack crash.", m_spellInfo->Id);

        SendCastResult(SPELL_FAILED_ERROR);
        finish(false);
        SetExecutedCurrently(false);
        return;
    }

    // update pointers base at GUIDs to prevent access to already nonexistent object
    UpdatePointers();

    // cancel at lost main target unit
    if (!m_targets.getUnitTarget() && m_targets.getUnitTargetGuid() && m_targets.getUnitTargetGuid() != m_caster->GetObjectGuid())
    {
        cancel();
        m_caster->DecreaseCastCounter();
        SetExecutedCurrently(false);
        return;
    }

    if (m_caster->GetTypeId() != TYPEID_PLAYER && m_targets.getUnitTarget() && m_targets.getUnitTarget() != m_caster && !m_spellInfo->HasAttribute(SPELL_ATTR_EX5_DONT_TURN_DURING_CAST))
    {
        Unit* charmer = m_caster->GetCharmer();
        if (charmer && !(charmer->GetTypeId() == TYPEID_PLAYER && ((Player*)charmer)->GetCamera().GetBody() == m_caster)) // need to check if target doesnt have a player controlling it
            m_caster->SetInFront(m_targets.getUnitTarget());
    }

    SpellCastResult castResult = CheckPower();
    if (castResult != SPELL_CAST_OK)
    {
        SendCastResult(castResult);
        finish(false);
        m_caster->DecreaseCastCounter();
        SetExecutedCurrently(false);
        return;
    }

    // triggered cast called from Spell::prepare where it was already checked
    if (!skipCheck)
    {
        castResult = CheckCast(false);
        if (castResult != SPELL_CAST_OK)
        {
            SendCastResult(castResult);
            finish(false);
            m_caster->DecreaseCastCounter();
            SetExecutedCurrently(false);
            return;
        }
    }

    // different triggered (for caster) and precast (casted before apply effect to target) cases
    switch(m_spellInfo->SpellFamilyName)
    {
        case SPELLFAMILY_GENERIC:
        {
            // Bandages
            if (m_spellInfo->Mechanic == MECHANIC_BANDAGE)
                AddPrecastSpell(11196);                     // Recently Bandaged
            // Stoneskin
            else if (m_spellInfo->Id == 20594)
                AddTriggeredSpell(65116);                   // Stoneskin - armor 10% for 8 sec
            else if (m_spellInfo->Id == 68992)              // Darkflight
            {
                AddPrecastSpell(96223);                     // Run Speed Marker
                if (m_caster->HasWorgenForm())
                    AddPrecastSpell(97709);                 // Altered Form
            }
            else if (m_spellInfo->Id == 68996)              // Two Forms
            {
                if (m_caster->IsInWorgenForm())
                    m_caster->RemoveSpellsCausingAura(SPELL_AURA_WORGEN_TRANSFORM);
                else if (m_caster->HasWorgenForm())
                    AddPrecastSpell(97709);                 // Altered Form
            }
            // Chaos Bane strength buff
            else if (m_spellInfo->Id == 71904)
                AddTriggeredSpell(73422);
            // Weak Alcohol
            else if (m_spellInfo->SpellIconID == 1306 && m_spellInfo->SpellVisual[0] == 11359)
                AddTriggeredSpell(51655);                   // BOTM - Create Empty Brew Bottle
            break;
        }
        case SPELLFAMILY_MAGE:
        {
            // Ice Block
            if (m_spellInfo->SpellFamilyFlags & uint64(0x0000008000000000))
                AddPrecastSpell(41425);                     // Hypothermia
            // Icy Veins
            else if (m_spellInfo->Id == 12472)
            {
                if (m_caster->HasAura(56374))               // Glyph of Icy Veins
                {
                    // not exist spell do it so apply directly
                    m_caster->RemoveSpellsCausingAura(SPELL_AURA_MOD_DECREASE_SPEED);
                    m_caster->RemoveSpellsCausingAura(SPELL_AURA_HASTE_SPELLS);
                }
            }
            // Fingers of Frost
            else if (m_spellInfo->Id == 44544)
                AddPrecastSpell(74396);                     // Fingers of Frost
            break;
        }
        case SPELLFAMILY_WARRIOR:
        {
            // Shield Slam
            if ((m_spellInfo->SpellFamilyFlags & uint64(0x0000020000000000)) && m_spellInfo->Category==1209)
            {
                if (m_caster->HasAura(58375))               // Glyph of Blocking
                    AddTriggeredSpell(58374);               // Glyph of Blocking
            }
            // Bloodrage
            if (m_spellInfo->SpellFamilyFlags & uint64(0x0000000000000100))
            {
                if (m_caster->HasAura(70844))               // Item - Warrior T10 Protection 4P Bonus
                    AddTriggeredSpell(70845);               // Stoicism
            }
            // Bloodsurge (triggered), Sudden Death (triggered)
            else if (m_spellInfo->Id == 46916 || m_spellInfo->Id == 52437)
                // Item - Warrior T10 Melee 4P Bonus
                if (Aura* aur = m_caster->GetAura(70847, EFFECT_INDEX_0))
                    if (roll_chance_i(aur->GetModifier()->m_amount))
                        AddTriggeredSpell(70849);           // Extra Charge!
            break;
        }
        case SPELLFAMILY_PRIEST:
        {
            // Power Word: Shield
            if (m_spellInfo->Mechanic == MECHANIC_SHIELD &&
                (m_spellInfo->SpellFamilyFlags & uint64(0x0000000000000001)))
                AddPrecastSpell(6788);                      // Weakened Soul
            // Prayer of Mending (jump animation), we need formal caster instead original for correct animation
            else if (m_spellInfo->SpellFamilyFlags & uint64(0x0000002000000000))
                AddTriggeredSpell(41637);

            switch (m_spellInfo->Id)
            {
                case 15237: AddTriggeredSpell(23455); break;// Holy Nova, rank 1
                case 15430: AddTriggeredSpell(23458); break;// Holy Nova, rank 2
                case 15431: AddTriggeredSpell(23459); break;// Holy Nova, rank 3
                case 27799: AddTriggeredSpell(27803); break;// Holy Nova, rank 4
                case 27800: AddTriggeredSpell(27804); break;// Holy Nova, rank 5
                case 27801: AddTriggeredSpell(27805); break;// Holy Nova, rank 6
                case 25331: AddTriggeredSpell(25329); break;// Holy Nova, rank 7
                case 48077: AddTriggeredSpell(48075); break;// Holy Nova, rank 8
                case 48078: AddTriggeredSpell(48076); break;// Holy Nova, rank 9
                default: break;
            }
            break;
        }
        case SPELLFAMILY_DRUID:
        {
            // Faerie Fire (Feral)
            if (m_spellInfo->Id == 16857 && m_caster->GetShapeshiftForm() != FORM_CAT)
                AddTriggeredSpell(60089);
            // Clearcasting
            else if (m_spellInfo->Id == 16870)
            {
                if (m_caster->HasAura(70718))               // Item - Druid T10 Balance 2P Bonus
                    AddPrecastSpell(70721);                 // Omen of Doom
            }
            // Berserk (Bear Mangle part)
            else if (m_spellInfo->Id == 50334)
                AddTriggeredSpell(58923);
            break;
        }
        case SPELLFAMILY_ROGUE:
            // Fan of Knives (main hand)
            if (m_spellInfo->Id == 51723 && m_caster->GetTypeId() == TYPEID_PLAYER &&
                    ((Player*)m_caster)->haveOffhandWeapon())
            {
                AddTriggeredSpell(52874);                   // Fan of Knives (offhand)
            }
            break;
        case SPELLFAMILY_HUNTER:
        {
            // Deterrence
            if (m_spellInfo->Id == 19263)
                AddPrecastSpell(67801);
            // Kill Command
            else if (m_spellInfo->Id == 34026)
            {
                if (m_caster->HasAura(37483))               // Improved Kill Command - Item set bonus
                    m_caster->CastSpell(m_caster, 37482, TRIGGERED_OLD_TRIGGERED);// Exploited Weakness
            }
            // Lock and Load
            else if (m_spellInfo->Id == 56453)
                AddPrecastSpell(67544);                     // Lock and Load Marker
            break;
        }
        case SPELLFAMILY_PALADIN:
        {
            // Divine Illumination
            if (m_spellInfo->Id == 31842)
            {
                if (m_caster->HasAura(70755))               // Item - Paladin T10 Holy 2P Bonus
                    AddPrecastSpell(71166);                 // Divine Illumination
            }
            // Hand of Reckoning
            else if (m_spellInfo->Id == 62124)
            {
                if (!m_targets.getUnitTarget() || m_targets.getUnitTarget()->GetTargetGuid() != m_caster->GetObjectGuid())
                    AddPrecastSpell(67485);                 // Hand of Rekoning (no typos in name ;) )
            }
            // Divine Shield, Divine Protection or Hand of Protection
            else if (m_spellInfo->SpellFamilyFlags & uint64(0x0000000000400080))
            {
                AddPrecastSpell(25771);                     // Forbearance
                AddPrecastSpell(61987);                     // Avenging Wrath Marker
            }
            // Lay on Hands
            else if (m_spellInfo->SpellFamilyFlags & uint64(0x0000000000008000))
            {
                // only for self cast
                if (m_caster == m_targets.getUnitTarget())
                    AddPrecastSpell(25771);                 // Forbearance
            }
            // Avenging Wrath
            else if (m_spellInfo->SpellFamilyFlags & uint64(0x0000200000000000))
                AddPrecastSpell(61987);                     // Avenging Wrath Marker
            break;
        }
        case SPELLFAMILY_SHAMAN:
        {
            // Bloodlust
            if (m_spellInfo->Id == 2825)
                AddPrecastSpell(57724);                     // Sated
            // Heroism
            else if (m_spellInfo->Id == 32182)
                AddPrecastSpell(57723);                     // Exhaustion
            // Spirit Walk
            else if (m_spellInfo->Id == 58875)
                AddPrecastSpell(58876);
            // Totem of Wrath
            else if (m_spellInfo->Effect[EFFECT_INDEX_0] == SPELL_EFFECT_APPLY_AREA_AURA_RAID && m_spellInfo->SpellFamilyFlags & uint64(0x0000000004000000))
                // only for main totem spell cast
                AddTriggeredSpell(30708);                   // Totem of Wrath
            break;
        }
        case SPELLFAMILY_DEATHKNIGHT:
        {
            // Chains of Ice
            if (m_spellInfo->Id == 45524)
                AddTriggeredSpell(55095);                   // Frost Fever
            break;
        }
        default:
            break;
    }

    // traded items have trade slot instead of guid in m_itemTargetGUID
    // set to real guid to be sent later to the client
    m_targets.updateTradeSlotItem();

#ifdef BUILD_ELUNA
    if (m_caster)
    {
        if (m_caster->GetTypeId() == TYPEID_PLAYER)
            if (Eluna* e = m_caster->GetEluna())
                e->OnSpellCast(m_caster->ToPlayer(), this, skipCheck);
    }
#endif

    if (m_caster->GetTypeId() == TYPEID_PLAYER)
    {
        if (!m_IsTriggeredSpell && m_CastItem)
            ((Player*)m_caster)->GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_USE_ITEM, m_CastItem->GetEntry());

        ((Player*)m_caster)->GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_CAST_SPELL, m_spellInfo->Id);
    }

    FillTargetMap();

    if (m_spellState == SPELL_STATE_FINISHED)               // stop cast if spell marked as finish somewhere in FillTargetMap
    {
        m_caster->DecreaseCastCounter();
        SetExecutedCurrently(false);
        return;
    }

    // CAST SPELL
    SendSpellCooldown();

    TakePower();
    TakeReagents();                                         // we must remove reagents before HandleEffects to allow place crafted item in same slot
    TakeAmmo();

    SendCastResult(castResult);
    SendSpellGo();                                          // we must send smsg_spell_go packet before m_castItem delete in TakeCastItem()...

    InitializeDamageMultipliers();

    Unit* procTarget = m_targets.getUnitTarget();
    if (!procTarget)
        procTarget = m_caster;

    // Okay, everything is prepared. Now we need to distinguish between immediate and evented delayed spells
    float speed = m_spellInfo->speed == 0.0f && m_triggeredBySpellInfo ? m_triggeredBySpellInfo->speed : m_spellInfo->speed;
    if (speed > 0.0f)
    {
        // Remove used for cast item if need (it can be already nullptr after TakeReagents call
        // in case delayed spell remove item at cast delay start
        TakeCastItem();

        // fill initial spell damage from caster for delayed casted spells
        for (TargetList::iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
            HandleDelayedSpellLaunch(&(*ihit));

        // Okay, maps created, now prepare flags
        m_immediateHandled = false;
        m_spellState = SPELL_STATE_TRAVELING;
        SetDelayStart(0);

        // on spell cast end proc,
        // critical hit related part is currently done on hit so proc there,
        // 0 damage since any damage based procs should be on hit
        // 0 victim proc since there is no victim proc dependent on successfull cast for caster
        m_caster->ProcDamageAndSpell(procTarget, m_procAttacker, 0, PROC_EX_CAST_END, 0, m_attackType, m_spellInfo);
    }
    else
    {
        m_caster->ProcDamageAndSpell(procTarget, m_procAttacker, 0, PROC_EX_CAST_END, 0, m_attackType, m_spellInfo);

        // Immediate spell, no big deal
        handle_immediate();
    }

    m_caster->DecreaseCastCounter();
    SetExecutedCurrently(false);
}

void Spell::handle_immediate()
{
    m_spellState = SPELL_STATE_LANDING;

    // process immediate effects (items, ground, etc.) also initialize some variables
    _handle_immediate_phase();

    // start channeling if applicable (after _handle_immediate_phase for get persistent effect dynamic object for channel target
    if (IsChanneledSpell(m_spellInfo) && m_duration)
    {
        m_spellState = SPELL_STATE_CHANNELING;
        SendChannelStart(m_duration);

        // Proc spell aura triggers on start of channeled spell
        ProcSpellAuraTriggers();
    }

    for (TargetList::iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
        DoAllEffectOnTarget(&(*ihit));

    for (GOTargetList::iterator ihit = m_UniqueGOTargetInfo.begin(); ihit != m_UniqueGOTargetInfo.end(); ++ihit)
        DoAllEffectOnTarget(&(*ihit));

    // spell is finished, perform some last features of the spell here
    _handle_finish_phase();

    // Remove used for cast item if need (it can be already nullptr after TakeReagents call
    TakeCastItem();

    if (m_spellState != SPELL_STATE_CHANNELING)
        finish(true);                                       // successfully finish spell cast (not last in case autorepeat or channel spell)
}

uint64 Spell::handle_delayed(uint64 t_offset)
{
    uint64 next_time = 0;

    if (!m_immediateHandled)
    {
        _handle_immediate_phase();
        m_immediateHandled = true;
    }

    // now recheck units targeting correctness (need before any effects apply to prevent adding immunity at first effect not allow apply second spell effect and similar cases)
    for (TargetList::iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
    {
        if (!ihit->processed)
        {
            if (ihit->timeDelay <= t_offset)
                DoAllEffectOnTarget(&(*ihit));
            else if (next_time == 0 || ihit->timeDelay < next_time)
                next_time = ihit->timeDelay;
        }
    }

    // now recheck gameobject targeting correctness
    for (GOTargetList::iterator ighit = m_UniqueGOTargetInfo.begin(); ighit != m_UniqueGOTargetInfo.end(); ++ighit)
    {
        if (!ighit->processed)
        {
            if (ighit->timeDelay <= t_offset)
                DoAllEffectOnTarget(&(*ighit));
            else if (next_time == 0 || ighit->timeDelay < next_time)
                next_time = ighit->timeDelay;
        }
    }
    // All targets passed - need finish phase
    if (next_time == 0)
    {
        // spell is finished, perform some last features of the spell here
        _handle_finish_phase();

        finish(true);                                       // successfully finish spell cast

        // return zero, spell is finished now
        return 0;
    }
    else
    {
        // spell is unfinished, return next execution time
        return next_time;
    }
}

void Spell::_handle_immediate_phase()
{
    // handle some immediate features of the spell here
    HandleThreatSpells();

    for (int j = 0; j < MAX_EFFECT_INDEX; ++j)
    {
        // apply Send Event effect to ground in case empty target lists
        if(m_spellInfo->Effect[j] == SPELL_EFFECT_SEND_EVENT && !HaveTargetsForEffect(SpellEffectIndex(j)))
        {
            HandleEffects(nullptr, nullptr, nullptr, SpellEffectIndex(j));
            continue;
        }
    }

    // initialize Diminishing Returns Data
    m_diminishLevel = DIMINISHING_LEVEL_1;
    m_diminishGroup = DIMINISHING_NONE;

    // process items
    for (ItemTargetList::iterator ihit = m_UniqueItemInfo.begin(); ihit != m_UniqueItemInfo.end(); ++ihit)
        DoAllEffectOnTarget(&(*ihit));

    // process ground
    for (int j = 0; j < MAX_EFFECT_INDEX; ++j)
    {
        // persistent area auras target only the ground
        if (m_spellInfo->Effect[j] == SPELL_EFFECT_PERSISTENT_AREA_AURA ||
                //summon a gameobject at the spell's destination xyz
                (m_spellInfo->Effect[j] == SPELL_EFFECT_TRANS_DOOR && m_spellInfo->EffectImplicitTargetA[j] == TARGET_ENUM_GAMEOBJECTS_SCRIPT_AOE_AT_DEST_LOC))
            HandleEffects(nullptr, nullptr, nullptr, SpellEffectIndex(j));
    }
}

void Spell::_handle_finish_phase()
{
    // spell log
    if (m_needSpellLog)
        m_spellLog.SendToSet();

    if (m_caster->m_extraAttacks && IsSpellHaveEffect(m_spellInfo, SPELL_EFFECT_ADD_EXTRA_ATTACKS))
    {
        if (Unit* victim = m_caster->GetVictim())
            m_caster->DoExtraAttacks(victim);
        else
            m_caster->m_extraAttacks = 0;
    }
}

void Spell::SendSpellCooldown()
{
    if (m_caster->GetTypeId() == TYPEID_PLAYER)
    {
        Player* casterPlayer = static_cast<Player*>(m_caster);
        // mana/health/etc potions, disabled by client (until combat out as declarate)
        if (m_CastItem && m_CastItem->IsPotion())
        {
            // need in some way provided data for Spell::finish SendCooldownEvent
            casterPlayer->SetLastPotionId(m_CastItem->GetEntry());

            m_caster->AddCooldown(*m_spellInfo, m_CastItem->GetProto(), true);   // add server side cooldown
            return;
        }

        // (1) have infinity cooldown but set at aura apply, (2) passive cooldown at triggering
        if (m_spellInfo->HasAttribute(SPELL_ATTR_DISABLED_WHILE_ACTIVE) || m_spellInfo->HasAttribute(SPELL_ATTR_PASSIVE))
            return;
    }

    m_caster->AddCooldown(*m_spellInfo, m_CastItem ? m_CastItem->GetProto() : nullptr);
}

void Spell::update(uint32 difftime)
{
    // update pointers based at it's GUIDs
    UpdatePointers();

    uint32 time = m_timer;

    if (m_targets.getUnitTargetGuid() && !m_targets.getUnitTarget())
    {
        cancel();
        return;
    }

    if (m_CastItemGuid && !m_CastItem)
    {
        cancel();
        return;
    }

    // check if the player or unit caster has moved before the spell finished (exclude casting on vehicles)
    if (((m_caster->GetTypeId() == TYPEID_PLAYER || m_caster->GetTypeId() == TYPEID_UNIT) && m_timer != 0) &&
        (m_castPositionX != m_caster->GetPositionX() || m_castPositionY != m_caster->GetPositionY() || m_castPositionZ != m_caster->GetPositionZ()) &&
        ((m_spellInfo->Effect[EFFECT_INDEX_0] != SPELL_EFFECT_STUCK) || !((Player*)m_caster)->m_movementInfo.HasMovementFlag(MOVEFLAG_FALLINGFAR)) &&
        !m_caster->HasAffectedAura(SPELL_AURA_ALLOW_CAST_WHILE_MOVING, m_spellInfo))
    {
        // always cancel for channeled spells
        if (m_spellState == SPELL_STATE_CHANNELING && !m_spellInfo->HasAttribute(SPELL_ATTR_EX5_CAN_CHANNEL_WHEN_MOVING))
            cancel();
        // don't cancel for melee, autorepeat, triggered and instant spells
        else if(!IsNextMeleeSwingSpell() && !IsAutoRepeat() && !m_IsTriggeredSpell && (m_spellInfo->InterruptFlags & SPELL_INTERRUPT_FLAG_MOVEMENT))
            cancel();
    }

    switch (m_spellState)
    {
        case SPELL_STATE_CASTING:
        {
            if (m_timer)
            {
                if (difftime >= m_timer)
                    m_timer = 0;
                else
                    m_timer -= difftime;
            }

            if (m_timer == 0 && !IsNextMeleeSwingSpell() && !IsAutoRepeat())
                cast();
        } break;
        case SPELL_STATE_CHANNELING:
        {
            if (m_timer > 0)
            {
                if (m_caster->GetTypeId() == TYPEID_PLAYER || m_caster->GetTypeId() == TYPEID_UNIT)
                {
                    // check if player has jumped before the channeling finished
                    if (m_caster->m_movementInfo.HasMovementFlag(MOVEFLAG_FALLING) &&
                        !m_caster->HasAffectedAura(SPELL_AURA_ALLOW_CAST_WHILE_MOVING, m_spellInfo))
                        cancel();

                    // check for incapacitating player states
                    if (m_caster->hasUnitState(UNIT_STAT_CAN_NOT_REACT))
                    {
                        // certain channel spells are not interrupted
                        if (!m_spellInfo->HasAttribute(SPELL_ATTR_EX_CHANNELED_1) && !m_spellInfo->HasAttribute(SPELL_ATTR_EX3_UNK28))
                            cancel();
                    }

                    // check if player has turned if flag is set
                    if((m_spellInfo->ChannelInterruptFlags & CHANNEL_FLAG_TURNING) && m_castOrientation != m_caster->GetOrientation() )
                        cancel();
                }

                // check if there are alive targets left
                if (!IsAliveUnitPresentInTargetList())
                {
                    SendChannelUpdate(0);
                    finish();
                }

                if (difftime >= m_timer)
                    m_timer = 0;
                else
                    m_timer -= difftime;
            }

            if (m_timer == 0)
            {
                SendChannelUpdate(0, true);

                // channeled spell processed independently for quest targeting
                // cast at creature (or GO) quest objectives update at successful cast channel finished
                // ignore autorepeat/melee casts for speed (not exist quest for spells (hm... )
                if (!IsAutoRepeat() && !IsNextMeleeSwingSpell())
                {
                    if (Player* p = m_caster->GetBeneficiaryPlayer())
                    {
                        for (TargetList::const_iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
                        {
                            TargetInfo const& target = *ihit;
                            if (!target.targetGUID.IsCreatureOrVehicle())
                                continue;

                            Unit* unit = m_caster->GetObjectGuid() == target.targetGUID ? m_caster : ObjectAccessor::GetUnit(*m_caster, target.targetGUID);
                            if (unit == nullptr)
                                continue;

                            p->RewardPlayerAndGroupAtCast(unit, m_spellInfo->Id);
                        }

                        for (GOTargetList::const_iterator ihit = m_UniqueGOTargetInfo.begin(); ihit != m_UniqueGOTargetInfo.end(); ++ihit)
                        {
                            GOTargetInfo const& target = *ihit;

                            GameObject* go = m_caster->GetMap()->GetGameObject(target.targetGUID);
                            if (!go)
                                continue;

                            p->RewardPlayerAndGroupAtCast(go, m_spellInfo->Id);
                        }
                    }
                }

                finish();
            }
        } break;
        default:
        {
        } break;
    }
}

void Spell::finish(bool ok)
{
    if (!m_caster)
        return;

    if (m_spellState == SPELL_STATE_FINISHED)
        return;

    m_spellState = SPELL_STATE_FINISHED;

    // other code related only to successfully finished spells
    if (!ok)
        return;

    // Normal spells proc on finish, channeled spells proc on start
    if(!IsChanneledSpell(m_spellInfo))
        ProcSpellAuraTriggers();

    // Heal caster for all health leech from all targets
    if (m_healthLeech)
    {
        uint32 absorb = 0;
        m_caster->CalculateHealAbsorb(uint32(m_healthLeech), &absorb);
        m_caster->DealHeal(m_caster, uint32(m_healthLeech) - absorb, m_spellInfo, false, absorb);
    }

    if (IsMeleeAttackResetSpell())
    {
        if (!m_spellInfo->HasAttribute(SPELL_ATTR_EX2_NOT_RESET_AUTO_ACTIONS))
        {
            m_caster->resetAttackTimer(BASE_ATTACK);
            if (m_caster->haveOffhandWeapon())
                m_caster->resetAttackTimer(OFF_ATTACK);
        }
    }

    if (m_spellInfo->AttributesEx & SPELL_ATTR_EX_REFUND_POWER)
    {
        for (TargetList::const_iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
        {
            switch (ihit->missCondition)
            {
                case SPELL_MISS_MISS:
                case SPELL_MISS_DODGE:
                    if (m_spellInfo->powerType == POWER_RAGE) // For Warriors only refund on parry/deflect, for rogues on all 4
                        break;
                case SPELL_MISS_PARRY:
                case SPELL_MISS_DEFLECT:
                    m_caster->ModifyPower(Powers(m_spellInfo->powerType), int32(float(m_powerCost) * 0.8f));
                    break;
                default:
                    break;
            }
        }
    }

    /*if (IsRangedAttackResetSpell())
        m_caster->resetAttackTimer(RANGED_ATTACK);*/

    // Clear combo at finish state
    if (m_caster->GetTypeId() == TYPEID_PLAYER && NeedsComboPoints(m_spellInfo))
    {
        // Not drop combopoints if negative spell and if any miss on enemy exist
        bool needDrop = true;
        if (!IsPositiveSpell(m_spellInfo->Id))
        {
            for (TargetList::const_iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
            {
                if (ihit->missCondition != SPELL_MISS_NONE && ihit->targetGUID != m_caster->GetObjectGuid())
                {
                    needDrop = false;
                    break;
                }
            }
        }
        if (needDrop)
            ((Player*)m_caster)->ClearComboPoints();
    }

    // potions disabled by client, send event "not in combat" if need
    if (m_caster->GetTypeId() == TYPEID_PLAYER)
        ((Player*)m_caster)->UpdatePotionCooldown(this);

    // call triggered spell only at successful cast (after clear combo points -> for add some if need)
    if (!m_TriggerSpells.empty())
        CastTriggerSpells();

    // Stop Attack for some spells
    if (m_spellInfo->HasAttribute(SPELL_ATTR_STOP_ATTACK_TARGET))
        m_caster->AttackStop();

    // update encounter state if needed
    Map* map = m_caster->GetMap();
    if (map->IsDungeon())
        ((DungeonMap*)map)->GetPersistanceState()->UpdateEncounterState(ENCOUNTER_CREDIT_CAST_SPELL, m_spellInfo->Id);
}

void Spell::SendCastResult(SpellCastResult result)
{
    if (result == SPELL_CAST_OK)
        return;

    if (m_caster->GetTypeId() != TYPEID_PLAYER)
        return;

    if (((Player*)m_caster)->GetSession()->PlayerLoading()) // don't send cast results at loading time
        return;

    SendCastResult((Player*)m_caster, m_spellInfo, m_cast_count, result);
}

void Spell::SendCastResult(Player* caster, SpellEntry const* spellInfo, uint8 cast_count, SpellCastResult result, bool isPetCastResult /*=false*/)
{
    if (result == SPELL_CAST_OK)
        return;

    WorldPacket data(isPetCastResult ? SMSG_PET_CAST_FAILED : SMSG_CAST_RESULT, (4 + 1 + 2));
    data << uint8(cast_count);                              // single cast or multi 2.3 (0/1)
    data << uint32(spellInfo->Id);
    data << uint8(!IsPassiveSpell(spellInfo) ? result : SPELL_FAILED_DONT_REPORT); // do not report failed passive spells
    switch (result)
    {
        case SPELL_FAILED_NOT_READY:
            data << uint32(0);                              // unknown, value 1 seen for 14177 (update cooldowns on client flag)
            break;
        case SPELL_FAILED_REQUIRES_SPELL_FOCUS:
            data << uint32(spellInfo->RequiresSpellFocus);
            break;
        case SPELL_FAILED_REQUIRES_AREA:                    // AreaTable.dbc id
            // hardcode areas limitation case
            switch (spellInfo->Id)
            {
                case 41617:                                 // Cenarion Mana Salve
                case 41619:                                 // Cenarion Healing Salve
                    data << uint32(3905);
                    break;
                case 41618:                                 // Bottled Nethergon Energy
                case 41620:                                 // Bottled Nethergon Vapor
                    data << uint32(3842);
                    break;
                case 45373:                                 // Bloodberry Elixir
                    data << uint32(4075);
                    break;
                default:                                    // default case (don't must be)
                    data << uint32(0);
                    break;
            }
            break;
        case SPELL_FAILED_TOTEMS:
            {
                for(int i = 0; i < MAX_SPELL_TOTEMS; ++i)
                    if(spellInfo->Totem[i])
                        data << uint32(spellInfo->Totem[i]);
            }
            break;
        case SPELL_FAILED_TOTEM_CATEGORY:
            {
                for(int i = 0; i < MAX_SPELL_TOTEM_CATEGORIES; ++i)
                    if(spellInfo->TotemCategory[i])
                        data << uint32(spellInfo->TotemCategory[i]);
            }
            break;
        case SPELL_FAILED_EQUIPPED_ITEM_CLASS:
            {
                data << uint32(spellInfo->EquippedItemClass);
                data << uint32(spellInfo->EquippedItemSubClassMask);
                //data << uint32(spellInfo->EquippedItemInventoryTypeMask);
            }
            break;
        case SPELL_FAILED_EQUIPPED_ITEM_CLASS_MAINHAND:
        case SPELL_FAILED_EQUIPPED_ITEM_CLASS_OFFHAND:
        {
            data << uint32(spellInfo->EquippedItemClass);
            data << uint32(spellInfo->EquippedItemSubClassMask);
            break;
        }
        case SPELL_FAILED_PREVENTED_BY_MECHANIC:
            data << uint32(0);                              // SpellMechanic.dbc id
            break;
        case SPELL_FAILED_CUSTOM_ERROR:
            data << uint32(0);                              // custom error id (see enum SpellCastResultCustom)
            break;
        case SPELL_FAILED_NEED_EXOTIC_AMMO:
        {
            data << uint32(spellInfo->EquippedItemSubClassMask);// seems correct...
            break;
        }
        case SPELL_FAILED_REAGENTS:
            data << uint32(0);                              // item id
            break;
        case SPELL_FAILED_NEED_MORE_ITEMS:
            data << uint32(0);                              // item id
            data << uint32(0);                              // item count?
            break;
        case SPELL_FAILED_MIN_SKILL:
            data << uint32(0);                              // SkillLine.dbc id
            data << uint32(0);                              // required skill value
            break;
        case SPELL_FAILED_TOO_MANY_OF_ITEM:
            data << uint32(0);                              // ItemLimitCategory.dbc id
            break;
        case SPELL_FAILED_FISHING_TOO_LOW:
            data << uint32(0);                              // required fishing skill
            break;
        default:
            break;
    }
    caster->GetSession()->SendPacket(data);
}

void Spell::SendSpellStart()
{
    if (!IsNeedSendToClient())
        return;

    DEBUG_FILTER_LOG(LOG_FILTER_SPELL_CAST, "Sending SMSG_SPELL_START id=%u", m_spellInfo->Id);

    uint32 castFlags = CAST_FLAG_HAS_TRAJECTORY;

    if (HasPersistentAuraEffect(m_spellInfo))
        castFlags |= CAST_FLAG_PERSISTENT_AA;

    if (m_spellInfo->runeCostID)
        castFlags |= CAST_FLAG_UNKNOWN19;

    if ((m_caster->GetTypeId() == TYPEID_PLAYER ||
        m_caster->GetTypeId() == TYPEID_UNIT && ((Creature*)m_caster)->IsPet()) &&
        m_spellInfo->powerType != POWER_HEALTH)
        castFlags |= CAST_FLAG_PREDICTED_POWER;

    if (m_casttime && (IsSpellHaveEffect(m_spellInfo, SPELL_EFFECT_HEAL) || IsSpellHaveEffect(m_spellInfo, SPELL_EFFECT_HEAL_PCT)) ||
        IsSpellHaveAura(m_spellInfo, SPELL_AURA_PERIODIC_HEAL))
    {
        castFlags |= CAST_FLAG_HEAL_PREDICTION;
    }

    WorldPacket data(SMSG_SPELL_START, (8 + 8 + 4 + 4 + 2));
    if (m_CastItem)
        data << m_CastItem->GetPackGUID();
    else
        data << m_caster->GetPackGUID();

    data << m_caster->GetPackGUID();
    data << uint8(m_cast_count);                            // pending spell cast
    data << uint32(m_spellInfo->Id);                        // spellId
    data << uint32(castFlags);                              // cast flags
    data << uint32(m_timer);                                // delay?
    data << uint32(m_casttime);                             // m_casttime

    data << m_targets;

    if (castFlags & CAST_FLAG_PREDICTED_POWER)              // predicted power
        data << uint32(m_caster->GetPower(Powers(m_spellInfo->powerType)));

    if (castFlags & CAST_FLAG_PREDICTED_RUNES)              // predicted runes
    {
        if (m_caster->GetTypeId() == TYPEID_PLAYER)
        {
            Player* caster = (Player*)m_caster;

            data << uint8(m_runesState);
            data << uint8(caster->GetRunesState());
            for (uint8 i = 0; i < MAX_RUNES; ++i)
                data << uint8(caster->GetRuneCooldownFraction(i));
        }
        else
        {
            data << uint8(0);
            data << uint8(0);
            for (uint8 i = 0; i < MAX_RUNES; ++i)
                data << uint8(0);
        }
    }

    if (castFlags & CAST_FLAG_AMMO)                         // projectile info
        WriteAmmoToPacket(data);

    if (castFlags & CAST_FLAG_IMMUNITY)                     // cast immunity
    {
        data << uint32(0);                                  // used for SetCastSchoolImmunities
        data << uint32(0);                                  // used for SetCastImmunities
    }

    if (castFlags & CAST_FLAG_HEAL_PREDICTION)
    {
        uint8 type = DOT;
        int32 amt = 0;
        for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
        {
            if (m_spellInfo->Effect[i] == SPELL_EFFECT_HEAL || m_spellInfo->Effect[i] == SPELL_EFFECT_HEAL_PCT)
            {
                type = 0;
                Unit* target = m_targets.getUnitTarget() ? m_targets.getUnitTarget() : m_caster;
                amt = CalculateDamage(SpellEffectIndex(i), target);
                amt = m_caster->SpellHealingBonusDone(target, m_spellInfo, amt, HEAL);
                break;
            }
        }

        data << uint32(amt);
        data << uint8(type);
        if (type == DOT)
            data << m_caster->GetPackGUID();
    }

    m_caster->SendMessageToSet(data, true);
}

void Spell::SendSpellGo()
{
    // not send invisible spell casting
    if (!IsNeedSendToClient())
        return;

    DEBUG_FILTER_LOG(LOG_FILTER_SPELL_CAST, "Sending SMSG_SPELL_GO id=%u", m_spellInfo->Id);

    uint32 castFlags = CAST_FLAG_UNKNOWN9;

    if (m_CastItem)
        castFlags |= CAST_FLAG_UNKNOWN7;

    if ((m_caster->GetTypeId() == TYPEID_PLAYER ||
        m_caster->GetTypeId() == TYPEID_UNIT && ((Creature*)m_caster)->IsPet()) &&
        m_spellInfo->powerType != POWER_HEALTH)
        castFlags |= CAST_FLAG_PREDICTED_POWER;

    if (m_caster->GetTypeId() == TYPEID_PLAYER && m_caster->getClass() == CLASS_DEATH_KNIGHT && m_spellInfo->runeCostID)
    {
        castFlags |= CAST_FLAG_UNKNOWN19;                   // same as in SMSG_SPELL_START
        castFlags |= CAST_FLAG_PREDICTED_RUNES;             // rune cooldowns list
    }

    if (m_powerCost)
        castFlags |= CAST_FLAG_PREDICTED_POWER;             // all powerCost spells have this

    WorldPacket data(SMSG_SPELL_GO, 50);                    // guess size

    if (m_CastItem)
        data << m_CastItem->GetPackGUID();
    else
        data << m_caster->GetPackGUID();

    data << m_caster->GetPackGUID();
    data << uint8(m_cast_count);                            // pending spell cast?
    data << uint32(m_spellInfo->Id);                        // spellId
    data << uint32(castFlags);                              // cast flags
    data << uint32(m_timer);
    data << uint32(WorldTimer::getMSTime());                // timestamp

    WriteSpellGoTargets(data);

    data << m_targets;

    if (castFlags & CAST_FLAG_PREDICTED_POWER)              // predicted power
        data << uint32(m_caster->GetPower(Powers(m_spellInfo->powerType)));

    if (castFlags & CAST_FLAG_PREDICTED_RUNES)              // predicted runes
    {
        if (m_caster->GetTypeId() == TYPEID_PLAYER)
        {
            Player* caster = (Player*)m_caster;

            data << uint8(m_runesState);
            data << uint8(caster->GetRunesState());
            for (uint8 i = 0; i < MAX_RUNES; ++i)
                data << uint8(255 - ((caster->GetRuneCooldown(i) / REGEN_TIME_FULL) * 51));
        }
        else
        {
            data << uint8(0);
            data << uint8(0);
            for (uint8 i = 0; i < MAX_RUNES; ++i)
                data << uint8(0);
        }
    }

    if (castFlags & CAST_FLAG_ADJUST_MISSILE)               // adjust missile trajectory duration
    {
        data << float(m_targets.GetElevation());
        data << uint32(m_delayMoment);
    }

    if (castFlags & CAST_FLAG_AMMO)                         // projectile info
        WriteAmmoToPacket(data);

    if (castFlags & CAST_FLAG_VISUAL_CHAIN)                 // spell visual chain effect
    {
        data << uint32(0);                                  // SpellVisual.dbc id?
        data << uint32(0);                                  // overrides previous field if > 0 and violencelevel client cvar < 2
    }

    if (m_targets.m_targetMask & TARGET_FLAG_DEST_LOCATION)
    {
        data << uint8(0);                                   // The value increase for each time, can remind of a cast count for the spell
    }

    if (m_targets.m_targetMask & TARGET_FLAG_VISUAL_CHAIN)  // probably used (or can be used) with CAST_FLAG_VISUAL_CHAIN flag
    {
        data << uint32(0);                                  // count

        // for(int = 0; i < count; ++i)
        //{
        //    // position and guid?
        //    data << float(0) << float(0) << float(0) << uint64(0);
        //}
    }

    m_caster->SendMessageToSet(data, true);
}

void Spell::WriteAmmoToPacket(WorldPacket& data)
{
    uint32 ammoInventoryType = 0;
    uint32 ammoDisplayID = 0;

    if (m_caster->GetTypeId() == TYPEID_PLAYER)
    {
        Item* pItem = ((Player*)m_caster)->GetWeaponForAttack(RANGED_ATTACK);
        if (pItem)
        {
            ammoInventoryType = pItem->GetProto()->InventoryType;
            if (ammoInventoryType == INVTYPE_THROWN)
                ammoDisplayID = pItem->GetProto()->DisplayInfoID;
            else
            {
                if(m_caster->GetDummyAura(46699))      // Requires No Ammo
                {
                    ammoDisplayID = 5996;                   // normal arrow
                    ammoInventoryType = INVTYPE_AMMO;
                }
            }
        }
    }
    else
    {
        for (uint8 i = 0; i < MAX_VIRTUAL_ITEM_SLOT; ++i)
        {
            if (uint32 item_id = m_caster->GetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + i))
            {
                if(ItemPrototype const* itemEntry = sItemStorage.LookupEntry<ItemPrototype>(item_id))
                //if(ItemEntry const * itemEntry = sItemStore.LookupEntry(item_id))
                {
                    if (itemEntry->Class == ITEM_CLASS_WEAPON)
                    {
                        switch (itemEntry->SubClass)
                        {
                            case ITEM_SUBCLASS_WEAPON_THROWN:
                                ammoDisplayID = itemEntry->DisplayInfoID;
                                ammoInventoryType = itemEntry->InventoryType;
                                break;
                            case ITEM_SUBCLASS_WEAPON_BOW:
                            case ITEM_SUBCLASS_WEAPON_CROSSBOW:
                                ammoDisplayID = 5996;       // is this need fixing?
                                ammoInventoryType = INVTYPE_AMMO;
                                break;
                            case ITEM_SUBCLASS_WEAPON_GUN:
                                ammoDisplayID = 5998;       // is this need fixing?
                                ammoInventoryType = INVTYPE_AMMO;
                                break;
                        }

                        if (ammoDisplayID)
                            break;
                    }
                }
            }
        }
    }

    data << uint32(ammoDisplayID);
    data << uint32(ammoInventoryType);
}

void Spell::WriteSpellGoTargets(WorldPacket& data)
{
    size_t count_pos = data.wpos();
    data << uint8(0);                                      // placeholder

    // This function also fill data for channeled spells:
    // m_needAliveTargetMask req for stop channeling if one target die
    uint32 hit  = m_UniqueGOTargetInfo.size();              // Always hits on GO
    uint32 miss = 0;

    for (TargetList::iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
    {
        if (ihit->effectMask == 0)                          // No effect apply - all immuned add state
        {
            // possibly SPELL_MISS_IMMUNE2 for this??
            ihit->missCondition = SPELL_MISS_IMMUNE2;
            ++miss;
        }
        else if (ihit->missCondition == SPELL_MISS_NONE)    // Add only hits
        {
            ++hit;
            data << ihit->targetGUID;
            m_needAliveTargetMask |= ihit->effectMask;
        }
        else
        {
            if (IsChanneledSpell(m_spellInfo) && (ihit->missCondition == SPELL_MISS_RESIST || ihit->missCondition == SPELL_MISS_REFLECT))
                m_duration = 0;                             // cancel aura to avoid visual effect continue
            ++miss;
        }
    }

    for (GOTargetList::const_iterator ighit = m_UniqueGOTargetInfo.begin(); ighit != m_UniqueGOTargetInfo.end(); ++ighit)
        data << ighit->targetGUID;                         // Always hits

    data.put<uint8>(count_pos, hit);

    data << (uint8)miss;
    for (TargetList::const_iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
    {
        if (ihit->missCondition != SPELL_MISS_NONE)         // Add only miss
        {
            data << ihit->targetGUID;
            data << uint8(ihit->missCondition);
            if (ihit->missCondition == SPELL_MISS_REFLECT)
                data << uint8(ihit->reflectResult);
        }
    }
    // Reset m_needAliveTargetMask for non channeled spell
    if (!IsChanneledSpell(m_spellInfo))
        m_needAliveTargetMask = 0;
}

void Spell::SendInterrupted(uint8 result)
{
    WorldPacket data(SMSG_SPELL_FAILURE, (8 + 4 + 1));
    data << m_caster->GetPackGUID();
    data << uint8(m_cast_count);
    data << uint32(m_spellInfo->Id);
    data << uint8(result);
    m_caster->SendMessageToSet(data, true);

    data.Initialize(SMSG_SPELL_FAILED_OTHER, (8 + 4));
    data << m_caster->GetPackGUID();
    data << uint8(m_cast_count);
    data << uint32(m_spellInfo->Id);
    data << uint8(result);
    m_caster->SendMessageToSet(data, true);
}

void Spell::SendChannelUpdate(uint32 time, bool properEnding)
{
    if (time == 0)
    {
        // Channel aura is removed during its update when channel ends properly
        // If infinite channel remove aura right away
        if (!properEnding || m_spellInfo->DurationIndex == 21)
        {
            m_caster->RemoveAurasByCasterSpell(m_spellInfo->Id, m_caster->GetObjectGuid());

            ObjectGuid target_guid = m_caster->GetChannelObjectGuid();
            if (target_guid != m_caster->GetObjectGuid() && target_guid.IsUnit())
                if (Unit* target = ObjectAccessor::GetUnit(*m_caster, target_guid))
                    target->RemoveAurasByCasterSpell(m_spellInfo->Id, m_caster->GetObjectGuid());
        }

        // Only finish channeling when latest channeled spell finishes
        if (m_caster->GetUInt32Value(UNIT_CHANNEL_SPELL) != m_spellInfo->Id)
            return;

        m_caster->SetChannelObjectGuid(ObjectGuid());
        m_caster->SetUInt32Value(UNIT_CHANNEL_SPELL, 0);
    }

    WorldPacket data(SMSG_CHANNEL_UPDATE, 8 + 4);
    data << m_caster->GetPackGUID();
    data << uint32(time);
    m_caster->SendMessageToSet(data, true);
}

void Spell::SendChannelStart(uint32 duration)
{
    WorldObject* target = nullptr;

    // select dynobject created by first effect if any
    if (m_spellInfo->Effect[EFFECT_INDEX_0] == SPELL_EFFECT_PERSISTENT_AREA_AURA)
        target = m_caster->GetDynObject(m_spellInfo->Id, EFFECT_INDEX_0);
    // select first not resisted target from target list for _0_ effect
    else if (!m_UniqueTargetInfo.empty())
    {
        for (TargetList::const_iterator itr = m_UniqueTargetInfo.begin(); itr != m_UniqueTargetInfo.end(); ++itr)
        {
            if ((itr->effectMask & (1 << EFFECT_INDEX_0)) && itr->reflectResult == SPELL_MISS_NONE &&
                    itr->targetGUID != m_caster->GetObjectGuid())
            {
                target = ObjectAccessor::GetUnit(*m_caster, itr->targetGUID);
                break;
            }
        }
    }
    else if (!m_UniqueGOTargetInfo.empty())
    {
        for (GOTargetList::const_iterator itr = m_UniqueGOTargetInfo.begin(); itr != m_UniqueGOTargetInfo.end(); ++itr)
        {
            if (itr->effectMask & (1 << EFFECT_INDEX_0))
            {
                target = m_caster->GetMap()->GetGameObject(itr->targetGUID);
                break;
            }
        }
    }

    WorldPacket data(SMSG_CHANNEL_START, (8 + 4 + 4));
    data << m_caster->GetPackGUID();
    data << uint32(m_spellInfo->Id);
    data << uint32(duration);
    data << uint8(0);       // unk1
    //if (unk1)
    //{
    //    data << uint32(0);
    //    data << uint32(0);
    //}
    data << uint8(0);       // unk2
    //if (unk2)
    //{
    //    data << ObjectGuid().WriteAsPacked();
    //    data << uint32(0);
    //    data << uint8(0);   // unk3
    //    if (unk3 == 2)
    //        data << ObjectGuid().WriteAsPacked();
    //}

    m_caster->SendMessageToSet(data, true);

    m_timer = duration;

    if (target)
        m_caster->SetChannelObjectGuid(target->GetObjectGuid());

    m_caster->SetUInt32Value(UNIT_CHANNEL_SPELL, m_spellInfo->Id);
}

void Spell::SendResurrectRequest(Player* target)
{
    // Both players and NPCs can resurrect using spells - have a look at creature 28487 for example
    // However, the packet structure differs slightly

    const char* sentName = m_caster->GetTypeId() == TYPEID_PLAYER ? "" : m_caster->GetNameForLocaleIdx(target->GetSession()->GetSessionDbLocaleIndex());

    WorldPacket data(SMSG_RESURRECT_REQUEST, (8 + 4 + strlen(sentName) + 1 + 1 + 1));
    data << m_caster->GetObjectGuid();
    data << uint32(strlen(sentName) + 1);

    data << sentName;
    data << uint8(m_caster->isSpiritHealer());
    // override delay sent with SMSG_CORPSE_RECLAIM_DELAY, set instant resurrection for spells with this attribute
    data << uint8(!m_spellInfo->HasAttribute(SPELL_ATTR_EX3_IGNORE_RESURRECTION_TIMER));

    data << uint32(m_spellInfo->Id);

    target->GetSession()->SendPacket(data);
}

void Spell::SendPlaySpellVisual(uint32 SpellID)
{
    if (m_caster->GetTypeId() != TYPEID_PLAYER)
        return;

    WorldPacket data;
    m_caster->BuildSendPlayVisualPacket(data, SpellID, false);

    ((Player*)m_caster)->GetSession()->SendPacket(data);
}

void Spell::TakeCastItem()
{
    if (!m_CastItem || m_caster->GetTypeId() != TYPEID_PLAYER)
        return;

    // not remove cast item at triggered spell (equipping, weapon damage, etc)
    if (m_IsTriggeredSpell && !(m_targets.m_targetMask & TARGET_FLAG_TRADE_ITEM))
        return;

    ItemPrototype const* proto = m_CastItem->GetProto();

    if (!proto)
    {
        // This code is to avoid a crash
        // I'm not sure, if this is really an error, but I guess every item needs a prototype
        sLog.outError("Cast item (%s) has no item prototype", m_CastItem->GetGuidStr().c_str());
        return;
    }

    bool expendable = false;
    bool withoutCharges = false;

    for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        if (proto->Spells[i].SpellId)
        {
            // item has limited charges
            if (proto->Spells[i].SpellCharges)
            {
                if (proto->Spells[i].SpellCharges < 0)
                    expendable = true;

                int32 charges = m_CastItem->GetSpellCharges(i);

                // item has charges left
                if (charges)
                {
                    (charges > 0) ? --charges : ++charges;  // abs(charges) less at 1 after use
                    if (proto->Stackable == 1)
                        m_CastItem->SetSpellCharges(i, charges);
                    m_CastItem->SetState(ITEM_CHANGED, (Player*)m_caster);
                }

                // all charges used
                withoutCharges = (charges == 0);
            }
        }
    }

    if (expendable && withoutCharges)
    {
        uint32 count = 1;
        ((Player*)m_caster)->DestroyItemCount(m_CastItem, count, true);

        // prevent crash at access to deleted m_targets.getItemTarget
        ClearCastItem();
    }
}

void Spell::TakePower()
{
    if (m_CastItem || m_triggeredByAuraSpell)
        return;

    bool hit = true;
    if (m_caster->GetTypeId() == TYPEID_PLAYER)
    {
        if (m_spellInfo->powerType == POWER_RAGE || m_spellInfo->powerType == POWER_ENERGY || m_spellInfo->powerType == POWER_HOLY_POWER)
        {
            ObjectGuid targetGuid = m_targets.getUnitTargetGuid();
            if (!targetGuid.IsEmpty())
                for (TargetList::iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
                    if (ihit->targetGUID == targetGuid)
                    {
                        if (ihit->missCondition != SPELL_MISS_NONE && ihit->missCondition != SPELL_MISS_MISS)
                            hit = false;
                        if (ihit->missCondition != SPELL_MISS_NONE)
                        {
                            // lower spell cost on fail (by talent aura)
                            if (Player* modOwner = ((Player*)m_caster)->GetSpellModOwner())
                                modOwner->ApplySpellMod(m_spellInfo->Id, SPELLMOD_SPELL_COST_REFUND_ON_FAIL, m_powerCost);
                        }
                        break;
                    }
        }
    }

    // health as power used
    if (m_spellInfo->powerType == POWER_HEALTH)
    {
        m_caster->ModifyHealth(-(int32)m_powerCost);
        m_caster->SendSpellNonMeleeDamageLog(m_caster, m_spellInfo->Id, m_powerCost, GetSpellSchoolMask(m_spellInfo), 0, 0, false, 0, false);
        return;
    }

    if (m_spellInfo->powerType >= MAX_POWERS)
    {
        sLog.outError("Spell::TakePower: Unknown power type '%d'", m_spellInfo->powerType);
        return;
    }

    Powers powerType = Powers(m_spellInfo->powerType);

    if (powerType == POWER_HOLY_POWER)
    {
        m_usedHolyPower = m_powerCost;

        // spells consume all holy power when successfully hit
        if (hit)
        {
            // Divine Purpose
            if (m_caster->HasAura(90174))
            {
                m_usedHolyPower = m_caster->GetMaxPower(POWER_HOLY_POWER);
                return;
            }
            else
                m_usedHolyPower = m_caster->GetPower(POWER_HOLY_POWER);
        }

        // Zealotry - does not take power
        if (m_spellInfo->Id == 85696)
            return;

        m_caster->ModifyPower(powerType, -(int32)m_usedHolyPower);
        return;
    }

    if (powerType == POWER_RUNE)
    {
        TakeRunePower(hit);
        return;
    }

    m_caster->ModifyPower(powerType, -(int32)m_powerCost);
}

SpellCastResult Spell::CheckRunePower()
{
    if (m_caster->GetTypeId() != TYPEID_PLAYER)
        return SPELL_CAST_OK;

    Player* plr = (Player*)m_caster;

    if (plr->getClass() != CLASS_DEATH_KNIGHT)
        return SPELL_CAST_OK;

    SpellRuneCostEntry const* src = sSpellRuneCostStore.LookupEntry(m_spellInfo->runeCostID);

    if (!src)
        return SPELL_CAST_OK;

    if (src->NoRuneCost())
        return SPELL_CAST_OK;

    // at this moment for rune cost exist only no cost mods, and no percent mods
    int32 runeCost[NUM_RUNE_TYPES];                         // blood, frost, unholy, death
    for (uint32 i = 0; i < RUNE_DEATH; ++i)
    {
        runeCost[i] = src->RuneCost[i];
        if (Player* modOwner = m_caster->GetSpellModOwner())
            modOwner->ApplySpellMod(m_spellInfo->Id, SPELLMOD_COST, runeCost[i]);
    }

    runeCost[RUNE_DEATH] = MAX_RUNES;                       // calculated later

    for (uint32 i = 0; i < MAX_RUNES; ++i)
    {
        RuneType rune = plr->GetCurrentRune(i);
        if (!plr->GetRuneCooldown(i) && runeCost[rune] > 0)
            --runeCost[rune];
    }

    for (uint32 i = 0; i < RUNE_DEATH; ++i)
        if (runeCost[i] > 0)
            runeCost[RUNE_DEATH] += runeCost[i];

    if (runeCost[RUNE_DEATH] > MAX_RUNES)
        return SPELL_FAILED_NO_POWER;                       // not sure if result code is correct

    return SPELL_CAST_OK;
}

void Spell::TakeRunePower(bool hit)
{
    if (m_caster->GetTypeId() != TYPEID_PLAYER)
        return;

    Player* plr = (Player*)m_caster;

    if (plr->getClass() != CLASS_DEATH_KNIGHT)
        return;

    SpellRuneCostEntry const* src = sSpellRuneCostStore.LookupEntry(m_spellInfo->runeCostID);

    if (!src)
        return;

    if (src->NoRuneCost() && src->NoRunicPowerGain())
        return;

    m_runesState = plr->GetRunesState();                    // store previous state

    // at this moment for rune cost exist only no cost mods, and no percent mods
    int32 runeCost[NUM_RUNE_TYPES];                         // blood, frost, unholy, death
    for (uint32 i = 0; i < RUNE_DEATH; ++i)
    {
        runeCost[i] = src->RuneCost[i];
        if (Player* modOwner = m_caster->GetSpellModOwner())
            modOwner->ApplySpellMod(m_spellInfo->Id, SPELLMOD_COST, runeCost[i]);
    }

    runeCost[RUNE_DEATH] = 0;                               // calculated later

    plr->ClearLastUsedRuneMask();

    for (uint32 i = 0; i < MAX_RUNES; ++i)
    {
        RuneType rune = plr->GetCurrentRune(i);
        if (!plr->GetRuneCooldown(i) && runeCost[rune] > 0)
        {
            uint16 baseCd = hit ? uint16(RUNE_BASE_COOLDOWN) : uint16(RUNE_MISS_COOLDOWN);
            plr->SetBaseRuneCooldown(i, baseCd);
            plr->SetRuneCooldown(i, baseCd);
            plr->SetLastUsedRune(rune);
            --runeCost[rune];
        }
    }

    runeCost[RUNE_DEATH] = runeCost[RUNE_BLOOD] + runeCost[RUNE_UNHOLY] + runeCost[RUNE_FROST];

    if (runeCost[RUNE_DEATH] > 0)
    {
        for (uint32 i = 0; i < MAX_RUNES; ++i)
        {
            RuneType rune = plr->GetCurrentRune(i);
            if (!plr->GetRuneCooldown(i) && rune == RUNE_DEATH)
            {
                uint16 baseCd = hit ? uint16(RUNE_BASE_COOLDOWN) : uint16(RUNE_MISS_COOLDOWN);
                plr->SetBaseRuneCooldown(i, baseCd);
                plr->SetRuneCooldown(i, baseCd);
                plr->SetLastUsedRune(rune);
                --runeCost[rune];

                // keep Death Rune type if missed
                if (hit)
                    plr->RestoreBaseRune(i);

                if (runeCost[RUNE_DEATH] == 0)
                    break;
            }
        }
    }

    if (hit)
    {
        // you can gain some runic power when use runes
        int32 rp = int32(src->runePowerGain);
        if (rp)
        {
            if (Player* modOwner = m_caster->GetSpellModOwner())
                modOwner->ApplySpellMod(m_spellInfo->Id, SPELLMOD_COST, rp);

            rp = int32(sWorld.getConfig(CONFIG_FLOAT_RATE_POWER_RUNICPOWER_INCOME) * rp);
            rp += m_caster->GetTotalAuraModifier(SPELL_AURA_MOD_RUNIC_POWER_REGEN) * rp / 100;
            if (rp > 0)
                plr->ModifyPower(POWER_RUNIC_POWER, (int32)rp);
        }
    }
}

void Spell::TakeAmmo()
{
    // take ammo
    if (m_attackType == RANGED_ATTACK && m_caster->GetTypeId() == TYPEID_PLAYER)
    {
        Item* pItem = ((Player*)m_caster)->GetWeaponForAttack(RANGED_ATTACK, true, false);

        // wands don't have ammo
        if (!pItem || pItem->GetProto()->SubClass == ITEM_SUBCLASS_WEAPON_WAND)
            return;

        if (pItem->GetProto()->InventoryType == INVTYPE_THROWN)
        {
            if (pItem->GetMaxStackCount() == 1)
            {
                // decrease durability for non-stackable throw weapon
                ((Player*)m_caster)->DurabilityPointLossForEquipSlot(EQUIPMENT_SLOT_RANGED);
            }
            else
            {
                // decrease items amount for stackable throw weapon
                uint32 count = 1;
                ((Player*)m_caster)->DestroyItemCount(pItem, count, true);
            }
        }
    }
}

void Spell::TakeReagents()
{
    if (m_caster->GetTypeId() != TYPEID_PLAYER)
        return;

    if (IgnoreItemRequirements())                           // reagents used in triggered spell removed by original spell or don't must be removed.
        return;

    Player* p_caster = (Player*)m_caster;
    if (p_caster->CanNoReagentCast(m_spellInfo))
        return;

    for(uint32 x = 0; x < MAX_SPELL_REAGENTS; ++x)
    {
        if(m_spellInfo->Reagent[x] <= 0)
            continue;

        uint32 itemid = m_spellInfo->Reagent[x];
        uint32 itemcount = m_spellInfo->ReagentCount[x];

        // if CastItem is also spell reagent
        if (m_CastItem)
        {
            ItemPrototype const* proto = m_CastItem->GetProto();
            if (proto && proto->ItemId == itemid)
            {
                for (int s = 0; s < MAX_ITEM_PROTO_SPELLS; ++s)
                {
                    // CastItem will be used up and does not count as reagent
                    int32 charges = m_CastItem->GetSpellCharges(s);
                    if (proto->Spells[s].SpellCharges < 0 && abs(charges) < 2)
                    {
                        ++itemcount;
                        break;
                    }
                }

                m_CastItem = nullptr;
                m_CastItemGuid.Clear();
            }
        }

        // if getItemTarget is also spell reagent
        if (m_targets.getItemTargetEntry() == itemid)
            m_targets.setItemTarget(nullptr);

        p_caster->DestroyItemCount(itemid, itemcount, true);
    }
}

void Spell::HandleThreatSpells()
{
    if (m_UniqueTargetInfo.empty())
        return;

    SpellThreatEntry const* threatEntry = sSpellMgr.GetSpellThreatEntry(m_spellInfo->Id);

    if (!threatEntry || (!threatEntry->threat && threatEntry->ap_bonus == 0.0f))
        return;

    float threat = threatEntry->threat;
    if (threatEntry->ap_bonus != 0.0f)
        threat += threatEntry->ap_bonus * m_caster->GetTotalAttackPowerValue(GetWeaponAttackType(m_spellInfo));

    bool positive = true;
    uint8 effectMask = 0;
    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
        if (m_spellInfo->Effect[i])
            effectMask |= (1 << i);

    if (m_negativeEffectMask & effectMask)
    {
        // can only handle spells with clearly defined positive/negative effect, check at spell_threat loading probably not perfect
        // so abort when only some effects are negative.
        if ((m_negativeEffectMask & effectMask) != effectMask)
        {
            DEBUG_FILTER_LOG(LOG_FILTER_SPELL_CAST, "Spell %u, rank %u, is not clearly positive or negative, ignoring bonus threat", m_spellInfo->Id, sSpellMgr.GetSpellRank(m_spellInfo->Id));
            return;
        }
        positive = false;
    }

    // since 2.0.1 threat from positive effects also is distributed among all targets, so the overall caused threat is at most the defined bonus
    threat /= m_UniqueTargetInfo.size();

    for (TargetList::const_iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
    {
        if (ihit->missCondition != SPELL_MISS_NONE)
            continue;

        Unit* target = m_caster->GetObjectGuid() == ihit->targetGUID ? m_caster : ObjectAccessor::GetUnit(*m_caster, ihit->targetGUID);
        if (!target)
            continue;

        // positive spells distribute threat among all units that are in combat with target, like healing
        if (positive)
        {
            target->getHostileRefManager().threatAssist(m_caster /*real_caster ??*/, threat, m_spellInfo);
        }
        // for negative spells threat gets distributed among affected targets
        else
        {
            if (!target->CanHaveThreatList())
                continue;

            target->AddThreat(m_caster, threat, false, GetSpellSchoolMask(m_spellInfo), m_spellInfo);
        }
    }

    DEBUG_FILTER_LOG(LOG_FILTER_SPELL_CAST, "Spell %u added an additional %f threat for %s " SIZEFMTD " target(s)", m_spellInfo->Id, threat, positive ? "assisting" : "harming", m_UniqueTargetInfo.size());
}

void Spell::HandleEffects(Unit* pUnitTarget, Item* pItemTarget, GameObject* pGOTarget, SpellEffectIndex i, float DamageMultiplier)
{
    unitTarget = pUnitTarget;
    itemTarget = pItemTarget;
    gameObjTarget = pGOTarget;

    uint8 eff = m_spellInfo->Effect[i];

    //SpellEffectEntry const* spellEffect = m_spellInfo->GetSpellEffect(SpellEffectIndex(i));

    damage = int32(CalculateDamage(i, unitTarget) * DamageMultiplier);

    DEBUG_FILTER_LOG(LOG_FILTER_SPELL_CAST, "Spell %u Effect%d : %u Targets: %s, %s, %s",
        m_spellInfo->Id, i, eff,
        unitTarget ? unitTarget->GetGuidStr().c_str() : "-",
        itemTarget ? itemTarget->GetGuidStr().c_str() : "-",
        gameObjTarget ? gameObjTarget->GetGuidStr().c_str() : "-");

    if (eff < TOTAL_SPELL_EFFECTS)
    {
        (*this.*SpellEffects[eff])(i);
    }
    else
        sLog.outError("WORLD: Spell %u Effect%d : %u > TOTAL_SPELL_EFFECTS", m_spellInfo->Id, i, eff);
}

void Spell::AddTriggeredSpell(uint32 spellId)
{
    SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellId);

    if (!spellInfo)
    {
        sLog.outError("Spell::AddTriggeredSpell: unknown spell id %u used as triggred spell for spell %u)", spellId, m_spellInfo->Id);
        return;
    }

    m_TriggerSpells.push_back(spellInfo);
}

void Spell::AddPrecastSpell(uint32 spellId)
{
    SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellId);

    if (!spellInfo)
    {
        sLog.outError("Spell::AddPrecastSpell: unknown spell id %u used as pre-cast spell for spell %u)", spellId, m_spellInfo->Id);
        return;
    }

    m_preCastSpells.push_back(spellInfo);
}

void Spell::CastTriggerSpells()
{
    for (SpellInfoList::const_iterator si = m_TriggerSpells.begin(); si != m_TriggerSpells.end(); ++si)
    {
        Spell* spell = new Spell(m_caster, (*si), true, m_originalCasterGUID);
        spell->SpellStart(&m_targets);                      // use original spell original targets
    }
}

void Spell::CastPreCastSpells(Unit* target)
{
    for (SpellInfoList::const_iterator si = m_preCastSpells.begin(); si != m_preCastSpells.end(); ++si)
        m_caster->CastSpell(target, (*si), TRIGGERED_OLD_TRIGGERED, m_CastItem);
}

Unit* Spell::GetPrefilledUnitTargetOrUnitTarget(SpellEffectIndex effIndex) const
{
    for (TargetList::const_iterator itr = m_UniqueTargetInfo.begin(); itr != m_UniqueTargetInfo.end(); ++itr)
        if (itr->effectMask & (1 << effIndex))
            return m_caster->GetMap()->GetUnit(itr->targetGUID);

    return m_targets.getUnitTarget();
}

SpellCastResult Spell::CheckCast(bool strict)
{
    // check cooldowns to prevent cheating (ignore passive spells, that client side visual only)
    if (m_caster->GetTypeId() == TYPEID_PLAYER && !m_spellInfo->HasAttribute(SPELL_ATTR_PASSIVE) &&
        !m_IsTriggeredSpell && !m_caster->IsSpellReady(*m_spellInfo, m_CastItem ? m_CastItem->GetProto() : nullptr))
    {
        if (m_triggeredByAuraSpell)
            return SPELL_FAILED_DONT_REPORT;
        else
            return SPELL_FAILED_NOT_READY;
    }

    if (!m_caster->IsAlive() && m_caster->GetTypeId() == TYPEID_PLAYER && !m_spellInfo->HasAttribute(SPELL_ATTR_CASTABLE_WHILE_DEAD) && !m_spellInfo->HasAttribute(SPELL_ATTR_PASSIVE))
        return SPELL_FAILED_CASTER_DEAD;

    // check global cooldown
    if (strict && !m_IsTriggeredSpell && m_caster->HaveGCD(m_spellInfo))
        return SPELL_FAILED_NOT_READY;

    // only allow triggered spells if at an ended battleground
    if (!m_IsTriggeredSpell && m_caster->GetTypeId() == TYPEID_PLAYER)
        if (BattleGround* bg = ((Player*)m_caster)->GetBattleGround())
            if (bg->GetStatus() == STATUS_WAIT_LEAVE)
                return SPELL_FAILED_DONT_REPORT;

    if (!m_IsTriggeredSpell && IsNonCombatSpell(m_spellInfo) &&
            m_caster->IsInCombat() && !m_caster->IsIgnoreUnitState(m_spellInfo, IGNORE_UNIT_COMBAT_STATE))
        return SPELL_FAILED_AFFECTING_COMBAT;

    if (m_caster->GetTypeId() == TYPEID_PLAYER && !((Player*)m_caster)->IsGameMaster() &&
            sWorld.getConfig(CONFIG_BOOL_VMAP_INDOOR_CHECK) &&
            VMAP::VMapFactory::createOrGetVMapManager()->isLineOfSightCalcEnabled())
    {
        if (m_spellInfo->HasAttribute(SPELL_ATTR_OUTDOORS_ONLY) &&
                !m_caster->GetTerrain()->IsOutdoors(m_caster->GetPositionX(), m_caster->GetPositionY(), m_caster->GetPositionZ()))
            return SPELL_FAILED_ONLY_OUTDOORS;

        if (m_spellInfo->HasAttribute(SPELL_ATTR_INDOORS_ONLY) &&
                m_caster->GetTerrain()->IsOutdoors(m_caster->GetPositionX(), m_caster->GetPositionY(), m_caster->GetPositionZ()))
            return SPELL_FAILED_ONLY_INDOORS;
    }
    // only check at first call, Stealth auras are already removed at second call
    // for now, ignore triggered spells
    if (strict && !m_IsTriggeredSpell)
    {
        // Ignore form req aura
        if (!m_caster->HasAffectedAura(SPELL_AURA_MOD_IGNORE_SHAPESHIFT, m_spellInfo))
        {
            // Cannot be used in this stance/form
            SpellCastResult shapeError = GetErrorAtShapeshiftedCast(m_spellInfo, m_caster->GetShapeshiftForm());
            if (shapeError != SPELL_CAST_OK)
                return shapeError;

            if (m_spellInfo->HasAttribute(SPELL_ATTR_ONLY_STEALTHED) && !(m_caster->HasStealthAura()))
                return SPELL_FAILED_ONLY_STEALTHED;
        }
    }

    // caster state requirements
    if(m_spellInfo->CasterAuraState && !m_caster->HasAuraState(AuraState(m_spellInfo->CasterAuraState)))
        return SPELL_FAILED_CASTER_AURASTATE;

    if(m_spellInfo->CasterAuraStateNot && m_caster->HasAuraState(AuraState(m_spellInfo->CasterAuraStateNot)))
        return SPELL_FAILED_CASTER_AURASTATE;

    // Caster aura req check if need
    if(m_spellInfo->casterAuraSpell && !m_caster->HasAura(m_spellInfo->casterAuraSpell))
        return SPELL_FAILED_CASTER_AURASTATE;
    if(m_spellInfo->excludeCasterAuraSpell)
    {
        // Special cases of non existing auras handling
        if(m_spellInfo->excludeCasterAuraSpell == 61988)
        {
            // Avenging Wrath Marker
            if (m_caster->HasAura(61987))
                return SPELL_FAILED_CASTER_AURASTATE;
        }
        else if(m_caster->HasAura(m_spellInfo->excludeCasterAuraSpell))
            return SPELL_FAILED_CASTER_AURASTATE;
    }

    if (m_caster->GetTypeId() == TYPEID_PLAYER)
    {
        // cancel autorepeat spells if cast start when moving
        // (not wand currently autorepeat cast delayed to moving stop anyway in spell update code)
        if (((Player*)m_caster)->IsMoving() && !m_caster->HasAffectedAura(SPELL_AURA_ALLOW_CAST_WHILE_MOVING, m_spellInfo))
        {
            // skip stuck spell to allow use it in falling case and apply spell limitations at movement
            if ((!((Player*)m_caster)->m_movementInfo.HasMovementFlag(MOVEFLAG_FALLINGFAR) || m_spellInfo->Effect[EFFECT_INDEX_0] != SPELL_EFFECT_STUCK) &&
                (IsAutoRepeat() || (m_spellInfo->AuraInterruptFlags & AURA_INTERRUPT_FLAG_NOT_SEATED) != 0))
                return SPELL_FAILED_MOVING;
        }

        if (!m_IsTriggeredSpell && NeedsComboPoints(m_spellInfo) && !m_caster->IsIgnoreUnitState(m_spellInfo, IGNORE_UNIT_TARGET_STATE) &&
                (!m_targets.getUnitTarget() || m_targets.getUnitTarget()->GetObjectGuid() != ((Player*)m_caster)->GetComboTargetGuid()) &&
                !m_spellInfo->HasAttribute(SPELL_ATTR_EX8_IGNORE_TARGET_FOR_COMBO_POINTS))
            // warrior not have real combo-points at client side but use this way for mark allow Overpower use
            return m_caster->getClass() == CLASS_WARRIOR ? SPELL_FAILED_CASTER_AURASTATE : SPELL_FAILED_NO_COMBO_POINTS;
    }

    // Spells like Disengage are allowed only in combat
    if (!m_caster->IsInCombat() && m_spellInfo->HasAttribute(SPELL_ATTR_STOP_ATTACK_TARGET) && m_spellInfo->HasAttribute(SPELL_ATTR_EX2_UNK26))
        return SPELL_FAILED_CASTER_AURASTATE;

    if(Unit *target = m_targets.getUnitTarget())
    {
        // target state requirements (not allowed state), apply to self also
        if(m_spellInfo->TargetAuraStateNot && target->HasAuraState(AuraState(m_spellInfo->TargetAuraStateNot)))
            return SPELL_FAILED_TARGET_AURASTATE;

        if (!m_IsTriggeredSpell && IsDeathOnlySpell(m_spellInfo) && target->IsAlive())
            return SPELL_FAILED_TARGET_NOT_DEAD;

        // Target aura req check if need
        if(m_spellInfo->targetAuraSpell && !target->HasAura(m_spellInfo->targetAuraSpell))
            return SPELL_FAILED_CASTER_AURASTATE;

        if(m_spellInfo->excludeTargetAuraSpell)
        {
            // Special cases of non existing auras handling
            if (m_spellInfo->excludeTargetAuraSpell == 61988)
            {
                // Avenging Wrath Marker
                if (target->HasAura(61987))
                    return SPELL_FAILED_CASTER_AURASTATE;
            }
            else if (target->HasAura(m_spellInfo->excludeTargetAuraSpell))
                return SPELL_FAILED_CASTER_AURASTATE;
        }

        // totem immunity for channeled spells(needs to be before spell cast)
        // spell attribs for player channeled spells
        if (m_spellInfo->HasAttribute(SPELL_ATTR_EX_CHANNEL_TRACK_TARGET) // TODO: Investigate this condition
            && m_spellInfo->HasAttribute(SPELL_ATTR_EX5_HASTE_AFFECT_DURATION)
            && target->GetTypeId() == TYPEID_UNIT && ((Creature*)target)->IsTotem())
            return SPELL_FAILED_IMMUNE;

        bool non_caster_target = target != m_caster && !IsSpellWithCasterSourceTargetsOnly(m_spellInfo);

        if (non_caster_target)
        {
            // target state requirements (apply to non-self only), to allow cast affects to self like Dirty Deeds
            if (m_spellInfo->TargetAuraState && !target->HasAuraStateForCaster(AuraState(m_spellInfo->TargetAuraState), m_caster->GetObjectGuid()) &&
                !m_caster->IsIgnoreUnitState(m_spellInfo, m_spellInfo->TargetAuraState == AURA_STATE_FROZEN ? IGNORE_UNIT_TARGET_NON_FROZEN : IGNORE_UNIT_TARGET_STATE))
                return SPELL_FAILED_TARGET_AURASTATE;

            // Not allow casting on flying player
            if (target->IsTaxiFlying())
            {
                switch (m_spellInfo->Id)
                {
                    // Except some spells from Taxi Flying cast
                    case 36573:                             // Vision Guide
                    case 42316:                             // Alcaz Survey Credit
                    case 42385:                             // Alcaz Survey Aura
                        break;
                    default:
                        return SPELL_FAILED_BAD_TARGETS;
                }
            }

            if (!IsIgnoreLosSpell(m_spellInfo) && !m_IsTriggeredSpell && VMAP::VMapFactory::checkSpellForLoS(m_spellInfo->Id) && !m_caster->IsWithinLOSInMap(target))
                return SPELL_FAILED_LINE_OF_SIGHT;

            if (m_caster->GetTypeId() == TYPEID_PLAYER)
            {
                // auto selection spell rank implemented in WorldSession::HandleCastSpellOpcode
                // this case can be triggered if rank not found (too low-level target for first rank)
                if (!m_CastItem && !m_IsTriggeredSpell)
                    // spell expected to be auto-downranking in cast handle, so must be same
                    if (m_spellInfo != sSpellMgr.SelectAuraRankForLevel(m_spellInfo, target->GetLevel()))
                        return SPELL_FAILED_LOWLEVEL;

                // Do not allow these spells to target creatures not tapped by us (Banish, Polymorph, many quest spells)
                if (m_spellInfo->HasAttribute(SPELL_ATTR_EX2_CANT_TARGET_TAPPED))
                    if (Creature const* targetCreature = dynamic_cast<Creature*>(target))
                        if ((!targetCreature->GetLootRecipientGuid().IsEmpty()) && !targetCreature->IsTappedBy((Player*)m_caster))
                            return SPELL_FAILED_CANT_CAST_ON_TAPPED;
            }

            if (strict && m_spellInfo->HasAttribute(SPELL_ATTR_EX3_TARGET_ONLY_PLAYER) && target->GetTypeId() != TYPEID_PLAYER && !IsAreaOfEffectSpell(m_spellInfo))
                return SPELL_FAILED_BAD_TARGETS;
        }
        else if (m_caster == target)
        {
            if (m_caster->GetTypeId() == TYPEID_PLAYER && m_caster->IsInWorld())
            {
                // Additional check for some spells
                // If 0 spell effect empty - client not send target data (need use selection)
                // TODO: check it on next client version
                if (m_targets.m_targetMask == TARGET_FLAG_SELF &&
                    m_spellInfo->EffectImplicitTargetA[EFFECT_INDEX_1] == TARGET_UNIT_ENEMY)
                {
                    target = m_caster->GetMap()->GetUnit(((Player*)m_caster)->GetSelectionGuid());
                    if (!target)
                        return SPELL_FAILED_BAD_TARGETS;

                    m_targets.setUnitTarget(target);
                }
            }

            // Some special spells with non-caster only mode

            // Fire Shield
            if (m_spellInfo->SpellFamilyName == SPELLFAMILY_WARLOCK &&
                m_spellInfo->SpellIconID == 16)
                return SPELL_FAILED_BAD_TARGETS;

            // Focus Magic (main spell)
            if (m_spellInfo->Id == 54646)
                return SPELL_FAILED_BAD_TARGETS;

            // Lay on Hands (self cast)
            if (m_spellInfo->SpellFamilyName == SPELLFAMILY_PALADIN &&
                m_spellInfo->SpellFamilyFlags & uint64(0x0000000000008000))
            {
                if (target->HasAura(25771))                 // Forbearance
                    return SPELL_FAILED_CASTER_AURASTATE;
                if (target->HasAura(61987))                 // Avenging Wrath Marker
                    return SPELL_FAILED_CASTER_AURASTATE;
            }
        }

        // check pet presents
        for (int j = 0; j < MAX_EFFECT_INDEX; ++j)
        {
            if(m_spellInfo->EffectImplicitTargetA[j] == TARGET_UNIT_CASTER_PET)
            {
                Pet* pet = m_caster->GetPet();
                if (!pet)
                {
                    if (m_triggeredByAuraSpell)             // not report pet not existence for triggered spells
                        return SPELL_FAILED_DONT_REPORT;
                    else
                        return SPELL_FAILED_NO_PET;
                }
                else if (!pet->IsAlive())
                    return SPELL_FAILED_TARGETS_DEAD;
                break;
            }
        }

        // check creature type
        // ignore self casts (including area casts when caster selected as target)
        if (non_caster_target)
        {
            if (!CheckTargetCreatureType(target))
            {
                if (target->GetTypeId() == TYPEID_PLAYER)
                    return SPELL_FAILED_TARGET_IS_PLAYER;
                else
                    return SPELL_FAILED_BAD_TARGETS;
            }

            // simple cases
            bool explicit_target_mode = false;
            bool target_hostile = false;
            bool target_hostile_checked = false;
            bool target_friendly = false;
            bool target_friendly_checked = false;
            for (int k = 0; k < MAX_EFFECT_INDEX;  ++k)
            {
                if (IsExplicitPositiveTarget(m_spellInfo->EffectImplicitTargetA[k]))
                {
                    if (!target_hostile_checked)
                    {
                        target_hostile_checked = true;
                        target_hostile = m_caster->IsHostileTo(target);
                    }

                    if (target_hostile)
                        return SPELL_FAILED_BAD_TARGETS;

                    explicit_target_mode = true;
                }
                else if (IsExplicitNegativeTarget(m_spellInfo->EffectImplicitTargetA[k]))
                {
                    if (!target_friendly_checked)
                    {
                        target_friendly_checked = true;
                        target_friendly = m_caster->IsFriendlyTo(target);
                    }

                    if (target_friendly)
                        return SPELL_FAILED_BAD_TARGETS;

                    explicit_target_mode = true;
                }
            }
            // TODO: this check can be applied and for player to prevent cheating when IsPositiveSpell will return always correct result.
            // check target for pet/charmed casts (not self targeted), self targeted cast used for area effects and etc
            if (!explicit_target_mode && m_caster->GetTypeId() == TYPEID_UNIT && m_caster->GetMasterGuid())
            {
                // check correctness positive/negative cast target (pet cast real check and cheating check)
                if (IsPositiveSpell(m_spellInfo->Id, m_caster, target))
                {
                    if (!target_hostile_checked)
                    {
                        target_hostile_checked = true;
                        target_hostile = m_caster->IsHostileTo(target);
                    }

                    if (target_hostile)
                        return SPELL_FAILED_BAD_TARGETS;
                }
                else
                {
                    if (!target_friendly_checked)
                    {
                        target_friendly_checked = true;
                        target_friendly = m_caster->IsFriendlyTo(target);
                    }

                    if (target_friendly)
                        return SPELL_FAILED_BAD_TARGETS;
                }
            }
        }

        if (IsPositiveSpell(m_spellInfo->Id, m_caster, target))
            if (target->IsImmuneToSpell(m_spellInfo, target == m_caster) && !target->hasUnitState(UNIT_STAT_ISOLATED))
                return SPELL_FAILED_TARGET_AURASTATE;

        // Must be behind the target.
        if (m_spellInfo->AttributesEx2 == SPELL_ATTR_EX2_FACING_TARGETS_BACK && m_spellInfo->HasAttribute(SPELL_ATTR_EX_FACING_TARGET) && target->HasInArc(m_caster))
        {
            // Exclusion for Pounce: Facing Limitation was removed in 2.0.1, but it still uses the same, old Ex-Flags
            // Exclusion for Mutilate:Facing Limitation was removed in 2.0.1 and 3.0.3, but they still use the same, old Ex-Flags
            // Exclusion for Throw: Facing limitation was added in 3.2.x, but that shouldn't be
            if (!m_spellInfo->IsFitToFamily(SPELLFAMILY_DRUID, uint64(0x0000000000020000)) &&
                    !m_spellInfo->IsFitToFamily(SPELLFAMILY_ROGUE, uint64(0x0020000000000000)) &&
                    m_spellInfo->Id != 2764)
            {
                SendInterrupted(2);
                return SPELL_FAILED_NOT_BEHIND;
            }
        }

        // Caster must be facing the targets front
        if (((m_spellInfo->Attributes == (SPELL_ATTR_ABILITY | SPELL_ATTR_NOT_SHAPESHIFT | SPELL_ATTR_DONT_AFFECT_SHEATH_STATE | SPELL_ATTR_STOP_ATTACK_TARGET)) && !m_caster->IsFacingTargetsFront(target))
            // Caster must be facing the target!
            || (m_spellInfo->HasAttribute(SPELL_ATTR_EX_FACING_TARGET) && !m_caster->HasInArc(target)))
        {
            SendInterrupted(2);
            return SPELL_FAILED_NOT_INFRONT;
        }

        // check if target is in combat
        if (non_caster_target && m_spellInfo->HasAttribute(SPELL_ATTR_EX_NOT_IN_COMBAT_TARGET) && target->IsInCombat())
            return SPELL_FAILED_TARGET_AFFECTING_COMBAT;

        // check if target is affected by Spirit of Redemption (Aura: 27827)
        if (target->HasAuraType(SPELL_AURA_SPIRIT_OF_REDEMPTION))
            return SPELL_FAILED_BAD_TARGETS;

        if (m_spellInfo->MaxTargetLevel && target->GetLevel() > m_spellInfo->MaxTargetLevel)
            return SPELL_FAILED_HIGHLEVEL;
    }
    // zone check
    uint32 zone, area;
    m_caster->GetZoneAndAreaId(zone, area);

    SpellCastResult locRes = sSpellMgr.GetSpellAllowedInLocationError(m_spellInfo, m_caster->GetMapId(), zone, area,
                             m_caster->GetBeneficiaryPlayer());
    if (locRes != SPELL_CAST_OK)
    {
        if (!IsSpellDoNotReportFailure(m_spellInfo))
            return locRes;
        else
            return SPELL_FAILED_DONT_REPORT;
    }
    

    // not let players cast spells at mount (and let do it to creatures)
    if (m_caster->IsMounted() && m_caster->GetTypeId() == TYPEID_PLAYER && !m_IsTriggeredSpell &&
            !IsPassiveSpell(m_spellInfo) && !m_spellInfo->HasAttribute(SPELL_ATTR_CASTABLE_WHILE_MOUNTED))
    {
        if (m_caster->IsTaxiFlying())
            return SPELL_FAILED_NOT_ON_TAXI;
        else
            return SPELL_FAILED_NOT_MOUNTED;
    }

    // always (except passive spells) check items
    if (!IsPassiveSpell(m_spellInfo))
    {
        SpellCastResult castResult = CheckItems();
        if (castResult != SPELL_CAST_OK)
            return castResult;
    }

    // check spell focus object
    if (m_spellInfo->RequiresSpellFocus)
    {
        GameObject* ok = nullptr;
        MaNGOS::GameObjectFocusCheck go_check(m_caster, m_spellInfo->RequiresSpellFocus);
        MaNGOS::GameObjectSearcher<MaNGOS::GameObjectFocusCheck> checker(ok, go_check);
        Cell::VisitGridObjects(m_caster, checker, m_caster->GetMap()->GetVisibilityDistance());

        if (!ok)
            return SPELL_FAILED_REQUIRES_SPELL_FOCUS;

        focusObject = ok;                                   // game object found in range
    }

    WorldObject* worldObject = m_caster->GetMap()->GetWorldObject(m_originalCasterGUID); // will be maybe needed in more places where we calculate distance

    // Database based targets from spell_target_script
    if (m_UniqueTargetInfo.empty())                         // skip second CheckCast apply (for delayed spells for example)
    {
        for (int j = 0; j < MAX_EFFECT_INDEX; ++j)
        {
            if (m_spellInfo->EffectImplicitTargetA[j] == TARGET_UNIT_SCRIPT_NEAR_CASTER ||
                m_spellInfo->EffectImplicitTargetB[j] == TARGET_UNIT_SCRIPT_NEAR_CASTER ||
                m_spellInfo->EffectImplicitTargetA[j] == TARGET_LOCATION_SCRIPT_NEAR_CASTER ||
                m_spellInfo->EffectImplicitTargetB[j] == TARGET_LOCATION_SCRIPT_NEAR_CASTER ||
                m_spellInfo->EffectImplicitTargetA[j] == TARGET_GAMEOBJECT_SCRIPT_NEAR_CASTER ||
                m_spellInfo->EffectImplicitTargetB[j] == TARGET_GAMEOBJECT_SCRIPT_NEAR_CASTER)
            {
                SQLMultiStorage::SQLMSIteratorBounds<SpellTargetEntry> bounds = sSpellScriptTargetStorage.getBounds<SpellTargetEntry>(m_spellInfo->Id);

                if (bounds.first == bounds.second)
                {
                    if (m_spellInfo->EffectImplicitTargetA[j] == TARGET_UNIT_SCRIPT_NEAR_CASTER || m_spellInfo->EffectImplicitTargetB[j] == TARGET_UNIT_SCRIPT_NEAR_CASTER)
                        sLog.outErrorDb("Spell entry %u, effect %i has EffectImplicitTargetA/EffectImplicitTargetB = TARGET_UNIT_SCRIPT_NEAR_CASTER , but creature are not defined in `spell_script_target`", m_spellInfo->Id, j);

                    if (m_spellInfo->EffectImplicitTargetA[j] == TARGET_LOCATION_SCRIPT_NEAR_CASTER || m_spellInfo->EffectImplicitTargetB[j] == TARGET_LOCATION_SCRIPT_NEAR_CASTER)
                        sLog.outErrorDb("Spell entry %u, effect %i has EffectImplicitTargetA/EffectImplicitTargetB = TARGET_LOCATION_SCRIPT_NEAR_CASTER , but gameobject or creature are not defined in `spell_script_target`", m_spellInfo->Id, j);

                    if (m_spellInfo->EffectImplicitTargetA[j] == TARGET_GAMEOBJECT_SCRIPT_NEAR_CASTER || m_spellInfo->EffectImplicitTargetB[j] == TARGET_GAMEOBJECT_SCRIPT_NEAR_CASTER)
                        sLog.outErrorDb("Spell entry %u, effect %i has EffectImplicitTargetA/EffectImplicitTargetB = TARGET_GAMEOBJECT_SCRIPT_NEAR_CASTER , but gameobject are not defined in `spell_script_target`", m_spellInfo->Id, j);
                }

                SpellRangeEntry const* srange = sSpellRangeStore.LookupEntry(m_spellInfo->rangeIndex);
                float range = GetSpellMaxRange(srange);

                // override range with default when it's not provided
                if (!range)
                    range = m_caster->GetMap()->IsDungeon() ? DEFAULT_VISIBILITY_INSTANCE : DEFAULT_VISIBILITY_DISTANCE;

                Creature* targetExplicit = nullptr;            // used for cases where a target is provided (by script for example)
                Creature* creatureScriptTarget = nullptr;
                GameObject* goScriptTarget = nullptr;
                bool foundButOutOfRange = false;

                for (SQLMultiStorage::SQLMultiSIterator<SpellTargetEntry> i_spellST = bounds.first; i_spellST != bounds.second; ++i_spellST)
                {
                    if (i_spellST->CanNotHitWithSpellEffect(SpellEffectIndex(j)))
                        continue;

                    switch (i_spellST->type)
                    {
                        case SPELL_TARGET_TYPE_GAMEOBJECT:
                        {
                            GameObject* p_GameObject = nullptr;

                            if (i_spellST->targetEntry)
                            {
                                MaNGOS::NearestGameObjectEntryInObjectRangeCheck go_check(*m_caster, i_spellST->targetEntry, range);
                                MaNGOS::GameObjectLastSearcher<MaNGOS::NearestGameObjectEntryInObjectRangeCheck> checker(p_GameObject, go_check);
                                Cell::VisitGridObjects(m_caster, checker, range);

                                if (p_GameObject)
                                {
                                    // remember found target and range, next attempt will find more near target with another entry
                                    creatureScriptTarget = nullptr;
                                    goScriptTarget = p_GameObject;
                                    range = go_check.GetLastRange();
                                }
                            }
                            else if (focusObject)           // Focus Object
                            {
                                float frange = m_caster->GetDistance(focusObject);
                                if (range >= frange)
                                {
                                    creatureScriptTarget = nullptr;
                                    goScriptTarget = focusObject;
                                    range = frange;
                                }
                            }
                            break;
                        }
                        case SPELL_TARGET_TYPE_CREATURE:
                        case SPELL_TARGET_TYPE_DEAD:
                        default:
                        {
                            Creature* p_Creature = nullptr;

                            // check if explicit target is provided and check it up against database valid target entry/state
                            if (Unit* pTarget = m_targets.getUnitTarget())
                            {
                                if (pTarget->GetTypeId() == TYPEID_UNIT && pTarget->GetEntry() == i_spellST->targetEntry)
                                {
                                    if ((i_spellST->type == SPELL_TARGET_TYPE_CREATURE && pTarget->IsAlive()) ||
                                        (i_spellST->type == SPELL_TARGET_TYPE_DEAD && ((Creature*)pTarget)->IsCorpse()))
                                    {
                                        // always use spellMaxRange, in case GetLastRange returned different in a previous pass
                                        WorldObject* searcher = (worldObject && (worldObject->GetTypeId() == TYPEID_GAMEOBJECT || worldObject->GetTypeId() == TYPEID_DYNAMICOBJECT)) ? worldObject : m_caster;
                                        if (pTarget->IsWithinDistInMap(searcher, GetSpellMaxRange(srange)))
                                            targetExplicit = (Creature*)pTarget;
                                        else
                                            foundButOutOfRange = true;
                                    }
                                }
                            }

                            // no target provided or it was not valid, so use closest in range
                            if (!targetExplicit)
                            {
                                WorldObject* objectForSearch = (worldObject && (worldObject->GetTypeId() == TYPEID_GAMEOBJECT || worldObject->GetTypeId() == TYPEID_DYNAMICOBJECT)) ? worldObject : m_caster;
                                MaNGOS::NearestCreatureEntryWithLiveStateInObjectRangeCheck u_check(*objectForSearch, i_spellST->targetEntry, i_spellST->type != SPELL_TARGET_TYPE_DEAD, i_spellST->type == SPELL_TARGET_TYPE_DEAD, range, false, m_caster);
                                MaNGOS::CreatureLastSearcher<MaNGOS::NearestCreatureEntryWithLiveStateInObjectRangeCheck> searcher(p_Creature, u_check);

                                // Visit all, need to find also Pet* objects
                                Cell::VisitAllObjects(objectForSearch, searcher, range);

                                range = u_check.GetLastRange();
                                if (u_check.FoundOutOfRange())
                                    foundButOutOfRange = true;
                            }

                            // always prefer provided target if it's valid
                            if (targetExplicit)
                                creatureScriptTarget = targetExplicit;
                            else if (p_Creature)
                                creatureScriptTarget = p_Creature;

                            if (creatureScriptTarget)
                                goScriptTarget = nullptr;

                            break;
                        }
                    }
                }

                if (creatureScriptTarget)
                {
                    // store coordinates for TARGET_LOCATION_SCRIPT_NEAR_CASTER
                    if (m_spellInfo->EffectImplicitTargetA[j] == TARGET_LOCATION_SCRIPT_NEAR_CASTER ||
                        m_spellInfo->EffectImplicitTargetB[j] == TARGET_LOCATION_SCRIPT_NEAR_CASTER)
                    {
                        m_targets.setDestination(creatureScriptTarget->GetPositionX(), creatureScriptTarget->GetPositionY(), creatureScriptTarget->GetPositionZ());

                        if (m_spellInfo->EffectImplicitTargetA[j] == TARGET_LOCATION_SCRIPT_NEAR_CASTER && m_spellInfo->Effect[j] != SPELL_EFFECT_PERSISTENT_AREA_AURA)
                            AddUnitTarget(creatureScriptTarget, SpellEffectIndex(j));
                    }
                    // store explicit target for TARGET_UNIT_SCRIPT_NEAR_CASTER
                    else
                    {
                        if (m_spellInfo->EffectImplicitTargetA[j] == TARGET_UNIT_SCRIPT_NEAR_CASTER ||
                            m_spellInfo->EffectImplicitTargetB[j] == TARGET_UNIT_SCRIPT_NEAR_CASTER)
                            AddUnitTarget(creatureScriptTarget, SpellEffectIndex(j));
                    }
                }
                else if (goScriptTarget)
                {
                    // store coordinates for TARGET_LOCATION_SCRIPT_NEAR_CASTER
                    if (m_spellInfo->EffectImplicitTargetA[j] == TARGET_LOCATION_SCRIPT_NEAR_CASTER ||
                        m_spellInfo->EffectImplicitTargetB[j] == TARGET_LOCATION_SCRIPT_NEAR_CASTER)
                    {
                        m_targets.setDestination(goScriptTarget->GetPositionX(), goScriptTarget->GetPositionY(), goScriptTarget->GetPositionZ());

                        if (m_spellInfo->EffectImplicitTargetA[j] == TARGET_LOCATION_SCRIPT_NEAR_CASTER && m_spellInfo->Effect[j] != SPELL_EFFECT_PERSISTENT_AREA_AURA)
                            AddGOTarget(goScriptTarget, SpellEffectIndex(j));
                    }
                    // store explicit target for TARGET_GAMEOBJECT_SCRIPT_NEAR_CASTER
                    else
                    {
                        if (m_spellInfo->EffectImplicitTargetA[j] == TARGET_GAMEOBJECT_SCRIPT_NEAR_CASTER ||
                            m_spellInfo->EffectImplicitTargetB[j] == TARGET_GAMEOBJECT_SCRIPT_NEAR_CASTER)
                            AddGOTarget(goScriptTarget, SpellEffectIndex(j));
                    }
                }
                // Missing DB Entry or targets for this spellEffect.
                else
                {
                    /* For TARGET_GAMEOBJECT_SCRIPT_NEAR_CASTER makes DB targets optional not required for now
                     * TODO: Makes more research for this target type
                     */
                    if (m_spellInfo->EffectImplicitTargetA[j] != TARGET_GAMEOBJECT_SCRIPT_NEAR_CASTER)
                    {
                        // not report target not existence for triggered spells
                        if (m_triggeredByAuraSpell || m_IsTriggeredSpell)
                            return SPELL_FAILED_DONT_REPORT;
                        else
                            return foundButOutOfRange ? SPELL_FAILED_OUT_OF_RANGE : SPELL_FAILED_BAD_TARGETS;
                    }
                }
            }
        }
    }

    if (!m_IsTriggeredSpell)
    {
        SpellCastResult castResult = CheckRange(strict);
        if (castResult != SPELL_CAST_OK)
            return castResult;
    }

    {
        SpellCastResult castResult = CheckPower();
        if (castResult != SPELL_CAST_OK)
            return castResult;
    }

    if (!m_IsTriggeredSpell)                                // triggered spell not affected by stun/etc
    {
        SpellCastResult castResult = CheckCasterAuras();
        if (castResult != SPELL_CAST_OK)
            return castResult;
    }

    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        // for effects of spells that have only one target
        switch(m_spellInfo->Effect[i])
        {
            case SPELL_EFFECT_INSTAKILL:
                // Death Pact
                if (m_spellInfo->Id == 48743)
                {
                    if (m_caster->GetTypeId() != TYPEID_PLAYER)
                        return SPELL_FAILED_ERROR;

                    if (!((Player*)m_caster)->GetSelectionGuid())
                        return SPELL_FAILED_BAD_IMPLICIT_TARGETS;
                    Pet* target = m_caster->GetMap()->GetPet(((Player*)m_caster)->GetSelectionGuid());

                    // alive
                    if (!target || target->IsDead())
                        return SPELL_FAILED_BAD_IMPLICIT_TARGETS;
                    // undead
                    if (target->GetCreatureType() != CREATURE_TYPE_UNDEAD)
                        return SPELL_FAILED_BAD_IMPLICIT_TARGETS;
                    // owned
                    if (target->GetOwnerGuid() != m_caster->GetObjectGuid())
                        return SPELL_FAILED_BAD_IMPLICIT_TARGETS;

                    float dist = GetSpellRadius(sSpellRadiusStore.LookupEntry(m_spellInfo->EffectRadiusIndex[i]));
                    if (!target->IsWithinDistInMap(m_caster,dist))
                        return SPELL_FAILED_OUT_OF_RANGE;

                    // will set in target selection code
                }
                break;
            case SPELL_EFFECT_DUMMY:
            {
                // By Spell ID
                if (m_spellInfo->Id == 19938)               // Awaken Lazy Peon
                {
                    Unit* target = m_targets.getUnitTarget();
                    // 17743 = Lazy Peon Sleep | 10556 = Lazy Peon
                    if (!target || !target->HasAura(17743) || target->GetEntry() != 10556)
                        return SPELL_FAILED_BAD_TARGETS;
                }
                else if (m_spellInfo->Id == 51582)          // Rocket Boots Engaged
                {
                    if (m_caster->IsInWater())
                        return SPELL_FAILED_ONLY_ABOVEWATER;
                }
                else if (m_spellInfo->Id == 51690)          // Killing Spree
                {
                    UnitList targets;

                    float radius = GetSpellMaxRange(sSpellRangeStore.LookupEntry(m_spellInfo->rangeIndex));

                    MaNGOS::AnyUnfriendlyVisibleUnitInObjectRangeCheck unitCheck(m_caster, m_caster, radius);
                    MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyVisibleUnitInObjectRangeCheck> checker(targets, unitCheck);
                    Cell::VisitAllObjects(m_caster, checker, radius);

                    if (targets.empty())
                        return SPELL_FAILED_OUT_OF_RANGE;
                }
                else if (m_spellInfo->Id == 68996)          // Two forms
                {
                    if (m_caster->IsInCombat())
                        return SPELL_FAILED_AFFECTING_COMBAT;
                }
                else if (m_spellInfo->SpellIconID == 156)   // Holy Shock
                {
                    Unit* target = m_targets.getUnitTarget();
                    // spell different for friends and enemies
                    // hart version required facing
                    if (target && !(m_caster->IsFriendlyTo(target) || m_caster->HasInArc(target)))
                        return SPELL_FAILED_UNIT_NOT_INFRONT;
                }
                // Fire Nova
                if (m_spellInfo->SpellFamilyName == SPELLFAMILY_SHAMAN && m_spellInfo->SpellIconID == 33)
                {
                    // fire totems slot
                    if (!m_caster->GetTotemGuid(TOTEM_SLOT_FIRE))
                        return SPELL_FAILED_TOTEMS;
                }
                break;
            }
            case SPELL_EFFECT_DISTRACT:                     // All nearby enemies must not be in combat
            {
                if (m_targets.m_targetMask & (TARGET_FLAG_DEST_LOCATION | TARGET_FLAG_SOURCE_LOCATION))
                {
                    UnitList targetsCombat;
                    float radius = GetSpellRadius(sSpellRadiusStore.LookupEntry(m_spellInfo->EffectRadiusIndex[i]));

                    FillAreaTargets(targetsCombat, radius, PUSH_DEST_CENTER, SPELL_TARGETS_AOE_DAMAGE);

                    if (targetsCombat.empty())
                        break;

                    for (UnitList::iterator itr = targetsCombat.begin(); itr != targetsCombat.end(); ++itr)
                        if ((*itr)->IsInCombat())
                            return SPELL_FAILED_TARGET_IN_COMBAT;
                }
                break;
            }
            case SPELL_EFFECT_SCHOOL_DAMAGE:
            {
                // Hammer of Wrath
                if (m_spellInfo->SpellVisual[0] == 7250)
                {
                    if (!m_targets.getUnitTarget())
                        return SPELL_FAILED_BAD_IMPLICIT_TARGETS;

                    if (m_targets.getUnitTarget()->GetHealth() > m_targets.getUnitTarget()->GetMaxHealth() * 0.2)
                        return SPELL_FAILED_BAD_TARGETS;
                }
                break;
            }
            case SPELL_EFFECT_TAMECREATURE:
            {
                // Spell can be triggered, we need to check original caster prior to caster
                Unit* caster = GetAffectiveCaster();
                if (!caster || caster->GetTypeId() != TYPEID_PLAYER ||
                        !m_targets.getUnitTarget() ||
                        m_targets.getUnitTarget()->GetTypeId() == TYPEID_PLAYER)
                    return SPELL_FAILED_BAD_TARGETS;

                Player* plrCaster = (Player*)caster;

                bool gmmode = m_triggeredBySpellInfo == nullptr;

                if (gmmode && !ChatHandler(plrCaster).FindCommand("npc tame"))
                {
                    plrCaster->SendPetTameFailure(PETTAME_UNKNOWNERROR);
                    return SPELL_FAILED_DONT_REPORT;
                }

                if (plrCaster->getClass() != CLASS_HUNTER && !gmmode)
                {
                    plrCaster->SendPetTameFailure(PETTAME_UNITSCANTTAME);
                    return SPELL_FAILED_DONT_REPORT;
                }

                Creature* target = (Creature*)m_targets.getUnitTarget();

                if (target->IsPet() || target->IsCharmed())
                {
                    plrCaster->SendPetTameFailure(PETTAME_CREATUREALREADYOWNED);
                    return SPELL_FAILED_DONT_REPORT;
                }

                if (target->GetLevel() > plrCaster->GetLevel() && !gmmode)
                {
                    plrCaster->SendPetTameFailure(PETTAME_TOOHIGHLEVEL);
                    return SPELL_FAILED_DONT_REPORT;
                }

                if (target->GetCreatureInfo()->IsExotic() && !plrCaster->CanTameExoticPets() && !gmmode)
                {
                    plrCaster->SendPetTameFailure(PETTAME_CANTCONTROLEXOTIC);
                    return SPELL_FAILED_DONT_REPORT;
                }

                if (!target->GetCreatureInfo()->isTameable(plrCaster->CanTameExoticPets()))
                {
                    plrCaster->SendPetTameFailure(PETTAME_NOTTAMEABLE);
                    return SPELL_FAILED_DONT_REPORT;
                }

                if (plrCaster->GetPetGuid() || plrCaster->GetCharmGuid())
                {
                    plrCaster->SendPetTameFailure(PETTAME_ANOTHERSUMMONACTIVE);
                    return SPELL_FAILED_DONT_REPORT;
                }

                break;
            }
            case SPELL_EFFECT_LEARN_SPELL:
            {
                if(m_spellInfo->EffectImplicitTargetA[i] != TARGET_UNIT_CASTER_PET)
                    break;

                Pet* pet = m_caster->GetPet();

                if (!pet)
                    return SPELL_FAILED_NO_PET;

                SpellEntry const *learn_spellproto = sSpellTemplate.LookupEntry<SpellEntry>(m_spellInfo->EffectTriggerSpell[i]);

                if (!learn_spellproto)
                    return SPELL_FAILED_NOT_KNOWN;

                if(m_spellInfo->SpellLevel > pet->GetLevel())
                    return SPELL_FAILED_LOWLEVEL;

                break;
            }
            case SPELL_EFFECT_LEARN_PET_SPELL:
            {
                Pet* pet = m_caster->GetPet();

                if (!pet)
                    return SPELL_FAILED_NO_PET;

                SpellEntry const *learn_spellproto = sSpellTemplate.LookupEntry<SpellEntry>(m_spellInfo->EffectTriggerSpell[i]);
                if (!learn_spellproto)
                    return SPELL_FAILED_NOT_KNOWN;

                if(m_spellInfo->SpellLevel > pet->GetLevel())
                    return SPELL_FAILED_LOWLEVEL;

                break;
            }
            case SPELL_EFFECT_APPLY_GLYPH:
            {
                uint32 glyphId = m_spellInfo->EffectMiscValue[i];
                if(GlyphPropertiesEntry const *gp = sGlyphPropertiesStore.LookupEntry(glyphId))
                    if(m_caster->HasAura(gp->SpellId))
                        return SPELL_FAILED_UNIQUE_GLYPH;
                break;
            }
            case SPELL_EFFECT_FEED_PET:
            {
                if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    return SPELL_FAILED_BAD_TARGETS;

                Item* foodItem = m_targets.getItemTarget();
                if (!foodItem)
                    return SPELL_FAILED_BAD_TARGETS;

                Pet* pet = m_caster->GetPet();

                if (!pet)
                    return SPELL_FAILED_NO_PET;

                if (!pet->HaveInDiet(foodItem->GetProto()))
                    return SPELL_FAILED_WRONG_PET_FOOD;

                if (!pet->GetCurrentFoodBenefitLevel(foodItem->GetProto()->ItemLevel))
                    return SPELL_FAILED_FOOD_LOWLEVEL;

                if (pet->IsInCombat())
                    return SPELL_FAILED_AFFECTING_COMBAT;

                break;
            }
            case SPELL_EFFECT_POWER_BURN:
            case SPELL_EFFECT_POWER_DRAIN:
            {
                // Can be area effect, Check only for players and not check if target - caster (spell can have multiply drain/burn effects)
                if (m_caster->GetTypeId() == TYPEID_PLAYER)
                    if (Unit* target = m_targets.getUnitTarget())
                        if (target != m_caster && int32(target->GetPowerType()) != m_spellInfo->EffectMiscValue[i])
                            return SPELL_FAILED_BAD_TARGETS;
                break;
            }
            case SPELL_EFFECT_CHARGE:
            {
                if (m_caster->hasUnitState(UNIT_STAT_ROOT))
                    return SPELL_FAILED_ROOTED;

                break;
            }
            case SPELL_EFFECT_SKINNING:
            {
                if (m_caster->GetTypeId() != TYPEID_PLAYER || !m_targets.getUnitTarget() || m_targets.getUnitTarget()->GetTypeId() != TYPEID_UNIT)
                    return SPELL_FAILED_BAD_TARGETS;

                if (!m_targets.getUnitTarget()->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SKINNABLE))
                    return SPELL_FAILED_TARGET_UNSKINNABLE;

                Creature* creature = (Creature*)m_targets.getUnitTarget();

                if (creature->IsAlive())
                    return SPELL_FAILED_TARGET_NOT_DEAD;

                if (creature->GetLootStatus() != CREATURE_LOOT_STATUS_LOOTED)// || creature->GetCreatureType() != CREATURE_TYPE_CRITTER)
                    return SPELL_FAILED_TARGET_NOT_LOOTED;

                uint32 skill = creature->GetCreatureInfo()->GetRequiredLootSkill();

                int32 skillValue = ((Player*)m_caster)->GetSkillValue(skill);
                int32 TarGetLevel = m_targets.getUnitTarget()->GetLevel();
                int32 ReqValue = (skillValue < 100 ? (TarGetLevel - 10) * 10 : TarGetLevel * 5);
                if (ReqValue > skillValue)
                    return SPELL_FAILED_LOW_CASTLEVEL;

                break;
            }
            case SPELL_EFFECT_OPEN_LOCK:
            {
                if (m_caster->GetTypeId() != TYPEID_PLAYER) // only players can open locks, gather etc.
                    return SPELL_FAILED_BAD_TARGETS;

                // we need a go target in case of TARGET_GAMEOBJECT (for other targets acceptable GO and items)
                if (m_spellInfo->EffectImplicitTargetA[i] == TARGET_GAMEOBJECT)
                {
                    if (!m_targets.getGOTarget())
                        return SPELL_FAILED_BAD_TARGETS;
                }

                // get the lock entry
                uint32 lockId;
                if (GameObject* go = m_targets.getGOTarget())
                {
                    // In BattleGround players can use only flags and banners
                    if (((Player*)m_caster)->InBattleGround() &&
                            !((Player*)m_caster)->CanUseBattleGroundObject())
                        return SPELL_FAILED_TRY_AGAIN;

                    lockId = go->GetGOInfo()->GetLockId();
                    if (!lockId)
                        return SPELL_FAILED_ALREADY_OPEN;

                    // check if its in use only when cast is finished (called from spell::cast() with strict = false)
                    if (!strict && go->IsInUse())
                        return SPELL_FAILED_CHEST_IN_USE;
                }
                else if (Item* item = m_targets.getItemTarget())
                {
                    // not own (trade?)
                    if (item->GetOwner() != m_caster)
                        return SPELL_FAILED_ITEM_GONE;

                    lockId = item->GetProto()->LockID;

                    // if already unlocked
                    if (!lockId || item->HasFlag(ITEM_FIELD_FLAGS, ITEM_DYNFLAG_UNLOCKED))
                        return SPELL_FAILED_ALREADY_OPEN;
                }
                else
                    return SPELL_FAILED_BAD_TARGETS;

                SkillType skillId = SKILL_NONE;
                int32 reqSkillValue = 0;
                int32 skillValue = 0;

                // check lock compatibility
                SpellCastResult res = CanOpenLock(SpellEffectIndex(i), lockId, skillId, reqSkillValue, skillValue);
                if (res != SPELL_CAST_OK)
                    return res;

                // Failing gathering attempt for mining and herbalism was removed in patch 3.1.0: http://wowwiki.wikia.com/wiki/Patch_3.1.0
                // chance for fail at orange LockPicking gathering attempt
                // second check prevent fail at rechecks
                if (!strict && skillId != SKILL_NONE && skillId != SKILL_HERBALISM && skillId != SKILL_MINING )
                {
                    bool canFailAtMax = skillId != SKILL_HERBALISM && skillId != SKILL_MINING;

                    // chance for failure in orange gather / lockpick (gathering skill can't fail at maxskill)
                    if ((canFailAtMax || skillValue < sWorld.GetConfigMaxSkillValue()) && reqSkillValue > irand(skillValue - 25, skillValue + 37))
                        return SPELL_FAILED_TRY_AGAIN;
                }
                break;
            }
            case SPELL_EFFECT_SUMMON_DEAD_PET:
            {
                Creature* pet = m_caster->GetPet();
                if (!pet)
                {
                    SpellCastResult result = Pet::TryLoadFromDB(m_caster);
                    if (result == SPELL_FAILED_NO_PET)
                        return SPELL_FAILED_NO_PET;

                    if (result == SPELL_CAST_OK)
                    {
                        ((Player*)m_caster)->SendPetTameFailure(PETTAME_NOTDEAD);
                        return SPELL_FAILED_DONT_REPORT;
                    }

                    if (result != SPELL_FAILED_TARGETS_DEAD)
                        return SPELL_FAILED_UNKNOWN;
                }
                else if (pet->IsAlive())
                    return SPELL_FAILED_ALREADY_HAVE_SUMMON;

                break;
            }
            // This is generic summon effect
            case SPELL_EFFECT_SUMMON:
            {
                if (SummonPropertiesEntry const *summon_prop = sSummonPropertiesStore.LookupEntry(m_spellInfo->EffectMiscValueB[i]))
                {
                    if (summon_prop->Group == SUMMON_PROP_GROUP_PETS)
                    {
                        if (m_caster->GetPetGuid())
                            return SPELL_FAILED_ALREADY_HAVE_SUMMON;

                        if (m_caster->GetCharmGuid())
                            return SPELL_FAILED_ALREADY_HAVE_CHARM;
                    }
                }

                break;
            }
            case SPELL_EFFECT_SUMMON_PET:
            {
                if (m_caster->GetCharmGuid())
                    return SPELL_FAILED_ALREADY_HAVE_CHARM;

                uint32 plClass = m_caster->getClass();
                if (plClass == CLASS_HUNTER)
                {
                    if (Creature* pet = m_caster->GetPet())
                    {
                        if (!pet->IsAlive())
                        {
                            ((Player*)m_caster)->SendPetTameFailure(PETTAME_DEAD);
                            return SPELL_FAILED_DONT_REPORT;
                        }
                        else
                            return SPELL_FAILED_ALREADY_HAVE_SUMMON;
                    }
                    else
                    {
                        SpellCastResult result = Pet::TryLoadFromDB(m_caster);
                        if (result == SPELL_FAILED_TARGETS_DEAD)
                        {
                            ((Player*)m_caster)->SendPetTameFailure(PETTAME_DEAD);
                            return SPELL_FAILED_DONT_REPORT;
                        }
                        else if (result != SPELL_CAST_OK)
                            return result;
                    }
                }
                else if (m_caster->GetPetGuid())
                {
                    if (plClass == CLASS_WARLOCK)                  // let warlock do a replacement summon
                    {
                        if (strict)     // Summoning Disorientation, trigger pet stun (cast by pet so it doesn't attack player)
                            if (Pet* pet = ((Player*)m_caster)->GetPet())
                                pet->CastSpell(pet, 32752, TRIGGERED_OLD_TRIGGERED, nullptr, nullptr, pet->GetObjectGuid());
                    }
                    else
                        return SPELL_FAILED_ALREADY_HAVE_SUMMON;
                }

                break;
            }
            case SPELL_EFFECT_SUMMON_PLAYER:
            {
                if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    return SPELL_FAILED_BAD_TARGETS;
                if (!((Player*)m_caster)->GetSelectionGuid())
                    return SPELL_FAILED_BAD_TARGETS;

                Player* target = sObjectMgr.GetPlayer(((Player*)m_caster)->GetSelectionGuid());
                if (!target || ((Player*)m_caster) == target || !target->IsInSameRaidWith((Player*)m_caster))
                    return SPELL_FAILED_BAD_TARGETS;

                // check if our map is dungeon
                if (sMapStore.LookupEntry(m_caster->GetMapId())->IsDungeon())
                {
                    InstanceTemplate const* instance = ObjectMgr::GetInstanceTemplate(m_caster->GetMapId());
                    if (!instance)
                        return SPELL_FAILED_TARGET_NOT_IN_INSTANCE;
                    if (instance->levelMin > target->GetLevel())
                        return SPELL_FAILED_LOWLEVEL;
                    if (instance->levelMax && instance->levelMax < target->GetLevel())
                        return SPELL_FAILED_HIGHLEVEL;
                }
                break;
            }
            case SPELL_EFFECT_RESURRECT:
            case SPELL_EFFECT_RESURRECT_NEW:
            {
                if (m_targets.m_targetMask & (TARGET_FLAG_CORPSE_ALLY | TARGET_FLAG_PVP_CORPSE))
                {
                    if (Corpse* corpse = m_caster->GetMap()->GetCorpse(m_targets.getCorpseTargetGuid()))
                    {
                        if (Player* owner = ObjectAccessor::FindPlayer(corpse->GetOwnerGuid()))
                        {
                            if (owner->HasAuraType(SPELL_AURA_PREVENT_RESURRECTION))
                                return SPELL_FAILED_TARGET_CANNOT_BE_RESURRECTED;
                        }
                        else
                            return SPELL_FAILED_TARGET_CANNOT_BE_RESURRECTED;
                    }
                    else
                        return SPELL_FAILED_TARGET_CANNOT_BE_RESURRECTED;
                }
                else if (m_targets.m_targetMask & TARGET_FLAG_UNIT)
                {
                    Unit* target = m_targets.getUnitTarget();
                    if (!target || target->HasAuraType(SPELL_AURA_PREVENT_RESURRECTION))
                        return SPELL_FAILED_TARGET_CANNOT_BE_RESURRECTED;
                }
                else
                    return SPELL_FAILED_TARGET_CANNOT_BE_RESURRECTED;

                break;
            }
            case SPELL_EFFECT_SELF_RESURRECT:
            {
                if (m_caster->HasAuraType(SPELL_AURA_PREVENT_RESURRECTION))
                    return SPELL_FAILED_TARGET_CANNOT_BE_RESURRECTED;

                break;
            }
            case SPELL_EFFECT_LEAP:
            case SPELL_EFFECT_TELEPORT_UNITS_FACE_CASTER:
            {
                if (!m_caster || m_caster->IsTaxiFlying())
                    return SPELL_FAILED_NOT_ON_TAXI;

                // Blink has leap first and then removing of auras with root effect
                // need further research with this
                if (m_spellInfo->Effect[i] != SPELL_EFFECT_LEAP)
                {
                    if (m_caster->hasUnitState(UNIT_STAT_ROOT))
                        return SPELL_FAILED_ROOTED;
                }

                if (m_caster->GetTypeId() == TYPEID_PLAYER)
                {
                    // not allow use this effect at battleground until battleground start
                    if (BattleGround const* bg = ((Player*)m_caster)->GetBattleGround())
                        if (bg->GetStatus() != STATUS_IN_PROGRESS)
                            return SPELL_FAILED_TRY_AGAIN;
                }

                break;
            }
            case SPELL_EFFECT_STEAL_BENEFICIAL_BUFF:
            {
                if (m_targets.getUnitTarget() == m_caster)
                    return SPELL_FAILED_BAD_TARGETS;
                break;
            }
            default: break;
        }
    }

    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        // Do not check in case of junk in DBC
        if (!IsAuraApplyEffect(m_spellInfo, SpellEffectIndex(i)))
            continue;

        // Possible Unit-target for the spell
        Unit* expectedTarget = GetPrefilledUnitTargetOrUnitTarget(SpellEffectIndex(i));

        switch (m_spellInfo->EffectApplyAuraName[i])
        {
            case SPELL_AURA_DUMMY:
            {
                // custom check
                switch (m_spellInfo->Id)
                {
                    case 34026:                             // Kill Command
                        if (!m_caster->GetPet())
                            return SPELL_FAILED_NO_PET;
                        break;
                    case 61336:                             // Survival Instincts
                        if (m_caster->GetTypeId() != TYPEID_PLAYER || !((Player*)m_caster)->IsInFeralForm())
                            return SPELL_FAILED_ONLY_SHAPESHIFT;
                        break;
                    default:
                        break;
                }
                break;
            }
            case SPELL_AURA_MOD_POSSESS:
            {
                if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    return SPELL_FAILED_UNKNOWN;

                if (expectedTarget == m_caster)
                    return SPELL_FAILED_BAD_TARGETS;

                if (m_caster->GetCharmGuid())
                    return SPELL_FAILED_ALREADY_HAVE_CHARM;

                if (m_caster->GetCharmerGuid())
                    return SPELL_FAILED_CHARMED;

                if (!expectedTarget)
                    return SPELL_FAILED_BAD_IMPLICIT_TARGETS;

                if (expectedTarget->GetCharmerGuid())
                    return SPELL_FAILED_CHARMED;

                if (int32(expectedTarget->GetLevel()) > CalculateDamage(SpellEffectIndex(i), expectedTarget))
                    return SPELL_FAILED_HIGHLEVEL;

                break;
            }
            case SPELL_AURA_MOD_CHARM:
            {
                if (expectedTarget == m_caster)
                    return SPELL_FAILED_BAD_TARGETS;

                if (m_caster->GetCharmGuid())
                    return SPELL_FAILED_ALREADY_HAVE_CHARM;

                if (m_caster->GetCharmerGuid())
                    return SPELL_FAILED_CHARMED;

                if (!expectedTarget)
                    return SPELL_FAILED_BAD_IMPLICIT_TARGETS;

                if (expectedTarget->GetCharmerGuid())
                    return SPELL_FAILED_CHARMED;

                if (int32(expectedTarget->GetLevel()) > CalculateDamage(SpellEffectIndex(i), expectedTarget))
                    return SPELL_FAILED_HIGHLEVEL;

                break;
            }
            case SPELL_AURA_MOD_POSSESS_PET:
            {
                if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    return SPELL_FAILED_UNKNOWN;

                if (m_caster->GetCharmGuid())
                    return SPELL_FAILED_ALREADY_HAVE_CHARM;

                if (m_caster->GetCharmerGuid())
                    return SPELL_FAILED_CHARMED;

                Pet* pet = m_caster->GetPet();
                if (!pet)
                    return SPELL_FAILED_NO_PET;

                if (pet->GetCharmerGuid())
                    return SPELL_FAILED_CHARMED;

                break;
            }
            case SPELL_AURA_MOUNTED:
            {
                if (m_caster->GetTypeId() == TYPEID_PLAYER && ((Player*)m_caster)->GetTransport())
                    return SPELL_FAILED_NO_MOUNTS_ALLOWED;

                if (m_spellInfo->EffectMiscValueB[i] && !m_caster->GetMountCapability(m_spellInfo->EffectMiscValueB[i]))
                    return SPELL_FAILED_NOT_HERE;

                // Ignore map check if spell have AreaId. AreaId already checked and this prevent special mount spells
                if (m_caster->GetTypeId() == TYPEID_PLAYER &&
                    !m_IsTriggeredSpell &&
                    !m_spellInfo->AreaGroupId &&
                    (m_caster->GetMap() && !m_caster->GetMap()->IsMountAllowed()))
                {
                    return SPELL_FAILED_NO_MOUNTS_ALLOWED;
                }

                if (m_caster->IsInDisallowedMountForm())
                    return SPELL_FAILED_NOT_SHAPESHIFT;

                break;
            }
            case SPELL_AURA_RANGED_ATTACK_POWER_ATTACKER_BONUS:
            {
                if (!expectedTarget)
                    return SPELL_FAILED_BAD_IMPLICIT_TARGETS;

                // can be casted at non-friendly unit or own pet/charm
                if (m_caster->IsFriendlyTo(expectedTarget))
                    return SPELL_FAILED_TARGET_FRIENDLY;

                break;
            }
            case SPELL_AURA_FLY:
            case SPELL_AURA_MOD_FLIGHT_SPEED_MOUNTED:
            {
                // not allow cast fly spells if not have req. skills  (all spells is self target)
                // allow always ghost flight spells
                if (m_caster->GetTypeId() == TYPEID_PLAYER && m_caster->IsAlive())
                {
                    if (!((Player*)m_caster)->CanStartFlyInArea(m_caster->GetMapId(), zone, area))
                        return m_IsTriggeredSpell ? SPELL_FAILED_DONT_REPORT : SPELL_FAILED_NOT_HERE;
                }
                break;
            }
            case SPELL_AURA_PERIODIC_MANA_LEECH:
            {
                if (!expectedTarget)
                    return SPELL_FAILED_BAD_IMPLICIT_TARGETS;

                if (m_caster->GetTypeId() != TYPEID_PLAYER || m_CastItem)
                    break;

                if (expectedTarget->GetPowerType() != POWER_MANA)
                    return SPELL_FAILED_BAD_TARGETS;

                break;
            }
            case SPELL_AURA_CONTROL_VEHICLE:
            {
                if (m_caster->HasAuraType(SPELL_AURA_MOUNTED))
                    return SPELL_FAILED_NOT_MOUNTED;

                if (!expectedTarget || !expectedTarget->IsVehicle())
                    return SPELL_FAILED_BAD_TARGETS;

                // It is possible to change between vehicles that are boarded on each other
                if (m_caster->IsBoarded() && m_caster->GetTransportInfo()->IsOnVehicle())
                {
                    // Check if trying to board a vehicle that is boarded on current transport
                    bool boardedOnEachOther = m_caster->GetTransportInfo()->HasOnBoard(expectedTarget);
                    // Check if trying to board a vehicle that has the current transport on board
                    if (!boardedOnEachOther)
                        boardedOnEachOther = expectedTarget->GetVehicleInfo()->HasOnBoard(m_caster);

                    if (!boardedOnEachOther)
                        return SPELL_FAILED_NOT_ON_TRANSPORT;
                }

                if (!expectedTarget->GetVehicleInfo()->CanBoard(m_caster))
                    return SPELL_FAILED_BAD_TARGETS;

                break;
            }
            case SPELL_AURA_MIRROR_IMAGE:
            {
                if (!expectedTarget)
                    return SPELL_FAILED_BAD_TARGETS;

                // Target must be creature. TODO: Check if target can also be player
                if (expectedTarget->GetTypeId() != TYPEID_UNIT)
                    return SPELL_FAILED_BAD_TARGETS;

                if (expectedTarget == m_caster)             // Clone self can't be accepted
                    return SPELL_FAILED_BAD_TARGETS;

                // It is assumed that target can not be cloned if already cloned by same or other clone auras
                if (expectedTarget->HasAuraType(SPELL_AURA_MIRROR_IMAGE))
                    return SPELL_FAILED_BAD_TARGETS;

                break;
            }
            case SPELL_AURA_WORGEN_TRANSFORM:
            {
                if (!m_caster->HasWorgenForm())
                    return m_IsTriggeredSpell ? SPELL_FAILED_DONT_REPORT : SPELL_FAILED_CANT_DO_THAT_RIGHT_NOW;
                break;
            }
            default:
                break;
        }
    }

    // check trade slot case (last, for allow catch any another cast problems)
    if (m_targets.m_targetMask & TARGET_FLAG_TRADE_ITEM)
    {
        if (m_caster->GetTypeId() != TYPEID_PLAYER)
            return SPELL_FAILED_NOT_TRADING;

        if (TradeSlots(m_targets.getItemTargetGuid().GetRawValue()) != TRADE_SLOT_NONTRADED)
            return SPELL_FAILED_ITEM_NOT_READY;

        // if trade not complete then remember it in trade data
        if (TradeData* my_trade = ((Player*)m_caster)->GetTradeData())
        {
            if (!my_trade->IsInAcceptProcess())
            {
                // Spell will be casted at completing the trade. Silently ignore at this place
                my_trade->SetSpell(m_spellInfo->Id, m_CastItem);
                return SPELL_FAILED_DONT_REPORT;
            }
        }
        else
            return SPELL_FAILED_NOT_TRADING;
    }

    if (m_caster->GetTypeId() == TYPEID_PLAYER && m_spellInfo->HasAttribute(SPELL_ATTR_EX2_TAME_BEAST))
    {
        Player* player = (Player*)m_caster;
        if (player->GetPetGuid() || player->GetCharmGuid())
        {
            player->SendPetTameFailure(PETTAME_ANOTHERSUMMONACTIVE);
            return SPELL_FAILED_DONT_REPORT;
        }
        else
        {
            SpellCastResult result = Pet::TryLoadFromDB((Player*)m_caster);
            if (result == SPELL_FAILED_TARGETS_DEAD || result == SPELL_CAST_OK)
            {
                player->SendPetTameFailure(PETTAME_ANOTHERSUMMONACTIVE);
                return SPELL_FAILED_DONT_REPORT;
            }
        }
    }

    // all ok
    return SPELL_CAST_OK;
}

SpellCastResult Spell::CheckPetCast(Unit* target)
{
    if (!m_caster->IsAlive())
        return SPELL_FAILED_CASTER_DEAD;

    if (m_caster->IsNonMeleeSpellCasted(false) && !m_spellInfo->HasAttribute(SPELL_ATTR_EX4_CAN_CAST_WHILE_CASTING)) // prevent spellcast interruption by another spellcast
        return SPELL_FAILED_SPELL_IN_PROGRESS;
    if (m_caster->IsInCombat() && IsNonCombatSpell(m_spellInfo))
        return SPELL_FAILED_AFFECTING_COMBAT;

    if (m_caster->GetTypeId() == TYPEID_UNIT && (((Creature*)m_caster)->IsPet() || m_caster->IsCharmed()))
    {
        // dead owner (currently only ghouled players can have alive pet casting)
        Unit* charmer = m_caster->GetMaster();
        if (charmer)
        {
            Player* pCharmer = charmer->GetTypeId() == TYPEID_PLAYER ? static_cast<Player*>(charmer) : nullptr;
            if (!charmer->IsAlive() && (!pCharmer || !pCharmer->IsGhouled()))
                return SPELL_FAILED_CASTER_DEAD;
        }

        if (!target && m_targets.getUnitTarget())
            target = m_targets.getUnitTarget();

        bool need = false;
        bool script = false;
        for (unsigned int i : m_spellInfo->EffectImplicitTargetA)
        {
            if (i == TARGET_UNIT_ENEMY ||
                i == TARGET_UNIT_FRIEND ||
                i == TARGET_UNIT_RAID ||
                i == TARGET_UNIT ||
                i == TARGET_UNIT_PARTY ||
                i == TARGET_LOCATION_CASTER_TARGET_POSITION)
            {
                need = true;
                if (!target)
                    return SPELL_FAILED_BAD_IMPLICIT_TARGETS;
                break;
            }
            else if (i == TARGET_LOCATION_SCRIPT_NEAR_CASTER)
            {
                script = true;
                continue;
            }
        }
        if (need)
            m_targets.setUnitTarget(target);
        else if (script == true)
            return CheckCast(true);

        Unit* _target = m_targets.getUnitTarget();

        // for target dead/target not valid
        if (_target && m_targets.m_targetMask & TARGET_FLAG_UNIT)
        {
            if (IsPositiveSpell(m_spellInfo->Id, m_caster, _target))
            {
                if (m_caster->IsHostileTo(_target))
                    return SPELL_FAILED_BAD_TARGETS;
            }
            else
            {
                if (!_target->isTargetableForAttack())
                    return SPELL_FAILED_BAD_TARGETS;            // guessed error

                bool duelvsplayertar = false;
                for (unsigned int j : m_spellInfo->EffectImplicitTargetA)
                {
                    //TARGET_UNIT  is positive AND negative
                    duelvsplayertar |= (j == TARGET_UNIT);
                }
                if (m_caster->IsFriendlyTo(target) && !duelvsplayertar)
                {
                    return SPELL_FAILED_BAD_TARGETS;
                }
            }
        }
        // cooldown
        if (!m_caster->IsSpellReady(*m_spellInfo))
            return SPELL_FAILED_NOT_READY;
    }

    return CheckCast(true);
}

SpellCastResult Spell::CheckCasterAuras() const
{
    if (m_spellInfo->HasAttribute(SPELL_ATTR_EX6_IGNORE_CASTER_AURAS))
        return SPELL_CAST_OK;

    // Flag drop spells totally immuned to caster auras
    // FIXME: find more nice check for all totally immuned spells
    // HasAttribute(SPELL_ATTR_EX3_UNK28) ?
    if (m_spellInfo->Id == 23336 ||                         // Alliance Flag Drop
            m_spellInfo->Id == 23334 ||                     // Horde Flag Drop
            m_spellInfo->Id == 34991)                       // Summon Netherstorm Flag
        return SPELL_CAST_OK;

    uint8 school_immune = 0;
    uint32 mechanic_immune = 0;
    uint32 dispel_immune = 0;

    // Check if the spell grants school or mechanic immunity.
    // We use bitmasks so the loop is done only once and not on every aura check below.
    if (m_spellInfo->HasAttribute(SPELL_ATTR_EX_DISPEL_AURAS_ON_IMMUNITY))
    {
        for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
        {
            if (m_spellInfo->EffectApplyAuraName[i] == SPELL_AURA_SCHOOL_IMMUNITY)
                school_immune |= uint32(m_spellInfo->EffectMiscValue[i]);
            else if (m_spellInfo->EffectApplyAuraName[i] == SPELL_AURA_MECHANIC_IMMUNITY)
                mechanic_immune |= 1 << uint32(m_spellInfo->EffectMiscValue[i] - 1);
            else if (m_spellInfo->EffectApplyAuraName[i] == SPELL_AURA_MECHANIC_IMMUNITY_MASK)
                mechanic_immune |= uint32(m_spellInfo->EffectMiscValue[i]);
            else if (m_spellInfo->EffectApplyAuraName[i] == SPELL_AURA_DISPEL_IMMUNITY)
                dispel_immune |= GetDispellMask(DispelType(m_spellInfo->EffectMiscValue[i]));
        }

        // immune movement impairment and loss of control (spell data have special structure for mark this case)
        if (IsSpellRemoveAllMovementAndControlLossEffects(m_spellInfo))
            mechanic_immune = IMMUNE_TO_MOVEMENT_IMPAIRMENT_AND_LOSS_CONTROL_MASK;
    }

    // Check whether the cast should be prevented by any state you might have.
    SpellCastResult prevented_reason = SPELL_CAST_OK;
    bool spellUsableWhileStunned = m_spellInfo->HasAttribute(SPELL_ATTR_EX5_USABLE_WHILE_STUNNED);

    // Have to check if there is a stun aura. Otherwise will have problems with ghost aura apply while logging out
    uint32 unitflag = m_caster->GetUInt32Value(UNIT_FIELD_FLAGS);     // Get unit state
    if (unitflag & UNIT_FLAG_STUNNED)
    {
        // Pain Suppression (have SPELL_ATTR_EX5_USABLE_WHILE_STUNNED that must be used only with glyph)
        if (m_spellInfo->SpellFamilyName == SPELLFAMILY_PRIEST && m_spellInfo->SpellIconID == 2178)
        {
            if (!m_caster->HasAura(63248))                  // Glyph of Pain Suppression
                spellUsableWhileStunned = false;
        }

        // spell is usable while stunned, check if caster has only mechanic stun auras, another stun types must prevent cast spell
        if (spellUsableWhileStunned)
        {
            bool is_stun_mechanic = true;
            Unit::AuraList const& stunAuras = m_caster->GetAurasByType(SPELL_AURA_MOD_STUN);
            for (Unit::AuraList::const_iterator itr = stunAuras.begin(); itr != stunAuras.end(); ++itr)
                if (!(*itr)->HasMechanic(MECHANIC_STUN))
                {
                    is_stun_mechanic = false;
                    break;
                }
            if (!is_stun_mechanic)
                prevented_reason = SPELL_FAILED_STUNNED;
        }
        else
            prevented_reason = SPELL_FAILED_STUNNED;
    }
    else if (unitflag & UNIT_FLAG_CONFUSED && !m_spellInfo->HasAttribute(SPELL_ATTR_EX5_USABLE_WHILE_CONFUSED))
        prevented_reason = SPELL_FAILED_CONFUSED;
    else if (unitflag & UNIT_FLAG_FLEEING && !m_spellInfo->HasAttribute(SPELL_ATTR_EX5_USABLE_WHILE_FEARED))
        prevented_reason = SPELL_FAILED_FLEEING;
    else if (unitflag & UNIT_FLAG_SILENCED && m_spellInfo->PreventionType == SPELL_PREVENTION_TYPE_SILENCE)
        prevented_reason = SPELL_FAILED_SILENCED;
    else if (unitflag & UNIT_FLAG_PACIFIED && m_spellInfo->PreventionType == SPELL_PREVENTION_TYPE_PACIFY)
        prevented_reason = SPELL_FAILED_PACIFIED;
    else if (m_caster->HasAuraType(SPELL_AURA_ALLOW_ONLY_ABILITY))
    {
        Unit::AuraList const& casingLimit = m_caster->GetAurasByType(SPELL_AURA_ALLOW_ONLY_ABILITY);
        for (Unit::AuraList::const_iterator itr = casingLimit.begin(); itr != casingLimit.end(); ++itr)
        {
            if (!(*itr)->isAffectedOnSpell(m_spellInfo))
            {
                prevented_reason = SPELL_FAILED_CASTER_AURASTATE;
                break;
            }
        }
    }

    // Attr must make flag drop spell totally immune from all effects
    if (prevented_reason != SPELL_CAST_OK)
    {
        if (school_immune || mechanic_immune || dispel_immune)
        {
            // Checking auras is needed now, because you are prevented by some state but the spell grants immunity.
            Unit::SpellAuraHolderMap const& auras = m_caster->GetSpellAuraHolderMap();
            for (Unit::SpellAuraHolderMap::const_iterator itr = auras.begin(); itr != auras.end(); ++itr)
            {
                SpellAuraHolder* holder = itr->second;
                SpellEntry const* pEntry = holder->GetSpellProto();

                if ((GetSpellSchoolMask(pEntry) & school_immune) && !pEntry->HasAttribute(SPELL_ATTR_EX_UNAFFECTED_BY_SCHOOL_IMMUNE))
                    continue;
                if ((1<<(pEntry->Dispel)) & dispel_immune)
                    continue;

                for (int32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                {
                    Aura* aura = holder->GetAuraByEffectIndex(SpellEffectIndex(i));
                    if (!aura)
                        continue;

                    if (GetSpellMechanicMask(pEntry, 1 << i) & mechanic_immune)
                        continue;
                    // Make a second check for spell failed so the right SPELL_FAILED message is returned.
                    // That is needed when your casting is prevented by multiple states and you are only immune to some of them.
                    switch (aura->GetModifier()->m_auraname)
                    {
                        case SPELL_AURA_MOD_STUN:
                            if (!spellUsableWhileStunned || !aura->HasMechanic(MECHANIC_STUN))
                                return SPELL_FAILED_STUNNED;
                            break;
                        case SPELL_AURA_MOD_CONFUSE:
                            if (!m_spellInfo->HasAttribute(SPELL_ATTR_EX5_USABLE_WHILE_CONFUSED))
                                return SPELL_FAILED_CONFUSED;
                            break;
                        case SPELL_AURA_MOD_FEAR:
                            if (!m_spellInfo->HasAttribute(SPELL_ATTR_EX5_USABLE_WHILE_FEARED))
                                return SPELL_FAILED_FLEEING;
                            break;
                        case SPELL_AURA_MOD_SILENCE:
                        case SPELL_AURA_MOD_PACIFY:
                        case SPELL_AURA_MOD_PACIFY_SILENCE:
                            if( m_spellInfo->PreventionType == SPELL_PREVENTION_TYPE_PACIFY)
                                return SPELL_FAILED_PACIFIED;
                            else if ( m_spellInfo->PreventionType == SPELL_PREVENTION_TYPE_SILENCE)
                                return SPELL_FAILED_SILENCED;
                            break;
                        default: break;
                    }
                }
            }
        }
        // You are prevented from casting and the spell casted does not grant immunity. Return a failed error.
        else
            return prevented_reason;
    }
    return SPELL_CAST_OK;
}

bool Spell::CanAutoCast(Unit* target)
{
    ObjectGuid targetguid = target->GetObjectGuid();

    for (int j = 0; j < MAX_EFFECT_INDEX; ++j)
    {
        if(m_spellInfo->Effect[j] == SPELL_EFFECT_APPLY_AURA)
        {
            if( m_spellInfo->StackAmount <= 1)
            {
                if (target->HasAura(m_spellInfo->Id, SpellEffectIndex(j)))
                    return false;
            }
            else
            {
                if(Aura* aura = target->GetAura(m_spellInfo->Id, SpellEffectIndex(j)))
                    if(aura->GetStackAmount() >= m_spellInfo->StackAmount)
                        return false;
            }
        }
        else if (IsAreaAuraEffect(m_spellInfo->Effect[j]))
        {
            if (target->HasAura(m_spellInfo->Id, SpellEffectIndex(j)))
                return false;
        }
    }

    SpellCastResult result = CheckPetCast(target);

    if (result == SPELL_CAST_OK || result == SPELL_FAILED_UNIT_NOT_INFRONT)
    {
        FillTargetMap();
        // check if among target units, our WANTED target is as well (->only self cast spells return false)
        for (TargetList::const_iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
            if (ihit->targetGUID == targetguid)
                return true;
    }
    return false;                                           // target invalid
}

SpellCastResult Spell::CheckRange(bool strict)
{
    Unit* target = m_targets.getUnitTarget();

    // add radius of caster and ~5 yds "give" for non stricred (landing) check
    float range_mod = strict ? 1.25f : 6.25;

    // special range cases
    switch (m_spellInfo->rangeIndex)
    {
        // self cast doesn't need range checking -- also for Starshards fix
        // spells that can be cast anywhere also need no check
        case SPELL_RANGE_IDX_SELF_ONLY:
        case SPELL_RANGE_IDX_ANYWHERE:
            return SPELL_CAST_OK;
        // combat range spells are treated differently
        case SPELL_RANGE_IDX_COMBAT:
        {
            range_mod = 0.0f; // These spells do not have any leeway like ranged ones do
            break;
        }
    }

    // leeway for moving
    if (target && m_caster->IsMoving() && target->IsMoving() && !m_caster->IsWalking() && !target->IsWalking() &&
        (m_spellInfo->rangeIndex == SPELL_RANGE_IDX_COMBAT || target->GetTypeId() == TYPEID_PLAYER))
        range_mod += 8.0f / 3.0f;

    SpellRangeEntry const* srange = sSpellRangeStore.LookupEntry(m_spellInfo->rangeIndex);
    bool friendly = target ? target->IsFriendlyTo(m_caster) : false;
    float max_range = GetSpellMaxRange(srange, friendly) + range_mod;
    float min_range = GetSpellMinRange(srange, friendly);

    if (Player* modOwner = m_caster->GetSpellModOwner())
        modOwner->ApplySpellMod(m_spellInfo->Id, SPELLMOD_RANGE, max_range);

    if (target && target != m_caster)
    {
        // distance from target in checks
        float dist = m_caster->GetCombatDistance(target, m_spellInfo->rangeIndex == SPELL_RANGE_IDX_COMBAT);

        if (dist > max_range)
            return SPELL_FAILED_OUT_OF_RANGE;
        if (min_range && dist < min_range)
            return SPELL_FAILED_TOO_CLOSE;
        if( m_caster->GetTypeId() == TYPEID_PLAYER &&
            (m_spellInfo->FacingCasterFlags & SPELL_FACING_FLAG_INFRONT) && !m_caster->HasInArc(target))
            return SPELL_FAILED_UNIT_NOT_INFRONT;
    }

    // TODO verify that such spells really use bounding radius
    if (m_targets.m_targetMask == TARGET_FLAG_DEST_LOCATION && m_targets.m_destX != 0 && m_targets.m_destY != 0 && m_targets.m_destZ != 0)
    {
        if (!m_caster->IsWithinDist3d(m_targets.m_destX, m_targets.m_destY, m_targets.m_destZ, max_range))
            return SPELL_FAILED_OUT_OF_RANGE;
        if (min_range && m_caster->IsWithinDist3d(m_targets.m_destX, m_targets.m_destY, m_targets.m_destZ, min_range))
            return SPELL_FAILED_TOO_CLOSE;
        if (!m_caster->IsWithinLOS(m_targets.m_destX, m_targets.m_destY, m_targets.m_destZ))
            return SPELL_FAILED_LINE_OF_SIGHT;
    }

    return SPELL_CAST_OK;
}

uint32 Spell::CalculatePowerCost(SpellEntry const* spellInfo, Unit* caster, Spell const* spell, Item* castItem)
{
    // item cast not used power
    if (castItem)
        return 0;

    // Spell drain all exist power on cast (Only paladin lay of Hands)
    if (spellInfo->HasAttribute(SPELL_ATTR_EX_DRAIN_ALL_POWER))
    {
        // If power type - health drain all
        if (spellInfo->powerType == POWER_HEALTH)
            return caster->GetHealth();
        // Else drain all power
        if (spellInfo->powerType < MAX_POWERS)
            return caster->GetPower(Powers(spellInfo->powerType));
        sLog.outError("Spell::CalculateManaCost: Unknown power type '%d' in spell %d", spellInfo->powerType, spellInfo->Id);
        return 0;
    }

    // Base powerCost
    int32 powerCost = spellInfo->ManaCost;
    // PCT cost from total amount
    if (uint32 manaCostPct = spellInfo->ManaCostPercentage)
    {
        switch (spellInfo->powerType)
        {
            // health as power used
            case POWER_HEALTH:
                powerCost += manaCostPct * caster->GetCreateHealth() / 100;
                break;
            case POWER_MANA:
                powerCost += manaCostPct * caster->GetCreateMana() / 100;
                break;
            case POWER_RAGE:
            case POWER_FOCUS:
            case POWER_ENERGY:
                powerCost += manaCostPct * caster->GetMaxPower(Powers(spellInfo->powerType)) / 100;
                break;
            case POWER_RUNE:
            case POWER_RUNIC_POWER:
                DEBUG_LOG("Spell::CalculateManaCost: Not implemented yet!");
                break;
            default:
                sLog.outError("Spell::CalculateManaCost: Unknown power type '%d' in spell %d", spellInfo->powerType, spellInfo->Id);
                return 0;
        }
    }

    SpellSchools school = GetFirstSchoolInMask(spell ? spell->m_spellSchoolMask : GetSpellSchoolMask(spellInfo));
    // Flat mod from caster auras by spell school
    powerCost += caster->GetInt32Value(UNIT_FIELD_POWER_COST_MODIFIER + school);
    // Shiv - costs 20 + weaponSpeed*10 energy (apply only to non-triggered spell with energy cost)
    if (spellInfo->HasAttribute(SPELL_ATTR_EX4_SPELL_VS_EXTEND_COST))
        powerCost += caster->GetAttackTime(OFF_ATTACK) / 100;
    // Apply cost mod by spell
    if (spell)
        if (Player* modOwner = caster->GetSpellModOwner())
            modOwner->ApplySpellMod(spellInfo->Id, SPELLMOD_COST, powerCost);

    if (!caster->IsControlledByPlayer() && spellInfo->HasAttribute(SPELL_ATTR_LEVEL_DAMAGE_CALCULATION))
    {
        GtNPCManaCostScalerEntry const* spellScaler = sGtNPCManaCostScalerStore.LookupEntry(spellInfo->SpellLevel - 1);
        GtNPCManaCostScalerEntry const* casterScaler = sGtNPCManaCostScalerStore.LookupEntry(caster->GetLevel() - 1);
        if (spellScaler && casterScaler)
            powerCost *= casterScaler->ratio / spellScaler->ratio;
    }

    // PCT mod from user auras by school
    powerCost = int32(powerCost * (1.0f + caster->GetFloatValue(UNIT_FIELD_POWER_COST_MULTIPLIER + school)));
    if (powerCost < 0)
        powerCost = 0;
    return powerCost;
}

SpellCastResult Spell::CheckPower()
{
    // item cast not used power
    if (m_CastItem)
        return SPELL_CAST_OK;

    // Do precise power regen on spell cast
    if (m_powerCost > 0 && m_caster->GetTypeId() == TYPEID_PLAYER)
    {
        Player* playerCaster = (Player*)m_caster;
        uint32 diff = REGEN_TIME_FULL - m_caster->GetRegenTimer();
        if (diff >= REGEN_TIME_PRECISE)
            playerCaster->RegenerateAll(diff);
    }

    // health as power used - need check health amount
    if (m_spellInfo->powerType == POWER_HEALTH)
    {
        if (m_caster->GetHealth() <= m_powerCost)
            return SPELL_FAILED_CASTER_AURASTATE;
        return SPELL_CAST_OK;
    }

    // Check valid power type
    if (m_spellInfo->powerType >= MAX_POWERS)
    {
        sLog.outError("Spell::CheckMana: Unknown power type '%d'", m_spellInfo->powerType);
        return SPELL_FAILED_UNKNOWN;
    }

    // check rune cost only if a spell has PowerType == POWER_RUNE
    if (m_spellInfo->powerType == POWER_RUNE)
    {
        SpellCastResult failReason = CheckRunePower();
        if (failReason != SPELL_CAST_OK)
            return failReason;
    }

    // Check power amount
    Powers powerType = Powers(m_spellInfo->powerType);
    if (m_caster->GetPower(powerType) < m_powerCost)
        return SPELL_FAILED_NO_POWER;

    return SPELL_CAST_OK;
}

bool Spell::IgnoreItemRequirements() const
{
    /// Check if it's an enchant scroll. These have no required reagents even though their spell does.
    if (m_CastItem && (m_CastItem->GetProto()->Flags & ITEM_FLAG_NO_REAGENT_COST))
        return true;

    if (m_IsTriggeredSpell)
    {
        /// Not own traded item (in trader trade slot) req. reagents including triggered spell case
        if (Item* targetItem = m_targets.getItemTarget())
            if (targetItem->GetOwnerGuid() != m_caster->GetObjectGuid())
                return false;

        /// Some triggered spells have same reagents that have master spell
        /// expected in test: master spell have reagents in first slot then triggered don't must use own
        return !(m_triggeredBySpellInfo && !m_triggeredBySpellInfo->Reagent[0]);
    }

    return false;
}

SpellCastResult Spell::CheckItems()
{
    if (m_caster->GetTypeId() != TYPEID_PLAYER)
        return SPELL_CAST_OK;

    Player* p_caster = (Player*)m_caster;
    bool isScrollItem = false;
    bool isVellumTarget = false;

    // cast item checks
    if (m_CastItem)
    {
        if (m_CastItem->IsInTrade())
            return SPELL_FAILED_ITEM_NOT_FOUND;

        uint32 itemid = m_CastItem->GetEntry();
        if (!p_caster->HasItemCount(itemid, 1))
            return SPELL_FAILED_ITEM_NOT_FOUND;

        ItemPrototype const* proto = m_CastItem->GetProto();
        if (!proto)
            return SPELL_FAILED_ITEM_NOT_FOUND;

        if (proto->Flags & ITEM_FLAG_NO_REAGENT_COST)
            isScrollItem = true;

        for (int i = 0; i < 5; ++i)
            if (proto->Spells[i].SpellCharges)
                if (m_CastItem->GetSpellCharges(i) == 0)
                    return SPELL_FAILED_NO_CHARGES_REMAIN;

        // consumable cast item checks
        if (proto->Class == ITEM_CLASS_CONSUMABLE && m_targets.getUnitTarget())
        {
            // such items should only fail if there is no suitable effect at all - see Rejuvenation Potions for example
            SpellCastResult failReason = SPELL_CAST_OK;
            for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
            {
                // skip check, pet not required like checks, and for TARGET_UNIT_CASTER_PET m_targets.getUnitTarget() is not the real target but the caster
                if (m_spellInfo->EffectImplicitTargetA[i] == TARGET_UNIT_CASTER_PET)
                    continue;

                if (m_spellInfo->Effect[i] == SPELL_EFFECT_HEAL)
                {
                    if (m_targets.getUnitTarget()->GetHealth() == m_targets.getUnitTarget()->GetMaxHealth())
                    {
                        failReason = SPELL_FAILED_ALREADY_AT_FULL_HEALTH;
                        continue;
                    }
                    else
                    {
                        failReason = SPELL_CAST_OK;
                        break;
                    }
                }

                // Mana Potion, Rage Potion, Thistle Tea(Rogue), ...
                if (m_spellInfo->Effect[i] == SPELL_EFFECT_ENERGIZE)
                {
                    if(m_spellInfo->EffectMiscValue[i] < 0 || m_spellInfo->EffectMiscValue[i] >= MAX_POWERS)
                    {
                        failReason = SPELL_FAILED_ALREADY_AT_FULL_POWER;
                        continue;
                    }

                    Powers power = Powers(m_spellInfo->EffectMiscValue[i]);
                    if (m_targets.getUnitTarget()->GetPower(power) == m_targets.getUnitTarget()->GetMaxPower(power))
                    {
                        failReason = SPELL_FAILED_ALREADY_AT_FULL_POWER;
                        continue;
                    }
                    else
                    {
                        failReason = SPELL_CAST_OK;
                        break;
                    }
                }
            }
            if (failReason != SPELL_CAST_OK)
                return failReason;
        }
    }

    // check target item (for triggered case not report error)
    if (m_targets.getItemTargetGuid())
    {
        if (m_caster->GetTypeId() != TYPEID_PLAYER)
            return m_IsTriggeredSpell && !(m_targets.m_targetMask & TARGET_FLAG_TRADE_ITEM)
                   ? SPELL_FAILED_DONT_REPORT : SPELL_FAILED_BAD_TARGETS;

        if (!m_targets.getItemTarget())
            return m_IsTriggeredSpell  && !(m_targets.m_targetMask & TARGET_FLAG_TRADE_ITEM)
                   ? SPELL_FAILED_DONT_REPORT : SPELL_FAILED_ITEM_GONE;

        isVellumTarget = m_targets.getItemTarget()->GetProto()->IsVellum();
        if (!m_targets.getItemTarget()->IsFitToSpellRequirements(m_spellInfo))
            return m_IsTriggeredSpell  && !(m_targets.m_targetMask & TARGET_FLAG_TRADE_ITEM)
                   ? SPELL_FAILED_DONT_REPORT : SPELL_FAILED_EQUIPPED_ITEM_CLASS;

        // Do not enchant vellum with scroll
        if (isVellumTarget && isScrollItem)
            return m_IsTriggeredSpell  && !(m_targets.m_targetMask & TARGET_FLAG_TRADE_ITEM)
                   ? SPELL_FAILED_DONT_REPORT : SPELL_FAILED_BAD_TARGETS;
    }
    // if not item target then required item must be equipped (for triggered case not report error)
    else
    {
        if (m_caster->GetTypeId() == TYPEID_PLAYER && !((Player*)m_caster)->HasItemFitToSpellReqirements(m_spellInfo))
            return m_IsTriggeredSpell ? SPELL_FAILED_DONT_REPORT : SPELL_FAILED_EQUIPPED_ITEM_CLASS;
    }

    // check reagents (ignore triggered spells with reagents processed by original spell) and special reagent ignore case.
    if (!IgnoreItemRequirements())
    {
        if (!p_caster->CanNoReagentCast(m_spellInfo))
        {
            for(uint32 i = 0; i < MAX_SPELL_REAGENTS; ++i)
            {
                if(m_spellInfo->Reagent[i] <= 0)
                    continue;

                uint32 itemid    = m_spellInfo->Reagent[i];
                uint32 itemcount = m_spellInfo->ReagentCount[i];
                
                // if CastItem is also spell reagent
                if (m_CastItem && m_CastItem->GetEntry() == itemid)
                {
                    ItemPrototype const* proto = m_CastItem->GetProto();
                    if (!proto)
                        return SPELL_FAILED_REAGENTS;

                    for (int s = 0; s < MAX_ITEM_PROTO_SPELLS; ++s)
                    {
                        // CastItem will be used up and does not count as reagent
                        int32 charges = m_CastItem->GetSpellCharges(s);
                        if (proto->Spells[s].SpellCharges < 0 && abs(charges) < 2)
                        {
                            ++itemcount;
                            break;
                        }
                    }
                }
                
                if (!p_caster->HasItemCount(itemid, itemcount))
                    return SPELL_FAILED_REAGENTS;
            }
        }

        // check totem-item requirements (items presence in inventory)
        uint32 totems = MAX_SPELL_TOTEMS;
        for (auto i : m_spellInfo->Totem)
        {
            if (i != 0)
            {
                if (p_caster->HasItemCount(i, 1))
                {
                    totems -= 1;
                }
            }
            else
                totems -= 1;
        }

            if (totems != 0)
                return SPELL_FAILED_TOTEMS;
    }

    // special checks for spell effects
    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        switch (m_spellInfo->Effect[i])
        {
            case SPELL_EFFECT_CREATE_ITEM:
            {
                if (!m_IsTriggeredSpell && m_spellInfo->EffectItemType[i])
                {
                    // Conjure Mana Gem (skip same or low level ranks for later recharge)
                    if (i == EFFECT_INDEX_0 && m_spellInfo->Effect[EFFECT_INDEX_1] == SPELL_EFFECT_DUMMY)
                    {
                        if (ItemPrototype const* itemProto = ObjectMgr::GetItemPrototype(m_spellInfo->EffectItemType[i]))
                        {
                            if (Item* item = p_caster->GetItemByLimitedCategory(itemProto->ItemLimitCategory))
                            {
                                if (item->GetProto()->ItemLevel <= itemProto->ItemLevel)
                                {
                                    if (item->HasMaxCharges())
                                        return SPELL_FAILED_ITEM_AT_MAX_CHARGES;

                                    // will recharge in next effect
                                    continue;
                                }
                            }
                        }
                    }

                    ItemPosCountVec dest;
                    InventoryResult msg = p_caster->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, m_spellInfo->EffectItemType[i], 1);
                    if (msg != EQUIP_ERR_OK )
                    {
                        p_caster->SendEquipError( msg, nullptr, nullptr, m_spellInfo->EffectItemType[i]);
                        return SPELL_FAILED_DONT_REPORT;
                    }
                }
                break;
            }
            case SPELL_EFFECT_RESTORE_ITEM_CHARGES:
            {
                if (Item* item = p_caster->GetItemByEntry(m_spellInfo->EffectItemType[i]))
                    if (item->HasMaxCharges())
                        return SPELL_FAILED_ITEM_AT_MAX_CHARGES;

                break;
            }
            case SPELL_EFFECT_ENCHANT_ITEM:
            case SPELL_EFFECT_ENCHANT_ITEM_PRISMATIC:
            {
                Item* targetItem = m_targets.getItemTarget();
                if (!targetItem)
                    return SPELL_FAILED_ITEM_NOT_FOUND;

                if(targetItem->GetProto()->ItemLevel < m_spellInfo->BaseLevel)
                    return SPELL_FAILED_LOWLEVEL;
                // Check if we can store a new scroll, enchanting vellum has implicit SPELL_EFFECT_CREATE_ITEM
                if (isVellumTarget && m_spellInfo->EffectItemType[i])
                {
                    ItemPosCountVec dest;
                    InventoryResult msg = p_caster->CanStoreNewItem( NULL_BAG, NULL_SLOT, dest, m_spellInfo->EffectItemType[i], 1);
                    if (msg != EQUIP_ERR_OK)
                    {
                        p_caster->SendEquipError(msg, nullptr, nullptr);
                        return SPELL_FAILED_DONT_REPORT;
                    }
                }
                // Not allow enchant in trade slot for some enchant type
                if (targetItem->GetOwner() != m_caster)
                {
                    uint32 enchant_id = m_spellInfo->EffectMiscValue[i];
                    SpellItemEnchantmentEntry const *pEnchant = sSpellItemEnchantmentStore.LookupEntry(enchant_id);
                    if(!pEnchant)
                        return SPELL_FAILED_ERROR;
                    if (pEnchant->slot & ENCHANTMENT_CAN_SOULBOUND)
                        return SPELL_FAILED_NOT_TRADEABLE;
                    // cannot replace vellum with scroll in trade slot
                    if (isVellumTarget)
                        return SPELL_FAILED_ITEM_ENCHANT_TRADE_WINDOW;
                }
                break;
            }
            case SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY:
            {
                Item* item = m_targets.getItemTarget();
                if (!item)
                    return SPELL_FAILED_ITEM_NOT_FOUND;
                // Not allow enchant in trade slot for some enchant type
                if (item->GetOwner() != m_caster)
                {
                    uint32 enchant_id = m_spellInfo->EffectMiscValue[i];
                    SpellItemEnchantmentEntry const *pEnchant = sSpellItemEnchantmentStore.LookupEntry(enchant_id);
                    if(!pEnchant)
                        return SPELL_FAILED_ERROR;
                    if (pEnchant->slot & ENCHANTMENT_CAN_SOULBOUND)
                        return SPELL_FAILED_NOT_TRADEABLE;
                }
                break;
            }
            case SPELL_EFFECT_ENCHANT_HELD_ITEM:
                // check item existence in effect code (not output errors at offhand hold item effect to main hand for example
                break;
            case SPELL_EFFECT_DISENCHANT:
            {
                if (!m_targets.getItemTarget())
                    return SPELL_FAILED_CANT_BE_DISENCHANTED;

                // prevent disenchanting in trade slot
                if (m_targets.getItemTarget()->GetOwnerGuid() != m_caster->GetObjectGuid())
                    return SPELL_FAILED_CANT_BE_DISENCHANTED;

                ItemPrototype const* itemProto = m_targets.getItemTarget()->GetProto();
                if (!itemProto)
                    return SPELL_FAILED_CANT_BE_DISENCHANTED;

                // must have disenchant loot (other static req. checked at item prototype loading)
                if (!itemProto->DisenchantID)
                    return SPELL_FAILED_CANT_BE_DISENCHANTED;

                // 2.0.x addon: Check player enchanting level against the item disenchanting requirements
                int32 item_disenchantskilllevel = itemProto->RequiredDisenchantSkill;
                if (item_disenchantskilllevel > int32(p_caster->GetSkillValue(SKILL_ENCHANTING)))
                    return SPELL_FAILED_LOW_CASTLEVEL;
                break;
            }
            case SPELL_EFFECT_PROSPECTING:
            {
                if (!m_targets.getItemTarget())
                    return SPELL_FAILED_CANT_BE_PROSPECTED;
                // ensure item is a prospectable ore
                if (!(m_targets.getItemTarget()->GetProto()->Flags & ITEM_FLAG_IS_PROSPECTABLE))
                    return SPELL_FAILED_CANT_BE_PROSPECTED;
                // prevent prospecting in trade slot
                if (m_targets.getItemTarget()->GetOwnerGuid() != m_caster->GetObjectGuid())
                    return SPELL_FAILED_CANT_BE_PROSPECTED;
                // Check for enough skill in jewelcrafting
                uint32 item_prospectingskilllevel = m_targets.getItemTarget()->GetProto()->RequiredSkillRank;
                if (item_prospectingskilllevel > p_caster->GetSkillValue(SKILL_JEWELCRAFTING))
                    return SPELL_FAILED_LOW_CASTLEVEL;
                // make sure the player has the required ores in inventory
                if (int32(m_targets.getItemTarget()->GetCount()) < CalculateDamage(SpellEffectIndex(i), m_caster))
                    return SPELL_FAILED_NEED_MORE_ITEMS;

                if (!LootTemplates_Prospecting.HaveLootFor(m_targets.getItemTargetEntry()))
                    return SPELL_FAILED_CANT_BE_PROSPECTED;

                break;
            }
            case SPELL_EFFECT_MILLING:
            {
                if (!m_targets.getItemTarget())
                    return SPELL_FAILED_CANT_BE_MILLED;
                // ensure item is a millable herb
                if (!(m_targets.getItemTarget()->GetProto()->Flags & ITEM_FLAG_IS_MILLABLE))
                    return SPELL_FAILED_CANT_BE_MILLED;
                // prevent milling in trade slot
                if (m_targets.getItemTarget()->GetOwnerGuid() != m_caster->GetObjectGuid())
                    return SPELL_FAILED_CANT_BE_MILLED;
                // Check for enough skill in inscription
                uint32 item_millingskilllevel = m_targets.getItemTarget()->GetProto()->RequiredSkillRank;
                if (item_millingskilllevel > p_caster->GetSkillValue(SKILL_INSCRIPTION))
                    return SPELL_FAILED_LOW_CASTLEVEL;
                // make sure the player has the required herbs in inventory
                if (int32(m_targets.getItemTarget()->GetCount()) < CalculateDamage(SpellEffectIndex(i), m_caster))
                    return SPELL_FAILED_NEED_MORE_ITEMS;

                if (!LootTemplates_Milling.HaveLootFor(m_targets.getItemTargetEntry()))
                    return SPELL_FAILED_CANT_BE_MILLED;

                break;
            }
            case SPELL_EFFECT_WEAPON_DAMAGE:
            case SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL:
            {
                if (m_caster->GetTypeId() != TYPEID_PLAYER) return SPELL_FAILED_TARGET_NOT_PLAYER;
                if (m_attackType != RANGED_ATTACK)
                    break;
                Item* pItem = ((Player*)m_caster)->GetWeaponForAttack(m_attackType, true, false);
                if (!pItem)
                    return SPELL_FAILED_EQUIPPED_ITEM;

                switch (pItem->GetProto()->SubClass)
                {
                    case ITEM_SUBCLASS_WEAPON_THROWN:
                    {
                        uint32 ammo = pItem->GetEntry();
                        if (!((Player*)m_caster)->HasItemCount(ammo, 1))
                            return SPELL_FAILED_NO_AMMO;
                    };  break;
                    case ITEM_SUBCLASS_WEAPON_WAND:
                        break;
                    default:
                        break;
                }
                break;
            }
            default: break;
        }
    }

    return SPELL_CAST_OK;
}

void Spell::Delayed()
{
    if (!m_caster || m_caster->GetTypeId() != TYPEID_PLAYER)
        return;

    if (m_spellState == SPELL_STATE_TRAVELING)
        return;                                             // spell is active and can't be time-backed

    if (isDelayableNoMore())                                // Spells may only be delayed twice
        return;

    // spells not loosing casting time ( slam, dynamites, bombs.. )
    if(!(m_spellInfo->InterruptFlags & SPELL_INTERRUPT_FLAG_DAMAGE))
        return;

    // check pushback reduce
    int32 delaytime = 500;                                  // spellcasting delay is normally 500ms
    int32 delayReduce = 100;                                // must be initialized to 100 for percent modifiers
    ((Player*)m_caster)->ApplySpellMod(m_spellInfo->Id, SPELLMOD_NOT_LOSE_CASTING_TIME, delayReduce);
    delayReduce += m_caster->GetTotalAuraModifier(SPELL_AURA_REDUCE_PUSHBACK) - 100;
    if (delayReduce >= 100)
        return;

    delaytime = delaytime * (100 - delayReduce) / 100;

    if (int32(m_timer) + delaytime > m_casttime)
    {
        delaytime = m_casttime - m_timer;
        m_timer = m_casttime;
    }
    else
        m_timer += delaytime;

    DETAIL_FILTER_LOG(LOG_FILTER_SPELL_CAST, "Spell %u partially interrupted for (%d) ms at damage", m_spellInfo->Id, delaytime);

    WorldPacket data(SMSG_SPELL_DELAYED, 8 + 4);
    data << m_caster->GetPackGUID();
    data << uint32(delaytime);

    m_caster->SendMessageToSet(data, true);
}

void Spell::DelayedChannel()
{
    if (!m_caster || m_caster->GetTypeId() != TYPEID_PLAYER || getState() != SPELL_STATE_CHANNELING)
        return;

    if (isDelayableNoMore())                                // Spells may only be delayed twice
        return;

    // check pushback reduce
    int32 delaytime = GetSpellDuration(m_spellInfo) * 25 / 100;// channeling delay is normally 25% of its time per hit
    int32 delayReduce = 100;                                // must be initialized to 100 for percent modifiers
    ((Player*)m_caster)->ApplySpellMod(m_spellInfo->Id, SPELLMOD_NOT_LOSE_CASTING_TIME, delayReduce);
    delayReduce += m_caster->GetTotalAuraModifier(SPELL_AURA_REDUCE_PUSHBACK) - 100;
    if (delayReduce >= 100)
        return;

    delaytime = delaytime * (100 - delayReduce) / 100;

    if (int32(m_timer) < delaytime)
    {
        delaytime = m_timer;
        m_timer = 0;
    }
    else
        m_timer -= delaytime;

    DEBUG_FILTER_LOG(LOG_FILTER_SPELL_CAST, "Spell %u partially interrupted for %i ms, new duration: %u ms", m_spellInfo->Id, delaytime, m_timer);

    for (TargetList::const_iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
    {
        if ((*ihit).missCondition == SPELL_MISS_NONE)
        {
            if (Unit* unit = m_caster->GetObjectGuid() == ihit->targetGUID ? m_caster : ObjectAccessor::GetUnit(*m_caster, ihit->targetGUID))
                unit->DelaySpellAuraHolder(m_spellInfo->Id, delaytime, unit->GetObjectGuid());
        }
    }

    for (int j = 0; j < MAX_EFFECT_INDEX; ++j)
    {
        // partially interrupt persistent area auras
        if (DynamicObject* dynObj = m_caster->GetDynObject(m_spellInfo->Id, SpellEffectIndex(j)))
            dynObj->Delay(delaytime);
    }

    SendChannelUpdate(m_timer);
}

void Spell::UpdateOriginalCasterPointer()
{
    if (m_originalCasterGUID == m_caster->GetObjectGuid())
        m_originalCaster = m_caster;
    else if (m_originalCasterGUID.IsGameObject())
    {
        GameObject* go = m_caster->IsInWorld() ? m_caster->GetMap()->GetGameObject(m_originalCasterGUID) : nullptr;
        m_originalCaster = go ? go->GetOwner() : nullptr;
    }
    else
    {
        Unit* unit = ObjectAccessor::GetUnit(*m_caster, m_originalCasterGUID);
        m_originalCaster = unit && unit->IsInWorld() ? unit : nullptr;
    }
}

void Spell::UpdatePointers()
{
    UpdateOriginalCasterPointer();

    m_targets.Update(m_caster);

    if (m_caster->GetTypeId() == TYPEID_PLAYER)
        m_CastItem = ((Player *)m_caster)->GetItemByGuid(m_CastItemGuid);
    else
        m_CastItem = nullptr;
}

bool Spell::CheckTargetCreatureType(Unit* target) const
{
    uint32 spellCreatureTargetMask = m_spellInfo->TargetCreatureType;

    // Curse of Doom: not find another way to fix spell target check :/
    if (m_spellInfo->SpellFamilyName == SPELLFAMILY_WARLOCK && m_spellInfo->Category == 1179)
    {
        // not allow cast at player
        if (target->GetTypeId() == TYPEID_PLAYER)
            return false;

        spellCreatureTargetMask = 0x7FF;
    }

    // Dismiss Pet, Taming Lesson and Control Robot skipped
    if (m_spellInfo->Id == 2641 || m_spellInfo->Id == 23356 || m_spellInfo->Id == 30009)
        spellCreatureTargetMask =  0;

    if (spellCreatureTargetMask)
    {
        uint32 TargetCreatureType = target->GetCreatureTypeMask();

        return !TargetCreatureType || (spellCreatureTargetMask & TargetCreatureType);
    }
    return true;
}

CurrentSpellTypes Spell::GetCurrentContainer()
{
    if (IsNextMeleeSwingSpell())
        return (CURRENT_MELEE_SPELL);
    else if (IsAutoRepeat())
        return (CURRENT_AUTOREPEAT_SPELL);
    else if (IsChanneledSpell(m_spellInfo))
        return (CURRENT_CHANNELED_SPELL);
    else
        return (CURRENT_GENERIC_SPELL);
}

bool Spell::CheckTarget(Unit* target, SpellEffectIndex eff)
{
    // Check targets for creature type mask and remove not appropriate (skip explicit self target case, maybe need other explicit targets)
    if(m_spellInfo->EffectImplicitTargetA[eff] != TARGET_UNIT_CASTER)
    {
        if (!CheckTargetCreatureType(target))
            return false;
    }

    // Check Aura spell req (need for AoE spells)
    if(m_spellInfo->targetAuraSpell && !target->HasAura(m_spellInfo->targetAuraSpell))
        return false;
    if (m_spellInfo->excludeTargetAuraSpell && target->HasAura(m_spellInfo->excludeTargetAuraSpell))
        return false;

    if (target != m_caster)
    {
        // Check targets for not_selectable unit flag and remove
        // A player can cast spells on his pet (or other controlled unit) though in any state
        if (target->GetMasterGuid() != m_caster->GetObjectGuid())
        {
            // any unattackable target skipped
            if (!m_ignoreUnattackableTarget && target->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE))
                return false;

            // unselectable targets skipped in all cases except TARGET_UNIT_SCRIPT_NEAR_CASTER targeting
            // in case TARGET_UNIT_SCRIPT_NEAR_CASTER target selected by server always and can't be cheated
            if ((!m_ignoreUnselectableTarget || target != m_targets.getUnitTarget()) &&
                target->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE) &&
                (!target->GetTransportInfo() || (target->GetTransportInfo() &&
                    !((Unit*)target->GetTransportInfo()->GetTransport())->IsVehicle())) &&
                m_spellInfo->EffectImplicitTargetA[eff] != TARGET_UNIT_SCRIPT_NEAR_CASTER &&
                m_spellInfo->EffectImplicitTargetB[eff] != TARGET_UNIT_SCRIPT_NEAR_CASTER &&
                m_spellInfo->EffectImplicitTargetA[eff] != TARGET_ENUM_UNITS_SCRIPT_AOE_AT_SRC_LOC &&
                m_spellInfo->EffectImplicitTargetB[eff] != TARGET_ENUM_UNITS_SCRIPT_AOE_AT_SRC_LOC &&
                m_spellInfo->EffectImplicitTargetA[eff] != TARGET_ENUM_UNITS_SCRIPT_AOE_AT_DEST_LOC &&
                m_spellInfo->EffectImplicitTargetB[eff] != TARGET_ENUM_UNITS_SCRIPT_AOE_AT_DEST_LOC &&
                m_spellInfo->EffectImplicitTargetA[eff] != TARGET_ENUM_UNITS_SCRIPT_IN_CONE_110 &&
                m_spellInfo->EffectImplicitTargetB[eff] != TARGET_ENUM_UNITS_SCRIPT_IN_CONE_110 &&
                m_spellInfo->EffectImplicitTargetA[eff] != TARGET_ENUM_UNITS_SCRIPT_IN_CONE_60 &&
                m_spellInfo->EffectImplicitTargetB[eff] != TARGET_ENUM_UNITS_SCRIPT_IN_CONE_60)
                return false;
        }

        // Check player targets and remove if in GM mode or GM invisibility (for not self casting case)
        if (target != m_caster && target->GetTypeId() == TYPEID_PLAYER)
        {
            if (((Player*)target)->GetVisibility() == VISIBILITY_OFF)
                return false;

        if (((Player*)target)->IsGameMaster() && !IsPositiveSpell(m_spellInfo->Id, m_caster, target))
                return false;
        }

        if (m_caster->GetTypeId() == TYPEID_PLAYER)
        {
            // Do not allow these spells to target creatures not tapped by us (Banish, Polymorph, many quest spells)
            if (m_spellInfo->HasAttribute(SPELL_ATTR_EX2_CANT_TARGET_TAPPED))
                if (Creature const* targetCreature = dynamic_cast<Creature*>(target))
                    if ((!targetCreature->GetLootRecipientGuid().IsEmpty()) && !targetCreature->IsTappedBy((Player*)m_caster))
                        return false;
        }
    }

    // Check targets for LOS visibility (except spells without range limitations )
    switch(m_spellInfo->Effect[eff])
    {
        case SPELL_EFFECT_SUMMON_PLAYER:                    // from anywhere
            break;
        case SPELL_EFFECT_DUMMY:
            if (m_spellInfo->Id != 20577)                   // Cannibalize
                break;
        // fall through
        case SPELL_EFFECT_RESURRECT_NEW:
            // player far away, maybe his corpse near?
            if (target != m_caster && !m_spellInfo->HasAttribute(SPELL_ATTR_EX2_IGNORE_LOS) && !target->IsWithinLOSInMap(m_caster))
            {
                if (!m_targets.getCorpseTargetGuid())
                    return false;

                Corpse* corpse = m_caster->GetMap()->GetCorpse(m_targets.getCorpseTargetGuid());
                if (!corpse)
                    return false;

                if (target->GetObjectGuid() != corpse->GetOwnerGuid())
                    return false;

                if (!m_spellInfo->HasAttribute(SPELL_ATTR_EX2_IGNORE_LOS) && !corpse->IsWithinLOSInMap(m_caster))
                    return false;
            }

            // all ok by some way or another, skip normal check
            break;
        default:                                            // normal case
            if (!IsIgnoreLosSpell(m_spellInfo))
            {
                if (target != m_caster)
                {
                    if (m_spellInfo->EffectImplicitTargetA[eff] == TARGET_LOCATION_DYNOBJ_POSITION)
                    {
                        if (DynamicObject* dynObj = m_caster->GetDynObject(m_triggeredByAuraSpell ? m_triggeredByAuraSpell->Id : m_spellInfo->Id))
                            if (!target->IsWithinLOSInMap(dynObj))
                                return false;
                    }
                    else if (WorldObject* caster = GetCastingObject())
                        if (!target->IsWithinLOSInMap(caster))
                            return false;
                }
            }
            break;
    }

    if (target->GetTypeId() != TYPEID_PLAYER && m_spellInfo->HasAttribute(SPELL_ATTR_EX3_TARGET_ONLY_PLAYER)
        && m_spellInfo->EffectImplicitTargetA[eff] != TARGET_UNIT_SCRIPT_NEAR_CASTER && m_spellInfo->EffectImplicitTargetA[eff] != TARGET_UNIT_SCRIPT_NEAR_CASTER)
        return false;

    if (m_spellInfo->HasAttribute(SPELL_ATTR_EX3_CAST_ON_DEAD) && target->IsAlive())
        return false;

    if (!IsAllowingDeadTarget(m_spellInfo) && !target->IsAlive() && !(target == m_caster && m_spellInfo->HasAttribute(SPELL_ATTR_CASTABLE_WHILE_DEAD)) && m_caster->GetTypeId() == TYPEID_PLAYER)
        return false;

    switch (m_spellInfo->Id)
    {
        case 25676:                                         // Drain Mana
            if (target->GetPowerType() != POWER_MANA)
                return false;
            break;
        case 37433:                                         // Spout (The Lurker Below), only players affected if its not in water
            if (target->GetTypeId() != TYPEID_PLAYER || target->IsInWater())
                return false;
            break;
        case 39090:                                         // Positive Charge
            if (!target->HasAura(39091))                    // Only deal damage if target has Negative Charge
                return false;
            break;
        case 39093:                                         // Negative Charge
            if (!target->HasAura(39088))                    // Only deal damage if target has Positive Charge
                return false;
            break;
        case 39921:                                         // Vimgol Pentagram Beam
        {
            if (target->GetTypeId() != TYPEID_UNIT || target->GetEntry() != 23040 || m_caster->GetTypeId() != TYPEID_UNIT || m_caster->GetEntry() != 23040)
                return false;

            uint8 pBunnyId = ((Creature*)m_caster)->AI()->GetScriptData();

            std::list<Creature*> creatureList;
            MaNGOS::AllCreaturesOfEntryInRangeCheck check(target, 23040, 60);
            MaNGOS::CreatureListSearcher<MaNGOS::AllCreaturesOfEntryInRangeCheck> searcher(creatureList, check);
            Cell::VisitGridObjects(target, searcher, 60);

            for (auto& creature : creatureList)
            {
                // Check that both have AI() before accessing
                if (creature->AI() && ((Creature*)target)->AI() &&
                    creature->AI()->GetScriptData() == ((Creature*)target)->AI()->GetScriptData())
                    pBunnyId = creature->AI()->GetScriptData();
            }

            std::list<Player*> playerList;
            MaNGOS::AnyPlayerInObjectRangeCheck checkPlayer(target, 4);
            MaNGOS::PlayerListSearcher<MaNGOS::AnyPlayerInObjectRangeCheck> playerSearcher(playerList, checkPlayer);
            Cell::VisitWorldObjects(target, playerSearcher, 4);
            for (auto& player : playerList)
            {
                switch (pBunnyId)
                {
                    case 0:
                        if (player->HasAura(39834))
                            return true;
                        break;
                    case 1:
                        if (player->HasAura(39851))
                            return true;
                        break;
                    case 2:
                        if (player->HasAura(39852))
                            return true;
                        break;
                    case 3:
                        if (player->HasAura(39853))
                            return true;
                        break;
                    case 4:
                        if (player->HasAura(39854))
                            return true;
                        break;
                }
            }
            return false;
        }
        case 68921:                                         // Soulstorm (FoS), only targets farer than 10 away
        case 69049:                                         // Soulstorm            - = -
            if (m_caster->IsWithinDist(target, 10.0f, false))
                return false;
            break;
        default:
            break;
    }

    return true;
}

bool Spell::IsNeedSendToClient() const
{
    return m_spellInfo->SpellVisual[0] || m_spellInfo->SpellVisual[1] || IsChanneledSpell(m_spellInfo) ||
           m_spellInfo->speed > 0.0f || (!m_triggeredByAuraSpell && !m_IsTriggeredSpell);
}

bool Spell::IsTriggeredSpellWithRedundentCastTime() const
{
    return m_IsTriggeredSpell && (m_spellInfo->ManaCost || m_spellInfo->ManaCostPercentage);
}

bool Spell::HaveTargetsForEffect(SpellEffectIndex effect) const
{
    for (TargetList::const_iterator itr = m_UniqueTargetInfo.begin(); itr != m_UniqueTargetInfo.end(); ++itr)
        if (itr->effectMask & (1 << effect))
            return true;

    for (GOTargetList::const_iterator itr = m_UniqueGOTargetInfo.begin(); itr != m_UniqueGOTargetInfo.end(); ++itr)
        if (itr->effectMask & (1 << effect))
            return true;

    for (ItemTargetList::const_iterator itr = m_UniqueItemInfo.begin(); itr != m_UniqueItemInfo.end(); ++itr)
        if (itr->effectMask & (1 << effect))
            return true;

    return false;
}

SpellEvent::SpellEvent(Spell* spell) : BasicEvent()
{
    m_Spell.reset(spell, [](Spell* toDelete)
    {
        if (toDelete->IsDeletable() || World::IsStopped())
        {
            delete toDelete;
        }
        else
        {
            sLog.outError("~SpellEvent: %s %u tried to delete non-deletable spell %u. Was not deleted, causes memory leak.",
                (toDelete->GetCaster()->GetTypeId() == TYPEID_PLAYER ? "Player" : "Creature"), toDelete->GetCaster()->GetGUIDLow(), toDelete->m_spellInfo->Id);
        }
    });
}

SpellEvent::~SpellEvent()
{
    if (m_Spell->getState() != SPELL_STATE_FINISHED)
        m_Spell->cancel();
}

bool SpellEvent::Execute(uint64 e_time, uint32 p_time)
{
    // update spell if it is not finished
    if (m_Spell->getState() != SPELL_STATE_FINISHED)
        m_Spell->update(p_time);

    // check spell state to process
    switch (m_Spell->getState())
    {
        case SPELL_STATE_FINISHED:
        {
            // spell was finished, check deletable state
            if (m_Spell->IsDeletable())
            {
                // check, if we do have unfinished triggered spells
                return true;                                // spell is deletable, finish event
            }
            // event will be re-added automatically at the end of routine)
        } break;

        case SPELL_STATE_CHANNELING:
        {
            // this spell is in channeled state, process it on the next update
            // event will be re-added automatically at the end of routine)
        } break;

        case SPELL_STATE_TRAVELING:
        {
            // first, check, if we have just started
            if (m_Spell->GetDelayStart() != 0)
            {
                // no, we aren't, do the typical update
                // check, if we have channeled spell on our hands
                if (IsChanneledSpell(m_Spell->m_spellInfo))
                {
                    // evented channeled spell is processed separately, casted once after delay, and not destroyed till finish
                    // check, if we have casting anything else except this channeled spell and autorepeat
                    if (m_Spell->GetCaster()->IsNonMeleeSpellCasted(false, true, true))
                    {
                        // another non-melee non-delayed spell is casted now, abort
                        m_Spell->cancel();
                    }
                    else
                    {
                        // do the action (pass spell to channeling state)
                        m_Spell->handle_immediate();
                    }
                    // event will be re-added automatically at the end of routine)
                }
                else
                {
                    // run the spell handler and think about what we can do next
                    uint64 t_offset = e_time - m_Spell->GetDelayStart();
                    uint64 n_offset = m_Spell->handle_delayed(t_offset);
                    if (n_offset)
                    {
                        // re-add us to the queue
                        m_Spell->GetCaster()->m_Events.AddEvent(this, m_Spell->GetDelayStart() + n_offset, false);
                        return false;                       // event not complete
                    }
                    // event complete
                    // finish update event will be re-added automatically at the end of routine)
                }
            }
            else
            {
                // delaying had just started, record the moment
                m_Spell->SetDelayStart(e_time);
                // re-plan the event for the delay moment
                m_Spell->GetCaster()->m_Events.AddEvent(this, e_time + m_Spell->GetDelayMoment(), false);
                return false;                               // event not complete
            }
        } break;

        default:
        {
            // all other states
            // event will be re-added automatically at the end of routine)
        } break;
    }

    // spell processing not complete, plan event on the next update interval
    m_Spell->GetCaster()->m_Events.AddEvent(this, e_time + 1, false);
    return false;                                           // event not complete
}

void SpellEvent::Abort(uint64 /*e_time*/)
{
    // oops, the spell we try to do is aborted
    if (m_Spell->getState() != SPELL_STATE_FINISHED)
        m_Spell->cancel();
}

bool SpellEvent::IsDeletable() const
{
    return m_Spell->IsDeletable();
}

SpellCastResult Spell::CanOpenLock(SpellEffectIndex effIndex, uint32 lockId, SkillType& skillId, int32& reqSkillValue, int32& skillValue)
{
    if (!lockId)                                            // possible case for GO and maybe for items.
        return SPELL_CAST_OK;

    // Get LockInfo
    LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);

    if (!lockInfo)
        return SPELL_FAILED_BAD_TARGETS;

    bool reqKey = false;                                    // some locks not have reqs

    for (int j = 0; j < 8; ++j)
    {
        switch (lockInfo->Type[j])
        {
            // check key item (many fit cases can be)
            case LOCK_KEY_ITEM:
                if (lockInfo->Index[j] && m_CastItem && m_CastItem->GetEntry() == lockInfo->Index[j])
                    return SPELL_CAST_OK;
                reqKey = true;
                break;
            // check key skill (only single first fit case can be)
            case LOCK_KEY_SKILL:
            {
                reqKey = true;

                // wrong locktype, skip
                if(uint32(m_spellInfo->EffectMiscValue[effIndex]) != lockInfo->Index[j])
                    continue;

                skillId = SkillByLockType(LockType(lockInfo->Index[j]));

                if (skillId != SKILL_NONE || skillId == MAX_SKILL_TYPE)
                {
                    // skill bonus provided by casting spell (mostly item spells)
                    // add the damage modifier from the spell casted (cheat lock / skeleton key etc.) (use m_currentBasePoints, CalculateDamage returns wrong value)
                    uint32 spellSkillBonus = uint32(m_currentBasePoints[effIndex]);
                    reqSkillValue = lockInfo->Skill[j];

                    // castitem check: rogue using skeleton keys. the skill values should not be added in this case.
                    // MAX_SKILL_TYPE - skill value scales with caster level
                    if (skillId == MAX_SKILL_TYPE)
                        skillValue = m_CastItem || m_caster->GetTypeId() != TYPEID_PLAYER ? 0 : m_caster->GetLevel() * 5;
                    else
                        skillValue = m_CastItem || m_caster->GetTypeId() != TYPEID_PLAYER ? 0 : ((Player*)m_caster)->GetSkillValue(skillId);

                    skillValue += spellSkillBonus;

                    if (skillValue < reqSkillValue)
                        return SPELL_FAILED_LOW_CASTLEVEL;
                }

                return SPELL_CAST_OK;
            }
        }
    }

    if (reqKey)
        return SPELL_FAILED_BAD_TARGETS;

    return SPELL_CAST_OK;
}

/**
 * Fill target list by units around (x,y) points at radius distance

 * @param targetUnitMap        Reference to target list that filled by function
 * @param x                    X coordinates of center point for target search
 * @param y                    Y coordinates of center point for target search
 * @param radius               Radius around (x,y) for target search
 * @param pushType             Additional rules for target area selection (in front, angle, etc)
 * @param spellTargets         Additional rules for target selection base at hostile/friendly state to original spell caster
 * @param originalCaster       If provided set alternative original caster, if =nullptr then used Spell::GetAffectiveObject() return
 */
void Spell::FillAreaTargets(UnitList& targetUnitMap, float radius, SpellNotifyPushType pushType, SpellTargets spellTargets, WorldObject* originalCaster /*=nullptr*/)
{
    MaNGOS::SpellNotifierCreatureAndPlayer notifier(*this, targetUnitMap, radius, pushType, spellTargets, originalCaster);
    Cell::VisitAllObjects(notifier.GetCenterX(), notifier.GetCenterY(), m_caster->GetMap(), notifier, radius);
}

void Spell::FillRaidOrPartyTargets(UnitList& targetUnitMap, Unit* member, Unit* center, float radius, bool raid, bool withPets, bool withcaster)
{
    Player* pMember = member->GetBeneficiaryPlayer();
    Group* pGroup = pMember ? pMember->GetGroup() : nullptr;

    if (pGroup)
    {
        uint8 subgroup = pMember->GetSubGroup();

        for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            Player* Target = itr->getSource();

            // IsHostileTo check duel and controlled by enemy
            if (Target && (raid || subgroup == Target->GetSubGroup())
                    && !m_caster->IsHostileTo(Target))
            {
                if ((Target == center || center->IsWithinDistInMap(Target, radius)) &&
                        (withcaster || Target != m_caster))
                    targetUnitMap.push_back(Target);

                if (withPets)
                    if (Pet* pet = Target->GetPet())
                        if ((pet == center || center->IsWithinDistInMap(pet, radius)) &&
                                (withcaster || pet != m_caster))
                            targetUnitMap.push_back(pet);
            }
        }
    }
    else
    {
        Unit* ownerOrSelf = pMember ? pMember : member->GetBeneficiary();
        if ((ownerOrSelf == center || center->IsWithinDistInMap(ownerOrSelf, radius)) &&
                (withcaster || ownerOrSelf != m_caster))
            targetUnitMap.push_back(ownerOrSelf);

        if (withPets)
            if (Pet* pet = ownerOrSelf->GetPet())
                if ((pet == center || center->IsWithinDistInMap(pet, radius)) &&
                        (withcaster || pet != m_caster))
                    targetUnitMap.push_back(pet);
    }
}

void Spell::FillRaidOrPartyManaPriorityTargets(UnitList& targetUnitMap, Unit* member, Unit* center, float radius, uint32 count, bool raid, bool withPets, bool withCaster)
{
    FillRaidOrPartyTargets(targetUnitMap, member, center, radius, raid, withPets, withCaster);

    PrioritizeManaUnitQueue manaUsers;
    for (UnitList::const_iterator itr = targetUnitMap.begin(); itr != targetUnitMap.end(); ++itr)
        if ((*itr)->GetPowerType() == POWER_MANA && !(*itr)->IsDead())
            manaUsers.push(PrioritizeManaUnitWraper(*itr));

    targetUnitMap.clear();
    while (!manaUsers.empty() && targetUnitMap.size() < count)
    {
        targetUnitMap.push_back(manaUsers.top().getUnit());
        manaUsers.pop();
    }
}

void Spell::FillRaidOrPartyHealthPriorityTargets(UnitList& targetUnitMap, Unit* member, Unit* center, float radius, uint32 count, bool raid, bool withPets, bool withCaster)
{
    FillRaidOrPartyTargets(targetUnitMap, member, center, radius, raid, withPets, withCaster);

    PrioritizeHealthUnitQueue healthQueue;
    for (UnitList::const_iterator itr = targetUnitMap.begin(); itr != targetUnitMap.end(); ++itr)
        if (!(*itr)->IsDead())
            healthQueue.push(PrioritizeHealthUnitWraper(*itr));

    targetUnitMap.clear();
    while (!healthQueue.empty() && targetUnitMap.size() < count)
    {
        targetUnitMap.push_back(healthQueue.top().getUnit());
        healthQueue.pop();
    }
}

WorldObject* Spell::GetAffectiveCasterObject() const
{
    if (!m_originalCasterGUID)
        return m_caster;

    if (m_originalCasterGUID.IsGameObject() && m_caster->IsInWorld())
        return m_caster->GetMap()->GetGameObject(m_originalCasterGUID);
    return m_originalCaster;
}

WorldObject* Spell::GetCastingObject() const
{
    if (m_originalCasterGUID.IsGameObject())
        return m_caster->IsInWorld() ? m_caster->GetMap()->GetGameObject(m_originalCasterGUID) : nullptr;
    else
        return m_caster;
}

void Spell::ResetEffectDamageAndHeal()
{
    m_damage = 0;
    m_healing = 0;
}

// handle SPELL_AURA_ADD_TARGET_TRIGGER auras
void Spell::ProcSpellAuraTriggers()
{
    Unit::AuraList const& targetTriggers = m_caster->GetAurasByType(SPELL_AURA_ADD_TARGET_TRIGGER);
    for(auto targetTrigger : targetTriggers)
    {
        if (!targetTrigger->isAffectedOnSpell(m_spellInfo))
            continue;
        for (TargetList::const_iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
        {
            if (ihit->missCondition == SPELL_MISS_NONE)
            {
                // check m_caster->GetGUID() let load auras at login and speedup most often case
                Unit* unit = m_caster->GetObjectGuid() == ihit->targetGUID ? m_caster : ObjectAccessor::GetUnit(*m_caster, ihit->targetGUID);
                if (unit && unit->IsAlive())
                {
                    SpellEntry const* auraSpellInfo = targetTrigger->GetSpellProto();
                    SpellEffectIndex auraSpellIdx = targetTrigger->GetEffIndex();
                    const uint32 procid = auraSpellInfo->EffectTriggerSpell[auraSpellIdx];
                    // Quick target mode check for procs and triggers (do not cast at friendly targets stuff against hostiles only)
                    if (IsPositiveSpellTargetModeForSpecificTarget(m_spellInfo, ihit->effectMask, m_caster, unit) != IsPositiveSpellTargetModeForSpecificTarget(procid, ihit->effectMask, m_caster, unit))
                        continue;
                    // Calculate chance at that moment (can be depend for example from combo points)
                    int32 auraBasePoints = targetTrigger->GetBasePoints();
                    int32 chance = m_caster->CalculateSpellDamage(unit, auraSpellInfo, auraSpellIdx, &auraBasePoints);
                    if (roll_chance_i(chance))
                        m_caster->CastSpell(unit, procid, TRIGGERED_OLD_TRIGGERED, nullptr, targetTrigger);
                }
            }
        }
    }
}

void Spell::SelectMountByAreaAndSkill(Unit* target, SpellEntry const* parentSpell, uint32 spellId75, uint32 spellId150, uint32 spellId225, uint32 spellId300, uint32 spellIdSpecial)
{
    if (!target || target->GetTypeId() != TYPEID_PLAYER)
        return;

    // Prevent stacking of mounts
    target->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);
    uint16 skillval = ((Player*)target)->GetSkillValue(SKILL_RIDING);
    if (!skillval)
        return;

    if (skillval >= 225 && (spellId300 > 0 || spellId225 > 0))
    {
        uint32 spellid = skillval >= 300 ? spellId300 : spellId225;
        SpellEntry const* pSpell = sSpellTemplate.LookupEntry<SpellEntry>(spellid);
        if (!pSpell)
        {
            sLog.outError("SelectMountByAreaAndSkill: unknown spell id %i by caster: %s", spellid, target->GetGuidStr().c_str());
            return;
        }

        // zone check
        uint32 zone, area;
        target->GetZoneAndAreaId(zone, area);

        SpellCastResult locRes = sSpellMgr.GetSpellAllowedInLocationError(pSpell, target->GetMapId(), zone, area, target->GetBeneficiaryPlayer());
        if (locRes != SPELL_CAST_OK || !((Player*)target)->CanStartFlyInArea(target->GetMapId(), zone, area))
            target->CastSpell(target, spellId150, TRIGGERED_OLD_TRIGGERED, nullptr, nullptr, ObjectGuid(), parentSpell);
        else if (spellIdSpecial > 0)
        {
            for (PlayerSpellMap::const_iterator iter = ((Player*)target)->GetSpellMap().begin(); iter != ((Player*)target)->GetSpellMap().end(); ++iter)
            {
                if (iter->second.state != PLAYERSPELL_REMOVED)
                {
                    SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(iter->first);
                    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
                    {
                        if(spellInfo->EffectApplyAuraName[i] == SPELL_AURA_MOD_FLIGHT_SPEED_MOUNTED)
                        {
                            int32 mountSpeed = spellInfo->CalculateSimpleValue(SpellEffectIndex(i));

                            // speed higher than 280 replace it
                            if (mountSpeed > 280)
                            {
                                target->CastSpell(target, spellIdSpecial, TRIGGERED_OLD_TRIGGERED, nullptr, nullptr, ObjectGuid(), parentSpell);
                                return;
                            }
                        }
                    }
                }
            }
            target->CastSpell(target, pSpell, TRIGGERED_OLD_TRIGGERED, nullptr, nullptr, ObjectGuid(), parentSpell);
        }
        else
            target->CastSpell(target, pSpell, TRIGGERED_OLD_TRIGGERED, nullptr, nullptr, ObjectGuid(), parentSpell);
    }
    else if (skillval >= 150 && spellId150 > 0)
        target->CastSpell(target, spellId150, TRIGGERED_OLD_TRIGGERED, nullptr, nullptr, ObjectGuid(), parentSpell);
    else if (spellId75 > 0)
        target->CastSpell(target, spellId75, TRIGGERED_OLD_TRIGGERED, nullptr, nullptr, ObjectGuid(), parentSpell);
}

void Spell::ClearCastItem()
{
    if (m_CastItem == m_targets.getItemTarget())
        m_targets.setItemTarget(nullptr);

    m_CastItem = nullptr;
    m_CastItemGuid.Clear();
}

void Spell::GetSpellRangeAndRadius(SpellEffectIndex effIndex, float& radius, uint32& EffectChainTarget) const
{
    if (m_spellInfo->EffectRadiusIndex[effIndex])
        radius = GetSpellRadius(sSpellRadiusStore.LookupEntry(m_spellInfo->EffectRadiusIndex[effIndex]));
    else
        radius = GetSpellMaxRange(sSpellRangeStore.LookupEntry(m_spellInfo->rangeIndex));

    if (Unit* realCaster = GetAffectiveCaster())
    {
        if (Player* modOwner = realCaster->GetSpellModOwner())
        {
            modOwner->ApplySpellMod(m_spellInfo->Id, SPELLMOD_RADIUS, radius);
            modOwner->ApplySpellMod(m_spellInfo->Id, SPELLMOD_JUMP_TARGETS, EffectChainTarget);
        }
    }

    switch (m_spellInfo->SpellFamilyName)
    {
        case SPELLFAMILY_PALADIN:
            if (m_spellInfo->Id == 20424)                   // Seal of Command (2 more target for single targeted spell)
            {
                // overwrite EffectChainTarget for non single target spell
                if (Spell* currSpell = m_caster->GetCurrentSpell(CURRENT_GENERIC_SPELL))
                    if (currSpell->m_spellInfo->MaxAffectedTargets > 0 ||
                        currSpell->m_spellInfo->EffectChainTarget[EFFECT_INDEX_0] > 0 ||
                        currSpell->m_spellInfo->EffectChainTarget[EFFECT_INDEX_1] > 0 ||
                        currSpell->m_spellInfo->EffectChainTarget[EFFECT_INDEX_2] > 0)
                        EffectChainTarget = 0;              // no chain targets
            }
            break;
    }

    // custom radius cases
    switch (m_spellInfo->SpellFamilyName)
    {
        case SPELLFAMILY_GENERIC:
        {
            switch (m_spellInfo->Id)
            {
                case 24811:                                 // Draw Spirit (Lethon)
                {
                    if (effIndex == EFFECT_INDEX_0)         // Copy range from EFF_1 to 0
                        radius = GetSpellRadius(sSpellRadiusStore.LookupEntry(m_spellInfo->EffectRadiusIndex[effIndex]));
                    break;
                }
                case 28241:                                 // Poison (Naxxramas, Grobbulus Cloud)
                case 54363:                                 // Poison (Naxxramas, Grobbulus Cloud) (H)
                {
                    uint32 auraId = (m_spellInfo->Id == 28241 ? 28158 : 54362);
                    if (SpellAuraHolder* auraHolder = m_caster->GetSpellAuraHolder(auraId))
                        radius = 0.5f * (60000 - auraHolder->GetAuraDuration()) * 0.001f;
                    break;
                }
                case 66881:                                 // Slime Pool (ToCrusader, Acidmaw & Dreadscale)
                case 67638:                                 // Slime Pool (ToCrusader, Acidmaw & Dreadscale) (Mode 1)
                case 67639:                                 // Slime Pool (ToCrusader, Acidmaw & Dreadscale) (Mode 2)
                case 67640:                                 // Slime Pool (ToCrusader, Acidmaw & Dreadscale) (Mode 3)
                    if (SpellAuraHolder* auraHolder = m_caster->GetSpellAuraHolder(66882))
                        radius = 0.5f * (60000 - auraHolder->GetAuraDuration()) * 0.001f;
                    break;
                case 56438:                                 // Arcane Overload
                    if (Unit* realCaster = GetAffectiveCaster())
                        radius = radius * realCaster->GetObjectScale();
                    break;
                case 42442:                                 // Vengeance Landing Cannonfire
                    radius = MAX_VISIBILITY_DISTANCE;
                    break;
                case 69057:                                 // Bone Spike Graveyard (Icecrown Citadel, Lord Marrowgar encounter, 10N)
                case 70826:                                 // Bone Spike Graveyard (Icecrown Citadel, Lord Marrowgar encounter, 25N)
                case 72088:                                 // Bone Spike Graveyard (Icecrown Citadel, Lord Marrowgar encounter, 10H)
                case 72089:                                 // Bone Spike Graveyard (Icecrown Citadel, Lord Marrowgar encounter, 25H)
                case 73142:                                 // Bone Spike Graveyard (during Bone Storm) (Icecrown Citadel, Lord Marrowgar encounter, 10N)
                case 73143:                                 // Bone Spike Graveyard (during Bone Storm) (Icecrown Citadel, Lord Marrowgar encounter, 25N)
                case 73144:                                 // Bone Spike Graveyard (during Bone Storm) (Icecrown Citadel, Lord Marrowgar encounter, 10H)
                case 73145:                                 // Bone Spike Graveyard (during Bone Storm) (Icecrown Citadel, Lord Marrowgar encounter, 25H)
                case 72350:                                 // Fury of Frostmourne
                case 72351:                                 // Fury of Frostmourne
                case 72706:                                 // Achievement Check
                case 72830:                                 // Achievement Check
                    radius = DEFAULT_VISIBILITY_INSTANCE;
                    break;
                default:
                    break;
            }
            break;
        }
        case SPELLFAMILY_DRUID:
        {
            switch (m_spellInfo->Id)
            {
                case 49376:                                 // Feral Charge - Cat
                    // No default radius for this spell, so we need to use the contact distance
                    radius = CONTACT_DISTANCE;
                    break;
            }
        }
        default:
            break;
    }
}

MaNGOS::unique_weak_ptr<Spell> Spell::GetWeakPtr() const
{
    return m_spellEvent->GetSpellWeakPtr();
}