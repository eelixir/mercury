#pragma once

#include <cstdint>
#include <random>
#include <string>

namespace Mercury {

    enum class SimulationVolatilityPreset;

    enum class MarketRegime {
        Calm,
        Normal,
        Stressed
    };

    inline const char* marketRegimeToString(MarketRegime regime) {
        switch (regime) {
            case MarketRegime::Calm:     return "calm";
            case MarketRegime::Stressed: return "stressed";
            case MarketRegime::Normal:
            default:                     return "normal";
        }
    }

    inline MarketRegime marketRegimeFromString(const std::string& value) {
        if (value == "calm")     return MarketRegime::Calm;
        if (value == "stressed") return MarketRegime::Stressed;
        return MarketRegime::Normal;
    }

    // Arrival intensities expressed in expected events per millisecond.
    struct ArrivalIntensity {
        double limitLambda = 0.0;
        double cancelLambda = 0.0;
        double marketableLambda = 0.0;
    };

    // Pareto (power-law) order-size dispersion: occasional large "whale"
    // prints superimposed on frequent small "retail" noise.
    struct OrderSizeDispersion {
        double minSize = 1.0;   // scale / x_m for the Pareto tail
        double alpha   = 2.2;   // tail exponent (smaller = fatter tail)
        double maxSize = 500.0; // hard cap to keep the simulation bounded
    };

    class RegimeManager {
    public:
        explicit RegimeManager(SimulationVolatilityPreset preset);

        void setPreset(SimulationVolatilityPreset preset);
        SimulationVolatilityPreset preset() const { return preset_; }

        // Force a regime. Clears the dwell counter and suspends auto-detection
        // for a short hold-down so explicit overrides remain visible.
        void forceRegime(MarketRegime regime);

        // Update the detected regime from observed environment signals.
        // `stepMs` is the duration of the current simulation tick.
        void observe(double realizedVolatilityBps,
                     bool   momentumBurst,
                     double averageSpread,
                     uint64_t stepMs);

        MarketRegime regime() const { return regime_; }

        // Combined base-preset x regime-multiplier arrival intensities.
        ArrivalIntensity intensity() const;
        OrderSizeDispersion dispersion() const;

        // Sample a Pareto-distributed order size, clamped to integer units.
        uint64_t sampleOrderSize(std::mt19937& rng) const;

        // Stateless variants so agents can sample directly from a
        // dispersion struct carried on a SimulationObservation.
        static uint64_t sampleOrderSize(const OrderSizeDispersion& dispersion, std::mt19937& rng);

        // Sample how many Poisson events fire across `stepMs` at `lambda` (events/ms).
        static uint32_t samplePoissonCount(double lambda, uint64_t stepMs, std::mt19937& rng);

    private:
        SimulationVolatilityPreset preset_;
        MarketRegime regime_;
        uint64_t dwellMsInRegime_ = 0;
        uint64_t holdDownMs_ = 0;
    };

}
