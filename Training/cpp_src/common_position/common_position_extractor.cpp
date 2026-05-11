#include "common_position_extractor.hpp"
#include "bagz_reader_impl.hpp"
#include "file_operations_impl.hpp"
#include "../utils/file_utils.hpp"
#include "../utils/stopwatch.hpp"
#include <fstream>
#include <algorithm>
#include <future>
#include <atomic>
#include <chrono>
#include <thread>
#include <simdjson.h>

namespace chessmimic {
CommonPositionExtractor::CommonPositionExtractor(Config config)
  : config_(std::move(config)),
    reader_factory_(std::make_unique<BagzReaderFactory>(config_.bagz_path)),
    file_ops_(std::make_unique<FileOperationsImpl>()),
    thread_pool_(std::make_shared<ThreadPool>(config_.num_threads)),
    logger_(std::make_shared<Logger>()) {
  ensureTempDirectory();
  CM_LOG_INFO("Initialized with {} threads, chunk size: {}",
              config_.num_threads, config_.chunk_size);
}

CommonPositionExtractor::CommonPositionExtractor(Config config,
                                                 std::unique_ptr<IBagzReaderFactory> reader_factory,
                                                 std::unique_ptr<IFileOperations> file_ops,
                                                 std::shared_ptr<ThreadPool> thread_pool,
                                                 std::shared_ptr<Logger> logger)
  : config_(std::move(config)),
    reader_factory_(std::move(reader_factory)),
    file_ops_(std::move(file_ops)),
    thread_pool_(std::move(thread_pool)),
    logger_(std::move(logger)) {
  ensureTempDirectory();
}

CommonPositionExtractor::~CommonPositionExtractor() {
  if (!config_.keep_temp_files) {
    cleanupTempFiles();
  }
}

void CommonPositionExtractor::ensureTempDirectory() {
  if (config_.temp_dir.empty()) {
    config_.temp_dir = std::filesystem::temp_directory_path() /
                       ("common_pos_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
  }
  std::filesystem::create_directories(config_.temp_dir);
}

void CommonPositionExtractor::cleanupTempFiles() const {
  if (std::filesystem::exists(config_.temp_dir)) {
    std::filesystem::remove_all(config_.temp_dir);
  }
}

void CommonPositionExtractor::extract() {
  auto overall_start = std::chrono::steady_clock::now();

  CM_LOG_INFO("Extracting common positions from {} to {}",
              config_.bagz_path, config_.output_path);

  SCOPED_TIMER("Total extraction time");

  // Phase 1: Process bagz file in chunks
  auto chunk_files = processChunks();

  // Phase 2: Sort chunks by FEN
  auto sorted_files = sortChunks(chunk_files);

  // Phase 3: Merge sorted chunks and filter by threshold
  mergeAndFilter(sorted_files);

  auto overall_end = std::chrono::steady_clock::now();
  auto total_seconds = std::chrono::duration_cast<std::chrono::seconds>(overall_end - overall_start).count();

  int hours = total_seconds / 3600;
  int minutes = (total_seconds % 3600) / 60;
  int seconds = total_seconds % 60;

  CM_LOG_INFO("Extraction complete in {}h {}m {}s", hours, minutes, seconds);
}

std::vector<std::string> CommonPositionExtractor::processChunks() {
  SCOPED_TIMER("Phase 1: Process chunks");

  size_t total_records = reader_factory_->size();

  // Limit records if specified
  if (config_.max_records > 0 && config_.max_records < total_records) {
    total_records = config_.max_records;
    CM_LOG_INFO("Processing {} records (limited from {}) in chunks of {}",
                total_records, reader_factory_->size(), config_.chunk_size);
  }
  else {
    CM_LOG_INFO("Processing {} records in chunks of {}", total_records, config_.chunk_size);
  }

  std::vector<std::future<std::string> > futures;
  std::atomic<size_t> chunks_completed{0};
  std::atomic<size_t> records_processed{0};
  size_t total_chunks = (total_records + config_.chunk_size - 1) / config_.chunk_size;

  CM_LOG_INFO("Creating {} chunks for {} threads", total_chunks, config_.num_threads);

  auto start_time = std::chrono::steady_clock::now();

  for (size_t chunk_start = 0; chunk_start < total_records; chunk_start += config_.chunk_size) {
    size_t chunk_end = std::min(chunk_start + config_.chunk_size, total_records);

    futures.push_back(thread_pool_->enqueue([this, chunk_start, chunk_end, &chunks_completed, &records_processed,
      total_chunks, total_records, start_time] {
      std::string chunk_file = processChunk(chunk_start, chunk_end);

      size_t completed = ++chunks_completed;

      // Update progress every 10 chunks or at milestones
      if (size_t records_done = records_processed.fetch_add(chunk_end - chunk_start) + (chunk_end - chunk_start);
        completed % 10 == 0 || completed == total_chunks ||
        records_done % 10'000'000 == 0) {
        auto current_time = std::chrono::steady_clock::now();

        if (auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count();
          elapsed > 0 && records_done > 0) {
          double rate = static_cast<double>(records_done) / elapsed;
          double remaining_seconds = (total_records - records_done) / rate;

          int hours = static_cast<int>(remaining_seconds / 3600);
          int minutes = static_cast<int>((remaining_seconds - hours * 3600) / 60);
          int seconds = static_cast<int>(remaining_seconds - hours * 3600 - minutes * 60);

          CM_LOG_INFO("Processed {}/{} chunks ({}/{} records, {}%) - Rate: {} rec/s - ETA: {}h {}m {}s",
                      completed, total_chunks, records_done, total_records,
                      static_cast<int>(100.0 * records_done / total_records),
                      static_cast<int>(rate),
                      hours, minutes, seconds);
        }
        else {
          CM_LOG_INFO("Processed {}/{} chunks ({}/{} records, {}%)",
                      completed, total_chunks, records_done, total_records,
                      static_cast<int>(100.0 * records_done / total_records));
        }
      }

      return chunk_file;
    }));
  }

  std::vector<std::string> chunk_files;
  for (auto& future: futures) {
    chunk_files.push_back(future.get());
  }

  return chunk_files;
}

std::string CommonPositionExtractor::processChunk(size_t start_idx, size_t end_idx) const {
  std::unordered_map<std::string, PositionData> fen_map;

  // Get thread-local reader ONCE for the entire chunk
  auto* reader = getThreadLocalReader();

  for (size_t i = start_idx; i < end_idx; ++i) {
    try {
      // Use the cached reader - no mutex acquisition in hot loop
      std::vector<uint8_t> record_bytes = reader->get_record(i);
      std::string record_str(record_bytes.begin(), record_bytes.end());

      auto pos_data = extractPositionFromRecord(record_str);

      // Aggregate with existing data for this FEN
      if (auto& existing = fen_map[pos_data.fen]; existing.fen.empty()) {
        existing = pos_data;
      }
      else {
        aggregatePosition(existing, pos_data);
      }
    } catch (const std::exception& e) {
      CM_LOG_WARNING("Error processing record {}: {}", i, e.what());
    }
  }

  // Write chunk to file
  std::string chunk_file = config_.temp_dir + "/chunk_" +
                           std::to_string(start_idx) + ".jsonl";

  // Clear file first
  std::ofstream clear(chunk_file, std::ios::trunc);
  clear.close();

  for (const auto& pos_data: fen_map | std::views::values) {
    if (pos_data.total > 0) {
      file_ops_->writeJsonLine(chunk_file, pos_data.toJson());
    }
  }

  return chunk_file;
}

PositionData CommonPositionExtractor::extractPositionFromRecord(const std::string& record_str) {
  // Create thread-local parser for better performance
  thread_local simdjson::dom::parser parser;
  simdjson::dom::element doc = parser.parse(record_str);

  std::string fen = extractFenFromRecord(record_str);

  PositionData pos_data;
  pos_data.fen = fen;

  // Extract moves and aggregate counts
  for (auto moves_obj = doc["moves"].get_object(); auto [move_key, clock_data]: moves_obj) {
    std::string move(move_key);

    for (auto clock_obj = clock_data.get_object(); auto [clock_key, rating_data]: clock_obj) {
      for (auto rating_obj = rating_data.get_object(); auto [rating_key, count]: rating_obj) {
        int64_t count_val = count.get_int64();
        pos_data.moves[move] += count_val;
        pos_data.total += count_val;
      }
    }
  }

  return pos_data;
}

std::string CommonPositionExtractor::extractFenFromRecord(const std::string& record_json) {
  // The record has structure: {"recent_and_fen": [recent_moves, fen], "moves": {...}}
  thread_local simdjson::dom::parser parser;
  simdjson::dom::element doc = parser.parse(record_json);

  auto recent_and_fen = doc["recent_and_fen"].get_array();
  // Skip first element (recent moves), get second element (FEN)
  auto fen_iter = recent_and_fen.begin();
  ++fen_iter; // Move to second element
  std::string full_fen((*fen_iter).get_string().value());

  // Strip halfmove clock and fullmove number from FEN
  // FEN format: position active_color castling en_passant halfmove fullmove
  // We want to keep only the first 4 parts for position comparison
  size_t space_count = 0;
  size_t pos = 0;
  for (size_t i = 0; i < full_fen.length(); ++i) {
    if (full_fen[i] == ' ') {
      space_count++;
      if (space_count == 4) {
        pos = i;
        break;
      }
    }
  }

  // Validate that we found a proper FEN with at least 4 spaces
  if (space_count < 4) {
    throw std::runtime_error("Invalid FEN format - expected at least 4 spaces, found " +
                             std::to_string(space_count) + " in: " + full_fen);
  }

  return full_fen.substr(0, pos);
}

void CommonPositionExtractor::aggregatePosition(PositionData& target, const PositionData& source) {
  for (const auto& [move, count]: source.moves) {
    target.moves[move] += count;
    target.total += count;
  }
}

std::vector<std::string> CommonPositionExtractor::sortChunks(const std::vector<std::string>& chunk_files) {
  SCOPED_TIMER("Phase 2: Sort chunks");

  CM_LOG_INFO("Sorting {} chunks by FEN", chunk_files.size());

  std::vector<std::future<std::string> > futures;
  std::atomic<size_t> chunks_sorted{0};
  auto start_time = std::chrono::steady_clock::now();

  for (size_t i = 0; i < chunk_files.size(); ++i) {
    futures.push_back(thread_pool_->enqueue([this, i, &chunk_files, &chunks_sorted, start_time] {
      std::string sorted_file = sortChunk(chunk_files[i], i);

      if (size_t completed = ++chunks_sorted; completed % 10 == 0 || completed == chunk_files.size()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::steady_clock::now() - start_time).count();

        if (elapsed > 0 && completed > 0) {
          double rate = static_cast<double>(completed) / elapsed;
          double remaining_seconds = (chunk_files.size() - completed) / rate;
          int minutes = static_cast<int>(remaining_seconds / 60);
          int seconds = static_cast<int>(remaining_seconds - minutes * 60);

          CM_LOG_INFO("Sorted {}/{} chunks ({}%) - ETA: {}m {}s",
                      completed, chunk_files.size(),
                      static_cast<int>(100.0 * completed / chunk_files.size()),
                      minutes, seconds);
        }
        else {
          CM_LOG_INFO("Sorted {}/{} chunks ({}%)",
                      completed, chunk_files.size(),
                      static_cast<int>(100.0 * completed / chunk_files.size()));
        }
      }

      return sorted_file;
    }));
  }

  std::vector<std::string> sorted_files;
  for (auto& future: futures) {
    sorted_files.push_back(future.get());
  }

  return sorted_files;
}

std::string CommonPositionExtractor::sortChunk(const std::string& chunk_file, size_t chunk_idx) {
  auto positions = loadPositionsFromFile(chunk_file);

  // Sort by FEN
  std::ranges::sort(positions,
                    [](const PositionData& a, const PositionData& b) {
                      return a.fen < b.fen;
                    });

  // Write sorted chunk
  std::string sorted_file = config_.temp_dir + "/sorted_" +
                            std::to_string(chunk_idx) + ".jsonl";

  savePositionsToFile(sorted_file, positions);

  // Remove original chunk if not keeping temp files
  if (!config_.keep_temp_files) {
    file_ops_->removeFile(chunk_file);
  }

  return sorted_file;
}

std::vector<PositionData> CommonPositionExtractor::loadPositionsFromFile(const std::string& path) const {
  std::vector<PositionData> positions;

  for (auto lines = file_ops_->readJsonLines(path); const auto& line: lines) {
    positions.push_back(PositionData::fromJson(line));
  }

  return positions;
}

void CommonPositionExtractor::savePositionsToFile(const std::string& path,
                                                  const std::vector<PositionData>& positions) const {
  // Clear file first
  std::ofstream clear(path, std::ios::trunc);
  clear.close();

  for (const auto& pos: positions) {
    file_ops_->writeJsonLine(path, pos.toJson());
  }
}

void CommonPositionExtractor::mergeAndFilter(const std::vector<std::string>& sorted_files) {
  SCOPED_TIMER("Phase 3: Merge and filter");

  CM_LOG_INFO("Merging {} sorted files and filtering by threshold {}",
              sorted_files.size(), config_.threshold);

  // Read all lines from all files
  std::vector<std::vector<std::string> > file_lines;
  for (const auto& file: sorted_files) {
    file_lines.push_back(file_ops_->readJsonLines(file));
  }

  // Use internal method that works with lines
  mergeAndFilterInternal(file_lines);

  // Cleanup sorted files if not keeping temp files
  if (!config_.keep_temp_files) {
    for (const auto& file: sorted_files) {
      file_ops_->removeFile(file);
    }
  }
}

void CommonPositionExtractor::mergeAndFilterInternal(const std::vector<std::vector<std::string> >& file_lines) {
  // Create position iterators for each file
  std::vector<size_t> indices(file_lines.size(), 0);
  std::vector<std::unique_ptr<PositionData> > current_positions(file_lines.size());

  // Load initial position from each file
  for (size_t i = 0; i < file_lines.size(); ++i) {
    if (indices[i] < file_lines[i].size()) {
      current_positions[i] = std::make_unique<PositionData>(PositionData::fromJson(file_lines[i][indices[i]]));
    }
  }

  // Clear output file
  std::ofstream clear(config_.output_path, std::ios::trunc);
  clear.close();

  size_t positions_written = 0;
  size_t positions_processed = 0;

  // Merge positions
  while (true) {
    // Find minimum FEN among current positions
    std::string min_fen;
    bool found_any = false;

    for (size_t i = 0; i < current_positions.size(); ++i) {
      if (current_positions[i]) {
        if (!found_any || current_positions[i]->fen < min_fen) {
          min_fen = current_positions[i]->fen;
          found_any = true;
        }
      }
    }

    if (!found_any) break;

    // Aggregate all positions with this FEN
    PositionData merged;
    merged.fen = min_fen;

    for (size_t i = 0; i < current_positions.size(); ++i) {
      if (current_positions[i] && current_positions[i]->fen == min_fen) {
        // Merge moves
        aggregatePosition(merged, *current_positions[i]);

        // Load next position from this file
        indices[i]++;
        if (indices[i] < file_lines[i].size()) {
          current_positions[i] = std::make_unique<PositionData>(PositionData::fromJson(file_lines[i][indices[i]]));
        }
        else {
          current_positions[i] = nullptr;
        }
      }
    }

    positions_processed++;

    // Write if exceeds threshold
    if (merged.total > config_.threshold) {
      file_ops_->writeJsonLine(config_.output_path, merged.toJson());
      positions_written++;
    }

    if (positions_processed % 100000 == 0) {
      CM_LOG_INFO("Processed {} positions, wrote {} exceeding threshold",
                  positions_processed, positions_written);
    }
  }

  CM_LOG_INFO("Final: Processed {} unique positions, wrote {} exceeding threshold {}",
              positions_processed, positions_written, config_.threshold);
}

IBagzReader* CommonPositionExtractor::getThreadLocalReader() const {
  auto thread_id = std::this_thread::get_id();

  std::lock_guard lock(pool_mutex_);
  auto it = reader_pool_.find(thread_id);
  if (it == reader_pool_.end()) {
    // Create a new reader for this thread
    auto reader = reader_factory_->createReader();
    auto* reader_ptr = reader.get();
    reader_pool_[thread_id] = std::move(reader);
    return reader_ptr;
  }

  return it->second.get();
}
} // namespace chessmimic
