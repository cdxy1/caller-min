package server

import (
	"context"
	"fmt"
	"log"
	"net"
	"os"
	"time"

	"github.com/cdxy1/caller-min/internal/receiver"
	"github.com/cdxy1/caller-min/internal/registry"
	"github.com/cdxy1/caller-min/internal/sender"
	"github.com/cdxy1/caller-min/internal/types"
)

type Server struct {
	cfg    Config
	conn   *net.UDPConn
	reg    *registry.Registry
	logger *log.Logger
}

func New(cfg Config, logger *log.Logger) (*Server, error) {
	if cfg.ListenAddr == "" {
		return nil, fmt.Errorf("listen address required")
	}
	if cfg.MaxPacketSize <= 0 {
		return nil, fmt.Errorf("max packet size must be positive")
	}
	if cfg.QueueSize <= 0 {
		return nil, fmt.Errorf("queue size must be positive")
	}

	addr, err := net.ResolveUDPAddr("udp", cfg.ListenAddr)
	if err != nil {
		return nil, fmt.Errorf("resolve udp address: %w", err)
	}

	conn, err := net.ListenUDP("udp", addr)
	if err != nil {
		return nil, fmt.Errorf("listen udp: %w", err)
	}

	if cfg.ReadBufferBytes > 0 {
		if err := conn.SetReadBuffer(cfg.ReadBufferBytes); err != nil {
			conn.Close()
			return nil, fmt.Errorf("set udp read buffer: %w", err)
		}
	}

	if logger == nil {
		logger = log.New(os.Stdout, "", log.LstdFlags)
	}

	return &Server{
		cfg:    cfg,
		conn:   conn,
		reg:    registry.NewRegistry(),
		logger: logger,
	}, nil
}

func (s *Server) Run(ctx context.Context) error {
	defer s.conn.Close()

	rxCh := make(chan types.Packet, s.cfg.QueueSize)
	recv := receiver.NewReceiver(s.conn, rxCh, s.reg, s.logger, receiver.Config{
		MaxPacketSize: s.cfg.MaxPacketSize,
		ReadTimeout:   s.cfg.ReadTimeout,
	})
	send := sender.NewSender(s.conn, rxCh, s.reg, s.logger)

	go recv.Run(ctx)
	go send.Run(ctx)

	if s.cfg.ClientTTL > 0 && s.cfg.CleanupInterval > 0 {
		go s.pruneLoop(ctx)
	}

	<-ctx.Done()
	return nil
}

func (s *Server) pruneLoop(ctx context.Context) {
	ticker := time.NewTicker(s.cfg.CleanupInterval)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			before := time.Now().Add(-s.cfg.ClientTTL)
			removed := s.reg.Prune(before)
			if removed > 0 && s.logger != nil {
				s.logger.Printf("pruned %d inactive clients", removed)
			}
		}
	}
}
