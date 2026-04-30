# Index Database Query Guide

The system uses a SQLite database named `index.db` located in the base storage directory (default `./data/leveliii/index.db`) to manage metadata for all stored radar frames. This replaces the previous multi-JSON file approach, providing better concurrency and faster querying.

## Database Schema

The database contains a single table named `frames` with the following schema:

```sql
CREATE TABLE frames (
    station TEXT,             -- 4-letter radar station ID (e.g., KTLX)
    product_code INTEGER,     -- Level III product code
    product_name TEXT,        -- Descriptive name/category (e.g., reflectivity, velocity)
    timestamp TEXT,           -- ISO-8601 formatted timestamp (e.g., 2024-05-20T12:30:00Z)
    filename TEXT,            -- Name of the data file on disk (e.g., 0.5.RDA, volumetric.RDA)
    PRIMARY KEY (station, product_name, timestamp, filename)
);
```

## Indexes

To ensure high performance, the following indexes are maintained:

- `idx_station_product`: Optimized for lookups by station and product name.
- `idx_timestamp`: Optimized for temporal queries.

## Common Queries

### List all products for a station

```sql
SELECT DISTINCT product_name FROM frames WHERE station = 'KTLX';
```

### Get latest 10 frames for a specific product

```sql
SELECT timestamp, filename
FROM frames 
WHERE station = 'KTLX' AND product_name = 'reflectivity' 
ORDER BY timestamp DESC 
LIMIT 10;
```

### Find frames within a time range

```sql
SELECT station, product_name, timestamp, filename
FROM frames 
WHERE timestamp BETWEEN '2024-05-20T12:00:00Z' AND '2024-05-20T13:00:00Z'
ORDER BY timestamp ASC;
```

### Get all tilts for a specific volume scan

```sql
SELECT filename
FROM frames 
WHERE station = 'KTLX' AND product_name = 'reflectivity' AND timestamp = '2024-05-20T12:30:00Z'
ORDER BY filename ASC;
```

## Accessing the Database

You can inspect the database using the SQLite command-line tool:

```bash
sqlite3 ./data/leveliii/index.db
```

Inside the SQLite prompt, you can run `.schema` to see the table structure or `.tables` to list tables.
