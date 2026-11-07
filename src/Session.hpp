#pragma once

#include <Geode/Geode.hpp>
#include <deque>
#include <functional>
#include <unordered_map>
#include <unordered_set>

using namespace geode::prelude;

struct CollabPeer {
    std::string id;
    std::string name;
    float x = 0.f;
    float y = 0.f;
};

struct CollabOp {
    std::string type; // "add", "upd", "del"
    std::string id;
    std::string data; // object save string (empty for del)
};

// One collab session, tied to the currently open editor.
// Everything here runs on the main thread.
class CollabSession {
public:
    using DoneCallback = std::function<void(std::string const& error)>;

    static CollabSession* get();

    bool isActive() const { return m_active; }
    bool isBusy() const { return m_busy; }
    bool isHost() const { return m_isHost; }
    std::string const& roomCode() const { return m_room; }
    std::string const& lastError() const { return m_lastError; }
    std::vector<CollabPeer> const& peers() const { return m_peers; }

    // "ABC123@192.168.1.20:8484" - one thing to send a friend, no settings needed
    std::string inviteCode() const;
    // Splits "CODE@server" into {code, server}; server is empty for a bare code
    static std::pair<std::string, std::string> parseInvite(std::string const& text);

    static std::string serverUrl();
    static void setServerUrl(std::string const& url);
    // Checks the server is reachable
    void ping(DoneCallback done);

    // Called by CollabNode when an editor opens / closes
    void attach(LevelEditorLayer* layer);
    void detach(LevelEditorLayer* layer);

    void host(DoneCallback done);
    void join(std::string code, DoneCallback done);
    void leave();

    // Driven every frame by CollabNode
    void update(float dt);

    static std::string displayName();

private:
    using JsonCallback = std::function<void(matjson::Value const& json, std::string const& error)>;

    void request(std::string const& path, matjson::Value body, JsonCallback cb);
    void reset();

    void scanLocalChanges();
    void sendSync();
    void applyRemote();
    void applyOp(CollabOp const& op);
    void applyFullState(matjson::Value const& objects, bool wipeUntracked);

    void track(std::string const& id, GameObject* obj);
    void untrack(std::string const& id);
    void removeLocal(std::string const& id);
    bool objectAlive(GameObject* obj);

    cocos2d::CCPoint viewCenter();
    std::string newId();

    LevelEditorLayer* m_layer = nullptr;

    bool m_active = false;
    bool m_busy = false;
    bool m_isHost = false;
    bool m_inFlight = false;
    int m_generation = 0;
    int m_failures = 0;
    float m_timer = 0.f;

    std::string m_room;
    std::string m_client;
    std::string m_lastError;
    std::string m_lanAddress;
    long long m_seq = 0;
    long long m_counter = 0;

    std::unordered_map<std::string, Ref<GameObject>> m_idToObj;
    std::unordered_map<GameObject*, std::string> m_objToId;
    std::unordered_map<std::string, std::string> m_lastState;

    std::deque<CollabOp> m_outbox;
    std::deque<CollabOp> m_inbox;
    std::optional<matjson::Value> m_pendingResync;

    std::vector<CollabPeer> m_peers;
};
