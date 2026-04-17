# Admin API Guide (HTML/API Endpoints)

The `nexrad_pipeline` provides a RESTful Admin API when started with the `--http` flag. The default port is **13480**.

### Base URL: `http://localhost:13480/api/`

---

### Station Management

#### `GET /api/stations`
- **Description**: Returns the list of currently monitored stations.
- **Response**: `[{"name": "KTLX", "status": "active"}, ...]`

#### `POST /api/stations`
- **Description**: Add a new NEXRAD station to the monitored list.
- **Body**: `{"name": "KTLX"}`
- **Response**: `{"success": true, "station": "KTLX"}`

#### `DELETE /api/stations/:name`
- **Description**: Stop monitoring a specific station.
- **Response**: `{"success": true, "station": "KTLX"}`

---

### System Monitoring

#### `GET /api/metrics`
- **Description**: Retrieve overall system performance metrics.
- **Response**:
```json
{
    "avg_frames_per_minute": 1.25,
    "disk_usage_gb": 4.52,
    "disk_usage_mb": 4628,
    "frame_count": 125,
    "frames_failed": 2,
    "frames_fetched": 123,
    "last_fetch_timestamp": 1708892143000,
    "success_rate": 98.4,
    "uptime_seconds": 3600
}
```

#### `GET /api/status`
- **Description**: Get current service operational status.
- **Response**: `{"fetcher_running": true, "status": "operational", "timestamp": 1708892143}`

---

### System Configuration

#### `GET /api/config`
- **Description**: Retrieve current memory and performance settings.
- **Response**:
```json
{
    "auto_cleanup_enabled": true,
    "buffer_pool_size": 10,
    "buffer_size_mb": 10,
    "cleanup_interval_seconds": 300,
    "fetcher_thread_pool_size": 4,
    "max_frames_per_station": 30,
    "scan_interval_seconds": 30
}
```

#### `POST /api/config`
- **Description**: Update system configuration at runtime. Triggers pool re-initialization.
- **Body**: Any subset of the configuration keys.
- **Example Body**: `{"fetcher_thread_pool_size": 8, "buffer_pool_size": 20}`
- **Response**: `{"success": true, "config": { ... updated config ... }}`

---

### Process Control

#### `POST /api/pause`
- **Description**: Stop all worker threads and halt data fetching.
- **Response**: `{"success": true, "status": "paused"}`

#### `POST /api/resume`
- **Description**: Restart worker threads and resume data fetching.
- **Response**: `{"success": true, "status": "resumed"}`
