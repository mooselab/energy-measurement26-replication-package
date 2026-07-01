#this was working as of aug 17,,, i broke the fetcher code somehow
#nvm fixed now still good circa aug 26
import http.server
import socketserver
import socket
import threading
import time
import struct
import re
import numpy 
import os
import signal

# Configuration
MY_IP = "100.100.100.155" #specify a bind ip to avoid some issues when using multiple dongles
#TARGET_IP = "132.207.153.254"  # Teensy server address
TARGET_IP = "100.100.100.100"
TARGET_PORT = 80               # Teensy server port
HTTP_PORT = 8080               # Local HTTP server port

def tcp_client():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    print(f"Connecting to {TARGET_IP}:{TARGET_PORT}...")
    s.bind((MY_IP, 0))
    s.connect((TARGET_IP, TARGET_PORT))
    print("TCP connection established.")

    # Tell the Teensy this connection is a logger
    s.sendall(b"logger\n")
    print("Sent 'logger'")
    
    msgbytes = 0
    remainingbytes = 0 #rollover bytes
    write_buffer_size = 2000000 #real buffer size
    #write_buffer_size = 4000 #slow mode
    buffer_size = 2000 #toy buffer size
    buffer = numpy.empty(buffer_size, dtype=numpy.uint8)
    write_buffer = numpy.empty(write_buffer_size, dtype=numpy.uint8)
    write_buffer_offset = 0
    offset = 0
    fileCounter = 0
    has_metadata = False
    while True:
        # Step 1: read header bytes indicating how long is the message
        if (remainingbytes == 0):
            #can assume msgbytes are all in buffer at this point.
            #print(f"cntr{fileCounter}")
            if (not has_metadata and offset != 0):#special case for metadata
                with open("metadata", "wb") as f:
                    f.write(memoryview(buffer)[:msgbytes]) #only write msg bytes
                    buffer.fill(0)#reset buffer
                    offset = 0
                    has_metadata = True
                    msgbytes = 0
            elif (offset > 0): #offset>0 signals we have data in buffer
                if (write_buffer_offset >= write_buffer_size - buffer_size):
                    #if remaining write buffer size < buffer size, write.
                    filename = f"data{fileCounter}.bin"
                    with open(filename, "wb") as f:
                        f.write(memoryview(write_buffer)[:write_buffer_offset])#only write msg bytes
                    fileCounter += 1
                    write_buffer.fill(0)
                    write_buffer_offset = 0
                    print("saving")
                write_buffer[write_buffer_offset : write_buffer_offset + msgbytes] = buffer[0:msgbytes]
                write_buffer_offset = write_buffer_offset + msgbytes #we can assume the offset pointer always tails the last bit of data.
                buffer.fill(0)#reset buffer
                offset = 0
                msgbytes = 0


            headerbytes = s.recv(4)
            #print(int.from_bytes(headerbytes,byteorder='little', signed=False))
            if len(headerbytes) == 0:
                #print("No connection?")
                break
            if len(headerbytes) < 4:
                #print(f"Partial header ({len(headerbytes)} bytes), skipping.")
                continue

            length = int.from_bytes(headerbytes,byteorder='little', signed=False)
            msgbytes = length
        else:
            length = remainingbytes
            #print(f"message is length {length}")
        #end of header bytes section 
        #print(f"offset@{offset}rcv{length}Bytes")
        recv_bytes = s.recv_into(memoryview(buffer)[offset:], length) #recv_into return number of bytes read
        offset += recv_bytes
        remainingbytes = length - recv_bytes

            #dataString = bytes(memoryview(buffer)[0:offset+10]).decode('utf-8', errors='replace')
            #trimmedString = re.sub(r'\n+\Z', '', dataString)
            #print(trimmedString)
            #print(offset)
            #print(offset*1.0/buffer_size)



# --- HTTP handler (unchanged) ---
class SimpleHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"HTTP server is running.\n")


if __name__ == "__main__":
    print(os.getcwd())
    os.chdir("C:/Users/andyh/Downloads/serialreceive/trace2")
    print(os.getcwd())
    threading.Thread(target=tcp_client, daemon=True).start()

    with socketserver.TCPServer(("", HTTP_PORT), SimpleHandler) as httpd:
        print(f"HTTP server running on port {HTTP_PORT}")
        httpd.serve_forever()