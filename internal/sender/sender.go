package sender

import (
	"context"
	"log"
	"net"

	"github.com/cdxy1/caller-min/internal/registry"
	"github.com/cdxy1/caller-min/internal/types"
)

type Sender struct {
	conn   *net.UDPConn
	rxCh   <-chan types.Packet
	reg    *registry.Registry
	logger *log.Logger
}

func NewSender(conn *net.UDPConn, rxCh <-chan types.Packet, reg *registry.Registry, logger *log.Logger) *Sender {
	return &Sender{conn: conn, rxCh: rxCh, reg: reg, logger: logger}
}

func (s *Sender) Run(ctx context.Context) {
	for {
		select {
		case <-ctx.Done():
			return
		case v, ok := <-s.rxCh:
			if !ok {
				return
			}
			from := ""
			if v.From != nil {
				from = v.From.String()
			}
			for _, addr := range s.reg.ReadAll() {
				if from != "" && addr.String() == from {
					continue
				}
				if _, err := s.conn.WriteToUDP(v.Data, addr); err != nil && s.logger != nil {
					s.logger.Printf("udp write error to %s: %v", addr.String(), err)
				}
			}
		}
	}
}
