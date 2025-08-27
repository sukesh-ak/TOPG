# TOPG - TOP for GPU (Nvidia GPU)
TOPG GPU Monitoring Server - Real-time GPU stats via WebSocket  

### Dashboard  
![alt text](assets/webui-dash.png)

### Connection Settings  
![alt text](assets/webui-settings.png)


## How to compile the server

### On Linux/WSL2/Windows
Install dependencies using [vcpkg - (VC++ Package Manager)](https://vcpkg.io/en/index.html) 

```bash
# Clone this repository 
$ git clone  https://github.com/sukesh-ak/topg.git
$ cd topg/server

# Grab vcpkg
$ git clone https://github.com/microsoft/vcpkg.git

# Run the bootstrap script for vcpkg
# Linux
$ ./vcpkg/bootstrap-vcpkg.sh  

# Windows
.\vcpkg\bootstrap-vcpkg.bat   
```

### Compile and Run
```bash
$ cmake . -B build -DCMAKE_TOOLCHAIN_FILE="vcpkg/scripts/buildsystems/vcpkg.cmake"
$ cmake --build build/

# Run executable with default parameters
# Linux
$ ./build/topg

# Windows
./build/debug/topg.exe
```

## How to run the server
```bash
TOPG GPU Monitoring Server - Real-time GPU stats via WebSocket
Usage:
  topgsrv [OPTION...]

  -h, --host arg  Host address to bind to (default: 0.0.0.0)
  -p, --port arg  Port to listen on (default: 8080)
      --help      Print usage information
```

## Websocket Web UI Client for visualization
```bash
# Run this from webui folder. 
$ cd topg/webui

# You can host this folder with anything. Here we use python
$ python3 -m http.server 8081
```
