package main

import (
	"context"
	"flag"
	"log"
	"os"
	"os/signal"
	"syscall"

	"github.com/cdxy1/caller-min/internal/server"
)

func main() {
	cfg := server.DefaultConfig()

	flag.StringVar(&cfg.ListenAddr, "listen", cfg.ListenAddr, "udp listen address (host:port)")
	flag.IntVar(&cfg.ReadBufferBytes, "read-buffer", cfg.ReadBufferBytes, "udp read buffer size in bytes (0 = OS default)")
	flag.IntVar(&cfg.MaxPacketSize, "max-packet", cfg.MaxPacketSize, "max packet size in bytes")
	flag.IntVar(&cfg.QueueSize, "queue", cfg.QueueSize, "internal packet queue size")
	flag.DurationVar(&cfg.ReadTimeout, "read-timeout", cfg.ReadTimeout, "udp read timeout for shutdown checks")
	flag.DurationVar(&cfg.ClientTTL, "client-ttl", cfg.ClientTTL, "remove clients after this idle duration (0 = disable)")
	flag.DurationVar(&cfg.CleanupInterval, "cleanup-interval", cfg.CleanupInterval, "how often to prune inactive clients")
	flag.Parse()

	logger := log.New(os.Stdout, "", log.LstdFlags)

	srv, err := server.New(cfg, logger)
	if err != nil {
		logger.Fatalf("server init: %v", err)
	}

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	logger.Printf("listening on %s", cfg.ListenAddr)
	if err := srv.Run(ctx); err != nil {
		logger.Printf("server stopped: %v", err)
	}
}
