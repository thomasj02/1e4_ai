import os
import sys
import re
import lz4.frame
import zstandard as zstd


def main(args):
    # Determine output directory
    if args.auto_output_dir:
        # Extract date from filename (e.g., lichess_db_standard_rated_2024-05.pgn.zst)
        if args.pgn == "-":
            print("Error: --auto_output_dir cannot be used with stdin input")
            sys.exit(1)
        
        # Look for pattern YYYY-MM in the filename
        match = re.search(r'(\d{4})-(\d{2})', os.path.basename(args.pgn))
        if not match:
            print("Error: Could not extract date from filename. Expected format like 'lichess_db_standard_rated_2024-05.pgn.zst'")
            sys.exit(1)
        
        year, month = match.groups()
        date_suffix = f"{year}{month}"  # Convert 2024-05 to 202405
        
        # Create directory name: split_pgn_{min_rating}_{max_rating}_{date}
        dir_name = f"split_pgn_{args.min_rating}_{args.max_rating}_{date_suffix}"
        output_dir = os.path.join(args.auto_output_dir, dir_name)
    else:
        output_dir = args.output_dir
    
    os.makedirs(output_dir, exist_ok=True)
    output_files = [lz4.frame.open(os.path.join(output_dir, f'output_{i}.pgn.lz4'), 'wb') for i in range(args.num_files)]

    game_cnt = 0
    output_file_idx = 0
    write_game = True
    if args.pgn == "-":
        f = sys.stdin
    else:
        if args.pgn.endswith('.zst'):
            f = zstd.open(args.pgn, 'rt')
        else:
            f = open(args.pgn)

    game_text = ''
    for line in f:
        if line.startswith('[Event'):
            if game_text and write_game:
                output_files[output_file_idx].write(game_text.encode())
                output_file_idx = (output_file_idx + 1) % args.num_files
                game_cnt += 1
                if game_cnt % 1000 == 0:
                    print(f'Processed {game_cnt} games')
            game_text = ''
            write_game = True if line.startswith('[Event \"Rated Blitz game\"]') else False
        if write_game:
            if line.startswith('[WhiteElo'):
                white_elo = int(line.split()[1][1:-2])
                if white_elo < args.min_rating or white_elo > args.max_rating:
                    write_game = False
                    continue
            if line.startswith('[BlackElo'):
                black_elo = int(line.split()[1][1:-2])
                if black_elo < args.min_rating or black_elo > args.max_rating:
                    write_game = False
                    continue
        game_text += line
    if game_text and write_game:
        output_files[output_file_idx].write(game_text.encode())
    
    # Close the input file if it's not stdin
    if args.pgn != "-":
        f.close()
    
    # Close all output files
    for output_file in output_files:
        output_file.close()


if __name__ == '__main__':
    import argparse

    parser = argparse.ArgumentParser(description='Split PGN file multiple smaller files')
    parser.add_argument('--pgn', type=str, help='PGN file to process (supports .zst compressed files)', required=True)
    parser.add_argument('--min_rating', type=int, help='Minimum rating to include', default=0)
    parser.add_argument('--max_rating', type=int, help='Maximum rating to include', default=10000)
    
    # Make output_dir and auto_output_dir mutually exclusive
    output_group = parser.add_mutually_exclusive_group(required=True)
    output_group.add_argument('--output_dir', type=str, help='Output directory')
    output_group.add_argument('--auto_output_dir', type=str, 
                            help='Base directory for auto-generated output path (e.g., data/). '
                                 'Will create subdirectory split_pgn_{min_rating}_{max_rating}_{YYYYMM} based on input filename')
    
    parser.add_argument('--num_files', type=int, help='Number of files to split into', required=True)
    _args = parser.parse_args()

    main(_args)
