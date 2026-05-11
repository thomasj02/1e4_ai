#include <nanobind/nanobind.h>
// ReSharper disable once CppUnusedIncludeDirective
#include <nanobind/stl/map.h>
#include <nanobind/stl/pair.h>
// ReSharper disable once CppUnusedIncludeDirective
#include <nanobind/stl/string.h>
// ReSharper disable once CppUnusedIncludeDirective
#include <nanobind/stl/vector.h>
// ReSharper disable once CppUnusedIncludeDirective
#include <nanobind/stl/set.h>
// ReSharper disable once CppUnusedIncludeDirective
#include <nanobind/stl/unordered_map.h>
#include <nanobind/ndarray.h>
#include <vector>
#include <string>
#include <cstring>
#include <unordered_map>
#include <set>
#include <array>
#include <sstream>
#include <algorithm>
#include <charconv>

#include "external/chess.hpp"
#include "core/bagz.hpp"
#include <simdjson.h>

namespace nb = nanobind;

// Simple function to test the setup
int add(int a, int b) {
  return a + b;
}

// Forward declarations for global access to maintain Python interface
std::vector<std::string> CHARACTERS;
std::unordered_map<std::string, int> CHARACTERS_INDEX;
std::set<std::string> SPACES_CHARACTERS;
int SEQUENCE_LENGTH;
int INPUT_VOCAB_SIZE;
int CLASS_TOKEN;
int PAD_TOKEN;
std::unordered_map<std::string, int> MOVE_TO_ACTION;
std::unordered_map<int, std::string> ACTION_TO_MOVE;
int NUM_ACTIONS;
// Constants for MoveDataset
constexpr int RECENT_MOVES_LENGTH = 12;

/**
 * ChessTokenizer class for tokenizing chess FEN strings
 */
class ChessTokenizer {
  public:
    /**
     * Constructor initializes all tokenizer constants and lookup tables
     */
    ChessTokenizer() {
      // Initialize character vocabulary
      characters_ = {
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
        "a", "b", "c", "d", "e", "f", "g", "h",
        "p", "n", "r", "k", "q",
        "P", "B", "N", "R", "Q", "K",
        "w", "."
      };

      // Initialize character index lookup
      characters_index_.clear();
      for (size_t i = 0; i < characters_.size(); i++) {
        characters_index_[characters_[i]] = static_cast<int>(i);
      }

      // Initialize other constants
      spaces_characters_ = {"1", "2", "3", "4", "5", "6", "7", "8"};
      sequence_length_ = 77 + 1; // +1 for the class token
      input_vocab_size_ = static_cast<int>(characters_.size()) + 2; // +1 for class token, +1 for pad token
      class_token_ = input_vocab_size_ - 2;
      pad_token_ = input_vocab_size_ - 1;

      // Initialize global variables to maintain Python interface
      CHARACTERS = characters_;
      CHARACTERS_INDEX = characters_index_;
      SPACES_CHARACTERS = spaces_characters_;
      SEQUENCE_LENGTH = sequence_length_;
      INPUT_VOCAB_SIZE = input_vocab_size_;
      CLASS_TOKEN = class_token_;
      PAD_TOKEN = pad_token_;
    }

    /**
     * Tokenize a FEN string into a numpy array of tokens
     */
    nb::ndarray<nb::numpy, uint8_t> tokenize(const std::string &fen) {
      // Initialize vector for indices
      std::vector<uint8_t> indices;
      indices.reserve(sequence_length_);

      // Split FEN string into components
      std::vector<std::string> fen_parts;
      std::istringstream fen_stream(fen);
      std::string part;
      while (fen_stream >> part) {
        fen_parts.push_back(part);
      }

      if (fen_parts.size() < 6) {
        throw std::runtime_error("Invalid FEN string: not enough components");
      }

      std::string board = fen_parts[0];
      std::string side = fen_parts[1];
      std::string castling = fen_parts[2];
      std::string en_passant = fen_parts[3];
      std::string halfmoves_last = fen_parts[4];
      std::string fullmoves = fen_parts[5];

      // First, extract and remove '/' characters from board
      std::erase(board, '/');

      // Process the side to move, then the board representation
      // Add 'w' or 'b' as first token
      add_char_token(indices, side[0]);

      // Process board representation
      tokenize_board(indices, board);

      // Process castling
      tokenize_castling(indices, castling);

      // Process en passant
      tokenize_en_passant(indices, en_passant);

      // Process halfmoves (since last capture) and fullmoves
      // Three digits for each, adding dots as padding if needed
      add_padded_string(indices, halfmoves_last, 3);
      add_padded_string(indices, fullmoves, 3);

      // Add the class token
      indices.push_back(class_token_);

      // Check sequence length
      if (indices.size() != static_cast<size_t>(sequence_length_)) {
        std::string error_msg = "Invalid tokenization length: ";
        error_msg += std::to_string(indices.size());
        error_msg += " expected ";
        error_msg += std::to_string(sequence_length_);
        throw std::runtime_error(error_msg);
      }

      // Create a numpy array by copying the vector data directly
      // Use nanobind's capsule_data to handle memory management
      auto *data = new std::vector(indices);
      auto capsule = nb::capsule(data, [](void *p) noexcept {
        delete static_cast<std::vector<uint8_t> *>(p);
      });

      return nb::ndarray<nb::numpy, uint8_t>(
        data->data(), // Data pointer
        {static_cast<size_t>(sequence_length_)}, // Shape
        capsule // Ownership capsule
      );
    }

    // Getters
    const std::vector<std::string> &get_characters() const { return characters_; }
    const std::unordered_map<std::string, int> &get_characters_index() const { return characters_index_; }
    const std::set<std::string> &get_spaces_characters() const { return spaces_characters_; }
    int get_sequence_length() const { return sequence_length_; }
    int get_input_vocab_size() const { return input_vocab_size_; }
    int get_class_token() const { return class_token_; }
    int get_pad_token() const { return pad_token_; }

  private:
    // Private member variables
    std::vector<std::string> characters_;
    std::unordered_map<std::string, int> characters_index_;
    std::set<std::string> spaces_characters_;
    int sequence_length_;
    int input_vocab_size_;
    int class_token_;
    int pad_token_;

    /**
     * Add a character token to the indices vector
     */
    void add_char_token(std::vector<uint8_t> &indices, char c) {
      std::string char_str(1, c);

      // Check if the character is valid
      if (!characters_index_.contains(char_str)) {
        throw std::runtime_error("Invalid character in FEN string: '" + char_str + "'");
      }

      indices.push_back(characters_index_[char_str]);
    }

    /**
     * Add a specified number of dot tokens
     */
    void add_dot_tokens(std::vector<uint8_t> &indices, size_t count) {
      for (size_t i = 0; i < count; i++) {
        indices.push_back(characters_index_["."]);
      }
    }

    /**
     * Add a string to indices and pad with dots to the target length
     */
    void add_padded_string(std::vector<uint8_t> &indices, const std::string &str, size_t target_length) {
      // Add existing characters
      for (char c: str) {
        add_char_token(indices, c);
      }

      // Pad with dots
      add_dot_tokens(indices, target_length - str.length());
    }

    /**
     * Tokenize the board representation
     */
    void tokenize_board(std::vector<uint8_t> &indices, const std::string &board) {
      for (char c: board) {
        if (std::string char_str(1, c); spaces_characters_.contains(char_str)) {
          // If character is a number, add that many '.' tokens
          int count = std::stoi(char_str);
          add_dot_tokens(indices, count);
        }
        else {
          // Otherwise, add the character token
          add_char_token(indices, c);
        }
      }
    }

    /**
     * Tokenize castling rights
     */
    void tokenize_castling(std::vector<uint8_t> &indices, const std::string &castling) {
      if (castling == "-") {
        // Add 4 dot tokens for no castling rights
        add_dot_tokens(indices, 4);
      }
      else {
        // Add each castling right character and pad to 4 characters
        add_padded_string(indices, castling, 4);
      }
    }

    /**
     * Tokenize en passant square
     */
    void tokenize_en_passant(std::vector<uint8_t> &indices, const std::string &en_passant) {
      if (en_passant == "-") {
        // Add 2 dot tokens for no en passant square
        add_dot_tokens(indices, 2);
      }
      else {
        // Add each character of the en passant square
        for (char c: en_passant) {
          add_char_token(indices, c);
        }
      }
    }
};

// Global tokenizer instance to maintain Python interface
static ChessTokenizer g_tokenizer;

// Wrapper for the tokenize function to maintain the Python interface
nb::ndarray<nb::numpy, uint8_t> tokenize(const std::string &fen) {
  return g_tokenizer.tokenize(fen);
}

/**
 * ChessMoveGenerator class for computing all possible chess moves
 */
class ChessMoveGenerator {
  public:
    ChessMoveGenerator()
      : chess_files_({"a", "b", "c", "d", "e", "f", "g", "h"}) {
    }

    /**
     * Compute all possible actions and their mappings for chess moves
     */
    std::pair<std::unordered_map<std::string, int>, std::unordered_map<int, std::string> >
    compute_all_possible_actions() {
      std::unordered_map<std::string, int> move_to_action;
      std::unordered_map<int, std::string> action_to_move;
      std::vector<std::string> all_moves;

      // Generate regular (non-promotion) moves
      auto regular_moves = generate_regular_moves();
      all_moves.insert(all_moves.end(), regular_moves.begin(), regular_moves.end());

      // Generate promotion moves
      auto promotion_moves = generate_promotion_moves();
      all_moves.insert(all_moves.end(), promotion_moves.begin(), promotion_moves.end());

      // Create the mapping dictionaries
      for (int action = 0; action < static_cast<int>(all_moves.size()); action++) {
        const std::string &move = all_moves[action];
        move_to_action[move] = action;
        action_to_move[action] = move;
      }

      // Return the maps
      return {move_to_action, action_to_move};
    }

  private:
    const std::vector<std::string> chess_files_;

    /**
     * Generate all possible regular chess moves (excluding promotions)
     */
    std::vector<std::string> generate_regular_moves() {
      std::vector<std::string> moves;
      moves.reserve(1800); // Pre-allocate space for efficiency

      // For each square (0-63)
      for (int square = 0; square < 64; square++) {
        std::vector<chess::Square> next_squares;
        chess::Square from_sq(square);

        // Add queen attacks (covers bishop and rook moves)
        add_moves_from_bitboard(moves, from_sq, chess::attacks::queen(from_sq, 0ULL));

        // Add knight attacks
        add_moves_from_bitboard(moves, from_sq, chess::attacks::knight(from_sq));
      }

      return moves;
    }

    /**
     * Add moves from a bitboard of attacked squares
     */
    void add_moves_from_bitboard(std::vector<std::string> &moves,
                                 chess::Square from_sq,
                                 chess::Bitboard attack_board) {
      // Convert bitboard to list of squares (simpler approach for compatibility)
      for (int target_sq = 0; target_sq < 64; target_sq++) {
        if ((attack_board & (1ULL << target_sq)) != 0) {
          chess::Square to_sq(target_sq);
          std::string move;
          move.reserve(4);
          move += from_sq;
          move += to_sq;
          moves.push_back(move);
        }
      }
    }

    /**
     * Generate all possible promotion moves
     */
    std::vector<std::string> generate_promotion_moves() {
      std::vector<std::string> promotion_moves;
      promotion_moves.reserve(128); // Pre-allocate space

      for (std::vector<std::pair<std::string, std::string> > promotion_ranks = {{"2", "1"}, {"7", "8"}}; const auto &[
             rank, next_rank]: promotion_ranks) {
        for (size_t index_file = 0; index_file < chess_files_.size(); index_file++) {
          const std::string &file = chess_files_[index_file];

          // Normal promotions (straight ahead)
          add_promotion_moves(promotion_moves, file, rank, file, next_rank);

          // Capture promotions - left side
          if (index_file > 0) {
            const std::string &next_file = chess_files_[index_file - 1];
            add_promotion_moves(promotion_moves, file, rank, next_file, next_rank);
          }

          // Capture promotions - right side
          if (index_file < chess_files_.size() - 1) {
            const std::string &next_file = chess_files_[index_file + 1];
            add_promotion_moves(promotion_moves, file, rank, next_file, next_rank);
          }
        }
      }

      return promotion_moves;
    }

    /**
     * Add promotion moves for a specific from-to square combination
     */
    void add_promotion_moves(std::vector<std::string> &promotion_moves,
                             const std::string &from_file, const std::string &from_rank,
                             const std::string &to_file, const std::string &to_rank) {
      std::string move_base;
      move_base.reserve(4);
      move_base.append(from_file).append(from_rank).append(to_file).append(to_rank);

      for (const auto &piece: {"q", "r", "b", "n"}) {
        std::string promotion_move = move_base;
        promotion_move.append(piece);
        promotion_moves.push_back(promotion_move);
      }
    }
};

// Global move generator instance to maintain Python interface
static ChessMoveGenerator g_move_generator;

// Wrapper for compute_all_possible_actions to maintain Python interface
std::pair<std::unordered_map<std::string, int>, std::unordered_map<int, std::string> >
compute_all_possible_actions() {
  return g_move_generator.compute_all_possible_actions();
}

/**
 * ChessMoveDatasetHelpers class for MoveDataset-related functions
 */
class ChessMoveDatasetHelpers {
  public:
    /**
     * Prepare recent moves tokens array
     * @param recent_moves Vector of recent move strings
     * @return Numpy array of token indices
     */
    nb::ndarray<nb::numpy, int64_t> prepare_recent_moves_tokens(const std::vector<std::string> &recent_moves) {
      // Create a numpy array for tokens
      std::vector<int64_t> tokens(RECENT_MOVES_LENGTH, PAD_TOKEN);

      if (size_t num_moves = recent_moves.size(); num_moves < RECENT_MOVES_LENGTH) {
        // Fill in only necessary values, starting at the right position
        for (size_t i = 0; i < num_moves; i++) {
          const std::string &move = recent_moves[i];
          auto it = MOVE_TO_ACTION.find(move);
          if (it == MOVE_TO_ACTION.end()) {
            throw std::runtime_error("Invalid move: " + move);
          }
          tokens[RECENT_MOVES_LENGTH - num_moves + i] = it->second;
        }
      }
      else {
        // Only use the last RECENT_MOVES_LENGTH moves
        size_t start_idx = num_moves - RECENT_MOVES_LENGTH;
        for (size_t i = 0; i < RECENT_MOVES_LENGTH; i++) {
          const std::string &move = recent_moves[start_idx + i];
          auto it = MOVE_TO_ACTION.find(move);
          if (it == MOVE_TO_ACTION.end()) {
            throw std::runtime_error("Invalid move: " + move);
          }
          tokens[i] = it->second;
        }
      }

      // Create a numpy array by copying the vector data directly
      auto *data = new std::vector(tokens);
      auto capsule = nb::capsule(data, [](void *p) noexcept {
        delete static_cast<std::vector<int64_t> *>(p);
      });

      return nb::ndarray<nb::numpy, int64_t>(
        data->data(), // Data pointer
        {static_cast<size_t>(RECENT_MOVES_LENGTH)}, // Shape
        capsule // Ownership capsule
      );
    }

    /**
     * Convert recent moves and FEN to model inputs
     * @param recent_moves Vector of recent move strings
     * @param fen FEN string
     * @return Python tuple of (recent_moves_tokens, fen_tokens, move_mask)
     */
    nb::tuple recent_moves_and_fen_to_inputs(const std::vector<std::string> &recent_moves, const std::string &fen) {
      // 1. Process recent moves efficiently
      nb::ndarray<nb::numpy, int64_t> move_tokens_array = prepare_recent_moves_tokens(recent_moves);

      // 2. Parse board and get legal moves using chess.hpp
      chess::Board board(fen);

      // 3. Create move mask efficiently
      auto *move_mask_data = new std::vector(NUM_ACTIONS, 0.0f);

      // Generate legal moves
      chess::Movelist legal_moves;
      chess::movegen::legalmoves<chess::movegen::MoveGenType::ALL>(legal_moves, board);

      // Process legal moves
      for (const auto &move: legal_moves) {
        // Convert to UCI string
        std::string move_str = chess::uci::moveToUci(move);

        if (auto it = MOVE_TO_ACTION.find(move_str); it != MOVE_TO_ACTION.end()) {
          (*move_mask_data)[it->second] = 1.0f;
        }
      }

      // Create a numpy array for move mask
      auto move_mask_capsule = nb::capsule(move_mask_data, [](void *p) noexcept {
        delete static_cast<std::vector<float> *>(p);
      });

      nb::ndarray<nb::numpy, float> move_mask = nb::ndarray<nb::numpy, float>(
        move_mask_data->data(), // Data pointer
        {static_cast<size_t>(NUM_ACTIONS)}, // Shape
        move_mask_capsule // Ownership capsule
      );

      // 4. Tokenize the FEN
      nb::ndarray<nb::numpy, uint8_t> fen_tokens_uint8 = g_tokenizer.tokenize(fen);

      // Convert uint8_t to int64_t for the fen tokens
      std::vector<int64_t> fen_tokens_int64(SEQUENCE_LENGTH);
      for (int i = 0; i < SEQUENCE_LENGTH; i++) {
        fen_tokens_int64[i] = static_cast<int64_t>(fen_tokens_uint8.data()[i]);
      }

      // Create a numpy array for fen tokens
      auto *fen_tokens_data = new std::vector(fen_tokens_int64);
      auto fen_tokens_capsule = nb::capsule(fen_tokens_data, [](void *p) noexcept {
        delete static_cast<std::vector<int64_t> *>(p);
      });

      auto fen_tokens = nb::ndarray<nb::numpy, int64_t>(
        fen_tokens_data->data(), // Data pointer
        {static_cast<size_t>(SEQUENCE_LENGTH)}, // Shape
        fen_tokens_capsule // Ownership capsule
      );

      return nb::make_tuple(move_tokens_array, fen_tokens, move_mask);
    }
};

// Global helpers instance
static ChessMoveDatasetHelpers g_move_dataset_helpers;

// Wrapper functions for Python interface
nb::ndarray<nb::numpy, int64_t> prepare_recent_moves_tokens(const std::vector<std::string> &recent_moves) {
  return g_move_dataset_helpers.prepare_recent_moves_tokens(recent_moves);
}

nb::tuple
recent_moves_and_fen_to_inputs(const std::vector<std::string> &recent_moves, const std::string &fen) {
  return g_move_dataset_helpers.recent_moves_and_fen_to_inputs(recent_moves, fen);
}

/**
 * Filter bagz file by minimum fullmove number
 * @param bagz_path Path to the bagz file
 * @param min_fullmove Minimum fullmove number (positions before this are excluded)
 * @param progress_callback Optional Python callback function(current, total) for progress updates
 * @return Vector of valid indices
 */
std::vector<int64_t> filter_bagz_by_fullmove(
  const std::string &bagz_path,
  int min_fullmove,
  nb::object progress_callback = nb::none()) {
  // Open bagz file
  chessmimic::BagFileReader reader(bagz_path, false);
  size_t total_records = reader.size();

  // Prepare simdjson parser (reusable)
  simdjson::dom::parser parser;

  // Result vector - pre-allocate with reasonable estimate
  std::vector<int64_t> valid_indices;
  valid_indices.reserve(total_records / 2);

  // Progress reporting (update every 1% or every 100k records, whichever is smaller)
  size_t progress_interval = std::min(total_records / 100, size_t(100000));
  if (progress_interval == 0) progress_interval = 1;
  bool has_callback = !progress_callback.is_none();

  // Scan through all records
  for (int64_t idx = 0; idx < static_cast<int64_t>(total_records); ++idx) {
    // Call progress callback periodically
    if (has_callback && idx % progress_interval == 0) {
      // Acquire GIL for Python callback
      nb::gil_scoped_acquire gil;
      progress_callback(idx, total_records);
    }

    try {
      // Get decompressed record
      std::vector<uint8_t> record_bytes = reader.get_record(idx);

      // Parse JSON with simdjson
      std::string_view json_view(
        reinterpret_cast<const char *>(record_bytes.data()),
        record_bytes.size()
      );
      simdjson::dom::element doc = parser.parse(json_view);

      // Extract recent_and_fen array
      simdjson::dom::array recent_and_fen = doc["recent_and_fen"].get_array();

      // Get FEN (second element of array)
      auto recent_and_fen_iter = recent_and_fen.begin();
      ++recent_and_fen_iter; // Skip first element (recent_moves)
      std::string_view fen = (*recent_and_fen_iter).get_string();

      // Parse fullmove number from FEN (last space-separated component)
      if (size_t last_space = fen.rfind(' '); last_space != std::string_view::npos && last_space + 1 < fen.size()) {
        // Extract fullmove number substring
        std::string_view fullmove_str = fen.substr(last_space + 1);

        // Convert to int (fast C++ conversion)
        int fullmove = 0;
        auto [ptr, ec] = std::from_chars(
          fullmove_str.data(),
          fullmove_str.data() + fullmove_str.size(),
          fullmove
        );

        if (ec == std::errc() && fullmove >= min_fullmove) {
          valid_indices.push_back(idx);
        }
      }
    } catch (...) {
      // Skip invalid records silently
    }
  }

  // Final progress callback (100%)
  if (has_callback) {
    nb::gil_scoped_acquire gil;
    progress_callback(total_records, total_records);
  }

  // Shrink to fit to reduce memory usage
  valid_indices.shrink_to_fit();

  return valid_indices;
}

NB_MODULE(chessmimic_core, m) {
  m.doc() = "C++ accelerated functions for ChessMimic";

  // ChessTokenizer is automatically initialized via the global instance g_tokenizer

  // Export test function
  m.def("add", &add, "Add two integers",
        nb::arg("a"), nb::arg("b"));

  // Initialize action mappings
  auto [move_to_action, action_to_move] = compute_all_possible_actions();
  MOVE_TO_ACTION = move_to_action;
  ACTION_TO_MOVE = action_to_move;
  NUM_ACTIONS = static_cast<int>(MOVE_TO_ACTION.size());

  // Export tokenizer constants
  m.attr("_CHARACTERS") = CHARACTERS;
  m.attr("_CHARACTERS_INDEX") = CHARACTERS_INDEX;
  m.attr("_SPACES_CHARACTERS") = SPACES_CHARACTERS;
  m.attr("SEQUENCE_LENGTH") = SEQUENCE_LENGTH;
  m.attr("INPUT_VOCAB_SIZE") = INPUT_VOCAB_SIZE;
  m.attr("CLASS_TOKEN") = CLASS_TOKEN;
  m.attr("PAD_TOKEN") = PAD_TOKEN;
  m.attr("MOVE_TO_ACTION") = MOVE_TO_ACTION;
  m.attr("ACTION_TO_MOVE") = ACTION_TO_MOVE;
  m.attr("NUM_ACTIONS") = NUM_ACTIONS;

  // Export tokenizer functions
  m.def("tokenize", &tokenize, "Tokenize a FEN string into an array of tokens", nb::arg("fen"));
  m.def("_compute_all_possible_actions", &compute_all_possible_actions,
        "Compute all possible chess moves and return move_to_action and action_to_move mappings");

  // Export MoveDataset helper functions
  m.def("prepare_recent_moves_tokens", &prepare_recent_moves_tokens,
        "Prepare recent moves tokens array", nb::arg("recent_moves"));
  m.def("recent_moves_and_fen_to_inputs", &recent_moves_and_fen_to_inputs,
        "Convert recent moves and FEN to model inputs", nb::arg("recent_moves"), nb::arg("fen"));

  // Export bagz filtering function (releases GIL for parallel execution)
  m.def("filter_bagz_by_fullmove", &filter_bagz_by_fullmove,
        "Filter bagz file by minimum fullmove number, returning valid indices",
        nb::arg("bagz_path"), nb::arg("min_fullmove"),
        nb::arg("progress_callback") = nb::none(),
        nb::call_guard<nb::gil_scoped_release>());
}
