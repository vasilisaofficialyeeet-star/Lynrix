#include "../src/rl/ppo_sac_hybrid.h"
#include "../src/rl/safe_online_trainer.h"
#include "../src/utils/tsc_clock.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <vector>

using namespace bybit;

// ─── Benchmark Harness ──────────────────────────────────────────────────────

template <typename Fn>
struct BenchResult {
    double mean_ns, min_ns, p50_ns, p99_ns, p999_ns, max_ns;
};

template <size_t N, typename Fn>
static BenchResult<Fn> bench(Fn&& fn) {
    std::vector<uint64_t> samples(N);

    // Warmup
    for (int i = 0; i < 100; ++i) fn();

    for (size_t i = 0; i < N; ++i) {
        uint64_t t0 = TscClock::now();
        fn();
        samples[i] = TscClock::elapsed_ns(t0);
    }

    std::sort(samples.begin(), samples.end());

    double sum = 0.0;
    for (auto s : samples) sum += static_cast<double>(s);

    return {
        sum / N,
        static_cast<double>(samples[0]),
        static_cast<double>(samples[N / 2]),
        static_cast<double>(samples[N * 99 / 100]),
        static_cast<double>(samples[N * 999 / 1000]),
        static_cast<double>(samples[N - 1])
    };
}

static void print_row(const char* name, auto r) {
    printf("  %-45s mean=%8.1fns  min=%6.0fns  p50=%6.0fns  p99=%7.0fns  p99.9=%8.0fns  max=%8.0fns\n",
           name, r.mean_ns, r.min_ns, r.p50_ns, r.p99_ns, r.p999_ns, r.max_ns);
}

// ─── Helpers ────────────────────────────────────────────────────────────────

static RL2State make_state(double seed) {
    RL2State s;
    for (size_t i = 0; i < RL2_STATE_DIM; ++i)
        s.features[i] = std::sin(seed + i * 0.1) * 0.5;
    return s;
}

// ─── Main ───────────────────────────────────────────────────────────────────

int main() {
    printf("══════════════════════════════════════════════════════════════════════\n");
    printf("                   RL Engine Benchmark (Stage 3)\n");
    printf("══════════════════════════════════════════════════════════════════════\n\n");

    HybridRLConfig cfg;
    HybridRLAgent agent(cfg);
    RL2State state = make_state(0.0);

    // ── Inference (deterministic) ──
    printf("  Hybrid PPO+SAC Inference:\n");
    {
        agent.set_exploring(false);
        auto r = bench<10000>([&]() {
            agent.select_action(state, false);
        });
        print_row("select_action (deterministic)", r);
    }

    // ── Inference (stochastic) ──
    {
        agent.set_exploring(true);
        auto r = bench<10000>([&]() {
            agent.select_action(state, true);
        });
        print_row("select_action (stochastic)", r);
    }

    // ── MLP2 Forward (actor: 32→128→4) ──
    {
        MLP2<RL2_STATE_DIM, RL2_HIDDEN_DIM, RL2_ACTION_DIM> net;
        net.init_weights(42);
        alignas(64) double input[RL2_STATE_DIM];
        for (size_t i = 0; i < RL2_STATE_DIM; ++i) input[i] = 0.1 * i;
        alignas(64) double output[RL2_ACTION_DIM];

        auto r = bench<10000>([&]() {
            net.forward(input, output);
        });
        print_row("MLP2 forward (32→128→4)", r);
    }

    // ── MLP2 Forward (Q-net: 36→128→1) ──
    {
        CriticQ qnet;
        qnet.init_weights(42);
        alignas(64) double input[RL2_Q_INPUT_DIM];
        for (size_t i = 0; i < RL2_Q_INPUT_DIM; ++i) input[i] = 0.1 * i;
        alignas(64) double output[1];

        auto r = bench<10000>([&]() {
            qnet.forward(input, output);
        });
        print_row("MLP2 forward (36→128→1, Q-net)", r);
    }

    // ── Store Transition ──
    printf("\n  Rollout Buffer:\n");
    {
        HybridRLAgent buf_agent(cfg);
        RL2Action action;
        action.raw[0] = 0.0; action.raw[1] = 1.0;
        action.raw[2] = 0.0; action.raw[3] = 1.0;

        auto r = bench<10000>([&]() {
            buf_agent.select_action(state, false);
            buf_agent.store_transition(state, action, 0.1, false);
        });
        print_row("select_action + store_transition", r);
    }

    // ── SPSC Push/Pop ──
    {
        SafeOnlineTrainer::RolloutQueue queue;
        RolloutSample sample;
        sample.reward = 1.0;

        auto r = bench<10000>([&]() {
            queue.try_push(sample);
            RolloutSample out;
            queue.try_pop(out);
        });
        print_row("SPSC push + pop (RolloutSample)", r);
    }

    // ── Rollout Buffer Push ──
    {
        RolloutBuffer buf;
        RolloutSample sample;
        sample.reward = 1.0;

        auto r = bench<10000>([&]() {
            buf.push(sample);
        });
        print_row("RolloutBuffer push (circular)", r);
    }

    // ── RL2State::build ──
    printf("\n  State Construction:\n");
    {
        Features feat{};
        feat.imbalance_1 = 0.3;
        feat.volatility = 0.02;
        Position pos{};
        pos.size = Qty(0.05);
        pos.entry_price = Price(50000.0);
        RegimeState regime{};
        regime.current = MarketRegime::Trending;
        regime.confidence = 0.8;
        FillProbability fp{};
        fp.queue_position = 0.3;
        FillProbTracker ft;
        VaRResult var{};
        var.mc_var_99 = 50.0;
        var.position_value = 2500.0;

        auto r = bench<10000>([&]() {
            RL2State::build(feat, pos, regime, fp, ft, var, 1.5, 0.7, 0.02, 0.5);
        });
        print_row("RL2State::build (32-dim)", r);
    }

    // ── Reward Compute ──
    {
        auto r = bench<100000>([&]() {
            HybridRLAgent::compute_reward(0.01, 0.02, 0.7, 150.0, 0.01, 2.0);
        });
        print_row("compute_reward", r);
    }

    // ── Soft Update ──
    printf("\n  Network Operations:\n");
    {
        CriticQ source, target;
        source.init_weights(42);
        target.init_weights(137);

        auto r = bench<1000>([&]() {
            target.soft_update_from(source, 0.005);
        });
        print_row("soft_update (36→128→1)", r);
    }

    // ── Snapshot/Restore ──
    {
        HybridRLAgent::Snapshot snap;

        auto r = bench<1000>([&]() {
            agent.snapshot(snap);
        });
        print_row("agent snapshot", r);

        auto r2 = bench<1000>([&]() {
            agent.restore(snap);
        });
        print_row("agent restore", r2);
    }

    // ── Full Update (128 samples, 2 epochs) ──
    printf("\n  Training (off hot-path):\n");
    {
        constexpr int TRAIN_RUNS = 10;
        uint64_t total_ns = 0;

        for (int run = 0; run < TRAIN_RUNS; ++run) {
            HybridRLConfig small_cfg;
            small_cfg.horizon = 128;
            small_cfg.batch_size = 64;
            small_cfg.ppo_epochs = 2;
            HybridRLAgent train_agent(small_cfg);

            for (size_t i = 0; i < 128; ++i) {
                RL2State s = make_state(static_cast<double>(i + run * 128));
                RL2Action a = train_agent.select_action(s, true);
                train_agent.store_transition(s, a, std::sin(i * 0.1), i == 127);
            }

            uint64_t t0 = TscClock::now();
            train_agent.update();
            total_ns += TscClock::elapsed_ns(t0);
        }

        double avg_ms = static_cast<double>(total_ns) / TRAIN_RUNS / 1e6;
        printf("  %-45s mean=%8.2f ms\n", "PPO+SAC update (128 samples, 2 epochs)", avg_ms);
    }

    // ── Full Update (512 samples, 4 epochs) ──
    {
        constexpr int TRAIN_RUNS = 5;
        uint64_t total_ns = 0;

        for (int run = 0; run < TRAIN_RUNS; ++run) {
            HybridRLConfig big_cfg;
            big_cfg.horizon = 512;
            big_cfg.batch_size = 128;
            big_cfg.ppo_epochs = 4;
            HybridRLAgent train_agent(big_cfg);

            for (size_t i = 0; i < 512; ++i) {
                RL2State s = make_state(static_cast<double>(i + run * 512));
                RL2Action a = train_agent.select_action(s, true);
                train_agent.store_transition(s, a, std::sin(i * 0.05), i == 511);
            }

            uint64_t t0 = TscClock::now();
            train_agent.update();
            total_ns += TscClock::elapsed_ns(t0);
        }

        double avg_ms = static_cast<double>(total_ns) / TRAIN_RUNS / 1e6;
        printf("  %-45s mean=%8.2f ms\n", "PPO+SAC update (512 samples, 4 epochs)", avg_ms);
    }

    printf("\n══════════════════════════════════════════════════════════════════════\n");

    return 0;
}
