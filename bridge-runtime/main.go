package main

import (
	"bufio"
	"encoding/binary"
	"encoding/json"
	"encoding/xml"
	"errors"
	"flag"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"os/signal"
	"strconv"
	"strings"
	"syscall"
	"time"
)

func varint(dst []byte, v uint64) []byte {
	for v >= 128 {
		dst = append(dst, byte(v)|0x80)
		v >>= 7
	}
	return append(dst, byte(v))
}
func bytesField(dst []byte, field uint64, value []byte) []byte {
	dst = varint(dst, field<<3|2)
	dst = varint(dst, uint64(len(value)))
	return append(dst, value...)
}
func request(url string) []byte {
	method := []byte{8, 16, 24, 1}
	method = bytesField(method, 11, []byte(url))
	remote := bytesField(nil, 7, []byte("AliceCapability"))
	remote = bytesField(remote, 8, method)
	envelope := bytesField(nil, 252, remote)
	envelope = varint(envelope, 253<<3)
	envelope = varint(envelope, 210)
	payload := make([]byte, 4, len(envelope)+4)
	binary.LittleEndian.PutUint32(payload, uint32(len(envelope)))
	return append(payload, envelope...)
}
func apply(url string) error {
	if url == "" {
		return errors.New("empty endpoint")
	}
	c, e := net.DialTimeout("tcp", "127.0.0.1:9875", 2*time.Second)
	if e != nil {
		return e
	}
	defer c.Close()
	c.SetDeadline(time.Now().Add(3 * time.Second))
	_, e = c.Write(request(url))
	return e
}
func hubKey() string {
	data, err := os.ReadFile("/data/data/ru.stationhub.app/shared_prefs/station_hub.xml")
	if err != nil {
		return ""
	}
	var prefs struct {
		Items []struct {
			Name  string `xml:"name,attr"`
			Value string `xml:",chardata"`
		} `xml:"string"`
	}
	if xml.Unmarshal(data, &prefs) != nil {
		return ""
	}
	for _, item := range prefs.Items {
		if item.Name == "pairing_key" && len(item.Value) == 16 {
			return item.Value
		}
	}
	return ""
}
func notifyHub(command string) {
	key := hubKey()
	if len(key) != 16 || strings.ContainsAny(key, " \r\n") {
		return
	}
	c, err := net.DialTimeout("tcp", "127.0.0.1:27362", time.Second)
	if err != nil {
		return
	}
	defer c.Close()
	_ = c.SetDeadline(time.Now().Add(time.Second))
	_, _ = c.Write([]byte(key + " " + command + "\n"))
}
func checkHub(address, key string) bool {
	if len(key) != 16 || strings.ContainsAny(key, " \r\n") {
		return false
	}
	c, err := net.DialTimeout("tcp", address, time.Second)
	if err != nil {
		return false
	}
	defer c.Close()
	_ = c.SetDeadline(time.Now().Add(time.Second))
	if _, err = c.Write([]byte(key + " STATE\n")); err != nil {
		return false
	}
	line, err := bufio.NewReaderSize(c, 1024).ReadString('\n')
	if err != nil || len(line) > 512 {
		return false
	}
	var status struct {
		Known  *bool  `json:"known"`
		Policy string `json:"policy"`
	}
	return json.Unmarshal([]byte(line), &status) == nil && status.Known != nil && status.Policy != ""
}
func waitDrained(endpoint string, timeout time.Duration) bool {
	client := &http.Client{Timeout: time.Second}
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		resp, err := client.Get(endpoint)
		if err == nil {
			var state struct {
				Status string `json:"status"`
				Active *int   `json:"active_connections"`
			}
			err = json.NewDecoder(io.LimitReader(resp.Body, 4096)).Decode(&state)
			resp.Body.Close()
			if err == nil && resp.StatusCode == 200 && state.Status == "ok" && state.Active != nil && *state.Active == 0 {
				return true
			}
		}
		time.Sleep(500 * time.Millisecond)
	}
	return false
}
func main() {
	mode := flag.String("mode", "", "pilot or restore")
	pid := flag.Int("bridge-pid", 0, "bridge process")
	timeout := flag.Duration("timeout", 180*time.Second, "unconfirmed pilot limit")
	cfgPath := flag.String("config", "/system/vendor/quasar/quasar.cfg", "firmware configuration")
	flag.Parse()
	if *mode == "check-hub" {
		if checkHub("127.0.0.1:27362", hubKey()) {
			return
		}
		os.Exit(1)
	}
	if *mode == "wait-drain" {
		if waitDrained("http://127.0.0.1:18443/healthz", *timeout) {
			fmt.Println("connection drained")
			return
		}
		fmt.Fprintln(os.Stderr, "connection not drained")
		os.Exit(1)
	}
	raw, e := os.ReadFile(*cfgPath)
	if e != nil {
		fmt.Fprintln(os.Stderr, "cannot read config")
		os.Exit(1)
	}
	var cfg struct {
		Alice struct {
			URL string `json:"uniProxyUrl"`
		} `json:"aliced"`
	}
	if json.Unmarshal(raw, &cfg) != nil || cfg.Alice.URL == "" {
		fmt.Fprintln(os.Stderr, "no original endpoint")
		os.Exit(1)
	}
	if *mode == "restore" {
		if apply(cfg.Alice.URL) != nil {
			fmt.Fprintln(os.Stderr, "restore command failed")
			os.Exit(1)
		}
		fmt.Println("restore command sent")
		return
	}
	if *mode != "pilot" || *pid <= 0 {
		fmt.Fprintln(os.Stderr, "invalid mode or pid")
		os.Exit(1)
	}
	proc, e := os.FindProcess(*pid)
	if e != nil || proc.Signal(syscall.Signal(0)) != nil {
		fmt.Fprintln(os.Stderr, "bridge not running")
		os.Exit(1)
	}
	if e = apply("ws://127.0.0.1:18443/uni.ws"); e != nil {
		fmt.Fprintln(os.Stderr, "could not set pilot endpoint")
		os.Exit(1)
	}
	fmt.Println("pilot endpoint command sent; bridge pid", strconv.Itoa(*pid))
	notifyHub("PING")
	stop := make(chan os.Signal, 1)
	signal.Notify(stop, syscall.SIGTERM, syscall.SIGINT)
	ticker := time.NewTicker(3 * time.Second)
	defer ticker.Stop()
	var deadline <-chan time.Time
	if *timeout > 0 {
		timer := time.NewTimer(*timeout)
		defer timer.Stop()
		deadline = timer.C
	}
	for {
		select {
		case <-stop:
			goto restore
		case <-deadline:
			goto restore
		case <-ticker.C:
			if proc.Signal(syscall.Signal(0)) != nil {
				goto restore
			}
			notifyHub("PING")
		}
	}
restore:
	notifyHub("INACTIVE")
	for i := 0; i < 5; i++ {
		if apply(cfg.Alice.URL) == nil {
			fmt.Println("restore command sent")
			return
		}
		time.Sleep(time.Second)
	}
	fmt.Fprintln(os.Stderr, "restore command failed")
	os.Exit(1)
}
