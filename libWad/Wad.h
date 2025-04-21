#pragma once

#include <string>
#include <vector>
#include <map>      
#include <cstdint>  
#include "file_utils.h"
#include "WadStructure.h"
#include <filesystem> 


using namespace std;

struct HeaderData {
  string magic;
  uint32_t num_descriptors;
  uint32_t descriptor_offset;
};


class Wad {
  HeaderData header;
  vector<Descriptor> descriptorList;

  string absolutePath;

  map<string, ElementNode *> fileMap;
  ElementNode * treeRoot; // file tree, starting with root

  int loadPhysicalFile(const string &path); // get the header and descriptors from file
  Wad(const string & path); 
  bool isValidContentName(string & path); // cannot have E#M#, _START, _END, CANNOT BE GREATER THEN 8 CHARACTERS!
  bool isValidDirectoryName(string & path); // has E#M# 

  vector<string> seperate_path(const string &path);

  string trimPath(const string &path);
  int readWadData(char *buffer, uint32_t length, uint32_t offset);
  int writeWadData(const char * buffer, uint32_t length, uint32_t offset);
  bool pathExists(const string &path);

  bool saveWad(); // save header and descriptors from element node list 
  void createAnyFile(const string &parentPath, const string& childName, int type); // create any file and then save desciptors


public:
  void print();

  static  Wad* loadWad(const string &path);

  ~Wad();
  string getMagic();
  bool isContent(const string &path);
  bool isDirectory(const string &path);
  int getSize(const string &path);
  int getContents(const string &path, char *buffer, int length, int offset=0);
  int getDirectory(const string &path, vector<string> *directory);
  void createDirectory(const string &path);
  void createFile(const string &path);
  int writeToFile(const string &path, const char *buffer, int length, int offset=0);

};