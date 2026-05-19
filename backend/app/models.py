from sqlalchemy import Column, String, Integer, Float, Boolean, DateTime
from sqlalchemy.sql import func
from app.database import Base
import uuid

def gen_uuid():
    return str(uuid.uuid4())

class User(Base):
    __tablename__ = "users"
    id         = Column(String, primary_key=True, default=gen_uuid)
    nim        = Column(String, unique=True, nullable=False)
    name       = Column(String, nullable=False)
    faculty    = Column(String, default="STEI")
    qr_code    = Column(String, unique=True)
    created_at = Column(DateTime, server_default=func.now())

class Bin(Base):
    __tablename__ = "bins"
    id           = Column(String, primary_key=True, default=gen_uuid)
    location     = Column(String, nullable=False)
    capacity_pct = Column(Float, default=0.0)
    last_updated = Column(DateTime, server_default=func.now())

class ThrowEvent(Base):
    __tablename__ = "throw_events"
    id               = Column(String, primary_key=True, default=gen_uuid)
    user_id          = Column(String, nullable=False)
    bin_id           = Column(String, nullable=False)
    timestamp        = Column(DateTime, server_default=func.now())
    points_earned    = Column(Integer, default=0)
    bin_level_before = Column(Float, default=0.0)
    bin_level_after  = Column(Float, default=0.0)
    verified         = Column(Boolean, default=False)

class UserPoints(Base):
    __tablename__ = "user_points"
    id            = Column(String, primary_key=True, default=gen_uuid)
    user_id       = Column(String, unique=True, nullable=False)
    total_xp      = Column(Integer, default=0)
    level         = Column(String, default="Bronze")
    streak_days   = Column(Integer, default=0)
    eco_points    = Column(Integer, default=0)
    last_activity = Column(DateTime)
