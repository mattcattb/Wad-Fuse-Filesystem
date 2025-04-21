#pragma once 

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <stdexcept> 
#include "WadStructure.h"
#include <regex>


int propagateFile(const std::string path, uint32_t length, uint32_t offset);


std::string get_cwd();

ElementNode* buildTreeFromDescriptors(const std::vector<Descriptor>& descriptors);

void buildMapElementHelper(ElementNode * root, std::map<std::string, ElementNode *> &elementMap, std::string path);

std::map<std::string, ElementNode*> buildMapFromElementTree(ElementNode * root);

void cleanupElementMap(std::map<std::string, ElementNode*>& elementMap);

void cleanupElementMap(std::map<std::string, ElementNode*>& elementMap);

void printTree(const ElementNode* node, int indent);

size_t findEndDescriptorIndex(const std::vector<Descriptor> desciptors, std::string path);