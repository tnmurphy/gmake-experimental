#!/usr/bin/env python3
import json
import sys

def extract_keys(data, depth, current_depth=0):
    if current_depth >= depth or not isinstance(data, dict):
        return {}
    result = {}
    for key, value in data.items():
        if isinstance(value, dict):
            result[key] = extract_keys(value, depth, current_depth + 1)
        else:
            result[key] = None
    return result

def main():
    if len(sys.argv) < 2:
        print("Usage: python extract_keys.py <json_file> [depth]")
        sys.exit(1)

    json_file = sys.argv[1]
    depth = int(sys.argv[2]) if len(sys.argv) > 2 else 1

    try:
        with open(json_file, 'r') as f:
            data = json.load(f)
    except Exception as e:
        print(f"Error loading JSON file: {e}")
        sys.exit(1)

    keys_structure = extract_keys(data, depth)
    print(json.dumps(keys_structure, indent=2))

if __name__ == "__main__":
    main()
