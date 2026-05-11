#!/usr/bin/env python
import argparse
import orjson
from collections import Counter
from bagz import BagReader

def load_bagz_data(bagz_path, compare_values=False):
    """Load data from a bagz file.
    
    Args:
        bagz_path: Path to the bagz file
        compare_values: If True, return both keys and values, otherwise just keys
    
    Returns:
        If compare_values is False: list of keys
        If compare_values is True: dict mapping keys to parsed JSON objects
    """
    result = {} if compare_values else []
    errors = 0
    
    for i, record in enumerate(BagReader(bagz_path)):
        try:
            # Parse JSON once
            data = orjson.loads(record)
            key = orjson.dumps(data["recent_and_fen"]).decode('utf-8')
            
            if compare_values:
                # Store the already parsed data instead of raw string
                result[key] = data
            else:
                result.append(key)
                
        except Exception as e:
            errors += 1
            print(f"Error parsing record {i} in {bagz_path}: {str(e)}")
            print(f"Problem record: {record[:200]}...")
            if errors >= 5:
                print(f"Too many errors ({errors}), aborting...")
                break
    
    return result

def compare_records(data1, data2):
    """Compare two JSON objects and return differences."""
    diffs = []
    
    # Compare all keys except recent_and_fen (which we know are the same)
    all_keys = set(data1.keys()) | set(data2.keys())
    for k in all_keys:
        if k == "recent_and_fen":
            continue
            
        if k not in data1:
            diffs.append(f"Key '{k}' missing in first record")
        elif k not in data2:
            diffs.append(f"Key '{k}' missing in second record")
        elif data1[k] != data2[k]:
            diffs.append(f"Value differs for key '{k}': {data1[k]} vs {data2[k]}")
    
    return diffs

def main():
    parser = argparse.ArgumentParser(description='Compare two bagz files')
    parser.add_argument('file1', help='Python bagz file')
    parser.add_argument('file2', help='C++ bagz file')
    parser.add_argument('--compare-values', '-v', action='store_true', 
                        help='Compare values as well as keys')
    parser.add_argument('--sample-size', '-n', type=int, default=50,
                        help='Number of samples to show (default: 50)')
    args = parser.parse_args()
    
    python_bagz = args.file1
    cpp_bagz = args.file2
    
    print(f"Loading data from {python_bagz}...")
    python_data = load_bagz_data(python_bagz, args.compare_values)
    
    if args.compare_values:
        python_keys = list(python_data.keys())
    else:
        python_keys = python_data
        
    print(f"  Found {len(python_keys)} keys")
    
    print(f"Loading data from {cpp_bagz}...")
    cpp_data = load_bagz_data(cpp_bagz, args.compare_values)
    
    if args.compare_values:
        cpp_keys = list(cpp_data.keys())
    else:
        cpp_keys = cpp_data
        
    print(f"  Found {len(cpp_keys)} keys")
    
    # Convert to sets for comparison
    python_key_set = set(python_keys)
    cpp_key_set = set(cpp_keys)
    
    # Find unique keys in each file
    unique_to_python = python_key_set - cpp_key_set
    unique_to_cpp = cpp_key_set - python_key_set
    
    print(f"\nUnique to {python_bagz}: {len(unique_to_python)} keys")
    print(f"Unique to {cpp_bagz}: {len(unique_to_cpp)} keys")
    
    # Check for duplicates in each file
    python_dupes = [k for k, count in Counter(python_keys).items() if count > 1]
    cpp_dupes = [k for k, count in Counter(cpp_keys).items() if count > 1]
    
    print(f"\nDuplicates in {python_bagz}: {len(python_dupes)} keys")
    print(f"Duplicates in {cpp_bagz}: {len(cpp_dupes)} keys")
    
    # Sample some differences if there are any
    unique_to_python = sorted(list(unique_to_python))
    unique_to_cpp = sorted(list(unique_to_cpp))
    
    if unique_to_python:
        print(f"\nSample keys unique to {python_bagz}:")
        for key in unique_to_python[:args.sample_size]:
            print(f"  {key}")
    
    if unique_to_cpp:
        print(f"\nSample keys unique to {cpp_bagz}:")
        for key in unique_to_cpp[:args.sample_size]:
            print(f"  {key}")
    
    # If we're comparing values, check for differences in common keys
    if args.compare_values:
        common_keys = python_key_set & cpp_key_set
        diff_values = 0
        print("\nChecking for value differences in common keys...")
        
        # Sample some value differences
        value_diffs = []
        
        for key in common_keys:
            # Data is already parsed, just compare directly
            python_json = python_data[key]
            cpp_json = cpp_data[key]
            
            if python_json != cpp_json:
                diff_values += 1
                if len(value_diffs) < args.sample_size:
                    diffs = compare_records(python_json, cpp_json)
                    value_diffs.append((key, diffs))
        
        print(f"  Found {diff_values} keys with different values out of {len(common_keys)} common keys")
        
        if value_diffs:
            print("\nSample value differences:")
            for key, diffs in value_diffs:
                print(f"  Key: {key}")
                for diff in diffs:
                    print(f"    - {diff}")
                print()

if __name__ == "__main__":
    main()