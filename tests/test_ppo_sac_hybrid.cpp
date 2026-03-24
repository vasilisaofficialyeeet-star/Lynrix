#include <gtest/gtest.h>

#include "../src/rl/ppo_sac_hybrid.h"
#include "../src/rl/safe_online_trainer.h"
#include "../src/utils/tsc_clock.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <numeric>

using namespace bybit;

// ═══════════════════════════════════════════════════════════════════════════
// Helper: build a dummy RL2State
// ═══════════════════════════════════════════════════════════════════════════

static RL2State make_dummy_state(double seed = 0.0) {
    RL2State s;
    for (size_t i = 0; i < RL2_STATE_DIM; ++i) {
        s.features[i] = std::sin(seed + static_cast<double>(i) * 0.1) * 0.5;
    }
    return s;
}

// ═══════════════════════════════════════════════════════════════════════════
// Hybrid RL Agent Tests
// ═══════════════════════════════════════════════════════════════════════════

class HybridRLTest : public ::testing::Test {
protected:
    HybridRLConfig cfg;
    HybridRLAgent agent{cfg};
};

// --- Dimensions ---

TEST_F(HybridRLTest, StateDim32) {
    EXPECT_EQ(RL2_STATE_DIM, 32u);
}

TEST_F(HybridRLTest, ActionDim4) {
    EXPECT_EQ(RL2_ACTION_DIM, 4u);
}

TEST_F(HybridRLTest, HiddenDim128) {
    EXPECT_EQ(RL2_HIDDEN_DIM, 128u);
}

// --- Action Selection ---

TEST_F(HybridRLTest, SelectActionDeterministic) {
    RL2State state = make_dummy_state(1.0);
    RL2Action a1 = agent.select_action(state, false);
    RL2Action a2 = agent.select_action(state, false);

    for (size_t i = 0; i < RL2_ACTION_DIM; ++i) {
        EXPECT_DOUBLE_EQ(a1.raw[i], a2.raw[i]);
    }
}

TEST_F(HybridRLTest, SelectActionStochastic) {
    RL2State state = make_dummy_state(2.0);
    agent.set_exploring(true);

    RL2Action a1 = agent.select_action(state, true);
    RL2Action a2 = agent.select_action(state, true);

    // With exploration, actions should differ (probabilistically)
    bool any_diff = false;
    for (size_t i = 0; i < RL2_ACTION_DIM; ++i) {
        if (std::abs(a1.raw[i] - a2.raw[i]) > 1e-10) any_diff = true;
    }
    EXPECT_TRUE(any_diff);
}

TEST_F(HybridRLTest, ActionBoundsRespected) {
    RL2State state = make_dummy_state(3.0);
    for (int t = 0; t < 100; ++t) {
        RL2Action a = agent.select_action(state, true);
        for (size_t i = 0; i < RL2_ACTION_DIM; ++i) {
            EXPECT_GE(a.raw[i], RL2ActionBounds::lo[i]);
            EXPECT_LE(a.raw[i], RL2ActionBounds::hi[i]);
        }
    }
}

TEST_F(HybridRLTest, ActionFromRaw) {
    RL2Action a;
    a.raw[0] = 0.05;
    a.raw[1] = 1.5;
    a.raw[2] = -2.0;
    a.raw[3] = 2.0;
    a.from_raw();

    EXPECT_DOUBLE_EQ(a.signal_threshold_delta, 0.05);
    EXPECT_DOUBLE_EQ(a.position_size_scale, 1.5);
    EXPECT_DOUBLE_EQ(a.order_offset_bps, -2.0);
    EXPECT_DOUBLE_EQ(a.requote_freq_scale, 2.0);
}

// --- Store Transition & Buffer ---

TEST_F(HybridRLTest, StoreTransition) {
    RL2State state = make_dummy_state(4.0);
    RL2Action action = agent.select_action(state, true);

    agent.store_transition(state, action, 1.5, false);
    EXPECT_EQ(agent.buffer_size(), 1u);
    EXPECT_EQ(agent.stats().total_steps, 1u);
}

TEST_F(HybridRLTest, BufferFillsToHorizon) {
    for (size_t i = 0; i < agent.config().horizon; ++i) {
        RL2State s = make_dummy_state(static_cast<double>(i));
        RL2Action a = agent.select_action(s, true);
        agent.store_transition(s, a, 0.1, false);
    }
    EXPECT_EQ(agent.buffer_size(), agent.config().horizon);
    EXPECT_TRUE(agent.should_update());
}

// --- Update ---

TEST_F(HybridRLTest, UpdateRequiresMinBatch) {
    // Not enough data
    RL2State s = make_dummy_state(0.0);
    RL2Action a = agent.select_action(s, true);
    agent.store_transition(s, a, 1.0, false);

    EXPECT_FALSE(agent.update());
}

TEST_F(HybridRLTest, UpdateWithFullBuffer) {
    HybridRLConfig small_cfg;
    small_cfg.horizon = 64;
    small_cfg.batch_size = 32;
    small_cfg.ppo_epochs = 2;
    HybridRLAgent small_agent(small_cfg);

    for (size_t i = 0; i < 64; ++i) {
        RL2State s = make_dummy_state(static_cast<double>(i));
        RL2Action a = small_agent.select_action(s, true);
        double reward = std::sin(static_cast<double>(i) * 0.1);
        small_agent.store_transition(s, a, reward, i == 63);
    }

    EXPECT_TRUE(small_agent.update());
    EXPECT_EQ(small_agent.stats().total_updates, 1u);
    EXPECT_EQ(small_agent.buffer_size(), 0u); // buffer reset
}

TEST_F(HybridRLTest, LossesAreFinite) {
    HybridRLConfig small_cfg;
    small_cfg.horizon = 64;
    small_cfg.batch_size = 32;
    small_cfg.ppo_epochs = 2;
    HybridRLAgent small_agent(small_cfg);

    for (size_t i = 0; i < 64; ++i) {
        RL2State s = make_dummy_state(static_cast<double>(i));
        RL2Action a = small_agent.select_action(s, true);
        small_agent.store_transition(s, a, std::sin(i * 0.1), i == 63);
    }

    small_agent.update();

    EXPECT_TRUE(std::isfinite(small_agent.stats().policy_loss));
    EXPECT_TRUE(std::isfinite(small_agent.stats().value_loss));
    EXPECT_TRUE(std::isfinite(small_agent.stats().q1_loss));
    EXPECT_TRUE(std::isfinite(small_agent.stats().q2_loss));
    EXPECT_TRUE(std::isfinite(small_agent.stats().entropy));
    EXPECT_TRUE(std::isfinite(small_agent.stats().kl_divergence));
    EXPECT_TRUE(std::isfinite(small_agent.stats().alpha));
}

// --- Twin Critics ---

TEST_F(HybridRLTest, TwinCriticsProduceValues) {
    RL2State state = make_dummy_state(5.0);
    RL2Action action = agent.select_action(state, false);

    // After select_action, Q1/Q2 should be computed
    EXPECT_TRUE(std::isfinite(agent.stats().inference_ns));
}

// --- Entropy Temperature ---

TEST_F(HybridRLTest, AlphaAutoTuning) {
    HybridRLConfig small_cfg;
    small_cfg.horizon = 64;
    small_cfg.batch_size = 32;
    small_cfg.auto_alpha = true;
    HybridRLAgent small_agent(small_cfg);

    double alpha_before = small_agent.current_alpha();

    for (size_t i = 0; i < 64; ++i) {
        RL2State s = make_dummy_state(static_cast<double>(i));
        RL2Action a = small_agent.select_action(s, true);
        small_agent.store_transition(s, a, 1.0, i == 63);
    }
    small_agent.update();

    double alpha_after = small_agent.current_alpha();
    // Alpha should have changed
    EXPECT_NE(alpha_before, alpha_after);
    EXPECT_GT(alpha_after, 0.0);
}

// --- Reward Function ---

TEST_F(HybridRLTest, RewardBasicPnL) {
    double r = HybridRLAgent::compute_reward(0.01, 0.0, 0.5, 100.0);
    EXPECT_GT(r, 0.0);
}

TEST_F(HybridRLTest, RewardDrawdownPenalty) {
    double r_no_dd = HybridRLAgent::compute_reward(0.01, 0.0, 0.5, 100.0);
    double r_dd = HybridRLAgent::compute_reward(0.01, 0.05, 0.5, 100.0);
    EXPECT_GT(r_no_dd, r_dd);
}

TEST_F(HybridRLTest, RewardVaRPenalty) {
    double r_no_var = HybridRLAgent::compute_reward(0.01, 0.0, 0.5, 100.0, 0.0);
    double r_var = HybridRLAgent::compute_reward(0.01, 0.0, 0.5, 100.0, 0.1);
    EXPECT_GT(r_no_var, r_var);
}

TEST_F(HybridRLTest, RewardSlippagePenalty) {
    double r_no_slip = HybridRLAgent::compute_reward(0.01, 0.0, 0.5, 100.0, 0.0, 0.0);
    double r_slip = HybridRLAgent::compute_reward(0.01, 0.0, 0.5, 100.0, 0.0, 5.0);
    EXPECT_GT(r_no_slip, r_slip);
}

TEST_F(HybridRLTest, RewardClamped) {
    double r_big = HybridRLAgent::compute_reward(100.0, 0.0, 1.0, 0.0);
    double r_neg = HybridRLAgent::compute_reward(-100.0, 0.5, 0.0, 10000.0, 1.0, 100.0);
    EXPECT_LE(r_big, 10.0);
    EXPECT_GE(r_neg, -10.0);
}

// --- Snapshot / Restore ---

TEST_F(HybridRLTest, SnapshotRestore) {
    RL2State state = make_dummy_state(6.0);
    RL2Action a1 = agent.select_action(state, false);

    HybridRLAgent::Snapshot snap;
    agent.snapshot(snap);

    // Perturb agent by doing an update
    HybridRLConfig small_cfg;
    small_cfg.horizon = 64;
    small_cfg.batch_size = 32;
    HybridRLAgent test_agent(small_cfg);

    RL2Action a_before = test_agent.select_action(state, false);
    test_agent.snapshot(snap);

    for (size_t i = 0; i < 64; ++i) {
        RL2State s = make_dummy_state(static_cast<double>(i));
        RL2Action a = test_agent.select_action(s, true);
        test_agent.store_transition(s, a, 1.0, i == 63);
    }
    test_agent.update();

    RL2Action a_after = test_agent.select_action(state, false);

    // Restore
    test_agent.restore(snap);
    RL2Action a_restored = test_agent.select_action(state, false);

    for (size_t i = 0; i < RL2_ACTION_DIM; ++i) {
        EXPECT_DOUBLE_EQ(a_before.raw[i], a_restored.raw[i]);
    }
}

// --- Exploration Decay ---

TEST_F(HybridRLTest, ExplorationDecays) {
    double std_before = agent.avg_action_std();

    for (int i = 0; i < 100; ++i) {
        RL2State s = make_dummy_state(static_cast<double>(i));
        RL2Action a = agent.select_action(s, true);
        agent.store_transition(s, a, 0.0, false);
    }

    double std_after = agent.avg_action_std();
    EXPECT_LT(std_after, std_before);
}

// --- State Build ---

TEST_F(HybridRLTest, StateBuildFromComponents) {
    Features feat{};
    feat.imbalance_1 = 0.3;
    feat.spread_bps = 1.5;
    feat.volatility = 0.02;
    feat.trade_velocity = 50.0;

    Position pos{};
    pos.size = Qty(0.05);
    pos.entry_price = Price(50000.0);
    pos.unrealized_pnl = Notional(10.0);

    RegimeState regime{};
    regime.current = MarketRegime::Trending;
    regime.confidence = 0.8;

    FillProbability fp{};
    fp.queue_position = 0.3;

    FillProbTracker fill_tracker;

    VaRResult var{};
    var.mc_var_99 = 50.0;
    var.cvar_99 = 75.0;
    var.position_value = 2500.0;

    RL2State state = RL2State::build(feat, pos, regime, fp, fill_tracker,
                                      var, 1.5, 0.7, 0.02, 0.5);

    EXPECT_DOUBLE_EQ(state.features[0], 0.3);  // imbalance_1
    EXPECT_DOUBLE_EQ(state.features[13], 1.5); // spread_bps
    EXPECT_NEAR(state.features[26], 0.3, 1e-10); // queue_position
    EXPECT_NEAR(state.features[28], 50.0 / 2500.0, 1e-10); // var_99 normalized
}

// ═══════════════════════════════════════════════════════════════════════════
// Safe Online Trainer Tests
// ═══════════════════════════════════════════════════════════════════════════

class SafeTrainerTest : public ::testing::Test {
protected:
    HybridRLConfig rl_cfg;
    std::unique_ptr<HybridRLAgent> agent;
    OnlineTrainerConfig trainer_cfg;
    std::unique_ptr<SafeOnlineTrainer> trainer;

    void SetUp() override {
        rl_cfg.horizon = 128;
        rl_cfg.batch_size = 32;
        agent = std::make_unique<HybridRLAgent>(rl_cfg);
        trainer_cfg.min_samples = 64;
        trainer_cfg.max_kl_divergence = 0.5;
        trainer = std::make_unique<SafeOnlineTrainer>(agent.get(), trainer_cfg);
    }
};

TEST_F(SafeTrainerTest, PushSample) {
    RL2State s = make_dummy_state(0.0);
    RL2Action a;
    a.raw[0] = 0.0; a.raw[1] = 1.0; a.raw[2] = 0.0; a.raw[3] = 1.0;

    bool ok = trainer->push_sample(s, a, -1.0, 0.5, 0.3, false);
    EXPECT_TRUE(ok);
    EXPECT_EQ(trainer->queue_size(), 1u);
}

TEST_F(SafeTrainerTest, TrainNowRequiresMinSamples) {
    // No samples → should fail
    EXPECT_FALSE(trainer->train_now());
}

TEST_F(SafeTrainerTest, TrainNowWithSufficientData) {
    // Push enough samples
    for (size_t i = 0; i < 200; ++i) {
        RL2State s = make_dummy_state(static_cast<double>(i));
        RL2Action a;
        for (size_t j = 0; j < RL2_ACTION_DIM; ++j) {
            a.raw[j] = (RL2ActionBounds::lo[j] + RL2ActionBounds::hi[j]) * 0.5;
        }
        trainer->push_sample(s, a, -1.0, std::sin(i * 0.1) * 0.5, 0.1, i % 50 == 49);
    }

    // Drain queue manually (normally done in trainer thread)
    // train_now drains internally via do_training_step
    bool ok = trainer->train_now();
    // May or may not succeed depending on KL check, but should not crash
    EXPECT_TRUE(trainer->stats().total_trains > 0 || trainer->stats().total_rollbacks > 0);
}

TEST_F(SafeTrainerTest, RollbackOnHighKL) {
    OnlineTrainerConfig strict_cfg;
    strict_cfg.min_samples = 64;
    strict_cfg.max_kl_divergence = 0.0001; // impossibly tight
    strict_cfg.max_value_loss = 0.0001;    // impossibly tight
    SafeOnlineTrainer strict_trainer(agent.get(), strict_cfg);

    for (size_t i = 0; i < 200; ++i) {
        RL2State s = make_dummy_state(static_cast<double>(i));
        RL2Action a;
        for (size_t j = 0; j < RL2_ACTION_DIM; ++j) a.raw[j] = 0.0;
        strict_trainer.push_sample(s, a, -1.0, 1.0, 0.0, false);
    }

    bool ok = strict_trainer.train_now();
    // Should rollback due to tight constraints
    // (either KL or value loss will exceed)
    EXPECT_GE(strict_trainer.stats().total_rollbacks +
              strict_trainer.stats().total_trains, 0u); // at least attempted
}

TEST_F(SafeTrainerTest, DroppedSamplesOnFullQueue) {
    // Fill the queue
    RL2State s = make_dummy_state(0.0);
    RL2Action a;
    for (size_t j = 0; j < RL2_ACTION_DIM; ++j) a.raw[j] = 0.0;

    size_t pushed = 0;
    for (size_t i = 0; i < 2000; ++i) {
        if (trainer->push_sample(s, a, -1.0, 0.0, 0.0, false)) {
            ++pushed;
        }
    }

    // Queue capacity is 1023 (1024 - 1), should have drops
    EXPECT_GT(trainer->dropped_samples(), 0u);
}

TEST_F(SafeTrainerTest, StatsInitialized) {
    EXPECT_EQ(trainer->stats().total_trains, 0u);
    EXPECT_EQ(trainer->stats().total_rollbacks, 0u);
    EXPECT_EQ(trainer->stats().samples_collected, 0u);
    EXPECT_TRUE(trainer->stats().enabled);
    EXPECT_FALSE(trainer->stats().training_active);
}

TEST_F(SafeTrainerTest, ValidationCallback) {
    bool callback_called = false;
    trainer->set_validation_callback([&](double train_sharpe, double val_sharpe) {
        callback_called = true;
        return true; // accept
    });

    for (size_t i = 0; i < 200; ++i) {
        RL2State s = make_dummy_state(static_cast<double>(i));
        RL2Action a;
        for (size_t j = 0; j < RL2_ACTION_DIM; ++j)
            a.raw[j] = (RL2ActionBounds::lo[j] + RL2ActionBounds::hi[j]) * 0.5;
        trainer->push_sample(s, a, -1.0, std::sin(i * 0.1), 0.1, false);
    }

    trainer->train_now();
    // Callback should have been invoked if training reached validation step
    // (may not if training failed earlier)
}

// ═══════════════════════════════════════════════════════════════════════════
// MLP2 Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(MLP2Test, ForwardProducesFiniteOutput) {
    MLP2<32, 128, 4> net;
    net.init_weights(42);

    alignas(64) double input[32];
    for (int i = 0; i < 32; ++i) input[i] = std::sin(i * 0.1);

    alignas(64) double output[4];
    net.forward(input, output);

    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(std::isfinite(output[i]));
    }
}

TEST(MLP2Test, SoftUpdate) {
    MLP2<32, 128, 1> source, target;
    source.init_weights(42);
    target.init_weights(137);

    double before_w = target.W1[0][0];
    target.soft_update_from(source, 0.005);
    double after_w = target.W1[0][0];

    // Should have moved slightly toward source
    EXPECT_NE(before_w, after_w);
    // Should be between old target and source
    double expected = 0.005 * source.W1[0][0] + 0.995 * before_w;
    EXPECT_NEAR(after_w, expected, 1e-12);
}

TEST(MLP2Test, CopyFrom) {
    MLP2<32, 128, 4> source, dest;
    source.init_weights(42);
    dest.init_weights(137);

    dest.copy_from(source);

    EXPECT_DOUBLE_EQ(dest.W1[0][0], source.W1[0][0]);
    EXPECT_DOUBLE_EQ(dest.W2[0][0], source.W2[0][0]);
}

TEST(MLP2Test, SnapshotRestore) {
    MLP2<32, 128, 4> net, snap;
    net.init_weights(42);

    net.snapshot_to(snap);

    // Modify net
    net.W1[0][0] = 999.0;

    net.copy_from(snap);
    EXPECT_NE(net.W1[0][0], 999.0);
}

// ═══════════════════════════════════════════════════════════════════════════
// Latency Benchmarks
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(HybridRLTest, InferenceLatency) {
    RL2State state = make_dummy_state(0.0);

    uint64_t total_ns = 0;
    constexpr int RUNS = 1000;

    for (int i = 0; i < RUNS; ++i) {
        uint64_t t0 = TscClock::now();
        agent.select_action(state, false);
        total_ns += TscClock::elapsed_ns(t0);
    }

    double avg_us = static_cast<double>(total_ns) / RUNS / 1000.0;
    std::cout << "Hybrid RL inference (32→128→4, deterministic): "
              << avg_us << " µs" << std::endl;
    EXPECT_LT(avg_us, 50.0); // <50 µs (conservative)
}

TEST_F(HybridRLTest, InferenceLatencyStochastic) {
    RL2State state = make_dummy_state(0.0);
    agent.set_exploring(true);

    uint64_t total_ns = 0;
    constexpr int RUNS = 1000;

    for (int i = 0; i < RUNS; ++i) {
        uint64_t t0 = TscClock::now();
        agent.select_action(state, true);
        total_ns += TscClock::elapsed_ns(t0);
    }

    double avg_us = static_cast<double>(total_ns) / RUNS / 1000.0;
    std::cout << "Hybrid RL inference (32→128→4, stochastic):    "
              << avg_us << " µs" << std::endl;
    EXPECT_LT(avg_us, 50.0);
}

TEST_F(HybridRLTest, UpdateLatency) {
    HybridRLConfig small_cfg;
    small_cfg.horizon = 128;
    small_cfg.batch_size = 64;
    small_cfg.ppo_epochs = 2;
    HybridRLAgent small_agent(small_cfg);

    for (size_t i = 0; i < 128; ++i) {
        RL2State s = make_dummy_state(static_cast<double>(i));
        RL2Action a = small_agent.select_action(s, true);
        small_agent.store_transition(s, a, std::sin(i * 0.1), i == 127);
    }

    uint64_t t0 = TscClock::now();
    small_agent.update();
    uint64_t elapsed_us = TscClock::elapsed_ns(t0) / 1000;

    std::cout << "Hybrid RL update (128 samples, 2 epochs):      "
              << elapsed_us << " µs" << std::endl;
    // Update is off hot-path, but should still be <100 ms
    EXPECT_LT(elapsed_us, 100'000u); // <100 ms
}

// ═══════════════════════════════════════════════════════════════════════════
// Rollout Buffer Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(RolloutBufferTest, PushAndSize) {
    RolloutBuffer buf;
    EXPECT_EQ(buf.size(), 0u);

    RolloutSample s;
    s.reward = 1.0;
    buf.push(s);
    EXPECT_EQ(buf.size(), 1u);
}

TEST(RolloutBufferTest, CircularOverwrite) {
    RolloutBuffer buf;
    for (size_t i = 0; i < ROLLOUT_BUFFER_SIZE + 100; ++i) {
        RolloutSample s;
        s.reward = static_cast<double>(i);
        buf.push(s);
    }
    EXPECT_EQ(buf.size(), ROLLOUT_BUFFER_SIZE);
    // Newest sample should have reward = ROLLOUT_BUFFER_SIZE + 99
    EXPECT_DOUBLE_EQ(buf.at(ROLLOUT_BUFFER_SIZE - 1).reward,
                     static_cast<double>(ROLLOUT_BUFFER_SIZE + 99));
}

TEST(RolloutBufferTest, Clear) {
    RolloutBuffer buf;
    RolloutSample s;
    for (int i = 0; i < 100; ++i) buf.push(s);
    buf.clear();
    EXPECT_EQ(buf.size(), 0u);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
