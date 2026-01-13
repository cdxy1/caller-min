package registry

import (
	"net"
	"sync"
	"time"
)

type Registry struct {
	mu      sync.RWMutex
	clients map[string]*Client
}

type Client struct {
	Addr     *net.UDPAddr
	LastSeen time.Time
}

func NewRegistry() *Registry {
	clMap := make(map[string]*Client)
	return &Registry{clients: clMap}
}

func (r *Registry) Insert(addr *net.UDPAddr, seen time.Time) {
	if addr == nil || seen.IsZero() {
		return
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	r.clients[addr.String()] = &Client{Addr: addr, LastSeen: seen}
}

func (r *Registry) ReadAll() []*net.UDPAddr {
	r.mu.RLock()
	defer r.mu.RUnlock()
	addrArr := make([]*net.UDPAddr, 0, 10)
	for _, addr := range r.clients {
		addrArr = append(addrArr, addr.Addr)
	}
	return addrArr
}

func (r *Registry) Prune(before time.Time) int {
	if before.IsZero() {
		return 0
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	removed := 0
	for key, client := range r.clients {
		if client.LastSeen.Before(before) {
			delete(r.clients, key)
			removed++
		}
	}
	return removed
}
