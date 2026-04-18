#!/usr/bin/env python3
import gzip
import json
import base64
import numpy as np
import argparse
import sys
import os
import struct

def get_quant_params(product_type):
    if product_type == "velocity" or product_type == "super_res_velocity":
        return {"min": -100.0, "max": 100.0}
    elif product_type == "spectrum_width":
        return {"min": 0.0, "max": 64.0}
    elif product_type == "differential_reflectivity":
        return {"min": -8.0, "max": 8.0}
    elif product_type == "specific_differential_phase" or product_type == "differential_phase":
        return {"min": 0.0, "max": 360.0}
    elif product_type == "cross_correlation_ratio" or product_type == "correlation_coefficient":
        return {"min": 0.0, "max": 1.1}
    elif product_type == "echo_tops" or product_type == "enhanced_echo_tops":
        return {"min": 0.0, "max": 230.0}
    elif product_type == "vil":
        return {"min": 0.0, "max": 75.0}
    elif product_type == "precipitation":
        return {"min": 0.0, "max": 24.0}
    return {"min": -32.0, "max": 95.0}

def decode_bitmask_format(binary_data, ray_count, gate_count, min_val, max_val):
    total_bits = ray_count * gate_count
    bitmask_bytes_count = (total_bits + 7) // 8

    bitmask = np.frombuffer(binary_data[:bitmask_bytes_count], dtype=np.uint8)
    packed_values = np.frombuffer(binary_data[bitmask_bytes_count:], dtype=np.uint8)

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
                    grid[r, g] = min_val + (val_quant / 255.0) * (max_val - min_val)
                    value_idx += 1

    return grid

def build_output(meta, grid, sparse=True):
    output = {}

    # Metadata block
    output["metadata"] = meta

    # Grid info
    output["grid"] = {
        "rays": int(grid.shape[0]),
        "gates": int(grid.shape[1])
    }

    # Stats
    nonzero = grid[grid != 0]
    output["stats"] = {
        "nonzero_count": int(nonzero.size),
        "min": float(np.min(nonzero)) if nonzero.size else 0,
        "max": float(np.max(nonzero)) if nonzero.size else 0,
        "mean": float(np.mean(nonzero)) if nonzero.size else 0
    }

    # Data
    if sparse:
        bins = []
        for r in range(grid.shape[0]):
            for g in range(grid.shape[1]):
                val = grid[r, g]
                if val != 0:
                    bins.append({
                        "ray": r,
                        "gate": g,
                        "value": float(val)
                    })
        output["data"] = {
            "type": "sparse",
            "bins": bins
        }
    else:
        output["data"] = {
            "type": "full",
            "values": grid.tolist()
        }

    return output

def main():
    parser = argparse.ArgumentParser(description='Convert radar data to structured text')
    parser.add_argument('file', help='Input .RDA file')
    parser.add_argument('--output', default='radar_output.json', help='Output file')
    parser.add_argument('--format', choices=['json', 'txt'], default='json')
    parser.add_argument('--full', action='store_true', help='Write full grid instead of sparse')

    args = parser.parse_args()

    if not os.path.exists(args.file):
        print(f"File not found: {args.file}")
        sys.exit(1)

    # Read file
    with gzip.open(args.file, 'rb') as f:
        raw_content = f.read()

    # Detect format
    is_new_format = False
    if len(raw_content) > 4:
        meta_size = struct.unpack('<I', raw_content[:4])[0]
        if meta_size < len(raw_content) and meta_size < 65536:
            try:
                meta = json.loads(raw_content[4:4+meta_size].decode('utf-8'))
                if 'f' in meta:
                    data_body = raw_content[4+meta_size:]
                    is_new_format = True
            except:
                pass

    if not is_new_format:
        meta = json.loads(raw_content.decode('utf-8'))
        data_body = base64.b64decode(meta.get('d', ''))

    if meta.get('f') != 'b':
        print("Unsupported format (only 'b' supported)")
        sys.exit(1)

    ray_count = meta.get('r', 720)
    gate_count = meta.get('g', 0)
    
    # Use quant range from metadata if available, otherwise derive from product type
    if 'qmin' in meta and 'qmax' in meta:
        min_val = meta['qmin']
        max_val = meta['qmax']
    else:
        params = get_quant_params(meta.get('p', ''))
        min_val = params["min"]
        max_val = params["max"]

    grid = decode_bitmask_format(
        data_body,
        ray_count,
        gate_count,
        min_val,
        max_val
    )

    structured = build_output(meta, grid, sparse=not args.full)

    # Write output
    if args.format == "json":
        with open(args.output, 'w') as f:
            json.dump(structured, f, indent=2)
    else:
        with open(args.output, 'w') as f:
            f.write("=== METADATA ===\n")
            f.write(json.dumps(structured["metadata"], indent=2))
            f.write("\n\n=== STATS ===\n")
            f.write(json.dumps(structured["stats"], indent=2))
            f.write("\n\n=== DATA ===\n")

            if structured["data"]["type"] == "sparse":
                for b in structured["data"]["bins"]:
                    f.write(f"ray={b['ray']} gate={b['gate']} value={b['value']}\n")
            else:
                f.write(json.dumps(structured["data"]["values"], indent=2))

    print(f"Wrote structured output to {args.output}")

if __name__ == "__main__":
    main()
