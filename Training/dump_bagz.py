#!/usr/bin/env python3
"""
Dump raw records from a BAGZ file without any formatting or structure assumptions.
"""

import argparse
import sys
from bagz import BagReader


def dump_bagz_records(bagz_path, start=0, limit=None, raw=False, show_index=True):
    """Dump raw records from a bagz file.
    
    Args:
        bagz_path (str): Path to the bagz file
        start (int): Starting index (0-based)
        limit (int, optional): Maximum number of records to dump
        raw (bool): If True, print raw bytes as hex, otherwise decode as UTF-8
        show_index (bool): If True, prefix each record with its index
    """
    try:
        # Create bagz reader
        reader = BagReader(bagz_path, separate_limits=False)
        
        # Get total number of records
        total_records = len(reader)
        print(f"Total records in bagz file: {total_records}", file=sys.stderr)
        
        if start >= total_records:
            print(f"Error: Start index {start} is >= total records {total_records}", file=sys.stderr)
            return 1
        
        # Determine how many records to dump
        end = min(start + limit, total_records) if limit else total_records
        
        print(f"Dumping records {start} to {end-1} ({end-start} records)", file=sys.stderr)
        print("-" * 80, file=sys.stderr)
        
        # Dump records
        for i in range(start, end):
            try:
                record = reader[i]
                
                if show_index:
                    print(f"[Record {i}]")
                
                if raw:
                    # Print as hex dump
                    hex_str = record.hex()
                    # Format hex dump in 16-byte lines
                    for j in range(0, len(hex_str), 32):
                        hex_line = hex_str[j:j+32]
                        # Add spaces between byte pairs
                        formatted = ' '.join(hex_line[k:k+2] for k in range(0, len(hex_line), 2))
                        print(formatted)
                else:
                    # Try to decode as UTF-8 string
                    try:
                        text = record.decode('utf-8')
                        print(text)
                    except UnicodeDecodeError:
                        print(f"[Binary data - {len(record)} bytes]")
                        # Fall back to hex for non-UTF8 data
                        hex_str = record.hex()
                        for j in range(0, len(hex_str), 32):
                            hex_line = hex_str[j:j+32]
                            formatted = ' '.join(hex_line[k:k+2] for k in range(0, len(hex_line), 2))
                            print(formatted)
                
                if show_index and i < end - 1:
                    print()  # Blank line between records
                    
            except Exception as e:
                print(f"Error reading record {i}: {str(e)}", file=sys.stderr)
                
        return 0
        
    except Exception as e:
        print(f"Error opening bagz file: {str(e)}", file=sys.stderr)
        return 1


def main():
    parser = argparse.ArgumentParser(
        description='Dump raw records from a BAGZ file.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Dump first 10 records
  dump_bagz.py input.bagz --limit 10
  
  # Dump all records starting from index 100
  dump_bagz.py input.bagz --start 100
  
  # Dump records 50-59 as hex
  dump_bagz.py input.bagz --start 50 --limit 10 --raw
  
  # Dump all records without index labels
  dump_bagz.py input.bagz --no-index
""")
    
    parser.add_argument('bagz_path', help='Path to the BAGZ file')
    parser.add_argument('--start', type=int, default=0, 
                        help='Starting record index (0-based, default: 0)')
    parser.add_argument('--limit', type=int, 
                        help='Maximum number of records to dump (default: all)')
    parser.add_argument('--raw', action='store_true',
                        help='Print raw bytes as hex instead of decoding as UTF-8')
    parser.add_argument('--no-index', action='store_true',
                        help='Do not prefix records with their index')
    
    args = parser.parse_args()
    
    return dump_bagz_records(
        bagz_path=args.bagz_path,
        start=args.start,
        limit=args.limit,
        raw=args.raw,
        show_index=not args.no_index
    )


if __name__ == "__main__":
    sys.exit(main())