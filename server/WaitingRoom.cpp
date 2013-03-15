#include "WaitingRoom.h"
#include "GameServer.h"
#include "debug.h"
#include "Users.h"
#include "Config.h"
namespace ygo
{
int WaitingRoom::minSecondsWaiting;
int WaitingRoom::maxSecondsWaiting;
const std::string WaitingRoom::banner = "Checkmate Server!";

WaitingRoom::WaitingRoom(RoomManager*roomManager,GameServer*gameServer):
    CMNetServerInterface(roomManager,gameServer),cicle_users(0)
{
    WaitingRoom::minSecondsWaiting=Config::getInstance()->waitingroom_min_waiting;
    WaitingRoom::maxSecondsWaiting=Config::getInstance()->waitingroom_max_waiting;
    event_base* net_evbase=roomManager->net_evbase;
    cicle_users = event_new(net_evbase, 0, EV_TIMEOUT | EV_PERSIST, cicle_users_cb, const_cast<WaitingRoom*>(this));
    periodic_updates_ev = event_new(net_evbase, 0, EV_TIMEOUT | EV_PERSIST, periodic_updates, const_cast<WaitingRoom*>(this));
    timeval timeout = {1, 0};
    event_add(cicle_users, &timeout);
    timeval timeout2 = {0, 100000};
    event_add(periodic_updates_ev, &timeout2);
}

WaitingRoom::~WaitingRoom()
{
    event_free(cicle_users);
    event_free(periodic_updates_ev);
}

void WaitingRoom::periodic_updates(evutil_socket_t fd, short events, void* arg)
{
    WaitingRoom*that = (WaitingRoom*)arg;
    for(auto it=that->players.begin(); it!=that->players.end(); ++it)
    {
        it->second.secondsWaiting+= 0.1;

        if(it->second.secondsWaiting< 1.5)
            continue;
        int cycle = 10 * (it->second.secondsWaiting-1.5);

        int start = cycle % (banner.length() + 10);
        if(start >= banner.length())
            start = 0;
        std::string newstr = banner.substr(start) + banner.substr(0,start);
        STOC_HS_PlayerEnter scpe;
        BufferIO::CopyWStr(newstr.c_str(), scpe.name, 20);

        scpe.pos = 1;
        //that->SendPacketToPlayer(it->first, STOC_HS_PLAYER_ENTER, scpe);


    }




}

void WaitingRoom::cicle_users_cb(evutil_socket_t fd, short events, void* arg)
{
    WaitingRoom*that = (WaitingRoom*)arg;

    if(!that->players.size())
        return;

    std::list<DuelPlayer*> players_bored;
    int numPlayersReady=0;
    for(auto it=that->players.begin(); it!=that->players.end(); ++it)
    {
        char nome[30];
        BufferIO::CopyWStr(it->first->name,nome,30);
        if(!that->players[it->first].isReady)
            continue;
        //log(INFO,"%s aspetta da %d secondi\n",nome,it->second.secondsWaiting);
        if(it->second.secondsWaiting>= maxSecondsWaiting)
            players_bored.push_back(it->first);
        if(it->second.secondsWaiting>= minSecondsWaiting)
            numPlayersReady++;
    }

    if(players_bored.size() && numPlayersReady>=2)
        for (auto it = players_bored.cbegin(); it != players_bored.cend(); ++it)
        {
            if(that->players.find(*it) != that->players.end())
            {

                that->ExtractPlayer(*it);
                that->roomManager->InsertPlayer(*it);
            }
        }
}


void WaitingRoom::ExtractPlayer(DuelPlayer* dp)
{
    players.erase(dp);
    dp->netServer=0;
    updateObserversNum();
}

void WaitingRoom::updateObserversNum()
{
    int num = players.size() -1;
    STOC_HS_WatchChange scwc;
    scwc.watch_count = num;
    for(auto it=players.begin(); it!=players.end(); ++it)
    {
        SendPacketToPlayer(it->first, STOC_HS_WATCH_CHANGE, scwc);
    }
}

void WaitingRoom::ChatWithPlayer(DuelPlayer*dp, std::string sender,std::string message)
{
    STOC_HS_PlayerEnter scpe;
    STOC_HS_PlayerChange scpc1;
    scpc1.status = (NETPLAYER_TYPE_PLAYER2 << 4) | PLAYERCHANGE_LEAVE;
    SendPacketToPlayer(dp, STOC_HS_PLAYER_CHANGE, scpc1);

    BufferIO::CopyWStr(sender.c_str(), scpe.name, 20);
    scpe.pos = 1;
    SendPacketToPlayer(dp, STOC_HS_PLAYER_ENTER, scpe);

    STOC_Chat scc;
    scc.player = NETPLAYER_TYPE_PLAYER2;
    int msglen = BufferIO::CopyWStr(message.c_str(), scc.msg, 256);
    SendBufferToPlayer(dp, STOC_CHAT, &scc, 4 + msglen * 2);

    scpc1.status = (NETPLAYER_TYPE_PLAYER2 << 4) | PLAYERCHANGE_LEAVE;
    SendPacketToPlayer(dp, STOC_HS_PLAYER_CHANGE, scpc1);

    BufferIO::CopyWStr(banner.c_str(), scpe.name, 20);
    scpe.pos = 1;
    SendPacketToPlayer(dp, STOC_HS_PLAYER_ENTER, scpe);

}

void WaitingRoom::SendNameToPlayer(DuelPlayer* dp,uint8_t pos,std::string message)
{
    STOC_HS_PlayerEnter scpe;
    BufferIO::CopyWStr(message.c_str(), scpe.name, 20);
    scpe.pos = pos;
    SendPacketToPlayer(dp, STOC_HS_PLAYER_ENTER, scpe);

}


void WaitingRoom::InsertPlayer(DuelPlayer* dp)
{
    dp->netServer=this;
    players[dp] = DuelPlayerInfo();

    HostInfo info;
    info.rule=2;
    info.mode=MODE_TAG;
    info.draw_count=1;
    info.no_check_deck=false;
    info.start_hand=5;
    info.lflist=1;
    info.time_limit=120;
    info.start_lp=8000;
    info.enable_priority=false;
    info.no_shuffle_deck=false;

    unsigned int hash = 1;
    for(auto lfit = deckManager._lfList.begin(); lfit != deckManager._lfList.end(); ++lfit)
    {
        if(info.lflist == lfit->hash)
        {
            hash = info.lflist;
            break;
        }
    }
    if(hash == 1)
        info.lflist = deckManager._lfList[0].hash;

    dp->type = NETPLAYER_TYPE_PLAYER1;

    STOC_JoinGame scjg;
    scjg.info=info;
    SendPacketToPlayer(dp, STOC_JOIN_GAME, scjg);

    STOC_TypeChange sctc;
    sctc.type = dp->type;
    SendPacketToPlayer(dp, STOC_TYPE_CHANGE, sctc);


    STOC_HS_PlayerEnter scpe;
    BufferIO::CopyWStr(dp->name, scpe.name, 20);
    scpe.pos = 0;
    SendPacketToPlayer(dp, STOC_HS_PLAYER_ENTER, scpe);

    //STOC_HS_PlayerEnter scpe;
    BufferIO::CopyWStr(banner.c_str(), scpe.name, 20);
    scpe.pos = 1;
    SendPacketToPlayer(dp, STOC_HS_PLAYER_ENTER, scpe);

    //usleep(50000);
    /*SendMessageToPlayer(dp,);
    SendMessageToPlayer(dp,"Type !tag to enter a tag duel, !single for a single duel or !match");
*/
    ChatWithPlayer(dp, "CheckMate","Welcome to the CheckMate server!");
    ChatWithPlayer(dp, "CheckMate","Type !tag to enter a tag duel, !single for a single duel or !match");

    updateObserversNum();

    playerReadinessChange(dp,true);

    STOC_HS_PlayerChange scpc;
    scpc.status = (dp->type << 4) | PLAYERCHANGE_READY;
    SendPacketToPlayer(dp, STOC_HS_PLAYER_CHANGE, scpc);

    char message[256];
    char name[20];
    BufferIO::CopyWStr(dp->name,name,20);
    std::string username(name);
    int rank = Users::getInstance()->getRank(username);
    int score = dp->cachedRankScore;

    switch (dp->loginStatus)
    {
    case Users::LoginResult::AUTHENTICATED:
        sprintf(message, "Rank:  %d",rank);
        SendNameToPlayer(dp,2,message);
        sprintf(message, "Score: %d",score);
        SendNameToPlayer(dp,3,message);
        break;

    case Users::LoginResult::INVALIDPASSWORD:
        SendNameToPlayer(dp,2,"Unregistered user");
        SendNameToPlayer(dp,3,"invalid password");
        break;

    case Users::LoginResult::INVALIDUSERNAME:
        SendNameToPlayer(dp,2,"Unregistered user");
        SendNameToPlayer(dp,3,"invalid username");
        break;

    case Users::LoginResult::NOPASSWORD:
        sprintf(message, "Rank:  %d",rank);
        SendNameToPlayer(dp,2,message);
        SendNameToPlayer(dp,3,"You need a password");
        break;

    case Users::LoginResult::UNRANKED:
        SendNameToPlayer(dp,2,"Unranked player!");
        SendNameToPlayer(dp,3,":-)");
        break;
    }

    if(dp->loginStatus != Users::LoginResult::AUTHENTICATED)
    {
        ChatWithPlayer(dp, "CheckMate","to register and login, go back and change the username to yourusername$yourpassword");
        //SendMessageToPlayer(dp,"to register and login, go back and change the username to yourusername$yourpassword");
    }

}
void WaitingRoom::LeaveGame(DuelPlayer* dp)
{
    ExtractPlayer(dp);
    gameServer->DisconnectPlayer(dp);
}

DuelPlayer* WaitingRoom::ExtractBestMatchPlayer(unsigned int referenceScore)
{
    if(!players.size())
        return nullptr;

    int qdifference = 0;
    DuelPlayer *chosenOne = nullptr;

    for(auto it=players.cbegin(); it!=players.cend(); ++it)
    {
        if(it->second.secondsWaiting >= minSecondsWaiting)
        {
            DuelPlayer* dp = it->first;
            if(!players[dp].isReady)
                continue;

            char opname[20];
            BufferIO::CopyWStr(dp->name,opname,20);
            int opscore = dp->cachedRankScore;

            int candidate_qdifference = abs(referenceScore-opscore);
            if(chosenOne == nullptr || candidate_qdifference < qdifference)
            {
                qdifference = candidate_qdifference;
                chosenOne = dp;
                continue;
            }

        }
    }

    int maxqdifference = std::max(400U,referenceScore/4);
    if( qdifference > maxqdifference)
        chosenOne = nullptr;
    if(chosenOne != nullptr)
    {
        ExtractPlayer(chosenOne);
        log(INFO,"qdifference = %d\n",qdifference);
    }
    return chosenOne;
}

DuelPlayer* WaitingRoom::ExtractBestMatchPlayer(DuelPlayer* referencePlayer)
{
    int score = referencePlayer->cachedRankScore;
    return ExtractBestMatchPlayer(score);
}

bool WaitingRoom::handleChatCommand(DuelPlayer* dp,unsigned short* msg)
{
    char messaggio[256];
    int msglen = BufferIO::CopyWStr(msg, messaggio, 256);
    log(INFO,"ricevuto messaggio %s\n",messaggio);

    if(!strcmp(messaggio,"!tag") || !strcmp(messaggio,"!t"))
    {
        ExtractPlayer(dp);
        return roomManager->InsertPlayer(dp,MODE_TAG);

    }
    else if(!strcmp(messaggio,"!single") || !strcmp(messaggio,"!s"))
    {
        ExtractPlayer(dp);
        return roomManager->InsertPlayer(dp,MODE_SINGLE);
    }
    else if(!strcmp(messaggio,"!match") || !strcmp(messaggio,"!m"))
    {
        ExtractPlayer(dp);
        return roomManager->InsertPlayer(dp,MODE_MATCH);
    }
    else
    {
        char sender[20];
        BufferIO::CopyWStr(dp->name,sender,20);
        for(auto it=players.begin(); it!=players.end(); ++it)
        {
            if(it->first== dp)
                continue;
            ChatWithPlayer(it->first, std::string(sender),messaggio);
        }

        return false;
    }


}
void WaitingRoom::HandleCTOSPacket(DuelPlayer* dp, char* data, unsigned int len)
{
    char*pdata=data;
    unsigned char pktType = BufferIO::ReadUInt8(pdata);

    switch(pktType)
    {

    case CTOS_PLAYER_INFO:
    {

        //log(INFO,"WaitingRoom:Player joined %s \n",name);
        break;
    }
    case CTOS_CHAT:
    {
        handleChatCommand(dp,(unsigned short*)pdata);
        break;
    }
    case CTOS_LEAVE_GAME:
    {
        LeaveGame(dp);
        break;
    }
    case CTOS_JOIN_GAME:
    {
        InsertPlayer(dp);
        break;
    }
    case CTOS_HS_READY:
    case CTOS_HS_NOTREADY:
    {

        playerReadinessChange(dp,CTOS_HS_NOTREADY - pktType);
        break;
    }
    case CTOS_HS_TODUELIST:
    {
        ExtractPlayer(dp);
        roomManager->InsertPlayer(dp,MODE_SINGLE);
        break;
    }
    }
}

}
