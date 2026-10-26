import socket
import requests
from urllib.parse import parse_qs

HOST = "0.0.0.0"
PORT = 9002

HEADERS = {
    "User-Agent": "PSPRA/0.1.0 (PSP Homebrew; Softcore) rcheevos/11.0",
    "Content-Type": "application/x-www-form-urlencoded"
}

def recv_all_from_psp(sock):
    chunks = []
    while True:
        data = sock.recv(4096)
        if not data:
            break
        chunks.append(data)
    return b"".join(chunks).decode('utf-8')

def log_request(url, post_data):
    if not post_data:
        print(f"[PSP -> RA] GET {url}")
        return
    
    params = parse_qs(post_data)
    command = params.get('r', ['unknown'])[0]
    user = params.get('u', [''])[0]
    
    print(f"[PSP -> RA] Command: '{command}' | User: '{user}' | URL: {url}")

server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind((HOST, PORT))
server.listen(5)

print(f"[PSPRA Proxy] Listening on {HOST}:{PORT}...")

while True:
    client_sock, addr = server.accept()
    try:
        raw_payload = recv_all_from_psp(client_sock)
        if "\n" in raw_payload:
            url, post_data = raw_payload.split("\n", 1)
            log_request(url, post_data)

            if post_data:
                resp = requests.post(url, data=post_data, headers=HEADERS, timeout=10)
            else:
                resp = requests.get(url, headers=HEADERS, timeout=10)

            print(f"[RA -> PSP] HTTP {resp.status_code} | Size: {len(resp.content)} bytes")
            client_sock.sendall(resp.content)
    except Exception as e:
        print(f"[PSPRA Proxy] Error: {e}")
    finally:
        client_sock.close()