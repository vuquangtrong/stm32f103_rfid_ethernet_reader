import socket

'''
Message format:
- ALIVE:
    + every 10 seconds
    + 8 bytes: [ID0][ID1][ID2][0x00][0xFF][0xFF][0xFF][0xFF]
- CARD_UID:
    + when card is detected
    + 8 bytes: [ID0][ID1][ID2][0x01][UID0][UID1][UID2][UID3]
'''

def start_udp_server(host='0.0.0.0', port=12345):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server_socket:
        server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        server_socket.bind((host, port))
        print(f"UDP server started on {host}:{port}")

        while True:
            message, client_address = server_socket.recvfrom(1024)
            print(f"Received {len(message)} bytes: {message.hex()}")
            reader = message[0:3]
            type = message[3]
            payload = message[4:]
            if type == 0x00 and payload == b'\xff\xff\xff\xff':
                print(f"{reader.hex()} Alive")
            elif type == 0x01:
                print(f"{reader.hex()} Card ID: {payload.hex()}")
            else:
                print(f"{reader.hex()} Unknown type: {type}")


if __name__ == "__main__":
    start_udp_server()
