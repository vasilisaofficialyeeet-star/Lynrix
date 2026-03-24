#include "src/app/application.h"
#include "src/config/config_loader.h"

#include <spdlog/spdlog.h>

#include <iostream>
#include <memory>
#include <string>

int main(int argc, char* argv[]) {
    try {
        std::string config_path = "config.json";
        if (argc > 1) {
            config_path = argv[1];
        }

        // Load configuration
        auto cfg = bybit::ConfigLoader::load(config_path);

        // Validate critical settings
        if (cfg.paper_trading) {
            spdlog::info("Running in PAPER TRADING mode");
        } else {
            if (cfg.api_key.empty() || cfg.api_secret.empty()) {
                spdlog::error("API key/secret not set. Set BYBIT_API_KEY and BYBIT_API_SECRET env vars.");
                return 1;
            }
            spdlog::warn("Running in LIVE TRADING mode — real money at risk");
        }

        // Create and run application (signal handling is internal via boost::asio::signal_set)
        auto app = std::make_unique<bybit::Application>(cfg);
        app->run();

        return 0;
    } catch (const std::exception& e) {
        spdlog::error("Fatal error: {}", e.what());
        return 1;
    }
}
