#ifndef NETSERVER_H
#define NETSERVER_H

#include "config.h"
#include "network.h"
#include "data_manager.h"
#include "deck_manager.h"
#include <unordered_map>
#include "RoomInterface.h"
#include "field.h"
#include <mutex>

#define MODE_HANDICAP   0x10
#define MODE_ANY 0xFF
namespace ygo
{
class GameServer;
class RoomManager;






class DuelRoom:public RoomInterface
{
    public:
    unsigned char mode;
    enum State {WAITING,FULL,PLAYING,ZOMBIE,DEAD};
    State state;
    int getLfList();
private:
    char ultimo_game_message;
    bool reCheckLfList();
    int lflist;
    static void DuelTimer(evutil_socket_t fd, short events, void* arg);
    void updateUserTimeout(DuelPlayer* dp);
    void Victory(char winner);
    char last_winner;
    int getNumDuelPlayers();
    void updateServerState();
    void destroyGame();
    event* auto_idle;
    event* user_timeout;
    static void auto_idle_cb(evutil_socket_t fd, short events, void* arg);
    static void user_timeout_cb(evutil_socket_t fd, short events, void* arg);

    DuelMode* duel_mode;
    void EverybodyIsPlaying();
    int ReadyMessagesSent;
    int numPlayers;
    bool chatReady;

    void playerConnected(DuelPlayer* dp);
    void playerDisconnected(DuelPlayer* dp);
    int getMaxDuelPlayers();
    void clientStarted();
    void setState(State state);
    void toObserver(DuelPlayer*dp);
    void ShowPlayerOdds();
    void ShowPlayerScores();
    void flushPendingMessages();
    DuelPlayer * getDpFromType(unsigned char);
	public:
	void RoomChat(DuelPlayer* dp, std::wstring messaggio);
    void SystemChatToPlayer(DuelPlayer*dp, const std::wstring,bool isAdmin=false,int color = 0);
	void RemoteChatToPlayer(DuelPlayer*dp, std::wstring,int color = 0);
    DuelRoom(RoomManager*roomManager,GameServer*,unsigned char mode);
    void LeaveGame(DuelPlayer* dp);
    bool StartServer(unsigned short port);
    bool StartBroadcast();
    void StopServer();
    void StopBroadcast();
    void StopListen();
    void createGame();
    void DisconnectPlayer(DuelPlayer* dp);
    void HandleCTOSPacket(DuelPlayer* dp, char* data, unsigned int len);

    void InsertPlayer(DuelPlayer* dp);
    void ExtractPlayer(DuelPlayer* dp);
    bool isAvailableToPlayer(DuelPlayer* dp, unsigned char mode);
    using RoomInterface::SendPacketToPlayer;
    void SendPacketToPlayer(DuelPlayer* dp, unsigned char proto);
    void SendPacketToPlayer(DuelPlayer* dp, unsigned char proto,STOC_TypeChange);
    void SendPacketToPlayer(DuelPlayer* dp, unsigned char proto,STOC_HS_PlayerChange);
    void SendBufferToPlayer(DuelPlayer* dp, unsigned char proto, void* buffer, size_t len);
};

}

#endif //NETSERVER_H
