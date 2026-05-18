# Tier system — 9 tiers synced with frontend
TIERS = [
    ("Copper",     0,      300),
    ("Bronze",     300,    800),
    ("Silver",     800,    2000),
    ("Gold",       2000,   5000),
    ("Platinum",   5000,   10000),
    ("Diamond",    10000,  20000),
    ("Mithril",    20000,  35000),
    ("Orichalcum", 35000,  60000),
    ("Adamantium", 60000,  999999),
]

def get_level(xp: int) -> str:
    for name, low, high in reversed(TIERS):
        if xp >= low:
            return name
    return "Copper"

def get_level_progress(xp: int) -> dict:
    for i, (name, low, high) in enumerate(TIERS):
        if xp < high:
            pct = round((xp - low) / (high - low) * 100, 1) if high != low else 100
            next_name = TIERS[i + 1][0] if i + 1 < len(TIERS) else None
            return {
                "level": name,
                "total_xp": xp,
                "progress_pct": min(pct, 100),
                "xp_to_next": max(0, high - xp),
                "current_min": low,
                "current_max": high,
                "next_tier": next_name,
            }
    return {
        "level": "Adamantium", "total_xp": xp,
        "progress_pct": 100, "xp_to_next": 0,
        "current_min": 60000, "current_max": 60000, "next_tier": None,
    }

def calculate_xp(bin_level: float, streak: int) -> tuple:
    base = 10
    if bin_level >= 80:
        base = int(base * 2.0)
    elif bin_level >= 60:
        base = int(base * 1.5)
    multiplier = min(1.0 + streak * 0.1, 2.0)
    xp  = int(base * multiplier)
    eco = xp // 100
    return xp, eco
