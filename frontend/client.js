class MultiplexingClient {
    constructor() {
        this.socket = null;
        this.connected = false;
        this.initializeEventListeners();
    }

    initializeEventListeners() {
        // Connection buttons
        document.getElementById('connectBtn').addEventListener('click', () => this.connect());
        document.getElementById('disconnectBtn').addEventListener('click', () => this.disconnect());
        
        // Operation buttons
        document.getElementById('sendEchoBtn').addEventListener('click', () => this.sendEcho());
        document.getElementById('getFileSizeBtn').addEventListener('click', () => this.getFileSize());
        document.getElementById('retrieveFileBtn').addEventListener('click', () => this.retrieveFile());
        document.getElementById('listDirBtn').addEventListener('click', () => this.listDirectory());
        
        // Clear buttons
        document.getElementById('clearResponseBtn').addEventListener('click', () => this.clearResponse());
        document.getElementById('clearLogBtn').addEventListener('click', () => this.clearLog());
    }

    connect() {
        const address = document.getElementById('serverAddress').value;
        const port = document.getElementById('serverPort').value;
        
        this.addLog(`Connecting to ${address}:${port}...`);
        
        // Connect to WebSocket proxy server
        this.socket = new WebSocket(`ws://localhost:3000`);
        
        this.socket.onopen = () => {
            this.connected = true;
            this.updateConnectionStatus(true);
            this.addLog('Connected successfully', 'success');
            
            // Send connection info to proxy
            this.socket.send(JSON.stringify({
                type: 'connect',
                address: address,
                port: parseInt(port)
            }));
        };
        
        this.socket.onmessage = (event) => {
            this.handleMessage(event.data);
        };
        
        this.socket.onerror = (error) => {
            this.addLog(`Connection error: ${error}`, 'error');
        };
        
        this.socket.onclose = () => {
            this.connected = false;
            this.updateConnectionStatus(false);
            this.addLog('Disconnected from server');
        };
    }

    disconnect() {
        if (this.socket) {
            this.socket.close();
            this.socket = null;
        }
    }

    updateConnectionStatus(connected) {
        const status = document.getElementById('connectionStatus');
        const connectBtn = document.getElementById('connectBtn');
        const disconnectBtn = document.getElementById('disconnectBtn');
        const operationsPanel = document.getElementById('operationsPanel');
        
        if (connected) {
            status.textContent = 'Connected';
            status.className = 'status connected';
            connectBtn.disabled = true;
            disconnectBtn.disabled = false;
            operationsPanel.style.display = 'block';
        } else {
            status.textContent = 'Disconnected';
            status.className = 'status disconnected';
            connectBtn.disabled = false;
            disconnectBtn.disabled = true;
            operationsPanel.style.display = 'none';
        }
    }

    sendEcho() {
        const message = document.getElementById('echoMessage').value;
        const compress = document.getElementById('echoCompress').checked;
        
        if (!message) {
            this.addResponse('Please enter a message to echo', 'error');
            return;
        }
        
        const data = {
            type: 'echo',
            payload: message,
            compress: compress
        };
        
        this.addLog(`Sending echo: "${message}" (compression: ${compress})`);
        this.socket.send(JSON.stringify(data));
    }

    getFileSize() {
        const fileName = document.getElementById('fileName').value;
        const compress = document.getElementById('fileCompress').checked;
        
        if (!fileName) {
            this.addResponse('Please enter a file name', 'error');
            return;
        }
        
        const data = {
            type: 'filesize',
            payload: fileName,
            compress: compress
        };
        
        this.addLog(`Requesting file size: "${fileName}"`);
        this.socket.send(JSON.stringify(data));
    }

    retrieveFile() {
        const fileName = document.getElementById('fileName').value;
        const compress = document.getElementById('fileCompress').checked;
        
        if (!fileName) {
            this.addResponse('Please enter a file name', 'error');
            return;
        }
        
        // Store the filename for when we receive the response
        this.pendingFileName = fileName;
        
        const data = {
            type: 'retrieve',
            payload: fileName,
            compress: compress
        };
        
        this.addLog(`Retrieving file: "${fileName}"`);
        this.socket.send(JSON.stringify(data));
    }

    listDirectory() {
        const data = {
            type: 'list',
            payload: '',
            compress: false
        };
        
        this.addLog('Requesting directory listing');
        this.socket.send(JSON.stringify(data));
    }

    handleMessage(data) {
        try {
            const response = JSON.parse(data);
            
            switch(response.type) {
                case 'echo':
                    this.addResponse(`Echo: ${response.data}`, 'success');
                    break;
                case 'filesize':
                    this.addResponse(`File size: ${response.data} bytes`, 'success');
                    break;
                case 'file':
                    console.log('File response received, calling handleFileResponse');
                    this.handleFileResponse(response.data, this.pendingFileName, response.encoding);
                    this.pendingFileName = null; // Clear after use
                    break;
                case 'list':
                    this.handleDirectoryListing(response.data);
                    break;
                case 'error':
                    this.addResponse(`Error: ${response.message}`, 'error');
                    break;
                default:
                    console.log('Unknown response type:', response.type, 'Full response:', response);
                    this.addResponse(`Unknown response type: ${response.type}`);
                    // If it looks like file data, show it
                    if (response.data) {
                        this.addResponse(response.data);
                    }
            }
        } catch (e) {
            this.addResponse(`Failed to parse response: ${e.message}`, 'error');
        }
    }

    handleFileResponse(data, filename, encoding) {
        console.log('handleFileResponse called with:', {
            dataLength: data ? data.length : 0,
            filename: filename,
            encoding: encoding
        });
        
        let binaryData;
        
        try {
            if (encoding === 'base64') {
                // Convert base64 to binary
                console.log('Decoding base64 data...');
                const binaryString = atob(data);
                const bytes = new Uint8Array(binaryString.length);
                for (let i = 0; i < binaryString.length; i++) {
                    bytes[i] = binaryString.charCodeAt(i);
                }
                binaryData = bytes;
                console.log('Decoded to', binaryData.length, 'bytes');
            } else {
                // Handle as text
                console.log('Encoding as text...');
                binaryData = new TextEncoder().encode(data);
            }
            
            // Create blob and trigger download
            const blob = new Blob([binaryData], { type: 'application/octet-stream' });
            const url = window.URL.createObjectURL(blob);
            console.log('Created blob URL:', url);
            
            // Create a temporary anchor element to trigger download
            const a = document.createElement('a');
            a.href = url;
            a.download = filename || 'download';
            a.style.display = 'none';
            document.body.appendChild(a);
            console.log('Triggering download for:', a.download);
            a.click();
            
            // Clean up with a small delay
            setTimeout(() => {
                document.body.removeChild(a);
                window.URL.revokeObjectURL(url);
            }, 100);
            
            this.addResponse(`File downloaded: ${filename || 'file'} (${binaryData.length} bytes)`, 'success');
        } catch (error) {
            console.error('Error in handleFileResponse:', error);
            this.addResponse(`Error downloading file: ${error.message}`, 'error');
        }
    }

    handleDirectoryListing(data) {
        // Server uses null bytes as separators
        const files = data.split('\0').filter(f => f.trim());
        this.addResponse(`Directory listing (${files.length} items):`, 'success');
        files.forEach(file => {
            this.addResponse(`  - ${file}`);
        });
    }

    addResponse(message, type = '') {
        const responseArea = document.getElementById('responseArea');
        const placeholder = responseArea.querySelector('.placeholder');
        if (placeholder) {
            placeholder.remove();
        }
        
        const item = document.createElement('div');
        item.className = `response-item ${type}`;
        item.innerHTML = `<span class="timestamp">${new Date().toLocaleTimeString()}</span>${this.escapeHtml(message)}`;
        responseArea.appendChild(item);
        responseArea.scrollTop = responseArea.scrollHeight;
    }

    addLog(message, type = '') {
        const logArea = document.getElementById('logArea');
        const placeholder = logArea.querySelector('.placeholder');
        if (placeholder) {
            placeholder.remove();
        }
        
        const item = document.createElement('div');
        item.className = `log-item ${type}`;
        item.innerHTML = `<span class="timestamp">${new Date().toLocaleTimeString()}</span>${this.escapeHtml(message)}`;
        logArea.appendChild(item);
        logArea.scrollTop = logArea.scrollHeight;
    }

    clearResponse() {
        const responseArea = document.getElementById('responseArea');
        responseArea.innerHTML = '<p class="placeholder">Responses will appear here...</p>';
    }

    clearLog() {
        const logArea = document.getElementById('logArea');
        logArea.innerHTML = '<p class="placeholder">Connection logs will appear here...</p>';
    }

    escapeHtml(text) {
        const div = document.createElement('div');
        div.textContent = text;
        return div.innerHTML;
    }
}

// Initialize client when DOM is loaded
document.addEventListener('DOMContentLoaded', () => {
    new MultiplexingClient();
});