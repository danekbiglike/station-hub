// Station Hub's account-scoped rating adapter. It binds only to loopback,
// validates the existing pairing key and never exports credentials or account IDs.
package main

import (
	"bufio"
	"context"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/json"
	"encoding/xml"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/url"
	"os"
	"strings"
	"sync"
	"time"
)

type field struct {
	id, wire int
	data     []byte
	n        uint64
}

func vi(b []byte, i *int) (uint64, bool) {
	var n uint64
	for shift := uint(0); shift < 64 && *i < len(b); shift += 7 {
		x := b[*i]
		*i++
		n |= uint64(x&127) << shift
		if x < 128 {
			return n, true
		}
	}
	return 0, false
}
func parse(b []byte) ([]field, bool) {
	var out []field
	for i := 0; i < len(b); {
		k, ok := vi(b, &i)
		if !ok || k == 0 {
			return nil, false
		}
		f := field{id: int(k >> 3), wire: int(k & 7)}
		switch f.wire {
		case 0:
			f.n, ok = vi(b, &i)
			if !ok {
				return nil, false
			}
		case 2:
			n, valid := vi(b, &i)
			if !valid || n > uint64(len(b)-i) {
				return nil, false
			}
			f.data = b[i : i+int(n)]
			i += int(n)
		default:
			return nil, false
		}
		out = append(out, f)
	}
	return out, true
}

type account struct {
	uid   uint64
	token string
}

func activeAccount() (account, error) {
	b, e := os.ReadFile("/data/quasar/account_storage.dat")
	if e != nil {
		return account{}, e
	}
	outer, ok := parse(b)
	if !ok {
		return account{}, errors.New("account format")
	}
	var active, current uint64
	for _, f := range outer {
		if f.wire == 0 && f.id == 3 {
			active = f.n
		}
		if f.wire == 0 && f.id == 2 {
			current = f.n
		}
	}
	if active == 0 {
		active = current
	}
	for _, f := range outer {
		if f.id != 1 || f.wire != 2 {
			continue
		}
		sub, ok := parse(f.data)
		if !ok {
			continue
		}
		var a account
		for _, v := range sub {
			if v.id == 1 && v.wire == 0 {
				a.uid = v.n
			}
			if v.id == 3 && v.wire == 2 {
				a.token = string(v.data)
			}
		}
		if a.uid != 0 && a.uid == active && len(a.token) >= 10 && len(a.token) <= 1024 {
			return a, nil
		}
	}
	return account{}, errors.New("no active account")
}
func pairingKey() string {
	b, e := os.ReadFile("/data/data/ru.stationhub.app/shared_prefs/station_hub.xml")
	if e != nil {
		return ""
	}
	var p struct {
		Items []struct {
			Name  string `xml:"name,attr"`
			Value string `xml:",chardata"`
		} `xml:"string"`
	}
	if xml.Unmarshal(b, &p) != nil {
		return ""
	}
	for _, x := range p.Items {
		if x.Name == "pairing_key" && len(x.Value) == 16 {
			return x.Value
		}
	}
	return ""
}

type item struct {
	ID json.Number `json:"id"`
}
type trackResponse struct {
	Result struct {
		Library struct {
			Tracks []item `json:"tracks"`
		} `json:"library"`
		Tracks []item `json:"tracks"`
	} `json:"result"`
}

func tracks(b []byte) (map[string]bool, error) {
	var r trackResponse
	if json.Unmarshal(b, &r) != nil {
		return nil, errors.New("response")
	}
	t := r.Result.Library.Tracks
	if t == nil {
		t = r.Result.Tracks
	}
	if t == nil {
		return nil, errors.New("missing tracks")
	}
	out := make(map[string]bool, len(t))
	for _, x := range t {
		if validID(x.ID.String()) {
			out[x.ID.String()] = true
		}
	}
	return out, nil
}
func validID(s string) bool {
	if len(s) == 0 || len(s) > 18 {
		return false
	}
	for _, c := range s {
		if c < '0' || c > '9' {
			return false
		}
	}
	return true
}

type engine struct {
	mu              sync.Mutex
	client          *http.Client
	accountHash     [32]byte
	uid             uint64
	loaded          time.Time
	likes, dislikes map[string]bool
}

func newEngine(dns string) *engine {
	r := &net.Resolver{PreferGo: true, Dial: func(ctx context.Context, network, address string) (net.Conn, error) {
		return (&net.Dialer{Timeout: 2 * time.Second}).DialContext(ctx, "udp", net.JoinHostPort(dns, "53"))
	}}
	d := &net.Dialer{Timeout: 4 * time.Second, Resolver: r}
	return &engine{client: &http.Client{Timeout: 8 * time.Second, Transport: &http.Transport{DialContext: d.DialContext, MaxIdleConns: 2, IdleConnTimeout: 30 * time.Second}, CheckRedirect: func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse }}}
}
func (e *engine) call(a account, method, path string, values url.Values) ([]byte, error) {
	var body io.Reader
	if values != nil {
		body = strings.NewReader(values.Encode())
	}
	req, err := http.NewRequest(method, "https://api.music.yandex.net"+path, body)
	if err != nil {
		return nil, err
	}
	req.Header.Set("Authorization", "OAuth "+a.token)
	if values != nil {
		req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	}
	resp, err := e.client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != 200 {
		return nil, errors.New("music service unavailable")
	}
	b, err := io.ReadAll(io.LimitReader(resp.Body, (8<<20)+1))
	if err != nil || len(b) > 8<<20 {
		return nil, errors.New("invalid response")
	}
	return b, nil
}
func (e *engine) ensure(a account, force bool) error {
	h := sha256.Sum256([]byte(a.token))
	if e.uid != a.uid || h != e.accountHash {
		e.loaded = time.Time{}
		e.likes = nil
		e.dislikes = nil
		e.uid = 0
		b, err := e.call(a, "GET", "/account/status", nil)
		if err != nil {
			return err
		}
		var r struct {
			Result struct {
				Account struct {
					UID uint64 `json:"uid"`
				} `json:"account"`
			} `json:"result"`
		}
		if json.Unmarshal(b, &r) != nil || r.Result.Account.UID != a.uid {
			return errors.New("account mismatch")
		}
		e.uid = a.uid
		e.accountHash = h
	}
	if !force && !e.loaded.IsZero() && time.Since(e.loaded) < 20*time.Second {
		return nil
	}
	b, err := e.call(a, "GET", fmt.Sprintf("/users/%d/likes/tracks", a.uid), nil)
	if err != nil {
		return err
	}
	likes, err := tracks(b)
	if err != nil {
		return err
	}
	b, err = e.call(a, "GET", fmt.Sprintf("/users/%d/dislikes/tracks", a.uid), nil)
	if err != nil {
		return err
	}
	dislikes, err := tracks(b)
	if err != nil {
		return err
	}
	e.likes = likes
	e.dislikes = dislikes
	e.loaded = time.Now()
	return nil
}
func (e *engine) execute(command, id string) string {
	if !validID(id) {
		return "UNAVAILABLE"
	}
	e.mu.Lock()
	defer e.mu.Unlock()
	a, err := activeAccount()
	if err != nil {
		return "UNAVAILABLE"
	}
	if err = e.ensure(a, command == "REFRESH"); err != nil {
		return "UNAVAILABLE"
	}
	if command == "STATE" || command == "REFRESH" {
		if e.likes[id] {
			return "LIKE"
		}
		if e.dislikes[id] {
			return "DISLIKE"
		}
		return "NONE"
	}
	kind := ""
	operation := "remove"
	if command == "REMOVE_LIKE" {
		kind = "likes"
	} else if command == "REMOVE_DISLIKE" {
		kind = "dislikes"
	} else if command == "SET_LIKE" {
		kind = "likes"
		operation = "add-multiple"
	} else if command == "SET_DISLIKE" {
		kind = "dislikes"
		operation = "add-multiple"
	} else {
		return "UNAVAILABLE"
	}
	current := kind == "likes" && e.likes[id] || kind == "dislikes" && e.dislikes[id]
	if operation == "remove" && !current || operation == "add-multiple" && current {
		return "OK"
	}
	_, err = e.call(a, "POST", fmt.Sprintf("/users/%d/%s/tracks/%s", a.uid, kind, operation), url.Values{"track-ids": {id}})
	if err != nil {
		return "UNAVAILABLE"
	}
	e.loaded = time.Time{}
	if err = e.ensure(a, true); err != nil {
		return "UNAVAILABLE"
	}
	current = kind == "likes" && e.likes[id] || kind == "dislikes" && e.dislikes[id]
	if operation == "remove" && current || operation == "add-multiple" && !current {
		return "UNAVAILABLE"
	}
	return "OK"
}
func main() {
	dns := os.Getenv("STATION_HUB_DNS")
	if net.ParseIP(dns) == nil {
		return
	}
	srv := newEngine(dns)
	listener, err := net.Listen("tcp", "127.0.0.1:27364")
	if err != nil {
		return
	}
	defer listener.Close()
	sem := make(chan struct{}, 4)
	for {
		c, err := listener.Accept()
		if err != nil {
			return
		}
		select {
		case sem <- struct{}{}:
			go func() {
				defer func() { <-sem }()
				defer c.Close()
				_ = c.SetDeadline(time.Now().Add(20 * time.Second))
				reader := bufio.NewReaderSize(c, 160)
				line, err := reader.ReadString('\n')
				if err != nil || len(line) > 128 {
					return
				}
				parts := strings.Fields(line)
				key := pairingKey()
				if len(parts) != 3 || len(key) != 16 || subtle.ConstantTimeCompare([]byte(parts[0]), []byte(key)) != 1 {
					return
				}
				result := srv.execute(parts[1], parts[2])
				_, _ = io.WriteString(c, result+"\n")
			}()
		default:
			_ = c.Close()
		}
	}
}
