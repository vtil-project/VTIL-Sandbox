# VTIL-Sandbox

VTIL-Sandbox is a local VTIL inspection tool with:
- a C++ backend (`VTIL-Sandbox`) that parses uploaded `.vtil` routines and exposes JSON over HTTP
- a Vue frontend (`VTIL-SandboxFrontend`) that visualizes blocks, CFG edges, and instruction details

## Current Architecture

- Backend API: `http://127.0.0.1:8090`
- Frontend dev server: `http://127.0.0.1:8080` (Vue CLI)
- Data flow:
  1. Frontend uploads `.vtil` bytes to backend
  2. Backend deserializes VTIL and builds rich JSON (blocks, CFG, instructions, descriptor/operand metadata)
  3. Frontend renders interactive block list, CFG view, and instruction inspector

## Repository Layout

- `VTIL-Core/` - VTIL core libraries
- `VTIL-Sandbox/` - C++ backend server
- `VTIL-SandboxFrontend/` - Vue frontend UI
- `CMakeLists.txt` - root build entry point

## Prerequisites

- CMake 3.14+
- MSVC (Windows)
- Node.js + npm

## Quick Start

### 1) Build backend

```bat
cmake -S . -B output -DVTIL_SANDBOX_BUILD_FRONTEND=OFF
cmake --build output --config Release --target sandbox
```

Tip: use `--config Debug` while actively debugging.

### 2) Run backend

```bat
output\VTIL-Sandbox\Release\sandbox.exe
```

If you built Debug instead, run:

```bat
output\VTIL-Sandbox\Debug\sandbox.exe
```

Backend should listen on:
- `http://127.0.0.1:8090`

### 3) Run frontend

```bat
cd VTIL-SandboxFrontend
npm install
npm run serve
```

Then open:
- `http://127.0.0.1:8080`

## API Endpoints

- `GET /health`
  - basic readiness probe
- `GET /api/state`
  - returns current routine state as JSON
- `POST /api/upload?name=<filename>`
  - body: raw `.vtil` bytes (`application/octet-stream`)

## Frontend Features

- block list + selection
- CFG view with controls (`+`, `-`, `100%`, `Fit`)
- incoming/outgoing edge highlighting for selected block
- instruction search and jump (`VIP` / `mnemonic` / `text`) with configurable priority
- expandable instruction rows with descriptor/operand inspector-style details
- dark mode

## Notes

- This is currently designed for local use (loopback API).
- Backend includes request size limits and timeout handling for safer local operation.
- If backend is not running, frontend status shows API offline.

## Development

Build frontend production bundle:

```bat
cd VTIL-SandboxFrontend
npm run build
```

Build backend only:

```bat
cmake --build output --config Release --target sandbox
```
