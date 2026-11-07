#include "CollabPopup.hpp"
#include "Session.hpp"

namespace {
    CCMenuItemSpriteExtra* makeButton(char const* text, char const* bg, CCObject* target, SEL_MenuHandler sel, float scale = .8f) {
        auto spr = ButtonSprite::create(text, "goldFont.fnt", bg, scale);
        return CCMenuItemSpriteExtra::create(spr, target, sel);
    }

    CCMenuItemSpriteExtra* makeIcon(char const* frame, float scale, CCObject* target, SEL_MenuHandler sel) {
        auto spr = CCSprite::createWithSpriteFrameName(frame);
        spr->setScale(scale);
        return CCMenuItemSpriteExtra::create(spr, target, sel);
    }

    CCLabelBMFont* smallLabel(std::string const& text, float scale, GLubyte opacity = 255) {
        auto l = CCLabelBMFont::create(text.c_str(), "chatFont.fnt");
        l->setScale(scale);
        l->setOpacity(opacity);
        return l;
    }

    CCMenu* menuAt() {
        auto m = CCMenu::create();
        m->setContentSize({0, 0});
        return m;
    }

    // Shows a spinner notification and returns it so the caller can hide it later
    Ref<Notification> loading(char const* text) {
        auto n = Notification::create(text, NotificationIcon::Loading, 0.f);
        n->show();
        return n;
    }

    bool looksLikeInvite(std::string const& text) {
        auto [code, server] = CollabSession::parseInvite(text);
        return code.size() == 6 && (text.size() <= 8 || !server.empty());
    }

    void doJoin(std::string code) {
        auto spinner = loading("Joining room...");
        CollabSession::get()->join(code, [spinner, code](std::string const& err) {
            spinner->hide();
            if (!err.empty()) {
                FLAlertLayer::create("Collab", ("Couldn't join:\n<cr>" + err + "</c>").c_str(), "OK")->show();
                return;
            }
            Notification::create(fmt::format("Joined room {} - start building!", code), NotificationIcon::Success)->show();
        });
    }
}

CollabPopup* CollabPopup::create() {
    auto ret = new CollabPopup();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool CollabPopup::init() {
    if (!Popup::init(330.f, 250.f)) return false;
    this->setTitle("Editor Collab");

    if (CollabSession::get()->isActive()) {
        this->buildConnected();
    }
    else {
        this->buildDisconnected();
    }
    return true;
}

// --------------------------------------------------------------- not in a room

void CollabPopup::buildDisconnected() {
    auto session = CollabSession::get();

    m_mainLayer->addChildAtPosition(
        smallLabel(fmt::format("Playing as {}", CollabSession::displayName()), .6f, 170),
        Anchor::Top, ccp(0, -40)
    );

    // --- Join (top, since most people will be joining) ---
    m_mainLayer->addChildAtPosition(smallLabel("Got an invite? Paste it here:", .7f), Anchor::Center, ccp(0, 72));

    m_inviteInput = TextInput::create(200.f, "ABC123@address");
    m_inviteInput->setCommonFilter(CommonFilter::Any);
    m_inviteInput->setMaxCharCount(120);
    m_mainLayer->addChildAtPosition(m_inviteInput, Anchor::Center, ccp(-38, 46));

    auto joinMenu = menuAt();
    auto pasteBtn = makeIcon("GJ_pasteBtn_001.png", .55f, this, menu_selector(CollabPopup::onPaste));
    pasteBtn->setPosition(ccp(80, 0));
    auto joinBtn = makeButton("Join", "GJ_button_02.png", this, menu_selector(CollabPopup::onJoin), .7f);
    joinBtn->setPosition(ccp(124, 0));
    joinMenu->addChild(pasteBtn);
    joinMenu->addChild(joinBtn);
    m_mainLayer->addChildAtPosition(joinMenu, Anchor::Center, ccp(-38, 46));

    // fill it in automatically if there's an invite on the clipboard
    auto clip = clipboard::read();
    if (looksLikeInvite(clip)) m_inviteInput->setString(clip);

    // --- Host ---
    m_mainLayer->addChildAtPosition(smallLabel("- or -", .7f, 140), Anchor::Center, ccp(0, 16));

    auto hostMenu = menuAt();
    auto hostBtn = makeButton("Host a Room", "GJ_button_01.png", this, menu_selector(CollabPopup::onHost));
    hostMenu->addChild(hostBtn);
    m_mainLayer->addChildAtPosition(hostMenu, Anchor::Center, ccp(0, -12));

    // --- Server (only matters for the host) ---
    m_mainLayer->addChildAtPosition(
        smallLabel("Server (only the host needs to set this)", .55f, 150), Anchor::Bottom, ccp(0, 58)
    );

    m_serverInput = TextInput::create(190.f, "127.0.0.1:8484");
    m_serverInput->setCommonFilter(CommonFilter::Any);
    m_serverInput->setMaxCharCount(120);
    m_serverInput->setString(Mod::get()->getSettingValue<std::string>("server-url"));
    m_serverInput->setCallback([](std::string const& text) {
        CollabSession::setServerUrl(text);
    });
    m_serverInput->setScale(.85f);
    m_mainLayer->addChildAtPosition(m_serverInput, Anchor::Bottom, ccp(-28, 34));

    auto testMenu = menuAt();
    testMenu->addChild(makeButton("Test", "GJ_button_04.png", this, menu_selector(CollabPopup::onTest), .6f));
    m_mainLayer->addChildAtPosition(testMenu, Anchor::Bottom, ccp(110, 34));

    if (session->isBusy()) {
        hostBtn->setEnabled(false);
        joinBtn->setEnabled(false);
    }
}

// --------------------------------------------------------------- in a room

void CollabPopup::buildConnected() {
    auto session = CollabSession::get();

    m_mainLayer->addChildAtPosition(
        smallLabel("Send this invite to your friends:", .7f, 190), Anchor::Top, ccp(0, -48)
    );

    auto invite = CCLabelBMFont::create(session->inviteCode().c_str(), "bigFont.fnt");
    invite->setColor({120, 255, 140});
    invite->limitLabelWidth(290.f, .6f, .25f);
    m_mainLayer->addChildAtPosition(invite, Anchor::Top, ccp(0, -72));

    auto copyMenu = menuAt();
    copyMenu->addChild(makeButton("Copy Invite", "GJ_button_01.png", this, menu_selector(CollabPopup::onCopy), .7f));
    m_mainLayer->addChildAtPosition(copyMenu, Anchor::Top, ccp(0, -102));

    std::string who;
    auto const& peers = session->peers();
    if (peers.empty()) {
        who = "Nobody else here yet";
    }
    else {
        who = "Building with: ";
        for (size_t i = 0; i < peers.size(); i++) {
            if (i) who += ", ";
            who += peers[i].name;
        }
    }
    auto whoLabel = CCLabelBMFont::create(who.c_str(), "chatFont.fnt");
    whoLabel->limitLabelWidth(290.f, .75f, .3f);
    m_mainLayer->addChildAtPosition(whoLabel, Anchor::Center, ccp(0, -22));

    if (!session->lastError().empty()) {
        auto err = CCLabelBMFont::create(session->lastError().c_str(), "chatFont.fnt");
        err->setColor({255, 110, 110});
        err->limitLabelWidth(290.f, .6f, .3f);
        m_mainLayer->addChildAtPosition(err, Anchor::Center, ccp(0, -42));
    }

    auto menu = menuAt();
    menu->addChild(makeButton("Leave Room", "GJ_button_06.png", this, menu_selector(CollabPopup::onLeave), .7f));
    m_mainLayer->addChildAtPosition(menu, Anchor::Bottom, ccp(0, 28));
}

// --------------------------------------------------------------- actions

void CollabPopup::saveServer() {
    if (m_serverInput) CollabSession::setServerUrl(m_serverInput->getString());
}

void CollabPopup::onTest(CCObject*) {
    this->saveServer();
    auto spinner = loading("Checking server...");
    CollabSession::get()->ping([spinner](std::string const& err) {
        spinner->hide();
        if (err.empty()) {
            Notification::create("Server is online!", NotificationIcon::Success)->show();
        }
        else {
            Notification::create(err, NotificationIcon::Error, 4.f)->show();
        }
    });
}

void CollabPopup::onHost(CCObject*) {
    this->saveServer();
    auto spinner = loading("Creating room...");
    this->onClose(nullptr);

    CollabSession::get()->host([spinner](std::string const& err) {
        spinner->hide();
        if (!err.empty()) {
            FLAlertLayer::create(
                "Collab",
                ("Couldn't create a room:\n<cr>" + err + "</c>\n\n"
                 "Make sure the server is running (double-click <cy>Start Server</c>).").c_str(),
                "OK"
            )->show();
            return;
        }
        clipboard::write(CollabSession::get()->inviteCode());
        Notification::create("Room created - invite copied! Send it to your friends", NotificationIcon::Success, 3.f)->show();
    });
}

void CollabPopup::onJoin(CCObject*) {
    this->saveServer();
    auto text = m_inviteInput ? std::string(m_inviteInput->getString()) : "";
    auto [code, server] = CollabSession::parseInvite(text);
    if (code.empty()) {
        Notification::create("Paste the invite your friend sent you first", NotificationIcon::Warning)->show();
        return;
    }
    // invites carry the server address, so the joiner never touches settings
    if (!server.empty()) CollabSession::setServerUrl(server);

    this->onClose(nullptr);

    auto layer = LevelEditorLayer::get();
    bool empty = !layer || !layer->m_objects || layer->m_objects->count() == 0;
    if (empty) {
        doJoin(code);
        return;
    }

    createQuickPopup(
        "Join Room",
        "This level isn't empty. Joining <cy>replaces it</c> with the room's level.\n"
        "Tip: join from a <cg>new empty level</c>. Continue?",
        "Cancel", "Join",
        [code](FLAlertLayer*, bool yes) {
            if (yes) doJoin(code);
        }
    );
}

void CollabPopup::onLeave(CCObject*) {
    CollabSession::get()->leave();
    Notification::create("Left the room", NotificationIcon::Info)->show();
    this->onClose(nullptr);
}

void CollabPopup::onCopy(CCObject*) {
    clipboard::write(CollabSession::get()->inviteCode());
    Notification::create("Invite copied", NotificationIcon::Success)->show();
}

void CollabPopup::onPaste(CCObject*) {
    if (m_inviteInput) m_inviteInput->setString(clipboard::read());
}
