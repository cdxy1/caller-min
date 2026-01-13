package types

import (
	"net"
	"time"
)

type Packet struct {
	From   *net.UDPAddr
	Data   []byte
	RecvAt time.Time
}
