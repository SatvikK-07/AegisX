import numpy as np


def _finite(values):
    array = np.asarray(values, dtype=float)
    return array[np.isfinite(array)]

def historical_var_es(returns, confidence=0.99, minimum=60):
    values = _finite(returns)
    if not 0.0 < confidence < 1.0:
        raise ValueError("confidence must be between zero and one")
    if len(values) < minimum:
        raise ValueError("insufficient observations")
    losses = -values
    var = float(np.quantile(losses, confidence, method="higher"))
    return {"var": var, "expected_shortfall": float(losses[losses >= var].mean())}


def drawdown_series(pnl):
    """Return running high-water mark and non-negative drawdown in tick P&L."""
    values = _finite(pnl)
    if not len(values):
        raise ValueError("no P&L observations")
    high_water = np.maximum.accumulate(values)
    return {"high_water": high_water, "drawdown": high_water - values, "maximum_drawdown": float((high_water - values).max())}


def stress_notional(positions, shocks):
    """Scenario P&L in ticks for aligned signed quantities, prices, and shocks."""
    quantities = _finite(positions["quantity"])
    prices = _finite(positions["price_ticks"])
    if len(quantities) != len(prices):
        raise ValueError("position quantities and prices must align")
    result = {}
    for name, shock in shocks.items():
        if not np.isfinite(shock):
            raise ValueError("shock must be finite")
        result[name] = float(np.dot(quantities, prices * float(shock)))
    return result
