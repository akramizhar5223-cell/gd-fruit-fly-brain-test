#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>

using namespace geode::prelude;

// Simple AI — no fancy stuff, just works ✅
struct FlyBrainAI {
    bool shouldJump(float dist, float heightDiff) {
        if (dist > 12.0f) return false;
        return (dist < 8.0f && heightDiff > 0.4f);
    }

    bool shouldHold(float dist, float heightDiff) {
        if (dist > 12.0f && std::abs(heightDiff) < 0.5f) return true;
        return (dist < 8.0f && heightDiff < -0.4f);
    }
};

static FlyBrainAI s_ai;
static bool s_aiActive = true;

// 5-second Resume Hold
class $modify(ModPauseLayer, PauseLayer) {
    bool m_holding = false;
    float m_timer = 0.0f;
    CCLabelBMFont* m_label = nullptr;

    void customSetup() {
        PauseLayer::customSetup();
        auto resume = this->getChildByID("resume-button");
        if (!resume) return;

        resume->removeFromParent();
        auto btn = CCMenuItemSpriteExtra::create(
            CCSprite::createWithSpriteFrameName("GJ_playBtn_001.png"),
            this,
            menu_selector(ModPauseLayer::onStart)
        );
        btn->setID("resume-button");
        btn->setPosition({0, -60});

        m_label = CCLabelBMFont::create("Hold 5s to unpause", "bigFont.fnt");
        m_label->setPosition({0, 40});
        m_label->setScale(0.5f);
        m_label->setVisible(false);
        this->addChild(m_label, 10);

        if (auto menu = this->getChildByType<CCMenu>(0)) {
            menu->addChild(btn);
        }
    }

    void onStart(CCObject*) {
        m_holding = true;
        m_timer = 0.0f;
        m_label->setVisible(true);
        this->schedule(schedule_selector(ModPauseLayer::onUpdate), 0.05f);
    }

    void onUpdate(float dt) {
        if (!m_holding) {
            this->unschedule(schedule_selector(ModPauseLayer::onUpdate));
            return;
        }
        m_timer += dt;

        if (m_timer >= 5.0f) {
            m_holding = false;
            this->unschedule(schedule_selector(ModPauseLayer::onUpdate));
            PauseLayer::onResume(nullptr);
        } else if (m_label) {
            m_label->setString(CCString::createWithFormat("Wait %.1fs...", 5.0f - m_timer)->getCString());
        }
    }

    void cancel() {
        if (m_holding) {
            m_holding = false;
            this->unschedule(schedule_selector(ModPauseLayer::onUpdate));
            if (m_label) m_label->setString("Cancelled");
        }
    }

    void mouseUp(CCEvent* e) { cancel(); PauseLayer::mouseUp(e); }
    void ccTouchEnded(CCTouch* t, CCEvent* e) { cancel(); PauseLayer::ccTouchEnded(t, e); }
};

// AI Player
class $modify(ModPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level) {
        if (!PlayLayer::init(level)) return false;
        s_aiActive = Mod::get()->getSettingValue<bool>("enable-ai");
        return true;
    }

    void update(float dt) {
        PlayLayer::update(dt);
        if (!s_aiActive || m_isPaused || m_gameState != 0) return;

        float px = m_player1->getPositionX();
        float py = m_player1->getPositionY();
        float nearDist = 999.0f, nearDy = 0.0f;

        for (auto* obj : m_gameObjects) {
            if (!obj || !obj->isVisible()) continue;
            float dx = obj->getPositionX() - px;
            if (dx > 0.5f && dx < nearDist) {
                nearDist = dx;
                nearDy = obj->getPositionY() - py;
            }
        }

        if (s_ai.shouldJump(nearDist, nearDy) && !m_player1->m_isJumping) {
            this->handleButton(true, false);
        }
        if (s_ai.shouldHold(nearDist, nearDy)) {
            this->handleButton(true, true);
        }
    }
};
