#!/bin/bash
# Batch processing script for large datasets
# Processes data in chunks to manage memory and disk usage

set -e  # Exit on error

# Configuration
RATING_BANDS="1600_1700 1700_1800 1800_1900"
MONTHS="202409 202410 202411 202412"
DATA_ROOT="/data/chess/split_pgn"
OUTPUT_ROOT="/data/chess/clock_bagz"
TEMP_DIR="/fast/ssd/temp"  # Use fast SSD for temporary files
THREADS=16  # Reduced for batch processing
BATCH_SIZE=100  # Process 100 files at a time

# Function to process a batch of files
process_batch() {
    local rating=$1
    local month=$2
    local batch_num=$3
    local files=("${@:4}")
    
    echo "Processing batch ${batch_num} for ${rating}/${month}..."
    
    # Create temporary output file
    local temp_output="${TEMP_DIR}/clock_${rating}_${month}_batch${batch_num}.bagz"
    
    # Process the batch
    ./build/pgn_to_clock_bagz \
        "${files[@]}" \
        --output "${temp_output}" \
        --threads ${THREADS} \
        --temp-dir ${TEMP_DIR}
    
    echo "Batch ${batch_num} complete: ${temp_output}"
}

# Ensure directories exist
mkdir -p ${OUTPUT_ROOT}
mkdir -p ${TEMP_DIR}

# Build the converter if needed
if [ ! -f "build/pgn_to_clock_bagz" ]; then
    echo "Building pgn_to_clock_bagz..."
    ./build_cpp_ext.sh
fi

# Process each rating band
for rating in ${RATING_BANDS}; do
    echo "=== Processing rating band: ${rating} ==="
    
    # Process each month
    for month in ${MONTHS}; do
        echo "Processing month: ${month}"
        
        # Get all files for this month
        files=(${DATA_ROOT}/${rating}/${month}/*.pgn.lz4)
        total_files=${#files[@]}
        
        if [ ${total_files} -eq 0 ]; then
            echo "No files found for ${rating}/${month}"
            continue
        fi
        
        echo "Found ${total_files} files"
        
        # Process in batches
        batch_num=1
        for ((i=0; i<${total_files}; i+=${BATCH_SIZE})); do
            # Get files for this batch
            end=$((i + BATCH_SIZE))
            if [ ${end} -gt ${total_files} ]; then
                end=${total_files}
            fi
            
            batch_files=("${files[@]:${i}:${BATCH_SIZE}}")
            
            # Process the batch
            process_batch ${rating} ${month} ${batch_num} "${batch_files[@]}"
            
            ((batch_num++))
        done
        
        # Merge all batches for this month
        echo "Merging batches for ${rating}/${month}..."
        output_file="${OUTPUT_ROOT}/clock_${rating}_${month}.bagz"
        
        # Use Python to merge BAGZ files
        python3 << EOF
from bagz import BagReader, BagWriter
import glob
import os

batch_files = sorted(glob.glob('${TEMP_DIR}/clock_${rating}_${month}_batch*.bagz'))
print(f"Merging {len(batch_files)} batch files...")

with BagWriter('${output_file}') as writer:
    total_records = 0
    for batch_file in batch_files:
        reader = BagReader(batch_file)
        for record in reader.read_records():
            writer.write(record)
            total_records += 1
        print(f"  Processed {batch_file}: {total_records:,} total records")

print(f"Merged {total_records:,} records to ${output_file}")

# Clean up batch files
for batch_file in batch_files:
    os.remove(batch_file)
    print(f"  Removed {batch_file}")
EOF
        
        echo "Month ${month} complete!"
        echo ""
    done
    
    # Merge all months for this rating band
    echo "Merging all months for ${rating}..."
    final_output="${OUTPUT_ROOT}/clock_${rating}_all.bagz"
    
    python3 << EOF
from bagz import BagReader, BagWriter
import glob

month_files = sorted(glob.glob('${OUTPUT_ROOT}/clock_${rating}_*.bagz'))
# Exclude the 'all' file if it exists
month_files = [f for f in month_files if not f.endswith('_all.bagz')]

print(f"Merging {len(month_files)} monthly files...")

with BagWriter('${final_output}') as writer:
    total_records = 0
    for month_file in month_files:
        reader = BagReader(month_file)
        count = sum(1 for record in reader.read_records() if writer.write(record) or True)
        total_records += count
        print(f"  Processed {month_file}: {count:,} records")

print(f"Created ${final_output} with {total_records:,} total records")
EOF
    
    echo "Rating band ${rating} complete!"
    echo ""
done

# Final statistics
echo "=== Final Statistics ==="
python3 << EOF
from bagz import BagReader
import glob

all_files = sorted(glob.glob('${OUTPUT_ROOT}/clock_*_all.bagz'))

for bagz_file in all_files:
    reader = BagReader(bagz_file)
    count = sum(1 for _ in reader.read_records())
    print(f"{bagz_file}: {count:,} records")
EOF

echo ""
echo "Batch processing complete!"
echo "Output files in: ${OUTPUT_ROOT}"