#include "Session.hpp"

#include <Geode/utils/web.hpp>
#include <Geode/utils/async.hpp>

namespace {
    std::string str(matjson::Value const& obj, std::string_view key) {
        if (!obj.isObject() || !obj.contains(key)) return "";
        return obj[key].asString().unwrapOr("");
    }

    long long num(matjson::Value const& obj, std::string_view key, long long fallback = 0) {
        if (!obj.isObject() || !obj.contains(key)) return fallback;
        return static_cast<long long>(obj[key].asInt().unwrapOr(fallback));
    }

    double dbl(matjson::Value const& obj, std::string_view key) {
        if (!obj.isObject() || !obj.contains(key)) return 0.0;
        return obj[key].asDouble().unwrapOr(0.0);
    }

}

std::string CollabSession::serverUrl() {
    auto url = Mod::get()->getSettingValue<std::string>("server-url");
    while (!url.empty() && (url.back() == '/' || url.back() == ' ')) url.pop_back();
    while (!url.empty() && url.front() == ' ') url.erase(url.begin());
    if (url.empty()) url = "127.0.0.1:8484";
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
        url = "http://" + url;
    }
    return url;
}

void CollabSession::setServerUrl(std::string const& url) {
    Mod::get()->setSettingValue<std::string>("server-url", url);
}

std::pair<std::string, std::string> CollabSession::parseInvite(std::string const& text) {
    std::string code, server;
    auto at = text.find('@');
    std::string codePart = at == std::string::npos ? text : text.substr(0, at);
    if (at != std::string::npos) server = text.substr(at + 1);

    for (char c : codePart) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            code += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    while (!server.empty() && std::isspace(static_cast<unsigned char>(server.back()))) server.pop_back();
    while (!server.empty() && std::isspace(static_cast<unsigned char>(server.front()))) server.erase(server.begin());
    return {code, server};
}

std::string CollabSession::inviteCode() const {
    if (m_room.empty()) return "";
    auto url = serverUrl();
    std::string host = url;
    bool https = host.rfind("https://", 0) == 0;
    if (host.rfind("http://", 0) == 0) host = host.substr(7);
    else if (https) host = host.substr(8);

    // localhost means nothing to a friend - use the LAN address the server reported
    bool local = host.rfind("127.", 0) == 0 || host.rfind("localhost", 0) == 0 || host.rfind("0.0.0.0", 0) == 0;
    if (local && !m_lanAddress.empty()) host = m_lanAddress;

    return m_room + "@" + (https ? "https://" + host : host);
}

void CollabSession::ping(DoneCallback done) {
    (void)async::spawn(
        web::WebRequest().timeout(std::chrono::seconds(6)).get(serverUrl() + "/health"),
        [done](web::WebResponse res) {
            if (res.ok()) {
                auto json = res.json();
                if (json.isOk() && json.unwrap().contains("ok")) {
                    done("");
                    return;
                }
                done("That address answered, but it isn't an Editor Collab server");
                return;
            }
            done(res.code() == 0 ? "No answer - is the server running and the address right?"
                                 : fmt::format("Server error ({})", res.code()));
        }
    );
}

CollabSession* CollabSession::get() {
    static CollabSession instance;
    return &instance;
}

std::string CollabSession::displayName() {
    auto name = Mod::get()->getSettingValue<std::string>("display-name");
    if (!name.empty()) return name;
    if (auto am = GJAccountManager::sharedState()) {
        std::string acc = am->m_username;
        if (!acc.empty()) return acc;
    }
    return "Player";
}

// ---------------------------------------------------------------- lifecycle

void CollabSession::attach(LevelEditorLayer* layer) {
    if (m_layer && m_layer != layer) this->leave();
    m_layer = layer;
}

void CollabSession::detach(LevelEditorLayer* layer) {
    if (m_layer != layer) return;
    this->leave();
    m_layer = nullptr;
}

void CollabSession::reset() {
    m_generation++;
    m_active = false;
    m_busy = false;
    m_isHost = false;
    m_inFlight = false;
    m_failures = 0;
    m_timer = 0.f;
    m_room.clear();
    m_client.clear();
    m_lanAddress.clear();
    m_seq = 0;
    m_counter = 0;
    m_idToObj.clear();
    m_objToId.clear();
    m_lastState.clear();
    m_outbox.clear();
    m_inbox.clear();
    m_pendingResync.reset();
    m_peers.clear();
}

void CollabSession::host(DoneCallback done) {
    if (!m_layer || m_active || m_busy) return;
    m_busy = true;
    m_lastError.clear();

    this->request("/create", matjson::makeObject({
        {"name", displayName()},
    }), [this, done](matjson::Value const& json, std::string const& err) {
        m_busy = false;
        if (!err.empty()) {
            m_lastError = err;
            done(err);
            return;
        }
        m_room = str(json, "room");
        m_client = str(json, "client");
        m_seq = num(json, "seq");
        m_lanAddress = str(json, "lan");
        m_isHost = true;
        m_active = true;
        m_timer = 999.f; // sync immediately: the first scan uploads the whole level
        done("");
    });
}

void CollabSession::join(std::string code, DoneCallback done) {
    if (!m_layer || m_active || m_busy) return;
    m_busy = true;
    m_lastError.clear();

    this->request("/join", matjson::makeObject({
        {"room", code},
        {"name", displayName()},
    }), [this, done](matjson::Value const& json, std::string const& err) {
        m_busy = false;
        if (!err.empty()) {
            m_lastError = err;
            done(err);
            return;
        }
        if (!m_layer) return;

        m_room = str(json, "room");
        m_client = str(json, "client");
        m_seq = num(json, "seq");
        m_lanAddress = str(json, "lan");
        m_isHost = false;

        if (json.contains("objects")) {
            this->applyFullState(json["objects"], true);
        }

        m_active = true;
        m_timer = 999.f;
        done("");
    });
}

void CollabSession::leave() {
    if (m_active && !m_room.empty()) {
        // fire and forget
        (void)async::spawn(
            web::WebRequest()
                .timeout(std::chrono::seconds(5))
                .bodyJSON(matjson::makeObject({{"room", m_room}, {"client", m_client}}))
                .post(serverUrl() + "/leave"),
            [](web::WebResponse) {}
        );
    }
    this->reset();
}

// ---------------------------------------------------------------- networking

void CollabSession::request(std::string const& path, matjson::Value body, JsonCallback cb) {
    int gen = m_generation;
    auto url = serverUrl() + path;

    (void)async::spawn(
        web::WebRequest()
            .timeout(std::chrono::seconds(20))
            .bodyJSON(body)
            .post(url),
        [this, gen, cb = std::move(cb)](web::WebResponse res) {
            if (gen != m_generation) return; // session ended while waiting

            auto parsed = res.json();
            if (!res.ok()) {
                std::string msg;
                if (parsed.isOk()) msg = str(parsed.unwrap(), "error");
                if (msg.empty()) {
                    msg = res.code() == 0
                        ? "Couldn't reach the server. Check the Server URL in the mod settings."
                        : fmt::format("Server error ({})", res.code());
                }
                cb(matjson::Value(), msg);
                return;
            }
            if (parsed.isErr()) {
                cb(matjson::Value(), "Server sent an invalid response");
                return;
            }
            cb(parsed.unwrap(), "");
        }
    );
}

void CollabSession::sendSync() {
    if (!m_active || m_inFlight) return;

    std::vector<matjson::Value> ops;
    std::vector<CollabOp> sent(m_outbox.begin(), m_outbox.end());
    m_outbox.clear();
    ops.reserve(sent.size());
    for (auto& op : sent) {
        auto o = matjson::makeObject({{"t", op.type}, {"id", op.id}});
        if (op.type != "del") o.set("s", op.data);
        ops.push_back(std::move(o));
    }

    auto center = this->viewCenter();
    m_inFlight = true;

    this->request("/sync", matjson::makeObject({
        {"room", m_room},
        {"client", m_client},
        {"since", m_seq},
        {"ops", matjson::Value(std::move(ops))},
        {"x", center.x},
        {"y", center.y},
    }), [this, sent = std::move(sent)](matjson::Value const& json, std::string const& err) {
        m_inFlight = false;

        if (!err.empty()) {
            // put our changes back so they go out with the next attempt
            for (auto it = sent.rbegin(); it != sent.rend(); ++it) m_outbox.push_front(*it);
            m_failures++;

            if (err.find("Room not found") != std::string::npos) {
                Notification::create("Collab room closed", NotificationIcon::Error)->show();
                this->leave();
                return;
            }
            if (m_failures == 5) {
                Notification::create("Collab: lost connection, retrying...", NotificationIcon::Warning)->show();
            }
            m_lastError = err;
            return;
        }

        if (m_failures >= 5) {
            Notification::create("Collab: reconnected", NotificationIcon::Success)->show();
        }
        m_failures = 0;
        m_lastError.clear();
        m_seq = num(json, "seq", m_seq);

        if (json.contains("resync") && json["resync"].asBool().unwrapOr(false) && json.contains("objects")) {
            m_inbox.clear();
            m_pendingResync = json["objects"];
        }
        else if (json.contains("ops") && json["ops"].isArray()) {
            for (auto const& o : json["ops"]) {
                CollabOp op{str(o, "t"), str(o, "id"), str(o, "s")};
                if (op.id.empty()) continue;
                m_inbox.push_back(std::move(op));
            }
        }

        m_peers.clear();
        if (json.contains("peers") && json["peers"].isArray()) {
            for (auto const& p : json["peers"]) {
                m_peers.push_back({
                    str(p, "id"), str(p, "name"),
                    static_cast<float>(dbl(p, "x")), static_cast<float>(dbl(p, "y")),
                });
            }
        }
    });
}

// ---------------------------------------------------------------- main loop

void CollabSession::update(float dt) {
    if (!m_active || !m_layer) return;
    // Objects move around while playtesting; never sync that.
    if (m_layer->m_playbackMode != PlaybackMode::Not) return;

    this->applyRemote();

    m_timer += dt;
    auto interval = static_cast<float>(Mod::get()->getSettingValue<double>("sync-interval"));
    if (m_timer < interval || m_inFlight) return;
    m_timer = 0.f;

    this->scanLocalChanges();
    this->sendSync();
}

void CollabSession::scanLocalChanges() {
    if (!m_layer || !m_layer->m_objects) return;

    std::unordered_set<GameObject*> seen;
    seen.reserve(m_layer->m_objects->count());

    for (auto obj : CCArrayExt<GameObject*>(m_layer->m_objects)) {
        if (!obj) continue;
        seen.insert(obj);
        std::string save = obj->getSaveString(m_layer);

        auto it = m_objToId.find(obj);
        if (it == m_objToId.end()) {
            auto id = this->newId();
            this->track(id, obj);
            m_lastState[id] = save;
            m_outbox.push_back({"add", id, save});
        }
        else {
            auto& last = m_lastState[it->second];
            if (last != save) {
                last = save;
                m_outbox.push_back({"upd", it->second, save});
            }
        }
    }

    std::vector<std::string> gone;
    for (auto& [id, ref] : m_idToObj) {
        if (!seen.contains(ref.data())) gone.push_back(id);
    }
    for (auto& id : gone) {
        this->untrack(id);
        m_outbox.push_back({"del", id, ""});
    }
}

void CollabSession::applyRemote() {
    if (m_pendingResync) {
        auto objects = std::move(*m_pendingResync);
        m_pendingResync.reset();
        this->applyFullState(objects, false);
    }
    if (m_inbox.empty()) return;

    while (!m_inbox.empty()) {
        auto op = std::move(m_inbox.front());
        m_inbox.pop_front();
        this->applyOp(op);
    }
    if (m_layer && m_layer->m_editorUI) {
        m_layer->m_editorUI->updateObjectInfoLabel();
    }
}

void CollabSession::applyOp(CollabOp const& op) {
    if (!m_layer) return;

    if (op.type == "del") {
        this->removeLocal(op.id);
        return;
    }
    if (op.type != "add" && op.type != "upd") return;
    if (op.data.empty()) return;

    auto last = m_lastState.find(op.id);
    if (last != m_lastState.end() && last->second == op.data) return;

    this->removeLocal(op.id);

    auto created = m_layer->createObjectsFromString(op.data, true, true);
    if (!created || created->count() == 0) return;

    auto obj = static_cast<GameObject*>(created->objectAtIndex(0));
    this->track(op.id, obj);
    // store what *we* serialize it as, so we don't echo it straight back
    m_lastState[op.id] = obj->getSaveString(m_layer);
}

void CollabSession::applyFullState(matjson::Value const& objects, bool wipeUntracked) {
    if (!m_layer || !objects.isObject()) return;
    auto ui = m_layer->m_editorUI;

    if (wipeUntracked) {
        if (ui) ui->deselectAll();
        auto copy = CCArray::create();
        copy->addObjectsFromArray(m_layer->m_objects);
        for (auto obj : CCArrayExt<GameObject*>(copy)) {
            m_layer->removeObject(obj, true);
        }
        m_idToObj.clear();
        m_objToId.clear();
        m_lastState.clear();
    }
    else {
        std::vector<std::string> stale;
        for (auto& [id, _] : m_idToObj) {
            if (!objects.contains(id)) stale.push_back(id);
        }
        for (auto& id : stale) this->removeLocal(id);
    }

    for (auto const& entry : objects) {
        auto id = entry.getKey();
        if (!id) continue;
        this->applyOp({"upd", *id, entry.asString().unwrapOr("")});
    }

    if (ui) ui->updateObjectInfoLabel();
}

// ---------------------------------------------------------------- tracking

void CollabSession::track(std::string const& id, GameObject* obj) {
    m_idToObj[id] = Ref(obj);
    m_objToId[obj] = id;
}

void CollabSession::untrack(std::string const& id) {
    auto it = m_idToObj.find(id);
    if (it == m_idToObj.end()) return;
    m_objToId.erase(it->second.data());
    m_idToObj.erase(it);
    m_lastState.erase(id);
}

bool CollabSession::objectAlive(GameObject* obj) {
    return m_layer && m_layer->m_objects && m_layer->m_objects->containsObject(obj);
}

void CollabSession::removeLocal(std::string const& id) {
    auto it = m_idToObj.find(id);
    if (it == m_idToObj.end()) return;

    Ref<GameObject> obj = it->second;
    this->untrack(id);

    if (!this->objectAlive(obj)) return;

    if (auto ui = m_layer->m_editorUI) {
        bool selected = ui->m_selectedObject == obj.data()
            || (ui->m_selectedObjects && ui->m_selectedObjects->containsObject(obj));
        if (selected) ui->deselectObject(obj);
    }
    m_layer->removeObject(obj, true);
}

cocos2d::CCPoint CollabSession::viewCenter() {
    if (!m_layer || !m_layer->m_objectLayer) return {0, 0};
    auto win = CCDirector::get()->getWinSize();
    return m_layer->m_objectLayer->convertToNodeSpace(ccp(win.width / 2, win.height / 2));
}

std::string CollabSession::newId() {
    return fmt::format("{}:{}", m_client, ++m_counter);
}
