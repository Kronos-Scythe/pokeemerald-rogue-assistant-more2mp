#pragma once
#include "Defines.h"
#include "GameConnectionBehaviour.h"
#include "Timer.h"
#include "enet/enet.h"

#include <atomic>
#include <mutex>
#include <string>
#include <deque>
#include <vector>

class MultiplayerBehaviour : public IGameConnectionBehaviour
{
public:
	static u16 const c_DefaultPort;

	// Hard cap on players in one session (host included). The game's own
	// netPlayerCount can lower this further, but never raise it.
	static u8 const c_MaxPlayerCount;

	MultiplayerBehaviour();

	virtual void OnAttach(GameConnection& game) override;
	virtual void OnDetach(GameConnection& game) override;
	virtual void OnUpdate(GameConnection& game) override;

	bool IsRequestingHostConnection() const;
	inline bool IsHost() const { return m_NetServer.load(std::memory_order_relaxed) != nullptr; }
	inline u16 GetPort() const { return m_Port.load(std::memory_order_relaxed); }

	bool IsAwaitingAddress() const { return !m_HasAttemptedConnection.load(std::memory_order_acquire); }
	bool IsConnected() const { return m_ConnState.load(std::memory_order_relaxed) >= ConnectionState::Connected; }

	// Players currently in the session, host included (host only - clients report 0)
	inline u8 GetConnectedPlayerCount() const { return m_ConnectedPlayerCount.load(std::memory_order_relaxed); }
	inline u8 GetMaxPlayerCount() const { return m_MaxPlayerCount.load(std::memory_order_relaxed); }
	std::string SanitiseConnectionAddress(std::string const& address);
	void ProvideConnectionAddress(std::string const& address);

private:
	enum class ConnectionState
	{
		AwaitingHandshake,
		AwaitingResponse,
		ConnectionConfirmed,
		Connected,

		Default = AwaitingHandshake
	};

	struct QueuedHandshake
	{
		ENetPeer* m_Peer;
		std::vector<u8> m_Data;
	};

	struct ServerState
	{
		std::vector<u8> m_PlayerProfiles;

		// The game has a single handshake slot, so with 2+ clients joining at
		// once the handshakes have to be fed through it one at a time.
		std::deque<QueuedHandshake> m_HandshakeQueue;
		ENetPeer* m_PendingHandshake;

		// What the handshake slot held just before we wrote the pending
		// handshake, so a leftover response from the previous client isn't
		// mistaken for the game's reply to this one.
		std::vector<u8> m_StaleHandshake;
		bool m_HasSeenHandshakeCleared;
		TimeDurationNS m_PendingHandshakeStart;

		UpdateTimer m_GameStateTimer;
		UpdateTimer m_PlayerStateTimer;

		ServerState()
			: m_PendingHandshake(nullptr)
			, m_HasSeenHandshakeCleared(false)
			, m_PendingHandshakeStart(0)
			, m_GameStateTimer(UpdateTimer::c_5UPS)
			, m_PlayerStateTimer(UpdateTimer::c_30UPS)
		{}
	};

	struct ClientState
	{
		UpdateTimer m_PlayerStateTimer;

		ClientState()
			: m_PlayerStateTimer(UpdateTimer::c_30UPS)
		{}
	};

	void OpenHostConnection(GameConnection& game);
	void OpenClientConnection(GameConnection& game);
	void CloseConnection(GameConnection& game);

	void PollConnection(GameConnection& game);
	void ConnectedUpdate(GameConnection& game);
	void HandleIncomingMessage(GameConnection& game, ENetEvent& netEvent);
	void HandlePeerDisconnect(GameConnection& game, ENetPeer* peer);

	void BeginNextHandshake(GameConnection& game);
	void UpdatePendingHandshake(GameConnection& game);
	void AbortPendingHandshake(char const* reason);
	bool IsPlayerIdInUse(u8 playerId, ENetPeer* ignorePeer) const;
	void RefreshConnectedPlayerCount();

	void SendMultiplayerConfirmationToGame(GameConnection& game);

	// PrimaryUI::Render reads these from the window thread while OnUpdate mutates
	// them on the connection thread, and ProvideConnectionAddress is a straight
	// cross-thread write from the window thread.
	std::atomic<u16> m_Port;
	std::atomic<ConnectionState> m_ConnState;

	mutable std::mutex m_ConnectionAddressMutex;
	std::string m_ConnectionAddressRaw;

	std::atomic<bool> m_HasAttemptedConnection;
	std::atomic<u8> m_RequestFlags;
	std::atomic<ENetHost*> m_NetServer;
	std::atomic<u8> m_ConnectedPlayerCount;
	std::atomic<u8> m_MaxPlayerCount;

	// Connection thread only
	u8 m_PlayerId;
	ENetHost* m_NetClient;
	ENetPeer* m_NetPeer;

	ServerState m_ServerState;
	ClientState m_ClientState;
};