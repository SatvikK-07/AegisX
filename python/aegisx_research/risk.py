import numpy as np
import pandas as pd


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


def parametric_var_es(returns, confidence=0.99, minimum=60):
    """Gaussian VaR/ES using a deterministic normal approximation."""
    values = _finite(returns)
    if len(values) < minimum:
        raise ValueError("insufficient observations")
    if not 0.0 < confidence < 1.0:
        raise ValueError("confidence must be between zero and one")
    # Fixed high-confidence z values avoid a scipy dependency.
    z_table = {0.95: (1.6448536269514722, 2.0627128075074257),
               0.975: (1.959963984540054, 2.337802792201413),
               0.99: (2.3263478740408408, 2.665214220345807)}
    if confidence not in z_table:
        raise ValueError("supported confidence levels are 0.95, 0.975, and 0.99")
    z_score, tail_ratio = z_table[confidence]
    mean = float(values.mean())
    standard_deviation = float(values.std(ddof=1))
    return {
        "var": -mean + z_score * standard_deviation,
        "expected_shortfall": -mean + tail_ratio * standard_deviation,
    }


def component_risk_contributions(positions, covariance):
    """Return Euler volatility contributions for signed tick notionals."""
    notionals = np.asarray(positions, dtype=float)
    matrix = np.asarray(covariance, dtype=float)
    if matrix.shape != (len(notionals), len(notionals)):
        raise ValueError("covariance matrix does not match positions")
    marginal = matrix @ notionals
    variance = float(notionals @ marginal)
    if variance <= 0.0:
        raise ValueError("portfolio variance must be positive")
    volatility = float(np.sqrt(variance))
    contribution = notionals * marginal / volatility
    return {
        "portfolio_volatility": volatility,
        "component_contributions": contribution,
        "contribution_sum": float(contribution.sum()),
    }


def liquidity_adjusted_notional(positions, liquidation_fraction=1.0):
    """Apply half-spread and square-root participation impact to a portfolio."""
    frame = pd.DataFrame(positions).copy()
    required = {"quantity", "price_ticks", "spread_ticks", "daily_volume"}
    missing = required.difference(frame.columns)
    if missing:
        raise ValueError(f"missing liquidity columns: {', '.join(sorted(missing))}")
    if not 0.0 < liquidation_fraction <= 1.0:
        raise ValueError("liquidation_fraction must be in (0, 1]")
    quantity = frame["quantity"].abs().astype(float) * liquidation_fraction
    participation = quantity / frame["daily_volume"].astype(float)
    if (participation < 0).any() or (~np.isfinite(participation)).any():
        raise ValueError("invalid daily volume")
    spread_cost = quantity * frame["spread_ticks"].astype(float) / 2.0
    impact_cost = quantity * frame["price_ticks"].astype(float) * 0.1 * np.sqrt(participation)
    return {
        "gross_notional_ticks": float((quantity * frame["price_ticks"].astype(float)).sum()),
        "spread_cost_ticks": float(spread_cost.sum()),
        "impact_cost_ticks": float(impact_cost.sum()),
        "liquidity_adjusted_notional_ticks": float(
            (quantity * frame["price_ticks"].astype(float) + spread_cost + impact_cost).sum()
        ),
    }


def portfolio_stress_report(positions, scenarios):
    """Apply named symbol/sector shocks and report P&L plus concentration."""
    frame = pd.DataFrame(positions).copy()
    required = {"symbol", "sector", "quantity", "price_ticks"}
    missing = required.difference(frame.columns)
    if missing:
        raise ValueError(f"missing stress columns: {', '.join(sorted(missing))}")
    frame["notional_ticks"] = frame["quantity"].astype(float) * frame["price_ticks"].astype(float)
    gross = float(frame["notional_ticks"].abs().sum())
    rows = []
    for name, scenario in scenarios.items():
        market_shock = float(scenario.get("market", 0.0))
        sector_shocks = scenario.get("sectors", {})
        symbol_shocks = scenario.get("symbols", {})
        shocks = []
        for row in frame.itertuples(index=False):
            shocks.append(float(symbol_shocks.get(row.symbol, sector_shocks.get(row.sector, market_shock))))
        pnl = float(np.dot(frame["notional_ticks"].to_numpy(), np.asarray(shocks)))
        rows.append({"scenario": name, "pnl_ticks": pnl, "loss_ticks": max(0.0, -pnl)})
    symbol_gross = frame.groupby("symbol")["notional_ticks"].apply(lambda values: values.abs().sum())
    return {
        "scenarios": pd.DataFrame(rows),
        "gross_exposure_ticks": gross,
        "net_exposure_ticks": float(frame["notional_ticks"].sum()),
        "largest_symbol_concentration": float(symbol_gross.max() / gross) if gross else 0.0,
    }
