const WebSocket = require('ws');
const net = require('net');

class TCPProxy {
    constructor(wsPort = 3000) {
        this.wsPort = wsPort;
        this.connections = new Map();
        this.initWebSocketServer();
    }

    initWebSocketServer() {
        this.wss = new WebSocket.Server({ port: this.wsPort });
        console.log(`WebSocket proxy server listening on port ${this.wsPort}`);

        this.wss.on('connection', (ws) => {
            console.log('New WebSocket client connected');
            let tcpClient = null;

            ws.on('message', (message) => {
                try {
                    const data = JSON.parse(message);
                    
                    if (data.type === 'connect') {
                        this.connectToTCPServer(ws, data.address, data.port);
                    } else if (tcpClient && tcpClient.connected) {
                        this.handleClientMessage(tcpClient, data);
                    }
                } catch (e) {
                    console.error('Error parsing message:', e);
                    ws.send(JSON.stringify({ type: 'error', message: e.message }));
                }
            });

            ws.on('close', () => {
                console.log('WebSocket client disconnected');
                if (tcpClient) {
                    tcpClient.destroy();
                }
            });
        });
    }

    connectToTCPServer(ws, address, port) {
        const tcpClient = new net.Socket();
        
        tcpClient.connect(port, address, () => {
            console.log(`Connected to TCP server at ${address}:${port}`);
            tcpClient.connected = true;
            this.connections.set(ws, tcpClient);
        });

        tcpClient.on('data', (data) => {
            this.handleTCPResponse(ws, data);
        });

        tcpClient.on('error', (err) => {
            console.error('TCP connection error:', err);
            ws.send(JSON.stringify({ type: 'error', message: err.message }));
        });

        tcpClient.on('close', () => {
            console.log('TCP connection closed');
            tcpClient.connected = false;
            this.connections.delete(ws);
        });

        return tcpClient;
    }

    handleClientMessage(tcpClient, data) {
        let header, payload;

        switch(data.type) {
            case 'echo':
                header = this.createHeader(0x0, data.compress, data.payload.length);
                payload = Buffer.from(data.payload, 'utf8');
                break;
            
            case 'filesize':
                header = this.createHeader(0x2, data.compress, data.payload.length);
                payload = Buffer.from(data.payload, 'utf8');
                break;
            
            case 'retrieve':
                header = this.createHeader(0x4, data.compress, data.payload.length);
                payload = Buffer.from(data.payload, 'utf8');
                break;
            
            case 'list':
                header = this.createHeader(0x6, false, 0);
                payload = Buffer.alloc(0);
                break;
            
            default:
                console.error('Unknown message type:', data.type);
                return;
        }

        const message = Buffer.concat([header, payload]);
        tcpClient.write(message);
    }

    createHeader(type, requiresCompression, length) {
        const header = Buffer.alloc(9);
        
        // First byte: type (4 bits), compression (1 bit), requires_compression (1 bit), padding (2 bits)
        let firstByte = (type << 4);
        if (requiresCompression) {
            firstByte |= (1 << 2); // Set requires_compression bit
        }
        header[0] = firstByte;
        
        // Next 8 bytes: length (big-endian)
        header.writeBigUInt64BE(BigInt(length), 1);
        
        return header;
    }

    handleTCPResponse(ws, data) {
        if (data.length < 9) {
            ws.send(JSON.stringify({ type: 'error', message: 'Invalid response from server' }));
            return;
        }

        const header = data[0];
        const type = (header >> 4) & 0x0F;
        const compressed = (header >> 3) & 0x01;
        const length = data.readBigUInt64BE(1);
        const payload = data.slice(9);

        let responseType, responseData;

        switch(type) {
            case 0x0: // Echo response
                responseType = 'echo';
                responseData = payload.toString('utf8');
                break;
            
            case 0x2: // File size response
                responseType = 'filesize';
                if (payload.length >= 8) {
                    responseData = payload.readBigUInt64BE(0).toString();
                } else {
                    responseData = 'Invalid size response';
                }
                break;
            
            case 0x4: // File retrieve response
                responseType = 'file';
                responseData = payload.toString('utf8');
                break;
            
            case 0x6: // Directory listing response
                responseType = 'list';
                responseData = payload.toString('utf8');
                break;
            
            case 0xF: // Error
                responseType = 'error';
                responseData = 'Server error';
                break;
            
            default:
                responseType = 'unknown';
                responseData = `Unknown response type: ${type}`;
        }

        ws.send(JSON.stringify({
            type: responseType,
            data: responseData,
            compressed: compressed
        }));
    }
}

// Start the proxy server
new TCPProxy(3000);