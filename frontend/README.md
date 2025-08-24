# Multiplexing Server Frontend Client

A web-based frontend client for interacting with the multiplexing TCP server.

## Architecture

The frontend consists of three main components:

1. **Web Interface** (`index.html`, `styles.css`, `client.js`) - A modern, responsive web UI
2. **WebSocket Proxy Server** (`proxy-server.js`) - Bridges WebSocket connections from the browser to TCP connections
3. **Protocol Handler** - Implements the custom binary protocol for server communication

## Features

- **Connection Management**: Connect/disconnect to the TCP server
- **Echo Messages**: Send messages with optional compression
- **File Operations**: 
  - Get file size
  - Retrieve file contents
  - List directory contents
- **Real-time Response Display**: View server responses and connection logs
- **Compression Support**: Optional compression for all operations

## Setup and Installation

1. Install Node.js dependencies:
```bash
cd frontend
npm install
```

2. Create a server configuration file (for the TCP server):
```bash
# Create a config file with server IP (4 bytes), port (2 bytes), and directory path
# Example: localhost (127.0.0.1), port 8080, directory "./files"
```

3. Start the TCP server (from parent directory):
```bash
make server
./server config.bin
```

4. Start the WebSocket proxy server:
```bash
npm start
```

5. Open the web interface:
- Open `index.html` in a web browser
- Or serve it with a local HTTP server:
```bash
python3 -m http.server 8000
# Then navigate to http://localhost:8000/index.html
```

## Protocol Implementation

The client implements the custom binary protocol:

### Message Header Format (9 bytes)
- Byte 0: Type (4 bits) | Compression (1 bit) | Requires Compression (1 bit) | Padding (2 bits)
- Bytes 1-8: Payload length (64-bit big-endian)

### Message Types
- `0x0`: Echo - Server echoes back the message
- `0x2`: File Size - Get size of a file
- `0x4`: Retrieve File - Get file contents
- `0x6`: List Directory - List files in server directory
- `0x8`: Shutdown - Close server connection
- `0xF`: Error - Error response from server

## Usage

1. Enter server address and port in the connection panel
2. Click "Connect" to establish connection
3. Use the operations panel to:
   - Send echo messages
   - Query file sizes
   - Retrieve files
   - List directory contents
4. View responses in the response panel
5. Monitor connection status in the log panel

## Development

To run in development mode with auto-restart:
```bash
npm run dev
```

## Notes

- The WebSocket proxy server runs on port 3000 by default
- Ensure the TCP server is running before connecting
- File operations work with files in the server's configured directory