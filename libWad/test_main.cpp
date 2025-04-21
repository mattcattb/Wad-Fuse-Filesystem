#include "file_utils.h" 
#include <iostream>
#include <vector>
#include <iomanip> 
#include "Wad.h"

int testElementCreation(std::vector<Descriptor> testDescriptors){
    // --- Create Sample Descriptor Data ---
    // Use the data you provided

  std::cout << "\n--- Input Descriptors ---" << std::endl;
  for(size_t i = 0; i < testDescriptors.size(); i += 1) {
      Descriptor desc = testDescriptors[i];
      std::cout << "  Name: " << std::setw(10) << std::left << desc.name
                << " Offset: " << std::setw(8) << desc.offset
                << " Length: " << desc.length << std::endl;
  }
  std::cout << "------------------------" << std::endl;

  std::cout << "\n--- Testing buildTreeFromDescriptors ---" << std::endl;
  ElementNode* rootNode = buildTreeFromDescriptors(testDescriptors);

  if (rootNode) {
      std::cout << "\n--- Generated Tree Structure ---" << std::endl;
      printTree(rootNode, 0);
      std::cout << "-----------------------------" << std::endl;
  } else {
      std::cout << "Tree building failed or returned null." << std::endl;
  }


  std::cout << "\n--- Testing buildMapFromDescriptors ---" << std::endl;

  std::map<std::string, ElementNode*> elementMap = buildMapFromElementTree(rootNode);

  std::cout << "\n--- Generated Map Contents ---" << std::endl;
  if (!elementMap.empty()) {
     for (auto const& [key, val] : elementMap) {
         std::cout << "  Key: " << std::setw(20) << std::left << key << " -> Node: "
                   << (val->type > 0 ? "[D] " : "[F] ") << val->name
                   << " (Addr: " << val << ")" << std::endl; 
     }
  } else {
       std::cout << "Map is empty." << std::endl;
  }
  std::cout << "---------------------------" << std::endl;


  std::cout << "\n--- Performing Cleanup ---" << std::endl;
  delete rootNode;
  rootNode = nullptr; 
  std::cout << "Tree deleted." << std::endl;

  cleanupElementMap(elementMap);
  std::cout << "Map nodes deleted." << std::endl;
  std::cout << "------------------------" << std::endl;


  std::cout << "\n--- Test Runner Finished ---" << std::endl;
}

int test_wad_creation(){
  string path = "sample1.wad";
  Wad * cur =  Wad::loadWad(path);

  cur->print();
}

int main() {
  std::cout << "--- Filesystem Utils Test Runner ---" << std::endl;

  std::vector<Descriptor> testDescriptors = {
    // name, offset, length
    {"F_START", 0, 0},         // Namespace Start
    {"F1_START", 0, 0},        // Nested Namespace Start
    {"E1M1", 67500, 0},        // Map Marker
    {"THINGS", 67500, 1380},   // Map Content 1
    {"LINEDEFS", 68880, 6650},  // Map Content 2
    {"SIDEDEFS", 75532, 19440}, // Map Content 3
    {"VERTEXES", 94972, 1868},  // Map Content 4
    {"SEGS", 96840, 8784},     // Map Content 5
    {"SSECTORS", 105624, 948}, // Map Content 6
    {"NODES", 106572, 6608},   // Map Content 7
    {"SECTORS", 113180, 2210},  // Map Content 8
    {"REJECT", 115392, 904},   // Map Content 9
    {"BLOCKMAP", 116296, 6922},// Map Content 10
    {"LOLWUT", 42, 9001},      // Regular file inside F1
    {"F1_END", 0, 0},          // Nested Namespace End
    {"F_END", 0, 0}            // Namespace End
  };

  testElementCreation(testDescriptors);
  test_wad_creation();
  return 0;
}