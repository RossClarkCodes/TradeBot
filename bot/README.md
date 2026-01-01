# Kraken XBT/CAD Trading Bot

A personal, single-user cryptocurrency trading bot for Kraken exchange. This bot trades only your own funds using legitimate market orders with strict risk controls.

## ⚠️ Important Disclaimers

- **This is personal trading software** - not a commercial product
- **No financial advice** - this bot does not provide investment advice
- **No guarantees** - past performance does not guarantee future results
- **You are responsible** - for understanding the risks, fees, and tax implications
- **Start small** - always test with small amounts first

## Features

- Take-profit and stop-loss based strategy
- Percent-based position sizing with risk controls
- Dry-run mode for paper trading with real price data
- State persistence and crash recovery
- Kill switch for emergency stops
- Comprehensive logging for auditing
- Rate limiting and exponential backoff
- No leverage, market orders only

## Safety Design

This bot is designed with safety as the top priority:

- **No leverage** - only trades available equity
- **Market orders only** - no complex order types
- **One position at a time** - no overlapping trades
- **Cooldown periods** - prevents overtrading
- **Daily trade limits** - caps trading frequency
- **Fee buffer** - reserves funds for fees
- **Kill switch** - immediate trading halt via file
- **State persistence** - recovers safely after restart

## Strategy Overview

The bot uses a simple take-profit/stop-loss strategy:

1. **Entry**: Buy when price resets below previous exit (or immediately for first trade)
2. **Exit**: Sell when price reaches take-profit (+1.5%) or stop-loss (-0.6%)
3. **Position sizing**: Based on percent of equity at risk, clamped to maximum position size

### Position Sizing Formula

```
risk_cad = equity_cad × risk_per_trade_pct
raw_position_cad = risk_cad / stop_loss_pct
max_position_cad = equity_cad × max_position_pct
position_cad = min(raw_position_cad, max_position_cad)
```

## Dependencies

### macOS (Homebrew)

```bash
brew install cmake
brew install curl
brew install openssl@3
brew install nlohmann-json
```

### Ubuntu/Debian

```bash
sudo apt-get update
sudo apt-get install -y cmake build-essential
sudo apt-get install -y libcurl4-openssl-dev
sudo apt-get install -y libssl-dev
sudo apt-get install -y nlohmann-json3-dev
```

## Building

```bash
cd bot

# Create build directory
mkdir -p build
cd build

# Configure (Release build)
cmake -DCMAKE_BUILD_TYPE=Release ..

# Build
make -j$(nproc)

# The executable is: build/trading_bot
```

### macOS OpenSSL Note

If CMake can't find OpenSSL on macOS, you may need to set the path:

```bash
cmake -DCMAKE_BUILD_TYPE=Release \
      -DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3) \
      ..
```

## Configuration

Edit `config.json` to customize the bot:

```json
{
  "pair": "XXBTZCAD",
  "take_profit_pct": 0.015,
  "stop_loss_pct": 0.006,
  "rebuy_reset_pct": 0.006,
  "risk_per_trade_pct": 0.01,
  "max_position_pct": 0.90,
  "min_cad_required_pct": 0.02,
  "poll_interval_seconds": 5,
  "cooldown_seconds": 600,
  "max_trades_per_day": 3,
  "dry_run": true,
  "sim_fee_pct_roundtrip": 0.004,
  "sim_initial_cad": 1000.0,
  "kraken_api_base": "https://api.kraken.com",
  "rate_limit_min_delay_ms": 500,
  "max_consecutive_failures": 10,
  "stale_price_seconds": 30
}
```

### Key Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `pair` | XXBTZCAD | Trading pair (Kraken format: XXBT=BTC, ZCAD=CAD) |
| `take_profit_pct` | 0.015 | Take profit at +1.5% |
| `stop_loss_pct` | 0.006 | Stop loss at -0.6% |
| `rebuy_reset_pct` | 0.006 | Price must drop 0.6% from exit before re-entry |
| `risk_per_trade_pct` | 0.01 | Risk 1% of equity per trade |
| `max_position_pct` | 0.90 | Maximum 90% of equity per position |
| `min_cad_required_pct` | 0.02 | Reserve 2% for fees/buffer |
| `cooldown_seconds` | 600 | 10-minute cooldown after each trade |
| `max_trades_per_day` | 3 | Maximum 3 trades per day |
| `dry_run` | true | Paper trading mode (no real orders) |

## Running

### Dry-Run Mode (Paper Trading)

```bash
# From the bot directory
./build/trading_bot config.json
```

No API credentials required for dry-run mode. The bot will:
- Fetch real prices from Kraken public API
- Simulate trades with configured initial balance
- Log all decisions and simulated fills
- Track P&L with simulated fees

### Live Mode (Real Trading)

1. **Generate API keys** on Kraken with permissions:
   - Query Funds
   - Query Open Orders & Trades
   - Create & Modify Orders

2. **Set environment variables**:

```bash
export KRAKEN_API_KEY="your-api-key"
export KRAKEN_API_SECRET="your-api-secret"
```

3. **Update config** (`dry_run: false`):

```json
{
  "dry_run": false
}
```

4. **Run the bot**:

```bash
./build/trading_bot config.json
```

### Running in Background

```bash
# Using nohup
nohup ./build/trading_bot config.json > /dev/null 2>&1 &

# Using screen
screen -S trading_bot
./build/trading_bot config.json
# Ctrl+A, D to detach

# Using tmux
tmux new -s trading_bot
./build/trading_bot config.json
# Ctrl+B, D to detach
```

## Kill Switch

To immediately stop the bot from placing new orders:

```bash
# Create kill switch file
touch KILL_SWITCH

# Bot will log "Kill switch active" and exit cleanly
```

To resume trading:

```bash
rm KILL_SWITCH
./build/trading_bot config.json
```

## State Management

The bot persists state to `state.json`:

```json
{
  "mode": "FLAT",
  "entry_price": null,
  "exit_price": 85000.00,
  "btc_amount": 0.0,
  "last_trade_time": 1735689600,
  "trades_today": 1,
  "trades_date_yyyy_mm_dd": "2026-01-01",
  "sim_cad_balance": 1015.00,
  "sim_btc_balance": 0.0
}
```

### Recovery on Restart

- In **dry-run mode**: Continues from saved simulation state
- In **live mode**: Reconciles state with actual Kraken balances

If you're holding BTC but `entry_price` is missing (e.g., after manual deposit), the bot will:
1. Log a WARNING
2. Set `entry_price` to current market price
3. Continue operating (you may want to manually verify/adjust)

## Logs

Logs are written to `logs/bot.log` and console.

Each log entry includes:
- Timestamp
- Current price
- Mode (FLAT/LONG)
- Entry/exit prices
- Take-profit and stop-loss levels
- Cooldown remaining
- Trades today
- Equity and position sizing
- Decision (NOOP/BUY/SELL/BLOCKED) and reason

Example log output:

```
[2026-01-01T10:30:00] [   INFO] Status | price=85000.00 | mode=FLAT | entry=null | exit=84500.00 | tp=0.00 | sl=0.00 | cooldown=300s | trades=1/3 | date=2026-01-01 | equity=1000.00 | available=1000.00 | risk_pct=1.00% | risk_cad=10.00 | pos_cad=1666.67 | max_pos=900.00 | decision=BLOCKED | reason=Cooldown active: 300s remaining
```

## Safety Checklist

Before running in live mode:

- [ ] Run dry-run for at least 48 hours
- [ ] Verify logs show expected behavior
- [ ] Test kill switch stops the bot
- [ ] Test restart recovers state correctly
- [ ] Start with small `risk_per_trade_pct` (e.g., 0.005 = 0.5%)
- [ ] Understand Kraken's fee structure
- [ ] Understand tax implications in your jurisdiction
- [ ] Have a plan for monitoring the bot
- [ ] Set up alerts for failures (check logs periodically)
- [ ] Verify balances reconcile correctly after trades

## Troubleshooting

### "API credentials not initialized"

- Set `KRAKEN_API_KEY` and `KRAKEN_API_SECRET` environment variables
- Verify API keys have required permissions on Kraken

### "Price data is stale"

- Network connectivity issues to Kraken
- Kraken API may be experiencing issues
- Bot will wait and retry with backoff

### "Too many consecutive API failures"

- Kraken API unavailable
- Network issues
- Bot halts for safety - check connectivity and restart

### "Kill switch active"

- Remove `KILL_SWITCH` file to resume trading

### State corruption

- Delete `state.json` and restart (will initialize fresh)
- In live mode, bot will reconcile with actual balances

## Project Structure

```
bot/
├── src/
│   ├── main.cpp          # Main entry point and loop
│   ├── config.hpp/cpp    # Configuration loading
│   ├── state.hpp/cpp     # State persistence
│   ├── logger.hpp/cpp    # Logging
│   ├── kraken_client.hpp/cpp  # Kraken API client
│   ├── strategy.hpp/cpp  # Trading logic
│   └── util.hpp/cpp      # Utilities
├── config.json           # Configuration file
├── state.json            # Persisted state
├── CMakeLists.txt        # Build configuration
├── README.md             # This file
└── logs/                 # Log directory (created at runtime)
```

## Legal / Compliance Notes

This bot:
- Trades only your own funds on your own Kraken account
- Uses only legitimate market orders
- Does not engage in any form of market manipulation
- Does not spam the order book
- Does not attempt to evade exchange controls
- Respects API rate limits

This is personal trading software for educational and personal use. You are responsible for:
- Compliance with your local laws and regulations
- Tax reporting on trading gains/losses
- Understanding the risks of cryptocurrency trading
- Kraken's terms of service compliance

## License

Personal use only. Not for commercial distribution or providing trading services to others.

