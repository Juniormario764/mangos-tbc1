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

#include "Server/WorldSession.h"
#include "Log.h"
#include "Globals/ObjectMgr.h"
#include "World/World.h"

enum ClassRoles
{
    LFG_ROLE_NONE = 0x00,
    LFG_ROLE_TANK = 0x01,
    LFG_ROLE_HEALER = 0x02,
    LFG_ROLE_DPS = 0x04
};

std::map<uint32, int32> GetTalentTrees(Player* _player)
{
    std::map<uint32, int32> tabs;
    for (uint32 i = 0; i < uint32(3); i++)
        tabs[i] = 0;

    uint32 classMask = _player->getClassMask();
    for (uint32 i = 0; i < sTalentStore.GetNumRows(); ++i)
    {
        TalentEntry const *talentInfo = sTalentStore.LookupEntry(i);
        if (!talentInfo)
            continue;

        TalentTabEntry const *talentTabInfo = sTalentTabStore.LookupEntry(talentInfo->TalentTab);
        if (!talentTabInfo)
            continue;

        if ((classMask & talentTabInfo->ClassMask) == 0)
            continue;

        int maxRank = 0;
        for (int rank = MAX_TALENT_RANK - 1; rank >= 0; --rank)
        {
            if (!talentInfo->RankID[rank])
                continue;

            uint32 spellid = talentInfo->RankID[rank];
            if (spellid && _player->HasSpell(spellid))
                maxRank = rank + 1;

        }
        tabs[talentTabInfo->tabpage] += maxRank;
    }

    return tabs;
}

int GetHighestTalentTree(Player* _player)
{
    if (_player->getLevel() >= 10 && !_player->GetFreeTalentPoints())
    {
        std::map<uint32, int32> tabs = GetTalentTrees(_player);

        int tab = -1, max = 0;
        for (uint32 i = 0; i < uint32(3); i++)
        {
            if (tab == -1 || max < tabs[i])
            {
                tab = i;
                max = tabs[i];
            }
        }
        return tab;
    }
    else
    {
        int tab = 0;

        switch (_player->getClass())
        {
        case CLASS_MAGE:
            tab = 1;
            break;
        case CLASS_PALADIN:
            tab = 2;
            break;
        case CLASS_PRIEST:
            tab = 1;
            break;
        }
        return tab;
    }
}

ClassRoles CalculateTalentRoles(Player* _player)
{
    ClassRoles role = LFG_ROLE_NONE;
    int tab = GetHighestTalentTree(_player);
    switch (_player->getClass())
    {
    case CLASS_PRIEST:
        if (tab == 2)
            role = LFG_ROLE_DPS;
        else
            role = LFG_ROLE_HEALER;
        break;
    case CLASS_SHAMAN:
        if (tab == 2)
            role = LFG_ROLE_HEALER;
        else
            role = LFG_ROLE_DPS;
        break;
    case CLASS_WARRIOR:
        if (tab == 2)
            role = LFG_ROLE_TANK;
        else
            role = LFG_ROLE_DPS;
        break;
    case CLASS_PALADIN:
        if (tab == 0)
            role = LFG_ROLE_HEALER;
        else if (tab == 1)
            role = LFG_ROLE_TANK;
        else if (tab == 2)
            role = LFG_ROLE_DPS;
        break;
    case CLASS_DRUID:
        if (tab == 0)
            role = LFG_ROLE_DPS;
        else if (tab == 1)
            role = (ClassRoles)(LFG_ROLE_TANK | LFG_ROLE_DPS);
        else if (tab == 2)
            role = LFG_ROLE_HEALER;
        break;
    default:
        role = LFG_ROLE_DPS;
        break;
    }
    return role;
}

bool IsCompatibleWithPlayer(Player* newPlayer, Player* oldPlayer)
{
    // new player
    ClassRoles newRole = CalculateTalentRoles(newPlayer);

    // other player
    ClassRoles oldRole = CalculateTalentRoles(oldPlayer);

    // check if same role/class
    // skip if both tanks
    if ((newRole & LFG_ROLE_TANK) && (oldRole & LFG_ROLE_TANK))
    {
        return false;
    }

    // skip if both heals
    if ((newRole & LFG_ROLE_HEALER) && (oldRole & LFG_ROLE_HEALER))
    {
        return false;
    }

    // skip if dps of same class
    if ((newRole & LFG_ROLE_DPS) && (oldRole & LFG_ROLE_DPS))
    {
        if (newPlayer->getClass() == oldPlayer->getClass())
            return false;
    }

    return true;
}

bool IsCompatibleWithGroup(Player* newPlayer, Group* group, bool sameClass = false)
{
    // new player
    ClassRoles newRole = CalculateTalentRoles(newPlayer);

    // group
    if (group)
    {
        uint32 dps = 0;
        uint32 heal = 0;
        uint32 tank = 0;
        uint32 offtank = 0;
        uint32 offdps = 0;
        uint32 notCompatible = 0;
        uint32 sameDpsClass = 0;
        uint8 myClass = newPlayer->getClass();
        bool strictCheck = group->GetMembersCount() > 2;

        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->getSource();

            if (!member)
                continue;

            //if (strictCheck && !IsCompatibleWithPlayer(newPlayer, member))
            //    return false;

            ClassRoles mRole = CalculateTalentRoles(member);

            if (mRole & LFG_ROLE_DPS)
            {
                // feral druid
                if (mRole & LFG_ROLE_TANK && (tank || dps < 3))
                {
                    offdps++;
                }
                else
                    dps++;

                if (member->getClass() == myClass)
                    sameDpsClass++;
            }

            if (mRole & LFG_ROLE_HEALER)
                heal++;

            if (mRole & LFG_ROLE_TANK)
            {
                // feral druid
                if (mRole & LFG_ROLE_DPS && (!tank || dps >= 3))
                {
                    offtank++;
                }
                else
                    tank++;
            }
        }

        // handle hybrid (feral)
        // if no tank count feral druid as tank to speed up queue
        if (!tank && offtank)
        {
            tank++;
            offtank--;
            offdps--;
        }
        // if more hybrids in group count as dps
        if (dps < 3 && offdps && offtank)
        {
            dps += offdps;
        }

        if ((dps >= 3 || sameDpsClass) && (newRole & LFG_ROLE_DPS) && !(newRole & LFG_ROLE_TANK))
            return false;

        if (heal >= 1 && (newRole & LFG_ROLE_HEALER))
            return false;

        if (tank >= 1 && (newRole & LFG_ROLE_TANK) && !(newRole & LFG_ROLE_DPS))
            return false;
    }

    return true;
}

static void AttemptJoin(Player* _player)
{
    // skip not can autojoin cases and player group case
    if (!_player->m_lookingForGroup.canAutoJoin() || _player->GetGroup())
        return;

    // TODO: Guard Player Map
    HashMapHolder<Player>::MapType const& players = sObjectAccessor.GetPlayers();
    for (HashMapHolder<Player>::MapType::const_iterator iter = players.begin(); iter != players.end(); ++iter)
    {
        Player* plr = iter->second;

        // skip enemies and self
        if (!plr || plr == _player || plr->GetTeam() != _player->GetTeam())
            continue;

        // skip players not in world or reconnecting
        if (!plr->IsInWorld() || plr->GetSession()->IsOffline())
            continue;

        // skip not auto add
        if (!plr->GetSession()->LookingForGroup_auto_add)
            continue;

        // skip non auto-join or empty slots, or non compatible slots
        if (!plr->m_lookingForGroup.more.canAutoJoin() || !_player->m_lookingForGroup.HaveInSlot(plr->m_lookingForGroup.more))
            continue;

        Group* grp = plr->GetGroup();

        // skip player in a battleground, not group leader, group is full cases
        if (grp && (grp->isBattleGroup() || grp->IsFull() || grp->GetLeaderGuid() != plr->GetObjectGuid()))
            continue;

        // check roles based on talents
        if (grp)
        {
            if (!IsCompatibleWithGroup(_player, grp))
                continue;
        }
        else
        {
            if (!IsCompatibleWithPlayer(_player, plr))
                continue;
        }

        // attempt create group, or skip
        if (!grp)
        {
            Group* group = new Group;

            if (!group->Create(plr->GetObjectGuid(), plr->GetName()))
            {
                delete group;
                continue;
            }

            sObjectMgr.AddGroup(group);
        }

        // stop at success join
        if (plr->GetGroup()->AddMember(_player->GetObjectGuid(), _player->GetName()))
        {
            if (sWorld.getConfig(CONFIG_BOOL_CHANNEL_RESTRICTED_LFG) && _player->GetSession()->GetSecurity() == SEC_PLAYER)
                _player->LeaveLFGChannel();
            break;
        }
        // full
        else
        {
            if (sWorld.getConfig(CONFIG_BOOL_CHANNEL_RESTRICTED_LFG) && plr->GetSession()->GetSecurity() == SEC_PLAYER)
                plr->LeaveLFGChannel();
        }
    }
}

static void AttemptAddMore(Player* _player)
{
    // skip non auto-join slot
    if (!_player->m_lookingForGroup.more.canAutoJoin())
        return;

    // skip player in a battleground, not group leader, group is full cases
    Group* _group = _player->GetGroup();

    if (_group && (_group->isBattleGroup() || _group->IsFull() || _group->GetLeaderGuid() != _player->GetObjectGuid()))
        return;

    // TODO: Guard Player map
    HashMapHolder<Player>::MapType const& players = sObjectAccessor.GetPlayers();
    for (HashMapHolder<Player>::MapType::const_iterator iter = players.begin(); iter != players.end(); ++iter)
    {
        Player* plr = iter->second;

        // skip enemies and self
        if (!plr || plr == _player || plr->GetTeam() != _player->GetTeam())
            continue;

        // skip players not in world or reconnecting
        if (!plr->IsInWorld() || plr->GetSession()->IsOffline())
            continue;

        // skip non auto-join or empty slots, or non compatible slots
        if (!plr->GetSession()->LookingForGroup_auto_join || !plr->m_lookingForGroup.HaveInSlot(_player->m_lookingForGroup.more))
            continue;

        // skip players in groups
        if (plr->GetGroup())
            continue;

        // check roles based on talents
        if (_group)
        {
            if (!IsCompatibleWithGroup(plr, _group))
                continue;
        }
        else
        {
            if (!IsCompatibleWithPlayer(plr, _player))
                continue;
        }

        // attempt create group if needed, or stop attempts
        if (!_group)
        {
            Group* group = new Group;

            if (!group->Create(_player->GetObjectGuid(), _player->GetName()))
            {
                delete group;
                return;                                     // can't create group (??)
            }

            sObjectMgr.AddGroup(group);
        }

        // stop at join fail (full)
        if (!_player->GetGroup()->AddMember(plr->GetObjectGuid(), plr->GetName()))
        {
            if (sWorld.getConfig(CONFIG_BOOL_CHANNEL_RESTRICTED_LFG) && _player->GetSession()->GetSecurity() == SEC_PLAYER)
                _player->LeaveLFGChannel();

            break;
        }

        // joined
        if (sWorld.getConfig(CONFIG_BOOL_CHANNEL_RESTRICTED_LFG) && plr->GetSession()->GetSecurity() == SEC_PLAYER)
            plr->LeaveLFGChannel();

        // and group full
        if (_player->GetGroup()->IsFull())
        {
            if (sWorld.getConfig(CONFIG_BOOL_CHANNEL_RESTRICTED_LFG) && _player->GetSession()->GetSecurity() == SEC_PLAYER)
                _player->LeaveLFGChannel();

            break;
        }
    }
}

void WorldSession::HandleLfgSetAutoJoinOpcode(WorldPacket& /*recv_data*/)
{
    DEBUG_LOG("CMSG_LFG_SET_AUTOJOIN");
    LookingForGroup_auto_join = true;

    if (!_player)                                           // needed because STATUS_AUTHED
        return;

    AttemptJoin(_player);
}

void WorldSession::HandleLfgClearAutoJoinOpcode(WorldPacket& /*recv_data*/)
{
    DEBUG_LOG("CMSG_LFG_CLEAR_AUTOJOIN");
    LookingForGroup_auto_join = false;
}

void WorldSession::HandleLfmSetAutoFillOpcode(WorldPacket& /*recv_data*/)
{
    DEBUG_LOG("CMSG_LFM_SET_AUTOFILL");
    LookingForGroup_auto_add = true;

    if (!_player)                                           // needed because STATUS_AUTHED
        return;

    AttemptAddMore(_player);
}

void WorldSession::HandleLfmClearAutoFillOpcode(WorldPacket& /*recv_data*/)
{
    DEBUG_LOG("CMSG_LFM_CLEAR_AUTOFILL");
    LookingForGroup_auto_add = false;
}

void WorldSession::HandleLfgClearOpcode(WorldPacket& /*recv_data */)
{
    // empty packet
    DEBUG_LOG("CMSG_CLEAR_LOOKING_FOR_GROUP");

    for (int i = 0; i < MAX_LOOKING_FOR_GROUP_SLOT; ++i)
        _player->m_lookingForGroup.slots[i].Clear();

    if (sWorld.getConfig(CONFIG_BOOL_CHANNEL_RESTRICTED_LFG) && _player->GetSession()->GetSecurity() == SEC_PLAYER)
        _player->LeaveLFGChannel();
}

void WorldSession::HandleSetLfgOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("CMSG_SET_LOOKING_FOR_GROUP");

    uint32 slot, temp;

    recv_data >> slot >> temp;

    uint32 entry = (temp & 0xFFFF);
    uint32 type = ((temp >> 24) & 0xFFFF);

    if (slot >= MAX_LOOKING_FOR_GROUP_SLOT)
        return;

    _player->m_lookingForGroup.slots[slot].Set(entry, type);
    DEBUG_LOG("LFG set: looknumber %u, temp %X, type %u, entry %u", slot, temp, type, entry);

    if (LookingForGroup_auto_join)
        AttemptJoin(_player);

    SendLFGListQueryResponse(LfgType(type), entry);
}

void WorldSession::HandleLfmClearOpcode(WorldPacket& /*recv_data */)
{
    // empty packet
    DEBUG_LOG("CMSG_CLEAR_LOOKING_FOR_MORE");

    _player->m_lookingForGroup.more.Clear();
}

void WorldSession::HandleSetLfmOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("CMSG_SET_LOOKING_FOR_MORE");
    // recv_data.hexlike();
    uint32 temp;

    recv_data >> temp;

    uint32 entry = (temp & 0xFFFF);
    uint32 type = ((temp >> 24) & 0xFFFF);

    _player->m_lookingForGroup.more.Set(entry, type);
    DEBUG_LOG("LFM set: temp %u, zone %u, type %u", temp, entry, type);

    if (LookingForGroup_auto_add)
        AttemptAddMore(_player);

    SendLFGListQueryResponse(LfgType(type), entry);
}

void WorldSession::HandleSetLfgCommentOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("CMSG_SET_LFG_COMMENT");
    // recv_data.hexlike();

    std::string comment;
    recv_data >> comment;
    DEBUG_LOG("LFG comment %s", comment.c_str());

    _player->m_lookingForGroup.comment = comment;
}

void WorldSession::HandleLFGListQuery(WorldPacket& recv_data)
{
    DEBUG_LOG("MSG_LOOKING_FOR_GROUP");
    // recv_data.hexlike();
    uint32 type, entry, unk;

    recv_data >> type >> entry >> unk;
    DEBUG_LOG("MSG_LOOKING_FOR_GROUP: type %u, entry %u, unk %u", type, entry, unk);

    if (LookingForGroup_auto_add)
        AttemptAddMore(_player);

    if (LookingForGroup_auto_join)
        AttemptJoin(_player);

    SendLFGListQueryResponse(LfgType(type), entry);
}

void WorldSession::SendLFGListQueryResponse(LfgType type, uint32 entry)
{
    // start prepare packet
    WorldPacket data(MSG_LOOKING_FOR_GROUP);
    data << uint32(type);                                   // type
    data << uint32(entry);                                  // entry from LFGDungeons.dbc
    data << uint32(0);                                      // displayed players count, placeholder
    data << uint32(0);                                      // found players count, placeholder

    uint32 displayed = 0;
    uint32 found = 0;

    // TODO: Guard Player map
    HashMapHolder<Player>::MapType const& players = sObjectAccessor.GetPlayers();
    for (const auto& i : players)
    {
        Player* plr = i.second;

        if (!plr || plr->GetTeam() != _player->GetTeam())
            continue;

        if (!plr->IsInWorld() || plr->GetSession()->IsOffline())
            continue;

        if (!plr->m_lookingForGroup.HaveInSlot(entry, type) && !plr->m_lookingForGroup.more.Is(entry, type))
            continue;

        const Group* grp = plr->GetGroup();

        if (grp && (grp->isBattleGroup() || grp->IsFull() || !grp->IsLeader(plr->GetObjectGuid())))
            continue;

        ++found;

        // Client hardcoded limitation on amount of players sent in the packet handler and UI:
        if (found > 50)
            continue;

        ++displayed;

        const bool lfm = plr->m_lookingForGroup.more.Is(entry, type);

        data << plr->GetPackGUID();                         // packed guid
        data << uint32(plr->getLevel());                    // level
        data << uint32(plr->GetZoneId());                   // current zone
        data << uint8(lfm);                                 // 0x00 - LFG, 0x01 - LFM

        if (lfm)
        {
            data << uint32(plr->m_lookingForGroup.more.entry | (plr->m_lookingForGroup.more.type << 24));
            data << uint32(0x1000000);
            data << uint32(0x1000000);
        }
        else
        {
            for (uint8 j = 0; j < MAX_LOOKING_FOR_GROUP_SLOT; ++j)
                data << uint32(plr->m_lookingForGroup.slots[j].entry | (plr->m_lookingForGroup.slots[j].type << 24));
        }

        data << plr->m_lookingForGroup.comment;

        data << uint32(0);                                  // other group members count, placeholder

        if (grp)
        {
            const size_t offset = (data.wpos() - 4);        // other group members count, offset
            uint32 count = 0;                               // other group members count

            for (const GroupReference* itr = grp->GetFirstMember(); itr != nullptr; itr = itr->next())
            {
                if (const Player* member = itr->getSource())
                {
                    if (member->GetObjectGuid() != plr->GetObjectGuid())
                    {
                        data << member->GetPackGUID();      // packed guid
                        data << uint32(member->getLevel()); // player level
                        ++count;
                    }
                }
            }

            data.put<uint32>(offset, count);            // other group members count, fill the placeholder
        }
    }

    // fill count placeholders
    data.put<uint32>(4 + 4,  displayed);
    data.put<uint32>(4 + 4 + 4, found);

    SendPacket(data);
}

void WorldSession::SendLFGUpdateLFG()
{
    WorldPacket data(SMSG_LFG_UPDATE_LFG, (4 * MAX_LOOKING_FOR_GROUP_SLOT));
    for (uint8 i = 0; i < MAX_LOOKING_FOR_GROUP_SLOT; ++i)
        data << uint32(_player->m_lookingForGroup.slots[i].entry | (_player->m_lookingForGroup.slots[i].type << 24));
    SendPacket(data);
}

void WorldSession::SendLFGUpdateLFM()
{
    WorldPacket data(SMSG_LFG_UPDATE_LFM);
    if (_player->m_lookingForGroup.more.Empty())
        data << uint8(0);
    else
    {
        data << uint8(1);
        data << uint32(_player->m_lookingForGroup.more.entry | (_player->m_lookingForGroup.more.type << 24));
    }
    SendPacket(data);
}
