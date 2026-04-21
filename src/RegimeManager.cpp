#include "RegimeManager.h"
#include "MarketRuntime.h"

#include <algorithm>
#include <cmath>

namespace Mercury {

    namespace {

        // Base arrival intensities (events per ms) for the Normal regime,
        // chosen so each symbol produces a few dozen resting orders and a
        // handful of crossings per second under the "Normal" preset.
        struct PresetIntensity {
            ArrivalIntensity normal;
            OrderSizeDispersion sizes;
        };

        PresetIntensity presetDefaults(SimulationVolatilityPreset preset) {
            switch (preset) {
                case SimulationVolatilityPreset::Low:
                    return PresetIntensity{
                        ArrivalIntensity{0.020, 0.010, 0.004},
                        OrderSizeDispersion{3.0, 2.6, 350.0}
                    };
                case SimulationVolatilityPreset::High:
                    return PresetIntensity{
                        ArrivalIntensity{0.060, 0.040, 0.030},
                        OrderSizeDispersion{2.0, 1.7, 800.0}
                    };
                case SimulationVolatilityPreset::Normal:
                default:
                    return PresetIntensity{
                        ArrivalIntensity{0.040, 0.020, 0.012},
                        OrderSizeDispersion{2.0, 2.0, 500.0}
                    };
            }
        }

        // Per-spec: Stressed halves limit arrivals, doubles cancels and
        // marketable flow. Calm is the mirror (slower cancels/marketable,
        // slightly faster passive posting).
        struct RegimeScale {
            double limit;
            double cancel;
            double marketable;
            double sizeAlphaBias; // additive shift to Pareto alpha
        };

        RegimeScale regimeScale(MarketRegime regime) {
            switch (regime) {
                case MarketRegime::Calm:
                    return RegimeScale{1.1, 0.6, 0.4, 0.4};   // thinner tail, sparser crossings
                case MarketRegime::Stressed:
                    return RegimeScale{0.5, 2.0, 2.0, -0.4};  // fatter whales, frantic activity
                case MarketRegime::Normal:
                default:
                    return RegimeScale{1.0, 1.0, 1.0, 0.0};
            }
        }

        double stressedVolThreshold(SimulationVolatilityPreset preset) {
            switch (preset) {
                case SimulationVolatilityPreset::Low:    return 35.0;
                case SimulationVolatilityPreset::High:   return 120.0;
                case SimulationVolatilityPreset::Normal:
                default:                                 return 70.0;
            }
        }

        double calmVolThreshold(SimulationVolatilityPreset preset) {
            switch (preset) {
                case SimulationVolatilityPreset::Low:    return 6.0;
                case SimulationVolatilityPreset::High:   return 20.0;
                case SimulationVolatilityPreset::Normal:
                default:                                 return 12.0;
            }
        }

    }

    RegimeManager::RegimeManager(SimulationVolatilityPreset preset)
        : preset_(preset), regime_(MarketRegime::Normal) {}

    void RegimeManager::setPreset(SimulationVolatilityPreset preset) {
        preset_ = preset;
        dwellMsInRegime_ = 0;
    }

    void RegimeManager::forceRegime(MarketRegime regime) {
        regime_ = regime;
        dwellMsInRegime_ = 0;
        // Hold the forced regime for at least two seconds before auto-switching.
        holdDownMs_ = 2000;
    }

    void RegimeManager::observe(double realizedVolatilityBps,
                                bool momentumBurst,
                                double /*averageSpread*/,
                                uint64_t stepMs) {
        dwellMsInRegime_ += stepMs;
        if (holdDownMs_ > 0) {
            holdDownMs_ = holdDownMs_ > stepMs ? holdDownMs_ - stepMs : 0;
            return;
        }

        const double stressThresh = stressedVolThreshold(preset_);
        const double calmThresh   = calmVolThreshold(preset_);

        MarketRegime target = regime_;
        if (momentumBurst || realizedVolatilityBps >= stressThresh) {
            target = MarketRegime::Stressed;
        } else if (realizedVolatilityBps <= calmThresh) {
            target = MarketRegime::Calm;
        } else {
            target = MarketRegime::Normal;
        }

        // Small hysteresis: require the observed state to persist before switching.
        constexpr uint64_t minDwellMs = 400;
        if (target != regime_ && dwellMsInRegime_ >= minDwellMs) {
            regime_ = target;
            dwellMsInRegime_ = 0;
        }
    }

    ArrivalIntensity RegimeManager::intensity() const {
        const auto base = presetDefaults(preset_).normal;
        const auto scale = regimeScale(regime_);
        return ArrivalIntensity{
            base.limitLambda      * scale.limit,
            base.cancelLambda     * scale.cancel,
            base.marketableLambda * scale.marketable
        };
    }

    OrderSizeDispersion RegimeManager::dispersion() const {
        auto sizes = presetDefaults(preset_).sizes;
        sizes.alpha = std::max(1.05, sizes.alpha + regimeScale(regime_).sizeAlphaBias);
        return sizes;
    }

    uint64_t RegimeManager::sampleOrderSize(std::mt19937& rng) const {
        return sampleOrderSize(dispersion(), rng);
    }

    uint64_t RegimeManager::sampleOrderSize(const OrderSizeDispersion& sizes, std::mt19937& rng) {
        // Inverse-CDF sample from a Pareto distribution:
        //   X = x_m * (1 - U)^(-1/alpha)
        std::uniform_real_distribution<double> u(0.0, 1.0);
        double uniform = u(rng);
        uniform = std::clamp(uniform, 1e-9, 1.0 - 1e-9);
        const double alpha = std::max(1.05, sizes.alpha);
        const double raw = sizes.minSize * std::pow(1.0 - uniform, -1.0 / alpha);
        const double clamped = std::min(raw, sizes.maxSize);
        return static_cast<uint64_t>(std::max(1.0, std::floor(clamped)));
    }

    uint32_t RegimeManager::samplePoissonCount(double lambda, uint64_t stepMs, std::mt19937& rng) {
        if (lambda <= 0.0 || stepMs == 0) {
            return 0;
        }
        const double mean = lambda * static_cast<double>(stepMs);
        // std::poisson_distribution is fine here — rates stay modest (<~30 events/tick).
        std::poisson_distribution<uint32_t> dist(mean);
        return dist(rng);
    }

}
