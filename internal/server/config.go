package server

import "time"

type Config struct {
	ListenAddr      string
	ReadBufferBytes int
	MaxPacketSize   int
	QueueSize       int
	ReadTimeout     time.Duration
	ClientTTL       time.Duration
	CleanupInterval time.Duration
}

func DefaultConfig() Config {
	return Config{
		ListenAddr:      "0.0.0.0:9000",
		ReadBufferBytes: 0,
		MaxPacketSize:   4096,
		QueueSize:       1024,
		ReadTimeout:     200 * time.Millisecond,
		ClientTTL:       30 * time.Second,
		CleanupInterval: 5 * time.Second,
	}
}
