#include "Wad.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <filesystem>
#include <regex>

using namespace std;

vector<string> Wad::seperate_path(const string &path){
  // seperates path into vector of size 2: parentDirectory path, and current file path
  vector<string> tokens;
  stringstream ss(path);
  string token;

  size_t lastSeparatorPos = path.find_last_of("/\\");

  tokens.push_back(path.substr(0, lastSeparatorPos));
  tokens.push_back(path.substr(lastSeparatorPos + 1, path.size()));
  return tokens;
}

bool Wad::isValidContentName(string & path){
// cannot have E#M#, _START, _END, CANNOT BE GREATER THEN 8 CHARACTERS!

  return false;
} 

bool Wad::isValidDirectoryName(string & path){
  // has E#M# 

  return false;
} 

void Wad::print(){
  cout << "==== WAD " << absolutePath << " ====\n" 
  << "HEADER: \n"
  << "\tmagic: " << header.magic 
  << "\tdescriptor offset: " << header.descriptor_offset
  << "\tnum descriptors: " << header.num_descriptors
  << endl;

  cout << "File Element Tree: \n" << endl;
  printTree(this->treeRoot, 2);
}

uint32_t Wad::readWadData(char *buffer, uint32_t length, uint32_t offset){
  fstream file;
  // cout << "== READ WAD DATA == \n" << "Opening file " << this->absolutePath << endl;
  file.open(this->absolutePath, ios::in | ios::binary);
  if (!file.is_open()){
    return -1;
  }

  file.seekg(offset);

  if (file.fail()){
    file.close();
    return -1;
  }

  file.read(buffer, length);

  std::streamsize bytesRead = file.gcount();

  file.close();
  return bytesRead;
}

int Wad::loadPhysicalFile(const string &path){
  // physically load in HEADER, DESCRIPTOR LIST!
  char magic[5];
  magic[4] = '\0';

  uint32_t numDescriptors;
  uint32_t descriptorOffset;

  int readSuccess = 1;
  if (readWadData(magic, 4, 0) == -1) return -1;
  if (readWadData((char*)&numDescriptors, 4, 4) == -1) return -1;
  if (readWadData((char*)&descriptorOffset, 4, 8) == -1) return -1;

  header.magic = magic; 
  header.num_descriptors = numDescriptors;
  header.descriptor_offset = descriptorOffset;


  char lumpData[descriptorOffset + 1];
  lumpData[descriptorOffset] = '\0';

  // now read in the descriptors!
  for (int i = 0; i < numDescriptors; i += 1){
    // first, we need to get the byte offset
    uint32_t elementOffset;
    uint32_t elementLength;
    char name[9];
    name[8] = '\0';
    uint32_t byteOffset = descriptorOffset + (i) * 16;
    readWadData((char*)&elementOffset, 4, byteOffset);
    readWadData((char*)&elementLength, 4, byteOffset + 4);
    readWadData(name, 8, byteOffset + 8);
    Descriptor cur;
    cur.length = elementLength;
    cur.offset = elementOffset;
    cur.name = name;
    
    descriptorList.push_back(cur);
  }

  return 1;
}

Wad::Wad(const string &path){

  // takes in wad path to real filesystem, initialize fstream object, store as member variable, use to read
  // header data constructs tree from descriptor list, no lump data reading yet. posix calls?
  this->absolutePath = path;
  int r = loadPhysicalFile(path);

  if (r == -1){
    return;
  }
  // lets create the element tree now from the stack!
  this->treeRoot = buildTreeFromDescriptors(this->descriptorList);
  
  // build element map
  this->fileMap = buildMapFromElementTree(this->treeRoot);
}

string Wad::trimPath(const string &path){

  if (path == "") {
    return "";
  }
  string trimmed = path;
  int l = trimmed.length();
  if (trimmed != "/" && trimmed[l - 1] == '/') {
      trimmed.pop_back();
  }

  
  return trimmed;
}

string getAbsolutePath(const string &path){
  string wadPath = path;

  if (wadPath.at(0) != '/'){
    // convert relative path to absolute
    wadPath = get_cwd() + "/" + wadPath;
  }

  return wadPath;

}

Wad* Wad::loadWad(const string &path){
  // prepare

  // first, get absolute path
  string absPath = getAbsolutePath(path);

  Wad* wadInstance = new Wad(absPath); // Dynamically allocate a Wad object
  
  // now, generate the other stuff.. 
  return wadInstance; 

}
string Wad::getMagic(){
  return header.magic;
}

bool Wad::pathExists(const string &path){

  if (path == ""){
    // empty path!
    return false;
  }

  if (path.empty()){
    return "";
  }
  if (this->fileMap.find(path) == this->fileMap.end()){
    return false;
  } else {
    return true;
  }
}

Wad::~Wad() {

  delete treeRoot;
  fileMap.clear();

  treeRoot = nullptr; 

}


bool Wad::isContent(const string &path){


  string tPath = trimPath(path);
  if (!pathExists(tPath)) return false;

  ElementNode* cur = this->fileMap[tPath];

  if (cur->type == 0) return true;

  return false;
}


bool Wad::isDirectory(const string &path){
  // true if is directory, 

  if (path == ""){
    // empty path!
    return false;
  }

  string tPath = trimPath(path);
  if (!pathExists(tPath)) return false;

  ElementNode* cur = this->fileMap[tPath];

  if (cur->type == 1 || cur->type == 2) return true;

  return false;
}
int Wad::getSize(const string &path){
  // size of file at path, -1 if if invalid path
  // size of file is element length within descriptor file
  string tPath = trimPath(path);

  if (!isContent(tPath)){
    return -1;
  }

  ElementNode * el = fileMap[tPath];
  int size = el->length;

  return size;
}
int Wad::getContents(const string &path, char *buffer, int length, int offset){
  // if given valid path to content file, read length amount of bytes starting at offset
  // -1 if invalid 
  // EX length 5, offset blank (0) all 5 bytes (hello) copied to buffer, return 4
  // return size availible from that offset length
  // TODO requires file read and proper offset and length sizing!
  string tPath = trimPath(path);

  if (!isContent(tPath)){
    return -1;
  }

  ElementNode * contentElement = fileMap[tPath];
  uint32_t fileLength = contentElement->length;
  uint32_t fileOffset = contentElement->offset;

  if (offset >= fileLength) {
    return 0;
  }

  // actual offset of the lump in memory (according to where offset to start)
  uint32_t aOffset = fileOffset + offset;

  // actual length (prevents out of bounds)
  uint32_t aLength = (length <= fileLength - offset ) ? length : fileLength - offset;

  int bytesRead = readWadData(buffer, aLength, aOffset);

  return bytesRead;
}
int Wad::getDirectory(const string &path, vector<string> *directory){
  // gets the directory and returns vector of all filenames that are child of it 
  
  string tpath = trimPath(path);

  if(!isDirectory(tpath)){
    return -1;
  }

  ElementNode * el = fileMap[tpath];

  for (int i = 0; i < el->children.size(); i += 1){
    ElementNode * cur_child = el->children[i];
    directory->push_back(cur_child->name);
  }

  return directory->size();
}
void Wad::createDirectory(const string &path){
  // TODO!!! also need to find a way to get the end descriptor... fuhhhh
  // ex. path /F/F1/F2, only work if /F/F1 is NOT CONTENT and IS NOT MAP DIRECTORY! 
  // shift Descriptor 32Btyes down to make space for F2_start, and F2_end. Parent directory must have space for 2 descriptors in F1...
  // shift by reading in starting at F1, move 32bytes down, 
  // add 2 to number descriptors in header! 

  string tPath = trimPath(path);

  if (tPath == ""){
    // cout << "[ERROR]: given empty file to create!" << endl;
    return;
  }

  vector<string> pathSplit = seperate_path(tPath);
  string parent = pathSplit[0];
  string child = pathSplit[1];

  if (parent == ""){
    parent = "/"; // parent is actually a root directory!
  }

  if (child.length() > 2) {
    // cout << "[ERROR]: filename" << child << "is too large to be a proper directory name!" << endl;
    return;
  }

  if (!pathExists(parent)){
    // cout << "[ERROR]: parent path " << parent << " is not a directory in the filesystem." << endl;
    return;
  }


  createAnyFile(parent, child, 2);

  return;
}

void Wad::createAnyFile(const string &parentPath, const string& childName, int type){
  // for creating either content or directory
  ElementNode * parentNode = fileMap[parentPath];
  ElementNode * fileNode = new ElementNode(childName, type, 0, 0);
  if(type != 0 && type != 2) {
    // cout << "[ERROR]: INVALID FILE GIVEN!" << type << endl;
    return;
  }


  if (parentNode->type != 2){
    // cout << "[ERROR]: parent directory of type " << parentNode->type << " is not a named directory!";
    return;
  } 
  parentNode->children.push_back(fileNode);

  // add to map
  string completePath = (parentPath == "/") ? parentPath + childName : parentPath + "/" + childName;
  fileMap[completePath] = fileNode;
  
  // update header
  header.num_descriptors += (type == 0) ? 1 : 2; // 1 for content, 2 for not content 

  // save headers and descriptors based on the new tree!
  if(!saveWad()){
    cout << "[ERROR]: error saving changes to file for new node " << fileNode->name << endl;
    return;
  }


  return;
}

void Wad::createFile(const string &path){
  // TODO!!! need to find a way to find the end desciptor... fuhhhh
  
  /*
    Takes path to file not created, creates INSIDE NAMESPACE DIRECTORY!!!
    Cannot containe E#M# in the name, or _START, _END 
  */
  string tPath = trimPath(path);

  vector<string> pathSplit = seperate_path(tPath);
  string parent = pathSplit[0];
  string child = pathSplit[1];

  if(child.length() > 8){
    // cout << "[ERROR]: filename " << child << " is too large to be a proper content name!" << endl;
  }

  if (parent == ""){
    parent = "/";
  }

  int childType = getDescriptorNameType(child);
  
  if (childType != 0) {
    // cout << "[ERROR]: filename " <<  child << " is not a content name!" << endl;
    return;
  }

  if (!pathExists(parent)){
    // cout << "[ERROR]: parent path " << parent << " is not a directory in the filesystem." << endl;
    return;
  }

  createAnyFile(parent, child, childType);
}
int Wad::writeToFile(const string &path, const char *buffer, int length, int offset){
  /*
    gets path to EMPTY FILE (-1 if directory or path doesnt exist, 0 if nonempty file)
    read length num bytes from buffer, writes into lump data starting at offset 
    must CREATE SPACE in lump data in middle of file to write content to file 
    MOVE FORWARD lump data in middle of file to write file contents
  */

  std::fstream file(absolutePath, std::ios::in | std::ios::out | std::ios::binary);
  if (!file.is_open()) {
    return false;
  }

  string tPath = trimPath(path);

  if (!pathExists(tPath)){
    return -1;
  }

  ElementNode * contentFileElement = fileMap[tPath];
  if (contentFileElement->type != 0){
    return -1;
  }

  if (contentFileElement->length != 0){
    return 0; 
  }

  if (offset > header.descriptor_offset){
    return -1;
  }

  // !!! buffer written could be less then length, how should this eeffect new file element?
  // push file back, and write to it!
  uint32_t offsetPosition = header.descriptor_offset;

  if (offset != 0){
    offsetPosition = offset;
  }
  
  contentFileElement->offset = offsetPosition;
  contentFileElement->length = length;

  // now need to update the descriptors and header
  file.seekg(offsetPosition);
  file.write(buffer, length);
  header.descriptor_offset += length;
  file.close();
  
  // save new element files to descriptors
  saveWad();
  return length;
}


void createDesciptorListTreeHelper(vector<Descriptor> &descriptors, ElementNode * root){
  
  Descriptor curDesciptor;
  curDesciptor.name = root->name;
  curDesciptor.length = root->length;
  curDesciptor.offset = root->offset;

  if (root->name == "/") {
    // root, dont add!
    for (auto childNode : root->children){
      createDesciptorListTreeHelper(descriptors, childNode);
    }

  } else if (root->type == 0){
    descriptors.push_back(curDesciptor);

  } else if (root->type == 1){
    // this is map desciptor
    descriptors.push_back(curDesciptor);
    for (auto childNode : root->children){
      createDesciptorListTreeHelper(descriptors, childNode);
    }
    
  } else if (root->type == 2){
    // named directory desciptor 
    curDesciptor.name += "_START";
    descriptors.push_back(curDesciptor);

    for (auto childNode : root->children){
      createDesciptorListTreeHelper(descriptors, childNode);
    }
    Descriptor endDescriptor;
    endDescriptor.length = 0;
    endDescriptor.offset = 0;
    endDescriptor.name = root->name + "_END";
    descriptors.push_back(endDescriptor);
  } 


}

bool Wad::saveWad(){
  // save headers and traverse file tree to save descriptors 
  vector<Descriptor> desciptors;
  createDesciptorListTreeHelper(desciptors, this->treeRoot);

  std::fstream file(absolutePath, std::ios::in | std::ios::out | std::ios::binary);

  if (!file.is_open()) {
    return false;
  }

  // write num header information
  file.seekg(4, std::ios::beg);
  file.write((char*)&header.num_descriptors, sizeof(header.num_descriptors));
  file.seekp(8, std::ios::beg);
  file.write(reinterpret_cast<const char*>(&header.descriptor_offset), sizeof(header.descriptor_offset));

  // now go through and save the descriptors
  for (size_t i = 0; i < desciptors.size(); i += 1){
    uint32_t cur_offset = i * 16 + header.descriptor_offset;
    const Descriptor& curDesc = desciptors[i];
    char nameBuffer[8];
    std::memset(nameBuffer, 0, sizeof(nameBuffer)); 
    std::memcpy(nameBuffer, curDesc.name.c_str(), std::min(curDesc.name.length(), sizeof(nameBuffer)));
    file.seekp(cur_offset, std::ios::beg);
    file.write((char* )&curDesc.offset,  sizeof(curDesc.offset));
    file.write((char*)& curDesc.length ,sizeof(curDesc.length));
    file.write(nameBuffer, 8);
  
  }

  return true;
}
