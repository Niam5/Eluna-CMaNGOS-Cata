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

#ifndef __BATTLEGROUND_H
#define __BATTLEGROUND_H

#include "Common.h"
#include "Globals/SharedDefines.h"
#include "Maps/Map.h"
#include "Util/ByteBuffer.h"
#include "Entities/ObjectGuid.h"

// magic event-numbers
#define BG_EVENT_NONE 255
// those generic events should get a high event id
#define BG_EVENT_DOOR 254
// only arena event
// cause this buff apears 90sec after start in every bg i implement it here
#define ARENA_BUFF_EVENT 253
#define ARENA_TIMELIMIT_POINTS_LOSS -16

class Creature;
class GameObject;
class Group;
class Player;
class WorldPacket;
class BattleGroundMap;

struct PvPDifficultyEntry;
struct WorldSafeLocsEntry;

struct BattleGroundEventIdx
{
    uint8 event1;
    uint8 event2;
};

enum BattleGroundSounds
{
    SOUND_HORDE_WINS                = 8454,
    SOUND_ALLIANCE_WINS             = 8455,
    SOUND_BG_START                  = 3439
};

enum BattleGroundQuests
{
    SPELL_WS_QUEST_REWARD           = 43483,
    SPELL_AB_QUEST_REWARD           = 43484,
    SPELL_AV_QUEST_REWARD           = 43475,
    SPELL_AV_QUEST_KILLED_BOSS      = 23658,
    SPELL_EY_QUEST_REWARD           = 43477,
    SPELL_AB_QUEST_REWARD_4_BASES   = 24061,
    SPELL_AB_QUEST_REWARD_5_BASES   = 24064
};

enum BattleGroundMarks
{
    SPELL_WS_MARK_LOSER             = 24950,                // not create marks now
    SPELL_WS_MARK_WINNER            = 24951,                // not create marks now
    SPELL_AB_MARK_LOSER             = 24952,                // not create marks now
    SPELL_AB_MARK_WINNER            = 24953,                // not create marks now
    SPELL_AV_MARK_LOSER             = 24954,                // not create marks now
    SPELL_AV_MARK_WINNER            = 24955,                // not create marks now

    SPELL_WG_MARK_VICTORY           = 24955,                // honor + mark
    SPELL_WG_MARK_DEFEAT            = 58494,                // honor + mark
};

enum BattleGroundMarksCount
{
    ITEM_WINNER_COUNT               = 3,
    ITEM_LOSER_COUNT                = 1
};

enum BattleGroundSpells
{
    SPELL_ARENA_PREPARATION         = 32727,                // use this one, 32728 not correct
    SPELL_ALLIANCE_GOLD_FLAG        = 32724,
    SPELL_ALLIANCE_GREEN_FLAG       = 32725,
    SPELL_HORDE_GOLD_FLAG           = 35774,
    SPELL_HORDE_GREEN_FLAG          = 35775,
    SPELL_PREPARATION               = 44521,                // Preparation
    SPELL_RECENTLY_DROPPED_FLAG     = 42792,                // Recently Dropped Flag
    SPELL_AURA_PLAYER_INACTIVE      = 43681,                // Inactive
    SPELL_ARENA_DAMPENING           = 74410,                // Arena - Dampening
    SPELL_BATTLEGROUND_DAMPENING    = 74411,                // Battleground - Dampening
};

enum BattleGroundTimeIntervals
{
    CHECK_PLAYER_POSITION_INVERVAL  = 1000,                 // ms
    RESURRECTION_INTERVAL           = 30000,                // ms
    INVITATION_REMIND_TIME          = 20000,                // ms
    INVITE_ACCEPT_WAIT_TIME         = 60000,                // ms
    TIME_TO_AUTOREMOVE              = 120000,               // ms
    MAX_OFFLINE_TIME                = 300,                  // secs
    RESPAWN_ONE_DAY                 = 86400,                // secs
    RESPAWN_IMMEDIATELY             = 0,                    // secs
    BUFF_RESPAWN_TIME               = 180,                  // secs
    ARENA_SPAWN_BUFF_OBJECTS        = 90000,                // ms - 90sec after start
    ARENA_FORCED_DRAW               = 2700000,              // ms - 45min after start
};

enum BattleGroundStartTimeIntervals
{
    BG_START_DELAY_2M               = 120000,               // ms (2 minutes)
    BG_START_DELAY_1M               = 60000,                // ms (1 minute)
    BG_START_DELAY_30S              = 30000,                // ms (30 seconds)
    BG_START_DELAY_15S              = 15000,                // ms (15 seconds) Used only in arena
    BG_START_DELAY_NONE             = 0,                    // ms
};

enum BattleGroundBuffObjects
{
    BG_OBJECTID_SPEEDBUFF_ENTRY     = 179871,
    BG_OBJECTID_REGENBUFF_ENTRY     = 179904,
    BG_OBJECTID_BERSERKERBUFF_ENTRY = 179905
};

const uint32 Buff_Entries[3] = { BG_OBJECTID_SPEEDBUFF_ENTRY, BG_OBJECTID_REGENBUFF_ENTRY, BG_OBJECTID_BERSERKERBUFF_ENTRY };

enum BattleGroundStatus
{
    STATUS_NONE         = 0,                                // first status, should mean bg is not instance
    STATUS_WAIT_QUEUE   = 1,                                // means bg is empty and waiting for queue
    STATUS_WAIT_JOIN    = 2,                                // this means, that BG has already started and it is waiting for more players
    STATUS_IN_PROGRESS  = 3,                                // means bg is running
    STATUS_WAIT_LEAVE   = 4                                 // means some faction has won BG and it is ending
};

struct BattleGroundPlayer
{
    time_t  OfflineRemoveTime;                              // for tracking and removing offline players from queue after 5 minutes
    Team    PlayerTeam;                                     // Player's team
};

struct BattleGroundObjectInfo
{
    BattleGroundObjectInfo() : object(nullptr), timer(0), spellid(0) {}

    GameObject*  object;
    int32       timer;
    uint32      spellid;
};

// handle the queue types and bg types separately to enable joining queue for different sized arenas at the same time
enum BattleGroundQueueTypeId
{
    BATTLEGROUND_QUEUE_NONE     = 0,
    BATTLEGROUND_QUEUE_AV       = 1,
    BATTLEGROUND_QUEUE_WS       = 2,
    BATTLEGROUND_QUEUE_AB       = 3,
    BATTLEGROUND_QUEUE_EY       = 4,
    BATTLEGROUND_QUEUE_SA       = 5,
    BATTLEGROUND_QUEUE_IC       = 6,
    BATTLEGROUND_QUEUE_TP       = 7,
    BATTLEGROUND_QUEUE_BG       = 8,
    BATTLEGROUND_QUEUE_2v2      = 9,
    BATTLEGROUND_QUEUE_3v3      = 10,
    BATTLEGROUND_QUEUE_5v5      = 11,
};

#define MAX_BATTLEGROUND_QUEUE_TYPES 12

enum ScoreType
{
    SCORE_KILLING_BLOWS         = 1,
    SCORE_DEATHS                = 2,
    SCORE_HONORABLE_KILLS       = 3,
    SCORE_BONUS_HONOR           = 4,
    // EY, but in SMSG_PVP_LOG_DATA opcode!
    SCORE_DAMAGE_DONE           = 5,
    SCORE_HEALING_DONE          = 6,
    // WS
    SCORE_FLAG_CAPTURES         = 7,
    SCORE_FLAG_RETURNS          = 8,
    // AB
    SCORE_BASES_ASSAULTED       = 9,
    SCORE_BASES_DEFENDED        = 10,
    // AV
    SCORE_GRAVEYARDS_ASSAULTED  = 11,
    SCORE_GRAVEYARDS_DEFENDED   = 12,
    SCORE_TOWERS_ASSAULTED      = 13,
    SCORE_TOWERS_DEFENDED       = 14,
    SCORE_SECONDARY_OBJECTIVES  = 15
};

enum BattleGroundType
{
    TYPE_BATTLEGROUND     = 3,
    TYPE_ARENA            = 4
};

enum BattleGroundStartingEvents
{
    BG_STARTING_EVENT_NONE  = 0x00,
    BG_STARTING_EVENT_1     = 0x01,
    BG_STARTING_EVENT_2     = 0x02,
    BG_STARTING_EVENT_3     = 0x04,
    BG_STARTING_EVENT_4     = 0x08
};

enum BattleGroundStartingEventsIds
{
    BG_STARTING_EVENT_FIRST     = 0,
    BG_STARTING_EVENT_SECOND    = 1,
    BG_STARTING_EVENT_THIRD     = 2,
    BG_STARTING_EVENT_FOURTH    = 3
};
#define BG_STARTING_EVENT_COUNT 4

enum GroupJoinBattlegroundResult
{
    ERR_BATTLEGROUND_NONE                           = 0,
    ERR_GROUP_JOIN_BATTLEGROUND_DESERTERS           = 2,    // You cannot join the battleground yet because you or one of your party members is flagged as a Deserter.
    ERR_ARENA_TEAM_PARTY_SIZE                       = 3,    // Incorrect party size for this arena.
    ERR_BATTLEGROUND_TOO_MANY_QUEUES                = 4,    // You can only be queued for 2 battles at once
    ERR_BATTLEGROUND_CANNOT_QUEUE_FOR_RATED         = 5,    // You cannot queue for a rated match while queued for other battles
    ERR_BATTLEDGROUND_QUEUED_FOR_RATED              = 6,    // You cannot queue for another battle while queued for a rated arena match
    ERR_BATTLEGROUND_TEAM_LEFT_QUEUE                = 7,    // Your team has left the arena queue
    ERR_BATTLEGROUND_NOT_IN_BATTLEGROUND            = 8,    // You can't do that in a battleground.
    ERR_BATTLEGROUND_JOIN_XP_GAIN                   = 9,    // wtf, doesn't exist in client...
    ERR_BATTLEGROUND_JOIN_RANGE_INDEX               = 10,   // Cannot join the queue unless all members of your party are in the same battleground level range.
    ERR_BATTLEGROUND_JOIN_TIMED_OUT                 = 11,   // %s was unavailable to join the queue. (uint64 guid exist in client cache)
    ERR_BATTLEGROUND_JOIN_TIMED_OUT2                = 12,   // same as 11
    ERR_BATTLEGROUND_TEAM_LEFT_QUEUE2               = 13,   // same as 7
    ERR_LFG_CANT_USE_BATTLEGROUND                   = 14,   // You cannot queue for a battleground or arena while using the dungeon system.
    ERR_IN_RANDOM_BG                                = 15,   // Can't do that while in a Random Battleground queue.
    ERR_IN_NON_RANDOM_BG                            = 16,   // Can't queue for Random Battleground while in another Battleground queue.
    ERR_BG_DEVELOPER_ONLY                           = 17,
    ERR_BATTLEGROUND_INVITATION_DECLINED            = 18,
    ERR_MEETING_STONE_NOT_FOUND                     = 19,
    ERR_WARGAME_REQUEST_FAILURE                     = 20,
    ERR_BATTLEFIELD_TEAM_PARTY_SIZE                 = 22,
    ERR_NOT_ON_TOURNAMENT_REALM                     = 23,
    ERR_BATTLEGROUND_PLAYERS_FROM_DIFFERENT_REALMS  = 24,
    ERR_REMOVE_FROM_PVP_QUEUE_GRANT_LEVEL           = 33,
    ERR_REMOVE_FROM_PVP_QUEUE_FACTION_CHANGE        = 34,
    ERR_BATTLEGROUND_JOIN_FAILED                    = 35,
    ERR_BATTLEGROUND_DUPE_QUEUE                     = 43,
};

class BattleGroundScore
{
    public:
        BattleGroundScore() : KillingBlows(0), Deaths(0), HonorableKills(0),
            BonusHonor(0), DamageDone(0), HealingDone(0)
        {}
        virtual ~BattleGroundScore() {}                     // virtual destructor is used when deleting score from scores map

        uint32 GetKillingBlows() const      { return KillingBlows; }
        uint32 GetDeaths() const            { return Deaths; }
        uint32 GetHonorableKills() const    { return HonorableKills; }
        uint32 GetBonusHonor() const        { return BonusHonor; }
        uint32 GetDamageDone() const        { return DamageDone; }
        uint32 GetHealingDone() const       { return HealingDone; }

        virtual uint32 GetAttr1() const     { return 0; }
        virtual uint32 GetAttr2() const     { return 0; }
        virtual uint32 GetAttr3() const     { return 0; }
        virtual uint32 GetAttr4() const     { return 0; }
        virtual uint32 GetAttr5() const     { return 0; }

        uint32 KillingBlows;
        uint32 Deaths;
        uint32 HonorableKills;
        uint32 BonusHonor;
        uint32 DamageDone;
        uint32 HealingDone;
};

/*
This class is used to:
1. Add player to battleground
2. Remove player from battleground
3. some certain cases, same for all battlegrounds
4. It has properties same for all battlegrounds
*/
class BattleGround
{
        friend class BattleGroundMgr;

    public:
        /* Construction */
        BattleGround();
        /*BattleGround(const BattleGround& bg);*/
        virtual ~BattleGround();
        virtual void Update(uint32 diff);                   // must be implemented in BG subclass of BG specific update code, but must in begginning call parent version
        virtual void Reset();                               // resets all common properties for battlegrounds, must be implemented and called in BG subclass
        virtual void StartingEventCloseDoors() {}
        virtual void StartingEventOpenDoors() {}

        /* achievement req. */
        virtual bool IsAllNodesControlledByTeam(Team /*team*/) const { return false; }
        bool IsTeamScoreInRange(Team team, uint32 minScore, uint32 maxScore) const;

        /* Battleground */
        // Get methods:
        ObjectGuid GetObjectGuid() { return ObjectGuid(HIGHGUID_BATTLEGROUND, uint32(m_ArenaType), uint32(m_TypeID)); }
        char const* GetName() const         { return m_Name; }
        BattleGroundTypeId GetTypeId() const { return m_TypeID; }
        BattleGroundBracketId GetBracketId() const { return m_BracketId; }
        // the instanceId check is also used to determine a bg-template
        // that's why the m_map hack is here..
        uint32 GetInstanceID()              { return m_Map ? GetBgMap()->GetInstanceId() : 0; }
        BattleGroundStatus GetStatus() const { return m_Status; }
        uint32 GetClientInstanceID() const  { return m_ClientInstanceID; }
        uint32 GetStartTime() const         { return m_StartTime; }
        uint32 GetEndTime() const           { return m_EndTime; }
        uint32 GetMaxPlayers() const        { return m_MaxPlayers; }
        uint32 GetMinPlayers() const        { return m_MinPlayers; }

        uint32 GetMinLevel() const          { return m_LevelMin; }
        uint32 GetMaxLevel() const          { return m_LevelMax; }

        uint32 GetMaxPlayersPerTeam() const { return m_MaxPlayersPerTeam; }
        uint32 GetMinPlayersPerTeam() const { return m_MinPlayersPerTeam; }

        int32 GetStartDelayTime() const     { return m_StartDelayTime; }
        ArenaType GetArenaType() const          { return m_ArenaType; }
        Team GetWinner() const              { return m_Winner; }
        uint32 GetBattlemasterEntry() const;
        uint32 GetBonusHonorFromKill(uint32 kills) const;

        // Set methods:
        void SetName(char const* Name)      { m_Name = Name; }
        void SetTypeID(BattleGroundTypeId TypeID) { m_TypeID = TypeID; }
        // here we can count minlevel and maxlevel for players
        void SetBracket(PvPDifficultyEntry const* bracketEntry);
        void SetStatus(BattleGroundStatus Status) { m_Status = Status; }
        void SetClientInstanceID(uint32 InstanceID) { m_ClientInstanceID = InstanceID; }
        void SetStartTime(uint32 Time)      { m_StartTime = Time; }
        void SetEndTime(uint32 Time)        { m_EndTime = Time; }
        void SetMaxPlayers(uint32 MaxPlayers) { m_MaxPlayers = MaxPlayers; }
        void SetMinPlayers(uint32 MinPlayers) { m_MinPlayers = MinPlayers; }
        void SetLevelRange(uint32 min, uint32 max) { m_LevelMin = min; m_LevelMax = max; }
        void SetRated(bool state)           { m_IsRated = state; }
        void SetArenaType(ArenaType type)   { m_ArenaType = type; }
        void SetArenaorBGType(bool _isArena) { m_IsArena = _isArena; }
        void SetWinner(Team winner)         { m_Winner = winner; }

        void ModifyStartDelayTime(int diff) { m_StartDelayTime -= diff; }
        void SetStartDelayTime(int Time)    { m_StartDelayTime = Time; }

        void SetMaxPlayersPerTeam(uint32 MaxPlayers) { m_MaxPlayersPerTeam = MaxPlayers; }
        void SetMinPlayersPerTeam(uint32 MinPlayers) { m_MinPlayersPerTeam = MinPlayers; }

        void AddToBGFreeSlotQueue();                        // this queue will be useful when more battlegrounds instances will be available
        void RemoveFromBGFreeSlotQueue();                   // this method could delete whole BG instance, if another free is available

        void DecreaseInvitedCount(Team team)      { (team == ALLIANCE) ? --m_InvitedAlliance : --m_InvitedHorde; }
        void IncreaseInvitedCount(Team team)      { (team == ALLIANCE) ? ++m_InvitedAlliance : ++m_InvitedHorde; }
        uint32 GetInvitedCount(Team team) const
        {
            if (team == ALLIANCE)
                return m_InvitedAlliance;
            else
                return m_InvitedHorde;
        }
        bool HasFreeSlots() const;
        uint32 GetFreeSlotsForTeam(Team team) const;

        bool isArena() const        { return m_IsArena; }
        bool isBattleGround() const { return !m_IsArena; }
        bool isRated() const        { return m_IsRated; }

        typedef std::map<ObjectGuid, BattleGroundPlayer> BattleGroundPlayerMap;
        BattleGroundPlayerMap const& GetPlayers() const { return m_Players; }
        uint32 GetPlayersSize() const { return m_Players.size(); }

        typedef std::map<ObjectGuid, BattleGroundScore*> BattleGroundScoreMap;
        BattleGroundScoreMap::const_iterator GetPlayerScoresBegin() const { return m_PlayerScores.begin(); }
        BattleGroundScoreMap::const_iterator GetPlayerScoresEnd() const { return m_PlayerScores.end(); }
        uint32 GetPlayerScoresSize() const { return m_PlayerScores.size(); }

        void StartBattleGround();

        void StartTimedAchievement(AchievementCriteriaTypes type, uint32 entry);

        /* Location */
        void SetMapId(uint32 MapID) { m_MapId = MapID; }
        uint32 GetMapId() const { return m_MapId; }

        /* Map pointers */
        void SetBgMap(BattleGroundMap* map) { m_Map = map; }
        BattleGroundMap* GetBgMap() const
        {
            MANGOS_ASSERT(m_Map);
            return m_Map;
        }

        void SetTeamStartLoc(Team team, float X, float Y, float Z, float O);
        void GetTeamStartLoc(Team team, float& X, float& Y, float& Z, float& O) const
        {
            PvpTeamIndex idx = GetTeamIndexByTeamId(team);
            X = m_TeamStartLocX[idx];
            Y = m_TeamStartLocY[idx];
            Z = m_TeamStartLocZ[idx];
            O = m_TeamStartLocO[idx];
        }

        void SetStartMaxDist(float startMaxDist) { m_startMaxDist = startMaxDist; }
        float GetStartMaxDist() const { return m_startMaxDist; }

        /* Packet Transfer */
        // method that should fill worldpacket with actual world states (not yet implemented for all battlegrounds!)
        virtual void FillInitialWorldStates(WorldPacket& /*data*/, uint32& /*count*/) {}
        void SendPacketToTeam(Team team, WorldPacket const& packet, Player* sender = nullptr, bool self = true);
        void SendPacketToAll(WorldPacket const& packet);

        template<class Do>
        void BroadcastWorker(Do& _do);

        void PlaySoundToTeam(uint32 SoundID, Team team);
        void PlaySoundToAll(uint32 SoundID);
        void CastSpellOnTeam(uint32 SpellID, Team team);
        void RewardHonorToTeam(uint32 Honor, Team team);
        void RewardReputationToTeam(uint32 faction_id, uint32 Reputation, Team team);
        void RewardMark(Player* plr, uint32 count);
        void SendRewardMarkByMail(Player* plr, uint32 mark, uint32 count);
        void RewardItem(Player* plr, uint32 item_id, uint32 count);
        void RewardQuestComplete(Player* plr);
        void RewardSpellCast(Player* plr, uint32 spell_id);
        void UpdateWorldState(uint32 Field, uint32 Value);
        void UpdateWorldStateForPlayer(uint32 Field, uint32 Value, Player* Source);
        virtual void EndBattleGround(Team winner);
        void BlockMovement(Player* plr);

        void SendMessageToAll(int32 entry, ChatMsg type, Player const* source = nullptr);
        void SendYellToAll(int32 entry, uint32 language, ObjectGuid guid);
        void PSendMessageToAll(int32 entry, ChatMsg type, Player const* source, ...);

        // specialized version with 2 string id args
        void SendMessage2ToAll(int32 entry, ChatMsg type, Player const* source, int32 strId1 = 0, int32 strId2 = 0);
        void SendYell2ToAll(int32 entry, uint32 language, ObjectGuid guid, int32 arg1, int32 arg2);

        /* Raid Group */
        Group* GetBgRaid(Team team) const { return m_BgRaids[GetTeamIndexByTeamId(team)]; }
        void SetBgRaid(Team team, Group* bg_raid);

        virtual void UpdatePlayerScore(Player* Source, uint32 type, uint32 value);

        static PvpTeamIndex GetTeamIndexByTeamId(Team team) { return team == ALLIANCE ? TEAM_INDEX_ALLIANCE : TEAM_INDEX_HORDE; }
        uint32 GetPlayersCountByTeam(Team team) const { return m_PlayersCount[GetTeamIndexByTeamId(team)]; }
        uint32 GetAlivePlayersCountByTeam(Team team) const; // used in arenas to correctly handle death in spirit of redemption / last stand etc. (killer = killed) cases
        void UpdatePlayersCountByTeam(Team team, bool remove)
        {
            if (remove)
                --m_PlayersCount[GetTeamIndexByTeamId(team)];
            else
                ++m_PlayersCount[GetTeamIndexByTeamId(team)];
        }

        // used for rated arena battles
        void SetArenaTeamIdForTeam(Team team, uint32 ArenaTeamId) { m_ArenaTeamIds[GetTeamIndexByTeamId(team)] = ArenaTeamId; }
        uint32 GetArenaTeamIdForTeam(Team team) const             { return m_ArenaTeamIds[GetTeamIndexByTeamId(team)]; }
        void SetArenaTeamRatingChangeForTeam(Team team, int32 RatingChange) { m_ArenaTeamRatingChanges[GetTeamIndexByTeamId(team)] = RatingChange; }
        int32 GetArenaTeamRatingChangeForTeam(Team team) const    { return m_ArenaTeamRatingChanges[GetTeamIndexByTeamId(team)]; }
        void CheckArenaWinConditions();

        /* Triggers handle */
        // must be implemented in BG subclass
        virtual bool HandleAreaTrigger(Player* /*Source*/, uint32 /*Trigger*/) { return false;  }
        // must be implemented in BG subclass if need AND call base class generic code
        virtual void HandleKillPlayer(Player* player, Player* killer);
        virtual void HandleKillUnit(Creature* /*unit*/, Player* /*killer*/) {}

        // Process Capture event
        virtual bool HandleEvent(uint32 /*eventId*/, GameObject* /*go*/) { return false; }

        // Called when a creature is created
        virtual void HandleCreatureCreate(Creature* /*creature*/) {}

        // Called when a gameobject is created
        virtual void HandleGameObjectCreate(GameObject* /*go*/) {}

        /* Battleground events */
        virtual void EventPlayerDroppedFlag(Player* /*player*/) {}
        virtual void EventPlayerClickedOnFlag(Player* /*player*/, GameObject* /*target_obj*/) {}
        virtual void EventPlayerCapturedFlag(Player* /*player*/) {}
        void EventPlayerLoggedIn(Player* player);
        void EventPlayerLoggedOut(Player* player);

        /* Death related */
        virtual WorldSafeLocsEntry const* GetClosestGraveYard(Player* player);

        virtual void AddPlayer(Player* plr);                // must be implemented in BG subclass

        void AddOrSetPlayerToCorrectBgGroup(Player* plr, ObjectGuid plr_guid, Team team);

        virtual void RemovePlayerAtLeave(ObjectGuid guid, bool Transport, bool SendPacket);
        // can be extended in in BG subclass

        /* event related */
        // called when a creature gets added to map (NOTE: only triggered if
        // a player activates the cell of the creature)
        void OnObjectDBLoad(Creature* /*creature*/);
        void OnObjectDBLoad(GameObject* /*obj*/);
        // (de-)spawns creatures and gameobjects from an event
        void SpawnEvent(uint8 event1, uint8 event2, bool spawn);
        bool IsActiveEvent(uint8 event1, uint8 event2)
        {
            if (m_ActiveEvents.find(event1) == m_ActiveEvents.end())
                return false;
            return m_ActiveEvents[event1] == event2;
        }
        ObjectGuid GetSingleCreatureGuid(uint8 event1, uint8 event2);

        void OpenDoorEvent(uint8 event1, uint8 event2 = 0);
        bool IsDoor(uint8 event1, uint8 event2);

        void HandleTriggerBuff(ObjectGuid go_guid);

        void SpawnBGObject(ObjectGuid guid, uint32 respawntime);
        void SpawnBGCreature(ObjectGuid guid, uint32 respawntime);

        void DoorOpen(ObjectGuid guid);
        void DoorClose(ObjectGuid guid);

        virtual Team GetPrematureWinner();

        virtual bool HandlePlayerUnderMap(Player* /*plr*/) { return false; }

        // since arenas can be AvA or Hvh, we have to get the "temporary" team of a player
        Team GetPlayerTeam(ObjectGuid guid);
        static Team GetOtherTeam(Team team) { return team ? ((team == ALLIANCE) ? HORDE : ALLIANCE) : TEAM_NONE; }
        static PvpTeamIndex GetOtherTeamIndex(PvpTeamIndex teamIdx) { return teamIdx == TEAM_INDEX_ALLIANCE ? TEAM_INDEX_HORDE : TEAM_INDEX_ALLIANCE; }
        bool IsPlayerInBattleGround(ObjectGuid guid);

        /* virtual score-array - get's used in bg-subclasses */
        int32 m_TeamScores[PVP_TEAM_COUNT];

        struct EventObjects
        {
            GuidVector gameobjects;
            GuidVector creatures;
        };

        // cause we create it dynamicly i use a map - to avoid resizing when
        // using vector - also it contains 2*events concatenated with PAIR32
        // this is needed to avoid overhead of a 2dimensional std::map
        std::map<uint32, EventObjects> m_EventObjects;
        // this must be filled first in BattleGroundXY::Reset().. else
        // creatures will get added wrong
        // door-events are automaticly added - but _ALL_ other must be in this vector
        std::map<uint8, uint8> m_ActiveEvents;

        MaNGOS::unique_weak_ptr<BattleGround> GetWeakPtr() const { return m_weakRef; }
        void SetWeakPtr(MaNGOS::unique_weak_ptr<BattleGround> weakRef) { m_weakRef = std::move(weakRef); }

    protected:
        // this method is called, when BG cannot spawn its own spirit guide, or something is wrong, It correctly ends BattleGround
        void EndNow();
        void PlayerAddedToBGCheckIfBGIsRunning(Player* plr);

        /* Scorekeeping */

        BattleGroundScoreMap m_PlayerScores;                // Player scores
        // must be implemented in BG subclass
        virtual void RemovePlayer(Player* /*player*/, ObjectGuid /*guid*/) {}

        /* Player lists, those need to be accessible by inherited classes */
        BattleGroundPlayerMap  m_Players;

        /*
        these are important variables used for starting messages
        */
        uint8 m_Events;
        BattleGroundStartTimeIntervals  m_StartDelayTimes[BG_STARTING_EVENT_COUNT];
        // this must be filled in constructors!
        uint32 m_StartMessageIds[BG_STARTING_EVENT_COUNT];

        bool   m_BuffChange;

    private:
        /* Battleground */
        BattleGroundTypeId m_TypeID;
        BattleGroundStatus m_Status;
        uint32 m_ClientInstanceID;                          // the instance-id which is sent to the client and without any other internal use
        uint32 m_StartTime;
        bool m_ArenaBuffSpawned;                            // to cache if arenabuff event is started (cause bool is faster than checking IsActiveEvent)
        uint32 m_validStartPositionTimer;
        int32 m_EndTime;                                    // it is set to 120000 when bg is ending and it decreases itself
        BattleGroundBracketId m_BracketId;
        ArenaType  m_ArenaType;                             // 2=2v2, 3=3v3, 5=5v5
        bool   m_InBGFreeSlotQueue;                         // used to make sure that BG is only once inserted into the BattleGroundMgr.BGFreeSlotQueue[bgTypeId] deque
        bool   m_IsArena;
        Team   m_Winner;
        int32  m_StartDelayTime;
        bool   m_IsRated;                                   // is this battle rated?
        bool   m_PrematureCountDown;
        uint32 m_PrematureCountDownTimer;
        char const* m_Name;

        /* Player lists */
        typedef std::deque<ObjectGuid> OfflineQueue;
        OfflineQueue m_OfflineQueue;                        // Player GUID

        /* Invited counters are useful for player invitation to BG - do not allow, if BG is started to one faction to have 2 more players than another faction */
        /* Invited counters will be changed only when removing already invited player from queue, removing player from battleground and inviting player to BG */
        /* Invited players counters*/
        uint32 m_InvitedAlliance;
        uint32 m_InvitedHorde;

        /* Raid Group */
        Group* m_BgRaids[PVP_TEAM_COUNT];                   // 0 - alliance, 1 - horde

        /* Players count by team */
        uint32 m_PlayersCount[PVP_TEAM_COUNT];

        /* Arena team ids by team */
        uint32 m_ArenaTeamIds[PVP_TEAM_COUNT];

        int32 m_ArenaTeamRatingChanges[PVP_TEAM_COUNT];

        /* Limits */
        uint32 m_LevelMin;
        uint32 m_LevelMax;
        uint32 m_MaxPlayersPerTeam;
        uint32 m_MaxPlayers;
        uint32 m_MinPlayersPerTeam;
        uint32 m_MinPlayers;

        /* Start location */
        uint32 m_MapId;
        BattleGroundMap* m_Map;
        float m_TeamStartLocX[PVP_TEAM_COUNT];
        float m_TeamStartLocY[PVP_TEAM_COUNT];
        float m_TeamStartLocZ[PVP_TEAM_COUNT];
        float m_TeamStartLocO[PVP_TEAM_COUNT];
        float m_startMaxDist;

        MaNGOS::unique_weak_ptr<BattleGround> m_weakRef;
};

// helper functions for world state list fill
inline void FillInitialWorldState(ByteBuffer& data, uint32& count, uint32 state, uint32 value)
{
    data << uint32(state);
    data << uint32(value);
    ++count;
}

inline void FillInitialWorldState(ByteBuffer& data, uint32& count, uint32 state, int32 value)
{
    data << uint32(state);
    data << int32(value);
    ++count;
}

inline void FillInitialWorldState(ByteBuffer& data, uint32& count, uint32 state, bool value)
{
    data << uint32(state);
    data << uint32(value ? 1 : 0);
    ++count;
}

struct WorldStatePair
{
    uint32 state;
    uint32 value;
};

inline void FillInitialWorldState(ByteBuffer& data, uint32& count, WorldStatePair const* array)
{
    for (WorldStatePair const* itr = array; itr->state; ++itr)
    {
        data << uint32(itr->state);
        data << uint32(itr->value);
        ++count;
    }
}

#endif
