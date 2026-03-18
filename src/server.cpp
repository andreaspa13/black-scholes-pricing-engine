// Prevent Windows.h from defining min/max macros that break STL and json.hpp
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// cpp-httplib requires Windows 10+ API surface (CreateFile2, etc.)
#define _WIN32_WINNT 0x0A00

#include "options/EuropeanOption.h"
#include "models/BlackScholesModel.h"
#include "models/MonteCarloModel.h"
#include "utils/MarketData.h"
#include "utils/PricingResult.h"
#include "utils/ImpliedVol.h"

#include "vendor/httplib.h"
#include "vendor/json.hpp"

#include <cmath>
#include <vector>
#include <set>
#include <random>
#include <algorithm>
#include <stdexcept>
#include <iostream>

using json = nlohmann::json;
using namespace options;

// ── CORS ──────────────────────────────────────────────────────────────────────
// The GUI is opened as a file:// page. Browsers treat that as a distinct origin
// and block cross-origin requests to localhost unless the server explicitly
// allows them. The three headers below satisfy the CORS preflight check.
void setCors(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin",  "*");
    res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
}

// ── GBM path generator ────────────────────────────────────────────────────────
// Generates one log-normal GBM path of (steps+1) points including S at t=0.
// This is a standalone function rather than calling MonteCarloModel::generatePath
// because that method is private (it is an implementation detail of the pricer)
// and the use-case here — a small number of display paths with an independent
// RNG — is distinct from the pricing simulation.
std::vector<double> generateVisPath(double S, double r, double sigma,
                                    double T, int steps, std::mt19937& rng) {
    std::normal_distribution<double> dist(0.0, 1.0);
    const double dt       = T / static_cast<double>(steps);
    const double drift    = r - 0.5 * sigma * sigma;  // Ito-corrected drift
    const double volSqrDt = sigma * std::sqrt(dt);

    std::vector<double> path;
    path.reserve(steps + 1);
    path.push_back(S);  // t=0 starting spot

    double spot = S;
    for (int i = 0; i < steps; ++i) {
        spot *= std::exp(drift * dt + volSqrDt * dist(rng));
        path.push_back(spot);
    }
    return path;
}

// ── Convergence series ────────────────────────────────────────────────────────
// Runs a fresh MC simulation and captures the running price estimate at 20
// log-spaced checkpoints between path 50 and maxN. This mirrors the JS
// convergenceSeries() that was previously in gui.html, now executed in C++.
struct ConvPoint { int n; double price; double se; };

std::vector<ConvPoint> buildConvergenceSeries(
        double S, double K, double r, double sigma, double T,
        OptionType type, int maxN, bool antithetic) {

    // Build log-spaced checkpoint set over [50, maxN] effective paths
    std::set<int> cpSet;
    const double logMin = std::log10(50.0);
    const double logMax = std::log10(static_cast<double>(maxN));
    for (int i = 0; i <= 20; ++i) {
        double frac = static_cast<double>(i) / 20.0;
        int cp = static_cast<int>(std::round(std::pow(10.0, logMin + (logMax - logMin) * frac)));
        cpSet.insert(std::max(1, cp));
    }
    std::vector<int> checkpoints(cpSet.begin(), cpSet.end());
    std::sort(checkpoints.begin(), checkpoints.end());

    std::mt19937 rng(42);
    std::normal_distribution<double> dist(0.0, 1.0);

    const double drift  = r - 0.5 * sigma * sigma;
    const double volSqT = sigma * std::sqrt(T);
    const double disc   = std::exp(-r * T);

    double sum = 0.0, sumSq = 0.0;
    int total = 0, cpIdx = 0;
    std::vector<ConvPoint> result;

    if (antithetic) {
        /**
         * Each iteration draws one Z, produces two terminal spots, and records
         * the pair-average payoff as one sample. The x-axis value (effectivePaths)
         * is 2*total so the convergence chart is directly comparable with plain MC
         * at the same computational cost.
         */
        const int maxPairs = maxN / 2;
        for (int i = 1; i <= maxPairs; ++i) {
            const double Z       = dist(rng);
            const double ST_pos  = S * std::exp(drift * T + volSqT * Z);
            const double ST_neg  = S * std::exp(drift * T - volSqT * Z);
            const double payoff  = 0.5 * (
                ((type == OptionType::Call) ? std::max(ST_pos - K, 0.0) : std::max(K - ST_pos, 0.0))
              + ((type == OptionType::Call) ? std::max(ST_neg - K, 0.0) : std::max(K - ST_neg, 0.0))
            );
            sum   += payoff;
            sumSq += payoff * payoff;
            ++total;

            const int effectivePaths = 2 * total;
            if (cpIdx < static_cast<int>(checkpoints.size())
                    && effectivePaths >= checkpoints[cpIdx]) {
                const double mean = sum / total;
                const double var  = (sumSq / total - mean * mean)
                                    * static_cast<double>(total)
                                    / static_cast<double>(total - 1);
                result.push_back({ effectivePaths, disc * mean,
                                    disc * std::sqrt(std::max(var, 0.0) / total) });
                ++cpIdx;
            }
        }
    } else {
        for (int i = 1; i <= maxN; ++i) {
            const double ST     = S * std::exp(drift * T + volSqT * dist(rng));
            const double payoff = (type == OptionType::Call)
                                  ? std::max(ST - K, 0.0)
                                  : std::max(K - ST, 0.0);
            sum   += payoff;
            sumSq += payoff * payoff;
            ++total;

            if (cpIdx < static_cast<int>(checkpoints.size()) && i >= checkpoints[cpIdx]) {
                const double mean = sum / total;
                const double var  = (sumSq / total - mean * mean)
                                    * static_cast<double>(total)
                                    / static_cast<double>(total - 1);
                result.push_back({ total, disc * mean,
                                    disc * std::sqrt(std::max(var, 0.0) / total) });
                ++cpIdx;
            }
        }
    }
    return result;
}

// ── main ──────────────────────────────────────────────────────────────────────
int main() {
    httplib::Server svr;

    // Handle CORS preflight — browser sends OPTIONS before the real POST
    svr.Options("/price", [](const httplib::Request&, httplib::Response& res) {
        setCors(res);
        res.status = 200;
    });

    svr.Post("/price", [](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        res.set_header("Content-Type", "application/json");

        try {
            // ── Parse request ──────────────────────────────────────────────
            const auto body = json::parse(req.body);

            const double S     = body.at("S").get<double>();
            const double K     = body.at("K").get<double>();
            const double r     = body.at("r").get<double>();
            const double sigma = body.at("sigma").get<double>();
            const double T     = body.at("T").get<double>();
            const std::string typeStr = body.at("type").get<std::string>();
            const int  numPaths     = body.value("numPaths",     10000);
            const int  numVisPaths  = body.value("numVisPaths",  50);
            const bool useAntithetic = body.value("useAntithetic", false);

            if (S <= 0 || K <= 0 || sigma <= 0 || T <= 0)
                throw std::invalid_argument("S, K, sigma, T must all be positive");
            if (typeStr != "call" && typeStr != "put")
                throw std::invalid_argument("type must be 'call' or 'put'");

            const OptionType otype = (typeStr == "call") ? OptionType::Call : OptionType::Put;
            const OptionType oOther = (typeStr == "call") ? OptionType::Put : OptionType::Call;

            // ── Construct inputs ───────────────────────────────────────────
            EuropeanOption opt(K, T, otype);
            EuropeanOption optOther(K, T, oOther);
            MarketData market{ S, r, sigma };

            // ── Price with Black-Scholes ───────────────────────────────────
            // Instantiated per-request (stateless, zero cost)
            BlackScholesModel bs;
            const PricingResult bsRes   = bs.price(opt,      market);
            const PricingResult bsOther = bs.price(optOther, market);

            // d1 and d2 are not stored in PricingResult, so re-derive them
            // here using the same formula from BlackScholesModel.cpp.
            const double sqrtT = std::sqrt(T);
            const double d1 = (std::log(S / K) + (r + 0.5 * sigma * sigma) * T)
                              / (sigma * sqrtT);
            const double d2 = d1 - sigma * sqrtT;

            // ── Price with Monte Carlo ─────────────────────────────────────
            // Fresh instance per-request: mutable RNG state is not thread-safe
            // across shared instances (documented design smell in MonteCarloModel.h).
            const VarianceReduction varRed = useAntithetic
                                             ? VarianceReduction::Antithetic
                                             : VarianceReduction::None;
            MonteCarloModel mc(numPaths, 1, 42, varRed);
            const PricingResult mcRes      = mc.price(opt,      market);
            const PricingResult mcOtherRes = mc.price(optOther, market);

            // ── Convergence series (both types) ────────────────────────────
            const auto conv      = buildConvergenceSeries(S, K, r, sigma, T, otype,  numPaths, useAntithetic);
            const auto convOther = buildConvergenceSeries(S, K, r, sigma, T, oOther, numPaths, useAntithetic);

            // ── Visual GBM paths ───────────────────────────────────────────
            std::mt19937 visRng(99);  // different seed from MC so paths look varied
            json pathsJson = json::array();
            for (int i = 0; i < std::min(numVisPaths, 150); ++i) {
                const auto p = generateVisPath(S, r, sigma, T, 100, visRng);
                pathsJson.push_back(p);
            }

            // ── Assemble JSON response ─────────────────────────────────────
            json convJson = json::array();
            for (const auto& cp : conv)
                convJson.push_back({ {"n", cp.n}, {"price", cp.price}, {"se", cp.se} });

            json convOtherJson = json::array();
            for (const auto& cp : convOther)
                convOtherJson.push_back({ {"n", cp.n}, {"price", cp.price}, {"se", cp.se} });

            const json response = {
                {"bs", {
                    {"price",  bsRes.price},
                    {"delta",  bsRes.delta},
                    {"gamma",  bsRes.gamma},
                    {"vega",   bsRes.vega},
                    {"theta",  bsRes.theta},
                    {"rho",    bsRes.rho},
                    {"stdErr", bsRes.stdErr},
                    {"d1",     d1},
                    {"d2",     d2}
                }},
                {"bsOther", {
                    {"price",  bsOther.price},
                    {"delta",  bsOther.delta},
                    {"gamma",  bsOther.gamma},
                    {"vega",   bsOther.vega},
                    {"theta",  bsOther.theta},
                    {"rho",    bsOther.rho},
                    {"stdErr", bsOther.stdErr},
                    {"d1",     d1},   // d1/d2 are symmetric for call/put
                    {"d2",     d2}
                }},
                {"mc", {
                    {"price",       mcRes.price},
                    {"stdErr",      mcRes.stdErr},
                    {"modelName",   mcRes.modelName}
                }},
                {"mcOther", {
                    {"price",       mcOtherRes.price},
                    {"stdErr",      mcOtherRes.stdErr},
                    {"modelName",   mcOtherRes.modelName}
                }},
                {"convergence",      convJson},
                {"convergenceOther", convOtherJson},
                {"paths",            pathsJson},
                {"varReduction", useAntithetic ? "antithetic" : "none"}
            };

            res.set_content(response.dump(), "application/json");

        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // ── /iv: implied volatility from market price ──────────────────────────────
    svr.Options("/iv", [](const httplib::Request&, httplib::Response& res) {
        setCors(res);
        res.status = 200;
    });

    svr.Post("/iv", [](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        res.set_header("Content-Type", "application/json");
        try {
            const auto body = json::parse(req.body);

            const double S           = body.at("S").get<double>();
            const double K           = body.at("K").get<double>();
            const double r           = body.at("r").get<double>();
            const double T           = body.at("T").get<double>();
            const double marketPrice = body.at("marketPrice").get<double>();
            const std::string typeStr = body.at("type").get<std::string>();

            if (typeStr != "call" && typeStr != "put")
                throw std::invalid_argument("type must be 'call' or 'put'");

            const OptionType otype = (typeStr == "call") ? OptionType::Call : OptionType::Put;
            const IVResult   ivRes = solveIV(marketPrice, otype, S, K, r, T);

            res.set_content(json({
                {"iv",        ivRes.impliedVol},
                {"converged", ivRes.converged},
                {"message",   ivRes.message}
            }).dump(), "application/json");

        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    const int PORT = 18080;
    std::cout << "Options pricing server running on http://localhost:" << PORT << "\n";
    std::cout << "Open gui.html in a browser, then use Run Pricing.\n";
    std::cout << "Press Ctrl+C to stop.\n\n";

    if (!svr.listen("localhost", PORT)) {
        std::cerr << "Error: could not bind to port " << PORT
                  << ". Is another process using it?\n";
        return 1;
    }
    return 0;
}
