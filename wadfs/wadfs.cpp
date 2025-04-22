#include <iostream>
#include <Wad.h>
#include "fuse.h"
#include <string.h> 
#include <errno.h>  
#include <sys/stat.h> 
#include <fcntl.h>  
#include <stdlib.h> 

#define WAD_DATA ((class Wad *) fuse_get_context()->private_data)

string get_parent_path_safe(const string& p) {
  if (p.empty() || p == "/") {
    return "/";
  }
  size_t last_slash = p.find_last_of('/');
  if (last_slash == string::npos) {
    return "/"; 
  }
  if (last_slash == 0) {
    // path is root
    return "/";
  }
  return p.substr(0, last_slash);
}

/*
./wadfs/wadfs -s somewadfile.wad /some/mount/directory 

The above requirements will be achieved using, at a minimum, the following six fuse callback functions:
get_attr, mknod, mkdir, read, write, and readdir
*/


static int wad_getattr(const char *path, struct stat *stbuf) {
  // returns file attributes
  memset(stbuf, 0, sizeof(struct stat));
  struct fuse_context *context = fuse_get_context();

  // ! may need to get the uuid or guid
  stbuf->st_gid = context->gid;
  stbuf->st_uid = context->uid;

  stbuf->st_atime = time( NULL ); 
	stbuf->st_mtime = time( NULL ); 

  string pathString(path);

  Wad * curWad = WAD_DATA;
  if (curWad->isDirectory(pathString)) {
    stbuf->st_mode = S_IFDIR | 0777;
		stbuf->st_nlink = 2;  
    return 0;
  } else if (curWad->isContent(pathString)) {

    stbuf->st_mode = S_IFREG | 0777;
    stbuf->st_nlink = 1;
    stbuf->st_size = curWad->getSize(pathString);
    return 0;
  } else {

    return -ENOENT;
  }
}

static int wad_mknod( const char *path, mode_t mode, dev_t rdev )
{
  // create a node (content file)
  Wad * currentWad = WAD_DATA;
  string pathString(path);
  if (currentWad->isDirectory(pathString) || currentWad->isContent(pathString)){
    // path already exists
    return -EEXIST;
  }

  string parentPath = get_parent_path_safe(pathString);
  if (!currentWad->isDirectory(parentPath)){
    // the parent path is not a directory, or is a map directory (cannot add files here!)
    return -ENOENT;
  }  
  currentWad->createFile(pathString);
	
	return 0;
}

static int wad_mkdir( const char* path, mode_t mode )
{
  Wad * curWad = WAD_DATA;
  string pathString(path);
  if (curWad->isContent(pathString) || curWad->isDirectory(pathString)) {
    return -EEXIST; 
  }

  string parent = get_parent_path_safe(pathString);
  // check if parent exists (if not root)
  if (parent != "/" && !curWad->isDirectory(parent) ){
    return -ENOENT; // Parent directory doesn't exist
  }

  curWad->createDirectory(pathString);
	
	return 0;
}


static int wad_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {

  Wad * curWad = WAD_DATA;
  string pathString(path);
  if (!curWad->isContent(pathString)){
    return -ENOENT;
  }
  // need the file size? 

  int bytesRead = curWad->getContents(pathString, buf, size, offset);
  return bytesRead;
}

static int wad_write( const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *info )
{
  // write content to a file!

  Wad* currentWad = WAD_DATA;
  string pathString(path);

  if (!currentWad->isContent(pathString)) {
    if (currentWad->isDirectory(pathString)) {
        return -EISDIR; // Can't write to a directory path
    } else {
        return -ENOENT; // File doesn't exist 
    }
  }

  int bytes_written = currentWad->writeToFile(pathString, buffer, size, offset);
  
  return bytes_written;
}


static int wad_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi) {
  // list of files/directories that reside in a given directory
  // fill with data for the cwd ., and parent ..  

  (void) offset;
  (void) fi;

  Wad * cur = WAD_DATA;

  if (!cur->isDirectory(path)){
    struct stat st_check;
    if (wad_getattr(path, &st_check) == -ENOENT) {
         return -ENOENT; // Path doesn't exist at all
    } else {
         return -ENOTDIR; // Path exists but is not a directory
    }
  }

  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);
  vector<string> filesInDirectory;
  int dirResult = cur->getDirectory(path, &filesInDirectory);

  for (const std::string& entryName : filesInDirectory) {
    struct stat st;
    std::string fullEntryPath;
    if (filler(buf, entryName.c_str(), NULL, 0) != 0) {
      return 0;
    }
  }

  return 0;
}


struct fuse_operations wad_fuse_operations = {
  .getattr = wad_getattr,
  .mknod = wad_mknod,
  .mkdir = wad_mkdir,
  .read = wad_read,
  .write = wad_write,
  .readdir = wad_readdir
};


int main(int argc, char * argv[]){
  if (argc <3) {
    std::cout << "not enough args" << std::endl;
    exit(EXIT_SUCCESS);
  }

  std::string wadPath = argv[argc - 2];

  Wad * myWad = Wad::loadWad(wadPath);
  argv[argc - 2] = argv[argc - 1];
  argc --;
  return fuse_main(argc, argv, &wad_fuse_operations, myWad);

}