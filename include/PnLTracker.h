#pragma once

#include "Order.h"
#include <string>
#include <fstream>
#include <cstdint>
#include <functional>
#include <atomic>
#include <unordered_map>
#include <vector>

namespace Mercury {

    /**
     * Position Entry - tracks individual position entries for accurate P&L calculation
     * Uses FIFO (First-In-First-Out) method for matching buys with sells
     */
    struct PositionEntry {
        uint64_t quantity = 0;
        int64_t price = 0;
        uint64_t tradeId = 0;
        uint64_t timestamp = 0;
    };

    /**
     * Client P&L Record - complete P&L state for a client
     */
    struct ClientPnL {
        uint64_t clientId = 0;
        
        // Position tracking
        int64_t longQuantity = 0;           // Total long quantity held
        int64_t shortQuantity = 0;          // Total short quantity held
        int64_t netPosition = 0;            // Net position (long - short)
        
        // Cost basis tracking
        int64_t totalBuyCost = 0;           // Total cost of all buys
        int64_t totalSellProceeds = 0;      // Total proceeds from all sells
        int64_t longCostBasis = 0;          // Cost basis of current long position
        int64_t shortCostBasis = 0;         // Cost basis of current short position
        
        // P&L
        int64_t realizedPnL = 0;            // Realized P&L from closed positions
        int64_t unrealizedPnL = 0;          // Unrealized P&L on open positions
        int64_t totalPnL = 0;               // Total P&L (realized + unrealized)
        
        // Trade statistics
        uint64_t totalTrades = 0;           // Total number of trades
        uint64_t totalBuyQuantity = 0;      // Total quantity bought
        uint64_t totalSellQuantity = 0;     // Total quantity sold
        uint64_t winningTrades = 0;         // Number of profitable closing trades
        uint64_t losingTrades = 0;          // Number of losing closing trades
        int64_t avgBuyPrice = 0;            // Average buy price
        int64_t avgSellPrice = 0;           // Average sell price
        
        // Volume-weighted average price of position
        int64_t vwapPosition = 0;           // VWAP of current position
        
        // For FIFO P&L calculation
        std::vector<PositionEntry> openLongEntries;   // Open long positions (FIFO queue)
        std::vector<PositionEntry> openShortEntries;  // Open short positions (FIFO queue)
        
        // Update calculated fields
        void updateCalculatedFields() {
            netPosition = longQuantity - shortQuantity;
            totalPnL = realizedPnL + unrealizedPnL;
            
            if (totalBuyQuantity > 0) {
                avgBuyPrice = totalBuyCost / static_cast<int64_t>(totalBuyQuantity);
            } else {
                avgBuyPrice = 0;
            }
            if (totalSellQuantity > 0) {
                avgSellPrice = totalSellProceeds / static_cast<int64_t>(totalSellQuantity);
            } else {
                avgSellPrice = 0;
            }
            
            // Calculate VWAP of current position
            if (longQuantity > 0 && longCostBasis != 0) {
                vwapPosition = longCostBasis / longQuantity;
            } else if (shortQuantity > 0 && shortCostBasis != 0) {
                vwapPosition = shortCostBasis / shortQuantity;
            } else {
                vwapPosition = 0;
            }
        }
    };

    /**
     * P&L Snapshot - point-in-time P&L record for CSV output
     */
    struct PnLSnapshot {
        uint64_t snapshotId = 0;
        uint64_t timestamp = 0;
        uint64_t clientId = 0;
        int64_t netPosition = 0;
        int64_t longQuantity = 0;
        int64_t shortQuantity = 0;
        int64_t realizedPnL = 0;
        int64_t unrealizedPnL = 0;
        int64_t totalPnL = 0;
        int64_t markPrice = 0;              // Price used for unrealized P&L
        int64_t costBasis = 0;              // Cost basis of current position
        int64_t avgEntryPrice = 0;          // Average entry price
        uint64_t tradeId = 0;               // Trade that triggered this snapshot
    };

    /**
     * PnLTracker - Position and Profit/Loss tracking system
     * 
     * Features:
     * - Per-client position tracking (long/short quantities)
     * - FIFO-based realized P&L calculation
     * - Mark-to-market unrealized P&L calculation
     * - CSV output for P&L snapshots
     * 
     * Thread Safety:
     * - This class is NOT thread-safe. External synchronization is required
     *   when accessing from multiple threads. Use a mutex to protect all
     *   method calls when used in concurrent environments.
     * 
     * Usage:
     *   PnLTracker tracker("pnl.csv");
     *   tracker.open();
     *   
     *   // After each trade:
     *   tracker.onTradeExecuted(trade, buyClientId, sellClientId, markPrice);
     *   
     *   // Or update mark-to-market unrealized P&L:
     *   tracker.updateMarkToMarket(clientId, currentPrice);
     */
    class PnLTracker {
    public:
        using PnLCallback = std::function<void(const PnLSnapshot&)>;

        PnLTracker();
        explicit PnLTracker(const std::string& outputPath);
        ~PnLTracker();

        // Prevent copying
        PnLTracker(const PnLTracker&) = delete;
        PnLTracker& operator=(const PnLTracker&) = delete;

        /**
         * Open the output file for writing
         * @return true if successful
         */
        bool open();

        /**
         * Close the output file
         */
        void close();

        /**
         * Check if output file is open
         */
        bool isOpen() const { return file_.is_open(); }

        /**
         * Process a trade execution and update P&L
         * @param trade The executed trade
         * @param buyClientId Client ID of the buyer (0 if not tracked)
         * @param sellClientId Client ID of the seller (0 if not tracked)
         * @param markPrice Current market price for unrealized P&L (use trade price if 0)
         */
        void onTradeExecuted(const Trade& trade, uint64_t buyClientId, 
                            uint64_t sellClientId, int64_t markPrice = 0);

        /**
         * Update unrealized P&L for a client based on current market price
         * @param clientId The client ID
         * @param markPrice Current market price
         * @return Updated unrealized P&L
         */
        int64_t updateMarkToMarket(uint64_t clientId, int64_t markPrice);

        /**
         * Update unrealized P&L for all clients
         * @param markPrice Current market price
         */
        void updateAllMarkToMarket(int64_t markPrice);

        /**
         * Get P&L record for a client
         * @param clientId The client ID
         * @return Client P&L record
         */
        ClientPnL getClientPnL(uint64_t clientId) const;

        /**
         * Get all client P&L records
         * @return Map of client ID to P&L record
         */
        const std::unordered_map<uint64_t, ClientPnL>& getAllClientPnL() const { 
            return clientPnL_; 
        }

        /**
         * Register callback for P&L snapshots
         */
        void setPnLCallback(PnLCallback callback) { pnlCallback_ = std::move(callback); }

        /**
         * Reset all P&L tracking
         */
        void reset();

        /**
         * Write a P&L snapshot to file
         * @param snapshot The snapshot to write
         * @return true if successful
         */
        bool writeSnapshot(const PnLSnapshot& snapshot);

        /**
         * Write current state for all clients
         * @param markPrice Current market price for unrealized P&L
         */
        void writeAllSnapshots(int64_t markPrice);

        /**
         * Flush output to disk
         */
        void flush();

        /**
         * Get file path
         */
        const std::string& getFilePath() const { return filepath_; }

        /**
         * Get number of snapshots written
         */
        size_t getSnapshotCount() const { return snapshotsWritten_; }

        /**
         * Get number of clients tracked
         */
        size_t getClientCount() const { return clientPnL_.size(); }

        /**
         * Get timestamp
         */
        uint64_t getTimestamp() { return ++currentTimestamp_; }

        /**
         * Set the last traded price (used as default mark price)
         */
        void setLastTradedPrice(int64_t price) { lastTradedPrice_ = price; }

        /**
         * Get the last traded price
         */
        int64_t getLastTradedPrice() const { return lastTradedPrice_; }

    private:
        std::string filepath_;
        std::ofstream file_;
        std::unordered_map<uint64_t, ClientPnL> clientPnL_;
        
        std::atomic<uint64_t> snapshotIdCounter_{0};
        std::atomic<uint64_t> currentTimestamp_{0};
        
        size_t snapshotsWritten_ = 0;
        int64_t lastTradedPrice_ = 0;
        
        PnLCallback pnlCallback_;

        /**
         * Get or create client P&L record
         */
        ClientPnL& getOrCreateClientPnL(uint64_t clientId);

        /**
         * Calculate realized P&L from closing a position (FIFO)
         * @param pnl Client P&L record
         * @param side Side of the closing trade (Buy closes short, Sell closes long)
         * @param quantity Trade quantity (will be reduced by closed amount)
         * @param price Trade price
         * @return Realized P&L from this trade
         */
        int64_t calculateRealizedPnL(ClientPnL& pnl, Side side, 
                                     uint64_t& quantity, int64_t price);

        /**
         * Calculate unrealized P&L for a position
         * @param pnl Client P&L record
         * @param markPrice Current market price
         * @return Unrealized P&L
         */
        int64_t calculateUnrealizedPnL(const ClientPnL& pnl, int64_t markPrice) const;

        /**
         * Generate snapshot ID
         */
        uint64_t generateSnapshotId() { return ++snapshotIdCounter_; }

        /**
         * Write CSV header
         */
        void writeHeader();

        /**
         * Notify callback about P&L update
         */
        void notifyPnLUpdate(const PnLSnapshot& snapshot);

        /**
         * Create a P&L snapshot from current state
         */
        PnLSnapshot createSnapshot(uint64_t clientId, int64_t markPrice, uint64_t tradeId = 0);
    };

    /**
     * PnLWriter - Simple CSV writer for P&L snapshots
     * 
     * Output CSV format (with header):
     * snapshot_id,timestamp,client_id,net_position,long_qty,short_qty,realized_pnl,unrealized_pnl,total_pnl,mark_price,cost_basis,avg_entry_price,trade_id
     */
    class PnLWriter {
    public:
        explicit PnLWriter(const std::string& filepath);
        ~PnLWriter();

        // Prevent copying
        PnLWriter(const PnLWriter&) = delete;
        PnLWriter& operator=(const PnLWriter&) = delete;

        bool open();
        void close();
        bool isOpen() const { return file_.is_open(); }
        
        bool writeSnapshot(const PnLSnapshot& snapshot);
        size_t writeSnapshots(const std::vector<PnLSnapshot>& snapshots);
        
        void flush();
        size_t getSnapshotCount() const { return snapshotsWritten_; }
        const std::string& getFilePath() const { return filepath_; }

    private:
        std::string filepath_;
        std::ofstream file_;
        size_t snapshotsWritten_ = 0;

        void writeHeader();
    };

}
