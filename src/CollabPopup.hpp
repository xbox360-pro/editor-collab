#pragma once

#include <Geode/Geode.hpp>

using namespace geode::prelude;

class CollabPopup : public geode::Popup {
public:
    static CollabPopup* create();

protected:
    bool init();

    void buildDisconnected();
    void buildConnected();

    void onHost(CCObject*);
    void onJoin(CCObject*);
    void onLeave(CCObject*);
    void onCopy(CCObject*);
    void onPaste(CCObject*);
    void onTest(CCObject*);

    TextInput* m_inviteInput = nullptr;
};
