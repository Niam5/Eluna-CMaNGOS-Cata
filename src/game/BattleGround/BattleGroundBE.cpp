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

#include "Entities/Object.h"
#include "Entities/Player.h"
#include "BattleGround.h"
#include "BattleGroundBE.h"
#include "Server/WorldPacket.h"
#include "Tools/Language.h"

BattleGroundBE::BattleGroundBE()
{
    m_StartDelayTimes[BG_STARTING_EVENT_FIRST]  = BG_START_DELAY_1M;
    m_StartDelayTimes[BG_STARTING_EVENT_SECOND] = BG_START_DELAY_30S;
    m_StartDelayTimes[BG_STARTING_EVENT_THIRD]  = BG_START_DELAY_15S;
    m_StartDelayTimes[BG_STARTING_EVENT_FOURTH] = BG_START_DELAY_NONE;
    // we must set messageIds
    m_StartMessageIds[BG_STARTING_EVENT_FIRST]  = LANG_ARENA_ONE_MINUTE;
    m_StartMessageIds[BG_STARTING_EVENT_SECOND] = LANG_ARENA_THIRTY_SECONDS;
    m_StartMessageIds[BG_STARTING_EVENT_THIRD]  = LANG_ARENA_FIFTEEN_SECONDS;
    m_StartMessageIds[BG_STARTING_EVENT_FOURTH] = LANG_ARENA_HAS_BEGUN;
}

void BattleGroundBE::StartingEventOpenDoors()
{
    OpenDoorEvent(BG_EVENT_DOOR);
}

void BattleGroundBE::AddPlayer(Player* plr)
{
    BattleGround::AddPlayer(plr);
    // create score and add it to map, default values are set in constructor
    BattleGroundBEScore* sc = new BattleGroundBEScore;

    m_PlayerScores[plr->GetObjectGuid()] = sc;

    UpdateWorldState(0x9f1, GetAlivePlayersCountByTeam(ALLIANCE));
    UpdateWorldState(0x9f0, GetAlivePlayersCountByTeam(HORDE));
}

void BattleGroundBE::RemovePlayer(Player* /*plr*/, ObjectGuid /*guid*/)
{
    if (GetStatus() == STATUS_WAIT_LEAVE)
        return;

    UpdateWorldState(0x9f1, GetAlivePlayersCountByTeam(ALLIANCE));
    UpdateWorldState(0x9f0, GetAlivePlayersCountByTeam(HORDE));

    CheckArenaWinConditions();
}

void BattleGroundBE::HandleKillPlayer(Player* player, Player* killer)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    if (!killer)
    {
        sLog.outError("Killer player not found");
        return;
    }

    BattleGround::HandleKillPlayer(player, killer);

    UpdateWorldState(0x9f1, GetAlivePlayersCountByTeam(ALLIANCE));
    UpdateWorldState(0x9f0, GetAlivePlayersCountByTeam(HORDE));

    CheckArenaWinConditions();
}

bool BattleGroundBE::HandlePlayerUnderMap(Player* player)
{
    player->TeleportTo(GetMapId(), 6238.930176f, 262.963470f, 0.889519f, player->GetOrientation());
    return true;
}

void BattleGroundBE::FillInitialWorldStates(WorldPacket& data, uint32& count)
{
    FillInitialWorldState(data, count, 0x9f1, GetAlivePlayersCountByTeam(ALLIANCE));
    FillInitialWorldState(data, count, 0x9f0, GetAlivePlayersCountByTeam(HORDE));
    FillInitialWorldState(data, count, 0x9f3, 1);
}

void BattleGroundBE::UpdatePlayerScore(Player* source, uint32 type, uint32 value)
{
    BattleGroundScoreMap::iterator itr = m_PlayerScores.find(source->GetObjectGuid());
    if (itr == m_PlayerScores.end())                        // player not found...
        return;

    // there is nothing special in this score
    BattleGround::UpdatePlayerScore(source, type, value);
}
