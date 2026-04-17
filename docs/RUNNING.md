# Running the Service

### Executables
The build process generates two main executables:
1. `nexrad_pipeline`: The main background daemon that fetches and processes real-time data.
2. `process_to_volumetric`: A utility for offline processing of Level II files into the project's output format.

### Configuration Priority
The service looks for configuration in the following order:
1. Command-line Arguments (Highest Priority)
2. Environment Variables
3. Hardcoded Defaults (Lowest Priority)

---

### Command-Line Arguments (`nexrad_pipeline`)
- `--no-http`: Disables the Admin API server on port 13480.
- `--catchup`: Enables the catch-up process of fetching historical frames on startup.
- `--threads <N>`: Set number of worker threads (Base default: 4).
- `--buffer-count <N>`: Set number of pre-allocated buffers (Base default: 10).
- `--buffer-size <N>`: Set size of each buffer in MB (Base default: 10).
- `--help`: Show usage information.

### Environment Variables
#### Performance & Memory
- `NEXRAD_THREADS`: Number of worker threads.
- `NEXRAD_BUFFER_COUNT`: Number of pre-allocated buffers.
- `NEXRAD_BUFFER_SIZE_MB`: Size of each buffer in MB.

#### Station Monitoring
- `NEXRAD_MONITORED_STATIONS`: Comma-separated list of 4-letter station IDs (e.g., `KTLX,KEWX`).
  - Set to `ALL` or `*` to monitor all NEXRAD stations via S3 scanning.
  - If not set, the service defaults to monitoring `KTLX,KCRP,KEWX`.

#### AWS Configuration
- `AWS_ACCESS_KEY_ID`: Your AWS access key.
- `AWS_SECRET_ACCESS_KEY`: Your AWS secret key.
- `AWS_REGION`: AWS region (default: `us-east-1`).
- `NEXRAD_SQS_QUEUE_URL`: (Deprecated/Internal) The service currently uses high-efficiency S3 polling.

---
