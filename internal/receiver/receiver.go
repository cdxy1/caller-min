package receiver

import (
	"context"
	"errors"
	"log"
	"net"
	"time"

	"github.com/cdxy1/caller-min/internal/registry"
	"github.com/cdxy1/caller-min/internal/types"
)

type Config struct {
	MaxPacketSize int
	ReadTimeout   time.Duration
}

type Receiver struct {
	conn   *net.UDPConn
	rxCh   chan<- types.Packet
	reg    *registry.Registry
	logger *log.Logger
	cfg    Config
}

func NewReceiver(conn *net.UDPConn, rxCh chan<- types.Packet, reg *registry.Registry, logger *log.Logger, cfg Config) *Receiver {
	return &Receiver{conn: conn, rxCh: rxCh, reg: reg, logger: logger, cfg: cfg}
}

func (recv *Receiver) Run(ctx context.Context) {
	buf := make([]byte, recv.cfg.MaxPacketSize)
	for {
		if recv.cfg.ReadTimeout > 0 {
			_ = recv.conn.SetReadDeadline(time.Now().Add(recv.cfg.ReadTimeout))
		}
		n, remoteAddr, err := recv.conn.ReadFromUDP(buf)
		if err != nil {
			if ctx.Err() != nil {
				close(recv.rxCh)
				return
			}
			if nerr, ok := err.(net.Error); ok && nerr.Timeout() {
				continue
			}
			if recv.logger != nil && !errors.Is(err, net.ErrClosed) {
				recv.logger.Printf("udp read error: %v", err)
			}
			continue
		}

		data := make([]byte, n)
		copy(data, buf[:n])
		pkt := types.Packet{
			From:   remoteAddr,
			Data:   data,
			RecvAt: time.Now(),
		}

		recv.reg.Insert(remoteAddr, pkt.RecvAt)

		select {
		case recv.rxCh <- pkt:
		default:
		}
	}
}
