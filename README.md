# Wad Fuse Filesystem

## Installation

### 1. Install libfuse
```bash
sudo apt install libfuse-dev fuse
sudo chmod 666 /dev/fuse
```

### 2. Make files
```bash
cd libWad 
make
cd ..
cd wadfs
make
cd ..
```

### 3. Run Fuse with a sample wad directory and a created mount directory
```bash
mkdir exampleMountDir
./wadfs/wadfs -s example/.wad ./exampleMountDir
'''
