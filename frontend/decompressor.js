const fs = require('fs');

class Decompressor {
    constructor() {
        this.dictionary = null;
        this.loadDictionary();
    }

    loadDictionary() {
        try {
            // Load the compression dictionary file
            const dictPath = '../(sample)compression.dict';
            const buffer = fs.readFileSync(dictPath);
            
            this.dictionary = [];
            let currentByte = 0;
            let currentBit = 8;
            
            // Parse the dictionary (256 entries)
            for (let i = 0; i < 256; i++) {
                // Read 8 bits for code length
                let codeLength = 0;
                for (let j = 7; j >= 0; j--) {
                    if (currentByte < buffer.length) {
                        const bit = (buffer[currentByte] >> (currentBit - 1)) & 1;
                        codeLength |= (bit << j);
                        currentBit--;
                        if (currentBit === 0) {
                            currentBit = 8;
                            currentByte++;
                        }
                    }
                }
                
                // Read the code bits
                const code = [];
                for (let j = 0; j < codeLength; j++) {
                    if (currentByte < buffer.length) {
                        const bit = (buffer[currentByte] >> (currentBit - 1)) & 1;
                        code.push(bit);
                        currentBit--;
                        if (currentBit === 0) {
                            currentBit = 8;
                            currentByte++;
                        }
                    }
                }
                
                this.dictionary.push({
                    byte: i,
                    codeLength: codeLength,
                    code: code
                });
            }
            
            console.log('Compression dictionary loaded:', this.dictionary.length, 'entries');
        } catch (error) {
            console.error('Failed to load compression dictionary:', error);
            this.dictionary = null;
        }
    }

    decompress(compressedData) {
        if (!this.dictionary) {
            console.error('No compression dictionary available');
            return compressedData;
        }

        if (compressedData.length === 0) {
            return Buffer.alloc(0);
        }

        // Last byte contains the number of padding bits
        const paddingBits = compressedData[compressedData.length - 1];
        const totalBits = ((compressedData.length - 1) * 8) - paddingBits;
        
        const decompressed = [];
        let currentCode = [];
        let bitsProcessed = 0;
        
        // Process each bit
        for (let byteIdx = 0; byteIdx < compressedData.length - 1; byteIdx++) {
            for (let bitIdx = 7; bitIdx >= 0; bitIdx--) {
                if (bitsProcessed >= totalBits) {
                    break;
                }
                
                const bit = (compressedData[byteIdx] >> bitIdx) & 1;
                currentCode.push(bit);
                bitsProcessed++;
                
                // Check if current code matches any dictionary entry
                for (const entry of this.dictionary) {
                    if (entry.codeLength === currentCode.length) {
                        let match = true;
                        for (let i = 0; i < entry.codeLength; i++) {
                            if (entry.code[i] !== currentCode[i]) {
                                match = false;
                                break;
                            }
                        }
                        if (match) {
                            decompressed.push(entry.byte);
                            currentCode = [];
                            break;
                        }
                    }
                }
            }
        }
        
        return Buffer.from(decompressed);
    }
}

module.exports = Decompressor;