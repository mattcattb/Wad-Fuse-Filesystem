#include <iostream>

std::string get_current_dir_name(){
  return "/";
}

// ./wadfs/wadfs -s somewadfile.wad /some/mount/directory 

int main(int argc, char * argv[]){
  if (argc <3) {
    std::cout << "not enough args" << std::endl;
  }

  std::string wadPath = argv[argc - 2];
  // relative path if / not there! must use absolute path
  if (wadPath.at(0) != '/'){
    wadPath = std::string(get_current_dir_name()) + "/" + wadPath;

    Wad * wad = Wad::loadWad(wadPath);
    argv[argc - 2] = argv[argc - 1];
    argc --;
    return fuse_main(argc, argv, &operations, myWad);
  }
}