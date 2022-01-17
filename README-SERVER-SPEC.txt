### THE TCP SERVER

The TCP server complies with the project specification, using the socket standard for interprocess communication to communicate with clients on a computer network.
The server takes a multithreaded approach to the distribution of the work requested by connecting clients. Each client connecting to the server, including those clients with multiple connections,
will be guaranteed a thread within which their requested work is conducted.

### PERFORMANT MULTITHREADING

The TCP server designed will use a multithreaded approach to enable parallel service of client requests.
Each client connecting to the server will be allocated a thread within which all client processing will occur.
However, to limit the size of the memory overhead of creating potentially hundreds of threads for connecting clients,
the server will contain a thread pool, such that threads can be reused throughout the runtime of the server, and are not destroyed
until the end of the process.

### PERFORMANT FILE HANDLING

All file handling in the server is to be conducted using memory mapping of files for enhanced performance.

### MULTIPLEXING OF FILE SERVICE

Upon each connection to the server, the contents of a prospective multiplexing file request are piped through shared memory to each thread also in the midst of
serving files to the client. The server delivers files in a byte-by-byte fashion, splitting the remainder of the request across the total number of valid requests for that file.

### COMPRESSION

Store elements of a compression dictionary in a globally accessible map data structure, where each element of the map is a linked list, containing coding of the same length. Each node in the linked list
contains the byte encoded for, the length of the encoding, and the encoding itself. Decompression involves matching bit seqeuences based on their length, and finding if a linked list exists with
that length, and searching the list for a matching sequence.
