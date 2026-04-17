# .RDA (Radar Data Archive) File Format Specification

The `.RDA` format is a compressed, bitmask-optimized format for storing NEXRAD Level II radar data. The current format is in testing and thus may change

## File Structure

The entire file is compressed using **Gzip**.

Once decompressed, the file follows this structure:

| Offset | Size (bytes) | Description |
|--------|--------------|-------------|
| 0      | 4            | **Metadata Size (S)**: Little-endian 32-bit unsigned integer. |
| 4      | S            | **Metadata**: UTF-8 encoded JSON string. |
| 4 + S  | B            | **Bitmask**: Packed bitmask where each bit represents a data point. |
| 4 + S + B | V         | **Quantized Values**: Array of 8-bit quantized values for "set" bits. |

### 1. Metadata (JSON)

The metadata block contains essential information about the radar frame or sweep.

**Key Map:**
- `s`: Station ID (e.g., "KTLX")
- `p`: Product type (e.g., "reflectivity", "velocity", "spectrum_width", "differential_reflectivity", "differential_phase", "correlation_coefficient")
- `t`: Timestamp (format: `YYYYMMDD_HHMMSS`)
- `e`: Elevation angle (tilt) in degrees.
- `f`: Format type (`"b"` for bitmask).
- `r`: Ray count (number of radials in the sweep).
- `g`: Gate count (number of range bins per ray).
- `gs`: Gate spacing (meters).
- `fg`: First gate distance (meters).
- `v`: Total number of valid (non-zero) data points.
- `tilts`: (Optional) Array of tilt angles if the file contains a full volume.
- `dualpol`: (Optional) Object containing dual-polarimetric metadata:
    - `sys_diff_refl`: System Differential Reflectivity (dB).
    - `sys_diff_phase`: System Differential Phase (deg).

### 2. Bitmask

The bitmask is a packed array of bits.
- **Total bits**: `ray_count * gate_count`
- **Total bytes (B)**: `ceil((ray_count * gate_count) / 8)`
- **Mapping**: Bit at index `i = (ray_idx * gate_count) + gate_idx` corresponds to the data point at `[ray_idx, gate_idx]`.
- **Bit Order**: Within a byte, bits are stored from Most Significant Bit (MSB) to Least Significant Bit (LSB). The bit at index 0 is `(byte[0] >> 7) & 1`.

### 3. Quantized Values

Only the data points marked as `1` in the bitmask are stored in this array.
- **Total bytes (V)**: Equal to the number of `1` bits in the bitmask.
- **Data type**: `uint8` (0-255).
- **De-quantization**:
  The raw value is mapped to a physical value based on the product type:
  - **Reflectivity**: `min = -32.0, max = 94.5`
  - **Velocity**: `min = -100.0, max = 100.0`
  - **Spectrum Width**: `min = 0.0, max = 64.0`
  - **Differential Reflectivity**: `min = -8.0, max = 8.0`
  - **Differential Phase**: `min = 0.0, max = 360.0`
  - **Correlation Coefficient**: `min = 0.0, max = 1.1`
  - **Dequantized Value**: `min + (uint8_val / 255.0) * (max - min)`

## Accessing Data

To access data at a specific ray and gate index:
1. Decompress the file.
2. Read the 4-byte metadata size.
3. Parse the JSON metadata.
4. Calculate the bit index: `bit_idx = (ray_idx * gate_count) + gate_idx`.
5. Check if bit `bit_idx` is set in the bitmask.
6. If set, count the number of set bits from `0` to `bit_idx - 1`. Let this count be `k`.
7. The value is at `quantized_values[k]`.

## Rendering

A Python helper script `render_radar.py` is provided to decode and visualize `.RDA` files.

```bash
python render_radar.py path/to/file.RDA
```
