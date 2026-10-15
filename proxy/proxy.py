import socket
import requests

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

server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind((HOST, PORT))
server.listen(5)

print(f"[PSPRA Proxy] Escutando na porta {PORT}...")

while True:
    client_sock, addr = server.accept()
    try:
        raw_payload = recv_all_from_psp(client_sock)
        if "\n" in raw_payload:
            url, post_data = raw_payload.split("\n", 1)
            print(f"[PSP -> RA] URL: {url} | Dados: {post_data[:60]}...")

            if post_data:
                resp = requests.post(url, data=post_data, headers=HEADERS, timeout=10)
            else:
                resp = requests.get(url, headers=HEADERS, timeout=10)

            print(f"[RA -> PSP] Status: {resp.status_code} | Bytes: {len(resp.content)}")
            client_sock.sendall(resp.content)
    except Exception as e:
        print(f"[ERRO] {e}")
    finally:
        client_sock.close()