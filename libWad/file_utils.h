#pragma once 

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <stdexcept> 
#include "WadStructure.h"
#include <regex>


std::string get_cwd();

ElementNode* buildTreeFromDescriptors(const std::vector<Descriptor>& descriptors);

void buildMapElementHelper(ElementNode * root, std::map<std::string, ElementNode *> &elementMap, std::string path);

std::map<std::string, ElementNode*> buildMapFromElementTree(ElementNode * root);

void printTree(const ElementNode* node, int indent);

