CREATE TABLE IF NOT EXISTS flash_counts (
  version TEXT NOT NULL,
  mode TEXT NOT NULL CHECK (mode IN ('update', 'wipe')),
  event TEXT NOT NULL CHECK (event IN ('download', 'flash')),
  count INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY (version, mode, event)
);
