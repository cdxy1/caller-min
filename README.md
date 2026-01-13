# caller-min

Minimal UDP audio relay (server) with a lightweight PortAudio client.

## Overview
- Server accepts UDP audio packets and forwards them to all other connected clients.
- Client captures mono 16-bit PCM audio and plays incoming audio in real time.
- Designed for simple LAN experiments; no codec, encryption, or mixing.

## Build
Server (Go):
```sh
go build -o bin/server ./cmd/server
```

Client (C + PortAudio):
```sh
cc cmd/client_c/main.c -o bin/client_c -lportaudio -lpthread
```

Or build both:
```sh
make build
```

## Server usage
```sh
bin/server -listen 0.0.0.0:9000
```

Flags:
- `-listen` UDP listen address (default `0.0.0.0:9000`)
- `-read-buffer` UDP read buffer bytes (0 = OS default)
- `-max-packet` max packet size in bytes (default `4096`)
- `-queue` internal queue size (default `1024`)
- `-read-timeout` read timeout to allow clean shutdown (default `200ms`)
- `-client-ttl` prune idle clients after this duration (default `30s`)
- `-cleanup-interval` how often to prune inactive clients (default `5s`)

## Client usage
```sh
bin/client_c -s 192.168.1.27:9000 -i 10 -o 10 -f 640
```

Flags:
- `-s` server address (default `127.0.0.1:9000`)
- `-r` sample rate (default `16000`)
- `-f` frames per buffer (default `320`)
- `-l` list audio devices and exit
- `-i` input device index
- `-o` output device index

## Notes
- All clients should use the same sample rate and frames-per-buffer values.
- This project forwards raw PCM data; it does not mix, compress, or encrypt audio.
- Significant part of the client was written by GPT-5.2 Codex
