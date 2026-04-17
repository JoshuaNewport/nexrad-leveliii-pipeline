#!/usr/bin/env python3
import gzip
import json
import base64
import numpy as np
import argparse
import sys
import os

# Optional dependency for plotting
try:
    import matplotlib.pyplot as plt
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False

import struct

def get_quant_params(product_type):
    if product_type == "velocity":
        return {"min": -100.0, "max": 100.0}
    elif product_type == "spectrum_width":
        return {"min": 0.0, "max": 64.0}
    elif product_type == "differential_reflectivity":
        return {"min": -8.0, "max": 8.0}
    elif product_type == "differential_phase":
        return {"min": 0.0, "max": 360.0}
    elif product_type == "cross_correlation_ratio":
        return {"min": 0.0, "max": 1.1}
    return {"min": -32.0, "max": 95.0}

def decode_bitmask_format(binary_data, ray_count, gate_count, product_type):
    """
    Decode the binary bitmask format:
    [Bitmask] + [Packed Values]
    """
    params = get_quant_params(product_type)
    min_val = params["min"]
    max_val = params["max"]
    
    # Calculate bitmask size in bytes
    total_bits = ray_count * gate_count
    bitmask_bytes_count = (total_bits + 7) // 8
    
    if len(binary_data) < bitmask_bytes_count:
        raise ValueError(f"Data too short: expected at least {bitmask_bytes_count} bytes for bitmask, got {len(binary_data)}")
    
    # Split bitmask and packed values
    bitmask = np.frombuffer(binary_data[:bitmask_bytes_count], dtype=np.uint8)
    packed_values = np.frombuffer(binary_data[bitmask_bytes_count:], dtype=np.uint8)
    
    # Reconstruct the grid
    grid = np.zeros((ray_count, gate_count), dtype=np.float32)
    
    value_idx = 0
    for r in range(ray_count):
        for g in range(gate_count):
            bit_idx = r * gate_count + g
            byte_idx = bit_idx // 8
            bit_pos = 7 - (bit_idx % 8)
            
            if (bitmask[byte_idx] >> bit_pos) & 1:
                if value_idx < len(packed_values):
                    val_quant = float(packed_values[value_idx])
                    # Dequantize
                    grid[r, g] = min_val + (val_quant / 255.0) * (max_val - min_val)
                    value_idx += 1
                    
    return grid

def main():
    parser = argparse.ArgumentParser(description='Render NEXRAD bitmask radar data')
    parser.add_argument('file', help='Path to .RDA radar file')
    parser.add_argument('--output', default='radar_plot.png', help='Output image file')
    
    args = parser.parse_args()
    
    if not os.path.exists(args.file):
        print(f"Error: File not found: {args.file}")
        sys.exit(1)
        
    try:
        with gzip.open(args.file, 'rb') as f:
            raw_content = f.read()
            
        # Try new format first (4-byte size + JSON)
        is_new_format = False
        if len(raw_content) > 4:
            meta_size = struct.unpack('<I', raw_content[:4])[0]
            # Sanity check: meta_size should be reasonable (e.g. < 64KB)
            if meta_size < len(raw_content) - 4 and meta_size < 65536:
                try:
                    meta_json = raw_content[4:4+meta_size].decode('utf-8')
                    meta = json.loads(meta_json)
                    if 'f' in meta:
                        data_body = raw_content[4+meta_size:]
                        is_new_format = True
                        print(f"Detected NEW format (bitmask) with metadata size {meta_size}")
                except:
                    pass

        if not is_new_format:
            # Try old format (pure JSON)
            try:
                meta_json = raw_content.decode('utf-8')
                meta = json.loads(meta_json)
                print(f"Detected OLD format (JSON/quantized)")
                # For old format, 'd' contained base64 data
                if 'd' in meta:
                    data_body = base64.b64decode(meta['d'])
                else:
                    data_body = b''
            except Exception as e:
                print(f"Error: Could not parse file as new or old format: {e}")
                sys.exit(1)
        
        print(f"Metadata: {meta}")
        
    except Exception as e:
        print(f"Error reading file: {e}")
        sys.exit(1)
        
    format_type = meta.get('f', 'q')
    if format_type == 'q':
        print("Processing legacy 'q' (quantized triplet) format...")
        # Old 7-byte triplet format: [Azimuth: 2][Range: 2][Value: 1][Unused: 2]
        # We'll just report stats as it's hard to render without full logic
        num_triplets = len(data_body) // 7
        print(f"Found {num_triplets} triplets")
        if num_triplets > 0:
            first_val = data_body[4] if len(data_body) > 4 else 0
            print(f"Example value: {first_val}")
        return

    if format_type != 'b':
        print(f"Warning: Unknown file format '{format_type}'.")
        sys.exit(1)
        
    ray_count = meta.get('r', 720)
    gate_count = meta.get('g', 0)
    
    if gate_count == 0:
        print("Error: gate_count is 0")
        sys.exit(1)
        
    print(f"Decoding {meta.get('p')} at {meta.get('e')}° ({ray_count}x{gate_count} grid)...")
    grid = decode_bitmask_format(data_body, ray_count, gate_count, meta.get('p', 'reflectivity'))
    
    if not HAS_MATPLOTLIB:
        print("\nSuccess! Data decoded, but matplotlib is not installed.")
        print(f"Grid shape: {grid.shape}")
        print(f"Non-zero bins: {np.count_nonzero(grid)}")
        print(f"Max value: {np.max(grid)}")
        print("\nTo render the plot, install matplotlib: pip install matplotlib")
        return

    # Convert to polar for rendering
    # Azimuths are evenly spaced 0-360 for ray_count rays
    azimuths = np.linspace(0, 2*np.pi, ray_count + 1)
    
    # Ranges
    # Use meta fields or defaults
    gate_spacing = meta.get('gs', 250)
    first_gate = meta.get('fg', 2125)
    ranges = np.linspace(first_gate, first_gate + gate_count * gate_spacing, gate_count + 1)
    
    # Create polar plot
    plt.figure(figsize=(12, 10))
    ax = plt.subplot(111, projection='polar')
    
    # Set theta offset to North (90 deg) and direction to clockwise
    ax.set_theta_offset(np.pi/2)
    ax.set_theta_direction(-1)
    
    # Filter out 0s for better visualization
    grid_plot = grid.copy()
    grid_plot[grid_plot == 0] = np.nan
    
    # Plot using pcolormesh
    mesh = ax.pcolormesh(azimuths[:-1], ranges[:-1], grid_plot.T, cmap='viridis', shading='auto')
    
    plt.colorbar(mesh, label=f"{meta.get('p', 'data')} raw value")
    plt.title(f"Radar {meta.get('p', 'data')} - Tilt {meta.get('e')}°\n{meta.get('t', 'Unknown Time')}", pad=20)
    
    # Add some grid lines
    ax.set_rlabel_position(135)
    plt.grid(True, linestyle=':', alpha=0.5)
    
    plt.savefig(args.output, dpi=150, bbox_inches='tight')
    print(f"Saved plot to {args.output}")

if __name__ == "__main__":
    main()
