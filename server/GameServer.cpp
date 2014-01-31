#include "GameServer.h"

#include "RoomManager.h"
#include "debug.h"
#include <time.h>
#include <netinet/tcp.h>
#include <thread>
#include "Statistics.h"

#include "Users.h"

using ygo::Config;
namespace ygo
{
GameServer::GameServer()
{
    net_evbase = 0;
    listener = nullptr;
    last_sent = 0;
    MAXPLAYERS = Config::getInstance()->max_users_per_process;
}

bool GameServer::StartServer(int server_fd,int manager_fd)
{
    if(net_evbase)
        return false;
    net_evbase = event_base_new();
    if(!net_evbase)
        return false;

    listener =evconnlistener_new(net_evbase,ServerAccept, this, LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE ,-1,server_fd);

    evutil_make_socket_nonblocking(server_fd);
    if(!listener)
    {
        event_base_free(net_evbase);
        net_evbase = 0;
        return false;
    }
    isListening=true;
    evconnlistener_set_error_cb(listener, ServerAcceptError);
    roomManager.setGameServer(const_cast<ygo::GameServer *>(this));


    manager_buf = bufferevent_socket_new(net_evbase, manager_fd, BEV_OPT_CLOSE_ON_FREE);
    bufferevent_setcb(manager_buf, ManagerRead, NULL, ManagerEvent, this);
    bufferevent_enable(manager_buf, EV_READ|EV_WRITE);


    return true;
}

void GameServer::ManagerRead(bufferevent *bev, void *ctx)
{

    GameServer* that = (GameServer*)ctx;
    evbuffer* input = bufferevent_get_input(bev);
    size_t len = evbuffer_get_length(input);
    unsigned short packet_len = 0;

    while(true)
    {
        if(len < sizeof(MessageType))
            return;
        MessageType mt;
        evbuffer_copyout(input, &mt, sizeof(mt));
        if(mt == MessageType::CHAT && len >= sizeof(GameServerChat))
        {
            GameServerChat gsc;
            evbuffer_remove(input, &gsc, sizeof(gsc));

            len -= sizeof(gsc);
            that->roomManager.BroadcastMessage(gsc.messaggio,gsc.isAdmin,true);
        }
        else
            return;

    }

}
void GameServer::ManagerEvent(bufferevent* bev, short events, void* ctx)
{
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR))
    {
        bufferevent_free(bev);
    }
}


int GameServer::getNumPlayers()
{
    return users.size();


}
GameServer::~GameServer()
{
    if(listener)
    {
        StopServer();
    }

    while(users.size() > 0)
    {
        log(WARN,"waiting for reboot, users connected: %lu\n",users.size());
        sleep(5);
    }

    if(net_evbase)
        event_base_loopbreak(net_evbase);
    while(net_evbase)
    {
        log(WARN,"waiting for server thread\n");
        sleep(1);
    }
}
void GameServer::StopServer()
{
    if(!net_evbase)
        return;

    if(listener == nullptr)
        return;
    StopListen();
    evconnlistener_free(listener);
    listener = nullptr;


}

void GameServer::StopListen()
{
    evconnlistener_disable(listener);
    isListening = false;
}

void GameServer::ServerAccept(evconnlistener* listener, evutil_socket_t fd, sockaddr* address, int socklen, void* ctx)
{
    GameServer* that = (GameServer*)ctx;

    int optval=1;
    int optlen = sizeof(optval);
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &optval, optlen);
    optval = 120;
    setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, &optval, optlen);
    optval = 4;
    setsockopt(fd, SOL_TCP, TCP_KEEPCNT, &optval, optlen);
    optval = 30;
    setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, &optval, optlen);


    bufferevent* bev = bufferevent_socket_new(that->net_evbase, fd, BEV_OPT_CLOSE_ON_FREE);
    DuelPlayer dp;
    dp.name[0] = 0;
    dp.type = 0xff;
    dp.bev = bev;
    dp.netServer=0;
    dp.loginStatus = Users::LoginResult::NOTENTERED;
    sockaddr_in* sa = (sockaddr_in*)address;

    inet_ntop(AF_INET, &(sa->sin_addr), dp.ip, INET_ADDRSTRLEN);

    dp.countryCode = Users::getInstance()->getCountryCode(std::string(dp.ip));

    /*
        //prendo il reverse hostname
        char node[NI_MAXHOST];

        int res = getnameinfo(address, socklen, node, sizeof(node), nullptr, 0, 0);
        if (res)
        {
            printf("%s\n", gai_strerror(res));
            //exit(1);
        }
        else
        printf("indirizzo: %s\n",node);
    */

    that->users[bev] = dp;
    bufferevent_setcb(bev, ServerEchoRead, NULL, ServerEchoEvent, ctx);
    bufferevent_enable(bev, EV_READ);
    if(that->users.size()>= that->MAXPLAYERS)
    {
        that->StopListen();

    }

    Statistics::getInstance()->setNumPlayers(that->getNumPlayers());
}
void GameServer::ServerAcceptError(evconnlistener* listener, void* ctx)
{
    GameServer* that = (GameServer*)ctx;
    that->StopListen();
}



void GameServer::callChatCallback(std::wstring message,bool isAdmin)
{
    GameServerChat gsc;
    gsc.type = CHAT;
    wcscpy(gsc.messaggio,message.c_str());
    gsc.isAdmin = isAdmin;
    bufferevent_write(manager_buf,&gsc,sizeof(GameServerChat));
}

void GameServer::injectChatMessage(std::wstring a,bool b)
{
    std::lock_guard<std::mutex> lock(injectedMessages_mutex);
    injectedMessages.push_back(std::pair<std::wstring,bool>(a,b));
}

DuelPlayer* GameServer::findPlayer(std::wstring nome)
{
    for(auto it = loggedUsers.begin(); it!=loggedUsers.end(); ++it)
    {
        if(it->first ==nome)
        {
            return (it->second);
        }
    }

    return nullptr;
}
void GameServer::ServerEchoRead(bufferevent *bev, void *ctx)
{
    GameServer* that = (GameServer*)ctx;
    evbuffer* input = bufferevent_get_input(bev);
    size_t len = evbuffer_get_length(input);
    unsigned short packet_len = 0;
    while(true)
    {
        if(len < 2)
            return;
        evbuffer_copyout(input, &packet_len, 2);
        if(len < packet_len + 2)
            return;
        evbuffer_remove(input, that->net_server_read, packet_len + 2);
        if(packet_len)
            that->HandleCTOSPacket(&(that->users[bev]), &(that->net_server_read[2]), packet_len);
        len -= packet_len + 2;
    }
}
void GameServer::ServerEchoEvent(bufferevent* bev, short events, void* ctx)
{
    GameServer* that = (GameServer*)ctx;
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR))
    {
        DuelPlayer* dp = &(that->users[bev]);
        if(dp->netServer)
        {
            dp->netServer->LeaveGame(dp);
            if(that->users.find(bev)!= that->users.end())
            {
                log(BUG,"BUG: tcp terminated but disconnectplayer not called\n");
                that->DisconnectPlayer(dp);
            }

        }
        else that->DisconnectPlayer(dp);
    }
}

void GameServer::RestartListen()
{
    if(!isListening)
    {
        evconnlistener_enable(listener);
        isListening = true;
    }
}

int GameServer::CheckAliveThread(void* parama)
{
    GameServer*that = (GameServer*)parama;

    if(!that->net_evbase)
        return 0;

    static time_t last_check = time(NULL);
    int sleepSeconds = 30;

    if(time(NULL)- last_check < sleepSeconds)
        return 0;

    if( !that->isAlive)
    {
        abort();
        exit(EXIT_FAILURE);
    }
    that->isAlive=false;
    last_check = time(NULL);

    return 0;
}
void GameServer::keepAlive(evutil_socket_t fd, short events, void* arg)
{
    GameServer*that = (GameServer*) arg;

    that->roomManager.BroadcastMessage(Config::getInstance()->spam_string,true,true);
}

void GameServer::sendStats(evutil_socket_t fd, short events, void* arg)
{
    GameServer *that = (GameServer*) arg;
    GameServerStats gss;
    gss.rooms = Statistics::getInstance()->getNumRooms();
    gss.players = Statistics::getInstance()->getNumPlayers();
    gss.isAlive = that->listener != nullptr;
    gss.type = STATS;
    bufferevent_write(that->manager_buf, &gss,sizeof(GameServerStats));

    if(!gss.isAlive && !that->getNumPlayers())
        event_base_loopbreak(that->net_evbase);

}
void GameServer::checkInjectedMessages_cb(evutil_socket_t fd, short events, void* arg)
{
    GameServer*that = (GameServer*) arg;
    std::lock_guard<std::mutex> lock(that->injectedMessages_mutex);
    if(that->injectedMessages.size() == 0)
        return;

    for(auto it = that->injectedMessages.begin(); it!=that->injectedMessages.end(); ++it)
    {




    }

    that->injectedMessages.clear();

}


int GameServer::ServerThread(void* parama)
{
    GameServer*that = (GameServer*)parama;
    //std::thread checkAlive(CheckAliveThread, that);
    event* keepAliveEvent = event_new(that->net_evbase, 0, EV_TIMEOUT | EV_PERSIST, keepAlive, that);
    timeval timeout = {600, 0};
    event_add(keepAliveEvent, &timeout);

    event* statsEvent = event_new(that->net_evbase, 0, EV_TIMEOUT | EV_PERSIST, sendStats, that);
    timeval statstimeout = {5, 0};
    event_add(statsEvent, &statstimeout);

    /*event* cicle_injected = event_new(that->net_evbase, 0, EV_TIMEOUT | EV_PERSIST, checkInjectedMessages_cb, parama);
    timeval timeout2 = {0, 200000};
    event_add(cicle_injected, &timeout2);
    */

    event_base_dispatch(that->net_evbase);
    event_free(keepAliveEvent);
    event_free(statsEvent);
    //event_free(cicle_injected);
    event_base_free(that->net_evbase);
    that->net_evbase = 0;
    //checkAlive.join();
    return 0;
}

bool GameServer::dispatchPM(std::wstring recipient,std::wstring message)
{
    std::transform(recipient.begin(), recipient.end(), recipient.begin(), ::tolower);
    printf("dispatch\n");
    DuelPlayer* dp = findPlayer(recipient);
    if(dp==nullptr)
        return false;

    dp->netServer->SystemChatToPlayer(dp,message,false,5);
    printf("completed\n");
    return true;
}
bool GameServer::sendPM(std::wstring recipient,std::wstring message)
{
    return dispatchPM(recipient,message);


}


void GameServer::DisconnectPlayer(DuelPlayer* dp)
{
    auto bit = users.find(dp->bev);
    if(bit != users.end())
    {
        if(dp->loginStatus == Users::LoginResult::NOPASSWORD || dp->loginStatus == Users::LoginResult::AUTHENTICATED)
        {

            wchar_t nome[25];
            BufferIO::CopyWStr(dp->name,nome,20);
            std::wstring nomes(nome);
            std::transform(nomes.begin(), nomes.end(), nomes.begin(), ::tolower);
            log(VERBOSE,"rimuovo %Ls, %d\n",nomes.c_str(),(int)loggedUsers.size());
            if(loggedUsers.find(nomes)!=loggedUsers.end())
                loggedUsers.erase(nomes);
        }

        dp->netServer=NULL;
        bufferevent_flush(dp->bev, EV_WRITE, BEV_FLUSH);
        bufferevent_disable(dp->bev, EV_READ);
        bufferevent_free(dp->bev);
        users.erase(bit);



    }

    if(listener != nullptr && users.size()< MAXPLAYERS)
        RestartListen();
    Statistics::getInstance()->setNumPlayers(users.size());
}


void GameServer::HandleCTOSPacket(DuelPlayer* dp, char* data, unsigned int len)
{
    char* pdata = data;
    unsigned char pktType = BufferIO::ReadUInt8(pdata);

    if(dp->netServer == NULL)
    {
        if(pktType==CTOS_PLAYER_INFO && dp->loginStatus == Users::LoginResult::NOTENTERED)
        {
            CTOS_PlayerInfo* pkt = (CTOS_PlayerInfo*)pdata;


            BufferIO::CopyWStr(pkt->name,dp->name,20);

            dp->loginStatus = Users::LoginResult::WAITINGJOIN;

            return;
        }
        else if(pktType == CTOS_JOIN_GAME && dp->name[0] != 0 && dp->loginStatus == Users::LoginResult::WAITINGJOIN)
        {
            CTOS_JoinGame * ctjg =(CTOS_JoinGame*) pdata;
            char loginstring[45];
            int c1 = BufferIO::CopyWStr(dp->name,loginstring,20);

            int passc = BufferIO::CopyWStr(ctjg->pass,&loginstring[c1+1],20);
            if(passc > 0)
                ;//loginstring[c1] = '$';

            auto result = Users::getInstance()->login(std::string(loginstring),dp->ip);

            BufferIO::CopyWStr(result.first.c_str(), dp->name, 20);
            dp->loginStatus = result.second;
            dp->color = result.color;

            auto score = Users::getInstance()->getFullScore(result.first);
            dp->cachedRankScore = score.first;
            dp->cachedGameScore = score.second;

            if(roomManager.InsertPlayerInWaitingRoom(dp))
            {
                wchar_t nome[25];
                BufferIO::CopyWStr(dp->name,nome,20);
                std::wstring nomes(nome);
                std::transform(nomes.begin(), nomes.end(), nomes.begin(), ::tolower);
                BufferIO::CopyWStr(nomes.c_str(),dp->namew_low,20);

                if(dp->loginStatus == Users::LoginResult::NOPASSWORD || dp->loginStatus == Users::LoginResult::AUTHENTICATED)
                {
                    loggedUsers[nomes] = dp;
                }

            }
            else
                return;
        }
        else if(!strcmp(data,"ping"))
        {

            printf("pong\n");
            bufferevent_write(dp->bev, "pong", 5);
            bufferevent_flush(dp->bev, EV_WRITE, BEV_FLUSH);
            //DisconnectPlayer(dp);
            return;
        }
        else if(!strncmp(data,"ipchange",8))
        {

            inet_ntop(AF_INET, &data[8], dp->ip, INET_ADDRSTRLEN);
            return;
        }

        else
        {
            log(WARN,"player info is not the first packet\n");
            return;
        }
    }

    roomManager.HandleCTOSPacket(dp,data,len);
    return;
}

}

