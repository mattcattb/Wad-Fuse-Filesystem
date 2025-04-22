#include "file_utils.h"
#include <iostream>
#include <stack> 
#include <regex> 
#include <filesystem>
#include <string>
#include <unistd.h>
#include <limits.h>
#include <fstream>

using namespace std;

string get_cwd(){
  const size_t size = 1024;
  char buffer[size];

  if (getcwd(buffer, size) != NULL) {
    return string(buffer); // Return the cwd as a string
  }
  else {
      return "";
  }
}

void printTree(const ElementNode* node, int indent = 0) {
  if (!node) return;

  for (int i = 0; i < indent; ++i) {
      std::cout << "  ";
  }
  cout << node->elementInfoString() << endl;
  // Recursively print children
  for (const ElementNode* child : node->children) {
      printTree(child, indent + 1);
  }
}


#include <utility> 


std::pair<bool, std::string> extractStartPrefix(const string& name) {
  const string suffix = "_START";
  if (name.length() >= suffix.length() && name.substr(name.length() - suffix.length()) == suffix) {
      return {true, name.substr(0, name.length() - suffix.length())};
  }
  return {false, ""};
}

std::pair<bool, std::string> extractEndPrefix(const string& name) {
  const string suffix = "_END";
  if (name.length() >= suffix.length() && name.substr(name.length() - suffix.length()) == suffix) {
      return {true, name.substr(0, name.length() - suffix.length())};
  }
  return {false, ""};
}

void printElementStack(std::vector<ElementNode*> stack){
  for (size_t i = 0; i < stack.size(); i += 1){
    ElementNode * cur = stack[i];
    cout << cur->name << " has total children " << cur->children.size() << endl; 
  }
}

ElementNode* buildTreeFromDescriptors(const std::vector<Descriptor>& descriptors) {

  vector<ElementNode *> elementStack;

  // remaining elements are in the root
  ElementNode * root = new ElementNode( "/", 2, 0, 0);
  elementStack.push_back(root);
  
  for (size_t i = 0; i < descriptors.size(); i += 1){  
    Descriptor desc = descriptors[i]; 
    // cout << "Current descriptor " << desc.name << " is type ";
    int type = getDescriptorNameType(desc.name);
    // cout << type << endl;
  
    if (type == 0){
      // descriptor is content
      // cout << "content " << endl;
      ElementNode *el = new ElementNode( desc.name, 0, desc.offset, desc.length);
      elementStack.back()->children.push_back(el);

    } else if (type == 1){
      // descriptor is a var map
      // cout << "mapped element " << desc.name << endl;
      // we need to add the next 10 here
      ElementNode * el = new ElementNode(desc.name, type=1);
      vector<ElementNode *> mapChildren;

      for (size_t j = 1; j <= 10 && i + j < descriptors.size(); ++j) {
        const auto& childDesc = descriptors[i + j];
        uint32_t childType = getDescriptorNameType(childDesc.name);
        // cout << "  Map child: " << childDesc.name << " type " << to_string(childType) << endl;
        ElementNode* childNode = new ElementNode( childDesc.name, childType, childDesc.offset, childDesc.length); 
        mapChildren.push_back(childNode);
      }
      i += 10; 
      el->children = mapChildren;
      elementStack.back()->children.push_back(el);

    } else if (type == 2) {
      // descriptor is a namespace directory start
      // cout << "namespace directory start " << endl;
      ElementNode * el = new ElementNode(desc.name, 2);
      elementStack.push_back(el);
    } else if (type == 3) {
      // descriptor is a namespace directory end
      // cout << "namespace directory " << desc.name << " end " << endl;
      if (!elementStack.empty() && elementStack.back() != root) {
        ElementNode * e = elementStack.back(); 
        e->name.erase(e->name.length() - 6, 6);
        
        elementStack.pop_back(); 
        elementStack.back()->children.push_back(e);

      }
    }
  }

  if (elementStack.empty() || elementStack.back()->name != "/"){
    cout << "[ERROR]: Last element is not root!" << endl;
  }
  // printElementStack(elementStack);
  return elementStack.back();
}


map<string, ElementNode *> buildMapFromElementTree( ElementNode * root) {
  map<string, ElementNode *> elementMap;
  buildMapElementHelper(root, elementMap, "");

  return elementMap;
}

void buildMapElementHelper(ElementNode * root, map<string, ElementNode *> &elementMap, string path) {
  // given a the tree map of descriptors, lets build

  string currentPath = path + root->name;
  elementMap.insert({currentPath, root});
  if (root->type == 0) {
    // base case, is content!
    return;
  }

  for (size_t i = 0; i < root->children.size(); i += 1){
    ElementNode * child = root->children[i];
    string newPath = currentPath + "/";
    if (root->name == "/"){
      newPath = "/";
    }
    buildMapElementHelper(child, elementMap, newPath);
  }
}