#!/usr/bin/env python
import sys
import argparse
from bagz import BagReader

def validate_bagz_file(bagz_path):
    """Validate that all records in a bagz file are non-empty."""
    print(f"Validating records in {bagz_path}...")
    
    total_records = 0
    empty_records = 0
    empty_record_indices = []
    
    for i, record in enumerate(BagReader(bagz_path)):
        total_records += 1
        
        if not record:
            empty_records += 1
            empty_record_indices.append(i)
            print(f"Found empty record at index {i}")
            
            # Print some context around the empty record
            if i > 0:
                try:
                    prev_record = BagReader(bagz_path)[i-1]
                    print(f"Previous record ({i-1}) length: {len(prev_record)}")
                    if len(prev_record) < 200:
                        print(f"Previous record content: {prev_record}")
                    else:
                        print(f"Previous record preview: {prev_record[:200]}...")
                except Exception as e:
                    print(f"Error reading previous record: {e}")
            
            # Try to read the next record
            try:
                next_record = BagReader(bagz_path)[i+1]
                print(f"Next record ({i+1}) length: {len(next_record)}")
                if len(next_record) < 200:
                    print(f"Next record content: {next_record}")
                else:
                    print(f"Next record preview: {next_record[:200]}...")
            except Exception as e:
                print(f"Error reading next record: {e}")
                
            if empty_records >= 10:
                print(f"Found 10 empty records, stopping validation...")
                break
    
    print(f"\nValidation results for {bagz_path}:")
    print(f"Total records: {total_records}")
    print(f"Empty records: {empty_records}")
    
    if empty_records > 0:
        print(f"Empty record indices: {empty_record_indices}")
        return False
    else:
        print("All records are non-empty. Validation successful!")
        return True

def main():
    parser = argparse.ArgumentParser(description='Validate bagz file records')
    parser.add_argument('bagz_file', help='Path to the bagz file to validate')
    parser.add_argument('--detailed', action='store_true', help='Print detailed information about records')
    
    args = parser.parse_args()
    
    if args.detailed:
        print(f"Detailed validation of {args.bagz_file}:")
        reader = BagReader(args.bagz_file)
        total_size = len(reader)
        print(f"Total records according to BagReader: {total_size}")
        
        # Validate the first few and last few records
        for i in list(range(min(5, total_size))) + list(range(max(0, total_size-5), total_size)):
            try:
                record = reader[i]
                print(f"Record {i} length: {len(record)}")
                if not record:
                    print(f"⚠️ Record {i} is empty!")
            except Exception as e:
                print(f"Error reading record {i}: {e}")
    
    # Perform validation
    success = validate_bagz_file(args.bagz_file)
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()