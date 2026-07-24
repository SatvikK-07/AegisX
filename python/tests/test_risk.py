import pytest
from aegisx_research.risk import historical_var_es

def test_requires_observations():
    with pytest.raises(ValueError):
        historical_var_es([0.01], minimum=2)

def test_loss_positive():
    assert historical_var_es([-0.02, 0.01, -0.01], confidence=0.5, minimum=3)["var"] >= 0

def test_confidence_bounds():
    with pytest.raises(ValueError):
        historical_var_es([0.01, -0.01], confidence=1.0, minimum=2)
