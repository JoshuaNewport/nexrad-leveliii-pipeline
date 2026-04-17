# Data Storage Layout Documentation

## Directory Structure

All Level III radar products are organized using the following hierarchical structure:

```
BASE_PATH/
└── STATION/
    └── STORAGE_CATEGORY/
        └── TIMESTAMP/
            ├── ELEVATION.RDA
            ├── ELEVATION.RDA
            └── ...
```

### Example

```
/data/leveliii/
├── KOUN/
│   ├── reflectivity/
│   │   └── 20260417_200334/
│   │       ├── 0.5.RDA
│   │       ├── 1.3.RDA
│   │       └── 2.4.RDA
│   ├── velocity/
│   │   └── 20260417_200334/
│   │       ├── 0.5.RDA
│   │       └── 1.3.RDA
│   ├── super_res_reflectivity/
│   │   └── 20260417_200334/
│   │       ├── 0.5.RDA
│   │       ├── 1.3.RDA
│   │       └── 2.4.RDA
│   └── enhanced_echo_tops/
│       └── 20260417_200334/
│           └── 0.0.RDA
├── KEWX/
│   ├── reflectivity/
│   │   └── ...
│   └── ...
└── ...
```

## Storage Categories

Storage categories group related products together in the same directory, allowing multiple products and elevations to coexist. This simplifies adding new products of the same category.

### Defined Storage Categories

| Storage Category | Products | Description |
|---|---|---|
| `reflectivity` | N0R, NCR, DR, N1Q, N2Q, TDR, TZL | Base and digital reflectivity products |
| `velocity` | N0V, SRM, DV, TDV | Velocity and radial velocity products |
| `super_res_reflectivity` | SDR (N0B, N1B, N2B, N3B, NXB, NYB, NZB, NAB, NBB) | Super-resolution reflectivity products |
| `super_res_velocity` | SDV (N0G, N1G) | Super-resolution velocity products |
| `correlation_coefficient` | DCC (N0C, N1C, N2C, N3C) | Digital correlation coefficient products |
| `differential_reflectivity` | DZD (N0X, N1X, N2X, N3X) | Digital differential reflectivity products |
| `specific_differential_phase` | DKD (N0K, N1K, N2K, N3K) | Specific differential phase products |
| `echo_tops` | NET, EET | Echo tops products |
| `enhanced_echo_tops` | EET | Enhanced echo tops products (when grouped separately) |
| `vil` | NVL, DVL | Vertically integrated liquid products |
| `hydrometeor_classification` | DHC, HHC, N0H, N1H | Hydrometeor classification products |
| `melting_layer` | ML | Melting layer products |
| `precipitation` | N1P, N3P, NTP, DPA, SPD, OHA, DAA, DTA, DUA, DOD, DSD, DSP | Precipitation and accumulation products |
| `spectrum_width` | SW | Spectrum width products |

## File Naming Convention

Radar data files use the following naming convention:

```
ELEVATION.RDA
```

Where:
- **ELEVATION**: Radar beam elevation angle in degrees (e.g., 0.5, 1.3, 2.4)
- **RDA**: File extension (Radar Data Archive)

The product type is determined by the `STORAGE_CATEGORY` directory, so the filename only needs the elevation angle.

### Examples

- `0.5.RDA` - Base reflectivity at 0.5° elevation (stored in `reflectivity/`)
- `0.0.RDA` - Enhanced echo tops at 0° elevation (stored in `enhanced_echo_tops/`)
- `1.3.RDA` - Super-resolution reflectivity at 1.3° elevation (stored in `super_res_reflectivity/`)
- `2.4.RDA` - Correlation coefficient at 2.4° elevation (stored in `correlation_coefficient/`)

## Adding New Products

To add a new product to the system:

1. **Add product code to ProductDatabase.cpp** in `InitializeProducts()`:
   ```cpp
   add_product(
       CODE,                           // Unique product code (e.g., 161)
       "SHORTNAME",                    // Product short name (e.g., "DCC")
       "Full Product Description",     // Human-readable description
       {"AWIPS1", "AWIPS2", ...},     // Alternative AWIPS names
       "category",                     // Display category (rarely used)
       "storage_category",             // Storage directory category (key!)
       is_compressed,                  // Compression flag (true/false)
       {PACKET_TYPE1, PACKET_TYPE2}   // Supported packet types
   );
   ```

2. **Choose or create a storage_category**:
   - Use existing category if product belongs to existing group
   - Create new category for new product types following the naming convention: `lowercase_with_underscores`

3. **Example: Adding N0B product to super_res_reflectivity**:
   ```cpp
   add_product(153, "SDR", "Super-Resolution Reflectivity", 
               {"N0B", "N1B", "N2B", "N3B"}, 
               "super_res",                    // category (display)
               "super_res_reflectivity",       // storage_category (filesystem)
               true, {16, 0xAF1F});
   ```

When N0B products are processed, they automatically go to:
```
STATION/super_res_reflectivity/TIMESTAMP/N0B_ELEVATION.RDA
```

## Implementation Details

### ProductDatabase

The `ProductDatabase` class manages product metadata:

- **`GetProductStorageCategory(code)`** - Returns the storage category directory name for organizing files
- **`GetProductCategory(code)`** - Returns the display category (rarely used in storage)
- **`GetProductName(code)`** - Returns the product short name for filenames

### FrameStorageManager

Handles file storage with the new directory layout:

- Automatically creates `STATION/STORAGE_CATEGORY/TIMESTAMP/` directories as needed
- File paths: `BASE_PATH/STATION/STORAGE_CATEGORY/TIMESTAMP/ELEVATION.RDA`
- Product type is implicit from the storage category directory
- Supports both individual elevations and volumetric data

### process_level3

Command-line utility for offline processing:

```bash
./process_level3 <input_file_or_dir> <output_dir>
```

Output structure automatically matches the defined storage layout.

## Benefits of This Design

1. **Extensibility**: Adding new products is trivial - just add one line to ProductDatabase
2. **Grouping**: Related products (e.g., all super-res reflectivity) live in the same directory
3. **Clarity**: Directory names clearly indicate data type and storage location
4. **Flexibility**: Multiple elevation angles for the same product coexist naturally
5. **Performance**: Simpler directory traversal compared to nested product folders
6. **Maintainability**: Storage category definition is a single field in the product database
