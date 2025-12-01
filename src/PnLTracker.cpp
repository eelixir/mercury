#include "PnLTracker.h"
#include <iostream>
#include <iomanip>
#include <algorithm>

namespace Mercury {

    // ==================== PnLTracker ====================

    PnLTracker::PnLTracker() : filepath_("pnl.csv") {
    }

    PnLTracker::PnLTracker(const std::string& outputPath) : filepath_(outputPath) {
    }

    PnLTracker::~PnLTracker() {
        close();
    }

    bool PnLTracker::open() {
        if (file_.is_open()) {
            return true;
        }

        file_.open(filepath_, std::ios::out | std::ios::trunc);
        if (!file_.is_open()) {
            std::cerr << "PnLTracker: Could not open file: " << filepath_ << "\n";
            return false;
        }

        writeHeader();
        return true;
    }

    void PnLTracker::close() {
        if (file_.is_open()) {
            file_.flush();
            file_.close();
        }
    }

    void PnLTracker::writeHeader() {
        file_ << "snapshot_id,timestamp,client_id,net_position,long_qty,short_qty,"
              << "realized_pnl,unrealized_pnl,total_pnl,mark_price,cost_basis,"
              << "avg_entry_price,trade_id\n";
    }

    ClientPnL& PnLTracker::getOrCreateClientPnL(uint64_t clientId) {
        auto it = clientPnL_.find(clientId);
        if (it == clientPnL_.end()) {
            ClientPnL newPnL;
            newPnL.clientId = clientId;
            auto result = clientPnL_.emplace(clientId, newPnL);
            return result.first->second;
        }
        return it->second;
    }

    ClientPnL PnLTracker::getClientPnL(uint64_t clientId) const {
        auto it = clientPnL_.find(clientId);
        if (it != clientPnL_.end()) {
            return it->second;
        }
        ClientPnL empty;
        empty.clientId = clientId;
        return empty;
    }

    void PnLTracker::onTradeExecuted(const Trade& trade, uint64_t buyClientId, 
                                      uint64_t sellClientId, int64_t markPrice) {
        // Use trade price as mark price if not specified
        if (markPrice <= 0) {
            markPrice = trade.price;
        }
        
        // Update last traded price
        lastTradedPrice_ = trade.price;

        // Process buyer's position (buy = increase long or decrease short)
        if (buyClientId > 0) {
            ClientPnL& buyerPnL = getOrCreateClientPnL(buyClientId);
            
            int64_t tradeValue = trade.price * static_cast<int64_t>(trade.quantity);
            buyerPnL.totalBuyCost += tradeValue;
            buyerPnL.totalBuyQuantity += trade.quantity;
            buyerPnL.totalTrades++;
            
            // Check if this trade closes existing short positions (FIFO)
            uint64_t remainingQty = trade.quantity;
            
            // Close short positions first (buying to cover)
            if (buyerPnL.shortQuantity > 0 && !buyerPnL.openShortEntries.empty()) {
                int64_t realized = calculateRealizedPnL(buyerPnL, Side::Buy, 
                                                         remainingQty, trade.price);
                buyerPnL.realizedPnL += realized;
            }
            
            // Any remaining quantity increases long position
            if (remainingQty > 0) {
                buyerPnL.longQuantity += remainingQty;
                buyerPnL.longCostBasis += trade.price * static_cast<int64_t>(remainingQty);
                
                // Add to open entries for FIFO tracking
                PositionEntry entry;
                entry.quantity = remainingQty;
                entry.price = trade.price;
                entry.tradeId = trade.tradeId;
                entry.timestamp = trade.timestamp;
                buyerPnL.openLongEntries.push_back(entry);
            }
            
            // Update unrealized P&L
            buyerPnL.unrealizedPnL = calculateUnrealizedPnL(buyerPnL, markPrice);
            buyerPnL.updateCalculatedFields();
            
            // Write snapshot
            PnLSnapshot buyerSnapshot = createSnapshot(buyClientId, markPrice, trade.tradeId);
            writeSnapshot(buyerSnapshot);
            notifyPnLUpdate(buyerSnapshot);
        }

        // Process seller's position (sell = increase short or decrease long)
        if (sellClientId > 0) {
            ClientPnL& sellerPnL = getOrCreateClientPnL(sellClientId);
            
            int64_t tradeValue = trade.price * static_cast<int64_t>(trade.quantity);
            sellerPnL.totalSellProceeds += tradeValue;
            sellerPnL.totalSellQuantity += trade.quantity;
            sellerPnL.totalTrades++;
            
            // Check if this trade closes existing long positions (FIFO)
            uint64_t remainingQty = trade.quantity;
            
            // Close long positions first (selling to close)
            if (sellerPnL.longQuantity > 0 && !sellerPnL.openLongEntries.empty()) {
                int64_t realized = calculateRealizedPnL(sellerPnL, Side::Sell, 
                                                         remainingQty, trade.price);
                sellerPnL.realizedPnL += realized;
            }
            
            // Any remaining quantity increases short position
            if (remainingQty > 0) {
                sellerPnL.shortQuantity += remainingQty;
                sellerPnL.shortCostBasis += trade.price * static_cast<int64_t>(remainingQty);
                
                // Add to open entries for FIFO tracking
                PositionEntry entry;
                entry.quantity = remainingQty;
                entry.price = trade.price;
                entry.tradeId = trade.tradeId;
                entry.timestamp = trade.timestamp;
                sellerPnL.openShortEntries.push_back(entry);
            }
            
            // Update unrealized P&L
            sellerPnL.unrealizedPnL = calculateUnrealizedPnL(sellerPnL, markPrice);
            sellerPnL.updateCalculatedFields();
            
            // Write snapshot
            PnLSnapshot sellerSnapshot = createSnapshot(sellClientId, markPrice, trade.tradeId);
            writeSnapshot(sellerSnapshot);
            notifyPnLUpdate(sellerSnapshot);
        }
    }

    int64_t PnLTracker::calculateRealizedPnL(ClientPnL& pnl, Side side, 
                                              uint64_t& quantity, int64_t price) {
        int64_t realizedPnL = 0;

        if (side == Side::Buy) {
            // Buying closes short positions (FIFO)
            while (quantity > 0 && !pnl.openShortEntries.empty()) {
                PositionEntry& entry = pnl.openShortEntries.front();
                
                uint64_t closeQty = std::min(quantity, entry.quantity);
                
                // Short P&L: (sell price - buy price) * quantity
                // For shorts: entry.price was the sell price, current price is buy to cover
                int64_t pnlPerUnit = entry.price - price;
                int64_t closingPnL = pnlPerUnit * static_cast<int64_t>(closeQty);
                realizedPnL += closingPnL;
                
                // Track winning/losing trades (each closed entry counts as a trade)
                if (closingPnL > 0) {
                    pnl.winningTrades++;
                } else if (closingPnL < 0) {
                    pnl.losingTrades++;
                }
                
                // Update short position tracking
                pnl.shortQuantity -= closeQty;
                pnl.shortCostBasis -= entry.price * static_cast<int64_t>(closeQty);
                
                entry.quantity -= closeQty;
                quantity -= closeQty;
                
                // Remove entry if fully closed
                if (entry.quantity == 0) {
                    pnl.openShortEntries.erase(pnl.openShortEntries.begin());
                }
            }
        } else {
            // Selling closes long positions (FIFO)
            while (quantity > 0 && !pnl.openLongEntries.empty()) {
                PositionEntry& entry = pnl.openLongEntries.front();
                
                uint64_t closeQty = std::min(quantity, entry.quantity);
                
                // Long P&L: (sell price - buy price) * quantity
                // For longs: entry.price was the buy price, current price is sell price
                int64_t pnlPerUnit = price - entry.price;
                int64_t closingPnL = pnlPerUnit * static_cast<int64_t>(closeQty);
                realizedPnL += closingPnL;
                
                // Track winning/losing trades (each closed entry counts as a trade)
                if (closingPnL > 0) {
                    pnl.winningTrades++;
                } else if (closingPnL < 0) {
                    pnl.losingTrades++;
                }
                
                // Update long position tracking
                pnl.longQuantity -= closeQty;
                pnl.longCostBasis -= entry.price * static_cast<int64_t>(closeQty);
                
                entry.quantity -= closeQty;
                quantity -= closeQty;
                
                // Remove entry if fully closed
                if (entry.quantity == 0) {
                    pnl.openLongEntries.erase(pnl.openLongEntries.begin());
                }
            }
        }

        return realizedPnL;
    }

    int64_t PnLTracker::calculateUnrealizedPnL(const ClientPnL& pnl, int64_t markPrice) const {
        int64_t unrealizedPnL = 0;

        // Unrealized P&L on long positions: (mark price - avg buy price) * quantity
        if (pnl.longQuantity > 0 && pnl.longCostBasis > 0) {
            int64_t longValue = markPrice * pnl.longQuantity;
            unrealizedPnL += longValue - pnl.longCostBasis;
        }

        // Unrealized P&L on short positions: (avg sell price - mark price) * quantity
        if (pnl.shortQuantity > 0 && pnl.shortCostBasis > 0) {
            int64_t shortLiability = markPrice * pnl.shortQuantity;
            unrealizedPnL += pnl.shortCostBasis - shortLiability;
        }

        return unrealizedPnL;
    }

    int64_t PnLTracker::updateMarkToMarket(uint64_t clientId, int64_t markPrice) {
        auto it = clientPnL_.find(clientId);
        if (it == clientPnL_.end()) {
            return 0;
        }

        ClientPnL& pnl = it->second;
        pnl.unrealizedPnL = calculateUnrealizedPnL(pnl, markPrice);
        pnl.updateCalculatedFields();
        
        return pnl.unrealizedPnL;
    }

    void PnLTracker::updateAllMarkToMarket(int64_t markPrice) {
        for (auto& [clientId, pnl] : clientPnL_) {
            pnl.unrealizedPnL = calculateUnrealizedPnL(pnl, markPrice);
            pnl.updateCalculatedFields();
        }
    }

    PnLSnapshot PnLTracker::createSnapshot(uint64_t clientId, int64_t markPrice, uint64_t tradeId) {
        const ClientPnL& pnl = getClientPnL(clientId);
        
        PnLSnapshot snapshot;
        snapshot.snapshotId = generateSnapshotId();
        snapshot.timestamp = getTimestamp();
        snapshot.clientId = clientId;
        snapshot.netPosition = pnl.netPosition;
        snapshot.longQuantity = pnl.longQuantity;
        snapshot.shortQuantity = pnl.shortQuantity;
        snapshot.realizedPnL = pnl.realizedPnL;
        snapshot.unrealizedPnL = pnl.unrealizedPnL;
        snapshot.totalPnL = pnl.totalPnL;
        snapshot.markPrice = markPrice;
        snapshot.costBasis = pnl.longCostBasis - pnl.shortCostBasis;
        snapshot.avgEntryPrice = pnl.vwapPosition;
        snapshot.tradeId = tradeId;
        
        return snapshot;
    }

    bool PnLTracker::writeSnapshot(const PnLSnapshot& snapshot) {
        if (!file_.is_open()) {
            return false;
        }

        file_ << snapshot.snapshotId << ","
              << snapshot.timestamp << ","
              << snapshot.clientId << ","
              << snapshot.netPosition << ","
              << snapshot.longQuantity << ","
              << snapshot.shortQuantity << ","
              << snapshot.realizedPnL << ","
              << snapshot.unrealizedPnL << ","
              << snapshot.totalPnL << ","
              << snapshot.markPrice << ","
              << snapshot.costBasis << ","
              << snapshot.avgEntryPrice << ","
              << snapshot.tradeId << "\n";

        if (file_.fail()) {
            std::cerr << "PnLTracker: Write failed for snapshot " << snapshot.snapshotId << "\n";
            return false;
        }

        snapshotsWritten_++;
        return true;
    }

    void PnLTracker::writeAllSnapshots(int64_t markPrice) {
        updateAllMarkToMarket(markPrice);
        
        for (const auto& [clientId, pnl] : clientPnL_) {
            PnLSnapshot snapshot = createSnapshot(clientId, markPrice, 0);
            writeSnapshot(snapshot);
        }
    }

    void PnLTracker::flush() {
        if (file_.is_open()) {
            file_.flush();
        }
    }

    void PnLTracker::reset() {
        clientPnL_.clear();
        snapshotsWritten_ = 0;
        lastTradedPrice_ = 0;
        snapshotIdCounter_ = 0;
    }

    void PnLTracker::notifyPnLUpdate(const PnLSnapshot& snapshot) {
        if (pnlCallback_) {
            pnlCallback_(snapshot);
        }
    }

    // ==================== PnLWriter ====================

    PnLWriter::PnLWriter(const std::string& filepath)
        : filepath_(filepath), snapshotsWritten_(0) {
    }

    PnLWriter::~PnLWriter() {
        close();
    }

    bool PnLWriter::open() {
        if (file_.is_open()) {
            return true;
        }

        file_.open(filepath_, std::ios::out | std::ios::trunc);
        if (!file_.is_open()) {
            std::cerr << "PnLWriter: Could not open file: " << filepath_ << "\n";
            return false;
        }

        writeHeader();
        return true;
    }

    void PnLWriter::close() {
        if (file_.is_open()) {
            file_.flush();
            file_.close();
        }
    }

    void PnLWriter::writeHeader() {
        file_ << "snapshot_id,timestamp,client_id,net_position,long_qty,short_qty,"
              << "realized_pnl,unrealized_pnl,total_pnl,mark_price,cost_basis,"
              << "avg_entry_price,trade_id\n";
    }

    bool PnLWriter::writeSnapshot(const PnLSnapshot& snapshot) {
        if (!file_.is_open()) {
            std::cerr << "PnLWriter: File not open\n";
            return false;
        }

        file_ << snapshot.snapshotId << ","
              << snapshot.timestamp << ","
              << snapshot.clientId << ","
              << snapshot.netPosition << ","
              << snapshot.longQuantity << ","
              << snapshot.shortQuantity << ","
              << snapshot.realizedPnL << ","
              << snapshot.unrealizedPnL << ","
              << snapshot.totalPnL << ","
              << snapshot.markPrice << ","
              << snapshot.costBasis << ","
              << snapshot.avgEntryPrice << ","
              << snapshot.tradeId << "\n";

        if (file_.fail()) {
            std::cerr << "PnLWriter: Write failed for snapshot " << snapshot.snapshotId << "\n";
            return false;
        }

        snapshotsWritten_++;
        return true;
    }

    size_t PnLWriter::writeSnapshots(const std::vector<PnLSnapshot>& snapshots) {
        size_t written = 0;
        for (const auto& snapshot : snapshots) {
            if (writeSnapshot(snapshot)) {
                written++;
            }
        }
        return written;
    }

    void PnLWriter::flush() {
        if (file_.is_open()) {
            file_.flush();
        }
    }

}
