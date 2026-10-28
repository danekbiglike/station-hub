package bridge

import (
	"context"
	"errors"
	"testing"
)

type fakeHub struct {
	state HubState
	calls []string
	fail  bool
}

func (f *fakeHub) State(context.Context) (HubState, error) {
	if f.fail {
		return HubState{}, errors.New("offline")
	}
	return f.state, nil
}
func (f *fakeHub) Action(_ context.Context, s string) (string, error) {
	f.calls = append(f.calls, s)
	return "OK", nil
}
func TestStationHubChoiceAndSafePolicy(t *testing.T) {
	ctx := context.Background()
	f := &fakeHub{state: HubState{Known: true, Station: true, PC: true, Next: true, Policy: "ask"}}
	r := &StationHubResolver{Hub: f}
	q, _ := r.Resolve(ctx, "Дальше")
	if q.Route != "stationhub" || q.Listen == nil || !*q.Listen || len(f.calls) != 0 {
		t.Fatalf("question: %+v %v", q, f.calls)
	}
	a, _ := r.Resolve(ctx, "Станция")
	if len(f.calls) != 1 || f.calls[0] != "NEXT_STATION" || a.Listen == nil || *a.Listen {
		t.Fatalf("answer: %+v %v", a, f.calls)
	}
	q, _ = r.Resolve(ctx, "Поставь лайк")
	if q.Listen == nil || !*q.Listen {
		t.Fatalf("like question: %+v", q)
	}
	_, _ = r.Resolve(ctx, "компьютер")
	if len(f.calls) != 2 || f.calls[1] != "LIKE_PC" {
		t.Fatalf("like choice: %v", f.calls)
	}
	f.state.Policy = "station"
	v, _ := r.Resolve(ctx, "дальше")
	if v.Route != "yandex" || len(f.calls) != 2 {
		t.Fatalf("safe: %+v %v", v, f.calls)
	}
	f.state.Policy = "pc"
	_, _ = r.Resolve(ctx, "дальше")
	if f.calls[2] != "NEXT_PC" {
		t.Fatalf("pc: %v", f.calls)
	}
	f.fail = true
	v, _ = r.Resolve(ctx, "дальше")
	if v.Route != "yandex" {
		t.Fatalf("offline: %+v", v)
	}
}
