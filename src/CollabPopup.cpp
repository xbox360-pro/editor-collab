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

    void doJoin(std::string code, std::string server) {
        auto spinner = loading("Joining room...");
        CollabSession::get()->join(code, server, [spinner, code](std::string const& err) {
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

    // --- Join ---
    m_mainLayer->addChildAtPosition(smallLabel("Join a friend's room:", .75f), Anchor::Center, ccp(0, 72));

    m_inviteInput = TextInput::create(170.f, "Room code");
    m_inviteInput->setCommonFilter(CommonFilter::Any);
    m_inviteInput->setMaxCharCount(120);
    m_mainLayer->addChildAtPosition(m_inviteInput, Anchor::Center, ccp(-45, 44));

    auto joinMenu = menuAt();
    auto pasteBtn = makeIcon("GJ_pasteBtn_001.png", .55f, this, menu_selector(CollabPopup::onPaste));
    pasteBtn->setPosition(ccp(65, 0));
    auto joinBtn = makeButton("Join", "GJ_button_02.png", this, menu_selector(CollabPopup::onJoin), .7f);
    joinBtn->setPosition(ccp(112, 0));
    joinMenu->addChild(pasteBtn);
    joinMenu->addChild(joinBtn);
    m_mainLayer->addChildAtPosition(joinMenu, Anchor::Center, ccp(-45, 44));

    // fill it in automatically if there's a code on the clipboard
    auto clip = clipboard::read();
    if (looksLikeInvite(clip)) m_inviteInput->setString(clip);

    // --- Host ---
    m_mainLayer->addChildAtPosition(smallLabel("- or -", .7f, 140), Anchor::Center, ccp(0, 12));

    auto hostMenu = menuAt();
    auto hostBtn = makeButton("Host a Room", "GJ_button_01.png", this, menu_selector(CollabPopup::onHost));
    hostMenu->addChild(hostBtn);
    m_mainLayer->addChildAtPosition(hostMenu, Anchor::Center, ccp(0, -18));

    // --- Server status line ---
    auto serverText = CollabSession::usingCustomServer()
        ? fmt::format("Server: {} (custom)", CollabSession::serverUrl())
        : std::string("Server: Global");
    auto serverLabel = smallLabel(serverText, .55f, 150);
    serverLabel->limitLabelWidth(220.f, .55f, .3f);
    m_mainLayer->addChildAtPosition(serverLabel, Anchor::Bottom, ccp(-30, 26));

    auto testMenu = menuAt();
    testMenu->addChild(makeButton("Test", "GJ_button_04.png", this, menu_selector(CollabPopup::onTest), .5f));
    m_mainLayer->addChildAtPosition(testMenu, Anchor::Bottom, ccp(110, 26));

    if (session->isBusy()) {
        hostBtn->setEnabled(false);
        joinBtn->setEnabled(false);
    }
}

// --------------------------------------------------------------- in a room

void CollabPopup::buildConnected() {
    auto session = CollabSession::get();

    m_mainLayer->addChildAtPosition(
        smallLabel("Send this code to your friends:", .7f, 190), Anchor::Top, ccp(0, -48)
    );

    auto invite = CCLabelBMFont::create(session->inviteCode().c_str(), "bigFont.fnt");
    invite->setColor({120, 255, 140});
    invite->limitLabelWidth(290.f, 1.f, .25f);
    m_mainLayer->addChildAtPosition(invite, Anchor::Top, ccp(0, -72));

    auto copyMenu = menuAt();
    copyMenu->addChild(makeButton("Copy Code", "GJ_button_01.png", this, menu_selector(CollabPopup::onCopy), .7f));
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

void CollabPopup::onTest(CCObject*) {
    auto spinner = loading("Checking server (can take a minute if it was asleep)...");
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
    auto spinner = loading("Creating room...");
    this->onClose(nullptr);

    CollabSession::get()->host([spinner](std::string const& err) {
        spinner->hide();
        if (!err.empty()) {
            FLAlertLayer::create(
                "Collab",
                ("Couldn't create a room:\n<cr>" + err + "</c>").c_str(),
                "OK"
            )->show();
            return;
        }
        clipboard::write(CollabSession::get()->inviteCode());
        Notification::create(
            fmt::format("Room {} created - code copied! Send it to your friends", CollabSession::get()->inviteCode()),
            NotificationIcon::Success, 3.f
        )->show();
    });
}

void CollabPopup::onJoin(CCObject*) {
    auto text = m_inviteInput ? std::string(m_inviteInput->getString()) : "";
    auto [code, server] = CollabSession::parseInvite(text);
    if (code.empty()) {
        Notification::create("Type the room code your friend sent you first", NotificationIcon::Warning)->show();
        return;
    }

    this->onClose(nullptr);

    auto layer = LevelEditorLayer::get();
    bool empty = !layer || !layer->m_objects || layer->m_objects->count() == 0;
    if (empty) {
        doJoin(code, server);
        return;
    }

    createQuickPopup(
        "Join Room",
        "This level isn't empty. Joining <cy>replaces it</c> with the room's level.\n"
        "Tip: join from a <cg>new empty level</c>. Continue?",
        "Cancel", "Join",
        [code, server](FLAlertLayer*, bool yes) {
            if (yes) doJoin(code, server);
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
    Notification::create("Code copied", NotificationIcon::Success)->show();
}

void CollabPopup::onPaste(CCObject*) {
    if (m_inviteInput) m_inviteInput->setString(clipboard::read());
}
