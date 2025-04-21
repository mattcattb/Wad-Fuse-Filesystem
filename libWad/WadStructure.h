
#pragma once 

#include <string>
#include <vector>
#include <regex>

struct Descriptor {
  std::string name;
  uint32_t offset;
  uint32_t length;
};

struct ElementNode {
  std::string name;
  uint32_t offset;
  uint32_t length;
  uint32_t type; // 0 content, 1 is map, 2 is namespace dir
  std::vector<ElementNode*> children; 

  ElementNode(std::string n = "", uint32_t type = 0, uint32_t off = 0, uint32_t len = 0)
      : name(n), offset(off), length(len), type(type) {}

  ~ElementNode() {
      for (ElementNode* child : children) {
          delete child; 
      }
      children.clear(); 
  }

  std::string elementInfoString() const{
    // returns string containing element info
    std::string typeString;
    if (type == 0){
      typeString = "Content"; 
    } else if (type == 1){
      typeString = "Map Dir";
    } else if (type == 2){
      typeString = "Namespace Dir";
    } else {
      typeString = "invalid";
    }


    std::string info = "[" + typeString + "] : " + name + " offset: " + std::to_string(offset) + " length: " +  std::to_string(length);
    if (type != 0){
      info += " with " + std::to_string(children.size()) + " children";
    } 

    return info;
  }
  ElementNode(const ElementNode&) = delete;
  ElementNode& operator=(const ElementNode&) = delete;
  ElementNode(ElementNode&&) = delete;
  ElementNode& operator=(ElementNode&&) = delete;
};

inline uint32_t getDescriptorNameType(std::string name){
  // 0 content, 1 marker, 2 named dir start 3 named dir end
  
  if (name.length() > 8) {
    // invalid length!
    return -1;
  }

  static const std::regex map_marker_regex(R"(^E\dM\d$)");

  static const std::regex start_marker_regex(R"(.*_START$)");
  
  static const std::regex end_marker_regex(R"(.*_END$)");
  
  if (regex_match(name, end_marker_regex)) {
    return 3; // Namespace Directory End
  }

  if (regex_match(name, start_marker_regex)) {
      return 2; // Namespace Directory Start
  }

  // Only if it's not a start/end marker, check if it's a map marker.
  if (regex_match(name, map_marker_regex)) {
      return 1; // Map Marker
  }

  // If none of the above specific patterns match, it's content.
  return 0; // Content

}
