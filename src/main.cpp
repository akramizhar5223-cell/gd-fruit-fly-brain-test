#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <chrono>
#include <deque>
#include <vector>
#include <cmath>
#include <fstream>
#include <sstream>

using namespace geode::prelude;
using namespace std::chrono;

struct FlyBrainAI {
    struct Neuron {
        float potential = 0.0f;
        float threshold = 1.0f;
        float decay = 0.92f;
        bool fired = false;
        float learnedWeight = 1.0f;
    };

    Neuron obstacleSensor;
    Neuron jumpNeuron;
    Neuron holdNeuron;
    Neuron straightFlyNeuron;

    struct MemoryEntry {
        float distance;
        float height;
        bool actionTaken;
        bool success;
        bool wasStraight;
    };
    
    std::deque<MemoryEntry> shortTerm;
    static constexpr size_t SHORT_TERM_MAX = 200;

    float lastDist = 999.0f;
    float lastHeight = 0.0f;
    bool isStraight = true;
    bool isHolding = false;
    int failCount = 0;
    int attemptCount = 0;
    float confidence = 0.5f;
    std::string levelID;

    void setLevel(std::string const& id) {
        levelID = id;
        loadMemory();
    }

    void update(float dist, float heightDiff, float dt) {
        isStraight = (dist > 15.0f && std::abs(heightDiff) < 0.4f);
        
        // Decay
        obstacleSensor.potential *= obstacleSensor.decay;
        jumpNeuron.potential *= jumpNeuron.decay;
        holdNeuron.potential *= holdNeuron.decay;
        straightFlyNeuron.potential *= 0.98f;

        // Inputs
        float distInput = std::max(0.0f, 3.0f / (dist + 0.5f));
        obstacleSensor.potential += distInput;

        if (isStraight) {
            straightFlyNeuron.potential += 0.3f;
        } else {
            straightFlyNeuron.potential *= 0.9f;
        }

        // Jump/Hold logic
        if (!isStraight && dist < 10.0f && dist > 0.3f) {
            if (heightDiff > 0.35f) {
                jumpNeuron.potential += distInput * 0.7f * jumpNeuron.learnedWeight;
            }
            if (heightDiff < -0.35f) {
                holdNeuron.potential += distInput * 0.6f * holdNeuron.learnedWeight;
            }
        }

        // Fire
        straightFlyNeuron.fired = straightFlyNeuron.potential >= 0.85f;
        jumpNeuron.fired = jumpNeuron.potential >= jumpNeuron.threshold;
        holdNeuron.fired = holdNeuron.potential >= holdNeuron.threshold;

        lastDist = dist;
        lastHeight = heightDiff;
    }

    bool shouldJump() {
        if (straightFlyNeuron.fired) return false;
        if (jumpNeuron.fired) {
            jumpNeuron.potential = 0.0f;
            return true;
        }
        return false;
    }

    bool shouldHold() {
        if (straightFlyNeuron.fired) return true;
        if (holdNeuron.fired) {
            holdNeuron.potential = 0.0f;
            return true;
        }
        return false;
    }

    void recordAttempt(bool success) {
        attemptCount++;
        if (!success) {
            failCount++;
            jumpNeuron.learnedWeight = std::max(0.5f, jumpNeuron.learnedWeight - 0.03f);
            holdNeuron.learnedWeight = std::max(0.5f, holdNeuron.learnedWeight - 0.02f);
        } else {
            jumpNeuron.learnedWeight = std::min(1.5f, jumpNeuron.learnedWeight + 0.015f);
            holdNeuron.learnedWeight = std::min(1.5f, holdNeuron.learnedWeight + 0.01f);
            straightFlyNeuron.learnedWeight = std::min(1.5f, straightFlyNeuron.learnedWeight + 0.02f);
        }
        if (attemptCount > 5) {
            confidence = 1.0f - float(failCount) / float(attemptCount);
            confidence = std::clamp(confidence, 0.15f, 0.95f);
        }
        saveMemory();
    }

    void reset() {
        obstacleSensor.potential = 0.0f;
        jumpNeuron.potential = 0.0f;
        holdNeuron.potential = 0.0f;
        straightFlyNeuron.potential = 0.0f;
        isHolding = false;
        shortTerm.clear();
    }

    std::string getSavePath() const {
        return Mod::get()->getSaveDir() / ("brain_" + levelID + ".json");
    }

    void saveMemory() {
        if (levelID.empty()) return;
        std::ofstream f(getSavePath());
        if (!f.is_open()) return;
        f << "{\n";
        f << "  \"attempts\": " << attemptCount << ",\n";
        f << "  \"fails\": " << failCount << ",\n";
        f << "  \"confidence\": " << confidence << ",\n";
        f << "  \"jump_weight\": " << jumpNeuron.learnedWeight << ",\n";
        f << "  \"hold_weight\": " << holdNeuron.learnedWeight << ",\n";
        f << "  \"straight_weight\": " << straightFlyNeuron.learnedWeight << "\n";
        f << "}\n";
    }

    void loadMemory() {
        if (levelID.empty()) return;
        std::ifstream f(getSavePath());
        if (!f.is_open()) return;
        std::string line, content;
        while (std::getline(f, line)) content += line;
        
        auto getFloat = [&](std::string const& key) -> float {
            size_t pos = content.find("\"" + key + "\":");
            if (pos == std::string::npos) return -1.0f;
            pos += key.size() + 3;
            float v = 0.0f;
            std::sscanf(content.substr(pos).c_str(), "%f", &v);
            return v;
        };

        if (float v = getFloat("attempts"); v > 0) attemptCount = int(v);
        if (float v = getFloat("fails"); v >= 0) failCount = int(v);
        if (float v = getFloat("confidence"); v > 0) confidence = v;
        if (float v = getFloat("jump_weight"); v > 0) jumpNeuron.learnedWeight = v;
        if (float v = getFloat("hold_weight"); v > 0) holdNeuron.learnedWeight = v;
        if (float v = getFloat("straight_weight"); v > 0) straightFlyNeuron.learnedWeight = v;
    }
};

static FlyBrainAI s_ai;
static bool s_aiActive = true;

// ==========================================
// PAUSE MENU — 5-SECOND RESUME HOLD
// ==========================================
class $modify(ModifiedPauseLayer, PauseLayer) {
    bool m_holding = false;
    steady_clock::time_point m_holdStart;
    bool m_done = false;
    CCLabelBMFont* m_label = nullptr;

    void customSetup() {
        PauseLayer::customSetup();
        auto resumeBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(this->getChildByID("resume-button"));
        if (!resumeBtn) return;

        resumeBtn->removeFromParent();
        auto newBtn = CCMenuItemSpriteExtra::create(
            resumeBtn->getNormalImage(),
            this,
            menu_selector(ModifiedPauseLayer::onHoldStart)
        );
        newBtn->setID("resume-button");
        newBtn->setPosition(resumeBtn->getPosition());

        m_label = CCLabelBMFont::create("Hold Resume 5s to unpause!", "bigFont.fnt");
        m_label->setPosition({0, 40});
        m_label->setScale(0.5f);
        m_label->setVisible(false);
        this->addChild(m_label, 10);

        if (auto menu = this->getChildByType<CCMenu>(0)) {
            menu->addChild(newBtn);
        }
    }

    void onHoldStart(CCObject*) {
        m_holding = true;
        m_done = false;
        m_holdStart = steady_clock::now();
        if (m_label) m_label->setVisible(true);
        this->schedule(schedule_selector(ModifiedPauseLayer::onHoldUpdate), 0.05f);
    }

    void onHoldUpdate(float) {
        if (!m_holding) {
            this->unschedule(schedule_selector(ModifiedPauseLayer::onHoldUpdate));
            return;
        }
        float elapsed = duration_cast<milliseconds>(steady_clock::now() - m_holdStart).count() / 1000.0f;
        
        if (m_label) {
            float rem = std::max(0.0f, 5.0f - elapsed);
            m_label->setString(CCString::createWithFormat("Unpausing in %.1fs...", rem)->getCString());
        }
        if (elapsed >= 5.0f && !m_done) {
            m_done = true;
            m_holding = false;
            this->unschedule(schedule_selector(ModifiedPauseLayer::onHoldUpdate));
            PauseLayer::onResume(nullptr);
        }
    }

    void cancelHold() {
        if (m_holding && !m_done) {
            m_holding = false;
            if (m_label) m_label->setString("Cancelled — hold 5s to unpause");
            this->unschedule(schedule_selector(ModifiedPauseLayer::onHoldUpdate));
        }
    }

    void mouseUp(CCEvent* e) { cancelHold(); PauseLayer::mouseUp(e); }
    void ccTouchEnded(CCTouch* t, CCEvent* e) { cancelHold(); PauseLayer::ccTouchEnded(t, e); }
};

// ==========================================
// PLAY LAYER — AI
// ==========================================
class $modify(ModifiedPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level) {
        if (!PlayLayer::init(level)) return false;
        std::string id = level->m_levelID ? std::to_string(level->m_levelID) : 
                         (level->m_levelName ? level->m_levelName : "unknown");
        s_ai.setLevel(id);
        s_ai.reset();
        s_aiActive = Mod::get()->getSettingValue<bool>("enable-ai");
        return true;
    }

    void update(float dt) {
        PlayLayer::update(dt);
        if (!s_aiActive || m_isPaused || m_gameState != 0) return;

        float px = m_player1->getPositionX();
        float py = m_player1->getPositionY();
        float nearestDist = 999.0f, nearestDy = 0.0f;

        for (auto* obj : m_gameObjects) {
            if (!obj || !obj->isVisible()) continue;
            float dx = obj->getPositionX() - px;
            if (dx > 0.5f && dx < nearestDist) {
                nearestDist = dx;
                nearestDy = obj->getPositionY() - py;
            }
        }

        s_ai.update(nearestDist, nearestDy, dt);

        if (s_ai.shouldJump() && !m_player1->m_isJumping) {
            this->handleButton(true, false);
        }
        bool wantHold = s_ai.shouldHold();
        if (wantHold != s_ai.isHolding) {
            s_ai.isHolding = wantHold;
            this->handleButton(wantHold, true);
        }
    }

    void levelComplete(GJLevelResultInfo* info) {
        s_ai.recordAttempt(true);
        PlayLayer::levelComplete(info);
    }

    void playerDeath(GameObject* p) {
        s_ai.recordAttempt(false);
        s_ai.reset();
        PlayLayer::playerDeath(p);
    }
};
