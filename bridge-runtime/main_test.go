package main

import (
	"bufio"
	"encoding/binary"
	"net"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"
)

func TestRequestFrame(t *testing.T) {
	u := "ws://127.0.0.1:18443/uni.ws"
	b := request(u)
	if int(binary.LittleEndian.Uint32(b[:4])) != len(b)-4 {
		t.Fatal("bad frame length")
	}
	expected := []byte{0xe2, 0x0f, 0x34, 0x3a, 0x0f, 'A', 'l', 'i', 'c', 'e', 'C', 'a', 'p', 'a', 'b', 'i', 'l', 'i', 't', 'y', 0x42, 0x21, 0x08, 0x10, 0x18, 0x01, 0x5a, 0x1b}
	if len(b) < len(expected)+4 {
		t.Fatal("short frame")
	}
	for i, v := range expected {
		if b[4+i] != v {
			t.Fatalf("incorrect byte %d: got %x want %x", i, b[4+i], v)
		}
	}
}

func TestCheckHubRequiresAuthenticatedState(t *testing.T) {
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer l.Close()
	request := make(chan string, 1)
	go func() {
		c, e := l.Accept()
		if e != nil {
			return
		}
		defer c.Close()
		line, _ := bufio.NewReader(c).ReadString('\n')
		request <- line
		_, _ = c.Write([]byte("{\"known\":true,\"policy\":\"ask\"}\n"))
	}()
	if !checkHub(l.Addr().String(), "0123456789abcdef") {
		t.Fatal("valid state not accepted")
	}
	if got := <-request; got != "0123456789abcdef STATE\n" {
		t.Fatalf("request = %q", got)
	}
	if checkHub(l.Addr().String(), "bad") {
		t.Fatal("invalid key accepted")
	}
}

func TestDrainRequiresVerifiedZero(t *testing.T) {
	for _, tc := range []struct {
		body string
		want bool
	}{
		{`{"status":"ok","active_connections":0}`, true},
		{`{"status":"ok","active_connections":1}`, false},
		{`{"status":"ok"}`, false},
		{`{"status":"error","active_connections":0}`, false},
	} {
		s := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { _, _ = w.Write([]byte(tc.body)) }))
		if got := waitDrained(s.URL, 100*time.Millisecond); got != tc.want {
			t.Errorf("%s: got %v", tc.body, got)
		}
		s.Close()
	}
}
