#include "json/json.h"

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

namespace json {

std::string prettify(const std::string &json_str) {
  rapidjson::Document doc;
  doc.Parse(json_str.c_str());
  if (doc.HasParseError()) {
    return {};
  }

  rapidjson::StringBuffer buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);
  return std::string(buffer.GetString(), buffer.GetSize());
}

std::string get_string(const std::string &json_str, const std::string &key) {
  rapidjson::Document doc;
  doc.Parse(json_str.c_str());
  if (doc.HasParseError() || !doc.IsObject())
    return {};
  auto it = doc.FindMember(key.c_str());
  if (it == doc.MemberEnd() || !it->value.IsString())
    return {};
  return std::string(it->value.GetString(), it->value.GetStringLength());
}

int get_int(const std::string &json_str, const std::string &key) {
  rapidjson::Document doc;
  doc.Parse(json_str.c_str());
  if (doc.HasParseError() || !doc.IsObject())
    return 0;
  auto it = doc.FindMember(key.c_str());
  if (it == doc.MemberEnd() || !it->value.IsInt())
    return 0;
  return it->value.GetInt();
}

bool is_valid(const std::string &json_str) {
  rapidjson::Document doc;
  doc.Parse(json_str.c_str());
  return !doc.HasParseError();
}

std::vector<std::string> keys(const std::string &json_str) {
  std::vector<std::string> result;
  rapidjson::Document doc;
  doc.Parse(json_str.c_str());
  if (doc.HasParseError() || !doc.IsObject())
    return result;
  for (auto it = doc.MemberBegin(); it != doc.MemberEnd(); ++it) {
    result.emplace_back(it->name.GetString(), it->name.GetStringLength());
  }
  return result;
}

} // namespace json
