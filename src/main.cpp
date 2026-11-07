#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>

#include "CollabPopup.hpp"
#include "Session.hpp"

using namespace geode::prelude;

namespace {
    ccColor3B colorFor(std::string const& id) {
        static const ccColor3B palette[] = {
            {255, 120, 120}, {120, 200, 255}, {140, 255, 140}, {255, 210, 100},
            {220, 140, 255}, {255, 160, 210}, {120, 255, 230}, {255, 170, 100},
        };
        size_t h = std::hash<std::string>{}(id);
        return palette[h % (sizeof(palette) / sizeof(palette[0]))];
    }
}

// Lives inside EditorUI: drives the session, shows the status line and
// draws a marker where every other collaborator is looking.
class CollabNode : public CCNode {
protected:
    LevelEditorLayer* m_layer = nullptr;
    CCMenu* m_menu = nullptr;
    CCLabelBMFont* m_status = nullptr;
    Ref<CCNode> m_peerLayer;
    std::unordered_map<std::string, Ref<CCNode>> m_markers;
    float m_labelTimer = 0.f;

public:
    static CollabNode* create(LevelEditorLayer* layer) {
        auto ret = new CollabNode();
        if (ret->initWithLayer(layer)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    ~CollabNode() override {
        CollabSession::get()->detach(m_layer);
        if (m_peerLayer) m_peerLayer->removeFromParent();
    }

    bool initWithLayer(LevelEditorLayer* layer) {
        if (!CCNode::init()) return false;
        m_layer = layer;
        this->setID("collab-node"_spr);

        auto win = CCDirector::get()->getWinSize();

        auto spr = ButtonSprite::create("Collab", "bigFont.fnt", "GJ_button_04.png", .8f);
        spr->setScale(.5f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(CollabNode::onOpen));
        btn->setID("collab-button"_spr);

        m_menu = CCMenu::create();
        m_menu->setContentSize({0, 0});
        m_menu->setPosition(ccp(win.width / 2, win.height - 16));
        m_menu->addChild(btn);
        this->addChild(m_menu);

        m_status = CCLabelBMFont::create("", "chatFont.fnt");
        m_status->setScale(.55f);
        m_status->setPosition(ccp(win.width / 2, win.height - 34));
        m_status->setVisible(false);
        this->addChild(m_status);

        if (m_layer && m_layer->m_objectLayer) {
            m_peerLayer = CCNode::create();
            m_peerLayer->setID("collab-peers"_spr);
            m_layer->m_objectLayer->addChild(m_peerLayer, 9999);
        }

        CollabSession::get()->attach(layer);
        this->scheduleUpdate();
        return true;
    }

    void onOpen(CCObject*) {
        if (auto popup = CollabPopup::create()) popup->show();
    }

    void update(float dt) override {
        auto session = CollabSession::get();
        session->update(dt);

        bool playtesting = m_layer && m_layer->m_playbackMode != PlaybackMode::Not;
        m_menu->setVisible(!playtesting);

        m_labelTimer += dt;
        if (m_labelTimer > .25f) {
            m_labelTimer = 0.f;
            this->refreshStatus(playtesting);
        }
        this->refreshMarkers();
    }

    void refreshStatus(bool playtesting) {
        auto session = CollabSession::get();
        if (!session->isActive() || playtesting) {
            m_status->setVisible(false);
            return;
        }
        m_status->setVisible(true);

        auto n = session->peers().size();
        std::string text = fmt::format(
            "Room {}  |  {} other{} here", session->roomCode(), n, n == 1 ? "" : "s"
        );
        if (!session->lastError().empty()) {
            text += "  |  reconnecting...";
            m_status->setColor({255, 140, 140});
        }
        else {
            m_status->setColor({160, 255, 170});
        }
        m_status->setString(text.c_str());
    }

    void refreshMarkers() {
        if (!m_peerLayer || !m_layer || !m_layer->m_objectLayer) return;

        auto session = CollabSession::get();
        bool show = session->isActive()
            && Mod::get()->getSettingValue<bool>("show-peers")
            && m_layer->m_playbackMode == PlaybackMode::Not;
        m_peerLayer->setVisible(show);
        if (!show) {
            if (!m_markers.empty()) {
                m_peerLayer->removeAllChildren();
                m_markers.clear();
            }
            return;
        }

        // keep markers the same size on screen no matter the zoom
        float zoom = m_layer->m_objectLayer->getScale();
        float inv = zoom > 0.01f ? 1.f / zoom : 1.f;

        std::unordered_set<std::string> alive;
        for (auto const& peer : session->peers()) {
            alive.insert(peer.id);
            auto it = m_markers.find(peer.id);
            CCNode* marker;
            if (it == m_markers.end()) {
                marker = this->makeMarker(peer);
                m_peerLayer->addChild(marker);
                m_markers[peer.id] = marker;
            }
            else {
                marker = it->second;
            }
            // glide toward the latest position instead of teleporting
            auto target = ccp(peer.x, peer.y);
            auto cur = marker->getPosition();
            marker->setPosition(cur + (target - cur) * .2f);
            marker->setScale(inv);
        }

        for (auto it = m_markers.begin(); it != m_markers.end();) {
            if (!alive.contains(it->first)) {
                it->second->removeFromParent();
                it = m_markers.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    CCNode* makeMarker(CollabPeer const& peer) {
        auto col = colorFor(peer.id);
        auto marker = CCNode::create();
        marker->setPosition(ccp(peer.x, peer.y));

        auto dot = CCDrawNode::create();
        dot->drawDot({0, 0}, 7.f, {0, 0, 0, .6f});
        dot->drawDot({0, 0}, 5.f, {col.r / 255.f, col.g / 255.f, col.b / 255.f, 1.f});
        marker->addChild(dot);

        auto name = CCLabelBMFont::create(peer.name.c_str(), "bigFont.fnt");
        name->setScale(.35f);
        name->setColor(col);
        name->setPosition(ccp(0, 14));
        marker->addChild(name);

        return marker;
    }
};

class $modify(CollabEditorUI, EditorUI) {
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;

        if (auto node = CollabNode::create(editorLayer)) {
            this->addChild(node, 1000);
        }
        return true;
    }
};

// Backup entry point: a Collab button in the editor pause menu too
class $modify(CollabPauseLayer, EditorPauseLayer) {
    void customSetup() {
        EditorPauseLayer::customSetup();

        auto win = CCDirector::get()->getWinSize();
        auto spr = ButtonSprite::create("Collab", "bigFont.fnt", "GJ_button_04.png", .8f);
        spr->setScale(.6f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(CollabPauseLayer::onCollab));
        btn->setID("collab-button"_spr);

        auto menu = CCMenu::create();
        menu->setID("collab-menu"_spr);
        menu->setContentSize({0, 0});
        menu->setPosition(ccp(win.width - 45.f, 25.f));
        menu->addChild(btn);
        this->addChild(menu, 100);
    }

    void onCollab(CCObject*) {
        if (auto popup = CollabPopup::create()) popup->show();
    }
};
