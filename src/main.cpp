#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/utils/web.hpp>
#include <chrono>
#include <deque>
#include <vector>
#include <cmath>
#include <fstream>
#include <sstream>

using namespace geode::prelude;
using namespace std::chrono;

// ==========================================
// FLY-BRAIN AI WITH PERSISTENT MEMORY
// Saves what it learns — remembers straight flying,
// safe distances, jump patterns, and improves over time
// ==========================================

struct LearnedPattern {
    float startDist;
    float heightDiff;
    float duration;
    bool action;      // true=jump, false=hold
    float successRate;
    int seenCount;
};

struct FlyBrainAI {
    struct Neuron {
        float potential = 0.0f;
        float threshold = 1.0f;
        float decay = 0.92f;
        bool fired = false;
        float baseWeight = 1.0f;
        float learnedWeight = 1.0f; // ADJUSTED BY EXPERIENCE
    };

    Neuron obstacleSensor;
    Neuron jumpNeuron;
    Neuron holdNeuron;
    Neuron straightFlyNeuron; // NEW: remembers straight flying!

    struct MemoryEntry {
        float distance;
        float height;
        float duration;
        bool actionTaken;
        bool success;
        bool wasStraightSection;
    };
    
    std::deque<MemoryEntry> shortTermMemory;
    std::vector<LearnedPattern> longTermMemory; // PERSISTENT — SAVED TO DISK
    static constexpr size_t SHORT_TERM_MAX = 300;
    static constexpr size_t LONG_TERM_MAX = 500;

    float lastDist = 999.0f;
    float lastHeight = 0.0f;
    float sectionTimer = 0.0f;
    bool isStraightSection = true;
    bool isHolding = false;
    int failCount = 0;
    int attemptCount = 0;
    float confidence = 0.5f;
    std::string levelIdentifier;

    FlyBrainAI() {
        loadMemory(); // LOAD PREVIOUS LEARNING ON START
    }

    void setLevel(std::string const& id) {
        levelIdentifier = id;
        loadMemory();
    }

    void update(float distToNext, float heightDiff, float speedMult, float dt) {
        sectionTimer += dt;

        // DETECT STRAIGHT FLYING SECTIONS
        if (distToNext > 15.0f && std::abs(heightDiff) < 0.4f) {
            isStraightSection = true;
            straightFlyNeuron.potential = straightFlyNeuron.potential * 0.98f + 0.3f;
        } else {
            isStraightSection = false;
            straightFlyNeuron.potential *= 0.9f;
        }

        // LEAKY INTEGRATE with learned weights
        obstacleSensor.potential = obstacleSensor.potential * obstacleSensor.decay;
        jumpNeuron.potential = jumpNeuron.potential * jumpNeuron.decay;
        holdNeuron.potential = holdNeuron.potential * holdNeuron.decay;

        float distInput = std::max(0.0f, 3.0f / (distToNext + 0.5f));
        float heightInput = heightDiff * 0.8f;
        obstacleSensor.potential += distInput;

        // USE LEARNED WEIGHTS instead of fixed values
        float jumpWeight = 0.6f * jumpNeuron.learnedWeight * confidence;
        float holdWeight = 0.6f * holdNeuron.learnedWeight;

        if (distToNext < 10.0f && distToNext > 0.3f) {
            if (heightDiff > 0.35f) {
                jumpNeuron.potential += distInput * jumpWeight;
            }
            if (heightDiff < -0.35f) {
                holdNeuron.potential += distInput * holdWeight;
            }
        }

        // FIRE DECISIONS
        straightFlyNeuron.fired = straightFlyNeuron.potential >= 0.85f;
        jumpNeuron.fired = jumpNeuron.potential >= jumpNeuron.threshold;
        holdNeuron.fired = holdNeuron.potential >= holdNeuron.threshold;

        lastDist = distToNext;
        lastHeight = heightDiff;
    }

    bool shouldJump() {
        if (straightFlyNeuron.fired) return false; // DON'T JUMP DURING STRAIGHT FLYING
        if (jumpNeuron.fired) {
            jumpNeuron.potential = 0.0f;
            return true;
        }
        return false;
    }

    bool shouldHold() {
        if (straightFlyNeuron.fired) return true; // HOLD FOR STRAIGHT FLYING
        if (holdNeuron.fired) {
            holdNeuron.potential = 0.0f;
            return true;
        }
        return false;
    }

    void recordStep(bool success) {
        if (shortTermMemory.size() >= SHORT_TERM_MAX) shortTermMemory.pop_front();
        shortTermMemory.push_back({
            lastDist, lastHeight, sectionTimer,
            shouldJump(), success, isStraightSection
        });
    }

    void recordAttempt(bool success) {
        attemptCount++;
        sectionTimer = 0.0f;
        
        if (!success) {
            failCount++;
            // Lower confidence, adjust weights to avoid same mistake
            jumpNeuron.learnedWeight = std::max(0.5f, jumpNeuron.learnedWeight - 0.03f);
            holdNeuron.learnedWeight = std::max(0.5f, holdNeuron.learnedWeight - 0.02f);
        } else {
            // Reinforce successful patterns
            jumpNeuron.learnedWeight = std::min(1.5f, jumpNeuron.learnedWeight + 0.015f);
            holdNeuron.learnedWeight = std::min(1.5f, holdNeuron.learnedWeight + 0.01f);
            straightFlyNeuron.learnedWeight = std::min(1.5f, straightFlyNeuron.learnedWeight + 0.02f);
        }

        if (attemptCount > 5) {
            confidence = 1.0f - (float)failCount / (float)attemptCount;
            confidence = std::clamp(confidence, 0.15f, 0.95f);
        }

        // Save successful patterns to LONG-TERM MEMORY
        if (success) {
            for (auto const& entry : shortTermMemory) {
                if (entry.wasStraightSection || entry.actionTaken) {
                    addToLongTerm(entry);
                }
            }
        }

        if (Mod::get()->getSettingValue<bool>("auto-save-memory")) {
            saveMemory();
        }
        
        shortTermMemory.clear();
    }

    void addToLongTerm(MemoryEntry const& e) {
        for (auto& p : longTermMemory) {
            if (std::abs(p.startDist - e.distance) < 1.0f &&
                std::abs(p.heightDiff - e.height) < 0.3f &&
                p.action == e.actionTaken) {
                // Update existing pattern
                p.seenCount++;
                p.successRate = (p.successRate * (p.seenCount - 1) + (e.success ? 1.0f : 0.0f)) / p.seenCount;
                return;
            }
        }
        // Add NEW pattern
        if (longTermMemory.size() < LONG_TERM_MAX) {
            longTermMemory.push_back({
                e.distance, e.height, e.duration,
                e.actionTaken, e.success ? 1.0f : 0.5f, 1
            });
        }
    }

    void reset() {
        obstacleSensor.potential = 0.0f;
        jumpNeuron.potential = 0.0f;
        holdNeuron.potential = 0.0f;
        straightFlyNeuron.potential = 0.0f;
        isHolding = false;
        isStraightSection = true;
        sectionTimer = 0.0f;
        shortTermMemory.clear();
    }

    // === PERSISTENT MEMORY SAVE/LOAD ===
    std::string getMemoryPath() const {
        return Mod::get()->getSaveDir() / ("brain_memory_" + levelIdentifier + ".json");
    }

    void saveMemory() {
        if (levelIdentifier.empty()) return;
        try {
            std::string json = "{\n";
            json += "  \"attempts\": " + std::to_string(attemptCount) + ",\n";
            json += "  \"fails\": " + std::to_string(failCount) + ",\n";
            json += "  \"confidence\": " + std::to_string(confidence) + ",\n";
            json += "  \"jump_weight\": " + std::to_string(jumpNeuron.learnedWeight) + ",\n";
            json += "  \"hold_weight\": " + std::to_string(holdNeuron.learnedWeight) + ",\n";
            json += "  \"straight_weight\": " + std::to_string(straightFlyNeuron.learnedWeight) + "\n";
            json += "}";
            std::ofstream f(getMemoryPath());
            f << json;
        } catch (...) {}
    }

    void loadMemory() {
        if (levelIdentifier.empty()) return;
        try {
            std::ifstream f(getMemoryPath());
            if (!f.is_open()) return;
            std::string line, content;
            while (std::getline(f, line)) content += line;
            
            auto extract = [&](std::string key) -> float {
                size_t pos = content.find("\"" + key + "\":");
                if (pos == std::string::npos) return -1;
                pos += key.length() + 3;
                float v = 0;
                sscanf(content.substr(pos).c_str(), "%f", &v);
                return v;
            };

            if (float v = extract("attempts"); v > 0) attemptCount = (int)v;
            if (float v = extract("fails"); v >= 0) failCount = (int)v;
            if (float v = extract("confidence"); v > 0) confidence = v;
            if (float v = extract("jump_weight"); v > 0) jumpNeuron.learnedWeight = v;
            if (float v = extract("hold_weight"); v > 0) holdNeuron.learnedWeight = v;
            if (float v = extract("straight_weight"); v > 0) straightFlyNeuron.learnedWeight = v;

            log::info("Loaded memory for {} — conf: {:.2f}", levelIdentifier, confidence);
        } catch (...) {
            log::warn("No saved memory found, starting fresh");
        }
    }
};

static FlyBrainAI s_ai;
static bool s_aiActive = true;

// ==========================================
// PAUSE MENU — 5-SECOND RESUME HOLD
// ==========================================

class $modify(ModifiedPauseLayer, PauseLayer) {
    bool m_isHoldingResume = false;
    steady_clock::time_point m_holdStart;
    bool m_holdCompleted = false;
    CCLabelBMFont* m_holdLabel = nullptr;

    void customSetup() {
        PauseLayer::customSetup();
        
        if (auto resumeBtn = this->getChildByID("resume-button")) {
            resumeBtn->removeFromParent();
            
            auto btn = CCMenuItemSpriteExtra::create(
                CCSprite::createWithSpriteFrameName("GJ_playBtn_001.png"),
                this,
                menu_selector(ModifiedPauseLayer::onResumeStart)
            );
            btn->setID("resume-button");
            btn->setPosition({0, -60});

            m_holdLabel = CCLabelBMFont::create(
                "Hold Resume 5s to unpause!",
                "bigFont.fnt"
            );
            m_holdLabel->setPosition({0, 40});
            m_holdLabel->setScale(0.5f);
            m_holdLabel->setVisible(false);
            this->addChild(m_holdLabel, 10);

            if (auto menu = this->getChildByType<CCMenu>(0)) {
                menu->addChild(btn);
            }
        }
    }

    void onResumeStart(CCObject*) {
        m_isHoldingResume = true;
        m_holdCompleted = false;
        m_holdStart = steady_clock::now();
        if (m_holdLabel) m_holdLabel->setVisible(true);
        this->schedule(schedule_selector(ModifiedPauseLayer::onHoldUpdate), 0.05f);
    }

    void onHoldUpdate(float) {
        if (!m_isHoldingResume) {
            this->unschedule(schedule_selector(ModifiedPauseLayer::onHoldUpdate));
            return;
        }

        auto elapsed = duration_cast<milliseconds>(
            steady_clock::now() - m_holdStart
        ).count() / 1000.0f;

        if (m_holdLabel) {
            float remaining = std::max(0.0f, 5.0f - elapsed);
            m_holdLabel->setString(
                CCString::createWithFormat("Unpausing in %.1fs...", remaining)->getCString()
            );
        }

        if (elapsed >= 5.0f && !m_holdCompleted) {
            m_holdCompleted = true;
            m_isHoldingResume = false;
            if (m_holdLabel) m_holdLabel->setString("Unpausing now!");
            this->unschedule(schedule_selector(ModifiedPauseLayer::onHoldUpdate));
            PauseLayer::onResume(nullptr);
        }
    }

    void mouseUp(CCEvent* evt) {
        if (m_isHoldingResume && !m_holdCompleted) {
            m_isHoldingResume = false;
            if (m_holdLabel) {
                m_holdLabel->setString("Cancelled — hold 5s to unpause");
            }
            this->unschedule(schedule_selector(ModifiedPauseLayer::onHoldUpdate));
        }
        PauseLayer::mouseUp(evt);
    }

    void ccTouchEnded(CCTouch* t, CCEvent* e) {
        if (m_isHoldingResume && !m_holdCompleted) {
            m_isHoldingResume = false;
            if (m_holdLabel) {
                m_holdLabel->setString("Cancelled — hold 5s to unpause");
            }
            this->unschedule(schedule_selector(ModifiedPauseLayer::onHoldUpdate));
        }
        PauseLayer::ccTouchEnded(t, e);
    }
};

// ==========================================
// PLAY LAYER — AI DECISION & INPUT
// ==========================================

class $modify(ModifiedPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level) {
        if (!PlayLayer::init(level)) return false;
        
        // Set unique ID for this level's memory
        std::string levelID = level->m_levelID 
            ? std::to_string(level->m_levelID) 
            : (level->m_levelName ? level->m_levelName : "unknown");
        s_ai.setLevel(levelID);
        s_ai.reset();
        s_aiActive = Mod::get()->getSettingValue<bool>("enable-ai");
        
        return true;
    }

    void update(float dt) {
        PlayLayer::update(dt);
        if (!s_aiActive || m_isPaused || m_gameState != 0) return;

        float playerX = m_player1->getPositionX();
        float playerY = m_player1->getPositionY();

        float nearestDist = 999.0f;
        float nearestYDiff = 0.0f;

        for (auto* obj : m_gameObjects) {
            if (!obj || !obj->isVisible()) continue;
            float objX = obj->getPositionX();
            float dx = objX - playerX;
            
            if (dx > 0.5f && dx < nearestDist) {
                nearestDist = dx;
                nearestYDiff = obj->getPositionY() - playerY;
            }
        }

        float speedMult = Mod::get()->getSettingValue<float>("ai-speed");
        s_ai.update(nearestDist, nearestYDiff, speedMult, dt);

        // EXECUTE DECISIONS
        if (s_ai.shouldJump()) {
            if (!m_player1->m_isJumping) {
                this->handleButton(true, false);
            }
        }

        bool wantHold = s_ai.shouldHold();
        if (wantHold != s_ai.isHolding) {
            s_ai.isHolding = wantHold;
            this->handleButton(wantHold, true);
        }

        s_ai.recordStep(false);
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
