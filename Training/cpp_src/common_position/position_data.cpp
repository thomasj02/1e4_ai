#include "position_data.hpp"
#include <sstream>

namespace chessmimic {
std::string PositionData::toJson() const {
  std::ostringstream oss;
  oss << "{\"fen\":\"" << fen << "\",\"moves\":{";

  bool first = true;
  for (const auto& [move, count]: moves) {
    if (!first) oss << ",";
    oss << "\"" << move << "\":" << count;
    first = false;
  }

  oss << "},\"total\":" << total << "}";
  return oss.str();
}

PositionData PositionData::fromJson(const std::string& json_str) {
  PositionData data;
  simdjson::dom::parser parser;
  simdjson::dom::element doc = parser.parse(json_str);

  data.fen = std::string(doc["fen"]);
  data.total = doc["total"].get_int64();

  for (auto moves_obj = doc["moves"].get_object(); auto [key, value]: moves_obj) {
    data.moves[std::string(key)] = value.get_int64();
  }

  return data;
}
} // namespace chessmimic
