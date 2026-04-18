# Level II / Level III Data Format Documentation

## Binary Storage Format

Both Level II and Level III data use the same bitmask-quantized format:

```
[4-byte little-endian metadata size][JSON metadata][bitmask bytes][quantized value bytes]
```

- **Metadata size**: 4-byte unsigned integer (little-endian)
- **JSON metadata**: UTF-8 encoded JSON with product-specific fields
- **Bitmask**: 1 bit per grid cell (1 = valid data, 0 = missing)
- **Values**: uint8_t (0-255), only stored for cells where bitmask=1

Compression: gzip applied to the entire uncompressed data.

---

## Quantization Parameters

Values are quantized to 8-bit unsigned integers (0-255) using:

```
normalized = (value - min) / (max - min)
quantized = round(normalized * 255)
```

To decode:

```
value = min + (quantized / 255.0) * (max - min)
```

### Product Type Mappings

| Product Type | Category | Min Value | Max Value | Units | Notes |
|--------------|----------|-----------|-----------|-------|-------|
| **reflectivity** | Base Reflectivity (N0R, N1Q, N2Q, etc.) | -32.0 | 94.5 | dBZ | Default fallback |
| **velocity** / **super_res_velocity** | Base Velocity (N0V), Digital Velocity (DV), Storm Relative (SRM) | -100.0 | 100.0 | m/s | |
| **spectrum_width** | Spectrum Width (SW) | 0.0 | 64.0 | m/s | |
| **differential_reflectivity** | Digital ZDR (DZD) | -8.0 | 8.0 | dB | |
| **specific_differential_phase** | Digital KDP (DKD) | 0.0 | 360.0 | deg/km | |
| **correlation_coefficient** | Digital CC (DCC), Hybrid HC (HHC) | 0.0 | 1.1 | ratio | Unitless |
| **echo_tops** / **enhanced_echo_tops** | Echo Tops (NET), Enhanced Echo Tops (EET) | 0.0 | 230.0 | kft | Thousands of feet |
| **vil** | Vertically Integrated Liquid (NVL, DVL) | 0.0 | 75.0 | kg/m² | |
| **precipitation** | Rainfall accumulations (N1P, N3P, NTP, etc.) | 0.0 | 24.0 | inches | |

---

## Level II Product List

| Code | Name | AWIPS | Category | Quantization Type |
|------|------|-------|----------|-------------------|
| 19 | N0R | N0R | Base Reflectivity | reflectivity |
| 27 | N0V | N0V | Base Velocity | velocity |
| 30 | SW | NSW | Spectrum Width | spectrum_width |
| 37 | NCR | NCR | Composite Reflectivity | reflectivity |
| 41 | NET | NET | Echo Tops | echo_tops |
| 56 | SRM | N0S,N1S,N2S,N3S | Storm Relative Mean Radial Velocity | velocity |
| 57 | NVL | NVL | Vertically Integrated Liquid | vil |
| 78 | N1P | N1P | 1-hour Rainfall | precipitation |
| 79 | N3P | N3P | 3-hour Rainfall | precipitation |
| 80 | NTP | NTP | Storm Total Rainfall | precipitation |
| 81 | DPA | DPA | Digital Precipitation Array | precipitation |
| 82 | SPD | SPD | Supplemental Precipitation | precipitation |
| 94 | DR | N0Q,N1Q,N2Q,NAQ,NBQ | Digital Reflectivity | reflectivity |
| 99 | DV | N0U,N1U,N2U,N3U | Digital Velocity | velocity |
| 110 | N1Q | N1Q | Base Reflectivity Layer 2 | reflectivity |
| 111 | N2Q | N2Q | Base Reflectivity Layer 3 | reflectivity |
| 134 | DVL | DVL | Digital VIL | vil |
| 135 | EET | EET | Enhanced Echo Tops | echo_tops |
| 138 | DSP | DSP | Digital Storm Total Precip | precipitation |
| 153 | SDR | NXB,NYB,NZB,N0B,NAB,N1B,NBB,N2B,N3B | Super-Res Reflectivity | reflectivity |
| 154 | SDV | NXG,NYG,NZG,N0G,NAG,N1G,NBU,N2U,N3U | Super-Res Velocity | velocity |
| 159 | DZD | NXX,NYX,NZX,N0X,NAX,N1X,NBX,N2X,N3X | Digital Differential Reflectivity | differential_reflectivity |
| 161 | DCC | NXC,NYC,NZC,N0C,NAC,N1C,NBC,N2C,N3C | Digital Correlation Coefficient | correlation_coefficient |
| 163 | DKD | N0K,N1K,N2K,N3K | Digital Specific Differential Phase | specific_differential_phase |
| 165 | DHC | N0H,N1H | Digital Hydrometeor Classification | classification* |
| 166 | ML | ML | Melting Layer | classification* |
| 169 | OHA | OHA | One Hour Accumulation | precipitation |
| 170 | DAA | DAA | Digital Accumulation Array | precipitation |
| 172 | DTA | DTA | Digital Storm Total | precipitation |
| 173 | DUA | DU3,DU6 | Digital User-Selectable Accumulation | precipitation |
| 174 | DOD | DOD | Digital One-Hour Difference | precipitation |
| 175 | DSD | DSD | Digital Storm Total Difference | precipitation |
| 177 | HHC | HHC | Hybrid Hydrometeor Classification | correlation_coefficient |
| 180 | TDR | TZ0,TZ1,TZ2 | Digital Reflectivity (Long Range) | reflectivity |
| 182 | TDV | TV0,TV1,TV2 | Digital Velocity (Long Range) | velocity |
| 186 | TZL | TZL | Long Range Reflectivity | reflectivity |

*Products marked with asterisk (*) use specialized handling - not standard bitmask quantization.

---

## Metadata Fields

### Frame-level (single elevation)

| Field | Type | Description |
|-------|------|-------------|
| s | string | Station identifier (e.g., "KTWX") |
| pc | int16 | Product code |
| p | string | Product name |
| t | string | Timestamp (ISO format or YYYYMMDD_HHMMSS) |
| e | float | Elevation angle (degrees) |
| f | string | Format type ("b" = bitmask) |
| r | int | Number of rays (typically 360 or 720) |
| g | int | Number of gates |
| gs | float | Gate spacing (meters) |
| fg | float | First gate range (meters) |
| v | int | Number of quantized values stored |

### Volumetric (multiple elevations)

| Field | Type | Description |
|-------|------|-------------|
| s | string | Station identifier |
| p | string | Product name |
| t | string | Timestamp |
| f | string | Format type ("b" = bitmask) |
| tilts | array | List of elevation angles |
| r | int | Number of rays |
| g | int | Number of gates |
| gs | float | Gate spacing |
| fg | float | First gate range |
| v | int | Total values across all tilts |

---

## Data Values

- **Value 0**: Below Signal-to-Noise Ratio threshold (no data)
- **Value 1**: Range ambiguous / overlaid data
- **Values 2-255**: Valid data, mapped to product-specific range

---

## File Extensions

- Single frame: `{elevation}.RDA` (e.g., `0.5.RDA`, `1.5.RDA`)
- Volumetric: `volumetric.RDA`
- Index: `index_{category}.json`

---

## References

- Archive II Format: `example_code/wxdata/archive2_format.txt`
- Level II Pipeline: `example_code/nexrad-levelii-pipeline/`