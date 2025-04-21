#include <iostream>
#include <fstream>

using namespace std;

int main(){
  fstream file;

  file.open("sample1.wad", ios::in | ios::out | ios::binary);

  if (!file.is_open()){
    return -1;
  }

  char magic[5];
  magic[4] = '\0';

  uint32_t numDescriptors;
  uint32_t descriptorOffset;

  file.read(magic, 4);
  file.read((char*)&numDescriptors, 4); // 4 bytes (32 bits)
  file.read((char*)&descriptorOffset, 4); // 4 bytes (32 bits)

  cout << "Magic: " << magic << "\n"
  << "num descriptors: " << numDescriptors << "\n"
  << "descriptor offset: " << descriptorOffset << std::endl;

  return 0;
}