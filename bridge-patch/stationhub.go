package bridge

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"net"
	"strings"
	"sync"
	"time"
)

type HubState struct {
	Known   bool   `json:"known"`
	Station bool   `json:"station"`
	PC      bool   `json:"pc"`
	Next    bool   `json:"next"`
	Policy  string `json:"policy"`
}
type HubClient interface {
	State(context.Context) (HubState, error)
	Action(context.Context, string) (string, error)
}
type StationHubResolver struct {
	Hub     HubClient
	mu      sync.Mutex
	pending string
	until   time.Time
}

func yes() *bool { b := true; return &b }
func no() *bool  { b := false; return &b }
func normalized(s string) string {
	return strings.Join(strings.Fields(strings.ToLower(strings.Trim(s, " \t\r\n.,!?"))), " ")
}
func (r *StationHubResolver) Resolve(ctx context.Context, text string) (Resolution, error) {
	s := normalized(text)
	r.mu.Lock()
	action := r.pending
	if time.Now().After(r.until) {
		action = ""
		r.pending = ""
	}
	r.mu.Unlock()
	if action != "" {
		target := ""
		switch s {
		case "компьютер", "на компьютере", "пк", "на пк":
			target = "PC"
		case "станция", "на станции", "алиса", "на алисе":
			target = "STATION"
		}
		if target != "" {
			r.mu.Lock()
			if r.pending != action {
				r.mu.Unlock()
				return Resolution{Route: "yandex"}, nil
			}
			r.pending = ""
			r.mu.Unlock()
			return r.execute(ctx, action, target)
		}
		r.mu.Lock()
		r.pending = ""
		r.mu.Unlock()
	}
	action = ""
	switch s {
	case "дальше", "следующий трек", "следующую песню":
		action = "NEXT"
	case "лайк", "поставь лайк":
		action = "LIKE"
	}
	if action == "" || r.Hub == nil {
		return Resolution{Route: "yandex"}, nil
	}
	state, err := r.Hub.State(ctx)
	if err != nil || !state.Known || state.Policy == "station" {
		return Resolution{Route: "yandex"}, nil
	}
	pc := state.PC && (action != "NEXT" || state.Next)
	if !pc {
		return Resolution{Route: "yandex"}, nil
	}
	if !state.Station || state.Policy == "pc" {
		return r.execute(ctx, action, "PC")
	}
	if state.Policy == "alice" {
		return Resolution{Route: "yandex"}, nil
	}
	if state.Policy != "ask" {
		return Resolution{Route: "yandex"}, nil
	}
	r.mu.Lock()
	r.pending = action
	r.until = time.Now().Add(25 * time.Second)
	r.mu.Unlock()
	question := "Что переключить: на компьютере или на Станции?"
	if action == "LIKE" {
		question = "Где поставить лайк: на компьютере или на Станции?"
	}
	return Resolution{Route: "stationhub", Answer: question, Listen: yes()}, nil
}
func (r *StationHubResolver) execute(ctx context.Context, action, target string) (Resolution, error) {
	if r.Hub == nil {
		return Resolution{Route: "stationhub", Answer: "Не удалось связаться со Station Hub.", Listen: no()}, nil
	}
	result, err := r.Hub.Action(ctx, action+"_"+target)
	answer := "Не удалось выполнить команду."
	if err == nil {
		switch result {
		case "OK":
			if action == "NEXT" {
				if target == "PC" {
					answer = "Переключила на компьютере."
				} else {
					answer = "Переключила на Станции."
				}
			} else {
				if target == "PC" {
					answer = "Поставила лайк на компьютере."
				} else {
					answer = "Поставила лайк на Станции."
				}
			}
		case "ALREADY":
			answer = "Этот трек уже отмечен на компьютере."
		case "NO_PLAYER":
			answer = "Открой вкладку Яндекс Музыки с играющим треком."
		case "QUEUED":
			if target == "PC" {
				answer = "Отправила команду компьютеру."
			} else {
				answer = "Отправила команду Станции."
			}
		}
	}
	return Resolution{Route: "stationhub", Answer: answer, Listen: no()}, nil
}

type LocalHub struct{ Address, Key string }

func (h LocalHub) call(ctx context.Context, cmd string) (string, error) {
	if len(h.Key) != 16 {
		return "", errors.New("missing hub key")
	}
	c, e := (&net.Dialer{Timeout: time.Second}).DialContext(ctx, "tcp", h.Address)
	if e != nil {
		return "", e
	}
	defer c.Close()
	_ = c.SetDeadline(time.Now().Add(5 * time.Second))
	if _, e = c.Write([]byte(h.Key + " " + cmd + "\n")); e != nil {
		return "", e
	}
	line, e := bufio.NewReaderSize(c, 1024).ReadString('\n')
	if e != nil {
		return "", e
	}
	if len(line) > 512 {
		return "", errors.New("oversized response")
	}
	return strings.TrimSpace(line), nil
}
func (h LocalHub) State(ctx context.Context) (HubState, error) {
	var s HubState
	x, e := h.call(ctx, "STATE")
	if e == nil {
		e = json.Unmarshal([]byte(x), &s)
	}
	return s, e
}
func (h LocalHub) Action(ctx context.Context, cmd string) (string, error) { return h.call(ctx, cmd) }
